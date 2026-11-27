#ifndef PI5LA_ANALYZE_H
#define PI5LA_ANALYZE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t runs;       /* complete runs examined (first and last run excluded) */
    uint64_t violations; /* runs whose length != expected */
    uint64_t min_run;
    uint64_t max_run;
    uint64_t transitions;
} run_stats_t;

/*
 * Run-length check of one channel in unpacked samples (channel N in bit N).
 * The first and last runs are partial and are not counted. With
 * expected == 0 only transitions/min/max are gathered.
 */
void analyze_runs(const uint8_t *samples, size_t n, unsigned ch, uint64_t expected,
                  run_stats_t *out);

#endif
