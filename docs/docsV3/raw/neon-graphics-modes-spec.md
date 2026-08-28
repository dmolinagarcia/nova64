# Neon GPU — Graphics Modes and Command Engine

**Project:** DANI-65816
**Subsystem:** Neon (FPGA-B)
**Document:** Modes 1 and 2, Blitter, Command Processor
**Date:** 2026-08-14
**Status:** Draft — depends on unresolved items in the Text Mode Specification

---

## 1. Scope

Specifies **Mode 1 (hires monochrome)** and **Mode 2 (multicolour)**, the drawing engine that fills their framebuffers, and the command interface through which the 65816 drives that engine.

The governing requirement is the user's: *Neon must be able to generate the framebuffer without CPU intervention.* Everything below is arranged around that. Mode 0 (text) is specified separately and is unaffected.

---

## 2. Terminology

| Term | Definition |
|---|---|
| **Blitter** | Block image transferrer — a hardware engine that fills, copies, and combines rectangular regions of memory without CPU involvement. |
| **Command list** | A sequence of drawing commands stored in memory and executed autonomously by the graphics processor. |
| **Bank (SDRAM)** | One of four independently-addressable arrays inside the SDRAM device. Accesses to different banks can be interleaved without paying row-activation delay. |
| **Row activation** | The delay incurred when SDRAM must open a new row before data in it can be accessed (~60 ns). |
| **Colour key** | A palette index designated as transparent; pixels of that value are not written during a copy. |
| **Page flip** | Displaying one framebuffer while drawing into another, then exchanging them. |
| **Barrel shifter** | Combinational logic that shifts a word by any amount in one cycle; required to place 1-bpp images at arbitrary horizontal positions. |
| **Clock enable** | A signal that gates a register's update without gating its clock, used here to derive a slower rate inside one clock domain. |

---

## 3. Clocking — Revision to Earlier Assumption

The Text Mode Specification allocated both PLLs: one at 51 MHz for pixels, one at ~100 MHz for SDRAM. **This is unnecessary and should be changed.**

Choose the SDRAM clock as exactly **twice** the pixel clock:

```
DIVR = 0, DIVF = 33, DIVQ = 2  →  12 MHz × 34 / 4 = 102.000 MHz
```

Note this is the same `DIVF` as the 51 MHz setting; only `DIVQ` changes.

The entire design then runs in **one clock domain at 102 MHz**, with the pixel stage advancing on a clock enable asserted every second cycle. `PCLK` is emitted through a DDR output register, producing a clean 51 MHz panel clock with no combinational clock path.

Three consequences, all favourable:

1. **No clock-domain crossing.** No synchroniser FIFOs between scanout and memory. This removes an entire class of intermittent, hard-to-diagnose failures.
2. **The second PLL is freed** — available for audio, or held in reserve.
3. Refresh rate is unchanged at 59.76 Hz; the Text Mode Specification requires no other amendment.

> **Risk R-1.** 102 MHz is demanding for an iCE40 HX. SDRAM controllers at 100–133 MHz on this family are documented, but timing closure is not guaranteed until synthesis. **Fallback:** run the whole design at 51 MHz single-rate, halving peak SDRAM bandwidth to 102 MB/s. Every performance figure below would halve — and would still exceed CPU-driven rendering by more than an order of magnitude. The architecture does not depend on 102 MHz; only the margins do.

---

## 4. Memory Bandwidth Budget

Neon's dedicated W9825G6KH (32 MB, 16-bit) at 102 MHz gives **204 MB/s peak**.

Overheads: row activation costs ~20 clocks per 512-word row (~4% on sequential access); auto-refresh costs ~0.2%. Sequential throughput is therefore ~185 MB/s. For a mixed workload with bank switching, budget **~150 MB/s effective**.

| Consumer | Mode 2a | Mode 1 |
|---|---|---|
| Scanout | 3.8 MB/s | 4.6 MB/s |
| Hardware cursor | 0.1 MB/s | 0.1 MB/s |
| Audio DMA | 0.7 MB/s | 0.7 MB/s |
| CPU aperture writes | 1.6 MB/s (worst case) | 1.6 MB/s |
| **Remaining for the drawing engine** | **~144 MB/s** | **~143 MB/s** |

Scanout consumes under 3% of the memory system. **Bandwidth is no longer a design constraint for Neon.** The binding constraints are FPGA logic (§11) and EBR (§10).

---

## 5. Mode 1 — Hires Monochrome

| Parameter | Value |
|---|---|
| Resolution | **1024 × 600** — native, no replication, square pixels |
| Depth | 1 bpp |
| Framebuffer | 128 B/line × 600 = **76,800 B (75 KB)** |
| Bit order | MSB = leftmost pixel |
| Colours | Two palette indices, from `MONO_FG` / `MONO_BG` registers |
| Border | None — full panel |
| Aperture | 2 pages of 64 KB (`VRAM_PAGE` required) |
| Scanout | 4.6 MB/s |

This is the GUI mode. Full panel resolution with 1:1 pixels is what makes readable text and precise window furniture possible.

**The expensive part of Mode 1 is not the framebuffer — it is arbitrary-alignment blitting.** Placing an image at any horizontal pixel position in a packed 1-bpp buffer requires shifting each source word by 0–15 bits and merging across word boundaries, with edge masks at both ends. This is the classic Amiga blitter problem. Estimated cost: **~400 LUT** for the barrel shifter and mask generator. It is not optional — a GUI that can only draw at 16-pixel horizontal boundaries is not a GUI.

---

## 6. Mode 2 — Multicolour

Two sub-modes, differing only in the replication factor and buffer size. Both are 8 bpp with a 256-entry palette.

| | **Mode 2a** | **Mode 2b** |
|---|---|---|
| Logical resolution | 320 × 200 | 512 × 300 |
| Replication | 3 × 3 | 2 × 2 |
| Displayed area | 960 × 600 | **1024 × 600 — exact** |
| Horizontal border | 32 px each side | none |
| Vertical border | none | none |
| Framebuffer | **64,000 B** | 153,600 B |
| Aperture pages | **1** | 3 |
| Scanout | 3.8 MB/s | 9.2 MB/s |
| Double-buffered total | 128 KB | 300 KB |

**Mode 2a** is the recommended default for games: it is the familiar 320×200 geometry, its framebuffer fits inside a single aperture window with 1,536 bytes to spare, and its pixels are exactly square because the panel's are.

**Mode 2b** fills the panel with no border and costs only memory and bandwidth, both of which are abundant. It is the better choice for anything where the black side bars would be objectionable.

Both are supported by identical logic; the sub-mode is a register field controlling the replication counters and the line stride. Adding 2b costs approximately nothing.

Palette: 256 entries × 18-bit RGB666, held in EBR.

---

## 7. Hardware Tile Mode — Recommended Against

The C64's VIC-II synthesised its display from a character matrix because it had no framebuffer and no bandwidth for one. Neither constraint applies here. The relevant comparison:

**Redrawing a full 320×200 tilemap with the blitter:** 20 × 13 tiles of 16×16 px = 260 tiles × 256 B = 66,560 B of opaque copy. At ~100 MB/s for read+write, this is **~1.3 ms** — under 8% of a 16.7 ms frame.

The blitter can therefore rebuild the entire tiled background **every frame**, from scratch, and still leave 92% of the frame for everything else. Pixel-smooth scrolling comes free by offsetting the destination coordinates.

| | Hardware tile engine | Blitter redraw |
|---|---|---|
| Logic cost | ~800 LUT | 0 (blitter already required) |
| Scroll | Register write | Offset in the draw loop |
| Full-screen tile change | 2,000 B of tilemap | 1.3 ms of blitting |
| Composability with drawn graphics | Poor — separate layer | Native, same buffer |
| Tile size / shape | Fixed at synthesis | Arbitrary |

**Recommendation: do not implement a hardware tile mode.** It costs ~800 LUT on a device where logic is the binding constraint, to accelerate something the blitter already does in 8% of a frame, and it fragments the rendering model into two incompatible paths.

### 7.1 The same argument applies to hardware sprites

A full 32-sprite engine was estimated at ~1,000 LUT. Blitting 32 masked 16×16 sprites costs 32 × 256 B ≈ 8 KB of masked transfer — **~0.12 ms**, or 0.7% of a frame.

Hardware sprites exist to avoid the save/restore-background problem in systems that cannot afford to redraw. With double buffering and 32 MB of SDRAM, that problem does not exist: each frame is drawn fresh into the back buffer.

**Recommendation: no general sprite engine.** Retain only the **hardware cursor**, extended from Mode 0 to the graphics modes (16×16 or 32×32, 2 bpp, ~150 LUT). A GUI pointer must track the mouse independently of the rendering loop, so this one case is genuinely worth hardware.

Combined saving: **~1,650 LUT**, roughly 21% of the device — which is what makes §11 close.

> This is a recommendation with a real trade-off, not a settled decision. What is given up: sprites that composite at scanout cost no framebuffer bandwidth and cannot be "erased" by drawing, which simplifies certain effects. What is gained: a single rendering model, and the logic budget to fit the blitter, command processor, and audio on one device.

---

## 8. Drawing Engine

### 8.1 Operations

| Operation | Description |
|---|---|
| `FILL_RECT` | Solid colour fill |
| `FILL_PATTERN` | Fill with an 8×8 pattern from the pattern register |
| `COPY_RECT` | Opaque rectangular copy, overlap-safe (direction selected automatically) |
| `COPY_KEYED` | Copy, skipping pixels equal to the colour key |
| `COPY_TILED` | Copy with the source wrapped modulo its dimensions — texture fill |
| `COPY_EXPAND` | 1-bpp source expanded to two colours — glyph and icon rendering into Mode 2 |
| `DRAW_LINE` | Bresenham, arbitrary endpoints |
| `DRAW_HLINE` / `DRAW_VLINE` | Optimised axis-aligned cases |

All operations respect a clip rectangle set by `SET_CLIP`.

> **`COPY_TILED` is the interpretation of "apply texture" carried forward from discussion.** It fills a region by repeating a source bitmap — the QuickDraw / Amiga-blitter sense of texturing. **Perspective-correct 3D texture mapping is out of scope and not implementable on this device**: it requires a per-pixel divide, and the iCE40 has neither hardware multipliers nor the logic budget for them.

### 8.2 Measured cost per full-screen operation (Mode 2a, 64,000 B)

| Operation | Traffic | Time | vs. 65816 |
|---|---|---|---|
| `FILL_RECT` | 64 KB write | **0.31 ms** | 129× |
| `COPY_RECT` | 64 KB read + 64 KB write | **~1.0 ms** | 40× |
| `COPY_KEYED` | read src + read dst + write dst | **~1.5 ms** | 27× |
| `DRAW_LINE` | ~1 px/clock | 1024 px in 10 µs | — |

The 65816 reference is ~40 ms for a full-screen rewrite through the aperture at 8 MHz.

Source and destination should be placed in **different SDRAM banks** where possible; the controller interleaves them, avoiding row-activation stalls on every alternation. The command processor does not enforce this — it is a memory-layout convention for the kernel and for asset loaders.

### 8.3 Frame budget

At 60 Hz, one frame is 16.7 ms. After scanout, cursor, audio, and CPU aperture traffic, approximately **15.5 ms** is available to the drawing engine per frame. That is roughly:

- 50 full-screen fills, or
- 15 full-screen opaque copies, or
- 10 full-screen keyed copies, or
- a full tilemap rebuild (1.3 ms) plus ~100 masked 16×16 sprites plus ~200 UI rectangles, with time left over.

---

## 9. Command Interface

Three layers, each usable independently. Layer 3 is the one that satisfies "without CPU intervention"; layers 1 and 2 exist because they are needed first during bring-up.

### 9.1 Layer 1 — Register-driven (bring-up only)

Parameters written to registers, execution triggered by a `GO` bit, completion detected by polling `BUSY`. Simple, verifiable, and requires no FIFO or command decoder. It is how the blitter gets tested in isolation. It is not intended for production use — the CPU is occupied for the duration of every operation.

### 9.2 Layer 2 — Command FIFO

The CPU writes 32-bit command words to `CMD_PORT` (a single aperture address). Neon buffers them in a 128-entry EBR FIFO and executes them in order, asynchronously.

The CPU checks `CMD_FIFO_FREE` before writing a command's worth of words. Because the CPU produces roughly 1.6 MB/s and the engine consumes commands far faster than that, the FIFO does not fill under any realistic drawing load — a full-screen fill is 5 words in and 0.31 ms of work.

This alone decouples the CPU from drawing. It is sufficient for a GUI.

### 9.3 Layer 3 — Command lists in SDRAM

The `EXEC_LIST` command redirects the command processor to fetch its instruction stream from an address in SDRAM instead of from the FIFO. The list may contain any drawing command plus flow control:

| Command | Effect |
|---|---|
| `JUMP addr` | Continue at `addr` |
| `CALL addr` / `RET` | One level of subroutine nesting |
| `WAIT_VBLANK` | Suspend until the next vertical blanking interval |
| `WAIT_LINE n` | Suspend until panel line `n` — raster-synchronised effects |
| `SWAP_BUFFERS` | Exchange front and back buffers at the next vblank |
| `SET_PALETTE i, rgb` | Palette write, may be mid-frame after `WAIT_LINE` |
| `SIGNAL n` | Raise an interrupt to the CPU with tag `n` |
| `END` | Return to FIFO execution |

**This is the mode the user asked for.** A game builds its display list once, terminates it with `SWAP_BUFFERS` and `JUMP` back to the start, and the CPU is then free: Neon renders every frame autonomously, forever, until the list is modified. The CPU's per-frame work reduces to patching coordinates inside an existing list — a handful of 16-bit stores.

The model is the ANTIC display list and the Amiga Copper, generalised to include the blitter.

**Self-modifying lists are the intended usage pattern.** Because the CPU can write into a list while Neon executes it, moving an object is a store to the parameter word of an existing `COPY_KEYED` command. A `LIST_LOCK` bit is provided so the kernel can guarantee the engine will not read a partially-updated command; the engine finishes its current command and stalls at the lock.

### 9.4 Command word format

Word 0:

```
bits 31:24  opcode
bits 23:16  flags (clip enable, colour-key enable, direction, ...)
bits 15:0   word count of following parameters
```

Parameters follow as 32-bit words, packed as two 16-bit coordinates each where applicable. `FILL_RECT` is 5 words total; `COPY_KEYED` is 7.

Fixed-length-per-opcode encoding was rejected in favour of an explicit word count so that unknown opcodes can be skipped rather than desynchronising the stream — this matters when gateware and kernel versions drift during development.

---

## 10. EBR Budget

iCE40 HX8K: 32 blocks × 512 B = 16 KB total.

| Structure | Size | Blocks |
|---|---|---|
| Text buffer (Mode 0) | 8 KB | 16 |
| Font (Mode 0) | 4 KB | 8 |
| 256-entry palette | 1 KB | 2 |
| Scanout line buffer, double | 1 KB | 2 |
| Blitter source/destination burst buffers | 1 KB | 2 |
| Command FIFO (128 × 32-bit) | 512 B | 1 |
| Cursor pattern | 512 B | 1 |
| **Total** | **16 KB** | **32** |

**Exactly full, with zero margin.** This closes only because §7 removed the sprite pattern cache and tile store.

> **Open item O-3 (revised, now urgent).** The 8 KB text buffer and 4 KB font are idle in graphics modes. Multiplexing the text buffer's 16 blocks into graphics use would provide 8 KB of slack, at the cost of address multiplexers and of destroying screen contents on a mode switch — acceptable, since returning to text mode implies a redraw anyway. **The font must not be reclaimed**: its bitstream initialisation is what allows printing before any software exists, and reusing those blocks would destroy it. Decide once the blitter's real EBR demand is known from synthesis.

---

## 11. Logic Budget

| Block | LUT (est.) |
|---|---|
| Mode 0 (text) — from prior specification | 900 |
| SDRAM controller and 4-way arbiter | 1,200 |
| Graphics scanout (Modes 1, 2a, 2b, replication) | 400 |
| Blitter — 8-bpp path | 700 |
| Blitter — 1-bpp barrel shifter and edge masking | 400 |
| Line drawing | 250 |
| Command processor (FIFO, list fetch, decode, flow control) | 800 |
| Hardware cursor, graphics modes | 150 |
| Audio (reserved) | 800 |
| **Total** | **~5,600** |

Against 7,680 LUT: **73%**, leaving ~2,000 LUT of margin.

For comparison, the configuration including a tile engine and a 32-sprite unit came to ~7,300 LUT — 95% — which is not a routable design once the audio subsystem grows. The recommendation in §7 is what converts this from marginal to comfortable.

---

## 12. CPU Access to the Framebuffer — A Real Constraint

In graphics modes the framebuffer is in SDRAM, and SDRAM reads carry row-activation latency. Neon cannot stretch PHI2 to cover it, because doing so requires a wait line to FPGA-A and the architecture has eliminated the inter-FPGA link.

**Therefore: the graphics-mode aperture is write-only.** Writes are absorbed by a 32-entry FIFO and retired to SDRAM by the arbiter; the CPU never waits. Reads return `$FF` and set `STATUS.READ_FAULT`.

This is less restrictive than it sounds. Asset flow is one-way — microSD → system RAM → aperture → VRAM — and once the command engine is drawing, the CPU has no reason to read the framebuffer at all. Where a read is genuinely needed (screen capture, debugging), a `PEEK` mechanism is provided: write an address to `PEEK_ADDR`, poll `STATUS.PEEK_READY`, read `PEEK_DATA`. It is slow and intended for diagnostics only.

Mode 0 is unaffected: its buffers are in dual-port EBR and remain fully readable with zero wait states.

> **Open item O-4 (revised).** If a signal line from FPGA-B to FPGA-A is added for interrupts, one additional line would permit PHI2 stretching and make the aperture fully readable. This is worth weighing as a pair rather than deciding the interrupt line in isolation.

---

## 13. Staging

Each stage is a usable machine. If work stops at any stage, nothing is stranded.

| Stage | Content | Acceptance |
|---|---|---|
| **N0** | Mode 0 text (prior specification) | Console at power-on, RP2040 only |
| **N1** | SDRAM controller; Mode 1 scanout; register-driven fill and copy | Bitmap displayed; blitter fills a rectangle |
| **N2** | Mode 2a/2b; palette; 8-bpp blitter; `COPY_KEYED`; double buffering | Animated multicolour scene at 60 Hz |
| **N3** | 1-bpp barrel-shift blitter; `COPY_EXPAND`; graphics cursor | GUI primitives — *Amiga milestone reachable here* |
| **N4** | Command FIFO | CPU no longer polls `BUSY` |
| **N5** | SDRAM command lists, flow control, `WAIT_LINE`, `SWAP_BUFFERS` | Autonomous frame rendering — *games target* |
| **N6** | Line drawing, `FILL_PATTERN`, `COPY_TILED` | — |

Note that **N3 delivers the primary milestone and N5 delivers the games capability.** They are separable, and N3 comes first.

---

## 14. Open Items

| ID | Item | Blocks |
|---|---|---|
| **R-1** | Confirm 102 MHz timing closure on HX8K; fallback to 51 MHz | N1 |
| **O-3** | EBR reuse of the text buffer in graphics modes | N2 |
| **O-4** | FPGA-B → FPGA-A signalling: interrupt line, and whether to add a wait line with it | Pin assignment |
| **O-7** | Ratify or reject the §7 recommendation against tile mode and sprites | N2 logic budget |
| **O-8** | SDRAM bank-allocation convention for source/destination separation | Kernel memory manager |
| **O-9** | Whether Mode 2b (512×300) is built at all, or 2a only | N2 |
| **O-10** | Command list revision/versioning between gateware and kernel | N5 |

Items O-1, O-2, O-5, O-6 from the Text Mode Specification remain open and are not repeated here.

---

## 15. Decisions Superseded by This Document

| Superseded | Replaced by |
|---|---|
| Two PLLs — 51 MHz pixel, 100 MHz SDRAM, separate clock domains | Single 102 MHz domain, pixel clock enable; second PLL freed |
| Hardware tile mode as stage N4 | Blitter tilemap redraw; hardware tile mode not implemented |
| 32-sprite hardware engine | Blitted sprites; hardware cursor retained |
| "PSRAM bandwidth ~40 MB/s is the graphics budget" | Dedicated SDRAM, ~150 MB/s effective |
| Estimated ~7,300 LUT total for Neon | ~5,600 LUT after removing tile and sprite engines |
