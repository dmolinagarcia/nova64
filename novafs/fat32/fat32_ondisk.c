/*
 * fat32_ondisk.c — L1: decoding and validation of FAT32 structures.
 *
 * No I/O, no allocation, no logging. Every value decoded here comes from
 * a disk that may be damaged, so every value is checked before anything
 * above is allowed to compute an address from it.
 */
#include <string.h>

#include "endian.h"
#include "fat32_ondisk.h"

/* Code page 437, bytes 0x80..0xFF, as Unicode. Short names are stored in
 * the OEM code page and 437 is what the Linux driver assumes by default. */
static const uint16_t cp437_high[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,  /* 80 */
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,  /* 88 */
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,  /* 90 */
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,  /* 98 */
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,  /* A0 */
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,  /* A8 */
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,  /* B0 */
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,  /* B8 */
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,  /* C0 */
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,  /* C8 */
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,  /* D0 */
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,  /* D8 */
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,  /* E0 */
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,  /* E8 */
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,  /* F0 */
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,  /* F8 */
};

static uint8_t log2_pow2(uint32_t v)
{
    uint8_t s = 0;

    while (v > 1u) {
        v >>= 1;
        s++;
    }
    return s;
}

/* ---- Boot sector ----------------------------------------------------- */

#define BAD(code, reason) do { *why = (reason); return (code); } while (0)

fs_err_t fat32_parse_bpb(const uint8_t *sec, fat32_geom_t *g, const char **why)
{
    const char *unused;
    uint32_t bps, spc, rsvd, nfats, root_ent, tot, fatsz;
    uint32_t first_data, fats, count, fat_entries;
    uint16_t fatsz16, ext;

    if (why == 0)
        why = &unused;
    *why = "";
    memset(g, 0, sizeof *g);

    if (sec[BS_Sign_OFF] != 0x55u || sec[BS_Sign_OFF + 1] != 0xAAu)
        BAD(FS_ENOTSUP, "no boot sector signature");
    if (memcmp(sec + BS_OEMName_OFF, "EXFAT   ", 8) == 0)
        BAD(FS_ENOTSUP, "exFAT volume");
    if (memcmp(sec + BS_OEMName_OFF, "NTFS    ", 8) == 0)
        BAD(FS_ENOTSUP, "NTFS volume");

    bps = rd16(sec + BPB_BytsPerSec_OFF);
    if (bps != 512u && bps != 1024u && bps != 2048u && bps != 4096u)
        BAD(FS_ECORRUPT, "BPB_BytsPerSec is not 512, 1024, 2048 or 4096");
    spc = rd8(sec + BPB_SecPerClus_OFF);
    if (spc == 0 || (spc & (spc - 1u)) != 0)
        BAD(FS_ECORRUPT, "BPB_SecPerClus is not a power of two");
    if (bps * spc > 32768u)
        BAD(FS_ECORRUPT, "cluster larger than 32 KiB");
    rsvd = rd16(sec + BPB_RsvdSecCnt_OFF);
    if (rsvd == 0)
        BAD(FS_ECORRUPT, "BPB_RsvdSecCnt is zero");
    nfats = rd8(sec + BPB_NumFATs_OFF);
    if (nfats == 0 || nfats > 2u)
        BAD(FS_ECORRUPT, "BPB_NumFATs is not 1 or 2");

    root_ent = rd16(sec + BPB_RootEntCnt_OFF);
    tot = rd16(sec + BPB_TotSec16_OFF);
    if (tot == 0)
        tot = rd32(sec + BPB_TotSec32_OFF);
    fatsz16 = rd16(sec + BPB_FATSz16_OFF);
    fatsz = fatsz16 != 0 ? fatsz16 : rd32(sec + BPB_FATSz32_OFF);
    if (tot == 0)
        BAD(FS_ECORRUPT, "total sector count is zero");
    if (fatsz == 0)
        BAD(FS_ECORRUPT, "FAT size is zero");

    /* The FAT type follows from the cluster count and from nothing else:
     * BS_FilSysType is documentation, not data. Every step is ordered so
     * that no intermediate value can overflow on a hostile BPB. */
    first_data = rsvd + (root_ent * DIR_ENTRY_SIZE + bps - 1u) / bps;
    if (first_data >= tot)
        BAD(FS_ECORRUPT, "reserved region larger than the volume");
    if (fatsz > (tot - first_data) / nfats)
        BAD(FS_ECORRUPT, "FATs larger than the volume");
    fats = nfats * fatsz;
    if (fats >= tot - first_data)
        BAD(FS_ECORRUPT, "no room for a data region");
    first_data += fats;
    count = (tot - first_data) >> log2_pow2(spc);

    if (count < 4085u)
        BAD(FS_ENOTSUP, "FAT12 volume");
    if (count < FAT32_MIN_CLUSTERS)
        BAD(FS_ENOTSUP, "FAT16 volume");

    /* From here on the volume is FAT32 and must look like one. */
    if (fatsz16 != 0 || root_ent != 0)
        BAD(FS_ECORRUPT, "FAT32 volume with FAT12/16 root or FAT size set");
    if (rd16(sec + BPB_FSVer_OFF) != 0)
        BAD(FS_ENOTSUP, "unsupported BPB_FSVer");
    if (count > FAT32_BAD - 2u)
        BAD(FS_ECORRUPT, "too many clusters for FAT32");

    g->bytes_per_sector    = (uint16_t)bps;
    g->sectors_per_cluster = (uint8_t)spc;
    g->num_fats            = (uint8_t)nfats;
    g->reserved_sectors    = (uint16_t)rsvd;
    g->sec_shift           = log2_pow2(bps);
    g->clus_shift          = log2_pow2(spc);
    g->fat_size            = fatsz;
    g->total_sectors       = tot;
    g->data_start          = first_data;

    /* A FAT too small to describe every cluster limits the volume to the
     * clusters it can describe, which is what the Linux driver does. One
     * FAT sector holds at least 128 entries, so this cannot underflow. */
    if (fatsz > (0xFFFFFFFFuL >> (g->sec_shift - 2u)))
        fat_entries = 0xFFFFFFFFuL;
    else
        fat_entries = fatsz << (g->sec_shift - 2u);
    if (fat_entries - 2u < count)
        count = fat_entries - 2u;
    g->cluster_count = count;
    g->max_cluster   = count + 1u;

    ext = rd16(sec + BPB_ExtFlags_OFF);
    g->ext_flags = ext;
    if (ext & BPB_EXTFLAGS_NOMIRROR) {
        g->active_fat = (uint8_t)(ext & BPB_EXTFLAGS_ACTIVE);
        if (g->active_fat >= nfats)
            BAD(FS_ECORRUPT, "BPB_ExtFlags names a FAT that does not exist");
    }
    g->fat_start = rsvd + (uint32_t)g->active_fat * fatsz;

    g->root_cluster = rd32(sec + BPB_RootClus_OFF);
    if (g->root_cluster < 2u || g->root_cluster > g->max_cluster)
        BAD(FS_ECORRUPT, "BPB_RootClus out of range");

    /* A missing or misplaced FSInfo only costs the free-space hint. */
    g->fsinfo_sector = rd16(sec + BPB_FSInfo_OFF);
    if (g->fsinfo_sector == 0 || g->fsinfo_sector >= rsvd)
        g->fsinfo_sector = 0;

    g->boot_sig = rd8(sec + BS_BootSig_OFF);
    if (g->boot_sig == BS_BOOTSIG_EXTENDED) {
        g->volume_id = rd32(sec + BS_VolID_OFF);
        memcpy(g->volume_label, sec + BS_VolLab_OFF, 11);
    } else {
        memset(g->volume_label, ' ', 11);
    }
    return FS_OK;
}

int fat32_parse_fsinfo(const uint8_t *sec, const fat32_geom_t *g,
                       uint32_t *free_count, uint32_t *next_free)
{
    uint32_t fc, nf;

    *free_count = FAT32_UNKNOWN;
    *next_free = FAT32_UNKNOWN;
    if (rd32(sec + FSI_LeadSig_OFF) != FSI_LEADSIG ||
        rd32(sec + FSI_StrucSig_OFF) != FSI_STRUCSIG ||
        rd32(sec + FSI_TrailSig_OFF) != FSI_TRAILSIG)
        return 0;

    fc = rd32(sec + FSI_Free_Count_OFF);
    nf = rd32(sec + FSI_Nxt_Free_OFF);
    if (fc <= g->cluster_count)
        *free_count = fc;
    if (nf >= 2u && nf <= g->max_cluster)
        *next_free = nf;
    return 1;
}

/* ---- Directory entries ------------------------------------------------ */

static int name_is(const uint8_t *de, const char *name11)
{
    return memcmp(de + DIR_Name_OFF, name11, 11) == 0;
}

fat32_de_kind_t fat32_de_kind(const uint8_t *de)
{
    uint8_t c = de[DIR_Name_OFF];
    uint8_t attr = de[DIR_Attr_OFF];

    if (c == DIR_NAME_END)
        return FAT_DE_END;
    if (c == DIR_NAME_DELETED)
        return FAT_DE_DELETED;
    if ((attr & FAT_ATTR_LONG_NAME_MASK) == FAT_ATTR_LONG_NAME)
        return FAT_DE_LFN;
    if (attr & FAT_ATTR_VOLUME_ID)
        return FAT_DE_VOLUME;
    if (c == '.') {
        if (name_is(de, ".          ") || name_is(de, "..         "))
            return FAT_DE_DOT;
        return FAT_DE_INVALID;      /* no 8.3 name starts with a dot */
    }
    if (c == ' ')
        return FAT_DE_INVALID;
    return FAT_DE_FILE;
}

int fat32_de_is_dotdot(const uint8_t *de)
{
    return name_is(de, "..         ") &&
           (de[DIR_Attr_OFF] & FAT_ATTR_LONG_NAME_MASK) != FAT_ATTR_LONG_NAME &&
           (de[DIR_Attr_OFF] & FAT_ATTR_DIRECTORY) != 0;
}

uint32_t fat32_de_cluster(const uint8_t *de)
{
    return ((uint32_t)rd16(de + DIR_FstClusHI_OFF) << 16) |
            (uint32_t)rd16(de + DIR_FstClusLO_OFF);
}

/* Appends one code page 437 byte as UTF-8. */
static uint16_t put_oem(char *out, uint16_t n, uint8_t c)
{
    uint16_t u;

    if (c < 0x80u) {
        out[n++] = (char)c;
        return n;
    }
    u = cp437_high[c - 0x80u];
    if (u < 0x800u) {
        out[n++] = (char)(0xC0u | (u >> 6));
    } else {
        out[n++] = (char)(0xE0u | (u >> 12));
        out[n++] = (char)(0x80u | ((u >> 6) & 0x3Fu));
    }
    out[n++] = (char)(0x80u | (u & 0x3Fu));
    return n;
}

static uint8_t trimmed(const uint8_t *s, uint8_t len)
{
    while (len > 0 && s[len - 1u] == ' ')
        len--;
    return len;
}

uint16_t fat32_short_name(const uint8_t *de, char *out)
{
    const uint8_t *name = de + DIR_Name_OFF;
    uint8_t ntres = de[DIR_NTRes_OFF];
    uint8_t base = trimmed(name, 8);
    uint8_t ext = trimmed(name + 8, 3);
    uint16_t n = 0;
    uint8_t i;

    for (i = 0; i < base; i++) {
        uint8_t c = name[i];
        if (i == 0 && c == DIR_NAME_KANJI_E5)
            c = DIR_NAME_DELETED;
        if ((ntres & DIR_NTRES_LC_BASE) && c >= 'A' && c <= 'Z')
            c = (uint8_t)(c + ('a' - 'A'));
        n = put_oem(out, n, c);
    }
    if (ext > 0) {
        out[n++] = '.';
        for (i = 0; i < ext; i++) {
            uint8_t c = name[8u + i];
            if ((ntres & DIR_NTRES_LC_EXT) && c >= 'A' && c <= 'Z')
                c = (uint8_t)(c + ('a' - 'A'));
            n = put_oem(out, n, c);
        }
    }
    out[n] = '\0';
    return n;
}

uint16_t fat32_label_name(const uint8_t *raw11, char *out)
{
    uint8_t len = trimmed(raw11, 11);
    uint16_t n = 0;
    uint8_t i;

    for (i = 0; i < len; i++)
        n = put_oem(out, n, raw11[i]);
    out[n] = '\0';
    return n;
}

uint8_t fat32_lfn_checksum(const uint8_t *name11)
{
    uint8_t sum = 0;
    uint8_t i;

    for (i = 0; i < 11u; i++)
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + name11[i]);
    return sum;
}

void fat32_lfn_units(const uint8_t *de, uint16_t out[LFN_UNITS_PER_ENTRY])
{
    uint8_t i;

    for (i = 0; i < 5u; i++)
        out[i] = rd16(de + LDIR_Name1_OFF + 2u * i);
    for (i = 0; i < 6u; i++)
        out[5u + i] = rd16(de + LDIR_Name2_OFF + 2u * i);
    for (i = 0; i < 2u; i++)
        out[11u + i] = rd16(de + LDIR_Name3_OFF + 2u * i);
}

/* ---- Timestamps -------------------------------------------------------- */

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31 };

    /* 2100 is the one century year inside FAT's 1980..2107 range. */
    if (month == 2 && (year % 4u) == 0 && year != 2100u)
        return 29;
    return dim[month - 1u];
}

int fat32_decode_datetime(uint16_t date, uint16_t time, uint8_t tenth,
                          fat_datetime_t *out)
{
    uint16_t year  = (uint16_t)(1980u + ((date >> 9) & 0x7Fu));
    uint8_t  month = (uint8_t)((date >> 5) & 0x0Fu);
    uint8_t  day   = (uint8_t)(date & 0x1Fu);
    uint8_t  hour  = (uint8_t)((time >> 11) & 0x1Fu);
    uint8_t  min   = (uint8_t)((time >> 5) & 0x3Fu);
    uint8_t  sec   = (uint8_t)((time & 0x1Fu) * 2u);

    if (month < 1u || month > 12u || day < 1u ||
        day > days_in_month(year, month) ||
        hour > 23u || min > 59u || tenth > 199u ||
        sec + tenth / 100u > 59u) {
        out->year = 1980;
        out->month = 1;
        out->day = 1;
        out->hour = 0;
        out->minute = 0;
        out->second = 0;
        out->centisecond = 0;
        return 0;
    }
    out->year = year;
    out->month = month;
    out->day = day;
    out->hour = hour;
    out->minute = min;
    out->second = (uint8_t)(sec + tenth / 100u);
    out->centisecond = (uint8_t)(tenth % 100u);
    return 1;
}
