#ifndef PI5LA_UTIL_H
#define PI5LA_UTIL_H

#include <stdbool.h>
#include <stdint.h>

/* RP1 PIO state machine clock (piolib pio_rp1.c: rp1_clock_get_hz). */
#define PI5LA_PIO_CLOCK_HZ 200000000.0

/* Clock divider in piolib's int + 1/256 frac form. div_int == 0 is not used. */
typedef struct {
    uint16_t div_int;
    uint8_t div_frac;
} clkdiv_t;

/*
 * Parse a number with an optional k / M / G suffix ("10M", "115.2k", "4G").
 * Returns false on malformed input or negative values.
 */
bool parse_si(const char *s, double *out);

/* As parse_si, but the result must be a non-negative integer. */
bool parse_si_u64(const char *s, uint64_t *out);

/* Divider for a PIO SM running at freq_hz SM cycles per second. */
bool clkdiv_for_hz(double freq_hz, clkdiv_t *out);

/* SM cycles per second for a divider. */
double clkdiv_hz(clkdiv_t div);

/* Divider value as a real number. */
double clkdiv_value(clkdiv_t div);

/* True when div_frac == 0 (no fractional-divider jitter). */
bool clkdiv_is_integer(clkdiv_t div);

double monotonic_s(void);

/* Wall-clock time as integer nanoseconds since the epoch (CLOCK_REALTIME,
   which is what NTP disciplines). A double holds only ~240 ns of resolution at
   today's epoch values, so anything that has to stay exact uses this. */
uint64_t realtime_ns(void);

/*
 * How much the system clock can be trusted, from adjtimex(2).
 *
 * `synchronised` is false when the kernel's STA_UNSYNC flag is set, i.e. no NTP
 * daemon is disciplining the clock; `max_error_us` is the kernel's own bound on
 * how far off it might be. Recorded with every burst so a consumer can tell a
 * locked clock from a free-running one.
 */
typedef struct {
    bool synchronised;
    long max_error_us;
} clocksync_t;

/* Fills *out. Returns false only if the kernel call itself failed. */
bool clocksync_read(clocksync_t *out);

#endif
