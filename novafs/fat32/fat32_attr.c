/*
 * fat32_attr.c — L2: attribute synthesis (FUS-03.i).
 *
 * FAT stores no inode, no owner, no mode bits and no link count. The
 * caller of the table is told all of them anyway (Y4.15), so each one is
 * made up under a rule stated here. None of the rules asks the host
 * anything (Y4.20). Mapping to a host user or time zone is a mount
 * option of the binding (FUS-03.m), not something this layer guesses.
 *
 *   ino    the position of the short entry (FUS-03.g); 1 for the root
 *   mode   directories 040755, files 0100644 (the defaults of Y1.16),
 *          with every write bit cleared when FAT_ATTR_READ_ONLY is set
 *   uid    0, and gid 0 (Y1.16): carried, never enforced
 *   nlink  1 for a file; 2 plus the number of subdirectories for a
 *          directory, which is how the Linux driver counts
 *   size   DIR_FileSize for a file; for a directory, its cluster chain in
 *          bytes, as the Linux driver reports it
 *   blocks the size rounded up to whole clusters, in 512-byte units
 *   mtime  DIR_WrtDate and DIR_WrtTime
 *   atime  DIR_LstAccDate at 00:00:00
 *   ctime  DIR_CrtDate, DIR_CrtTime and DIR_CrtTimeTenth
 *          (the root has no entry, and all three of its times are 0)
 *
 * Times are local time with no zone applied (Y1.17); an invalid field
 * decodes to 1980-01-01 00:00:00.
 */
#include <string.h>

#include "fat32_priv.h"

#define MODE_DIR    (FS_S_IFDIR | 0755u)
#define MODE_FILE   (FS_S_IFREG | 0644u)
#define MODE_WRITE  0222u

static int64_t fat_time(uint16_t date, uint16_t time, uint8_t tenth)
{
    fat_datetime_t dt;

    fat32_decode_datetime(date, time, tenth, &dt);
    return (int64_t)fat32_datetime_to_unix(&dt);
}

/* Subdirectories of a directory, not counting "." and "..". */
static fs_err_t count_subdirs(fat_fs_t *fs, uint32_t cluster, uint32_t *n)
{
    fat_dir_t d;
    const uint8_t *de;
    uint32_t cl;
    uint16_t index;
    fs_err_t e;

    *n = 0;
    e = fat_dir_open_cluster(fs, cluster, &d);
    if (e != FS_OK)
        return e;
    while ((e = fat_dir_next_raw(&d, &de, &cl, &index)) == FS_OK) {
        if (fat32_de_kind(de) == FAT_DE_FILE && (de[DIR_Attr_OFF] & FAT_ATTR_DIRECTORY))
            (*n)++;
    }
    return e == FS_ENOENT ? FS_OK : e;
}

/* Clusters in a chain, bounded and cycle-checked like every walk. */
static fs_err_t count_clusters(fat_fs_t *fs, uint32_t first, uint32_t *n)
{
    fat_chain_t c;
    fs_err_t e;

    e = fat_chain_start(fs, &c, first);
    if (e != FS_OK)
        return e;
    *n = 1;
    while ((e = fat_chain_next(fs, &c)) == FS_OK)
        (*n)++;
    return e == FS_ENOENT ? FS_OK : e;
}

fs_err_t fat_getattr(fat_fs_t *fs, const fat_node_t *node, fs_attr_t *out)
{
    uint8_t  cshift = FAT_CLUSTER_BYTES_SHIFT(fs);
    uint32_t cmask = ((uint32_t)1 << cshift) - 1u;
    fs_err_t e;

    memset(out, 0, sizeof *out);
    out->ino = node->ino;
    out->uid = 0;
    out->gid = 0;

    if (fat_node_is_dir(node)) {
        uint32_t subdirs, clusters;

        e = count_subdirs(fs, node->cluster, &subdirs);
        if (e == FS_OK)
            e = count_clusters(fs, node->cluster, &clusters);
        if (e != FS_OK)
            return e;
        out->mode = MODE_DIR;
        out->nlink = 2u + subdirs;
        out->size = (uint64_t)clusters << cshift;
    } else {
        out->mode = MODE_FILE;
        out->nlink = 1;
        out->size = node->size;
    }
    if (node->attr & FAT_ATTR_READ_ONLY)
        out->mode &= ~MODE_WRITE;

    /* Rounded up to whole clusters, in a width that cannot overflow. */
    out->blocks = ((out->size + cmask) & ~(uint64_t)cmask) >> 9;

    if (node->ino != FAT_INO_ROOT) {
        out->mtime = fat_time(node->wrt_date, node->wrt_time, 0);
        out->atime = fat_time(node->acc_date, 0, 0);
        out->ctime = fat_time(node->crt_date, node->crt_time, node->crt_tenth);
    }
    return FS_OK;
}
