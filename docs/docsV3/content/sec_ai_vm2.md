# NVM32 — the 65816 interpreter
> sixteen registers in the direct page · the dispatch loop · one gate, three machines

This sheet specifies the interpreter that executes NVM32 on the physical W65C816S at 8 MHz, the gate between native and bytecode code, and the interrupt and fault behaviour that follows from both. The ISA is [sheet VM1](sec_ai_vm1); the core that runs the same binaries in gateware is [sheet VM3](sec_ai_vm3); the harness that replaces the cycle estimates below with measurements is [sheet VM4](sec_ai_vm4).

- VM2.1 — **Deferred with the rest of the VM programme** ([VM1.2](sec_ai_vm1#vm12)). Implementation begins after the C11 reference interpreter exists and the kernel runs its first user process. The design is written down now so that reopening is implementation, not redesign.
- VM2.2 — **Interpreter speed is priority #1 for the whole programme**, which is why the encoding of [sheet VM1](sec_ai_vm1) is co-designed with this dispatch loop rather than handed to it. Every encoding rule in [VM1.25](sec_ai_vm1#vm125) exists to make some part of this sheet shorter.
- VM2.3 — **Bit-identical semantics with the other executors, verified by differential testing against the C11 reference as golden model** — the pattern already proven on the file system ([Y2.22](sec_ai_y2#y222)). The interpreter is never its own oracle.

## Interpreter architecture — the direct page is the register file.

- VM2.4 — **R0–R15 occupy 64 contiguous direct-page bytes, 4 bytes each, little-endian.** With D pointing at that block, a VM register is `dp,X` with X = register × 4 — **the fastest addressing the machine has**, and the reason the encoding pre-doubles the register nibbles ([VM1.25](sec_ai_vm1#vm125)). The kernel points D at the process's block; the interpreter itself is re-entrant, read-only and shared by every process.
- VM2.5 — **The PC is a 24-bit guest pointer held as a 16-bit offset plus a bank byte in the direct page.** Fetch keeps the code bank in DBR and uses offset addressing; after each fetch the instruction length is added to the offset, and when it crosses a 16-bit boundary the carry increments the bank byte. **Straight-line code therefore pays an increment plus a rarely-taken branch**, and the bank arithmetic costs nothing on the common path.
- VM2.6 — **Native mode with M = 0 and X = 0 by default**, because every 32-bit VM operation is two 16-bit operations. Handlers that touch bytes toggle M locally and restore it.
  NOTE: **Minimising `REP`/`SEP` churn is the single most important micro-optimization in this sheet.** The dispatch loop and every 16- and 32-bit handler stay in 16-bit mode throughout; a handler that switches width twice has spent more on the switch than on the work.
- VM2.7 — **The dispatch table and every handler live in one program bank — bank `$01` of the reserved host region — because `JMP (addr,X)` resolves its indirect address in the current program bank.** The interpreter fits within 64 KB, and handlers end by jumping back to the dispatcher rather than returning, which removes the stack churn a `JSR`/`RTS` shape would pay on every instruction.
- VM2.8 — **The process state window is one structure with two writers.** R0–R15 at offset 0, then the VM PC offset and bank, the trap cause and service number, and the saved kernel stack pointer. On the 65816 it *is* the interpreter's direct page; on the core it is the window the hardware reads and writes ([VM3.14](sec_ai_vm3#vm314)). **Same layout, two writers** — which is what makes a process migratable between them.
  NOTE: It sits inside the process PCB ([N.1](sec_ai_n#n1)) rather than beside it, so a context switch moves one pointer and the VM registers are never copied ([VM2.30](sec_ai_vm2#vm230)).

## The dispatch loop and its handlers — ca65, 16-bit A and X, D at the register window.

Cycle figures throughout are datasheet estimates to be calibrated by [sheet VM4](sec_ai_vm4), not measurements.

- VM2.9 — **The whole dispatcher is five instructions**, and `IP` is the VM PC in the direct page:

```asm
; --- main dispatch: fetch opcode unit, jump through the 256-entry table ---
NEXT:
        lda   (IP)          ; ~6c: fetch unit0 (opcode:8 | fields:8)
        and   #$00FF        ; ~2c: isolate the opcode byte
        asl   a             ; ~2c: x2 for a word table index
        tax                 ; ~2c
        jmp   (OPTAB,x)     ; ~6c: dispatch, table in this program bank
; OPTAB: .addr h_nop, h_add, ...   (256 entries)
```

- VM2.10 — **Three-address `ADD`, 32 bits as two 16-bit adds**, and the cycle breakdown is the number the whole encoding argument turns on:

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

  NOTE: **~60–90 cycles including decode and dispatch: ~18 dispatch, ~25 nibble extraction, ~20 useful work, ~12 PC advance.** That is [VM1.45](sec_ai_vm1#vm145)'s number, and it is where every encoding candidate has to find its gain.
- VM2.11 — **A 32-bit load uses long indirect indexed addressing, so guest pointers are usable directly** and the 65816 handles the bank carry itself. Estimated ~90–120 cycles:

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

- VM2.12 — **A signed 32-bit compare-and-branch is `SBC` low then high, with the overflow correction the 65816 does not do for you.** Estimated ~40–70 cycles; `CALL` and `RET` manipulate LR and the PC in the direct page at ~25–40:

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

- VM2.13 — [[!blocking]] **`MEMCPY` maps onto `MVN`/`MVP` on paper and [E.21](sec_ai_e#e21) forbids exactly that, so one of the two has to give.** The block move is the fastest copy the machine has — 7 cycles per byte, X = source low 16, Y = destination low 16, C = count − 1, banks as operands — and it is natively interruptible and restartable with progress in X, Y and C, which is precisely what [VM1.33](sec_ai_vm1#vm133)'s restart rule asks for. **But guest memory is pageable by definition**, and [E.21](sec_ai_e#e21) bans `MVN`/`MVP` against pageable memory categorically, because a move straddles many pages and its per-byte state sits inside [E.18](sec_ai_e#e18)'s corruption windows.
  NOTE: **The three ways out, none of them chosen here.** Implement `MEMCPY` as a software loop of ordinary loads and stores, each individually restartable — safe, and several times slower, which costs the block operations their reason to exist. Or pin the source and destination pages for the duration, which turns a guest instruction into a kernel call. Or narrow [E.21](sec_ai_e#e21) to exclude a move whose restart state the VM already guarantees. **The decision belongs to whoever owns [E.21](sec_ai_e#e21), not to this sheet** (→ [Q169](sec_ai_q#q169)).
  NOTE: Cross-bank copies are segmented per 64 KB bank either way, and that is the one place where interpreter and core differ most — the core has a flat 24-bit datapath. **Write order and restart behaviour must be checked explicitly in the conformance suite** ([VM3.22](sec_ai_vm3#vm322), → [Q160](sec_ai_q#q160)).

## Throughput — the estimate, and why it is not a time.

- VM2.14 — **At 8 MHz and ~60–120 cycles per typical VM instruction, steady-state throughput is estimated at 150,000–400,000 VM-instructions per second** for an ALU-and-branch weighted mix, lower for load- and store-heavy code. That is roughly **20–50× slower than hand-written native 65816** — consistent with SWEET16's ~10× for a 16-bit VM, scaled for 32-bit width and richer decode.
- VM2.15 — **These are engineering estimates, not measurements, and they exclude PHI2 stall time.** Because the platform stops the clock on a cache miss rather than inserting wait states ([E.4](sec_ai_e#e4), [L.15](sec_ai_l#l15)), **cycle counts do not measure wall-clock time**: whenever the working set spills the SRAM cache, memory latency dominates dispatch and real throughput falls below these figures.
  NOTE: Validating this is the first job of [sheet VM4](sec_ai_vm4), and [VM1.50](sec_ai_vm1#vm150)'s abandonment threshold depends on the answer. The caveat travels with the number wherever the number goes.

## The gate — one vector, three implementations.

- VM2.16 — **The kernel reaches bytecode through one indirect entry point resolved at boot**, so that the same kernel source runs on the physical 65816, on a machine with a coprocessor, and on a core that executes NVM32 natively. Everything above the gate and everything below it is identical; **only a 3-byte vector changes**.
- VM2.17 — **The vector is a pointer in data and never self-modifying code.** Patching would create exactly the instruction-cache coherence hazard that Argon 2's snooping exists to handle ([NV1.25](sec_ai_nv1#nv125)), and the project forbids self-modifying code in its own code for that reason ([D92](sec_ai_q#d92)).
- VM2.18 — **The state window and the system information block, both in the reserved host region:**

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
SI_CPUID    = $02       ; 0 = W65C816S, otherwise the Argon 2 signature
SI_CAPS     = $04       ; copy of Helium's capability register
SI_GATE     = $06       ; 24-bit pointer to the selected gate
```

- VM2.19 — **The kernel side is identical on every machine**, and `JML [abs]` reads its 24-bit pointer from bank `$00`, which is where the reserved region lives:

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

  NOTE: The return address was pushed by the caller's `JSL`, so each implementation only has to end in `RTL`. That is the whole of the calling contract.
- VM2.20 — **Implementation A, the physical 65816** — save the kernel's stack, point D at the process window, set DBR to the bytecode's bank, and fall into the dispatcher:

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

- VM2.21 — **Implementation B, a core with native NVM32 mode** — the same signature, with the mode switch where the dispatch loop was:

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

  NOTE: **`WDM` (`$42`) is the opcode WDC reserved for expansion, and on a real 65816 it is a two-byte no-op** — so using it as the NVM32 mode entry costs nothing and does not violate the instruction-compatibility contract ([NV1.5](sec_ai_nv1#nv15)). **This is a use of `WDM` that [M.3](sec_ai_m#m3) does not rule out and the distinction matters**: [M.3](sec_ai_m#m3) rejected having *Helium snoop* `WDM` on the bus to carry syscalls; here nothing on the bus decodes anything and the CPU itself implements the opcode ([D89](sec_ai_q#d89)).
- VM2.22 — **Implementation C, the physical 65816 with the bytecode coprocessor** ([sheet VM3](sec_ai_vm3)) — release the core, sleep until its halt interrupt:

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

  NOTE: This synchronous form is the simplest correct one and **wastes the host while the core runs**. The scheduler-aware form returns at once with the process marked running on the core, and the scheduler blocks it until the halt interrupt arrives ([N.3](sec_ai_n#n3)). Which one ships is a kernel decision and is **invisible to the bytecode**.
- VM2.23 — **Three properties hold across all three implementations, and each one is load-bearing.** **Entry is a call, not a jump** — `WDM #VMENTER` blocks, the core saves the 65816-mode PC, executes bytecode, and on a trap returns to the following instruction, which is the exact analogue of `JMP NEXT … RTL` and is what lets the gate have one signature. **The two register sets do not coexist** — entering NVM32 mode leaves A, X, Y, D and DBR undefined on return, which is why the gate saves them, and which is consistent with NVM32 state being only {R0–R15, PC}. **The bytecode does not change by one code unit** — it leaves through `SYSCALL`, and the marshalling code is the same on every machine because it reads the PCB, not the hardware.
- VM2.24 — **The emulator must model `WDM` as a two-byte no-op in noVa64 mode** ([sheet EM4](sec_ai_em4)). Several 65816 emulators use it as a debugger breakpoint; **if ours did, the probe below would detect an Argon 2 that does not exist**, and every gate selection in the emulator would be wrong in the one way that is hard to see.
  TEST: Execute `wdm #$00` in the emulator with A non-zero and confirm A, P and the cycle count are unchanged and the PC advanced by two.

### Selecting the gate at boot — each fact from where it is known.

- VM2.25 — **The gate target depends on two facts that live in different places, so each is asked of the party that can answer it.** *Which CPU is executing* comes from the CPU itself, through a `WDM #WDM_CPUID` probe: on a real 65816 `WDM` is a guaranteed no-op, so A is unchanged; on Argon 2 it loads a non-zero signature. **The CPU cannot report wrongly, and nothing depends on a register staying in step with a bitstream.** *What surrounds the CPU* comes from Helium — whether Argon carries an NVM32 core, the MMU version, the cache size — because Helium cannot see electrically which CPU is on the bus but knows everything outside it.

| CPU (`WDM` probe) | Argon (Helium capability) | Gate |
|---|---|---|
| W65C816S | Absent | A — the interpreter |
| W65C816S | Present | C — the coprocessor |
| Argon 2 | — | B — native `WDM #VMENTER` |

- VM2.26 — **The probe runs in the BIOS, before paging is enabled:**

```asm
; BIOS, before paging is enabled
sys_probe:
        rep     #$30
        lda     #$0000
        wdm     #WDM_CPUID          ; real 65816: 2-byte NOP, A unchanged
        sta     f:SYSINFO+SI_CPUID  ; 0 = W65C816S, else Argon 2 signature
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

- VM2.27 — **The BIOS writes the vector, not Helium.** Helium could technically do it — the Debug Agent already writes memory to load `bios.bin` ([D82](sec_ai_q#d82)) — but that would couple gateware to the software memory layout, so moving the vector would mean a new bitstream. **Hardware supplies facts; software decides policy.**
- VM2.28 — **The system information block is the BIOS-to-kernel contract, and it is the answer [Q5](sec_ai_q#q5) has been waiting for.** It carries the CPU identity, Helium's capabilities, coprocessor presence and the resolved gate, and it has a version field. The kernel needs it beyond the gate — what to save on a context switch, whether NVM32 processes can be dispatched to a core — and all detection happens **in one place, once, at boot** ([J.2](sec_ai_j#j2), [I.2](sec_ai_i#i2)).
  NOTE: **BIOS implementations may differ between machines; the block's format may not** ([NV1.11](sec_ai_nv1#nv111)). That asymmetry is the whole point of having a block rather than a convention.
- VM2.29 — **It lives in one shared read-only page — the last 2 KB page of bank `$00`** — written by the BIOS before paging is enabled and mapped identically into every process, exactly as the interpreter code in bank `$01` is. `JML [abs]` reads its pointer from bank `$00`, so the gate call works unchanged in every process ([L.11](sec_ai_l#l11), [D91](sec_ai_q#d91)).

## Interrupts and preemption — and a correction to the original rationale.

- VM2.30 — **NVM32 has no interrupts.** Architectural state is {R0–R15, PC} and nothing else; interrupts are a host phenomenon and the bytecode never sees one.
- VM2.31 — **The original report's reason for preemptibility was wrong, and the correction is recorded rather than quietly replaced.** It claimed the interpreter is preemptible "because the host timer IRQ is honoured only between handler dispatches". **That is false**: the 65816 accepts an IRQ at any *host* instruction boundary, including between the low and high `ADC` of a 32-bit add. **What actually makes preemption safe is that the partial state of a VM instruction lives in A, X, Y, P and the process's direct page**, so a context switch that saves A, X, Y, P, D, DBR, PBR, S and PC implicitly saves the half-finished VM instruction; on resume the handler continues where it was.
  NOTE: **No VM-level atomicity and no polling in the dispatch loop are required.** VM-instruction atomicity is required for faults and for exporting state — not for preemption. The wrong reason matters because it leads directly to a needless polling loop in `NEXT`, at ~5% of every instruction forever.
- VM2.32 — **Context switches are cheap, and an interpreted process costs no more to switch than a native one.** The sixteen VM registers are never saved — they stay in their direct-page block — so a switch is D, S and the ASID ([N.2](sec_ai_n#n2)). **Cheaper than a real register file.**
- VM2.33 — **Reaching a VM instruction boundary is needed in exactly three cases**: migrating a process to the core, showing {R0–R15, PC} to a debugger, and delivering anything asynchronous to the bytecode itself. Two mechanisms, and the cheap-looking one is the expensive one:

| Mechanism | Fast-path cost | Notes |
|---|---|---|
| A flag tested in `NEXT` | ~5–6 cycles of every 60–120, a permanent ~5% tax | Simple, but paid hundreds of thousands of times a second for a rare event |
| **A shadow dispatch bank** | Zero | A second copy of the interpreter in another program bank whose 256 `OPTAB` entries all point at the exit handler. The kernel switches the process's saved PBR; the running handler finishes, reaches `NEXT`, and lands in the kernel at the next exact boundary. With the MMU the code pages are physically shared and only the table page differs |

  NOTE: **The shadow bank is adopted precisely because the event is rare.** Paying zero on the common path and one bank of address space for the uncommon one is the right trade at these frequencies.
- VM2.34 — **Four requirements land on the kernel, because the IRQ is taken on the interpreted process's own stack and direct page** — the 65816 pushes PB, PC and P to bank `$00` before the handler runs ([E.12](sec_ai_e#e12)).
  1. The reserved host region — the process's 65816 direct page and stack — is resident and pinned. **A page fault while pushing the interrupt frame is not a debuggable situation** ([E.20](sec_ai_e#e20), [L.11](sec_ai_l#l11)).
  2. The handler assumes nothing about D, DBR or the M/X widths; it enters with whatever the interpreter had. Set the widths explicitly and load the kernel's own D before touching anything.
  3. The interpreter keeps DBR pointing at the bytecode's code bank, so kernel code relying on DBR sets it.
  4. Trap cause and service number live **in the process's PCB**, never in kernel globals, or two processes overwrite each other.
- VM2.35 — **No critical sections are needed, so there is no `SEI` anywhere in the dispatch loop.** `MVN`/`MVP` is interruptible per byte, so even a 64 KB copy delays an interrupt by at most 7 cycles. **Worst-case latency is set by PHI2 stretching**, not by the interpreter: during a cache miss the clock is stopped and no IRQ is taken.
  NOTE: The scheduler quantum therefore comes from the fixed frequency reference, never from counting VM instructions or cycles ([D92](sec_ai_q#d92), [L.15](sec_ai_l#l15)). Direct cost is negligible — a 100 Hz tick with a 200-cycle handler is 0.25% of an 8 MHz budget ([N.3](sec_ai_n#n3)) — and switches and cache pollution dominate.
- VM2.36 — **One rule is shared with the core: the outside world interrupts the host, never the VM, and state is exported only at instruction boundaries.** Device interrupts belong to the 65816; a core is stopped and resumed, never interrupted ([VM3.18](sec_ai_vm3#vm318)). That symmetry is what makes mid-execution migration possible at all.

## System interaction — faults, syscalls, and one thing that must not nest.

- VM2.37 — **A guest access that misses triggers Helium's hardware page-table walk, and a bounded fill stalls PHI2 transparently** — the interpreter never notices ([L.8](sec_ai_l#l8), [F.7](sec_ai_f#f7)). Unmapped, permission and copy-on-write cases assert `ABORTB` ([L.4](sec_ai_l#l4)). Because the VM commits only after the access, once the kernel services the fault the handler simply re-executes from the same VM PC.
  NOTE: **The commit discipline is therefore not extra work on this target — `ABORTB` provides it** ([E.16](sec_ai_e#e16)) — as long as handlers are written in the order: compute the effective address, perform the access, then write the destination and advance the PC ([VM1.34](sec_ai_vm1#vm134)).
- VM2.38 — **`SYSCALL` stores cause and number into the PCB and leaves through the common exit**, after which the kernel validates, marshals and issues `COP #sig` against its existing native entry ([VM1.39](sec_ai_vm1#vm139), [J.3](sec_ai_j#j3)). Faults reach the same exit **without advancing IP**, so the instruction re-executes once the fault is serviced:

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

- VM2.39 — **Pointer validation is the only barrier between a guest pointer and the reserved host region**, and it is three tests: high byte zero, inside the guest data or stack region, correct permission. There is no fourth test to add later — this is the boundary.
- VM2.40 — **`vm_run` is not re-entrant for the same process.** A syscall that itself needs to execute bytecode — a `spawn`, for instance ([N.6](sec_ai_n#n6)) — needs another PCB. **It must not nest.**

## Optimizations, abandonment conditions and what is left open.

- VM2.41 — **The verdicts, decided on cost rather than on appetite:**

| Optimization | Benefit | Cost | Verdict |
|---|---|---|---|
| Threaded dispatch — jump to `NEXT`, never return | ~12 cycles per instruction | Trivial | **Adopt** |
| 16-bit default mode, avoiding `REP`/`SEP` churn | Large | Trivial | **Adopt** |
| Superinstructions: compare-and-branch, `ENTER`/`LEAVE`, FP-relative load and store | Fewer dispatches on hot paths | Small, already in the opcode map | **Adopt** |
| Shadow dispatch bank for instruction boundaries | Removes a ~5% polling tax | Small, one extra bank | **Adopt** |
| Helium memory-mapped 32×32 multiply and divide helper | Removes hundreds of cycles per operation | Gateware plus a specification boundary | **Recommend** — Helium owns the bus, and it is shareable with the core |
| Load-time pre-decode to a threaded or token image | ~1.5–2× dispatch speedup | 1.5–2× RAM per process; breaks read-only sharing | Defer, behind a flag |
| A Helium decode helper | Marginal | Gateware complexity | **Reject** |

- VM2.42 — **If measured throughput is below ~100,000 VM-instructions per second on representative code** after the adopted optimizations and the multiply and divide helper, reconsider load-time pre-decode or a change of native format ([VM1.50](sec_ai_vm1#vm150)).
- VM2.43 — **If the shadow dispatch bank cannot be mapped cheaply by the MMU, fall back to the polled flag and accept the ~5% tax.** It is a worse design and a working one.
- VM2.44 — **Four things stay open here**: the encoding of Helium's capability register and of the system information block including its version field, which the gate selection depends on and which the BIOS owns (→ [Q168](sec_ai_q#q168)); whether `WDM #VMENTER` needs an argument beyond X = PCB, such as an ASID or a step count; whether the shadow-bank exit handler is shared with the syscall exit or kept separate; and the calibrated per-handler cycle costs that replace every estimate in this sheet ([sheet VM4](sec_ai_vm4)).
