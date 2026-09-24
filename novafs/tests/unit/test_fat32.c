/*
 * test_fat32.c — unit tests for L0, L1 and long-name assembly.
 *
 * Everything here runs on static byte arrays: no image, no mount, no
 * external tools. The image-based tests live in tests/run_tests.py.
 */
#include <stdio.h>
#include <string.h>

#include "bdev.h"
#include "endian.h"
#include "fat32_ondisk.h"
#include "fat32_priv.h"
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

    printf("unit: %u checks, %u failed\n", checks, failures);
    return failures ? 1 : 0;
}
