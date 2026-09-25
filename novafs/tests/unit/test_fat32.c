/*
 * test_fat32.c — unit tests for L0, L1 and long-name assembly.
 *
 * Everything here runs on static byte arrays: no image, no mount, no
 * external tools. The image-based tests live in tests/run_tests.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bdev.h"
#include "endian.h"
#include "fat32_fsops.h"
#include "fat32_ondisk.h"
#include "fat32_priv.h"
#include "fsops.h"
#include "mbr.h"

static unsigned failures, checks, corrupt_reports;

#define CHECK(cond) do {                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",                   \
                    __FILE__, __LINE__, #cond);                            \
        }                                                                  \
    } while (0)

static void quiet_logger(const char *what)
{
    (void)what;
    corrupt_reports++;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

/* ---- L0 -------------------------------------------------------------------- */

static void test_endian(void)
{
    static const uint8_t b[] = { 0x00, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12 };

    CHECK(rd16(b + 1) == 0x1234);                 /* odd address */
    CHECK(rd32(b + 3) == 0x12345678uL);
    CHECK(rd16((const uint8_t *)"\xFF\xFF") == 0xFFFF);
}

static void test_bdev(void)
{
    static uint8_t disk[8 * 512];
    uint8_t buf[2 * 512];
    bdev_t bd;
    unsigned i;

    for (i = 0; i < sizeof disk; i++)
        disk[i] = (uint8_t)(i / 512);

    CHECK(bdev_mem_open(&bd, disk, sizeof disk, 500) == BDEV_EINVAL);
    CHECK(bdev_mem_open(&bd, disk, sizeof disk, 256) == BDEV_EINVAL);
    CHECK(bdev_mem_open(&bd, disk, sizeof disk + 100, 512) == BDEV_OK);
    CHECK(bd.sector_count == 8);                  /* partial sector dropped */

    CHECK(bdev_read(&bd, 6, buf, 2) == BDEV_OK);
    CHECK(buf[0] == 6 && buf[512] == 7);
    CHECK(bdev_read(&bd, 7, buf, 2) == BDEV_ERANGE);
    CHECK(bdev_read(&bd, 8, buf, 1) == BDEV_ERANGE);
    CHECK(bdev_read(&bd, 0xFFFFFFFFuL, buf, 2) == BDEV_ERANGE);    /* wraps */
    CHECK(bdev_read(&bd, 3, buf, 0xFFFFFFFFuL) == BDEV_ERANGE);
    CHECK(bdev_read(&bd, 100, buf, 0) == BDEV_OK);                 /* no-op */
    CHECK(bd.reads == 1 && bd.sectors_read == 2);

    {
        bdev_t view;

        CHECK(bdev_part_open(&view, &bd, 2, 4) == BDEV_OK);
        CHECK(view.sector_count == 4);
        CHECK(bdev_read(&view, 0, buf, 2) == BDEV_OK && buf[0] == 2 && buf[512] == 3);
        CHECK(bdev_read(&view, 3, buf, 2) == BDEV_ERANGE);      /* inside the parent */
        CHECK(bdev_part_open(&view, &bd, 6, 3) == BDEV_ERANGE);
        CHECK(bdev_part_open(&view, &bd, 8, 1) == BDEV_ERANGE);
        CHECK(bdev_part_open(&view, &bd, 0, 0) == BDEV_ERANGE);
    }
}

static void test_mbr(void)
{
    uint8_t sec[512];
    mbr_part_t p[4];

    memset(sec, 0, sizeof sec);
    sec[446 + 16 + 4] = 0x0C;
    put32(sec + 446 + 16 + 8, 2048);
    put32(sec + 446 + 16 + 12, 100000);
    CHECK(mbr_parse(sec, p) == 0);                /* no signature yet */
    sec[510] = 0x55;
    sec[511] = 0xAA;
    CHECK(mbr_parse(sec, p) == 1);
    CHECK(p[0].type == 0 && p[1].type == 0x0C);
    CHECK(p[1].lba == 2048 && p[1].count == 100000);
    CHECK(mbr_type_is_container(0x05) && mbr_type_is_container(0xEE));
    CHECK(!mbr_type_is_container(0x0C));
}

/* ---- L1: boot sector -------------------------------------------------------- */

/* A FAT32 boot sector: 512-byte sectors, 1 sector per cluster, 32 reserved,
 * two FATs of 780 sectors, so the data region starts at sector 1592. */
#define T_DATA_START    1592u

static void make_bpb(uint8_t *sec, uint32_t total)
{
    memset(sec, 0, 512);
    sec[0] = 0xEB;
    sec[1] = 0x58;
    sec[2] = 0x90;
    memcpy(sec + BS_OEMName_OFF, "mkfs.fat", 8);
    put16(sec + BPB_BytsPerSec_OFF, 512);
    sec[BPB_SecPerClus_OFF] = 1;
    put16(sec + BPB_RsvdSecCnt_OFF, 32);
    sec[BPB_NumFATs_OFF] = 2;
    sec[BPB_Media_OFF] = 0xF8;
    put32(sec + BPB_TotSec32_OFF, total);
    put32(sec + BPB_FATSz32_OFF, 780);
    put32(sec + BPB_RootClus_OFF, 2);
    put16(sec + BPB_FSInfo_OFF, 1);
    put16(sec + BPB_BkBootSec_OFF, 6);
    sec[BS_BootSig_OFF] = 0x29;
    put32(sec + BS_VolID_OFF, 0xCAFEF00DuL);
    memcpy(sec + BS_VolLab_OFF, "NOVA64     ", 11);
    memcpy(sec + BS_FilSysType_OFF, "FAT32   ", 8);
    sec[510] = 0x55;
    sec[511] = 0xAA;
}

static fs_err_t parse(const uint8_t *sec, fat32_geom_t *g)
{
    return fat32_parse_bpb(sec, g, 0);
}

static void test_bpb(void)
{
    uint8_t sec[512];
    fat32_geom_t g;
    const char *why = 0;

    make_bpb(sec, 100000);
    CHECK(parse(sec, &g) == FS_OK);
    CHECK(g.bytes_per_sector == 512 && g.sec_shift == 9);
    CHECK(g.sectors_per_cluster == 1 && g.clus_shift == 0);
    CHECK(g.fat_start == 32);
    CHECK(g.data_start == T_DATA_START);
    CHECK(g.cluster_count == 100000u - T_DATA_START);
    CHECK(g.max_cluster == g.cluster_count + 1u);
    CHECK(g.root_cluster == 2 && g.fsinfo_sector == 1);
    CHECK(g.volume_id == 0xCAFEF00DuL);
    CHECK(memcmp(g.volume_label, "NOVA64     ", 11) == 0);

    /* The type follows from the cluster count alone. */
    make_bpb(sec, T_DATA_START + 65525u);
    CHECK(parse(sec, &g) == FS_OK && g.cluster_count == 65525u);
    make_bpb(sec, T_DATA_START + 65524u);
    CHECK(fat32_parse_bpb(sec, &g, &why) == FS_ENOTSUP);
    CHECK(strcmp(why, "FAT16 volume") == 0);
    make_bpb(sec, T_DATA_START + 4084u);
    CHECK(fat32_parse_bpb(sec, &g, &why) == FS_ENOTSUP);
    CHECK(strcmp(why, "FAT12 volume") == 0);
    make_bpb(sec, 100000);
    memcpy(sec + BS_FilSysType_OFF, "FAT16   ", 8);    /* documentation only */
    CHECK(parse(sec, &g) == FS_OK);

    make_bpb(sec, 100000);
    put16(sec + BPB_BytsPerSec_OFF, 0);
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put16(sec + BPB_BytsPerSec_OFF, 768);
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    sec[BPB_SecPerClus_OFF] = 3;
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    sec[BPB_SecPerClus_OFF] = 0;
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put16(sec + BPB_BytsPerSec_OFF, 4096);
    sec[BPB_SecPerClus_OFF] = 16;                   /* 64 KiB clusters */
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put16(sec + BPB_RsvdSecCnt_OFF, 0);
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    sec[BPB_NumFATs_OFF] = 0;
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    sec[BPB_NumFATs_OFF] = 3;
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put32(sec + BPB_TotSec32_OFF, 0);
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put32(sec + BPB_FATSz32_OFF, 0);
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put32(sec + BPB_FATSz32_OFF, 0xFFFFFFFFuL);    /* must not overflow */
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put16(sec + BPB_RsvdSecCnt_OFF, 0xFFFF);
    put32(sec + BPB_TotSec32_OFF, 0xFFFFFFFFuL);
    put32(sec + BPB_FATSz32_OFF, 0x7FFFFFFFuL);
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put32(sec + BPB_RootClus_OFF, 1);
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put32(sec + BPB_RootClus_OFF, 100000u - T_DATA_START + 2u);
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put16(sec + BPB_RootEntCnt_OFF, 512);
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    make_bpb(sec, 100000);
    put16(sec + BPB_FSVer_OFF, 0x0001);
    CHECK(parse(sec, &g) == FS_ENOTSUP);

    make_bpb(sec, 100000);
    sec[510] = 0;
    CHECK(parse(sec, &g) == FS_ENOTSUP);
    make_bpb(sec, 100000);
    memcpy(sec + BS_OEMName_OFF, "EXFAT   ", 8);
    CHECK(parse(sec, &g) == FS_ENOTSUP);

    /* Mirroring disabled: read the FAT that is named active. */
    make_bpb(sec, 100000);
    put16(sec + BPB_ExtFlags_OFF, 0x0081);
    CHECK(parse(sec, &g) == FS_OK && g.active_fat == 1 && g.fat_start == 32u + 780u);
    put16(sec + BPB_ExtFlags_OFF, 0x0082);
    CHECK(parse(sec, &g) == FS_ECORRUPT);
    put16(sec + BPB_ExtFlags_OFF, 0x0002);          /* ignored while mirrored */
    CHECK(parse(sec, &g) == FS_OK && g.active_fat == 0);

    /* A FAT too small for the data region limits the cluster count. */
    make_bpb(sec, 300000);
    put32(sec + BPB_FATSz32_OFF, 600);
    CHECK(parse(sec, &g) == FS_OK && g.cluster_count == 600u * 128u - 2u);

    /* A misplaced FSInfo is dropped, not fatal. */
    make_bpb(sec, 100000);
    put16(sec + BPB_FSInfo_OFF, 32);
    CHECK(parse(sec, &g) == FS_OK && g.fsinfo_sector == 0);
}

static void test_fsinfo(void)
{
    uint8_t bs[512], fsi[512];
    fat32_geom_t g;
    uint32_t fc, nf;

    make_bpb(bs, 100000);
    CHECK(parse(bs, &g) == FS_OK);
    memset(fsi, 0, sizeof fsi);
    put32(fsi + FSI_LeadSig_OFF, FSI_LEADSIG);
    put32(fsi + FSI_StrucSig_OFF, FSI_STRUCSIG);
    put32(fsi + FSI_TrailSig_OFF, FSI_TRAILSIG);
    put32(fsi + FSI_Free_Count_OFF, 1000);
    put32(fsi + FSI_Nxt_Free_OFF, 5);
    CHECK(fat32_parse_fsinfo(fsi, &g, &fc, &nf) == 1 && fc == 1000 && nf == 5);

    put32(fsi + FSI_Free_Count_OFF, g.cluster_count + 1u);
    put32(fsi + FSI_Nxt_Free_OFF, 1);
    CHECK(fat32_parse_fsinfo(fsi, &g, &fc, &nf) == 1);
    CHECK(fc == FAT32_UNKNOWN && nf == FAT32_UNKNOWN);

    put32(fsi + FSI_Free_Count_OFF, 1000);
    put32(fsi + FSI_StrucSig_OFF, 0);
    CHECK(fat32_parse_fsinfo(fsi, &g, &fc, &nf) == 0 && fc == FAT32_UNKNOWN);
}

/* ---- L1: directory entries --------------------------------------------------- */

static void make_de(uint8_t *de, const char *name11, uint8_t attr, uint8_t ntres)
{
    memset(de, 0, 32);
    memcpy(de, name11, 11);
    de[DIR_Attr_OFF] = attr;
    de[DIR_NTRes_OFF] = ntres;
}

static void test_dirent(void)
{
    uint8_t de[32];
    char out[FAT_SHORT_NAME_BUF];

    make_de(de, "README  TXT", FAT_ATTR_ARCHIVE, 0);
    CHECK(fat32_de_kind(de) == FAT_DE_FILE);
    CHECK(fat32_short_name(de, out) == 10 && strcmp(out, "README.TXT") == 0);
    make_de(de, "README  TXT", 0, DIR_NTRES_LC_BASE | DIR_NTRES_LC_EXT);
    fat32_short_name(de, out);
    CHECK(strcmp(out, "readme.txt") == 0);
    make_de(de, "README  TXT", 0, DIR_NTRES_LC_BASE);
    fat32_short_name(de, out);
    CHECK(strcmp(out, "readme.TXT") == 0);
    make_de(de, "NOEXT      ", FAT_ATTR_DIRECTORY, 0);
    CHECK(fat32_short_name(de, out) == 5 && strcmp(out, "NOEXT") == 0);
    make_de(de, "\x05" "BC     TXT", 0, 0);          /* stands for 0xE5 */
    CHECK(fat32_de_kind(de) == FAT_DE_FILE);
    fat32_short_name(de, out);
    CHECK(strcmp(out, "\xCF\x83" "BC.TXT") == 0);   /* U+03C3 */
    make_de(de, "\xA5" "AND\x9A      ", 0, 0);         /* CP437 N-tilde, U-umlaut */
    fat32_short_name(de, out);
    CHECK(strcmp(out, "\xC3\x91" "AND\xC3\x9C") == 0);
    make_de(de, "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4", 0, 0);
    CHECK(fat32_short_name(de, out) == 34);         /* the worst case fits */

    make_de(de, "README  TXT", 0, 0);
    de[0] = 0x00;
    CHECK(fat32_de_kind(de) == FAT_DE_END);
    de[0] = 0xE5;
    CHECK(fat32_de_kind(de) == FAT_DE_DELETED);
    make_de(de, "A          ", FAT_ATTR_LONG_NAME, 0);
    CHECK(fat32_de_kind(de) == FAT_DE_LFN);
    make_de(de, "A          ", FAT_ATTR_LONG_NAME | FAT_ATTR_ARCHIVE, 0);
    CHECK(fat32_de_kind(de) == FAT_DE_VOLUME);      /* not an LFN: mask 0x3F */
    make_de(de, "LABEL      ", FAT_ATTR_VOLUME_ID, 0);
    CHECK(fat32_de_kind(de) == FAT_DE_VOLUME);
    make_de(de, ".          ", FAT_ATTR_DIRECTORY, 0);
    CHECK(fat32_de_kind(de) == FAT_DE_DOT && !fat32_de_is_dotdot(de));
    make_de(de, "..         ", FAT_ATTR_DIRECTORY, 0);
    CHECK(fat32_de_kind(de) == FAT_DE_DOT && fat32_de_is_dotdot(de));
    make_de(de, "..         ", 0, 0);
    CHECK(!fat32_de_is_dotdot(de));
    make_de(de, ".HIDDEN    ", 0, 0);
    CHECK(fat32_de_kind(de) == FAT_DE_INVALID);
    make_de(de, " LEADING   ", 0, 0);
    CHECK(fat32_de_kind(de) == FAT_DE_INVALID);

    make_de(de, "README  TXT", 0, 0);
    put16(de + DIR_FstClusHI_OFF, 0x0012);
    put16(de + DIR_FstClusLO_OFF, 0x3456);
    CHECK(fat32_de_cluster(de) == 0x00123456uL);

    CHECK(fat32_lfn_checksum((const uint8_t *)"README  TXT") == 0x73);
    CHECK(fat32_lfn_checksum((const uint8_t *)"LONGFI~1TXT") == 0xD4);

    CHECK(fat32_label_name((const uint8_t *)"NOVA64     ", out) == 6);
    CHECK(strcmp(out, "NOVA64") == 0);
    CHECK(fat32_label_name((const uint8_t *)"           ", out) == 0);
}

static void test_datetime(void)
{
    fat_datetime_t dt;
    uint16_t date = (uint16_t)(((2024 - 1980) << 9) | (2 << 5) | 29);
    uint16_t time = (uint16_t)((13 << 11) | (45 << 5) | 29);

    CHECK(fat32_decode_datetime(date, time, 0, &dt) == 1);
    CHECK(dt.year == 2024 && dt.month == 2 && dt.day == 29);
    CHECK(dt.hour == 13 && dt.minute == 45 && dt.second == 58);
    CHECK(fat32_decode_datetime(date, time, 150, &dt) == 1);
    CHECK(dt.second == 59 && dt.centisecond == 50);

    CHECK(fat32_decode_datetime((uint16_t)(((2023 - 1980) << 9) | (2 << 5) | 29), 0, 0, &dt) == 0);
    CHECK(dt.year == 1980 && dt.month == 1 && dt.day == 1);
    CHECK(fat32_decode_datetime((uint16_t)(((2100 - 1980) << 9) | (2 << 5) | 29), 0, 0, &dt) == 0);
    CHECK(fat32_decode_datetime((uint16_t)((13 << 5) | 1), 0, 0, &dt) == 0);
    CHECK(fat32_decode_datetime(date, (uint16_t)(24 << 11), 0, &dt) == 0);
    CHECK(fat32_decode_datetime(date, time, 200, &dt) == 0);
    CHECK(fat32_decode_datetime(0, 0, 0, &dt) == 0 && dt.year == 1980);
    CHECK(fat32_decode_datetime(0xFF9F, 0xBF7D, 0, &dt) == 1);  /* 2107-12-31 23:59:58 */
    CHECK(dt.year == 2107 && dt.month == 12 && dt.day == 31 && dt.second == 58);

    /* Seconds, with no zone: the reference values are Python's timegm. */
    CHECK(fat32_datetime_to_unix(&dt) == 0xFFFFFFFFuL);           /* saturates */
    fat32_decode_datetime(date, time, 0, &dt);
    CHECK(fat32_datetime_to_unix(&dt) == 1709214358uL);
    fat32_decode_datetime(0, 0, 0, &dt);
    CHECK(fat32_datetime_to_unix(&dt) == 315532800uL);             /* 1980-01-01 */
    fat32_decode_datetime((uint16_t)(((2000 - 1980) << 9) | (3 << 5) | 1), 0, 0, &dt);
    CHECK(fat32_datetime_to_unix(&dt) == 951868800uL);             /* after 2000-02-29 */
    fat32_decode_datetime((uint16_t)(((2106 - 1980) << 9) | (2 << 5) | 7),
                          (uint16_t)((6 << 11) | (28 << 5) | 7), 0, &dt);
    CHECK(fat32_datetime_to_unix(&dt) == 4294967294uL);            /* the last one */
}

/* ---- Long names ----------------------------------------------------------------- */

/* Writes LFN entry `ord` of a run into de, carrying units[0..12]. */
static void make_lfn(uint8_t *de, uint8_t ord, uint8_t sum, const uint16_t *u)
{
    unsigned i;

    memset(de, 0, 32);
    de[LDIR_Ord_OFF] = ord;
    de[LDIR_Attr_OFF] = FAT_ATTR_LONG_NAME;
    de[LDIR_Chksum_OFF] = sum;
    for (i = 0; i < 5; i++)
        put16(de + LDIR_Name1_OFF + 2 * i, u[i]);
    for (i = 0; i < 6; i++)
        put16(de + LDIR_Name2_OFF + 2 * i, u[5 + i]);
    for (i = 0; i < 2; i++)
        put16(de + LDIR_Name3_OFF + 2 * i, u[11 + i]);
}

/* Splits an ASCII name into 13-unit fragments, 0x0000-terminated and
 * 0xFFFF-padded as the format prescribes. Returns the fragment count. */
static unsigned fragments(const char *name, uint16_t frag[][13])
{
    unsigned len = (unsigned)strlen(name), n = (len + 13) / 13, i;

    if (len % 13 == 0)
        n = len / 13;
    for (i = 0; i < n * 13; i++)
        frag[i / 13][i % 13] = i < len ? (uint16_t)name[i] : i == len ? 0 : 0xFFFF;
    return n;
}

static int32_t run_lfn(fat_dir_t *d, const char *name, const uint8_t *sde,
                       char *out, uint32_t cap)
{
    uint16_t frag[LFN_MAX_ENTRIES][13];
    uint8_t de[32];
    uint8_t sum = fat32_lfn_checksum(sde);
    unsigned n = fragments(name, frag), i;

    fat_lfn_reset(d);
    for (i = n; i >= 1; i--) {
        make_lfn(de, (uint8_t)(i | (i == n ? LDIR_LAST_LONG_ENTRY : 0)), sum, frag[i - 1]);
        fat_lfn_feed(d, de);
    }
    return fat_lfn_take(d, sde, out, cap);
}

static void test_lfn(void)
{
    static fat_dir_t d;
    uint16_t frag[LFN_MAX_ENTRIES][13];
    uint8_t sde[32], de[32];
    char out[FAT_CFG_NAME_MAX + 1];
    char name[300];
    unsigned i;

    make_de(sde, "LONGFI~1TXT", 0, 0);

    CHECK(run_lfn(&d, "Long File Name With Spaces.txt", sde, out, FAT_CFG_NAME_MAX) == 30);
    CHECK(strcmp(out, "Long File Name With Spaces.txt") == 0);
    CHECK(run_lfn(&d, "exactly13char", sde, out, FAT_CFG_NAME_MAX) == 13);  /* no terminator */
    CHECK(run_lfn(&d, "Long File Name With Spaces.txt", sde, out, 10) == -1);

    for (i = 0; i < 255; i++)
        name[i] = (char)('a' + i % 26);
    name[255] = '\0';
    CHECK(run_lfn(&d, name, sde, out, FAT_CFG_NAME_MAX) == 255);
    name[255] = 'x';
    name[256] = '\0';
    CHECK(run_lfn(&d, name, sde, out, FAT_CFG_NAME_MAX) == 0);    /* > 255 units */

    /* A checksum that belongs to another short name orphans the run. */
    make_de(de, "OTHER   TXT", 0, 0);
    fragments("hello.txt", frag);
    fat_lfn_reset(&d);
    make_lfn(de, 0x41, fat32_lfn_checksum(de), frag[0]);
    fat_lfn_feed(&d, de);
    CHECK(fat_lfn_take(&d, sde, out, FAT_CFG_NAME_MAX) == 0);

    /* Ordinals out of order, a missing first entry, a missing last flag. */
    fragments("abcdefghijklmnopqrstuvwxyz0123", frag);   /* 3 fragments */
    fat_lfn_reset(&d);
    make_lfn(de, 0x43, fat32_lfn_checksum(sde), frag[2]);
    fat_lfn_feed(&d, de);
    make_lfn(de, 0x01, fat32_lfn_checksum(sde), frag[0]);
    fat_lfn_feed(&d, de);
    CHECK(fat_lfn_take(&d, sde, out, FAT_CFG_NAME_MAX) == 0);
    fat_lfn_reset(&d);
    make_lfn(de, 0x02, fat32_lfn_checksum(sde), frag[1]);
    fat_lfn_feed(&d, de);
    make_lfn(de, 0x01, fat32_lfn_checksum(sde), frag[0]);
    fat_lfn_feed(&d, de);
    CHECK(fat_lfn_take(&d, sde, out, FAT_CFG_NAME_MAX) == 0);
    fat_lfn_reset(&d);
    make_lfn(de, 0x55, fat32_lfn_checksum(sde), frag[0]);   /* ordinal 21 */
    fat_lfn_feed(&d, de);
    CHECK(fat_lfn_take(&d, sde, out, FAT_CFG_NAME_MAX) == 0);

    /* A new run abandons an unfinished one. */
    fat_lfn_reset(&d);
    make_lfn(de, 0x43, fat32_lfn_checksum(sde), frag[2]);
    fat_lfn_feed(&d, de);
    fragments("hello.txt", frag);
    make_lfn(de, 0x41, fat32_lfn_checksum(sde), frag[0]);
    fat_lfn_feed(&d, de);
    CHECK(fat_lfn_take(&d, sde, out, FAT_CFG_NAME_MAX) == 9);
    CHECK(strcmp(out, "hello.txt") == 0);
    CHECK(fat_lfn_take(&d, sde, out, FAT_CFG_NAME_MAX) == 0);      /* consumed */

    /* UTF-16: BMP, a surrogate pair, and unpaired halves. */
    {
        static const uint16_t u[13] = { 0x00F1, 0x20AC, 0xD83D, 0xDE80,
                                        0xD800, 'x', 0xDC00, 0, 0xFFFF,
                                        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
        fat_lfn_reset(&d);
        make_lfn(de, 0x41, fat32_lfn_checksum(sde), u);
        fat_lfn_feed(&d, de);
        CHECK(fat_lfn_take(&d, sde, out, FAT_CFG_NAME_MAX) == 2 + 3 + 4 + 3 + 1 + 3);
        CHECK(strcmp(out, "\xC3\xB1" "\xE2\x82\xAC" "\xF0\x9F\x9A\x80"
                          "\xEF\xBF\xBD" "x" "\xEF\xBF\xBD") == 0);
    }
}

/* ---- L2 and the table, on a volume built in memory ----------------------------- */

#define V_CLUSTERS  66000u                      /* just above the FAT32 minimum */
#define V_SECTORS   (T_DATA_START + V_CLUSTERS)
#define V_OFFSET    2048u                       /* partition start, MBR case */

static void put_de(uint8_t *de, const char *name11, uint8_t attr,
                   uint32_t cluster, uint32_t size)
{
    make_de(de, name11, attr, 0);
    put16(de + DIR_FstClusHI_OFF, (uint16_t)(cluster >> 16));
    put16(de + DIR_FstClusLO_OFF, (uint16_t)cluster);
    put32(de + DIR_FileSize_OFF, size);
}

/* A FAT32 volume: HELLO.TXT (cluster 3, "hello") and SUB (cluster 4)
 * holding an empty INNER.TXT, under a volume label. */
static void make_volume(uint8_t *v)
{
    uint8_t *fsi = v + 512, *root, *sub;
    unsigned f;

    make_bpb(v, V_SECTORS);
    put32(fsi + FSI_LeadSig_OFF, FSI_LEADSIG);
    put32(fsi + FSI_StrucSig_OFF, FSI_STRUCSIG);
    put32(fsi + FSI_TrailSig_OFF, FSI_TRAILSIG);
    put32(fsi + FSI_Free_Count_OFF, V_CLUSTERS - 3u);
    put32(fsi + FSI_Nxt_Free_OFF, 5);
    for (f = 0; f < 2; f++) {
        uint8_t *fat = v + (32u + f * 780u) * 512u;
        put32(fat + 0, 0x0FFFFFF8uL);
        put32(fat + 4, 0x0FFFFFFFuL);
        put32(fat + 8, 0x0FFFFFFFuL);           /* root */
        put32(fat + 12, 0x0FFFFFFFuL);          /* HELLO.TXT */
        put32(fat + 16, 0x0FFFFFFFuL);          /* SUB */
    }
    root = v + T_DATA_START * 512u;
    put_de(root, "NOVA64     ", FAT_ATTR_VOLUME_ID, 0, 0);
    put_de(root + 32, "HELLO   TXT", FAT_ATTR_ARCHIVE, 3, 5);
    put_de(root + 64, "SUB        ", FAT_ATTR_DIRECTORY, 4, 0);
    memcpy(v + (T_DATA_START + 1u) * 512u, "hello", 5);
    sub = v + (T_DATA_START + 2u) * 512u;
    put_de(sub, ".          ", FAT_ATTR_DIRECTORY, 4, 0);
    put_de(sub + 32, "..         ", FAT_ATTR_DIRECTORY, 0, 0);
    put_de(sub + 64, "INNER   TXT", FAT_ATTR_ARCHIVE, 0, 0);
}

static void test_mount(void)
{
    static fat_fs_t fs;
    uint8_t *disk = calloc(V_OFFSET + V_SECTORS, 512);
    uint8_t buf[512];
    bdev_t bd;
    uint32_t lba, count;
    char label[FAT_SHORT_NAME_BUF];

    if (!disk) {
        CHECK(disk != 0);
        return;
    }
    /* Whole device. */
    make_volume(disk);
    bdev_mem_open(&bd, disk, V_SECTORS * 512u, 512);
    CHECK(fat_probe(&bd, buf, &lba, &count) == FS_OK && lba == 0 && count == V_SECTORS);
    CHECK(fat_mount_auto(&fs, &bd) == FS_OK && fs.g.cluster_count == V_CLUSTERS);
    CHECK(fs.fsinfo_valid && fs.fsinfo_free == V_CLUSTERS - 3u);
    CHECK(fat_label(&fs, label) == FS_OK && strcmp(label, "NOVA64") == 0);

    /* The same volume in the second partition of an MBR disk, after one
     * that holds nothing, so that probing has to skip it. */
    memmove(disk + V_OFFSET * 512u, disk, V_SECTORS * 512u);
    memset(disk, 0, V_OFFSET * 512u);
    disk[446 + 4] = 0x83;
    put32(disk + 446 + 8, 64);
    put32(disk + 446 + 12, 100);
    disk[462 + 4] = 0x0C;
    put32(disk + 462 + 8, V_OFFSET);
    put32(disk + 462 + 12, V_SECTORS);
    disk[510] = 0x55;
    disk[511] = 0xAA;
    bdev_mem_open(&bd, disk, (V_OFFSET + V_SECTORS) * 512u, 512);
    CHECK(fat_probe(&bd, buf, &lba, &count) == FS_OK && lba == V_OFFSET);
    CHECK(fat_mount_auto(&fs, &bd) == FS_OK && fs.part_lba == V_OFFSET);
    CHECK(fat_mount(&fs, &bd, 0, 0) != FS_OK);      /* the MBR is no volume */

    /* A volume larger than its device. */
    bdev_mem_open(&bd, disk, (V_OFFSET + V_SECTORS - 1u) * 512u, 512);
    CHECK(fat_mount(&fs, &bd, V_OFFSET, 0) == FS_ECORRUPT);
    free(disk);
}

static void test_fsops(void)
{
    static fat32_mount_t m;
    const fsops_t *ops = &fat32_fsops;
    uint8_t *disk = calloc(V_SECTORS, 512);
    fs_dir_cursor_t *c[FAT32_FSOPS_DIRS + 1];
    fs_file_t *f;
    fs_dirent_t de;
    fs_attr_t a;
    fs_ino_t hello, sub, ino;
    uint64_t blocks, bfree, files, ffree;
    uint32_t bsize;
    char buf[16];
    size_t got;
    unsigned i;
    bdev_t bd;

    if (!disk) {
        CHECK(disk != 0);
        return;
    }
    make_volume(disk);
    bdev_mem_open(&bd, disk, V_SECTORS * 512u, 512);

    CHECK(ops->mount(&m, &bd, 0) == FS_EROFS);          /* refused, not downgraded */
    CHECK(ops->mount(&m, &bd, FS_MOUNT_RDONLY) == FS_OK);
    CHECK(ops->write == 0 && ops->create == 0 && ops->mkdir == 0 &&
          ops->unlink == 0 && ops->rmdir == 0 && ops->rename == 0 &&
          ops->truncate == 0 && ops->setattr == 0 && ops->sync == 0);

    CHECK(ops->statfs(&m, &blocks, &bfree, &files, &ffree, &bsize) == FS_OK);
    CHECK(blocks == V_CLUSTERS && bfree == V_CLUSTERS - 3u && bsize == 512 && files == 0);

    CHECK(ops->lookup(&m, FS_INO_ROOT, "hello.txt", &hello) == FS_OK);
    CHECK(ops->lookup(&m, FS_INO_ROOT, "SUB", &sub) == FS_OK);
    CHECK(ops->lookup(&m, FS_INO_ROOT, "SUB/INNER.TXT", &ino) == FS_EINVAL);
    CHECK(ops->lookup(&m, FS_INO_ROOT, "nope", &ino) == FS_ENOENT);
    CHECK(ops->lookup(&m, hello, "x", &ino) == FS_ENOTDIR);
    CHECK(ops->lookup(&m, sub, "..", &ino) == FS_OK && ino == FS_INO_ROOT);
    CHECK(ops->lookup(&m, sub, ".", &ino) == FS_OK && ino == sub);
    CHECK(ops->readlink(&m, hello, buf, sizeof buf) == FS_EINVAL);

    CHECK(ops->getattr(&m, FS_INO_ROOT, &a) == FS_OK);
    CHECK(a.ino == FS_INO_ROOT && a.mode == (FS_S_IFDIR | 0755u) && a.nlink == 3);
    CHECK(a.size == 512 && a.mtime == 0);
    CHECK(ops->getattr(&m, sub, &a) == FS_OK && a.nlink == 2);
    CHECK(ops->getattr(&m, hello, &a) == FS_OK);
    CHECK(a.mode == (FS_S_IFREG | 0644u) && a.nlink == 1 && a.size == 5 && a.blocks == 1);
    CHECK(a.uid == 0 && a.gid == 0 && a.mtime == 315532800);   /* date 0: 1980 */

    /* Listings start with "." and "..", for the root as for SUB. */
    CHECK(ops->opendir(&m, FS_INO_ROOT, &c[0]) == FS_OK);
    CHECK(ops->readdir(&m, c[0], &de) == FS_OK && strcmp(de.name, ".") == 0 &&
          de.ino == FS_INO_ROOT && de.type == FS_DT_DIR);
    CHECK(ops->readdir(&m, c[0], &de) == FS_OK && strcmp(de.name, "..") == 0 &&
          de.ino == FS_INO_ROOT);
    CHECK(ops->readdir(&m, c[0], &de) == FS_OK && strcmp(de.name, "HELLO.TXT") == 0 &&
          de.ino == hello && de.type == FS_DT_REG && de.name_len == 9);
    CHECK(ops->readdir(&m, c[0], &de) == FS_OK && de.ino == sub);
    CHECK(ops->readdir(&m, c[0], &de) == FS_ENOENT);
    CHECK(ops->closedir(&m, c[0]) == FS_OK);
    CHECK(ops->closedir(&m, c[0]) == FS_EINVAL);        /* already closed */
    CHECK(ops->opendir(&m, sub, &c[0]) == FS_OK);
    CHECK(ops->readdir(&m, c[0], &de) == FS_OK && de.ino == sub);
    CHECK(ops->readdir(&m, c[0], &de) == FS_OK && de.ino == FS_INO_ROOT);
    CHECK(ops->readdir(&m, c[0], &de) == FS_OK && strcmp(de.name, "INNER.TXT") == 0);
    CHECK(ops->closedir(&m, c[0]) == FS_OK);
    CHECK(ops->opendir(&m, hello, &c[0]) == FS_ENOTDIR);

    /* The pools run out cleanly. */
    for (i = 0; i < FAT32_FSOPS_DIRS; i++)
        CHECK(ops->opendir(&m, FS_INO_ROOT, &c[i]) == FS_OK);
    CHECK(ops->opendir(&m, FS_INO_ROOT, &c[i]) == FS_EMFILE);
    for (i = 0; i < FAT32_FSOPS_DIRS; i++)
        CHECK(ops->closedir(&m, c[i]) == FS_OK);

    CHECK(ops->open(&m, hello, FS_O_RDWR, &f) == FS_EROFS);
    CHECK(ops->open(&m, sub, FS_O_RDONLY, &f) == FS_EISDIR);
    CHECK(ops->open(&m, hello, FS_O_RDONLY, &f) == FS_OK);
    CHECK(ops->read(&m, f, buf, sizeof buf, 0, &got) == FS_OK && got == 5);
    CHECK(memcmp(buf, "hello", 5) == 0);
    CHECK(ops->read(&m, f, buf, sizeof buf, 3, &got) == FS_OK && got == 2);
    CHECK(ops->read(&m, f, buf, sizeof buf, 0x100000000uLL, &got) == FS_OK && got == 0);
    CHECK(ops->close(&m, f) == FS_OK);
    CHECK(ops->read(&m, f, buf, sizeof buf, 0, &got) == FS_EINVAL);

    CHECK(ops->unmount(&m) == FS_OK);
    CHECK(ops->getattr(&m, FS_INO_ROOT, &a) == FS_EINVAL);
    free(disk);
}

int main(void)
{
    fs_set_corrupt_logger(quiet_logger);

    test_endian();
    test_bdev();
    test_mbr();
    test_bpb();
    test_fsinfo();
    test_dirent();
    test_datetime();
    test_lfn();
    test_mount();
    test_fsops();

    printf("unit: %u checks, %u failed\n", checks, failures);
    return failures ? 1 : 0;
}
