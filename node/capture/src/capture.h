/**
 * @file capture.h
 * @brief PIO capture: one-shot runs into memory, and continuous streaming to a sink.
 */
#ifndef PI5LA_CAPTURE_H
#define PI5LA_CAPTURE_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "piolib.h"
#include "progbuild.h"
#include "util.h"

/** Number of GPIOs the RP1 PIO drives: GPIO 0..27. */
#define CAPTURE_GPIO_COUNT 28
/** Periods in the cyclic DMA ring (rp1-pio allows at most 4). */
#define CAPTURE_DMA_BUF_COUNT 4
/** Default DMA period size in bytes, for --stream and the tools. */
#define CAPTURE_DMA_BUF_DEFAULT (1u << 20)
/**
 * Default DMA period size in bytes for file capture.
 *
 * Writing to disk needs more slack than streaming does: an SD card that
 * averages 14 MB/s still stalls for seconds at a time, and a 4 x 1 MiB ring
 * holds only ~2 s at 2 MB/s (measured 2026-09-21: it overflowed after 7 s,
 * while 4 MiB periods ran 30 s clean). File capture therefore defaults to a
 * 4 x 4 MiB ring; --stream and the tools keep the smaller one.
 */
#define CAPTURE_DMA_BUF_RECORD (4u << 20)

/** How samples are moved from the PIO FIFO to memory. */
typedef enum {
    /** Cyclic DMA (the default). */
    CAPTURE_DMA = 0,
    /** Word-by-word FIFO reads; the Milestone 2 baseline. */
    CAPTURE_NAIVE,
} capture_mode_t;

/** What to capture and how. */
typedef struct {
    /** GPIO of channel 0. */
    unsigned base_gpio;
    /** Channel count: 1, 2, 4 or 8. */
    unsigned channels;
    /** Clock divider: sample rate = 200 MHz / div. */
    clkdiv_t div;
    /** Samples to capture (capture_run() only). */
    uint64_t samples;
    /** DMA or naive FIFO reads. */
    capture_mode_t mode;
    /** Trigger condition, or TRIG_NONE. */
    trig_type_t trig;
    /** Trigger channel, relative to base_gpio. */
    unsigned trig_pin;
    /** Cyclic DMA period size in bytes, a multiple of 4. */
    uint32_t dma_buf_bytes;
    /** Optional: set from a signal handler to stop the capture. */
    volatile sig_atomic_t *stop;
} capture_config_t;

/** Result of capture_run(). Free it with capture_result_free(). */
typedef struct {
    /** Packed FIFO words, as delivered. */
    uint32_t *words;
    /** Number of words in `words`. */
    size_t n_words;
    /** Actual sample rate, in Hz. */
    double rate_hz;

    /** Packed bytes received. */
    uint64_t bytes;
    /** Seconds from SM enable to the last data. */
    double total_s;
    /** Bytes received after the first buffer/word. */
    uint64_t steady_bytes;
    /** Seconds from the first buffer/word to the last data. */
    double steady_s;

    /* Drop detection */
    /** FDEBUG RXSTALL seen: per period with DMA; 0 or 1 at the end with naive reads. */
    uint64_t stall_events;
    /** True when est_lost_samples could be computed. */
    bool est_valid;
    /** Estimated lost samples: rate * steady_s - samples in steady_bytes. */
    int64_t est_lost_samples;

    /** DMA ring wrapped over unread data; capture cut short. */
    bool overflow;
    /** Stopped early through cfg->stop. */
    bool interrupted;
} capture_result_t;

/**
 * Capture cfg->samples samples into memory.
 *
 * @param pio PIO instance to use.
 * @param cfg What to capture.
 * @param res Filled with the samples and statistics.
 * @return 0 on success, -1 on error (message on stderr).
 */
int capture_run(PIO pio, const capture_config_t *cfg, capture_result_t *res);

/**
 * Called with each completed DMA period of packed FIFO words, in order.
 *
 * @param words   Packed FIFO words of one period.
 * @param n_words Number of words.
 * @param ctx     The caller's context pointer, as given to capture_stream().
 * @return 0 to keep streaming, non-zero to stop.
 */
typedef int (*capture_sink_t)(const uint32_t *words, size_t n_words, void *ctx);

/** Result of capture_stream(). */
typedef struct {
    /** Actual sample rate, in Hz. */
    double rate_hz;
    /** Packed bytes delivered to the sink. */
    uint64_t bytes;
    /** Seconds from SM enable to the last period. */
    double total_s;
    /** Periods with RXSTALL set. */
    uint64_t stall_events;
    /** DMA ring wrapped: sink too slow, stream stopped. */
    bool overflow;
    /** Sink returned non-zero. */
    bool sink_stopped;
    /** cfg->stop was set. */
    bool interrupted;

    /**
     * Wall clock at sample 0, in nanoseconds since the epoch.
     *
     * Filled in *during* the run so the sink can read it: the caller holds
     * this struct while capture_stream() is working. It is taken the instant
     * the state machine is enabled, so it marks sample 0 -- not the setup that
     * precedes it.
     */
    uint64_t start_realtime_ns;
    /**
     * Wall clock when the latest period landed, in nanoseconds since the epoch.
     *
     * Refreshed as each period lands, which is what lets a long run follow
     * the NTP-disciplined clock instead of drifting with the PIO crystal.
     */
    uint64_t period_realtime_ns;
} capture_stream_result_t;

/**
 * Capture continuously (cyclic DMA, no gaps).
 *
 * Runs until cfg->stop is set, the sink asks to stop, or the ring overflows.
 * cfg->samples is ignored.
 *
 * @param pio  PIO instance to use.
 * @param cfg  What to capture.
 * @param sink Called with each completed period.
 * @param ctx  Passed to the sink unchanged.
 * @param res  Filled with statistics, partly while the run is in progress.
 * @return 0 on a clean stop, -1 on error (message on stderr).
 */
int capture_stream(PIO pio, const capture_config_t *cfg, capture_sink_t sink, void *ctx,
                   capture_stream_result_t *res);

/**
 * Free the samples held by a capture_run() result.
 *
 * @param res Result to free; its word buffer is released and cleared.
 */
void capture_result_free(capture_result_t *res);

/**
 * Checks progbuild output against the pioasm-generated la8 / la8_trig programs.
 *
 * @return True if every built program matches.
 */
bool capture_selftest_programs(void);

#endif
