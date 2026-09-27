#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Thread-safe Circular Ring Buffer
 * 
 * Used to decouple the high-priority libhackrf USB transfer callback
 * from the DSP demodulation and audio output pipeline.
 */

typedef struct ring_buffer ring_buffer_t;

/**
 * Allocate and initialize a new ring buffer.
 * @param capacity Maximum number of bytes the buffer can hold (recommend power of 2).
 * @return Pointer to ring_buffer_t or NULL on failure.
 */
ring_buffer_t* ring_buffer_create(size_t capacity);

/**
 * Free all resources associated with the ring buffer.
 */
void ring_buffer_destroy(ring_buffer_t *rb);

/**
 * Push data into the ring buffer (called by the producer / HackRF callback).
 * @return Number of bytes actually written.
 */
size_t ring_buffer_write(ring_buffer_t *rb, const uint8_t *data, size_t length);

/**
 * Read data from the ring buffer (called by the consumer / DSP processing loop).
 * @return Number of bytes actually read.
 */
size_t ring_buffer_read(ring_buffer_t *rb, uint8_t *dest, size_t length);

/**
 * Get the number of readable bytes currently stored in the buffer.
 */
size_t ring_buffer_available_read(const ring_buffer_t *rb);

/**
 * Get the remaining free space in bytes.
 */
size_t ring_buffer_available_write(const ring_buffer_t *rb);

#endif /* RING_BUFFER_H */
