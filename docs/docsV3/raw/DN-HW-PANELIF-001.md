# DN-HW-PANELIF-001 — Display Panel Interface

**Project:** noVa64
**Domain:** Hardware
**Status:** Active
**Revision:** A

---

## 1. Revision history

| Rev | Status | Summary |
|-----|--------|---------|
| A | Active | Initial issue. Establishes LVDS as the target panel interface, supersedes the eDP direction of the Phase 2 panel decision. Defines half-resolution scanout, shared-bus dual-channel topology, and RGB666 pin routing. |

---

## 2. Purpose and scope

This note defines the physical interface between NEON (FPGA-B) and the system display panel: signalling standard, channel topology, colour depth at the pin boundary, logical geometry, and the scanout bandwidth budget that follows from those choices.

**In scope:** panel signalling, serializer topology, pin and pair counts, logical resolution, framebuffer geometry, scanout bandwidth, panel selection criteria, backlight interface.

**Out of scope:** the CLUT register contract and palette semantics (see `DN-GPU-CLUT-001`), compositor design, blitter architecture, chassis mechanics.

**Supersedes:** the previously assumed Phase 2 direction of "14-inch panel with ANX6345 eDP bridge". That direction is withdrawn; see §4.

---

## 3. Decisions

| # | Decision | Section |
|---|----------|---------|
| D1 | Panel interface is LVDS (FPD-Link / OpenLDI), not eDP. | §4 |
| D2 | Framebuffer is rendered at half the panel's native resolution in both axes; pixels are duplicated on output. | §5, §6 |
| D3 | Where the panel requires dual-channel LVDS, both channels are driven from a single shared RGB bus. | §7 |
| D4 | The NEON pin boundary carries RGB666. Internal colour storage remains 24-bit. | §8 |
| D5 | Framebuffer stride is fixed at 1024 bytes per line regardless of active width. | §6 |
| D6 | Panel timings are RTL generics, never constants. | §6 |

---

## 4. Rationale for LVDS over eDP (D1)

The eDP direction was predicated on the ANX6345 bridge. Two findings withdraw it:

1. **The ANX6345 has no franchised distribution channel.** It is available only through brokers and residual stock. This is incompatible with the project's stated longevity requirement.
2. **The category has no hand-solderable member.** The nearest actively distributed substitute, Toshiba TC358767AXBG, is supplied in P-VFBGA81 (5 × 5 mm, 0.50 mm pitch). BGA is the project's single declared package restriction. Every RGB-to-eDP bridge surveyed shares this constraint, because eDP is a high-speed serial interface designed for tablet and ultrabook form factors.

LVDS inverts every one of these properties:

- **The interface is frozen.** No link training, no AUX channel, no DPCD, no capability negotiation. Fixed 7:1 serialization of parallel RGB at 7× pixel clock.
- **The panel supply is deep and multi-sourced.** Industrial LVDS panels remain in active production with published 5–10 year availability commitments, and a large secondary market exists in laptop replacement panels.
- **No proprietary silicon sits in the critical path.** The serializer is either a mature TSSOP part with second sources, or absent entirely if the FPGA drives LVDS natively.
- **Most LVDS panels carry no EDID.** Unlike eDP, where EDID over AUX is mandatory. Timings are taken from the datasheet and fixed in RTL, eliminating discovery logic.

### 4.1 LVDS in one paragraph, for the record

LVDS (ANSI/TIA/EIA-644) is an electrical signalling technique: a current-mode driver injects approximately 3.5 mA into a differential pair terminated in 100 Ω, producing a swing of roughly 350 mV about a 1.2 V common mode. Low swing yields low power and low EMI; the fields of the two conductors largely cancel; coupled noise affects both conductors equally and is rejected as common mode.

FPD-Link is the protocol layered on top. It exists because a 24-bit parallel RGB bus requires 28 single-ended signals switching at pixel rate — unroutable through a hinge. FPD-Link serializes those 28 bits onto 3 or 4 differential data pairs plus one clock pair, each data pair carrying 7 bits per pixel-clock period. The clock pair carries the **pixel** clock; the receiver multiplies it by 7 internally.

| Configuration | Pairs | Bits | Payload |
|---|---|---|---|
| 18-bit, single channel | 3 data + 1 clock | 21 | RGB666 + HS/VS/DE |
| 24-bit, single channel | 4 data + 1 clock | 28 | RGB888 + HS/VS/DE + 1 spare |

---

## 5. Scanout bandwidth budget (D2)

NEON's dedicated SDRAM delivers approximately 140–170 MB/s usable. The figures below use 155 MB/s as the nominal reference. Framebuffer depth is 8 bpp indexed throughout.

| Mode | Framebuffer pixels | Scanout | % of 155 MB/s |
|---|---|---|---|
| 1024×600, full (current 10.1" prototype) | 614 K | 36.9 MB/s | ~24% |
| 1366×768, full | 1,049 K | 62.9 MB/s | ~41% |
| **1366×768, half → 680×384** | **262 K** | **15.7 MB/s** | **~10%** |
| 1920×1080, full | 2,074 K | 124.4 MB/s | ~80% |
| **1920×1080, half → 960×540** | **518 K** | **31.1 MB/s** | **~20%** |

### 5.1 Why the saving is fourfold, not twofold

The panel is unchanged; only the framebuffer contents and the read pattern change.

- **Horizontal duplication** reads one framebuffer byte every two pixel-clock cycles from the line buffer. It generates no additional SDRAM traffic in either case.
- **Vertical duplication** displays each framebuffer line on two consecutive scanlines. The scanout DMA therefore fetches each line **once** and the line buffer serves it **twice**.

The second effect is what multiplies the saving. Half the pixels per line, fetched for half as many lines.

### 5.2 Consequences

- A Full HD panel at half resolution consumes **less** scanout bandwidth than the current 10.1" prototype panel at full resolution.
- Full-resolution operation is not viable at either candidate size: 41% and 80% of total SDRAM bandwidth permanently committed to refreshing the screen.
- Half resolution is therefore a **viability condition**, not an optimisation.

### 5.3 Second-order effects

- Framebuffer and per-window backing stores shrink by 4×, so more windows fit and each recomposition moves a quarter of the bytes.
- Blitter work per operation drops by 4×. Full-screen recomposition as a fallback becomes cheap.
- The scanout DMA has **two line periods** to fetch half the data. Underrun margin improves by roughly 4×, which permits raising the blitter's maximum burst length from 16 to 64 words or more. Longer bursts improve SDRAM page-hit efficiency, so real usable bandwidth rises above the nominal figure.

### 5.4 Explicit non-claim

Half resolution does **not** improve GUI responsiveness directly. The binding constraint on NEON is CPU-side command emission (~50 cycles per 12-byte command), not memory bandwidth. What half resolution achieves is the removal of bandwidth from the constraint set entirely, freeing headroom for the server-side string cache and pre-built per-window command sublists that do address the real bottleneck.

---

## 6. Logical geometry and framebuffer layout (D2, D5, D6)

### 6.1 Full HD panel: 960 × 540 logical

960 = 2⁶ × 15. Divisible by 8, 16, 32, and 64. No adjustment is required: blitter block operations land on word boundaries naturally, character cells align exactly, and the blitter's 2-position shifter is never fighting misalignment.

### 6.2 HD panel: 680 × 384 logical

1366 / 2 = 683, which is **prime**. An odd active width breaks two things simultaneously: line addressing requires a multiply rather than a shift, and with 8 bpp pixels on a ×16 SDRAM each line begins on a different byte parity, forcing the 2-position shifter into constant misalignment.

Mitigation: render **680** columns, not 683. 680 = 8 × 85, therefore divisible by 8 and byte-aligned. The 6 surplus panel columns (1366 − 1360) are left black, split 3 and 3 at the edges. At approximately 0.22 mm per panel pixel this is a 0.7 mm border per side.

### 6.3 Stride

Framebuffer stride is **1024 bytes per line** in all modes, regardless of active width.

- Line address is `base + (y << 10)`. No multiplier.
- 1024 bytes = 512 SDRAM words, which aligns cleanly to row boundaries and improves page-hit rate.
- Waste: 344 bytes/line × 540 lines = 186 KB (FHD), or 344 × 384 = 132 KB (HD), out of 64 MB. Immaterial.

| Mode | Framebuffer footprint |
|---|---|
| 960 × 540, stride 1024 | 553 KB |
| 680 × 384, stride 1024 | 393 KB |

### 6.4 Line buffer

Scanout uses a **full double line buffer** in EBR, not a small elasticity FIFO. The DMA fetches line *n+1* as a long burst sequence while line *n* is displayed. This converts scanout from a constant trickle competing with every other client into one efficient burst per line followed by a long uninterrupted window for the blitter.

| Mode | Line buffer (double) | % of HX8K EBR (16 KB) |
|---|---|---|
| 960 × 540 | 1,920 bytes | ~12% |
| 680 × 384 | 1,360 bytes | ~8% |
| 1366 × 768 full (for comparison) | 2,732 bytes | ~17% |

### 6.5 Timings as generics (D6)

All panel timing parameters — htotal, hactive, front porch, sync width, back porch, and vertical equivalents — are RTL generics. Changing panels must be a constant-package change and rebuild, never an RTL edit. This is the mechanism by which panel obsolescence is contained at the interface.

---

## 7. Pixel clock and channel topology (D3)

| Panel | Pixel clock | Channel requirement |
|---|---|---|
| 1366 × 768 @ 60 | ~72 MHz | Single channel |
| 1920 × 1080 @ 60 | 148.5 MHz | **Dual channel** (single-channel ceiling is ~112 MHz) |

### 7.1 The pin problem and its resolution

Dual-channel LVDS conventionally requires the source to emit **two pixels per clock**: 48 bits of RGB888 plus syncs plus clock equals 52 pins. Against a TQ144 device with approximately 107 usable I/O — of which roughly 38 are committed to SDRAM and 32 to the multiplexed Helium link — this does not fit.

**Resolution:** with horizontal duplication, the even and odd pixel of each pair are identical by construction. Dual-channel LVDS splits precisely by even and odd pixel. Therefore **both channels carry an identical data stream**.

A single 24-bit (or 18-bit) RGB bus is wired in parallel to two identical serializers. Both receive the same data, the same clock, and the same syncs, and produce identical, in-phase outputs — exactly what a dual-channel panel with duplicated pixels requires.

Consequences:

- NEON spends **22 pins on video** (RGB666), identical to the single-channel case.
- NEON's internal pixel clock is **74.25 MHz**, not 148.5 — comparable to the ~72 MHz of the HD case, with no fabric timing difficulty on iCE40.
- Horizontal duplication costs zero logic; it occurs in the PCB wiring.
- Added BOM cost is one additional serializer, approximately €3.

### 7.2 Mandatory verification

The panel's channel split **must be even/odd pixel interleave**, which is standard for laptop panels and most industrial panels. Some large-format panels split by **left half / right half** instead. On such a panel the shared-bus technique does not apply: the two channels would require genuinely distinct data streams, returning the pin count to 52 and making the panel unusable in this design.

**This is a panel selection criterion, not a detail.** See §11.

### 7.3 Runtime mode consequence

Because horizontal duplication is realised in the wiring on a dual-channel panel, horizontal resolution is **fixed in hardware** and cannot be switched at runtime. Vertical duplication remains a runtime register bit. Available modes on a Full HD panel:

| Logical mode | Logical pixel aspect | Scanout | % of 155 MB/s |
|---|---|---|---|
| 960 × 540 | Square | 31.1 MB/s | ~20% |
| 960 × 1080 | 2:1 wide | 62.2 MB/s | ~40% |

On a single-channel HD panel, horizontal duplication stays inside the FPGA and both axes remain runtime-switchable. This is a genuine trade: maximum resolution versus mode flexibility.

### 7.4 Serializer selection

Two SN75LVDS83B (TSSOP-56), fed identically, are preferred over a purpose-built dual-channel part. Rationale: identical devices driven from an identical bus and clock are trivially in phase, are simpler to reason about during bring-up, and are individually second-sourced (Thine THC63LVDM83D). Both are hand-solderable.

At 74.25 MHz pixel clock the per-pair rate is 519.75 Mbps, well within the SN75LVDS83B's 135 MHz pixel-clock rating.

---

## 8. Output colour depth (D4)

The NEON pin boundary carries **RGB666**. Internal CLUT storage remains 24-bit; truncation occurs in the output pad register.

### 8.1 What RGB666 costs and buys

| | RGB888 | RGB666 | RGB444 |
|---|---|---|---|
| FPGA pins (data + HS/VS/DE/PCLK) | 28 | **22** | 16 |
| LVDS pairs, single channel | 5 | **4** | 4 |
| LVDS pairs, dual channel | 10 | **8** | 8 |
| Output colour space | 16.7 M | **262,144** | 4,096 |
| Available greys | 256 | **64** | 16 |
| Scanout bandwidth | — | **no change** | no change |
| CLUT EBR blocks (dual bank) | 3 | **3** | 2 |

### 8.2 Bandwidth is unaffected

Output depth has **no effect whatsoever** on scanout bandwidth. Traffic is determined by framebuffer depth, not by interface depth. Each logical pixel occupies one byte in SDRAM — a palette index — and that is what the DMA reads. Expansion to real colour occurs in the CLUT, downstream of the line buffer, in the pixel clock domain. SDRAM does not participate.

This is recorded explicitly because it is an assumption comfortable to make backwards.

### 8.3 Why not RGB444

RGB444 saves 6 further pins and one EBR block, and would be defensible under a deliberately flat visual style. It is rejected on the grounds of **grey ramp granularity**: RGB444 offers 16 greys in the entire output space, a quantisation step of ~6.7% per channel, against RGB666's 64 greys at ~1.6%. Short gradients, glyph edges, and soft shadows band visibly at 6.7%. Having 256 palette entries does not help — if the output space contains only 16 greys, there are no intermediate values to place in the entries.

The Amiga comparison (4,096 candidates producing well-regarded graphics) holds in a different regime: 32 simultaneous colours rather than 256, output to a CRT whose soft pixel edges mask quantisation, under a flat hard-edged art style with no antialiasing.

### 8.4 The asymmetry that settles it

- Routing 18 bits and later preferring 12: tie the low bits low in RTL. One-line change.
- Routing 12 bits and later wanting 18: board respin.

RGB666 preserves both options for 6 pins, and those 6 pins are available. RGB444 remains a **software mode** via the configurable truncation field in `DN-GPU-CLUT-001`.

### 8.5 Unused-bit hazard

In FPD-Link, the fourth pair of each channel carries R6/R7, G6/G7, B6/B7. When that pair is unconnected, those bits are undefined at the receiver. Most panels tie them low internally; **some leave them floating**, producing noise in the low colour bits. Verify per panel.

---

## 9. Boundary signal table: NEON → serializer

Signals below are emitted once and wired in parallel to both serializers on a dual-channel panel.

| Signal | Width | Direction | Notes |
|---|---|---|---|
| `VID_R[7:2]` | 6 | NEON → SER | RGB666; R[1:0] not routed |
| `VID_G[7:2]` | 6 | NEON → SER | RGB666; G[1:0] not routed |
| `VID_B[7:2]` | 6 | NEON → SER | RGB666; B[1:0] not routed |
| `VID_HS` | 1 | NEON → SER | Horizontal sync, polarity per panel |
| `VID_VS` | 1 | NEON → SER | Vertical sync, polarity per panel |
| `VID_DE` | 1 | NEON → SER | Data enable |
| `VID_PCLK` | 1 | NEON → SER | 74.25 MHz (FHD dual) or ~72 MHz (HD single) |
| **Total** | **22** | | |

Serializer static configuration (strapped, not driven):

| Strap | Purpose |
|---|---|
| Colour mapping select | VESA/PSWG or JEIDA/OpenLDI — **per panel, see §11** |
| Clock edge select | Per panel datasheet |
| Power-down | Tied to panel enable domain |

---

## 10. Boundary signal table: serializer → panel

Per channel, RGB666:

| Signal | Pairs | Notes |
|---|---|---|
| `LVDS_D0±` | 1 | Data lane 0 |
| `LVDS_D1±` | 1 | Data lane 1 |
| `LVDS_D2±` | 1 | Data lane 2 |
| `LVDS_CLK±` | 1 | Pixel clock, not bit clock |
| **Per channel** | **4 pairs / 8 conductors** | |
| **Dual channel total** | **8 pairs / 16 conductors** | |

Panel supply and control:

| Signal | Notes |
|---|---|
| `PANEL_VCC` | Typically 3.3 V, 300–600 mA logic supply. Gated by the RP2354B EC per Sheet 1.1 REV C. |
| `PANEL_EN` | Logic enable, sequenced ahead of backlight |
| `BL_EN` | Backlight enable |
| `BL_PWM` | Backlight dimming |
| `BL_V` | LED string supply, 20–40 V from boost converter |

---

## 11. Panel selection criteria

A candidate panel must satisfy all of the following. Items marked **blocking** disqualify the panel outright.

| # | Criterion | Blocking |
|---|---|---|
| C1 | LVDS (FPD-Link / OpenLDI) interface | Yes |
| C2 | If dual-channel: split is **even/odd pixel interleave**, not left/right half (§7.2) | Yes |
| C3 | LED backlight, not CCFL | Yes |
| C4 | 3.3 V logic supply | Yes |
| C5 | Colour mapping (VESA or JEIDA) documented in datasheet | Yes |
| C6 | Complete timing parameters published | Yes |
| C7 | Behaviour of unused colour bits documented or verified (§8.5) | No — verify at bring-up |
| C8 | Published lifecycle commitment (industrial grade preferred) | No — prototype may use laptop replacement stock |

### 11.1 Market note

Two distinct markets exist:

- **Laptop replacement panels (~2007–2014).** Cheap and abundant, but a declining, non-replenished stock — laptops migrated to eDP around 2013–2014. HD (1366×768) stock is deeper than FHD.
- **Industrial LVDS panels.** Actively produced with published availability commitments, extended temperature range, higher brightness, 3–5× the cost. Industrial vendors remained on LVDS long after laptops moved to eDP, and **FHD dual-channel is currently their strongest format** — better long-term availability than HD.

Recommended strategy: prototype on a cheap laptop replacement panel, specify an industrial panel with a published lifecycle for the production revision. Both are electrically the same interface, so migration is a timing-constant change (§6.5).

### 11.2 Colour mapping hazard

Two incompatible schemes exist for distributing 8 bits per colour across 7-bit lanes: **VESA/PSWG** and **JEIDA/OpenLDI**. They differ in where the low bits of each component land. Selecting the wrong one produces a tinted, posterised image — not a black screen, which makes the fault harder to diagnose. The correct mapping must be recorded in the panel datasheet extract before schematic capture, and set by serializer strap.

---

## 12. Open items blocking schematic capture

| # | Item | Blocks |
|---|---|---|
| O1 | Panel diagonal decision (14" vs 15.6") — currently open, gates chassis, battery, keyboard, and moulding | Panel selection, all downstream mechanics |
| O2 | Specific panel part number and datasheet extract (timings, mapping, channel split, unused-bit behaviour) | C2, C5, C6, C7 |
| O3 | SN75LVDS83B pin assignment against NEON TQ144 pinout, pending formal Helium/NEON pin budget | Video pin block |
| O4 | Backlight boost converter selection (string voltage and current from panel datasheet) | Sheet 1.1 backlight block |
| O5 | Confirmation that two independently-clocked SN75LVDS83B devices driven from a common clock meet the panel's inter-channel skew specification | §7.4 |
| O6 | Panel connector selection (30-pin vs 40-pin, FFC vs board-to-board) | Mechanical, FPC routing |

---

## 13. Abandonment conditions

| Condition | Action |
|---|---|
| No candidate panel satisfies C2 (even/odd split) at the chosen diagonal | Fall back to a single-channel HD panel at 680×384. Do not reopen the eDP option. |
| O5 resolves negatively — dual discrete serializers cannot meet skew | Re-evaluate a purpose-built dual-channel serializer, subject to the BGA restriction. If none qualifies, fall back to single-channel HD. |
| The 6 pins required for RGB666 over RGB444 are not available after the formal NEON pin budget closes | Drop to RGB444 routing. Accept the flat visual style as a project decision and record it. |
| Industrial panel cost proves prohibitive at the chosen diagonal | Ship with laptop replacement stock, buy a lifetime quantity of 3–5 units, and record the panel as a known end-of-life dependency in `DN-SYS-LIFECYCLE-001`. |

---

## 14. Cross-references

- `DN-GPU-CLUT-001` — palette register contract, output truncation control, line comparison
- `DN-SYS-LIFECYCLE-001` — component lifecycle register (pending)
- Sheet 1.1 REV C — power rail sequencing, panel and backlight domain gating
