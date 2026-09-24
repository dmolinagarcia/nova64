/*
 * fat32_ondisk.h — L1: FAT32 on-disk structures. Offsets, not structs.
 *
 * Everything declared here decodes bytes that are already in memory and
 * performs no I/O at all, so it can be unit-tested against static arrays
 * (DN-FS-FUSE-001 §4.1 rule 1). Field names follow the Microsoft FAT
 * specification so each line can be checked against it.
 */
#ifndef NOVAFS_FAT32_ONDISK_H
#define NOVAFS_FAT32_ONDISK_H

#include <stdint.h>

#include "fs_err.h"

/* ---- Boot sector and BIOS parameter block --------------------------- */
#define BS_jmpBoot_OFF          0
#define BS_OEMName_OFF          3
#define BPB_BytsPerSec_OFF      11
#define BPB_SecPerClus_OFF      13
#define BPB_RsvdSecCnt_OFF      14
#define BPB_NumFATs_OFF         16
#define BPB_RootEntCnt_OFF      17
#define BPB_TotSec16_OFF        19
#define BPB_Media_OFF           21
#define BPB_FATSz16_OFF         22
#define BPB_HiddSec_OFF         28
#define BPB_TotSec32_OFF        32
/* FAT32 extended BPB */
#define BPB_FATSz32_OFF         36
#define BPB_ExtFlags_OFF        40
#define BPB_FSVer_OFF           42
#define BPB_RootClus_OFF        44
#define BPB_FSInfo_OFF          48
#define BPB_BkBootSec_OFF       50
#define BS_BootSig_OFF          66
#define BS_VolID_OFF            67
#define BS_VolLab_OFF           71
#define BS_FilSysType_OFF       82
#define BS_Sign_OFF             510

#define BS_BOOTSIG_EXTENDED     0x29u
#define BPB_EXTFLAGS_NOMIRROR   0x0080u     /* only one FAT is active    */
#define BPB_EXTFLAGS_ACTIVE     0x000Fu     /* ...and this is its number */

/* ---- FSInfo sector --------------------------------------------------- */
#define FSI_LeadSig_OFF         0
#define FSI_StrucSig_OFF        484
#define FSI_Free_Count_OFF      488
#define FSI_Nxt_Free_OFF        492
#define FSI_TrailSig_OFF        508

#define FSI_LEADSIG             ((uint32_t)0x41615252uL)
#define FSI_STRUCSIG            ((uint32_t)0x61417272uL)
#define FSI_TRAILSIG            ((uint32_t)0xAA550000uL)

/* ---- FAT entries ----------------------------------------------------- */
#define FAT32_ENTRY_MASK  ((uint32_t)0x0FFFFFFFuL)   /* top 4 bits reserved  */
#define FAT32_BAD         ((uint32_t)0x0FFFFFF7uL)
#define FAT32_EOC_MIN     ((uint32_t)0x0FFFFFF8uL)   /* >= this: end of chain */
#define FAT32_MIN_CLUSTERS ((uint32_t)65525uL)       /* fewer: FAT12 or 16   */
#define FAT32_UNKNOWN     ((uint32_t)0xFFFFFFFFuL)   /* "not known"          */

/* ---- Directory entries ----------------------------------------------- */
#define DIR_ENTRY_SIZE          32u
#define DIR_Name_OFF            0
#define DIR_Attr_OFF            11
#define DIR_NTRes_OFF           12
#define DIR_CrtTimeTenth_OFF    13
#define DIR_CrtTime_OFF         14
#define DIR_CrtDate_OFF         16
#define DIR_LstAccDate_OFF      18
#define DIR_FstClusHI_OFF       20
#define DIR_WrtTime_OFF         22
#define DIR_WrtDate_OFF         24
#define DIR_FstClusLO_OFF       26
#define DIR_FileSize_OFF        28

#define DIR_NAME_END            0x00u   /* this and every later entry free */
#define DIR_NAME_DELETED        0xE5u
#define DIR_NAME_KANJI_E5       0x05u   /* stands for a real 0xE5 byte     */

#define DIR_NTRES_LC_BASE       0x08u   /* show the base name in lowercase */
#define DIR_NTRES_LC_EXT        0x10u   /* show the extension in lowercase */

#define FAT_ATTR_READ_ONLY      0x01u
#define FAT_ATTR_HIDDEN         0x02u
#define FAT_ATTR_SYSTEM         0x04u
#define FAT_ATTR_VOLUME_ID      0x08u
#define FAT_ATTR_DIRECTORY      0x10u
#define FAT_ATTR_ARCHIVE        0x20u
#define FAT_ATTR_LONG_NAME      0x0Fu
#define FAT_ATTR_LONG_NAME_MASK 0x3Fu

/* A directory holds at most 65536 entries (2 MiB), per the specification. */
#define FAT_DIR_MAX_ENTRIES     ((uint32_t)65536uL)

/* ---- Long file name entries ----------------------------------------- */
#define LDIR_Ord_OFF            0
#define LDIR_Name1_OFF          1       /* 5 UTF-16 units */
#define LDIR_Attr_OFF           11
#define LDIR_Type_OFF           12
#define LDIR_Chksum_OFF         13
#define LDIR_Name2_OFF          14      /* 6 UTF-16 units */
#define LDIR_FstClusLO_OFF      26
#define LDIR_Name3_OFF          28      /* 2 UTF-16 units */

#define LDIR_LAST_LONG_ENTRY    0x40u
#define LFN_UNITS_PER_ENTRY     13u
#define LFN_MAX_ENTRIES         20u     /* 20 * 13 = 260 >= 255 units */
#define LFN_MAX_UNITS           255u

/* Decoded and validated geometry. All sector numbers are relative to the
 * start of the volume and counted in logical (BPB) sectors. */
typedef struct fat32_geom {
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint8_t  num_fats;
    uint16_t reserved_sectors;
    uint16_t fsinfo_sector;         /* 0 when the volume has none         */
    uint16_t ext_flags;
    uint8_t  active_fat;            /* the FAT this driver reads          */
    uint8_t  sec_shift;             /* log2(bytes_per_sector)             */
    uint8_t  clus_shift;            /* log2(sectors_per_cluster)          */
    uint8_t  boot_sig;
    uint32_t fat_size;              /* sectors per FAT                    */
    uint32_t total_sectors;
    uint32_t root_cluster;
    uint32_t fat_start;             /* first sector of the active FAT     */
    uint32_t data_start;            /* first sector of cluster 2          */
    uint32_t cluster_count;         /* data clusters: 2 .. max_cluster    */
    uint32_t max_cluster;           /* cluster_count + 1                  */
    uint32_t volume_id;
    uint8_t  volume_label[11];      /* BS_VolLab, raw and space padded    */
} fat32_geom_t;

/* Decodes and validates the boot sector in `sec` (at least 512 bytes).
 * Returns FS_OK, FS_ENOTSUP when the sector is readable but is not a
 * FAT32 volume this driver will mount (FAT12/16, exFAT, NTFS, a newer
 * BPB_FSVer), or FS_ECORRUPT when it claims to be FAT and is not
 * self-consistent. It does not log: *why, when not NULL, receives a
 * short static reason, and the caller decides whether it is damage worth
 * reporting or merely a probe that did not match. */
fs_err_t fat32_parse_bpb(const uint8_t *sec, fat32_geom_t *g, const char **why);

/* Decodes an FSInfo sector. Returns 1 if all three signatures are valid.
 * The two counters are hints that are routinely stale: absurd values are
 * replaced by FAT32_UNKNOWN rather than trusted, and both are
 * FAT32_UNKNOWN when the signatures do not validate. */
int fat32_parse_fsinfo(const uint8_t *sec, const fat32_geom_t *g,
                       uint32_t *free_count, uint32_t *next_free);

/* ---- Directory entry decoding ---------------------------------------- */
typedef enum {
    FAT_DE_END,                 /* 0x00: end of directory              */
    FAT_DE_DELETED,             /* 0xE5                                */
    FAT_DE_LFN,                 /* a long name fragment                */
    FAT_DE_VOLUME,              /* the volume label                    */
    FAT_DE_DOT,                 /* "." or ".."                         */
    FAT_DE_INVALID,             /* a short name that cannot be valid   */
    FAT_DE_FILE                 /* a file or directory                 */
} fat32_de_kind_t;

fat32_de_kind_t fat32_de_kind(const uint8_t *de);

/* 1 if the entry is the ".." entry of a subdirectory. */
int fat32_de_is_dotdot(const uint8_t *de);

uint32_t fat32_de_cluster(const uint8_t *de);

/* Short name as displayed: base, a dot only if there is an extension,
 * the NT lowercase flags applied, and OEM bytes decoded as code page 437
 * into UTF-8. `out` must hold FAT_SHORT_NAME_BUF bytes; the result is
 * NUL-terminated and its length is returned. */
#define FAT_SHORT_NAME_BUF      35u     /* 11 chars * 3 bytes, dot, NUL */
uint16_t fat32_short_name(const uint8_t *de, char *out);

/* Decodes an 11-byte space-padded name (the volume label format) the same
 * way, without the dot and without the lowercase flags. */
uint16_t fat32_label_name(const uint8_t *raw11, char *out);

/* The checksum of an 11-byte short name that every LFN entry of its run
 * carries. */
uint8_t fat32_lfn_checksum(const uint8_t *name11);

/* Extracts the 13 UTF-16 code units of one LFN entry. */
void fat32_lfn_units(const uint8_t *de, uint16_t out[LFN_UNITS_PER_ENTRY]);

/* ---- Timestamps ------------------------------------------------------ */
typedef struct fat_datetime {
    uint16_t year;              /* 1980 .. 2107                        */
    uint8_t  month;             /* 1 .. 12                             */
    uint8_t  day;               /* 1 .. 31                             */
    uint8_t  hour;              /* 0 .. 23                             */
    uint8_t  minute;            /* 0 .. 59                             */
    uint8_t  second;            /* 0 .. 59                             */
    uint8_t  centisecond;       /* 0 .. 99, from DIR_CrtTimeTenth only */
} fat_datetime_t;

/* Decodes a FAT date, time and (creation only, else 0) tenth field.
 * FAT timestamps are local time with no zone; no zone is applied here.
 * Returns 1 if the fields are valid. Otherwise returns 0 and fills in
 * the FAT epoch, 1980-01-01 00:00:00, which is also what a zero date
 * (a field the writer never set) decodes to. */
int fat32_decode_datetime(uint16_t date, uint16_t time, uint8_t tenth,
                          fat_datetime_t *out);

#endif /* NOVAFS_FAT32_ONDISK_H */
