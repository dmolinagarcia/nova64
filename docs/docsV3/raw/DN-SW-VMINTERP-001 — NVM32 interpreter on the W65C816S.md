# DN-SW-VMINTERP-001 — NVM32 interpreter on the W65C816S

*noVa64 · Rev B (draft for discussion)* · 2026-09-20 · @Someone

## Revision history

| Rev | Date | Change |
| --- | --- | --- |
| A | 2026-09-20 | Initial draft. Interpreter architecture, dispatch loop, entry gate, interrupt and preemption model. Corrects the preemption rationale carried in the original VM report. |
| B | 2026-09-21 | Gate selection moved to the BIOS: CPU self-identification through a `WDM` probe, platform capabilities from Helium, a third gate implementation for the FPGA-C coprocessor, and a system information block in a shared read-only page as the BIOS-to-kernel contract. Fixes Rev A placing the gate vector as if bank `$00` were global. |

*Review cycle: reviewers, dates and decisions to be recorded here before this note leaves draft status.*

## Status and scope

**Deferred, with DN-SW-VMISA-001.** Implementation begins after the C11 reference interpreter exists and the kernel runs its first user process. This note specifies the design so that the reopening is a matter of implementation, not redesign.

Scope: the interpreter that executes NVM32 on the physical W65C816S at 8 MHz, the gate between native and bytecode code, and the interrupt and fault behaviour that follows. The ISA is DN-SW-VMISA-001; the softcore that runs the same binaries is DN-HW-VMCORE-001; the measurement harness is DN-SW-VMBENCH-001.

Two properties drive every decision here:

1. **Interpreter speed is priority #1** for the whole VM programme, which is why the encoding is co-designed with this dispatch loop.
2. **Bit-identical semantics with the other executors**, verified by differential testing against the C11 reference as golden model.

## Interpreter architecture

**VM registers in the direct page.** R0–R15 occupy 64 contiguous DP bytes, 4 bytes each, little-endian. With D pointing at that block a VM register is `dp,X` with X = register × 4 — the fastest addressing the machine has, and the direct descendant of SWEET16's registers in the first bytes of main memory. The kernel points D at the process's block; the interpreter itself is re-entrant and read-only, shared by every process.

**PC representation:** a 24-bit guest pointer held as a 16-bit offset plus a bank byte in DP. Fetch keeps the code bank in DBR and uses offset addressing. After each fetch the instruction length is added to the offset; when it crosses a 16-bit boundary the carry increments the bank byte. Straight-line code therefore pays an increment plus a rarely-taken branch.

**CPU mode: native mode, M = 0 and X = 0 by default** (16-bit accumulator and index), because every 32-bit VM operation is two 16-bit operations. Handlers that touch bytes toggle M locally. **Minimising `REP`/`SEP` churn is the single most important micro-optimization**: the dispatch loop and all 16- and 32-bit handlers stay in 16-bit mode throughout.

**Placement:** the dispatch table and every handler live in one program bank — bank `$01` of the reserved host region — because `JMP (addr,X)` resolves its indirect address in the current program bank. Interpreter code fits within 64 KB. Handlers end by jumping back to the dispatcher rather than returning, to avoid stack churn.

**Process state layout.** The state window is one structure, used by both executors: R0–R15 at offset 0, then the VM PC offset and bank, the trap cause and service number, and the saved kernel stack pointer. On the 65816 it is the interpreter's direct page; on the softcore it is the window the hardware reads and writes. Same layout, two writers.

## Dispatch loop and handlers

ca65 syntax, 16-bit A/X/Y, D = the process register window, `IP` = the VM PC in DP. Cycle figures are estimates from the datasheet, to be calibrated in DN-SW-VMBENCH-001.

```asm
; --- main dispatch: fetch opcode unit, jump through the 256-entry table ---
NEXT:
        lda   (IP)          ; ~6c: fetch unit0 (opcode:8 | fields:8)
        and   #$00FF        ; ~2c: isolate the opcode byte
        asl   a             ; ~2c: ×2 for a word table index
        tax                 ; ~2c
        jmp   (OPTAB,x)     ; ~6c: dispatch, table in this program bank
; OPTAB: .addr h_nop, h_add, ...   (256 entries)
```

Three-address ADD, 32-bit through two 16-bit adds:

```asm
h_add:
        ; decode rd/rs from unit0's high byte and rt from unit1 into DP offsets
        lda   Rs_lo
        clc
        adc   Rt_lo
        sta   Rd_lo
        lda   Rs_hi
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

Estimated \~60–90 cycles including decode and dispatch. The breakdown matters for DN-SW-VMISA-001's encoding candidates: \~18 cycles dispatch, \~25 nibble extraction, \~20 useful work, \~12 PC advance.

32-bit load with displacement, using long indirect indexed addressing so guest pointers are usable directly:

```asm
h_ldw:
        ; EA = Rs + sign_extend(disp16), 24-bit
        ; validate high byte == 0, else ADDR_FAULT
        lda   [EA]          ; ~7c: low 16 bits, 65816 handles the bank carry
        sta   Rd_lo
        ldy   #2
        lda   [EA],y        ; ~7c: high 16 bits
        sta   Rd_hi
        ; advance IP; jmp NEXT
```

Estimated \~90–120 cycles.

Signed compare-and-branch:

```asm
h_blt:
        lda   Rs_lo
        cmp   Rt_lo
        lda   Rs_hi
        sbc   Rt_hi         ; 32-bit signed compare: SBC low then high
        bvc   :+
        eor   #$8000        ; correct the comparison on overflow
:       bmi   taken
        jmp   NEXT
taken:  ; IP += sign_extend(disp16); renormalize bank; jmp NEXT
```

Estimated \~40–70 cycles. `CALL` and `RET` manipulate LR and the PC in DP, \~25–40 cycles.

**Block move maps to `MVN`/`MVP`:** X = source low 16, Y = destination low 16, C = count − 1, banks as operands. At 7 cycles per byte this is the fastest copy the machine has, and it is natively interruptible and restartable with progress in X, Y and C — exactly what the ISA's restart rule requires. Cross-bank copies are segmented per 64 KB bank, which is the one place where interpreter and softcore implementations differ most; the conformance suite must check write order and restart behaviour explicitly.

## Throughput

At 8 MHz and \~60–120 cycles per typical VM instruction, steady-state throughput is estimated at **150,000–400,000 VM-instructions per second** for an ALU and branch weighted mix, lower for load/store-heavy code. That is roughly **20–50× slower than hand-written native 65816**, consistent with SWEET16's \~10× for a 16-bit VM, scaled for 32-bit width and richer decode.

**These are engineering estimates, not measurements**, and they exclude PHI2 stall time. Because the platform stops the clock on a cache miss rather than using wait states, cycle counts do not measure wall-clock time: whenever the working set spills the 1 MB SRAM cache, memory latency dominates dispatch and real throughput drops below these figures.

Validating this is the first job of DN-SW-VMBENCH-001, and the abandonment threshold below depends on it.

## The gate: native code entering the VM

The kernel reaches bytecode through **one indirect entry point resolved at boot**, so the same kernel source runs on the physical 65816 and on a core that executes NVM32 natively. Everything above the gate and everything below it is identical; only a 3-byte vector changes.

Do not implement this with self-modifying code. A pointer in data avoids the instruction-cache coherence hazard that patching would create on Argon (DN-HW-ARGON-001, H1).

```asm
; --- PCB / state window, bank $00 (reserved host region) --------
PCB_R0      = $00       ; R0..R15 = $00..$3F, little-endian, 4 B each
PCB_IP      = $40       ; VM PC, 16-bit offset
PCB_IPBANK  = $42
PCB_CAUSE   = $43
PCB_NUM     = $44
PCB_KS      = $46       ; kernel stack pointer, saved on entry

; --- system information block, filled once by the BIOS ----------
SYSINFO     = $F800     ; last 2 KB page of bank $00, shared, read-only
SI_VERSION  = $00       ; block format version
SI_CPUID    = $02       ; 0 = W65C816S, otherwise the Argon signature
SI_CAPS     = $04       ; copy of Helium's capability register
SI_GATE     = $06       ; 24-bit pointer to the selected gate
```

The kernel side, identical on both machines:

```asm
; jsl vm_run:  X = PCB address
;              returns with PCB_CAUSE / PCB_NUM set and the window updated
vm_run:
        jml     [SYSINFO+SI_GATE]   ; the target ends in RTL and returns to the JSL

run_program:
        ldx     #proc0_pcb
        jsl     vm_run          ; runs bytecode until it traps
        lda     f:proc0_pcb+PCB_CAUSE
        cmp     #CAUSE_SYSCALL
        bne     do_fault
        jsr     marshal_and_cop
        ldx     #proc0_pcb
        jsl     vm_run          ; resume at the already-advanced IP
```

`JML [abs]` reads its 24-bit pointer from bank `$00`, which is where the reserved region lives. The return address was pushed by the caller's `JSL`, so each implementation only has to end in `RTL`.

**Implementation A — physical 65816:**

```asm
vm_gate_interp:
        php
        rep     #$30
        phb
        phd
        tsc
        sta     PCB_KS,x        ; kernel stack
        txa
        tcd                     ; D -> process window
        sep     #$20
        lda     PCB_IPBANK
        pha
        plb                     ; DBR = bytecode's code bank
        rep     #$20
        jmp     NEXT
; the interpreter's common exit restores S/D/DBR/P and does RTL
```

**Implementation B — a core with native NVM32 mode:**

```asm
vm_gate_native:
        php
        rep     #$30
        phb
        phd
        wdm     #WDM_VMENTER    ; X = PCB: load {R0..R15,PC}, switch mode
        ; control returns HERE when the VM traps; cause already in the PCB
        pld
        plb
        plp
        rtl
```

`WDM` (`$42`) is the opcode WDC reserved for expansion: on a real 65816 it is a two-byte no-op, so using it as the NVM32 mode entry costs nothing and does not violate the instruction-compatibility contract.

**Implementation C — physical 65816 with the FPGA-C coprocessor (DN-HW-VMCORE-001):**

```asm
vm_gate_copro:
        php
        rep     #$30
        phb
        phd
        txa
        sta     f:COPRO_PCB     ; which state window the core loads
        lda     #COPRO_GO
        sta     f:COPRO_CTRL    ; release the core
@wait:  wai                     ; sleep until the core's halt interrupt
        lda     f:COPRO_STAT
        and     #COPRO_HALTED
        beq     @wait
        pld
        plb
        plp
        rtl
```

This synchronous form is the simplest correct one and has the same signature as A and B. It wastes the host while the core runs; the scheduler-aware form returns at once with the process marked running on the core, and the scheduler blocks it until the halt interrupt arrives. Which form ships is a kernel decision, invisible to the bytecode.

### Selecting the gate at boot

The gate target depends on two facts that live in different places, so each comes from where it is known:

- **Which CPU is executing — from the CPU itself.** A `WDM #WDM_CPUID` probe: on a real 65816 `WDM` is a guaranteed no-op, so A is unchanged; on Argon it loads a non-zero signature. The CPU cannot report wrongly, and nothing depends on a register staying in step with a bitstream.
- **What surrounds the CPU — from Helium.** Whether FPGA-C carries an NVM32 core, the MMU version, cache size. Helium cannot see electrically whether a physical 65816 or Argon is on the bus, but it knows everything outside the CPU.

| CPU (`WDM` probe) | FPGA-C (Helium capability) | Gate |
| --- | --- | --- |
| W65C816S | Absent | A — interpreter |
| W65C816S | Present | C — coprocessor |
| Argon | — | B — native `WDM #VMENTER` |

```asm
; BIOS, before paging is enabled
sys_probe:
        rep     #$30
        lda     #$0000
        wdm     #WDM_CPUID          ; real 65816: 2-byte NOP, A unchanged
        sta     f:SYSINFO+SI_CPUID  ; 0 = W65C816S, else Argon signature
        bne     @argon
        lda     f:HELIUM_CAPS
        sta     f:SYSINFO+SI_CAPS
        and     #CAP_FPGAC_NVM32
        bne     @copro
        lda     #.loword(vm_gate_interp)
        bra     @set
@copro: lda     #.loword(vm_gate_copro)
        bra     @set
@argon: lda     f:HELIUM_CAPS
        sta     f:SYSINFO+SI_CAPS
        lda     #.loword(vm_gate_native)
@set:   sta     f:SYSINFO+SI_GATE
        sep     #$20
        lda     #^vm_gate_interp    ; all three gates live in the interpreter bank
        sta     f:SYSINFO+SI_GATE+2
        rep     #$20
        rts
```

**The BIOS writes, not Helium.** Helium could technically write the vector — the Debug Agent already writes memory to load `bios.bin` — but that would couple gateware to the software memory layout, so moving the vector would mean a new bitstream. Hardware supplies facts; software decides policy.

**The system information block is the BIOS-to-kernel contract.** It carries the CPU identity, Helium's capabilities, FPGA-C presence and the resolved gate, and it has a version field. The kernel needs it beyond the gate — what to save on a context switch, whether NVM32 processes can be dispatched to a coprocessor. All detection happens in one place, once, at boot. BIOS implementations may differ between machines; the block's format may not.

**Where it lives.** Every process has its own virtual bank `$00`, so the block sits in one shared read-only page — the last 2 KB page of bank `$00` — written by the BIOS before paging is enabled and mapped identically into every process, like the interpreter code in bank `$01`. `JML [abs]` reads its pointer from bank `$00`, so the gate call works unchanged.

**Emulator requirement.** DN-SW-EMU-001 must treat `WDM` as a two-byte no-op when modelling noVa64. Several 65816 emulators use it as a debugger breakpoint; if ours did, the probe would detect an Argon that does not exist.

Three properties to keep:

- **Entry is a call, not a jump.** `WDM #VMENTER` blocks: the core saves the 65816-mode PC, executes bytecode, and on a trap returns to the following instruction. It is the exact analogue of the interpreter's `JMP NEXT … RTL`, which is what lets the gate have one signature.
- **The two register sets do not coexist.** Entering NVM32 mode leaves A, X, Y, D and DBR undefined on return, which is why the gate saves them. Consistent with NVM32 state being only {R0–R15, PC}.
- **The bytecode does not change by one code unit.** It leaves through `SYSCALL`, and the marshalling code is the same on both machines because it reads the PCB, not the hardware.

The handler side, and the common exit:

```asm
h_syscall:
        lda     (IP)            ; unit0 = opcode:8 | number:8
        xba
        and     #$00FF
        sta     PCB_NUM,x
        inc     IP              ; IP += 2, so we resume AFTER the SYSCALL
        inc     IP
        bne     :+
        inc     IPBANK
:       lda     #CAUSE_SYSCALL
        sta     PCB_CAUSE,x
        ; falls through to the common exit, which restores S/D/DBR/P and RTLs
```

Faults reach the same exit **without advancing IP**, so the instruction re-executes once the fault is serviced.

## Interrupts and preemption

**NVM32 has no interrupts.** Architectural state is {R0–R15, PC} and nothing else; interrupts are a host phenomenon and the bytecode never sees them.

**Correction to the original report.** It claimed the interpreter is preemptible "because the host timer IRQ is honoured only between handler dispatches". That is false: the 65816 accepts an IRQ at any *host* instruction boundary, including between the low and high `ADC` of a 32-bit add. What actually makes preemption safe is that the partial state of a VM instruction lives in A, X, Y, P and the process's direct page, so a context switch that saves A, X, Y, P, D, DBR, PBR, S and PC implicitly saves the half-finished VM instruction; on resume the handler continues where it was. **No VM-level atomicity and no polling in the dispatch loop are required.** VM-instruction atomicity is required for faults and for exporting state — not for preemption.

**Context switches are cheap.** An interpreted process costs no more to switch than a native one: the 16 VM registers are never saved, they stay in their direct-page block, so a switch is D, S and the ASID. Cheaper than a real register file.

**Reaching a VM instruction boundary** is needed in three cases: migrating to the softcore, showing {R0–R15, PC} to a debugger, and delivering anything asynchronous to the bytecode itself.

| Mechanism | Fast-path cost | Notes |
| --- | --- | --- |
| Flag tested in `NEXT` | \~5–6 cycles of every 60–120, a permanent \~5% tax | Simple, but paid hundreds of thousands of times a second for a rare event |
| **Shadow dispatch bank** | Zero | A second copy of the interpreter in another program bank whose 256 `OPTAB` entries all point at the exit handler. The kernel switches the process's saved PBR; the running handler finishes, reaches `NEXT`, and lands in the kernel at the next exact boundary. With the MMU the code pages are physically shared and only the table page differs |

Recommended: the shadow bank, precisely because the event is rare.

**Kernel requirements.** The IRQ is taken on the interpreted process's own stack and direct page, since the 65816 pushes PB, PC and P to bank `$00` before the handler runs. Therefore:

- The reserved host region — the process's 65816 DP and stack — must be resident and guarded. A page fault while pushing the interrupt frame is not a debuggable situation.
- The handler may assume nothing about D, DBR or the M/X widths; it enters with whatever the interpreter had. Set widths explicitly and load the kernel's own D before touching anything.
- The interpreter keeps DBR pointing at the bytecode's code bank; kernel code relying on DBR must set it.
- Trap cause and service number live **in the process's PCB**, never in kernel globals, or two processes overwrite each other.

**Latency.** No critical sections are needed, so no `SEI` anywhere in the dispatch loop. `MVN`/`MVP` is interruptible per byte, so a 64 KB copy delays an interrupt by at most 7 cycles. Worst-case latency is set by PHI2 stretching: during a cache miss the clock is stopped and no IRQ is taken. The scheduler quantum must come from the fixed frequency reference, never from counting VM instructions or cycles. Direct cost is negligible — a 100 Hz tick with a 200-cycle handler is 0.25% of an 8 MHz budget; switches and cache pollution dominate.

**Symmetry with the softcore.** Device interrupts belong to the 65816; the softcore is only stopped and resumed. Both executors share one rule — the outside world interrupts the host, never the VM, and state is exported only at instruction boundaries — which is what makes mid-execution migration possible.

## System interaction

**Page faults.** A guest access that misses triggers Helium's hardware page-table walk; a bounded fill stalls PHI2 transparently and the interpreter never notices. Unmapped, permission and copy-on-write cases assert ABORTB. Because the VM commit happens only after the access, once the kernel services the fault the handler simply re-executes from the same VM PC. The commit discipline is therefore not extra work on this target — ABORTB provides it, as long as handlers are written in the order: compute effective address, perform the access, then write the destination and advance the PC.

**Syscall forwarding.** `SYSCALL` stores cause and number into the PCB and leaves through the common exit; the kernel then validates the service number, marshals and validates pointer arguments, and issues `COP #sig` against its existing native syscall entry. Pointer validation is the only barrier between a guest pointer and the reserved host region: high byte zero, inside the guest data or stack regions, correct permission.

**Re-entrancy limit.** `vm_run` is not re-entrant for the same process. A syscall that itself needs to execute bytecode — an `exec`, for example — needs another PCB; it must not nest.

## Optimizations, abandonment conditions and open questions

| Optimization | Benefit | Cost | Verdict |
| --- | --- | --- | --- |
| Threaded dispatch — jump to `NEXT`, never return | \~12 cycles per instruction | Trivial | **Adopt** |
| 16-bit default mode, avoiding `REP`/`SEP` churn | Large | Trivial | **Adopt** |
| Superinstructions: compare-and-branch, `ENTER`/`LEAVE`, FP-relative load/store | Fewer dispatches on hot paths | Small, already in the opcode map | **Adopt** |
| Shadow dispatch bank for instruction boundaries | Removes a \~5% polling tax | Small, one extra bank | **Adopt** |
| Helium memory-mapped 32×32 multiply and divide helper | Removes hundreds of cycles per operation | Gateware plus a specification boundary | **Recommend** — Helium owns the bus, and it is shareable with the softcore |
| Load-time pre-decode to a threaded or token image | \~1.5–2× dispatch speedup | 1.5–2× RAM per process; breaks read-only sharing | Defer, behind a flag |
| Helium decode helper | Marginal | Gateware complexity | **Reject** |

Abandonment conditions:

- If measured throughput is below \~100,000 VM-instructions per second on representative code after the adopted optimizations and the multiply/divide helper, reconsider load-time pre-decode or a change of native format (DN-SW-VMISA-001).
- If the shadow dispatch bank cannot be mapped cheaply by the MMU, fall back to the polled flag and accept the \~5% tax.

Open questions:

- [ ] Encoding of Helium's capability register and of the system information block, including its version field. Owned by the BIOS note; the gate selection above depends on it.
- [ ] Does `WDM #VMENTER` need an argument beyond X = PCB, for example an ASID or a step count?
- [ ] Is the shadow-bank exit handler shared with the syscall exit, or separate?
- [ ] Calibrated cycle costs per handler, to replace the estimates here (DN-SW-VMBENCH-001).
