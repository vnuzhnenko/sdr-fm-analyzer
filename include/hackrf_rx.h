#ifndef HACKRF_RX_H
#define HACKRF_RX_H

#include <stdint.h>
#include <stdbool.h>
#include "ring_buffer.h"

/**
 * HackRF Receiver Configuration
 */
typedef struct {
    uint64_t freq_hz;          /* Center tuning frequency (e.g., 100100000 for 100.1 MHz) */
    uint32_t sample_rate_hz;   /* Baseband sample rate (e.g., 8000000 for 8 MSPS) */
    uint32_t lna_gain;         /* LNA (IF) gain: 0-40 dB in 8 dB steps */
    uint32_t vga_gain;         /* VGA (baseband) gain: 0-62 dB in 2 dB steps */
    bool     amp_enable;       /* Optional RF amplifier (+14 dB): 0 or 1 */
} hackrf_config_t;

/**
 * Handle to the receiver state.
 */
typedef struct hackrf_rx_context hackrf_rx_context_t;

/**
 * Initialize the HackRF library and open the first available device.
 * @param cfg Initial hardware parameters.
 * @param rb  Target ring buffer to feed raw interleaved I/Q samples (int8_t).
 * @return Context pointer or NULL on failure.
 */
hackrf_rx_context_t* hackrf_rx_init(const hackrf_config_t *cfg, ring_buffer_t *rb);

/**
 * Start streaming raw I/Q samples from the HackRF.
 * @return 0 on success, non-zero on failure.
 */
int hackrf_rx_start(hackrf_rx_context_t *ctx);

/**
 * Stop streaming samples.
 * @return 0 on success, non-zero on failure.
 */
int hackrf_rx_stop(hackrf_rx_context_t *ctx);

/**
 * Re-tune center frequency during runtime.
 */
int hackrf_rx_set_freq(hackrf_rx_context_t *ctx, uint64_t freq_hz);

/**
 * Adjust gains during runtime.
 */
int hackrf_rx_set_gains(hackrf_rx_context_t *ctx, uint32_t lna_gain, uint32_t vga_gain);

/**
 * Close device and clean up libhackrf resources.
 */
void hackrf_rx_cleanup(hackrf_rx_context_t *ctx);

#endif /* HACKRF_RX_H */
