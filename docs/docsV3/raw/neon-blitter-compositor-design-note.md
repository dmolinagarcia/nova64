# NEON Blitter & Compositor — Design Note

**Project:** noVa64
**Subsystem:** FPGA-B ("NEON") — video and audio
**Document status:** Design proposal, pre-RTL
**Date:** 2026-08-15

---

## 0. Purpose and scope

This note specifies the blitter (block image transfer engine) and the display
compositor that live inside FPGA-B, together with the SDRAM arbitration policy
that both depend on.

It covers:

- The memory baseline and the bandwidth budget derived from it
- The SDRAM bank allocation policy, which is a first-order architectural
  decision rather than an implementation detail
- The blitter datapath, register map and command model
- The scanout pipeline, its real-time guarantee, and the hardware cursor
- The chosen compositor model and its consequences for the kernel

It does **not** cover: the ANX6345 bridge initialisation, the audio subsystem
beyond its claim on memory bandwidth, or the CPU-side window manager.

**Stale-context correction.** Earlier design iterations gave FPGA-B an
APS6404L PSRAM over QSPI. That part is removed from the design entirely.
FPGA-B now has its own dedicated 16-bit SDRAM, identical to the part used by
FPGA-A. Any bandwidth figure derived from the ~66 MB/s QSPI ceiling is
obsolete.

---

## 1. Terminology

Terms are defined here on first use and are not assumed elsewhere in the
document.

**Blit** — a two-dimensional block transfer. A rectangular region of memory is
read, optionally combined with one or two other regions by a logical
operation, and written to a destination rectangle.

**Channel** — one of the blitter's four independent memory ports. Channels A,
B and C are read ports; channel D is the write port.

**Minterm** — one row of the truth table for the logical function combining
channels A, B and C. With three inputs there are eight rows, so the complete
function is described by an eight-bit value. That byte is called the *logic
function* (LF) and selects one of 256 possible operations.

**Modulo** — a signed value added to a channel's address pointer at the end of
every scanline of the blit. It is the difference between the stride of the
source bitmap and the width of the rectangle being transferred, and it is what
makes the transfer two-dimensional rather than linear.

**Surface** — any rectangular pixel buffer in SDRAM: the scanout framebuffer,
a window backing store, a font atlas, an icon sheet.

**Backing store** — a surface holding the complete contents of one window,
including the parts currently hidden behind other windows.

**Scanout** — the process of reading the framebuffer in raster order and
driving pixels to the display timing generator.

**Compositor** — the logic (here, kernel code driving the blitter) that
assembles the visible framebuffer from the individual window backing stores.

**Damage / dirty rectangle** — a region of the screen whose composited content
is no longer valid and must be rebuilt.

---

## 2. Memory baseline

| Parameter | Value |
|---|---|
| Part | AS4C32M16SB-7TIN or IS42S16320F-7TLI |
| Organisation | 32M × 16, four banks |
| Capacity | 64 MB |
| Package | TSOP-II 54 |
| Footprint fallback | 32 MB part, pin-compatible |
| Controller clock (design target) | 100 MHz |
| CAS latency at 100 MHz | 2 |
| Rows per bank | 8192 |
| Columns per row | 1024 words = 2048 bytes |
| Peak bandwidth | 200 MB/s |
| Usable bandwidth (burst-oriented traffic) | 140–170 MB/s |

Two consequences of the row geometry are worth stating explicitly, because
they shape the rest of the design.

**A row holds exactly 2 KB.** At 8 bits per pixel that is 2048 pixels. For a
1024-pixel-wide framebuffer, one row activation serves two complete scanlines.
Row-management overhead in the scanout path is therefore negligible — roughly
one activation per two lines, against 512 words of payload.

**Refresh costs about 0.9%.** One AUTO REFRESH command every 7.8125 µs, each
occupying `tRFC` ≈ 66 ns, or 7 clocks out of every 781. This is budgeted, not
ignored, but it is not a design constraint.

---

## 3. Bandwidth budget

### 3.1 Scanout cost

Scanout is unconditional, continuous traffic. It is subtracted from the budget
before anything else is considered.

| Mode | Bytes per frame | At 60 Hz |
|---|---|---|
| 640 × 400 × 8 bpp | 256 000 | 15.4 MB/s |
| 800 × 600 × 8 bpp | 480 000 | 28.8 MB/s |
| 1024 × 600 × 8 bpp | 614 400 | 36.9 MB/s |
| 1024 × 600 × 16 bpp | 1 228 800 | 73.7 MB/s |
| 1366 × 768 × 8 bpp | 1 049 088 | 62.9 MB/s |

### 3.2 Composition cost

A full-screen composite reads every source pixel once and writes every
destination pixel once. Overlapped regions are read more than once only if the
compositor is naive about occlusion; with front-to-back clipping the read side
approaches one screen's worth.

Full-screen composite at 1024 × 600 × 8 bpp ≈ 1.23 MB of traffic.

### 3.3 Combined headroom

Taking 1024 × 600 × 8 bpp as the working case and 160 MB/s as the usable
figure:

| Consumer | Demand |
|---|---|
| Scanout, 60 Hz | 36.9 MB/s |
| Audio DMA (stereo, 44.1 kHz, 16-bit, plus mixing reads) | < 1 MB/s |
| Remaining for blitter and CPU aperture | ≈ 122 MB/s |

At 1.23 MB per full-screen composite, the remaining budget supports
approximately **100 full-screen composites per second**. The design target is
60, and in practice damage tracking will reduce most frames to a small
fraction of a full screen.

**This is the finding that settles the compositor model.** Full recomposition
is affordable. Damage tracking becomes an efficiency optimisation, not an
architectural requirement.

### 3.4 Modes that do not fit

1024 × 600 × 16 bpp costs 73.7 MB/s of scanout and doubles every composite.
The combined figure exceeds the usable budget at 60 Hz. 16 bpp is therefore
specified as a non-composited mode only — suitable for a single full-screen
application that owns the framebuffer directly, not for the windowed GUI.

---

## 4. Pixel format

**8 bits per pixel, palettised, is the native GUI format.**

The reasoning is not primarily about memory. It is about the shifter.

A blit whose destination does not begin on a word boundary requires the source
data to be shifted before combination. On the Amiga, with 1-bit-deep
bitplanes, this meant a 16-position barrel shifter on each of two channels —
a substantial amount of logic.

With a 16-bit memory bus:

| Depth | Pixels per word | Shifter positions required |
|---|---|---|
| 4 bpp | 4 | 4 |
| **8 bpp** | **2** | **2** |
| 16 bpp | 1 | 1 (none) |

At 8 bpp the shifter degenerates to a byte swap: a single multiplexer per
channel. This is a large saving in an iCE40 and removes the shifter from the
critical path.

A 256-entry palette RAM in EBR maps the 8-bit index to the panel's colour
depth in the scanout path.

---

## 5. SDRAM bank allocation

The SDRAM has four independently addressable banks, each with its own open-row
register. A bank can be activated while another is transferring. Exploiting
this is the difference between roughly 80 MB/s and roughly 160 MB/s of
delivered bandwidth.

The failure mode being avoided: a 2D blit advances by one bitmap stride at the
end of every scanline. If source, destination and scanout all live in the same
bank, every line boundary forces a PRECHARGE and ACTIVATE that cannot be
overlapped with anything, costing roughly 40 ns each. Over a 200-line blit
that is 8 µs of pure stall.

### 5.1 Allocation policy

| Bank | Contents |
|---|---|
| 0 | Scanout framebuffer, front buffer |
| 1 | Scanout framebuffer, back buffer |
| 2 | Window backing stores (even-numbered windows) |
| 3 | Window backing stores (odd-numbered windows), font atlas, icon sheets, audio buffers |

The invariant this enforces: **during composition, the blitter's read traffic
(banks 2/3) and its write traffic (bank 0 or 1) are in different banks from
each other and from scanout (the other of bank 0/1).** Every ACTIVATE can be
issued during another bank's data phase.

Bank bits are the high address bits, so this allocation is expressed simply as
an address-space partition:

| Bank | Address range | Size |
|---|---|---|
| 0 | `0x000000`–`0x3FFFFF` | 16 MB |
| 1 | `0x400000`–`0x7FFFFF` | 16 MB |
| 2 | `0x800000`–`0xBFFFFF` | 16 MB |
| 3 | `0xC00000`–`0xFFFFFF` | 16 MB |

The allocator in the kernel must respect this partition. It is not free to
place a backing store wherever it likes.

### 5.2 Burst policy

Minimum blitter burst is **8 words (16 bytes)**. Shorter bursts fail to
amortise `tRCD` and the arbitration handover.

This has a visible consequence for the window manager: **wide rectangles are
cheaper per pixel than narrow ones.** A 16 × 400 blit and a 400 × 16 blit move
the same pixels, but the former pays 400 line-boundary costs against 8 words
of payload each. Vertical scrollbars, window borders and other narrow
furniture should be batched or drawn by the CPU where practical.

---

## 6. Blitter architecture

### 6.1 Datapath

```
  Bank 2/3 ─┬─► [A fetch] ─► [shift A] ─┐
            ├─► [B fetch] ─► [shift B] ─┤
            └─► [C fetch] ──────────────┤
                                        ▼
                              [ minterm LUT, LF byte ]
                                        │
                              [ first/last word mask ]
                                        │
                              [ optional fill logic ]
                                        │
  Bank 0/1 ◄──────────────── [D write, burst buffer]
```

Each of the four channels has:

- A 24-bit byte pointer into SDRAM
- A 16-bit signed modulo, added at end of line
- An enable bit; a disabled read channel supplies a constant from its data
  register instead of consuming bandwidth

Shared across channels:

- Width in words and height in lines
- First-word and last-word masks, applied to channel A, for non-word-aligned
  left and right edges
- Descending mode: pointers decrement rather than increment, required for
  overlapping copies that move down-right
- The LF byte

### 6.2 Logic function encoding

The LF byte is indexed by `{A, B, C}` treated as a three-bit number. Bit *n*
of LF is the output when `{A,B,C} = n`.

Useful values:

| LF | Function | Use |
|---|---|---|
| `$00` | 0 | Clear |
| `$0C` | B | Straight copy |
| `$3C` | B XOR C | Rubber-band outlines, XOR cursors |
| `$CA` | (A AND B) OR (NOT A AND C) | **Cookie-cut: masked sprite over background** |
| `$F0` | A | Pattern fill from A |
| `$FA` | B OR C | Additive overlay |
| `$FF` | 1 | Set |

`$CA` is derived as follows, and is the operation the compositor uses for
every window that has a non-rectangular or partially transparent region:

| A | B | C | Out | Bit |
|:-:|:-:|:-:|:-:|:-:|
| 1 | 1 | 1 | 1 | 7 |
| 1 | 1 | 0 | 1 | 6 |
| 1 | 0 | 1 | 0 | 5 |
| 1 | 0 | 0 | 0 | 4 |
| 0 | 1 | 1 | 1 | 3 |
| 0 | 1 | 0 | 0 | 2 |
| 0 | 0 | 1 | 1 | 1 |
| 0 | 0 | 0 | 0 | 0 |

→ `1100 1010` = `$CA`.

For the common case of a fully opaque rectangular window, the compositor uses
`$0C` with channels A and C disabled: one read stream, one write stream, half
the traffic.

### 6.3 Deferred features

**Line mode** (Bresenham line drawing in hardware) and **area fill** (single-
bit state propagating along each line to fill polygon interiors) are both
Amiga blitter features with clear value. They are deferred to a second RTL
pass; the register map reserves space for them.

---

## 7. Register map

The blitter and scanout registers occupy the top page of the FPGA-B aperture.

> **Open item.** The CPU reaches FPGA-B through the 64 KB aperture at bank
> `$FE`, gated by the MMU's `VRAM_SEL`. This note proposes that page
> `$FE:FF00`–`$FE:FFFF` be carved out as the NEON register window, leaving
> `$FE:0000`–`$FE:FEFF` as the direct pixel window. This must be reconciled
> with the FPGA-A aperture decode before RTL.

All registers are byte-wide. Multi-byte registers are little-endian and are
latched on write to the highest-numbered byte, so that a partially written
pointer can never be acted upon.

| Offset | Name | Width | Access | Description |
|---|---|---|---|---|
| `$00`–`$02` | `BLT_APT` | 24 | W | Channel A pointer |
| `$04`–`$06` | `BLT_BPT` | 24 | W | Channel B pointer |
| `$08`–`$0A` | `BLT_CPT` | 24 | W | Channel C pointer |
| `$0C`–`$0E` | `BLT_DPT` | 24 | W | Channel D pointer |
| `$10`–`$11` | `BLT_AMOD` | 16 | W | Channel A modulo, signed |
| `$12`–`$13` | `BLT_BMOD` | 16 | W | Channel B modulo, signed |
| `$14`–`$15` | `BLT_CMOD` | 16 | W | Channel C modulo, signed |
| `$16`–`$17` | `BLT_DMOD` | 16 | W | Channel D modulo, signed |
| `$18`–`$19` | `BLT_SIZH` | 16 | W | Width in words |
| `$1A`–`$1B` | `BLT_SIZV` | 16 | W | Height in lines |
| `$1C` | `BLT_LF` | 8 | W | Logic function minterm byte |
| `$1D` | `BLT_CON` | 8 | W | See below |
| `$1E`–`$1F` | `BLT_AFWM` | 16 | W | First-word mask for channel A |
| `$20`–`$21` | `BLT_ALWM` | 16 | W | Last-word mask for channel A |
| `$22`–`$23` | `BLT_ADAT` | 16 | W | Constant for A when disabled |
| `$24`–`$25` | `BLT_BDAT` | 16 | W | Constant for B when disabled |
| `$26`–`$27` | `BLT_CDAT` | 16 | W | Constant for C when disabled |
| `$28` | `BLT_START` | 8 | W | Any write enqueues the current register set |
| `$29` | `BLT_STATUS` | 8 | R | See below |
| `$2A` | `BLT_QUEUE` | 8 | R | Queue occupancy, 0–8 |
| `$2B` | `BLT_CTRL` | 8 | W | `[0]` abort current, `[1]` flush queue, `[2]` IRQ enable |
| `$30`–`$3F` | — | — | — | Reserved: line mode, fill mode |

**`BLT_CON` bit assignment**

| Bit | Name | Meaning |
|---|---|---|
| 0 | `SHA` | Channel A shift: 0 = aligned, 1 = one pixel right |
| 1 | `SHB` | Channel B shift |
| 2 | `ENA` | Channel A enable |
| 3 | `ENB` | Channel B enable |
| 4 | `ENC` | Channel C enable |
| 5 | `END` | Channel D enable |
| 6 | `DESC` | Descending mode |
| 7 | `MASK` | Enable first/last word masks |

**`BLT_STATUS` bit assignment**

| Bit | Name | Meaning |
|---|---|---|
| 0 | `BUSY` | A blit is in progress |
| 1 | `QFULL` | Command queue is full; further writes to `BLT_START` are ignored |
| 2 | `QEMPTY` | Queue empty and engine idle |
| 3 | `IRQ` | Completion interrupt pending; cleared on read |
| 7:4 | — | Reserved, read as zero |

### 7.1 Command queue

`BLT_START` pushes a snapshot of the register file into an 8-deep FIFO held in
EBR. The CPU may therefore set up the next blit while the current one runs,
which matters a great deal on a 65816 at 14 MHz: the register setup for one
blit is roughly 30 byte stores, and without a queue the engine would idle
through every one of them.

A completion interrupt is raised when the queue drains, not on every blit.

---

## 8. Arbitration

Five clients contend for the SDRAM controller. Priority is fixed; preemption
happens only at burst boundaries.

| Priority | Client | Deadline character |
|---|---|---|
| 0 | Refresh | Hard, but schedulable with 7.8 µs of slack |
| 1 | Scanout FIFO refill, below watermark | **Hard real-time. Cannot fail.** |
| 2 | Audio DMA | Soft; deep FIFO absorbs jitter |
| 3 | CPU aperture access | Stalls PHI2 while pending |
| 4 | Blitter | Best effort |

### 8.1 The scanout guarantee

Scanout is not merely high priority. If its FIFO underruns mid-line, the
display shows corruption for that line — there is no recovery and no
degradation mode. The arbiter must therefore be shown to satisfy it under
worst case, not merely to favour it.

This is the inverse of the Amiga's `BLTPRI` bit, which allowed the blitter to
claim every free slot and starve the CPU. Here the blitter must never be able
to starve the display.

**Worst-case scanout stall**, in 100 MHz clocks:

| Contribution | Clocks |
|---|---|
| In-flight blitter burst cannot be preempted | 8 |
| Refresh already granted | 7 |
| PRECHARGE of the scanout bank | 2 |
| ACTIVATE | 2 |
| CAS latency | 2 |
| Arbitration and handover | 2 |
| **Total** | **23 (230 ns)** |

**FIFO sizing.** At 1024 × 600 × 60 Hz the pixel clock is approximately
53.7 MHz including blanking. During the active region the FIFO drains at one
byte per pixel clock, or about 27 Mwords/s. A 230 ns stall drains 6.2 words.

A 256-word FIFO (one EBR) provides a margin of forty times the worst case. Two
EBRs are specified regardless, giving 512 words, because the second also
absorbs the burst-granularity mismatch during horizontal blanking prefetch.

The watermark for requesting refill is set at half depth.

### 8.2 CPU aperture accesses

A CPU read or write through the `$FE` aperture stalls PHI2 until the SDRAM
controller services it. Placing the CPU above the blitter keeps this stall
bounded at roughly the same 230 ns worst case.

The kernel should nonetheless treat direct aperture access as expensive and
route bulk pixel movement through the blitter. The aperture exists for
irregular drawing the blitter cannot express, not for bulk transfer.

---

## 9. Scanout pipeline

```
SDRAM bank 0/1 ─► [ 512-word FIFO ] ─► [ palette RAM, 256 × RGB ]
                                              │
                        [ hardware cursor overlay, 32 × 32 ]
                                              │
                        [ timing generator: PCLK/HSYNC/VSYNC/DE ]
                                              │
                                        ANX6345 ─► panel
```

### 9.1 Hardware cursor

A 32 × 32, 4 bpp cursor bitmap in EBR (512 bytes, one block), with index 0
treated as transparent, composited into the pixel stream during scanout.

The justification is bandwidth, not convenience. Pointer motion is the highest
frequency event in a GUI. Without an overlay, every mouse movement damages two
rectangles — the old position and the new — and forces a composite pass. With
an overlay, moving the pointer costs two register writes and zero memory
traffic.

| Register | Width | Description |
|---|---|---|
| `CURS_X` | 16 | Horizontal position, signed, may be negative for partial off-screen |
| `CURS_Y` | 16 | Vertical position, signed |
| `CURS_CTRL` | 8 | `[0]` enable |
| `CURS_DATA` | 8 | Auto-incrementing write port for bitmap upload |
| `CURS_PAL` | 8 × 4 | Cursor palette, indices 1–3 |

### 9.2 Double buffering

Front and back buffers occupy banks 0 and 1. The compositor writes the back
buffer; scanout reads the front. Buffers swap at VBLANK by exchanging the base
address register, which also exchanges which bank each role occupies — the
bank-separation invariant of §5.1 holds either way round.

A `VBLANK` interrupt and a `FB_SWAP_PENDING` status bit let the kernel avoid
starting a composite it cannot finish before the next swap point.

---

## 10. Compositor model

### 10.1 Options considered

| Model | Bandwidth | Memory | Kernel complexity | Hidden content |
|---|---|---|---|---|
| A — Single framebuffer, damage list, clipping (Amiga Intuition, X11) | Minimal | Minimal | High: clip-region arithmetic in the drawing path | Lost; application must redraw |
| B — Backing stores, full recomposition (Quartz, Wayland) | High | High | Low | Preserved |
| C — Backing stores, recomposition of damaged rectangles only | Moderate | High | Moderate | Preserved |

### 10.2 Decision

**Model C, implemented as a strict optimisation of Model B.**

The compositor is written so that a full-screen composite is always correct
and always affordable. Damage tracking then restricts the work actually done,
but the correctness of the display never depends on the damage list being
right. A bug in damage accounting costs performance, not visible corruption,
and a `COMPOSITE_ALL` path is always available as a fallback.

This is possible only because §3.3 showed full recomposition fits inside the
budget. With the previous PSRAM baseline it did not, and Model A would have
been forced.

### 10.3 Memory cost

At 1024 × 600 × 8 bpp:

| Item | Size |
|---|---|
| Front buffer | 600 KB |
| Back buffer | 600 KB |
| Backing store, full-screen window | 600 KB |
| Backing store, typical 640 × 400 window | 250 KB |
| Font atlas, one face | ~64 KB |

Against 64 MB, even a generous allowance for backing stores uses a few percent
of the SDRAM. Memory is not a constraint on this decision.

### 10.4 Composite pass

For each damaged rectangle, back to front over the window stack:

1. Clip the window's visible region against the damaged rectangle.
2. If the result is empty, skip.
3. If the window is opaque and rectangular: one blit, `LF = $0C`, channels B
   and D only.
4. Otherwise: one blit, `LF = $CA`, with the window's mask on channel A, its
   pixels on channel B, and the destination read back on channel C.

Clipping is expressed entirely through the pointer, width, height and modulo
registers. No additional hardware clipping logic is required — this is the
same mechanism that makes the transfer two-dimensional in the first place.

### 10.5 Drawing into a backing store

Two paths coexist, and both are needed:

**Blitter path** — rectangle fills, glyph blits from the font atlas, widget
chrome from an icon sheet, scrolling a text region. Text is the important
case: with the font bitmap resident in bank 3, drawing a character is an
8 × 16 blit and the CPU issues only the command. This is the difference
between a usable terminal and one that visibly redraws.

**CPU aperture path** — irregular geometry, anything the blitter cannot
express. Slow, and it stalls PHI2, so it is reserved for cases with no
alternative.

---

## 11. Resource estimate, iCE40HX4K

| Block | LUT4 estimate | EBR (4 Kb blocks) |
|---|---|---|
| SDRAM controller with bank interleaving | 700–900 | 0 |
| Arbiter | 200–300 | 0 |
| Blitter datapath and address generators | 900–1200 | 0 |
| Blitter command queue, 8 deep | 150 | 2 |
| Scanout FIFO | 100 | 2 |
| Palette RAM | 50 | 2 |
| Cursor overlay | 200 | 1 |
| Timing generator | 200 | 0 |
| Audio DMA and mixer | 400 | 2 |
| CPU bus interface and register file | 400 | 0 |
| **Total** | **≈ 3300–3900** | **9** |

The HX4K provides 3520 usable LUT4 and 20 EBR. **The LUT estimate is at or
above the ceiling.** This is a genuine risk and is listed as an open item
below; the HX8K in the same TQ144 package is a drop-in escape route and the
footprint should be treated as HX8K-capable from the start.

---

## 12. Open items

| # | Item | Blocks |
|---|---|---|
| 1 | Reconcile the `$FE:FF00` register window proposal with the FPGA-A aperture decode and `VRAM_SEL` logic | RTL and schematic |
| 2 | Confirm HX4K capacity against the §11 estimate; decide whether to specify HX8K for FPGA-B | Part selection, schematic |
| 3 | Fix the working resolution. §3 assumes 1024 × 600; the 14" panel choice is still open and the budget must be re-derived once it is fixed | Everything in §3 |
| 4 | SDRAM controller clock: 100 MHz assumed. Confirm achievable timing closure in nextpnr for the HX-series speed grade selected | Bandwidth figures |
| 5 | Decide whether descending mode is required in the first RTL pass, or whether the compositor can guarantee non-overlapping blits | Blitter complexity |
| 6 | Kernel-side SDRAM allocator must enforce the bank partition of §5.1 | Kernel memory manager |
| 7 | Audio DMA burst size and FIFO depth, so the §8.1 worst case can be finalised | Arbiter |
| 8 | Whether the font atlas is per-face resident or paged in from system memory | Kernel, bank 3 budget |

---

## 13. Summary of decisions

1. FPGA-B has dedicated 16-bit SDRAM at 100 MHz. PSRAM is removed from the
   design.
2. 8 bpp palettised is the native GUI format, chosen because it reduces the
   blitter shifter to a two-position multiplexer.
3. SDRAM banks are partitioned by function so that blitter reads, blitter
   writes and scanout never contend for the same open row.
4. The blitter is a four-channel engine with 256 logic functions, 2D modulo
   addressing, edge masking and an 8-deep command queue.
5. Arbitration is fixed-priority with a proven worst-case scanout stall of
   230 ns, against a FIFO sized for forty times that.
6. A hardware cursor overlay removes pointer motion from the composite path
   entirely.
7. The compositor uses backing stores with damage-limited recomposition, with
   full recomposition always available as a correct fallback.
