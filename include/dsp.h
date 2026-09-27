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
    uint32_t input_sample_rate;   /* e.g., 8000000 (8 MSPS from HackRF) */
    uint32_t output_sample_rate;  /* e.g., 48000 (standard audio rate) */
    int32_t  freq_offset_hz;      /* e.g., +250000 to off-tune from HackRF DC spike */
    float    deemphasis_us;       /* 50.0f (Europe) or 75.0f (US) */
} dsp_config_t;

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
size_t dsp_process_iq_to_pcm(dsp_pipeline_t *dsp,
                             const int8_t *raw_iq,
                             size_t raw_iq_len,
                             int16_t *out_pcm,
                             size_t max_pcm_len);


#endif /* DSP_H */
