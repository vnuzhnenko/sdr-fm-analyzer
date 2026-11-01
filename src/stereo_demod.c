#include <stdlib.h>
#include <math.h>
#include "stereo_demod.h"
#include "biquad_filter.h"

#define STEREO_UPPER_FREQ_HZ  15000.0f

#define PILOT_FREQ_HZ         19000.0f  // Pilot tone at 19 kHz 
#define PILOT_BANDWIDTH_HZ    600.0f    // 600 Hz passband around 19 kHz
#define PILOT_BPF_Q           (PILOT_FREQ_HZ / PILOT_BANDWIDTH_HZ)
#define PILOT_TAU_SEC         0.010f  // 10 ms time constant

#define MODULATED_FREQ_HZ       38000.0f  // modulated stereo signal centered at 38 kHz 
#define MODULATED_BANDWIDTH_HZ  (STEREO_UPPER_FREQ_HZ * 2.0f)  // 30000 Hz passband around 38 kHz
#define MODULATED_BPF_Q         (MODULATED_FREQ_HZ / MODULATED_BANDWIDTH_HZ)

struct stereo_demod {
    size_t buffer_capacity; // for 19 kHz and 38 kHz buffers

    biquad_t pilot_bpf;     // band pass filter for a pilot 19 kHz tone
    float *pilot_buffer;    // holds 19 kHz pilot tone
    float pilot_energy_avg;   // moving average accumulator
    float pilot_energy_alpha; // moving average coefficient (sample rate dependent)

    float *carrier_buffer;  // holds 38 kHz synthesized stereo carrier

    biquad_t modulated_stereo_bpf;    // band pass filter for stereo signal 23 - 53 kHz
    float *modulated_stereo_buffer;   // holds modulated stereo signal
    biquad_t stereo_lpf;              // low pass filter for stereo signal < 15 kHz
    float *stereo_buffer;             // holds demodulated stereo signal

    bool is_stereo;
};

/**
 * Initializer
 */
stereo_demod_t* stereo_demod_create(float sample_rate_hz, size_t max_samples) {
    if (max_samples == 0 || sample_rate_hz <= 0.0f) {
        return NULL;
    }

    stereo_demod_t *demod = (stereo_demod_t*)calloc(1, sizeof(stereo_demod_t));
    if (!demod) return NULL;

    demod->buffer_capacity = max_samples;

    demod->pilot_buffer = (float*)malloc(sizeof(float) * max_samples);
    if (!demod->pilot_buffer) {
        stereo_demod_destroy(demod);
        return NULL;
    }
    demod->carrier_buffer = (float*)malloc(sizeof(float) * max_samples);
    if (!demod->carrier_buffer) {
        stereo_demod_destroy(demod);
        return NULL;
    }
    demod->modulated_stereo_buffer = (float*)malloc(sizeof(float) * max_samples);
    if (!demod->modulated_stereo_buffer) {
        stereo_demod_destroy(demod);
        return NULL;
    }
    demod->stereo_buffer = (float*)malloc(sizeof(float) * max_samples);
    if (!demod->stereo_buffer) {
        stereo_demod_destroy(demod);
        return NULL;
    }

    biquad_init_bandpass(&demod->pilot_bpf, PILOT_FREQ_HZ, PILOT_BPF_Q, sample_rate_hz);

    biquad_init_bandpass(&demod->modulated_stereo_bpf, MODULATED_FREQ_HZ, MODULATED_BPF_Q, sample_rate_hz);

    biquad_init_lowpass(&demod->stereo_lpf, STEREO_UPPER_FREQ_HZ, sample_rate_hz);

    // determine coeficient for moving average
    demod->pilot_energy_alpha = 1.0f - expf(-1.0f / (sample_rate_hz * PILOT_TAU_SEC));

    return demod;
}

/**
 * Deinitializer
 */
void stereo_demod_destroy(stereo_demod_t *demod) {
    if (!demod) {
        return;
    }
    free(demod->pilot_buffer);
    free(demod->carrier_buffer);
    free(demod->modulated_stereo_buffer);
    free(demod->stereo_buffer);
    free(demod);
}

/**
 * Receive an FM Multiplex (MPX) signal and try to extract 19 kHz pilot tone
 */
static void extract_19khz_tone(stereo_demod_t *demod, const float *mpx_in, size_t num_samples) {
    if (!demod || !mpx_in || num_samples == 0) {
        return;
    }
    biquad_process(&demod->pilot_bpf, mpx_in, demod->pilot_buffer, num_samples);
}

/**
 * Based on the 19 kHz pilot tone, generate a 19 * 2 kHz tone for stereo carrier
 * It is based on tirgonometry formula cos(2a) = 2 cos^2(a) - 1
 */
static void synthesize_38khz_carrier(stereo_demod_t *demod, size_t num_samples) {
    if (!demod || num_samples == 0) {
        return;
    }

    for (size_t n = 0; n < num_samples; n++) {
        float x = demod->pilot_buffer[n];
        float x_sq = x * x;

        // Smoothly track the average of x^2
        demod->pilot_energy_avg += demod->pilot_energy_alpha * (x_sq - demod->pilot_energy_avg);

        // Protect against division by zero when signal is pure silence (0.0)
        if (demod->pilot_energy_avg > 1e-7f) {
            demod->carrier_buffer[n] = (x_sq - demod->pilot_energy_avg) / demod->pilot_energy_avg;
        } else {
            demod->carrier_buffer[n] = 0.0f;
        }
    }
    // avarage is also used to determine if there is a stero signal
    demod->is_stereo = (demod->pilot_energy_avg > 0.0015f);
}


void stereo_demod_process(stereo_demod_t *demod, const float *mpx_in, size_t mpx_samples) {
    if (!demod || !mpx_in || mpx_samples == 0) {
        return;
    }

    size_t num_samples = (mpx_samples > demod->buffer_capacity) ? demod->buffer_capacity : mpx_samples;
    extract_19khz_tone(demod, mpx_in, num_samples);
    synthesize_38khz_carrier(demod, num_samples);
}

/**
 * Returns true if there is a stereo signal detected in the FM baseband
 */
bool stereo_demod_is_active(const stereo_demod_t *demod) {
    return demod ? demod->is_stereo : false;
}
