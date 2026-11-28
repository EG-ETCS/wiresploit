#include "analyze.h"

#include <string.h>

void analyze_runs(const uint8_t *samples, size_t n, unsigned ch, uint64_t expected,
                  run_stats_t *out)
{
    size_t i, run_start = 0;
    int seen_first_edge = 0;

    memset(out, 0, sizeof(*out));
    if (n == 0)
        return;

    for (i = 1; i < n; i++) {
        unsigned prev = (samples[i - 1] >> ch) & 1u;
        unsigned cur = (samples[i] >> ch) & 1u;

        if (cur == prev)
            continue;
        out->transitions++;
        if (seen_first_edge) {
            uint64_t len = i - run_start;

            if (out->runs == 0 || len < out->min_run)
                out->min_run = len;
            if (len > out->max_run)
                out->max_run = len;
            out->runs++;
            if (expected && len != expected)
                out->violations++;
        }
        seen_first_edge = 1;
        run_start = i;
    }
}
