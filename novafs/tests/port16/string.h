/*
 * Minimal <string.h> for `make check16`, whose freestanding 16-bit
 * target ships no C library. Declarations only; nothing is linked.
 */
#ifndef NOVAFS_PORT16_STRING_H
#define NOVAFS_PORT16_STRING_H

#include <stddef.h>

void  *memcpy(void *restrict dst, const void *restrict src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char  *strchr(const char *s, int c);

#endif
