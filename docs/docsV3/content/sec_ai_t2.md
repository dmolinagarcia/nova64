# Neon — palette and line control
> two banks · one write port · a global offset · and a beam nobody can chase

The framebuffer is 8 bpp indexed, so a framebuffer byte is not a colour: it is an index into a table software owns. This sheet is that table and the registers around it — how it is stored, how it is written, how it is switched atomically, and what the line comparator beside it can and cannot be used for. It is the register contract, not the schematic; the wires are in [sheet U1](sec_ai_u1). It supersedes the palette of [T1.49](sec_ai_t1#t149) outright, and in doing so it spends block RAM the budget of [T1.12](sec_ai_t1#t112) did not have.

## Two principles, and the second is why this sheet exists at all.

- T2.1 — **Every effect achieved by moving the palette emits no commands to Neon.** Reloading the whole table touches no framebuffer byte, so fades, colour cycling, selection highlighting and theme changes cost register writes rather than blitter work. That matters here more than it did on the machines the technique comes from, because **CPU-side command emission is the binding constraint on the subsystem** ([T1.2](sec_ai_t1#t12), [T3.3](sec_ai_t3#t33)) and this is one of the few mechanisms that sidesteps it entirely.
- T2.2 — **The table is addressed through a register port, not mapped into the aperture.** [T1.49](sec_ai_t1#t149) put 256 entries of four bytes at `$FE:8000` and argued the palette belongs with the framebuffer because it is bulk data and nothing about it is privileged. **That is withdrawn** (→ [D68](sec_ai_q#d68)). Two banks cannot be exposed as one linear kilobyte without either doubling the aperture footprint or adding a bank bit to the address decode, and an auto-incrementing index port loads a full table in fewer accesses than the mapped version it replaces.

## The colour model — 256 at once, out of a space the pins decide.

| Property | Value |
|---|---|
| Simultaneous colours | 256 |
| Candidates per entry, RGB666 output | 262,144 |
| Candidates per entry, RGB888 output | 16,777,216 |
| Available greys, RGB666 output | 64 |

- T2.3 — **Storage is 24-bit RGB888 regardless of what reaches the pins** (→ [D69](sec_ai_q#d69)), and the reason is that it costs nothing. A 256 × 18 table needs two block RAMs and a 256 × 24 table needs two as well ([T2.5](sec_ai_t2#t25)), so the wider entry is free — and an 18-bit table would bind the software ABI to the physical routing, so a later board revision with spare pins would break binary compatibility with everything already written. [U1.20](sec_ai_u1#u120) routes RGB666 today; **that is a board fact, not an ABI fact**, and the two are deliberately not the same number here.
- T2.4 — **The expected allocation, recorded because [T2.9](sec_ai_t2#t29)'s offset is designed against it.** Software is not expected to use 256 arbitrary colours.

| Range | Purpose |
|---|---|
| ~16 entries | System colours: desktop, borders, active and inactive title bars, text, shadow, highlight |
| ~200 entries | General purpose: grey ramp, saturated primaries, a coarse colour cube for images |
| Remainder | Cursor and direct-write scratch |

  NOTE: A palette manager in the OS has to arbitrate this between applications, and it does not exist in any sheet yet. It is a component of the GUI rather than of Neon, and it has to be provisioned before an application-level colour API is defined at all (→ [Q135](sec_ai_q#q135), [sheet V](sec_ai_v)).

## Storage — three block RAMs, and the budget they come out of is already spent.

| Block | Configuration | Contents |
|---|---|---|
| 1 | 256 × 16 | Bank A, red and green |
| 2 | 256 × 16 | Bank B, red and green |
| 3 | 512 × 8 | Blue for both banks — A at 0–255, B at 256–511 |

- T2.5 — **Three blocks, and packing both banks' blue into one 512 × 8 block is what saves the fourth.** The naive allocation is 2 + 2; the iCE40 EBR's selectable width configurations make 512 × 8 a single block, so the two blue channels share one.
- T2.6 — [[!blocking]] **This does not fit, and the sheet says so rather than assuming a relief.** [T1.11](sec_ai_t1#t111) budgets **2 blocks** for the palette inside a total of **32 of 32 with zero margin** ([T1.12](sec_ai_t1#t112)), and [T3.38](sec_ai_t3#t338) already showed the combined design wanting 34 or 35 before this sheet asked for a third. **The reliefs are known and are the same ones**: a 128-glyph font frees four blocks ([Q45](sec_ai_q#q45)) and the cursor at 2 bpp rather than 4 gives back half of one ([T3.29](sec_ai_t3#t329)). **Which of them is taken is left open here deliberately** — it is a synthesis-informed decision and there is now more than one claimant on the same relief (→ [Q133](sec_ai_q#q133), [Q61](sec_ai_q#q61)).

## The write protocol — an index port and two 16-bit writes.

- T2.7 — **Per entry: one write to the low data register, then one to the high.** The high write commits the entry and increments the index. A full palette load is one index write followed by 512 data writes, **513 accesses**. The index increments modulo 256 and never wraps into the other bank; the write bank is selected only by the control register.
  NOTE: **Three 8-bit writes per entry, the VGA protocol, is rejected on the constrained resource.** It costs 769 accesses against 513, and at roughly 50 cycles per Neon command that is about **12,800 CPU cycles per palette load**. Two 16-bit writes also let Calypsi emit word stores straight out of a `uint32_t` array, which the byte protocol cannot.
  NOTE: The high-byte commit is the same discipline as [T1.48](sec_ai_t1#t148) and [T3.24](sec_ai_t3#t324) — a multi-byte register takes effect on the write to its highest byte, so a half-written value can never be acted on. **Worth keeping identical across every block in Neon**, and it is.

## Bank switching — the whole point of two banks is that the change is atomic.

- T2.8 — **The intended pattern is four steps.** Select the inactive bank for writes · write entries at leisure, across as many frames as necessary · arm the swap · flip the active bank. Hardware latches the change at the next vertical blanking and clears the pending flag. **There is no intermediate state in which some entries are new and others old, and there is no copy operation.**
  NOTE: Immediate mode stays available, with the swap disarmed, and is expected during boot, in text mode, and wherever latency matters more than a single-frame artefact. Writing the *active* bank during active scanout produces a visible flash on the line being drawn, which is a fair price in a console and not in a GUI.
  NOTE: **A single bank with a deferred-write buffer flushed at vblank was considered and is withdrawn**, recorded here so it is not reintroduced later as an apparent improvement. To defer a full palette load the pending buffer must hold 768 bytes, which is exactly one table — so the block RAM cost is identical to dual banking, but with a copy state machine on top and 256 cycles of flush during blanking. **Dual banking makes the buffer be the table.**

## The global offset — one register write instead of sixteen.

- T2.9 — **An 8-bit offset is added to the framebuffer index, modulo 256, before the table is addressed.** With sixteen entries per sub-palette, changing the appearance of an entire region is one store rather than a re-upload. Anticipated uses are GUI themes, active and inactive window styling, selection highlighting, and flash and fade effects. As with bank switching the value is not the visual trick: **it converts N CPU operations into one, on the resource that is actually scarce.**
  NOTE: The offset register is the obvious target for per-line modification, which is what makes the deferred mechanisms of [T2.14](sec_ai_t2#t214) worth reserving space for and what [T2.12](sec_ai_t2#t212) then rules out for the CPU.

## Output truncation — a software mode rather than a board decision.

| Setting | Bits per channel | Output space | Greys |
|---|---|---|---|
| `00` | 8 | 16.7 M | 256 |
| `01` — reset value | 6 | 262,144 | 64 |
| `10` | 4 | 4,096 | 16 |

- T2.10 — **How many high bits of each stored channel reach the pins is a runtime register field**, applied in the output pad register after cursor compositing. Since [U1.20](sec_ai_u1#u120) carries RGB666 at the boundary, selecting 8 bits per channel has no effect on current hardware. **The field exists so that RGB444 is a software mode and so that a future board routing RGB888 needs no RTL and no ABI change** — which is the same argument as [T2.3](sec_ai_t2#t23), stated from the other end.

## The cursor palette — its own port, and that is the point of it.

- T2.11 — **The hardware cursor holds a small palette of its own and is composited in the final stage**, by comparing the current pixel coordinate against the cursor position and multiplexing over the framebuffer pixel, so pointer motion costs no SDRAM bandwidth and no compositor work. That is the same conclusion [T1.31](sec_ai_t1#t131) and [T3.29](sec_ai_t3#t329) reach from bandwidth; what this sheet adds is that **the cursor palette gets its own independently addressed write port.** Sharing the main index port would put pointer shape changes into contention with palette loads, which is precisely the traffic the sheet is arranged to avoid.
  NOTE: Entry count depends on cursor bit depth, and that is still carrying two numbers across three sheets — 4 bpp in the source note, 2 bpp in [T3.29](sec_ai_t3#t329), 2 bpp in [T1.31](sec_ai_t1#t131). **T3.29's reconciliation is the one to take**: 2 bpp at 32 × 32 is 256 bytes, it leaves half a block spare, and it gives three colours plus transparent, which is what all three descriptions actually wanted (→ [Q134](sec_ai_q#q134)).

## Line comparison — implemented, and not for the reason it usually is.

- T2.12 — **A comparator against a logical scanline raises an interrupt, and a live line counter is readable at any time.** Line numbers are **logical, matching the framebuffer, not panel scanlines**: with the vertical duplication of [U1.6](sec_ai_u1#u16) active, logical line *n* is panel lines 2*n* and 2*n*+1, and the interrupt is raised at the start of the first.
- T2.13 — **Per-line palette modification by the CPU is not possible, and this is a time budget rather than an optimisation problem.** The figures are for 1920 × 1080 at 60 Hz against the 65816 at 8 MHz.

| Interval | Duration | 65816 cycles |
|---|---|---|
| Full line | 14.81 µs | ~118 |
| Active region | 12.93 µs | ~103 |
| **Horizontal blanking** | **1.89 µs** | **~15** |
| Vertical blanking, 45 lines | 666 µs | **~5,330** |

  NOTE: The interrupt sequence alone consumes 7 to 8 cycles acknowledging, pushing the program bank, counter and status, and fetching the vector. Adding register preservation **exhausts the horizontal blanking budget before the first useful instruction executes**. At 1366 × 768 the figure lands between roughly 15 and 20 cycles and the conclusion does not move. The comparison is a 148.5 MHz beam against an 8 MHz CPU, a ratio about forty times worse than the 8-bit machines whose raster effects motivate the technique; **the Amiga did it with the Copper, not with the 68000.**
  NOTE: The vertical blanking interrupt, at roughly 5,330 cycles, is comfortable for a full palette reload — and with [T2.8](sec_ai_t2#t28)'s dual banking even that urgency is gone. **The comparator is implemented for beam synchronisation, tear-free update scheduling and knowing where the beam is**, and those are good enough reasons on their own.
  NOTE: **Two register sets now describe the same hardware and only one can survive.** [T1](sec_ai_t1) already defines a raster counter, a compare register and an interrupt enable and status pair in Neon's `$FF:8000` file, with vertical blank, raster compare and command-list signal as its three sources. This sheet defines a logical line counter, a line compare, and its own enable and write-1-to-clear status. **They are the same mechanism named twice**, and merging them is part of the same exercise as placing this block at all (→ [Q138](sec_ai_q#q138), [Q132](sec_ai_q#q132)).

## Reserved — a display list processor, and the sheet it has to be reconciled with.

- T2.14 — **Sixteen register offsets and one SDRAM arbiter client are reserved and not implemented.** The intended mechanism is a Copper-class state machine reading a list of line, register and value entries from Neon's SDRAM and executing them during horizontal blanking **at Neon's clock rate rather than the CPU's** — at 74.25 MHz the 1.89 µs blanking interval gives roughly 140 cycles, room for dozens of register writes per line. Estimated cost is a few hundred LUT4 for the pointer, comparator, fetch state machine and arbiter port.
- T2.15 — [[open]] **This overlaps [T1.39](sec_ai_t1#t139) and the two sheets have not been reconciled.** T1 already specifies a command processor executing lists from SDRAM, with `WAIT_LINE`, `SWAP_BUFFERS` and a mid-frame `SET_PALETTE`, budgeted at 800 LUT and placed at stage N5 — which is most of what this reservation describes, already committed rather than reserved. **What is genuinely new here is only the per-line register-write form**, and whether that arrives as opcodes in T1's list or as a second engine beside it is undecided (→ [Q139](sec_ai_q#q139)).
  NOTE: [T2.13](sec_ai_t2#t213)'s analysis argues *for* T1's list rather than against it. The reason the CPU cannot chase the beam is that it is the CPU; a processor inside Neon at Neon's clock is exactly the escape, and T1 already budgeted one.
  NOTE: A cheaper alternative was identified and is left open: a small block RAM table indexed by logical line, holding four bits per line — bank selection plus a sub-palette offset. At 540 logical lines that is 2,160 bits, one block configured 1024 × 4, plus one read per line and a multiplexer in the index path. It covers split screens, horizontal bands and zone tinting, **but not per-line distinct colours**, and it is worth building **only if the display list processor is ruled out permanently**, since the processor subsumes it.

## Register names — the addresses are open and the names are not.

- T2.16 — **The block's base address is unallocated**, pending reconciliation with Neon's existing register map ([T1](sec_ai_t1) at `$FF:8000`, [T3](sec_ai_t3) inside the same page). Offsets below are relative to that base and are the source note's, kept so the ordering and the widths are on record; **they are provisional and the base is the blocking half** (→ [Q132](sec_ai_q#q132)).

| Name | Access | Width | Description |
|---|---|---|---|
| `CLUT_INDEX` | W | 16 | Entry index 0–255, auto-incrementing after each committed entry |
| `CLUT_DATA_LO` | W | 16 | Green in bits 15:8, blue in bits 7:0 |
| `CLUT_DATA_HI` | W | 16 | Red in bits 7:0. **The write commits the entry and increments the index** |
| `CLUT_CTRL` | R/W | 16 | Write bank · active bank · swap on vsync · output depth |
| `CLUT_OFFSET` | R/W | 16 | Global index offset, bits 7:0 ([T2.9](sec_ai_t2#t29)) |
| `CLUT_STATUS` | R | 16 | Swap pending · in vertical blanking |
| `CURSOR_CLUT_INDEX` | W | 16 | Cursor palette index, auto-incrementing ([T2.11](sec_ai_t2#t211)) |
| `CURSOR_CLUT_DATA_LO` · `_HI` | W | 16 | As the main data pair; the high write commits |
| `LINE_COMPARE` | R/W | 16 | Logical scanline at which the interrupt is raised |
| `LINE_CURRENT` | R | 16 | Live logical scanline |
| `VID_INT_ENABLE` | R/W | 16 | Vertical blank start · line compare |
| `VID_INT_STATUS` | R/W1C | 16 | Same bits, write 1 to clear |
| — | — | — | Reserved: display list processor, 16 bytes ([T2.14](sec_ai_t2#t214)) |

  NOTE: The interrupt bits aggregate into Neon's single line to Helium's controller the same way [T1.47](sec_ai_t1#t147) describes, and the kernel demultiplexes by reading status. **Nothing here changes the interrupt topology**; it changes how many status registers there are to read, which is [Q138](sec_ai_q#q138).

## What this sheet supersedes, and what it leaves standing.

| In [sheet T1](sec_ai_t1) | Status |
|---|---|
| [T1.49](sec_ai_t1#t149) — 256 × 18-bit palette at `$FE:8000`, four bytes per entry | **Superseded.** Register port, two banks, 24-bit entries ([D68](sec_ai_q#d68)) |
| [T1.11](sec_ai_t1#t111) — palette at 2 block RAMs | **Superseded.** Three blocks, and the budget no longer closes ([Q133](sec_ai_q#q133)) |
| [T1.29](sec_ai_t1#t129) — first 16 entries initialised from the bitstream | **Stands.** Both banks initialise; a correct screen at power-on is unaffected |
| [T1.31](sec_ai_t1#t131) — hardware cursor composited at scanout | **Stands**, with a separate palette port added ([T2.11](sec_ai_t2#t211)) |
| [T1.39](sec_ai_t1#t139) — command lists with `SET_PALETTE` and `WAIT_LINE` | **Unreconciled** ([T2.15](sec_ai_t2#t215), [Q139](sec_ai_q#q139)) |
| T1's raster counter, compare and interrupt registers | **Unreconciled** ([Q138](sec_ai_q#q138)) |

- T2.17 — **The one thing that must not be lost in the merge is [T1.1](sec_ai_t1#t11).** Text mode depends on nothing: its buffers live in block RAM and initialise from the bitstream, and the machine prints before software exists. A palette reached only through a register port is still reachable by the EC over the service port ([T1.46](sec_ai_t1#t146)), so that ordering survives — but it is now one more thing the service port has to be able to drive, and it belongs in the stage N0 acceptance rather than being assumed.

## Abandonment conditions — what to do if the budget does not close.

| Condition | Action |
|---|---|
| Neon's LUT4 use exceeds the ceiling and dual banking is on the critical path | Drop to one bank and reinstate the deferred-write buffer of [T2.8](sec_ai_t2#t28), accepting the copy state machine. **Do not** drop bank switching in favour of per-entry immediate writes |
| Block RAM pressure leaves fewer than three blocks for the table | Reduce to 18-bit storage — two blocks, still dual bank — and record the resulting ABI restriction explicitly. This breaks [D69](sec_ai_q#d69) and must be a documented revision, never a silent change |
| The display list processor is formally ruled out | Close [T2.14](sec_ai_t2#t214), release the reservation, and evaluate the per-line control table on its own merits |
| The line comparator proves unused once the GUI gates close | **Retain it.** It costs one comparator and it is a diagnostic asset during bring-up |

## Verification — three of these need no CPU.

- [ ] T2.18 — **A full palette load through the index port** reads back entry for entry, in both banks.
- [ ] T2.19 — **An armed bank swap takes effect at the following vertical blank** and never mid-frame, with the pending flag clearing on the swap.
- [ ] T2.20 — **Changing the global offset by 16** shifts every displayed colour by one sub-palette with no framebuffer write.
- [ ] T2.21 — **The output depth field visibly quantises** the grey ramp at 4 bits per channel and is indistinguishable from 6 at the current pin routing.
- [ ] T2.22 — **The line compare interrupt fires once per frame** at the programmed logical line, and the live counter agrees with it.
