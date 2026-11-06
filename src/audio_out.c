#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "audio_out.h"

struct audio_out { // self

    /* De-emphasis filter state and coefficient */
    float deemphasis_state_left;
    float deemphasis_state_right;
    float deemphasis_alpha;

    /* -- Audio resampling */
    float audio_resample_step; 
    float audio_resample_acc;
    float audio_previous_left;
    float audio_previous_right;
};

/**
 * Initializer
 */
audio_out_t* audio_out_create(float input_sample_rate_hz, float out_sample_rate_hz, float deemphasis_us) {
    if (input_sample_rate_hz <= 0.0f || out_sample_rate_hz <= 0.0f) {
        return NULL;
    }

    audio_out_t *self = (audio_out_t *)calloc(1, sizeof(audio_out_t));
    if (!self) {
        return NULL;
    }

    /*
     * De-emphasis single-pole IIR filter coefficient:
     * time constant tau = deemphasis_us * 1e-6 s (e.g., 50 us -> 0.000050 s)
     * alpha = 1.0 - exp(-1.0 / (sample_rate * tau))
     */
    if (deemphasis_us > 0.0f) {
        float tau = deemphasis_us * 1e-6f;
        self->deemphasis_alpha = 1.0f - expf(-1.0f / (input_sample_rate_hz * tau));
    } else {
        self->deemphasis_alpha = 1.0f; // Passthrough
    }
    self->deemphasis_state_left = 0.0f;
    self->deemphasis_state_right = 0.0f;

    /* -- Audio resampling */
    self->audio_resample_step = out_sample_rate_hz / input_sample_rate_hz;
    self->audio_resample_acc = 0.0;
    self->audio_previous_left = 0.0;
    self->audio_previous_right = 0.0;

    return self;
}

/**
 * Destructor
 */
void audio_out_destroy(audio_out_t *self) {
    free(self);
}

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
) {
    if (!self || !left_channel || !right_channel || !out_pcm || num_samples == 0 || out_pcm_max_len == 0) {
        return 0;
    }

    size_t pcm_samples_written = 0;
    for (size_t n = 0; n < num_samples; n++) {
        float angle_left = left_channel[n];
        float angle_right = right_channel[n];

        // Apply single-pole IIR de-emphasis filter.
        // It is still possbile to completely bypass it with
        //   float audio_sample = angle;
        self->deemphasis_state_left += self->deemphasis_alpha * (angle_left - self->deemphasis_state_left);
        self->deemphasis_state_right += self->deemphasis_alpha * (angle_right - self->deemphasis_state_right);
        float audio_sample_left = self->deemphasis_state_left;
        float audio_sample_right = self->deemphasis_state_right;

        float audio_scaled_left = audio_sample_left * (float)INT16_MAX;
        if (audio_scaled_left > (float)INT16_MAX)
        {
            audio_scaled_left = (float)INT16_MAX;
        }
        else if (audio_scaled_left < (float)INT16_MIN)
        {
            audio_scaled_left = (float)INT16_MIN;
        }

        float audio_scaled_right = audio_sample_right * (float)INT16_MAX;
        if (audio_scaled_right > (float)INT16_MAX)
        {
            audio_scaled_right = (float)INT16_MAX;
        }
        else if (audio_scaled_right < (float)INT16_MIN)
        {
            audio_scaled_right = (float)INT16_MIN;
        }

        // 250 KHz -> 48 KHz linear downsampling
        self->audio_resample_acc += self->audio_resample_step;
        if (self->audio_resample_acc >= 1.0)
        {
            self->audio_resample_acc -= 1.0f;

            // Fraction between previous sample and current sample
            float mu = self->audio_resample_acc / self->audio_resample_step;
            float linered_sample_left = (1 - mu) * self->audio_previous_left + mu * audio_scaled_left;
            float linered_sample_right = (1 - mu) * self->audio_previous_right + mu * audio_scaled_right;

            if (pcm_samples_written + 1 < out_pcm_max_len)
            {
                out_pcm[pcm_samples_written++] = (int16_t)linered_sample_left;
                out_pcm[pcm_samples_written++] = (int16_t)linered_sample_right;
            }
        }
        self->audio_previous_left = audio_scaled_left;
        self->audio_previous_right = audio_scaled_right;
    }
    return pcm_samples_written;
}