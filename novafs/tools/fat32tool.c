/*
 * fat32tool.c — command-line front end to the read-only FAT32 library.
 *
 * Exists to exercise the library against real images, from a shell and
 * from the test harness. Host-only: it uses stdio and the heap freely.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bdev.h"
#include "fat32_fs.h"

#define TREE_MAX_DEPTH  64      /* a directory loop must not recurse forever */
#define PATH_BUF        8192

static const char *progname = "fat32tool";

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
        "commands:\n"
        "  info                       volume geometry and free space\n"
        "  ls [PATH]                  list a directory\n"
        "  tree [PATH]                recursive listing with CRC-32 of each file\n"
        "  stat PATH                  one entry in detail\n"
        "  cat PATH [OFFSET [LENGTH]] file contents to stdout\n"
        "  check                      self-consistency of lookup, numbering and reads\n",
        progname);
}

/* ---- Formatting ------------------------------------------------------------ */

static void fmt_time(char *out, size_t sz, uint16_t date, uint16_t time, uint8_t tenth)
{
    fat_datetime_t dt;

    fat32_decode_datetime(date, time, tenth, &dt);
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

/* ---- Commands --------------------------------------------------------------- */

static int cmd_info(fat_fs_t *fs)
{
    fat_statfs_t st;
    char label[FAT_SHORT_NAME_BUF];
    fs_err_t e;

    printf("volume_lba          %u\n", fs->part_lba);
    printf("bytes_per_sector    %u\n", fs->g.bytes_per_sector);
    printf("sectors_per_cluster %u\n", fs->g.sectors_per_cluster);
    printf("reserved_sectors    %u\n", fs->g.reserved_sectors);
    printf("num_fats            %u\n", fs->g.num_fats);
    printf("fat_size            %u\n", fs->g.fat_size);
    printf("active_fat          %u\n", fs->g.active_fat);
    printf("total_sectors       %u\n", fs->g.total_sectors);
    printf("fat_start           %u\n", fs->g.fat_start);
    printf("data_start          %u\n", fs->g.data_start);
    printf("cluster_count       %u\n", fs->g.cluster_count);
    printf("root_cluster        %u\n", fs->g.root_cluster);
    printf("volume_id           %08X\n", fs->g.volume_id);
    if (fs->fsinfo_free == FAT32_UNKNOWN)
        printf("fsinfo_free         unknown\n");
    else
        printf("fsinfo_free         %u\n", fs->fsinfo_free);

    e = fat_statfs(fs, &st);
    if (e != FS_OK)
        return fail("statfs", e);
    printf("cluster_size        %u\n", st.cluster_size);
    printf("free_clusters       %u\n", st.free_clusters);

    e = fat_label(fs, label);
    if (e != FS_OK)
        return fail("label", e);
    printf("label               %s\n", label);
    return 0;
}

static int cmd_ls(fat_fs_t *fs, const char *path)
{
    fat_dir_t d;
    fat_dirent_t *de = malloc(sizeof *de);
    fs_err_t e;

    if (!de)
        return fail(path, FS_EIO);
    e = fat_opendir(fs, path, &d);
    if (e != FS_OK) {
        free(de);
        return fail(path, e);
    }
    while ((e = fat_dir_read(&d, de)) == FS_OK) {
        char attr[6], mtime[32];

        fmt_attr(attr, de->node.attr);
        fmt_time(mtime, sizeof mtime, de->node.wrt_date, de->node.wrt_time, 0);
        printf("%s %10u %s  %-12s  %s\n", attr, de->node.size, mtime,
               de->short_name, de->name);
    }
    free(de);
    return e == FS_ENOENT ? 0 : fail(path, e);
}

static fs_err_t crc_file(fat_fs_t *fs, const fat_node_t *node, uint32_t *crc)
{
    static uint8_t chunk[3001];     /* odd on purpose: exercises partial sectors */
    fat_file_t f;
    uint32_t got;
    fs_err_t e;

    *crc = 0;
    e = fat_file_open(fs, node, &f);
    if (e != FS_OK)
        return e;
    do {
        e = fat_file_read(&f, chunk, sizeof chunk, &got);
        if (e != FS_OK)
            return e;
        *crc = crc32_update(*crc, chunk, got);
    } while (got > 0);
    return FS_OK;
}

static int tree_walk(fat_fs_t *fs, const fat_node_t *dir, char *path, size_t len,
                     int depth)
{
    fat_dir_t d;
    fat_dirent_t *de;
    fs_err_t e;
    int rc = 0;

    if (depth > TREE_MAX_DEPTH)
        return fail(path, FS_ELOOP);
    de = malloc(sizeof *de);
    if (!de)
        return fail(path, FS_EIO);
    e = fat_dir_open(fs, dir, &d);
    if (e != FS_OK) {
        free(de);
        return fail(path, e);
    }

    while (rc == 0 && (e = fat_dir_read(&d, de)) == FS_OK) {
        char mtime[32];
        size_t n = len;

        if (len + 1 + de->name_len + 1 > PATH_BUF) {
            rc = fail(path, FS_ENAMETOOLONG);
            break;
        }
        if (n == 0 || path[n - 1] != '/')
            path[n++] = '/';
        memcpy(path + n, de->name, (size_t)de->name_len + 1);
        n += de->name_len;
        fmt_time(mtime, sizeof mtime, de->node.wrt_date, de->node.wrt_time, 0);

        if (fat_node_is_dir(&de->node)) {
            printf("D %s %s\n", mtime, path);
            rc = tree_walk(fs, &de->node, path, n, depth + 1);
        } else {
            uint32_t crc;
            e = crc_file(fs, &de->node, &crc);
            if (e != FS_OK) {
                rc = fail(path, e);
                break;
            }
            printf("F %u %08x %s %s\n", de->node.size, crc, mtime, path);
        }
        path[len] = '\0';
    }
    free(de);
    if (rc == 0 && e != FS_ENOENT)
        rc = fail(path, e);
    return rc;
}

static int cmd_tree(fat_fs_t *fs, const char *start)
{
    char *path = malloc(PATH_BUF);
    fat_node_t node;
    fs_err_t e;
    int rc;

    if (!path)
        return fail(start, FS_EIO);
    e = fat_stat(fs, start, &node);
    if (e != FS_OK) {
        free(path);
        return fail(start, e);
    }
    snprintf(path, PATH_BUF, "%s", start);
    rc = tree_walk(fs, &node, path, strlen(path), 0);
    free(path);
    return rc;
}

static int cmd_stat(fat_fs_t *fs, const char *path)
{
    fat_node_t n;
    char attr[6], t[32];
    fs_err_t e = fat_stat(fs, path, &n);

    if (e != FS_OK)
        return fail(path, e);
    fmt_attr(attr, n.attr);
    printf("ino      %u\n", n.ino);
    printf("type     %s\n", fat_node_is_dir(&n) ? "directory" : "file");
    printf("attr     %s (0x%02x)\n", attr, n.attr);
    printf("size     %u\n", n.size);
    printf("cluster  %u\n", n.cluster);
    fmt_time(t, sizeof t, n.wrt_date, n.wrt_time, 0);
    printf("modified %s\n", t);
    fmt_time(t, sizeof t, n.crt_date, n.crt_time, n.crt_tenth);
    printf("created  %s\n", t);
    fmt_time(t, sizeof t, n.acc_date, 0, 0);
    printf("accessed %.10s\n", t);
    return 0;
}

static int cmd_cat(fat_fs_t *fs, const char *path, uint32_t off, uint32_t len)
{
    static uint8_t buf[65536];
    fat_file_t f;
    fs_err_t e = fat_open(fs, path, &f);

    if (e != FS_OK)
        return fail(path, e);
    while (len > 0) {
        uint32_t want = len < sizeof buf ? len : (uint32_t)sizeof buf, got;

        e = fat_file_pread(&f, buf, want, off, &got);
        if (e != FS_OK)
            return fail(path, e);
        if (got == 0)
            break;
        fwrite(buf, 1, got, stdout);
        off += got;
        len -= got;
    }
    return 0;
}

/* ---- check: the library against itself -------------------------------------- */

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

static void check_reads(check_t *c, fat_fs_t *fs, const fat_node_t *node,
                        const char *path)
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
    e = fat_file_open(fs, node, &f);
    if (e == FS_OK)
        e = fat_file_pread(&f, fwd, node->size, 0, &got);
    if (e == FS_OK && got != node->size)
        e = FS_EIO;
    /* Backwards, in chunks that straddle sector and cluster boundaries, so
     * every read restarts the chain walk from the first cluster. */
    for (off = node->size; e == FS_OK && off > 0; ) {
        uint32_t len = off < 1000u ? off : 1000u;

        off -= len;
        e = fat_file_pread(&f, bwd + off, len, off, &got);
        if (e == FS_OK && got != len)
            e = FS_EIO;
    }
    if (e != FS_OK)
        check_err(c, path, "reading", e);
    else if (memcmp(fwd, bwd, node->size) != 0)
        check_err(c, path, "backward reads differ from a forward read", FS_OK);
    free(fwd);
    free(bwd);
}

static void check_entry(check_t *c, fat_fs_t *fs, const fat_node_t *dir,
                        const fat_dirent_t *de, const char *path)
{
    const fat_node_t *n = &de->node;
    fat_node_t r;
    char upper[FAT_CFG_NAME_MAX + 1];
    size_t i;
    fs_err_t e;

    if (fs->ino_ok) {
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
        c->inos[c->n++] = n->ino;
        if (n->ino == FAT_INO_NONE || n->ino == FAT_INO_ROOT)
            check_err(c, path, "reserved inode number", FS_OK);
        e = fat_node_from_ino(fs, n->ino, &r);
        if (e != FS_OK || !same_node(n, &r))
            check_err(c, path, "node from inode number differs", e);
    }

    e = fat_lookup(fs, dir, de->name, &r);
    if (e != FS_OK || !same_node(n, &r))
        check_err(c, path, "lookup by name", e);
    e = fat_lookup(fs, dir, de->short_name, &r);
    if (e != FS_OK || !same_node(n, &r))
        check_err(c, path, "lookup by short name", e);
    for (i = 0; i <= de->name_len; i++) {
        char ch = de->name[i];
        upper[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 'a' + 'A') : ch;
    }
    e = fat_lookup(fs, dir, upper, &r);
    if (e != FS_OK || !same_node(n, &r))
        check_err(c, path, "lookup ignoring case", e);
    e = fat_stat(fs, path, &r);
    if (e != FS_OK || !same_node(n, &r))
        check_err(c, path, "path resolution", e);

    if (fat_node_is_dir(n)) {
        e = fat_parent(fs, n, &r);
        if (e != FS_OK || !same_node(dir, &r))
            check_err(c, path, "parent", e);
        e = fat_lookup(fs, n, "..", &r);
        if (e != FS_OK || !same_node(dir, &r))
            check_err(c, path, "lookup of '..'", e);
        e = fat_lookup(fs, n, ".", &r);
        if (e != FS_OK || !same_node(n, &r))
            check_err(c, path, "lookup of '.'", e);
    } else {
        check_reads(c, fs, n, path);
    }
}

static int check_walk(check_t *c, fat_fs_t *fs, const fat_node_t *dir,
                      char *path, size_t len, int depth)
{
    fat_dir_t d;
    fat_dirent_t *de;
    fs_err_t e;

    if (depth > TREE_MAX_DEPTH)
        return fail(path, FS_ELOOP);
    de = malloc(sizeof *de);
    if (!de)
        return fail(path, FS_EIO);
    e = fat_dir_open(fs, dir, &d);
    if (e != FS_OK) {
        free(de);
        return fail(path, e);
    }
    while ((e = fat_dir_read(&d, de)) == FS_OK) {
        size_t n = len;

        if (len + 2 + de->name_len > PATH_BUF) {
            free(de);
            return fail(path, FS_ENAMETOOLONG);
        }
        if (n == 0 || path[n - 1] != '/')
            path[n++] = '/';
        memcpy(path + n, de->name, (size_t)de->name_len + 1);
        n += de->name_len;
        check_entry(c, fs, dir, de, path);
        if (fat_node_is_dir(&de->node) &&
            check_walk(c, fs, &de->node, path, n, depth + 1) != 0) {
            free(de);
            return 1;
        }
        path[len] = '\0';
    }
    free(de);
    return e == FS_ENOENT ? 0 : fail(path, e);
}

static int cmp_ino(const void *a, const void *b)
{
    fat_ino_t x = *(const fat_ino_t *)a, y = *(const fat_ino_t *)b;
    return x < y ? -1 : x > y;
}

static int cmd_check(fat_fs_t *fs)
{
    check_t c = { 0 };
    char *path = malloc(PATH_BUF);
    fat_node_t root;
    size_t i;
    int rc;

    if (!path)
        return fail("check", FS_EIO);
    fat_root(fs, &root);
    strcpy(path, "/");
    rc = check_walk(&c, fs, &root, path, 1, 0);
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
    static fat_fs_t fs;
    uint32_t sector_size = 512, lba = 0;
    int have_lba = 0, i = 1, rc;
    const char *image, *cmd;
    bdev_t bd;
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

    if (bdev_file_open(&bd, image, sector_size) != BDEV_OK) {
        fprintf(stderr, "%s: %s: cannot open\n", progname, image);
        return 1;
    }
    e = have_lba ? fat_mount(&fs, &bd, lba, 0) : fat_mount_auto(&fs, &bd);
    if (e != FS_OK) {
        rc = fail(image, e);
        bdev_close(&bd);
        return rc;
    }

    if (strcmp(cmd, "info") == 0 && argc == i) {
        rc = cmd_info(&fs);
    } else if (strcmp(cmd, "ls") == 0 && argc - i <= 1) {
        rc = cmd_ls(&fs, argc > i ? argv[i] : "/");
    } else if (strcmp(cmd, "tree") == 0 && argc - i <= 1) {
        rc = cmd_tree(&fs, argc > i ? argv[i] : "/");
    } else if (strcmp(cmd, "stat") == 0 && argc - i == 1) {
        rc = cmd_stat(&fs, argv[i]);
    } else if (strcmp(cmd, "cat") == 0 && argc - i >= 1 && argc - i <= 3) {
        uint32_t off = 0, len = 0xFFFFFFFFu;
        if ((argc - i >= 2 && !parse_u32(argv[i + 1], &off)) ||
            (argc - i == 3 && !parse_u32(argv[i + 2], &len)))
            rc = (usage(), 2);
        else
            rc = cmd_cat(&fs, argv[i], off, len);
    } else if (strcmp(cmd, "check") == 0 && argc == i) {
        rc = cmd_check(&fs);
    } else {
        usage();
        rc = 2;
    }

    fat_unmount(&fs);
    bdev_close(&bd);
    return rc;
}
