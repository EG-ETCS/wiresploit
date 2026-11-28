/**
 * @file progbuild.h
 * @brief Capture PIO programs built at runtime, and the trigger option parser.
 */
#ifndef PI5LA_PROGBUILD_H
#define PI5LA_PROGBUILD_H

#include <stdbool.h>
#include <stdint.h>

/** Trigger condition that starts a capture. */
typedef enum {
    /** No trigger: start at once. */
    TRIG_NONE = 0,
    /** Start on a low-to-high edge of the trigger pin. */
    TRIG_RISING,
    /** Start on a high-to-low edge of the trigger pin. */
    TRIG_FALLING,
    /** Start as soon as the trigger pin is high. */
    TRIG_HIGH,
    /** Start as soon as the trigger pin is low. */
    TRIG_LOW,
} trig_type_t;

/** Longest capture program: two wait instructions and the `in` loop. */
#define LA_PROG_MAX_INSTR 3

/**
 * Capture program built at runtime.
 *
 * Optional wait instructions for the trigger, then a single
 * `in pins, <channels>` loop:
 *
 * @code
 *   none     :                                in pins, N
 *   high:p   : wait 1 pin p                ;  in pins, N
 *   low:p    : wait 0 pin p                ;  in pins, N
 *   rising:p : wait 0 pin p ; wait 1 pin p ;  in pins, N
 *   falling:p: wait 1 pin p ; wait 0 pin p ;  in pins, N
 * @endcode
 *
 * Pin p is relative to the SM input base.
 */
typedef struct {
    /** Encoded instructions. */
    uint16_t instr[LA_PROG_MAX_INSTR];
    /** Number of instructions used. */
    uint8_t length;
    /** Index of the first instruction of the loop. */
    uint8_t wrap_target;
    /** Index of the last instruction of the loop. */
    uint8_t wrap;
} la_prog_t;

/**
 * Build the capture program for a channel count and trigger.
 *
 * @param channels Channel count; must divide 32.
 * @param trig     Trigger condition.
 * @param trig_pin Trigger channel, relative to the SM input base.
 * @param out      Receives the program.
 * @return False if the channel count is invalid or trig_pin is not one of the channels.
 */
bool la_build_program(unsigned channels, trig_type_t trig, unsigned trig_pin, la_prog_t *out);

/**
 * Encode `in pins, count` (same bit layout as piolib's pio_instructions.h).
 *
 * @param count Number of pins to shift in.
 * @return The instruction word.
 */
uint16_t pio_enc_in_pins(unsigned count);

/**
 * Encode `wait <polarity> pin <pin>` (same bit layout as piolib's pio_instructions.h).
 *
 * @param polarity Level to wait for: true for 1, false for 0.
 * @param pin      Pin, relative to the SM input base.
 * @return The instruction word.
 */
uint16_t pio_enc_wait_pin(bool polarity, unsigned pin);

/**
 * Parse a trigger option: "rising:0" -> TRIG_RISING, 0.
 *
 * @param s    Text to parse.
 * @param type Receives the trigger condition.
 * @param pin  Receives the trigger channel.
 * @return False on malformed input.
 */
bool trig_parse(const char *s, trig_type_t *type, unsigned *pin);

/**
 * Name of a trigger condition, the word trig_parse() reads before the colon.
 *
 * @param t Trigger condition.
 * @return "rising", "falling", "high" or "low"; "none" for TRIG_NONE.
 */
const char *trig_name(trig_type_t t);

#endif
