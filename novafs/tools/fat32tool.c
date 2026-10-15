/*
 * fat32tool.c — command-line front end to the read-only FAT32 driver.
 *
 * Most commands go through the fsops table, exactly as a caller of the
 * table would. The tool resolves paths itself, one lookup per
 * component, because path resolution belongs to the caller and the
 * driver never sees a '/' (Y1.11). `info` and `dir` show the FAT view
 * through L2 directly, which is the difference sheet Y4.15 is about:
 * what the filesystem stores against what a caller is told.
 *
 * Host-only: it uses stdio and the heap freely.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bdev.h"
#include "fat32_fs.h"
#include "fat32_fsops.h"
#include "fsops.h"

#define TREE_MAX_DEPTH  64      /* a directory loop must not recurse forever */
#define PATH_BUF        8192

static const char *progname = "fat32tool";
static const fsops_t *ops = &fat32_fsops;
static fat32_mount_t mnt;       /* the ctx of every fsops call */
#define FS  (&mnt.fs)           /* the same volume, seen through L2 */

static int fail(const char *what, fs_err_t e)
{
    fprintf(stderr, "%s: %s: %s\n", progname, what, fs_strerror(e));
    return 1;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: %s [-s SECTOR_SIZE] [-o LBA] IMAGE COMMAND [ARGS]\n"
        "\n"
        "  -s SECTOR_SIZE  device sector size in bytes (default 512)\n"
        "  -o LBA          volume start, in device sectors (default: probe)\n"
        "\n"
        "commands, through the fsops table:\n"
        "  ls [PATH]                  list a directory as a caller sees it\n"
        "  tree [PATH]                recursive listing with CRC-32 of each file\n"
        "  stat PATH                  attributes, and the FAT entry behind them\n"
        "  cat PATH [OFFSET [LENGTH]] file contents to stdout\n"
        "  check                      cross-check the table against L2\n"
        "\n"
        "commands, through L2:\n"
        "  info                       volume geometry and free space\n"
        "  dir [PATH]                 list a directory as FAT stores it\n",
        progname);
}

/* ---- Formatting ------------------------------------------------------------ */

/* Times carry no zone (Y1.17), so they print as UTC. */
static void fmt_unix(char *out, size_t sz, int64_t t)
{
    time_t tt = (time_t)t;
    struct tm tm;

    gmtime_r(&tt, &tm);
    strftime(out, sz, "%Y-%m-%d %H:%M:%S", &tm);
}

static void fmt_fat_time(char *out, size_t sz, uint16_t date, uint16_t time)
{
    fat_datetime_t dt;

    fat32_decode_datetime(date, time, 0, &dt);
    snprintf(out, sz, "%04u-%02u-%02u %02u:%02u:%02u",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}

static void fmt_attr(char *out, uint8_t attr)
{
    out[0] = (attr & FAT_ATTR_DIRECTORY) ? 'd' : '-';
    out[1] = (attr & FAT_ATTR_READ_ONLY) ? 'r' : '-';
    out[2] = (attr & FAT_ATTR_HIDDEN)    ? 'h' : '-';
    out[3] = (attr & FAT_ATTR_SYSTEM)    ? 's' : '-';
    out[4] = (attr & FAT_ATTR_ARCHIVE)   ? 'a' : '-';
    out[5] = '\0';
}

static void fmt_mode(char *out, uint32_t mode)
{
    static const char rwx[] = "rwxrwxrwx";
    int i;

    out[0] = (mode & FS_S_IFMT) == FS_S_IFDIR ? 'd' :
             (mode & FS_S_IFMT) == FS_S_IFLNK ? 'l' : '-';
    for (i = 0; i < 9; i++)
        out[1 + i] = (mode & (0400u >> i)) ? rwx[i] : '-';
    out[10] = '\0';
}

static int is_dir(const fs_attr_t *a)
{
    return (a->mode & FS_S_IFMT) == FS_S_IFDIR;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    static int ready;
    size_t i;

    if (!ready) {
        uint32_t c, k;
        for (i = 0; i < 256; i++) {
            c = (uint32_t)i;
            for (k = 0; k < 8; k++)
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = 1;
    }
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

/* ---- The caller's half: path resolution and whole listings ------------------ */

/* One lookup per component, "." and ".." included, since the driver
 * resolves both. A trailing '/' requires a directory. */
static fs_err_t resolve(const char *path, fs_ino_t *out)
{
    char comp[FAT_CFG_NAME_MAX + 1];
    fs_ino_t cur = FS_INO_ROOT;
    int want_dir = 0;
    fs_err_t e;

    for (;;) {
        size_t n;

        while (*path == '/')
            path++;
        if (*path == '\0')
            break;
        n = strcspn(path, "/");
        if (n > FAT_CFG_NAME_MAX)
            return FS_ENAMETOOLONG;
        memcpy(comp, path, n);
        comp[n] = '\0';
        path += n;
        want_dir = *path == '/';
        e = ops->lookup(&mnt, cur, comp, &cur);
        if (e != FS_OK)
            return e;
    }
    if (want_dir) {
        fs_attr_t a;

        e = ops->getattr(&mnt, cur, &a);
        if (e != FS_OK)
            return e;
        if (!is_dir(&a))
            return FS_ENOTDIR;
    }
    *out = cur;
    return FS_OK;
}

typedef struct {
    fs_dirent_t *e;
    size_t       n, cap;
} listing_t;

/* A whole directory, with the cursor closed again before returning, so
 * a recursive walk holds one cursor at a time whatever its depth. */
static fs_err_t list_dir(fs_ino_t ino, listing_t *l)
{
    fs_dir_cursor_t *c;
    fs_err_t e, e2;

    l->e = 0;
    l->n = l->cap = 0;
    e = ops->opendir(&mnt, ino, &c);
    if (e != FS_OK)
        return e;
    for (;;) {
        if (l->n == l->cap) {
            fs_dirent_t *grown;

            l->cap = l->cap ? 2 * l->cap : 64;
            grown = realloc(l->e, l->cap * sizeof *l->e);
            if (!grown) {
                e = FS_EIO;
                break;
            }
            l->e = grown;
        }
        e = ops->readdir(&mnt, c, &l->e[l->n]);
        if (e != FS_OK)
            break;
        l->n++;
    }
    e2 = ops->closedir(&mnt, c);
    if (e == FS_ENOENT)
        e = e2;
    if (e != FS_OK) {
        free(l->e);
        l->e = 0;
    }
    return e;
}

static int is_dot(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static int join(char *path, size_t len, const char *name)
{
    size_t n = len, nl = strlen(name);

    if (len + 2 + nl > PATH_BUF)
        return -1;
    if (n == 0 || path[n - 1] != '/')
        path[n++] = '/';
    memcpy(path + n, name, nl + 1);
    return (int)(n + nl);
}

/* Reads a whole file through the table in odd-sized chunks, so that
 * partial sectors are exercised, and copies it out when `copy` is given. */
static fs_err_t read_all(fs_ino_t ino, uint32_t *crc, uint8_t *copy, uint64_t cap)
{
    static uint8_t chunk[3001];
    fs_file_t *f;
    uint64_t off = 0;
    size_t got;
    fs_err_t e, e2;

    *crc = 0;
    e = ops->open(&mnt, ino, FS_O_RDONLY, &f);
    if (e != FS_OK)
        return e;
    do {
        e = ops->read(&mnt, f, chunk, sizeof chunk, off, &got);
        if (e != FS_OK)
            break;
        *crc = crc32_update(*crc, chunk, got);
        if (copy && off + got <= cap)
            memcpy(copy + off, chunk, got);
        off += got;
    } while (got > 0);
    e2 = ops->close(&mnt, f);
    return e != FS_OK ? e : e2;
}

/* ---- Commands through the table --------------------------------------------- */

static int cmd_ls(const char *path)
{
    listing_t l;
    fs_ino_t ino;
    size_t i;
    fs_err_t e = resolve(path, &ino);

    if (e == FS_OK)
        e = list_dir(ino, &l);
    if (e != FS_OK)
        return fail(path, e);
    for (i = 0; i < l.n; i++) {
        fs_attr_t a;
        char mode[11], mtime[32];

        e = ops->getattr(&mnt, l.e[i].ino, &a);
        if (e != FS_OK) {
            int rc = fail(l.e[i].name, e);

            free(l.e);
            return rc;
        }
        fmt_mode(mode, a.mode);
        fmt_unix(mtime, sizeof mtime, a.mtime);
        printf("%s %3u %u %u %10llu %s  %s\n", mode, a.nlink, a.uid, a.gid,
               (unsigned long long)a.size, mtime, l.e[i].name);
    }
    free(l.e);
    return 0;
}

static int tree_walk(fs_ino_t dir, char *path, size_t len, int depth)
{
    listing_t l;
    size_t i;
    int rc = 0;
    fs_err_t e;

    if (depth > TREE_MAX_DEPTH)
        return fail(path, FS_ELOOP);
    e = list_dir(dir, &l);
    if (e != FS_OK)
        return fail(path, e);

    for (i = 0; rc == 0 && i < l.n; i++) {
        fs_attr_t a;
        char mtime[32];
        int n;

        if (is_dot(l.e[i].name))
            continue;
        n = join(path, len, l.e[i].name);
        if (n < 0) {
            rc = fail(path, FS_ENAMETOOLONG);
            break;
        }
        e = ops->getattr(&mnt, l.e[i].ino, &a);
        if (e != FS_OK) {
            rc = fail(path, e);
            break;
        }
        fmt_unix(mtime, sizeof mtime, a.mtime);
        if (is_dir(&a)) {
            printf("D %s %s\n", mtime, path);
            rc = tree_walk(l.e[i].ino, path, (size_t)n, depth + 1);
        } else {
            uint32_t crc;

            e = read_all(l.e[i].ino, &crc, 0, 0);
            if (e != FS_OK) {
                rc = fail(path, e);
                break;
            }
            printf("F %llu %08x %s %s\n", (unsigned long long)a.size, crc, mtime, path);
        }
        path[len] = '\0';
    }
    free(l.e);
    return rc;
}

static int cmd_tree(const char *start)
{
    char *path = malloc(PATH_BUF);
    fs_ino_t ino;
    fs_err_t e;
    int rc;

    if (!path)
        return fail(start, FS_EIO);
    e = resolve(start, &ino);
    if (e != FS_OK) {
        free(path);
        return fail(start, e);
    }
    snprintf(path, PATH_BUF, "%s", start);
    rc = tree_walk(ino, path, strlen(path), 0);
    free(path);
    return rc;
}

static int cmd_stat(const char *path)
{
    fs_attr_t a;
    fat_node_t n;
    fs_ino_t ino;
    char mode[11], attr[6], t[32];
    fs_err_t e = resolve(path, &ino);

    if (e == FS_OK)
        e = ops->getattr(&mnt, ino, &a);
    if (e == FS_OK)
        e = fat_node_from_ino(FS, ino, &n);
    if (e != FS_OK)
        return fail(path, e);

    fmt_mode(mode, a.mode);
    printf("ino      %u\n", a.ino);
    printf("type     %s\n", is_dir(&a) ? "directory" : "file");
    printf("mode     0%o (%s)\n", a.mode, mode);
    printf("nlink    %u\n", a.nlink);
    printf("uid      %u\n", a.uid);
    printf("gid      %u\n", a.gid);
    printf("size     %llu\n", (unsigned long long)a.size);
    printf("blocks   %llu\n", (unsigned long long)a.blocks);
    fmt_unix(t, sizeof t, a.mtime);
    printf("mtime    %s\n", t);
    fmt_unix(t, sizeof t, a.ctime);
    printf("ctime    %s\n", t);
    fmt_unix(t, sizeof t, a.atime);
    printf("atime    %s\n", t);
    /* What the FAT entry itself holds. */
    fmt_attr(attr, n.attr);
    printf("cluster  %u\n", n.cluster);
    printf("attr     %s (0x%02x)\n", attr, n.attr);
    return 0;
}

static int cmd_cat(const char *path, uint64_t off, uint64_t len)
{
    static uint8_t buf[65536];
    fs_file_t *f;
    fs_ino_t ino;
    fs_err_t e = resolve(path, &ino);

    if (e == FS_OK)
        e = ops->open(&mnt, ino, FS_O_RDONLY, &f);
    if (e != FS_OK)
        return fail(path, e);
    while (len > 0) {
        size_t want = len < sizeof buf ? (size_t)len : sizeof buf, got;

        e = ops->read(&mnt, f, buf, want, off, &got);
        if (e != FS_OK || got == 0)
            break;
        fwrite(buf, 1, got, stdout);
        off += got;
        len -= got;
    }
    ops->close(&mnt, f);
    return e == FS_OK ? 0 : fail(path, e);
}

/* ---- Commands through L2 ----------------------------------------------------------- */

static int cmd_info(const bdev_t *vol)
{
    uint64_t blocks, bfree, files, ffree;
    uint32_t bsize;
    char label[FAT_SHORT_NAME_BUF];
    fs_err_t e;

    printf("volume_lba          %u\n", vol->part_lba);
    printf("bytes_per_sector    %u\n", FS->g.bytes_per_sector);
    printf("sectors_per_cluster %u\n", FS->g.sectors_per_cluster);
    printf("reserved_sectors    %u\n", FS->g.reserved_sectors);
    printf("num_fats            %u\n", FS->g.num_fats);
    printf("fat_size            %u\n", FS->g.fat_size);
    printf("active_fat          %u\n", FS->g.active_fat);
    printf("total_sectors       %u\n", FS->g.total_sectors);
    printf("fat_start           %u\n", FS->g.fat_start);
    printf("data_start          %u\n", FS->g.data_start);
    printf("cluster_count       %u\n", FS->g.cluster_count);
    printf("root_cluster        %u\n", FS->g.root_cluster);
    printf("volume_id           %08X\n", FS->g.volume_id);
    if (FS->fsinfo_free == FAT32_UNKNOWN)
        printf("fsinfo_free         unknown\n");
    else
        printf("fsinfo_free         %u\n", FS->fsinfo_free);

    e = ops->statfs(&mnt, &blocks, &bfree, &files, &ffree, &bsize);
    if (e != FS_OK)
        return fail("statfs", e);
    printf("cluster_size        %u\n", bsize);
    printf("free_clusters       %llu\n", (unsigned long long)bfree);

    e = fat_label(FS, label);
    if (e != FS_OK)
        return fail("label", e);
    printf("label               %s\n", label);
    return 0;
}

static int cmd_dir(const char *path)
{
    fat_dir_t d;
    fat_node_t node;
    fat_dirent_t *de = malloc(sizeof *de);
    fs_ino_t ino;
    fs_err_t e;

    if (!de)
        return fail(path, FS_EIO);
    e = resolve(path, &ino);
    if (e == FS_OK)
        e = fat_node_from_ino(FS, ino, &node);
    if (e == FS_OK)
        e = fat_dir_open(FS, &node, &d);
    if (e != FS_OK) {
        free(de);
        return fail(path, e);
    }
    while ((e = fat_dir_read(&d, de)) == FS_OK) {
        char attr[6], mtime[32];

        fmt_attr(attr, de->node.attr);
        fmt_fat_time(mtime, sizeof mtime, de->node.wrt_date, de->node.wrt_time);
        printf("%s %10u %s  %-12s  %s\n", attr, de->node.size, mtime,
               de->short_name, de->name);
    }
    free(de);
    return e == FS_ENOENT ? 0 : fail(path, e);
}

/* ---- check: the table and L2 against each other ------------------------------------ */

typedef struct {
    fat_ino_t *inos;
    size_t     n, cap;
    unsigned   errors;
} check_t;

static void check_err(check_t *c, const char *path, const char *what, fs_err_t e)
{
    fprintf(stderr, "check: %s: %s (%s)\n", path, what, fs_strerror(e));
    c->errors++;
}

static int same_node(const fat_node_t *a, const fat_node_t *b)
{
    return a->ino == b->ino && a->cluster == b->cluster && a->size == b->size &&
           a->attr == b->attr && a->wrt_date == b->wrt_date &&
           a->wrt_time == b->wrt_time;
}

/* L2: backward reads against a forward one, which restarts the chain
 * walk from the first cluster on every read. */
static void check_reads(check_t *c, const fat_node_t *node, const char *path,
                        const uint8_t *table_copy)
{
    uint8_t *fwd, *bwd;
    fat_file_t f;
    uint32_t off, got;
    fs_err_t e;

    if (node->size == 0)
        return;
    fwd = malloc(node->size);
    bwd = malloc(node->size);
    if (!fwd || !bwd) {
        free(fwd);
        free(bwd);
        return;
    }
    e = fat_file_open(FS, node, &f);
    if (e == FS_OK)
        e = fat_file_pread(&f, fwd, node->size, 0, &got);
    if (e == FS_OK && got != node->size)
        e = FS_EIO;
    for (off = node->size; e == FS_OK && off > 0; ) {
        uint32_t len = off < 1000u ? off : 1000u;

        off -= len;
        e = fat_file_pread(&f, bwd + off, len, off, &got);
        if (e == FS_OK && got != len)
            e = FS_EIO;
    }
    if (e != FS_OK)
        check_err(c, path, "reading through L2", e);
    else if (memcmp(fwd, bwd, node->size) != 0)
        check_err(c, path, "backward reads differ from a forward read", FS_OK);
    else if (table_copy && memcmp(fwd, table_copy, node->size) != 0)
        check_err(c, path, "the table and L2 read different bytes", FS_OK);
    free(fwd);
    free(bwd);
}

/* One entry, reached through the table: every other route to it must
 * agree. */
static void check_entry(check_t *c, fs_ino_t dir_ino, const fs_dirent_t *de,
                        const char *path)
{
    fat_node_t dir, n, r;
    fat_dirent_t *fde;
    fat_dir_t d;
    fs_attr_t a;
    fs_ino_t ino;
    char upper[sizeof de->name];
    size_t i;
    fs_err_t e;

    if (c->n == c->cap) {
        fat_ino_t *grown;

        c->cap = c->cap ? 2 * c->cap : 1024;
        grown = realloc(c->inos, c->cap * sizeof *c->inos);
        if (!grown) {
            fprintf(stderr, "check: out of memory\n");
            exit(1);
        }
        c->inos = grown;
    }
    c->inos[c->n++] = de->ino;
    if (de->ino == FAT_INO_NONE || de->ino == FAT_INO_ROOT)
        check_err(c, path, "reserved inode number", FS_OK);

    /* The table's own routes. */
    e = ops->lookup(&mnt, dir_ino, de->name, &ino);
    if (e != FS_OK || ino != de->ino)
        check_err(c, path, "lookup by name", e);
    for (i = 0; i <= de->name_len; i++) {
        char ch = de->name[i];
        upper[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 'a' + 'A') : ch;
    }
    e = ops->lookup(&mnt, dir_ino, upper, &ino);
    if (e != FS_OK || ino != de->ino)
        check_err(c, path, "lookup ignoring case", e);
    e = resolve(path, &ino);
    if (e != FS_OK || ino != de->ino)
        check_err(c, path, "path resolution", e);
    e = ops->getattr(&mnt, de->ino, &a);
    if (e != FS_OK || a.ino != de->ino)
        check_err(c, path, "getattr", e);
    else if (is_dir(&a) != (de->type == FS_DT_DIR))
        check_err(c, path, "entry type and mode disagree", FS_OK);

    /* L2's routes to the same entry. */
    e = fat_node_from_ino(FS, dir_ino, &dir);
    if (e == FS_OK)
        e = fat_node_from_ino(FS, de->ino, &n);
    if (e != FS_OK) {
        check_err(c, path, "node from inode number", e);
        return;
    }
    fde = malloc(sizeof *fde);
    e = fat_dir_open(FS, &dir, &d);
    while (e == FS_OK && (e = fat_dir_read(&d, fde)) == FS_OK && fde->node.ino != de->ino)
        ;
    if (e != FS_OK) {
        check_err(c, path, "L2 listing misses the entry", e);
    } else {
        e = fat_lookup(FS, &dir, fde->short_name, &r);
        if (e != FS_OK || !same_node(&n, &r))
            check_err(c, path, "lookup by short name", e);
        if (!same_node(&n, &fde->node))
            check_err(c, path, "node from inode number differs from the listing", FS_OK);
    }
    free(fde);

    if (fat_node_is_dir(&n)) {
        e = fat_parent(FS, &n, &r);
        if (e != FS_OK || r.ino != dir_ino)
            check_err(c, path, "parent", e);
    }
}

static int check_walk(check_t *c, fs_ino_t dir, fs_ino_t parent, char *path,
                      size_t len, int depth)
{
    listing_t l;
    fs_attr_t a;
    uint32_t subdirs = 0;
    size_t i;
    fs_err_t e;

    if (depth > TREE_MAX_DEPTH)
        return fail(path, FS_ELOOP);
    e = list_dir(dir, &l);
    if (e != FS_OK)
        return fail(path, e);
    if (l.n < 2 || strcmp(l.e[0].name, ".") != 0 || l.e[0].ino != dir ||
        strcmp(l.e[1].name, "..") != 0 || l.e[1].ino != parent)
        check_err(c, path, "listing does not start with '.' and '..'", FS_OK);

    for (i = 0; i < l.n; i++) {
        int n;

        if (is_dot(l.e[i].name))
            continue;
        n = join(path, len, l.e[i].name);
        if (n < 0) {
            free(l.e);
            return fail(path, FS_ENAMETOOLONG);
        }
        check_entry(c, dir, &l.e[i], path);
        if (l.e[i].type == FS_DT_DIR) {
            subdirs++;
            if (check_walk(c, l.e[i].ino, dir, path, (size_t)n, depth + 1) != 0) {
                free(l.e);
                return 1;
            }
        } else {
            fs_attr_t fa;
            fat_node_t fn;
            uint32_t crc;
            uint8_t *copy = 0;

            if (ops->getattr(&mnt, l.e[i].ino, &fa) == FS_OK && fa.size > 0)
                copy = malloc((size_t)fa.size);
            e = read_all(l.e[i].ino, &crc, copy, copy ? fa.size : 0);
            if (e != FS_OK)
                check_err(c, path, "reading through the table", e);
            else if (fat_node_from_ino(FS, l.e[i].ino, &fn) == FS_OK)
                check_reads(c, &fn, path, copy);
            free(copy);
        }
        path[len] = '\0';
    }
    free(l.e);

    e = ops->getattr(&mnt, dir, &a);
    if (e != FS_OK || a.nlink != 2u + subdirs)
        check_err(c, path, "directory link count", e);
    return 0;
}

static int cmp_ino(const void *a, const void *b)
{
    fat_ino_t x = *(const fat_ino_t *)a, y = *(const fat_ino_t *)b;
    return x < y ? -1 : x > y;
}

static int cmd_check(void)
{
    check_t c = { 0 };
    char *path = malloc(PATH_BUF);
    size_t i;
    int rc;

    if (!path)
        return fail("check", FS_EIO);
    strcpy(path, "/");
    rc = check_walk(&c, FS_INO_ROOT, FS_INO_ROOT, path, 1, 0);
    free(path);
    if (rc != 0) {
        free(c.inos);
        return rc;
    }

    if (c.n > 1)                    /* qsort(NULL, 0, ...) is undefined */
        qsort(c.inos, c.n, sizeof *c.inos, cmp_ino);
    for (i = 1; i < c.n; i++) {
        if (c.inos[i] == c.inos[i - 1]) {
            fprintf(stderr, "check: inode number %u used twice\n", c.inos[i]);
            c.errors++;
        }
    }
    free(c.inos);
    for (i = 0; i < FAT32_FSOPS_DIRS; i++)
        if (mnt.dirs[i].used)
            check_err(&c, "/", "a directory cursor was left open", FS_OK);
    for (i = 0; i < FAT32_FSOPS_FILES; i++)
        if (mnt.files[i].used)
            check_err(&c, "/", "a file was left open", FS_OK);
    if (c.errors) {
        fprintf(stderr, "check: %u errors\n", c.errors);
        return 1;
    }
    printf("check: %zu entries OK\n", c.n);
    return 0;
}

/* ---- main ------------------------------------------------------------------------- */

static int parse_u32(const char *s, uint32_t *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 0);

    if (*s == '\0' || *end != '\0' || v > 0xFFFFFFFFul)
        return 0;
    *out = (uint32_t)v;
    return 1;
}

int main(int argc, char **argv)
{
    static uint8_t probe_buf[FAT_CFG_MAX_SECTOR];
    uint32_t sector_size = 512, lba = 0, count = 0;
    int have_lba = 0, i = 1, rc;
    const char *image, *cmd;
    bdev_t dev, vol;
    fs_err_t e;

    for (; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &sector_size))
                return usage(), 2;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &lba))
                return usage(), 2;
            have_lba = 1;
        } else {
            return usage(), 2;
        }
    }
    if (argc - i < 2)
        return usage(), 2;
    image = argv[i++];
    cmd = argv[i++];

    if (bdev_file_open(&dev, image, sector_size) != BDEV_OK) {
        fprintf(stderr, "%s: %s: cannot open\n", progname, image);
        return 1;
    }

    /* Finding the volume is below the driver: the table mounts a device
     * that is the volume. */
    if (have_lba) {
        e = lba < dev.sector_count ? FS_OK : FS_EINVAL;
        count = dev.sector_count - lba;
    } else {
        e = fat_probe(&dev, probe_buf, &lba, &count);
    }
    if (e == FS_OK && bdev_part_open(&vol, &dev, lba, count) != BDEV_OK)
        e = FS_EINVAL;
    if (e == FS_OK)
        e = ops->mount(&mnt, &vol, FS_MOUNT_RDONLY);
    if (e != FS_OK) {
        rc = fail(image, e);
        bdev_close(&dev);
        return rc;
    }

    if (strcmp(cmd, "info") == 0 && argc == i) {
        rc = cmd_info(&vol);
    } else if (strcmp(cmd, "ls") == 0 && argc - i <= 1) {
        rc = cmd_ls(argc > i ? argv[i] : "/");
    } else if (strcmp(cmd, "dir") == 0 && argc - i <= 1) {
        rc = cmd_dir(argc > i ? argv[i] : "/");
    } else if (strcmp(cmd, "tree") == 0 && argc - i <= 1) {
        rc = cmd_tree(argc > i ? argv[i] : "/");
    } else if (strcmp(cmd, "stat") == 0 && argc - i == 1) {
        rc = cmd_stat(argv[i]);
    } else if (strcmp(cmd, "cat") == 0 && argc - i >= 1 && argc - i <= 3) {
        uint32_t off = 0, len = 0xFFFFFFFFu;
        if ((argc - i >= 2 && !parse_u32(argv[i + 1], &off)) ||
            (argc - i == 3 && !parse_u32(argv[i + 2], &len)))
            rc = (usage(), 2);
        else
            rc = cmd_cat(argv[i], off, len);
    } else if (strcmp(cmd, "check") == 0 && argc == i) {
        rc = cmd_check();
    } else {
        usage();
        rc = 2;
    }

    ops->unmount(&mnt);
    bdev_close(&dev);
    return rc;
}
