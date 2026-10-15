/*
 * fat32_fsops.c — FAT32 behind the fsops table.
 *
 * Every slot is a thin adapter over the L2 functions. The one piece of
 * behaviour that lives here and nowhere else: directory listings start
 * with "." and "..", for the root as for subdirectories, which is what
 * the Linux driver returns and what fat_dir_read() leaves out.
 */
#include <string.h>

#include "fat32_fsops.h"

#define M(ctx)  ((fat32_mount_t *)(ctx))

static fs_err_t node_of(fat32_mount_t *m, fs_ino_t ino, fat_node_t *out)
{
    if (!m->mounted)
        return FS_EINVAL;
    return fat_node_from_ino(&m->fs, ino, out);
}

/* The slot behind a handle, or NULL if the handle is not one of ours. */
static fat32_dir_slot_t *dir_slot(fat32_mount_t *m, fs_dir_cursor_t *c)
{
    unsigned i;

    for (i = 0; i < FAT32_FSOPS_DIRS; i++) {
        if ((fs_dir_cursor_t *)&m->dirs[i] == c && m->dirs[i].used)
            return &m->dirs[i];
    }
    return 0;
}

static fat32_file_slot_t *file_slot(fat32_mount_t *m, fs_file_t *f)
{
    unsigned i;

    for (i = 0; i < FAT32_FSOPS_FILES; i++) {
        if ((fs_file_t *)&m->files[i] == f && m->files[i].used)
            return &m->files[i];
    }
    return 0;
}

/* ---- Volume ------------------------------------------------------------ */

static fs_err_t f32_mount(void *ctx, bdev_t *bd, unsigned flags)
{
    fat32_mount_t *m = M(ctx);
    fs_err_t e;

    memset(m, 0, sizeof *m);
    if (!(flags & FS_MOUNT_RDONLY))
        return FS_EROFS;
    e = fat_mount(&m->fs, bd, 0, 0);
    if (e != FS_OK)
        return e;
    /* An inode-keyed table cannot serve a volume whose numbers do not
     * fit (data regions over 128 GiB). */
    if (!m->fs.ino_ok) {
        fat_unmount(&m->fs);
        return FS_ENOTSUP;
    }
    m->mounted = 1;
    return FS_OK;
}

static fs_err_t f32_unmount(void *ctx)
{
    fat32_mount_t *m = M(ctx);

    fat_unmount(&m->fs);
    memset(m, 0, sizeof *m);
    return FS_OK;
}

static fs_err_t f32_statfs(void *ctx, uint64_t *blocks, uint64_t *bfree,
                           uint64_t *files, uint64_t *ffree, uint32_t *bsize)
{
    fat32_mount_t *m = M(ctx);
    fat_statfs_t st;
    fs_err_t e;

    if (!m->mounted)
        return FS_EINVAL;
    e = fat_statfs(&m->fs, &st);
    if (e != FS_OK)
        return e;
    *blocks = st.total_clusters;
    *bfree = st.free_clusters;
    *files = 0;                     /* FAT has no inode table to count */
    *ffree = 0;
    *bsize = st.cluster_size;
    return FS_OK;
}

/* ---- Read ------------------------------------------------------------------ */

static fs_err_t f32_getattr(void *ctx, fs_ino_t ino, fs_attr_t *out)
{
    fat32_mount_t *m = M(ctx);
    fat_node_t node;
    fs_err_t e = node_of(m, ino, &node);

    if (e != FS_OK)
        return e;
    return fat_getattr(&m->fs, &node, out);
}

static fs_err_t f32_lookup(void *ctx, fs_ino_t parent, const char *name,
                           fs_ino_t *out)
{
    fat32_mount_t *m = M(ctx);
    fat_node_t dir, node;
    fs_err_t e;

    /* One component: a driver never sees a '/' (Y1.8). */
    if (strchr(name, '/') != 0)
        return FS_EINVAL;
    e = node_of(m, parent, &dir);
    if (e == FS_OK)
        e = fat_lookup(&m->fs, &dir, name, &node);
    if (e == FS_OK)
        *out = node.ino;
    return e;
}

static fs_err_t f32_readlink(void *ctx, fs_ino_t ino, char *buf, size_t bufsz)
{
    fat_node_t node;
    fs_err_t e = node_of(M(ctx), ino, &node);

    (void)buf;
    (void)bufsz;
    return e != FS_OK ? e : FS_EINVAL;      /* FAT has no symbolic links */
}

/* ---- Directory ------------------------------------------------------------------ */

static fs_err_t f32_opendir(void *ctx, fs_ino_t ino, fs_dir_cursor_t **out)
{
    fat32_mount_t *m = M(ctx);
    fat_node_t node;
    unsigned i;
    fs_err_t e = node_of(m, ino, &node);

    if (e != FS_OK)
        return e;
    if (!fat_node_is_dir(&node))
        return FS_ENOTDIR;
    for (i = 0; i < FAT32_FSOPS_DIRS && m->dirs[i].used; i++)
        ;
    if (i == FAT32_FSOPS_DIRS)
        return FS_EMFILE;

    e = fat_dir_open(&m->fs, &node, &m->dirs[i].d);
    if (e != FS_OK)
        return e;
    m->dirs[i].used = 1;
    m->dirs[i].dots = 0;
    m->dirs[i].self = ino;
    *out = (fs_dir_cursor_t *)&m->dirs[i];
    return FS_OK;
}

static void put_name(fs_dirent_t *out, const char *name)
{
    size_t n = strlen(name);

    out->name_len = (uint16_t)n;
    memcpy(out->name, name, n + 1u);
}

static fs_err_t f32_readdir(void *ctx, fs_dir_cursor_t *c, fs_dirent_t *out)
{
    fat32_mount_t *m = M(ctx);
    fat32_dir_slot_t *s = dir_slot(m, c);
    fat_dirent_t *de = &m->scratch;
    fs_err_t e;

    if (s == 0)
        return FS_EINVAL;

    if (s->dots < 2u) {
        fat_node_t self, parent;

        out->type = FS_DT_DIR;
        if (s->dots++ == 0) {
            out->ino = s->self;
            put_name(out, ".");
            return FS_OK;
        }
        e = node_of(m, s->self, &self);
        if (e == FS_OK)
            e = fat_parent(&m->fs, &self, &parent);
        if (e != FS_OK)
            return e;
        out->ino = parent.ino;
        put_name(out, "..");
        return FS_OK;
    }

    e = fat_dir_read(&s->d, de);
    if (e != FS_OK)
        return e;
    out->ino = de->node.ino;
    out->type = fat_node_is_dir(&de->node) ? FS_DT_DIR : FS_DT_REG;
    /* A long name can take 765 bytes of UTF-8 and the entry holds 255.
     * The 8.3 alias always fits, and looking it up finds the same file. */
    put_name(out, de->name_len < sizeof out->name ? de->name : de->short_name);
    return FS_OK;
}

static fs_err_t f32_closedir(void *ctx, fs_dir_cursor_t *c)
{
    fat32_dir_slot_t *s = dir_slot(M(ctx), c);

    if (s == 0)
        return FS_EINVAL;
    s->used = 0;
    return FS_OK;
}

/* ---- File ---------------------------------------------------------------------- */

static fs_err_t f32_open(void *ctx, fs_ino_t ino, int flags, fs_file_t **out)
{
    fat32_mount_t *m = M(ctx);
    fat_node_t node;
    unsigned i;
    fs_err_t e;

    if ((flags & FS_O_ACCMODE) != FS_O_RDONLY)
        return FS_EROFS;
    e = node_of(m, ino, &node);
    if (e != FS_OK)
        return e;
    if (fat_node_is_dir(&node))
        return FS_EISDIR;
    for (i = 0; i < FAT32_FSOPS_FILES && m->files[i].used; i++)
        ;
    if (i == FAT32_FSOPS_FILES)
        return FS_EMFILE;

    e = fat_file_open(&m->fs, &node, &m->files[i].f);
    if (e != FS_OK)
        return e;
    m->files[i].used = 1;
    *out = (fs_file_t *)&m->files[i];
    return FS_OK;
}

static fs_err_t f32_read(void *ctx, fs_file_t *f, void *buf, size_t len,
                         uint64_t off, size_t *got)
{
    fat32_file_slot_t *s = file_slot(M(ctx), f);
    uint32_t len32 = (uint32_t)len, got32;
    fs_err_t e;

    *got = 0;
    if (s == 0)
        return FS_EINVAL;
    /* FAT32 is 32-bit throughout: nothing lies at or past 4 GiB. */
    if (off > 0xFFFFFFFFu)
        return FS_OK;
    if ((size_t)len32 != len)
        len32 = 0xFFFFFFFFu;
    e = fat_file_pread(&s->f, buf, len32, (uint32_t)off, &got32);
    *got = (size_t)got32;           /* got32 <= len, so it fits */
    return e;
}

static fs_err_t f32_close(void *ctx, fs_file_t *f)
{
    fat32_file_slot_t *s = file_slot(M(ctx), f);

    if (s == 0)
        return FS_EINVAL;
    s->used = 0;
    return FS_OK;
}

const fsops_t fat32_fsops = {
    "fat32",
    f32_mount, f32_unmount, f32_statfs,
    f32_getattr, f32_lookup, f32_readlink,
    f32_opendir, f32_readdir, f32_closedir,
    f32_open, f32_read, f32_close,
    /* write half: read-only driver (Y1.9) */
    0, 0, 0, 0, 0, 0, 0, 0, 0
};
