# NVM32 — toolchain and measurement
> vbcc retargeted, not written · and the counters that freeze the ISA

Two halves of one problem. The first is how C source becomes an NVX image: a retargeted vbcc code generator, a vasm CPU module, vlink packaging and a runtime. The second is the harness that turns [VM1.44](sec_ai_vm1#vm144)'s four encoding candidates into queries against measured data, **so that the ISA freezes on evidence rather than on taste**. They share a sheet because they share a gate: the histogram the compiler produces at T3 is what decides the encoding at T4.

- VM4.1 — **This is the Stage 2 deliverable and it blocks nothing before Stage 1 exists** ([VM1.4](sec_ai_vm1#vm14)). The C11 reference interpreter comes first, is PC-side, and makes every test below a fast host-side loop — so the compiler is validated long before any of it runs on hardware.
- VM4.2 — **Four deliverables, all new work**: `vbccnvm32`, the NVM32 code generator in `machines/nvm32` · `vasmnvm32_std`, the NVM32 CPU module for vasm · a linker script, the `mknvx` packaging tool and the `vc` target configuration · and the runtime — `crt0`, libcall helpers, soft float and a C library port.
- VM4.3 — **The front end, the optimizer, the assembler core and the linker are reused unchanged, and only the target-specific parts are written here. That is the whole reason for choosing vbcc over a compiler of our own**, and it is also why [VM1.49](sec_ai_vm1#vm149) can dismiss the stack-machine argument: the historical case for stack VMs is that they need no register allocator, which matters only to a project writing its own compiler.

```
C source -> vbcc front end + optimizer -> NVM32 backend -> vasm core
         -> NVM32 cpu module -> vlink + linker script + runtime -> mknvx -> NVX image
```

- VM4.4 — **Where the effort actually lands**, and it is not where a first guess puts it:

| Component | Origin | Effort |
|---|---|---|
| vbcc front end, optimizer, `vc` driver | Reused | Configuration only |
| `machines/nvm32` code generator | New | 1,500–3,000 lines of C |
| vasm core, syntax and output modules | Reused | None |
| `cpus/nvm32` module | New | Opcode table plus encoder |
| vlink | Reused | Linker script only |
| `mknvx` | New | ~200 lines, host side |
| `crt0`, libcalls, soft float, libc | New or ported | **The largest non-backend item** |

- VM4.5 — **The same NVX image is the input to all three executors** — the C11 reference interpreter, the 65816 interpreter and the core — which is what makes differential testing possible at all ([VM3.20](sec_ai_vm3#vm320)).
- VM4.6 — **A private toolchain is permitted and publishing it is not, without written consent, and this must be settled before the backend is written because it can change the tool choice.** vbcc may be redistributed only unmodified and used non-commercially; **a vbcc carrying the NVM32 backend is a modified version.** vasm and vlink carry equivalent terms.
  NOTE: Two conversations are owed rather than one. The vbcc backend chapter asks anyone writing a code generator to **contact the author first** so the target can be integrated upstream — he wrote the QNICE-FPGA backend and its C library himself, so a custom FPGA CPU is familiar ground there. And the vasm manual states that vasm is human-made software and asks contributors to check beforehand whether a contribution might contain code not written by themselves, **explicitly naming generative AI**. Private use is fine either way; upstreaming requires both conversations first (→ [Q165](sec_ai_q#q165)).
- VM4.7 — **The consequence for the project: the NVM32 toolchain is shareable only as source patches the recipient applies to their own vbcc and vasm copies.** If it must ever be redistributable as a whole, the fallback is the LLVM route ([VM4.33](sec_ai_vm4#vm433)) — which is a months-long decision, not a licence checkbox, and is why this item sits before the backend rather than after it.

## The vbcc backend — two helpers and one interesting decision.

- VM4.8 — **`gen_code()` receives a doubly linked list of quadruples**: an operator with two source operands and a target, each of which is a constant, a register, a variable, or a dereference of one of those. **Most of the backend is therefore two helpers** — load any operand into a register, store a register into any target — **plus one case per operation**, around the required functions `init_cg`, `freturn`, `regok`, `dangerous_IC`, `must_convert`, `shortcut` and the data emitters. Start from the example backend shipped with vbcc, which models a generic 32-bit machine and is already close.
  NOTE: **That operand model is also evidence for an ISA question.** vbcc's quadruples may name memory directly, so a memory-to-memory VM maps onto them almost one to one — which is why fixed-form memory operands are *more* toolchain-friendly than the three-address form, not less ([VM1.46](sec_ai_vm1#vm146)).
- VM4.9 — **Because NVM32 has no memory-to-memory operations, the code generator needs two scratch registers of its own, and `regsa[]` is the documented way to reserve them.** R11 and R12 are taken; R8–R10 stay callee-saved ([VM1.37](sec_ai_vm1#vm137)). `dangerous_IC()` normally flags divisions and dereferences, but **NVM32 defines division totally** ([VM1.32](sec_ai_vm1#vm132)), so only dereferences need it.
- VM4.10 — **The flagless branch is the one NVM32-specific decision in the backend, and vbcc anticipates it.** vbcc models comparison as COMPARE or TEST setting condition codes, followed by a branch; with multiple condition-code registers left disabled, the instruction after a COMPARE **is guaranteed** to be the conditional branch. So COMPARE emits nothing, the branch emits one instruction, and the fused form of [VM1.13](sec_ai_vm1#vm113) costs the backend a buffered pointer rather than a pattern matcher:

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

- VM4.11 — **Correctness first, then speed — but speed matters more here than on real silicon.** An optimized backend commonly produces code up to twice as fast as a simple one, and **on an interpreted target that factor is paid in 65816 cycles per VM instruction** ([VM2.10](sec_ai_vm2#vm210)). Three optimizations, in order: folding pointer-add plus dereference into base+displacement addressing (vbcc's intermediate code has no addressing modes, so this is entirely the backend's job), a peephole pass over the buffered assembly output, and compact entry and exit through `ENTER`/`LEAVE` ([VM1.28](sec_ai_vm1#vm128)).

## The C ABI as implemented — ILP32, and where it meets Calypsi.

- VM4.12 — **Set in `init_cg()`, and it diverges from Calypsi by design** ([VM1.35](sec_ai_vm1#vm135)). The two ABIs meet only at the syscall marshalling layer:

| Item | Value |
|---|---|
| `char` / `short` / `int` / `long` | 1 / 2 / 4 / 4 bytes |
| `long long` | 8 bytes, in an r0:r1 register pair or in memory |
| Pointer | 4 bytes stored, 24 bits effective, top byte must be zero |
| `float` / `double` | 4 / 8 bytes, soft float through libcalls |
| `maxalign` | 4 |
| Arguments | R0–R3, then the stack; variadic arguments always on the stack |
| Return | R0, or the r0:r1 pair; structs through a hidden pointer |
| Caller-saved | R0–R7 |
| Callee-saved | R8–R10 |
| Code-generator temporaries | R11, R12, reserved through `regsa[]` |
| SP / LR / FP | R13 / R14 / R15 |

- VM4.13 — **Four mechanism notes, each of which is a place a backend usually goes wrong.** `reg_parm()` is told whether an argument belongs to the variadic part and returns 0 there, so `stdarg.h` stays plain pointer arithmetic. **Register pairs are declared as an extra register** with identical `regscratch` for both halves; 64-bit add and subtract are inlined with the carry produced by the unsigned `SETcc`, and 64-bit multiply, divide and all floating point go through the libcall interface vbcc introduced in 0.9d for exactly these cases. `must_convert()` returns 0 between `int` and pointer — same size, same representation. And `shortcut()` returns 0 initially: **promote everything to 32 bits and revisit only if profiling justifies it.**
- VM4.14 — **Registers always hold `char` and `short` values properly extended, so every comparison is a plain 32-bit one.** That invariant is what makes the `SEXT`/`ZEXT` request in [VM4.27](sec_ai_vm4#vm427) necessary rather than decorative. **`char` is signed, fixed now and never revisited.**

## The vasm module, linking and the driver.

- VM4.15 — **Standard relocations only.** NVM32 fields are whole little-endian bytes and words at unit boundaries ([VM1.25](sec_ai_vm1#vm125)), so 32-bit absolute and 16-bit PC-relative relocations suffice and **vlink needs no CPU-specific code at all**. Output is `-Fvobj` objects.
- VM4.16 — **Out-of-range conditional branches are relaxed into the inverted branch over an absolute `JMP`**, the same trick the 6502 module offers as an option. The vasm tree already carries modules for small and virtual CPUs — SWEET16, Trillek TR3200, unSP, and HANS, a custom processor from a hobby project — and **SWEET16 and HANS are the closest models to copy from**.
- VM4.17 — **A Python assembler is acceptable at gate T0 only, while the encoding is still moving. It is not a deliverable**: separate compilation of the runtime and the C library needs a real object format and a real linker.
- VM4.18 — **vlink places sections per the linker script and writes one raw binary per segment plus a manifest (`-b rawseg`); `mknvx` reads that manifest and emits the NVX header.** Executables are linked at fixed addresses and never relocated ([VM1.42](sec_ai_vm1#vm142)) — the same conclusion the native toolchain reached from the same premise ([O.5](sec_ai_o#o5)).
- VM4.19 — **Addressing a global costs 10 bytes today** — a `MOVI32` of the address, then `LDW`. If profiling justifies it, reserve a GP register for a small-data section: **vlink merges base-relative sections into a single small data section addressed through a base register by default**, which brings the common case down to a 4-byte `LDW rd,[gp+off16]`. The cost is one of the sixteen registers. **Decide at gate T5, not before** (→ [Q167](sec_ai_q#q167)).
- VM4.20 — **The `vc` driver needs only a config file, read as one command-line argument per line.** Target name `nvm32-nova`, invoked as `vc +nvm32-nova`, modelled on the existing unSP configuration. `-lvc` is the NVM32 runtime library, following the QNICE-FPGA convention of building `libvc.a` and `startup.o` into the target's lib directory; `mknvx` runs after `vc` from the build script, not from this config:

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

## Runtime and C library — the largest item that is not the backend.

- VM4.21 — **vbcc's own support library is not source-available, so the entire runtime is new work.** Four parts: `crt0.o` sets SP from the NVX header, clears BSS, calls `main` and exits through `SYSCALL` · libcall helpers for 64-bit multiply, divide, modulo and shifts, and narrow conversions · soft float · and a libc over the NVM32 syscall service table ([VM1.40](sec_ai_vm1#vm140)).
- VM4.22 — **Berkeley SoftFloat is the base for floating point** — vbcc's own math library draws on the same code, so the interfaces are known to fit. **PDCLib is the recommended libc**: CC0, deliberately only the ISO C library with no POSIX extensions, and ported by copying its `platform/example/` directory and adapting the glue, which here is the service table. **PDCLib targets C11 while vbcc supports C89 plus parts of C99, so budget time for edits.**
- VM4.23 — [[open]] **Soft float interpreted on an 8 MHz 65816 costs hundreds of VM instructions per operation, each 60–120 host cycles.** Applications in the Apple II and Amiga milestones should avoid it outright. **Whether floats deserve opcodes in the reserved space is a decision that must be made before the freeze at T4, not after** (→ [Q162](sec_ai_q#q162)).
- VM4.24 — **The encoding gets one machine-readable table — opcode, format, length, mnemonic, operand kinds, semantics class — and every consumer is generated from it.** There are six consumers and the encoding changes at least once more before the freeze: the vasm opcode table · the disassembler · the C11 reference interpreter's decoder and length table · the 65816 interpreter's 256-entry dispatch and length tables ([VM2.9](sec_ai_vm2#vm29)) · the core's combinational decode map ([VM3.6](sec_ai_vm3#vm36)) · and the backend's mnemonic names.
  NOTE: **Hand-maintaining six copies is the most likely source of a semantic divergence between executors, which is exactly what the bit-identical requirement forbids** ([VM1.8](sec_ai_vm1#vm18)). The generator scripts live with the conformance suite, not with any one consumer.

## Change requests still pending the freeze.

- VM4.25 — **Writing even a skeleton backend exposed six ISA decisions that cost instructions in ordinary C**, and this is the argument for [D94](sec_ai_q#d94) in its most concrete form: **freezing before the first backend existed is how these would have become permanent.** All six are resolved before the T4 freeze, on the evidence of the T3 histogram.

| # | vbcc pattern | Cost as first specified | Requested change |
|---|---|---|---|
| A1 | `TEST` + `BEQ`/`BNE` — every `if (x)`, every `while (n--)` | No zero register, so each test needs a `MOVIZ` first | Add `BEQZ`/`BNEZ`/`BLTZ`/`BGEZ`/`BLEZ`/`BGTZ rs,disp16` |
| A2 | `COMPARE` against a small constant | The constant is loaded into a temporary first | Immediate-compare branches, if the histogram agrees |
| A3 | `CONVERT` to or from `char`/`short` | Sign extension needs `SHL`+`SAR` by 24, slow in the interpreter | Add `SEXT8`/`SEXT16` and `ZEXT8`/`ZEXT16` |
| A4 | Struct assignment and push — block copies | `MEMCPY` hard-wires R0–R2, forcing register shuffles at every call site | Give `MEMCPY`/`MEMSET` explicit register operands |
| A5 | Access to globals | disp16 PC-relative cannot reach `.data` from `.text` | Resolved: fixed-address linking, no PIC ([VM1.42](sec_ai_vm1#vm142)) |
| A6 | Code-generator temporaries | No memory-to-memory operations | Reserve R11 and R12 ([VM4.9](sec_ai_vm4#vm49)) |

- VM4.26 — **A1 is the cheapest and the most valuable of the six**, because the pattern it removes is the most common one in C: every truth test of a variable pays a `MOVIZ` — a whole dispatch, ~55 cycles — for a comparison against a constant the ISA declined to hardwire ([VM1.10](sec_ai_vm1#vm110)). It is the clearest case where the interpreter's cost model and the compiler's output disagree with a decision taken for hardware reasons.
- VM4.27 — **A3 follows directly from [VM4.14](sec_ai_vm4#vm414)'s invariant.** Registers hold narrow types already extended, so every `char` and `short` conversion is a real instruction, and doing it with a pair of shifts costs two dispatches where one opcode would do.
- VM4.28 — **A4 does not weaken the restart rule, and that is worth stating because it looks as if it might.** Giving `MEMCPY` and `MEMSET` explicit register operands leaves the restart state exactly where it was — **in whichever registers the instruction names** — so [VM1.33](sec_ai_vm1#vm133) is unaffected. What it removes is a register shuffle at every call site, forced by hard-wiring R0–R2 in an ABI where R0–R3 are also the argument registers.
- VM4.29 — **Three further corrections are recorded as errors rather than as requests**, in the project's habit of keeping withdrawn decisions visible ([D02](sec_ai_q#d02)). **QBE was wrongly described as a strong second choice**: it targets amd64, arm64 and riscv64, 32-bit ports were a wish-list item in its maintainers' 2022 talk, and its intermediate language hard-codes pointer-sized values as 64-bit — an ILP32 port of QBE and cproc is a project in itself. **The PIC strategy was unworkable** and is replaced by fixed-address linking ([VM1.42](sec_ai_vm1#vm142)). **And the freeze order was wrong**, which is [D94](sec_ai_q#d94).

## Verification and gates.

- VM4.30 — **Three layers, in the pattern already proven on the file system** ([Y2.22](sec_ai_y2#y222)). **Conformance suites at `-O0` and `-O2`** — c-testsuite plus the tests from Nora Sandler's *Writing a C Compiler*, which a q3vm fork already uses to verify its own lcc-based bytecode toolchain; a failure only at `-O2` normally means the optimizer exposed a backend assumption, not an optimizer bug. **Csmith differential testing** — each generated program prints a checksum, compiled with host gcc and with `vbccnvm32`, the latter run on the reference interpreter and the two compared, with the generator restricted to implemented features until T4. **And the opcode histogram**, counted by the reference interpreter as it runs the suites and real programs.
  NOTE: **The histogram is not a by-product. It is the evidence the ISA freeze rests on** and the input to every superinstruction decision ([VM4.45](sec_ai_vm4#vm445)).
- VM4.31 — **Six gates, and the ISA freezes at the fifth:**

| Gate | Exit criterion |
|---|---|
| T0 | `vasmnvm32_std` assembles the hand-written conformance tests; traces match on the reference interpreter |
| T1 | Trivial programs, loops and calls compile at `-O0`; `crt0` and the exit syscall work |
| T2 | Every intermediate construct handled; conformance suites pass at `-O0` and `-O2` |
| T3 | Csmith differential testing clean over a large seed range; **opcode histogram collected** |
| T4 | 64-bit and soft-float helpers, libc port, `printf` over syscalls; float-opcode decision made; **NVM32 v1 frozen** |
| T5 | Addressing modes, peephole, GP small data; code density and VM-instruction counts measured on a fixed benchmark set |

- VM4.32 — **Effort, solo and part-time: the backend alone is 1–3 months; with the vasm module, the runtime, the libc port and the test harness, the whole toolchain is closer to 3–5.** These are estimates, not measurements, and they are the reason [VM4.35](sec_ai_vm4#vm435)'s abandonment condition exists.
- VM4.33 — **Alternatives considered. vbcc wins on code quality for an interpreted target**, because cross-module inlining, global common-subexpression elimination, loop optimizations and inter-procedural register allocation each remove VM instructions, **and every removed instruction is 60–120 cycles**:

| Option | Strength | Why not first choice |
|---|---|---|
| lcc | Proven for C-to-VM — id Software derived the Quake III VM instruction set from lcc's bytecode target; the backend is documented in the Fraser and Hanson book | C89 only; modest optimizer; the author of the Z-machine vbcc backend abandoned lcc because its backend interface was only partially documented |
| TCC | Compiler, assembler and linker in one small package | Barely optimizes, **which is the most expensive property on this target** |
| QBE + cproc | Small, modern SSA backend | 64-bit only today, and the IL hard-codes pointers as 64-bit ([VM4.29](sec_ai_vm4#vm429)) |
| GCC | Mature, complete C | Machine description plus a binutils port; many months solo |
| LLVM | Best code, C++ and Rust, and a redistributable licence | Many months solo; justified only if licensing ([VM4.6](sec_ai_vm4#vm46)) or Rust becomes a requirement |

  NOTE: **vbcc being ruled out elsewhere in this document is not a contradiction.** [O.2](sec_ai_o#o2) rules it out as a *65816* compiler, where it has no 16-bit code generation at all; here it is a cross compiler for a target that does not exist yet, which is the case it is built for ([D96](sec_ai_q#d96)). Fallback ranking if vbcc fails: LLVM if redistribution matters, lcc if it does not.

## The measurement harness — turning arguments into queries.

- VM4.34 — **Four encoding candidates compete in [VM1.44](sec_ai_vm1#vm144) and none can be decided by argument, so the harness decides them.** It **needs no hardware and no compiler**: it runs entirely on the PC against the C11 reference interpreter, which makes it **the cheapest possible way to retire the biggest open risk in the ISA**. The reference interpreter produces the counters, the counters decide the candidates at T3, the ISA freezes at T4.
- VM4.35 — **Four numbers per benchmark, because counting VM instructions alone is not enough** — a `MEMCPY` and a `NOP` count the same, and the candidates under evaluation change precisely how many instructions do the same work:

| Metric | What it measures | Source |
|---|---|---|
| VM instructions executed | Dynamic work | A counter in the reference interpreter |
| **Estimated 65816 cycles** | The metric that decides | Instruction counts × a per-opcode cost table |
| `.text` bytes | Code density — priority #2 — and Argon 2's code bandwidth | Section size |
| Guest memory accesses | The core's bottleneck, and cache pressure | Counters in the load and store handlers |

- VM4.36 — **The cost model is the core of this section, and it does not need to be accurate — it needs to be consistent**, because every use compares variants against each other rather than against the world. It is a per-opcode table in 65816 cycles derived from the handlers of [sheet VM2](sec_ai_vm2). **When the real interpreter exists, calibrate the table against measured cycles and re-run the stored history so that old comparisons stay valid.**
- VM4.37 — **The fourth metric exists because the cycle model is blind to the thing that dominates on real hardware.** Cache misses with PHI2 stopped set wall-clock time ([VM2.15](sec_ai_vm2#vm215)), and no cycle count can see them; **guest memory accesses are the available proxy for cache pressure** until hardware measurement is possible.
- VM4.38 — **The interpreter records patterns, not just opcodes, and this is the part most harnesses skip.** Each pending ISA question is answered before anything is implemented, which is far cheaper than building four ISA variants and comparing them:

| Counter | Answers |
|---|---|
| Adjacent load-op and op-store pairs | The value of fixed-form memory operands |
| Instructions where `rd == rs` | The value of two-address forms |
| Runs of N instructions sharing a destination, with the histogram of N | The value of `WITH`/RP |
| Calls per thousand instructions, and mean stack depth | The value of register windows |
| Branch distance distribution | Whether disp16 is the right size |
| Immediate value distribution | Whether smaller immediate forms would pay |
| Register pressure — live values at each point | Whether sixteen registers is right, and what windows would cost |

  NOTE: **The design rule that follows: before proposing an encoding change, add the counter that would justify it. A proposal without a counter is not ready to be argued about.** Each counter is a few lines in the reference interpreter and costs nothing at run time on the real machine, because it exists only in the PC model.
- VM4.39 — **Three groups of benchmark, each answering a different question.** **Standard**, for comparability with published numbers: **Dhrystone**, mandatory and notoriously unrepresentative — heavy on string handling, light on real arithmetic, so **report it and never optimize for it** — and **Coremark**, designed specifically to correct Dhrystone's defects.
- VM4.40 — **Real noVa64 code is the only group that says whether a change matters.** NVFS is the best candidate available today: already written, already carrying a test suite, and exercising pointer and structure handling ([sheet Y2](sec_ai_y2)). A GUI workload joins it once one exists ([sheet V](sec_ai_v)).
- VM4.41 — **Six synthetic kernels, each isolating one axis, to explain *why* a number moved**: array traversal (sequential access, loop overhead) · linked-list walk (pointer-intensive, cache-hostile) · recursion (call density, stack depth) · structure copy (block operations) · call-heavy code with shallow bodies (frame cost, the register-window question) · expression-heavy arithmetic (the `WITH`/RP question).
  NOTE: Until the compiler exists at T2 these are hand-written in NVM32 assembly — tedious but feasible, and **they are the group that matters most for encoding decisions**. The real programs wait for the compiler.
- VM4.42 — **Four rules of discipline, because this is where benchmark suites usually rot.** **The set is frozen**, and changing it invalidates the whole history; additions go into a clearly separated second set with its own baseline. **Results are versioned with the code**, as CSV or JSON in the repository, one row per interpreter commit — **what is wanted is a trend over months, not isolated figures**. **A variant is measured against the baseline from the same source**, recompiled or reassembled, never hand-tuned code for one variant against generic code for another. And **regression is automatic**: a change that worsens a metric beyond a set threshold fails the build.
- VM4.43 — **Record the calibration date in the results**, so that nobody compares pre- and post-calibration numbers without noticing ([VM4.36](sec_ai_vm4#vm436)).
- VM4.44 — **What the harness cannot see is PHI2 stall time.** Every number it produces is a cycle-model number, and on hardware the real figure is worse by an amount that depends on working-set size. **State that caveat next to any throughput claim that leaves this project.**
- VM4.45 — **The decision criteria, written in advance so that the measurement decides rather than the person reading it.** Each threshold is a first proposal to be ratified **before** the counters are run — setting them afterwards is how a benchmark becomes an argument (→ [Q163](sec_ai_q#q163)):

| Candidate | Counter | Adopt if | Otherwise |
|---|---|---|---|
| Fixed-form memory operands | Adjacent load-op / op-store pairs | More than 20% of executed instructions participate in such a pair | Reject; the restart and microcode cost is not repaid |
| Two-address forms | Instructions with `rd == rs` | Any measurable share — the opcodes are already reserved and the cost is zero | Keep them reserved and unused |
| `WITH` / RP | Runs of ≥3 instructions sharing a destination | More than 20–25% of instructions sit in such a run | Reject; two-address forms already captured the benefit |
| Register windows | Calls per thousand instructions, mean depth | Call-dense code with shallow frames, and spill counts that windows would remove | Reject; the sixteen-register conflict is not worth 5–15% |
| A superinstruction for a hot opcode | Per-opcode cycle share | Any single opcode exceeds 150 interpreter cycles or a large share of total cycles | No action |

- VM4.46 — **A candidate that adds architectural state clears a higher bar than one that does not**, because {R0–R15, PC} is what migration and cheap preemption are built on ([VM1.14](sec_ai_vm1#vm114)). The rule is stated here rather than left to judgement at the table.

## Abandonment conditions and what is left open.

- VM4.47 — **If the vbcc author declines the target and redistribution turns out to matter, drop vbcc at gate T1 and take the LLVM route.** Work done on the vasm module, the runtime and the tests is unaffected, which is why T1 is the right place to decide.
- VM4.48 — **If measured code density at T5 is worse than 1.4× hand-written NVM32 assembly**, add GP small data and immediate-compare branches before adding any further superinstruction.
- VM4.49 — **If the backend is not through T2 after four months of part-time work, freeze feature work and ship what compiles.** The interpreter and the core do not depend on the compiler ([VM4.41](sec_ai_vm4#vm441)).
- VM4.50 — **If PDCLib resists vbcc's C99 subset for more than two weeks, write a minimal freestanding libc instead** — `string.h`, a `stdlib.h` subset, `printf` over syscalls — and defer full conformance.
- VM4.51 — **If hand-writing the synthetic kernels proves slower than expected, cut the set to three** — array traversal, linked-list walk, call-heavy — **and accept narrower evidence rather than delaying T3.**
- VM4.52 — **If the cost model cannot be calibrated to within ±25% of measured 65816 cycles, stop reporting estimated cycles as a headline number** and use instruction counts plus memory accesses instead.
- VM4.53 — **Five things stay open**: whether floating point gets opcodes (→ [Q162](sec_ai_q#q162)) · whether a GP small-data register is worth one of the sixteen (→ [Q167](sec_ai_q#q167)) · what exactly the project's benchmark set is, and whether [VM4.45](sec_ai_vm4#vm445)'s thresholds are accepted as written (→ [Q163](sec_ai_q#q163)) · whether the syscall service table's C headers are generated from the same source as the kernel's or written twice · and where the results live, and who owns re-running the history after a calibration.
