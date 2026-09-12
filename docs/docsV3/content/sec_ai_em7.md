# Building an emulator — the course
> the part you are modelling · nineteen stages · code you can type in · and a test that can fail

This sheet takes a competent C programmer who has never written an emulator from an empty directory to a working, instrumented, browser-hosted noVa64. Every stage has code, a build that runs, and **a test that can fail** — the last being the part that distinguishes a course from a tour. [EM1](sec_ai_em1) says what to model and to what fidelity, [EM4](sec_ai_em4) reports what was built; this one is how, and [W6](sc_w6) is the machine you type it on.

## Part 0 — the machine you are modelling.

- EM7.1 — **[E.2](sec_ai_e#e2) is the programmer's model and [E.12](sec_ai_e#e12) the vectors; what follows is the same thing from the implementer's side.** Skip it only if you already know the part.

| Register | Width | What it is |
|---|---|---|
| `A` | 16 | Accumulator. Its **low 8 bits** are used when `M` is set; the high half remains and is called `B` |
| `X`, `Y` | 16 | Index registers. 8-bit when `X` is set, and **the high byte is then forced to zero** |
| `S` | 16 | Stack pointer. In emulation mode the high byte is forced to `$01` |
| `D` | 16 | Direct page register. Direct-page addressing computes `D + offset` |
| `DBR` | 8 | Data bank — bits 23:16 for most data accesses |
| `PBR` | 8 | Program bank — bits 23:16 for instruction fetches |
| `PC` | 16 | Program counter. **It wraps within its bank** and never carries into `PBR` |
| `P` | 8 | Status flags |
| `E` | 1 | Emulation flag. Not visible in `P`; swapped with carry by `XCE` |

- EM7.2 — **The status register, and bit 5 and bit 4 change meaning with the mode.**

```
bit  7   6   5   4   3   2   1   0
     N   V   M   X   D   I   Z   C      native mode
     N   V   1   B   D   I   Z   C      emulation mode
```

  `N`, `V`, `Z`, `C`, `D` and `I` are as on a 6502. **`M` selects an 8-bit accumulator when set; `X` selects 8-bit index registers when set.** In emulation mode both are forced set, so everything is 8-bit and bit 4 behaves as the 6502 `B` flag.
- EM7.3 — **The part powers up in emulation mode, where it is a 6502 with extras**, and `XCE` exchanges carry with the emulation flag.

```asm
    CLC          ; carry = 0
    XCE          ; E = old carry = 0  -> native mode
    REP #$30     ; clear M and X  -> 16-bit accumulator and index
    SEP #$20     ; set M          -> 8-bit accumulator, 16-bit index
```

  **This is where most of your bugs will come from.** An immediate operand is one byte or two depending on a flag that changes at run time.
- EM7.4 — **24 bits of address space, as 256 banks of 64 KB, and which bank an access uses depends on the addressing mode.** Fetches use `PBR`; most data accesses use `DBR`; direct page and stack are always bank zero; long addressing carries its own bank byte. The vectors are two sets in bank zero, and there is only one reset vector:

```
native:      COP $FFE4   BRK $FFE6   ABORT $FFE8   NMI $FFEA   IRQ $FFEE
emulation:   COP $FFF4               ABORT $FFF8   NMI $FFFA   IRQ/BRK $FFFE
reset:       $FFFC   (there is only one)
```

- EM7.5 — **And the property everything else rests on: every cycle of a 65816 is a bus cycle.** There are no internal cycles that stay off the bus — even an internal operation cycle drives an address — so a three-cycle instruction performs exactly three bus accesses. **That single fact is why the emulator is structured the way it is**, and it is the subject of the next stage.

## Stage 1 — skeleton, memory and the bus.

- EM7.6 — **Goal: a program that allocates the machine's memory, performs bus accesses and counts cycles. No CPU yet.**
- EM7.7 — **Put the cycle counter in the bus layer, and resist the instinct to write the CPU first.** With cycles counted inside the CPU, every opcode has to know its own timing and you end up maintaining a timing table alongside an interpreter — two things that must agree and eventually will not. **Worse, the MMU and the cache then have nowhere to charge their stalls.** Put the counter in the bus and cycle accuracy becomes a property of emitting the right accesses in the right order; you never write a cycle count anywhere.

```c
/* include/nova64.h */
#ifndef NOVA64_H
#define NOVA64_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef uint32_t va_t;      /* 24-bit address as the CPU emits it */
typedef uint32_t pa_t;      /* physical address                   */
typedef uint64_t cycle_t;

typedef enum {
    ACC_FETCH,      /* opcode or operand fetch     */
    ACC_READ,
    ACC_WRITE,
    ACC_VECTOR,
    ACC_IDLE        /* internal cycle, no transfer */
} access_t;

#define PA_SDRAM_SIZE  (64u * 1024u * 1024u)
#define PA_SRAM_BASE   0x10000000u
#define PA_SRAM_SIZE   (2u  * 1024u * 1024u)

typedef struct { uint8_t *sdram, *sram; } mem_t;

typedef struct {
    cycle_t  cycles;
    uint64_t bus_fetch, bus_read, bus_write, bus_idle, bus_vector;
} instr_t;

typedef struct system_s {
    mem_t   mem;
    instr_t in;
    bool    halted;
} system_t;

int  mem_init(mem_t *m);
void mem_free(mem_t *m);
uint8_t mem_raw_read (mem_t *m, pa_t pa);
void    mem_raw_write(mem_t *m, pa_t pa, uint8_t v);

uint8_t bus_read8 (system_t *s, va_t va, access_t kind);
void    bus_write8(system_t *s, va_t va, uint8_t v);
void    bus_idle  (system_t *s);

#endif
```

```c
/* src/memory.c */
#include "nova64.h"
#include <stdlib.h>

int mem_init(mem_t *m)
{
    m->sdram = calloc(1, PA_SDRAM_SIZE);
    m->sram  = calloc(1, PA_SRAM_SIZE);
    if (!m->sdram || !m->sram) { mem_free(m); return -1; }
    return 0;
}

void mem_free(mem_t *m)
{
    free(m->sdram); free(m->sram);
    m->sdram = m->sram = NULL;
}

uint8_t mem_raw_read(mem_t *m, pa_t pa)
{
    if (pa < PA_SDRAM_SIZE) return m->sdram[pa];
    if (pa >= PA_SRAM_BASE && pa < PA_SRAM_BASE + PA_SRAM_SIZE)
        return m->sram[pa - PA_SRAM_BASE];
    return 0xFF;                    /* unpopulated space reads as open bus */
}

void mem_raw_write(mem_t *m, pa_t pa, uint8_t v)
{
    if (pa < PA_SDRAM_SIZE) { m->sdram[pa] = v; return; }
    if (pa >= PA_SRAM_BASE && pa < PA_SRAM_BASE + PA_SRAM_SIZE)
        m->sram[pa - PA_SRAM_BASE] = v;
}
```

```c
/* src/bus.c */
#include "nova64.h"

static void count_kind(instr_t *in, access_t kind)
{
    switch (kind) {
    case ACC_FETCH:  in->bus_fetch++;  break;
    case ACC_READ:   in->bus_read++;   break;
    case ACC_WRITE:  in->bus_write++;  break;
    case ACC_VECTOR: in->bus_vector++; break;
    case ACC_IDLE:   in->bus_idle++;   break;
    }
}

uint8_t bus_read8(system_t *s, va_t va, access_t kind)
{
    va &= 0xFFFFFFu;
    count_kind(&s->in, kind);
    s->in.cycles += 1;
    return mem_raw_read(&s->mem, (pa_t)va);      /* flat map for now */
}

void bus_write8(system_t *s, va_t va, uint8_t v)
{
    va &= 0xFFFFFFu;
    count_kind(&s->in, ACC_WRITE);
    s->in.cycles += 1;
    mem_raw_write(&s->mem, (pa_t)va, v);
}

void bus_idle(system_t *s)
{
    /* An internal cycle is still a bus cycle on this part; it simply carries
     * no useful transfer. One cycle, and it can never stall. */
    s->in.bus_idle++;
    s->in.cycles += 1;
}
```

- EM7.8 — **The build is four lines, and the test is where the value is.**

```make
CFLAGS := -std=c11 -Wall -Wextra -O2 -Iinclude
SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

nova64: $(OBJ)
	$(CC) -o $@ $^ -lm
```

```c
#include "nova64.h"
#include <assert.h>

int main(void)
{
    system_t s = {0};
    assert(mem_init(&s.mem) == 0);

    cycle_t c0 = s.in.cycles;
    bus_write8(&s, 0x001234, 0x5A);
    assert(bus_read8(&s, 0x001234, ACC_READ) == 0x5A);
    assert(s.in.cycles - c0 == 2);          /* exactly one cycle per access */

    bus_idle(&s);
    assert(s.in.cycles - c0 == 3);
    assert(s.in.bus_idle == 1);             /* and it transferred nothing */

    /* unpopulated physical space reads as open bus, not as zero */
    assert(bus_read8(&s, 0x0FFFFFF, ACC_READ) == 0xFF);

    mem_free(&s.mem);
    puts("bus ok");
    return 0;
}
```

  **The cycle assertions are the point.** If a later change makes an access cost two cycles, this goes red at once instead of quietly skewing every measurement taken afterwards.
  NOTE: **Trap — making the bus read take a *physical* address.** It must take the address the CPU emits, because translation lands there at [EM7.36](sec_ai_em7#em736). Fix the signature now and you will not have to touch every call site later.

## Stage 2 — making it live.

- EM7.9 — **Goal: load an image, take the reset vector, execute a few opcodes, halt.** The moment it becomes a computer. Reset performs two bus accesses, and they count.

```c
#define F_C 0x01u
#define F_Z 0x02u
#define F_I 0x04u
#define F_D 0x08u
#define F_X 0x10u
#define F_M 0x20u
#define F_V 0x40u
#define F_N 0x80u

typedef struct {
    uint16_t A, X, Y, S, D, PC;
    uint8_t  DBR, PBR, P;
    bool     E, stopped;
} cpu_t;

void cpu_reset(system_t *s)
{
    cpu_t *c = &s->cpu;
    memset(c, 0, sizeof *c);
    c->E = true;                       /* emulation mode          */
    c->P = F_M | F_X | F_I;            /* 8-bit, interrupts off   */
    c->S = 0x01FD;                     /* stack in page one       */

    uint16_t lo = bus_read8(s, 0xFFFC, ACC_VECTOR);
    uint16_t hi = bus_read8(s, 0xFFFD, ACC_VECTOR);
    c->PC = (uint16_t)(lo | (hi << 8));
}
```

- EM7.10 — **Fetch and dispatch, with five opcodes and a loud default.**

```c
static inline void setzn8(cpu_t *c, uint8_t v)
{
    c->P = (uint8_t)((c->P & ~(F_Z | F_N))
         | (v == 0 ? F_Z : 0) | (v & 0x80u ? F_N : 0));
}

static uint8_t fetch8(system_t *s)
{
    cpu_t *c = &s->cpu;
    uint8_t v = bus_read8(s, ((va_t)c->PBR << 16) | c->PC, ACC_FETCH);
    c->PC = (uint16_t)(c->PC + 1);     /* wraps in bank, no carry to PBR */
    return v;
}

static uint16_t fetch16(system_t *s)
{
    uint16_t lo = fetch8(s);
    return (uint16_t)(lo | ((uint16_t)fetch8(s) << 8));
}

static void unimplemented(system_t *s, uint8_t op, uint16_t at)
{
    fprintf(stderr, "cpu: unimplemented opcode $%02X at %02X:%04X\n",
            op, s->cpu.PBR, at);
    s->halted = true;
}

void cpu_step(system_t *s)
{
    cpu_t *c = &s->cpu;
    uint16_t at = c->PC;
    uint8_t op = fetch8(s);

    switch (op) {
    case 0xA9:                                  /* LDA #imm, 8-bit */
        { uint8_t m = fetch8(s);
          c->A = (uint16_t)((c->A & 0xFF00u) | m);
          setzn8(c, m); }
        break;

    case 0xAD:                                  /* LDA abs */
        { uint16_t a = fetch16(s);
          uint8_t m = bus_read8(s, ((va_t)c->DBR << 16) | a, ACC_READ);
          c->A = (uint16_t)((c->A & 0xFF00u) | m);
          setzn8(c, m); }
        break;

    case 0x8D:                                  /* STA abs */
        { uint16_t a = fetch16(s);
          bus_write8(s, ((va_t)c->DBR << 16) | a, (uint8_t)c->A); }
        break;

    case 0x4C: c->PC = fetch16(s); break;                   /* JMP abs */
    case 0xEA: bus_idle(s); break;                          /* NOP     */
    case 0xDB: bus_idle(s); c->stopped = true; break;       /* STP     */

    default:   unimplemented(s, op, at); break;
    }
}
```

- EM7.11 — **Notice what you did not write: there is no cycle count anywhere in the dispatch.** `LDA abs` costs four cycles because it performs four accesses — the opcode, two address bytes, the data. **That is not a coincidence you have to maintain, it is what the code does.**
- EM7.12 — **An image to run, and an assertion that pins the emitted sequence.**

```python
# tools/mkfirst.py
import sys
ORG, END = 0xE000, 0x10000
code = bytes([
    0xA9, 0x42,              # LDA #$42
    0x8D, 0x00, 0x20,        # STA $2000
    0xAD, 0x00, 0x20,        # LDA $2000
    0xDB,                    # STP
])
img = bytearray(END - ORG)
img[0:len(code)] = code
img[0xFFFC - ORG] = ORG & 0xFF
img[0xFFFD - ORG] = ORG >> 8
open(sys.argv[1], "wb").write(img)
```

```c
system_t s = {0};
mem_init(&s.mem);
load_file(&s.mem, "tests/first.bin", 0xE000);
cpu_reset(&s);

assert(s.cpu.PC == 0xE000);             /* the reset vector was taken */
assert(s.in.cycles == 2);               /* and cost two accesses      */

while (!s.halted && !s.cpu.stopped) cpu_step(&s);

assert(mem_raw_read(&s.mem, 0x2000) == 0x42);
assert((s.cpu.A & 0xFF) == 0x42);
assert(s.in.cycles == 2 + 2 + 4 + 4 + 1);   /* reset, LDA#, STA, LDA, STP */
```

  That last assertion is worth more than it looks: **it pins the emitted cycle sequence**, which is what you will break by accident every time you touch an addressing mode.
  NOTE: **Trap — a silent default case.** A missing opcode that does nothing turns into a program that runs and produces wrong answers, and you will spend a day finding it. Halt loudly ([EM4.4](sec_ai_em4#em44)).

## Stage 3 — register width, the hardest part.

- EM7.13 — **This is where a 65816 emulator is genuinely harder than a 6502 one, and three consequences catch everyone.** **Immediate operands change length** — `LDA #$12` is two bytes with `M` set and three with it clear — so getting it wrong lands the program counter mid-instruction and everything after is garbage. **When your emulator executes nonsense, suspect width before suspecting the opcode.** **Setting the `X` flag zeroes the index high bytes** — not masks, zeroes, permanently. **And emulation mode forces both flags** whatever you write.

```c
static inline bool acc16(const cpu_t *c) { return !c->E && !(c->P & F_M); }
static inline bool idx16(const cpu_t *c) { return !c->E && !(c->P & F_X); }

static uint32_t rdw(system_t *s, va_t a, bool wide)
{
    uint32_t lo = bus_read8(s, a, ACC_READ);
    if (!wide) return lo;
    return lo | ((uint32_t)bus_read8(s, (a + 1) & 0xFFFFFF, ACC_READ) << 8);
}

static void wrw(system_t *s, va_t a, uint32_t v, bool wide)
{
    bus_write8(s, a, (uint8_t)v);
    if (wide) bus_write8(s, (a + 1) & 0xFFFFFF, (uint8_t)(v >> 8));
}
```

  **Because these go through the bus, a 16-bit access naturally costs two cycles. You did not have to say so.**
- EM7.14 — **The three mode-control opcodes, and every clause in them is load-bearing.**

```c
case 0xC2:                                  /* REP #imm */
    { uint8_t m = fetch8(s); bus_idle(s);
      c->P &= (uint8_t)~m;
      if (c->E) c->P |= (F_M | F_X);
      if (c->P & F_X) { c->X &= 0xFFu; c->Y &= 0xFFu; } }
    break;

case 0xE2:                                  /* SEP #imm */
    { uint8_t m = fetch8(s); bus_idle(s);
      c->P |= m;
      if (c->P & F_X) { c->X &= 0xFFu; c->Y &= 0xFFu; } }
    break;

case 0xFB:                                  /* XCE */
    { bus_idle(s);
      bool new_e = (c->P & F_C) != 0;
      c->P = (uint8_t)((c->P & ~F_C) | (c->E ? F_C : 0));
      c->E = new_e;
      if (c->E) {
          c->P |= (F_M | F_X);
          c->X &= 0xFFu; c->Y &= 0xFFu;
          c->S = (uint16_t)(0x0100u | (c->S & 0xFFu));
      } }
    break;

case 0xA9:                                  /* LDA #imm, width-aware */
    { bool w = acc16(c);
      uint32_t m = w ? fetch16(s) : fetch8(s);
      if (w) c->A = (uint16_t)m;
      else   c->A = (uint16_t)((c->A & 0xFF00u) | m);
      setzn(c, m, w); }
    break;
```

- EM7.15 — **Four assertions, and every one of them corresponds to a bug someone has shipped.**

```c
/* 8-bit: two bytes, two cycles */
run_snippet(&s, (uint8_t[]){0xA9, 0x12}, 2);
assert((s.cpu.A & 0xFF) == 0x12);
assert(s.in.cycles == 2);

/* native, 16-bit: three bytes, three cycles */
run_snippet(&s, (uint8_t[]){0x18, 0xFB, 0xC2, 0x30, 0xA9, 0x34, 0x12}, 7);
assert(s.cpu.A == 0x1234);
assert(s.cpu.E == false);

/* SEP #$10 must zero the index high bytes, not merely mask them */
s.cpu.X = 0xBEEF;
run_snippet(&s, (uint8_t[]){0xE2, 0x10}, 2);
assert(s.cpu.X == 0x00EF);

/* XCE back to emulation forces M, X and the stack page */
s.cpu.S = 0x0234;
run_snippet(&s, (uint8_t[]){0x38, 0xFB}, 2);
assert(s.cpu.E);
assert((s.cpu.P & (F_M | F_X)) == (F_M | F_X));
assert(s.cpu.S == 0x0134);
```

## Stage 4 — addressing modes.

- EM7.16 — **Every mode is a function returning a 24-bit address and performing its accesses on the way**, which is what keeps the penalties honest: a page-cross costs a cycle because the function emits an idle, not because a table says four.

```c
static va_t ea_dp(system_t *s)                  /* direct page */
{
    cpu_t *c = &s->cpu;
    uint8_t o = fetch8(s);
    if (c->D & 0xFFu) bus_idle(s);              /* penalty when DL != 0 */
    return (va_t)((uint16_t)(c->D + o));        /* always bank zero     */
}

static va_t ea_abs(system_t *s)                 /* absolute */
{
    cpu_t *c = &s->cpu;
    uint16_t a = fetch16(s);
    return ((va_t)c->DBR << 16) | a;            /* data bank */
}

static va_t ea_absx(system_t *s)                /* absolute,X */
{
    cpu_t *c = &s->cpu;
    uint16_t a = fetch16(s);
    va_t base = ((va_t)c->DBR << 16) | a;
    va_t ea   = (base + c->X) & 0xFFFFFFu;
    if (!idx16(c) || ((base & 0xFF00u) != (ea & 0xFF00u)))
        bus_idle(s);                            /* page-cross penalty */
    return ea;
}

static va_t ea_long(system_t *s)                /* absolute long */
{
    va_t lo = fetch16(s);
    return lo | ((va_t)fetch8(s) << 16);        /* carries its own bank */
}

static va_t ea_indy(system_t *s)                /* (dp),Y */
{
    cpu_t *c = &s->cpu;
    va_t p = ea_dp(s);
    uint16_t ptr = (uint16_t)rdw(s, p, true);
    va_t base = ((va_t)c->DBR << 16) | ptr;
    va_t ea   = (base + c->Y) & 0xFFFFFFu;
    if (!idx16(c) || ((base & 0xFF00u) != (ea & 0xFF00u))) bus_idle(s);
    return ea;
}

static va_t ea_indlong(system_t *s)             /* [dp] */
{
    va_t p = ea_dp(s);
    va_t lo = rdw(s, p, true);
    return lo | ((va_t)bus_read8(s, (p + 2) & 0xFFFFFF, ACC_READ) << 16);
}
```

  With those in place an opcode is one line: `case 0xBD: { uint32_t m = rdw(s, ea_absx(s), acc16(c)); load_a(c, m); } break;`
- EM7.17 — **Which bank an access uses is the most common addressing bug, so it is worth a table rather than a habit.**

| Mode | Bank |
|---|---|
| Instruction fetch | `PBR` |
| Direct page, stack | always `$00` |
| Absolute, absolute indexed, indirect | `DBR` |
| Long, long indexed, `[dp]`, `[dp],Y` | from the operand |

```c
s.cpu.DBR = 0x12;  s.cpu.D = 0x0100;  s.cpu.X = 0x0010;

assert(ea_of(&s, 0xA5, (uint8_t[]){0x40})             == 0x000140); /* dp ignores DBR  */
assert(ea_of(&s, 0xAD, (uint8_t[]){0x00, 0x30})       == 0x123000); /* abs uses DBR    */
assert(ea_of(&s, 0xAF, (uint8_t[]){0x00, 0x30, 0x7E}) == 0x7E3000); /* long ignores it */

/* absolute,X crossing a page costs an extra cycle in 8-bit index mode */
s.cpu.P |= F_X; s.cpu.X = 0xFF;
cycle_t c0 = s.in.cycles;
(void)ea_of(&s, 0xBD, (uint8_t[]){0x01, 0x30});
assert(s.in.cycles - c0 == 4);          /* fetch, lo, hi, penalty */
```

  NOTE: **Trap — direct page wraps within bank zero.** `D = $FFF0` with offset `$20` gives `$000010`, not `$010010`. Cast through `uint16_t` and the wrap is free.

## Stage 5 — the ALU and the flags.

- EM7.18 — **The overflow expression is worth understanding rather than copying.** `a ^ m` has the sign bit clear when the operands agree in sign; `a ^ r` has it set when the result differs from `a`. **Overflow is exactly "the operands agreed and the result did not".**

```c
static void op_adc(cpu_t *c, uint32_t m)
{
    bool w = acc16(c);
    uint32_t a    = w ? c->A : (c->A & 0xFFu);
    uint32_t lim  = w ? 0x10000u : 0x100u;
    uint32_t sign = w ? 0x8000u  : 0x80u;
    uint32_t carry = (c->P & F_C) ? 1u : 0u;

    uint32_t r = a + m + carry;

    /* Overflow: the operands agreed in sign and the result disagrees. */
    c->P = (uint8_t)((c->P & ~(F_V | F_C))
         | (((~(a ^ m) & (a ^ r)) & sign) ? F_V : 0)
         | ((r >= lim) ? F_C : 0));

    r &= (lim - 1u);
    if (w) c->A = (uint16_t)r;
    else   c->A = (uint16_t)((c->A & 0xFF00u) | r);
    setzn(c, r, w);
}
```

- EM7.19 — **Decimal mode is real and it is used, so add nibble by nibble and correct each.**

```c
if (c->P & F_D) {
    uint32_t nib = w ? 4u : 2u, cin = carry, r = 0;
    for (uint32_t i = 0; i < nib; i++) {
        uint32_t sh = i * 4u;
        uint32_t d = ((a >> sh) & 0xFu) + ((m >> sh) & 0xFu) + cin;
        cin = 0;
        if (d > 9u) { d += 6u; cin = 1u; }
        r |= (d & 0xFu) << sh;
    }
    carry = cin;
}
```

- EM7.20 — **Read-modify-write instructions read, spend an internal cycle, then write — and forgetting that idle leaves every one of them a cycle short.**

```c
static void rmw(system_t *s, va_t a, uint32_t (*f)(cpu_t *, uint32_t, bool))
{
    bool w = acc16(&s->cpu);
    uint32_t v = rdw(s, a, w);
    bus_idle(s);                    /* the modify cycle is a bus cycle */
    wrw(s, a, f(&s->cpu, v, w), w);
}
```

- EM7.21 — **Test the ALU table-driven, because the interesting cases are all boundaries.**

```c
struct { uint8_t a, m, cin, r, flags; } adc_cases[] = {
    { 0x50, 0x10, 0, 0x60, 0            },   /* no overflow            */
    { 0x50, 0x50, 0, 0xA0, F_N | F_V    },   /* positive overflow      */
    { 0xD0, 0x90, 0, 0x60, F_V | F_C    },   /* negative overflow      */
    { 0xFF, 0x01, 0, 0x00, F_Z | F_C    },   /* carry out, zero result */
    { 0x00, 0x00, 1, 0x01, 0            },   /* carry in               */
};

/* decimal */
assert(adc_decimal(0x09, 0x01, 0) == 0x10);
assert(adc_decimal(0x99, 0x01, 0) == 0x00);     /* and carry set */
```

## Stage 6 — validation against the only oracle you have.

- EM7.22 — [[!blocking]] **This is the most important stage in the course.** Everything else in this project is validated against your own reading of a specification, which means **a consistent misunderstanding passes silently**. The CPU core is the one exception: other people have published exhaustive tests.
- EM7.23 — **Two suites, and the first checks the thing Stages 1 to 4 were built to make checkable.** The **SingleStepTests / Tom Harte** processor tests give, per instruction, an initial state, a final state and **the complete bus cycle trace** — so they check the *sequence*, not merely the count. **Klaus Dormann's functional tests** are a binary that exercises 6502 behaviour exhaustively and traps on failure, covering emulation mode. Check the current repository layout before writing a loader; the format has changed over time.

```json
{ "name": "ad 12 34",
  "initial": { "pc": 1234, "s": 511, "a": 0, "x": 0, "y": 0,
               "p": 36, "dbr": 0, "pbr": 0, "d": 0, "e": 0,
               "ram": [[4660, 18], [4661, 52]] },
  "final":   { "...": "..." },
  "cycles":  [[4660, 173, "--r"]] }
```

- EM7.24 — **The harness, and recording the trace is a few lines in the bus layer — another dividend of [EM7.7](sec_ai_em7#em77).**

```c
static int run_case(const testcase_t *t)
{
    system_t s = {0};
    mem_init(&s.mem);

    s.cpu.PC = t->init.pc;   s.cpu.S = t->init.s;
    s.cpu.A  = t->init.a;    s.cpu.X = t->init.x;   s.cpu.Y = t->init.y;
    s.cpu.P  = t->init.p;    s.cpu.E = t->init.e;   s.cpu.D = t->init.d;
    s.cpu.DBR = t->init.dbr; s.cpu.PBR = t->init.pbr;
    for (int i = 0; i < t->init.n_ram; i++)
        mem_raw_write(&s.mem, t->init.ram[i].addr, t->init.ram[i].val);

    trace_begin(&s);                /* record every bus access */
    cpu_step(&s);

    int bad = 0;
    bad += cmp_u16("PC", s.cpu.PC, t->final.pc);
    bad += cmp_u16("A",  s.cpu.A,  t->final.a);
    /* ... every register ... */
    for (int i = 0; i < t->final.n_ram; i++)
        bad += cmp_u8("ram", mem_raw_read(&s.mem, t->final.ram[i].addr),
                             t->final.ram[i].val);

    bad += cmp_int("cycle count", s.trace_n, t->n_cycles);
    for (int i = 0; i < t->n_cycles && i < s.trace_n; i++) {
        bad += cmp_u32("cycle addr", s.trace[i].addr, t->cycles[i].addr);
        bad += cmp_u8 ("cycle data", s.trace[i].val,  t->cycles[i].val);
    }
    mem_free(&s.mem);
    return bad;
}
```

- EM7.25 — **Run one opcode's file, fix, repeat — never all 256 at once.** A wall of failures teaches nothing, while a whole addressing mode failing identically means the bug is in the mode function and one fix repairs twenty opcodes. **Expect the first pass to fail**, with the causes in roughly this order: a missing idle in a read-modify-write or a branch; width handling on immediates; direct page not wrapping in bank zero; page-cross penalties applied in the wrong direction; flags after decimal arithmetic.
- EM7.26 — **Once the CPU may deliberately diverge from a stock part, these suites stop covering the divergence.** **Additive divergence keeps them valid**: WDC reserved `WDM` (`$42`) precisely as an escape prefix with no defined behaviour, so extensions behind it leave every published test passing. **Modifying an existing opcode invalidates them exactly where it lands**, and that should be a recorded decision rather than something noticed later ([EM6.15](sec_ai_em6#em615)).
  NOTE: [[!blocking]] **This document has already ruled the other way, for a different use.** [M.3](sec_ai_m#m3) rejected a `WDM` command channel to Helium in favour of memory-mapped registers, and its note goes further than the rejection: **`WDM` stays a reserved no-op and a softcore must implement it as one, decoding nothing.** An extension prefix decodes the signature byte, so the two positions cannot both stand. The distinction that might save it is that M.3 refused a *hardware* channel while this is a *core* extension, but that is a distinction the softcore of [sheet E](sec_ai_e) would have to honour in silicon (→ [Q115](sec_ai_q#q115)).
- EM7.27 — [[!blocking]] **Outside the CPU there is no oracle at all, and that is grounds for humility about every other green test in the tree.** The MMU, cache and blitter tests encode this document **as the person writing them read it**, so a consistent misreading passes. When the emulator later disagrees with hardware, **the specification is a suspect alongside the code** — which is not a weakness of the method, it is [EM1.3](sec_ai_em1#em13).

## Stage 7 — parameters and instrumentation.

- EM7.28 — **Goal: stop hard-coding numbers you do not know.** The hardware does not exist; you do not know the cache line size, the TLB depth, the SDRAM clock or the blitter throughput. **Compile them in and the emulator can only consume decisions; make them parameters and it can produce them**, which is [EM7.56](sec_ai_em7#em756) and the reason the whole thing is worth building. A `key = value` loader is fifty lines and pays for itself the first time you sweep.

```c
typedef struct {
    double   phi2_mhz;
    uint32_t cache_size, cache_line_bytes, cache_ways;
    uint32_t tlb_entries, tlb_ways;
    double   sram_access_ns, sdram_clock_mhz;
    uint32_t sdram_cas_latency, sdram_trcd, sdram_trp, sdram_burst_len;
} params_t;
```

- EM7.29 — **Predict before you run.** At a 100 MHz SDRAM clock a 32-byte fill is about 340 ns; at 50 MHz the same fill is about 680 ns. **If the number moves the wrong way, the model is wrong, not the parameter.**
  NOTE: **Trap — counters nobody reads.** A counter you never print is a counter that is silently wrong. Write the report early ([EM3.15](sec_ai_em3#em315)).

## Stage 8 — time: cycles are not seconds.

- EM7.30 — **When a cache miss makes the CPU wait, what physically happens?** Under **wait states**, PHI2 free-runs and the CPU is held on `RDY` for whole extra cycles — and on the 65816 `RDY` works for reads and writes, unlike the NMOS 6502. Under **a stretched clock**, PHI2 itself is halted until memory responds; the core is static, so it may be stopped indefinitely ([E.4](sec_ai_e#e4)). **They are not equivalent and the difference is not academic**: under wait states the machine cannot wait for less than a whole cycle, so a 20 ns SRAM access costs a full 125 ns cycle at 8 MHz. **noVa64 stretches** ([EM2.4](sec_ai_em2#em24)).
- EM7.31 — **Model latency in nanoseconds and derive cycles from it, never the other way round.**

```c
static void charge(system_t *s, double stall_ns)
{
    double phi2_ns = 1000.0 / s->p.phi2_mhz;

    if (s->p.stall_model == STALL_STRETCH) {
        s->in.cycles  += 1;
        s->in.wall_ns += phi2_ns + stall_ns;     /* pay the latency exactly */
    } else {
        uint64_t extra = (uint64_t)ceil(stall_ns / phi2_ns);
        s->in.cycles  += 1 + extra;
        s->in.wall_ns += (double)(1 + extra) * phi2_ns;
    }
    s->in.stall_ns += stall_ns;
}
```

  Every bus function calls this instead of incrementing the cycle count directly.
- EM7.32 — **With a stretched clock, counting cycles no longer measures time**, so the machine needs a fixed-frequency counter software can read that does not derive from PHI2. **Latch on the low byte so the counter cannot advance mid-read.**

```c
case IO_CLK0:
    /* Latch on the low byte so the counter cannot advance mid-read. */
    s->clk_latch = (uint32_t)(s->in.wall_ns / 1000.0);
    return (uint8_t)s->clk_latch;
case IO_CLK1: return (uint8_t)(s->clk_latch >> 8);
case IO_CLK2: return (uint8_t)(s->clk_latch >> 16);
case IO_CLK3: return (uint8_t)(s->clk_latch >> 24);
```

  **That is a hardware requirement created by a timing decision** — a good example of the emulator earning its keep before any silicon exists ([EM2.6](sec_ai_em2#em26)).
- EM7.33 — **Three assertions: the counter tracks wall time, the latch holds, and a miss costs time but not cycles.**

```c
/* the counter tracks wall time, not cycles */
uint32_t t0 = read_clock(&s);
for (int i = 0; i < 8000; i++) bus_idle(&s);     /* 1 ms at 8 MHz */
assert(read_clock(&s) - t0 >= 990 && read_clock(&s) - t0 <= 1010);

/* the latch: read the low byte, let time pass, read the rest */
uint32_t lo = io_read8(&s, IO_CLK0);
for (int i = 0; i < 80000; i++) bus_idle(&s);    /* 10 ms of drift */
uint32_t hi = io_read8(&s, IO_CLK1)
            | (io_read8(&s, IO_CLK2) << 8)
            | (io_read8(&s, IO_CLK3) << 16);
assert(read_clock(&s) - (lo | (hi << 8)) > 9900);   /* the old value, intact */

/* under stretching, a miss costs time but not cycles */
cycle_t c0 = s.in.cycles; double w0 = s.in.wall_ns;
(void)bus_read8(&s, 0x400000, ACC_READ);
assert(s.in.cycles - c0 == 1);
assert(s.in.wall_ns - w0 > 1000.0 / s.p.phi2_mhz);
```

  NOTE: **Trap — comparing CPI across stall models.** Under stretching it drops from 3.43 to 3.00 while the machine does the same work in the same time. Compare instruction rate; the cycle has stopped being a unit of work ([EM2.12](sec_ai_em2#em212)).

## Stage 9 — the cache.

- EM7.34 — **Do not model the data.** The backing arrays are authoritative and nothing can observe cache contents except through timing, so storing lines would be a week of work producing nothing any program could detect ([EM1.9](sec_ai_em1#em19)).

```c
double cache_access(cache_t *c, const params_t *p, instr_t *in,
                    pa_t pa, bool is_write)
{
    uint32_t set  = (pa >> c->line_shift) & c->set_mask;
    pa_t     tag  =  pa >> (c->line_shift + ilog2u(c->sets));
    size_t   base = (size_t)set * c->ways;
    c->tick++;

    for (uint32_t w = 0; w < c->ways; w++)
        if (c->valid[base + w] && c->tag[base + w] == tag) {
            in->cache_hit++;
            c->lru[base + w] = c->tick;
            if (is_write) c->dirty[base + w] = 1;
            return 0.0;
        }

    in->cache_miss++;
    uint32_t victim = pick_victim(c, base);
    double stall = sdram_fill_ns(p, c->line_bytes);
    if (p->cache_writeback && c->dirty[base + victim])
        stall += sdram_fill_ns(p, c->line_bytes);

    c->valid[base+victim] = 1;
    c->dirty[base+victim] = is_write;
    c->tag  [base+victim] = tag;
    c->lru  [base+victim] = c->tick;
    return stall;
}

static double sdram_fill_ns(const params_t *p, uint32_t line_bytes)
{
    double sdram_ns = 1000.0 / p->sdram_clock_mhz;
    uint32_t beats  = (line_bytes + 1) / 2;              /* x16 device */
    uint32_t bursts = (beats + p->sdram_burst_len - 1) / p->sdram_burst_len;
    double clocks = bursts * (p->sdram_trcd + p->sdram_cas_latency
                            + p->sdram_burst_len + p->sdram_trp);
    return clocks * sdram_ns;
}
```

- EM7.35 — **Two numbers you can predict by hand, and almost any indexing error in the tag computation breaks one of them.** Stride exactly one line and every access misses; halve the stride and every second access hits.

```c
set_params(&s, .line_bytes = 32, .ways = 2);
for (int i = 0; i < 1024; i++) (void)bus_read8(&s, i * 32, ACC_READ);
assert(s.in.cache_miss == 1024);

for (int i = 0; i < 1024; i++) (void)bus_read8(&s, i * 16, ACC_READ);
assert(s.in.cache_miss == 512);

/* 32-byte line, x16 SDRAM at 100 MHz, CL3, tRCD 3, 8-beat bursts:
 *   2 bursts x (3 + 3 + 8 + 3) = 34 clocks = 340 ns */
assert(fabs(sdram_fill_ns(&p, 32) - 340.0) < 1.0);
```

  NOTE: **Trap — believing an associativity result from a synthetic workload.** A linear stride never conflicts, so it cannot tell direct-mapped from four-way. Ours gave identical numbers for every associativity — **a result that looks like a finding and is an artifact of the test program** ([EM6.6](sec_ai_em6#em66)).

## Stage 10 — the MMU.

- EM7.36 — **2 KB pages, 8192 entries covering a 16 MB space, four bytes per entry: 32 KB of page table per process, resident in SRAM** ([sheet L](sec_ai_l)). Translation lands in the bus layer, which is why its address argument had to be virtual from the first day.

```c
#define PAGE_SHIFT      11u
#define PTE_VALID       0x01u
#define PTE_READ        0x02u
#define PTE_WRITE       0x04u
#define PTE_EXEC        0x08u
#define PTE_COW         0x10u
#define PTE_FRAME_SHIFT 8u

bool mmu_translate(system_t *s, va_t va, access_t kind,
                   pa_t *out_pa, double *stall_ns, abort_cause_t *cause)
{
    mmu_t *u = &s->mmu;
    if (!u->enabled) { *out_pa = va & 0xFFFFFFu; return true; }

    uint32_t vpn = va >> PAGE_SHIFT, off = va & (PAGE_SIZE - 1);
    tlb_ent_t *e = tlb_lookup(u, u->asid, vpn);
    uint32_t pte;

    if (e) pte = e->pte;
    else {
        pte = read32(&s->mem, u->pt_base + (pa_t)vpn * 4u);
        *stall_ns += 2.0 * s->p.sram_access_ns;   /* two halfword reads */
        if (!(pte & PTE_VALID)) { *cause = ABORT_UNMAPPED; return false; }
        tlb_insert(u, u->asid, vpn, pte);
    }

    switch (kind) {
    case ACC_FETCH: if (!(pte & PTE_EXEC))  { *cause = ABORT_PERM; return false; } break;
    case ACC_WRITE: if (pte & PTE_COW)      { *cause = ABORT_COW;  return false; }
                    if (!(pte & PTE_WRITE)) { *cause = ABORT_PERM; return false; } break;
    default:        if (!(pte & PTE_READ))  { *cause = ABORT_PERM; return false; } break;
    }

    *out_pa = ((pa_t)(pte >> PTE_FRAME_SHIFT) << PAGE_SHIFT) | off;
    return true;
}
```

- EM7.37 — **Tag TLB entries with the address space identifier as well as the page number.** Without it a context switch silently lets one process read another's mappings — **a bug that cannot appear until you have two processes and will then be very hard to find.**

```c
pte_set(&s, 0, 0x400, PTE_VALID|PTE_READ|PTE_WRITE|PTE_EXEC);
pte_set(&s, 1, 0x401, PTE_VALID|PTE_READ|PTE_EXEC);          /* read-only */
pte_set(&s, 2, 0x402, PTE_VALID|PTE_READ|PTE_WRITE|PTE_COW); /* COW       */
/* VPN 3 left invalid */

assert(try_write(&s, 0x000010, 0xC3) == ABORT_NONE);
assert(mem_raw_read(&s.mem, 0x200010) == 0xC3);       /* translated */
assert(try_write(&s, 0x000810, 0x11) == ABORT_PERM);
assert(try_write(&s, 0x001010, 0x22) == ABORT_COW);   /* COW outranks WRITE */
assert(try_read (&s, 0x001810)       == ABORT_UNMAPPED);

/* the ASID one: same page, different process, must miss */
uint64_t m0 = s.in.tlb_miss;
s.mmu.asid = 2;
(void)try_read(&s, 0x000010);
assert(s.in.tlb_miss > m0);
```

## Stage 11 — aborts.

- EM7.38 — **An abort does not cut the instruction short.** The instruction runs to its normal length with transfers and register updates suppressed, and **then** the abort vector is taken with the register image the instruction started with ([E.16](sec_ai_e#e16)). The obvious implementation — `setjmp`/`longjmp` from the bus back to the dispatch loop — **is wrong twice**: it does not port to WebAssembly without the Exception Handling proposal, and it charges the aborted instruction however many cycles it had reached rather than its true length. A flag is portable, simpler, and closer to the hardware.

```c
void cpu_step(system_t *s)
{
    s->cpu_at_insn_start = s->cpu;      /* save the register image */
    s->abort_pending = false;

    uint8_t op = fetch8(s);
    switch (op) { /* ... */ }

    if (s->abort_pending) {
        s->cpu = s->cpu_at_insn_start;  /* restore */
        s->abort_pending = false;
        take_vector(s, s->cpu.E ? VEC_E_ABORT : VEC_N_ABORT);
    }
}

uint8_t bus_read8(system_t *s, va_t va, access_t kind)
{
    if (s->abort_pending) { charge(s, 0.0); return 0xFF; }
    /* ... */
}
```

- EM7.39 — **The cycle assertion is what distinguishes this model from the unwind**, and it is the one a reviewer should look for.

```c
uint8_t  before = mem_raw_read(&s.mem, target);
cpu_t    regs   = s.cpu;
cycle_t  c0     = s.in.cycles;

run_instruction_that_aborts(&s);

assert(mem_raw_read(&s.mem, target) == before);      /* write suppressed  */
assert(regs_equal(&s.cpu_at_insn_start, &regs));     /* registers restored */
assert(s.in.cycles - c0 == cycles_when_not_aborted); /* full length        */
assert(s.cpu.PC == abort_vector_target);
```

  NOTE: **Trap — suppressing the memory write but not the device access.** An aborted instruction that still pokes an I/O register has side effects the hardware would not have. [E.17](sec_ai_e#e17) makes the same point from the other end, where a write that has already driven the bus cannot be undone at all.

## Stage 12 — I/O, console and input.

- EM7.40 — **Adopt the rule early: the guest-side interface must be hardware-real, and only the host transport may be emulator-specific** ([EM3.1](sec_ai_em3#em31)). A console is a character device the hardware will have; whether the host end is a terminal, a pty or a browser pane is your business. **Inventing a guest device the hardware will not have produces software that runs under emulation and fails on silicon.**

```c
uint8_t io_read8(system_t *s, uint32_t off)
{
    switch (off) {
    case IO_CON_DATA:   return console_rx_take(&s->con);
    case IO_CON_STATUS: return 0x01u | (console_rx_valid(&s->con) ? 0x02u : 0);
    case IO_MBX_STATUS: return mailbox_peek(&s->mbx, NULL) ? 1 : 0;
    default:            return 0xFF;
    }
}

void mailbox_key(mailbox_t *m, instr_t *in, uint8_t scancode, bool down)
{
    mbx_event_t e = { MBX_EV_KEY, scancode, down ? 1 : 0, 0, 0 };
    mailbox_push(m, in, &e);
}
```

- EM7.41 — **Input arrives through the mailbox, because on this machine the EC originates it** and the emulator occupies the EC's position without emulating it ([EM3.7](sec_ai_em3#em37)).

```c
io_write8(&s, IO_CON_DATA, 'A');
assert(s.in.console_tx == 1);

/* an unmapped I/O offset must be discarded, not fall through to memory */
io_write8(&s, 0x00EE, 0x99);
assert(mem_raw_read(&s.mem, PA_IO_BASE + 0x00EE) != 0x99);

mailbox_key(&s.mbx, &s.in, 5, true);
assert(io_read8(&s, IO_MBX_STATUS) == 1);
assert(io_read8(&s, IO_MBX_D0) == 5);
io_write8(&s, IO_MBX_POP, 0);
assert(io_read8(&s, IO_MBX_STATUS) == 0);
```

  NOTE: **Trap — adding convenience registers.** Every one is something the guest will be written against and the hardware will have to provide. Ours has exactly one host-only register and its name says so — and it still put the whole map in the wrong bank ([EM4.10](sec_ai_em4#em410)).

## Stage 13 — determinism.

- EM7.42 — **Do this now. It is cheap now and expensive later.** Input is the only source of non-determinism in an interactive session, so log it as `(cycle, event)` pairs at the mailbox boundary and every run becomes reproducible. **By the time you are debugging a preemptive scheduler, retrofitting this means touching everything.**

```c
bool trace_next_input(cycle_t now, mbx_event_t *out);   /* replay */
void trace_input(cycle_t at, const mbx_event_t *e);     /* record */

static void pump_input(system_t *s)
{
    mbx_event_t e;
    while (trace_next_input(s->in.cycles, &e))
        mailbox_push(&s->mbx, &s->in, &e);
}
```

- EM7.43 — **The strongest cheap test in the course.** Any accidental dependence on host state breaks it immediately — an uninitialised variable, a pointer value, wall-clock time, iteration order over a hash map. **It is the test most likely to catch a bug you did not know you had.**

```c
run_with_trace("session.trace", &r1);
run_with_trace("session.trace", &r2);

assert(r1.cycles == r2.cycles);
assert(hash(r1.sdram, PA_SDRAM_SIZE) == hash(r2.sdram, PA_SDRAM_SIZE));
assert(hash(r1.sram,  PA_SRAM_SIZE)  == hash(r2.sram,  PA_SRAM_SIZE));
assert(memcmp(&r1.cpu, &r2.cpu, sizeof(cpu_t)) == 0);
```

## Stage 14 — the blitter.

- EM7.44 — **An Amiga-style blitter computes a boolean function of up to three source channels, chosen by a byte that *is* the truth table.** Index it with the three input bits. `$CC` is `D = B`, a plain copy; every other value gives a different function, including masked, inverted and combined forms. **Once you understand this, the blitter stops being mysterious.**

```c
uint32_t idx = (uint32_t)((A << 2) | (B << 1) | C);
int D = (minterm >> idx) & 1;

static void do_blit(neon_t *n, uint8_t minterm, uint32_t dx, uint32_t dy,
                    uint32_t w, uint32_t h, uint32_t src_words)
{
    uint32_t base = NEON_SRC_BASE + src_words * 2u;
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++) {
            int A = 1;                          /* mask channel tied high */
            int B = src_get(n, base, w, i, j);
            int C = fb_get(n, dx + i, dy + j);
            int D = (minterm >> ((A<<2)|(B<<1)|C)) & 1;
            fb_put(n, dx + i, dy + j, D);
        }
}
```

- EM7.45 — **The input space is eight combinations, so all 256 functions can be tested completely. Very few parts of an emulator admit that — take it.**

```c
for (int mt = 0; mt < 256; mt++)
  for (int a = 0; a < 2; a++)
    for (int b = 0; b < 2; b++)
      for (int c = 0; c < 2; c++)
        assert(blit_pixel(mt, a, b, c)
            == ((mt >> ((a<<2)|(b<<1)|c)) & 1));
```

  NOTE: **Trap — declaring the blitter finished here.** The word-granular shifter, the edge masks, descending mode and the A mask channel are all still missing, and they are what make blits correct for unaligned and overlapping regions. **Say so in the code, or someone will trust it** ([EM4.7](sec_ai_em4#em47)).

## Stage 15 — text mode.

- EM7.46 — **Drawing text by blitting one glyph per character costs a command per character — 52 cycles each, so 62 ms to repaint a full screen.** Instead, let the display hold the character buffer: the CPU stores characters, the display renders and scrolls, and **scrolling becomes a ring origin rather than a memory move.**

```c
static uint8_t *cell(neon_t *n, uint32_t x, uint32_t y)
{
    uint32_t ry = (n->top_row + y) % n->rows;
    return &n->chars[(size_t)ry * n->cols + x];
}

static void text_scroll(system_t *s)
{
    neon_t *n = &s->neon;
    n->top_row = (n->top_row + 1u) % n->rows;
    uint32_t ry = (n->top_row + n->rows - 1u) % n->rows;
    memset(&n->chars[(size_t)ry * n->cols], ' ', n->cols);  /* clear the new row */
}
```

- EM7.47 — **Two traps, and both of them bit us.** **Requiring a present**: if rendering happens only when the guest issues a present command, a console program that never presents shows nothing. Real text-mode hardware scans out continuously, so render on demand whenever the framebuffer is read. **And forgetting the font**: a text mode with an empty glyph store renders a blank screen while every counter looks perfect. **That second one is not a bug to patch around — it is a hardware question the emulator surfaced.** Every text-mode machine ever built initialises its glyph store from ROM, because the console must work before any software runs, which is [T1.53](sec_ai_t1#t153) arrived at from the software side.

```c
void neon_scanout(neon_t *n)
{
    if (!n->text_enable || !n->dirty) return;
    render_text(n);
    n->dirty = false;
}
```

```c
for (int i = 0; i < ROWS + 1; i++) write_line(&s, i);

assert(s.neon.top_row == 1);
assert(*cell(&s.neon, 0, 0) == first_char_of_line(1));   /* line 0 scrolled off */

cycle_t c0 = s.in.cycles;
neon_text_write(&s, '\n');                               /* forces a scroll */
assert(s.in.cycles - c0 == 0);                           /* the CPU paid nothing */
```

## Stage 16 — WebAssembly.

- EM7.48 — **Treat the port as a test.** Ours found the abort problem of [EM7.38](sec_ai_em7#em738), and fixing it improved the model. **A second build of one source is a second opinion.** What has to change is small: guard the POSIX-only code, replace `main` with exported entry points, and index the statistics so the JavaScript needs no knowledge of your struct layout.

```c
#define EXPORT __attribute__((visibility("default"), used))

EXPORT int      nova_init(void);
EXPORT int      nova_load(const uint8_t *data, uint32_t len, uint32_t addr);
EXPORT void     nova_reset(void);
EXPORT uint32_t nova_run(uint32_t cycles);
EXPORT uint8_t *nova_render(void);
EXPORT void     nova_key(uint32_t scancode, int down);
EXPORT double   nova_stat(uint32_t which);
```

- EM7.49 — **Make the run loop sliceable, because a blocking `while` freezes the tab.**

```c
uint64_t sys_run_slice(system_t *s, uint64_t n)
{
    uint64_t until = s->in.cycles + n;
    while (!s->halted && s->in.cycles < until) { pump_input(s); cpu_step(s); }
    return s->in.cycles - (until - n);
}
```

- EM7.50 — **The build line, and every clause in it was learned the hard way** ([EM5.1](sec_ai_em5#em51) to [EM5.4](sec_ai_em5#em54)).

```sh
clang --target=wasm32-wasi --sysroot=$WASI_SYSROOT \
  -DNOVA_NO_PTY -std=c11 -O2 -Iinclude \
  -nostartfiles -Wl,--no-entry -Wl,--export=__wasm_call_ctors \
  -Wl,--export-dynamic -Wl,--initial-memory=134217728 \
  src/*.c -o web/nova64.wasm -lm
```

  **`-nostartfiles`** — the module is a library with no `main`, so linking an object whose only purpose is to call `main` is wrong; it also cut the module from 346 KB to 97 KB and removed the object older linkers reject. **Export the initialiser by name rather than selecting the reactor model** — the latter is a target option, so a clang built without that target reports it as an unknown *argument*, a message pointing at the version rather than the cause. **Probe by compiling** a trivial file for `wasm32`, never with the print-targets option, which exists only from clang 13 and so fails on exactly the compilers worth catching. **And match the sysroot release to your LLVM**, because the linker reads the sysroot's objects.
- EM7.51 — **Test differentially, against yourself: run the same boot image natively and in wasm, then assert identical cycle counts, identical text output and identical framebuffer content.** Ours agree exactly. **Two independent builds of one source agreeing to the bit is worth more than either looking correct alone** ([EM5.15](sec_ai_em5#em515)).
  NOTE: **Trap — benchmarking through JIT warm-up**, or with a 70 MB allocation inside the timing loop. Our first measurement said 2.6 Mcycle/s; the honest steady-state figure is 23.2. **If a benchmark surprises you, suspect the harness first.**

## Stage 17 — the browser shell.

- EM7.52 — **The browser supplies three things a native build cannot**: a live pixel window, pointer lock — **which is the capture requirement, not a cosmetic** — and physical key identity through the event's key *code* rather than the character it produces ([EM5.9](sec_ai_em5#em59) to [EM5.11](sec_ai_em5#em511)).

```js
const SLICE = Math.round(8e6 / 60);

function frame() {
  if (!running) return;
  X.nova_run(SLICE);
  drainTee();
  paint();
  if (X.nova_halted()) { running = false; return; }
  requestAnimationFrame(frame);
}

function paint() {
  const w = X.nova_fb_width(), h = X.nova_fb_height();
  if (!imgData) imgData = ctx.createImageData(w, h);
  imgData.data.set(new Uint8Array(mem.buffer, X.nova_render(), w * h * 4));
  ctx.putImageData(imgData, 0, 0);
}
```

- EM7.53 — **Test it headlessly, and make the assertions read the machine's own counters rather than "nothing threw".**

```js
const hid = () => X.nova_stat(HID_EVENTS);

const h0 = hid();
dispatch(new KeyboardEvent("keydown", { code: "KeyA" }));
dispatch(new KeyboardEvent("keyup",   { code: "KeyA" }));
assert(hid() === h0 + 2);                    /* a mapped key: two events */

const h1 = hid();
dispatch(new KeyboardEvent("keydown", { code: "F13" }));
assert(hid() === h1);                        /* unmapped: nothing */

releasePointerLock();
const h2 = hid();
dispatch(new MouseEvent("mousemove", { movementX: 5 }));
assert(hid() === h2);                        /* dropped when not captured */
```

  NOTE: **Trap — believing headless coverage is browser coverage.** The stubs are precisely the risky parts: the canvas is recorded rather than rendered, pointer lock is synchronous here and asynchronous in reality, key events arrive without modifiers or repeat, CSS is parsed and not applied. **Wiring gets verified; presentation does not** ([EM5.17](sec_ai_em5#em517)).

## Stage 18 — boot images with a real assembler.

- EM7.54 — **Stop hand-assembling bytes.** `ca65` and `ld65` from cc65 handle the 65816, and a linker configuration places the code and the vector block ([sheet O](sec_ai_o)).

```
MEMORY {
    ROM:     start = $00E000, size = $1FE4, fill = yes, file = %O;
    VECTORS: start = $00FFE4, size = $001C, fill = yes, file = %O;
}
SEGMENTS {
    CODE:    load = ROM,     type = ro, start = $00E000;
    RODATA:  load = ROM,     type = ro;
    VECTORS: load = VECTORS, type = ro;
}
```

```asm
        .p816
        .segment "CODE"
reset:  sei
        clc
        xce                     ; native mode
        .a16
        .i16
        rep #$30                ; 16-bit A and index
        ldx #$01FF
        txs
```

- EM7.55 — **Three traps, and the middle one is the best argument in the course for having built the thing.** **Width directives** must match what `REP` and `SEP` did at run time, or the image assembles cleanly and executes garbage. **A branch that skips a mode switch**: our first template had a duplicate label that made a branch jump past a `REP #$20`, so the next block decoded with an 8-bit accumulator, `A9 04 00` became a load followed by `BRK`, and the machine restarted in a loop. **On hardware that is an afternoon with a logic analyser; under an emulator with instruction counters it is minutes.** And **`STZ` has no long addressing mode** — the assembler catches that one.
  TEST: the image is exactly 8192 bytes, the bytes at offset `$1FFC` hold the reset vector, and running it shows the banner.

## Stage 19 — using it as an instrument.

- EM7.56 — **The point of all of it. The emulator exists to answer questions the hardware cannot yet answer**, and because every timing value is a parameter you can sweep.

```sh
for line in 16 32 64 128; do
  sed "s/^cache.line_bytes.*/cache.line_bytes = $line/" default.params > tmp
  ./nova64 -c tmp -b workload.bin | grep "stalled on fill"
done
```

- EM7.57 — **What ours produced.**

| Question | Answer |
|---|---|
| What does a cache miss cost at 8 MHz? | about 340 ns, three cycles |
| What does a page walk cost? | 20 ns stretched, a full cycle with wait states |
| What does a Neon command cost to emit? | 52 cycles, and 188 if you write the obvious loop |
| What does hardware text mode save? | 17 cycles per character, and a free scroll |
| What would a 50 MHz softcore give? | 3.97×, not 6.25× |

- EM7.58 — **Sanity-check every measurement by hand, and the check is arithmetic you can do on paper.** Two bursts of `3 + 3 + 8 + 3` SDRAM clocks is 34 clocks is 340 ns, and 340 over 125 is 2.7, so three PHI2 cycles. **The measurement agreed. Had it not, one of the two was wrong and both were worth checking.**
- EM7.59 — **Three ways to fool yourself, all of which we managed.** **An unrepresentative workload** — the associativity sweep gave a clean, consistent, worthless answer. **Numbers without their assumptions** — every figure here rests on invented command encodings and unfixed SDRAM parameters, and a number quoted without that context will outlive the context. **And forgetting the operating point** — "the memory hierarchy barely matters" is true at 8 MHz and false at 50 ([EM6.5](sec_ai_em6#em65)).

## What the whole exercise teaches.

- EM7.60 — **Model what can be observed, and nothing else.** Every subsystem where the coarse model was chosen saved days and cost nothing, which is [EM1.7](sec_ai_em1#em17) confirmed in retrospect rather than asserted in advance.
- EM7.61 — **One structural decision carries the rest.** Putting the cycle counter in the bus layer decided where the MMU, the cache, the stalls and the aborts would live, and none of it needed rethinking. **There is usually exactly one such decision in a project, and it is usually made on the first day, by accident.**
- EM7.62 — **Build the second implementation to test the first.** Porting to wasm found a design flaw; writing the boot template found two more; native and wasm agreeing to the bit is a stronger statement than either passing its own tests. **This is [EM1.3](sec_ai_em1#em13) one level down**, and it worked at both levels.
- EM7.63 — **Prefer tests that pin a number.** "It renders" is not a test. "1024 accesses produce exactly 1024 misses at 340 ns each" is.
- EM7.64 — **Know which oracle you have.** Exactly one part of this project can be checked against something you did not write: the 65816 test suites. Everywhere else a consistent misunderstanding passes silently.
- EM7.65 — **The gaps you hit are findings.** The missing font, the command port width, the absent character device, the need for a fixed-frequency counter — none was an emulator bug. Each was a hardware decision nobody had taken, and **finding one in software costs a morning where finding it in silicon costs a respin** ([EM6.16](sec_ai_em6#em616)).
