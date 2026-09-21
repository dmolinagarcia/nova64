# DN-SW-VMISA-001 — NVM32 instruction set architecture

*noVa64 · Rev B (draft for discussion)* · 2026-09-20 · @Someone

## Revision history

| Rev | Date | Change |
| --- | --- | --- |
| A | 2026-09-17 | Initial draft. 16 registers, 16-bit code units, flagless compare-and-branch, 32-bit-clean pointers, RISC-V division conventions. |
| B | 2026-09-20 | Three corrections to Rev A (below). VM work deferred with a written reopening condition. Encoding candidates added as open, measurement-gated items. ISA freeze moved from "now" to toolchain gate T4. |
| B.1 | 2026-09-21 | "Settle now" list extended with the three noVa128 portability requirements and the emulator's `WDM` rule. |

**Corrections to Rev A**, recorded as errors rather than open questions:

1. **The PIC strategy in Rev A §1.7 was wrong.** It relied on a global pointer the ABI table never reserves, and a 16-bit PC-relative displacement cannot reach the data region from the text region in this memory map. Corrected in "Executable format": executables are linked at fixed addresses and never relocated.
2. **QBE was wrongly described as a strong second choice for the compiler.** It targets 64-bit architectures only. Corrected in DN-SW-VMCC-001; the second choice is LLVM or lcc.
3. **The freeze order was wrong.** Rev A said "ratify the ISA now". The ISA freezes at gate T4, once the compiler produces real code and the reference interpreter has produced an opcode histogram. Freezing before the first backend existed is how the change requests in DN-SW-VMCC-001 appendix A would have become permanent.

*Review cycle: reviewers, dates and decisions to be recorded here before this note leaves draft status.*

## Status and scope

**Status: deferred.** noVa64 continues with the original plan — native 65816 code compiled by Calypsi. NVM32 becomes the application development model once the OS and a minimal kernel exist; Calypsi stays the kernel compiler.

Rationale for deferring: the dependency runs one way. The VM needs a process model, page tables, a loader, a scheduler and a service table from the kernel; the kernel needs nothing from the VM. Building the VM first means guessing interfaces that do not exist yet. The headline argument for NVM32 — one binary on interpreter and softcore — also depends on FPGA-C, which is itself deferred.

**Reopening condition: the kernel executes its first user process, with page faults and syscalls working.** At that point Stage 1 (the C11 reference interpreter inside DN-SW-EMU-001) begins; it is PC-side software and competes for no hardware time.

Seven things must be settled before then, because they cost nothing now and are expensive later. The last three come from DN-HW-ARGON-001: they are what keeps the noVa64 kernel binary-compatible with noVa128.

| Decision | Owner | Why now |
| --- | --- | --- |
| Kernel services reached through a numbered service table, not `COP` signatures scattered through the code | Kernel | The VM must never call native code directly; this is also better hygiene for the kernel itself |
| Banks `$00`–`$01` reserved in the process memory map from the first loader, plus one shared read-only page for the system information block | Kernel / loader | Reclaiming that region later is a migration |
| Memory-mapped registers idempotent on read (NEON, Helium) | NEON, Helium | Required by Argon's 65816 modes; discovering it late means redesigning the command port |
| The PC emulator can host a reference interpreter, and treats `WDM` as a two-byte no-op | DN-SW-EMU-001 | Nothing to write yet; the `WDM` rule keeps the CPU probe honest |
| No cycle counting for time; the fixed frequency reference is the only clock | Kernel, drivers | Already implied by PHI2 stretching; stating it makes it binding |
| No self-modifying code in project code | Kernel, BIOS | Avoids depending on Argon's instruction-cache snooping |
| Software-visible Helium and NEON register map kept stable, versioned through the system information block if it ever changes | Helium, NEON, BIOS | The 16-bit inter-FPGA path of noVa128 must not leak into the programmer's model |

Accepted risk: the kernel ABI will be shaped around Calypsi's 16-bit `int`. The marshalling layer specified here exists for exactly that reason.

This note specifies the ISA. The interpreter is DN-SW-VMINTERP-001, the coprocessor DN-HW-VMCORE-001, the toolchain DN-SW-VMCC-001, the measurement harness DN-SW-VMBENCH-001, and the noVa128 tri-mode core DN-HW-ARGON-001.

## Programmer's model

- **16 general-purpose 32-bit registers, R0–R15.** Eight is too few for a C ABI without spills, which are expensive in the interpreter. Sixteen fits a 4-bit nibble field and costs 64 bytes of direct page in the interpreter and a small EBR register file in hardware. Thirty-two would need 5-bit fields, which breaks clean nibble decode — the single most load-bearing constraint in this encoding.
- **No hardwired zero register.** It aids RISC hardware but wastes a register in the interpreter and complicates the 16-registers-in-64-DP-bytes mapping. `MOVIZ rd,#0` covers it.
- **Register roles are ABI conventions, not hardware.** R13 = SP, R14 = LR, R15 = FP, R0–R3 = arguments and returns. Both implementations treat all 16 uniformly.
- **PC: 24-bit, byte-addressed, instructions 16-bit aligned.** Always points into the 24-bit guest virtual space.
- **Flagless.** Comparisons fold into branches (`Bcc rs,rt,disp`) or `SETcc rd,rs,rt`. There is no architectural status word. A flags register would force the interpreter to compute and store N/Z/V/C after every ALU operation — dead work most of the time — and would force flag-forwarding hazards on the core.
- **Architectural state is {R0–R15, PC} and nothing else.** A single non-architectural trap-cause latch exists, used only when halted for a fault or syscall.

That last line is the invariant the whole design rests on. It is what makes a process migratable mid-execution between interpreter and softcore, and what makes preemption cheap. Any proposal that adds architectural state — a flags register, a window pointer, an implicit accumulator — is paying against it and must say so explicitly (see "Encoding candidates pending measurement").

## Memory model

**Little-endian**, matching the 65816.

**Pointers are 32-bit values; only the low 24 bits are address, and bits \[31:24\] must be zero.** Any load, store or branch whose effective pointer has non-zero high bits raises `ADDR_FAULT`, enforced identically on both implementations.

This is the 68000 lesson applied deliberately. Classic Mac OS and AmigaOS stored flags in the high byte of pointers because the hardware ignored it; the resulting "32-bit dirty" software broke when full 32-bit addressing arrived. ARM's Top-Byte-Ignore shows tagging can be clean when defined from day one, so TBI-style semantics stay reserved behind a feature bit — but the default is check and trap.

**Guest memory map** (per-process 24-bit virtual space):

| Range | Size | Use |
| --- | --- | --- |
| `$00_0000`–`$00_FFFF` | 64 KB | Reserved host region: 65816 direct pages, 65816 stack, interpreter scratch, VM state window, trap mailbox. Guest may not touch it |
| `$01_0000`–`$01_FFFF` | 64 KB | Interpreter code and dispatch table, read-only, shared, mapped identically in every process |
| `$02_0000`–`$0F_FFFF` | \~896 KB | Guest code, read-execute |
| `$10_0000`–`$EF_FFFF` | \~14 MB | Guest data, BSS and heap, read-write |
| `$F0_0000`–`$FF_FFFF` | 1 MB | Guest stack, growing down from `$FF_FFFF`, guard page at the bottom |

The loader refuses to map guest segments over banks `$00`–`$01`, and the MMU marks them no-access for guest pointers. On the softcore those banks are never generated as guest effective addresses; the region stays reserved only so one binary's memory map is valid on both executors.

**Alignment:** 16- and 32-bit accesses require natural alignment; misalignment raises `ADDR_FAULT`. Unaligned access costs extra cycles and bank-carry handling on the 65816 and forces byte assembly in hardware.

**Faults:** unmapped page → `PAGE_FAULT`; permission or copy-on-write → `PROT_FAULT`. Both restartable, per the commit rules in DN-SW-VMINTERP-001.

## Instruction encoding

Every instruction is one, two or three 16-bit little-endian code units. **The opcode byte alone determines length**, via a 256-entry table in the interpreter or a small combinational map in hardware.

This is the Dalvik pattern, chosen because it suits both targets. On the 65816 a single 16-bit fetch yields an 8-bit opcode plus two 4-bit register fields — exactly what the dispatcher needs. In hardware the first byte gives length and format with no multi-way logic.

| Format | Layout (unit0 \[unit1\] \[unit2\]) | Example |
| --- | --- | --- |
| F1 | `op:8 \| 00:8` | `HALT`, `RET`, `NOP`, `YIELD` |
| F2 | `op:8 \| rd:4 rs:4` | two-address `ADD rd,rs`, `MOV`, `NEG` |
| F3 | `op:8 \| rd:4 rs:4` + `rt:4 z:4 :8` | three-address `ADD rd,rs,rt` |
| F4 | `op:8 \| rd:4 z:4` + `imm:16` | `MOVIZ`/`MOVIS rd,#imm16` |
| F5 | `op:8 \| rd:4 z:4` + `imm:32` | `MOVI32 rd,#imm32` |
| F6 | `op:8 \| rd:4 rs:4` + `disp:16` | load/store `[rs+disp]`, PC-relative |
| F7 | `op:8 \| rs:4 rt:4` + `disp:16` | compare-and-branch `Bcc rs,rt,disp` |

Three rules exist for the 65816 and must not be relaxed:

1. Register fields are always the two nibbles of unit0's second byte, so one load plus a mask extracts them, pre-doubled to index 4-byte DP slots.
2. Immediates and displacements are always whole 16- or 32-bit little-endian words at a unit boundary — never bitfields spanning bytes. This is why RV32I was rejected as the native format.
3. Sign extension is decided by opcode (`MOVIZ` zero-extends, `MOVIS` sign-extends), never by a mode bit, so both implementations stay branch-free on extension.

**Opcode map**, 8-bit primary opcode:

| Range | Class |
| --- | --- |
| `$00–$0F` | System and flow: NOP, HALT, YIELD, BKPT, SYSCALL, RET, CALL, CALLR, JMP, JMPR |
| `$10–$2F` | ALU register-register, two- and three-address: ADD, SUB, AND, OR, XOR, NOT, NEG, MUL, MULH, MULHU, DIV, DIVU, REM, REMU |
| `$30–$3F` | Shifts and rotates: SHL, SHR, SAR, ROL, ROR, register and immediate count |
| `$40–$5F` | ALU register-immediate: ADDI, SUBI, ANDI, ORI, XORI, MOVIZ, MOVIS, MOVI32 |
| `$60–$7F` | Loads and stores: LDB, LDBU, LDH, LDHU, LDW, STB, STH, STW, base+disp16 |
| `$80–$9F` | Compare-and-branch: BEQ, BNE, BLT, BLTU, BGE, BGEU, BRA, SETcc |
| `$A0–$AF` | Stack and frame fast paths: PUSH, POP, FP-relative load/store, ENTER, LEAVE |
| `$B0–$BF` | Block operations: MEMCPY, MEMSET |
| `$C0–$EF` | Reserved for extensions, feature-bit gated |
| `$F0–$FE` | Reserved for the privileged extension (standalone mode) |
| `$FF` | Guaranteed illegal on both implementations — canary |

Reserved and illegal opcodes trap identically as `ILLEGAL_OP`.

## Operation semantics

These are the tables both implementations must match exactly. They follow eBPF and RISC-V where those are already precise and hardware-friendly, because the goal is boring semantics that a retargeted C compiler and a small FSM both like.

- **Arithmetic** wraps modulo 2³² in two's complement; no overflow trap. `ADD`, `SUB` and `MUL` are identical for signed and unsigned in the low word.
- **MULH / MULHU:** the high 32 bits of the signed or unsigned 64-bit product.
- **Shifts:** the shift amount is **masked to 5 bits** (`count & 31`) for every shift and rotate. This settles "shift count ≥ 32" identically on both sides. `SAR` is arithmetic, replicating the sign.
- **Loads:** `LDB` and `LDH` sign-extend; `LDBU` and `LDHU` zero-extend; `LDW` is full width.
- **Compares and branches:** signed (`BLT`, `BGE`) and unsigned (`BLTU`, `BGEU`) provided explicitly; no flags consulted.

**Division and remainder** are fully defined, with no trap — deliberately branchless and bit-identical:

| Case | DIV / DIVU | REM / REMU |
| --- | --- | --- |
| divisor = 0 | `0xFFFFFFFF` | dividend, unchanged |
| signed `INT_MIN / −1` | `INT_MIN` (`0x80000000`) | `0` |

These are the RISC-V conventions, chosen because they are what both an iterative divider FSM and the C reference implement most naturally. **This diverges from eBPF, which zeroes the destination on divide-by-zero.** The divergence is deliberate and must stay recorded here so no one "fixes" it later. Because division is defined, `DIV0` is reserved and unused in the trap causes.

**Block operations are restartable.** `MEMCPY` and `MEMSET` keep progress in registers (R0 = destination, R1 = source or value, R2 = remaining count), operate one element at a time from low to high, and decrement R2. Re-executing with the current register values resumes correctly. This is what makes both the ABORTB path and the softcore fault path precise.

**Trap causes:** `ILLEGAL_OP`, `ADDR_FAULT` (misalignment or non-zero pointer high byte), `PAGE_FAULT`, `PROT_FAULT`, `BKPT`, `SYSCALL`.

**Commit point.** An instruction commits register and PC writes only at its end: compute the effective address, perform the access, and only then write the destination register and advance the PC. On the 65816 this is nearly free because ABORTB already restarts the host instruction without modifying registers. On the softcore it is the real work — the FSM must not update the register file or PC until the memory stage succeeds.

## ABI

**C ABI, "ILP32-24":** `char` 1, `short` 2, `int` 4, `long` 4, `long long` 8, pointers 4 bytes stored with 24 bits effective, `maxalign` 4, `char` signed. Deliberately divergent from Calypsi's 16-bit `int`: the VM has 32-bit registers, so a 16-bit `int` would cause constant widen and narrow churn.

| Register | Role | Saver |
| --- | --- | --- |
| R0–R3 | Arguments and returns (R0:R1 for 64-bit) | Caller |
| R4–R7 | Temporaries | Caller |
| R8–R10 | Temporaries | Callee |
| R11–R12 | Code-generator temporaries, not allocatable | Caller |
| R13 | SP, full descending, 4-byte aligned | Callee |
| R14 | LR | Callee, via `ENTER` |
| R15 | FP | Callee |

R11 and R12 are reserved because NVM32 has no memory-to-memory operations, so the code generator needs scratch of its own (DN-SW-VMCC-001). Variadic arguments always go on the stack. Structs use natural alignment, members in declaration order, size rounded to 4.

**Stack frame:** `CALL` places the return address in LR; `ENTER n` pushes LR and the old FP and reserves n bytes; `LEAVE` and `RET` reverse it. Stack arguments are pushed right to left.

**Syscall ABI:** `SYSCALL` carries an 8-bit service number in unit0's low byte, with an optional 16-bit extended number in unit1. Arguments in R0–R5, result in R0 and R1, errors as a negative R0 in errno style — never a flag. `SYSCALL` halts the VM with cause `SYSCALL`, leaving the number and arguments visible in the state window.

**Marshalling onto the native kernel.** The handler (1) validates the service number against the table; (2) validates every pointer argument — high byte zero, inside the guest data or stack regions, correct permission, and not naming the reserved host region; no address translation is needed because the guest virtual space *is* the process virtual space; (3) converts 32-bit NVM32 arguments to the widths the Calypsi kernel entry expects, then issues `COP #sig`; (4) packs the result into R0 and R1 and resumes. This marshalling layer is the single place that knows both ABIs.

**Guest code never calls 65816 machine code directly.** Only this portable trap crosses the boundary, which is what lets the same binary run on the softcore. A controlled native extension mechanism is provided as **data**, not code: a per-process service table mapping small integers to kernel handlers. Because it is a number space resolved by the host, it is implementation-independent. Any "call native address" mechanism would be fatal on the softcore and is excluded.

## Executable format (NVX)

Header: magic `"NVX1"`, ISA major and minor, a **feature-bit vector** (bit 0 base, bit 1 MUL/DIV present, bit 2 block operations, bit 3 reserved for TBI-style tagging), 24-bit entry PC, section table for `.text`, `.rodata`, `.data` and `.bss`, requested stack size.

**Fixed-address linking, no PIC.** Every process owns its whole 24-bit space, so `.text` always starts at `$02_0000` and `.data` at `$10_0000`, the same in every process. Executables are linked there and never relocated at load. This replaces the PC-relative PIC strategy in Rev A, which was unworkable: a 16-bit displacement cannot reach the data region from the text region. Position independence is deferred to a future shared-library mechanism, if one is ever needed.

A consequence worth stating: `.text` stays shareable read-only across processes and across both executors, because it is identical, not because it is relocatable.

**Feature discovery.** The loader checks the header's feature bits against the current executor and refuses or down-selects. Version 1 mandates the same feature set on both implementations, so one binary always runs on both; the mechanism exists for later divergence, not for present-day optionality.

## Encoding candidates pending measurement

Four ideas raised after Rev A. **None is adopted; all are decided by the T3 histogram** (DN-SW-VMBENCH-001), which is the reason the freeze moved to T4.

The motivation is one number: in the `ADD rd,rs,rt` handler, roughly 55 of \~70 cycles are fixed overhead — 18 dispatch, 25 nibble extraction, 12 PC advance — and only \~20 are the actual addition. Useful work is 25–30% of the cycles. Anything that amortises those 55 cycles over more work wins, which is why historically interpreted VMs (Z-machine, Dis) are CISC while JIT-oriented ones are RISC. This project is the former case.

| Candidate | Estimated gain | Risk | Notes |
| --- | --- | --- | --- |
| Fixed-form memory operands | 30–40% | Medium | The largest prize |
| Two-address forms (`ADD rd,rs`) | 15–20% | **None** | Already reserved in `$10–$2F` |
| `WITH` / RP prefix register | 5–10% | Medium | Adds architectural state |
| Register windows | 5–15% | High | Conflicts with 16 visible registers |

**Fixed-form memory operands.** What kills CISC interpreters is mode bytes: per-operand mode specifiers need a dispatch per operand. The winning shape is fixed-form CISC — the opcode still determines length, format and every field position, but the instruction does the work of three. `a[i] = b[i] + c` goes from \~270 cycles in three instructions to \~165 in one. Costs: the restart contract must be specified per opcode when an instruction has several faulting accesses, and it pushes the softcore toward microcode, against the hardwired-control preference. Note also that vbcc's intermediate code is quadruples whose operands may be memory directly, so a memory-to-memory VM maps onto it almost one to one — this candidate is *more* toolchain-friendly, not less. Rev A rejected Inferno Dis for having "general memory operands"; that reasoning holds for general operands with mode bytes and does not hold for fixed-form ones. The middle ground was never evaluated.

**`WITH` / RP.** A prefix register naming an implicit destination: `WITH r4` then `ADD.RP r5`, `SHL.RP #2`. Explicit `WITH` is much better than "last register accessed", which is unanalysable and diverges silently between implementations. With `WITH` the value is readable by working backwards, the backend generates it as a local peephole over runs sharing a destination, and RP updates only in its own instruction, so restart stays simple. Rules that must be fixed if adopted: **RP is undefined after any call and at every branch target** (caller-saved, runs never cross basic blocks). It is a prefix register in the Thumb-2 family, not an exotic idea. Decision criterion: runs of three or more instructions sharing a destination must be common enough; two-instruction runs are cancelled out by the `WITH` itself.

**Register windows.** SPARC-style sliding windows suit this machine better than silicon — sliding is just moving D in the interpreter, so non-call instructions pay nothing, and depth is free in EBR since a 32-bit-wide block needs 2 EBR whether it holds 16 registers or 256. Backing store fits in bank `$00` (\~500 frames of 64 bytes per process), and overflow can be caught by a guard page through ABORTB rather than an explicit check per `CALL`. **But SPARC has 32 visible registers and NVM32 has 16.** Splitting 16 into globals, in, local and out leaves four of each, and vbcc would spill constantly at \~100 cycles a spill. Raising the count breaks the 4-bit nibble, which is priority #1. Worth stealing regardless: SPARC's overlap trick where the caller's SP becomes the callee's FP, and keeping the return address inside the window so `CALL` need not save LR.

**Stack machine — rejected outright, not pending.** A stack VM does *less* work per dispatch, which is the wrong direction: `a + b` is one dispatch in registers and four on a stack, so \~220 cycles of overhead instead of \~55. Published comparisons of the two models on the same VM put the register form at about half the executed instructions, \~25% more bytes, and \~30% less time — and dispatch weighs far more here than on a modern host. C does not favour a stack either: real C code is dominated by local-variable access, and locals in a stack VM are accessed through indexed load-local/store-local, which is a register file reinvented at one dispatch per access. The empirical answer already exists: Google moved from a stack to registers when building Dalvik to interpret Java on constrained devices. The historical argument for stack VMs — no register allocator needed in the compiler — does not apply, because this project reuses a mature compiler. What is worth taking from stack machines is the frame idioms, which are already in the opcode map.

## Abandonment conditions and open questions

- If measured interpreter throughput is below \~100,000 VM-instructions per second on representative code, after the adopted optimizations and the Helium multiply/divide helper, reconsider either load-time pre-decode or switching the native format to RV32IM — buying a free toolchain and existing softcores at the cost of worse 65816 decode.
- If no encoding candidate clears its T3 criterion, freeze the ISA as specified here and stop revisiting the encoding.
- If FPGA-C and noVa128 are both abandoned, the one-binary argument disappears and the VM should be re-justified on its own merits (portability, isolation, development speed) or dropped.
- If a hot opcode is found to cost more than 150 interpreter cycles, add a superinstruction before changing the encoding.

Open questions:

- [ ] Does floating point get opcodes in the reserved space, or stay soft float? Blocks the T4 freeze.
- [ ] Which encoding candidates are adopted? Decided by the T3 histogram.
- [ ] Are the interpreter's instruction-boundary semantics for `MEMCPY` across banks identical to the softcore's flat 24-bit datapath, in write order and restart behaviour? This is the one instruction whose implementation differs most between targets; verify explicitly in the conformance suite.
- [ ] Instruction-cache coherence: if the softcore caches guest `.text`, the cache must be flushed on the relevant TLB and ASID events. Specify in DN-HW-VMCORE-001 before implementation.
- [ ] Does the kernel stay 65816-native, move to NVM32, or split? Argon makes all three possible; the coprocessor model does not.
