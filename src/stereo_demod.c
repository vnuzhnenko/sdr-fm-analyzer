#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "stereo_demod.h"
#include "biquad_filter.h"

#define MONO_UPPER_FREQ_HZ    15000.0f
#define STEREO_UPPER_FREQ_HZ  15000.0f

#define PILOT_FREQ_HZ         19000.0f  // Pilot tone at 19 kHz 
#define PILOT_BANDWIDTH_HZ    600.0f    // 600 Hz passband around 19 kHz
#define PILOT_BPF_Q           (PILOT_FREQ_HZ / PILOT_BANDWIDTH_HZ)
#define PILOT_TAU_SEC         0.010f  // 10 ms time constant

#define MODULATED_FREQ_HZ       38000.0f  // modulated stereo signal centered at 38 kHz 
#define MODULATED_BANDWIDTH_HZ  (STEREO_UPPER_FREQ_HZ * 2.0f)  // 30000 Hz passband around 38 kHz
#define MODULATED_BPF_Q         (MODULATED_FREQ_HZ / MODULATED_BANDWIDTH_HZ)

struct stereo_demod { // self
    size_t buffer_capacity; // shared: for 19 kHz, 38 kHz and stereo buffers

    biquad_t mono_lpf;             // low pass lilter for mono signal
    biquad_t pilot_bpf;            // band pass filter for a pilot 19 kHz tone
    biquad_t modulated_stereo_bpf; // band pass filter for stereo signal 23 - 53 kHz
    biquad_t stereo_diff_lpf;      // low pass filter for stereo difference signal

    float pilot_energy_avg;   // moving average accumulator
    float pilot_energy_alpha; // moving average coefficient (sample rate dependent)

    float *subcarrier_buffer;  // shared: holds 19 kHz pilot tone or 38 kHz synthesized stereo carrier
    float *left_buffer;        // out buffer for a left channel
    float *right_buffer;       // out buffer for a right channel

    bool is_stereo;
};

/**
 * Initializer
 */
stereo_demod_t* stereo_demod_create(float sample_rate_hz, size_t max_samples) {
    if (max_samples == 0 || sample_rate_hz <= 0.0f) {
        return NULL;
    }

    stereo_demod_t *self = (stereo_demod_t*)calloc(1, sizeof(stereo_demod_t));
    if (!self) return NULL;

    self->buffer_capacity = max_samples;

    self->subcarrier_buffer = (float*)malloc(sizeof(float) * max_samples);
    if (!self->subcarrier_buffer) {
        stereo_demod_destroy(self);
        return NULL;
    }
    self->left_buffer = (float*)malloc(sizeof(float) * max_samples);
    if (!self->left_buffer) {
        stereo_demod_destroy(self);
        return NULL;
    }
    self->right_buffer = (float*)malloc(sizeof(float) * max_samples);
    if (!self->right_buffer) {
        stereo_demod_destroy(self);
        return NULL;
    }

    biquad_init_lowpass(&self->mono_lpf, STEREO_UPPER_FREQ_HZ, sample_rate_hz);

    // determine coeficient for moving average
    self->pilot_energy_alpha = 1.0f - expf(-1.0f / (sample_rate_hz * PILOT_TAU_SEC));
    biquad_init_bandpass(&self->pilot_bpf, PILOT_FREQ_HZ, PILOT_BPF_Q, sample_rate_hz);

    biquad_init_bandpass(&self->modulated_stereo_bpf, MODULATED_FREQ_HZ, MODULATED_BPF_Q, sample_rate_hz);

    biquad_init_lowpass(&self->stereo_diff_lpf, STEREO_UPPER_FREQ_HZ, sample_rate_hz);

    return self;
}

/**
 * Deinitializer
 */
void stereo_demod_destroy(stereo_demod_t *self) {
    if (!self) {
        return;
    }
    free(self->subcarrier_buffer);
    free(self->left_buffer);
    free(self->right_buffer);
    free(self);
}

/**
 * Extract Mono signal from FM MPX baseband
 * A left out channel is only used at this step
 */
static void extract_mono(stereo_demod_t *self, const float *mpx_in, size_t num_samples) {
    if (!self || !mpx_in || num_samples == 0) {
        return;
    }
    biquad_process(&self->mono_lpf, mpx_in, self->left_buffer, num_samples);
}

/**
 * Receive an FM Multiplex (MPX) signal and try to extract 19 kHz pilot tone
 */
static void extract_19khz_tone(stereo_demod_t *self, const float *mpx_in, size_t num_samples) {
    if (!self || !mpx_in || num_samples == 0) {
        return;
    }
    biquad_process(&self->pilot_bpf, mpx_in, self->subcarrier_buffer, num_samples);
}

/**
 * Based on the 19 kHz pilot tone, generate a 19 * 2 kHz tone for stereo carrier
 * It is based on tirgonometry formula cos(2a) = 2 cos^2(a) - 1
 */
static void synthesize_38khz_carrier(stereo_demod_t *self, size_t num_samples) {
    if (!self || num_samples == 0) {
        return;
    }

    for (size_t n = 0; n < num_samples; n++) {
        // subcarrier buffer is used in place to synthesize 38 kHz out of 19 kHz
        float x = self->subcarrier_buffer[n];
        float x_sq = x * x;

        // Smoothly track the average of x^2
        self->pilot_energy_avg += self->pilot_energy_alpha * (x_sq - self->pilot_energy_avg);

        // Protect against division by zero when signal is pure silence (0.0)
        if (self->pilot_energy_avg > 1e-7f) {
            self->subcarrier_buffer[n] = (x_sq - self->pilot_energy_avg) / self->pilot_energy_avg;
        } else {
            self->subcarrier_buffer[n] = 0.0f;
        }
    }
    // avarage is also used to determine if there is a stero signal
    self->is_stereo = (self->pilot_energy_avg > 0.0015f);
    // self->is_stereo = false;
}

/**
 * Extract stereo difference based on the 38 kHz carrier
 * Re-use self->right_buffer two times to temporary store the difference
 */
static void extract_stereo_difference(stereo_demod_t *self, const float *mpx_in, size_t num_samples) {
    if (!self || !mpx_in || num_samples == 0) {
        return;
    }
    biquad_process(&self->modulated_stereo_bpf, mpx_in, self->right_buffer, num_samples);

    // mixing: self->right_buffer * cos(2*pi*38_000) =>
    //          => demodulated stereo diff at 0..15 kHz and 76..91 kHz at half of amplitude
    for (size_t n = 0; n < num_samples; n++) {
        self->right_buffer[n] = self->right_buffer[n] * self->subcarrier_buffer[n] * 2.0f;
    }
    // Filter out the 76..91 kHz part of signal and hold only 0..15 kHz
    biquad_process(&self->stereo_diff_lpf, self->right_buffer, self->right_buffer, num_samples);
}

/**
 * An entry point function to run this DSP pipeline
 */
void stereo_demod_process(stereo_demod_t *self, const float *mpx_in, size_t mpx_samples) {
    if (!self || !mpx_in || mpx_samples == 0) {
        return;
    }
    size_t num_samples = (mpx_samples > self->buffer_capacity) ? self->buffer_capacity : mpx_samples;

    extract_mono(self, mpx_in, num_samples);
    extract_19khz_tone(self, mpx_in, num_samples);
    synthesize_38khz_carrier(self, num_samples);

    if (self->is_stereo) {
        extract_stereo_difference(self, mpx_in, num_samples);
        // Matrix decode
        for (size_t n = 0; n < num_samples; n++) {
            float mono = self->left_buffer[n];
            float diff = self->right_buffer[n];
            self->left_buffer[n] = (mono + diff) * 0.5f;
            self->right_buffer[n] = (mono - diff) * 0.5f;
        }
    } else {
        // no stereo, so the right channel is equal to the left channel
        memcpy(self->right_buffer, self->left_buffer, num_samples * sizeof(float));
    }
}

/**
 * Returns true if there is a stereo signal detected in the FM baseband
 */
bool stereo_demod_is_active(const stereo_demod_t *self) {
    return self ? self->is_stereo : false;
}

const float *stereo_demod_get_left(const stereo_demod_t *self) {
    return self ? self->left_buffer : NULL;
}

const float *stereo_demod_get_right(const stereo_demod_t *self) {
    return self ? self->right_buffer : NULL;
}
