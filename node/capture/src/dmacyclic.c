#include "dmacyclic.h"

#include "rp1_ioctl.h"

int dmacyclic_config(int fd, unsigned sm, uint32_t period_bytes, uint32_t periods)
{
    struct rp1_pio_sm_config_xfer_v2_args args = {
        .sm = (uint16_t)sm,
        .dir = RP1_PIO_DIR_FROM_SM,
        .flags = RP1_PIO_SM_CONFIG_XFER_FL_DMA_PREFER_HEAVY | RP1_PIO_SM_CONFIG_XFER_FL_DMA_CYCLE,
        .buf_size = period_bytes,
        .buf_count = periods,
    };

    return ioctl(fd, RP1_PIO_IOC_SM_CONFIG_XFER_V2, &args) < 0 ? -1 : 0;
}

int dmacyclic_read(int fd, unsigned sm, uint32_t period_bytes, void *buf)
{
    struct rp1_pio_sm_xfer_data32_args args = {
        .sm = (uint16_t)sm,
        .dir = RP1_PIO_DIR_FROM_SM,
        .data_bytes = period_bytes,
        .data = buf,
    };

    return ioctl(fd, RP1_PIO_IOC_SM_XFER_DATA32, &args) < 0 ? -1 : 0;
}
