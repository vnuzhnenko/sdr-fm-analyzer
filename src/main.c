#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#include "hackrf_rx.h"
#include "dsp.h"
#include "ring_buffer.h"

#define DEFAULT_FREQ_HZ         101500000ULL // 101.5 MHz
#define DEFAULT_SAMPLE_RATE     8000000U     // 8 MSPS as recommended on https://hackrf.readthedocs.io/en/latest/sampling_rate.html
#define DEFAULT_LNA_GAIN        32U          // 0-40 dB in 8 dB steps
#define DEFAULT_VGA_GAIN        20U          // 0-62 dB in 2 dB steps
#define DEFAULT_AUDIO_RATE      48000U       // 48 kHz standard audio
#define RING_BUFFER_SIZE        (4 * 1024 * 1024) // 4 MB I/O receive buffer
#define CHUNK_READ_SIZE         (64 * 1024)       // 64 KB block per DSP run

static volatile bool g_running = true;

static void sigint_handler(int signum) {
    (void)signum;
    g_running = false;
}

static void print_usage(const char *prog_name) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  -f <Hz>     Center frequency in Hz (default: %llu)\n"
        "  -s <rate>   HackRF sample rate in Hz (default: %u)\n"
        "  -l <gain>   LNA gain 0-40 dB (default: %u)\n"
        "  -v <gain>   VGA gain 0-62 dB (default: %u)\n"
        "  -a <rate>   Output audio sample rate (default: %u)\n"
        "  -h          Show this help message\n\n"
        "Example:\n"
        "  %s -f 101100000 | ffplay -f s16le -ar 48000 -\n",
        prog_name,
        DEFAULT_FREQ_HZ,
        DEFAULT_SAMPLE_RATE,
        DEFAULT_LNA_GAIN,
        DEFAULT_VGA_GAIN,
        DEFAULT_AUDIO_RATE,
        prog_name
    );
}

int main(int argc, char *argv[]) {
    hackrf_config_t hw_cfg = {
        .freq_hz = DEFAULT_FREQ_HZ,
        .sample_rate_hz = DEFAULT_SAMPLE_RATE,
        .lna_gain = DEFAULT_LNA_GAIN,
        .vga_gain = DEFAULT_VGA_GAIN,
        .amp_enable = false,
    };

    dsp_config_t dsp_cfg = {
        .input_sample_rate = DEFAULT_SAMPLE_RATE,
        .output_sample_rate = DEFAULT_AUDIO_RATE,
        .freq_offset_hz = 250000, // 250 kHz offset to avoid DC spike
        .deemphasis_us = 50.0f,   // 50 us is European standard
    };

    int opt;
    while ((opt = getopt(argc, argv, "f:s:l:v:a:h")) != -1) {
        switch (opt) {
            case 'f': hw_cfg.freq_hz = (uint64_t)strtoull(optarg, NULL, 10); break;
            case 's': hw_cfg.sample_rate_hz = (uint32_t)strtoul(optarg, NULL, 10); break;
            case 'l': hw_cfg.lna_gain = (uint32_t)strtoul(optarg, NULL, 10); break;
            case 'v': hw_cfg.vga_gain = (uint32_t)strtoul(optarg, NULL, 10); break;
            case 'a': dsp_cfg.output_sample_rate = (uint32_t)strtoul(optarg, NULL, 10); break;
            case 'h': default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    dsp_cfg.input_sample_rate = hw_cfg.sample_rate_hz;

    /* Register signal handler for clean Ctrl+C shutdown */
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    fprintf(stderr, "[fm-analyzer] Initializing receiver...\n");
    fprintf(stderr, "[fm-analyzer] Target Frequency : %llu Hz\n", (unsigned long long)hw_cfg.freq_hz);
    fprintf(stderr, "[fm-analyzer] Sample Rate      : %u Hz\n", hw_cfg.sample_rate_hz);
    fprintf(stderr, "[fm-analyzer] Audio Rate       : %u Hz\n", dsp_cfg.output_sample_rate);

    /* Allocate Ring Buffer */
    ring_buffer_t *rb = ring_buffer_create(RING_BUFFER_SIZE);
    if (!rb) {
        fprintf(stderr, "[fm-analyzer] Error: Failed to allocate ring buffer\n");
        return 1;
    }

    /* Initialize DSP Pipeline */
    dsp_pipeline_t *dsp = dsp_create(&dsp_cfg);
    if (!dsp) {
        fprintf(stderr, "[fm-analyzer] Error: Failed to create DSP pipeline\n");
        ring_buffer_destroy(rb);
        return 1;
    }

    /**
     * https://github.com/greatscottgadgets/hackrf/blob/main/host/libhackrf/src/hackrf.h#L277-L285
     * 
     * Steps for starting an RX or TX operation:
     * - initialize libhackrf
     * - open device
     * - setup device (frequency, samplerate, gain, etc.)
     * - setup callbacks, start operation (`hackrf_start_*`)
     * - the main program should go to sleep
     * - when done, the transfer callback should return non-zero value, and signal the main thread to stop
     * - stop operation via `hackrf_stop_*`
     * - close device, exit library, etc.
     */

    /* Initialize HackRF Hardware */
    // Shift hardware frequency so software mixer centers the target station
    hw_cfg.freq_hz += dsp_cfg.freq_offset_hz;
    hackrf_rx_context_t *rx_ctx = hackrf_rx_init(&hw_cfg, rb);
    if (!rx_ctx) {
        fprintf(stderr, "[fm-analyzer] Warning: Could not open HackRF device (or dummy mode)\n");
    }

    /* Start Hardware Streaming */
    if (rx_ctx && hackrf_rx_start(rx_ctx) != 0) {
        fprintf(stderr, "[fm-analyzer] Error: Failed to start HackRF RX stream\n");
    } else {
        fprintf(stderr, "[fm-analyzer] Streaming started. Press Ctrl+C to stop.\n");
    }

    /* Init buffers for batch processing */
    uint8_t *raw_iq_chunk = (uint8_t*)malloc(CHUNK_READ_SIZE);
    int16_t *pcm_out = (int16_t*)malloc(CHUNK_READ_SIZE * sizeof(int16_t));

    /* Main Consumer / DSP Loop */
    while (g_running) {
        size_t available = ring_buffer_available_read(rb);
        if (available >= CHUNK_READ_SIZE) {
            size_t bytes_read = ring_buffer_read(rb, raw_iq_chunk, CHUNK_READ_SIZE);
            if (bytes_read > 0) {
                size_t pcm_samples = dsp_process_iq_to_pcm(dsp, (int8_t*)raw_iq_chunk, bytes_read, pcm_out, CHUNK_READ_SIZE);
                if (pcm_samples > 0) {
                    /* Write raw 16-bit PCM bytes to stdout */
                    fwrite(pcm_out, sizeof(int16_t), pcm_samples, stdout);
                    fflush(stdout);
                }
            }
        } else {
            // Buffer size is less than a processing chunk, wait for new data
            usleep(1000); // 1 ms
        }
    }

    fprintf(stderr, "\n[fm-analyzer] Shutting down...\n");

    if (rx_ctx) {
        hackrf_rx_cleanup(rx_ctx);
    }
    dsp_destroy(dsp);
    ring_buffer_destroy(rb);
    free(raw_iq_chunk);
    free(pcm_out);

    fprintf(stderr, "[fm-analyzer] Clean shutdown complete.\n");
    return 0;
}
