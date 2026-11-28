/**
 * @file burst.h
 * @brief Burst state machine for monitor mode.
 *
 * Sampling never stops: every DMA period is scanned (see idle.h), and this
 * decides what to write. While the bus is quiet nothing is written, so an idle
 * hour costs no disk. When a change appears, the burst starts `pre` samples
 * before it, taken from the caller's pre-roll ring, and ends `post` samples
 * after the last change once `gap` samples have passed with no change.
 *
 * @code
 *   waiting --change--> recording --gap with no change--> end (keep up to post)
 * @endcode
 *
 * All positions are absolute sample numbers since the capture began. Pure
 * logic: no I/O, no allocation.
 */
#ifndef PI5LA_BURST_H
#define PI5LA_BURST_H

#include <stdbool.h>
#include <stdint.h>

/** State of the burst state machine. Set up with burst_init(). */
typedef struct {
    /** Lead-in kept before the first change, in samples. */
    uint64_t pre_samples;
    /** Tail kept after the last change, in samples. */
    uint64_t post_samples;
    /** Quiet time that ends a burst, in samples. */
    uint64_t gap_samples;
    /** True while a burst is open. */
    bool recording;
    /** Samples fed so far. */
    uint64_t total;
    /** Absolute index just past the last change seen. */
    uint64_t last_change;
    /** Absolute index where the open burst starts. */
    uint64_t burst_start;
    /** Absolute index of the change that opened the burst. */
    uint64_t first_edge;
} burst_t;

/** What the caller should do with the period it just fed in. */
typedef struct {
    /** Open a burst starting at `start`. */
    bool begin;
    /** Absolute index where the new burst starts (valid with `begin`). */
    uint64_t start;
    /**
     * The change itself, before `pre` is taken off `start`: when the traffic
     * actually arrived.
     */
    uint64_t first_edge;
    /** Append samples [write_from, write_to). */
    bool write;
    /** First absolute sample to append (valid with `write`). */
    uint64_t write_from;
    /** Absolute sample just past the last one to append (valid with `write`). */
    uint64_t write_to;
    /** Close the burst, keeping samples up to `end_at`. */
    bool end;
    /** Absolute sample the closed burst is cut at (valid with `end`). */
    uint64_t end_at;
} burst_action_t;

/**
 * Reset the state machine to "waiting" with the given lengths.
 *
 * @param b            State to initialise.
 * @param pre_samples  Lead-in kept before the first change, in samples.
 * @param post_samples Tail kept after the last change, in samples.
 * @param gap_samples  Quiet time that ends a burst, in samples.
 */
void burst_init(burst_t *b, uint64_t pre_samples, uint64_t post_samples, uint64_t gap_samples);

/**
 * Feed one period and get back what to do with it.
 *
 * The period is n_samples long, with the first and last change inside it at
 * `first_change` / `last_change` (sample offsets within the period, only read
 * when `changed`).
 *
 * @param b            State machine.
 * @param n_samples    Length of the period, in samples.
 * @param changed      True if the period contains a change.
 * @param first_change Offset of the first change within the period.
 * @param last_change  Offset just past the last change within the period.
 * @param out          Filled with the actions to take; zeroed first.
 */
void burst_feed(burst_t *b, uint64_t n_samples, bool changed, uint64_t first_change,
                uint64_t last_change, burst_action_t *out);

/**
 * Capture stopped: close any open burst.
 *
 * @param b      State machine.
 * @param end_at Set to the absolute sample to cut the burst at, when one is open.
 * @return True if a burst is still open; *end_at then says how much of it to keep.
 */
bool burst_flush(burst_t *b, uint64_t *end_at);

#endif
