#include "burst.h"
#include "check.h"

/* pre = 100, post = 200, gap = 1000 samples; periods of 1000 samples. */
static void init(burst_t *b)
{
    burst_init(b, 100, 200, 1000);
}

int main(void)
{
    burst_t b;
    burst_action_t a;
    uint64_t end_at;

    /* Quiet periods write nothing at all: this is what keeps an idle hour free. */
    init(&b);
    burst_feed(&b, 1000, false, 0, 0, &a);
    CHECK(!a.begin && !a.write && !a.end);
    CHECK(a.first_edge == 0);                     /* only meaningful when begin */
    burst_feed(&b, 1000, false, 0, 0, &a);
    CHECK(!a.write && b.total == 2000);

    /* A change at 500 in the third period starts a burst 100 samples earlier. */
    burst_feed(&b, 1000, true, 500, 600, &a);
    CHECK(a.begin && a.start == 2400);            /* 2500 - pre */
    CHECK(a.first_edge == 2500 && b.first_edge == 2500);   /* the arrival itself */
    CHECK(a.write && a.write_from == 2400 && a.write_to == 3000);
    CHECK(!a.end);                                /* only 400 quiet so far */

    /* Traffic continues: keep writing, no end. */
    burst_feed(&b, 1000, true, 10, 900, &a);
    CHECK(!a.begin && a.write && a.write_from == 3000 && a.write_to == 4000 && !a.end);

    /* Quiet again: 1100 samples since the last change (3900) passes the gap,
       so the burst closes, keeping post samples of tail. */
    burst_feed(&b, 1000, false, 0, 0, &a);
    CHECK(a.write && a.write_from == 4000 && a.write_to == 5000);
    CHECK(a.end && a.end_at == 4100 && b.last_change == 3900);
    CHECK(!b.recording);

    /* The gap check runs once per period, so a burst closes on the period after
       the line falls quiet: keep last change + post. */
    init(&b);
    burst_feed(&b, 1000, true, 100, 200, &a);
    CHECK(a.begin && a.start == 0);               /* 100 - pre, clamped to the start */
    CHECK(a.first_edge == 100);                   /* clamping `start` never moves it */
    burst_feed(&b, 2000, false, 0, 0, &a);
    CHECK(a.end && a.end_at == 400);              /* 200 + post */
    CHECK(a.write && a.write_from == 1000 && a.write_to == 3000);
    CHECK(!b.recording);

    /* The next change starts a second burst. */
    burst_feed(&b, 1000, true, 500, 500, &a);
    CHECK(a.begin && a.start == 3400 && a.write_from == 3400);
    CHECK(a.first_edge == 3500);

    /* A short burst followed by silence inside one period ends in that period. */
    init(&b);
    burst_feed(&b, 5000, true, 100, 200, &a);
    CHECK(a.begin && a.start == 0);               /* pre clamped at the start of capture */
    CHECK(a.first_edge == 100);
    CHECK(a.end && a.end_at == 400);
    CHECK(a.write && a.write_from == 0 && a.write_to == 5000);

    /* end_at never runs past what was captured. */
    init(&b);
    burst_feed(&b, 1000, true, 0, 950, &a);
    CHECK(!a.end);
    CHECK(burst_flush(&b, &end_at) && end_at == 1000);   /* 950 + 200 clamped to 1000 */
    CHECK(!burst_flush(&b, &end_at));                    /* nothing open now */

    /* Flushing while quiet reports no open burst. */
    init(&b);
    burst_feed(&b, 1000, false, 0, 0, &a);
    CHECK(!burst_flush(&b, &end_at));

    CHECK_DONE();
}
