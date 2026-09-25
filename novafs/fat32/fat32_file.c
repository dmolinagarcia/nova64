/*
 * fat32_file.c — L2: reading file contents.
 */
#include <string.h>

#include "fat32_priv.h"

fs_err_t fat_file_open(fat_fs_t *fs, const fat_node_t *node, fat_file_t *f)
{
    if (fat_node_is_dir(node))
        return FS_EISDIR;

    f->fs = fs;
    f->first = node->cluster;
    f->size = node->size;
    f->pos = 0;
    f->index = 0;
    f->chain_valid = 0;

    /* An empty file legitimately has cluster 0, which is not a cluster,
     * and must never be walked. A non-empty one must have a chain that
     * could hold it. */
    if (f->size > 0) {
        uint32_t clusters = ((f->size - 1u) >> FAT_CLUSTER_BYTES_SHIFT(fs)) + 1u;

        if (f->first < 2u || f->first > fs->g.max_cluster)
            return fs_corrupt("non-empty file without a valid first cluster");
        if (clusters > fs->g.cluster_count)
            return fs_corrupt("file larger than the volume");
    }
    return FS_OK;
}

/* Positions the chain at logical cluster n, which is inside the file.
 * Forward moves continue from where the last read left off, so that a
 * sequential read costs one FAT lookup per cluster instead of n. */
static fs_err_t seek_cluster(fat_file_t *f, uint32_t n)
{
    fs_err_t e;

    if (!f->chain_valid || n < f->index) {
        f->chain_valid = 0;
        e = fat_chain_start(f->fs, &f->chain, f->first);
        if (e != FS_OK)
            return e;
        f->index = 0;
        f->chain_valid = 1;
    }
    while (f->index < n) {
        e = fat_chain_next(f->fs, &f->chain);
        if (e != FS_OK) {
            f->chain_valid = 0;
            if (e == FS_ENOENT)
                return fs_corrupt("cluster chain shorter than the file size");
            return e;
        }
        f->index++;
    }
    return FS_OK;
}

fs_err_t fat_file_pread(fat_file_t *f, void *buf, uint32_t len, uint32_t off,
                        uint32_t *got)
{
    fat_fs_t *fs = f->fs;
    uint8_t   cshift = FAT_CLUSTER_BYTES_SHIFT(fs);
    uint32_t  cmask = ((uint32_t)1 << cshift) - 1u;
    uint32_t  smask = (uint32_t)fs->g.bytes_per_sector - 1u;
    uint8_t  *dst = (uint8_t *)buf;

    *got = 0;
    if (off >= f->size)
        return FS_OK;
    if (len > f->size - off)
        len = f->size - off;

    while (len > 0) {
        uint32_t in_clus = off & cmask;
        uint32_t in_sec = in_clus & smask;
        uint32_t sector, n;
        fs_err_t e;

        e = seek_cluster(f, off >> cshift);
        if (e != FS_OK)
            return e;
        sector = fat_cluster_sector(fs, f->chain.cur) + (in_clus >> fs->g.sec_shift);

        if (in_sec == 0 && len > smask) {
            /* Whole sectors go straight to the caller, up to the end of
             * the cluster. */
            uint32_t nsec = (cmask + 1u - in_clus) >> fs->g.sec_shift;

            if (nsec > len >> fs->g.sec_shift)
                nsec = len >> fs->g.sec_shift;
            e = fat_read_sectors(fs, sector, nsec, dst);
            if (e != FS_OK)
                return e;
            n = nsec << fs->g.sec_shift;
        } else {
            e = fat_load_buf(fs, sector);
            if (e != FS_OK)
                return e;
            n = smask + 1u - in_sec;
            if (n > len)
                n = len;
            memcpy(dst, fs->buf + in_sec, (size_t)n);     /* n <= sector */
        }
        dst += n;
        off += n;
        len -= n;
        *got += n;
    }
    return FS_OK;
}

fs_err_t fat_file_read(fat_file_t *f, void *buf, uint32_t len, uint32_t *got)
{
    fs_err_t e = fat_file_pread(f, buf, len, f->pos, got);

    f->pos += *got;
    return e;
}

void fat_file_seek(fat_file_t *f, uint32_t pos)
{
    f->pos = pos;
}

fs_err_t fat_open(fat_fs_t *fs, const char *path, fat_file_t *f)
{
    fat_node_t node;
    fs_err_t e = fat_resolve(fs, 0, path, &node);

    if (e != FS_OK)
        return e;
    return fat_file_open(fs, &node, f);
}
