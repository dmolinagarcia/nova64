# Neon GPU — Text Mode Specification

**Project:** DANI-65816
**Subsystem:** Neon (FPGA-B) — Graphics Processing Unit
**Document:** Text Mode (Mode 0)
**Date:** 2026-08-14
**Status:** Draft — provisional pending panel datasheet

---

## 1. Scope

This document specifies **Mode 0 (Text)** of the Neon graphics processor, implemented in FPGA-B. It defines display geometry, pixel timing, the text buffer and font storage model, the character attribute format, the CPU-visible register map, and the resource budget.

Graphics modes (framebuffer, blitter, sprites, tiles) are **out of scope** and are specified separately. Where a decision in this document constrains those modes, it is called out explicitly.

**Design intent:** Text mode is the *first* Neon deliverable and must be operational before any other Neon subsystem exists. It is therefore specified so that it depends on **no SDRAM controller, no arbiter, no FIFO, and no inter-FPGA link**. A working console is the debugging instrument for everything that follows.

---

## 2. Terminology

Terms are defined at first use. This section is a convenience index, not a substitute.

| Term | Definition |
|---|---|
| **Neon** | The combined graphics and audio processor implemented in FPGA-B. |
| **Cell** | One character position on screen: a glyph code plus an attribute byte. |
| **Glyph** | The pixel pattern for one character code, stored in the font. |
| **EBR** | Embedded Block RAM — dedicated dual-port memory blocks inside the iCE40 FPGA, distinct from logic (LUT) resources. |
| **Aperture** | The 64 KB window in CPU bank `$FE` through which the 65816 accesses Neon. |
| **Scanout** | The process of reading display data and emitting pixels to the panel in real time. |
| **RGB-TTL** | A parallel digital video interface: separate colour bit lines plus pixel clock and sync signals, driven at logic levels (3.3 V here). |
| **Active area** | The region of the panel that displays image data, as opposed to blanking intervals. |
| **Border** | Panel active area not occupied by the text grid, filled with a solid colour. |
| **Ring buffer** | A buffer addressed modulo its size, so that advancing a start pointer rotates the visible contents without moving data. |

---

## 3. Target Hardware

| Item | Value | Confidence |
|---|---|---|
| Panel | 10.1", 1024×600 | Confirmed by user |
| Panel interface | RGB-TTL, 40-pin, 3.3 V | **Assumed — must be verified** |
| Colour depth to panel | RGB666 (18-bit) | Decided (pin-limited) |
| FPGA-B device | iCE40 **HX8K**, TQ144 | Decided |
| FPGA-B local memory | Dedicated W9825G6KH SDRAM, 16-bit | Decided — **not used in Mode 0** |
| Reference clock | 12 MHz crystal | Existing |

> **Open item O-1.** If the selected panel is eDP rather than RGB-TTL, the ANX6345 bridge and its init sequence return, and §4 pin counts change. This specification assumes RGB-TTL.

---

## 4. Display Timing

### 4.1 Pixel clock

Target: 1344 × 635 total × 60 Hz = **51.20 MHz**.

Achievable from the 12 MHz reference using one iCE40 PLL in SIMPLE mode:

```
DIVR = 0, DIVF = 33, DIVQ = 3
Fout = 12 MHz × (33+1) / ((0+1) × 2^3) = 51.000 MHz
```

Resulting refresh rate: **59.76 Hz**. This is within the tolerance of every 1024×600 panel surveyed and requires no fractional synthesis.

The second PLL is reserved for the SDRAM clock (~100 MHz) used by graphics modes. **Both PLLs are then committed; no third clock domain is available.**

### 4.2 Timing parameters — PROVISIONAL

| | Active | Front porch | Sync | Back porch | Total |
|---|---|---|---|---|---|
| Horizontal (px) | 1024 | 24 | 136 | 160 | **1344** |
| Vertical (lines) | 600 | 3 | 6 | 26 | **635** |

Sync polarity: both negative (typical for this panel class).

> **Open item O-2.** These blanking values are a plausible set consistent with the 51.2 MHz target, **not** values taken from a datasheet. Panel vendors differ substantially. They must be replaced with the figures for the selected part before schematic capture. The pixel clock and the active geometry will not change; only the porches will.

### 4.3 Panel signals

`R[5:0]`, `G[5:0]`, `B[5:0]`, `PCLK`, `HSYNC`, `VSYNC`, `DE` — 22 pins, plus backlight enable and PWM.

---

## 5. Display Geometry

### 5.1 Grid

| Parameter | Value | Derivation |
|---|---|---|
| Cell size | **8 × 16 px** | 1024 / 8 = 128 exactly |
| Columns | **128** | |
| Rows | **32** | 32 × 16 = 512 lines |
| Total cells | **4096** | = 2^12, exactly addressable in 12 bits |
| Text area | 1024 × 512 px | |
| Vertical border | **44 lines top, 44 lines bottom** | (600 − 512) / 2 |
| Horizontal border | none | |

The 8×16 cell is deliberate. It is the standard VGA text cell, which means:

- Any existing 8×16 VGA font is usable without modification.
- Glyph row lookup is a 4-bit shift, not a multiply by 15.
- **Descenders are not truncated.** A 15-line cell would clip the lower row of `g j p q y`, forcing a bespoke font.

The 4096-cell total being an exact power of two is what makes the ring-buffer scroll of §8.2 free — the address wrap is a bit mask, not a comparator.

### 5.2 Vertical centring

Text rows occupy panel active lines 44 through 555 inclusive. Lines 0–43 and 556–599 display `BORDER_COLOR`.

---

## 6. Memory Model

### 6.1 Rationale — why EBR, not SDRAM

The text buffer and font are held entirely in FPGA-B block RAM. This is the central architectural decision of Mode 0 and it buys four things:

1. **CPU reads work with zero wait states.** EBR is genuinely dual-port: port A serves scanout, port B serves the CPU aperture. A 51 MHz fabric against an 8 MHz PHI2 means every CPU access — read *or* write — completes within its bus cycle. No write FIFO, no shadow copy in system SRAM, no `VRAM_WAIT` stall line back to FPGA-A.
2. **No SDRAM controller is required.** The console is available before the most defect-prone block in Neon has been written.
3. **No arbiter is required.** Scanout and CPU never contend.
4. **Glyph fetch latency is one cycle.** Font lookup is random access with no burst locality; in SDRAM it would pay a row-activate penalty (~60 ns) per fetch for no benefit.

### 6.2 Allocation

| Structure | Size | EBR blocks | Notes |
|---|---|---|---|
| Text buffer | 8192 B | 16 | 4096 cells × 2 B |
| Font | 4096 B | 8 | 256 glyphs × 16 B |
| Palette | 288 bits | 0 | Flip-flops, not EBR |
| Line buffer | — | 0 | **Not required** (see §7) |
| **Mode 0 total** | **12 KB** | **24** | of 32 available (HX8K) |
| **Free for graphics** | **4 KB** | **8** | |

> **Open item O-3.** EBR is allocated at synthesis time, not switched per mode. The 8 KB text buffer is dead weight while a graphics mode is running, and 4 KB is a thin allowance for blitter and sprite line buffers. Two escape valves exist: reduce the font to 128 glyphs (2 KB — full ASCII plus box-drawing), or multiplex the text buffer EBR into graphics-mode structures at the cost of address muxes. **Do not decide this now.** Make the glyph count a synthesis parameter and resolve it once the blitter has been synthesised and its real EBR demand is known.

### 6.3 Aperture map (bank `$FE`)

Mode 0 fits within a single 64 KB window. The `VRAM_PAGE` register defined for graphics modes is ignored in text mode.

| Range | Size | Contents | CPU access |
|---|---|---|---|
| `$FE:0000` – `$FE:1FFF` | 8 KB | Text buffer | R/W |
| `$FE:2000` – `$FE:2FFF` | 4 KB | Font | R/W |
| `$FE:3000` – `$FE:303F` | 64 B | Palette | R/W |
| `$FE:3040` – `$FE:FEFF` | — | Unmapped (reads return `$00`) | — |
| `$FE:FF00` – `$FE:FFFF` | 256 B | Register file | see §9 |

Access to bank `$FE` remains subject to the MMU's `VRAM_SEL` protection in FPGA-A; this specification does not alter the protection model.

---

## 7. Character Generation

No line buffer is needed. Character generation is fully real-time.

Per cell, the pipeline requires one text-buffer read and one font read every 8 pixel clocks. At 51 MHz with single-cycle EBR, this is a factor of 8 of slack.

For panel active line `L` where 44 ≤ L ≤ 555:

```
text_row  = (L - 44) >> 4
glyph_row = (L - 44) & 15

cell_index = (TEXT_START + text_row * 128 + column) & 0x0FFF
cell_addr  = cell_index << 1

code       = text_buffer[cell_addr]
attribute  = text_buffer[cell_addr + 1]

glyph_bits = font[(code << 4) | glyph_row]
```

Each of the 8 bits of `glyph_bits` selects between the foreground and background palette entries, MSB first (leftmost pixel).

The pipeline is 3 stages deep (cell fetch → font fetch → shift/palette). Horizontal timing must therefore begin cell fetch 3 clocks before the first active pixel; this is absorbed in the back porch.

For lines outside the text area, and for the horizontal blanking intervals, the output is `BORDER_COLOR`.

---

## 8. Text Buffer

### 8.1 Cell format

Two bytes per cell, little-endian, naturally aligned:

```
Byte 0 (even):  code[7:0]        — glyph index into font
Byte 1 (odd):   bg[3:0] fg[3:0]  — bits 7..4 background, bits 3..0 foreground
```

Both are palette indices into the 16-entry palette.

A 65816 in 16-bit accumulator mode (`M=0`) writes a complete cell with a single `STA` instruction. Note that this is **one instruction but two bus cycles** — the 65816 data bus is 8 bits wide. The saving is in instruction overhead, not bus traffic.

Full-screen fill: 4096 cells = 8192 bytes ≈ **5 ms** at 8 MHz with an unrolled 16-bit store loop.

> **Deferred.** There is no per-character blink attribute. The classic VGA approach reallocates attribute bit 7, which would cut the background palette to 8 entries. Retaining full 16/16 colour is judged more valuable. If blink is wanted later it should be added as a global mode bit that reinterprets bit 7, not as a silent change to the format.

### 8.2 Scrolling — `TEXT_START`

The text buffer is addressed as a ring of 4096 cells. `TEXT_START` is the cell index displayed at the top-left corner.

To scroll up one line, add 128 to `TEXT_START` (modulo 4096) and clear the 128 cells of the newly exposed bottom row.

| | Bytes moved | Time @ 8 MHz |
|---|---|---|
| Naive block move | 8192 | ~5 ms |
| `TEXT_START` + clear one row | 256 | **~160 µs** |

A factor of 32. For a terminal-style console this is the difference between scrolling being a visible cost and being free.

Because the ring wraps at exactly 4096 cells, no wrap-around special case exists in software — the hardware masks the index.

---

## 9. Register File

Base `$FE:FF00`. All registers are byte-wide. Reserved bits read as zero and must be written as zero.

| Offset | Name | Access | Description |
|---|---|---|---|
| `$00` | `NEON_ID` | R | Magic value `$4E` (`'N'`) |
| `$01` | `NEON_VER` | R | Gateware version, BCD |
| `$02` | `NEON_MODE` | R/W | `0` = text. Other values reserved. |
| `$03` | `TEXT_CTRL` | R/W | b0 `DISPLAY_EN` (0 = border colour over whole screen) |
| `$04` | `TEXT_START_L` | R/W | Cell index bits [7:0] |
| `$05` | `TEXT_START_H` | R/W | Cell index bits [11:8] |
| `$06` | `CURSOR_POS_L` | R/W | Cell index bits [7:0] |
| `$07` | `CURSOR_POS_H` | R/W | Cell index bits [11:8] |
| `$08` | `CURSOR_CTRL` | R/W | see §10 |
| `$09` | `BORDER_COLOR` | R/W | b3:0 palette index |
| `$0A` | `STATUS` | R | b0 `VBLANK`, b1 `HBLANK` |
| `$0B` | `RASTER_L` | R | Current panel line [7:0] |
| `$0C` | `RASTER_H` | R | Current panel line [9:8] |
| `$0D` | `RASTER_CMP_L` | R/W | Compare line [7:0] |
| `$0E` | `RASTER_CMP_H` | R/W | Compare line [9:8] |
| `$0F` | `IRQ_EN` | R/W | b0 vblank, b1 raster compare |
| `$10` | `IRQ_STATUS` | R/W1C | Same bit assignment; write 1 to clear |

`TEXT_START` and `CURSOR_POS` are latched on write to the high byte, so the low/high pair is applied atomically. **Software must write low then high.**

---

## 10. Hardware Cursor

`CURSOR_CTRL` (`$FE:FF08`):

| Bits | Field | Values |
|---|---|---|
| 1:0 | `SHAPE` | 0 = full block, 1 = underline (rows 14–15), 2 = half block (rows 8–15), 3 = vertical bar (column 0) |
| 3:2 | `BLINK` | 0 = steady, 1 = 1 Hz, 2 = 2 Hz, 3 = reserved |
| 4 | `ENABLE` | 1 = visible |

The cursor is rendered by **swapping foreground and background palette indices** for the affected pixels of the cell at `CURSOR_POS`. It consumes no text buffer storage and requires no save/restore of the underlying cell by software — a persistent source of complexity in software-drawn consoles.

Blink is derived from the vertical sync counter.

`CURSOR_POS` is a **buffer-absolute** cell index, not a screen-relative one. It therefore scrolls with the text when `TEXT_START` advances, which is the correct behaviour for the cell the cursor is attached to. A terminal that wants the cursor to remain on the last visible row must update `CURSOR_POS` when it scrolls — one register write.

Estimated cost: ~60 LUT.

---

## 11. Palette

16 entries, 18-bit RGB666, held in flip-flops (288 FF).

Four bytes per entry at `$FE:3000 + (index × 4)`:

| Byte | Contents |
|---|---|
| 0 | `R[5:0]` in bits 5:0 |
| 1 | `G[5:0]` in bits 5:0 |
| 2 | `B[5:0]` in bits 5:0 |
| 3 | Reserved, write zero |

The four-byte stride wastes 16 bytes but makes the address arithmetic a shift. The full 18-bit value reaches the panel unmodified — 262,144 selectable colours per entry.

**Default palette** is initialised in the bitstream to the standard 16-colour CGA/EGA set, so the display is usable at reset with no software involvement.

---

## 12. Font

### 12.1 Format

256 glyphs × 16 bytes = 4096 bytes. Byte `n` of a glyph is the pixel row `n`, MSB = leftmost pixel, `1` = foreground.

Address of glyph row: `(code << 4) | row`.

### 12.2 Two-tier residency

**Default font — initialised in the bitstream.** The system can print from the instant FPGA-B configuration completes, before the RP2040 has mounted the microSD card, before the kernel exists, and before the 65816 has been released from reset. This is not a convenience; it is the only way to get boot and bring-up diagnostics onto the screen.

**Replaceable at runtime.** The kernel may overwrite any part of `$FE:2000`–`$FE:2FFF` from a font file on the microSD card. Writes go to EBR port B while scanout reads port A.

> Writing the font outside of vertical blanking may produce a single-frame artefact on the glyph row being fetched at that instant. This is harmless, but a kernel loading a full font should do so during vblank (`STATUS.VBLANK`) or accept one frame of tearing.

### 12.3 Bitstream initialisation of the text buffer

The text buffer EBR is likewise initialised at synthesis: every cell set to code `$20` (space) with attribute `$0F` (light grey on black). The screen is therefore blank and correctly coloured at power-on rather than displaying random EBR contents.

---

## 13. Resource Estimate

Provisional, pending synthesis. iCE40 HX8K provides 7680 LUT and 32 EBR.

| Block | LUT (est.) |
|---|---|
| Timing generator | 120 |
| Character generator pipeline | 200 |
| Hardware cursor | 60 |
| Palette and output stage | 150 |
| Aperture / CPU bus interface | 250 |
| Register file, IRQ logic | 120 |
| **Mode 0 total** | **~900** |

Roughly 12% of the device. The remainder is available for the SDRAM controller, blitter, sprites, and audio — the blocks whose combined estimate (~7300 LUT) was previously identified as the binding constraint on Neon. **LUT, not bandwidth, remains the limiting resource for Neon as a whole**, but Mode 0 is not where the pressure is.

---

## 14. CPU Interface Summary

| Property | Value |
|---|---|
| Location | Bank `$FE`, 64 KB aperture |
| Read access | **Supported, zero wait states** |
| Write access | Supported, zero wait states |
| Wait-state mechanism | None required |
| Inter-FPGA signalling | None required |
| Bank capture | Latched from D[7:0] during PHI2 low |

The bank byte is captured by FPGA-B from the CPU data bus during the low phase of PHI2, as the 65816 emits it there. This avoids eight dedicated bank lines and is what keeps the FPGA-B pin count inside the TQ144 budget.

> **Open item O-4.** An interrupt line from FPGA-B to the 65816 `IRQB` is required for the vblank and raster-compare interrupts of §9. This was not included in the earlier FPGA-B pin budget (~99 of ~107 usable I/O). It adds one pin. Confirm whether this line goes directly to the CPU or is routed through FPGA-A for masking; the latter reintroduces an inter-FPGA signal, which the architecture has otherwise eliminated.

---

## 15. Verification Criteria (stage N0)

Mode 0 is accepted when all of the following hold:

1. Panel syncs and displays a stable image at 59.76 Hz with no visible tearing or jitter.
2. At power-on, before any CPU activity, the screen shows a blank field in the default background colour with correct borders.
3. RP2040 (or a test harness) writing to the aperture produces correct glyphs at correct positions.
4. Reading back the text buffer through the aperture returns exactly what was written.
5. Advancing `TEXT_START` by 128 scrolls the display by exactly one row with no artefacts.
6. The hardware cursor appears at `CURSOR_POS`, in the selected shape, blinking at the selected rate.
7. A font written through the aperture takes effect on the following frame.
8. `RASTER_L/H` read back plausible values that track the display; the vblank interrupt fires once per frame.

Criteria 1–2 are achievable **before the 65816 is populated**, using the RP2040 alone. This is the intended E0 bring-up path.

---

## 16. Open Items

| ID | Item | Blocks |
|---|---|---|
| **O-1** | Confirm panel is RGB-TTL, not eDP. Obtain part number. | Schematic capture |
| **O-2** | Replace provisional blanking values with datasheet figures. | Schematic capture |
| **O-3** | Font glyph count (256 vs 128) and EBR reuse across modes. | Blitter design |
| **O-4** | IRQ line routing: FPGA-B → CPU direct, or via FPGA-A. | Pin assignment |
| **O-5** | Backlight PWM: driven by FPGA-B or RP2040? | Power design |
| **O-6** | Reset and configuration order — FPGA-B must be configured and displaying before the 65816 leaves reset. | RP2040 firmware |

---

## 17. Decisions Superseded by This Document

Recorded so that earlier notes are not applied by mistake.

| Superseded | Replaced by |
|---|---|
| APS6404L PSRAM as Neon local memory | Dedicated W9825G6KH SDRAM (and Mode 0 uses neither) |
| ANX6345 eDP bridge, ≥14" panel | 10.1" 1024×600, RGB-TTL direct drive (pending O-1) |
| 128×40 grid, 8×15 cell | 128×32 grid, 8×16 cell |
| Text buffer of 10 KB in video memory | 8 KB in EBR |
| "A 16-bit cell write is a single bus access" | One instruction, **two** bus cycles |
| Framebuffer larger than 64 KB in one aperture window | `VRAM_PAGE` register (graphics modes only) |
