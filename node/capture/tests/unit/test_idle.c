#include <math.h>

#include "check.h"
#include "idle.h"

int main(void)
{
    idle_scan_t s;
    size_t end = 99;
    double d;

    /* 8 channels, idle-high GPIO 2..8 and low GPIO 9: level 0x7f in every slot. */
    {
        const uint32_t idle[3] = { 0x7f7f7f7f, 0x7f7f7f7f, 0x7f7f7f7f };
        const uint32_t busy[4] = { 0x7f7f7f7f, 0x7e7e7e7f, 0x7e7e7e7e, 0x7e7e7e7e };

        idle_scan_init(&s, 8);
        CHECK(!idle_scan_words(&s, idle, 3, &end) && end == 99);
        /* Channel 0 falls inside word 1; word 2 holds the new level throughout. */
        CHECK(idle_scan_words(&s, busy, 4, &end) && end == 2);
        /* The level carries over between calls. */
        CHECK(!idle_scan_words(&s, busy + 2, 2, &end));
        /* A change exactly at a word boundary counts. */
        CHECK(idle_scan_words(&s, idle, 1, &end) && end == 1);
    }

    /* A non-constant first word is a change. */
    {
        const uint32_t w[2] = { 0x0000ff00, 0x00000000 };

        idle_scan_init(&s, 8);
        CHECK(idle_scan_words(&s, w, 2, &end) && end == 1);
    }

    /* 1 channel: one bit per sample. */
    {
        const uint32_t hi[2] = { 0xffffffff, 0xffffffff };
        const uint32_t edge[2] = { 0x7fffffff, 0x00000000 };

        idle_scan_init(&s, 1);
        CHECK(!idle_scan_words(&s, hi, 2, &end));
        CHECK(idle_scan_words(&s, edge, 2, &end) && end == 1);
        CHECK(!idle_scan_words(&s, edge + 1, 1, &end));
    }

    /* 4 channels: rep of level 0xa is 0xaaaaaaaa. */
    {
        const uint32_t w[2] = { 0xaaaaaaaa, 0xaaaaaaa5 };

        idle_scan_init(&s, 4);
        CHECK(s.rep[0xa] == 0xaaaaaaaa);
        CHECK(idle_scan_words(&s, w, 2, &end) && end == 2 && s.level == 0xa);
    }

    CHECK(parse_duration("10s", &d) && d == 10);
    CHECK(parse_duration("10", &d) && d == 10);
    CHECK(parse_duration("500ms", &d) && fabs(d - 0.5) < 1e-12);
    CHECK(parse_duration("1.5m", &d) && d == 90);
    CHECK(!parse_duration("0s", &d));
    CHECK(!parse_duration("", &d));
    CHECK(!parse_duration("5h", &d));
    CHECK(!parse_duration("-1s", &d));
    CHECK(!parse_duration("s", &d));

    CHECK_DONE();
}
