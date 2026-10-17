#include "dsp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FM_DECIMATED_RATE 250000.0f // Intermediate sample rate after decimation
#define FM_CHANNEL_CUTOFF 100000.0f // 100 kHz (±100 kHz for 200 kHz FM channel)
#define MAX_INBOUND_IQ_VALUE 128.0f

struct dsp_pipeline {
    dsp_config_t config;

    /* -- Software mixer (off-tune) configuration */

    float mixer_phase; // Phase accumulator for frequency translation mixer
    float mixer_phase_step;

    /* -- I/Q Decimation filter step and accumulators */

    int decimation; // 2, 3, 4, 8, 16, 32...
    complex_sample_t *decimation_buffer;
    size_t decimation_capacity; // max number of complex_sample_t elements in buffer
    int decimated_counter;
    float decimated_sum_i;
    float decimated_sum_q;

    /* -- Demodulation to FM Mupliplex basenband (MPX) */

    float *mpx_buffer;
    size_t mpx_buffer_capacity;

    /* Previous complex sample for polar discriminator */
    float prev_i;
    float prev_q;
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

    /* -- Software mixer (off-tune) */

    // Mixer phase step to off-tune the HackRF center skipe: 2 * PI * f_offset / f_in
    dsp->mixer_phase_step = 2.0f * (float)M_PI * (float)cfg->freq_offset_hz / (float)cfg->input_sample_rate;
    dsp->mixer_phase = 0.0f;

    /* -- Init I/Q decimation pipeline */

    // Calculate a decimation step and reset accumulators
    // This is a low pass filter actually
    dsp->decimation = (int) cfg->input_sample_rate / FM_DECIMATED_RATE;
    dsp->decimated_counter = 0;
    dsp->decimated_sum_i = 0.0;
    dsp->decimated_sum_q = 0.0;

    // Initialize an output buffer used for storing decimated values
    // Sized dynamically based on the caller's configured max chunk size (fallback to 64 KB if 0)
    size_t max_chunk_bytes = (cfg->max_input_chunk_bytes > 0) ? cfg->max_input_chunk_bytes : (64 * 1024);
    size_t max_raw_iq_samples = max_chunk_bytes / 2;
    int safe_decimation = (dsp->decimation > 0) ? dsp->decimation : 1;
    // +1 sample accounts for worst-case accumulator carryover from the previous chunk
    dsp->decimation_capacity = (max_raw_iq_samples / safe_decimation) + 1;
    dsp->decimation_buffer = (complex_sample_t *)malloc(dsp->decimation_capacity * sizeof(complex_sample_t));
    if (!dsp->decimation_buffer) {
        dsp_destroy(dsp);
        return NULL;
    }

    /* -- Init FM demodulation pipeline */

    dsp->mpx_buffer_capacity = dsp->decimation_capacity; // keep the same sample rate as for decimation
    dsp->mpx_buffer = (float *)malloc(dsp->mpx_buffer_capacity * sizeof(float));
    if (!dsp->mpx_buffer) {
        dsp_destroy(dsp);
        return NULL;
    }

    dsp->prev_i = 0.0f;
    dsp->prev_q = 0.0f;

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
    if (dsp->decimation_buffer) {
        free(dsp->decimation_buffer);
    }
    if (dsp->mpx_buffer) {
        free(dsp->mpx_buffer);
    }
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

    size_t num_complex_samples = rf_downconvert_and_decimate(
        dsp, raw_iq, raw_iq_len, dsp->decimation_buffer, dsp->decimation_capacity
    );

    size_t pcm_samples_written = 0;
    for (size_t n = 0; n < num_complex_samples; n++) {
        float decimated_i = dsp->decimation_buffer[n].i;
        float decimated_q = dsp->decimation_buffer[n].q;

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

        if (audio_scaled > (float)INT16_MAX)
        {
            audio_scaled = (float)INT16_MAX;
        }
        else if (audio_scaled < (float)INT16_MIN)
        {
            audio_scaled = (float)INT16_MIN;
        }

        // 250 KHz -> 48 KHz linear downsampling
        dsp->audio_resample_acc += dsp->audio_resample_step;
        if (dsp->audio_resample_acc >= 1.0)
        {
            dsp->audio_resample_acc -= 1.0f;

            // Fraction between previous sample and current sample
            float mu = dsp->audio_resample_acc / dsp->audio_resample_step;
            float linered_sample = (1 - mu) * dsp->audio_previous_sample + mu * audio_scaled;

            if (pcm_samples_written < max_pcm_len)
            {
                out_pcm[pcm_samples_written++] = (int16_t)linered_sample;
            }
        }
        dsp->audio_previous_sample = audio_scaled;
    }

    return pcm_samples_written;
}

/**
 * Get raw data from HackRF, downconvert it and decimate
 */
size_t rf_downconvert_and_decimate(dsp_pipeline_t *dsp,
                             const int8_t *raw_iq,
                             size_t raw_iq_bytes,
                             complex_sample_t *out_complex_samples,
                             size_t max_out_samples) {

    if (!dsp || !raw_iq || !out_complex_samples || raw_iq_bytes == 0 || max_out_samples == 0) {
        return 0;
    }

    size_t num_complex_samples = raw_iq_bytes / 2; // interleaved I, Q pairs
    size_t complex_samples_written = 0;

    for (size_t n = 0; n < num_complex_samples; n++) {
        // convert each I/Q pair to floats and normalize
        float i = raw_iq[n * 2] / MAX_INBOUND_IQ_VALUE;
        float q = raw_iq[n * 2 + 1] / MAX_INBOUND_IQ_VALUE;

        // Shift the frequency by doing mixing: (i + j*q) * (cos(phase) + j*sin(phase))
        float cos_val = cosf(dsp->mixer_phase);
        float sin_val = sinf(dsp->mixer_phase);
        float mixed_i = i * cos_val - q * sin_val;
        float mixed_q = i * sin_val + q * cos_val;

        dsp->mixer_phase += dsp->mixer_phase_step;
        // keep phase in the range [-PI, +PI], to prevent a buffer overflow with accumulation
        // Use  [-PI, +PI], not [0, +2PI] to keep compatibility with tan2f(y, x)
        if (dsp->mixer_phase > (float)M_PI) {
            dsp->mixer_phase -= 2.0f * (float)M_PI;
        } else if (dsp->mixer_phase < -(float)M_PI) {
            dsp->mixer_phase += 2.0f * (float)M_PI;
        }

        // Accumulate for further decimation
        dsp->decimated_sum_i += mixed_i;
        dsp->decimated_sum_q += mixed_q;

        // it's time to decimate
        if (++dsp->decimated_counter >= dsp->decimation) {
            float decimated_i = dsp->decimated_sum_i / dsp->decimated_counter;
            float decimated_q = dsp->decimated_sum_q / dsp->decimated_counter;

            if (complex_samples_written < max_out_samples) {
                out_complex_samples[complex_samples_written++] = (complex_sample_t){
                    .i = decimated_i,
                    .q = decimated_q};
            }

            dsp->decimated_counter = 0;
            dsp->decimated_sum_i = 0.0;
            dsp->decimated_sum_q = 0.0;
        }
    }
    return complex_samples_written;
}