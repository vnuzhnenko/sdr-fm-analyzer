#ifndef STEREO_DEMOD_H
#define STEREO_DEMOD_H

#include <stddef.h>
#include <stdbool.h>

typedef struct stereo_demod stereo_demod_t;

/**
 * Initializer and deinitializer
 */
stereo_demod_t* stereo_demod_create(float sample_rate_hz, size_t num_samples);
void            stereo_demod_destroy(stereo_demod_t *demod);

void            stereo_demod_process(stereo_demod_t *demod, const float *mpx_in, size_t num_samples);
bool            stereo_demod_is_active(const stereo_demod_t *demod);
const float*    stereo_demod_get_difference(const stereo_demod_t *demod);

#endif // STEREO_DEMOD_H