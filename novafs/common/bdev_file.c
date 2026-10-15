/*
 * bdev_file.c — host backend: an image file read with pread().
 *
 * pread() rather than lseek() + read(), so that the backend stays safe if
 * it is ever driven from more than one thread. On the machine this file
 * is replaced wholesale by the SD block driver.
 */
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bdev.h"

static int file_read(bdev_t *bd, uint32_t lba, void *buf, uint32_t nsec)
{
    size_t left = (size_t)nsec * bd->sector_size;
    off_t  off  = (off_t)lba * (off_t)bd->sector_size;
    char  *p    = (char *)buf;

    while (left > 0) {
        ssize_t n = pread(bd->priv.fd, p, left, off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return BDEV_EIO;
        }
        if (n == 0)                 /* the file shrank under us */
            return BDEV_EIO;
        p    += n;
        off  += n;
        left -= (size_t)n;
    }
    return BDEV_OK;
}

static void file_close(bdev_t *bd)
{
    close(bd->priv.fd);
    bd->priv.fd = -1;
}

int bdev_file_open(bdev_t *bd, const char *path, uint32_t sector_size)
{
    struct stat st;
    off_t count;
    int fd;

    memset(bd, 0, sizeof *bd);
    if (sector_size < 512u || (sector_size & (sector_size - 1u)) != 0)
        return BDEV_EINVAL;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return BDEV_EIO;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return BDEV_EIO;
    }

    /* A trailing partial sector is not addressable. Anything beyond
     * 2^32 sectors cannot be reached by an MBR volume either. */
    count = st.st_size / (off_t)sector_size;
    if (count > (off_t)UINT32_MAX)
        count = (off_t)UINT32_MAX;

    bd->read = file_read;
    bd->close = file_close;
    bd->priv.fd = fd;
    bd->sector_size = sector_size;
    bd->sector_count = (uint32_t)count;
    return BDEV_OK;
}
