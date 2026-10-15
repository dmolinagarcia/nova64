/*
 * fat32_fsops.h — FAT32 behind the fsops table (sheet Y4, gate FUS-03).
 *
 *     static fat32_mount_t m;
 *     fat32_fsops.mount(&m, bd, FS_MOUNT_RDONLY);
 *     fat32_fsops.lookup(&m, FS_INO_ROOT, "docs", &ino);
 *
 * The device passed to mount is the volume itself. Finding the volume
 * inside a partitioned device belongs below the driver (fat_probe() and
 * bdev_part_open()), just as Y1's mount receives the partition's start.
 *
 * The driver is read-only. A read-write mount is refused rather than
 * downgraded, because this table has no way to report a downgrade
 * (Y1.13), and every write slot is NULL (Y1.9).
 *
 * Directory cursors and open files are the driver's objects, as Y4's
 * table has them. They come from fixed pools in the mount, so there is
 * still no allocation, and FS_EMFILE when a pool is empty.
 */
#ifndef NOVAFS_FAT32_FSOPS_H
#define NOVAFS_FAT32_FSOPS_H

#include "fat32_fs.h"
#include "fsops.h"

#ifndef FAT32_FSOPS_DIRS
#define FAT32_FSOPS_DIRS    8u      /* open directory cursors per mount */
#endif
#ifndef FAT32_FSOPS_FILES
#define FAT32_FSOPS_FILES   16u     /* open files per mount             */
#endif

/* Pool slots. Callers see them only as fs_dir_cursor_t and fs_file_t. */
typedef struct fat32_dir_slot {
    uint8_t    used;
    uint8_t    dots;                /* "." and ".." returned so far */
    fat_ino_t  self;
    fat_dir_t  d;
} fat32_dir_slot_t;

typedef struct fat32_file_slot {
    uint8_t    used;
    fat_file_t f;
} fat32_file_slot_t;

/* The ctx of every fsops call. */
typedef struct fat32_mount {
    fat_fs_t          fs;
    uint8_t           mounted;
    fat_dirent_t      scratch;      /* one, because calls never overlap (Y4.13) */
    fat32_dir_slot_t  dirs[FAT32_FSOPS_DIRS];
    fat32_file_slot_t files[FAT32_FSOPS_FILES];
} fat32_mount_t;

extern const fsops_t fat32_fsops;

#endif /* NOVAFS_FAT32_FSOPS_H */
