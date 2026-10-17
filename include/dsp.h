#ifndef DSP_H
#define DSP_H

#include <stdint.h>
#include <stddef.h>

/**
 * DSP Pipeline State
 * 
 * Manages sample rate conversion, FM demodulation, and audio filtering.
 */
typedef struct dsp_pipeline dsp_pipeline_t;

typedef struct {
    uint32_t input_sample_rate;     /* e.g., 8000000 (8 MSPS from HackRF) */
    uint32_t output_sample_rate;    /* e.g., 48000 (standard audio rate) */
    int32_t  freq_offset_hz;        /* e.g., +250000 to off-tune from HackRF DC spike */
    float    deemphasis_us;         /* 50.0f (Europe) or 75.0f (US) */
    size_t   max_input_chunk_bytes; /* Max raw I/Q chunk size in bytes per dsp_process call */
} dsp_config_t;

/**
 * Complex sample representation for decimated baseband I/Q.
 */
typedef struct {
    float i;
    float q;
} complex_sample_t;

/**
 * Create and configure the DSP pipeline.
 */
dsp_pipeline_t* dsp_create(const dsp_config_t *cfg);

/**
 * Free DSP pipeline resources.
 */
void dsp_destroy(dsp_pipeline_t *dsp);

/**
 * Process a block of raw interleaved 8-bit signed I/Q samples from HackRF:
 * [I0, Q0, I1, Q1, ...]
 * 
 * Pipeline stages:
 * 1. Convert int8_t I/Q to normalized float complex [-1.0, 1.0]
 * 2. (Optional) Frequency shift by freq_offset_hz to center the signal
 * 3. Decimation stage 1 & Channel Low-Pass Filtering
 * 4. FM Discriminator / Demodulation (polar discriminator / atan2)
 * 5. De-emphasis filter (single-pole IIR)
 * 6. Decimation stage 2 down to output_sample_rate (48 kHz)
 * 7. Convert output to 16-bit signed PCM (int16_t)
 * 
 * @param dsp         DSP pipeline instance
 * @param raw_iq      Input buffer of interleaved int8_t (I, Q)
 * @param raw_iq_len  Number of bytes in raw_iq (must be even, length = 2 * num_samples)
 * @param out_pcm     Output buffer to receive 16-bit mono PCM audio
 * @param max_pcm_len Maximum capacity of out_pcm (in number of int16_t elements)
 * @return Number of int16_t audio samples written to out_pcm
 */
size_t dsp_process_iq_to_pcm(
    dsp_pipeline_t *dsp,
    const int8_t *raw_iq,
    size_t raw_iq_len,
    int16_t *out_pcm,
    size_t max_pcm_len
);

/**
 * Downconverts and decimates raw interleaved 8-bit I/Q samples from HackRF.
 *
 * Operations performed:
 * 1. Normalizes raw signed 8-bit I/Q [I0, Q0, I1, Q1, ...] to floating point [-1.0, 1.0].
 * 2. Applies digital frequency translation (software mixer) using dsp->phase and dsp->phase_step
 *    to shift the target station away from the HackRF DC center spike.
 * 3. Applies anti-aliasing channel low-pass filtering and decimation by factor M
 *    (e.g., 8 MSPS -> 250 kSPS, factor M = 32).
 *
 * @param dsp             Pointer to the DSP pipeline state (maintains mixer phase & filter state).
 * @param raw_iq          Input buffer of interleaved int8_t samples [I0, Q0, I1, Q1, ...].
 * @param raw_iq_bytes    Total byte length of raw_iq (must be even, = 2 * number of raw samples).
 * @param out_decim_iq    Output buffer to receive decimated normalized complex samples.
 * @param max_out_samples Maximum capacity of out_decim_iq (in complex_sample_t elements).
 *
 * @return Number of decimated complex samples written to out_decim_iq.
 */
size_t rf_downconvert_and_decimate(
    dsp_pipeline_t *dsp,
    const int8_t *raw_iq,
    size_t raw_iq_bytes,
    complex_sample_t *out_complex_samples,
    size_t max_out_samples
);

/**
 * Performs FM demodulation over a decimated buffer
 *
 * @param dsp             Pointer to the DSP pipeline state
 * @param complex_samples Input buffer of decimates normilize complex samples
 * @param num_samples     Number of complex samples in the intpu buffre to process
 * @param out_mpx         Output buffer of float values to receive FM Multiplex baseband
 * @param max_out_bytes   Maximum capacity of out_max in bytes
 */
size_t fm_demodulate_to_mpx(
    dsp_pipeline_t *dsp,
    const complex_sample_t *complex_samples,
    size_t num_complex_samples,
    float *out_mpx,
    size_t out_mpx_capacity
);

#endif /* DSP_H */
