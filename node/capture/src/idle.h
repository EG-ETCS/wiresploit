/**
 * @file idle.h
 * @brief Activity detection on packed FIFO words, for --idle-stop and monitor mode.
 *
 * A word is idle when every sample in it equals the level of the last sample
 * seen before it, on every channel. Any other word contains a change. The
 * scan runs on every DMA period at up to 12.5 M words/s, so an idle word costs
 * one comparison.
 */
#ifndef PI5LA_IDLE_H
#define PI5LA_IDLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Scanner state, carried from one period to the next. Set up with idle_scan_init(). */
typedef struct {
    /** Channel count: 1, 2, 4 or 8. */
    unsigned channels;
    /** Samples per word. */
    unsigned spw;
    /** True once a first sample has set `level`. */
    bool have_level;
    /** Channel levels of the last sample seen. */
    uint8_t level;
    /** Word holding a level in every slot. */
    uint32_t rep[256];
    /** Bit of channel k in the last slot. */
    uint8_t last_pos[8];
} idle_scan_t;

/**
 * Prepare a scanner for a channel count.
 *
 * @param s        Scanner to initialise.
 * @param channels Channel count: 1, 2, 4 or 8.
 */
void idle_scan_init(idle_scan_t *s, unsigned channels);

/**
 * Scans n words in order, continuing from the previous call.
 *
 * @param s          Scanner state.
 * @param words      Packed FIFO words.
 * @param n          Number of words.
 * @param change_end Set to the index just past the last word with a change.
 * @return True if any word contains a change; *change_end is then the index
 *         just past the last such word. False otherwise, leaving *change_end
 *         alone.
 */
bool idle_scan_words(idle_scan_t *s, const uint32_t *words, size_t n, size_t *change_end);

/**
 * As idle_scan_words, but *change_begin also receives the index of the first
 * word containing a change.
 *
 * Burst capture needs it to know where in the period the traffic started, so
 * the lead-in can be measured from there.
 *
 * @param s            Scanner state.
 * @param words        Packed FIFO words.
 * @param n            Number of words.
 * @param change_begin Set to the index of the first word with a change.
 * @param change_end   Set to the index just past the last word with a change.
 * @return True if any word contains a change. False otherwise, leaving both
 *         outputs alone.
 */
bool idle_scan_range(idle_scan_t *s, const uint32_t *words, size_t n, size_t *change_begin,
                     size_t *change_end);

/**
 * Parse a duration: a number with an optional ms / s / m suffix.
 *
 * For example "500ms", "10s", "1.5m"; no suffix means seconds. Must be > 0.
 *
 * @param str     Text to parse.
 * @param seconds Receives the duration, in seconds.
 * @return True on success, false on malformed input or a value <= 0.
 */
bool parse_duration(const char *str, double *seconds);

#endif
