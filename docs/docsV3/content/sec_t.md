# Neon — the GPU
> three modes · a blitter · display lists · what the CPU never does

Neon (FPGA-B) is the graphics half of the machine: a text mode that works before any software exists, two bitmap modes, a drawing engine the CPU commands rather than feeds, and a command processor that renders whole frames with the 65816 doing nothing at all. This sheet is the whole of it. Audio shares the device, the memory arbiter and the register base, and is otherwise out of scope. Source: the four Neon handoff documents, consolidated — and reconciled against this board, which differs from what they assumed in four places, every one of them consequential.

## Three principles — the rest of the sheet is derived from these, and they are worth more than any individual number in it.

- T.1 — **Text mode depends on nothing.** Mode 0 uses no SDRAM, no arbiter, no FIFO and no inter-FPGA signalling: its buffers live in Neon's own block RAM and are initialised from the bitstream. A console therefore exists *before* the SDRAM controller — the most defect-prone block in Neon — has been written, which makes the console the instrument for debugging everything that comes after it. **This ordering is deliberate and must survive every later revision of this sheet.**
- T.2 — **The CPU must not draw.** The 65816 at 8 MHz moves ~1.3 MB/s through the `$FE` aperture in practice and ~1.6 MB/s at its theoretical best; a full 320×200 screen costs it ~40 ms, more than two frames. The drawing engine does the same fill in **0.31 ms**. Every decision below moves work off the CPU, and the sequence ends in the autonomous command lists of [T.39](sec_t#t39), where per-frame CPU cost is ~180 bytes of stores.
- T.3 — **Logic is the constraint, not bandwidth.** With its own SDRAM ([F.12](sec_f#f12)), scanout consumes under 3 % of Neon's memory system. What limits Neon is the HX8K's **7,680 LUT and 32 EBR blocks**. Every feature is judged on LUT cost against what the blitter can already do — which is what removed the tile engine and the sprite engine ([T.44](sec_t#t44)), and it is the only reason the budget closes.

## Four reconciliations — where the handoff documents and this board disagree. Read these before trusting any figure below.

- T.4 — **The reference clock is 25 MHz, not 12.** The handoff derives a single 102.000 MHz domain from a 12 MHz crystal. This board has **one 25 MHz oscillator** starred to all three FPGAs ([B.7](sec_b#b7)), and 102.000 MHz is not reachable from it: the iCE40 PLL needs its phase-detector input at 10 MHz or above, which allows only `DIVR=0` (25 MHz) or `DIVR=1` (12.5 MHz), and neither lands on 102. Two settings bracket it.

| Setting | PFD | VCO | Core | Pixel | at 1344×635 | at 1344×640 |
|---|---|---|---|---|---|---|
| `DIVR=0 DIVF=32 DIVQ=3` | 25 MHz | 825 MHz | **103.125 MHz** | **51.5625 MHz** | 60.42 Hz | **59.95 Hz** |
| `DIVR=1 DIVF=64 DIVQ=3` | 12.5 MHz | 812.5 MHz | 101.5625 MHz | 50.78125 MHz | 59.50 Hz | 59.04 Hz |

- T.5 — **Take the first, and move the vertical total from 635 to 640.** The horizontal set is untouched — 1024 active + 24 front + 136 sync + 160 back = 1344 — and the vertical becomes 600 active + 3 front + 6 sync + 31 back = 640, giving **59.95 Hz**. Every derived figure in this sheet is computed at 103.125 MHz core, 51.5625 MHz pixel, 16.68 ms per frame. **This costs nothing to change later**: the blanking values were provisional in the handoff and remain provisional here, because the module datasheet is still missing (→ [Q1](sec_q#q1), [Q43](sec_q#q43)). What will not change is the pixel clock and the active geometry.
  NOTE: The 60 Hz target is physics, not preference — the panel's polarity inversion becomes visible flicker as the rate drops ([sheet H](sec_h)) — but 59.95 against 60.00 is four hundredths of a percent and is invisible. Do not spend a fractional PLL on it.
- T.6 — **Neon's SDRAM is 64 MB, not 32.** The handoff assumes a W9825G6KH; [D14](sec_q#d14) put an AS4C32M16SB-7TIN on both FPGAs, with the W9825G6KH kept only as a drop-in fallback on the same footprint. Bandwidth is unaffected — the bus is 16 bits wide either way — and every capacity argument in the handoff gets easier: the pre-rendered level buffer of [T.60](sec_t#t60) is 1.64 MB, **2.5 % of memory rather than 5 %**, and a dozen levels could be resident at once.
- T.7 — **The wait line the handoff wished for already exists.** Its whole §7 restriction — a write-only graphics aperture, with a slow `PEEK` register pair for diagnostic reads — rests on the premise that Neon cannot stall the CPU because the inter-FPGA link was removed. It was not removed entirely: [B.6](sec_b#b6) keeps **NEON_BUS_BSY (Neon → Helium)** alive for exactly this, and [sheet H](sec_h) already routes Neon's VSync interrupt into Helium's PIC. Both wires are in the pin budget. **So the aperture can be readable, and `PEEK` is redundant and is dropped** (→ [D32](sec_q#d32), [T.15](sec_t#t15)).
  NOTE: The handoff would have had that line stretch RDY; it must gate **PHI2** instead, for [D16](sec_q#d16)'s reason — RDY is reported ignored during write cycles on some W65C816S revisions, and gating the clock sidesteps the question entirely. The wire is now named `NEON_BUS_SEL` / `NEON_BUS_BSY` and its protocol is [T.18](sec_t#t18); the philosophy is in the names, **we select Neon's bus, and while that bus is busy Helium holds the CPU** (→ [B.6](sec_b#b6), [D37](sec_q#d37)).

## Clocking — one domain, and a second PLL nobody has claimed.

- T.8 — **A single clock domain at 103.125 MHz.** The pixel stage advances on a clock enable asserted every second cycle, giving 51.5625 MHz; `PCLK` leaves through a **DDR output register**, so the panel gets a clean clock with no combinational clock path and no divided-clock domain. SDRAM runs at the full rate, exactly 2× the pixel rate. The consequence is the one that matters: **there is no clock-domain crossing anywhere inside Neon** — no synchroniser FIFO between scanout and memory — which eliminates an entire class of intermittent, hard-to-diagnose failure. ((The panel latches on the falling DCLK edge, so the gateware changes data on the rising one — [E0.4](sec_p#e04).))
- T.9 — **The second PLL is free and has two claimants.** The handoff's earlier draft spent both, one on pixels and one on SDRAM; the single-domain decision returns one. Audio wants a master clock the I2S divider can hit cleanly, and the VGA bring-up path of [sheet H](sec_h) wants 25.175 MHz for 640×480 — which the video domain cannot supply, since 103.125 / 4 is 25.78 MHz, 2.4 % fast. Most monitors tolerate that; the panel is the product and VGA is scaffolding, so this is not worth a fight (→ [Q54](sec_q#q54)).
- T.10 — [[!blocking]] **103 MHz is demanding for an iCE40 HX and closure is unproven until synthesis.** SDRAM controllers in this range are documented on the family; that is not the same as having one placed and routed with a blitter beside it. **Fallback:** run everything single-rate at 51.5625 MHz, halving peak SDRAM bandwidth to ~103 MB/s. Every performance figure below halves and still beats CPU-driven rendering by more than an order of magnitude. **The architecture does not depend on 103 MHz; only the margins do** (→ [Q44](sec_q#q44)).

## Memory — two of them, with a strict division of responsibility.

- T.11 — **EBR holds everything that must be reachable without a wait state, and everything scanout needs at single-cycle latency.** It is genuinely dual-port: port A serves scanout, port B serves the CPU aperture. At 103 MHz fabric against 8 MHz PHI2 every CPU access to EBR — read *or* write — completes inside its bus cycle. No write FIFO, no shadow copy in system SRAM, no arbiter, and no wait line asserted. Glyph fetch is one cycle; in SDRAM the same random access would pay a row activation (~60 ns) for no benefit.

| Structure | Size | Blocks | Used in |
|---|---|---|---|
| Text buffer | 8 KB | 16 | Mode 0 |
| Font | 4 KB | 8 | Mode 0 |
| Palette — 256 × 18 bit | 1 KB | 2 | all modes |
| Scanout line buffer, double | 1 KB | 2 | Modes 1, 2 |
| Blitter source/destination burst buffers | 1 KB | 2 | Modes 1, 2 |
| Command FIFO — 128 × 32 bit | 512 B | 1 | Modes 1, 2 |
| Cursor pattern | 512 B | 1 | all modes |
| **Total** | **16 KB** | **32 of 32** | |

- T.12 — **Fully allocated, zero margin**, and it closes only because [T.44](sec_t#t44) removed the tile store and the sprite pattern cache. Two reliefs exist and should be treated as insurance rather than as slack: **reduce the font to 128 glyphs** — full ASCII plus box-drawing, freeing 4 blocks, and it should be a synthesis parameter from the first day rather than a later edit; or **multiplex the 16 text-buffer blocks into graphics use**, freeing 8 KB in graphics modes at the cost of address multiplexers and of destroying screen contents across a mode switch. The second is acceptable on its own terms — returning to text mode implies a redraw — but it has a software consequence that must be designed in rather than retrofitted ([T.55](sec_t#t55)).
  NOTE: **The font blocks must not be reclaimed under any scheme.** Their bitstream initialisation is the entire mechanism by which the machine prints before software exists ([T.28](sec_t#t28)); reusing them destroys it. Which relief to take is decided once the blitter's real EBR demand is known from synthesis, and not before (→ [Q45](sec_q#q45)).
- T.13 — **SDRAM holds framebuffers, image assets and command lists** — everything large, nothing latency-critical. At 103.125 MHz on a 16-bit bus, peak is **206 MB/s**. Row activation costs ~20 clocks per 512-word row, about 4 % on sequential access; auto-refresh ~0.2 %. Sequential throughput is therefore ~187 MB/s, and the figure to budget against for mixed work is **~150 MB/s effective**.

| Consumer | Mode 2a | Mode 1 |
|---|---|---|
| Scanout | 3.8 MB/s | 4.6 MB/s |
| Hardware cursor | 0.1 MB/s | 0.1 MB/s |
| Audio DMA | 0.7 MB/s | 0.7 MB/s |
| CPU aperture traffic, worst case | 1.6 MB/s | 1.6 MB/s |
| **Left to the drawing engine** | **~144 MB/s** | **~143 MB/s** |

- T.14 — Scanout consumes under 3 % of the memory system: **memory bandwidth is not a design constraint for Neon**, and any argument that reaches for it has gone wrong somewhere else. One layout convention does matter — put the source and the destination of a copy in **different SDRAM banks** where possible, so the controller interleaves them instead of paying a row activation on every alternation. The hardware does not enforce it; it is a rule for the kernel's VRAM allocator and the asset loader (→ [Q50](sec_q#q50)).

## CPU access — what the aperture is, and the one thing software should still not do with it.

- T.15 — **Mode 0 is fully read/write with zero wait states**, as [T.11](sec_t#t11) requires — its buffers are in EBR and `NEON_BUS_BSY` is never asserted for them, so the console path stays free of everything below and [T.1](sec_t#t1) is untouched. **Modes 1 and 2 are readable too**, through the stall protocol of [T.20](sec_t#t20). The handoff's `PEEK` path cost ~20 CPU cycles per byte plus a register file and a state machine to drive it; this costs a wire already in the budget and is about six times faster. **The slow mechanism is deleted and its address space returned.**
- T.16 — **The software model should nevertheless be written as though the aperture were write-only.** Asset flow is one-way — microSD → system memory → aperture → VRAM — and once the drawing engine is running the CPU has no reason to read the framebuffer at all. The two operations that historically want reads are screen capture and a colour-picker tool; both now simply read, at a speed neither cares about. **Any design in which the CPU reads pixels in a loop has taken a wrong turn** and will run at aperture speed instead of the blitter's.
  NOTE: This is a stronger position than the handoff's, not a weaker one. A GUI cannot implement save-under by pulling the screen into system memory and pushing it back — not because it is forbidden, but because it is ~60× slower than telling the blitter to move the same rectangle inside VRAM, where the pixels never travel over the bus at all ([T.58](sec_t#t58)).
- T.17 — Writes are absorbed by a **32-entry FIFO** and retired to SDRAM by the arbiter, so a write never stalls the CPU even when a burst is in flight. The register file is in fabric registers, not memory, and is readable and writable in every mode with no latency at all — which is what allows it to live in the privileged bank rather than in the aperture ([T.23](sec_t#t23)).
- T.18 — **The read protocol: Helium stalls first and asks afterwards.** The obvious arrangement — Neon detects the read and asserts a wait line in time to hold off the rising edge of PHI2 — is a race nobody should sign up for. The low phase is ~62 ns at 8 MHz, and inside it Neon would have to see the address settle, decode it, and get a signal across an inter-FPGA crossing that is asynchronous in practice, since Helium's core clock and Neon's 103.125 MHz come from different PLL outputs of the same oscillator. **The race disappears once you notice Helium does not need to be told.** Helium decodes the bank itself ([F.3](sec_f#f3)) and is the party that asserts `NEON_BUS_SEL`; it therefore knows a read of `$FE` is in progress at the same instant Neon does. So:

| Step | Who | What |
|---|---|---|
| 1 | Helium | Decodes bank `$FE` with `RWB` high and asserts `NEON_BUS_SEL`. **Holds PHI2 low unconditionally** — it does not wait to observe anything |
| 2 | Neon | Latches the address, requests the read through its arbiter, and holds `NEON_BUS_BSY` asserted while it works |
| 3 | Neon | Places the byte in an output register and **deasserts `NEON_BUS_BSY`** — the meaningful edge of the whole protocol |
| 4 | Helium | Sees busy clear, raises PHI2. Neon drives D0–D7 through the high phase; the CPU latches on the falling edge |

  NOTE: **Stalling PHI2 low rather than stretching the high phase is deliberate and it is what keeps this cheap.** Extending the high phase would need a fresh reading of the W65C816S datasheet on holding a static core with the clock high, which nobody has done; stalling low reuses the exact clock gate Helium already owns for cache fills and the exact datasheet permission [D16](sec_q#d16) already established. It also falls out correctly on the bus: D0–D7 carries the bank byte while PHI2 is low ([F.3](sec_f#f3)), so Neon *must not* drive during the low phase, and in this scheme it never does.
  NOTE: **`NEON_BUS_BSY` is busy by default, and that is a safety property, not a convenience.** Were it idle-low, Helium could stall and immediately sample "not busy" before Neon had even noticed the cycle, and resume onto a bus nobody was driving. Busy-by-default also makes an absent, unconfigured or reset Neon read as *busy* — so the machine stalls and the watchdog reports it, instead of silently returning whatever a floating bus settles at. That matters because [E1.4](sec_p#e14)'s staged-population rule guarantees there will be stages where **Neon is not on the board at all.**
- T.19 — **Four requirements, and the first is the one that would otherwise ship as an intermittent bug.**

| # | Requirement | Why |
|---|---|---|
| 1 | **A read drains the write FIFO before it is issued** | Writes are posted ([T.17](sec_t#t17)). A read that overtakes a pending write returns the *previous* contents — and it would fail intermittently, according to arbiter load, so "read back what was written" would pass almost every time. Draining is simpler than store-to-load forwarding and this path is slow by design |
| 2 | **The arbiter bounds the wait** | A row activation is ~60 ns and irrelevant. A read that queues behind a full blitter operation is up to 1.5 ms with the CPU clock stopped. The CPU read port needs high priority and the blitter must be preemptible at a bounded burst boundary (→ [Q29](sec_q#q29)) |
| 3 | **Helium's fill watchdog covers this path** | No answer within its window ⇒ resume PHI2, return `$FF`, raise the fault. Without it an unconfigured Neon freezes the machine with no clock and no diagnostic path — the failure [F.11](sec_f#f11) already demanded a watchdog for |
| 4 | **The software model stays write-only** | The restriction was doing useful work: it is *why* save-under is a blitter copy and why assets flow one way. With reads available someone will use them, and the symptom is a slow GUI rather than a compile error ([T.16](sec_t#t16)) |

  NOTE: **It is far less slow than "slow" suggests, and the numbers argue for deleting `PEEK` rather than keeping both paths.** A `LDA $FE0000,x` is 5 cycles, 625 ns at 8 MHz; the stall adds ~300–400 ns. Inside a copy loop of ~10 cycles that is **+30 %** — a VRAM read loop runs at roughly 70 % of the speed of the same loop against SRAM, because **the bottleneck is the 65816, not the SDRAM.** Capturing the 64 KB of Mode 2a takes ~26 ms, under two frames, against ~160 ms through `PEEK`.
  NOTE: The same wire serves a second requester with no clock involved at all. When **Helium** reads VRAM — the debug agent issuing an external cycle with `BE` low and the CPU off the bus ([R.5](sec_r#r5)) — there is no PHI2 to stall; Helium simply waits for busy to clear before sampling D0–D7. **`NEON_BUS_BSY` is a ready line that Helium happens to use to gate PHI2 when the requester is the CPU**, and that is the cleanest way to hold the whole mechanism in mind.

## Two windows, and the division between them is the protection model — `$FE` is Neon's data, `$FF:8xxx` is Neon's control.

| Range | Size | Mode 0 | Modes 1 / 2 |
|---|---|---|---|
| `$FE:0000`–`$FE:1FFF` | 8 KB | Text buffer, R/W | VRAM window |
| `$FE:2000`–`$FE:2FFF` | 4 KB | Font, R/W | VRAM window |
| `$FE:3000`–`$FE:7FFF` | 20 KB | unmapped, reads `$00` | VRAM window |
| `$FE:8000`–`$FE:83FF` | 1 KB | Palette, R/W | Palette, R/W |
| `$FE:8400`–`$FE:FFFF` | 31 KB | unmapped, reads `$00` — **and nothing in it is privileged** | unmapped |
| `$FF:8000`–`$FF:80FF` | 256 B | Register file | Register file |
| `$FF:8100`–`$FF:FFFF` | — | reserved to Neon, reads `$00` | reserved to Neon |

- T.20 — In graphics modes `$FE:0000`–`$FE:7FFF` is a **32 KB sliding window** into SDRAM at byte offset `VRAM_PAGE × 32768`. The window is a power of two, so page arithmetic is a shift. It supersedes an earlier claim that the Mode 2a framebuffer fits one aperture page — it spans two — and the point is moot in practice, because with the blitter drawing, the CPU walks the framebuffer only during asset upload, where paging costs one register write per 32 KB. The upper half of the aperture is now wholly free, which is headroom for a larger window later and is deliberately left unclaimed.
- T.21 — **Neon's registers live in bank `$FF`, and this is what closes the hole an earlier draft could only paper over.** That draft put them at `$FE:FF00`, inside the aperture — and [J.5](sec_j#j5) lets a process map aperture pages into its own address space, because `$FE` is user-mappable by [L.10](sec_l#l10). A process holding a mapping that reached the register file could write `LIST_BASE` and `CMD_CTRL.LIST_RUN`, and the command processor then reads and writes **anywhere in the 64 MB with no MMU between it and memory** — a complete escape from the protection model, arrived at through a device node rather than through a bug. The fix was a rule the kernel had to remember. **Moving the file into `$FF` makes it a property of the architecture instead**: bank `$FF` is unmappable to user space by construction, the self-protecting keystone of the whole scheme, so the command processor's control surface now sits behind the same wall as the MMU's.
  NOTE: **This costs nothing in pins and introduces no new mechanism.** Neon already taps A0–A15 and already captures the bank byte off D0–D7 during the low phase of PHI2 ([T.51](sec_t#t51)) — that is how it recognises `$FE`. Recognising a second bank is the same comparator with a different constant. `NEON_BUS_SEL` means **"Helium has validated this cycle for Neon"** and covers both windows — for `$FE`, after translation and the permission check; here, after the bank decode and the privilege check. That generality is why the net is named for Neon's bus rather than for video memory ([B.6](sec_b#b6), [Q56](sec_q#q56)).
- T.22 — **The base is `$FF:8000` so that Helium's decode is one address bit.** Helium's rule becomes "A15 set in bank `$FF` ⇒ not mine" — a single-bit test, not a range comparator — and it keeps `$FF:0000`–`$FF:7FFF` entirely for its own blocks, so the allocation [Q22](sec_q#q22) still owes for the timer, the interrupt controller, the peripheral block and the power block proceeds with **no risk of ever colliding with Neon**. Neon's file needs 256 bytes and fits one of Q22's walls exactly.
- T.23 — **Three requirements on the gateware, and the first is the only genuinely new work on the board.** **Helium must release D0–D7** on a read in Neon's window and let Neon drive them; it is the same turnaround an `$FE` read already performs, so it is a reuse rather than a new kind of thing, but it is one more case to get right and it belongs in the [E5](sec_p#e5) test list. **Neon must gate on the strobe, not on the address alone** — without it, a user-mode store to `$FF:8000` reaches the register before `ABORTB` lands, and ABORT cancels the instruction on the CPU without side effects; it cannot un-write a register on the far side of the bus. And **no stall is needed**: the file is fabric registers, so Neon drives data inside the PHI2 high phase — ~62 ns, about six fabric clocks at 103 MHz — exactly as the SRAM does in [F.3](sec_f#f3), and `NEON_BUS_BSY` stays deasserted. Register latency is fixed and identical to Helium's own `$FF` blocks.
  NOTE: One convention differs and should be stated rather than discovered. [M.4](sec_m#m4) makes Helium's MMU registers 16-bit, accessed with `M=0`; Neon's are byte-wide with 16- and 24-bit values latching on their high byte ([T.51](sec_t#t51)). That is allowed — Q22's whole point is that walls between blocks let each keep its own rules, and the power block of [sheet S](sec_s) already departs further — but a kernel header that assumes one convention across bank `$FF` will be wrong.
  NOTE: **What does not change is the property the sheet is built on.** The service port reaches the register file directly ([T.48](sec_t#t48)), so the EC still drives Neon with Helium unconfigured and the CPU in reset. Had the registers been reachable only through Helium's decode, this move would have broken [T.55](sec_t#t55) — the whole boot display — and it does not.

## Mode 0 — text, and the reason Neon is specified first.

| Parameter | Value | Derivation |
|---|---|---|
| Cell | **8 × 16 px** | 1024 / 8 = 128 exactly |
| Grid | **128 × 32** | 32 × 16 = 512 lines |
| Cells | **4096** | 2¹², exactly 12-bit addressable |
| Text area | 1024 × 512 px | panel lines 44–555 |
| Border | 44 lines top and bottom | (600 − 512) / 2 |
| Text buffer | 8 KB in EBR | 4096 cells × 2 B |
| Font | 4 KB in EBR | 256 glyphs × 16 B |

- T.24 — **The 8 × 16 cell is the standard VGA text cell, and every part of that is load-bearing.** Any existing 8 × 16 VGA font drops in unmodified; glyph row lookup is a 4-bit shift rather than a multiply by 15; and **descenders are not truncated** — a 15-line cell clips `g j p q y` and forces a bespoke font nobody wants to draw. The 4096-cell total being an exact power of two is what makes the scroll of [T.27](sec_t#t27) free: the ring wrap is a bit mask, not a comparator.
- T.25 — **Character generation is fully real-time and needs no line buffer.** Per cell the pipeline wants one text-buffer read and one font read every 8 pixel clocks, which at 51.5625 MHz against single-cycle EBR is a factor of 8 of slack. For panel active line `L` in 44…555: `text_row = (L-44) >> 4` · `glyph_row = (L-44) & 15` · `cell_index = (TEXT_START + text_row*128 + column) & $0FFF` · `code = buf[cell_index<<1]` · `attribute = buf[(cell_index<<1)+1]` · `glyph_bits = font[(code<<4) | glyph_row]`. Each of the 8 bits selects between the foreground and background palette entries, MSB leftmost. The pipeline is three stages deep, so cell fetch begins three clocks before the first active pixel — absorbed in the back porch. Outside the text area and during horizontal blanking the output is `BORDER_COLOR`.
- T.26 — **Cell format: two bytes, little-endian, naturally aligned.** Byte 0 is the glyph code; byte 1 is `bg[3:0]` in bits 7:4 and `fg[3:0]` in bits 3:0, both indices into the first 16 palette entries. A 65816 in 16-bit accumulator mode writes a whole cell with one `STA` — **one instruction but two bus cycles**, because the data bus is 8 bits wide; the saving is instruction overhead, not bus traffic, and an earlier note claiming otherwise is wrong. A full-screen fill is 8,192 bytes, about **5 ms** at 8 MHz with an unrolled loop.
  NOTE: **Deferred: there is no per-character blink attribute.** The classic VGA approach reallocates attribute bit 7 and cuts the background palette to 8 entries; keeping full 16/16 colour is judged more valuable. If blink is wanted later it arrives as a *global* mode bit that reinterprets bit 7 — never as a silent change to the format.
- T.27 — **Scrolling is a pointer move.** The buffer is a ring of 4096 cells and `TEXT_START` is the cell index shown at the top-left corner; to scroll one line, add 128 modulo 4096 and clear the 128 cells newly exposed at the bottom.

| | Bytes moved | Time at 8 MHz |
|---|---|---|
| Naive block move | 8192 | ~5 ms |
| `TEXT_START` + clear one row | 256 | **~160 µs** |

  NOTE: A factor of 32, and because the ring wraps at exactly 4096 the software needs no wrap-around special case at all — the hardware masks the index. On a console that scrolls continuously through boot this is the difference between scrolling being a visible cost and being free.
- T.28 — **Two-tier font residency.** The **default font is initialised in the bitstream**, so the system prints from the instant `CDONE` rises — before the microSD is mounted, before a kernel exists, before the 65816 has left reset. This is not a convenience; it is the only mechanism by which boot and bring-up diagnostics reach a screen. The font is then **replaceable at runtime** by writing `$FE:2000`–`$FE:2FFF`, which goes to EBR port B while scanout reads port A.
  NOTE: Writing the font outside vertical blanking may produce a single-frame artefact on the glyph row being fetched at that instant. Harmless, but a kernel loading a full font should wait for `STATUS.VBLANK` or accept one torn frame.
- T.29 — **Power-on state is a correct screen, not a random one.** The text buffer EBR is likewise initialised at synthesis — every cell code `$20`, attribute `$0F`, light grey on black — and the palette to the standard 16-colour CGA/EGA set. The panel therefore shows a blank, correctly bordered field the moment configuration completes, with no register written by anybody.

## The hardware cursor — the one place dedicated hardware beat the blitter.

- T.30 — In Mode 0 the cursor is rendered by **swapping the foreground and background palette indices** for the affected pixels of the cell at `CURSOR_POS`. It consumes no buffer storage and needs no save/restore of the underlying cell by software — a persistent source of complexity in software-drawn consoles. `CURSOR_CTRL` selects shape (block · underline, rows 14–15 · half block, rows 8–15 · vertical bar, column 0), blink (steady · 1 Hz · 2 Hz, derived from the vertical sync counter) and enable. Cost: ~60 LUT.
- T.31 — `CURSOR_POS` is a **buffer-absolute** cell index, not a screen-relative one, so it scrolls with the text it is attached to — which is the correct behaviour for the cell the cursor belongs to. A terminal that wants the cursor pinned to the last visible row updates it when it scrolls: one register write. In Modes 1 and 2 the cursor becomes a 16 × 16 or 32 × 32, 2 bpp pattern held in EBR and positioned by pixel coordinate, composited at scanout. **It survived the cull of [T.44](sec_t#t44) for one reason: a GUI pointer must track the mouse independently of the rendering loop.**

## Mode 1 — hires monochrome, and the GUI's mode.

| Parameter | Value |
|---|---|
| Resolution | **1024 × 600** — native, no replication, square pixels |
| Depth | 1 bpp, MSB leftmost |
| Framebuffer | 128 B/line × 600 = **76,800 B** |
| Colours | `MONO_FG`, `MONO_BG` — two palette indices |
| Border | none, full panel |
| Scanout | 4.6 MB/s |

- T.32 — Full panel resolution at 1:1 is what makes readable text and precise window furniture possible, and nothing else about Mode 1 is expensive. [[open]] **The expensive part is arbitrary-alignment blitting, not the framebuffer.** Placing an image at any horizontal position in a packed 1-bpp buffer means shifting each source word by 0–15 bits, merging across word boundaries and masking both edges — the classic Amiga blitter problem, estimated at **~400 LUT** for the barrel shifter and mask generator. It is not optional *for a 1-bpp destination*, and that qualification turns out to matter: at **8 bpp on a 16-bit bus the shifter degenerates to a byte swap**, one multiplexer, so a 1024 × 600 × 8 bpp GUI mode would delete these 400 LUT and buy 256 colours instead of two, for a framebuffer of 614,400 B and 36.8 MB/s of scanout — both comfortable. **That mode does not exist in this sheet and [sheet U](sec_u) assumes it throughout** (→ [U.10](sec_u#u10), [Q59](sec_q#q59)).

## Mode 2 — multicolour, in two sub-modes that share all their logic.

| | **Mode 2a** | **Mode 2b** |
|---|---|---|
| Logical resolution | 320 × 200 | 512 × 300 |
| Replication | 3 × 3 | 2 × 2 |
| Displayed area | 960 × 600 | **1024 × 600 — exact** |
| Horizontal border | 32 px each side | none |
| Framebuffer | **64,000 B** | 153,600 B |
| Scanout | 3.8 MB/s | 9.2 MB/s |
| Double-buffered | 128 KB | 300 KB |

- T.33 — Both are 8 bpp against the 256-entry palette, and the sub-mode is a register field driving the replication counters and the line stride — **supporting 2b costs approximately nothing in logic.** Mode 2a is the recommended default for games: familiar geometry, and pixels that are exactly square because the panel's are. Mode 2b fills the panel with no side bars, costing only memory and bandwidth, both of which are abundant.
  NOTE: **This is where [D06](sec_q#d06)'s px-doubling finally lands.** It was recorded as a global policy — draw at 512×300, emit each pixel twice — and superseded when the PSRAM that motivated it disappeared. It returns as **Mode 2b**, a mode among three rather than a property of the machine, which is the right shape for it: the GUI gets native 1024×600 in Mode 1 and games get replication in Mode 2, instead of every subsystem paying for one compromise.

## The drawing engine — eight operations, all clipped, all strided. [[!contradiction]] A fifth handoff specifies a different engine — see [sheet U](sec_u).
  NOTE: **What follows is the operation-based engine.** A later design note specifies the Amiga four-channel blitter instead — 256 bitwise logic functions, pointer-and-modulo addressing, mask surfaces rather than a colour key. They are different machines and the choice is open and blocking ([Q58](sec_q#q58), [U.1](sec_u#u1)). Sheet U also settles three things this sheet left open — the bank partition, the arbiter's worst case and descending mode — none of which depends on how that choice goes.

| Operation | Description |
|---|---|
| `FILL_RECT` | Solid colour fill |
| `FILL_PATTERN` | Fill with the 8 × 8 pattern register |
| `COPY_RECT` | Opaque rectangular copy, overlap-safe — direction chosen from the sign of the displacement |
| `COPY_KEYED` | Copy skipping pixels equal to the colour key |
| `COPY_TILED` | Copy with the source wrapped modulo its dimensions — texture fill |
| `COPY_EXPAND` | 1-bpp source expanded to two colours — glyphs and icons into Modes 1 and 2 |
| `DRAW_LINE` | Bresenham, arbitrary endpoints |
| `DRAW_HLINE` · `DRAW_VLINE` | Optimised axis-aligned cases |

- T.34 — All eight respect the clip rectangle set by `SET_CLIP`, and all take **independent source and destination strides**, so any operation may address a sub-rectangle of a larger image. That one property is what makes the atlas and the pre-rendered level buffer of [T.60](sec_t#t60) work at all.
  NOTE: `COPY_TILED` is what "apply a texture" means here — filling a region by repeating a bitmap, in the QuickDraw and Amiga-blitter sense. **Perspective-correct 3D texture mapping is out of scope and is not implementable on this device**: it needs a per-pixel divide, and the iCE40 has neither hardware multipliers nor the logic budget to build them.

| Full-screen operation, Mode 2a — 64,000 B | Traffic | Time | vs. the 65816 |
|---|---|---|---|
| `FILL_RECT` | 64 KB write | **0.31 ms** | 129× |
| `COPY_RECT` | 64 KB read + 64 KB write | **~1.0 ms** | 40× |
| `COPY_KEYED` | read src + read dst + write dst | **~1.5 ms** | 27× |
| `DRAW_LINE` | ~1 px/clock | 1024 px in 10 µs | — |

- T.35 — One frame is 16.68 ms; after scanout, cursor, audio and CPU traffic, roughly **15.5 ms** is left for drawing. That is about 50 full-screen fills, or 15 full-screen opaque copies, or 10 keyed copies — per frame. The reference on the right-hand column is the 65816 needing ~40 ms for a single full-screen rewrite through the aperture, which is the whole argument of [T.2](sec_t#t2) stated as a ratio.
- T.36 — **Double buffering should be the default for anything animated.** `DISPLAY_BASE` holds the 24-bit SDRAM address being scanned out and `DRAW_BASE` the one the engine writes; `SWAP_BUFFERS` exchanges them **at the next vertical blanking interval, never mid-frame**, so a partially drawn frame is never displayed. The command completes immediately and the exchange is latched and applied by the timing generator. The cost is 128 KB against 64 MB.

## The command interface — three layers, each usable without the ones above it.

- T.37 — **Layer 1, register-driven.** Parameters into the blitter registers, `BLIT_CTRL.GO` to execute, `STATUS.BLIT_BUSY` polled for completion. It needs no FIFO and no decoder, which is exactly why it exists: it is how the blitter is tested in isolation at stage N1. **It is not for production use** — the CPU is occupied for the duration of every operation.
- T.38 — **Layer 2, the command FIFO.** The CPU writes 32-bit command words to `CMD_PORT`, a single aperture address; Neon buffers 128 of them in EBR and executes them in order, asynchronously. Software checks `CMD_FIFO_FREE` before writing a whole command's worth of words, so the engine never begins decoding a half-written command. **The FIFO does not fill under realistic load**: the CPU produces ~1.6 MB/s while a full-screen fill is four words in and 0.31 ms of work. This layer alone decouples the CPU from drawing and is sufficient for a GUI.
- T.39 — **Layer 3, command lists in SDRAM — the autonomous mode, and the reason the command processor's 800 LUT are worth spending.** `LIST_BASE` plus `CMD_CTRL.LIST_RUN` redirects the processor to fetch its instruction stream from memory instead of from the FIFO.

| Command | Effect |
|---|---|
| `JUMP addr` | Continue at `addr` |
| `CALL addr` · `RET` | One level of subroutine nesting |
| `WAIT_VBLANK` | Suspend until the next vertical blanking interval |
| `WAIT_LINE n` | Suspend until panel line `n` — raster-synchronised effects |
| `SWAP_BUFFERS` | Exchange front and back buffers at the next vblank |
| `SET_PALETTE i, rgb` | Palette write; legal mid-frame after a `WAIT_LINE` |
| `SET_CLIP` | Set the clip rectangle for everything after it |
| `SIGNAL n` | Raise an interrupt to the CPU with tag `n`, readable in `IRQ_TAG` |
| `END` | Return to FIFO execution |

- T.40 — A game builds its display list once, ends it with `SWAP_BUFFERS` and a `JUMP` back to the top, and **the CPU is then free: Neon renders every frame indefinitely until the list is modified.** Per-frame work reduces to patching coordinates inside a list that already exists — a handful of stores. The model is the ANTIC display list and the Amiga Copper, generalised to include the blitter, and it is the mechanism the whole sheet has been building toward.
- T.41 — **Self-modifying lists are the intended usage pattern, not an abuse of one.** Because the CPU can write into a list while Neon executes it, moving an object is a store to a parameter word of an existing `COPY_KEYED`. Two mechanisms guard against the engine reading a half-updated command, and they are for different jobs. **`CMD_CTRL.LIST_LOCK`** makes the engine finish its current command and stall before fetching the next; the kernel patches, clears the lock, execution resumes — but holding the lock across a vblank costs a whole frame, so patches under it must be short. **`SIGNAL` plus the frame gap** puts a `SIGNAL n` at the end of the list, immediately before it loops back to `WAIT_VBLANK`, and the CPU patches in the interval between the signal and the next frame's drawing; no lock, no stall, and a patch that arrives late affects the following frame rather than tearing the current one. **The second is for steady-state animation, the first for structural changes to the list.**

## Command word format — and the revision the programming guide forced back into it.

- T.42 — **Commands carry coordinates, not addresses**, and this is a change from the handoff specification, made while writing the game example and worth more than it first appears. Computing `dst = base + y × stride + x` on the 65816 is a shift-and-add sequence per object per frame — the CPU has no multiplier — and it is precisely the class of work this architecture exists to eliminate. Neon computes the address instead, from `DRAW_BASE` and `LINE_STRIDE`, at the cost of **one sequential multiply per command, ~16 clocks and ~80 LUT** — 160 ns against a command that takes tens of microseconds. The larger consequence follows immediately: because a destination is a coordinate pair packed into one 32-bit word, **moving an object is a single 32-bit patch to one word of a list**, which is what makes [T.62](sec_t#t62) cost 180 µs a frame instead of several milliseconds.

| Word 0 field | Bits | Contents |
|---|---|---|
| opcode | 31:24 | See the operation tables above |
| flags | 23:16 | Clip enable · key enable · direction · … |
| count | 15:0 | Parameter words following |

| `COPY_KEYED` — 6 words | Contents | Patched per frame |
|---|---|---|
| w0 | Header, count = 5 | no |
| w1 | `XY(src_x, src_y)` — offset +4 | yes, animation frame |
| w2 | `XY(dst_x, dst_y)` — offset +8 | yes, position |
| w3 | `WH(w, h)` | no |
| w4 | Source base, 24-bit | no |
| w5 | Key in 31:24, source stride in 15:0 | no |

- T.43 — `FILL_RECT` is 4 words, `COPY_RECT` and `COPY_KEYED` 6, `COPY_EXPAND` 6 with foreground and background colours in w5. Destination base and stride come from the registers; source base and stride are per-command, because atlases and level buffers have their own geometry. **An explicit word count was chosen over a fixed length per opcode so that an unknown opcode can be skipped rather than desynchronising the stream** — which matters exactly when gateware and kernel versions drift during development, and they will (→ [Q51](sec_q#q51)).
  NOTE: Two requirements on the clip unit come with this format and are not optional. It must **sign-extend coordinates and clip negatives**, so an object can hang off the left or top edge; and it must **reject a fully off-screen rectangle before issuing any memory traffic**, because the supported idiom for an unused list entry is to park it off-screen rather than to remove it — removing it would mean rewriting the list every time an enemy dies (→ [Q48](sec_q#q48)).

## Removed on purpose — the tile engine and the sprite engine, recorded so the argument is not had twice.

- T.44 — **Tile mode.** The blitter redraws a full 320 × 200 tilemap — 260 tiles of 16 × 16, 66,560 B of opaque copy — in **~1.3 ms**, under 8 % of a frame. It can therefore rebuild the entire tiled background every frame from scratch and leave 92 % for everything else, with pixel-smooth scrolling free by offsetting destination coordinates. A hardware tile engine costs ~800 LUT to accelerate that, and fragments rendering into two incompatible paths. **Sprite engine.** Blitting 32 masked 16 × 16 sprites is ~8 KB of keyed transfer, **~0.12 ms**, 0.7 % of a frame. Hardware sprites exist to avoid save/restore of background on machines that cannot afford to redraw; with double buffering and 64 MB of SDRAM that problem does not arise, because every frame is drawn fresh into the back buffer.
- T.45 — Combined saving **~1,650 LUT, about 21 % of the device** — which is what takes Neon from 95 % occupancy, not routable once audio grows, to 77 %. What is given up is real and should be stated: sprites composited at scanout cost no framebuffer bandwidth and cannot be erased by drawing, which simplifies certain effects. What is gained is one rendering model, and the logic budget to fit the blitter, the command processor and audio on a single device. **The hardware cursor is retained** ([T.31](sec_t#t31)).

## The service port — four pins that were already there.

- T.46 — The RP2040 configures FPGA-B over SPI. **After configuration completes the same four pins are reused as a control channel into Neon**, costing no additional pins and roughly 200 LUT. It reaches the register file, the text buffer, the font and the palette in EBR, and SDRAM through the same arbiter port the CPU aperture uses. Three things follow, none of them available any other way. **A display before the CPU exists** — the EC writes boot progress to the text buffer while Helium is still being configured and the 65816 is still in reset, which is exactly when a display is most needed and latest to arrive. **A debug channel independent of the CPU bus** — if the 65816 hangs, the EC can still read Neon's registers and write to the screen. And **bulk asset loading that bypasses the CPU** — at 20 MHz SPI the port sustains ~2.5 MB/s from microSD into VRAM against the CPU's ~1.3 MB/s through the aperture, and costs the 65816 nothing (→ [Q52](sec_q#q52)).
  NOTE: This is the second independent debug path on the board and it is worth seeing them as a pair. [Sheet R](sec_r)'s agent gives the EC memory and bus visibility from *inside Helium*; the service port gives it a screen and Neon's state from inside Neon. **Between them, a machine with a dead CPU still has a way to say what is wrong** — and the two paths share no logic, so a fault in one does not take the other.

## Interrupts — three sources, one wire, and it goes to the PIC.

| Bit | Source |
|---|---|
| 0 | Vertical blank |
| 1 | Raster line compare against `RASTER_CMP` |
| 2 | `SIGNAL` executed from a command list; tag in `IRQ_TAG` |

- T.47 — The handoff leaves open whether Neon's interrupt goes straight to the 65816's `IRQB` or through FPGA-A. **This board already answered it**: [sheet H](sec_h) routes VSync into Helium's prioritized interrupt controller, which is the right answer for three reasons — masking and priority come free from a PIC that exists, the CPU keeps a single interrupt entry path, and it costs one pin on each FPGA rather than a wire into a socket. The three sources above are aggregated inside Neon into one line and demultiplexed by the kernel reading `IRQ_STATUS`, which is `W1C`.
  NOTE: Until the gateware exists, software polls `STATUS.VBLANK`; every usage pattern in this sheet is written to work either way. That is not a fallback, it is the bring-up path — the console at [T.53](sec_t#t53) runs with no interrupt controller in existence.

## Register map — base `$FF:8000`, privileged and unmappable ([T.21](sec_t#t21)). All registers byte-wide; reserved bits read zero and must be written zero.

| Offset | Name | Access | Description |
|---|---|---|---|
| `$00` | `NEON_ID` | R | `$4E`, the letter `N` |
| `$01` | `NEON_VER` | R | Gateware version, BCD |
| `$02` | `NEON_MODE` | R/W | 0 text · 1 hires mono · 2 = 320×200 · 3 = 512×300 |
| `$03` | `NEON_CTRL` | R/W | b0 `DISPLAY_EN` — clear paints border colour over the whole screen |
| `$04` | `STATUS` | R | b0 `VBLANK` · b1 `HBLANK` · b2 `BLIT_BUSY` · b3 `CMD_BUSY` · b6 `FIFO_FULL` · b7 `LIST_ACTIVE` |
| `$05` | `BORDER_COLOR` | R/W | Palette index |
| `$06`·`$07` | `RASTER` | R | Current panel line, 10-bit |
| `$08`·`$09` | `RASTER_CMP` | R/W | Compare line |
| `$0A` | `IRQ_EN` | R/W | Bits as the interrupt table above |
| `$0B` | `IRQ_STATUS` | R/W1C | Same bits; write 1 to clear |
| `$0C` | `IRQ_TAG` | R | Tag from the last `SIGNAL` |
| `$10`·`$11` | `TEXT_START` | R/W | Top-left cell index, 12-bit |
| `$12`·`$13` | `CURSOR_POS` | R/W | Cell index, 12-bit |
| `$14` | `CURSOR_CTRL` | R/W | b1:0 shape · b3:2 blink · b4 enable |
| `$18`–`$1A` | `DISPLAY_BASE` | R/W | 24-bit SDRAM address of the displayed framebuffer |
| `$1B`–`$1D` | `DRAW_BASE` | R/W | 24-bit SDRAM address of the draw target |
| `$1E`·`$1F` | `MONO_FG` · `MONO_BG` | R/W | Mode 1 foreground and background palette indices |
| `$20` | `VRAM_PAGE` | R/W | Aperture window select; base = value × 32768 |
| `$21`·`$22` | `LINE_STRIDE` | R/W | Bytes per framebuffer line |
| `$28`–`$2D` | `BLIT_DST` · `BLIT_SRC` | R/W | Destination and source base addresses |
| `$2E`–`$31` | `BLIT_W` · `BLIT_H` | R/W | Width and height in pixels |
| `$32`–`$35` | `BLIT_DST_STRIDE` · `BLIT_SRC_STRIDE` | R/W | Bytes per line, each side |
| `$36`–`$38` | `BLIT_COLOR` · `BLIT_COLOR2` · `BLIT_KEY` | R/W | Fill colour · `COPY_EXPAND` background · colour key |
| `$39`·`$3A` | `BLIT_OP` · `BLIT_CTRL` | R/W | Operation code · b0 `GO`, b1 key enable, b2 clip enable |
| `$3C`–`$43` | `CLIP_X0` … `CLIP_Y1` | R/W | Clip rectangle, inclusive |
| `$48`–`$4F` | `PATTERN` | R/W | 8 × 8 bit pattern, one byte per row |
| `$50`–`$53` | `CMD_PORT` | W | 32-bit command word, latched on the write to `$53` |
| `$54`·`$55` | `CMD_FIFO_FREE` · `CMD_CTRL` | R · R/W | Free entries · b0 `LIST_LOCK`, b1 `LIST_RUN`, b2 `FIFO_RESET` |
| `$56`–`$5B` | `LIST_BASE` · `LIST_PC` | R/W · R | List start address · current fetch address, for debugging |
| `$60`–`$68` | `CURSOR_X` · `CURSOR_Y` · `CURSOR_GFX_CTRL` · `CURSOR_COLORS` | R/W | Graphics-mode cursor |
| `$70`–`$73` | — | — | **Free.** Was `PEEK_ADDR` / `PEEK_DATA`, deleted by [T.15](sec_t#t15) |

- T.48 — `TEXT_START`, `CURSOR_POS`, `RASTER_CMP` and `LINE_STRIDE` **latch on the write to their high byte**, so a 16-bit `STA` applies them atomically with no explicit sequencing — which is what a 16-bit store does naturally, low byte first. The 24-bit address registers take effect on the write to their highest byte, three stores, same rule. **Software must never write these high-to-low**, and the gateware must not make the low-byte write visible on its own.
- T.49 — **The palette is 256 entries of 18-bit RGB666 at `$FE:8000`**, four bytes each — R, G, B in bits 5:0, fourth byte reserved — so the address arithmetic is a shift. It wastes one byte in four and buys 262,144 selectable colours per entry, reaching the panel unmodified. It is initialised in the bitstream to the standard 16-colour CGA/EGA set in its first 16 entries, which is what makes [T.29](sec_t#t29) true.
  NOTE: This supersedes the handoff's first draft, which put a 16-entry palette at `$FE:3000`. **The palette stays in the aperture while the registers do not**, and the split is the one [T.21](sec_t#t21) draws: the palette is bulk data written a kilobyte at a time and nothing about it is privileged, so it belongs with the framebuffer. The offset `$8000` recurring in both banks is a mnemonic worth keeping — `$FE:8000` is Neon's palette, `$FF:8000` is Neon's control — but it is two decodes, not one, and a kernel header should not treat them as related.

## Resource budget — provisional until synthesis, which is the only thing that settles any of it.

| Block | LUT (est.) | | Block | LUT (est.) |
|---|---|---|---|---|
| Timing generator | 120 | | Blitter — 8-bpp path | 700 |
| Text character generator | 200 | | Blitter — 1-bpp barrel shift and edge masks | 400 |
| Text cursor | 60 | | Line drawing | 250 |
| Palette and output stage | 150 | | Command processor — FIFO, fetch, decode, flow | 800 |
| Aperture / CPU bus interface | 250 | | Coordinate multiplier ([T.42](sec_t#t42)) | 80 |
| Register file and interrupt logic | 120 | | Graphics cursor | 150 |
| SDRAM controller and 4-way arbiter | 1,200 | | Service port | 200 |
| Graphics scanout — Modes 1, 2a, 2b | 400 | | Audio, reserved | 800 |
| | | | **Total** | **~5,880 of 7,680 — 77 %** |

- T.50 — That leaves ~1,800 LUT of margin. The configuration including the tile and sprite engines came to ~7,300 — 95 % — which is not a routable design once audio grows, and the removal of [T.44](sec_t#t44) is the whole difference. **Mode 0 alone is ~900 LUT, about 12 % of the device**, which is the number that matters for [T.53](sec_t#t53): the console is cheap enough to synthesise on its own, long before anything else in Neon exists.

## Pin budget — the recount [Q12](sec_q#q12) has been asking for, and it closes, barely.

| Block | Pins | |
|---|---|---|
| Panel RGB666 + `PCLK` `HSYNC` `VSYNC` `DE` | 22 | 18 bpp is the recommendation; 24 bpp would not fit |
| SDRAM — 13 addr + 2 bank + 16 data + 8 control | 39 | |
| CPU bus tap — A0–A15, D0–D7, PHI2, RWB | 26 | Bank byte captured off D0–D7 during PHI2 low |
| I2S to the PCM5102A | 4 | |
| SSPI configuration, reused as the service port | 4 | `CRESET_B` and `CDONE` are dedicated pins, not I/O |
| `NEON_BUS_SEL` in · `NEON_BUS_BSY` out · `NEON_IRQ` out | 3 | The three surviving inter-FPGA control lines. `NEON_BUS_SEL` validates **both** of Neon's windows ([T.21](sec_t#t21)) — moving the registers into `$FF` cost no pin |
| VGA bring-up sync | 2 | The R-2R ladders hang off the panel's own RGB bus |
| **Total** | **100 of ~107** | |

- T.51 — **Two corrections to the old estimate, and they pull in opposite directions.** It reserved **16 pins for an inter-FPGA link that [D04](sec_q#d04) deleted**, and it never counted the CPU bus tap that replaced it — which is 26 pins, so the substitution costs 10 and the budget was wrong in the dangerous direction. Against that, **the bank byte is captured from D0–D7 during the low phase of PHI2**, as the 65816 emits it there, which avoids eight dedicated bank lines and is what keeps the count inside the TQ144 at all. `VDA`/`VPA` are not tapped: Neon has no reason to distinguish an opcode fetch from a data access.
- T.52 — **Backlight `EN` and PWM are not on this list, and that is a decision.** The handoff gives them to Neon and puts `BACKLIGHT_EN` in `NEON_CTRL`; they belong to the EC, which already owns every rail enable through its expander ([D.6](sec_d#d6)) and is the only party awake both before Neon is configured and while the switched domain is coming down. Neon recovers two pins it needs and the bit leaves the register map. **This closes [Q6](sec_q#q6)** (→ [D33](sec_q#d33)).

## What the software actually does — five scenarios, in the order the machine meets them.

- T.53 — **Power-on: nothing configures Neon for display, because it is already displaying.** The moment `CDONE` rises the timing generator begins scanning and the panel receives valid video — 128 × 32 spaces, light grey on black, correct borders — with no register written, no mode selected and no PLL to wait for beyond the FPGA's own configuration. The EC then switches its SPI to service-port mode and writes a banner, and every subsequent boot step is **visible on the screen as it happens**: Helium configuring, the card mounting, the BIOS loading, the CPU being released. If a step fails, the failure is on the panel rather than inferred from a dark one. **Neon is the only subsystem on this board that is useful before anything else works, and that ordering should be preserved as the design evolves.**
- T.54 — **The BIOS inherits a working display and initialises nothing.** It does not probe Neon, does not set a mode, does not load a font; it stores bytes to `$FE:0000` and characters appear. It should read `NEON_ID` once and expect `$4E`, purely to tell "no Neon fitted" from "Neon fitted", and continue in either case. A `putchar` is a 16-bit `STA` to `$FE0000,x` — **absolute-long indexed, so the bank byte is in the instruction and the routine runs from any bank with no bank-register manipulation** — followed by a 16-bit store to `CURSOR_POS`, which latches atomically on its high byte. Scrolling is the pointer move of [T.27](sec_t#t27).
- T.55 — **The kernel's mechanism is identical to the BIOS's** — the same stores to the same addresses — and four things differ, all of them in software layering. The console becomes a kernel-internal driver behind the fixed five-function interface of [J.4](sec_j#j4), reached as a device node. Bank `$FE` stays privileged in the sense that matters ([T.21](sec_t#t21)). A custom font may be loaded from the card into `$FE:2000` during vblank. And the display tick can come from the vblank interrupt rather than from polling.
  NOTE: **One constraint has to be built in from the start and cannot be retrofitted cheaply.** The text buffer occupies EBR that graphics modes may reclaim ([T.12](sec_t#t12)), so switching to a graphics mode and back can destroy the screen contents. **The console driver must therefore hold its own scrollback in system memory and be able to repaint the screen from it.** This is normal for a virtual-console design; it is only expensive if it is discovered late (→ [Q47](sec_q#q47)).
- T.56 — **The GUI, in Mode 1.** Assets — the UI font as a 1-bpp glyph atlas, icons, patterns — are uploaded to VRAM once at boot, by the CPU through the aperture or by the service port straight from the card at twice the rate. **Generated content never becomes pixel data in system memory at all**: drawing a line of text is one `COPY_EXPAND` per glyph from an atlas already resident, selected by a width table, so proportional fonts cost nothing extra. A window is roughly a dozen commands plus one per title character — `SET_CLIP`, two `FILL_RECT`, four lines, a keyed icon, then the title — about 1.2 KB of command words and **~1 ms of blitter time, while the CPU returns to the event loop.** The CPU writes pixels exactly once per asset and never again.
  NOTE: **The software built on this is [sheet V](sec_v)**, and its first correction is to the sentence above: emitting 1.2 KB through the aperture at ~1.3 MB/s is **~0.9 ms of CPU** against that ~1 ms of blitter time, so the CPU does not return to the event loop nearly as promptly as this reads. The ratio only becomes the one this sheet claims once the commands live in a list that is re-executed rather than re-emitted ([T.39](sec_t#t39)) — which is why the GUI wants N5 for client repaint even though the compositor itself does not (→ [V.24](sec_v#v24), [V.35](sec_v#v35)).
- T.57 — **A window is not a rectangle that moves, and getting this wrong produces a design that cannot work.** There are no windows in VRAM: there is one flat framebuffer containing pixels, and a window is a structure in system memory — coordinates, title, widgets, z-order. **No association exists in hardware between the pixels and the window that produced them.** So if window A sits over window B and A moves away, B's pixels are not underneath; they were overwritten when A was drawn, and they are gone. A single `COPY_RECT` drag does work and costs ~0.3 ms for 400 × 300 at 1 bpp, but it leaves two problems behind: the exposed region where the window was, and whatever it now covers. **A straight `COPY_RECT` drag is correct only over a uniform background.** This is why every windowing system has a damage model, and it follows directly from having a flat framebuffer rather than from anyone's incompetence.
  NOTE: **Neon makes the damage model cheap, and that is the useful part.** Repainting an entire window costs ~1 ms, so the compositor can skip precise damage-region arithmetic — the most defect-prone part of any window manager — and simply repaint the full rectangle of every window intersecting the dirty area, back to front. **That is the recommended first implementation**; optimise to precise regions only if a specific case demands it. On machines of this class the trade was not available; here it is. [[open]] **A later design note reverses this**, recommending per-window backing stores with damage-limited recomposition on the arithmetic that the budget carries ~90 full-screen composites a second — which makes full recomposition a correct fallback and damage tracking a pure optimisation. The disagreement is downstream of the mode question rather than a matter of taste (→ [U.31](sec_u#u31), [Q60](sec_q#q60)). **[Sheet V](sec_v) then writes the OS against backing stores** and finds the system gets smaller for it: no refresh events, no cliprect arithmetic, ten event types, and a hung client that still displays (→ [V.6](sec_v#v6)).
- T.58 — **Save-under works, and it is worth spelling out why.** The restriction that made it look impossible was on the CPU reading VRAM; the blitter reads VRAM without restriction. A `COPY_RECT` moves the region about to be occluded into a scratch pool in VRAM and another moves it back, and the saved pixels never leave VRAM or touch the bus. It remains a bounded special case — a menu over a known rectangle, restored before anything else can draw there — and it does not substitute for the damage model.
  NOTE: **Per-window VRAM buffers with compositing** would remove the damage problem entirely, since occluded content would still exist and moving a window would be a recomposite rather than a repaint. With 64 MB and ~143 MB/s it is entirely affordable — ~15 KB per window on top of the 75 KB framebuffer — which is not true of most systems this size. **It is nonetheless deferred, not rejected**: it is materially more gateware and more software, and it must not be attempted before the flat model works (→ [Q55](sec_q#q55)).
- T.59 — **Games, in Mode 2a, and the one place where the display list earns its logic cost.** The obvious method — a tilemap in system memory redrawn per frame, one `COPY_RECT` per tile — fails, and it fails for an instructive reason. The blitter handles 260 tiles in ~1.3 ms without difficulty; **the CPU cannot feed it.** 260 commands × 6 words × 4 bytes is 6,240 bytes through the aperture at ~1.3 MB/s = **4.8 ms per frame**, nearly a third of the budget spent emitting commands rather than drawing. The bottleneck moved from the blitter to the command stream, which is the trap Layer 3 exists to avoid.
- T.60 — **The approach that works: pre-render the level once, scroll it with one command.** At level load, render the whole background into a wide SDRAM buffer — 8,192 px by 200 at 8 bpp is 1.64 MB, 2.5 % of memory, and the tile-by-tile cost is irrelevant because it happens behind a load screen. Per frame, the entire scrolling background is **one `COPY_RECT` with a source stride of 8,192**, windowing a 320-wide viewport out of the level; scrolling is a change to `scroll_x`, **one 16-bit store**. Parallax is a second such command from a second buffer with its own scroll rate, drawn first and keyed over.
- T.61 — Sprite frames live in a VRAM atlas loaded at level start, and each character is one `COPY_KEYED` selecting a source rectangle. The frame list — `WAIT_VBLANK`, `SWAP_BUFFERS`, the two background copies, twenty-four keyed sprites, the HUD, `SIGNAL 1`, `JUMP` — is built once and then **executed forever**; unused sprite slots are parked off-screen rather than removed, where the clip unit rejects them before any memory traffic ([T.43](sec_t#t43)). The CPU writes `LIST_BASE`, sets `LIST_RUN`, and from that point **Neon renders continuously with no CPU involvement at all**: if the game logic stopped entirely the display would keep refreshing at 60 Hz with the last-patched contents.

| Per frame | Blitter | | CPU |
|---|---|---|---|
| Parallax sky | ~1.0 ms | Background `scroll_x` | 1 word |
| Level background, keyed | ~1.4 ms | 24 sprite positions and source frames | 48 words |
| 24 sprites at 32 × 32 | ~0.4 ms | HUD when it changes | ~4 words |
| **Total** | **~2.8 ms of 15.5 ms** | **~180 bytes** | **~140 µs of 16.68 ms** |

- T.62 — **Roughly 18 % of the drawing budget and 1 % of the CPU.** What is left is enough for a third parallax layer, a full-screen palette effect, or several times as many actors — and, more to the point, essentially the entire 65816 for game logic, which is what it should be doing. Synchronisation is the `SIGNAL 1` of [T.41](sec_t#t41): the CPU patches between the signal and the next `WAIT_VBLANK`, no lock is taken and the engine never stalls.
  NOTE: The per-frame patch loop is 48 32-bit stores and is the natural assembly candidate if the budget ever tightens — a hand-written version with absolute-long indexed addressing and a fixed unroll roughly halves it. It is not needed at 140 µs, and it depends on the Calypsi addressing question that already blocks the rest of the C on this machine (→ [Q19](sec_q#q19), [Q53](sec_q#q53)).

## Staging — N0 to N6, and where they land on the board's own build.

| Stage | Content | Acceptance | Board stage |
|---|---|---|---|
| **N0** | Mode 0 text; service port | Console at power-on, EC only, no CPU | **[E1.7](sec_p#e17)** |
| **N1** | SDRAM controller; Mode 1 scanout; register-driven fill and copy | Bitmap displayed; blitter fills a rectangle | E5 |
| **N2** | Modes 2a/2b; 256-colour palette; 8-bpp blitter; `COPY_KEYED`; double buffering | Animated multicolour scene at 60 Hz | E5 |
| **N3** | 1-bpp barrel-shift blitter; `COPY_EXPAND`; graphics cursor | GUI primitives — **the Amiga milestone becomes reachable** | E8 |
| **N4** | Command FIFO | The CPU stops polling `BLIT_BUSY` | E8 |
| **N5** | SDRAM command lists; flow control; `WAIT_LINE`; `SWAP_BUFFERS` | Autonomous frame rendering — the games target | E8 · + |
| **N6** | `DRAW_LINE`, `FILL_PATTERN`, `COPY_TILED` | — | + |

- T.63 — **Each stage is a usable machine, and if work stops at any of them nothing is stranded.** The progression is the same one the layering of [T.37](sec_t#t37)–[T.39](sec_t#t39) describes: a failure in the command processor still leaves a working GUI-capable blitter, and a failure in SDRAM still leaves a working boot console. N3 delivers the primary milestone and N5 the games capability, **in that order and separably**.
- T.64 — **The right-hand column is the finding, not the table.** [Sheet P](sec_p) currently puts everything about video at E5, three stages after the FPGAs are first configured. N0 needs no SDRAM, no CPU, no Helium and no bus — only Neon, the panel and the EC's SPI — so **it belongs at [E1.7](sec_p#e17), beside the blinky that proves configuration**, and it is a far better proof: a bitstream that puts 128 × 32 correct characters on a panel has demonstrated the PLL, the timing generator, EBR initialisation, the RGB pins, the FPC and the backlight in one shot. **Bringing it forward gives the whole project a screen three stages earlier than planned**, and every stage after it is easier to debug for having one (→ [Q43](sec_q#q43)).
  NOTE: Start below the gateware even so: the panel's own **BIST** mode generates test patterns with no external clock and no bitstream, which separates a panel, FPC or backlight fault from a logic fault before either can be blamed on the other ([E5](sec_p#e5)).

## Verification — stage N0, and four of these need no CPU.

- [ ] T.65 — **The panel syncs** and shows a stable image at 59.95 Hz with no visible tearing or jitter.
- [ ] T.66 — **At power-on, before any CPU activity**, the screen shows a blank field in the default background colour with correct borders.
- [ ] T.67 — **The EC writes glyphs through the service port** and they appear at the correct positions.
- [ ] T.68 — **`RASTER` tracks the display** and `STATUS.VBLANK` sets once per frame.
  NOTE: Those four are achievable **before the 65816 is populated**, which is the point of the whole arrangement and the intended [E1.7](sec_p#e17) path. The four below need the CPU or a test harness driving the aperture.
- [ ] T.69 — **Reading the text buffer back** through the aperture returns exactly what was written.
- [ ] T.70 — **Advancing `TEXT_START` by 128** scrolls the display by exactly one row, with no artefacts.
- [ ] T.71 — **The hardware cursor** appears at `CURSOR_POS`, in the selected shape, blinking at the selected rate.
- [ ] T.72 — **A font written through the aperture** takes effect on the following frame.

![Fig. 10 — Neon. The text path on the left depends on nothing but the bitstream and reaches the panel without touching SDRAM; the graphics path on the right is fed by commands, not pixels, and the CPU's only per-frame work is patching a list it built once.](figures/fig-10-neon.svg)
LEGEND: Trace legend: <span class="m">mint = pixel and command paths</span> · <span class="g">gold = the EC's service port and bitstream initialisation</span> · dashed = interrupts and the paths software should not use.
