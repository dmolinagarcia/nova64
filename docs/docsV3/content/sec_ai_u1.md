# The display panel interface
> LVDS, not eDP · half resolution as a viability condition · two serializers on one bus

This sheet is the physical boundary between Neon and the screen: signalling standard, channel topology, colour depth at the pins, the logical geometry the framebuffer is rendered at, and the scanout bandwidth that follows from all of it. The register side of colour is [sheet T2](sec_ai_t2); the engine behind the framebuffer is [sheet T3](sec_ai_t3). It withdraws the eDP bridge direction, and it is written against a laptop-class panel rather than the 10.1-inch module the rest of the document still assumes — **which is a conflict with [D05](sec_ai_q#d05) that this sheet raises and does not settle** ([U1.28](sec_ai_u1#u128)).

## The interface — LVDS, and the bridge path closes behind it.

- U1.1 — **The panel interface is LVDS, in its FPD-Link / OpenLDI form, and eDP is withdrawn** (→ [D70](sec_ai_q#d70)). The eDP direction was never really a decision about eDP: it was a decision about one bridge chip, and the chip does not survive examination.
- U1.2 — **Two findings withdraw it, and either one alone would be enough.** The ANX6345 has **no franchised distribution channel** — brokers and residual stock only, which is incompatible with a project that states longevity as a requirement. And **the category has no hand-solderable member**: the nearest actively distributed substitute is supplied in a 5 × 5 mm 0.5 mm-pitch BGA, and BGA is this project's single declared package restriction ([D01](sec_ai_q#d01)). Every RGB-to-eDP bridge surveyed shares that constraint, because eDP is a serial interface designed for tablet and ultrabook form factors.
- U1.3 — **LVDS inverts every one of those properties, which is the whole argument.** The interface is **frozen** — no link training, no AUX channel, no capability negotiation, just fixed 7:1 serialization of parallel RGB at seven times the pixel clock. The **panel supply is deep and multi-sourced**, with industrial parts in active production carrying published availability commitments and a large secondary market behind them. **No proprietary silicon sits in the critical path**: the serializer is a mature TSSOP part with second sources. And **most LVDS panels carry no EDID at all**, where eDP mandates it over AUX, so timings come from the datasheet and are fixed in RTL rather than discovered.
- U1.4 — **What LVDS is, in one item, because the two halves get conflated.** The electrical half is a current-mode driver injecting about 3.5 mA into a 100 Ω-terminated differential pair, a swing of roughly 350 mV about a 1.2 V common mode — low power, low emission, and coupled noise rejected as common mode. The protocol half, FPD-Link, exists because a 24-bit parallel RGB bus needs 28 single-ended signals switching at pixel rate, which is unroutable through a hinge. It serializes those onto three or four data pairs plus a clock pair, seven bits per pair per pixel-clock period. **The clock pair carries the pixel clock, not the bit clock**; the receiver multiplies by seven internally.

| Configuration | Pairs | Bits | Payload |
|---|---|---|---|
| 18-bit, single channel | 3 data + 1 clock | 21 | RGB666 plus sync and data enable |
| 24-bit, single channel | 4 data + 1 clock | 28 | RGB888 plus sync and data enable, one spare |

## Half resolution — a viability condition, and the word is chosen deliberately.

- U1.5 — **Neon's own SDRAM delivers roughly 140 to 170 MB/s usable; the table below budgets against 155.** Framebuffer depth is 8 bpp indexed throughout, which is the assumption [T3.10](sec_ai_t3#t310) argued for and [T1.32](sec_ai_t1#t132) left open.

| Mode | Framebuffer pixels | Scanout | Share of 155 MB/s |
|---|---|---|---|
| 1024 × 600 full — the current 10.1″ prototype | 614 K | 36.9 MB/s | ~24 % |
| 1366 × 768 full | 1,049 K | 62.9 MB/s | ~41 % |
| **1366 × 768 half → 680 × 384** | **262 K** | **15.7 MB/s** | **~10 %** |
| 1920 × 1080 full | 2,074 K | 124.4 MB/s | ~80 % |
| **1920 × 1080 half → 960 × 540** | **518 K** | **31.1 MB/s** | **~20 %** |

- U1.6 — **The saving is fourfold rather than twofold, and the second factor is the one that is easy to miss.** The panel is unchanged; only the framebuffer contents and the read pattern change. **Horizontal duplication** reads one framebuffer byte every two pixel clocks out of the line buffer and generates no additional SDRAM traffic either way. **Vertical duplication** displays each framebuffer line on two consecutive scanlines, so the scanout DMA fetches each line **once** and the line buffer serves it **twice**. Half the pixels per line, fetched for half as many lines.
  NOTE: This is why line numbers in [T2.12](sec_ai_t2#t212) are logical rather than physical. With vertical duplication active, logical line *n* is panel lines 2*n* and 2*n*+1, and every register that names a line names the framebuffer's.
- U1.7 — **Full-resolution operation is not viable at either candidate size, and that is what makes this a condition rather than an optimisation.** Committing 41 % or 80 % of total SDRAM bandwidth permanently to refreshing the screen leaves a machine that cannot draw. Against that, **a Full HD panel at half resolution consumes less scanout bandwidth than the current 10.1-inch prototype at full resolution** — which is the sentence to keep, because it means moving to a larger screen makes the memory system easier rather than harder.
- U1.8 — **Three second-order effects follow, and the third contradicts a proven bound in [sheet T3](sec_ai_t3).** Framebuffer and per-window backing stores shrink by four, so more windows fit and each recomposition moves a quarter of the bytes. Blitter work per operation drops by four, which makes full-screen recomposition cheap enough to be the fallback [T3.31](sec_ai_t3#t331) wants it to be. And **the scanout DMA now has two line periods to fetch half the data**, improving underrun margin roughly fourfold — which the source note reads as permission to raise the blitter's maximum burst from 16 to 64 words or more, for better page-hit efficiency.
  NOTE: [[!blocking]] **That last one cannot simply be taken.** [T3.26](sec_ai_t3#t326) places the CPU above the blitter and bounds its read stall by *one burst*, and [T3.27](sec_ai_t3#t327)'s 23-clock worst case is computed with an eight-word in-flight burst that cannot be preempted. **A 64-word burst multiplies the non-preemptible term by eight**, which moves both the scanout stall and the CPU read stall that [D37](sec_ai_q#d37) depends on. The scanout FIFO has the margin to absorb it; the PHI2 stall is the one to check (→ [Q146](sec_ai_q#q146)).
- U1.9 — **The explicit non-claim: half resolution does not improve GUI responsiveness directly.** The binding constraint on Neon is CPU-side command emission, not memory bandwidth — the same point as [T1.2](sec_ai_t1#t12) and [T3.3](sec_ai_t3#t33). What half resolution achieves is **the removal of bandwidth from the constraint set entirely**, which frees headroom for the server-side string cache and the pre-built per-window sublists of [sheet V](sec_ai_v) that do address the real bottleneck. Recorded because a fourfold saving invites the wrong conclusion.

## Logical geometry — and one of the two candidates needs arithmetic.

- U1.10 — **Full HD halves cleanly: 960 × 540.** 960 is divisible by 8, 16, 32 and 64, so blitter block operations land on word boundaries naturally, character cells align exactly, and the two-position shifter of [T3.10](sec_ai_t3#t310) is never fighting misalignment. No adjustment is required.
- U1.11 — **HD does not, because 1366 / 2 = 683 is prime.** An odd active width breaks two things at once: line addressing needs a multiply rather than a shift, and with 8 bpp pixels on a 16-bit SDRAM each line begins on a different byte parity, forcing the shifter into constant misalignment. **The mitigation is to render 680 columns rather than 683** — 680 = 8 × 85, byte-aligned — and leave the six surplus panel columns black, split three and three at the edges. At roughly 0.22 mm per panel pixel that is a 0.7 mm border per side.
- U1.12 — **Stride is fixed at 1024 bytes per line in every mode, regardless of active width** (→ [D71](sec_ai_q#d71)). Line address becomes `base + (y << 10)` with no multiplier; 1024 bytes is 512 SDRAM words, which aligns to row boundaries and improves the page-hit rate. The waste is 344 bytes per line — 186 KB at Full HD, 132 KB at HD, out of 64 MB.

| Mode | Framebuffer footprint |
|---|---|
| 960 × 540, stride 1024 | 553 KB |
| 680 × 384, stride 1024 | 393 KB |

- U1.13 — **Scanout uses a full double line buffer, not a small elasticity FIFO**, and the difference is structural rather than a matter of depth. The DMA fetches line *n+1* as one long burst sequence while line *n* is displayed, which converts scanout from a constant trickle competing with every other client into **one efficient burst per line followed by a long uninterrupted window for the blitter.**

| Mode | Line buffer, double | Share of HX8K block RAM |
|---|---|---|
| 960 × 540 | 1,920 B | ~12 % |
| 680 × 384 | 1,360 B | ~8 % |
| 1366 × 768 full, for comparison | 2,732 B | ~17 % |

  NOTE: [T1.11](sec_ai_t1#t111) budgets 1 KB in two blocks for a double line buffer at 1024 pixels. The Full HD figure above is larger, and it lands on the same block RAM total that [T2.6](sec_ai_t2#t26) and [T3.38](sec_ai_t3#t338) already cannot close. **It is the same open question, with a third claimant** ([Q133](sec_ai_q#q133)).
- U1.14 — **Every panel timing parameter is an RTL generic, never a constant** (→ [D71](sec_ai_q#d71)). Horizontal and vertical total, active, front porch, sync width and back porch all come from a constants package. **Changing panels must be a package change and a rebuild, never an RTL edit** — this is the whole mechanism by which panel obsolescence is contained at the interface rather than spreading into the design.

## Pixel clock and channel topology — the pin problem, and the trick that dissolves it.

| Panel | Pixel clock | Channels |
|---|---|---|
| 1366 × 768 at 60 Hz | ~72 MHz | Single |
| 1920 × 1080 at 60 Hz | 148.5 MHz | **Dual** — the single-channel ceiling is ~112 MHz |

- U1.15 — **Dual-channel LVDS conventionally requires the source to emit two pixels per clock**, which is 48 bits of RGB888 plus syncs plus clock: 52 pins. Against a TQ144 with roughly 107 usable I/O, of which about 38 go to SDRAM and 32 to the Helium link, **this does not fit and no amount of budgeting makes it fit.**
- U1.16 — **With horizontal duplication the even and odd pixel of each pair are identical by construction, and dual-channel LVDS splits precisely by even and odd pixel. Therefore both channels carry the same data stream.** A single 18-bit RGB bus is wired in parallel to two identical serializers, which receive the same data, clock and syncs and produce identical in-phase outputs — exactly what a dual-channel panel fed duplicated pixels requires (→ [D72](sec_ai_q#d72)).

| Consequence | Value |
|---|---|
| Neon pins spent on video | **22**, identical to the single-channel case |
| Neon's internal pixel clock | **74.25 MHz**, not 148.5 |
| Cost of horizontal duplication in logic | **zero** — it happens in the PCB wiring |
| Added BOM | one further serializer, about €3 |

  NOTE: [[!blocking]] **74.25 MHz is not a clock this document has.** Every figure in [sheet T1](sec_ai_t1) derives from a 103.125 MHz core and a 51.5625 MHz pixel clock out of the 25 MHz oscillator ([T1.4](sec_ai_t1#t14), [T1.5](sec_ai_t1#t15), [T1.8](sec_ai_t1#t18)), and [T3.13](sec_ai_t3#t313) computes the arbiter and the memory baseline against the same 103.125. **Whether 74.25 MHz is reachable from the 25 MHz star at all has not been checked**, and if it is, the single-domain property of [T1.8](sec_ai_t1#t18) — no clock-domain crossing anywhere inside Neon — is what would have to be re-argued, since the memory system would no longer be an exact multiple of the pixel rate (→ [Q137](sec_ai_q#q137)).
- U1.17 — [[!blocking]] **The panel's channel split must be even/odd pixel interleave, and this is a selection criterion rather than a detail.** It is standard for laptop panels and most industrial panels, but **some large-format panels split by left half and right half instead.** On such a panel the shared-bus technique does not apply at all: the two channels would carry genuinely distinct streams, the pin count returns to 52, and the panel is unusable in this design.
- U1.18 — **Because horizontal duplication lives in the wiring on a dual-channel panel, horizontal resolution is fixed in hardware and cannot be switched at runtime.** Vertical duplication stays a register bit, so a Full HD panel offers 960 × 540 with square logical pixels at ~20 % of bandwidth, or 960 × 1080 at a 2:1 logical aspect and ~40 %. **On a single-channel HD panel horizontal duplication stays inside the FPGA and both axes remain switchable.** That is a genuine trade — maximum resolution against mode flexibility — and it is decided by the panel rather than by the gateware.
- U1.19 — **Two SN75LVDS83B in TSSOP-56, fed identically, are preferred over a purpose-built dual-channel part.** Identical devices driven from an identical bus and clock are trivially in phase, are simpler to reason about during bring-up, and are individually second-sourced by the Thine THC63LVDM83D. Both are hand-solderable. At 74.25 MHz the per-pair rate is 519.75 Mbps, well inside the part's 135 MHz pixel-clock rating.

## Output colour depth — RGB666 at the pins, and the asymmetry that settles it.

- U1.20 — **The Neon pin boundary carries RGB666** (→ [D72](sec_ai_q#d72)). Internal storage stays 24-bit and truncation happens in the output pad register, which is [T2.3](sec_ai_t2#t23) and [T2.10](sec_ai_t2#t210) from the hardware side.

| | RGB888 | RGB666 | RGB444 |
|---|---|---|---|
| FPGA pins, data plus sync, data enable and clock | 28 | **22** | 16 |
| LVDS pairs, single channel | 5 | **4** | 4 |
| LVDS pairs, dual channel | 10 | **8** | 8 |
| Output colour space | 16.7 M | **262,144** | 4,096 |
| Available greys | 256 | **64** | 16 |
| Scanout bandwidth | — | **no change** | no change |
| CLUT block RAMs, dual bank | 3 | **3** | 2 |

- U1.21 — **Output depth has no effect whatsoever on scanout bandwidth, and this is recorded explicitly because it is an assumption comfortable to make backwards.** Traffic is set by framebuffer depth, not interface depth: each logical pixel is one byte in SDRAM, a palette index, and that is what the DMA reads. Expansion to real colour happens in the lookup table, downstream of the line buffer, in the pixel clock domain. **SDRAM does not participate.**
- U1.22 — **RGB444 is rejected on grey ramp granularity rather than on colour count.** It would save six pins and one block RAM and is defensible under a deliberately flat visual style, but it offers 16 greys in the entire output space — a quantisation step of about 6.7 % per channel against RGB666's 64 greys at 1.6 %. Short gradients, glyph edges and soft shadows band visibly at 6.7 %, and **having 256 palette entries does not help**: if the output space contains only 16 greys there are no intermediate values to put in the entries.
  NOTE: The Amiga comparison — 4,096 candidates producing well-regarded graphics — holds in a different regime: 32 simultaneous colours rather than 256, output to a CRT whose soft pixel edges mask quantisation, under a flat hard-edged art style with no antialiasing.
- U1.23 — **The asymmetry is what actually decides it.** Routing 18 bits and later preferring 12 is a one-line RTL change tying the low bits low. Routing 12 and later wanting 18 is a board respin. **RGB666 preserves both options for six pins, and those six pins are available** — and RGB444 survives as a software mode through [T2.10](sec_ai_t2#t210)'s truncation field.
- U1.24 — **The unused-bit hazard, which is a bring-up trap rather than a design one.** In FPD-Link the fourth pair of each channel carries the top two bits of each component. With that pair unconnected those bits are undefined at the receiver: most panels tie them low internally, **some leave them floating**, and the symptom is noise in the low colour bits rather than an obvious failure. Verify per panel.

## The boundary — 22 pins out of Neon, wired in parallel to both serializers.

| Signal | Width | Notes |
|---|---|---|
| `VID_R[7:2]` · `VID_G[7:2]` · `VID_B[7:2]` | 18 | RGB666; the low two bits of each component are not routed |
| `VID_HS` · `VID_VS` | 2 | Sync, polarity per panel |
| `VID_DE` | 1 | Data enable |
| `VID_PCLK` | 1 | 74.25 MHz dual-channel Full HD, or ~72 MHz single-channel HD |
| **Total** | **22** | |

- U1.25 — **The serializer's own configuration is strapped, not driven** — colour mapping select, clock edge select, and a power-down tied to the panel enable domain. Per channel the serializer emits three data pairs and one clock pair, so **four pairs and eight conductors per channel, eight pairs and sixteen conductors for a dual-channel panel.** The clock pair carries the pixel clock ([U1.4](sec_ai_u1#u14)).
  NOTE: **The panel's supply and backlight are the EC's, not Neon's**, which is [T1.52](sec_ai_t1#t152) applied unchanged: the logic supply is gated by the RP2354B, the panel enable is sequenced ahead of the backlight, and backlight enable, dimming and the 20–40 V LED string all belong to the power tree of [sheet C](sec_ai_c) rather than to this interface.

## Panel selection — six criteria that disqualify, two that only warn.

| # | Criterion | Blocking |
|---|---|---|
| C1 | LVDS in FPD-Link / OpenLDI form | Yes |
| C2 | If dual-channel: **even/odd pixel interleave**, not left/right half ([U1.17](sec_ai_u1#u117)) | Yes |
| C3 | LED backlight, not CCFL | Yes |
| C4 | 3.3 V logic supply | Yes |
| C5 | Colour mapping — VESA or JEIDA — documented in the datasheet | Yes |
| C6 | Complete timing parameters published | Yes |
| C7 | Behaviour of unused colour bits documented or verified ([U1.24](sec_ai_u1#u124)) | No — verify at bring-up |
| C8 | Published lifecycle commitment, industrial grade preferred | No — the prototype may use replacement stock |

- U1.26 — **Two distinct markets exist and the strategy uses both.** Laptop replacement panels from roughly 2007 to 2014 are cheap and abundant but are **declining, non-replenished stock**, since laptops migrated to eDP around 2013; HD stock runs deeper than Full HD. Industrial LVDS panels are actively produced with published availability, wider temperature range and higher brightness at three to five times the cost, and **Full HD dual-channel is currently their strongest format** — better long-term availability than HD, which inverts the used-market picture. **Prototype on a cheap replacement panel, specify an industrial one for production**; they are electrically the same interface, so migration is a timing-constant change under [U1.14](sec_ai_u1#u114).
- U1.27 — **The colour mapping hazard deserves its own item because of how it fails.** Two incompatible schemes distribute the bits of each component across the seven-bit lanes: VESA/PSWG and JEIDA/OpenLDI. Choosing wrong produces **a tinted, posterised image rather than a black screen**, which is far harder to diagnose than a dead link. The correct mapping must be in the datasheet extract before schematic capture and is set by serializer strap.

## Where this sheet disagrees with the rest of the document.

- U1.28 — [[!blocking]] **The panel this sheet is written against is not the panel [D05](sec_ai_q#d05) chose.** D05 fixed a direct RGB-TTL 10.1-inch module with no bridge chip, and [sheet H](sec_ai_h), [T1.4](sec_ai_t1#t14) and [T1.5](sec_ai_t1#t15) derive the clock, the geometry and every mode from it. This sheet's own table still calls that module "the current prototype", so **the plainest reading is that the 10.1-inch panel remains the prototype and LVDS is the production direction** — but nothing states that, and D05's note explicitly keeps eDP as the future variant, which [U1.1](sec_ai_u1#u11) has now closed. **What must not happen is the document carrying two panel decisions and pretending they are one** (→ [Q145](sec_ai_q#q145)).
- U1.29 — [[open]] **The mode question of [Q59](sec_ai_q#q59) is settled by implication here and should be settled explicitly.** This sheet assumes 8 bpp indexed throughout, which is what [T3.10](sec_ai_t3#t310) and [T3.11](sec_ai_t3#t311) argued for and what deletes the 1-bpp barrel shifter; [T1.32](sec_ai_t1#t132)'s Mode 1 at 1 bpp has no place in the geometry above. **Three sheets now assume 8 bpp and one specifies 1 bpp**, and that is a decision to record rather than a consensus to rely on.
- U1.30 — **What this sheet does not touch is worth stating.** The text mode of [T1.1](sec_ai_t1#t11) depends on nothing but the bitstream and reaches the panel without SDRAM, and none of the above changes that — but its 128 × 32 grid at an 8 × 16 cell is derived from 1024 active pixels ([T1.24](sec_ai_t1#t124)), so a different active width is a different grid. **The property survives; the numbers in it do not**, and the console is the one part of Neon that must be correct before anything can be debugged.

## Open items blocking schematic capture.

| # | Item | Blocks |
|---|---|---|
| O1 | Panel diagonal, 14″ against 15.6″ | Chassis, battery, keyboard, moulding ([Q140](sec_ai_q#q140)) |
| O2 | Panel part number and datasheet extract — timings, mapping, channel split, unused-bit behaviour | C2, C5, C6, C7 ([Q141](sec_ai_q#q141)) |
| O3 | Serializer pin assignment against Neon's TQ144 pinout | The video pin block ([Q142](sec_ai_q#q142)) |
| O4 | Backlight boost converter, string voltage and current from the panel datasheet | The backlight block of [sheet C](sec_ai_c) ([Q144](sec_ai_q#q144)) |
| O5 | Whether two independently-clocked serializers on a common clock meet the panel's inter-channel skew specification | [U1.19](sec_ai_u1#u119) ([Q143](sec_ai_q#q143)) |
| O6 | Panel connector — 30-pin against 40-pin, FFC against board-to-board | Mechanical and FPC routing ([Q144](sec_ai_q#q144)) |

## Abandonment conditions.

| Condition | Action |
|---|---|
| No candidate panel satisfies C2 at the chosen diagonal | Fall back to a single-channel HD panel at 680 × 384. **Do not reopen eDP** |
| O5 resolves negatively and dual discrete serializers cannot meet skew | Re-evaluate a purpose-built dual-channel part, subject to the BGA restriction; if none qualifies, fall back to single-channel HD |
| The six pins for RGB666 over RGB444 are unavailable once Neon's pin budget closes | Drop to RGB444 routing, accept the flat visual style as a project decision, and record it |
| Industrial panel cost proves prohibitive at the chosen diagonal | Ship with replacement stock, buy a lifetime quantity of three to five units, and record the panel as a known end-of-life dependency |

## Verification — at bring-up, and the first two come before any gateware.

- [ ] U1.31 — **The panel's own built-in test pattern displays** with no external clock and no bitstream, separating a panel, FPC or backlight fault from a logic fault before either can be blamed on the other ([T1.64](sec_ai_t1#t164)).
- [ ] U1.32 — **Continuity and mapping strap check** against the datasheet extract, before power is applied to the panel.
- [ ] U1.33 — **The panel syncs and holds a stable image** at the datasheet timings with the serializers fed from the shared bus.
- [ ] U1.34 — **Both channels are in phase**: a full-screen single-colour field shows no vertical seam at the channel boundary.
- [ ] U1.35 — **A grey ramp shows 64 distinct steps**, confirming RGB666 reaches the panel and that the unused pair is not floating ([U1.24](sec_ai_u1#u124)).
- [ ] U1.36 — **Vertical duplication toggles at runtime** and the logical line counter of [T2.12](sec_ai_t2#t212) tracks the framebuffer rather than the panel.
