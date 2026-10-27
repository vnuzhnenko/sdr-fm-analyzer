#include <stdlib.h>
#include <math.h>
#include "stereo_demod.h"
#include "biquad_filter.h"

#define PILOT_FREQ_HZ         19000.0f  // Pilot tone at 19 kHz 
#define PILOT_BANDWIDTH_HZ    600.0f    // 600 Hz passband around 19 kHz
#define PILOT_BPF_Q           (PILOT_FREQ_HZ / PILOT_BANDWIDTH_HZ)
#define STEREO_PILOT_TAU_SEC  0.010f  // 10 ms time constant

struct stereo_demod {
    biquad_t pilot_bpf;     // filter that 
    size_t buffer_capacity; // for 19 kHz and 39 kHz buffers
    float *pilot_buffer;    // holds 19 kHz pilot tone
    float *carrier_buffer;  // holds 38 kHz synthesized stereo carrier
    float pilot_energy_avg;   // moving average accumulator
    float pilot_energy_alpha; // moving average coefficient (sample rate dependent)
    bool is_stereo;
};

/**
 * Initializer
 */
stereo_demod_t* stereo_demod_create(float sample_rate_hz, size_t max_samples) {
    if (max_samples == 0 || sample_rate_hz <= 0.0f) {
        return NULL;
    }

    stereo_demod_t *stereo = (stereo_demod_t*)calloc(1, sizeof(stereo_demod_t));
    if (!stereo) return NULL;

    stereo->buffer_capacity = max_samples;
    stereo->pilot_buffer = (float*)malloc(sizeof(float) * max_samples);
    if (!stereo->pilot_buffer) {
        stereo_demod_destroy(stereo);
        return NULL;
    }
    stereo->carrier_buffer = (float*)malloc(sizeof(float) * max_samples);
    if (!stereo->carrier_buffer) {
        stereo_demod_destroy(stereo);
        return NULL;
    }

    biquad_init_bandpass(&stereo->pilot_bpf, PILOT_FREQ_HZ, PILOT_BPF_Q, sample_rate_hz);

    // determine coeficient for moving average
    stereo->pilot_energy_alpha = 1.0f - expf(-1.0f / (sample_rate_hz * STEREO_PILOT_TAU_SEC));

    return stereo;
}

/**
 * Deinitializer
 */
void stereo_demod_destroy(stereo_demod_t *stereo) {
    if (!stereo) {
        return;
    }
    if (stereo->pilot_buffer != NULL) {
        free(stereo->pilot_buffer);
    }
    if (stereo->carrier_buffer != NULL) {
        free(stereo->carrier_buffer);
    }
    free(stereo);
}

/**
 * Receive an FM Multiplex (MPX) signal and try to extract 19 kHz pilot tone
 */
void extract_19khz_tone(stereo_demod_t *stereo, const float *mpx_in, size_t mpx_samples) {
    if (!stereo || !mpx_in || mpx_samples == 0) {
        return;
    }
    size_t num_samples = mpx_samples;
    if (mpx_samples > stereo->buffer_capacity) {
        num_samples = stereo->buffer_capacity;
    }
    biquad_process(&stereo->pilot_bpf, mpx_in, stereo->pilot_buffer, num_samples);
}

/**
 * Based on the 19 kHz pilot tone, generate a 19 * 2 kHz tone for stereo carrier
 * BAsed on tirgonometry cos(2a) = 2 cos^2(a) - 1
 */
void synthesize_38khz_carrier(stereo_demod_t *stereo, size_t mpx_samples) {
    if (!stereo || mpx_samples == 0) {
        return;
    }
    size_t num_samples = mpx_samples;
    if (mpx_samples > stereo->buffer_capacity) {
        num_samples = stereo->buffer_capacity;
    }

    for (size_t n = 0; n < num_samples; n++) {
        float x = stereo->pilot_buffer[n];
        float x_sq = x * x;

        // Smoothly track the average of x^2
        stereo->pilot_energy_avg += stereo->pilot_energy_alpha * (x_sq - stereo->pilot_energy_avg);

        // Protect against division by zero when signal is pure silence (0.0)
        if (stereo->pilot_energy_avg > 1e-7f) {
            stereo->carrier_buffer[n] = (x_sq - stereo->pilot_energy_avg) / stereo->pilot_energy_avg;
        } else {
            stereo->carrier_buffer[n] = 0.0f;
        }
    }
    // avarage is also used to determine if there is a stero signal
    stereo->is_stereo = (stereo->pilot_energy_avg > 0.0015f);
}

/**
 * Returns true if there is a stereo signal detected in the FM baseband
 */
bool has_stereo_payload(const stereo_demod_t *stereo) {
    return stereo ? stereo->is_stereo : false;
}
