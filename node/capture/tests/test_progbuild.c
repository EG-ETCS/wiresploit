#include "check.h"
#include "progbuild.h"

int main(void)
{
    la_prog_t p;
    trig_type_t t;
    unsigned pin;

    /* Encodings from piolib pio_instructions.h; capture.c re-checks against pioasm at runtime. */
    CHECK(pio_enc_in_pins(8) == 0x4008);
    CHECK(pio_enc_in_pins(1) == 0x4001);
    CHECK(pio_enc_wait_pin(true, 0) == 0x20a0);
    CHECK(pio_enc_wait_pin(false, 3) == 0x2023);

    CHECK(la_build_program(8, TRIG_NONE, 0, &p));
    CHECK(p.length == 1 && p.wrap_target == 0 && p.wrap == 0 && p.instr[0] == 0x4008);

    CHECK(la_build_program(8, TRIG_HIGH, 0, &p));
    CHECK(p.length == 2 && p.wrap_target == 1 && p.wrap == 1);
    CHECK(p.instr[0] == 0x20a0 && p.instr[1] == 0x4008);

    CHECK(la_build_program(4, TRIG_RISING, 2, &p));
    CHECK(p.length == 3 && p.wrap_target == 2);
    CHECK(p.instr[0] == pio_enc_wait_pin(false, 2) && p.instr[1] == pio_enc_wait_pin(true, 2));
    CHECK(p.instr[2] == 0x4004);

    CHECK(la_build_program(8, TRIG_FALLING, 7, &p));
    CHECK(p.instr[0] == pio_enc_wait_pin(true, 7) && p.instr[1] == pio_enc_wait_pin(false, 7));

    CHECK(!la_build_program(3, TRIG_NONE, 0, &p));
    CHECK(!la_build_program(4, TRIG_LOW, 4, &p));

    CHECK(trig_parse("rising:0", &t, &pin) && t == TRIG_RISING && pin == 0);
    CHECK(trig_parse("low:7", &t, &pin) && t == TRIG_LOW && pin == 7);
    CHECK(!trig_parse("rise:0", &t, &pin));
    CHECK(!trig_parse("high:", &t, &pin));
    CHECK(!trig_parse("high:x", &t, &pin));
    CHECK(!trig_parse("high", &t, &pin));

    CHECK_DONE();
}
