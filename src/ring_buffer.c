#include "ring_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

/*
 * Data structure for thread-safe circular ring buffer.
 *
 * TIP: For concurrency between one producer (HackRF RX callback) and
 * one consumer (DSP thread), using C11 atomic head/tail pointers
 */
struct ring_buffer {
    uint8_t *buffer;
    size_t capacity;
    atomic_size_t head; // Modified only by a producer
    atomic_size_t tail; // modified only by a consumer
};

ring_buffer_t* ring_buffer_create(size_t capacity) {
    if (capacity == 0) {
        return NULL;
    }
    ring_buffer_t *rb = (ring_buffer_t*)calloc(1, sizeof(ring_buffer_t));
    if (!rb) {
        return NULL;
    }
    
    rb->capacity = capacity;
    rb->buffer = (uint8_t*)malloc(capacity);
    if (!rb->buffer) {
        free(rb);
        return NULL;
    }

    atomic_init(&rb->head, 0);
    atomic_init(&rb->tail, 0);

    return rb;
}

void ring_buffer_destroy(ring_buffer_t *rb) {
    if (!rb) return;
    if (rb->buffer) {
        free(rb->buffer);
    }
    free(rb);
}

/**
 * Writes {length} bytes from {data} into the ring buffer.
 * Returns the actual number of bytes written.
 */
size_t ring_buffer_write(ring_buffer_t *rb, const uint8_t *data, size_t length) {
    if (!rb || !data || length == 0) return 0;

    size_t available = ring_buffer_available_write(rb);
    if (available == 0) {
        return 0; /* Buffer is full */
    }

    size_t to_write = (length < available) ? length : available;
    // Clear bit 0: guarantees to_write is alway even, to keep every I/Q pair together
    to_write &= ~1UL;
    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);

    /* 1. Copy first chunk (from head up to the physical end of the buffer) */
    size_t bytes_to_end = rb->capacity - head;
    size_t first_chunk = (to_write < bytes_to_end) ? to_write : bytes_to_end;
    memcpy(&rb->buffer[head], data, first_chunk);

    /* 2. Copy second chunk (wrapped around to index 0), if any */
    size_t second_chunk = to_write - first_chunk;
    if (second_chunk > 0) {
        memcpy(&rb->buffer[0], data + first_chunk, second_chunk);
    }

    /* 3. Advance head and store with release semantics */
    size_t new_head = (head + to_write) % rb->capacity;
    atomic_store_explicit(&rb->head, new_head, memory_order_release);

    return to_write;
}

/**
 * Reads {length} bytes from buffer and writes it to a destination.
 * Return number of bytes written
 */
size_t ring_buffer_read(ring_buffer_t *rb, uint8_t *dest, size_t length) { // TODO: covrent to void* dest
    if (!rb || !dest || length == 0) return 0;

    size_t available = ring_buffer_available_read(rb);
    if (available == 0) {
        return 0;
    }

    size_t to_read = (length < available) ? length : available;
    // Clear bit 0: guarantees to_read is alway even, to keep every I/Q pair together
    to_read &= ~1UL;
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);

    // Copy first chunk from tail up to the end of the buffer
    size_t bytes_to_end = rb->capacity - tail;
    size_t first_chunk = (to_read < bytes_to_end) ? to_read : bytes_to_end;
    memcpy(dest, &rb->buffer[tail], first_chunk);

    //  Copy second chunk wrapped around to index 0), if any
    size_t second_chunk = to_read - first_chunk;
    if (second_chunk > 0) {
        memcpy(dest + first_chunk, &rb->buffer[0], second_chunk);
    }

    // Advance tail and store with release semantics
    size_t new_tail = (tail + to_read) % rb->capacity;
    atomic_store_explicit(&rb->tail, new_tail, memory_order_release);

    return to_read;
}

/**
 * How many bytes are available for reading
 */
size_t ring_buffer_available_read(const ring_buffer_t *rb) {
    if (!rb) return 0;

    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);

    if (head >= tail) {
        return head - tail;
    } else {
        return (rb->capacity - tail) + head;
    }
}

/**
 * How many bytes can be written into a buffer
 */
size_t ring_buffer_available_write(const ring_buffer_t *rb) {
    if (!rb) return 0;
    size_t occuppied = ring_buffer_available_read(rb);
    // capacity - 1 is used for unambiguity (is buffer empty of full when head == tail?)
    return rb->capacity - 1 - occuppied;
}