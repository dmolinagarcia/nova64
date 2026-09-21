# noVa64 Portable Bytecode VM — ISA, 65816 Interpreter, and FPGA Softcore
### DN-SW-VMISA-001 / DN-SW-VMINTERP-001 / DN-HW-VMCORE-001 — Rev A (draft for discussion)
*Revision history: Rev A — initial draft, 2026-09-17. [placeholder for review cycle: reviewers, dates, decisions]*

## TL;DR
- **Adopt a custom register VM ("NVM32") with 16-bit code units in the Dalvik style: a single 16-bit fetch on the 65816 yields opcode + register nibbles, giving the fastest interpreter dispatch while remaining trivially decodable in hardware.** The VM has 16 general 32-bit registers, a 24-bit PC, flagless compare-and-branch, and a strict little-endian, **32-bit-clean** pointer model where the upper 8 bits of every pointer must be zero and are *checked*, not ignored.
- **The 65816 runs it as a re-entrant, read-only, native-mode interpreter using `JMP (addr,X)` table dispatch with VM registers in the direct page**; estimated throughput ≈ 150,000–400,000 VM-instructions/second at 8 MHz before cache-miss PHI2 stalls — i.e. ~20–50× slower than hand-written native code, in line with Wozniak's SWEET16, which "runs at about one-tenth the speed of the equivalent native 6502 code" for a *16-bit* VM, scaled up for 32-bit width and richer decode.
- **The same binary runs natively on a multi-cycle FSM softcore that fits an iCE40 HX8K (7,680 logic cells, 32 EBR, per Lattice DS1040)** — budget ~2,500–3,500 LUT4, leaving room for a cache and bus-mastering — for an estimated 20–40× speedup over the interpreter, **bounded by memory bandwidth through Helium, not core Fmax** (~40–50 MHz is achievable; PicoRV32 reaches 65.22 MHz on HX8K).

## Key Findings

1. **A 16-bit-code-unit, register-based encoding is the sweet spot for *both* targets.** Dalvik proves the pattern: its documentation notes register-based instructions "reduce memory and dispatch overhead," fixed-width instructions "simplif[y] parsing," and "the 16-bit code units minimize reads." On the 65816 a single 16-bit `LDA (PC),Y` fetch yields an 8-bit opcode plus two 4-bit register fields — precisely the fields the dispatcher needs — while in hardware the first byte trivially determines instruction length.

2. **Flagless compare-and-branch beats a flags register on both targets, and they do not conflict here.** A program-status word forces the interpreter to compute and store N/Z/V/C after every ALU op (dead work most of the time) and forces the softcore to carry flag-forwarding hazards. Fused compare-and-branch (as in eBPF and RISC-V-style `BLT rs,rt,off`) is cheap to interpret and simple in hardware.

3. **The 68000 top-byte lesson is decisive: NVM32 is 32-bit clean.** Classic Mac OS/AmigaOS stored flags in the high byte of pointers because the 68000's "address bus is 24 bits wide" and "the leftmost byte is ignored"; Apple's own guidance described the result as a "24-bit world, where the hardware ignores the high byte of all memory addresses," which created the "32-bit dirty" software that broke when full 32-bit addressing arrived. ARM's later Top-Byte-Ignore (TBI, ARMv8) shows tagging *can* be clean if defined from day one — but the safe, future-proof choice for a solo project is to **require the top 8 bits to be zero and trap on violation**, reserving TBI-style semantics behind a feature bit.

4. **The softcore is memory-bound, not compute-bound — as the project already concluded.** PicoRV32 on iCE40 HX8K uses **1,982 of 7,680 logic cells** with a placed timing result of **15.33 ns = 65.22 MHz** (Parallax Forums HX8K benchmark), at an average CPI of "approximately 4" (YosysHQ/picorv32 README). A custom bytecode FSM of similar size easily meets the ~40–50 MHz "more CPU" target. The binding constraint is bandwidth through Helium's arbitration to the 2 MB SRAM / 64 MB SDRAM hierarchy.

5. **No DSP means iterative multiply/divide — and that is fine.** The iCE40 HX8K has no DSP blocks usable by the Yosys/nextpnr open flow. Per ZipCPU's "Building a Better Verilog Multiply," a naive shift-add "32x32 bit multiply" needs "307 LUT4s," reducible to "112 LUT4s" by shifting the accumulator; an iterative divider is comparable. Both are multi-cycle (32–40 cycles), which matches an FSM core and mirrors PicoRV32's own costs: "a MUL instruction will execute in 40 cycles… a DIV[U]/REM[U] instruction will execute in 40 cycles" (picorv32 README).

6. **A ZPU-style trap-to-software for optional opcodes is compatible with bit-identical semantics — reserve it, don't implement it now.** The ZPU splits its opcode space into mandatory and optional instructions "emulated by code in vectors," letting a minimal core omit complex ops. We adopt the *reservation* (feature bits + illegal-op trap) but keep MUL/DIV/block-ops mandatory in v1 on both implementations, so no software runtime is needed on the core and semantics stay identical.

## Details

### Part 0 — Design drivers and the central conflict
Fixed priorities: (1) interpreter speed on 8 MHz 65816; (2) code density; (3) simple hardware on a small iCE40; (4) toolchain friendliness. Non-negotiable: **bit-identical semantics** across interpreter and softcore — flags, overflow, shift counts ≥ 32, division by zero, INT_MIN/−1, illegal opcodes, and fault behavior.

The recurring conflict is **decode locality (65816) vs. hardware regularity (softcore)**. The 65816 wants one-byte/one-unit opcodes, register indices pre-scaled for direct-page indexing, and immediates that are whole little-endian bytes/words at unit boundaries (never bitfields spanning byte boundaries), with as few `REP`/`SEP` mode switches as possible. Hardware wants fixed field positions, few formats, and length known from the first unit. The Dalvik-style 16-bit-unit format satisfies both far better than RISC-V (immediates scattered across bitfields are costly to reassemble on a 65816) or a fully variable byte stream (awkward multi-way length logic in hardware). Where the two targets genuinely conflict — e.g. the 65816 prefers 16-bit-at-a-time work while hardware prefers a 32-bit datapath — we resolve in favor of a clean 32-bit *architecture* (priority-neutral) implemented as two 16-bit operations on the 65816.

### Part 1 — DELIVERABLE 1: NVM32 ISA specification (DN-SW-VMISA-001)

#### 1.1 Programmer's model
- **16 general-purpose 32-bit registers, R0–R15.** Eight is too few for a C ABI (args + temporaries + SP/FP/LR) without spills, which are expensive in the interpreter; 16 fits a 4-bit nibble field, matches Dalvik/Thumb register-pressure norms, and costs only 64 bytes of direct page (interpreter) and a 16×32 register file in EBR (hardware). Thirty-two registers would need 5-bit fields, breaking clean nibble decode, and double the DP footprint. SWEET16 is the direct ancestor: Wozniak's design has "sixteen internal 16 bit registers, actually the first 32 bytes in main memory, labelled R0 through R15."
- **No hardwired zero register.** It aids RISC hardware but wastes a register for the interpreter and complicates the "16 registers in 64 DP bytes" mapping. Provide a cheap `MOVIZ rd,#0` / `CLR` instead.
- **Conventional roles (soft, ABI-level, not hardwired):** R13 = SP, R14 = LR, R15 = FP; R0–R3 = args/returns. Hardware treats all 16 uniformly.
- **PC:** 24-bit, byte-addressed, instructions 16-bit aligned (IALIGN=16). Always points into the 24-bit guest virtual space.
- **Condition handling: flagless.** Comparisons fold into branches (`Bcc rs,rt,disp`) or `SETcc rd,rs,rt`. No architectural PSW.
- **Status/control state:** a single non-architectural "trap-cause" latch, used only when halted for a fault/syscall; not user-visible. Therefore **process state = {R0–R15, PC} only** — this is what makes migration between interpreter and softcore trivial.

#### 1.2 Memory model
- **Little-endian**, matching the 65816 ("the low byte is the lower address").
- **Pointers are 32-bit values; only the low 24 bits are address, and bits [31:24] must be zero.** Any load/store/branch whose effective pointer has non-zero high bits raises `ADDR_FAULT`, enforced identically on both implementations.
- **Guest memory map (per-process 24-bit virtual space):**
  | Range | Size | Use |
  |---|---|---|
  | `$00_0000`–`$00_FFFF` | 64 KB | **Reserved host region** (bank $00): 65816 direct page(s), 65816 stack, interpreter scratch, VM register save area, trap/mailbox block. Guest may not touch it. |
  | `$01_0000`–`$01_FFFF` | 64 KB | Interpreter code + dispatch table image (read-only, shared, mapped identically in every process). |
  | `$02_0000`–`$0F_FFFF` | ~896 KB | Guest code (.text), read-execute. |
  | `$10_0000`–`$EF_FFFF` | ~14 MB | Guest data/heap (.data/.bss/heap), read-write. |
  | `$F0_0000`–`$FF_FFFF` | 1 MB | Guest stack (grows down from `$FF_FFFF`), guard page at bottom. |

  This solves the "interpreter code, DP, stack and bank-$00 all live in the same address space" problem: the loader refuses to map guest segments over banks $00–$01, and the MMU marks them no-access for guest pointers. On the softcore these two banks are simply never generated as guest effective addresses (the softcore has no 65816 DP/stack), so the region is reserved-but-unused there — kept reserved only so one binary's memory map is valid on both executors.
- **Alignment:** 16- and 32-bit accesses require natural alignment; misalignment raises `ADDR_FAULT`. Unaligned 32-bit access on the 65816 costs extra cycles and bank-carry handling, and forces byte-assembly in hardware; aligned-only keeps both fast and identical.
- **Invalid/reserved access:** unmapped page → `PAGE_FAULT`; permission/COW → `PROT_FAULT`; both via the restartable mechanism in §1.5.

#### 1.3 Instruction encoding (chosen: 16-bit code units, Dalvik-style)
Every instruction is one, two, or three 16-bit little-endian code units. **The opcode byte alone determines length** via a 256-entry length table (interpreter) or a small combinational map (hardware).

| Format | Layout (unit0 [unit1] [unit2]) | Example use |
|---|---|---|
| **F1 (op)** | `op:8 \| 00:8` | `HALT`, `RET`, `NOP`, `YIELD` |
| **F2 (op, rd, rs)** | `op:8 \| rd:4 rs:4` | 2-addr `ADD rd,rs`, `MOV`, `NEG` |
| **F3 (op, rd, rs, rt)** | `op:8 \| rd:4 rs:4` + `rt:4 z:4 :8` | 3-addr ALU `ADD rd,rs,rt` |
| **F4 (op, rd, imm16)** | `op:8 \| rd:4 z:4` + `imm:16` | `MOVIZ/MOVIS rd,#imm16` |
| **F5 (op, rd, imm32)** | `op:8 \| rd:4 z:4` + `imm:32` | `MOVI32 rd,#imm32` |
| **F6 (op, rd, rs, disp16)** | `op:8 \| rd:4 rs:4` + `disp:16` | load/store `[rs+disp]`, PC-rel |
| **F7 (op, rs, rt, disp16)** | `op:8 \| rs:4 rt:4` + `disp:16` | compare-and-branch `Bcc rs,rt,disp` |

Rules baked in for the 65816: register fields are always the two nibbles of unit0's second byte (one `LDA` + mask/shift extracts them, pre-doubled with `ASL` to index 4-byte DP slots); immediates/displacements are always whole 16-/32-bit little-endian words at a unit boundary (a direct `LDA (PC),Y` reads them with no shifting); sign-extension is decided by *opcode* (`MOVIZ` zero-extends, `MOVIS` sign-extends), never by a mode bit, so both implementations stay branch-free on extension.

**Opcode map (8-bit primary opcode, 256 entries):**
| Range | Class |
|---|---|
| `$00–$0F` | System/flow: NOP, HALT, YIELD, BKPT, SYSCALL, RET, CALL, CALLR, JMP, JMPR |
| `$10–$2F` | ALU reg-reg (2-/3-addr): ADD, SUB, AND, OR, XOR, NOT, NEG, MUL, MULH, MULHU, DIV, DIVU, REM, REMU |
| `$30–$3F` | Shifts/rotates: SHL, SHR, SAR, ROL, ROR (reg and imm count) |
| `$40–$5F` | ALU reg-imm: ADDI, SUBI, ANDI, ORI, XORI, MOVIZ, MOVIS, MOVI32 |
| `$60–$7F` | Loads/stores: LDB/LDBU/LDH/LDHU/LDW, STB/STH/STW (base+disp16) |
| `$80–$9F` | Compare-and-branch: BEQ, BNE, BLT, BLTU, BGE, BGEU (rs,rt,disp16); BRA; SETcc |
| `$A0–$AF` | Stack/frame fast paths: PUSH, POP, LDW.fp/STW.fp (FP-rel small offset), ENTER, LEAVE |
| `$B0–$BF` | Block ops: MEMCPY, MEMSET (restartable, §1.4) |
| `$C0–$EF` | **Reserved for extensions** (feature-bit gated) |
| `$F0–$FE` | **Reserved for privileged/system ISA** (standalone mode, Part 4) |
| `$FF` | Guaranteed **illegal** on both implementations — canary |

Reserved and illegal opcodes trap identically (`ILLEGAL_OP`). Versioning: the executable header carries ISA major/minor + a feature-bit vector (§1.7).

#### 1.4 Operation semantics (bit-identical tables)
These are the semantics both implementations MUST match exactly; they follow eBPF/RISC-V where those are already precise and hardware-friendly.
- **Arithmetic** wraps mod 2³² (two's complement); no overflow trap. `ADD/SUB/MUL` identical for signed/unsigned low word.
- **MULH/MULHU:** high 32 bits of the signed/unsigned 64-bit product.
- **Shifts:** shift amount **masked to 5 bits** (`count & 31`) for all shift/rotate ops — this fixes "shift count ≥ 32" identically (the same rule eBPF states: "a mask of… 0x1F (31) for 32-bit operations"). `SAR` is arithmetic (sign-replicating).
- **Division/remainder edge cases (defined, no trap — deliberately branchless and bit-identical):**
  | Case | DIV/DIVU | REM/REMU |
  |---|---|---|
  | divisor = 0 | `0xFFFFFFFF` | dividend (unchanged) |
  | signed `INT_MIN / −1` | `INT_MIN` (`0x80000000`) | `0` |
  These follow the **RISC-V** convention, chosen because it is the one both the iterative divider FSM and the C reference implement most naturally. (This intentionally differs from eBPF, which zeroes the destination on div-by-zero — flagged as a decision, §Caveats.)
- **Compares/branches:** signed (`BLT/BGE`) and unsigned (`BLTU/BGEU`) provided explicitly; no flags consulted.
- **Loads:** `LDB/LDH` sign-extend, `LDBU/LDHU` zero-extend to 32 bits; `LDW` full width.
- **Block ops (MEMCPY/MEMSET) are restartable:** progress kept in registers (R0=dst, R1=src/val, R2=remaining count), operating one element at a time low→high, decrementing R2. Re-executing with current register values resumes correctly — the multi-step-instruction restart requirement that makes both the ABORTB path and the softcore fault path precise (§1.5).

#### 1.5 Exceptions, traps, preemption, atomicity
Causes: `ILLEGAL_OP`, `ADDR_FAULT` (misalign or non-zero high byte), `PAGE_FAULT`, `PROT_FAULT`, `BKPT`, `SYSCALL`. (`DIV0` is *reserved-unused* because division is fully defined.)

**Instruction atomicity / commit point.** An NVM32 instruction commits register/PC writes only at its end — the crucial cross-implementation contract:
- **On the 65816 interpreter it is nearly free**, because ABORTB already provides it. The WDC W65C816S datasheet states: *"The ABORTB input can interrupt the currently executing instruction without modifying internal register, thus allowing virtual memory system design."* More precisely, an aborted instruction runs to completion but its computational changes to registers/memory are discarded and it restarts. So the interpreter must ensure the VM-level commit (writing the destination DP slot, advancing VM PC) happens *after* the faultable memory access: compute effective address → do the access → only then write the DP register and advance PC. If the access faults, Helium aborts and restarts the *host* instruction, and the whole VM instruction re-executes cleanly.
- **On the softcore this constraint is the real work.** The FSM must not update the architectural register file or PC until the MEM stage succeeds. On a fault it asserts its ABORTB-equivalent to Helium, freezes {R0–R15, PC} at pre-instruction values, latches the trap cause, and halts. Block ops keep progress in registers, so they too restart precisely. (Helium must block the write on a faulting store — the same requirement ABORTB imposes.)

**Preemption.** State is only {R0–R15, PC}. In the interpreter these live in the DP + save area, so a task switch is "save DP window + PC, load next"; the interpreter is *transparently* preemptible because the host timer IRQ is honored only between handler dispatches (the handler is the atomic unit). The softcore is preempted by a kernel stop: it finishes/aborts the current instruction restartably, writes {R0–R15, PC} to its state window, and idles.

#### 1.6 ABI and calling convention (NVM32 C ABI, "ILP32-24")
- **Types:** `int`=32, `long`=32, `short`=16, `char`=8; pointers = 32-bit storage / 24-bit effective. **ILP32 — deliberately diverging from Calypsi's 16-bit `int`** (§Caveats): the VM has 32-bit registers, so 16-bit `int` would waste the point and cause constant widen/narrow churn.
- **Register roles:**
  | Reg | Role | Saver |
  |---|---|---|
  | R0–R3 | args / returns (R0:R1 for 64-bit) | caller |
  | R4–R11 | temporaries/locals | R4–R7 caller, R8–R11 callee |
  | R12 | scratch/assembler temp | caller |
  | R13 | SP (full descending, 4-byte aligned) | callee |
  | R14 | LR | callee (via ENTER) |
  | R15 | FP | callee |
- **Stack frame:** `CALL` puts return address in LR; `ENTER n` pushes LR+old FP and reserves n bytes; `LEAVE`/`RET` reverse it. Stack args pushed right-to-left, 4-byte aligned. Structs: natural alignment, members in declaration order, size rounded to 4.
- **Syscall ABI (bytecode → host):** `SYSCALL` with an 8-bit service number in unit0's low byte (optional 16-bit extended number in unit1); args in R0–R5; result in R0 (+R1); errors as negative R0 (errno-style), never a flag. `SYSCALL` halts the VM with cause `SYSCALL`, number/args visible in the state window.

**Mapping NVM32 syscalls onto the native COP kernel ABI.** The native kernel is entered by the 65816 `COP` instruction with an inline signature byte (Calypsi convention). The interpreter's `SYSCALL` handler: (1) reads/validates the service number against a table; (2) **marshals and validates pointer arguments** — every R holding a guest pointer is range-checked (high byte zero, inside the guest data/stack regions, correct R/W permission) and confirmed *not* to name the reserved host region (no address translation is needed because the guest virtual space *is* the process virtual space); (3) converts 32-bit NVM32 args to the width each Calypsi kernel entry expects — this marshalling layer is the single place that knows both ABIs — then executes `COP #sig`; (4) packs the result back into R0/R1 and resumes. On the softcore the identical `SYSCALL` halts the core, the 65816 kernel runs the *same* marshalling code from the state window, and resumes the core. **Guest code never calls 65816 machine code directly** (A5) — only this portable trap crosses the boundary, so the same binary works on the softcore.

**Controlled native extension / service table (A5): worthwhile, implemented as *data*.** A per-process service table maps small integers → kernel handlers. Because it is a number space resolved by the host, it is implementation-independent and safe on the softcore — strongly preferred over any "call native address" mechanism, which would be softcore-fatal.

#### 1.7 Executable format (NVX)
Header: magic `"NVX1"`, ISA major/minor, **feature-bit vector** (bit0 base, bit1 MUL/DIV present [=1 in v1], bit2 block ops, bit3 reserved TBI-tagging, …), 24-bit entry PC, section table (.text/.rodata/.data/.bss sizes), requested stack size, load-bias policy. **PIC strategy:** intra-module control flow uses PC-relative branches/calls (`JMPR/BRA/CALLR`, signed disp16/disp24); data uses FP/SP-relative or a reserved global pointer. The image is position-independent within its 24-bit space, so .text needs no relocation — important because .text is shared read-only across processes *and* across both executors; a minimal relocation table exists only for absolute pointer constants in .data. **Feature discovery:** the loader checks header feature bits against the current executor and refuses/down-selects; since v1 mandates the same feature set on both, one binary always runs on both.

#### 1.8 Existing-ISA alternatives considered (and why custom wins)
- **RV32I(MC):** huge upside — free GCC/LLVM/Rust and tiny proven softcores (PicoRV32 1,982 LCs @ 65.22 MHz on HX8K; SERV ~198 LUTs; FemtoRV32 <1,000 LUTs). **Fatal for priority #1:** RISC-V immediates are deliberately scrambled across bitfields to cut hardware mux cost — the opposite of what a 65816 interpreter needs, where reassembling a branch immediate costs many shifts/ORs per instruction. RV32C helps density (RISC-V spec: "25%–30% code-size reduction") but worsens 65816 length-decode and field extraction. **Rejected as the native format — but recommended as the fallback** if throughput targets are missed (§Recommendations).
- **68000 / subset:** tempting (literally 32-bit registers over a 24-bit address space, plus vbcc/GCC). **Rejected:** big-endian (fights the little-endian 65816 and the C11 host emulator), complex variable-length decode and flag semantics (expensive to make bit-identical), and large softcores. We inherit its useful shape (32-bit regs / 24-bit address) anyway.
- **eBPF-like:** excellent fixed 64-bit encoding, proven for interpretation + JIT + FPGA (hBPF, hXDP). **Rejected as-is:** 64-bit registers and 8-byte instructions double per-instruction memory traffic and hurt density on this class of machine. We **borrow** its precise ALU/shift/branch semantics and its call-as-trap "helper function" model.
- **ZPU:** borrow the optional-opcode-trap *pattern* and the "smallest 32-bit GCC core" existence proof; reject the pure-stack model (poorer density and higher memory traffic than a register file, and worse for the 65816, which benefits from indexable DP register slots).
- **SWEET16:** the direct ancestor/existence proof — a 16-register VM interpreted on a 6502 in "only about 300 bytes," "about one-tenth the speed of the equivalent native 6502 code." We scale its idea to 32-bit/24-bit with modern dispatch.
- **Inferno Dis:** memory-to-memory three-operand CISC VM that JITs well; instructive, but general memory operands are costlier to interpret than a register file on the 65816. Rejected; register model kept.
- **Load-time pre-decode/translation:** viable middle path (translate NVM32 to a threaded/token form at load). Costs ~1.5–2× the .text size in RAM and breaks read-only sharing unless per-process. Recommended only as an interpreter optimization behind a flag, not the architecture.

**Verdict:** a custom ISA wins because priority #1 (interpreter speed) and the bit-identical constraint are best served by an encoding co-designed with the 65816's fetch/dispatch, while priorities #3–4 are satisfied by keeping the *semantics* a boring subset of RISC-V/eBPF conventions that both hardware and a retargeted C compiler already like.

### Part 2 — DELIVERABLE 2: Executing NVM32 on the W65C816S (DN-SW-VMINTERP-001)

#### 2.1 Interpreter architecture
- **VM registers in the direct page.** R0–R15 occupy 64 contiguous DP bytes (4 bytes each, little-endian); with the DP register pointing at this block, a VM register is `dp,X` where X = reg×4 — mirroring SWEET16's registers in "the first 32 bytes in main memory." Per-process, the kernel points DP at that process's 64-byte block; interpreter code is re-entrant and read-only.
- **PC representation:** 24-bit guest pointer as a 16-bit offset plus a bank byte in DP (`PCbank:PCoff`). Fetch keeps the code bank in DBR and uses offset addressing with periodic renormalization. **Renormalization:** after each fetch, add the instruction length to PCoff; when PCoff crosses a 16-bit boundary the carry increments PCbank. Straight-line code pays only an increment plus a rarely-taken bank-carry branch.
- **CPU mode strategy: native mode, default M=0/X=0 (16-bit A and index)**, because 32-bit VM ops are two 16-bit operations. Handlers touching bytes (`LDB`) toggle M locally with `SEP/REP #$20`. Minimizing `REP`/`SEP` churn is the top micro-optimization: keep the whole loop and all 16/32-bit handlers in 16-bit mode. The datasheet's +1-cycle-in-16-bit-mode penalty is already priced in.
- **Placement:** the dispatch table and all handlers live in one program bank (bank $01 of the reserved region), because `JMP (addr,X)` resolves the indirect address in the *current program bank*. Interpreter code fits ≤64 KB. Handlers end by jumping back to the dispatcher (threaded dispatch), not `RTS`, to avoid stack churn.

#### 2.2 Dispatch loop and representative handlers (ca65 syntax; 16-bit A/X/Y; DP = VM register block; `IP` = VM PC pointer in DP)

```asm
; --- main dispatch: fetch opcode unit, jump through 256-entry table ---
NEXT:
        lda   (IP)          ; ~6c: fetch unit0 (opcode:8 | fields:8)
        and   #$00FF        ; ~2c: isolate opcode byte
        asl   a             ; ~2c: *2 for word table index
        tax                 ; ~2c
        jmp   (OPTAB,x)     ; ~6c: dispatch (table in this program bank)
; OPTAB: .addr h_nop, h_add, ...   (256 entries)
```

Reg-reg 3-address ADD (`ADD rd,rs,rt`, F3) — 32-bit add via two 16-bit ADC:
```asm
h_add:
        ; decode rd/rs (unit0 hi byte) and rt (unit1) into DP offsets (nibble+ASL)
        lda   Rs_lo         ; low 16 bits of rs   (dp)
        clc
        adc   Rt_lo
        sta   Rd_lo
        lda   Rs_hi         ; high 16 bits
        adc   Rt_hi
        sta   Rd_hi
        lda   IP            ; advance IP by 4 (two units)
        clc
        adc   #4
        sta   IP
        bcc   :+
        inc   IPbank
:       jmp   NEXT
```
Estimated incl. decode+dispatch ≈ 60–90 cycles.

32-bit load with displacement (`LDW rd,[rs+disp16]`, F6) — uses `[dp],Y` long-indirect indexed with 24-bit pointers, the mechanism that makes guest pointers directly usable (A2):
```asm
h_ldw:
        ; EA = Rs + sign_extend(disp16), 24-bit; validate high byte == 0 else ADDR_FAULT
        lda   [EA]          ; ~7c: low 16 bits (65816 handles bank carry)
        sta   Rd_lo
        ldy   #2
        lda   [EA],y        ; ~7c: high 16 bits
        sta   Rd_hi
        ; advance IP; jmp NEXT
```
Estimated ≈ 90–120 cycles.

Signed compare-and-branch (`BLT rs,rt,disp16`, F7):
```asm
h_blt:
        lda   Rs_lo
        cmp   Rt_lo
        lda   Rs_hi
        sbc   Rt_hi         ; 32-bit signed compare via SBC of low then high
        bvc   :+
        eor   #$8000        ; correct signed comparison on overflow
:       bmi   taken
        jmp   NEXT          ; not taken: IP += 4 (folded above)
taken:  ; IP += sign_extend(disp16); renormalize bank; jmp NEXT
```
Estimated ≈ 40–70 cycles. CALL/RET manipulate LR/PC in DP (~25–40 c).

Block move (`MEMCPY`) maps to **MVN/MVP**: set X=src low16, Y=dst low16, C=count−1, banks in the operands, then `MVN src,dst`. At **7 cycles/byte** (WDC/SNESdev: "Each byte takes 7 CPU cycles of the 65C816") this is the fastest copy available and is natively interruptible/restartable, matching §1.4 (X/Y/C hold progress). Cross-bank copies are segmented per 64 KB bank.

`SYSCALL` stores cause+number into the mailbox, restores kernel DP/stack, and returns to the kernel, which issues `COP #sig` (§1.6).

#### 2.3 Throughput estimate
At 8 MHz and ~60–120 cycles per typical VM instruction (dispatch+decode+execute), steady-state throughput ≈ **150,000–400,000 VM-instr/s** for an ALU/branch-weighted mix, lower for load/store-heavy code — roughly **20–50× slower than hand-written native 65816**, consistent with SWEET16's ~10× for a 16-bit VM scaled to 32-bit width and richer decode. **Cache-miss PHI2 stalls are not in these cycle counts** (the project stretches/stops PHI2 on misses), so wall-clock throughput drops whenever the working set spills the 1 MB SRAM cache — memory latency then dominates dispatch.

#### 2.4 System interaction
- **Page faults:** a guest access that misses triggers Helium's hardware page-table walk; a bounded fill stalls PHI2 transparently and the interpreter never notices. Unmapped/permission/COW cases assert ABORTB; because the VM commit happens only after the access (§1.5), after the kernel services the fault the handler simply re-executes from the same VM PC.
- **Syscall forwarding via COP** as in §1.6 — the COP handler is the kernel's existing native syscall entry.

#### 2.5 Optimizations — cost/benefit (respecting complexity pruning)
| Optimization | Benefit | Cost | Verdict |
|---|---|---|---|
| Threaded dispatch (jump to NEXT, not RTS) | saves ~12 c/instr | trivial | **Adopt now** |
| 16-bit default mode (avoid REP/SEP churn) | large | trivial | **Adopt now** |
| Superinstructions (compare-and-branch, ENTER/LEAVE, FP-rel load/store, CLR) | fewer dispatches on hot paths | small (already in map) | **Adopt now** |
| Load-time pre-decode to threaded/token image | ~1.5–2× dispatch speedup | 1.5–2× RAM/process; breaks RO sharing | Defer; behind a flag |
| Helium MMIO 32×32 MUL/DIV helper | removes 100s of cycles per MUL/DIV | gateware + a spec boundary | **Recommend** (Helium owns the bus; shareable with the softcore) |
| Helium decode helper | marginal | gateware complexity | Reject (pruning) |

#### 2.6 Tooling
- **Assembler + linker** emitting NVX (buildable on ca65/ld65 conventions or a small custom tool).
- **Loader** in the native kernel (validates header/feature bits, maps segments, sets entry PC).
- **Disassembler** (drives differential trace comparison).
- **Reference interpreter in portable C11**, embedded in the existing PC emulator (DN-SW-EMU-001) — the **golden model**.
- **Conformance suite + differential testing** (the proven NVFS pattern: spec + reference implementation + host tooling + conformance suite + differential testing): every opcode and every §1.4 edge case, run on (a) the C reference, (b) the 65816 interpreter (emulator and hardware), and later (c) the softcore in Verilator — all traces must match bit-for-bit on {R0–R15, PC, memory writes}.

### Part 3 — DELIVERABLE 3: Native execution on an iCE40 softcore (DN-HW-VMCORE-001)

#### 3.1 Microarchitecture — multi-cycle FSM, hardwired control
Recommended: a **multi-cycle FSM with hardwired control**, not a pipeline and not microcode. Priority #3 (simplicity on a small iCE40) and the memory-bound nature of the design make a pipeline a poor trade; hardwired control for ~60 opcodes is smaller than a microstore.

FSM states: FETCH0 → DECODE (length + fields) → [FETCH1/FETCH2] → EXEC (ALU/addr-calc) → MEM (load/store, may stall/abort) → WB (commit regfile+PC). **Commit only in WB** ⇒ precise faults (§1.5).
- **Decoder:** the first byte indexes a small combinational table giving length + format + control lines — trivial because formats are few and fields are fixed nibbles.
- **32-bit ALU:** LUT4-based add/sub/logic on the iCE40 fast carry chain.
- **Shifter:** **iterative** (1 bit/cycle, ≤31 cycles) to save LUTs, or a small two-level shifter (FemtoRV's costs ~40 LUT4).
- **Multiply/divide:** **iterative shift-add, mandatory.** 32×32 multiplier ≈ **307 LUT4** (or ~**112 LUT4** with accumulator-shifting, per ZipCPU), ~32–40 cycles; iterative divider comparable, implementing §1.4's exact edge cases in FSM terminal states. No DSP is used (none available in the open flow on HX). Alternatively, **share Helium's MMIO MUL/DIV unit** (§2.5) to save core LUTs.
- **Register file:** 16×32-bit in EBR (iCE40 EBR = 4 Kbit, 256×16 true-dual-port; a 32-bit-wide 1R/1W file ≈ 2 EBR, ~4 EBR for two read ports), or in LUT fabric (~512 FFs) if EBR is scarce — EBR is the better use.
- **Estimated resources:** core ~**2,500–3,500 LUT4** and ~4–6 EBR, comfortably inside HX8K's **7,680 LC / 32 EBR**, leaving ~4,000 LUT4 for a small I/D cache and the Helium bus-master interface. Calibration: PicoRV32 **1,982 LCs @ 65.22 MHz** on HX8K (CPI ≈ 4); FemtoRV32 <1,000–1,180 LUTs @ 80 MHz (~0.57 DMIPS/MHz); SERV ~198 LUTs but CPI ≈ 32 (too slow). **Fmax target ~40–50 MHz** matches the project's "~50 MHz-class softcore… more CPU" note.
- **Optional ZPU-style trap-to-software:** reserved (feature bits + illegal-op trap) but **not** used for MUL/DIV in v1, to keep semantics bit-identical without a software runtime on the core. It becomes useful only for a future ultra-minimal core that must omit block ops or MUL.

Verilog skeleton (decode + precise commit; illustrative):
```verilog
localparam S_FETCH0=0,S_DECODE=1,S_FETCH1=2,S_EXEC=3,S_MEM=4,S_WB=5,S_HALT=6;
always @(posedge clk) begin
  case (state)
    S_FETCH0: begin ir0 <= mem_rdata16; mem_addr <= pc;       state <= S_DECODE; end
    S_DECODE: begin
        opcode <= ir0[7:0]; rd <= ir0[15:12]; rs <= ir0[11:8];
        ilen   <= len_tab[ir0[7:0]];              // 1, 2, or 3 units
        state  <= need_unit1 ? S_FETCH1 : S_EXEC;
    end
    S_EXEC: begin alu_y <= alu_out; state <= is_mem ? S_MEM : S_WB; end
    S_MEM:  begin
        if      (fault)   begin cause <= mcause; state <= S_HALT; end  // no WB → precise
        else if (mem_ack) state <= S_WB;                               // else stall (PHI2-equiv)
    end
    S_WB:   begin regfile[rd] <= wb_val; pc <= next_pc; state <= S_FETCH0; end
    S_HALT: begin /* raise trap to 65816 kernel; freeze regs/pc */ end
  endcase
end
```

#### 3.2 Memory interface
- **Bus mastering through Helium's arbitration**, sharing the MMU/TLB/page tables/ASIDs exactly as the 65816 does — the softcore issues 24-bit virtual addresses in the *same* per-process space, so page tables are identical and one binary's pointers are valid on both.
- **Restartable fault signaling equivalent to ABORTB:** on a Helium fault response the MEM state freezes {regfile, PC}, latches cause, and halts — the architectural analogue of ABORTB's "no register modification, restart the instruction," with Helium blocking the write on a faulting store.
- **Bandwidth is the binding constraint.** A naive 2-byte-per-fetch FSM at 40 MHz can saturate the shared bus; mitigations: a small **local instruction cache** (burst-fill a line from SDRAM via Helium), a **data cache / store buffer**, and **burst/prefetch** of sequential code units. Because misses stall (PHI2-equivalent), effective IPC is set by hit rate, not Fmax.
- **Estimated speedup:** at ~5–10 cycles/VM-instr @ 40 MHz ≈ 4–8M VM-instr/s vs. the interpreter's ~0.15–0.4M ⇒ **~20–100×** before contention; realistically **~20–40×** once shared-bus contention and misses are included — exactly the regime that makes high-resolution graphics modes (infeasible at 8 MHz) viable.

#### 3.3 Host/coprocessor integration
- **Start/stop/preempt/resume via a state window + mailbox** in the reserved host region: the kernel writes {R0–R15, PC, ASID, entry} and releases the core; to preempt, it asserts stop, the core finishes/aborts the current instruction restartably and writes {R0–R15, PC} back. Because architectural state is identical to the interpreter's, **a process can migrate mid-execution between interpreter and softcore** — run it interpreted until the softcore is free, then hand off the exact same {R0–R15, PC}. This shared-state migration is a headline benefit.
- **Syscall forwarding:** `SYSCALL` halts the core with cause+number+args in the window; the 65816 kernel runs the *same* marshalling/COP path as the interpreter and resumes the core.
- **Interrupts:** device IRQs remain the 65816's; the softcore is a coprocessor, simply stopped/resumed. A per-core "please stop" doorbell bounds preemption latency.
- **Debug:** breakpoints (`BKPT` + address-match comparators), single-step (run-one-instruction FSM mode), and trace export via **Helium's Debug Agent**, reusing existing infrastructure.

#### 3.4 Verification
- **Same conformance suite, C11 reference as golden model.** Verilator co-simulation runs the softcore against the reference with per-instruction trace comparison on {R0–R15, PC, memory writes}. Differential testing across all three executors (C reference, 65816 interpreter, softcore) is the acceptance gate.
- **Cheap formal checks** where they pay: the decoder's length/field extraction and the ALU edge cases (shift mask, div-by-zero, INT_MIN/−1) are small enough for bounded model checking (yosys-smtbmc) against the reference truth tables.

### Part 4 — Standalone-mode sketch (privileged extension)
The reserved opcode block `$F0–$FE` (§1.3) hosts a **privileged/system ISA**: a supervisor-mode bit, a handful of CSRs (status, trap-cause, trap-PC, page-table base, ASID), trap vectors, and MMU-control ops (TLB flush, page-table-base load). In standalone mode the softcore is the only CPU and boots a kernel *compiled to NVM32 bytecode*; user/supervisor separation and the CSRs replace the 65816 kernel's role. **This is explicitly not the primary design** — encoding space is reserved so it can be added without breaking v1 binaries, but v1 defines only user-mode NVM32 plus the coprocessor model of §3.3.

## Recommendations
**Stage 0 — Ratify the ISA (now).** Freeze NVM32 v1 as specified (16 registers, 16-bit code units, flagless compare-and-branch, 32-bit-clean pointers, the §1.4 semantics tables). Publish as DN-SW-VMISA-001 Rev A. *Trigger to revisit before freeze:* if hand-analysis shows a hot opcode costs >150 interpreter cycles, add a superinstruction first.

**Stage 1 — Build the golden model + conformance suite (now, software-only).** Write the C11 reference interpreter inside the existing PC emulator (DN-SW-EMU-001) plus the conformance/differential harness (NVFS pattern). Cheapest, highest-leverage step; unblocks everything.

**Stage 2 — Retarget a C compiler. Recommended: vbcc.** It is explicitly "portable and retargetable… fully supports cross-compiling for 8, 16, 32 and 64bit architectures," already ships 65816 and 68k/ColdFire backends to crib from, and supports differently sized pointers — ideal for the 32-bit-storage/24-bit-effective model. Estimated solo effort for a clean register ISA backend: **~1–3 months part-time** (machine description + calling convention + peephole), far below a GCC/LLVM port. *Alternatives:* a QBE-based backend (small, modern) is a strong second; LLVM/GCC only if Rust/C++ become requirements; chibicc/lcc are fine for bootstrapping tests but weak optimizers.

**Stage 3 — 65816 interpreter (DN-SW-VMINTERP-001).** Implement the dispatch loop and handlers in ca65; validate against the golden model on hardware. *Abandonment/branch condition:* if measured throughput is **below ~100,000 VM-instr/s** on representative code (after Stage-2 optimizations and the Helium MUL/DIV helper), reconsider either (a) load-time pre-decode, or (b) switching the native format to RV32IM to buy a free toolchain and existing softcores — accepting worse 65816 decode.

**Stage 4 — Helium MMIO MUL/DIV helper.** Cheap gateware that removes the worst interpreter hot spots and is shareable with the softcore.

**Stage 5 — Softcore (DN-HW-VMCORE-001), deferred per project.** Prototype on the Phase-1 Colorlight ECP5 (44K LUT4 — abundant headroom) with Verilator differential testing, then target iCE40 HX8K. *Abandonment conditions:* if the core + minimum viable cache **cannot fit HX8K within timing at ≥40 MHz**, then either (a) keep the softcore on the larger ECP5 in the product, or (b) drop MUL/DIV/block-ops to the reserved trap-to-software path to shrink the core, or (c) narrow the datapath to 16-bit with 32-bit as multi-cycle. *Success threshold to justify building the softcore at all:* a measured **≥20× speedup** over the interpreter on graphics-mode workloads.

## Caveats
- **Throughput numbers are engineering estimates, not measurements.** The 150k–400k VM-instr/s interpreter figure and the 20–100× softcore speedup are derived from per-handler cycle counts and published core CPIs; they exclude PHI2-stall time on cache misses, which the project itself notes means CPU cycle counts do *not* measure wall-clock time. Validate empirically in Stage 1/3.
- **Discrepancy with the "softcore 65816" project decision.** This design makes FPGA-C *bytecode-native*, not 65816-native. Consequence: **the native 65816 kernel can never run on FPGA-C unless also compiled to NVM32 bytecode.** That is acceptable under the coprocessor model (A1) but must be an explicit, recorded decision; standalone mode (Part 4) is the only path to a softcore-only machine and requires a bytecode kernel.
- **Calypsi ABI mismatch is real and deliberate.** Calypsi uses 16-bit `int` and 24-bit pointers (the Calypsi 65816 compiler uses 32-bit data pointers "so frequent use of REP and SEP in pointer arithmetic sequences can be avoided," but int is 16-bit); NVM32's C ABI is ILP32 (32-bit `int`, 32-bit pointer storage). The two ABIs meet *only* at the syscall marshalling layer (§1.6). Guest C is compiled by the retargeted NVM32 compiler, **not** by Calypsi; Calypsi remains the native-kernel compiler. Do not share compiled objects across the boundary.
- **Emulator scope.** The softcore is currently out of the PC emulator's scope, but the *VM* (interpreter + C reference) is squarely in scope and is the golden model — recommend explicitly widening DN-SW-EMU-001 to host the reference interpreter, keeping the softcore in Verilator rather than the C emulator.
- **Division convention differs from eBPF.** We chose the RISC-V div-by-zero / INT_MIN÷−1 conventions (§1.4) because they are most natural for both the iterative divider and the C reference; this is a conscious divergence from eBPF's zero-on-div0 rule and must be stated in DN-SW-VMISA-001 so no one "fixes" it later.
- **Some quantitative FPGA figures come from adjacent parts/community benchmarks.** The HX8K PicoRV32 figure (1,982 LCs, 65.22 MHz) is from a community benchmark rather than an official Lattice table, and several small-core numbers are measured on iCE40 UP5K (slower silicon) rather than HX8K. Treat ±20% as the confidence band until a trial synthesis of the actual core is run.
- **Open question — instruction-cache coherence.** If the softcore caches guest .text locally and the kernel reuses/remaps pages (COW, paging), the softcore's I-cache must be flushed on the relevant TLB/ASID events. Specify this in DN-HW-VMCORE-001 before implementation.
- **Open question — MEMCPY across banks vs. hardware.** The interpreter maps MEMCPY to MVN/MVP (bank-confined, segmented per 64 KB); the softcore has a flat 24-bit datapath. Both must produce identical memory-write order and identical restart behavior — verify explicitly in the conformance suite, since this is the one instruction whose *implementation* differs most between the two targets.