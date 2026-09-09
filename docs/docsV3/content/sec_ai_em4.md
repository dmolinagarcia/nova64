# What was built
> the bus is the spine · the abort is a flag · and the I/O map is in the wrong bank

A skeleton with the architecture in place and a working subset of each part. C11, no dependencies beyond libc and libm, POSIX only for the console pty. This sheet is the report against the specification of [EM1](sec_ai_em1) and [EM3](sec_ai_em3): what exists, how it works, and where it is knowingly wrong. **Where the two disagree, the specification sheets are the specification and this one is the report.**

| Step | State |
|---|---|
| 65816 core, cycle-stepped, flat 16 MB | **partial** — structure complete, opcode subset, unvalidated |
| Record and replay | **partial** — replay works, no live producer outside the browser |
| Memory map with per-access hook | **done** |
| Parameter set and instrumentation | **done** |
| MMU, TLB, `ABORTB`, cache model | **done** — directed tests pass |
| Console, mailbox, Debug Agent parity | **partial** — console and mailbox present, Agent absent |
| SD block device | **done** — harness not yet pointed at it |
| Neon: blitter, text mode, compositor | **partial** — framebuffer, minterm blitter, text mode; no shifter, no edge masks, no compositor |
| Sweep campaign | **infrastructure only** |

## The bus layer is the spine, and that one decision carried the rest.

- EM4.1 — **Every 65816 cycle is a bus cycle**, so opcodes are written as straight-line sequences of read, write and idle calls in the order the real part performs them, and each call advances PHI2 by one cycle plus any stall charged by the MMU walk, the cache fill or a device. **Cycle accuracy is therefore a property of emitting the right accesses in the right order, not of a timing table kept in agreement with an interpreter.** There is no cycle count written anywhere in the CPU: `LDA abs` costs four cycles because it performs four accesses. The consequence that mattered later is that the MMU, the cache, the stall model and the abort all had one obvious place to live ([EM7.7](sec_ai_em7#em77)).

## The abort model — a flag, and it is more faithful than the unwind it replaced.

- EM4.2 — **`ABORTB` was a `setjmp`/`longjmp` unwind and is now a suppression flag, and the change fixed a fidelity bug rather than working around a port.** WebAssembly cannot unwind without the Exception Handling proposal, which forced the rewrite — but on the real part an abort **does not cut the instruction short**: it runs to its normal length with transfers and register updates suppressed, and the vector is taken afterwards with the register image the instruction started with ([E.16](sec_ai_e#e16)). The bus layer now sets a pending flag, further accesses in that instruction consume their cycles and touch nothing, and the saved register image is restored at the end. **The aborted instruction costs its true number of cycles** instead of however many it had reached when the fault occurred.
  NOTE: The trap here is suppressing the memory write but not the device access. An aborted instruction that still pokes an I/O register has side effects the hardware would not have — and [E.17](sec_ai_e#e17) makes the same point from the hardware end, where a write that has already driven the bus cannot be undone at all.

## The CPU core — a working subset, and not validated.

- EM4.3 — **The opcode map covers loads, stores, the ALU group, read-modify-write, branches, jumps and subroutines, stack, transfers, flag and mode control, `BRK`, `COP`, `WDM` and `STP`**, across direct page and its indexed forms, absolute and indexed, long and long indexed, the indirect forms including `[dp]` and `[dp],Y`, and stack-relative. **`MVN` and `MVP` are not implemented**, which is why [EM6.7](sec_ai_em6#em67) is arithmetic rather than measurement.
- EM4.4 — **An unimplemented opcode halts with the opcode and address printed rather than silently doing nothing.** A missing opcode that quietly does nothing turns into a program that runs and produces wrong answers, and finding that costs a day. Gaps are visible rather than latent.
- EM4.5 — [[!blocking]] **The core is not validated, and a program that runs proves nothing.** The external suites are the only third-party oracle in this entire project and they have not been wired in ([EM7.23](sec_ai_em7#em723)). Nothing measured by the emulator should be trusted more than the core underneath it.

## Memory, MMU and cache.

- EM4.6 — **Two backing arrays — 64 MB of system SDRAM and the 2 MB SRAM partitioned into cache data, page tables and pinned regions — with Neon's SDRAM a third and separate array** belonging to a separate device, as [F.12](sec_ai_f#f12) requires. The MMU implements 2 KB pages, 8192 entries per process, an ASID-tagged TLB, a hardware walk reading the SRAM partition and the four abort causes, per [sheet L](sec_ai_l). Directed tests written from that sheet pass; **there is no external oracle for any of it** ([EM7.27](sec_ai_em7#em727)).
  NOTE: The ASID tag is the detail worth not skipping. Without it a context switch silently lets one process read another's mappings — a bug that cannot appear until there are two processes and is then very hard to find.

## Neon — two paths, both real, and one of them is the wrong shape.

- EM4.7 — **The blitter is exact where it is implemented and absent where it is not.** All 256 minterms of A, B and C are correct, with `FILL`, `BLIT`, `POKE` and `PRESENT` behind a 12-byte command FIFO; `FILL` writes a colour index across planes and `BLIT` takes a mono source to the planes a mask selects, which is how coloured text is drawn on this kind of engine. **Not modelled: the word-granular shifter, the edge masks, descending mode and the A mask channel.** Blits are pixel-granular with A tied high — correct for aligned glyph work and wrong in the general case, which is exactly the state that gets trusted if it is not written down ([EM6.18](sec_ai_em6#em618)).
- EM4.8 — **Text mode holds the character ring inside Neon, advances the cursor and scrolls by moving a ring origin**, so the CPU stores characters and never issues a blit for text. **It scans out continuously rather than on `PRESENT`**, because a console that had to present a frame to show a character would not be a text mode — and the glyph store comes up initialised from a compiled-in font, standing in for bitstream initialisation. **Both of those were rediscovered rather than read**: [T1.53](sec_ai_t1#t153) already requires exactly this, on the same reasoning, from the hardware side.
- EM4.9 — [[!blocking]] **The emulator's display geometry and pixel format both contradict Neon as specified, and the second divergence is the more serious.** Text is modelled at **128 × 75 cells of 8 × 8 pixels** against [T1.53](sec_ai_t1#t153)'s **128 × 32 of 8 × 16**, which is a parameter and a buffer size. But the graphics modes are modelled as **bitplanes** — mono, 4 colour, 16 colour, 256 colour, with the blitter paying per plane — while [T1.33](sec_ai_t1#t133) and [sheet T2](sec_ai_t2) specify a **chunky 8 bpp framebuffer indexed into a 256-entry palette**. Those are different machines: an Amiga-style planar blitter and a chunky indexed one differ in the cost model, in the blit inner loop, and in what a `FILL` even means. **Every per-mode figure in [EM6.1](sec_ai_em6#em61) rests on the planar assumption** (→ [Q135](sec_ai_q#q135)).

| Emulator mode | fb bytes | Cells | Neon blit | CPU glyphs at 8 MHz |
|---|---|---|---|---|
| 0 · text 128 × 75 | 76,800 | 9600 | 1.54 ms | n/a |
| 1 · 1024 × 600 mono | 76,800 | 9600 | 1.54 ms | 62.4 ms |
| 2 · 1024 × 600, 4 colour | 153,600 | 9600 | 3.07 ms | 62.4 ms |
| 3 · 640 × 480 mono | 38,400 | 4800 | 0.77 ms | 31.2 ms |
| 4 · 640 × 480, 16 colour | 153,600 | 4800 | 3.07 ms | 31.2 ms |
| 5 · 512 × 384, 16 colour | 98,304 | 3072 | 1.97 ms | 20.0 ms |
| 6 · 320 × 240, 256 colour | 76,800 | 1200 | 1.54 ms | 7.8 ms |

  NOTE: **Two things the table is still worth reading for.** Planes cost what pixels cost, so the mode list is built to keep the framebuffer byte budget inside one band — depth and resolution become a genuine trade rather than two independent knobs. And **the two cost columns move independently**: Neon's cost tracks bytes while the CPU's tracks *cells*, because text in a graphics mode is one command per glyph. Between mode 1 and mode 6 the blitter cost is identical and the CPU cost differs eightfold, which is why any mode decision about text should look at cells. The blit column scales with an assumed 50 MB/s and [F.12](sec_ai_f#f12) budgets ~150 MB/s effective, so treat that column as pessimistic by roughly three.

## The invented I/O map, which is the emulator's own worst divergence.

- EM4.10 — [[!blocking]] **The emulator placed its console, its Neon command port and its microsecond counter in bank `$FE`, and that bank is Neon's user-mappable data aperture.** The corpus is explicit and recent: `$FE:0000`–`$FE:1FFF` is the text buffer, `$FE:2000`–`$FE:2FFF` the font, Neon's register file is at `$FF:8000` and Helium's blocks occupy `$FF:0000`–`$FF:7FFF` ([T1.19](sec_ai_t1#t119), [T1.21](sec_ai_t1#t121), [M.4](sec_ai_m#m4)). **[T1.21](sec_ai_t1#t121) moved Neon's registers out of `$FE` precisely to close a protection hole** — [J.5](sec_ai_j#j5) and [L.10](sec_ai_l#l10) let a process map aperture pages into its own address space, so a register file inside `$FE` is reachable by unprivileged code. The emulator's map reopens that hole and puts a character device on top of the text buffer at the same time. **Everything in its register header is provisional and the bank itself is wrong.**
  NOTE: One thing the emulator got right by accident and one it got right on purpose. [M.4](sec_ai_m#m4) already requires 16-bit registers accessed with `M=0`, which **is** the two-byte command port [EM6.2](sec_ai_em6#em62) argues for from measurement — so that finding is a confirmation of an existing decision rather than a new one. And the SD block device follows a shape a card can actually support, which is the part [G.6](sec_ai_g#g6) leaves to the register level.

## The SD card — a host image file, a block at a time.

- EM4.11 — **The card is a host image attached at startup, accessed through a 512-byte buffer**, because a byte-at-a-time interface to a block device is a category error: the card cannot do it and software written against one would not port. Set the LBA, issue a command, then read or write the data register 512 times; the pointer advances on each access and a command resets it. **The image is read and written in place rather than loaded**, since a card image is routinely larger than the machine's own RAM and the NVFS crash-injection harness needs to see the file after an abrupt stop ([sheet Y2](sec_ai_y2)). Reads past the end set the error bit rather than reading past the file, and opening read-only succeeds and fails writes, which is what a golden image wants.

## Determinism, and the four views onto what Neon did.

- EM4.12 — **Record and replay was built in from the start rather than added later**, as a log of `(cycle, event)` pairs at the mailbox boundary. It is cheap at the beginning and expensive afterwards, and by the time the thing being debugged is a preemptive scheduler, retrofitting it means touching everything ([EM7.42](sec_ai_em7#em742)).
- EM4.13 — **Four views, and they are blind to different things — which is the whole reason there are four.**

| View | Shows | Blind to |
|---|---|---|
| Live tee | every byte written to the text data port, streamed to stdout | everything Neon does |
| Character buffer | Neon's character ring, as text | glyph upload and rendering |
| Canvas or pixel preview | the real framebuffer | nothing |
| Serial console | a separate device entirely | Neon entirely |

  NOTE: **The first two are host-only aids that skip Neon, and that is what makes them convenient and what makes them dangerous.** A wrong glyph index, a bad font upload or a broken ring origin leaves both of them looking perfect while the screen is blank or garbled. **Watch software through the tee and check Neon through the pixels**; when the two disagree, the Neon model is wrong.
- EM4.14 — **There is no keyboard in the native build.** The mailbox has no live producer outside the browser and trace replay, so an echo loop prints its banner natively and nothing more. Interactive work belongs in the browser ([sheet EM5](sec_ai_em5)).

## Boot images, and the two traps the assembler will not catch.

- EM4.15 — **A boot image is a raw 8 KB binary covering `$00E000`–`$00FFFF` carrying its own vectors** — what the EC places in memory before releasing reset ([EM2.15](sec_ai_em2#em215)) — assembled with `ca65` and `ld65` from cc65 ([sheet O](sec_ai_o)). Four exist: the template, which comes up in text mode and echoes key presses; a console banner that then strides 32 KB to move the cache; text drawn by one blit per glyph; and hardware text mode with a 90-line scroll test.
- EM4.16 — **Register width is not tracked for you, and getting it wrong assembles cleanly and executes garbage.** `ca65` needs `.a8`, `.a16`, `.i8` and `.i16` to know how to assemble immediate operands, and they must match what `REP` and `SEP` actually did at run time. **It is the single most common way to lose an afternoon on this architecture** ([EM7.13](sec_ai_em7#em713)).
  NOTE: The second trap is smaller and the assembler does catch it: **`STZ` has no long addressing mode**, so zeroing a register in a far bank needs a load followed by a store.
