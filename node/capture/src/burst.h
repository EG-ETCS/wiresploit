#ifndef PI5LA_BURST_H
#define PI5LA_BURST_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Burst state machine for monitor mode.
 *
 * Sampling never stops: every DMA period is scanned (see idle.h), and this
 * decides what to write. While the bus is quiet nothing is written, so an idle
 * hour costs no disk. When a change appears, the burst starts `pre` samples
 * before it, taken from the caller's pre-roll ring, and ends `post` samples
 * after the last change once `gap` samples have passed with no change.
 *
 *   waiting --change--> recording --gap with no change--> end (keep up to post)
 *
 * All positions are absolute sample numbers since the capture began. Pure
 * logic: no I/O, no allocation.
 */
typedef struct {
    uint64_t pre_samples;
    uint64_t post_samples;
    uint64_t gap_samples;
    bool recording;
    uint64_t total;         /* samples fed so far */
    uint64_t last_change;   /* absolute index just past the last change seen */
    uint64_t burst_start;   /* absolute index where the open burst starts */
    uint64_t first_edge;    /* absolute index of the change that opened the burst */
} burst_t;

/* What the caller should do with the period it just fed in. */
typedef struct {
    bool begin;             /* open a burst starting at `start` */
    uint64_t start;
    uint64_t first_edge;    /* the change itself, before `pre` is taken off
                               `start`: when the traffic actually arrived */
    bool write;             /* append samples [write_from, write_to) */
    uint64_t write_from;
    uint64_t write_to;
    bool end;               /* close the burst, keeping samples up to `end_at` */
    uint64_t end_at;
} burst_action_t;

void burst_init(burst_t *b, uint64_t pre_samples, uint64_t post_samples, uint64_t gap_samples);

/*
 * Feed one period: n_samples long, with the first and last change inside it at
 * `first_change` / `last_change` (sample offsets within the period, only read
 * when `changed`).
 */
void burst_feed(burst_t *b, uint64_t n_samples, bool changed, uint64_t first_change,
                uint64_t last_change, burst_action_t *out);

/*
 * Capture stopped. Returns true if a burst is still open; *end_at then says
 * how much of it to keep.
 */
bool burst_flush(burst_t *b, uint64_t *end_at);

#endif
