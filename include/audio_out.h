#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stddef.h>
#include <stdint.h>

typedef struct audio_out audio_out_t;

/**
 * Initializer and deititializer
 */
audio_out_t* audio_out_create(float in_sample_rate_hz, float out_smaple_rate_hz, float deemphasis_us);
void         audio_out_destroy(audio_out_t *self);

/**
 * Finalise preparation of audio signal to be ready for playing
 */
size_t audio_out_process(
    audio_out_t *self,
    const float *left_channel,
    const float *right_channel,
    size_t num_samples,
    int16_t *out_pcm,
    size_t out_pcm_max_len
);

#endif // AUDIO_OUT_H