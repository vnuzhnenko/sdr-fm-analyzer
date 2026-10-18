#ifndef BIQUAD_FILTER_H
#define BIQUAD_FILTER_H

#include <stddef.h>

typedef struct {
    // Normized coeficients (a0 = 1)
    float b0, b1, b2;
    float a1, a2;

    // Delay state registers
    float z1, z2;
} biquad_t;

void biquad_reset(biquad_t *filter);

void biquad_init_lowpass(biquad_t *filter, float cutoff_hz, float sample_rate_hz);

void biquad_init_bandpass(biquad_t *filter, float center_hz, float q, float sample_rate_hz);

void biquad_process(biquad_t *filter, const float *in_buffer, float *out_buffer, size_t count);

#endif // BIQUAD_FILTER_H