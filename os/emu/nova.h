#ifndef NOVA_H
#define NOVA_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t a, x, y, s, d, pc;
    uint8_t pbr, dbr, p;
    int e;
    int stopped;            /* 1 = STP executed, 2 = emulator halt */
    int waiting;
    uint64_t cycles;
} cpu_t;

extern cpu_t cpu;
void cpu_reset(void);
void cpu_step(void);

uint8_t bus_read(uint32_t addr);
void bus_write(uint32_t addr, uint8_t v);

#endif
