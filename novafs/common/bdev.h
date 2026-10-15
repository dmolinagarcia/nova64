/*
 * bdev.h — L0, the block device (sheet Y4.4, Y4.9, Y4.10).
 *
 * The only layer that knows what the backing store is. Everything above
 * it asks for whole sectors by number; the backend behind the read hook
 * can be an image file on the host, a memory buffer in a test, or the SD
 * block driver of sheet G on the machine.
 *
 * Widths are 32 bits throughout: an MBR partition cannot start or extend
 * beyond 2^32 sectors, and the machine has nothing wider.
 */
#ifndef NOVAFS_BDEV_H
#define NOVAFS_BDEV_H

#include <stdint.h>

#define BDEV_OK        0
#define BDEV_EIO      -1        /* backend failure                       */
#define BDEV_ERANGE   -2        /* request outside the device            */
#define BDEV_EINVAL   -3        /* bad geometry or argument at open time */

typedef struct bdev bdev_t;

/* Backend hook. Reads nsec whole sectors starting at lba into buf. It is
 * only ever called with a request already range-checked by bdev_read(). */
typedef int (*bdev_read_fn)(bdev_t *bd, uint32_t lba, void *buf, uint32_t nsec);

struct bdev {
    bdev_read_fn   read;
    void         (*close)(bdev_t *bd);      /* optional                     */
    union {
        int            fd;                  /* bdev_file                    */
        const uint8_t *mem;                 /* bdev_mem                     */
        void          *ptr;                 /* anything else                */
    } priv;
    uint32_t       part_lba;                /* bdev_part_open: first sector */
    uint32_t       sector_size;             /* bytes, a power of two >= 512 */
    uint32_t       sector_count;            /* addressable sectors          */
    uint32_t       reads;                   /* backend calls, for the tests */
    uint32_t       sectors_read;
};

/* Reads nsec sectors at lba. Fails with BDEV_ERANGE, and reads nothing,
 * if any part of the request lies outside the device: a short read that
 * reported success would turn damage into plausible data. */
int  bdev_read(bdev_t *bd, uint32_t lba, void *buf, uint32_t nsec);
void bdev_close(bdev_t *bd);

/* Partition view: sectors lba .. lba+count-1 of `parent`, renumbered from
 * 0, so that a volume inside a partition is mounted like a whole device.
 * The parent must stay open while the view is used. */
int  bdev_part_open(bdev_t *view, bdev_t *parent, uint32_t lba, uint32_t count);

/* Memory backend: the device is `bytes` bytes at `base`. */
int  bdev_mem_open(bdev_t *bd, const void *base, uint32_t bytes,
                   uint32_t sector_size);

#ifndef NOVAFS_FREESTANDING
/* Host backend: the device is an image file, opened read-only. */
int  bdev_file_open(bdev_t *bd, const char *path, uint32_t sector_size);
#endif

#endif /* NOVAFS_BDEV_H */
