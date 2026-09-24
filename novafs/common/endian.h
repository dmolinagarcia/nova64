/*
 * endian.h — explicit little-endian accessors, alignment-safe.
 *
 * FAT32 is little-endian on disk and several of its fields sit on odd
 * offsets (BPB_BytsPerSec is a 16-bit field at byte 11), so fields are
 * always read through these accessors and never through a packed struct
 * cast over a sector buffer (DN-FS-FUSE-001 §4.2, sheet Y4.7).
 *
 * Every shift is done on a type that is at least as wide as the result,
 * so the code is also correct with a 16-bit int.
 */
#ifndef NOVAFS_ENDIAN_H
#define NOVAFS_ENDIAN_H

#include <stdint.h>

static inline uint8_t rd8(const uint8_t *p)
{
    return p[0];
}

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static inline uint32_t rd32(const uint8_t *p)
{
    return  (uint32_t)p[0]        | ((uint32_t)p[1] <<  8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#endif /* NOVAFS_ENDIAN_H */
