#include "dsp.h"
#include "audio_out.h"
#include "stereo_demod.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FM_DECIMATED_RATE 250000.0f // Intermediate sample rate after decimation
#define FM_CHANNEL_CUTOFF 100000.0f // 100 kHz (±100 kHz for 200 kHz FM channel)
#define MAX_INBOUND_IQ_VALUE 128.0f

/**
 * Complex sample representation for decimated baseband I/Q.
 */
typedef struct {
    float i;
    float q;
} complex_sample_t;

/**
 * DSP Pipeline State
 */
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

    /* -- MPX to Stereo demodulation */

    stereo_demod_t *stereo_demodulation;

    /* -- Audio out */

    audio_out_t *audio_out;
};

/**
 * Initializer
 */
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

    /* -- Stereo demodulation */

    dsp->stereo_demodulation = stereo_demod_create(FM_DECIMATED_RATE, dsp->mpx_buffer_capacity);
    if (!dsp->stereo_demodulation) {
        dsp_destroy(dsp);
        return NULL;
    }

    /* -- Audio out */

    dsp->audio_out = audio_out_create(FM_DECIMATED_RATE, cfg->output_sample_rate, cfg->deemphasis_us);
    if (!dsp->audio_out) {
        dsp_destroy(dsp);
        return NULL;
    }

    return dsp;
}

/**
 * Deinitializer
 */
void dsp_destroy(dsp_pipeline_t *dsp) {
    if (!dsp) return;
    free(dsp->decimation_buffer);
    free(dsp->mpx_buffer);
    stereo_demod_destroy(dsp->stereo_demodulation);
    audio_out_destroy(dsp->audio_out);
    free(dsp);
}

/**
 * Downconverts and decimates raw interleaved 8-bit I/Q samples from HackRF.
 * 
 * Operations performed:
 * 1. Normalizes raw signed 8-bit I/Q [I0, Q0, I1, Q1, ...] to floating point [-1.0, 1.0].
 * 2. Applies digital frequency translation (software mixer) using dsp->phase and dsp->phase_step
 *    to shift the target station away from the HackRF DC center spike.
 * 3. Applies anti-aliasing channel low-pass filtering and decimation by factor M
 *    (e.g., 8 MSPS -> 250 kSPS, factor M = 32).
 */
static size_t rf_downconvert_and_decimate(
    dsp_pipeline_t *dsp,
    const int8_t *raw_iq,
    size_t raw_iq_bytes,
    complex_sample_t *out_complex_samples,
    size_t max_out_samples
) {

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

/**
 * Demodulate decimated I/Q samples into FM Multiplex Baseband
 * Receives I/Q values, does FM demodulation and writes angles in radians to output buffer
 */
static size_t fm_demodulate_to_mpx(
    dsp_pipeline_t *dsp,
    const complex_sample_t *complex_samples,
    size_t num_complex_samples,
    float *out_mpx,
    size_t out_mpx_capacity
) {
    if (!dsp || !complex_samples || num_complex_samples == 0 || !out_mpx || out_mpx_capacity == 0) {
        return 0;
    }

    size_t processed_complex_samples = 0;
    for (size_t n = 0; n < num_complex_samples && n < out_mpx_capacity; n++) {
        float decimated_i = complex_samples[n].i;
        float decimated_q = complex_samples[n].q;

        // Core of FM demodulation
        // Calculating Δθ = atan2 (Q[n], I[n]) - atan2 (Q[n-1], I[n-1])
        // by using complex conjugate that uses only one atan2 call:
        // Δθ = atan2 (Q[n] * I[n-1] - I[n] * Q[n-1], I[n] * I[n-1] + Q[n] * Q[n-1])
        float angle = atan2f(decimated_q * dsp->prev_i - decimated_i * dsp->prev_q, decimated_i * dsp->prev_i + decimated_q * dsp->prev_q);

        dsp->prev_i = decimated_i;
        dsp->prev_q = decimated_q;
        out_mpx[processed_complex_samples++] = angle;
    }
    return processed_complex_samples;
}

size_t dsp_process_iq_to_pcm(
    dsp_pipeline_t *dsp,
    const int8_t *raw_iq,
    size_t raw_iq_len,
    int16_t *out_pcm,
    size_t max_pcm_len
) {
    if (!dsp || !raw_iq || !out_pcm || raw_iq_len == 0 || max_pcm_len == 0) {
        return 0;
    }

    // Processing tier 1: Decimation of high-rate RF domain
    size_t num_complex_samples = rf_downconvert_and_decimate(
        dsp, raw_iq, raw_iq_len, dsp->decimation_buffer, dsp->decimation_capacity
    );

    // Processing Tier 2: FM demoduation
    size_t num_mpx_samples = fm_demodulate_to_mpx(
        dsp, dsp->decimation_buffer, num_complex_samples, dsp->mpx_buffer, dsp->mpx_buffer_capacity
    );

    // Processing Tier 3: Specialezed consumers

    // Process mono and stereo signals
    stereo_demod_process(dsp->stereo_demodulation, dsp->mpx_buffer, num_mpx_samples);

    // Complete processing of audio signal
    const float *left_channel = stereo_demod_get_left(dsp->stereo_demodulation);
    const float *right_channel = stereo_demod_get_right(dsp->stereo_demodulation);
    size_t pcm_samples_written = audio_out_process(dsp->audio_out, left_channel, right_channel, num_mpx_samples, out_pcm, max_pcm_len);

    return pcm_samples_written;
}