#include <math.h>

#include "check.h"
#include "util.h"

int main(void)
{
    double v;
    uint64_t u;
    clkdiv_t d;
    clocksync_t cs;
    uint64_t ns;

    CHECK(parse_si("10M", &v) && v == 10e6);
    CHECK(parse_si("100k", &v) && v == 100e3);
    CHECK(parse_si("115.2k", &v) && fabs(v - 115200) < 1e-6);
    CHECK(parse_si("4000", &v) && v == 4000);
    CHECK(!parse_si("", &v));
    CHECK(parse_si("4G", &v) && v == 4e9);
    CHECK(!parse_si("10T", &v));
    CHECK(!parse_si("-5", &v));
    CHECK(!parse_si("5kk", &v));

    CHECK(parse_si_u64("1M", &u) && u == 1000000);
    CHECK(!parse_si_u64("1.5", &u));

    CHECK(clkdiv_for_hz(10e6, &d) && d.div_int == 20 && d.div_frac == 0);
    CHECK(clkdiv_is_integer(d) && clkdiv_hz(d) == 10e6);
    CHECK(clkdiv_for_hz(200e6, &d) && d.div_int == 1);
    CHECK(!clkdiv_for_hz(400e6, &d));
    CHECK(!clkdiv_for_hz(1000, &d));
    CHECK(clkdiv_for_hz(3e6, &d) && d.div_int == 66 && d.div_frac == 171);
    CHECK(!clkdiv_is_integer(d));

    /* Wall clock: nanosecond resolution, and it moves forwards. */
    ns = realtime_ns();
    CHECK(ns > 1700000000000000000ull);     /* comfortably after 2023 */
    CHECK(realtime_ns() >= ns);

    /* Clock discipline: whatever this machine's NTP state is, the read itself
       must work and report a bound rather than nonsense. */
    CHECK(clocksync_read(&cs));
    CHECK(cs.max_error_us >= 0);

    CHECK_DONE();
}
