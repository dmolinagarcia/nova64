# What the emulator established
> the numbers · two evaluations · and the gaps it opened in this document

Everything below is provisional, and the reason is worth stating before the first table rather than after it: **these numbers rest on an invented command encoding, an assumed command size, an invented text register map and a bank that turned out to be the wrong one** ([EM4.10](sec_ai_em4#em410)). A different format moves them. What does not move is the shape of the conclusions, and three of them contradict working assumptions this document was carrying.

## The measurements.

- EM6.1 — **What the instrument has read so far.**

| Quantity | Result |
|---|---|
| Cache line fill, 32 B at 8 MHz | **3 PHI2 cycles**, ~340 ns |
| Page table walk | 20 ns stretched · a full cycle under wait states · **never zero** ([EM2.11](sec_ai_em2#em211)) |
| Neon command emission, byte loop through a 1-byte port | 188 cycles |
| Neon command emission, unrolled 16-bit stores, 2-byte port | **52 cycles** |
| Text mode, tight loop | **17 cycles per character** |
| Text mode scroll | **0 cycles** |
| Full 128 × 75 repaint by blitting | 62 ms |
| Stall fraction, 8 MHz / 50 MHz | 12.3% / **44.3%** |
| Effective speedup for a 6.25× clock increase | **3.97×** |
| Cache line size sensitivity, 8 MHz / 50 MHz | 4% / **26%** |

  NOTE: The fill figure was predicted by hand before it was measured — two bursts of `3 + 3 + 8 + 3` SDRAM clocks is 34 clocks is 340 ns is 2.7 PHI2 cycles — and the two agreed. **Had they not, one of the two was wrong and both were worth checking**, which is the only reason a sanity calculation is worth doing at all.
- EM6.2 — **The command port should be two bytes wide, and this document already says so.** On a 65816 a 16-bit store writes to an address and the one after it, so a one-byte port makes every command cost twice the stores it needs — the 52-cycle figure requires a two-byte port **and** an unrolled emitter, and the obvious counted loop costs 3.6× more. The emulator recorded this as a hardware decision it had surfaced; in fact [M.4](sec_ai_m#m4) already fixes registers at 16 bits accessed with `M=0`. **So the measurement confirms an existing decision rather than forcing a new one**, which is exactly the kind of agreement [EM1.3](sec_ai_em1#em13) was hoping for.
- EM6.3 — **The loop matters as much as the port.** Leaving the width-switching instructions inside the character loop costs 23 cycles per character; hoisting them out costs 17. **Six cycles, a third of the budget, from the placement of two instructions** — which says that the emission figures this subsystem is designed around are a property of the emitter as much as of the hardware, and that any number quoted for them has to name the code that produced it.
- EM6.4 — **Text mode's win is the scroll, not the per-character cost.** The per-character improvement over one blit per glyph is 3×, which is useful. The result that matters is that **characters already on screen are never re-sent and a scroll is a register write rather than a full repaint**, so incremental output costs only the characters actually written and a scrolling console costs nothing instead of 62 ms per line.
  NOTE: **It does not help the GUI**, and the limit is structural rather than incidental. A scan-out text mode is mutually exclusive with the graphical compositor, so text inside windows is still one command per glyph and the 62 ms figure still applies there ([sheet T3](sec_ai_t3)). This buys a fast console and monitor, not fast text in windows.
- EM6.5 — **The memory hierarchy is nearly invisible at 8 MHz and would not be at 50, so the finding has an operating point attached.** Cache geometry looking second-order is an artifact of the clock: at 8 MHz the stall fraction is 12.3% and line size moves total cycles by 4%; at 50 MHz they are 44.3% and 26%. **If the softcore of [sheet E](sec_ai_e) is ever populated, cache design becomes a first-order problem and the geometry open item becomes urgent.** A conclusion quoted without its operating point will outlive the operating point.
- EM6.6 — **The associativity sweep produced a clean, consistent, worthless answer.** The toy workload strides linearly through 32 KB and never conflicts, so it cannot distinguish direct-mapped from four-way, and it reported no difference at every setting. **That looks like a finding and is an artifact of the test program.** Answering the associativity question needs real kernel and GUI workloads ([EM6.21](sec_ai_em6#em621)).

## Evaluated, not implemented — block moves through a Neon aperture.

- EM6.7 — **The proposal is to let `MVN` stream command lists and glyph data into Neon without a store loop.** The 65816's block moves take a source and destination bank, use the index registers as pointers and the accumulator as a count, auto-increment, and move at **7 cycles per byte** — the fastest bulk copy the part has. **The aperture is a requirement rather than a convenience**: `MVN` increments its destination pointer, so a single-address FIFO cannot receive it, and the window must **alias across a whole bank** — ignoring the low address bits and appending to the FIFO wherever in the window the write lands — or the pointer walks out of a 256-byte window after 256 bytes and the move has to be broken into chunks, reintroducing the loop the instruction was meant to remove. Aliased, one instruction moves up to 64 KB. In hardware it is address decode, not new datapath, and it is additive to the existing register port.
- EM6.8 — **The cost, against the alternatives, and the headline comparison flatters it.**

| Path | Cycles/byte | At 8 MHz | 512-byte font | 9,600-byte window list |
|---|---|---|---|---|
| Unrolled 16-bit stores, operand in direct page | 4.33 | 1.85 MB/s | 277 µs | 5.2 ms |
| **Block move through an aperture** | **7.00** | **1.14 MB/s** | **448 µs** | **8.4 ms** |
| Indexed load and long store, streaming from SDRAM | 10.00 | 0.80 MB/s | 640 µs | 12.0 ms |
| Byte loop through a one-byte port | 15.67 | 0.51 MB/s | 1003 µs | 18.8 ms |

  NOTE: The top row exceeds the ~1.3 MB/s that [T1.2](sec_ai_t1#t12) budgets for the aperture, and is close to its ~1.6 MB/s best case. The discrepancy is small and unexplained, and it is a reason to treat both numbers as the same estimate rather than as an independent confirmation.
- EM6.9 — **Against the emitter that produced the measured 52 cycles, block moves are 62% slower per byte, so the honest scope is narrow.** For emitting a single command the CPU has just computed in direct page, the stores win outright. **The comparison that matters is against streaming a *pre-built* list out of SDRAM** — which is what the per-window command sublists of [T1.39](sec_ai_t1#t139) are. There the store loop needs an indexed load, a long store, an increment and a compare per two bytes: ten cycles per byte against seven. The block move is 30% faster, about six instructions shorter, and takes no register pressure beyond the three it already uses. **Block moves for pre-built lists, stores for one-off commands. Both, not either.**
- EM6.10 — **It is not the answer for bulk pixel data and does not change the order of magnitude.** 1.14 MB/s means a 76,800-byte mono framebuffer takes 67 ms to push and a four-plane mode takes 135 ms. If bitmap upload ever becomes a real workload, **neither instruction sequence is the answer and a DMA engine in Helium is** — reading SDRAM and feeding Neon's port at bus rate, costing the CPU nothing. That question should be decided on its own terms rather than as a corollary of this one (→ [Q135](sec_ai_q#q135)).
- EM6.11 — [[!blocking]] **[E.21](sec_ai_e#e21) forbids `MVN` and `MVP` against pageable memory, categorically, and Neon's aperture is pageable.** That ban exists because a block move straddles many pages, its per-byte state is mid-instruction, and the abort corruption windows of [E.18](sec_ai_e#e18) sit inside it. **Bank `$FE` is user-mappable by [L.10](sec_ai_l#l10) and [J.5](sec_ai_j#j5) maps aperture pages into a process**, so a block move into the aperture is a block move against pageable memory whichever end faults. Either the aperture pages are pinned for the duration, or the ban gets an explicit and argued exception, or the proposal does not survive. **It is not enough that the destination is a device.**
- EM6.12 — **Three consequences to weigh, and one of them decides whether it can be measured at all.** The instruction **loads the data bank register as a side effect** and software has to restore it. It is **interruptible** and restarts — good for latency, but an interrupt handler that touches Neon mid-move leaves interleaved bytes in the FIFO, so it needs a lock or a rule. And **neither instruction is implemented in the core** ([EM4.3](sec_ai_em4#em43)), so every figure in [EM6.8](sec_ai_em6#em68) is arithmetic derived from a datasheet cycle count. **Implement them and measure before adopting any of this.**

## Evaluated, not adopted — an external 65816 core.

- EM6.13 — **`lib65816` was assessed as a replacement for the core written here and rejected for three independent reasons, any one of which is sufficient.** It is **GPL-3.0 rather than LGPL**, so linking it makes the whole emulator GPL-3.0 and serving a WebAssembly module to a browser is distribution — a licensing decision rather than a technical one, and on its own probably decisive. **Its timing comes from a per-opcode table rather than from the bus**, which is the architecture [EM4.1](sec_ai_em4#em41) argues against, and the consequence is not aesthetic: it **forfeits validation against the external suites**, which compare the *sequence* of bus cycles rather than the total. **The library would be taken on for a validated core, and taking it on removes the means of validating it.** And **its aborts are recognised at instruction boundaries** — the faulting instruction runs to completion with the offending write already committed, and the vector is taken afterwards. For demand paging that is not a rough edge, it is wrong ([EM4.2](sec_ai_em4#em42), [E.16](sec_ai_e#e16)).
- EM6.14 — **Where it would genuinely be useful is as an oracle run as a separate program, never linked.** Compare final register and memory state per instruction against ours; two programs exchanging data are not a derived work, so the licence does not reach us — worth confirming properly rather than on this reading. The value is real, because **outside the CPU this project has no external oracle at all** ([EM7.27](sec_ai_em7#em727)). **It ranks second, though**: the cycle-sequence suites are strictly stronger and carry no licensing entanglement, so wire those in first and reach for this only if a discrepancy appears that they do not explain.
- EM6.15 — **One thing worth borrowing regardless.** It routes the reserved `WDM` opcode to a host callback, which is the same escape-hatch shape [EM7.26](sec_ai_em7#em726) recommends for CPU extensions — evidence that the approach is conventional rather than novel.

## The gaps this work opened in the corpus.

- EM6.16 — **None of these is an emulator question.** Each is something the emulator had to invent because no sheet fixed it, or something it surfaced as a consequence of a decision taken here. The right-hand column is what this document currently says.

| Gap | State in this document |
|---|---|
| A fixed-frequency counter software can read | **New requirement**, created by the stretched clock ([EM2.6](sec_ai_em2#em26)). Only a 100 Hz tick exists; the register block is part of what [Q22](sec_ai_q#q22) is owed |
| The touch panel discarded; GT911 and its I2C bus released | **Consequences unreflected** — [C.13](sec_ai_c#c13), [H.3](sec_ai_h#h3), the EC pin budget, and the panel part number ([EM3.11](sec_ai_em3#em311)) |
| Neon command encoding, opcodes and the 12-byte command size | Open. The 12-byte assumption forced a restriction a different size would remove |
| Text mode register map, cell geometry, character buffer size | **Contradicted** — [T1.53](sec_ai_t1#t153) fixes 128 × 32 at 8 × 16 ([EM4.9](sec_ai_em4#em49)) |
| Framebuffer organisation: planar against chunky indexed | **Contradicted** — [T1.33](sec_ai_t1#t133) and [sheet T2](sec_ai_t2) fix 8 bpp indexed ([EM4.9](sec_ai_em4#em49)) |
| The I/O window base | **Contradicted** — [T1.21](sec_ai_t1#t121) and [M.4](sec_ai_m#m4) fix the banks ([EM4.10](sec_ai_em4#em410)) |
| Neon command port width | **Already fixed** at 16 bits by [M.4](sec_ai_m#m4) ([EM6.2](sec_ai_em6#em62)) |
| Glyph store: ROM, writable RAM, or both | Open. Initialised from the bitstream in both models ([T1.53](sec_ai_t1#t153)) |
| Scancode set; whether the EC delivers make and break codes | Open ([EM3.9](sec_ai_em3#em39)) |
| Pointer transport, and delta against absolute position | Open, and it decides [EM3.13](sec_ai_em3#em313) |
| Target-side character device for the console | Open, and narrower than it looked ([EM3.6](sec_ai_em3#em36)) |
| Cache and TLB geometry, replacement and write policy | Open. [EM6.21](sec_ai_em6#em621) is meant to close it |
| Page table entry bit layout | Open against [sheet L](sec_ai_l) |
| A bank-aliased write aperture, and a DMA path at all | Open, and constrained by [EM6.11](sec_ai_em6#em611) |
| SD register layout as Helium presents it | Placement fixed by [G.6](sec_ai_g#g6); the registers themselves open |
| SDRAM controller clock and burst configuration; arbitration policy | Open |

## Pending work.

- EM6.17 — [[!blocking]] **Wire the external 65816 test suites into the build, before anything else on this list.** They are the only external oracle in the whole project, nothing in the core should be trusted until they pass, and **they will find bugs** ([EM7.23](sec_ai_em7#em723)). This acquires a second edge now that the CPU is allowed to diverge from a stock part: **additive divergence behind the reserved `WDM` prefix leaves the suites valid; modifying an existing opcode invalidates them exactly where it lands**, and that should be a deliberate choice rather than a discovery.
- EM6.18 — **The emulator's own gaps, in rough order.** Complete the opcode map, which is mechanical. Add the blitter's word-granular shifter, edge masks, descending mode and A mask channel, which [EM1.8](sec_ai_em1#em18)'s bit-exactness requires. Point the existing NVFS harness at the SD block device, which exists and has not been used ([sheet Y2](sec_ai_y2)). Implement the block moves and measure [EM6.8](sec_ai_em6#em68) instead of calculating it. Build Debug Agent parity ([EM3.3](sec_ai_em3#em33)). Add dirty-region tracking to the two renderers ([EM5.14](sec_ai_em5#em514)). Give the native build an input path. **Compositor and damage tracking are deferred by decision** — the GUI is not the current target — and audio is not required by any gate.
- EM6.19 — **And the browser shell needs a person to look at it once**, on a real display with a real mouse: pointer lock under load, key events with modifiers and repeat, and canvas scaling where 1024 pixels do not map one to one ([EM5.17](sec_ai_em5#em517)).

## Where it stands against its own build order.

- [x] EMU1 — **65816 core, cycle-stepped, flat 16 MB, with record and replay infrastructure.**
  TEST: the external suites pass. **Not met** — the core runs and is unvalidated ([EM6.17](sec_ai_em6#em617)).
- [x] EMU2 — **Memory map layer with a per-access hook; SRAM and SDRAM region model.**
  TEST: regions decoded correctly and the hook fires on every access.
- [x] EMU3 — **Parameter set loader and instrumentation counters.**
  TEST: parameters settable at run time, counters readable and plausible.
- [x] EMU4 — **MMU with hardware walk, ASID-tagged TLB and `ABORTB`; cache controller with its cost model.**
  TEST: directed tests written from [sheet L](sec_ai_l) pass.
- [~] EMU5 — **Character device, console on terminal and virtual serial, mailbox interface, Debug Agent parity.**
  TEST: the monitor runs under the emulator and is reachable from a standard terminal program. **The emulated Apple II milestone.** Console and mailbox exist; the Agent does not.
- [~] EMU6 — **SD block device, with the NVFS harness running against it.**
  TEST: the existing NVFS suite passes unmodified. The device exists; the harness has not been pointed at it.
- [~] EMU7 — **Neon command interface, bit-exact blitter, backing stores, damage tracking, frame-granularity compositor, host window. HID input over the mailbox and the capture toggle.**
  TEST: a windowed GUI composites correctly and responds to keyboard and mouse. **The emulated Amiga milestone.** Blitter and text mode exist; there is no compositor.
- [ ] EMU8 — **Parameter sweep campaign.**
  TEST: a cache geometry recommendation and a Neon emission cost report are issued, and the sheets they feed are revised.
- EM6.20 — **The order is sequential and it was inserted into the plan ahead of the kernel gates**, because the emulator unblocks kernel work without depending on the prototype bring-up of [sheet P3](sec_ai_p3). That is [EM1.1](sec_ai_em1#em11) expressed as a schedule rather than as a purpose.
- EM6.21 — [[open]] **The campaign that closes the geometry questions has not run, and the toy workload is why.** [EM6.6](sec_ai_em6#em66) showed a synthetic program giving a confident wrong answer, so the sweep has to run against **real kernel and GUI workloads** before it can recommend anything — and those workloads are themselves the output of the development the emulator exists to enable. **The two are the same piece of work approached from opposite ends**, which is the honest reason this step is last and the reason it should not be quietly skipped once the kernel runs.
