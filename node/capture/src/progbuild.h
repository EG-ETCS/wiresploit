#ifndef PI5LA_PROGBUILD_H
#define PI5LA_PROGBUILD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TRIG_NONE = 0,
    TRIG_RISING,
    TRIG_FALLING,
    TRIG_HIGH,
    TRIG_LOW,
} trig_type_t;

#define LA_PROG_MAX_INSTR 3

/*
 * Capture program built at runtime: optional wait instructions for the
 * trigger, then a single `in pins, <channels>` loop.
 *
 *   none     :                                in pins, N
 *   high:p   : wait 1 pin p                ;  in pins, N
 *   low:p    : wait 0 pin p                ;  in pins, N
 *   rising:p : wait 0 pin p ; wait 1 pin p ;  in pins, N
 *   falling:p: wait 1 pin p ; wait 0 pin p ;  in pins, N
 *
 * Pin p is relative to the SM input base.
 */
typedef struct {
    uint16_t instr[LA_PROG_MAX_INSTR];
    uint8_t length;
    uint8_t wrap_target;
    uint8_t wrap;
} la_prog_t;

bool la_build_program(unsigned channels, trig_type_t trig, unsigned trig_pin, la_prog_t *out);

/* Instruction encoders (same bit layout as piolib's pio_instructions.h). */
uint16_t pio_enc_in_pins(unsigned count);
uint16_t pio_enc_wait_pin(bool polarity, unsigned pin);

/* "rising:0" -> TRIG_RISING, 0. Returns false on malformed input. */
bool trig_parse(const char *s, trig_type_t *type, unsigned *pin);
const char *trig_name(trig_type_t t);

#endif
