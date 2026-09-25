/*
 * fat32_priv.h — L2 internals shared between the fat32_*.c files.
 */
#ifndef NOVAFS_FAT32_PRIV_H
#define NOVAFS_FAT32_PRIV_H

#include "fat32_fs.h"

#define FAT_CLUSTER_BYTES_SHIFT(fs) \
    ((uint8_t)((fs)->g.sec_shift + (fs)->g.clus_shift))

/* First logical sector of a cluster already known to be in range. */
static inline uint32_t fat_cluster_sector(const fat_fs_t *fs, uint32_t cluster)
{
    return fs->g.data_start + ((cluster - 2u) << fs->g.clus_shift);
}

/* Sector I/O, in logical sectors relative to the volume. */
fs_err_t fat_read_sectors(fat_fs_t *fs, uint32_t sector, uint32_t n, void *dst);
fs_err_t fat_load_buf(fat_fs_t *fs, uint32_t sector);

/* FAT access (fat32_fat.c). fat_get_next() returns in *next either the
 * successor, which is then a valid cluster, or a value >= FAT32_EOC_MIN.
 * Free, bad and out-of-range successors are reported as corruption. */
fs_err_t fat_get_next(fat_fs_t *fs, uint32_t cluster, uint32_t *next);
fs_err_t fat_count_free(fat_fs_t *fs, uint32_t *out);

/* Bounded, cycle-checked walk over a cluster chain. */
fs_err_t fat_chain_start(fat_fs_t *fs, fat_chain_t *c, uint32_t first);
fs_err_t fat_chain_next(fat_fs_t *fs, fat_chain_t *c);     /* FS_ENOENT: end */

/* Directory internals (fat32_dir.c). */
void     fat_node_decode(const fat_fs_t *fs, const uint8_t *de,
                         uint32_t cluster, uint16_t index, fat_node_t *out);
fs_err_t fat_dir_open_cluster(fat_fs_t *fs, uint32_t cluster, fat_dir_t *d);

/* Raw iteration: the next 32-byte entry of any kind, and where it lives.
 * The pointer is into fs->buf and is valid until the next sector load. */
fs_err_t fat_dir_next_raw(fat_dir_t *d, const uint8_t **de,
                          uint32_t *cluster, uint16_t *index);

/* Long names (fat32_lfn.c). */
void     fat_lfn_reset(fat_dir_t *d);
void     fat_lfn_feed(fat_dir_t *d, const uint8_t *de);
/* If a complete run belonging to the short entry `de` is pending, writes
 * it into out as UTF-8 and returns its length; else returns 0. Returns
 * -1 if it is valid but does not fit in `cap` bytes plus a NUL. */
int32_t  fat_lfn_take(fat_dir_t *d, const uint8_t *de, char *out, uint32_t cap);

#endif /* NOVAFS_FAT32_PRIV_H */
