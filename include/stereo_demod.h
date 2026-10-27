#ifndef STEREO_DEMOD_H
#define STEREO_DEMOD_H

#include <stddef.h>
#include <stdbool.h>

typedef struct stereo_demod stereo_demod_t;

/**
 * Initializer and deinitializer
 */
stereo_demod_t* stereo_demod_create(float sample_rate_hz, size_t max_samples);
void stereo_demod_destroy(stereo_demod_t *stereo);

/**
 * @param stereo_demod_t Pointer the stereo_demod_t 
 * @param mxp_in FM MPX buffer
 * @param mxp_samples Number of MPX float samples available for processing
 */
void extract_19khz_tone(stereo_demod_t *stereo, const float *mpx_in, size_t mpx_samples);

void synthesize_38khz_carrier(stereo_demod_t *stereo, size_t mpx_samples);

bool has_stereo_payload(const stereo_demod_t *stereo);

#endif // STEREO_DEMOD_H