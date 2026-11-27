#ifndef PI5LA_GENERATOR_H
#define PI5LA_GENERATOR_H

#include <stdbool.h>
#include <stdint.h>

#include "piolib.h"
#include "util.h"

#define GENERATOR_DEFAULT_GPIO 20

typedef struct {
    PIO pio;
    int sm;
    uint offset;
    const pio_program_t *program;
    uint gpio;
    double actual_hz;
    bool slow; /* sqwave_slow (delay loop) instead of sqwave */
} generator_t;

/*
 * Square wave on `gpio` at freq_hz. Uses sqwave.pio when the clock divider can
 * reach the frequency (>= ~1.53 kHz), otherwise sqwave_slow.pio at divider 1.
 * Returns 0 on success, -1 on error (message on stderr).
 */
int generator_start(PIO pio, uint gpio, double freq_hz, generator_t *g);

/*
 * sqwave.pio with an explicit divider: each output level lasts exactly
 * `div` PIO clock ticks. Used when the generator must be locked to a capture
 * divider (integer multiples give an exact number of samples per half period).
 */
int generator_start_div(PIO pio, uint gpio, clkdiv_t div, generator_t *g);

void generator_stop(generator_t *g);

#endif
