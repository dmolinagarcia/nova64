# DN-SW-VMCC-001 — NVM32 C toolchain

*noVa64 · Rev A (draft for discussion)* · 2026-09-17 · @Someone

## Revision history

| Rev | Date | Change |
| --- | --- | --- |
| A | 2026-09-17 | Initial draft for discussion. Selects vbcc + vasm + vlink, specifies the NVM32 backend, the runtime and the T0–T5 gates. Raises change requests against DN-SW-VMISA-001 and DN-SW-VMINTERP-001 (appendices A and B). |

*Review cycle: reviewers, dates and decisions to be recorded here before this note leaves draft status.*

## Scope and deliverables

This note specifies how C source becomes an NVX image for NVM32: a retargeted vbcc code generator, a vasm CPU module, vlink packaging and a runtime. It is the Stage 2 deliverable of the VM programme and blocks nothing before Stage 1 (the C11 reference interpreter) exists.

Four deliverables, all new work:

1. `vbccnvm32` — the NVM32 code generator (`machines/nvm32`).
2. `vasmnvm32_std` — the NVM32 CPU module for vasm.
3. Linker script, `mknvx` packaging tool and the `vc` target configuration.
4. Runtime: `crt0`, libcall helpers, soft float and a C library port.

| Related note | Relationship |
| --- | --- |
| DN-SW-VMISA-001 | Defines the ISA this toolchain targets. Appendix A raises six change requests against Rev A. |
| DN-SW-VMINTERP-001 | Consumes the output on the 65816. Appendix B raises change requests on interrupts and preemption. |
| DN-HW-VMCORE-001 | Consumes the same binaries on the softcore. No toolchain impact. |
| DN-SW-EMU-001 | Hosts the C11 reference interpreter used as the golden model for every test in section "Verification and gates". |

Out of scope: the ISA itself, the interpreter implementation, debugger and profiler tooling, and any shared-library or dynamic-linking mechanism.

## Toolchain architecture

The compiler front end, the optimizer, the assembler core and the linker are reused unchanged; only the target-specific parts are written here. That is the whole reason for choosing vbcc over a from-scratch compiler.

```mermaid
flowchart LR
  C[C source] --> VB[vbcc front end<br/>+ optimizer]
  VB --> BE[NVM32 backend]
  BE --> AS[vasm core]
  AS --> CM[NVM32 cpu module]
  CM --> LD[vlink]
  LD --> RT[Runtime<br/>+ linker script]
  RT --> MK[mknvx]
  MK --> NVX[NVX image]
```

| Component | Origin | Effort |
| --- | --- | --- |
| vbcc front end, optimizer, `vc` driver | Reused | Configuration only |
| `machines/nvm32` code generator | New | 1,500–3,000 lines of C |
| vasm core, syntax and output modules | Reused | None |
| `cpus/nvm32` module | New | Opcode table + encoder |
| vlink | Reused | Linker script only |
| `mknvx` | New | \~200 lines, host side |
| `crt0`, libcalls, soft float, libc | New or ported | Largest non-backend item |

The same NVX image is the input to all three executors: the C11 reference interpreter, the 65816 interpreter and the softcore.

## Licensing and contribution constraints

A private noVa64 toolchain is permitted; publishing it is not, without written consent. This must be settled before the backend is written, because it can change the tool choice.

- vbcc may be redistributed only unmodified and used for non-commercial purposes; distributing a modified version or commercial use needs the author's written consent. A vbcc carrying the NVM32 backend is a modified version.
- vasm and vlink carry equivalent terms.
- The vbcc backend chapter asks anyone writing a code generator to contact the author first so the target can be integrated into the official source tree. He wrote the QNICE-FPGA backend and its C library himself, so a custom FPGA CPU is familiar ground there.
- The vasm manual states that vasm is human-made software and asks contributors to check beforehand if a contribution might contain code not written by themselves, explicitly naming generative AI. AI-assisted modules are fine for private use; upstreaming requires that conversation first.

Consequence for the project: the NVM32 toolchain is usable and shareable only as source patches the recipient applies to their own vbcc/vasm copies, unless consent is obtained. If noVa64's toolchain must ever be redistributable as a whole, the fallback is the LLVM route in "Alternatives considered".

## vbcc backend for NVM32

The backend is a directory `machines/nvm32` holding `machine.c`, `machine.h` and `machine.dt`; `make TARGET=nvm32 bin/vbccnvm32` builds the compiler. Start from the example backend shipped with vbcc, which models a generic 32-bit RISC or CISC machine and is already close to NVM32.

`gen_code()` receives a doubly linked list of quadruples: an operator with two source operands (`q1`, `q2`) and a target (`z`). Each operand is a constant, a register, a variable (auto, static or extern) or a dereference of one of those. Most of the backend is therefore two helpers — load any operand into a register, store a register into any target — plus one case per operation, around the required functions `init_cg`, `freturn`, `regok`, `dangerous_IC`, `must_convert`, `shortcut` and the data emitters `gen_var_head`, `gen_dc`, `gen_ds`, `gen_align`.

The NVM32-specific decision is the flagless branch. vbcc models comparison as COMPARE/TEST setting condition codes followed by a branch, and the manual anticipates machines that encode signedness in the branch and fuse the two; with multiple condition-code registers left disabled, the IC after a COMPARE is guaranteed to be the conditional branch, FREEREGs aside. So COMPARE emits nothing and the branch emits one instruction.

```c
/* machines/nvm32/machine.c — excerpt */
#include "supp.h"

char cg_copyright[] = "vbcc NVM32 code generator (noVa64) 0.1";

/* vbcc numbers registers 1..MAXR (17): 1..16 = r0..r15, 17 = the r0:r1 pair */
char *regnames[MAXR+1] = { "noreg",
  "r0","r1","r2","r3","r4","r5","r6","r7",
  "r8","r9","r10","r11","r12","sp","lr","fp","r0:r1" };

/*                           r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 10 11 12 sp lr fp pr */
int regsa[MAXR+1]      = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0};
int regscratch[MAXR+1] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1};
int reg_prio[MAXR+1]   = {0, 1, 2, 3, 4, 6, 6, 6, 6, 5, 5, 5, 0, 0, 0, 0, 0, 0};
/* r11/r12 are code-generator temporaries; sp/lr/fp are never allocated.
   regsize[]/regtype[] and the type tables are filled in init_cg().        */

#define TMP1 12   /* r11 */
#define TMP2 13   /* r12 */

static struct IC *pending;          /* last COMPARE/TEST, not yet emitted */

static void gen_branch(FILE *f, struct IC *b)
{
  static const char *cc[] = { "eq", "ne", "lt", "ge", "le", "gt" };
  struct IC *c = pending;
  int t   = c->typf;
  int uns = (t & UNSIGNED) || ISPOINTER(t);   /* pointers compare unsigned */
  int op  = b->code - BEQ;                    /* BEQ..BGT are consecutive  */
  int ra  = load_tmp(f, &c->q1, t, TMP1);     /* helper: operand -> reg    */
  int rb;

  pending = 0;
  if (c->code == TEST) {                      /* if (x) / while (n--)      */
    emit(f, "\tb%sz\t%s,l%d\n", cc[op], regnames[ra], iclabel(b));
    return;
  }
  rb = load_tmp(f, &c->q2, t, TMP2);
  if (op >= 4) {                              /* LE/GT: swap, use GE/LT    */
    int x = ra; ra = rb; rb = x;
    op = (op == 4) ? 3 : 2;
  }
  emit(f, "\tb%s%s\t%s,%s,l%d\n", cc[op], (uns && op >= 2) ? "u" : "",
       regnames[ra], regnames[rb], iclabel(b));
}

/* in the gen_code() loop over the IC list: */
if (c == COMPARE || c == TEST) { pending = p;      continue; }
if (c >= BEQ && c <= BGT)      { gen_branch(f, p); continue; }
```

Because NVM32 has no memory-to-memory operations, the code generator needs two scratch registers of its own; `regsa[]` is the documented way to reserve them. `dangerous_IC()` normally flags divisions and dereferences, but NVM32 defines division by zero, so only dereferences need it.

Correctness first, then speed. The manual notes an optimized backend commonly produces code up to twice as fast as a simple one; on an interpreted target that factor is paid in 65816 cycles per VM instruction, so it matters more than on real silicon. The three optimizations worth doing, in order: folding pointer-add plus dereference into base+displacement addressing modes (vbcc's intermediate code uses no addressing modes, so this is entirely the backend's job), a peephole pass over the buffered assembly output, and compact function entry/exit using ENTER/LEAVE.

## C ABI as implemented

ILP32 with 24-bit effective pointers, set in `init_cg()`. This diverges from Calypsi's 16-bit `int` by design; the two ABIs meet only at the syscall marshalling layer.

| Item | Value |
| --- | --- |
| `char` / `short` / `int` / `long` | 1 / 2 / 4 / 4 bytes |
| `long long` | 8 bytes, in an r0:r1 register pair or memory |
| pointer | 4 bytes stored, 24 bits effective, top byte must be zero |
| `float` / `double` | 4 / 8 bytes, soft float via libcalls |
| `maxalign` | 4 |
| Arguments | R0–R3, then stack; variadic arguments always on the stack |
| Return | R0, or the r0:r1 pair; structs via hidden pointer |
| Caller-saved | R0–R7 |
| Callee-saved | R8–R10 |
| Code-generator temporaries | R11, R12 (reserved via `regsa[]`, not allocatable) |
| SP / LR / FP | R13 / R14 / R15 |

Notes on the mechanism:

- `reg_parm()` is told whether an argument belongs to the variadic part, so it returns 0 there. Variadic arguments therefore always sit on the stack and `stdarg.h` is plain pointer arithmetic.
- Register pairs are declared as an extra register (the r0:r1 entry) with identical `regscratch` for both halves. 64-bit add and subtract are inlined, with the carry produced by the unsigned `SETcc`; 64-bit multiply, divide and all floating point go through the libcall interface vbcc introduced in 0.9d for exactly these cases.
- `must_convert()` returns 0 between `int` and pointer: same size, same representation.
- Invariant for narrow types: registers always hold `char` and `short` values properly sign- or zero-extended, so every comparison is a plain 32-bit one. This is what makes the SEXT/ZEXT request in appendix A necessary.
- `shortcut()` returns 0 initially: promote everything to 32 bits and revisit only if profiling justifies it.
- `char` signedness: **signed**, fixed now and never revisited.

## vasm CPU module

A vasm CPU module is a `cpu.h` and a `cpu.c` under `cpus/nvm32`, whose central job is turning a parsed instruction or data operand into bytes plus any relocations; the binary for a CPU and syntax pair is named `vasm<cpu>_<syntax>`, here `vasmnvm32_std`.

Design decisions:

- **Standard relocations only.** NVM32 fields are whole little-endian bytes and words at unit boundaries, so 32-bit absolute and 16-bit PC-relative relocations suffice and vlink needs no CPU-specific code.
- **Branch relaxation.** Out-of-range conditional branches are rewritten as the inverted branch over an absolute JMP, the same trick the 6502 module offers as an option.
- **Templates.** The vasm tree already carries modules for small and virtual CPUs — SWEET16, Trillek TR3200, unSP, and HANS, a custom processor from a hobby project. SWEET16 and HANS are the closest models.
- **Output.** `-Fvobj` objects for vlink.

A Python assembler is acceptable at gate T0 only, while the encoding is still moving. It is not a deliverable: separate compilation of the runtime and the C library needs a real object format and linker.

## Linking and NVX packaging

Executables are linked at fixed addresses and never relocated at load time. Every process owns its whole 24-bit space, so `.text` always starts at `$02_0000` and `.data` at `$10_0000` — the same addresses in every process. Position-independent code is not required and is deferred to a future shared-library story.

The chain is: vlink places sections per the linker script and writes one raw binary per segment plus a manifest naming each segment, its base address and its length (`-b rawseg`); `mknvx` reads that manifest and emits the NVX header — magic, ISA major/minor, feature bits, entry PC, section table, requested stack size.

Addressing globals costs 10 bytes today (`MOVI32` of the address, then `LDW`). If profiling justifies it, reserve a GP register for a small-data section: vlink by default merges base-relative sections into a single small data section addressed through a base register, which brings the common case down to a 4-byte `LDW rd,[gp+off16]`. Cost: one of the 16 registers. Decide at gate T5, not before.

## vc driver configuration

The `vc` driver needs only a config file, read as a collection of command-line arguments with one argument per line. Target name `nvm32-nova`, invoked as `vc +nvm32-nova`. Modeled on the existing unSP/V.Smile target configuration.

```
-cc=vbccnvm32 -quiet %s -o= %s %s -O=%ld -I$VBCC/targets/nvm32-nova/include
-ccv=vbccnvm32 %s -o= %s %s -O=%ld -I$VBCC/targets/nvm32-nova/include
-as=vasmnvm32_std -quiet -Fvobj %s -o %s
-asv=vasmnvm32_std -Fvobj %s -o %s
-rm=rm %s
-rmv=rm %s
-ld=vlink -b rawseg -Cvbcc -T$VBCC/targets/nvm32-nova/nvx.cmd -L$VBCC/targets/nvm32-nova/lib $VBCC/targets/nvm32-nova/lib/crt0.o %s %s -o %s -lvc
-ldv=vlink -b rawseg -Cvbcc -T$VBCC/targets/nvm32-nova/nvx.cmd -L$VBCC/targets/nvm32-nova/lib $VBCC/targets/nvm32-nova/lib/crt0.o %s %s -o %s -lvc -Mmapfile
-l2=vlink -b rawseg -Cvbcc -T$VBCC/targets/nvm32-nova/nvx.cmd -L$VBCC/targets/nvm32-nova/lib %s %s -o %s
-l2v=vlink -b rawseg -Cvbcc -T$VBCC/targets/nvm32-nova/nvx.cmd -L$VBCC/targets/nvm32-nova/lib %s %s -o %s -Mmapfile
-ul=-l%s
-cf=-F%s
-ml=1000
```

`-lvc` is the NVM32 runtime library; QNICE-FPGA follows the same convention, building its own `libvc.a` and `startup.o` into the target's lib directory. `mknvx` runs after `vc` from the build script, not from this config.

## Runtime and C library

vbcc's own support library is not source-available, so the entire runtime is new work. Four parts:

| Part | Contents |
| --- | --- |
| `crt0.o` | Set SP from the NVX header, clear BSS, call `main`, exit through `SYSCALL` |
| Libcall helpers | 64-bit multiply, divide, modulo and shifts; narrow conversions |
| Soft float | `float`/`double` arithmetic and conversions |
| libc | The ISO C library, over the NVM32 syscall service table |

For soft float, Berkeley SoftFloat (John R. Hauser) is the base; vbcc's own math library draws on the same code, so the interfaces are known to fit. For libc, PDCLib is the recommended port: CC0, deliberately only the ISO C library with no POSIX extensions, and ported by copying its `platform/example/` directory and adapting the glue — which here is the syscall service table. PDCLib targets C11 while vbcc supports C89 plus parts of C99, so budget time for edits.

Floating point warning: soft float interpreted on an 8 MHz 65816 costs hundreds of VM instructions per operation, each 60–120 host cycles. Applications in the Apple II and Amiga milestones should avoid it. Whether floats deserve opcodes in the reserved space is a decision that must be made before the ISA freeze at gate T4, not after.

## Single source for the encoding

The NVM32 encoding gets one machine-readable table — opcode, format, length, mnemonic, operand kinds, semantics class — and every consumer is generated from it. There are six consumers and the encoding will change at least once more before the freeze:

1. The vasm CPU module's opcode table.
2. The disassembler.
3. The C11 reference interpreter's decoder and length table.
4. The 65816 interpreter's 256-entry dispatch table and length table (ca65).
5. The softcore's combinational decode map (Verilog).
6. The backend's mnemonic names.

Hand-maintaining six copies is the most likely source of a semantic divergence between executors, which is exactly what the bit-identical requirement forbids. The generator scripts live with the conformance suite.

## Verification and gates

The C11 reference interpreter makes every test a fast host-side loop, so the compiler is validated long before any of it runs on hardware. Three layers, in the NVFS pattern already proven on the file system:

- **Conformance suites at `-O0` and `-O2`.** c-testsuite, plus the tests from Nora Sandler's *Writing a C Compiler* (which a q3vm fork already uses to verify its own lcc-based bytecode toolchain). A failure only at `-O2` normally means the optimizer exposed a backend assumption, not an optimizer bug.
- **Csmith differential testing.** Each generated program prints a checksum: compile with host gcc and with `vbccnvm32`, run the latter on the reference interpreter, compare. Restrict the generator to implemented features until gate T4.
- **Opcode histogram.** The reference interpreter counts opcodes as it runs the suites and real programs. This is the evidence the ISA freeze rests on, and the input to the superinstruction decisions.

| Gate | Exit criterion |
| --- | --- |
| T0 | `vasmnvm32_std` assembles the hand-written conformance tests; traces match on the reference interpreter |
| T1 | Trivial programs, loops and calls compile at `-O0`; `crt0` and the exit syscall work |
| T2 | Every IC handled; conformance suites pass at `-O0` and `-O2` |
| T3 | Csmith differential testing clean over a large seed range; opcode histogram collected |
| T4 | 64-bit and soft-float helpers, libc port, `printf` over syscalls; float-opcode decision made; **NVM32 v1 frozen** |
| T5 | Addressing modes, peephole, GP small data; code density and VM-instruction counts measured on a fixed benchmark set |

Effort estimate, solo and part-time: the backend alone matches the 1–3 months in DN-SW-VMISA-001; with the vasm module, runtime, libc port and test harness, the whole toolchain is closer to 3–5 months. These are estimates, not measurements.

## Alternatives considered

vbcc wins on code quality for an interpreted target: its cross-module inlining, global common-subexpression elimination, loop optimizations and inter-procedural register allocation remove VM instructions, and each removed instruction is 60–120 cycles on the 65816.

| Option | Strength | Why not first choice |
| --- | --- | --- |
| lcc | Proven for C-to-VM — id Software derived the Quake III VM instruction set from lcc's bytecode interpreter target; backend documented in the Fraser/Hanson book | C89 only; modest optimizer; the author of the Z-machine vbcc backend abandoned lcc because its backend interface was only partially documented |
| TCC | Compiler, assembler and linker in one small package | Barely optimizes, which is the most expensive property on this target |
| QBE + cproc | Small, modern SSA backend | 64-bit only today: targets amd64, arm64 and riscv64, 32-bit ports were a wish-list item in 2022, and the IL hard-codes pointers as 64-bit `l`. An ILP32 port of both is a project in itself |
| GCC | Mature, complete C | Machine description plus a binutils port; many months solo |
| LLVM | Best code, C++ and Rust, and a redistributable licence | Many months solo; justified only if licensing (section "Licensing and contribution constraints") or Rust becomes a requirement |

Fallback ranking if vbcc fails: LLVM if redistribution matters, lcc if it does not.

## Abandonment conditions and open questions

Abandonment conditions, in the project's usual form — each can be acted on without reopening this note:

- If the vbcc author declines the target and redistribution turns out to matter, drop vbcc at gate T1 and take the LLVM route; work done on the vasm module, runtime and tests is unaffected.
- If measured code density at gate T5 is worse than 1.4× hand-written NVM32 assembly on the benchmark set, add GP small data and immediate-compare branches before adding any further superinstruction.
- If the backend is not through gate T2 after four months of part-time work, freeze feature work and ship what compiles: the interpreter and softcore do not depend on the compiler.
- If PDCLib resists vbcc's C99 subset for more than two weeks, write a minimal freestanding libc instead (`string.h`, `stdlib.h` subset, `printf` over syscalls) and defer full conformance.

Open questions:

- [ ] Does floating point get opcodes in the reserved space, or stay soft float? Blocks the T4 freeze.
- [ ] Is a GP small-data register worth one of the 16 registers? Decide with T5 measurements.
- [ ] Which benchmark set defines "code density" and "VM instructions per second" for this project? Dhrystone plus two real noVa64 programs is the proposal.
- [ ] Does the syscall service table get C headers generated from the same source as the kernel's, or hand-written twice?

## Appendix A — change requests against DN-SW-VMISA-001

Writing even a skeleton backend exposed six Rev A decisions that cost instructions in ordinary C. All are requested for Rev B, to be resolved before the T4 freeze.

| # | vbcc pattern | Cost on Rev A | Requested change |
| --- | --- | --- | --- |
| A1 | `TEST` + `BEQ`/`BNE` — every `if (x)`, every `while (n--)` | No zero register, so each test needs a `MOVIZ` first | Add `BEQZ`/`BNEZ`/`BLTZ`/`BGEZ`/`BLEZ`/`BGTZ rs,disp16` |
| A2 | `COMPARE` against a small constant | Constant loaded into a temporary first | Immediate-compare branches, if the T3 histogram agrees |
| A3 | `CONVERT` to or from `char`/`short` | Sign extension needs `SHL`+`SAR` by 24, slow in the interpreter | Add `SEXT8`/`SEXT16` and `ZEXT8`/`ZEXT16` |
| A4 | Struct `ASSIGN`/`PUSH` (block copies) | `MEMCPY` hard-wires R0–R2, forcing register shuffles at every call site | Give `MEMCPY`/`MEMSET` explicit register operands; restart state lives in those same registers, so §1.4 is unaffected |
| A5 | Access to globals | disp16 PC-relative cannot reach `.data` at `$10_0000` from `.text` at `$02_0000` | See correction 2 below |
| A6 | Code-generator temporaries | No memory-to-memory operations | Reserve R11 and R12 for the code generator; R8–R10 stay callee-saved |

Three corrections to Rev A, recorded as errors rather than requests:

1. **QBE is not a "strong second".** It targets amd64, arm64 and riscv64; 32-bit ports were a wish-list item in its maintainers' 2022 talk, and IL written for it hard-codes pointer-sized values as 64-bit. Porting QBE and cproc to ILP32 is a project in itself. Rev B should demote it to a long shot; the replacement second choice is LLVM or lcc, per section "Alternatives considered".
2. **The PIC paragraph in §1.7 is wrong.** It relies on a global pointer the §1.6 ABI table never reserves, and a 16-bit PC-relative displacement cannot reach the data region from the text region in the §1.2 memory map. Since every process owns its whole 24-bit space, executables need no PIC at all: link at fixed addresses, never relocate at load. PIC belongs only to a future shared-library mechanism.
3. **The freeze order in Stage 0 is wrong.** "Ratify the ISA now" should become "ratify at gate T4", once the compiler produces real code and the reference interpreter has produced an opcode histogram. Freezing before the first backend exists is how A1–A4 would have become permanent.

## Appendix B — change requests against DN-SW-VMINTERP-001

NVM32 has no interrupts: architectural state is {R0–R15, PC} and nothing else. Interrupts are a host phenomenon and the bytecode never sees them. Rev A of the interpreter note states the wrong reason for this, which is worth correcting because the wrong reason leads to a needless polling loop.

**B1 — Correction to §1.5.** Rev A says the interpreter is preemptible "because the host timer IRQ is honored only between handler dispatches". That is false: the 65816 accepts an IRQ at any host instruction boundary, including between the low and high `ADC` of a 32-bit `ADD`. What actually makes preemption safe is that the partial state of a VM instruction lives in A, X, Y, P and the process's direct page, so a kernel context switch saving A, X, Y, P, D, DBR, PBR, S and PC implicitly saves the half-finished VM instruction. On resume the handler continues where it was. No VM-level atomicity and no polling in the dispatch loop are required. Rev B should state that VM-instruction atomicity is required for faults and for exporting state, not for preemption.

**B2 — Context switch cost.** Follows from B1 and is worth recording as a benefit: an interpreted process is no more expensive to switch than a native one. The 16 VM registers are never saved — they stay in their direct-page block — so a switch is D, S and the ASID. Cheaper than a real register file.

**B3 — Reaching a VM instruction boundary.** Three cases need one: migrating a process to the softcore, showing {R0–R15, PC} to a debugger, and delivering anything asynchronous to the bytecode itself. Two mechanisms:

| Mechanism | Fast-path cost | Notes |
| --- | --- | --- |
| Flag tested in `NEXT` | \~5–6 cycles of every 60–120, a permanent \~5% tax | Simple, but paid 400,000 times a second for a rare event |
| Shadow dispatch bank | Zero | A second copy of the interpreter in another program bank whose 256 `OPTAB` entries all point at the exit handler. The kernel switches the process's saved PBR; the running handler finishes, reaches `NEXT`, and lands in the kernel at the next exact boundary. With the MMU the code pages are physically shared; only the table page differs |

Recommendation: the shadow bank, precisely because the event is rare.

**B4 — Kernel requirements.** The IRQ is taken on the interpreted process's own stack and direct page, since the 65816 pushes PB, PC and P to bank $00 before the handler runs. Therefore:

- The reserved host region (the process's 65816 DP and stack) must be resident and guarded. A page fault while pushing the interrupt frame is not a debuggable situation.
- The handler may assume nothing about D, DBR or the M/X widths; it enters with whatever the interpreter had. Set widths explicitly and load the kernel's own D before touching anything.
- The interpreter keeps DBR pointing at the bytecode's code bank; kernel code that relies on DBR must set it.

**B5 — Latency and quantum.** The interpreter needs no critical sections, so no `SEI` is required anywhere in the dispatch loop. `MEMCPY` on `MVN`/`MVP` is interruptible between bytes with X, Y and C as progress, so a 64 KB copy delays an interrupt by at most 7 cycles — the same property §1.4 already requires for restart. Worst-case latency is therefore set by PHI2 stretching, not by the interpreter: during a cache miss the clock is stopped and no IRQ is taken. The scheduler quantum must come from the fixed frequency reference, never from counting VM instructions or cycles. Direct cost is negligible: a 100 Hz tick with a 200-cycle handler is 0.25% of an 8 MHz budget; switches and cache pollution dominate.

**B6 — Symmetry with the softcore.** Device interrupts belong to the 65816; the softcore is only stopped and resumed. Both executors therefore share one rule — the outside world interrupts the host, never the VM, and state is exported only at instruction boundaries — which is what makes mid-execution migration between interpreter and softcore possible.

## Sources

Pages consulted for the tool interfaces, licences and precedents cited above.

- [vbcc backend interface](http://sun.hasenbraten.de/vbcc/docs/vbcc_13.html) — backend files, build, IC model, required functions, register tables, optimization hints
- [vbcc general and licence](http://sun.hasenbraten.de/vbcc/docs/vbcc_1.html) and [the frontend](http://sun.hasenbraten.de/vbcc/docs/vbcc_2.html) — terms, optimizer list, `vc` config file
- [vasm licence and AI disclaimer](http://sun.hasenbraten.de/vasm/release/vasm_1.html), [module interface](http://sun.hasenbraten.de/vasm/release/vasm_toc.html), [HANS module](http://sun.hasenbraten.de/vasm/release/vasm_39.html), [6502/65816 module](http://sun.hasenbraten.de/vasm/release/vasm_28.html)
- [vlink manual](https://github.com/8l/vlink/blob/master/vlink.texi) — rawbin1, rawbin2, rawseg, small-data merging
- [QNICE-FPGA](https://github.com/sy2002/QNICE-FPGA) — vbcc/vasm/vlink toolchain for a custom FPGA CPU
- [vbcc 0.9d release notes](https://os4coding.net/news/vbcc-09d-c-compiler-released) — libcall interface, SoftFloat provenance
- [Review of vbcc](https://briancallahan.net/blog/20211204.html) — libvc is not source-available
- [PDCLib](https://github.com/DevSolar/pdclib) and [porting notes](https://e43oss.atlassian.net/wiki/spaces/PDCLIB/pages/360469/Porting)
- [Q3VM specification](https://icculus.org/~phaethon/q3mc/q3vm_specs.html) and [q3vm](https://github.com/dinoboards/q3vm) — lcc as a C-to-bytecode front end, C compiler test suites
- [QBE](https://c9x.me/compile/), [its IL](https://c9x.me/compile/doc/il.html), [FOSDEM 2022 talk](https://archive.fosdem.org/2022/schedule/event/lg_qbe/attachments/slides/4878/export/events/attachments/lg_qbe/slides/4878/qbe.pdf) — supported targets and the 32-bit question
- [vbccz](https://cowlark.com/vbcc-z-compiler/index.html) — a vbcc backend targeting a virtual machine
