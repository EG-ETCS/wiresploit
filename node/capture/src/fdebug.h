/**
 * @file fdebug.h
 * @brief PIO FIFO debug register (FDEBUG) accessed through the rp1-pio READ_HW /
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
#ifndef PI5LA_FDEBUG_H
#define PI5LA_FDEBUG_H

#include <stdint.h>

/** RP1 bus address of the PIO FDEBUG register. */
#define FDEBUG_ADDR 0xf0000008u
/** RX FIFO of state machine `sm` was full and the SM stalled (samples lost). */
#define FDEBUG_RXSTALL(sm) (1u << (sm))
/** RX FIFO of state machine `sm` was read while empty. */
#define FDEBUG_RXUNDER(sm) (1u << (8 + (sm)))
/** TX FIFO of state machine `sm` was written while full. */
#define FDEBUG_TXOVER(sm) (1u << (16 + (sm)))
/** State machine `sm` stalled on an empty TX FIFO. */
#define FDEBUG_TXSTALL(sm) (1u << (24 + (sm)))

/**
 * Opens a separate /dev/pio0 handle for register access.
 *
 * @return The file descriptor, or -1.
 */
int fdebug_open(void);

/**
 * Close a handle from fdebug_open().
 *
 * @param fd The handle; a negative value is ignored.
 */
void fdebug_close(int fd);

/**
 * Read the FDEBUG register.
 *
 * @param fd    Handle from fdebug_open().
 * @param value Receives the register value.
 * @return 0 on success, -1 on error (errno set).
 */
int fdebug_read(int fd, uint32_t *value);

/**
 * Clear FDEBUG bits (write 1 to clear).
 *
 * @param fd   Handle from fdebug_open().
 * @param mask Bits to clear, built from the FDEBUG_* macros.
 * @return 0 on success, -1 on error (errno set).
 */
int fdebug_clear(int fd, uint32_t mask);

#endif
