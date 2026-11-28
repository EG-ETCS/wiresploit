/**
 * @file generator.h
 * @brief Square-wave generator on a spare PIO state machine, for loopback tests.
 */
#ifndef PI5LA_GENERATOR_H
#define PI5LA_GENERATOR_H

#include <stdbool.h>
#include <stdint.h>

#include "piolib.h"
#include "util.h"

/** GPIO the generator drives by default (pin 38). */
#define GENERATOR_DEFAULT_GPIO 20

/** A running generator. Stop it with generator_stop(). */
typedef struct {
    /** PIO instance the program runs on. */
    PIO pio;
    /** Claimed state machine, or -1 once stopped. */
    int sm;
    /** Where the program was loaded in instruction memory. */
    uint offset;
    /** The loaded program. */
    const pio_program_t *program;
    /** Output GPIO. */
    uint gpio;
    /** Frequency actually produced, in Hz. */
    double actual_hz;
    /** sqwave_slow (delay loop) instead of sqwave. */
    bool slow;
} generator_t;

/**
 * Square wave on `gpio` at freq_hz.
 *
 * Uses sqwave.pio when the clock divider can reach the frequency
 * (>= ~1.53 kHz), otherwise sqwave_slow.pio at divider 1.
 *
 * @param pio     PIO instance to use.
 * @param gpio    Output GPIO.
 * @param freq_hz Requested frequency, in Hz.
 * @param g       Filled with the running generator.
 * @return 0 on success, -1 on error (message on stderr).
 */
int generator_start(PIO pio, uint gpio, double freq_hz, generator_t *g);

/**
 * sqwave.pio with an explicit divider.
 *
 * Each output level lasts exactly `div` PIO clock ticks. Used when the
 * generator must be locked to a capture divider (integer multiples give an
 * exact number of samples per half period).
 *
 * @param pio  PIO instance to use.
 * @param gpio Output GPIO.
 * @param div  Clock divider.
 * @param g    Filled with the running generator.
 * @return 0 on success, -1 on error (message on stderr).
 */
int generator_start_div(PIO pio, uint gpio, clkdiv_t div, generator_t *g);

/**
 * Stop the generator and release its state machine and program.
 *
 * @param g Generator to stop; calling it again is harmless.
 */
void generator_stop(generator_t *g);

#endif
