/*
 * Hardware check for src/fdebug.c: does FDEBUG at 0xf0000008 report RXSTALL
 * in bit <sm>, and does writing 1 clear it?
 *
 * Runs `in pins, 8` with autopush at 10 kSa/s on GPIO 2..9 and never reads the
 * FIFO, so it fills in a few milliseconds and the SM stalls. No wiring needed.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fdebug.h"
#include "piolib.h"
#include "progbuild.h"

static int rd(int fd, const char *what, uint32_t *v)
{
    if (fdebug_read(fd, v)) {
        printf("%-34s READ_HW failed: %s\n", what, strerror(errno));
        return -1;
    }
    printf("%-34s FDEBUG = 0x%08x\n", what, *v);
    return 0;
}

int main(void)
{
    pio_program_t program;
    pio_sm_config c;
    la_prog_t lp;
    uint32_t v;
    uint offset;
    int fd, sm, k, pass = 1;
    PIO pio;

    pio = pio_open(0);
    if (PIO_IS_ERR(pio)) {
        fprintf(stderr, "pio_open(0) failed: %s\n", strerror(-PIO_ERR_VAL(pio)));
        return 1;
    }
    fd = fdebug_open();
    if (fd < 0) {
        fprintf(stderr, "open /dev/pio0: %s\n", strerror(errno));
        return 1;
    }

    la_build_program(8, TRIG_NONE, 0, &lp);
    program.instructions = lp.instr;
    program.length = lp.length;
    program.origin = -1;
    program.pio_version = 0;

    sm = pio_claim_unused_sm(pio, true);
    offset = pio_add_program(pio, &program);
    for (k = 0; k < 8; k++)
        pio_gpio_init(pio, 2 + k);
    pio_sm_set_consecutive_pindirs(pio, sm, 2, 8, false);
    c = pio_get_default_sm_config_for_pio(pio);
    sm_config_set_wrap(&c, offset + lp.wrap_target, offset + lp.wrap);
    sm_config_set_in_pins(&c, 2);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv_int_frac(&c, 20000, 0); /* 10 kSa/s */
    pio_sm_init(pio, sm, offset, &c);
    printf("using SM %d; expecting RXSTALL mask 0x%08x\n\n", sm, FDEBUG_RXSTALL(sm));

    if (rd(fd, "1. initial:", &v))
        goto fail;
    if (fdebug_clear(fd, FDEBUG_RXSTALL(sm))) {
        printf("WRITE_HW failed: %s\n", strerror(errno));
        goto fail;
    }
    if (rd(fd, "2. after clear, SM disabled:", &v))
        goto fail;
    if (v & FDEBUG_RXSTALL(sm)) {
        printf("   -> bit still set after clear\n");
        pass = 0;
    }

    pio_sm_set_enabled(pio, sm, true);
    usleep(200000);
    printf("%-34s rx fifo full = %d\n", "3. SM ran 200 ms, never read:",
           pio_sm_is_rx_fifo_full(pio, sm));
    if (rd(fd, "   stalled:", &v))
        goto fail;
    if (!(v & FDEBUG_RXSTALL(sm))) {
        printf("   -> RXSTALL bit for SM %d NOT set\n", sm);
        pass = 0;
    }

    pio_sm_set_enabled(pio, sm, false);
    if (fdebug_clear(fd, FDEBUG_RXSTALL(sm))) {
        printf("WRITE_HW failed: %s\n", strerror(errno));
        goto fail;
    }
    if (rd(fd, "4. SM disabled, after clear:", &v))
        goto fail;
    if (v & FDEBUG_RXSTALL(sm)) {
        printf("   -> bit still set after clear\n");
        pass = 0;
    }

    pio_remove_program(pio, &program, offset);
    pio_sm_unclaim(pio, sm);
    fdebug_close(fd);
    pio_close(pio);
    printf("\n%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 2;

fail:
    pio_sm_set_enabled(pio, sm, false);
    fdebug_close(fd);
    pio_close(pio);
    printf("\nFAIL\n");
    return 2;
}
