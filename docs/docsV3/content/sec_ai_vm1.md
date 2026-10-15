# NVM32 — the instruction set
> one binary, three executors · why a virtual machine at all · the ISA that follows

NVM32 is a 32-bit register virtual machine with a 24-bit address space, designed so that **the same binary runs interpreted on the 8 MHz W65C816S and natively on a softcore**. It is the application development model the machine grows into, not the one it starts with: the kernel stays native 65816 code compiled by Calypsi ([O.2](sec_ai_o#o2)), and the VM arrives once there is a kernel worth targeting. This sheet specifies the architecture. The interpreter is [sheet VM2](sec_ai_vm2), the coprocessor [sheet VM3](sec_ai_vm3), the toolchain and the measurement harness [sheet VM4](sec_ai_vm4), and the noVa128 core that executes it in silicon [sheet NV1](sec_ai_nv1).

- VM1.1 — **The argument is one binary across three executors, and it is the only argument that survives scrutiny.** An interpreter on the physical 65816, a bytecode core on Argon, and a tri-mode core in noVa128 all run the *same* NVX image, unrecompiled, with a process migratable between them mid-execution. Everything else the VM offers — isolation, development speed, a C compiler with a modern ABI — is available by other means; this is not.
  NOTE: Which is also the honest weakness. **If Argon is never populated and noVa128 never happens, the headline argument disappears** and the VM has to be re-justified on portability and development speed alone, or dropped ([VM1.51](sec_ai_vm1#vm151), → [Q13](sec_ai_q#q13)).
- VM1.2 — **Status: deferred, with a written reopening condition — the kernel executes its first user process, with page faults and syscalls working.** noVa64 continues on the original plan. This is not a shelf: it is a date expressed as a milestone, and the notes exist so that reopening is implementation work rather than redesign.
- VM1.3 — **The dependency runs one way, which is what decides the order.** The VM needs a process model, page tables, a loader, a scheduler and a service table from the kernel ([sheet N](sec_ai_n)); the kernel needs nothing from the VM. Building the VM first means guessing interfaces that do not exist yet.
  NOTE: The second reason to defer is that the one-binary argument depends on Argon, which is itself deferred ([B.4](sec_ai_b#b4)). Two deferred things do not make one urgent one.
- VM1.4 — **Stage 1 is PC-side software and competes for no hardware time**, which is why it starts the moment the condition is met: a C11 reference interpreter inside the emulator ([sheet EM1](sec_ai_em1)), which becomes the golden model for every executor that follows. It is the cheapest step in the programme and it unblocks all the others.
- VM1.5 — **Seven things are settled now, because they cost nothing today and are expensive later.** Four come from the VM itself; the last three come from [sheet NV1](sec_ai_nv1) and are what keeps the noVa64 kernel binary-compatible with noVa128.

| Decision | Owner | Why now |
|---|---|---|
| Kernel services reached through a numbered service table, not `COP` signatures scattered through the code | Kernel | The VM must never call native code directly, and it is better hygiene for the kernel regardless ([D90](sec_ai_q#d90)) |
| Banks `$00`–`$01` reserved in the process memory map from the first loader, plus one shared read-only page for the system information block | Kernel, loader | Reclaiming that region later is a migration, not an edit ([D91](sec_ai_q#d91)) |
| Memory-mapped registers idempotent on read | Neon, Helium | Required by Argon 2's 65816 modes; discovering it late means redesigning the command port ([NV1.26](sec_ai_nv1#nv126)) |
| The emulator can host a reference interpreter, and treats `WDM` as a two-byte no-op | [sheet EM1](sec_ai_em1) | Nothing to write yet, and the `WDM` rule is what keeps the CPU probe honest ([VM2.24](sec_ai_vm2#vm224)) |
| No cycle counting for time; the fixed frequency reference is the only clock | Kernel, drivers | Already implied by the PHI2 stall ([L.15](sec_ai_l#l15)); stating it makes it binding |
| No self-modifying code in project code | Kernel, BIOS | Avoids depending on Argon 2's instruction-cache snooping ([NV1.25](sec_ai_nv1#nv125)) |
| Software-visible Helium and Neon register map kept stable, versioned through the system information block if it ever changes | Helium, Neon, BIOS | noVa128's 16-bit inter-FPGA path must not leak into the programmer's model |

- VM1.6 — **Accepted risk, stated rather than mitigated: the kernel ABI is shaped around Calypsi's 16-bit `int`.** NVM32 is ILP32. The two ABIs meet at exactly one place, the syscall marshalling layer ([VM1.39](sec_ai_vm1#vm139)), and compiled objects never cross that boundary ([O.2](sec_ai_o#o2)).
- VM1.7 — **Guest C is compiled by the retargeted NVM32 compiler, never by Calypsi**, and Calypsi remains the kernel compiler. The two toolchains coexist without meeting ([sheet VM4](sec_ai_vm4)).
- VM1.8 — **Four priorities, in this order, and they decide every argument in this sheet.** Interpreter speed on the 8 MHz 65816 · code density · simple hardware on a small FPGA · toolchain friendliness. Non-negotiable across all four: **bit-identical semantics** between every executor, down to shift counts ≥ 32, division by zero, `INT_MIN / −1`, illegal opcodes and fault behaviour.
  NOTE: The recurring conflict is **decode locality against hardware regularity**. The 65816 wants one-byte opcodes, register indices pre-scaled for direct-page indexing, and immediates that are whole little-endian words at unit boundaries. Hardware wants fixed field positions, few formats, and length known from the first unit. Where the two genuinely disagree, the resolution is a clean 32-bit *architecture* implemented as two 16-bit operations on the 65816 — the cost lands on the interpreter, not on the ISA.

## The programmer's model — sixteen registers, no flags, and nothing else.

- VM1.9 — **Sixteen general-purpose 32-bit registers, R0–R15.** Eight is too few for a C ABI without spills, and a spill is ~100 interpreter cycles. Sixteen fits a 4-bit nibble field and costs 64 bytes of direct page in the interpreter and a small EBR register file in hardware. **Thirty-two would need 5-bit fields, which breaks clean nibble decode — the single most load-bearing constraint in this encoding.**
  NOTE: The direct ancestor is SWEET16, whose sixteen 16-bit registers are the first 32 bytes of main memory. The idea that a VM's register file belongs in the fastest addressing mode the host has is forty years old and still correct here.
- VM1.10 — **No hardwired zero register.** It aids RISC hardware and wastes a register in the interpreter, where it also complicates the sixteen-registers-in-64-bytes mapping. `MOVIZ rd,#0` covers the cases that matter.
- VM1.11 — **Register roles are ABI conventions, not hardware.** R13 = SP, R14 = LR, R15 = FP, R0–R3 = arguments and returns. Both implementations treat all sixteen uniformly, so a change of convention is a compiler change and never a core change.
- VM1.12 — **PC: 24-bit, byte-addressed, instructions 16-bit aligned.** It always points into the 24-bit guest virtual space, which is the process's own space and not a window onto it.
- VM1.13 — **Flagless.** Comparisons fold into branches (`Bcc rs,rt,disp`) or into `SETcc rd,rs,rt`. There is no architectural status word. A flags register would force the interpreter to compute and store N/Z/V/C after every ALU operation — dead work most of the time — and would force flag-forwarding hazards on the core. The two targets agree for once, and the design takes the agreement.
- VM1.14 — **Architectural state is {R0–R15, PC} and nothing else.** A single non-architectural trap-cause latch exists, used only while halted for a fault or a syscall, and it is not visible to guest code.
  NOTE: **This is the invariant the whole design rests on.** It is what makes a process migratable mid-execution between interpreter and core ([VM3.16](sec_ai_vm3#vm316)), and what makes preemption cheap ([VM2.32](sec_ai_vm2#vm232)). Any proposal that adds architectural state — a flags register, a window pointer, an implicit accumulator — is paying against it and **must say so explicitly** ([VM1.44](sec_ai_vm1#vm144)).

## The memory model — one space per process, and pointers that are checked.

- VM1.15 — **Little-endian**, matching the 65816, so that no executor pays a swap.
- VM1.16 — **Pointers are 32-bit values; only the low 24 bits are address, and bits 31–24 must be zero.** Any load, store or branch whose effective pointer has non-zero high bits raises `ADDR_FAULT`, enforced identically on both implementations. The high byte is **checked, not ignored**.
  NOTE: This is the 68000 lesson applied on purpose. Classic Mac OS and AmigaOS stored flags in the high byte of pointers because the hardware ignored it, and the resulting "32-bit dirty" software broke when full 32-bit addressing arrived. ARM's Top-Byte-Ignore shows tagging can be clean when it is defined from day one, so TBI-style semantics stay **reserved behind a feature bit** — but the default is check and trap.
- VM1.17 — **The guest memory map, per-process in a 24-bit virtual space**, and it is deliberately the same shape as the native one ([L.10](sec_ai_l#l10)):

| Range | Size | Use |
|---|---|---|
| `$00_0000`–`$00_FFFF` | 64 KB | Reserved host region: 65816 direct pages, 65816 stack, interpreter scratch, the VM state window, the trap mailbox, the system information block. Guest code may not touch it |
| `$01_0000`–`$01_FFFF` | 64 KB | Interpreter code and dispatch table, read-only, shared, mapped identically in every process |
| `$02_0000`–`$0F_FFFF` | ~896 KB | Guest code, read-execute |
| `$10_0000`–`$DF_FFFF` | ~13 MB | Guest data, BSS and heap, read-write |
| `$E0_0000`–`$EF_FFFF` | 1 MB | Guest stack, growing down from `$EF_FFFF`, guard page at the bottom |
| `$F0_0000`–`$FF_FFFF` | 1 MB | The kernel, growing down from `$FD`, then the VRAM window and privileged I/O — never addressable by a guest ([D102](sec_ai_q#d102)) |

- VM1.18 — **Two banks are reserved and the loader enforces it**, refusing to map guest segments over `$00`–`$01` while the MMU marks them no-access for guest pointers ([L.5](sec_ai_l#l5)). On a core executing NVM32 natively those banks are never generated as guest effective addresses at all — the region stays reserved **only so that one binary's memory map is valid on both executors**.
  NOTE: This is the item that has to reach the loader before the loader is written ([N.6](sec_ai_n#n6), [D91](sec_ai_q#d91)). It is cheap now and a migration later, which is the whole reason it is in [VM1.5](sec_ai_vm1#vm15)'s table.
- VM1.19 — **Natural alignment is required for 16- and 32-bit accesses; misalignment raises `ADDR_FAULT`.** Unaligned access costs extra cycles and bank-carry handling on the 65816 and forces byte assembly in hardware. Aligned-only keeps both fast *and* identical, which is the second requirement and the harder one.
- VM1.20 — **Faults are two, and both are restartable**: an unmapped page raises `PAGE_FAULT`, a permission or copy-on-write violation raises `PROT_FAULT`. They map onto the machine's existing fault taxonomy ([L.4](sec_ai_l#l4)) rather than inventing a second one.
- VM1.21 — **The guest virtual space *is* the process virtual space**, which is what removes an entire layer: no address translation happens at the syscall boundary, only validation ([VM1.39](sec_ai_vm1#vm139)). A VM whose guest space were a window into the process space would need a translation on every pointer argument, and would lose the migration property with it.

## Instruction encoding — Dalvik-shaped, because both targets want the same thing.

- VM1.22 — **Every instruction is one, two or three 16-bit little-endian code units, and the opcode byte alone determines the length** — through a 256-entry table in the interpreter, or a small combinational map in hardware. There is no length prefix and no multi-way length logic.
- VM1.23 — **This is the Dalvik pattern, and it is chosen because it suits both targets rather than because it is fashionable.** On the 65816 a single 16-bit fetch yields an 8-bit opcode plus two 4-bit register fields — exactly what the dispatcher needs, in one load. In hardware the first byte gives length and format with no decode tree.
- VM1.24 — **Seven formats, and no more:**

| Format | Layout (unit0 · unit1 · unit2) | Example |
|---|---|---|
| F1 | `op:8 \| 00:8` | `HALT`, `RET`, `NOP`, `YIELD` |
| F2 | `op:8 \| rd:4 rs:4` | two-address `ADD rd,rs`, `MOV`, `NEG` |
| F3 | `op:8 \| rd:4 rs:4` · `rt:4 z:4 :8` | three-address `ADD rd,rs,rt` |
| F4 | `op:8 \| rd:4 z:4` · `imm:16` | `MOVIZ` / `MOVIS rd,#imm16` |
| F5 | `op:8 \| rd:4 z:4` · `imm:32` | `MOVI32 rd,#imm32` |
| F6 | `op:8 \| rd:4 rs:4` · `disp:16` | load and store `[rs+disp]`, PC-relative |
| F7 | `op:8 \| rs:4 rt:4` · `disp:16` | compare-and-branch `Bcc rs,rt,disp` |

- VM1.25 — **Three rules exist for the 65816 and must not be relaxed.** Register fields are **always** the two nibbles of unit0's second byte, so one load plus a mask extracts them, pre-doubled to index 4-byte direct-page slots. Immediates and displacements are **always** whole 16- or 32-bit little-endian words at a unit boundary, never bitfields spanning bytes. And **sign extension is decided by the opcode** — `MOVIZ` zero-extends, `MOVIS` sign-extends — never by a mode bit, so both implementations stay branch-free on extension.
  NOTE: The second rule is why RV32IM was rejected as the native format. Reassembling an immediate scattered across bitfields is nearly free in silicon and expensive on a 65816, and this machine's binding target is the 65816 ([VM1.8](sec_ai_vm1#vm18)). RV32IM survives as the fallback if the interpreter misses its throughput floor ([VM1.50](sec_ai_vm1#vm150)).
- VM1.26 — **The opcode map, 8-bit primary opcode:**

| Range | Class |
|---|---|
| `$00–$0F` | System and flow: `NOP`, `HALT`, `YIELD`, `BKPT`, `SYSCALL`, `RET`, `CALL`, `CALLR`, `JMP`, `JMPR` |
| `$10–$2F` | ALU register-register, two- and three-address: `ADD`, `SUB`, `AND`, `OR`, `XOR`, `NOT`, `NEG`, `MUL`, `MULH`, `MULHU`, `DIV`, `DIVU`, `REM`, `REMU` |
| `$30–$3F` | Shifts and rotates: `SHL`, `SHR`, `SAR`, `ROL`, `ROR`, register and immediate count |
| `$40–$5F` | ALU register-immediate: `ADDI`, `SUBI`, `ANDI`, `ORI`, `XORI`, `MOVIZ`, `MOVIS`, `MOVI32` |
| `$60–$7F` | Loads and stores: `LDB`, `LDBU`, `LDH`, `LDHU`, `LDW`, `STB`, `STH`, `STW`, base + disp16 |
| `$80–$9F` | Compare-and-branch: `BEQ`, `BNE`, `BLT`, `BLTU`, `BGE`, `BGEU`, `BRA`, `SETcc` |
| `$A0–$AF` | Stack and frame fast paths: `PUSH`, `POP`, FP-relative load and store, `ENTER`, `LEAVE` |
| `$B0–$BF` | Block operations: `MEMCPY`, `MEMSET` |
| `$C0–$EF` | Reserved for extensions, feature-bit gated |
| `$F0–$FE` | Reserved for the privileged extension — standalone mode ([VM3.23](sec_ai_vm3#vm323)) |
| `$FF` | Guaranteed illegal on every implementation — the canary |

- VM1.27 — **Reserved and illegal opcodes trap identically as `ILLEGAL_OP`**, which is what makes the canary worth having: a runaway PC into zeroed or unwritten memory is caught on both executors at the same instruction.
- VM1.28 — **The `$A0–$AF` block is the one concession to the interpreter's cost model, and it is made before the measurements rather than after.** `ENTER`, `LEAVE` and FP-relative load and store are superinstructions for patterns a C compiler emits constantly, and each one removes a whole dispatch — ~55 cycles — from a function's prologue or epilogue ([VM1.45](sec_ai_vm1#vm145)).

## Operation semantics — the tables every executor must match exactly.

These follow eBPF and RISC-V wherever those are already precise and hardware-friendly. The goal is boring semantics that a retargeted C compiler and a small FSM both like.

- VM1.29 — **Arithmetic wraps modulo 2³² in two's complement, with no overflow trap.** `ADD`, `SUB` and `MUL` are identical for signed and unsigned in the low word. `MULH` and `MULHU` give the high 32 bits of the signed or unsigned 64-bit product.
- VM1.30 — **The shift amount is masked to 5 bits — `count & 31` — for every shift and rotate**, which settles "shift count ≥ 32" identically on both sides rather than leaving it to whichever host behaviour leaks through. `SAR` is arithmetic and replicates the sign.
- VM1.31 — **Loads state their extension in the mnemonic**: `LDB` and `LDH` sign-extend, `LDBU` and `LDHU` zero-extend, `LDW` is full width. Compares and branches provide signed (`BLT`, `BGE`) and unsigned (`BLTU`, `BGEU`) forms explicitly, because there are no flags to consult.
- VM1.32 — **Division and remainder are fully defined, never trap, and are deliberately branchless:**

| Case | `DIV` / `DIVU` | `REM` / `REMU` |
|---|---|---|
| divisor = 0 | `$FFFFFFFF` | the dividend, unchanged |
| signed `INT_MIN / −1` | `INT_MIN` (`$80000000`) | `0` |

  NOTE: **These are the RISC-V conventions, and the divergence from eBPF is deliberate and recorded so that nobody "fixes" it later** ([D93](sec_ai_q#d93)). eBPF zeroes the destination on divide by zero; this does not. The convention is chosen because it is what an iterative divider FSM and a C reference implement most naturally — the same reason twice. Because division is total, `DIV0` is reserved and unused in the trap causes.
- VM1.33 — **Block operations are restartable, and the restart state lives in registers.** `MEMCPY` and `MEMSET` keep progress in R0 (destination), R1 (source or value) and R2 (remaining count), operate one element at a time from low to high, and decrement R2. Re-executing the instruction with the current register values resumes it correctly.
  NOTE: This is what makes both the `ABORTB` path on the 65816 and the fault path on the core precise without either implementation carrying hidden state. It is also why the toolchain asks for explicit register operands rather than the hard-wired R0–R2 ([VM4.28](sec_ai_vm4#vm428)) — the restart state lives in whichever registers the instruction names, so the property is unaffected.
- VM1.34 — **An instruction commits register and PC writes only at its end.** Compute the effective address, perform the access, and only then write the destination register and advance the PC. Trap causes are `ILLEGAL_OP`, `ADDR_FAULT`, `PAGE_FAULT`, `PROT_FAULT`, `BKPT` and `SYSCALL`.
  NOTE: **The commit rule costs the two implementations very differently, and that asymmetry is worth knowing.** On the 65816 it is nearly free, because `ABORTB` already restarts the host instruction without modifying registers ([E.16](sec_ai_e#e16)) — the discipline is just handler-writing order. On the core it is the real work: the FSM must not update the register file or the PC until the memory stage succeeds ([VM3.5](sec_ai_vm3#vm35)).

## The ABI — ILP32-24, and one place where two ABIs meet.

- VM1.35 — **"ILP32-24":** `char` 1, `short` 2, `int` 4, `long` 4, `long long` 8, pointers 4 bytes stored with 24 bits effective, `maxalign` 4, `char` signed. **Deliberately divergent from Calypsi's 16-bit `int`**: the VM has 32-bit registers, so a 16-bit `int` would cause constant widen-and-narrow churn on every expression.
- VM1.36 — **Register roles:**

| Register | Role | Saver |
|---|---|---|
| R0–R3 | Arguments and returns (R0:R1 for 64-bit) | Caller |
| R4–R7 | Temporaries | Caller |
| R8–R10 | Temporaries | Callee |
| R11–R12 | Code-generator temporaries, not allocatable | Caller |
| R13 | SP, full descending, 4-byte aligned | Callee |
| R14 | LR | Callee, through `ENTER` |
| R15 | FP | Callee |

- VM1.37 — **R11 and R12 are reserved because NVM32 has no memory-to-memory operations**, so the code generator needs scratch of its own ([VM4.9](sec_ai_vm4#vm49)). Variadic arguments always go on the stack, which makes `stdarg.h` plain pointer arithmetic. Structs use natural alignment, members in declaration order, size rounded to 4.
- VM1.38 — **The stack frame is three instructions.** `CALL` places the return address in LR; `ENTER n` pushes LR and the old FP and reserves n bytes; `LEAVE` and `RET` reverse it. Stack arguments are pushed right to left.
- VM1.39 — **`SYSCALL` carries an 8-bit service number in unit0's low byte**, with an optional 16-bit extended number in unit1. Arguments in R0–R5, result in R0 and R1, errors as a negative R0 in errno style — **never as a flag**, since there are none. `SYSCALL` halts the VM with cause `SYSCALL`, leaving the number and the arguments visible in the state window.
  NOTE: **The marshalling handler is four steps and it is the single place in the system that knows both ABIs.** It validates the service number against the table; validates every pointer argument — high byte zero, inside the guest data or stack region, correct permission, not naming the reserved host region; converts the 32-bit NVM32 arguments to the widths the Calypsi kernel entry expects, then issues `COP #sig` against the existing native syscall entry ([J.3](sec_ai_j#j3), [M.1](sec_ai_m#m1)); and packs the result into R0 and R1. **No address translation is needed, because the guest virtual space is the process virtual space** ([VM1.21](sec_ai_vm1#vm121)).
- VM1.40 — **Guest code never calls 65816 machine code directly, and this is a load-bearing prohibition rather than a style rule.** Only the portable `SYSCALL` trap crosses the boundary, which is exactly what lets the same binary run on a core with no 65816 in it. Native extension is offered as **data** — a per-process service table mapping small integers to kernel handlers — and never as a callable address. **Any "call native address" mechanism is fatal on the softcore and is excluded by construction** ([D90](sec_ai_q#d90)).

## The executable format — NVX, linked at fixed addresses.

- VM1.41 — **The header carries magic `"NVX1"`, ISA major and minor, a feature-bit vector, a 24-bit entry PC, a section table for `.text`, `.rodata`, `.data` and `.bss`, and a requested stack size.** Feature bits: 0 base, 1 MUL and DIV present, 2 block operations, 3 reserved for TBI-style tagging.
- VM1.42 — **Fixed-address linking, and no position-independent code.** Every process owns its whole 24-bit space, so `.text` always starts at `$02_0000` and `.data` at `$10_0000`, **the same addresses in every process**. Executables are linked there and never relocated at load — which is the same conclusion the native toolchain already reached from the same premise ([O.5](sec_ai_o#o5), [N.4](sec_ai_n#n4)).
  NOTE: **This replaces the PC-relative PIC strategy of the first draft, which was unworkable and is recorded rather than quietly dropped.** It relied on a global pointer the ABI table never reserves, and a 16-bit displacement cannot reach the data region from the text region in this memory map. Position independence is deferred to a future shared-library mechanism, if one is ever wanted.
- VM1.43 — **A consequence worth stating: `.text` stays shareable read-only across processes and across executors, because it is identical — not because it is relocatable.** The loader checks the header's feature bits against the current executor and refuses or down-selects; version 1 mandates the same feature set everywhere, so one binary always runs on all three. The mechanism exists for later divergence, not for present-day optionality.

## Encoding candidates pending measurement — four ideas, none adopted.

- VM1.44 — **None of the four is adopted, and all four are decided by the T3 histogram** ([VM4.34](sec_ai_vm4#vm434)). This is why the ISA freeze sits at gate T4 and not at the start ([D94](sec_ai_q#d94)).
- VM1.45 — **The motivation is one number.** In the `ADD rd,rs,rt` handler, roughly 55 of ~70 cycles are fixed overhead — 18 dispatch, 25 nibble extraction, 12 PC advance — and only ~20 are the addition itself. **Useful work is 25–30% of the cycles.** Anything that amortises those 55 cycles over more work wins.
  NOTE: Which is why historically interpreted VMs are CISC and JIT-oriented ones are RISC. This project is firmly the former case, and the encoding should be allowed to follow the evidence there rather than inherit a RISC aesthetic from machines with different economics.

| Candidate | Estimated gain | Risk | Standing |
|---|---|---|---|
| Fixed-form memory operands | 30–40% | Medium | The largest prize |
| Two-address forms (`ADD rd,rs`) | 15–20% | **None** | Already reserved in `$10–$2F` |
| `WITH` / RP prefix register | 5–10% | Medium | Adds architectural state |
| Register windows | 5–15% | High | Conflicts with sixteen visible registers |

- VM1.46 — **Fixed-form memory operands are the largest prize and the one most often mis-rejected.** What kills CISC interpreters is *mode bytes*: a per-operand mode specifier needs a dispatch per operand. The winning shape is **fixed-form** CISC, where the opcode still determines length, format and every field position, but the instruction does the work of three. `a[i] = b[i] + c` goes from ~270 cycles in three instructions to ~165 in one.
  NOTE: **The first draft rejected Inferno's Dis for having "general memory operands", and that reasoning holds for general operands with mode bytes and does not hold for fixed-form ones. The middle ground was never evaluated.** Note also that vbcc's intermediate code is quadruples whose operands may be memory directly, so a memory-to-memory VM maps onto it almost one to one — this candidate is *more* toolchain-friendly, not less ([VM4.8](sec_ai_vm4#vm48)).
  NOTE: Two costs, both real. The restart contract must be specified per opcode once an instruction has several faulting accesses ([VM1.34](sec_ai_vm1#vm134)), and it pushes the core towards microcode, against its hardwired-control preference ([VM3.4](sec_ai_vm3#vm34)).
- VM1.47 — **`WITH` / RP is a prefix register naming an implicit destination** — `WITH r4`, then `ADD.RP r5`, `SHL.RP #2`. **Explicit `WITH` is much better than "last register accessed"**, which is unanalysable and diverges silently between implementations; with `WITH` the value is readable by working backwards, the backend generates it as a local peephole over runs sharing a destination, and RP updates only in its own instruction, so restart stays simple.
  NOTE: Two rules must be fixed if it is adopted: **RP is undefined after any call and at every branch target**. Runs never cross basic blocks, and RP is caller-saved. The decision criterion is that runs of three or more instructions sharing a destination be common enough — two-instruction runs are cancelled out by the `WITH` itself.
- VM1.48 — **Register windows suit this machine better than they suit silicon, and still lose.** Sliding is just moving D in the interpreter, so non-call instructions pay nothing; depth is nearly free in EBR, since a 32-bit-wide block needs 2 EBR whether it holds 16 registers or 256; the backing store fits in bank `$00` at ~500 frames per process; and overflow can be caught by a guard page through `ABORTB` rather than an explicit check per `CALL`. **But SPARC has 32 visible registers and NVM32 has 16.** Splitting 16 into globals, in, local and out leaves four of each, and the compiler would spill constantly.
  NOTE: Raising the register count breaks the 4-bit nibble, which is priority #1 ([VM1.9](sec_ai_vm1#vm19)). **Two things are worth stealing regardless of the verdict**: SPARC's overlap trick, where the caller's SP becomes the callee's FP, and keeping the return address inside the window so `CALL` need not save LR.
- VM1.49 — **A stack machine is rejected outright and is not a pending option.** A stack VM does *less* work per dispatch, which is the wrong direction here: `a + b` is one dispatch in registers and four on a stack — ~220 cycles of overhead instead of ~55. Published comparisons of the two models on the same VM put the register form at about half the executed instructions, ~25% more bytes and ~30% less time, and **dispatch weighs far more here than on a modern host**, so the gap widens rather than narrows.
  NOTE: C does not favour a stack either. Real C code is dominated by local-variable access, and locals in a stack VM are reached through indexed load-local and store-local — **a register file reinvented at one dispatch per access**. The empirical answer already exists: Google moved from a stack to registers when building Dalvik to interpret Java on constrained devices. And the historical argument for stack VMs, that no register allocator is needed, does not apply to a project that reuses a mature compiler ([VM4.3](sec_ai_vm4#vm43)). What is worth taking from stack machines is the frame idioms, and those are already in the opcode map ([VM1.28](sec_ai_vm1#vm128)).

## Abandonment conditions — written in advance, actionable without reopening.

- VM1.50 — **If measured interpreter throughput is below ~100,000 VM-instructions per second on representative code**, after the adopted optimizations and Helium's multiply and divide helper, reconsider either load-time pre-decode or **switching the native format to RV32IM** — which buys a free toolchain and existing softcores at the cost of worse 65816 decode.
- VM1.51 — **If Argon and noVa128 are both abandoned, the one-binary argument disappears** and the VM must be re-justified on its own merits — portability, isolation, development speed — or dropped ([VM1.1](sec_ai_vm1#vm11)).
- VM1.52 — **If no encoding candidate clears its T3 criterion, freeze the ISA as specified here and stop revisiting the encoding.** A candidate that adds architectural state clears a higher bar than one that does not, because {R0–R15, PC} is what migration and cheap preemption are built on.
- VM1.53 — **If a hot opcode is found to cost more than 150 interpreter cycles, add a superinstruction before changing the encoding.** The cheap fix is tried first, every time.

Open questions live in [sheet Q](sec_ai_q): floating point as opcodes or soft float (→ [Q162](sec_ai_q#q162)), which encoding candidates are adopted (→ [Q166](sec_ai_q#q166)), whether `MEMCPY` behaves identically across a bank boundary on both executors (→ [Q160](sec_ai_q#q160)), and whether the kernel itself stays 65816-native, moves to NVM32 or splits (→ [Q164](sec_ai_q#q164)).
