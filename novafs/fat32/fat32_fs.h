/*
 * fat32_fs.h — L2: a read-only FAT32 filesystem library.
 *
 * Mounts a FAT32 volume over an L0 block device and gives read access to
 * it: path resolution, directory listing with long file names, file
 * reads at any offset, free-space reporting.
 *
 * Design rules, from DN-FS-FUSE-001 and sheet Y4:
 *   - No allocation. Every object is a caller-owned structure, so the
 *     library runs unchanged where there is no heap worth using.
 *   - 32-bit arithmetic throughout, and correct with a 16-bit int.
 *   - Nothing decoded from disk is used as an address before it has been
 *     checked; every chain walk is bounded and detects cycles; damage is
 *     reported as FS_ECORRUPT through fs_corrupt(), never absorbed.
 *   - Single-threaded. A mount and everything opened on it must be used
 *     from one thread at a time.
 *
 * Names are UTF-8. Long names are rebuilt from their UTF-16 fragments;
 * short names are decoded as code page 437. Lookups fold ASCII case only.
 */
#ifndef NOVAFS_FAT32_FS_H
#define NOVAFS_FAT32_FS_H

#include <stdint.h>

#include "bdev.h"
#include "fat32_ondisk.h"
#include "fs_err.h"

/* ---- Build-time configuration ---------------------------------------- */

/* Largest logical sector size accepted. Sizes every sector buffer below;
 * 512 is enough for SD cards and saves 3.5 KiB per buffer. */
#ifndef FAT_CFG_MAX_SECTOR
#define FAT_CFG_MAX_SECTOR      4096u
#endif

/* FAT sector cache slots, a power of two. Chain walks have good locality,
 * so even a few slots turn n FAT reads into roughly n / 128. */
#ifndef FAT_CFG_FAT_CACHE
#define FAT_CFG_FAT_CACHE       4u
#endif

/* Longest name returned, in UTF-8 bytes. 765 holds any legal long name
 * (255 UTF-16 units of up to 3 bytes each). A long name that does not fit
 * is replaced by its short alias and flagged with FAT_DE_NAME_SHORTENED. */
#ifndef FAT_CFG_NAME_MAX
#define FAT_CFG_NAME_MAX        765u
#endif

/* ---- Nodes ------------------------------------------------------------ */

/* FAT has no inodes, so numbers are synthesised from the position of the
 * short directory entry: unique while the file exists, stable until it
 * is renamed or moved, and 1 for the root (DN-FS-FUSE-001 step 28). */
typedef uint32_t fat_ino_t;

#define FAT_INO_NONE    0u  /* volume too large for 32-bit numbering */
#define FAT_INO_ROOT    1u

/* A file or directory: a decoded copy of its directory entry. Nodes are
 * plain values; they hold no resources and need no release. */
typedef struct fat_node {
    fat_ino_t ino;
    uint32_t  cluster;          /* first cluster; 0 for an empty file */
    uint32_t  size;             /* bytes; 0 for a directory           */
    uint8_t   attr;             /* FAT_ATTR_*                         */
    uint8_t   crt_tenth;
    uint16_t  crt_time;
    uint16_t  crt_date;
    uint16_t  acc_date;
    uint16_t  wrt_time;
    uint16_t  wrt_date;
} fat_node_t;

#define fat_node_is_dir(n)  (((n)->attr & FAT_ATTR_DIRECTORY) != 0)

/* ---- The mounted volume ------------------------------------------------ */

typedef struct fat_fs {
    bdev_t       *bd;
    fat32_geom_t  g;
    uint32_t      part_lba;         /* device sector holding volume sector 0 */
    uint8_t       dev_shift;        /* log2(logical sector / device sector)  */
    uint8_t       ino_shift;        /* log2(directory entries per cluster)   */
    uint8_t       ino_ok;           /* numbering fits in 32 bits             */
    uint8_t       fsinfo_valid;
    uint32_t      fsinfo_free;      /* FSInfo hints, FAT32_UNKNOWN if absent */
    uint32_t      fsinfo_next;
    uint32_t      free_count;       /* counted on first use, then cached     */

    /* One logical sector for directories and partial file reads. */
    uint32_t      buf_sector;
    uint8_t       buf_valid;
    uint8_t       buf[FAT_CFG_MAX_SECTOR];

    /* Direct-mapped cache over the active FAT. */
    uint32_t      fc_sector[FAT_CFG_FAT_CACHE];
    uint8_t       fc_valid[FAT_CFG_FAT_CACHE];
    uint8_t       fc_buf[FAT_CFG_FAT_CACHE][FAT_CFG_MAX_SECTOR];
} fat_fs_t;

/* Mounts the volume that starts at device sector `lba` and spans `count`
 * device sectors (0: to the end of the device). The device must stay
 * open, and `fs` must stay where it is, until fat_unmount(). */
fs_err_t fat_mount(fat_fs_t *fs, bdev_t *bd, uint32_t lba, uint32_t count);

/* Mounts a whole-device ("superfloppy") volume, or else the first MBR
 * primary partition that holds a FAT32 volume, whatever its type byte. */
fs_err_t fat_mount_auto(fat_fs_t *fs, bdev_t *bd);

/* Nothing to release; clears the structure so stale use fails loudly. */
void fat_unmount(fat_fs_t *fs);

typedef struct fat_statfs {
    uint32_t cluster_size;      /* bytes                     */
    uint32_t total_clusters;
    uint32_t free_clusters;
} fat_statfs_t;

/* Free space is counted from the FAT on the first call and cached; the
 * FSInfo counter is only a hint (fs->fsinfo_free), and the Linux driver
 * does not trust it by default either. */
fs_err_t fat_statfs(fat_fs_t *fs, fat_statfs_t *out);

/* Volume label: the root directory's label entry if there is one, else
 * the boot sector's, else "". `out` holds FAT_SHORT_NAME_BUF bytes. */
fs_err_t fat_label(fat_fs_t *fs, char *out);

/* ---- Nodes and names ---------------------------------------------------- */

void fat_root(const fat_fs_t *fs, fat_node_t *out);

/* Rebuilds a node from its number, as an inode-keyed interface needs.
 * The number must come from this mount. FS_ENOENT if the entry is gone. */
fs_err_t fat_node_from_ino(fat_fs_t *fs, fat_ino_t ino, fat_node_t *out);

/* Looks one name up in a directory. "." and ".." are resolved to real
 * nodes, with their real numbers. */
fs_err_t fat_lookup(fat_fs_t *fs, const fat_node_t *dir, const char *name,
                    fat_node_t *out);

/* The directory containing `dir` (the root for the root). */
fs_err_t fat_parent(fat_fs_t *fs, const fat_node_t *dir, fat_node_t *out);

/* Resolves a '/'-separated path. Relative paths start at `base`, or at
 * the root when base is NULL; absolute ones always at the root. A
 * non-directory in the middle of the path, or before a trailing '/', is
 * FS_ENOTDIR rather than FS_ENOENT. */
fs_err_t fat_resolve(fat_fs_t *fs, const fat_node_t *base, const char *path,
                     fat_node_t *out);

/* ---- Directories ---------------------------------------------------------- */

typedef struct fat_chain {
    uint32_t cur;               /* current cluster                     */
    uint32_t steps;             /* successors taken, bounded by volume */
    uint32_t mark;              /* Brent's cycle detection             */
    uint32_t power;
    uint32_t lam;
} fat_chain_t;

typedef struct fat_dir {
    fat_fs_t    *fs;
    uint32_t     first;         /* first cluster, for rewinding        */
    fat_chain_t  chain;
    uint16_t     index;         /* next entry in the current cluster   */
    uint8_t      done;
    uint32_t     seen;          /* entries consumed, at most 65536     */
    /* Long name being assembled from its fragments. */
    uint8_t      lfn_state;
    uint8_t      lfn_count;
    uint8_t      lfn_expect;
    uint8_t      lfn_sum;
    uint16_t     lfn[LFN_MAX_ENTRIES * LFN_UNITS_PER_ENTRY];
} fat_dir_t;

#define FAT_DE_NAME_LONG        0x01u   /* name came from a long name     */
#define FAT_DE_NAME_SHORTENED   0x02u   /* long name too big for `name`   */

typedef struct fat_dirent {
    fat_node_t node;
    uint16_t   name_len;
    uint8_t    flags;
    char       name[FAT_CFG_NAME_MAX + 1u];
    char       short_name[FAT_SHORT_NAME_BUF];
} fat_dirent_t;

/* Directory iteration in on-disk order. "." and ".." are never returned,
 * for the root and subdirectories alike; fat_lookup() resolves them.
 * Neither is the volume label, nor deleted or invalid entries. */
fs_err_t fat_dir_open(fat_fs_t *fs, const fat_node_t *dir, fat_dir_t *d);

/* FS_OK with the next entry, FS_ENOENT at the end, or an error. */
fs_err_t fat_dir_read(fat_dir_t *d, fat_dirent_t *out);

void fat_dir_rewind(fat_dir_t *d);

/* ---- Files ------------------------------------------------------------------ */

typedef struct fat_file {
    fat_fs_t    *fs;
    uint32_t     first;         /* first cluster                        */
    uint32_t     size;
    uint32_t     pos;           /* for fat_file_read()                  */
    fat_chain_t  chain;         /* positioned at logical cluster `index` */
    uint32_t     index;
    uint8_t      chain_valid;
} fat_file_t;

fs_err_t fat_file_open(fat_fs_t *fs, const fat_node_t *node, fat_file_t *f);

/* Reads up to len bytes at `off`. *got is short only at end of file, and
 * 0 at or beyond it. Sequential access costs O(1) FAT lookups per
 * cluster; seeking backwards restarts from the first cluster. */
fs_err_t fat_file_pread(fat_file_t *f, void *buf, uint32_t len, uint32_t off,
                        uint32_t *got);

/* Reads at the current position and advances it. */
fs_err_t fat_file_read(fat_file_t *f, void *buf, uint32_t len, uint32_t *got);

/* Any position is accepted; reading beyond the end returns 0 bytes. */
void fat_file_seek(fat_file_t *f, uint32_t pos);

/* ---- Path conveniences ------------------------------------------------------- */

fs_err_t fat_stat(fat_fs_t *fs, const char *path, fat_node_t *out);
fs_err_t fat_opendir(fat_fs_t *fs, const char *path, fat_dir_t *d);
fs_err_t fat_open(fat_fs_t *fs, const char *path, fat_file_t *f);

#endif /* NOVAFS_FAT32_FS_H */
