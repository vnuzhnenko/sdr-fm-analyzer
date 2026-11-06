#ifndef STEREO_DEMOD_H
#define STEREO_DEMOD_H

#include <stddef.h>
#include <stdbool.h>

typedef struct stereo_demod stereo_demod_t;

/**
 * Initializer and deinitializer
 */
stereo_demod_t* stereo_demod_create(float sample_rate_hz, size_t num_samples);
void            stereo_demod_destroy(stereo_demod_t *self);

/**
 * Process the FM Multiplex buffer
 */
void stereo_demod_process(stereo_demod_t *self, const float *mpx_in, size_t num_samples);

/**
 * Returns true if there is a stereo signal detected in the FM baseband
 * after processing the FM Multiplex buffer
 */
bool stereo_demod_is_active(const stereo_demod_t *self);

/**
 * Returns original audio signal for left stereo channel
 * Left and right channels are equal for mongo signal
 */
const float *stereo_demod_get_left(const stereo_demod_t *self);

/**
 * Returns original audio signal for right stereo channel
 * Left and right channels are equal for mongo signal
 */
const float *stereo_demod_get_right(const stereo_demod_t *self);

#endif // STEREO_DEMOD_H