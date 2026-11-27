#include "progbuild.h"

#include <stdlib.h>
#include <string.h>

#define PIO_INSTR_WAIT 0x2000u
#define PIO_INSTR_IN 0x4000u
#define PIO_SRC_PINS 0u

uint16_t pio_enc_in_pins(unsigned count)
{
    return (uint16_t)(PIO_INSTR_IN | (PIO_SRC_PINS << 5) | (count & 0x1fu));
}

uint16_t pio_enc_wait_pin(bool polarity, unsigned pin)
{
    unsigned arg1 = 1u | (polarity ? 4u : 0u);
    return (uint16_t)(PIO_INSTR_WAIT | (arg1 << 5) | (pin & 0x1fu));
}

bool la_build_program(unsigned channels, trig_type_t trig, unsigned trig_pin, la_prog_t *out)
{
    uint8_t n = 0;

    if (channels == 0 || channels > 32 || 32 % channels != 0)
        return false;
    if (trig != TRIG_NONE && trig_pin >= channels)
        return false;

    memset(out, 0, sizeof(*out));
    switch (trig) {
    case TRIG_NONE:
        break;
    case TRIG_HIGH:
        out->instr[n++] = pio_enc_wait_pin(true, trig_pin);
        break;
    case TRIG_LOW:
        out->instr[n++] = pio_enc_wait_pin(false, trig_pin);
        break;
    case TRIG_RISING:
        out->instr[n++] = pio_enc_wait_pin(false, trig_pin);
        out->instr[n++] = pio_enc_wait_pin(true, trig_pin);
        break;
    case TRIG_FALLING:
        out->instr[n++] = pio_enc_wait_pin(true, trig_pin);
        out->instr[n++] = pio_enc_wait_pin(false, trig_pin);
        break;
    default:
        return false;
    }
    out->wrap_target = n;
    out->wrap = n;
    out->instr[n++] = pio_enc_in_pins(channels);
    out->length = n;
    return true;
}

static const struct {
    const char *name;
    trig_type_t type;
} trig_names[] = {
    { "rising", TRIG_RISING },
    { "falling", TRIG_FALLING },
    { "high", TRIG_HIGH },
    { "low", TRIG_LOW },
};

bool trig_parse(const char *s, trig_type_t *type, unsigned *pin)
{
    const char *colon = strchr(s, ':');
    char *end;
    unsigned long p;
    size_t i;

    if (!colon || colon[1] == '\0')
        return false;
    p = strtoul(colon + 1, &end, 10);
    if (*end != '\0' || p > 31)
        return false;
    for (i = 0; i < sizeof(trig_names) / sizeof(trig_names[0]); i++) {
        if (strlen(trig_names[i].name) == (size_t)(colon - s) &&
            !strncmp(s, trig_names[i].name, (size_t)(colon - s))) {
            *type = trig_names[i].type;
            *pin = (unsigned)p;
            return true;
        }
    }
    return false;
}

const char *trig_name(trig_type_t t)
{
    size_t i;

    for (i = 0; i < sizeof(trig_names) / sizeof(trig_names[0]); i++)
        if (trig_names[i].type == t)
            return trig_names[i].name;
    return "none";
}
