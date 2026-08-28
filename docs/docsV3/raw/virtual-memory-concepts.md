# Virtual Memory Concepts — DANI-65816

Conceptual reference document. Explains how a paged virtual memory system works
at a low level and fixes the vocabulary used throughout the rest of the project
documentation.

REV A

---

## 1. The problem it solves

Without virtual memory, every program sees real physical addresses. That creates
three problems:

1. **Relocation** — the program has to know what address it will be loaded at, or
   always be loaded at the same one.
2. **Protection** — any program can read or write another process's memory, or
   the kernel's.
3. **Fragmentation** — free memory breaks into holes and nothing fits any more
   even when the total free space would be enough.

Virtual memory solves all three by introducing **a translation layer between the
address the CPU generates and the address that reaches the memory chips**. The
process lives in a fiction: it believes it has the full 16 MB to itself, starting
at zero.

---

## 2. The core idea: paging

The address space is cut into fixed-size chunks. The chunks on the virtual side
are called **pages**; those on the physical side, **frames**. Both are the same
size — in DANI-65816, **2 KB**.

Translation amounts to answering a single question:

> which physical frame holds virtual page N?

The displacement within the page is never translated: the low bits pass through
unchanged from the virtual address to the physical one. This is why the virtual
address is read as two fields, which have standard names: the high field is the
**VPN** (Virtual Page Number) and the low one the **offset**.

```
Virtual address — 24 bits (W65C816S limit)

   23                       11 10                    0
  +---------------------------+-----------------------+
  |        VPN (13 bits)      |    offset (11 bits)   |
  +---------------------------+-----------------------+
      which page                   which byte inside
```

With 13 bits of VPN there are **8,192 pages per process**. Each one needs an entry
stating which frame it maps to and with what permissions: the **page table**.

Each entry in that table is called a **PTE** (Page Table Entry). It is a word —
32 bits in this design — holding the number of the frame where the page lives
plus a handful of status and permission bits. The table is nothing more than an
array of PTEs indexed by VPN.

```
8,192 entries x 4 bytes = 16 KB of table per process
```

---

## 3. The path of a translation

Translation is performed by the **MMU** (Memory Management Unit): the hardware
block that receives the virtual address and delivers the physical one. In many
architectures it is integrated into the CPU; in DANI-65816 no such block exists
inside the 65816, so FPGA-A implements it.

If every memory access required reading the PTE from the table first, every
access would cost twice as much. The universal solution is to cache recent
translations inside the MMU itself, in a small, very fast associative memory
called the **TLB** (Translation Lookaside Buffer). In DANI-65816 this is 32-64
entries held in **EBR** (Embedded Block RAM), the iCE40's internal RAM blocks.

```
              Virtual address
              VPN 13 b + offset 11 b
                      |
                      v
                 +---------+
                 |   TLB   |   looks up ASID + VPN
                 +---------+
                   /     \
                 hit      miss
                  |         |
        frame available   the walker reads
          immediately     the PTE in SRAM (~4 cycles)
                  \        /
                   \      /
                      v
              Physical address
              FRAME 16 b + offset 11 b
```

Two new terms appear in the diagram:

- **ASID** (Address Space IDentifier) — a tag marking which process each TLB entry
  belongs to. The lookup compares ASID and VPN together, so entries belonging to
  other processes can never produce a false hit. Without ASID the whole TLB would
  have to be flushed on every process switch.
- **Walker** (page-table walker) — the logic that goes to the table in memory and
  extracts the PTE when the TLB misses. In some architectures it is a kernel
  routine; in DANI-65816 it is hardware inside FPGA-A, which is why the cost is
  only ~4 cycles.

A TLB miss is **not an error**: it merely costs one table walk.

---

## 4. When translation genuinely fails

Besides the frame number, the PTE carries **flags**: individual bits describing
the state of the page and what may be done with it. The ones that matter here are
`P` (present — the page is in physical memory), `W` (write — it may be written),
`X` (execute — it may be executed) and `U` (user — accessible from user code and
not only from the kernel). The full list is in the glossary.

The walker reads the PTE, checks those bits, and can run into two anomalous
situations:

### 4.1 Bit P = 0 — page not present

The page is not in physical memory. Either it has never been touched, or it was
evicted to storage. The MMU aborts the access and notifies the kernel, which:

1. allocates a free frame,
2. brings in the contents if required,
3. writes the PTE,
4. retries the instruction.

This is the **page fault**, the basis of demand paging. In v1 loading is eager, so
this case is deferred.

### 4.2 Incorrect permissions

A write to a page with `W=0`, execution of a page with `X=0`, a user access to a
page with `U=0`. There is no retry here: it is a program error and the kernel
terminates it.

### 4.3 Mechanism on the 65816

Both cases use the **ABORTB** pin. FPGA-A detects the problem *during* the cycle
and aborts the instruction before it modifies any state, leaving the CPU in a
position to retry cleanly. The **VPA** pin distinguishes an instruction-fetch
cycle from a data cycle, and therefore allows the `X` flag to be checked.

Fault information is exposed to the kernel through FPGA-A registers:
`FAULT_ADDR`, `FAULT_CAUSE`, `CONTEXT`, `MODE`.

---

## 5. Why FRAME is 16 bits against VPN's 13

The two fields measure different things:

| Field | Measures | Set by |
|---|---|---|
| VPN | how much memory **one process** sees | the CPU |
| FRAME | how much memory **the machine** has | the MMU design |

There is no reason for them to match, and in DANI-65816 they deliberately do not.

### 5.1 The virtual side is capped by the CPU

The 65816 emits 24-bit addresses and that is that: 16 MB. It is a limit of the
silicon, not a design decision.

```
16 MB / 2 KB           = 8,192 pages -> 13 bits
24 bits - 11 of offset = 13 bits of VPN
```

The 13 bits are not selectable: they fall out of a subtraction.

### 5.2 The physical side is not

The physical address is not emitted by the 65816: it is assembled by FPGA-A after
translation. The FPGA drives the memory bus and can address whatever it likes.
The ceiling is set by the width of the FRAME field:

```
16 bits of FRAME x 2 KB = 65,536 frames = 128 MB
```

The machine can therefore hold 128 MB of RAM even though no process ever sees
more than 16 MB. **The MMU breaks the 65816's architectural barrier at system
level without breaking it at process level.**

The 3-bit difference means up to **8 complete virtual address spaces** can be
resident simultaneously in physical memory — exactly what preemptive multitasking
with resident processes requires.

### 5.3 Why 16 and not 15

The planned pool is 48 MB = 24,576 frames, which fits in 15 bits (15 bits reach
64 MB). The sixteenth bit is free:

```
32-bit PTE
+- FRAME[15:0]                    16 bits
+- P W X U D A NC PIN + SW[7:0]   16 bits
```

The flags occupy exactly 8 + 8 = 16 bits, so the other half-word was going
unused. Rounding FRAME up to 16 costs nothing and leaves the ceiling at 128 MB in
case the SDRAM is expanded.

The split into exact halves also simplifies the walker: FRAME is the low
half-word and the flags the high one. The FPGA reads 32 bits from SRAM, takes the
lower 16 for the address bus and the upper ones for the permission logic, in
parallel and with no extra shifting or masking cycles.

### 5.4 The relationship also works in reverse

The sum of all virtual address spaces across all processes can comfortably exceed
the 128 MB of physical memory. That is not a problem: it is the second purpose of
virtual memory. With demand paging, the pages that do not fit live on the SD
card.

---

## 6. Glossary

**Virtual memory** — a layer of indirection between the addresses the CPU
generates and those that reach the RAM.

**Virtual (or logical) address** — the address the CPU emits. What the program
"believes". 24 bits in DANI-65816.

**Physical address** — the address that reaches the SRAM/SDRAM chips. 27 bits in
DANI-65816.

**Page** — a fixed-size block of the virtual space. 2 KB.

**Frame** — a block of the same size in physical memory. To translate is to pair
a page with a frame.

**VPN** (Virtual Page Number) — high bits of the virtual address; identifies the
page. 13 bits.

**Offset** — low bits; the byte within the page. Never translated. 11 bits.

**MMU** (Memory Management Unit) — the hardware that translates. In DANI-65816 it
is not in the CPU: FPGA-A implements it.

**Page table** — an array indexed by VPN holding the PTEs. One per process, 16 KB.

**PTE** (Page Table Entry) — one entry of that table. 32 bits: frame number plus
flags.

**PTE flags** — `P` present in RAM, `W` writable, `X` executable, `U` accessible
from user mode, `D` dirty (modified since it was loaded), `A` accessed (used
recently), `NC` non-cacheable, `PIN` non-evictable, `SW` bits free for the
kernel's own use.

**TLB** (Translation Lookaside Buffer) — cache of VPN-to-frame translations inside
the MMU. Avoids re-reading the table on every access.

**TLB hit / miss** — success or failure in that cache. A miss costs one table
walk; it is not an error.

**Page walk / walker** — the traversal of the page table to resolve a miss. It can
be done by hardware (the DANI-65816 case, ~4 cycles) or by software through an
exception.

**ASID** (Address Space IDentifier) — tag identifying which process each TLB entry
belongs to. Without it the whole TLB would have to be flushed on every context
switch.

**Page fault** — exception raised on access to a page with `P=0`. Recoverable.

**Demand paging** — loading pages only when they are touched, rather than all of
them up front. Deferred to post-v1.

**Swapping** — evicting rarely used pages to storage in order to free frames.

**Pinning** — marking a page as non-evictable. Required for the kernel, the
interrupt vectors and the bank $00 stack.

**Context switch** — switching process; entails pointing the MMU at a different
page table.

**Memory protection** — preventing a process from reading or writing outside its
own space. Achieved through the PTE flags, not through translation itself.

**ABORTB** — W65C816S pin that cancels the instruction in progress without side
effects. The system's fault mechanism.

**VPA / VDA** — pins indicating whether the cycle is an instruction fetch or a
data access. They make checking the `X` flag possible.

**EBR** (Embedded Block RAM) — RAM blocks inside the iCE40. Not a virtual memory
concept: it appears here because its 128 Kbit are the reason only the TLB fits
inside the FPGA while the page table lives in external SRAM.

---

## 7. Summary of figures

| Parameter | Value |
|---|---|
| Page size | 2 KB |
| Virtual address | 24 bits (16 MB per process) |
| VPN | 13 bits (8,192 pages) |
| Offset | 11 bits |
| PTE | 32 bits |
| FRAME field | 16 bits |
| Physical address | 27 bits (128 MB ceiling) |
| Page table | 16 KB per process |
| Planned physical pool | 48 MB = 24,576 frames |
| TLB entries | 32-64, in FPGA-A EBR |
| Page walk cost | ~4 cycles (10 ns SRAM) |
