# DN-SW-EMU-004 — Prior art: 65816-OS and its VS Code debugger

**Domain:** SW (software)
**Topic:** EMU (host-side system emulator and its tooling)
**Revision:** A — draft for review, not frozen
**Date:** 2026-09-25
**Language:** English

**Source.** <https://github.com/alexanderbh/65816-OS>, read at commit
`456c3b950c9934d23615bb7d33a2d43c74112e7d` (2024-02-06), 173 commits from
2021-10-30. Author: Alexander Hansen. Licence: MIT.

**Purpose.** To record what a comparable hobby 65816 project built, what it
learned, and which of it is worth carrying into noVa64 — so that the sheets
that take something from it can cite a reviewed source instead of a memory.
The consolidated result is [sheet EM8](../content/sec_ai_em8.md); the smaller
items land in sheets O, N, EM3, EM4, R and Q.

**What this note does not do.** It copies no code. Every idea taken from the
project is restated in noVa64's own terms, and where a piece of its code is
worth reusing later — the `.dbg` reader of §6.2 is the only candidate — that
is a separate decision with the MIT notice kept (§10).

---

## Revision history

| Rev | Date | Change |
|---|---|---|
| A | 2026-09-25 | First reading of the repository; transfer table; verified CPU-core defects. |

---

## 1. What the project is

Three things, built by one person over about two and a half years:

1. **An operating system** for a breadboard W65C816 computer, in ca65
   assembly: preemptive multitasking, a shell, a handful of programs, and
   drivers for the board's peripherals.
2. **An emulator** of that exact computer, in TypeScript.
3. **A source-level debugger** for it, as a Visual Studio Code extension
   forked from Microsoft's `vscode-mock-debug` sample, driving the emulator
   through the Debug Adapter Protocol.

The motivation is stated in the README and is the same as noVa64's in the
post *We need an emulator*: reprogramming EEPROMs for every change was too
slow, even with a serial line for debug output. The emulator and debugger
became the main development platform; the hardware is flashed "once in a
while".

The author is candid about the state of it: the debugger "is a mess since it
was copied from the vscode Mock debugger example", the emulator and debugger
are "(too) tightly coupled", and of the source-to-address mapping, "to this
day I cannot remember how I wrote that code".

---

## 2. Layout

```
os/                 the OS — ~3,800 lines of ca65; main.asm .includes everything
  bios/             init and drivers: spi, keyboard (PS/2), ra8875, usb245r, ramtest
  kernel/           interrupt, tasks, scheduler, streams, syscalls
  stdlib/           strlen, readnum, strcmp behind one numbered entry point
  programs/         shell, ps, echo, perf, clock, dumpregs, dumpstack
  ca65.config       ld65 memory map and segments
vscode65816/        the VS Code extension
  src/lib/          the emulator: CPU.ts (1,716 lines), System, 6522VIA, RA8875,
                    Keyboard, RAM, ROM, Register
  src/mockDebug.ts  the debug adapter (927 lines)
  src/activateMockDebug.ts   webview for the display and the keyboard
  workspaceTester*/ small standalone test programs with their own launch configs
ctest/              a trivial Calypsi C experiment
hardware/           KiCad project (schematic only; the PCB has no tracks) and the
                    decode GAL's equations
```

There is no test suite anywhere in the repository.

---

## 3. The machine

- W65C816 at 12 MHz (a 10 MHz can oscillator is in the schematic), 5 V.
- 512 KB SRAM (AS6C4008), bank byte latched by a 74LS573.
- 32 KB EEPROM (AT28C256), programmed with `minipro`.
- Address decode in an ATF22V10: bank `$00` is RAM `$0000–$AFFF`, I/O
  `$B000–$BFFF` in 512-byte slots, ROM `$C000–$FFFF`; banks `$01–$07` are RAM.
- One W65C22 VIA does nearly everything: timer 1 is the scheduler tick, port B
  bit-bangs SPI, port A receives PS/2 bytes deserialised by a 74HC595 with the
  CA1 interrupt.
- RA8875 800 × 480 TFT controller on the bit-banged SPI.
- A USB245R FIFO on a second VIA as the debug serial line.

**Nothing here transfers to the target board**, where Helium, Neon and the EC
do all of it at 3.3 V (§9, item 11).

---

## 4. The operating system

### 4.1 Structure

One translation unit: `main.asm` includes the BIOS, the kernel, the stdlib and
every program, and ld65 places `KERNEL` data in RAM at `$0200` and code in ROM
at `$C000`, with the stdlib at `$E000`. The build has two targets, `build` and
`build-debug`; only the second emits a listing, a VICE label file and the
ld65 debug file.

### 4.2 Tasks and the scheduler

- **Sixteen task slots**, the TCB stored as parallel arrays (`TaskStatus[]`,
  `TaskA[]`, `TaskX[]`, `TaskStackPointer[]` …). A task has a slot and,
  separately, a task ID drawn from a counter starting at `$0100`.
- **Round-robin preemption on VIA timer 1**, latch `$FFFF`: about 183 Hz at
  12 MHz (the source comment says 256).
- **Per-task direct page and stack**: DP at `$9x00`, stack top at `$AxFF`, `x`
  being the slot. The README names this as the reason the author abandoned an
  earlier 6502 OS: on the 6502 each task switch meant remapping memory; on the
  65816 moving D and S is enough.
- **The context switch rewrites the interrupt frame.** The IRQ handler pushes
  B, D, A, X and Y after the hardware frame; the scheduler reads that frame
  with stack-relative loads, copies every value into the TCB arrays, `TCS`es
  to the next task's saved S, rebuilds a frame there from the TCB, and returns
  into the common epilogue (`PLY/PLX/PLA/PLD/PLB/RTI`).
- **No blocking**: `Sys_WaitForTask` spins on its own status byte until
  another task's exit changes it, and `TaskExit` marks the slot and loops
  forever until the next tick preempts it.
- **No memory protection**, by design: "There is no memory safety in the OS
  and there will not be."
- The commit history clusters its trouble here: *interrupt working a bit.
  Stops after a few interrupts*, *attempt at schedule fix*, *interrupt issue*.

### 4.3 Calls into the system

- Kernel services are `JSL`s to fixed labels (`Sys_GetPID`,
  `Sys_WaitForTask`, `Sys_Exit`).
- The stdlib has **one entry point, with the service number in A**: the
  dispatcher saves A, X and Y, scales A and jumps through a word table with
  `JMP (StdLibTable,X)`. Arguments are read stack-relative.
- Native vectors: IRQ is set; **COP, BRK, ABORT and NMI are left at `$0000`**.

### 4.4 Streams, keyboard, shell

- Three 16-byte ring buffers; only stdin is used, filled by the keyboard ISR.
- The keyboard driver decodes PS/2 set 2 with a scancode-to-ASCII table whose
  comments ("relocated F7", "relocated Print Screen key") match Daryl Rictor's
  PC keyboard code published on 6502.org.
- The shell is a line buffer in its own direct page, a command table of
  `name → entry` pairs, spawn-and-wait, and a trailing `&` for background.
  `ps` lists the TCB; `perf` prints the tick and task-switch counters.
- Kernel initialisation prints `[ -- ] name`, then overwrites the bracket
  with `\r[ OK ]` or `\r[FAIL]`.

### 4.5 Assembler conventions

- `.smart` is on, so ca65 tracks `REP`/`SEP` operands in straight-line code.
- Width macros — `longa`, `longi`, `longr`, `shorta`, `shorti`, `shortr` —
  emit the `REP`/`SEP` **and** the matching `.A16`/`.I8` directive together.
- Every routine is preceded by `.A8`/`.A16`/`.I8`/`.I16`, which serves as its
  entry-width contract.

---

## 5. The emulator

### 5.1 Shape

- Instruction-stepped: `CPU.step()` is a 256-way switch, and each operation
  adds its own cycle count, then tells listeners (the VIA timer) how many
  PHI2 cycles passed.
- Memory is resolved through a map built for `$0000–$FFFF` only; **any access
  outside bank `$00` throws**, so the 512 KB of the real board is not modelled.
- The RA8875 is emulated at bit level, clocked by the guest's own SPI
  bit-banging, and drawn on an 800 × 480 canvas in a webview.
- The keyboard maps the browser's `KeyboardEvent.code` to PS/2 set-2
  scancodes through an explicit table — by physical key, as [EM3.9] requires
  of noVa64 — and hands each byte over twice to approximate the device's
  timing.
- Reset loads S with `$0100` plus a **random** low byte, so no two runs are
  identical.

### 5.2 Defects found in the CPU core

Read from the source; the first was confirmed by running the expression
under Node.

| Defect | Evidence | Consequence |
|---|---|---|
| **`SBC` is wrong in value and carry.** The subtrahend is complemented with JavaScript's `~` (a negative number) and the borrow term is `C ? 0 : 1` — inverted. | `$0A − $05` with C=1 gives `$04`, C=0; hardware gives `$05`, C=1. `CLC; SBC #$2F` on `$35` gives `$06`; hardware gives `$05`. | The OS computes the saved stack pointer in the scheduler with `CLC; SBC`, so emulator and hardware disagree **on the context-switch path**. |
| 16-bit `SBC` computes V from `A.byte`. | Source. | Wrong V in 16-bit subtraction. |
| **`BRK` loads the vector's address into PC** (`$FFFE` / `$FFE6`) instead of the vector's contents. | Source. | Any `BRK` runs the vector table as code. |
| **IRQ always vectors through `$FFFE`**, the emulation-mode vector, even in native mode. | Source. | Works only because the OS installs the same handler at `$FFEE` and `$FFFE`. |
| Opcode `$57` is decoded as `EOR (dp),Y` instead of `EOR [dp],Y`. | Every other `$x7` row uses the long-indirect mode. | Silent wrong address. |
| `MVN`, `MVP`, `PLP`, `WAI`, `STP`, `WDM` throw "Not implemented"; `RTI` throws in emulation mode. | Source. | Loud, at least. |
| Decimal mode is ignored by `ADC` and `SBC`. | Source. | Silent. |
| Cycle counts are per-opcode constants plus ad-hoc penalties. | Source. | Timing is approximate by construction. |

None of these would survive the SingleStepTests suite of [EM7.23]. They are
recorded because they are the plainest available evidence for [EM7.22]'s
claim: this core ran a multitasking OS with a shell for years, and **a program
that runs proved nothing about it**.

---

## 6. The debugger

### 6.1 Protocol and features

A Debug Adapter Protocol server running inside the VS Code extension host,
with the emulator in the same process. It offers: launch with stop-on-entry,
continue, pause, step in, step over, step out, breakpoints by source line, a
call stack named by labels, scopes for the registers, the P flags, the
emulator's cycle counter, the top of the stack and the VIA's registers, memory
read and write on the stack scope, and evaluation of a label to its value
using the size recorded for it. A webview beside the editor shows the RA8875
and forwards key events to the emulated keyboard. The extension also has a
browser build target inherited from the sample; whether it works with this
emulator was not checked.

### 6.2 The source map — the one piece worth studying

`ld65 --dbgfile` writes a tab-separated text file of records. The adapter
reads five kinds:

```
file  id=0,name="src/main.asm",size=1367,mtime=0x6381F14C,mod=0
seg   id=0,name="CODE",start=0x00C000,size=0x0448,addrsize=absolute,type=ro,oname="bin/main.bin",ooffs=16384
span  id=3,seg=0,start=0,size=2
line  id=351,file=1,line=28,type=2,count=1,span=454+449+443+434+429+423+415+368+364+346
sym   id=0,name="LongDelayLoop2",addrsize=absolute,scope=0,def=255,ref=550,val=0xC441,seg=0,type=lab
```

The address of a line is `seg.start + span.start`, and one `line` record can
name **several spans** — a macro line expanded ten times names ten. `type=2`
marks a line inside a macro body. From these the adapter builds two maps,
address → file:line (preferring the macro line when both exist) and
file:line → addresses, plus a symbol table of name → value and size.

**Its defect**: the file:line → addresses map is written twice per span, the
second write unconditional, so a line with several addresses keeps only the
last one — a breakpoint on a macro line binds one expansion out of ten.

It also never checks that the `.dbg` belongs to the image it is debugging,
although the records carry source sizes and mtimes and the segments carry the
output file offset.

### 6.3 Stepping and the call stack

- Step over and step out count call and return **opcodes**: `JSR`, `JSL` and
  `JSR (abs,X)` deepen, `RTS` and `RTL` shallow. Interrupts, `COP`, `BRK` and
  `RTI` are not counted.
- The call stack is a list the CPU core appends to on `JSR`/`JSL` and pops on
  `RTS`/`RTL`. A task switch — a `TCS` to another stack — leaves it describing
  a stack the CPU is no longer on.
- Breakpoints are an address set checked after every instruction against
  `PBR:PC`.

---

## 7. The Calypsi experiment

`ctest/` compiles a trivial `main()` with Calypsi 3.6.10 in the
`--code-model=large --data-model=small` combination, linking `clib-lc-sd.a`
with a Scheme-syntax linker file placing the direct page, a 256-byte stack and
a 4 KB heap. The object file is ELF with DWARF debug information. This is the
same compiler and the same model pair that [O.2] and [O.3] select; beyond
confirming that, it is too small to carry anything.

---

## 8. Hardware

A KiCad schematic (W65C816, W65C22, two AS6C4008, 74LS573, two ATF22V10,
74HC595, 74HC14, Mini-DIN-6 PS/2 connector, can oscillator) and the CUPL
source of the decode GAL. The PCB file has no tracks. Of historical interest
only for noVa64.

---

## 9. What transfers to noVa64

| # | Item | Verdict | Lands in |
|---|---|---|---|
| 1 | A source-level debugger in VS Code speaking the Debug Adapter Protocol | **Adapt the idea** — the main result | [EM8], [D127] |
| 2 | The ld65 `.dbg` reader: address ↔ file:line, symbols with sizes | **Adapt the logic**, fixing §6.2's defect and adding the staleness check | [EM8], [D128] |
| 3 | A webview beside the editor with the display and keyboard capture by key code | **Adapt** — noVa64's browser shell of [EM5] is the display, and the table maps to HID usages | [EM8] |
| 4 | Width macros that emit `REP`/`SEP` with the directive, `.smart`, an entry-width annotation per routine | **Adopt as is** | [O.10], [EM4.16], [D129] |
| 5 | Always emit `-g`, `--dbgfile`, `-Ln` and a listing | **Adopt**, as one build rather than a second target | [O.10], [D128] |
| 6 | One entry point, service number in A, `JMP (table,X)` | **Confirms** [D90] and [J.3]; nothing to copy | — |
| 7 | Context switch by rewriting the interrupt frame; per-task D and S | **A lesson, not code**: keep the frame where the interrupt left it and store S | [N.2] |
| 8 | `ps`, `perf`, the `[ -- ] → [ OK ]` boot log | Minor | — |
| 9 | Calypsi `lc-sd` with a linker file | Confirms [O.3] | — |
| 10 | The TypeScript CPU core | **No** — [EM7]'s bus-layer design is the better one, and §5.2 | — |
| 11 | The hardware | **No** | — |
| 12 | Vectors left at `$0000` | **A rule, by counter-example**: no vector is ever zero | [O.10], [D130] |

On item 7 in particular: in noVa64 the MMU gives every process its own
virtual bank `$00` ([L.10], [N.4]), so the per-task D and S that the project is
proudest of come for free — and with them the direct-page pseudo-registers
that [Q19] worries about, which live in memory the process owns.

---

## 10. Licence and credit

- The repository is MIT. Its `LICENSE` still carries the template's
  placeholder, `Copyright (c) [year] [fullname]`, so the author's name is
  taken from the commits.
- `vscode65816/` is derived from `vscode-mock-debug`, MIT, © Microsoft
  Corporation.
- The keyboard table appears to derive from Daryl Rictor's 6502.org code;
  noVa64 needs none of it, since its keymap is HID-based ([CN1.11]).
- MIT code may be incorporated into a GPL-3.0 project provided its copyright
  and permission notice are kept. Nothing is incorporated by this note.
- Credited in `docs/references.md`.

[EM3.9]: ../content/sec_ai_em3.md
[EM5]: ../content/sec_ai_em5.md
[EM7]: ../content/sec_ai_em7.md
[EM7.22]: ../content/sec_ai_em7.md
[EM7.23]: ../content/sec_ai_em7.md
[EM8]: ../content/sec_ai_em8.md
[EM4.16]: ../content/sec_ai_em4.md
[O.2]: ../content/sec_ai_o.md
[O.3]: ../content/sec_ai_o.md
[O.10]: ../content/sec_ai_o.md
[N.2]: ../content/sec_ai_n.md
[N.4]: ../content/sec_ai_n.md
[L.10]: ../content/sec_ai_l.md
[J.3]: ../content/sec_ai_j.md
[CN1.11]: ../content/sec_ai_cn1.md
[D90]: ../content/sec_ai_q.md
[D127]: ../content/sec_ai_q.md
[D128]: ../content/sec_ai_q.md
[D129]: ../content/sec_ai_q.md
[D130]: ../content/sec_ai_q.md
[Q19]: ../content/sec_ai_q.md
