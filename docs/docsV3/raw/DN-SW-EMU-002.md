# DN-SW-EMU-002 — noVa64 Emulator Implementation

**Domain:** SW (software)
**Topic:** EMU (host-side system emulator)
**Revision:** B
**Status:** Active
**Language:** English

**Relation to DN-SW-EMU-001:** that note fixes *what* the emulator should
model and to what fidelity. This one records *what was built*, how it works,
how to use it, and what is missing. Where the two disagree, DN-SW-EMU-001 is
the specification and this note is the report.

---

## Revision history

| Rev | Status | Summary |
|-----|--------|---------|
| A   | Superseded | First issue. Covers the emulator core, the WebAssembly build and web delivery, code structure, usage, measured results, and pending work. |
| B   | Active | Adds the SD block device; adds two evaluations that record work considered and its outcome: MVN through a NEON aperture (5b, worth doing, narrowly scoped) and lib65816 (5c, not adopted). Records the stretched-clock decision and the fixed-frequency counter it requires. |

---

## 1. How the emulator works

### 1.1 The bus layer is the spine

Every W65C816S cycle is a bus cycle; the part has no internal cycles that stay
off the bus. So opcodes are written as straight-line sequences of `bus_read8`,
`bus_write8` and `bus_idle` calls in the order the real part performs them, and
each call advances the PHI2 counter by one cycle plus any stall charged by the
MMU walk, the cache fill or an I/O device.

Cycle accuracy is therefore a property of emitting the right sequence of
accesses, not of a separate timing table that has to be kept in agreement with
the interpreter. `src/bus.c`.

### 1.2 Abort model

ABORTB was originally a `setjmp`/`longjmp` unwind. WebAssembly cannot do that
without the Exception Handling proposal, and replacing it produced a **more
faithful** model rather than a compromise.

On the real part an abort does not cut the instruction short: the instruction
runs to its normal length with transfers and register updates suppressed, and
the abort vector is taken afterwards with the register image the instruction
started with. The emulator now sets `abort_pending` in the bus layer; further
accesses in that instruction consume their cycles and touch nothing; `cpu_step`
restores the saved register image at the end and vectors.

The aborted instruction now costs its true number of cycles instead of however
many it had reached when the fault occurred.

### 1.3 CPU coverage

A working subset of the opcode map: loads, stores, the ALU group, read-modify-
write, branches, jumps and subroutines, stack, transfers, flag and mode
control, `BRK`, `COP`, `WDM`, `STP`. Addressing modes cover direct page and its
indexed forms, absolute and indexed, long and long indexed, the indirect forms
including `[dp]` and `[dp],y`, and stack-relative.

Unimplemented opcodes halt with the opcode and address printed rather than
silently doing nothing, so gaps are visible rather than latent.

**The core is not validated.** A program that runs proves nothing. See §7.

### 1.4 Memory, MMU and cache

Two backing arrays: 64 MB system SDRAM and the 2 MB SRAM, partitioned into
cache data, page tables and pinned regions. NEON's SDRAM is a separate array
belonging to a separate device.

The MMU implements 2 KB pages, 8192 entries per process, an ASID-tagged TLB, a
hardware page table walk reading the SRAM partition, and the four abort causes.
Directed tests written from the DN memory model are in `tests/test_mmu.c`;
there is no external oracle for this subsystem.

The cache model holds **tags only**. Line data is not duplicated because the
backing arrays are authoritative and nothing in the system can observe cache
contents except through timing. Geometry, associativity, replacement and write
policy are all runtime parameters.

### 1.5 NEON

Two paths, both real.

**Blitter.** Framebuffer with configurable bitplanes, a 12-byte command FIFO,
and `FILL`, `BLIT`, `POKE` and `PRESENT` operations. Minterm logic is exact:
all 256 functions of A, B and C. `FILL` writes a colour index across planes;
`BLIT` takes a mono source to the planes selected by a plane mask, which is how
coloured text on an Amiga-style blitter is drawn.

**Text mode.** NEON holds a character ring internally, advances the cursor and
scrolls by moving a ring origin. The CPU stores characters and never issues a
blit for text. Rendering happens inside NEON and costs the CPU nothing.

Text scans out continuously rather than on `PRESENT`: `neon_scanout()` brings
the framebuffer up to date whenever it is read. A console that had to present a
frame to show a character would not be a text mode.

The glyph store comes up initialised from `src/font8x8.c`, standing in for
bitstream initialisation, because a text mode has to be usable before any
software runs. Software can still overwrite it with `POKE`.

**Not modelled, and it matters:** the word-granular shifter, the edge masks,
descending mode and the A mask channel. Blits are pixel-granular with A tied
high. Correct for aligned glyph work, wrong in the general case.

### 1.6 Display modes

Mode 0 is pure text; the rest are graphics modes distinguished by resolution
*and* bitplane count, selected through `IO_VID_MODE`.

| mode | | fb bytes | cells | NEON blit | CPU glyphs @8 MHz |
|---|---|---|---|---|---|
| 0 | text 128x75 | 76,800 | 9600 | 1.54 ms | n/a |
| 1 | 1024x600 mono | 76,800 | 9600 | 1.54 ms | 62.4 ms |
| 2 | 1024x600 4 colour | 153,600 | 9600 | 3.07 ms | 62.4 ms |
| 3 | 640x480 mono | 38,400 | 4800 | 0.77 ms | 31.2 ms |
| 4 | 640x480 16 colour | 153,600 | 4800 | 3.07 ms | 31.2 ms |
| 5 | 512x384 16 colour | 98,304 | 3072 | 1.97 ms | 20.0 ms |
| 6 | 320x240 256 colour | 76,800 | 1200 | 1.54 ms | 7.8 ms |

The list is built to keep the framebuffer byte budget inside one band, because
an Amiga-style blitter pays per plane and blitter throughput rather than pixel
count is the constraint. That makes depth and resolution a genuine trade.

The two cost columns move independently. NEON's cost tracks bytes; the CPU's
tracks *cells*, because text in a graphics mode is one command per glyph.
Between mode 1 and mode 6 the blitter cost is identical and the CPU cost
differs eightfold.

### 1.7 Timing model

Everything is a runtime parameter (`config/default.params`), because none of
these values exists in the DN corpus. The emulator is meant to produce them,
not consume them.

### 1.8 Instrumentation and determinism

Counters for cycles, instructions, bus traffic by kind, cache and TLB
behaviour, abort causes, context switches, syscalls, NEON commands and emission
cycles, text characters and scrolls, frames, mailbox and console traffic.

Input record and replay was built in from the start rather than added later.
Input is the principal source of non-determinism in an interactive session, so
the log is a list of (cycle, event) pairs at the mailbox boundary.

### 1.9 What is not emulated

No EC. The emulator starts in the state that exists after the EC releases CPU
reset. The **mailbox is modelled as an interface** — its register set and event
semantics must match, because guest software reads input through it — but the
Cortex-M33 behind it is not. The emulator occupies the EC's position and
originates traffic directly.

No gateware simulation, no analogue behaviour, no audio.

---

## 2. The WebAssembly build and web delivery

### 2.1 Why client-side

The emulator compiles to WebAssembly and runs in the browser. Nothing executes
on the server, so a static host is enough, and the machine is available from
anywhere without a toolchain.

### 2.2 Build shape

The module is a library, not a program. It has no `main` and no entry point.

- `-nostartfiles`, `-Wl,--no-entry`. Linking `crt1.o` was wrong: that object
  exists to call `main`. Dropping it also took the module from 346 KB to
  **97 KB**, because the argument and environment machinery went with it, and
  removed the object that older linkers reject when the sysroot is newer than
  they are.
- `-Wl,--export=__wasm_call_ctors`, `-Wl,--export-dynamic`. Deliberately **not**
  `-mexec-model=reactor`: that is a WebAssembly-target option, so a clang built
  without that target reports it as an unknown *argument*, which reads like a
  version problem and is not one. The flags used here are accepted by every
  clang, and the JS hosts call whichever initialiser the module exports.
- `-Wl,--initial-memory=134217728`. The backing arrays are about 70 MB.

The Makefile probes wasm support by compiling a trivial file for `wasm32`, not
with `-print-targets`, which only exists from clang 13 and so fails on exactly
the compilers worth catching.

### 2.3 The JS host

`WebAssembly.instantiateStreaming` against a handful of WASI stubs, wrapped in
a Proxy so that whichever calls a given build path pulls in are answered
without maintaining a list that goes stale. Only `fd_write` does anything.

The host owns the clock: `nova_run()` once per animation frame with a cycle
budget of `8e6 / 60`, then `nova_render()` and `putImageData` to a canvas.
Nothing blocks the tab.

Exported interface: init, load, reset, run, halted, render, framebuffer
geometry, mode, key, mouse, tee drain, and indexed statistics so the host needs
no knowledge of struct layout.

### 2.4 What the browser supplies

Three things the native build could not.

**A live pixel window**, replacing the `.pgm` sequence.

**Pointer lock, which is the capture requirement of DN-SW-EMU-001 §6.4.** That
note argues capture is functional rather than convenient under a relative
motion protocol, because an uncaptured pointer stops producing deltas at a
screen edge. Pointer lock is that mechanism and `movementX/Y` is that delta
stream.

**Physical key identity.** `KeyboardEvent.code` names the key by position, not
by the character it produces, so a Spanish, US or Dvorak host keyboard all
deliver the same scancode. §6.3 requires host layout not to leak into the
guest; `code` gives that and `key` would not.

### 2.5 Measured

Under node, after JIT warm-up, on the text-mode workload: **23.2 Mcycle/s**,
which is 2.9x real time at 8 MHz and 0.5x at 50 MHz. Comfortable for the
current target; a 50 MHz softcore would not run in real time without work.

Two things dominate and neither is the CPU core: `render_text()` repaints all
9600 cells whenever the framebuffer is read, and `nova_render()` converts the
whole framebuffer to RGBA every frame. Both need dirty-region tracking before
anything interactive.

### 2.6 Committing the module

`nova64.wasm` is a static artifact that changes only when the emulator's C
changes, which is rare next to how often a boot image changes. Committing it
lets a machine with no wasm toolchain serve and run the console. The hazard is
drift, so `make wasm-check` compares its mtime against the sources and
`make serve` runs it first.

### 2.7 Test coverage of the web path

`make shell-test` loads `index.html` into jsdom with stubs and drives the real
flow: load an image, run frames, capture input, type, move the mouse. 21 checks
against both boot images.

It verifies the tee output and the rendered frame match the native build
exactly, and — reading the machine's own HID counter rather than checking that
nothing threw — that a mapped key delivers two mailbox events, an unmapped key
none, and pointer motion is delivered while captured and dropped when not.

**The stubs are the parts most likely to be wrong.** The 2D canvas is recorded,
not rendered. Pointer lock is synchronous here and asynchronous in reality,
with browsers differing on `movementX/Y` scaling and acceleration. Key events
are synthesised without modifiers, repeat or IME. CSS is parsed and not
applied. Wiring is verified; presentation is not.

---

## 3. Code structure

```
include/nova64.h      types, parameters, all subsystem state, the I/O map
src/bus.c             the spine: cycle accounting, translation, abort
src/cpu.c             65816 core
src/mmu.c             TLB, hardware walk, permission checks
src/cache.c           tag-only cache with SDRAM fill cost
src/memory.c          SDRAM and SRAM arrays
src/neon.c            framebuffer, minterm blitter, text mode, mode table
src/font8x8.c         the glyph set NEON comes up with
src/io.c              console, mailbox, register decode
src/params.c          parameter loader
src/instr.c           counters and report
src/trace.c           record and replay
src/system.c          assembly, reset, sliceable run loop
src/main.c            native CLI
src/wasm_api.c        WebAssembly entry points

boot/boot.s           boot image template, ca65
boot/nova64.inc       I/O map and command opcodes for the assembler
boot/nova64.cfg       ld65 memory layout and vector block

web/index.html        browser console: canvas, controls, readouts
web/nova64-node.mjs   run the module under node
web/bench.mjs         throughput benchmark
web/shell-test.mjs    headless test of the browser shell

tools/mkboot.py       hello world plus a cache-touching loop
tools/mktext.py       text by blitting, one command per glyph
tools/mktextmode.py   NEON hardware text mode with a scroll test
tools/sweep.sh        parameter sweep

tests/test_mmu.c      directed MMU tests
config/default.params the parameter set
```

The C has no dependencies beyond libc and libm. The pty is behind
`NOVA_NO_PTY` for wasm builds.

---

## 4. Using it

### 4.1 Prerequisites

| For | Needs |
|---|---|
| the native emulator | a C11 compiler and `make` |
| boot images | `cc65`, for `ca65` and `ld65` |
| the WebAssembly build | clang with the wasm32 target, `lld`, a WASI sysroot, the wasm32 builtins |
| the headless harnesses | node, and `jsdom` for `make shell-test` |

Pick a wasi-sdk release near your LLVM: 11 was built with LLVM 10, 12 with
LLVM 11. `wasm-ld` reads the sysroot's objects and a much older linker rejects
a much newer sysroot. The builtins must come from the same release and must go
in `$(clang -print-resource-dir)/lib/wasi/`, because clang looks for them
there and nowhere else.

### 4.2 Building

```
make everything    # native emulator, WebAssembly module, boot image
make check         # every test
make               # native emulator only
make -C boot       # assemble boot/boot.s
```

### 4.3 Running natively

```
./nova64 -c config/default.params -b boot/boot.bin -n 300000
```

`-n` bounds the run in PHI2 cycles, `-a` sets the load address, `-p` opens a
pty for the console so PuTTY or minicom can attach, `-R` and `-P` record and
replay input, `-d` dumps the parameter set.

At the end it renders the screen and prints the instrumentation report.

**There is no keyboard natively.** The mailbox is fed only by the browser or by
trace replay, so an echo loop shows its banner and nothing more. Interactive
work belongs in the browser.

### 4.4 Running in a browser

```
make serve         # assembles the boot image, copies it, serves on 8080
```

Then **Reload boot.bin**, **Run**, **Capture input**. `Esc` releases capture.

The four views show different things and are worth keeping straight:

| view | shows | blind to |
|---|---|---|
| live tee | every byte written to `TXT_DATA` | everything NEON does |
| character buffer | NEON's ring as text | glyph upload and rendering |
| canvas / pixel preview | the real framebuffer | nothing |
| serial console on `$FE0000` | a separate device | NEON entirely |

The first two are host-only aids that skip NEON, which is what makes them
convenient and what makes them dangerous: a wrong glyph index or a broken ring
origin leaves both looking perfect. Watch software through the tee and check
NEON through the pixels. When they disagree, the NEON model is wrong.

### 4.5 Boot images available

| image | built by | what it does |
|---|---|---|
| `boot/boot.bin` | `make -C boot` | the template: text mode, banner, echoes key presses |
| `tests/hello.bin` | `tools/mkboot.py` | console banner, then strides 32 KB to move the cache |
| `tests/text.bin` | `tools/mktext.py` | uploads a font and draws text with one BLIT per glyph |
| `tests/textmode.bin` | `tools/mktextmode.py` | hardware text mode with a 90-line scroll test |

A boot image is a raw 8 KB binary covering `$00E000`–`$00FFFF` carrying its own
vectors: what the EC places in memory before releasing reset.

**There is no ROM in this machine.** Everything is RAM, so "BIOS" means only
"the image the EC loads", and its size and placement are a free choice rather
than a consequence of a part at a fixed address. The 8 KB at `$E000` is a
convenience: the only hard requirement is the vector block at `$00FFE4`. The
browser shell has `$E000` hardcoded; the native build takes `-a`.

Write your own from `boot/boot.s`. Two things the assembler will not tell you:
`ca65` needs `.a8`/`.a16`/`.i8`/`.i16` to match what `REP`/`SEP` did at run
time, and getting that wrong assembles cleanly and executes garbage; and `STZ`
has no long addressing mode, so zeroing an I/O register needs `LDA #0` then
`STA`.

---

## 5. What the emulator has established

Measured, all provisional, all resting on invented interfaces.

| | |
|---|---|
| cache line fill, 32 B at 8 MHz | 3 PHI2 cycles |
| page table walk | 0 cycles — page tables are in 10 ns SRAM against a 125 ns PHI2 |
| NEON command emission, byte loop, 1-byte port | 188 cycles |
| NEON command emission, unrolled 16-bit stores, 2-byte port | **52 cycles** |
| text mode, tight loop | **17 cycles per character** |
| text mode scroll | 0 cycles |
| full 128x75 repaint by blitting | 62 ms |
| stall fraction at 8 MHz / 50 MHz | 12.3% / **44.3%** |
| effective speedup for a 6.25x clock increase | **3.97x** |
| cache line size sensitivity at 8 MHz / 50 MHz | 4% / **26%** |

Four conclusions worth carrying into hardware decisions.

**The command port should be two bytes wide.** On a 65816 a 16-bit `STA` writes
to `addr` and `addr+1`. A one-byte port makes every command cost twice the
stores it needs. The 52-cycle figure requires both that and an unrolled
emitter; the obvious counted loop costs 3.6x more.

**The loop matters as much as the port.** Leaving `SEP`/`REP` inside the
character loop costs 23 cycles per character against 17 with them hoisted out.
Six cycles, a third of the budget, from two misplaced instructions.

**Text mode's win is the scroll, not the per-character cost.** Characters
already on screen are never re-sent, and a scroll is a register write rather
than a full repaint. It does not help the GUI: a scan-out text mode is mutually
exclusive with the compositor, so text inside windows is still blits.

**The memory hierarchy is nearly invisible at 8 MHz and would not be at
50 MHz.** The finding that cache geometry is second-order is an artifact of the
clock. If a softcore is ever adopted, cache design becomes a first-order
problem and the geometry open item becomes urgent.

The associativity sweep produced no signal and the result is worthless: the toy
workload strides linearly and never conflicts. That question needs real kernel
and GUI workloads.

---

## 5b. Evaluated, not implemented: MVN through a NEON aperture

The 65816 has block move instructions, `MVN` and `MVP`. They take a source and
destination bank as operands, use `X` and `Y` as pointers and `A` as a count,
auto-increment or decrement, and move at **7 cycles per byte**. They are the
fastest bulk copy on the part.

The proposal is to expose a window in NEON's address space that `MVN` can
write into, so command lists and glyph data stream in without a store loop.

### What the aperture has to be

`MVN` increments the destination pointer, so a single-address FIFO cannot
receive it: the second byte lands one address later. The window is therefore a
requirement, not a convenience.

It should **alias across a whole bank**, ignoring the low address bits and
appending to the FIFO regardless of where in the window the write lands.
Otherwise `Y` walks out of a 256-byte window after 256 bytes and a move has to
be broken into chunks, which reintroduces the loop the instruction was meant to
remove. Aliased, one `MVN` can move up to 64 KB.

In hardware this is address decode, not new datapath. It is additive and does
not replace the existing single-register port.

### What it costs, against the alternatives

| path | cycles/byte | at 8 MHz | 512-byte font | 9,600-byte window list |
|---|---|---|---|---|
| unrolled 16-bit stores, operand in direct page | 4.33 | 1.85 MB/s | 277 us | 5.2 ms |
| **`MVN` through an aperture** | **7.00** | **1.14 MB/s** | **448 us** | **8.4 ms** |
| `LDA abs,x` / `STA long` loop streaming from SDRAM | 10.00 | 0.80 MB/s | 640 us | 12.0 ms |
| byte loop through a one-byte port | 15.67 | 0.51 MB/s | 1003 us | 18.8 ms |

### Assessment

**It is not a general win, and the headline comparison flatters it.** Against
the unrolled 16-bit store sequence — the emitter that produced the measured 52
cycles per command — `MVN` is 62% slower per byte. For emitting a single
command that the CPU has just computed in direct page, the stores win and
`MVN` should not be used.

**It wins where it was proposed to win.** The comparison that matters is
against streaming a *pre-built* list out of SDRAM, because that is what the
per-window command sublists are. There the store loop needs an indexed load, a
long store, an increment and a compare per two bytes: ten cycles per byte
against seven. `MVN` is 30% faster and about six instructions shorter, and it
takes no register pressure beyond `X`, `Y` and `A`.

So the honest scope is: **`MVN` for streaming pre-built command lists; stores
for one-off commands.** Both, not either.

**It is not the answer for bulk pixel data.** 1.14 MB/s means a full 76,800-byte
mono framebuffer takes 67 ms to push, and a 4-plane 153,600-byte mode takes
135 ms. If bitmap upload ever becomes a real workload, neither instruction
sequence is the answer and a DMA engine in Helium is — reading SDRAM and
feeding NEON's port at bus rate, costing the CPU nothing. `MVN` narrows the gap
to a store loop; it does not change the order of magnitude.

### Consequences to weigh before adopting

- **`MVN` loads `DBR` with the destination bank** as a side effect. Software
  has to restore it.
- **`MVN` is interruptible.** The instruction restarts and continues, which is
  good for latency but means an interrupt handler must not touch NEON
  mid-move, or the FIFO receives interleaved bytes. A lock, or a rule.
- **`MVN` and `MVP` are not implemented in the emulator core** (`$54` and
  `$44`). They fall under completing the opcode map, and they would need
  implementing before any of this can be measured rather than calculated.
- **Every figure above is arithmetic, not measurement.** The 7-cycle figure is
  from the datasheet and the rest are derived from it. The only measured
  numbers in this section are the 52 and 188 cycle emission costs.

### Recommendation

Worth doing, scoped narrowly: an aperture aliased across a bank, used only for
streaming pre-built command lists, with the single-register port retained for
everything else. Implement `MVN`/`MVP` in the core first and measure rather
than trusting the table above.

Not a substitute for a DMA path, and the DMA question should be decided on its
own terms.

---

## 5c. Evaluated, not adopted: lib65816

`lib65816` is an open-source W65C816 core library by Samuel A. Falvo II, used
in the Kestrel project, `run65816` and `jniEmu816`. The question was whether to
drop it in as our CPU core in place of the one written here.

Note there are two unrelated projects under that name; this evaluation is of
Falvo's, at `github.com/sam-falvo/lib65816`, read at commit `c174b21`.

### Not viable as a component. Three independent reasons

**1. GPL-3.0, not LGPL.** Linking it makes the entire emulator GPL-3.0, and
serving the WebAssembly module to a browser is distribution. This is a project
licensing decision rather than a technical one, and on its own it probably
settles the matter. It should be checked by someone who does licences.

**2. Timing comes from a per-opcode table, not from the bus.**

```c
cpu_cycle_count += cpu_curr_cycle_table[opcode];      /* src/dispatch.c:189 */
```

This is the architecture DN-SW-EMU-003 Stage 1 argues against, and the
consequence is not aesthetic. **It forfeits validation against the Tom Harte
suites**, which compare the *sequence* of bus cycles rather than the total.
lib65816 does not emit internal cycles as addressed bus accesses, so no trace
can be compared.

That destroys the reason for adopting it. The library would be taken on for a
validated core, and taking it on removes the means of validating it.

A static table also cannot express the data-dependent penalties — page
crossing, `D` low byte non-zero — that the Harte traces do check.

**3. Aborts are recognised at instruction boundaries.**

```c
if (cpu_abort) goto abort;                            /* src/dispatch.c:180 */
```

`CPU_abort()` raises a flag examined when the next instruction is dispatched.
Our MMU raises an abort *during* an access, from the page table entry. Under
lib65816 the faulting instruction would run to completion — with the offending
write already committed — and the vector would be taken afterwards. For demand
paging that is not a rough edge, it is wrong. Section 1.2 of this note explains
why the correct behaviour matters.

### Scale, for context

2,540 lines of C, thirteen stars, last commit August 2024. Genuine and
maintained, and well known within the 6502 community. It is not a widely
audited dependency, and adopting it would not bring the assurance that phrase
usually implies.

### Where it would be useful

**As an oracle run as a separate program, never linked.** Compare final
register and memory state per instruction against our core. Two programs
exchanging data are not a derived work, so the licence does not reach us —
again, worth confirming properly.

The value is real: outside the CPU, this project has no external oracle at all,
so a second opinion on instruction semantics is worth having.

**It ranks second, though.** The Harte suites are strictly stronger — they
check the cycle sequence, which lib65816 cannot — and carry no licensing
entanglement. Wire those in first and reach for lib65816 only if a discrepancy
appears that they do not explain.

### One thing worth borrowing

The library routes `WDM` to a host callback (`E_WDM`, `EMUL_handleWDM`). That
is the same escape-hatch shape recommended in §7 for CPU extensions, and it is
evidence the approach is conventional rather than novel.

---

## 6. Hardware specification gaps this work surfaced

None of these is an emulator question. Each is something the emulator had to
invent because no design note fixes it, and each will change.

- **NEON command port width.** Two bytes, for the reason above.
- **NEON command encoding**, opcodes and the 12-byte size. The 12-byte
  assumption forced a restriction — blit sources addressed as a word offset
  into a fixed window — that a different size would remove.
- **Text mode register map, cell geometry and character buffer size.** A
  128x75 buffer is about 9.6 KB, plus a glyph store, plus a scan-out or render
  path, none of it costed against the FPGA budget.
- **Glyph store.** Initialised from the bitstream here. Whether it is ROM,
  writable RAM, or both is undecided.
- **Scancode set**, and whether the EC delivers make and break codes as
  modelled.
- **Pointer transport** — USB host on the EC, PS/2 or an I2C trackpad — and
  whether the EC integrates motion into absolute position or delivers deltas.
  The latter decides whether host capture is functional or a convenience.
- **Target-side character device** for the console. No user-facing UART appears
  in the corpus.
- **Cache and TLB geometry**, replacement and write policy.
- **PTE bit layout** and the I/O window base.
- **Display mode list** and panel geometry.
- **Whether NEON exposes a bank-aliased write aperture** for `MVN` streaming,
  and whether a DMA path from Helium's SDRAM to NEON exists at all. See 5b.
- **SD interface as Helium presents it to the CPU.** The emulator models
  block-at-a-time through a 512-byte buffer with an auto-advancing pointer,
  which is a shape a card can actually support, but the register layout is
  invented.
- **Consequences of discarding the touch panel**: the GT911 and its dedicated
  I2C bus disappear, releasing EC pins and the AON backfeed constraint that bus
  existed to satisfy. Sheet 1.1 REV C and the EC pin budget need revision.

---

## 7. Pending work

### Blocking

**Wire the external 65816 test suites into `make check`.** The Tom Harte
per-instruction tests and the Klaus Dormann functional tests are the only
external oracle in the whole project, and nothing in the core should be trusted
until they pass. They will find bugs. This is more important than anything else
in this list.

This acquires a second edge now that the CPU is allowed to diverge from the
65816. Divergence that is **additive** — extensions behind the reserved `WDM`
(`$42`) prefix — leaves the suites valid. Divergence that **modifies** existing
opcode behaviour invalidates them exactly where it lands, and that should be a
deliberate choice rather than a discovery.

### Emulator gaps

- Complete the opcode map. Mechanical.
- Blitter: word-granular shifter, edge masks, descending mode, the A mask
  channel. Required for DN-SW-EMU-001's bit-exactness.
- Run the existing NVFS harness against the SD block device unmodified. The
  device exists now — image-file backed, block at a time, with the error and
  range cases tested — but the harness has not been pointed at it.
- Implement `MVN`/`MVP`, then measure the aperture proposal in 5b instead of
  calculating it.
- Optionally add lib65816 as a second, out-of-process oracle for instruction
  semantics, after the Harte suites are passing. See 5c, including the licence
  constraint that keeps it out of the build.
- Debug Agent command parity, so the monitor and host tooling work against both
  the emulator and hardware.
- Compositor and damage tracking. Deferred by decision: the GUI is not the
  current target.
- Native input path. The mailbox has no live producer outside the browser.
- `render_text()` and `nova_render()` need dirty-region tracking before
  anything interactive; they dominate the wasm profile.
- Audio. Not required by any gate.

### Web

- Verify the shell in a real browser beyond first impressions: pointer lock
  behaviour under load, key events with modifiers and repeat, canvas scaling on
  displays where 1024 pixels do not map 1:1.
- The shell targets the WASI build. Switching to an emscripten module means
  replacing the `instantiateStreaming` block with the generated loader and
  reading `HEAPU8.buffer`.

### Calibration

- Run the parameter sweep against real kernel and GUI workloads rather than the
  toy program, and issue a cache geometry recommendation.
- Measure NEON emission cost under a realistic GUI workload and test whether the
  server-side string cache and pre-built command sublists deliver what is
  assumed of them.

---

## 8. References

- DN-SW-EMU-001 Rev A — emulator purpose, fidelity model, timing model, open
  items, abandonment conditions
- DN-HW-ECIF-001 Rev B — EC interface, mailbox, Debug Agent
- `README.md`, `web/README.md`, `boot/README.md` in the source tree
