/**
 * @file analyze.h
 * @brief Run-length checks on unpacked samples, used by the loopback tools.
 */
#ifndef PI5LA_ANALYZE_H
#define PI5LA_ANALYZE_H

#include <stddef.h>
#include <stdint.h>

/** Result of analyze_runs(): run lengths of one channel. */
typedef struct {
    /** Complete runs examined (first and last run excluded). */
    uint64_t runs;
    /** Runs whose length != expected. */
    uint64_t violations;
    /** Shortest complete run, in samples. */
    uint64_t min_run;
    /** Longest complete run, in samples. */
    uint64_t max_run;
    /** Level changes seen on the channel. */
    uint64_t transitions;
} run_stats_t;

/**
 * Run-length check of one channel in unpacked samples (channel N in bit N).
 *
 * The first and last runs are partial and are not counted. With
 * expected == 0 only transitions/min/max are gathered.
 *
 * @param samples  Unpacked samples, one byte each.
 * @param n        Number of samples.
 * @param ch       Channel to check, 0..7.
 * @param expected Expected run length in samples, or 0 to skip the check.
 * @param out      Filled with the statistics.
 */
void analyze_runs(const uint8_t *samples, size_t n, unsigned ch, uint64_t expected,
                  run_stats_t *out);

#endif
