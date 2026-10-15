/*
 * mbr.h — decoding of the classic MBR partition table. No I/O.
 */
#ifndef NOVAFS_MBR_H
#define NOVAFS_MBR_H

#include <stdint.h>

#define MBR_TABLE_OFF       446u
#define MBR_ENTRY_SIZE      16u
#define MBR_SIG_OFF         510u

#define MBR_TYPE_EMPTY      0x00u
#define MBR_TYPE_EXTENDED   0x05u
#define MBR_TYPE_EXT_LBA    0x0Fu
#define MBR_TYPE_EXT_LINUX  0x85u
#define MBR_TYPE_GPT        0xEEu

typedef struct mbr_part {
    uint8_t  status;
    uint8_t  type;
    uint32_t lba;               /* first sector                 */
    uint32_t count;             /* number of sectors            */
} mbr_part_t;

/* Decodes the four primary entries of a 512-byte (or larger) sector.
 * Returns 1 if the 0x55AA signature is present, 0 otherwise; the table
 * is decoded either way. */
int mbr_parse(const uint8_t *sec, mbr_part_t part[4]);

/* 1 for an extended-partition container or a GPT protective entry,
 * neither of which can hold a volume directly. */
int mbr_type_is_container(uint8_t type);

#endif /* NOVAFS_MBR_H */
