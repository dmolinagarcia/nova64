/*
 * fsops.h — the internal filesystem table of the host track (sheet Y4).
 *
 * This table is the track's deliverable (Y4.5). FAT32, ext2 and NVFS are
 * implemented behind it until it stops changing, and then it is lifted
 * into sheet Y1 (Y4.23). It starts in the shape Y4 gives it. Every change
 * to it goes into fsops.md with the reason that forced it. Every place it
 * differs from Y1's vnode_ops is an open question (Q172), because Y1 is
 * the authority (D99).
 *
 * It is keyed on inode numbers and never on paths (Y4.6, Y1.7). lookup
 * takes a parent and one component, so a driver never sees a '/', and
 * path resolution belongs to the caller (Y1.11).
 *
 * This is not a FUSE interface. No FUSE type appears here, and the
 * binding maps fs_err_t to errno once, at its boundary (Y4.8).
 */
#ifndef NOVAFS_FSOPS_H
#define NOVAFS_FSOPS_H

#include <stddef.h>
#include <stdint.h>

#include "bdev.h"
#include "fs_err.h"

typedef uint32_t fs_ino_t;

#define FS_INO_ROOT     1u

/* POSIX type and permission encoding: sheet Y1.3 passes `mode` through
 * unchanged, and NVFS stores it bit for bit (Y2.9). */
#define FS_S_IFMT       0170000u
#define FS_S_IFDIR      0040000u
#define FS_S_IFREG      0100000u
#define FS_S_IFLNK      0120000u

/* Directory entry types, with the values of DT_*. */
#define FS_DT_UNKNOWN   0u
#define FS_DT_DIR       4u
#define FS_DT_REG       8u
#define FS_DT_LNK       10u

/* mount flags */
#define FS_MOUNT_RDONLY 0x0001u

/* open flags: access mode, with the values of O_RDONLY and friends */
#define FS_O_ACCMODE    0x0003
#define FS_O_RDONLY     0x0000
#define FS_O_WRONLY     0x0001
#define FS_O_RDWR       0x0002

/* POSIX-shaped. A filesystem that stores none of it must synthesise it
 * under stated rules (FUS-03.i). It must never ask the host who the user
 * is or what the time is (Y4.20). */
typedef struct {
    fs_ino_t  ino;
    uint32_t  mode;
    uint32_t  nlink;
    uint32_t  uid, gid;         /* carried, never enforced (Y1.16)   */
    uint64_t  size;
    uint64_t  blocks;           /* 512-byte units                    */
    int64_t   atime, mtime, ctime;  /* seconds since 1970            */
} fs_attr_t;

typedef struct fs_dirent {
    fs_ino_t  ino;
    uint8_t   type;             /* FS_DT_*                           */
    uint16_t  name_len;
    char      name[256];        /* NUL-terminated                    */
} fs_dirent_t;

typedef struct fs_dir_cursor fs_dir_cursor_t;   /* opaque, driver-owned */
typedef struct fs_file       fs_file_t;         /* opaque, driver-owned */

/*
 * Every slot returns FS_OK or an error. A NULL slot means the driver does
 * not implement it, and the caller answers FS_ENOTSUP (Y1.9). The write
 * half is present from the start, so the shape of the table settles
 * while all three read paths are written against it (Y4).
 */
typedef struct fsops {
    const char *name;

    /* Volume. Mounted over an L0 device, never over a path. */
    fs_err_t (*mount)   (void *ctx, bdev_t *bd, unsigned flags);
    fs_err_t (*unmount) (void *ctx);
    fs_err_t (*statfs)  (void *ctx, uint64_t *blocks, uint64_t *bfree,
                         uint64_t *files, uint64_t *ffree, uint32_t *bsize);

    /* Read. */
    fs_err_t (*getattr) (void *ctx, fs_ino_t ino, fs_attr_t *out);
    fs_err_t (*lookup)  (void *ctx, fs_ino_t parent,
                         const char *name, fs_ino_t *out);
    fs_err_t (*readlink)(void *ctx, fs_ino_t ino, char *buf, size_t bufsz);

    /* Directory. readdir answers FS_ENOENT after the last entry. */
    fs_err_t (*opendir) (void *ctx, fs_ino_t ino, fs_dir_cursor_t **out);
    fs_err_t (*readdir) (void *ctx, fs_dir_cursor_t *c, fs_dirent_t *out);
    fs_err_t (*closedir)(void *ctx, fs_dir_cursor_t *c);

    /* File. */
    fs_err_t (*open)    (void *ctx, fs_ino_t ino, int flags, fs_file_t **out);
    fs_err_t (*read)    (void *ctx, fs_file_t *f, void *buf,
                         size_t len, uint64_t off, size_t *got);
    fs_err_t (*close)   (void *ctx, fs_file_t *f);

    /* Write, left NULL until FUS-06. */
    fs_err_t (*write)   (void *ctx, fs_file_t *f, const void *buf,
                         size_t len, uint64_t off, size_t *put);
    fs_err_t (*create)  (void *ctx, fs_ino_t parent, const char *name,
                         uint32_t mode, fs_ino_t *out);
    fs_err_t (*mkdir)   (void *ctx, fs_ino_t parent, const char *name,
                         uint32_t mode, fs_ino_t *out);
    fs_err_t (*unlink)  (void *ctx, fs_ino_t parent, const char *name);
    fs_err_t (*rmdir)   (void *ctx, fs_ino_t parent, const char *name);
    fs_err_t (*rename)  (void *ctx, fs_ino_t op, const char *on,
                                    fs_ino_t np, const char *nn);
    fs_err_t (*truncate)(void *ctx, fs_ino_t ino, uint64_t size);
    fs_err_t (*setattr) (void *ctx, fs_ino_t ino,
                         const fs_attr_t *in, unsigned mask);
    fs_err_t (*sync)    (void *ctx);
} fsops_t;

#endif /* NOVAFS_FSOPS_H */
