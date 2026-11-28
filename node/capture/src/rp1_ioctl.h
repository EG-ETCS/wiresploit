/**
 * @file rp1_ioctl.h
 * @brief rp1-pio character device ioctls used directly by pi5la (not through piolib).
 *
 * Layouts from the kernel uapi header include/uapi/misc/rp1_pio_if.h
 * (raspberrypi/linux rpi-6.18.y); piolib does not install its copy.
 * This is the only file that holds the kernel ABI.
 */
#ifndef PI5LA_RP1_IOCTL_H
#define PI5LA_RP1_IOCTL_H

#include <stdint.h>
#include <sys/ioctl.h>

/** ioctl type number of the rp1-pio device. */
#define RP1_PIO_IOC_MAGIC 102

/** Transfer direction: from the state machine (RX FIFO) to the host. */
#define RP1_PIO_DIR_FROM_SM 1

/** Arguments of RP1_PIO_IOC_READ_HW / RP1_PIO_IOC_WRITE_HW. */
struct rp1_access_hw_args {
    /** RP1 bus address of the register. */
    uint32_t addr;
    /** Bytes to transfer. */
    uint32_t len;
    /** Host buffer to read into or write from. */
    void *data;
};

/* Flags for RP1_PIO_IOC_SM_CONFIG_XFER_V2 */
/** Flag named in the kernel header; its value is 0, so it adds no bit. */
#define RP1_PIO_SM_CONFIG_XFER_FL_DMA_PREFER_HEAVY 0
/** Flag: run the transfer as one cyclic DMA ring (see dmacyclic.h). */
#define RP1_PIO_SM_CONFIG_XFER_FL_DMA_CYCLE (1 << 2)

/** Arguments of RP1_PIO_IOC_SM_CONFIG_XFER_V2. */
struct rp1_pio_sm_config_xfer_v2_args {
    /** State machine number. */
    uint16_t sm;
    /** Transfer direction, for example RP1_PIO_DIR_FROM_SM. */
    uint16_t dir;
    /** RP1_PIO_SM_CONFIG_XFER_FL_* flags. */
    uint32_t flags;
    /** Size of one DMA buffer (period), in bytes. */
    uint32_t buf_size;
    /** Number of DMA buffers (periods). */
    uint32_t buf_count;
};

/** Arguments of RP1_PIO_IOC_SM_XFER_DATA32. */
struct rp1_pio_sm_xfer_data32_args {
    /** State machine number. */
    uint16_t sm;
    /** Transfer direction, for example RP1_PIO_DIR_FROM_SM. */
    uint16_t dir;
    /** Bytes to transfer. */
    uint32_t data_bytes;
    /** Host buffer. */
    void *data;
};

/** Move data between a state machine FIFO and a host buffer. */
#define RP1_PIO_IOC_SM_XFER_DATA32 \
    _IOW(RP1_PIO_IOC_MAGIC, 2, struct rp1_pio_sm_xfer_data32_args)
/** Configure DMA for a state machine FIFO (buffer size, count, flags). */
#define RP1_PIO_IOC_SM_CONFIG_XFER_V2 \
    _IOW(RP1_PIO_IOC_MAGIC, 4, struct rp1_pio_sm_config_xfer_v2_args)
/** Read an RP1 register. */
#define RP1_PIO_IOC_READ_HW _IOW(RP1_PIO_IOC_MAGIC, 8, struct rp1_access_hw_args)
/** Write an RP1 register. */
#define RP1_PIO_IOC_WRITE_HW _IOW(RP1_PIO_IOC_MAGIC, 9, struct rp1_access_hw_args)

#endif
