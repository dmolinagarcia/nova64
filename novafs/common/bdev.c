/*
 * bdev.c — range-checked entry point and the memory backend.
 */
#include <string.h>

#include "bdev.h"

static int sector_size_ok(uint32_t ss)
{
    return ss >= 512u && (ss & (ss - 1u)) == 0;
}

int bdev_read(bdev_t *bd, uint32_t lba, void *buf, uint32_t nsec)
{
    int r;

    if (nsec == 0)
        return BDEV_OK;
    /* Written so that neither side can overflow. */
    if (lba >= bd->sector_count || nsec > bd->sector_count - lba)
        return BDEV_ERANGE;

    r = bd->read(bd, lba, buf, nsec);
    if (r == BDEV_OK) {
        bd->reads++;
        bd->sectors_read += nsec;
    }
    return r;
}

void bdev_close(bdev_t *bd)
{
    if (bd->close)
        bd->close(bd);
    bd->read = 0;
    bd->close = 0;
}

static int part_read(bdev_t *bd, uint32_t lba, void *buf, uint32_t nsec)
{
    bdev_t *parent = (bdev_t *)bd->priv.ptr;

    /* In range of the view, so in range of the parent: see bdev_part_open. */
    return bdev_read(parent, bd->part_lba + lba, buf, nsec);
}

int bdev_part_open(bdev_t *view, bdev_t *parent, uint32_t lba, uint32_t count)
{
    memset(view, 0, sizeof *view);
    if (lba >= parent->sector_count || count == 0 ||
        count > parent->sector_count - lba)
        return BDEV_ERANGE;
    view->read = part_read;
    view->priv.ptr = parent;
    view->part_lba = lba;
    view->sector_size = parent->sector_size;
    view->sector_count = count;
    return BDEV_OK;
}

static int mem_read(bdev_t *bd, uint32_t lba, void *buf, uint32_t nsec)
{
    /* One sector at a time, so that no length exceeds a 16-bit size_t.
     * The offsets fit: bdev_mem_open() bounded the device to 4 GiB. */
    uint8_t *dst = (uint8_t *)buf;

    for (; nsec > 0; nsec--, lba++, dst += bd->sector_size)
        memcpy(dst, bd->priv.mem + lba * bd->sector_size,
               (size_t)bd->sector_size);
    return BDEV_OK;
}

int bdev_mem_open(bdev_t *bd, const void *base, uint32_t bytes,
                  uint32_t sector_size)
{
    memset(bd, 0, sizeof *bd);
    if (!sector_size_ok(sector_size) || base == 0)
        return BDEV_EINVAL;
    bd->read = mem_read;
    bd->priv.mem = (const uint8_t *)base;
    bd->sector_size = sector_size;
    bd->sector_count = bytes / sector_size;
    return BDEV_OK;
}
