#include "hackrf_rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
  #if __has_include(<libhackrf/hackrf.h>)
    #include <libhackrf/hackrf.h>
  #elif __has_include(<hackrf.h>)
    #include <hackrf.h>
  #endif
#endif

struct hackrf_rx_context {
    hackrf_config_t config;
    ring_buffer_t *ring_buffer;
    hackrf_device *device;
    bool is_streaming;
};

/*
 * Asynchronous USB transfer callback invoked by libhackrf's worker thread.
 * Runs in a sparate libhackrf Worker CPU thread, as per their docs.
 * 
 * Rules:
 * 1. Must execute as fast as possible to avoid USB buffer overruns
 * 2. Return 0 to continue receiving samples; return non-zero to abort
 */
static int hackrf_rx_callback(hackrf_transfer *transfer) {
    if (!transfer || !transfer->rx_ctx) {
        return 0;
    }

    hackrf_rx_context_t *ctx = (hackrf_rx_context_t *)transfer->rx_ctx;

    if (transfer->valid_length > 0 && ctx->ring_buffer) {
        size_t written = ring_buffer_write(ctx->ring_buffer, transfer->buffer, (size_t)transfer->valid_length);
        if (written < (size_t)transfer->valid_length) {
            /* Ring buffer is full; samples dropped */
            static int drop_warn_counter = 0;
            if (++drop_warn_counter % 100 == 1) {
                fprintf(stderr, "[fm-analyzer] Warning: Ring buffer overflow, samples dropped!\n");
            }
        }
    }

    return 0;
}

hackrf_rx_context_t* hackrf_rx_init(const hackrf_config_t *cfg, ring_buffer_t *rb) {
    if (!cfg || !rb) {
        fprintf(stderr, "[fm-analyzer] Error: Invalid config or ring buffer\n");
        return NULL;
    }

    hackrf_rx_context_t *ctx = (hackrf_rx_context_t *)calloc(1, sizeof(hackrf_rx_context_t));
    if (!ctx) {
        perror("[fm-analyzer] calloc failed");
        return NULL;
    }

    ctx->config = *cfg;
    ctx->ring_buffer = rb;
    ctx->device = NULL;
    ctx->is_streaming = false;

    /* Step 1: Initialize libhackrf library: https://github.com/greatscottgadgets/hackrf/blob/main/host/libhackrf/src/hackrf.h#L54-L58 */
    int result = hackrf_init();
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] hackrf_init() failed: %s (%d)\n",
                hackrf_error_name(result), result);
        free(ctx);
        return NULL;
    }

    /* Step 2: Open first detected HackRF device */
    result = hackrf_open(&ctx->device);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] hackrf_open() failed: %s (%d)\n",
                hackrf_error_name(result), result);
        hackrf_exit();
        free(ctx);
        return NULL;
    }

    /* Step 3: Set ADC/DAC sample rate */
    result = hackrf_set_sample_rate(ctx->device, (double)cfg->sample_rate_hz);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] hackrf_set_sample_rate(%u) failed: %s (%d)\n",
                cfg->sample_rate_hz, hackrf_error_name(result), result);
        hackrf_close(ctx->device);
        hackrf_exit();
        free(ctx);
        return NULL;
    }

    /* Step 4: Configure analog baseband anti-aliasing filter */
    uint32_t filter_bw = hackrf_compute_baseband_filter_bw_round_down_lt(cfg->sample_rate_hz);
    result = hackrf_set_baseband_filter_bandwidth(ctx->device, filter_bw);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] Warning: hackrf_set_baseband_filter_bandwidth(%u) failed: %s (%d)\n",
                filter_bw, hackrf_error_name(result), result);
    }

    /* Step 5: Tune center frequency */
    result = hackrf_set_freq(ctx->device, cfg->freq_hz);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] hackrf_set_freq(%llu) failed: %s (%d)\n",
                (unsigned long long)cfg->freq_hz, hackrf_error_name(result), result);
        hackrf_close(ctx->device);
        hackrf_exit();
        free(ctx);
        return NULL;
    }

    /* Step 6: Set VGA and LNA gains */
    hackrf_rx_set_gains(ctx, cfg->lna_gain, cfg->vga_gain);

    /* Step 7: RF amplifier enable/disable */
    result = hackrf_set_amp_enable(ctx->device, cfg->amp_enable ? 1 : 0);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] Warning: hackrf_set_amp_enable() failed: %s (%d)\n",
                hackrf_error_name(result), result);
    }

    return ctx;
}

int hackrf_rx_start(hackrf_rx_context_t *ctx) {
    if (!ctx || !ctx->device) {
        return -1;
    }
    if (ctx->is_streaming) {
        return 0;
    }

    int result = hackrf_start_rx(ctx->device, hackrf_rx_callback, (void *)ctx);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] hackrf_start_rx() failed: %s (%d)\n",
                hackrf_error_name(result), result);
        return result;
    }

    ctx->is_streaming = true;
    return 0;
}

int hackrf_rx_stop(hackrf_rx_context_t *ctx) {
    if (!ctx || !ctx->device) {
        return -1;
    }
    if (!ctx->is_streaming) {
        return 0;
    }

    int result = hackrf_stop_rx(ctx->device);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] hackrf_stop_rx() failed: %s (%d)\n",
                hackrf_error_name(result), result);
        return result;
    }

    ctx->is_streaming = false;
    return 0;
}

/** 
 * Tuning: https://github.com/greatscottgadgets/hackrf/blob/main/host/libhackrf/src/hackrf.h#L233-L237
 */
int hackrf_rx_set_freq(hackrf_rx_context_t *ctx, uint64_t freq_hz) {
    if (!ctx || !ctx->device) {
        return -1;
    }

    int result = hackrf_set_freq(ctx->device, freq_hz);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] hackrf_set_freq(%llu) failed: %s (%d)\n",
                (unsigned long long)freq_hz, hackrf_error_name(result), result);
        return result;
    }

    ctx->config.freq_hz = freq_hz;
    return 0;
}

/**
 * For RX: https://github.com/greatscottgadgets/hackrf/blob/main/host/libhackrf/src/hackrf.h#L225-L227
 */
int hackrf_rx_set_gains(hackrf_rx_context_t *ctx, uint32_t lna_gain, uint32_t vga_gain) {
    if (!ctx || !ctx->device) {
        return -1;
    }

    if (lna_gain > 40) lna_gain = 40;
    lna_gain = (lna_gain / 8) * 8; // Quntinization to 8dB steps

    if (vga_gain > 62) vga_gain = 62;
    vga_gain = (vga_gain / 2) * 2; // Quantinization to 2dB steps

    int res_lna = hackrf_set_lna_gain(ctx->device, lna_gain);
    if (res_lna != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] hackrf_set_lna_gain(%u) failed: %s (%d)\n",
                lna_gain, hackrf_error_name(res_lna), res_lna);
    }

    int res_vga = hackrf_set_vga_gain(ctx->device, vga_gain);
    if (res_vga != HACKRF_SUCCESS) {
        fprintf(stderr, "[fm-analyzer] hackrf_set_vga_gain(%u) failed: %s (%d)\n",
                vga_gain, hackrf_error_name(res_vga), res_vga);
    }

    ctx->config.lna_gain = lna_gain;
    ctx->config.vga_gain = vga_gain;

    return (res_lna == HACKRF_SUCCESS && res_vga == HACKRF_SUCCESS) ? 0 : -1;
}

void hackrf_rx_cleanup(hackrf_rx_context_t *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->is_streaming && ctx->device) {
        hackrf_stop_rx(ctx->device);
        ctx->is_streaming = false;
    }

    if (ctx->device) {
        hackrf_close(ctx->device);
        ctx->device = NULL;
    }

    hackrf_exit();
    free(ctx);
}
