#include "generator.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sqwave.pio.h"
#include "sqwave_slow.pio.h"

/* Loop count limit for sqwave_slow: N is a 32-bit OSR value. */
#define SLOW_MAX_N 0xffffffffull

static int generator_setup(PIO pio, uint gpio, const pio_program_t *prog, uint wrap_target,
                           uint wrap, clkdiv_t div, generator_t *g)
{
    pio_sm_config c;

    memset(g, 0, sizeof(*g));
    g->pio = pio;
    g->gpio = gpio;
    g->program = prog;
    g->sm = -1;

    g->sm = pio_claim_unused_sm(pio, false);
    if (g->sm < 0) {
        fprintf(stderr, "generator: no free PIO state machine\n");
        return -1;
    }
    g->offset = pio_add_program(pio, prog);
    if (g->offset == PIO_ORIGIN_INVALID) {
        fprintf(stderr, "generator: no PIO program space\n");
        pio_sm_unclaim(pio, g->sm);
        g->sm = -1;
        return -1;
    }

    pio_gpio_init(pio, gpio);
    pio_sm_set_consecutive_pindirs(pio, g->sm, gpio, 1, true);

    c = pio_get_default_sm_config_for_pio(pio);
    sm_config_set_wrap(&c, g->offset + wrap_target, g->offset + wrap);
    sm_config_set_set_pins(&c, gpio, 1);
    sm_config_set_clkdiv_int_frac(&c, div.div_int, div.div_frac);
    pio_sm_init(pio, g->sm, g->offset, &c);
    return 0;
}

int generator_start_div(PIO pio, uint gpio, clkdiv_t div, generator_t *g)
{
    if (generator_setup(pio, gpio, &sqwave_program, sqwave_wrap_target, sqwave_wrap, div, g))
        return -1;
    g->actual_hz = clkdiv_hz(div) / 2.0;
    pio_sm_set_enabled(pio, g->sm, true);
    return 0;
}

int generator_start(PIO pio, uint gpio, double freq_hz, generator_t *g)
{
    clkdiv_t div;
    long long half_cycles;
    uint64_t n;

    if (!(freq_hz > 0)) {
        fprintf(stderr, "generator: frequency must be > 0\n");
        return -1;
    }

    /* sqwave toggles every SM cycle, so the SM runs at 2 * freq. */
    if (clkdiv_for_hz(2.0 * freq_hz, &div))
        return generator_start_div(pio, gpio, div, g);

    if (2.0 * freq_hz > PI5LA_PIO_CLOCK_HZ) {
        fprintf(stderr, "generator: %.0f Hz is above the 100 MHz limit\n", freq_hz);
        return -1;
    }

    /* Too slow for the divider: delay loop at divider 1, half period = N + 3 cycles. */
    half_cycles = llround(PI5LA_PIO_CLOCK_HZ / (2.0 * freq_hz));
    if (half_cycles < 3 || (uint64_t)(half_cycles - 3) > SLOW_MAX_N) {
        fprintf(stderr, "generator: %g Hz is out of range\n", freq_hz);
        return -1;
    }
    n = (uint64_t)half_cycles - 3;

    div.div_int = 1;
    div.div_frac = 0;
    if (generator_setup(pio, gpio, &sqwave_slow_program, sqwave_slow_wrap_target,
                        sqwave_slow_wrap, div, g))
        return -1;
    g->slow = true;
    g->actual_hz = PI5LA_PIO_CLOCK_HZ / (2.0 * (double)(n + 3));
    pio_sm_put_blocking(pio, g->sm, (uint32_t)n);
    pio_sm_set_enabled(pio, g->sm, true);
    return 0;
}

void generator_stop(generator_t *g)
{
    if (!g->pio || g->sm < 0)
        return;
    pio_sm_set_enabled(g->pio, g->sm, false);
    pio_remove_program(g->pio, g->program, g->offset);
    pio_sm_unclaim(g->pio, g->sm);
    g->sm = -1;
}
