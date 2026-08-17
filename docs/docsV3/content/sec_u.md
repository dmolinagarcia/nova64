# Neon — blitter and compositor
> four channels · 256 logic functions · bank partitioning · backing stores

[Sheet T](sec_t) specifies Neon as a whole and says what the drawing engine *does*; this sheet is the engine itself — its datapath, its arbitration policy, and the window compositor built on it. It comes from a fifth handoff written a day after the other four, and it does two things at once: **it closes three open questions with real numbers, and it proposes a blitter that is not the one sheet T describes.** That second half is stated first, because everything after it is conditional on how it is resolved.

## The conflict — two blitters, and they are different machines, not different register maps.

- U.1 — [[!blocking]] **Sheet T's engine is operation-based; this note's is channel-based.** T's blitter takes `FILL_RECT`, `COPY_KEYED`, `COPY_EXPAND` and the rest, in commands carrying `(x, y)` coordinates, fed from a 128-entry FIFO or a display list in SDRAM ([T.32](sec_t#t32), [D30](sec_q#d30)). This note's is the Amiga blitter: **four memory channels — A, B, C read, D write — combined by any of 256 bitwise logic functions**, addressed by raw pointers and per-line modulos, with edge masks and a descending mode, fed from an 8-deep queue of register snapshots. Both are good designs. They are not variants of one design.
- U.2 — **The capability difference is real and cuts both ways.** The minterm LUT is bitwise, so a **colour key cannot be expressed in it** — an 8-bit equality test is not a boolean function of three bits. The channel model does transparency with a **mask surface** on channel A and logic function `$CA`, which means every sprite carries a second bitmap that something must generate and keep in step. Conversely T's fixed operation set has **no XOR**, so no rubber-band outline and no reversible cursor, and no arbitrary bitwise compositing at all.

| | Sheet T — operations | This note — channels |
|---|---|---|
| Transparency | `COPY_KEYED`, one designated index, no extra memory | Mask surface plus `LF = $CA` |
| Glyphs | `COPY_EXPAND`, 1-bpp atlas expanded on the fly | Mask-and-copy from an 8-bpp atlas, or a pre-expanded one |
| Arbitrary bitwise logic | none | **256 functions**, XOR included |
| Pattern fill | `FILL_PATTERN`, 8 × 8 register | `LF = $F0` from channel A with a modulo of −stride |
| Addressing | `(x, y)`; Neon computes `base + y × stride + x` | Four pointers and four modulos, computed by the CPU |
| Setup per blit | 4–6 command words | ~30 byte stores |
| Clipping | clip rectangle in hardware | expressed through pointer, width, height and modulo |

- U.3 — **The decisive number is setup cost, and it falls out of [D30](sec_q#d30) rather than from either document's advocacy.** At ~30 byte stores per blit, the channel model costs about **19 µs of CPU per blit** at 8 MHz. For the GUI that is irrelevant: a composite pass is a handful of large blits and the setup amortises over hundreds of kilobytes. **For the game scenario of [T.59](sec_t#t59) it is fatal** — 24 sprites is ~450 µs per frame of pointer arithmetic, against the 140 µs that sheet T budgets for *the entire frame*, and the display list cannot help because a list of raw pointers has to be recomputed every time anything moves. This is exactly the work D30 removed by making commands carry coordinates.
  NOTE: The note's own 8-deep command queue exists to mitigate this, and its justification quotes a **65816 at 14 MHz**. This board runs 8 MHz ([B.1](sec_b#b1)), so the figure is worse here than the note assumes, not better.
- U.4 — **There is a third option and it is probably the right one: keep this datapath and put T's command layer on top of it.** The command processor already has to exist for display lists, and it already has the multiplier D30 budgeted; having it generate four pointers and four modulos from a coordinate pair is arithmetic it is already doing. That buys the 256 logic functions, the mask channel and the edge masks **and** coordinate commands, display lists and a bounded per-frame CPU cost. `COPY_KEYED` and `COPY_EXPAND` remain genuinely extra hardware — a comparator and a bit expander respectively — because no minterm can express either.
  NOTE: It is roughly LUT-neutral, which is the surprising part. This note's datapath is 900–1200 against sheet T's 700 for the 8-bpp path plus 400 for the 1-bpp barrel shifter — and [U.10](sec_u#u10) deletes that shifter outright. The command processor's 800 and the multiplier's 80 sit on top either way (→ [U.37](sec_u#u37)).
- U.5 — **Recommendation: adopt the datapath, the bank policy and the arbiter from this note; keep the command model from sheet T.** But this is an architectural choice with consequences for the kernel, the toolchain and both milestones, and it should be made deliberately rather than inherited from whichever document was read last (→ [Q58](sec_q#q58)).

## What this note settles, and it is more than it overturns.

- U.6 — **The SDRAM bank partition** ([U.16](sec_u#u16)) is a concrete policy where [T.14](sec_t#t14) had only a convention, and it closes [Q50](sec_q#q50). The difference it makes is a factor of two in delivered bandwidth, which is not a tuning detail.
- U.7 — **The arbiter's worst case is proven, not asserted** ([U.28](sec_u#u28)): 23 clocks, **223 ns** at 103.125 MHz, with preemption at burst boundaries and the CPU placed above the blitter. That is precisely the bound [Q57](sec_q#q57) asked for and the priority order [Q29](sec_q#q29) needs on Neon's side, and it independently confirms the read-stall protocol of [D37](sec_q#d37) — the note arrives at "a CPU access stalls PHI2 until the controller services it" from the other direction.
- U.8 — **Descending mode is required, not optional.** The note lists it as an open question; sheet T already answered it by specifying `COPY_RECT` as overlap-safe with the direction chosen automatically ([T.32](sec_t#t32)), which is descending mode by another name. A window drag is the case that needs it.
- U.9 — **Its own first open item is already closed.** The note proposes `$FE:FF00` for the register window and asks for it to be reconciled against the aperture decode; [D36](sec_q#d36) moved the registers to `$FF:8000` instead, because `$FE` is user-mappable and `$FF` is not.

## Pixel format — the strongest new argument in the note, and it reaches back into Mode 1.

- U.10 — **8 bits per pixel, palettised, because of the shifter — not because of memory.** A blit whose destination does not start on a word boundary must shift its source before combining. On a 16-bit bus the shifter width is set by how many pixels share a word.

| Depth | Pixels per word | Shifter positions | Cost |
|---|---|---|---|
| 1 bpp | 16 | 16 | The Amiga's barrel shifter — **~400 LUT** ([T.30](sec_t#t30)) |
| 4 bpp | 4 | 4 | Four-way mux per channel |
| **8 bpp** | **2** | **2** | **A byte swap. One mux per channel** |
| 16 bpp | 1 | none | Free, but see [U.15](sec_u#u15) |

- U.11 — **This undercuts [T.30](sec_t#t30), which calls the 1-bpp barrel shifter "not optional" for the GUI.** It is not optional *for Mode 1*, and Mode 1 is 1 bpp only because an earlier draft assumed the GUI had to be cheap. At 1024 × 600 × 8 bpp the framebuffer is 614,400 B against Mode 1's 76,800 — irrelevant against 64 MB — and scanout is **36.8 MB/s against 4.6**, still under a quarter of the effective budget. What is bought: **256 colours instead of two, and ~400 LUT deleted.** ((`COPY_EXPAND` still needs a bit-granular source read for 1-bpp glyph atlases — that is [Q49](sec_q#q49) — but it never needs a 1-bpp *destination*, which is what the barrel shifter was for.))
- U.12 — So a fourth mode is implied and sheet T does not have it: **1024 × 600 × 8 bpp, the GUI mode as this note assumes it throughout.** Everything in §3 of the note is computed for it. Whether it replaces Mode 1 or joins it is not a detail — it decides whether the 400 LUT come back, and it decides what the window server is written against (→ [Q59](sec_q#q59)).

## Memory baseline — the same part as [F.8](sec_f#f8), and two consequences of its geometry.

| Parameter | Value |
|---|---|
| Part | AS4C32M16SB-7TIN, 32M × 16, four banks, 64 MB ([D14](sec_q#d14)) |
| Rows per bank · columns per row | 8192 · 1024 words = **2048 B** |
| Controller clock | 103.125 MHz ([T.8](sec_t#t8)); the note assumes 100 |
| CAS latency | 2 |
| Peak · usable | 206 MB/s · **~150 MB/s** budgeted for mixed work |

- U.13 — **A row holds exactly 2 KB, which is two complete 1024-pixel scanlines at 8 bpp.** Row management in the scanout path is therefore about one activation per two lines against 512 words of payload — negligible, and it is a property of the geometry rather than of the controller.
- U.14 — **Refresh costs 0.84 %.** One AUTO REFRESH every 7.8125 µs, each occupying `tRFC` ≈ 66 ns. Budgeted, not ignored, and not a constraint.

| Consumer at 1024 × 600 × 8 bpp | Demand |
|---|---|
| Scanout, 59.95 Hz | 36.8 MB/s |
| Audio DMA, stereo 44.1 kHz 16-bit plus mixing reads | < 1 MB/s |
| CPU aperture, worst case | 1.6 MB/s |
| **Left to the blitter** | **~111 MB/s** |

- U.15 — **A full-screen composite is 1.23 MB — read every source pixel once, write every destination pixel once — so the budget carries about 90 of them per second.** The design target is 60. **This is the finding that settles the compositor model** ([U.31](sec_u#u31)): full recomposition is affordable, and damage tracking becomes an optimisation rather than a correctness requirement. It is also why **16 bpp is specified as a non-composited mode only** — 73.7 MB/s of scanout and double the composite traffic exceeds the budget at 60 Hz, so 16 bpp suits a single full-screen application that owns the framebuffer, never the windowed GUI.
  NOTE: The note reaches ~100 composites per second against a 160 MB/s usable figure; the 90 above uses sheet T's more conservative ~150 MB/s and its 59.95 Hz. The conclusion is the same and does not depend on which figure is right, which is the useful thing about a margin of this size.

## Bank allocation — a first-order decision, not an implementation detail.

- U.16 — The four banks each carry their own open-row register, and a bank can be activated while another transfers. **Exploiting that is the difference between roughly 80 and roughly 160 MB/s delivered.** The failure being avoided is specific: a 2D blit advances by one stride at every line boundary, and if source, destination and scanout share a bank, each boundary forces a PRECHARGE and ACTIVATE that overlaps with nothing — about 40 ns, and over a 200-line blit, 8 µs of pure stall.

| Bank | Address range | Contents |
|---|---|---|
| 0 | `$000000`–`$3FFFFF` | Scanout framebuffer, front |
| 1 | `$400000`–`$7FFFFF` | Scanout framebuffer, back |
| 2 | `$800000`–`$BFFFFF` | Window backing stores, even-numbered windows |
| 3 | `$C00000`–`$FFFFFF` | Backing stores, odd windows · font atlas · icon sheets · audio buffers |

- U.17 — The invariant: **during composition the blitter's reads (banks 2/3), its writes (bank 0 or 1) and scanout (the other of 0/1) are always in different banks.** Every ACTIVATE is issued inside another bank's data phase. Expressing it as an address partition works only if **the controller maps the bank bits to the high address bits**, which is a requirement on the controller and not a property of the device — worth stating because the obvious alternative, interleaving banks on low bits to spread sequential traffic, would destroy the whole policy.
- U.18 — **Minimum blitter burst is 8 words, 16 bytes**; shorter fails to amortise `tRCD` and the arbitration handover. This has a consequence the window manager can feel: **wide rectangles are cheaper per pixel than narrow ones.** A 16 × 400 blit and a 400 × 16 blit move identical pixel counts, but the first pays 400 line-boundary costs against 8 words of payload each. Vertical scrollbars, window borders and similar narrow furniture should be batched, or drawn by the CPU where that is practical.
  NOTE: The kernel's VRAM allocator is not free to place a surface wherever it likes, and that obligation now has a specific shape rather than a general one (→ [Q50](sec_q#q50), [U.6](sec_u#u6)).

## The blitter datapath.

- U.19 — Three read channels and one write channel converge on a minterm LUT, then edge masks, then the write burst buffer. **Each channel carries a 24-bit byte pointer, a 16-bit signed modulo added at the end of every line, and an enable bit** — and a *disabled* read channel supplies a constant from its data register instead of consuming bandwidth, which is what makes a solid fill cost one write stream and no reads.
- U.20 — Shared across channels: width in words, height in lines, first-word and last-word masks applied to channel A for non-aligned left and right edges, descending mode for overlapping copies that move down and right, and the logic function byte.
- U.21 — **The modulo is what makes the transfer two-dimensional.** It is the difference between the source bitmap's stride and the width of the rectangle being moved, so the same registers that walk a rectangle also express a clip against it: clipping needs no separate hardware, which is the same economy that lets one `COPY_RECT` window a viewport out of a much wider level buffer in [T.58](sec_t#t58).
- U.22 — **The logic function byte is indexed by `{A, B, C}` read as a three-bit number; bit *n* of `LF` is the output for input *n*.** Eight rows of a truth table, 256 functions.

| `LF` | Function | Use |
|---|---|---|
| `$00` | 0 | Clear |
| `$0C` | B | Straight copy |
| `$3C` | B XOR C | Rubber-band outlines, reversible cursors |
| `$CA` | (A AND B) OR (NOT A AND C) | **Cookie-cut — masked sprite over background** |
| `$F0` | A | Pattern fill from channel A |
| `$FA` | B OR C | Additive overlay |
| `$FF` | 1 | Set |

- U.23 — `$CA` is the one worth being able to derive, because it is what every non-rectangular or partially transparent window uses. With the mask on A, the window's pixels on B and the destination read back on C, the output takes B where the mask is 1 and C where it is 0.

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

  NOTE: `1100 1010` = `$CA`. For a fully opaque rectangular window the compositor uses `$0C` with A and C disabled instead — one read stream, one write stream, **half the traffic**, and it is the common case.

## Register map — offsets within Neon's register page, wherever [D36](sec_q#d36) puts it.

| Offset | Name | Width | Access | Description |
|---|---|---|---|---|
| `$00`–`$02` · `$04`–`$06` | `BLT_APT` · `BLT_BPT` | 24 | W | Channel A and B pointers |
| `$08`–`$0A` · `$0C`–`$0E` | `BLT_CPT` · `BLT_DPT` | 24 | W | Channel C and D pointers |
| `$10`–`$17` | `BLT_AMOD` … `BLT_DMOD` | 16 each | W | Per-channel modulo, signed, added at end of line |
| `$18`–`$19` · `$1A`–`$1B` | `BLT_SIZH` · `BLT_SIZV` | 16 | W | Width in words · height in lines |
| `$1C` | `BLT_LF` | 8 | W | Logic function minterm byte |
| `$1D` | `BLT_CON` | 8 | W | `SHA` `SHB` shift · `ENA`–`END` enables · `DESC` · `MASK` |
| `$1E`–`$21` | `BLT_AFWM` · `BLT_ALWM` | 16 | W | First-word and last-word masks for channel A |
| `$22`–`$27` | `BLT_ADAT` · `BLT_BDAT` · `BLT_CDAT` | 16 each | W | Constants supplied when a channel is disabled |
| `$28` | `BLT_START` | 8 | W | Any write enqueues a snapshot of the register set |
| `$29` | `BLT_STATUS` | 8 | R | b0 `BUSY` · b1 `QFULL` · b2 `QEMPTY` · b3 `IRQ`, cleared on read |
| `$2A` | `BLT_QUEUE` | 8 | R | Queue occupancy, 0–8 |
| `$2B` | `BLT_CTRL` | 8 | W | b0 abort current · b1 flush queue · b2 IRQ enable |
| `$30`–`$3F` | — | — | — | Reserved: line mode, area fill |

- U.24 — **`BLT_START` pushes a snapshot of the whole register file into an 8-deep FIFO in block RAM**, so the CPU can set up the next blit while the current one runs. The completion interrupt is raised when the queue *drains*, not per blit. The queue exists because setup is ~30 stores and without it the engine idles through every one of them — which is the mitigation [U.3](sec_u#u3) weighs, and it is why the queue disappears if the command layer of sheet T is adopted instead.
  NOTE: Multi-byte registers are little-endian and **latch on the write to their highest byte**, so a half-written pointer can never be acted on — the same discipline as [T.47](sec_t#t47) and worth keeping identical across every block in Neon.
  NOTE: **Line mode** — Bresenham in hardware — and **area fill** — a single bit propagating along each line to fill polygon interiors — are both deferred to a second RTL pass, with register space reserved. Sheet T places `DRAW_LINE` at stage N6 ([T.61](sec_t#t61)), which agrees.

## Arbitration — five clients, and one of them cannot be allowed to fail.

| Priority | Client | Deadline character |
|---|---|---|
| 0 | Refresh | Hard, but schedulable with 7.8 µs of slack |
| 1 | Scanout FIFO refill below watermark | **Hard real-time. Cannot fail** |
| 2 | Audio DMA | Soft; a deep FIFO absorbs jitter |
| 3 | CPU aperture access | Stalls PHI2 while pending ([D37](sec_q#d37)) |
| 4 | Blitter | Best effort |

- U.25 — **Scanout is not merely high priority.** If its FIFO underruns mid-line the display shows corruption for that line: there is no recovery and no degradation mode. The arbiter must therefore be *shown* to satisfy it in the worst case, not merely favour it. **This is the inverse of the Amiga's `BLTPRI` bit**, which let the blitter claim every free slot and starve the CPU; here the blitter must never be able to starve the display.
- U.26 — **The CPU sits above the blitter, and that placement is what bounds [D37](sec_q#d37)'s read stall.** Preemption happens only at burst boundaries, and the minimum burst is 8 words — so a CPU read waits for one burst, never for a whole blit. That is the "bounded burst boundary" requirement of [T.19](sec_t#t19) stated as an arbiter policy, and it is what stops a full-screen keyed copy from adding 1.5 ms to interrupt latency.

| Worst-case scanout stall | Clocks |
|---|---|
| In-flight blitter burst cannot be preempted | 8 |
| Refresh already granted | 7 |
| PRECHARGE of the scanout bank | 2 |
| ACTIVATE | 2 |
| CAS latency | 2 |
| Arbitration and handover | 2 |
| **Total** | **23 — 223 ns at 103.125 MHz** |

- U.27 — **FIFO sizing follows from that number and has enormous margin.** During the active region the FIFO drains one byte per pixel clock — 51.56 MB/s, about 25.8 Mwords/s — so a 223 ns stall drains **5.8 words**. A 256-word FIFO would already give forty times the worst case; **512 words, two block RAMs, is specified anyway** because the second also absorbs the burst-granularity mismatch during horizontal-blanking prefetch. Refill is requested at half depth.
- U.28 — That margin is the point rather than the number: **the guarantee survives every open item on this sheet.** If the clock drops to 51.5625 MHz under [Q44](sec_q#q44)'s fallback, if the burst grows, if audio's demand doubles — the stall stays two orders of magnitude inside the FIFO. **A real-time guarantee that depends on an unverified estimate is not a guarantee**, and this one does not.

## Hardware cursor — justified by bandwidth, not by convenience.

- U.29 — A **32 × 32 cursor in block RAM with index 0 transparent**, composited into the pixel stream at scanout, positioned by `CURS_X` and `CURS_Y` — signed, so the pointer may hang off any edge — with an auto-incrementing `CURS_DATA` port for bitmap upload and a three-entry `CURS_PAL`. **Pointer motion is the highest-frequency event in a GUI**: without an overlay every mouse movement damages two rectangles, the old position and the new, and forces a composite pass. With one it costs two register writes and **zero memory traffic**. This is the same conclusion [T.43](sec_t#t43) reached from the opposite direction — it is why the cursor survived when the sprite engine did not.
  NOTE: One reconciliation. The note specifies 4 bpp, sheet T 2 bpp; both fit 512 bytes at 32 × 32 only if the depth is 4, and both describe three colours plus transparent, which needs 2. **2 bpp at 32 × 32 is 256 bytes and leaves half a block spare** — take that, and say so, rather than carrying two numbers.

## Double buffering.

- U.30 — Front and back buffers occupy banks 0 and 1; the compositor writes the back, scanout reads the front, and they exchange at `VBLANK` by swapping the base address register. **The swap also exchanges which bank holds which role, and the separation invariant of [U.17](sec_u#u17) holds either way round** — which is not luck, it is why the partition was drawn with two framebuffer banks rather than one. A `VBLANK` interrupt and a `FB_SWAP_PENDING` status bit let the kernel avoid starting a composite it cannot finish before the next swap point.

## The compositor — and this reverses a deferral in sheet T.

| Model | Bandwidth | Memory | Kernel complexity | Hidden content |
|---|---|---|---|---|
| A — single framebuffer, damage list, clipping (Intuition, X11) | Minimal | Minimal | **High** — clip-region arithmetic in every drawing path | Lost; the application must redraw |
| B — backing stores, full recomposition (Quartz, Wayland) | High | High | Low | Preserved |
| C — backing stores, recomposition of damaged rectangles only | Moderate | High | Moderate | Preserved |

- U.31 — **Model C, implemented strictly as an optimisation of Model B.** The compositor is written so that a full-screen composite is always correct and always affordable; damage tracking then restricts the work actually done. **A bug in damage accounting therefore costs performance, not visible corruption**, and a `COMPOSITE_ALL` path is always available as a fallback. That inversion — correctness in the slow path, speed in the fast one — is the whole value of the model, and it is only reachable because [U.15](sec_u#u15) showed full recomposition fits.
- U.32 — **Sheet T recommends Model A and defers backing stores to [Q55](sec_q#q55); this note recommends C and gives the arithmetic.** The disagreement is not really about compositing — it is about *which framebuffer*. T argued Model A against a flat 1-bpp Mode 1 where a repaint is cheap and memory was assumed tight; this note argues C against 8-bpp backing stores with 64 MB behind them. **Both are right about their own premise**, and the premise is [Q59](sec_q#q59)'s mode question, not a matter of taste (→ [Q60](sec_q#q60)).
  NOTE: The consequences for the kernel are opposite and neither is retrofittable cheaply. Under A, occluded pixels are **gone** and the window manager must repaint from application state — [T.55](sec_t#t55) spells out why that forces a damage model. Under C they are **preserved**, and moving a window is a recomposite. Writing the window server against one and switching to the other is close to a rewrite.

| Item at 1024 × 600 × 8 bpp | Size |
|---|---|
| Front buffer · back buffer | 600 KB each |
| Backing store, full-screen window | 600 KB |
| Backing store, typical 640 × 400 window | 250 KB |
| Font atlas, one face | ~64 KB |

- U.33 — Against 64 MB, a generous allowance for backing stores is a few percent of memory. **Memory is not a constraint on this decision** — which is the same thing [T.3](sec_t#t3) says about Neon generally, and it is why the decision can be made on kernel complexity instead.
- U.34 — **The composite pass**, for each damaged rectangle, back to front over the window stack: clip the window's visible region against the damaged rectangle · skip if empty · **if opaque and rectangular, one blit with `LF = $0C`, channels B and D only** · otherwise one blit with `LF = $CA`, the mask on A, the window's pixels on B and the destination read back on C. Clipping is expressed entirely through the pointer, width, height and modulo registers ([U.21](sec_u#u21)).
- U.35 — **Two drawing paths into a backing store coexist and both are needed.** The **blitter path** covers rectangle fills, glyph blits from the font atlas, widget chrome from an icon sheet, and scrolling a text region — and text is the case that matters: with the font resident in bank 3, drawing a character is an 8 × 16 blit and the CPU issues only the command. **That is the difference between a usable terminal and one that visibly redraws.** The **CPU aperture path** covers irregular geometry the blitter cannot express; it stalls PHI2 ([D37](sec_q#d37)) and is reserved for cases with no alternative.
  NOTE: The aperture exists for irregular drawing, **not for bulk transfer** — which restates [T.16](sec_t#t16) from the compositor's side and is the same rule the whole of Neon is arranged around.

## Resource cost — and the block RAM does not close.

| Block | LUT4 | Blocks |
|---|---|---|
| SDRAM controller with bank interleaving | 700–900 | — |
| Arbiter | 200–300 | — |
| Blitter datapath and address generators | 900–1200 | — |
| Blitter command queue, 8 deep | 150 | 2 |
| Scanout FIFO | 100 | 2 |
| Palette RAM | 50 | 2 |
| Cursor overlay | 200 | 1 |
| Timing generator | 200 | — |
| Audio DMA and mixer | 400 | 2 |
| CPU bus interface and register file | 400 | — |
| **Total** | **~3300–3900** | **9** |

- U.36 — **The note's own conclusion — that this is at or above the ceiling — was true of an HX4K and is not true here.** It estimates against 3520 usable LUT4 and calls the risk genuine; [D01](sec_q#d01) put **HX8K** in every socket, so the same figures land at about half the device and its open item 2 is closed before it was asked. What the note could not know is what else shares the part: Mode 0's text generator, the command processor, and everything else in [T.48](sec_t#t48).
- U.37 — Folded into sheet T's budget the totals barely move, which is [U.4](sec_u#u4)'s point in numbers: **this datapath at 900–1200 replaces T's 700 for the 8-bpp path plus 400 for the 1-bpp barrel shifter, and [U.11](sec_u#u11) deletes the shifter anyway.** Neon stays near **6,000 of 7,680 — about 78 %** either way. **Logic is not what decides [Q58](sec_q#q58).**
- U.38 — [[!blocking]] **Block RAM is what decides it, and it does not fit.** Sheet T is at **32 of 32 with zero margin** ([T.12](sec_t#t12)). This note's 9 blocks include **2 for audio that T never counted** and **2 for the 8-deep snapshot queue against T's 1 for the command FIFO** — so the combined design wants 34 or 35 blocks, before the blitter's own burst buffers. The relief exists and is already identified: a **128-glyph font frees 4 blocks** ([Q45](sec_q#q45)), and the cursor at 2 bpp rather than 4 gives back half of one ([U.29](sec_u#u29)). **What was insurance is now spent**, and it must be decided before RTL rather than after synthesis (→ [Q61](sec_q#q61)).

## Stale context in the source note, recorded so it is not carried forward.

| In the note | Superseded by |
|---|---|
| iCE40 **HX4K**, LUT budget at the ceiling | **HX8K** in all three sockets ([D01](sec_q#d01)) |
| **ANX6345** bridge in the scanout pipeline | Direct RGB-TTL to the panel ([D05](sec_q#d05)); the bridge is a future variant only ([Q23](sec_q#q23)) |
| Working resolution open, 14" panel under consideration | 10.1", 1024 × 600, fixed ([D05](sec_q#d05)) |
| Register window proposed at `$FE:FF00` | `$FF:8000`, out of the user-mappable bank ([D36](sec_q#d36)) |
| 65816 at **14 MHz** | 8 MHz ([B.1](sec_b#b1)) — setup costs are worse here, not better |
| SDRAM controller at 100 MHz | 103.125 MHz, from the 25 MHz oscillator ([T.4](sec_t#t4)) |
| CPU aperture **writes** stall PHI2 | Writes are posted through a 32-entry FIFO and never stall; only reads stall ([T.17](sec_t#t17), [D37](sec_q#d37)) |
| APS6404L PSRAM, ~66 MB/s QSPI ceiling | Already removed board-wide ([D13](sec_q#d13)) — the note records this correction itself |

![Fig. 11 — The blitter and the bank partition. Three read channels and one write channel meet in the minterm LUT; the partition below is what keeps every ACTIVATE inside another bank's data phase, and it is worth a factor of two in delivered bandwidth.](figures/fig-11-blitter.svg)
LEGEND: Trace legend: <span class="m">mint = channel and pixel data</span> · <span class="g">gold = the bank partition and the arbiter's hard deadline</span> · dashed = the paths that exist only under the channel model of [U.1](sec_u#u1).
