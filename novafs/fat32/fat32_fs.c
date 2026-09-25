/*
 * fat32_fs.c — L2: mounting, sector access and volume-wide queries.
 */
#include <string.h>

#include "fat32_priv.h"
#include "mbr.h"

static uint8_t log2_pow2(uint32_t v)
{
    uint8_t s = 0;

    while (v > 1u) {
        v >>= 1;
        s++;
    }
    return s;
}

static int dev_sector_ok(const bdev_t *bd)
{
    uint32_t ss = bd->sector_size;

    return ss >= 512u && ss <= FAT_CFG_MAX_SECTOR && (ss & (ss - 1u)) == 0;
}

/* ---- Sector access ------------------------------------------------------ */

fs_err_t fat_read_sectors(fat_fs_t *fs, uint32_t sector, uint32_t n, void *dst)
{
    int r;

    /* Every address above is computed from validated geometry, so this
     * can only trip on damage the checks upstream failed to catch. */
    if (sector >= fs->g.total_sectors || n > fs->g.total_sectors - sector)
        return fs_corrupt("access beyond the end of the volume");
    r = bdev_read(fs->bd, fs->part_lba + (sector << fs->dev_shift), dst,
                  n << fs->dev_shift);
    if (r == BDEV_OK)
        return FS_OK;
    if (r == BDEV_ERANGE)
        return fs_corrupt("access beyond the end of the device");
    return FS_EIO;
}

fs_err_t fat_load_buf(fat_fs_t *fs, uint32_t sector)
{
    fs_err_t e;

    if (fs->buf_valid && fs->buf_sector == sector)
        return FS_OK;
    fs->buf_valid = 0;
    e = fat_read_sectors(fs, sector, 1, fs->buf);
    if (e != FS_OK)
        return e;
    fs->buf_sector = sector;
    fs->buf_valid = 1;
    return FS_OK;
}

/* ---- Mounting ------------------------------------------------------------- */

fs_err_t fat_mount(fat_fs_t *fs, bdev_t *bd, uint32_t lba, uint32_t count)
{
    const char *why;
    uint8_t dev_log2;
    fs_err_t e;

    memset(fs, 0, sizeof *fs);
    if (!dev_sector_ok(bd))
        return FS_ENOTSUP;
    if (lba >= bd->sector_count)
        return FS_EINVAL;
    if (count == 0)
        count = bd->sector_count - lba;
    else if (count > bd->sector_count - lba)
        return FS_EINVAL;

    if (bdev_read(bd, lba, fs->buf, 1) != BDEV_OK)
        return FS_EIO;
    e = fat32_parse_bpb(fs->buf, &fs->g, &why);
    if (e == FS_ECORRUPT)
        return fs_corrupt(why);
    if (e != FS_OK)
        return e;

    /* A logical sector may span several device sectors, never the
     * reverse, and has to fit the buffers. */
    dev_log2 = log2_pow2(bd->sector_size);
    if (fs->g.bytes_per_sector > FAT_CFG_MAX_SECTOR || fs->g.sec_shift < dev_log2)
        return FS_ENOTSUP;
    fs->dev_shift = (uint8_t)(fs->g.sec_shift - dev_log2);
    if (fs->g.total_sectors > (count >> fs->dev_shift))
        return fs_corrupt("volume extends beyond its partition or device");

    fs->bd = bd;
    fs->part_lba = lba;
    fs->ino_shift = (uint8_t)(FAT_CLUSTER_BYTES_SHIFT(fs) - 5u);
    fs->ino_ok = fs->g.cluster_count <= (0xFFFFFFFEuL >> fs->ino_shift);
    fs->free_count = FAT32_UNKNOWN;
    fs->fsinfo_free = FAT32_UNKNOWN;
    fs->fsinfo_next = FAT32_UNKNOWN;

    if (fs->g.fsinfo_sector != 0) {
        e = fat_load_buf(fs, fs->g.fsinfo_sector);
        if (e != FS_OK)
            return e;
        fs->fsinfo_valid = (uint8_t)fat32_parse_fsinfo(fs->buf, &fs->g,
                                                       &fs->fsinfo_free,
                                                       &fs->fsinfo_next);
    }
    return FS_OK;
}

fs_err_t fat_probe(bdev_t *bd, uint8_t *buf, uint32_t *lba, uint32_t *count)
{
    fat32_geom_t g;
    mbr_part_t part[4];
    fs_err_t e0;
    int boot_jump, is_mbr;
    unsigned i;

    if (!dev_sector_ok(bd))
        return FS_ENOTSUP;
    if (bd->sector_count == 0)
        return FS_EINVAL;
    if (bdev_read(bd, 0, buf, 1) != BDEV_OK)
        return FS_EIO;

    *lba = 0;
    *count = bd->sector_count;
    e0 = fat32_parse_bpb(buf, &g, 0);
    if (e0 == FS_OK)
        return FS_OK;
    boot_jump = (buf[0] == 0xEBu && buf[2] == 0x90u) || buf[0] == 0xE9u;
    is_mbr = mbr_parse(buf, part);

    for (i = 0; is_mbr && i < 4u; i++) {
        uint32_t plba = part[i].lba, pcount = part[i].count;

        if (part[i].type == MBR_TYPE_EMPTY || mbr_type_is_container(part[i].type))
            continue;
        if (plba == 0 || plba >= bd->sector_count || pcount == 0)
            continue;
        if (bdev_read(bd, plba, buf, 1) != BDEV_OK)
            return FS_EIO;
        if (fat32_parse_bpb(buf, &g, 0) != FS_OK)
            continue;
        /* A table that overstates the partition is left for fat_mount()
         * to judge against the volume's own size. */
        if (pcount > bd->sector_count - plba)
            pcount = bd->sector_count - plba;
        *lba = plba;
        *count = pcount;
        return FS_OK;
    }

    /* Sector 0 starts with the jump every FAT boot sector has, and no
     * partition holds a volume: a damaged volume, not a partition table. */
    if (boot_jump && e0 == FS_ECORRUPT)
        return FS_OK;
    return FS_ENOTSUP;
}

fs_err_t fat_mount_auto(fat_fs_t *fs, bdev_t *bd)
{
    uint32_t lba, count;
    fs_err_t e;

    if (!dev_sector_ok(bd))
        return FS_ENOTSUP;
    e = fat_probe(bd, fs->buf, &lba, &count);
    if (e != FS_OK)
        return e;
    return fat_mount(fs, bd, lba, count);
}

void fat_unmount(fat_fs_t *fs)
{
    memset(fs, 0, sizeof *fs);
}

/* ---- Volume queries ------------------------------------------------------- */

fs_err_t fat_statfs(fat_fs_t *fs, fat_statfs_t *out)
{
    if (fs->free_count == FAT32_UNKNOWN) {
        uint32_t n;
        fs_err_t e = fat_count_free(fs, &n);

        if (e != FS_OK)
            return e;
        fs->free_count = n;
    }
    out->cluster_size = (uint32_t)1 << FAT_CLUSTER_BYTES_SHIFT(fs);
    out->total_clusters = fs->g.cluster_count;
    out->free_clusters = fs->free_count;
    return FS_OK;
}

fs_err_t fat_label(fat_fs_t *fs, char *out)
{
    fat_dir_t d;
    const uint8_t *de;
    uint32_t cluster;
    uint16_t index;
    fs_err_t e;

    e = fat_dir_open_cluster(fs, fs->g.root_cluster, &d);
    if (e != FS_OK)
        return e;
    while ((e = fat_dir_next_raw(&d, &de, &cluster, &index)) == FS_OK) {
        if (fat32_de_kind(de) == FAT_DE_VOLUME) {
            fat32_label_name(de + DIR_Name_OFF, out);
            return FS_OK;
        }
    }
    if (e != FS_ENOENT)
        return e;

    if (fs->g.boot_sig == BS_BOOTSIG_EXTENDED &&
        memcmp(fs->g.volume_label, "NO NAME    ", 11) != 0)
        fat32_label_name(fs->g.volume_label, out);
    else
        out[0] = '\0';
    return FS_OK;
}
