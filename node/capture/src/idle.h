#ifndef PI5LA_IDLE_H
#define PI5LA_IDLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Activity detection on packed FIFO words, for --idle-stop.
 *
 * A word is idle when every sample in it equals the level of the last sample
 * seen before it, on every channel. Any other word contains a change. The
 * scan runs on every DMA period at up to 12.5 M words/s, so an idle word costs
 * one comparison.
 */
typedef struct {
    unsigned channels;
    unsigned spw;               /* samples per word */
    bool have_level;
    uint8_t level;              /* channel levels of the last sample seen */
    uint32_t rep[256];          /* word holding a level in every slot */
    uint8_t last_pos[8];        /* bit of channel k in the last slot */
} idle_scan_t;

void idle_scan_init(idle_scan_t *s, unsigned channels);

/*
 * Scans n words in order, continuing from the previous call. Returns true if
 * any word contains a change; *change_end is then the index just past the
 * last such word. Returns false and leaves *change_end alone otherwise.
 */
bool idle_scan_words(idle_scan_t *s, const uint32_t *words, size_t n, size_t *change_end);

/*
 * As idle_scan_words, but *change_begin also receives the index of the first
 * word containing a change. Burst capture needs it to know where in the period
 * the traffic started, so the lead-in can be measured from there.
 */
bool idle_scan_range(idle_scan_t *s, const uint32_t *words, size_t n, size_t *change_begin,
                     size_t *change_end);

/*
 * Parse a duration: a number with an optional ms / s / m suffix ("500ms",
 * "10s", "1.5m"); no suffix means seconds. Must be > 0.
 */
bool parse_duration(const char *str, double *seconds);

#endif
