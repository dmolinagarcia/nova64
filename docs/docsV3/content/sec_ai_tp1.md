# Toolchain and systems-software training plan
> self-study · paced by the gates it serves · modules M0–M8 · checkpoints TR0–TR8

A self-study plan covering compilation, object files, linking, loading, libraries, debuggers, the C runtime, and compiler retargeting. Each module is ordered and sized to support a specific stretch of noVa64 software work.
  **Supports:** DN-SW-CONSOLE-001 (CN0–CN4), the K-series gates, DN-SW-EMU-001, DN-SW-VMISA-001 / DN-SW-VMINTERP-001 / DN-SW-VMCC-001 (NVM32 Stages 1–2, gates T0–T5), DN-SW-VMBENCH-001, and a debugger design note not yet written (DN-SW-DEBUG-001, [§6.3](sec_ai_tp1#tp13)).
  NOTE: **Transcribed from DN-PLAN-TRAINING-001 Rev B, a draft for review and not frozen.** The text is the note's own, section numbers included; its §6.1–§6.3 are the items [TP1.1](sec_ai_tp1#tp11)–[TP1.3](sec_ai_tp1#tp13), and the notes like this one are the sheet's. The notes it cites are consolidated elsewhere in this document, and the table at the foot of the sheet says where: its CN0–CN4 are the series [CON-00](sec_ai_cn1#con-00)–[CON-04](sec_ai_cn1#con-04), and its K4 and K5 are [P5.i](sec_ai_p2#p5i) and [P5.k](sec_ai_p2#p5k).
  NOTE: **Everything it says about debuggers is a proposal.** The debug port, the remote protocol and the VS Code adapter are inputs to DN-SW-DEBUG-001, which is planned and not written (→ [Q189](sec_ai_q#q189)).

## Revision history

| Rev | Date | Change |
|---|---|---|
| A | 2026-09-25 | Initial draft. |
| B | 2026-09-25 | Adds M3 (how debuggers work) and M4 (debugging from VS Code). Renumbers the later modules to M5–M8 and their checkpoints to TR5–TR8. Aligns the NVM32 modules with the DN-SW-VMCC-001 gates. **Correction:** Rev A placed the NVM32 ISA freeze at Stage 0, following DN-SW-VMISA-001 Rev A. Rev B of that note moved the freeze to gate T4, after the compiler produces real code and an opcode histogram exists. |

## 1. Purpose and principles

This plan exists to support the project, not to replace it. Four rules follow from that.

1. **Just in time.** Each module is timed to run just before the gate that needs it. Nothing is read "for completeness". Chapters that serve no upcoming gate are listed as reference, not assigned.
2. **Sequential.** Modules run one after another, like the single-thread development plan. No parallel tracks.
3. **Every module ends in a checkpoint that produces something the project keeps.** That can be a tool, a design input, a test or a written decision. A checkpoint that produces nothing reusable is a sign the module is mis-scoped.
4. **Linux is the lab.** The concepts (sections, symbols, relocations, archives, loaders, breakpoints, debug information, remote debugging) are the same on ELF/Linux, and the tools to inspect them already exist there. Each concept is observed on Linux first, then applied to the 65816 toolchain, the emulator and NVX.

Training checkpoints (TR0–TR8) are **not** project gates. They never block a CN, K or T gate. They exist so that the project gate is reached with understanding rather than trial and error.

## 2. Map: project need → module → gate

| Project need | Module | Before gate |
|---|---|---|
| Understand the compile/assemble/link pipeline and object files | M0 | CN0 |
| Kernel image format shared with `exec` (D-5); BIOS loader; NVX relocations | M1 | CN0, CN2 |
| crt0, linker placement, sections, 65816 startup code | M2 | CN0 |
| Source-level debugging of kernel code in the emulator; the emulator's debug port | M3 | CN0 |
| Debugging from VS Code, including the browser build of the emulator | M4 | When terminal debugging becomes the bottleneck |
| libc surface vs syscall stubs (D-1); tty; shell; `exec`/`wait`; fd model | M5 | CN1–CN4, K4–K5 |
| How a compiler turns C into instructions; ABI; register allocation | M6 | NVM32 reopening, before T0 |
| Bytecode VM dispatch; golden-model interpreter | M7 | NVM32 Stage 1 |
| Retargeting vbcc to NVM32 | M8 | T1–T4 (NVM32 v1 frozen at T4) |

## 3. Modules

## M0 — Anatomy of the toolchain, on Linux

**Effort:** ~10 h.
  **Goal:** see every stage of the pipeline with your own eyes.

### Materials

- GCC and binutils already on any Linux box: `gcc -E`, `-S`, `-c`, `readelf`, `objdump`, `nm`, `ar`, `ldd`.
- Compiler Explorer (godbolt.org) to watch C turn into assembly as you type.

### Exercises

1. Take a two-file program (`main.c` calling a function in `util.c`). Produce and read each intermediate: `.i`, `.s`, `.o`, executable.
2. `readelf -S -s -r util.o main.o`: list sections, symbols (defined and undefined) and relocation entries.
3. `objdump -dr main.o`, then `objdump -d` on the linked executable. Find the bytes the linker patched.
4. Build `util.o` into `libutil.a` (static) and `libutil.so` (`-fPIC -shared`). Link against each. Compare sizes, `ldd` output and `objdump` of the call site. Run with `LD_DEBUG=bindings` to watch the dynamic linker resolve symbols.

- TR0 — **Checkpoint TR0.** Before running each tool, write down the prediction: which symbols are undefined, how many relocations exist, what their targets are. Then verify. Done when the predictions match.

## M1 — Linking and loading in depth

**Effort:** ~25 h.
  **Goal:** understand object formats, symbol resolution, relocation, archives and loaders well enough to specify NVX and the BIOS/`exec` loader.

### Materials

- *Computer Systems: A Programmer's Perspective* (Bryant, O'Hallaron), **chapter 7, "Linking"**. Core reading.
- Ian Lance Taylor, *Linkers* blog series, **parts 1–6, 8, 11 and 12** (introduction, shared libraries, relocations, ELF, archives, symbol resolution). The rest is reference.
- *Linkers and Loaders* (John R. Levine), **chapters 1, 3, 6, 7 and 8** (linking and loading, object files, libraries, relocation, loading). Chapters 9–10 (shared libraries, dynamic linking) are reference until shared libraries are considered for noVa64.

- TR1 — **Checkpoint TR1.** A one-page input to the executable-format section of the OS contract (and to DN-SW-CONSOLE-001 D-5):
  - NVX section list and what the loader does with each;
  - the relocation types each target actually needs (65816 native, NVM32), with the field they patch;
  - what the BIOS loader and the kernel `exec` loader share, and what differs.

## M2 — The 65816 toolchain in practice

**Effort:** ~20 h.
  **Goal:** apply M0–M1 to the tools the kernel is actually built with.

### Materials

- **Calypsi manual**: sections, linker placement rules, startup code, large model, calling convention.
- **cc65 documentation, ld65 chapter**: the `MEMORY` / `SEGMENTS` config file is a small, readable example of a linker script. ca65 is already the project assembler.
- **vbcc manual, 65816 target chapters** (Apple IIgs, SNES, and "Simulator/Standalone" including *Adapting to other systems*). This is a worked example of exactly what noVa64 has to do: startup code, heap, stdio and a linker script for a new 65816 system.
- 65816 C compiler explorer (mike42/65816-c-compilers) to compare the output of different 65816 compilers for the same function.

### Exercises

1. Compile ten small C functions with Calypsi. Read the assembly and write down the calling convention you observe: argument passing, return values, who saves what, how the frame is built.
2. Write the linker configuration for the monolithic kernel against the DN-SW-CONSOLE-001 memory layout.

- TR2 — **Checkpoint TR2.** A crt0 plus linker configuration that brings the monolithic kernel up in the emulator: native mode, direct page, stack, `.bss` cleared, `.data` initialised, `main` called. This is the software half of CN0.

## M3 — How debuggers work

**Effort:** ~35 h, including the stub.
  **Timing:** after M2, before CN0. A working debugger in the emulator pays for itself from the first kernel bring-up.
  **Goal:** understand the three parts of a debugger, and give the emulator a debug port that a real source-level debugger can use.

### What this module teaches

- A debugger is three separate parts, joined by protocols:
  1. **Debug information**, written by the compiler: which address belongs to which source line, where each variable lives, the types, and how to walk back up the call stack.
  2. **Target control**: stop, resume, step, read and write registers and memory, set breakpoints.
  3. **The user interface**: a command line or an editor.
  Because the parts only meet through protocols, each one can be replaced without touching the others.
- **Breakpoints.** On real hardware there are two kinds. A software breakpoint replaces an instruction with a trap (`BRK`, opcode `$00`, on the 65816); the original byte is restored and the trap put back when execution resumes. A hardware breakpoint uses an address comparator. In an emulator, a breakpoint is a check of the PC before each instruction and changes nothing in memory.
- **Stepping.** An emulator can simply run one instruction. The 65816 has no trace flag, so stepping on hardware needs temporary breakpoints on every possible next instruction, or Helium's bus-cycle stepping. Stepping one source line means stepping instructions until the line table says the line has changed. "Step over" treats `JSR`/`JSL` and the whole call as one unit.
- **The call stack** needs unwinding information (DWARF CFI) or knowledge of the compiler's frame layout. It is usually the hardest part.
- **Watchpoints** (stop when an address is written) are almost free in an emulator, because every access already goes through the bus layer, and expensive on hardware.

### Materials

- Eli Bendersky, *How debuggers work*, **parts 1–3** (free blog): basics, breakpoints, debug information. Short and clear. Start here.
- Sy Brand, ***Building a Debugger*** (No Starch, 2025): a native x64 Linux debugger written from scratch. Read the parts on breakpoints, stepping, ELF/DWARF line tables and stack unwinding, and skip the x64-specific material. The author's earlier blog series, *Writing a Linux Debugger*, is a free, shorter alternative.
- Michael J. Eager, *Introduction to the DWARF Debugging Format* (free PDF, dwarfstd.org).
- **GDB manual, "Remote Serial Protocol" appendix**: the packet format and the minimum set of commands a stub must answer.
- **Calypsi documentation for `db65816`**, and the **Calypsi-remote-debug** repository. Calypsi includes a source-level debugger that reads ELF/DWARF and controls its target through part of the gdbserver protocol (`db65816 --target-remote <device> program.elf`). The repository contains a real stub for a 65816 machine (C256 Foenix) that runs on the target over a serial line.

### Exercises

1. On Linux, debug a small C program with `gdb` against `gdbserver :1234`, with `set debug remote 1` turned on. Read the packets exchanged for a breakpoint, a step and a register read.
2. `readelf --debug-dump=decodedline` on a program built with `-g`. Find the line table and map one PC to its source line by hand.
3. Build a Calypsi C program with debug information and inspect its ELF/DWARF in the same way. Record which DWARF sections Calypsi emits: line table, variables and types, and frame information.
4. Read the Calypsi-remote-debug source. List the packets `db65816` sends and the register layout it expects for the 65816.

- TR3 — **Checkpoint TR3.** A GDB remote-protocol stub in the emulator: the emulator's **debug port**.
  - It implements the subset `db65816` uses (TR3 exercise 4).
  - The stub core is transport-independent: bytes in, bytes out. The native build carries it over TCP. If `db65816` accepts only a serial device, a pseudo-terminal bridge (for example `socat`) connects the two.
  - Pass criterion: `db65816` stops at a breakpoint on a C source line of the kernel running in the emulator, steps, prints a local variable, and resumes.
  - The design decisions go into DN-SW-DEBUG-001 ([§6.3](sec_ai_tp1#tp13)).

## M4 — Debugging from VS Code

**Effort:** ~30 h of study, then adapter work.
  **Timing:** after TR3. Can wait until debugging in a terminal with `db65816` becomes the bottleneck.
  **Goal:** control the emulator from VS Code, including the browser build: breakpoints set in the editor, variables, registers, memory and disassembly.

### What this module teaches

- VS Code does not talk to debuggers directly. It speaks the **Debug Adapter Protocol** (DAP, JSON messages) to a **debug adapter**. The adapter translates DAP into whatever the target speaks, which here is the remote-protocol stub from TR3. The adapter is also where debug information is read and interpreted.
- **A web page cannot open a listening port.** A browser only makes outgoing connections. The browser emulator therefore connects out over a WebSocket, either to the adapter or to a small bridge, while the native build keeps its TCP port. The same stub serves both transports.
- In code-server, extensions run on the server, so the adapter runs on the server too, next to the native emulator. Only the emulator page runs in the browser.

### Materials

- **Debug Adapter Protocol** overview and specification (microsoft.github.io/debug-adapter-protocol).
- VS Code API documentation, **"Debugger Extension"** guide, and the **Mock Debug** sample (github.com/microsoft/vscode-mock-debug): a minimal adapter to start from.
- **VS64** (github.com/rolandshacks/vs64): a VS Code extension that debugs 6502 code on a built-in emulator, and on the VICE emulator through its binary monitor protocol. It is the closest existing example of what this module builds.
- code-server documentation on port forwarding (`/proxy/<port>/`), for reaching the emulator page and its WebSocket from the browser.

### Exercises

1. Run Mock Debug with DAP tracing on. Read the sequence initialize → setBreakpoints → configurationDone → stopped → stackTrace → scopes → variables.
2. Read how VS64 maps DAP requests onto its emulator and onto VICE.

- TR4 — **Checkpoint TR4.** A noVa64 debug adapter for VS Code, stage 1: symbols and line table. That covers breakpoints on source lines, step, continue, registers, a memory view and disassembly. It must work against both the native emulator (TCP) and the browser emulator (WebSocket). Stage 2 (local variables and types from DWARF) and stage 3 (call stack) are project work, not training.

## M5 — The OS interface that C programs need

**Effort:** ~40 h, spread across CN1–CN4 (read each part just before its gate).
  **Goal:** understand syscalls, file descriptors, the console driver, the shell and process creation, as implemented in a small real system.

### Materials

- **xv6 book** (MIT, RISC-V edition), with the xv6 source alongside:
  - *Operating system interfaces* — processes, `exec`, fds, the shell. Read before CN1; maps onto DN-SW-CONSOLE-001 §8.
  - *Traps and system calls* — read before CN4 (COP stubs).
  - *Interrupts and device drivers* — the console driver and its line discipline. Read before CN1; maps onto §6.
  - *File system* — read before CN3.
  - Source to study: `console.c`, `sh.c`, `sysfile.c`, `exec.c`, `usys.pl` (how syscall stubs are generated).
- ***Operating Systems: Three Easy Pieces*** (OSTEP), selected chapters: *Process API*, *Address Spaces*, *Paging* (introduction and TLBs), *Files and Directories*, *File System Implementation*.
- **OSDev Wiki**: *OS Specific Toolchain*, *Creating a C Library*.

- TR5 — **Checkpoint TR5.** A review note against DN-SW-CONSOLE-001 §10.2 and D-1:
  - the full libc surface the shell and the first `/bin` programs need;
  - for each function, whether it is pure C, a thin syscall stub, or needs kernel support not yet specified;
  - any gap or discrepancy found, recorded in DN-SW-CONSOLE-001 §14.

## M6 — How compilers work

**Effort:** ~60–90 h.
  **Timing:** after CN4, when the NVM32 programme reopens, and before DN-SW-VMCC-001 gate T0.
  **Goal:** understand code generation well enough to judge the NVM32 encoding and ABI as a compiler target while the encoding candidates are still open.

### Materials

- **Nand2Tetris, Part II** (nand2tetris.org, free; Coursera version is paid and gives a certificate). Projects 7–11: VM translator and compiler. Project 12 (OS) is optional. Part I (hardware) can be skipped.
- ***Writing a C Compiler*** (Nora Sandler, No Starch). Part I builds a working compiler for a C subset in small steps. Then read the chapters on optimisation and register allocation. Its test suite is reused in DN-SW-VMCC-001 gate T2.
- **chibicc** (Rui Ueyama, GitHub) as an alternative or complement: a small C compiler whose commit history is meant to be read in order.

- TR6 — **Checkpoint TR6.** Hand-compile a set of representative C functions (loop over an array, struct access, function call with more than four arguments, switch, 64-bit arithmetic) to NVM32 assembly. Record every place where the encoding or the ABI makes code generation awkward. The result is input to the encoding candidates that DN-SW-VMBENCH-001 measures ahead of the T4 freeze.

## M7 — Bytecode virtual machines

**Effort:** ~30 h.
  **Timing:** before NVM32 Stage 1 (C11 reference interpreter).
  **Goal:** learn interpreter structure, dispatch techniques and VM testing.

### Materials

- ***Crafting Interpreters*** (Robert Nystrom, free online), **Part III, "A Bytecode Virtual Machine"** (clox), including the final optimisation chapter. Part II can be skipped.
- Wozniak's SWEET16 article and source, the direct ancestor of NVM32's register-in-direct-page model.

- TR7 — **Checkpoint TR7.** First version of the NVM32 C11 reference interpreter inside the emulator, running the conformance tests for the ALU and compare-and-branch opcodes. This is the reference-interpreter half of T0.

## M8 — Retargeting a real C compiler

**Effort:** ~40 h of study, then backend work.
  **Timing:** NVM32 Stage 2 (DN-SW-VMCC-001).
  **Goal:** write the vbcc backend and the vasm CPU module for NVM32.

### Materials

- **vbcc manual, "Backend Interface" chapter**: what `machine.c`, `machine.h` and `machine.dt` must provide.
- **vbcc backends as examples**: the 65816 and 6502 backends (same family as the host CPU) and one register-rich backend.
- **vasm manual**, CPU module interface, and the existing SWEET16 module as a template.
- ***A Retargetable C Compiler: Design and Implementation*** (Fraser and Hanson), the lcc book: the code-generation chapters. lcc was designed to be retargeted, and the book explains the design decisions behind a backend interface.

- TR8 — **Checkpoint TR8.** DN-SW-VMCC-001 gate T1: trivial programs, loops and calls compile at `-O0`, and `crt0` and the exit syscall work on the reference interpreter from TR7. Gates T2–T5 are project work.

## 4. Schedule relative to project gates

No calendar dates. The plan is paced by the gates it supports.

```
M0 ─► M1 ─► M2 ─► M3 ───────────────────────►  CN0
                  M4 (when terminal debugging is the bottleneck)
            M5 (xv6 interfaces + drivers)   ─►  CN1
            M5 (file system chapters)       ─►  CN2, CN3
            M5 (traps and syscalls)         ─►  CN4 / K5
                                    NVM32 reopens
M6 ──────────────────────────────────────────►  T0 (input to VMBENCH)
M7 ──────────────────────────────────────────►  Stage 1 reference interpreter
M8 ──────────────────────────────────────────►  T1 … T4 (NVM32 v1 frozen)
```

Estimated total: ~290–320 h of study, excluding project work that doubles as a checkpoint (TR2, TR3, TR4, TR7, TR8).
  NOTE: Every hour figure on this sheet is an estimate, not a measurement.

## 5. Materials

| Material | Author | Format / cost | Location | Modules |
|---|---|---|---|---|
| *Computer Systems: A Programmer's Perspective* | Bryant, O'Hallaron | Book | csapp.cs.cmu.edu | M1 |
| *Linkers* series (20 parts) | Ian Lance Taylor | Blog, free | airs.com/blog/archives/38 (part 1) to /57 (part 20); ToC at lwn.net/Articles/276782 | M1 |
| *Linkers and Loaders* | John R. Levine | Book; draft freely available | linker.iecc.com | M1 |
| Calypsi manual, `db65816` | Håkan Thörngren | Free with the compiler | calypsi.cc | M2, M3 |
| Calypsi-remote-debug | Håkan Thörngren | Free, GitHub | github.com/hth313/Calypsi-remote-debug | M3 |
| cc65 / ld65 documentation | cc65 project | Free | cc65.github.io/doc/ld65.html | M2 |
| vbcc manual (65816 targets, Backend Interface) | Volker Barthelmann | Free | compilers.de/vbcc.html | M2, M8 |
| 65816 C compiler explorer | Michael Billington | Free, GitHub | github.com/mike42/65816-c-compilers | M2 |
| *How debuggers work*, parts 1–3 | Eli Bendersky | Blog, free | eli.thegreenplace.net (2011) | M3 |
| *Building a Debugger* | Sy Brand | Book, No Starch (2025) | nostarch.com/building-a-debugger | M3 |
| *Introduction to the DWARF Debugging Format* | Michael J. Eager | PDF, free | dwarfstd.org | M3 |
| GDB manual, Remote Serial Protocol appendix | GNU | Free | sourceware.org/gdb/documentation | M3 |
| Debug Adapter Protocol | Microsoft | Specification, free | microsoft.github.io/debug-adapter-protocol | M4 |
| VS Code "Debugger Extension" guide, Mock Debug sample | Microsoft | Free | code.visualstudio.com/api; github.com/microsoft/vscode-mock-debug | M4 |
| VS64 | rolandshacks (GitHub) | Free, GitHub | github.com/rolandshacks/vs64 | M4 |
| code-server guide (port forwarding) | Coder | Free | coder.com/docs/code-server/guide | M4 |
| xv6 book and source | Cox, Kaashoek, Morris (MIT) | Free | pdos.csail.mit.edu/6.1810; github.com/mit-pdos/xv6-riscv | M5 |
| *Operating Systems: Three Easy Pieces* | Arpaci-Dusseau | Free online | ostep.org | M5 |
| OSDev Wiki | Community | Free | wiki.osdev.org | M5 |
| Nand2Tetris Part II | Nisan, Schocken | Free (site) or paid (Coursera) | nand2tetris.org | M6 |
| *Writing a C Compiler* | Nora Sandler | Book, No Starch | nostarch.com/writing-c-compiler | M6 |
| chibicc | Rui Ueyama | Free, GitHub | github.com/rui314/chibicc | M6 |
| *Crafting Interpreters* | Robert Nystrom | Free online, or book | craftinginterpreters.com | M7 |
| vasm manual | Volker Barthelmann, Frank Wille | Free | sun.hasenbraten.de/vasm | M8 |
| *A Retargetable C Compiler* (lcc) | Fraser, Hanson | Book (out of print, used copies); source free | github.com/drh/lcc | M8 |

**Reference only, not assigned:** *Engineering a Compiler* (Cooper, Torczon) and *Compilers: Principles, Techniques, and Tools* ("the Dragon book"), for looking up specific topics during M6 and M8.

## 6. Notes for other documents

- TP1.1 — **vbcc 65816 support.** DN-SW-VMISA-001 (Stage 2) states that vbcc already ships a 65816 backend. Confirmed: vbcc has a native-mode 65816 backend with Apple IIgs, SNES and simulator targets. This also gives the project a second 65816 C compiler to compare against Calypsi output.
- TP1.2 — **vbcc licence.** vbcc is free for non-commercial use; commercial use needs a licence from the author. It does not affect a solo non-commercial project, but it should be recorded in DN-SW-VMCC-001 in case noVa64 software is ever distributed commercially.
  NOTE: DN-SW-VMCC-001 already records it, and so does this document: [VM4.6](sec_ai_vm4#vm46) and [Q165](sec_ai_q#q165).
- TP1.3 — **Inputs to DN-SW-DEBUG-001** (debugger design note, not yet written). DN-SW-VMCC-001 leaves debugger tooling out of scope, and nothing else covers it.
  - **One target protocol.** Adopt the GDB remote serial protocol, at least the subset `db65816` uses, as the single debug protocol of the project. It serves `db65816` immediately and the VS Code adapter later. It runs over TCP (native emulator), WebSocket (browser emulator) and, later, the hardware debug path.
  - **Hardware.** The 65816's registers cannot be read from outside the CPU (Helium Debug Agent note, §7.2), so hardware debugging needs a stub running on the 65816 itself (a `BRK` handler). There are two routes. One is to port Calypsi-remote-debug to noVa64 over the prototype's CPU-visible UART. The other is the EC-side remote-protocol stub already listed as a deferred item in the Debug Agent note.
  - **Two debug-information formats.** Calypsi emits ELF/DWARF for C. The BIOS is assembled with ca65/ld65, which uses its own debug file. The adapter has to read both, or the BIOS has to move to the Calypsi assembler.
  - **NVM32.** Source-level debugging of bytecode programs needs its own layer: the adapter reads VM registers from the direct-page block, not from the 65816. Defer until T2.
  - **To verify:** whether WebSocket connections pass through the code-server `/proxy/<port>/` path. The code-server documentation does not say so explicitly.
  NOTE: **These are proposals, not decisions**, and they stay open until DN-SW-DEBUG-001 is written (→ [Q189](sec_ai_q#q189)). The Debug Agent note's §7.2 is [R.15](sec_ai_r#r15) here, and its deferred remote-protocol stub on the EC is [R.24](sec_ai_r#r24), still deferred.

## 7. Adjustment and abandonment conditions

- **Skip a module** if the related project gate has already been passed without it. Keep only the chapters that address a problem actually hit.
- **Nand2Tetris or Sandler, not necessarily both.** If one of them already makes TR6 straightforward, drop the other.
- **Cut M1 short** if CS:APP chapter 7 and Taylor parts 1–6 are enough to write TR1. Levine then becomes reference.
- **Rework TR3** if `db65816` uses too small or too unusual a subset of the remote protocol to serve as the project's debug protocol. The stub then follows what the VS Code adapter needs instead, and `db65816` is dropped.
- **Stop M4 at stage 1** if `db65816` in a terminal covers variable inspection well enough.
- **Revise this plan** whenever the project roadmap moves NVM32, adopts shared libraries, or changes compiler (for example, adopting vbcc for the native kernel).

## Where the notes it cites live in this document.

| The plan cites | Here |
|---|---|
| DN-SW-CONSOLE-001, gates CN0–CN4 | [Sheet CN1](sec_ai_cn1), series [CON-00](sec_ai_cn1#con-00)–[CON-04](sec_ai_cn1#con-04) |
| DN-SW-CONSOLE-001 D-1, the libc surface | [D103](sec_ai_q#d103), [CN1.1](sec_ai_cn1#cn11) |
| DN-SW-CONSOLE-001 D-5, the kernel image format | [D107](sec_ai_q#d107), [CN1.5](sec_ai_cn1#cn15) |
| DN-SW-CONSOLE-001 §6, the console device | [CN1.9](sec_ai_cn1#cn19)–[CN1.13](sec_ai_cn1#cn113) |
| DN-SW-CONSOLE-001 §8, the shell | [CN1.18](sec_ai_cn1#cn118)–[CN1.24](sec_ai_cn1#cn124) |
| DN-SW-CONSOLE-001 §10.2, the syscall slice | The table under [CN1.26](sec_ai_cn1#cn126) |
| DN-SW-CONSOLE-001 §14, discrepancies | Open questions in [sheet Q](sec_ai_q), as [Q178](sec_ai_q#q178) and [Q183](sec_ai_q#q183) were |
| The K-series gates, K4 and K5 | [P5.i](sec_ai_p2#p5i) and [P5.k](sec_ai_p2#p5k) |
| DN-SW-EMU-001 | [Sheets EM1](sec_ai_em1)–[EM3](sec_ai_em3); the debug port is pending work at [EM6.19a](sec_ai_em6#em619a) |
| DN-SW-VMISA-001 | [Sheet VM1](sec_ai_vm1) |
| DN-SW-VMINTERP-001 | [Sheet VM2](sec_ai_vm2) |
| DN-SW-VMCC-001, gates T0–T5 | [Sheet VM4](sec_ai_vm4), gates at [VM4.31](sec_ai_vm4#vm431) |
| DN-SW-VMBENCH-001 | [Sheet VM4](sec_ai_vm4), from [VM4.34](sec_ai_vm4#vm434) |
| Helium Debug Agent note, §7.2 | [R.15](sec_ai_r#r15) |
| Helium Debug Agent note, the deferred EC-side stub | [R.24](sec_ai_r#r24) |
| DN-SW-DEBUG-001 | Planned, not written (→ [Q189](sec_ai_q#q189)) |
