/*
 * fat32_lfn.c — L2: reassembly of VFAT long file names.
 *
 * A long name is stored as a run of LFN entries just before its short
 * entry, last fragment first: ordinals descend to 1 and the first entry
 * on disk carries LDIR_LAST_LONG_ENTRY. Every fragment repeats the
 * checksum of the short name it belongs to.
 *
 * A run that breaks any of those rules is orphaned — typically left by a
 * crash — and is dropped in favour of the short name. That is not an
 * error: the kernel tolerates orphans and so must we.
 */
#include "fat32_priv.h"

enum { LFN_NONE, LFN_BUILDING, LFN_COMPLETE };

void fat_lfn_reset(fat_dir_t *d)
{
    d->lfn_state = LFN_NONE;
    d->lfn_count = 0;
    d->lfn_expect = 0;
}

void fat_lfn_feed(fat_dir_t *d, const uint8_t *de)
{
    uint8_t ord = de[LDIR_Ord_OFF];
    uint8_t n = (uint8_t)(ord & (uint8_t)~LDIR_LAST_LONG_ENTRY);

    if (n < 1u || n > LFN_MAX_ENTRIES) {
        fat_lfn_reset(d);
        return;
    }
    if (ord & LDIR_LAST_LONG_ENTRY) {
        /* Starts a run, abandoning any unfinished one. */
        d->lfn_state = LFN_BUILDING;
        d->lfn_count = n;
        d->lfn_sum = de[LDIR_Chksum_OFF];
    } else if (d->lfn_state != LFN_BUILDING || n != d->lfn_expect ||
               de[LDIR_Chksum_OFF] != d->lfn_sum) {
        fat_lfn_reset(d);
        return;
    }
    fat32_lfn_units(de, &d->lfn[(uint16_t)(n - 1u) * LFN_UNITS_PER_ENTRY]);
    d->lfn_expect = (uint8_t)(n - 1u);
    if (d->lfn_expect == 0)
        d->lfn_state = LFN_COMPLETE;
}

/* Appends one code point as UTF-8; returns the new length, or -1 if it
 * does not fit in cap bytes. */
static int32_t put_utf8(char *out, uint32_t n, uint32_t cap, uint32_t cp)
{
    uint32_t len = cp < 0x80u ? 1u : cp < 0x800u ? 2u :
                   cp < (uint32_t)0x10000uL ? 3u : 4u;

    if (n + len > cap)
        return -1;
    switch (len) {
    case 1:
        out[n] = (char)cp;
        break;
    case 2:
        out[n]     = (char)(0xC0u | (cp >> 6));
        out[n + 1] = (char)(0x80u | (cp & 0x3Fu));
        break;
    case 3:
        out[n]     = (char)(0xE0u | (cp >> 12));
        out[n + 1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[n + 2] = (char)(0x80u | (cp & 0x3Fu));
        break;
    default:
        out[n]     = (char)(0xF0u | (cp >> 18));
        out[n + 1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[n + 2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[n + 3] = (char)(0x80u | (cp & 0x3Fu));
        break;
    }
    return (int32_t)(n + len);
}

int32_t fat_lfn_take(fat_dir_t *d, const uint8_t *de, char *out, uint32_t cap)
{
    uint16_t units, i;
    uint32_t n = 0;
    int ok;

    ok = d->lfn_state == LFN_COMPLETE &&
         d->lfn_sum == fat32_lfn_checksum(de + DIR_Name_OFF);
    units = (uint16_t)(d->lfn_count * LFN_UNITS_PER_ENTRY);
    fat_lfn_reset(d);
    if (!ok)
        return 0;

    /* The name ends at a 0x0000 unit, or fills the run exactly. */
    for (i = 0; i < units; i++) {
        if (d->lfn[i] == 0)
            break;
    }
    units = i;
    if (units == 0 || units > LFN_MAX_UNITS)
        return 0;

    for (i = 0; i < units; i++) {
        uint32_t cp = d->lfn[i];
        int32_t r;

        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1u < units &&
            d->lfn[i + 1u] >= 0xDC00u && d->lfn[i + 1u] <= 0xDFFFu) {
            cp = (uint32_t)0x10000uL + ((cp - 0xD800u) << 10) +
                 (uint32_t)(d->lfn[i + 1u] - 0xDC00u);
            i++;
        } else if (cp >= 0xD800u && cp <= 0xDFFFu) {
            cp = 0xFFFDu;               /* unpaired surrogate */
        }
        r = put_utf8(out, n, cap, cp);
        if (r < 0)
            return -1;
        n = (uint32_t)r;
    }
    out[n] = '\0';
    return (int32_t)n;
}
