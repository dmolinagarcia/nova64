/*
 * fat32_dir.c — L2: directory iteration, lookup and path resolution.
 */
#include <string.h>

#include "endian.h"
#include "fat32_priv.h"

#if FAT_CFG_NAME_MAX < FAT_SHORT_NAME_BUF - 1u
#error "FAT_CFG_NAME_MAX must hold any short name"
#endif

void fat_root(const fat_fs_t *fs, fat_node_t *out)
{
    memset(out, 0, sizeof *out);
    out->ino = FAT_INO_ROOT;
    out->cluster = fs->g.root_cluster;
    out->attr = FAT_ATTR_DIRECTORY;
}

/*
 * Inode numbers (FUS-03.g, Y4.6): the index of the short entry
 * counted across the data region, plus 2, so that 1 stays the root's.
 * It fits in 32 bits for data regions up to 128 GiB; beyond that the
 * volume still mounts, with every number FAT_INO_NONE.
 */
void fat_node_decode(const fat_fs_t *fs, const uint8_t *de,
                     uint32_t cluster, uint16_t index, fat_node_t *out)
{
    out->attr      = de[DIR_Attr_OFF];
    out->cluster   = fat32_de_cluster(de);
    out->size      = (out->attr & FAT_ATTR_DIRECTORY) ? 0 : rd32(de + DIR_FileSize_OFF);
    out->crt_tenth = de[DIR_CrtTimeTenth_OFF];
    out->crt_time  = rd16(de + DIR_CrtTime_OFF);
    out->crt_date  = rd16(de + DIR_CrtDate_OFF);
    out->acc_date  = rd16(de + DIR_LstAccDate_OFF);
    out->wrt_time  = rd16(de + DIR_WrtTime_OFF);
    out->wrt_date  = rd16(de + DIR_WrtDate_OFF);
    out->ino = fs->ino_ok ? 2u + ((cluster - 2u) << fs->ino_shift) + index
                          : FAT_INO_NONE;
}

/* ---- Iteration ----------------------------------------------------------- */

fs_err_t fat_dir_open_cluster(fat_fs_t *fs, uint32_t cluster, fat_dir_t *d)
{
    fs_err_t e;

    d->fs = fs;
    d->first = cluster;
    e = fat_chain_start(fs, &d->chain, cluster);
    if (e != FS_OK)
        return e;
    d->index = 0;
    d->done = 0;
    d->seen = 0;
    fat_lfn_reset(d);
    return FS_OK;
}

fs_err_t fat_dir_open(fat_fs_t *fs, const fat_node_t *dir, fat_dir_t *d)
{
    if (!fat_node_is_dir(dir))
        return FS_ENOTDIR;
    return fat_dir_open_cluster(fs, dir->cluster, d);
}

void fat_dir_rewind(fat_dir_t *d)
{
    /* Cannot fail: the first cluster was validated when d was opened. */
    (void)fat_dir_open_cluster(d->fs, d->first, d);
}

fs_err_t fat_dir_next_raw(fat_dir_t *d, const uint8_t **de,
                          uint32_t *cluster, uint16_t *index)
{
    fat_fs_t *fs = d->fs;
    uint8_t   eps_shift = (uint8_t)(fs->g.sec_shift - 5u);
    uint32_t  epc = (uint32_t)1 << (FAT_CLUSTER_BYTES_SHIFT(fs) - 5u);
    const uint8_t *p;
    fs_err_t  e;

    if (d->done)
        return FS_ENOENT;
    if (d->index == epc) {
        e = fat_chain_next(fs, &d->chain);
        if (e == FS_ENOENT)
            d->done = 1;
        if (e != FS_OK)
            return e;
        d->index = 0;
    }
    if (d->seen >= FAT_DIR_MAX_ENTRIES)
        return fs_corrupt("directory larger than 65536 entries");

    e = fat_load_buf(fs, fat_cluster_sector(fs, d->chain.cur) +
                         ((uint32_t)d->index >> eps_shift));
    if (e != FS_OK)
        return e;
    p = fs->buf + (((uint32_t)d->index & ((1u << eps_shift) - 1u)) << 5);

    /* A zero first byte marks this entry and every later one as free:
     * stop, do not skip. */
    if (p[DIR_Name_OFF] == DIR_NAME_END) {
        d->done = 1;
        return FS_ENOENT;
    }
    *de = p;
    *cluster = d->chain.cur;
    *index = d->index;
    d->index++;
    d->seen++;
    return FS_OK;
}

fs_err_t fat_dir_read(fat_dir_t *d, fat_dirent_t *out)
{
    const uint8_t *de;
    uint32_t cluster;
    uint16_t index;
    int32_t n;
    fs_err_t e;

    for (;;) {
        e = fat_dir_next_raw(d, &de, &cluster, &index);
        if (e != FS_OK)
            return e;
        switch (fat32_de_kind(de)) {
        case FAT_DE_LFN:
            fat_lfn_feed(d, de);
            continue;
        case FAT_DE_FILE:
            break;
        default:                /* deleted, label, dot or invalid */
            fat_lfn_reset(d);
            continue;
        }

        fat_node_decode(d->fs, de, cluster, index, &out->node);
        fat32_short_name(de, out->short_name);
        n = fat_lfn_take(d, de, out->name, FAT_CFG_NAME_MAX);
        if (n > 0) {
            out->flags = FAT_DE_NAME_LONG;
            out->name_len = (uint16_t)n;
        } else {
            out->flags = n < 0 ? FAT_DE_NAME_SHORTENED : 0;
            out->name_len = fat32_short_name(de, out->name);
        }
        return FS_OK;
    }
}

/* ---- Lookup ------------------------------------------------------------- */

static uint8_t fold(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
}

/* Case-insensitive for ASCII only, which is a documented deviation from
 * the kernel driver, whose folding depends on its iocharset. */
static int name_eq(const char *a, uint32_t alen, const char *b)
{
    uint32_t i;

    for (i = 0; i < alen; i++) {
        if (b[i] == '\0' || fold((uint8_t)a[i]) != fold((uint8_t)b[i]))
            return 0;
    }
    return b[alen] == '\0';
}

/* The cluster named by the ".." entry of a subdirectory, which is always
 * its second entry. On disk, 0 there means the root. */
static fs_err_t dotdot_of(fat_fs_t *fs, uint32_t dir_cluster, uint32_t *parent)
{
    const uint8_t *de;
    fs_err_t e;

    if (dir_cluster < 2u || dir_cluster > fs->g.max_cluster)
        return fs_corrupt("directory cluster out of range");
    e = fat_load_buf(fs, fat_cluster_sector(fs, dir_cluster));
    if (e != FS_OK)
        return e;
    de = fs->buf + DIR_ENTRY_SIZE;
    if (!fat32_de_is_dotdot(de))
        return fs_corrupt("subdirectory without a '..' entry");
    *parent = fat32_de_cluster(de);
    if (*parent == 0)
        *parent = fs->g.root_cluster;
    return FS_OK;
}

fs_err_t fat_parent(fat_fs_t *fs, const fat_node_t *dir, fat_node_t *out)
{
    fat_dir_t d;
    fat_dirent_t de;
    uint32_t p, gp;
    fs_err_t e;

    if (!fat_node_is_dir(dir))
        return FS_ENOTDIR;
    if (dir->ino == FAT_INO_ROOT || dir->cluster == fs->g.root_cluster) {
        fat_root(fs, out);
        return FS_OK;
    }
    e = dotdot_of(fs, dir->cluster, &p);
    if (e != FS_OK)
        return e;
    if (p == fs->g.root_cluster) {
        fat_root(fs, out);
        return FS_OK;
    }

    /* ".." only gives the parent's cluster. Its node, and so its number,
     * comes from its own entry, which lives in the grandparent. */
    e = dotdot_of(fs, p, &gp);
    if (e != FS_OK)
        return e;
    e = fat_dir_open_cluster(fs, gp, &d);
    if (e != FS_OK)
        return e;
    while ((e = fat_dir_read(&d, &de)) == FS_OK) {
        if (fat_node_is_dir(&de.node) && de.node.cluster == p) {
            *out = de.node;
            return FS_OK;
        }
    }
    if (e == FS_ENOENT)
        return fs_corrupt("directory missing from the parent its '..' names");
    return e;
}

static fs_err_t lookup_n(fat_fs_t *fs, const fat_node_t *dir,
                         const char *name, uint32_t len, fat_node_t *out)
{
    fat_dir_t d;
    fat_dirent_t de;
    fs_err_t e;

    if (!fat_node_is_dir(dir))
        return FS_ENOTDIR;
    if (len == 0)
        return FS_ENOENT;
    if (len == 1 && name[0] == '.') {
        *out = *dir;
        return FS_OK;
    }
    if (len == 2 && name[0] == '.' && name[1] == '.')
        return fat_parent(fs, dir, out);
    if (len > FAT_CFG_NAME_MAX)
        return FS_ENAMETOOLONG;

    e = fat_dir_open(fs, dir, &d);
    if (e != FS_OK)
        return e;
    while ((e = fat_dir_read(&d, &de)) == FS_OK) {
        if (name_eq(name, len, de.name) || name_eq(name, len, de.short_name)) {
            *out = de.node;
            return FS_OK;
        }
    }
    return e;
}

fs_err_t fat_lookup(fat_fs_t *fs, const fat_node_t *dir, const char *name,
                    fat_node_t *out)
{
    return lookup_n(fs, dir, name, (uint32_t)strlen(name), out);
}

fs_err_t fat_resolve(fat_fs_t *fs, const fat_node_t *base, const char *path,
                     fat_node_t *out)
{
    fat_node_t cur, next;
    int want_dir = 0;
    fs_err_t e;

    if (base == 0 || path[0] == '/')
        fat_root(fs, &cur);
    else
        cur = *base;

    for (;;) {
        const char *end;

        while (*path == '/')
            path++;
        if (*path == '\0')
            break;
        for (end = path; *end != '\0' && *end != '/'; end++)
            ;
        e = lookup_n(fs, &cur, path, (uint32_t)(end - path), &next);
        if (e != FS_OK)
            return e;
        cur = next;
        path = end;
        want_dir = *path == '/';
    }
    if (want_dir && !fat_node_is_dir(&cur))
        return FS_ENOTDIR;
    *out = cur;
    return FS_OK;
}

fs_err_t fat_node_from_ino(fat_fs_t *fs, fat_ino_t ino, fat_node_t *out)
{
    uint8_t  eps_shift = (uint8_t)(fs->g.sec_shift - 5u);
    uint32_t rel, cluster;
    uint16_t index;
    const uint8_t *de;
    fs_err_t e;

    if (ino == FAT_INO_ROOT) {
        fat_root(fs, out);
        return FS_OK;
    }
    if (!fs->ino_ok)
        return FS_ENOTSUP;
    if (ino < 2u)
        return FS_EINVAL;

    rel = ino - 2u;
    cluster = (rel >> fs->ino_shift) + 2u;
    index = (uint16_t)(rel & (((uint32_t)1 << fs->ino_shift) - 1u));
    if (cluster > fs->g.max_cluster)
        return FS_EINVAL;

    e = fat_load_buf(fs, fat_cluster_sector(fs, cluster) +
                         ((uint32_t)index >> eps_shift));
    if (e != FS_OK)
        return e;
    de = fs->buf + (((uint32_t)index & ((1u << eps_shift) - 1u)) << 5);
    if (fat32_de_kind(de) != FAT_DE_FILE)
        return FS_ENOENT;
    fat_node_decode(fs, de, cluster, index, out);
    return FS_OK;
}

fs_err_t fat_stat(fat_fs_t *fs, const char *path, fat_node_t *out)
{
    return fat_resolve(fs, 0, path, out);
}

fs_err_t fat_opendir(fat_fs_t *fs, const char *path, fat_dir_t *d)
{
    fat_node_t node;
    fs_err_t e = fat_resolve(fs, 0, path, &node);

    if (e != FS_OK)
        return e;
    return fat_dir_open(fs, &node, d);
}
