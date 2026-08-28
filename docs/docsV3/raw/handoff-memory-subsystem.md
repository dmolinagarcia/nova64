# DANI-65816 — Handoff: Memory Subsystem Revision

**Session scope:** elimination of PSRAM, SDRAM sizing, SRAM bus sharing, and page-miss
handling mechanism.
**Status:** three decisions settled, one structural consequence identified that changes the
role of the SRAM.

---

## 1. Summary of decisions

| # | Decision | Supersedes |
|---|---|---|
| D1 | PSRAM eliminated from the entire board, including FPGA-B's framebuffer | APS6404L on FPGA-B |
| D2 | 64 MB SDRAM on FPGA-A and on FPGA-B, footprint accepts 32 MB fallback | 32 MB W9825G6KH as the only option |
| D3 | SRAM narrowed to ×8; D0–D7 **and** A0–A10 shared with the CPU bus | IS61WV102416 (1M×16, 41 pins) |
| D4 | Page miss handled by **stalling PHI2**, not by ABORT | ABORT for all fault classes |
| D5 | SRAM is a **hardware cache with tags**, not a frame migration tier | SRAM as an addressable fast frame pool |

D5 is a consequence of D4 and is the most important change in this session. See §5.

---

## 2. SDRAM sizing (D1, D2)

### Ceilings evaluated

- **PTE format:** `FRAME[15:0]` with 2 KB pages → 65,536 frames → **128 MB** hard ceiling.
  Unchanged; leave as is for headroom.
- **Silicon available:** SDR SDRAM tops out at 512 Mbit. No 1 Gbit SDR part exists in
  volume. 512 Mbit ×16 = **64 MB per device**.
- **Pin budget:** the binding constraint. See §3.

### Selected parts

| Role | Part | Organization | Package |
|---|---|---|---|
| Primary | `AS4C32M16SB-7TIN` (Alliance) | 32M×16, 512 Mbit | TSOP-II 54 |
| Alternate | `IS42S16320F-7TLI` (ISSI) | 32M×16, 512 Mbit | TSOP-II 54 |
| Alternate | `IME5116SDBETG-75IA` (Intelligent Memory) | 32M×16, 512 Mbit | TSOP-II 54 |
| Fallback | `W9825G6KH` (Winbond) | 4M×16, 256 Mbit | TSOP-II 54 |

The 512 Mbit and 256 Mbit parts share the same 54-pin footprint and the same 13 address
lines. The only difference is one additional column-address bit (10 vs 9), which is a
parameter change in the controller. **Populating either device requires no board change.**

### Placement

- **FPGA-A:** one device, 64 MB, main memory backing store.
- **FPGA-B:** one identical device, 64 MB, video framebuffer and audio buffers.

FPGA-B keeps local memory rather than streaming pixels from FPGA-A. Feeding video across
the inter-FPGA link would require ~26 MB/s sustained with hard deadlines and would make
FPGA-A the bottleneck for the whole system. A second SDRAM device costs the same pin count
as any smaller memory, reuses the same symbol, footprint and gateware controller, and gives
enough bandwidth for double buffering and 16 bpp.

**Do not populate a second device on FPGA-A to reach 128 MB.** The 65816 at 14 MHz writes
roughly 7 MB/s; zeroing 64 MB already takes ~9 s. Route a second `/CS` to an unpopulated
footprint instead.

---

## 3. SRAM bus sharing (D3)

### Key insight

With 2 KB pages, **A0–A10 are not translated**. The physical address is
`FRAME[n:0] : offset[10:0]`, and the offset comes straight from the CPU. Therefore both the
data bus *and* the low address bus can be shared nets.

### Wiring

```
SRAM D0–D7   → CPU data bus net          0 pins
SRAM A0–A10  → CPU address bus net       0 pins   (page offset, untranslated)
SRAM A11–A19 → FPGA-A                    9 pins   (frame number; A20 if 2 MB)
/CE, /OE, /WE → FPGA-A                   3 pins
                                        --------
                                        12–13 pins for 1–2 MB
```

The SRAM address bus *is* the physical address bus, split at the page boundary. The FPGA
only drives the translated portion.

### FPGA-A pin budget (iCE40HX8K-TQ144, 107 I/O)

| Block | Pins |
|---|---|
| 65816 bus (A0–15, D0–7 bidirectional, PHI2, RWB, RDY, RESB, IRQB, NMIB, ABORTB, VPA, VDA, VPB, E, MX, BE) | 38 |
| SDRAM 64 MB | 39 |
| SRAM 1 MB | 13 |
| **Subtotal** | **90** |
| **Free** | **17** |

Free-pin allocation: inter-FPGA link 8, I2C (GT911 + fuel gauge) 2, RP2040 handshake 3,
spare 4.

For comparison: the ×16 SRAM variant totalled 118 pins (impossible), and sharing only the
data bus totalled 101 (6 free — not enough for the inter-FPGA link).

### Timing pipeline

This is what makes the sharing work, and it also improves ABORTB margin:

- **PHI2 low:** CPU drives A0–A15 and the bank byte on D0–D7. FPGA latches the bank, forms
  the 24-bit virtual address, performs the TLB lookup (~10 ns in EBR), and prepares A11–A19
  and `/CE`.
- **PHI2 high:** `/CE` asserts. The SRAM has the full high phase (~35 ns at 14 MHz) to
  deliver data directly to the CPU, with no FPGA in the data path.

Two consequences:

1. **ABORTB has half a cycle of margin.** Translation and permission checking complete
   before the rising edge, not in competition with the memory access.
2. **SDRAM leaves the critical timing path entirely.** Its controller becomes a DMA/paging
   engine rather than a low-latency backing store. Significant de-risking for bring-up.

### Selected parts

| Size | Part | Notes |
|---|---|---|
| 1 MB | `AS7C38096A-10TIN` (Alliance) | 1024K×8, 3.3 V, 10 ns, TSOP-II 44, industrial. ~€3. **Populate this first.** |
| 2 MB | `AS7C316096` (Alliance) | 2M×8, 3.3 V, 10 ns, TSOP-II 44 |
| 2 MB | `IS61WV20488BLL-10TLI` (ISSI) | 2M×8, 10 ns, TSOP-II 44. ~$17, MOQ 135 |

**10 ns is mandatory, no exceptions.** `/OE` must be deasserted during PHI2 low because the
bus carries the bank byte then, so the usable window is half a phase. 55 ns parts
(AS6C8008 and similar) are excluded.

### Costs accepted

- **The FPGA can no longer access the SRAM independently.** Page-table walks, SDRAM→SRAM
  fills, and RP2040 BIOS preload all require taking the bus: RDY low to freeze the CPU, BE
  low to tristate its buses, then the FPGA drives A0–A10 and D0–D7 through the pins it
  already has. CPU bus pins become bidirectional (no pin cost, real gateware cost).
- **Page-table walks cost 4 SRAM accesses instead of 2** (32-bit PTE at 8-bit width).
  ~100 ns per walk, only on TLB miss. Irrelevant with a 64-entry ASID-tagged TLB.
- **Capacitive loading.** D0–D7 and A0–A10 now carry three loads (CPU, FPGA, SRAM). This is
  the most layout-critical net on the board: short traces, no long stubs to the SRAM.

---

## 4. PHI2 stalling for page misses (D4)

### Viability confirmed

The W65C816S has a fully static core. The WDC datasheet states that PHI2 may be held in
either state to preserve internal register contents as a standby mode. There is no minimum
clock frequency. The clock can be stopped indefinitely.

### Mechanism

Stop the clock **low**. This matches the pipeline in §3: the miss is detected during PHI2
low, before the CPU enters its data phase.

Because the CPU keeps driving the bank byte on the shared D0–D7 while stopped, `BE=0` is
required. BE is asynchronous and does not depend on the clock, so it works with PHI2 frozen.

```
PHI2 falling edge
  → CPU drives A0–A15 + bank byte
  → TLB lookup → MISS
  → do NOT raise PHI2
  → BE = 0                  (CPU releases A, D, RWB)
  → FPGA performs the fill  (owns A0–A20 and D0–D7)
  → BE = 1
  → raise PHI2              (SRAM now holds the correct data)
```

The CPU observes one very long but otherwise normal cycle. No software involvement.

### Fault taxonomy — bounded vs unbounded

This distinction must be enforced in the gateware. Stalling PHI2 on a fault that needs the
kernel deadlocks the machine immediately, because the kernel is the only entity that could
resolve it and it cannot run.

| Event | Mechanism |
|---|---|
| Page resident in SDRAM, not in SRAM | **Stall PHI2**, hardware fill, µs range |
| Page not present (zero-fill, swap from SD) | ABORT → kernel |
| Permission violation (W on R/O, U on kernel, X on data) | ABORT → kernel |
| Copy-on-write | ABORT → kernel |

**ABORTB is retained.** It now serves only genuine memory protection, which is what it is
for. Removing the frequent case from the ABORT path substantially reduces risk: the
65816's ABORT has a deserved reputation for being delicate around instruction restart, and
it will now only be exercised on real faults where the process may be terminated anyway.

---

## 5. Structural consequence: SRAM becomes a cache (D5)

### The problem

If a fill *migrates* a page from an SDRAM frame to an SRAM frame, the FPGA must rewrite the
PTE with the new frame number. That breaks two things:

- **`mshare`.** A shared page has multiple PTEs in different process page tables pointing at
  the same frame. Hardware would have to locate and update all of them. Not feasible in
  gateware.
- **TLB coherence.** Other cached entries holding the old frame number become stale.

### The resolution

**Never migrate.** The SRAM stops being a tier of allocatable frames and becomes a hardware
cache over the SDRAM, with tags in EBR, invisible to the page tables. The PTE always points
at the SDRAM frame and never changes.

### Proposed 1 MB partition

- **256 KB fixed, uncached:** kernel, active page tables, bank $00 (stack + direct page),
  interrupt vectors. Directly addressed, never evictable. This is where the `PIN` PTE bit
  applies.
- **768 KB cache:** 384 lines of 2 KB, **4-way set associative**. Tags ≈ 384 × 31 bits ≈
  1.5 KB of EBR, alongside the TLB.

Direct mapping is not viable: 64 MB / 2 KB = 32,768 physical lines competing for 384
positions, ~85:1. Four ways are not optional.

### Fill granularity

A full 2 KB fill is expensive over the 8-bit shared bus: 2048 writes at ~2 FPGA clocks each
at 100 MHz ≈ 41 µs, plus ~11 µs of SDRAM read, plus as much again if a dirty victim must be
written back. That is 50–100 µs with interrupts completely blocked.

**Use 256 B sub-blocks** with per-sub-block valid and dirty bits, one tag per 2 KB line.
This restores the 256 B line size already derived from tCEM constraints. Fill drops to
~6 µs, interrupt latency stays healthy, and the ~1.8 KB of the page that may never be
touched is not transferred. Cost: 8 valid + 8 dirty bits per line, already counted above.

---

## 6. Loose ends to carry forward

- **Time base.** If PHI2 stops, cycle counts are no longer real time. The scheduler timer
  must be a free-running counter in FPGA-A on its own clock, never derived from CPU cycles.
  Any cycle-counting delay loop in the kernel becomes invalid.
- **SDRAM refresh** must continue during a fill. Interleave auto-refresh into the fill state
  machine.
- **Watchdog.** A hung fill freezes the machine with no diagnostic path — no clock, no
  ABORT, nothing. Add a timer in FPGA-A that resumes PHI2 and raises NMI or ABORT if a fill
  exceeds N µs.
- **Clock gating** must be glitch-free (synchronous enable), and the first pulse on resume
  must meet the minimum PHI2 high width.
- **RDY is bidirectional.** The CPU pulls it low on WAI. Open-drain with pull-up; do not
  drive it high hard.
- **Power.** Stopping the clock is literally the low-power mode. Favourable for a portable.

## 7. Items to verify

1. **SRAM footprint compatibility.** Confirm in the datasheets that A20 on the
   `AS7C316096` (2M×8) falls on an NC pin of the `AS7C38096A` (1M×8) in TSOP-II 44. Usual
   for Alliance, but not yet checked.
2. **SDRAM column count.** Confirm the 512 Mbit part uses 10 column bits vs 9 on the
   256 Mbit part with otherwise identical pinout.
3. **FPGA-B pin budget** with its own SDRAM. Preliminary estimate was 100 of 107
   (18 bpp panel 22, VGA R-2R 14, I2S 4, SDRAM 39, link 16, config 4, backlight PWM 1).
   Needs a proper recount. A 24 bpp panel would push it to ~106 — 18 bpp recommended.
4. **ER-TFT101-1 datasheet** — still the blocking item for the KiCad schematic (backlight
   current/voltage, capacitive touch variant confirmation).

## 8. Revised FPGA-A memory map summary

```
FPGA-A (iCE40HX8K-TQ144, 107 I/O)
├─ 65816 bus ..................... 38 pins   A0-15, D0-7 bidirectional
├─ SDRAM 64 MB ................... 39 pins   AS4C32M16SB-7TIN
│    └─ backing store, DMA/paging engine, off the critical timing path
├─ SRAM 1 MB ..................... 13 pins   AS7C38096A-10TIN, 10 ns
│    ├─ D0-D7   → CPU net          (0)
│    ├─ A0-A10  → CPU net          (0)      page offset, untranslated
│    ├─ A11-A19 → FPGA             (9)      frame number  [A20 if 2 MB]
│    ├─ /CE /OE /WE                (3)
│    ├─ 256 KB pinned: kernel, page tables, bank $00, vectors
│    └─ 768 KB cache: 384 × 2 KB lines, 4-way, 256 B sub-blocks
└─ Free .......................... 17 pins
```
