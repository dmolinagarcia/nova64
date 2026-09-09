# noVa64 host emulator — first sketch

Implements DN-SW-EMU-001 Rev A (draft). C11, no dependencies beyond libc and
libm, POSIX for the console pty.

## What you need

This repository contains no build outputs. Nothing here installs anything;
these are the prerequisites, by what they unlock.

| For | Needs |
|---|---|
| the native emulator | a C11 compiler and `make`. Nothing else. |
| boot images | `cc65`, for `ca65` and `ld65` |
| the WebAssembly build | clang 13 or newer with the wasm32 target, `lld`, a WASI sysroot, and the wasm32 compiler-rt builtins |
| the headless harnesses | node, and `jsdom` for `make shell-test` |

The native emulator has no dependencies beyond libc, so it builds anywhere.
Everything else is optional and its absence only costs you that one target.

### Notes on the WebAssembly prerequisites

The WASI sysroot and the wasm32 builtins come from the same wasi-sdk release,
as separate archives. Point `WASI_SYSROOT` at wherever you put the sysroot.

The builtins are the awkward one: clang links them automatically for a wasm
target and looks for them in its own resource directory, so they have to go in
`$(clang -print-resource-dir)/lib/wasi/` rather than anywhere of your choosing.

Pick a wasi-sdk release near your LLVM: 11 was built with LLVM 10, 12 with 11,
and so on. `wasm-ld` reads the sysroot's objects and a much older linker will
reject a much newer sysroot. See `web/README.md` if it does.

**Clang 10 will not do**, and fails in a way that misdirects: a clang built
without the WebAssembly target reports wasm-specific options as unknown
*arguments*, which reads like a version problem rather than a missing target.
The Makefile probes by compiling a trivial file for wasm32 — not with
`-print-targets`, which only exists from clang 13 and so fails on exactly the
compilers worth catching — and names your compiler when the probe fails.

If you build the module elsewhere, `web/nova64.wasm` is a static artifact that
can simply be dropped in; `make wasm-check` guards it against going stale
against the C sources.

## Building

```
make everything # native emulator, WebAssembly module, boot image
make check      # every test
make serve      # http://localhost:8080

make            # native emulator only
make test       # native run + MMU unit tests
make textmode   # the text-mode demo
make -C boot    # assemble boot/boot.s -> boot/boot.bin
./tools/sweep.sh
```

## What this is

A skeleton with the architecture in place and a working subset of each part:

| Build order step | State |
|---|---|
| 1. 65816 core, cycle-stepped, flat 16 MB | **partial** — structure complete, opcode subset |
| 1. record/replay | **partial** — replay works, no live producer yet |
| 2. memory map with per-access hook | **done** |
| 3. parameter set + instrumentation | **done** |
| 4. MMU, TLB, ABORTB, cache model | **done**, directed tests pass |
| 5. console + mailbox + debug parity | **partial** — console and mailbox present, Debug Agent absent |
| 6. SD block device | **absent** |
| 7. NEON, blitter, compositor | **partial** — framebuffer, minterm blitter, FILL/BLIT/POKE/PRESENT; no shifter, no edge masks, no compositor |
| 8. sweep campaign | **infrastructure only** |

## What this is not

**Not a validated core.** A program that runs proves nothing. Validation is
step 1 against external test suites, and it has not been done. Unimplemented
opcodes halt with the opcode and address printed rather than silently doing
nothing, so gaps are visible rather than latent.

**Not a source of hardware truth.** Every register address, the HID event
encoding, the PTE bit layout and the whole physical memory map are invented
here because no DN fixes them. Everything marked PROVISIONAL in the source is
expected to change. Guest software written against these interfaces today will
need revision.

## Design points worth knowing before reading the code

**The bus layer is the spine.** Every 65816 cycle is a bus cycle — there are no
internal cycles that stay off the bus. So opcodes are written as straight-line
sequences of `bus_read8` / `bus_write8` / `bus_idle` in the order the real part
performs them, and each call advances PHI2 by one cycle plus any stall. Cycle
accuracy is a property of emitting the right sequence, not of a separate timing
table. `src/bus.c`.

**ABORTB unwinds with longjmp.** The real part aborts the instruction in
progress and restores the register state it had at its start. `cpu_step` saves
a register image and arms `setjmp`; a translation failure in the bus layer
longjmps back, the image is restored, and the abort vector is taken.

**The cache model holds tags only.** Line data is not duplicated — the backing
arrays are authoritative. Nothing in the system can observe cache contents
except through timing, so modelling them would be work with no observable
consequence.

**Everything in the timing model is a runtime parameter.** Not because
parameterisation is elegant, but because none of these values exists in the DN
corpus yet. The emulator is meant to produce them, not consume them.

## First measurements

From the sweep, on the toy workload in `tools/mkboot.py`:

```
line   ways  cycles     misses     stall/miss  stall%
16     1     23999      1030       2.00        8.58%
32     2     25023      1028       3.00        12.32%
64     2     25029      515        6.00        12.35%
128    2     24777      258        11.00       11.45%
```

Two observations, both provisional:

1. **A cache line fill costs 2–3 PHI2 cycles at 32 bytes.** This is the
   arithmetic DN-SW-EMU-001 section 4.3 recorded as a hypothesis, and it holds
   under the default parameters. Total cycles move by about 4% across an
   eightfold range of line size. If this survives contact with real workloads,
   cache geometry is a second-order design decision here.

2. **Page table walks cost zero cycles.** Page tables live in 10 ns SRAM while
   PHI2 is 125 ns, so a walk fits inside a cycle that was going to be spent
   anyway. The TLB miss, normally the dominant cost on any machine with virtual
   memory, is close to free. Asserted by `tests/test_mmu.c`, so it will fail
   loudly if a parameter change breaks it.

**Associativity shows no effect at all in this sweep, and that result is
worthless.** The toy workload strides linearly through 32 KB and never
conflicts, so it cannot distinguish direct-mapped from 4-way. Answering the
associativity question needs real kernel and GUI workloads, which is step 8
proper, not this demonstration.

## Video, and why there is no text mode

The target has no character generator. Implementing a text device with a
character RAM would be inventing hardware, so `tools/mktext.py` does what the
real machine would have to do: uploads a 64-glyph font into NEON's own SDRAM
and issues **one BLIT per character**.

```
make text                  # build the image and run it
```

That makes the run a direct measurement of the primary quantity in
DN-SW-EMU-001 section 7, and the measurement contradicts the working
assumption.

| Emitter | Port width | cycles/command | Full 128x60 screen |
|---|---|---|---|
| byte loop | 1 byte | **188** | 1,443,840 cycles = 180 ms |
| unrolled, 16-bit stores | 2 bytes | **52** | 399,360 cycles = 50 ms |

### Then text mode was added to NEON

The design changed in response: NEON now keeps the character buffer
internally, renders from it, and scrolls by moving a ring origin. The CPU
stores characters and nothing else. `tools/mktextmode.py` exercises it.

| Path | cycles/char | Full 128x75 screen | Scroll one line |
|---|---|---|---|
| one BLIT per glyph | 52 | 499,200 cyc = 62 ms | 499,200 cyc = 62 ms |
| text mode, tight loop | **17** | 153,600 cyc = 19 ms | **0 cyc** |

The per-character improvement is 3x, which is useful. The scroll is the real
result: a console that scrolls costs nothing instead of a full repaint per
line, and characters already on screen are never re-sent at all. Incremental
output costs only the characters actually written.

The loop matters as much as the port. Leaving `SEP`/`REP` inside the character
loop costs 23 cycles per character; hoisting them out costs 17. Six cycles per
character is a third of the budget, from two instructions.

**Text mode does not solve the GUI.** A scan-out text mode is mutually
exclusive with the graphical compositor. Text inside GUI windows is still
blits, and the 62 ms figure still applies there. This buys a fast console and
monitor, not fast text in windows.

### Three things follow from the blitter numbers

**The ~50 cycles/command assumption is achievable but not free.** It requires
both a two-byte-wide command port and an unrolled emitter. The obvious
implementation — a counted loop storing one byte at a time — costs 3.6x more.

**The port width is a hardware decision the emulator just surfaced.** On a
65816 a 16-bit `STA` writes to `addr` and `addr+1`. If the NEON command port
is one byte wide, every command costs twice the stores it needs to. The port
here occupies `$FE0030-$FE0031` so a 16-bit store lands two bytes in the FIFO.
No DN fixes this; it should.

**Full-screen text redraw is ~50 ms even with the good emitter**, and that is
emission alone, with the CPU doing nothing else. Twenty frames per second for
a full repaint, before any application work. This is the quantitative case for
damage-limited recomposition and pre-built command sublists: they are not
optimisations, they are the difference between a usable machine and an
unusable one.

All of these numbers are provisional. They rest on an invented command format,
an assumed 12-byte command size and an invented text register map, and a
different format would move them.

The text-mode hardware is not free either: a 128x75 character buffer is about
9.6 KB, plus a glyph store, plus a scan-out or render path — none of which is
costed against the FPGA budget yet.

## The SD card

A host image file, attached with `-s` or `boot.sd`. Block at a time through a
512-byte buffer, because a byte-at-a-time interface to a block device is a
category error: the card cannot do it, and software written against one would
not port.

```
./nova64 -c config/default.params -b boot/boot.bin -s card.img
sd: card.img, 4 blocks (0.0 MB)
```

Set the LBA, issue a command, then read or write `SD_DATA` 512 times; the
pointer advances on each access and a command resets it. Registers are in
`boot/nova64.inc`. The image is read and written in place rather than loaded:
a card image is routinely larger than the machine's own RAM, and the NVFS
crash-injection harness needs to see the file after an abrupt stop.

Reads past the end of the card set the error bit rather than reading past the
file. Opening read-only succeeds and fails writes, which is what you want for
a golden image you do not intend to modify.

## Seeing what NEON produced

Three views, and they check different things.

| view | switch | shows | blind to |
|---|---|---|---|
| live tee | `video.text_tee` | every byte written to `TXT_DATA`, streamed to stdout | everything NEON does |
| character buffer | `video.text_dump` | NEON's character ring as text, at PRESENT | glyph upload and rendering |
| pixel preview | `video.ansi` | a 1:1 window of the real framebuffer | nothing, but it is a corner |
| full frame | `video.ppm` | a `.pgm` per PRESENT, the whole screen | nothing |

The first two are HOST-ONLY development aids with no hardware counterpart, and
they are convenient precisely because they skip NEON. That is also what makes
them dangerous: a wrong glyph index, a bad font upload or a broken ring origin
leaves both of them looking perfect while the actual screen is blank or
garbled.

So: watch software through the tee, and check NEON through the pixels. When
the character buffer and the framebuffer disagree, the NEON model is wrong.

The serial console on `$FE0000` is a fourth, separate path that does not
involve NEON at all.

A live pixel window would need a host graphics library and does not exist yet;
the `.pgm` sequence is the current substitute.

## The clock is stretched

**Decision: PHI2 is stretched, always.** The machine halts its clock until
memory responds rather than free-running and holding the CPU on `RDY`. The
65816 core is static, so the clock may be stopped indefinitely.

`phi2.stall_model` still selects the other model for comparison, but
`stretch` is the default and the design intent.

### What that commits you to

**A cycle no longer measures time.** Nothing in software may schedule, time
out or delay by counting cycles. The machine therefore needs a fixed-frequency
reference that software can read, and it must not derive from PHI2. That is
the free-running microsecond counter at `$FE0050`; read the low byte first,
which latches the upper three so a rollover between two reads cannot yield a
value that never existed.

This is a hardware requirement created by the decision, not a convenience.

**Anything needing a steady clock must not hang off PHI2.** Timers, serial
baud generation, audio sample rates, the EC interface. Each needs its own
source.

**Helium must generate PHI2 rather than receive it.** The clock generator
becomes part of the FPGA and has to extend a phase on demand.

**In exchange, the CPU interface gets simpler.** No `RDY` sequencing, and
signals qualified by PHI2 phase stay valid while the phase is held, which is
why stretching is the easier way to attach slow peripherals.

### What it measures

Wall time is accumulated separately from the cycle count, because the two are
no longer proportional. Average PHI2 becomes a real, workload-dependent
number:

```
striding through memory   7.0956 MHz   (88.7% of nominal)
text through the blitter  7.9821 MHz   (99.8%)
hardware text mode        7.9588 MHz   (99.5%)
```

Compare instruction rate across stall models, never cycles or CPI. Under
stretching CPI drops from 3.428 to 3.005 while the machine does the same work
in the same time; the cycle stops being a unit of work.

The emission figures are unchanged by the decision — 52 cycles per NEON
command, 17 per character in text mode — because those paths are stores to I/O
that never wait on memory.

### The alternative, for the record

| | wait states | stretched clock |
|---|---|---|
| PHI2 | free-runs at nominal | halted until memory responds |
| a stall | rounded up to whole cycles | paid exactly |
| average PHI2 | constant by construction | a real measurement |
| what varies | cycles per instruction | cycle duration |

Both are available on a 65816: `RDY` works on reads and writes on this part.

Rounding costs a wait-state machine 1.2% of wall time on these workloads,
rising to about 2% at 16 MHz. Small, because a fill is 270 ns against a 125 ns
cycle and rounds to three. Where it bites hardest is the TLB walk: 20 ns of
SRAM rounds up to a whole 125 ns cycle, a **6.25x penalty on the real
latency**. That is the strongest argument for the decision above.

That correction matters, because an earlier version of this model subtracted a
cycle from the walk on the grounds that it "fits inside a cycle that was going
to happen anyway", and concluded page walks were free. That subtraction was an
assumption dressed as arithmetic. A walk is a genuine extra pair of SRAM reads.
Under a stretched clock it costs 20 ns; under wait states it costs a full
cycle. It was never free.

## Display modes

Mode 0 is pure text; the rest are graphics modes distinguished by resolution
*and* bitplane count. `IO_VID_MODE` selects; the emulator prints the table at
the end of every run.

| mode | | fb bytes | cells | NEON blit | CPU glyphs @8 MHz |
|---|---|---|---|---|---|
| 0 | text 128x75 | 76,800 | 9600 | 1.54 ms | **n/a** |
| 1 | 1024x600 mono | 76,800 | 9600 | 1.54 ms | 62.4 ms |
| 2 | 1024x600 4 colour | 153,600 | 9600 | 3.07 ms | 62.4 ms |
| 3 | 640x480 mono | 38,400 | 4800 | 0.77 ms | 31.2 ms |
| 4 | 640x480 16 colour | 153,600 | 4800 | 3.07 ms | 31.2 ms |
| 5 | 512x384 16 colour | 98,304 | 3072 | 1.97 ms | 20.0 ms |
| 6 | 320x240 256 colour | 76,800 | 1200 | 7.8 ms | 7.8 ms |

Two things the table is for.

**Planes cost what pixels cost.** An Amiga-style blitter pays per plane, so
640x480x4 and 1024x600x2 are the same work. The mode list is built to keep the
framebuffer byte budget inside one band, because blitter throughput and not
pixel count is the constraint. That makes depth and resolution a genuine
trade rather than two independent knobs.

**The two cost columns move independently.** NEON's blit cost tracks bytes;
the CPU's cost tracks *cells*, because text in a graphics mode is one command
per glyph. Between mode 1 and mode 6 the blitter cost is identical and the CPU
cost differs eightfold. Whenever a mode decision is about text, cells are the
number to look at.

The blit column scales linearly with `neon.blit_bytes_per_sec`, which is an
assumption. A x16 SDRAM at 100 MHz gives roughly 200 MB/s peak; a
three-operand blit lands near a third of that, hence 50 MB/s. Neither the
SDRAM clock nor the blitter datapath width is fixed in any DN, so treat the
whole column as an order of magnitude.

Mode 0 pays no CPU glyph cost at all, which is the entire argument for keeping
a pure text mode alongside the graphics ones.

## Layout

```
include/nova64.h    types, parameters, all subsystem state
src/bus.c           the spine: cycle accounting, translation, abort
src/cpu.c           65816 core
src/mmu.c           TLB, hardware walk, permission checks
src/cache.c         tag-only cache with SDRAM fill cost
src/io.c            console (pty), mailbox, NEON stub
src/memory.c        SDRAM and SRAM arrays
src/params.c        parameter loader
src/instr.c         counters and report
src/trace.c         record/replay
src/system.c        assembly and run loop
src/main.c          CLI
tools/mkboot.py     boot image assembler
src/neon.c          framebuffer, minterm blitter, command decode
tools/mktext.py     text by blitting, one command per glyph
tools/mktextmode.py NEON hardware text mode, characters straight to NEON
tools/sweep.sh      parameter sweep
tests/test_mmu.c    directed MMU tests
```

## Console

`./nova64 -p` creates a pty and prints its path, so PuTTY, minicom or screen
can attach:

```
$ ./nova64 -p -c config/default.params -b tests/hello.bin
console pty: /dev/pts/3
```

The guest side is a character device, not an invented UART — but which
character device the target actually has is an open item, so the register
layout at `$FE0000` is provisional.

## Next, in order

1. Wire the external 65816 test suites into `make test`. Nothing else in the
   core should be trusted until this passes, and it will find bugs.
2. Fill the opcode map. The remaining ones are mechanical.
3. SD block device, then run the existing NVFS harness against it unmodified.
4. Debug Agent command parity, so the monitor and host tooling work against
   both the emulator and hardware.
