#ifndef PI5LA_FDEBUG_H
#define PI5LA_FDEBUG_H

#include <stdint.h>

/*
 * PIO FIFO debug register (FDEBUG) accessed through the rp1-pio READ_HW /
 * WRITE_HW ioctls.
 *
 * Why: piolib's pio_sm_get_flags() needs RP1 firmware op PIO_SM_GET_FLAGS
 * (op 33). Firmware reporting "op count 33" lacks it, and piolib then panics.
 * READ_HW / WRITE_HW (ops 29 / 30) are available on that firmware.
 *
 * Address 0xf0000008 is from piolib's examples/rp1sm.c. The bit layout
 * (RXSTALL = bit sm, write 1 to clear) is the RP2040 PIO layout, assumed to
 * carry over to RP1; tools/fdebugtest checks it on hardware.
 */
#define FDEBUG_ADDR 0xf0000008u
#define FDEBUG_RXSTALL(sm) (1u << (sm))
#define FDEBUG_RXUNDER(sm) (1u << (8 + (sm)))
#define FDEBUG_TXOVER(sm) (1u << (16 + (sm)))
#define FDEBUG_TXSTALL(sm) (1u << (24 + (sm)))

/* Opens a separate /dev/pio0 handle for register access. Returns fd or -1. */
int fdebug_open(void);
void fdebug_close(int fd);

/* Return 0 on success, -1 on error (errno set). */
int fdebug_read(int fd, uint32_t *value);
int fdebug_clear(int fd, uint32_t mask);

#endif
