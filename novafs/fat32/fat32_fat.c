/*
 * fat32_fat.c — L2: the file allocation table and cluster chains.
 */
#include "endian.h"
#include "fat32_priv.h"

#if (FAT_CFG_FAT_CACHE & (FAT_CFG_FAT_CACHE - 1u)) != 0 || FAT_CFG_FAT_CACHE == 0
#error "FAT_CFG_FAT_CACHE must be a power of two"
#endif

static fs_err_t fat_cache_get(fat_fs_t *fs, uint32_t sector, const uint8_t **out)
{
    uint32_t slot = sector & (FAT_CFG_FAT_CACHE - 1u);

    if (!fs->fc_valid[slot] || fs->fc_sector[slot] != sector) {
        fs_err_t e;

        fs->fc_valid[slot] = 0;
        e = fat_read_sectors(fs, sector, 1, fs->fc_buf[slot]);
        if (e != FS_OK)
            return e;
        fs->fc_sector[slot] = sector;
        fs->fc_valid[slot] = 1;
    }
    *out = fs->fc_buf[slot];
    return FS_OK;
}

fs_err_t fat_get_next(fat_fs_t *fs, uint32_t cluster, uint32_t *next)
{
    const uint8_t *sec;
    uint32_t byte, v;
    fs_err_t e;

    if (cluster < 2u || cluster > fs->g.max_cluster)
        return fs_corrupt("FAT lookup of an out-of-range cluster");

    /* Entry N is the 32-bit word at byte N * 4 of the FAT. N is at most
     * 0x0FFFFFF6, so the product fits. */
    byte = cluster << 2;
    e = fat_cache_get(fs, fs->g.fat_start + (byte >> fs->g.sec_shift), &sec);
    if (e != FS_OK)
        return e;
    v = rd32(sec + (byte & (fs->g.bytes_per_sector - 1u))) & FAT32_ENTRY_MASK;

    if (v >= FAT32_EOC_MIN) {
        *next = v;
        return FS_OK;
    }
    if (v == FAT32_BAD)
        return fs_corrupt("bad cluster inside a cluster chain");
    if (v < 2u)
        return fs_corrupt("free cluster inside a cluster chain");
    if (v > fs->g.max_cluster)
        return fs_corrupt("cluster chain successor out of range");
    *next = v;
    return FS_OK;
}

fs_err_t fat_count_free(fat_fs_t *fs, uint32_t *out)
{
    uint8_t  eps_shift = (uint8_t)(fs->g.sec_shift - 2u);
    uint32_t eps = (uint32_t)1 << eps_shift;
    uint32_t cl = 2, nfree = 0;

    while (cl <= fs->g.max_cluster) {
        uint32_t i = cl & (eps - 1u);
        fs_err_t e = fat_load_buf(fs, fs->g.fat_start + (cl >> eps_shift));

        if (e != FS_OK)
            return e;
        for (; i < eps && cl <= fs->g.max_cluster; i++, cl++) {
            if ((rd32(fs->buf + (i << 2)) & FAT32_ENTRY_MASK) == 0)
                nfree++;
        }
    }
    *out = nfree;
    return FS_OK;
}

/*
 * Chains are walked with two independent safeguards (DN-FS-FUSE-001
 * step 25). The step bound alone guarantees termination: a chain longer
 * than the volume must loop. Brent's algorithm finds the loop in about
 * twice its length instead of the volume's, and unlike Floyd's it needs
 * no second walker, so it costs no extra FAT reads.
 */
fs_err_t fat_chain_start(fat_fs_t *fs, fat_chain_t *c, uint32_t first)
{
    if (first < 2u || first > fs->g.max_cluster)
        return fs_corrupt("cluster chain starts out of range");
    c->cur = first;
    c->steps = 0;
    c->mark = first;
    c->power = 1;
    c->lam = 0;
    return FS_OK;
}

fs_err_t fat_chain_next(fat_fs_t *fs, fat_chain_t *c)
{
    uint32_t next;
    fs_err_t e;

    e = fat_get_next(fs, c->cur, &next);
    if (e != FS_OK)
        return e;
    if (next >= FAT32_EOC_MIN)
        return FS_ENOENT;

    if (++c->steps >= fs->g.cluster_count)
        return fs_corrupt("cluster chain longer than the volume");
    c->cur = next;
    if (next == c->mark)
        return fs_corrupt("cluster chain loops");
    if (++c->lam == c->power) {
        c->mark = next;
        c->power <<= 1;
        c->lam = 0;
    }
    return FS_OK;
}
