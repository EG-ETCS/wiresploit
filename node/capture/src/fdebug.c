#define _GNU_SOURCE
#include "fdebug.h"

#include <fcntl.h>
#include <unistd.h>

#include "rp1_ioctl.h"

int fdebug_open(void)
{
    return open("/dev/pio0", O_RDWR | O_CLOEXEC);
}

void fdebug_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

int fdebug_read(int fd, uint32_t *value)
{
    struct rp1_access_hw_args args = { .addr = FDEBUG_ADDR, .len = 4, .data = value };

    return ioctl(fd, RP1_PIO_IOC_READ_HW, &args) < 0 ? -1 : 0;
}

int fdebug_clear(int fd, uint32_t mask)
{
    struct rp1_access_hw_args args = { .addr = FDEBUG_ADDR, .len = 4, .data = &mask };

    return ioctl(fd, RP1_PIO_IOC_WRITE_HW, &args) < 0 ? -1 : 0;
}
