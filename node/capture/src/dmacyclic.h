/**
 * @file dmacyclic.h
 * @brief Cyclic DMA receive from a PIO state machine (rp1-pio, kernel 6.18+).
 *
 * The kernel allocates one ring of period_bytes * periods and submits a single
 * cyclic DMA transfer. The DMA controller (dw-axi-dmac) links the last period
 * back to the first in hardware, so reception never pauses between periods,
 * unlike queued one-shot receives, which leave a gap at each hand-over.
 *
 * piolib has no API for this, so the ioctls are issued on a /dev/pio0 handle
 * owned by pi5la. The same handle must be used for config and reads: the
 * kernel tracks the DMA claim per open file.
 */
#ifndef PI5LA_DMACYCLIC_H
#define PI5LA_DMACYCLIC_H

#include <stdint.h>

/**
 * Configure the cyclic DMA ring for one state machine.
 *
 * @param fd           Open /dev/pio0 handle, also used for the reads.
 * @param sm           State machine number.
 * @param period_bytes Size of one period, a multiple of 4.
 * @param periods      Number of periods in the ring, at most 4.
 * @return 0 or -1 (errno set).
 */
int dmacyclic_config(int fd, unsigned sm, uint32_t period_bytes, uint32_t periods);

/**
 * Blocks until the next period completes and copies period_bytes into buf.
 *
 * @param fd           The handle given to dmacyclic_config().
 * @param sm           State machine number.
 * @param period_bytes Size of one period, as configured.
 * @param buf          Receives period_bytes bytes.
 * @return 0, or -1 with errno set: EOVERFLOW if the ring wrapped over unread
 *         data (host fell behind; the capture cannot continue), EINTR on a signal.
 */
int dmacyclic_read(int fd, unsigned sm, uint32_t period_bytes, void *buf);

#endif
