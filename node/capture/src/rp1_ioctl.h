#ifndef PI5LA_RP1_IOCTL_H
#define PI5LA_RP1_IOCTL_H

/*
 * rp1-pio character device ioctls used directly by pi5la (not through piolib).
 * Layouts from the kernel uapi header include/uapi/misc/rp1_pio_if.h
 * (raspberrypi/linux rpi-6.18.y); piolib does not install its copy.
 */

#include <stdint.h>
#include <sys/ioctl.h>

#define RP1_PIO_IOC_MAGIC 102

#define RP1_PIO_DIR_FROM_SM 1

struct rp1_access_hw_args {
    uint32_t addr;
    uint32_t len;
    void *data;
};

/* Flags for RP1_PIO_IOC_SM_CONFIG_XFER_V2 */
#define RP1_PIO_SM_CONFIG_XFER_FL_DMA_PREFER_HEAVY 0
#define RP1_PIO_SM_CONFIG_XFER_FL_DMA_CYCLE (1 << 2)

struct rp1_pio_sm_config_xfer_v2_args {
    uint16_t sm;
    uint16_t dir;
    uint32_t flags;
    uint32_t buf_size;
    uint32_t buf_count;
};

struct rp1_pio_sm_xfer_data32_args {
    uint16_t sm;
    uint16_t dir;
    uint32_t data_bytes;
    void *data;
};

#define RP1_PIO_IOC_SM_XFER_DATA32 \
    _IOW(RP1_PIO_IOC_MAGIC, 2, struct rp1_pio_sm_xfer_data32_args)
#define RP1_PIO_IOC_SM_CONFIG_XFER_V2 \
    _IOW(RP1_PIO_IOC_MAGIC, 4, struct rp1_pio_sm_config_xfer_v2_args)
#define RP1_PIO_IOC_READ_HW _IOW(RP1_PIO_IOC_MAGIC, 8, struct rp1_access_hw_args)
#define RP1_PIO_IOC_WRITE_HW _IOW(RP1_PIO_IOC_MAGIC, 9, struct rp1_access_hw_args)

#endif
