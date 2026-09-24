/*
 * mbr.c — MBR partition table decoding.
 */
#include "endian.h"
#include "mbr.h"

int mbr_parse(const uint8_t *sec, mbr_part_t part[4])
{
    unsigned i;

    for (i = 0; i < 4u; i++) {
        const uint8_t *e = sec + MBR_TABLE_OFF + i * MBR_ENTRY_SIZE;
        part[i].status = rd8(e + 0);
        part[i].type   = rd8(e + 4);
        part[i].lba    = rd32(e + 8);
        part[i].count  = rd32(e + 12);
    }
    return sec[MBR_SIG_OFF] == 0x55u && sec[MBR_SIG_OFF + 1u] == 0xAAu;
}

int mbr_type_is_container(uint8_t type)
{
    return type == MBR_TYPE_EXTENDED || type == MBR_TYPE_EXT_LBA ||
           type == MBR_TYPE_EXT_LINUX || type == MBR_TYPE_GPT;
}
