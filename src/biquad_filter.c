#include "biquad_filter.h"
#include <math.h>

void biquad_reset(biquad_t *filter) {
    if (!filter) {
        return;
    }
    filter->z1 = 0;
    filter->z2 = 0;
}

void biquad_init_lowpass(biquad_t *filter, float cutoff_hz, float sample_rate_hz) {
    if (!filter || sample_rate_hz <= 0) {
        return;
    }

    // Use the Butterworth 2nd-order low pass filter
    float omega = 2.0f * (float)M_PI * (cutoff_hz / sample_rate_hz);
    float alpha = sinf(omega) * M_SQRT1_2;

    float a0 = 1 + alpha;
    float a1 = -2 * cosf(omega);
    float a2 = 1 - alpha;

    float b0 = (1 - cosf(omega)) / 2;
    float b1 = 1 - cosf(omega);
    float b2 = b0;

    // normilize the values
    filter->a1 = a1 / a0;
    filter->a2 = a2 / a0;
    filter->b0 = b0 / a0;
    filter->b1 = b1 / a0;
    filter->b2 = b2 / a0;

    biquad_reset(filter);
}

void biquad_init_bandpass(biquad_t *filter, float center_hz, float q, float sample_rate_hz) {
    if (!filter || sample_rate_hz <= 0 || q <= 0) {
        return;
    }

    // Use the Butterworth 2nd-order band pass filter
    float omega = 2.0f * (float)M_PI * (center_hz / sample_rate_hz);
    float alpha = sinf(omega) / (2.0f * q);

    float a0 = 1 + alpha;
    float a1 = -2 * cosf(omega);
    float a2 = 1 - alpha;

    float b0 = alpha;
    float b1 = 0;
    float b2 = -b0;

    // normilize the values
    filter->a1 = a1 / a0;
    filter->a2 = a2 / a0;
    filter->b0 = b0 / a0;
    filter->b1 = b1 / a0;
    filter->b2 = b2 / a0;

    biquad_reset(filter);
}

void biquad_process(biquad_t *filter, const float *in_buffer, float *out_buffer, size_t count) {
    if (!filter || !in_buffer || !out_buffer || count == 0) {
        return;
    }
    float z1 = filter->z1;
    float z2 = filter->z2;

    for (size_t n = 0; n < count; n++) {
        float x = in_buffer[n];
        float y = filter->b0 * x + z1;
        z1 = filter->b1 * x - filter->a1 * y + z2;
        z2 = filter->b2 * x - filter->a2 * y;
        out_buffer[n] = y;
    }
    filter->z1 = z1;
    filter->z2 = z2;
}