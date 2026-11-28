#include "analyze.h"
#include "check.h"

static size_t square(uint8_t *buf, size_t n, unsigned ch, size_t phase, size_t half)
{
    size_t i;

    for (i = 0; i < n; i++)
        buf[i] = (((i + phase) / half) % 2) ? (uint8_t)(1u << ch) : 0;
    return n;
}

int main(void)
{
    uint8_t buf[1000];
    run_stats_t st;

    square(buf, sizeof(buf), 3, 17, 50);
    analyze_runs(buf, sizeof(buf), 3, 50, &st);
    CHECK(st.runs > 0);
    CHECK(st.violations == 0);
    CHECK(st.min_run == 50 && st.max_run == 50);

    /* Other channels are constant. */
    analyze_runs(buf, sizeof(buf), 0, 0, &st);
    CHECK(st.transitions == 0 && st.runs == 0);

    /* A stall stretches one run. */
    square(buf, sizeof(buf), 0, 0, 50);
    buf[249] = buf[250] = buf[251] = buf[248]; /* run 200..249 grows, 250..299 shrinks */
    analyze_runs(buf, sizeof(buf), 0, 50, &st);
    CHECK(st.violations == 2);

    /* Partial first/last runs are ignored. */
    square(buf, 120, 0, 25, 50); /* 25 | 50 | 45 */
    analyze_runs(buf, 120, 0, 50, &st);
    CHECK(st.runs == 1 && st.violations == 0);

    CHECK_DONE();
}
