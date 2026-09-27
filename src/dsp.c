#include "dsp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FM_DECIMATED_RATE 250000.0f // Intermediate sample rate after decimation
#define FM_CHANNEL_CUTOFF 100000.0f // 100 kHz (±100 kHz for 200 kHz FM channel)

struct dsp_pipeline {
    dsp_config_t config;

    /* Previous complex sample for polar discriminator (FM demod) */
    float prev_i;
    float prev_q;

    /* Phase accumulator for frequency translation mixer */
    float phase;
    float phase_step;

    /* Decimation filter step and accumulators */
    int decimation; // 2, 3, 4, 8, 16, 32...
    int decimated_counter;
    float decimated_sum_i;
    float decimated_sum_q;

    /* De-emphasis filter state and coefficient */
    float deemphasis_state;
    float deemphasis_alpha;

    /* Audio resampling */
    float audio_resample_step; 
    float audio_resample_acc;
    float audio_previous_sample;
};

dsp_pipeline_t* dsp_create(const dsp_config_t *cfg) {
    if (!cfg) return NULL;

    dsp_pipeline_t *dsp = (dsp_pipeline_t*)calloc(1, sizeof(dsp_pipeline_t));
    if (!dsp) return NULL;

    dsp->config = *cfg;
    dsp->prev_i = 0.0f;
    dsp->prev_q = 0.0f;
    dsp->phase = 0.0f;

    // Mixer phase step to off-tune the HackRF center skipe: 2 * PI * f_offset / f_in
    dsp->phase_step = 2.0f * (float)M_PI * (float)cfg->freq_offset_hz / (float)cfg->input_sample_rate;

    // Calculate a decimation step and reset accumulators
    // This is a low pass filter actually
    dsp->decimation = (int) cfg->input_sample_rate / FM_DECIMATED_RATE;
    dsp->decimated_counter = 0;
    dsp->decimated_sum_i = 0.0;
    dsp->decimated_sum_q = 0.0;

    /*
     * De-emphasis single-pole IIR filter coefficient:
     * time constant tau = deemphasis_us * 1e-6 s (e.g., 50 us -> 0.000050 s)
     * alpha = 1.0 - exp(-1.0 / (sample_rate * tau))
     */
    if (cfg->deemphasis_us > 0.0f) {
        float tau = cfg->deemphasis_us * 1e-6f;
        dsp->deemphasis_alpha = 1.0f - expf(-1.0f / ((float)cfg->output_sample_rate * tau));
    } else {
        dsp->deemphasis_alpha = 1.0f; /* Passthrough */
    }

    dsp->audio_resample_step = (float)cfg->output_sample_rate / FM_DECIMATED_RATE;
    dsp->audio_resample_acc = 0.0;
    dsp->audio_previous_sample = 0.0;

    return dsp;
}

void dsp_destroy(dsp_pipeline_t *dsp) {
    if (!dsp) return;
    free(dsp);
}

size_t dsp_process_iq_to_pcm(dsp_pipeline_t *dsp,
                             const int8_t *raw_iq,
                             size_t raw_iq_len,
                             int16_t *out_pcm,
                             size_t max_pcm_len) {
    if (!dsp || !raw_iq || !out_pcm || raw_iq_len == 0 || max_pcm_len == 0) {
        return 0;
    }

    size_t num_iq_samples = raw_iq_len / 2; // interleaved I, Q pairs
    size_t pcm_samples_written = 0;

    for (size_t n = 0; n < num_iq_samples; n++) {
        // convert each I/Q pair to floats and normalize
        float i = raw_iq[n * 2] / 128.0f;
        float q = raw_iq[n * 2 + 1] / 128.0f;

        // Shift the frequency by doing mixing: (i + j*q) * (cos(phase) + j*sin(phase))
        float cos_val = cosf(dsp->phase);
        float sin_val = sinf(dsp->phase);
        float mixed_i = i * cos_val - q * sin_val;
        float mixed_q = i * sin_val + q * cos_val;

        dsp->phase += dsp->phase_step;
        // keep phase in the range [-PI, +PI], to prevent a buffer overflow with accumulation
        // Use  [-PI, +PI], not [0, +2PI] to keep compatibility with tan2f(y, x)
        if (dsp->phase > (float)M_PI) {
            dsp->phase -= 2.0f * (float)M_PI;
        } else if (dsp->phase < -(float)M_PI) {
            dsp->phase += 2.0f * (float)M_PI;
        }

        // Accumulate for further decimation
        dsp->decimated_sum_i += mixed_i;
        dsp->decimated_sum_q += mixed_q;

        // it's time to decimate
        if (++dsp->decimated_counter >= dsp->decimation) {
            float decimated_i = dsp->decimated_sum_i / dsp->decimated_counter;
            float decimated_q = dsp->decimated_sum_q / dsp->decimated_counter;

            dsp->decimated_counter = 0;
            dsp->decimated_sum_i = 0.0;
            dsp->decimated_sum_q = 0.0;

            // Core of FM demodulation
            // Calculating Δθ = atan2 (Q[n], I[n]) - atan2 (Q[n-1], I[n-1])
            // by using complex conjugate that uses only one atan2 call: 
            // Δθ = atan2 (Q[n] * I[n-1] - I[n] * Q[n-1], I[n] * I[n-1] + Q[n] * Q[n-1]) 
            float angle = atan2f(decimated_q * dsp->prev_i - decimated_i * dsp->prev_q, decimated_i * dsp->prev_i + decimated_q * dsp->prev_q);

            dsp->prev_i = decimated_i;
            dsp->prev_q = decimated_q;

            // Apply single-pole IIR de-emphasis filter.
            // It is still possbile to completely bypass it with
            //   float audio_sample = angle;
            dsp->deemphasis_state += dsp->deemphasis_alpha * (angle - dsp->deemphasis_state);
            float audio_sample = dsp->deemphasis_state;

            float audio_scaled = audio_sample * (float)INT16_MAX;
            if (audio_scaled > (float)INT16_MAX) {
                audio_scaled = (float)INT16_MAX;
            } else if (audio_scaled < (float)INT16_MIN) {
                audio_scaled = (float)INT16_MIN;
            }

            // 250 KHz -> 48 KHz linear downsampling 
            dsp->audio_resample_acc += dsp->audio_resample_step;
            if (dsp->audio_resample_acc >= 1.0) {
                dsp->audio_resample_acc -= 1.0f;

                // Fraction between previous sample and current sample
                float mu = dsp->audio_resample_acc / dsp->audio_resample_step;
                float linered_sample = (1 - mu) * dsp->audio_previous_sample + mu * audio_scaled;

                if (pcm_samples_written < max_pcm_len) {
                    out_pcm[pcm_samples_written++] = (int16_t)linered_sample;
                }
            }
            dsp->audio_previous_sample = audio_scaled;
        }
    }

    return pcm_samples_written;
}