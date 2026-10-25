#include <stdlib.h>
#include "stereo_demod.h"
#include "biquad_filter.h"

#define PILOT_FREQ_HZ       19000.0f  // Pilot tone at 19 kHz 
#define PILOT_BANDWIDTH_HZ  600.0f    // 600 Hz passband around 19 kHz
#define PILOT_BPF_Q         (PILOT_FREQ_HZ / PILOT_BANDWIDTH_HZ)

struct stereo_demod {
    biquad_t pilot_bpf;     // filter that 
    size_t buffer_capacity; // for 19 kHz and 39 kHz buffers
    float *pilot_buffer;    // holds 19 kHz pilot tone
    float *carrier_buffer;  // holds 38 kHz synthesized stereo carrier
    bool is_stereo;
};

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

    return stereo;
}

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