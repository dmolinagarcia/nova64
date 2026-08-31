# The ABORTB Pin of the WDC W65C816S: A Technical Analysis for Demand-Paged Virtual Memory (noVa64)

## TL;DR
- ABORTB can support demand paging and copy-on-write on the W65C816S **only if the MMU asserts it in the same PHI2 cycle the faulting address becomes valid AND gates the memory write-enable so the faulting store never commits** — the CPU does not roll back a write that has already driven the bus, so the hardware must prevent it.
- The mechanism has hard, documented limits in WDC's own datasheet Caveat §8.4: aborting *after* the modify cycle of a read-modify-write instruction, after cycle 3 of RTI, or after cycle 2 of an interrupt/BRK/COP sequence **corrupts P, PBR and/or DBR** — so abort is *restartable* but not perfectly *precise* on every instruction, and the stack, direct page, vectors and handler must always be resident.
- For noVa64 the division of labor you already chose is correct: PHI2-stall for bounded cache fills, ABORTB for unbounded/exceptional faults — but you must pin the stack, direct page, interrupt vectors and the abort handler, forbid MVN/MVP on pageable memory, and treat the abort decision as a same-cycle combinational function of the translated address, guarded so it never fires during a vector-pull cycle.

## Key Findings

1. **ABORTB has no dedicated timing symbol.** It is governed by the generic *Processor Control* setup/hold parameters shared with IRQB/NMIB/RESB/RDY. Per the WDC W65C816S Datasheet (March 13, 2024), p.28: **"tPCS Processor Control Setup Time 10 … tPCH Processor Control Hold Time 10 … nS"** in the 5 V/14 MHz column, relaxing to "tPCS … 15 / tPCH … 10" at the next (8 MHz/3.3 V) grade. At 14 MHz a PHI2 cycle is ≈71 ns, so the MMU must translate the address and reach an abort decision within roughly half a cycle.

2. **The datasheet describes ABORTB as a pulse/level-sensitive input:** "an abort will occur whenever there is a negative pulse (or level) on the ABORTB pin during a PHI2 clock." It must be asserted only after VDA/VPA indicate a valid address, held until PHI2 falls, then released; it must not be held longer than one cycle.

3. **The abort sequence is register-preserving in the good case:** the current instruction runs to completion but *no registers or memory are changed*; then PB, the **aborted instruction's own address** (MSB then LSB), and SR are pushed; I is set, D is cleared, PB←$00, and PC is loaded from the abort vector. Because the *instruction's own address* (not the following instruction) is stacked, RTI re-executes the faulting instruction — this is what makes demand paging possible. Per BCS Technology's "Investigating 65C816 Interrupts" (6502.org): **"the 65C816 does not automatically save .A, .B, .X, .Y, DB and DP, nor does it change any bits in SR except D and I"** — the handler must preserve those itself.

4. **Vectors (verified verbatim against the WDC datasheet):** "The Abort vector address is 00FFF8,9 (Emulation mode) or 00FFE8,9 (Native mode)." So native-mode ABORT = **$00FFE8/$00FFE9**; emulation-mode ABORT = **$00FFF8/$00FFF9**.

5. **Interrupt priority is RESB > ABORTB > NMIB > IRQB.** Per BigDumbDinosaur (BCS Technology): "if…ABORTB and NMIB are simultaneously asserted, the 65C816 will respond to ABORTB and then upon completion of the abort interrupt service routine, will respond to NMIB, assuming that NMIB is still asserted." ABORT is non-maskable.

6. **Writes are NOT undone.** An abort discards *internal* computational results, but any bus write cycle the instruction already performed has physically happened. The MMU must gate write-enable in the aborting cycle.

## Details

### 1. Functional description / how it works

**Electrical / timing.** ABORTB is an active-low input. WDC gives it no unique AC parameter; it shares the *Processor Control Setup Time* **tPCS = 10 ns (min)** and *Processor Control Hold Time* **tPCH = 10 ns (min)** with IRQB, NMIB, RESB and RDY at the 5 V/14 MHz grade (tPCS relaxes to 15 ns at 3.3 V/8 MHz). Community bench experience on 6502.org states that on current WDC silicon essentially everything, including ABORT, is sampled on the falling edge of PHI2, and that the '816 demands a PHI2 slew of 5 ns or better. Practically, system logic must (a) wait for VDA and/or VPA to signal a valid memory cycle, (b) decode/translate the address, and (c) pull ABORTB low with ≥10 ns setup before the PHI2 fall, holding through the fall and releasing immediately. Per Wikipedia's "Interrupts in 65xx processors" (summarizing Eyes & Lichty): **"the logic must not assert ABORTB until the processor has asserted the VDA or VPA signals. Also, ABORTB must remain asserted until the fall of the phase-two clock and then be immediately released."** WDC Caveat §8.4.1: "ABORTB should be held low for a period not to exceed one cycle. Also, if ABORTB is held low during the Abort Interrupt sequence, the Abort Interrupt will be aborted… The ABORTB internal latch is cleared during the second cycle of the Abort Interrupt."

**Abort sequence (BCS Technology / WDC).** On a correctly-timed abort: (1) all steps of the current instruction complete but no changes are made to registers or memory; (2) PB is pushed; (3) the aborted instruction's address is pushed MSB-first then LSB; (4) SR is pushed; (5) I set; (6) D cleared; (7) PB←$00; (8) PC ← abort vector; (9) execution transfers to the handler. RTI reverses this and returns *to the aborted instruction* (unless the handler rewrites the stacked address), so the instruction is retried after the fault is resolved.

**VPB / VDA / VPA interaction.** VDA and VPA are the "valid data address" / "valid program address" status outputs; they read `00` during internal (dead) cycles, so the MMU must only consider asserting ABORTB when VDA or VPA is high (a real bus access). VPB (vector pull) goes low during the two vector-fetch cycles of the abort sequence; system logic can watch VPB both to redirect the vector on the fly and to *count* vector pulls as a sanity check that the abort was actually taken — and, crucially, to inhibit any further abort while VPB is low.

**Writes already committed.** Wikipedia's 65xx interrupt article and the 6502.org memory-protection threads make the same point: the abort suppresses internal register/memory *result commitment*, but a store cycle that already drove the bus with RWB low has physically written. For a store that faults, the MMU must be the thing that both asserts ABORTB and de-asserts the memory's write-enable in that same cycle. This is the central hardware invariant for copy-on-write.

### 2. Known bugs, limitations and errata — "does it always work?"

The authoritative source is **WDC's own datasheet Caveat §8.4**, which explicitly enumerates the cases where an abort *does* modify state:

- **§8.4.1.1 Read-Modify-Write (ASL, LSR, ROL, ROR, INC, DEC, TSB, TRB):** "Processor status modified if ABORTB is asserted after a modify cycle." Worse, the RMW *write* to the target has already occurred by then — so a late abort on an RMW both corrupts P and leaves a partial write in memory.
- **§8.4.1.2 RTI:** "Processor status modified if ABORTB is asserted after cycle 3."
- **§8.4.1.3 IRQB, NMIB, ABORTB, BRK, COP:** verbatim, **"When ABORTB is asserted after cycle 2, PBR and DBR will become 00 (Emulation mode) or PBR will become 00 (Native mode)."** I.e. aborting *during an interrupt/trap sequence itself* silently corrupts the bank registers.
- **§8.4.2:** "The ABORT Interrupt has been designed for virtual memory systems. For this reason, asynchronous ABORTB's may cause undesirable results due to the above conditions."
- WDC's per-cycle instruction note: "This is the last cycle which may be aborted or the Status, PBR or DBR registers will be updated."

The unifying rule: **abort is safe only if asserted on/for the memory cycle that actually generates the faulting address, at or before the instruction's "last abortable cycle."** If the MMU asserts ABORTB one cycle too late — or asserts it during the abort/interrupt sequence — state is corrupted. This is the substance behind the community claims that ABORT is "broken":

- On 6502.org a contributor recalled (attributing an MMU investigation by "Toshi") that "ABORT is not handled properly on an instruction fetch, which would preclude the usage of virtually mapped executable pages." Others simply repeat "ABORT being buggy."
- **BigDumbDinosaur** (BCS Technology, author of the definitive interrupt tutorial) notes that using ABORT for memory protection "may cause an infinite loop if stack memory faults" — the abort sequence itself pushes to the stack, so if the stack page is unmapped, the push faults, re-aborts, and the machine deadlocks. Hence **the stack must always be resident.**
- BigDumbDinosaur and Alarm Siren note ABORT is insufficient to prevent an unprivileged task executing WAI or STP (which halt the machine) — a privilege-enforcement gap, not a paging gap.
- **BigEd** notes the ABORT pin's behavior "may be completely unexplored within a practical system" — much of the consensus rests on datasheet reading and small experiments rather than a fielded VM machine.

**Restartable vs. precise.** *Restartable* means that after the handler fixes the fault, RTI can re-run the instruction and get the right result; *precise* means the machine state at the fault is exactly the pre-instruction state. The '816's abort is **restartable for ordinary loads/stores if the write is gated and the fault is detected on the faulting cycle**, but it is **not precise for RMW/RTI/interrupt-sequence cases** per §8.4. Demand paging needs restartability on the instructions that touch pageable memory — achievable by constraining *which* instructions may touch pageable memory and *when* the abort fires.

**MVN/MVP block moves.** These are explicitly designed to be *interruptible*: they decrement C and increment/decrement X and Y per byte, and (per the Super Famicom Wiki and 6502.org) "the move will resume after the ISR returns, provided the C, X, Y registers, Program Counter and MVN instruction are unchanged." That makes them resumable by IRQ/NMI, but hostile to abort-based paging: a block move can straddle many pages, its per-byte register state is mid-instruction, and combined with §8.4's register-corruption windows a mid-move page fault is unsafe to rely on. **Forbid MVN/MVP on pageable memory.**

**Silicon revisions.** The basic 65C816 was second-sourced by VLSI Technology, GTE, Sanyo and others (mid-1980s–early-1990s) and later made a fully static core by WDC. No source documents any silicon revision that *fixed* the §8.4 abort caveats; they persist in the current (2024) WDC datasheet. The 65C802 has an ABORT *vector* but no ABORT *pin* (it is pin-compatible with the 6502), so it cannot generate aborts at all.

**Emulation-mode stack wrap / bank crossing.** In emulation mode the stack is hard-wired to page 1 ($000100–$0001FF) and wraps within it; the abort push sequence is subject to that wrap. Because PC wraps within a bank, and direct page and stack always resolve to bank $00, the abort handler, vectors, stack and DP must all live in bank $00 and be permanently mapped.

### 3. Real-world use cases

| System | Uses ABORT? | Purpose | Notes |
|---|---|---|---|
| **Apple IIGS** (Mega II / FPI / CYA / VGC) | **No** | — | Highest-volume '816 machine. FPI/CYA + Mega II do bank-switching/shadowing and fast↔slow (2.8 MHz vs 1 MHz) speed matching, not virtual memory. ABORT is unused; GS/OS, ProDOS 16, ORCA, GNO/ME rely on banking, not paging. |
| **SNES / Super Famicom (Ricoh 5A22)** | **Not as an external page-fault input** | — | Per NESdev's "tepples" (2022): "The 5A22 consists of a stock 65816 core licensed from WDC combined with a memory controller capable of DMA and HDMA." SNES developers state "ABORT doesn't exist on the SNES." (The SNESdev CPU-pinout wiki does list a pin 76 "/ABORT" as an internal S-CPU signal, but it is not exposed for WDC-style demand paging.) |
| **CMD SuperCPU (C64/C128)** | **No** | — | '816 accelerator with its own RAM/ROM; uses RAM mirroring and bus tricks, no ABORT-based VM. |
| **Foenix C256 / F256** | **No** | — | Slot-based MMUs (8 KB blocks / MLUTs) for bank switching within a 16-/21-bit space; no demand paging, no ABORT. |
| **X65-SBC (NORA FPGA)** | **Yes** | Instruction trapping | Documented non-VM use: NORA raises ABORT to trap "banned" 65816-only opcodes (and SEP/REP) in emulation mode, then software-emulates/skips them. Confirms ABORT is usable for opcode trapping. |
| **Neon816** | FPGA (Lattice XP2) | — | Homebrew '816 + FPGA for video/audio; no documented ABORT-based VM. |
| **6502.org homebrew MMU threads** | Proposed only | Protection / paging | Detailed design discussions (BigDumbDinosaur, Dr Jefyll, et al.); consensus that protection is feasible with care but full VM is largely untested on silicon. |
| **noVa64 (this project)** | Planned | Demand paging + COW | Your own 6502.org thread ("noVa64 — Full 65816 Madness computer") documents the FPGA-MMU-halts-CPU approach. |

**Other documented non-VM uses of ABORT:** breakpoint/diagnostic "panic button" (debounced button → ABORT as a super-NMI, per the 6502.org "ABORT as general purpose interrupt" thread), single-stepping/watchdog, and — as X65/NORA proves — trapping unimplemented/illegal instructions for software emulation. No mainstream 65816 OS depends on ABORT.

### 4. Practical design guidance

**Timing budget.** At 14 MHz (~71 ns/cycle) with tPCS = 10 ns setup and tPCH = 10 ns hold, the MMU has on the order of 35–50 ns after VDA/VPA and the address become valid to perform TLB lookup + permission check and drive ABORTB. An iCE40-class FPGA (≈10 ns pin-to-pin) can make a single-cycle ASID-tagged TLB hit/miss decision, but a multi-level page-table walk cannot complete in-cycle — which is exactly why your architecture is right to resolve *unbounded* work in the abort handler (software) and use ABORT only as the *trigger*. At a few MHz the budget is comfortable; at 14 MHz it is tight and argues for a registered TLB whose hit/miss is a single combinational term.

**The write-gating invariant (mandatory for COW).** In the same cycle the MMU decides to abort a store, it must force the SRAM/SDRAM write-enable inactive so the faulting write never commits. Because the '816 does not roll back committed writes, COW is achievable *only* if the not-present/read-only decision is available before the write strobe and gates it:
`WE_to_memory = CPU_RWB_low AND cycle_permitted AND NOT abort_this_cycle`.
This is the single most important hardware rule in the design.

**Resident/pinned regions (mandatory).** To avoid the §8.4 and stack-fault failure modes, permanently pin (via your PTE's PIN flag): (a) the hardware stack page(s) in bank $00; (b) the direct page; (c) the interrupt/abort vectors and the abort/IRQ/NMI handlers; (d) any page holding code that performs RMW or block moves on non-pinned data. Your PTE already carries P/W/X/U/D/A/NC/**PIN** — make the kernel *enforce* that stack, DP, vectors and handlers are always PIN=1 and refuse to unmap them.

**Instruction discipline (Calypsi C / ca65).** With the large model, 24-bit pointers and JSL/RTL cross-bank calls:
- Keep the stack and direct page in pinned physical RAM, never demand-paged.
- Prohibit MVN/MVP against pageable regions; route large copies through a software loop of ordinary loads/stores (each individually restartable) or a DMA/blitter path arbitrated by Helium.
- Prefer non-RMW sequences on pageable data (load → modify in register → store) so a fault lands on a plain load or plain store, not on an RMW modify cycle. Where RMW on pageable data is unavoidable, pin the target or "pre-touch" it with a probe load before the RMW.
- Consider a software-probe discipline at function entry for large stack frames so a fault occurs on a benign probe rather than mid-instruction.
- Always resolve faults at instruction boundaries — the abort already re-enters via RTI-to-the-faulting-opcode; never attempt to resume mid-instruction.

**Comparison: PHI2-stall / RDY vs ABORT.** Your stated policy matches WDC's design intent:

| Property | PHI2 stall (or RDY low) | ABORTB |
|---|---|---|
| Best for | *Bounded* delays: SRAM-cache fill from SDRAM, bus arbitration, refresh contention | *Unbounded/exceptional*: unmapped page, permission fault, COW, kernel intervention |
| CPU state | Frozen in place; fully transparent; no stack/vector activity | Instruction retried via handler; stack push + ~8-cycle sequence |
| Max duration | Must be short (long stalls starve IRQ latency, DRAM refresh, video/audio timing) | Arbitrarily long (software resolves, then RTI) |
| Risk | If too long, breaks real-time (video/audio FPGA, refresh) | §8.4 corruption if mistimed; stack-fault deadlock if stack not pinned |
| Note | WDC Caveat §8.17 warns RDY-pulled-during-write has special behavior — prefer stretching PHI2 low, keeping ≤5 ns slew | Must never fire during a VPB (vector-pull) cycle |

Because you stretch PHI2 (rather than gate it with glitches) and Helium also drives video/audio and refresh, keep the stall bounded (e.g., one cache-line fill) and hand anything longer to ABORT. Whatever stretches PHI2 must preserve the '816's required ≤5 ns clean edges.

## Recommendations

**Stage 1 — Prove the primitive on real silicon (before committing the OS design).**
1. Bring up the board with the TLB and write-gate but VM disabled.
2. Bench-test the abort primitive with a logic analyzer on PHI2/VDA/VPA/RWB/ABORTB/VPB: assert ABORTB on a chosen load and confirm (a) the correct address is stacked, (b) RTI re-executes it, (c) A/X/Y/P/DB/DP are intact.
3. Repeat for a *store* and confirm the write was gated (memory unchanged).
*Go/no-go:* if plain load/store aborts are clean and writes are suppressed, proceed. If not, fall back to a PHI2-stall-only design with statically mapped banks (Foenix-style).

**Stage 2 — Characterize the §8.4 hazards on your actual parts.**
4. Deliberately assert ABORT one cycle late on an RMW, on an RTI (after cycle 3), and after cycle 2 of an interrupt, and observe the P/PBR/DBR corruption first-hand — to prove your production logic can never do this.
5. Confirm your MMU asserts ABORT *only* on the faulting cycle (gated by VDA/VPA) and *never* while VPB is low.

**Stage 3 — Enforce invariants in hardware + kernel.**
6. Hard-wire the write-gate equation above.
7. Make Helium refuse to abort any cycle where VPB is low (protect the abort sequence from re-abort — the §8.4.1.3 corruption case).
8. Kernel: force PIN=1 on stack, DP, vectors and all handler pages; refuse to unmap them.
9. Toolchain: keep stack+DP in pinned RAM; ban MVN/MVP on pageable memory; provide a pre-touch intrinsic for large frames and for unavoidable RMW-on-pageable.

**Stage 4 — Layer COW and demand paging.**
10. Mark COW pages read-only (W=0); on write-fault the gated write is suppressed, the handler copies the page, remaps W=1, and RTIs to retry.
11. Handle privileged instructions separately: ABORT cannot stop WAI/STP by an unprivileged task, so either don't run untrusted code or detect WAI/STP opcodes in Helium's instruction-stream monitor (as X65/NORA does) and abort them at fetch.

**Benchmarks that change the plan.** If bench testing shows aborts on plain loads/stores are not reliably precise on your silicon lot, drop demand paging and ship static banking + bounded PHI2 stall. If the 14 MHz budget can't fit a single-cycle TLB decision within tPCS, clock down (a few MHz) or pipeline the TLB so hit/miss is registered one cycle earlier.

## Caveats
- **No fielded 65816 virtual-memory machine is documented.** The Apple IIGS, SNES, SuperCPU and Foenix all avoid ABORT-based paging. The strongest real deployment of ABORT is *instruction trapping* (X65/NORA), not paging. Much of the "it works / it's buggy" discourse is datasheet interpretation plus small experiments; the community itself (BigEd) calls the pin "completely unexplored within a practical system."
- **The §8.4 corruption cases are from WDC's own current datasheet and are authoritative.** Where forum anecdote ("ABORT is broken for instruction fetch") conflicts with the datasheet, treat the datasheet's specific cycle-level statements as primary and the anecdote as an imprecise summary of the same underlying constraint.
- **ABORTB timing has no dedicated symbol.** Anyone citing a "tABS" is mistaken — the governing parameters are tPCS/tPCH (10 ns @ 14 MHz).
- Whether a mid-instruction page fault on **MVN/MVP** can ever be made safe, and the exact "last abortable cycle" for every addressing mode at your clock, can only be settled by bench measurement on your specific WDC parts.

### Open questions that require bench testing on real silicon
1. On your specific WDC lot, are aborts on plain loads and plain stores *precise* (A/X/Y/P/DB/DP all intact) 100% of the time when asserted on the faulting cycle?
2. Does gating write-enable in the abort cycle reliably prevent the store — no partial/glitch write — across temperature and at 14 MHz?
3. What is the exact last-abortable cycle for each addressing mode (especially indexed and [dp],Y long) at your clock, and does your MMU always assert within it?
4. Does aborting a page-crossing indexed access (where the extra cycle occurs) behave precisely?
5. Can a mid-block MVN/MVP fault ever be resumed correctly, or must it be categorically banned?
6. Does the VPB-guard (never abort during vector pull) fully prevent the "abort the abort" corruption of §8.4.1.3?
7. Is there any observable difference between second-source (GTE/Sanyo/VLSI) parts and current WDC parts in abort behavior, should you ever substitute CPUs?
8. Does the emulation-mode page-1 stack wrap create any abort-push hazard if a task's stack approaches $0100/$01FF?

---

### Summary table — known ABORT limitations

| Limitation | Affected instructions / cases | Severity | Mitigation |
|---|---|---|---|
| Abort after the *modify* cycle corrupts P **and** the write has already committed | RMW: ASL, LSR, ROL, ROR, INC, DEC, TSB, TRB | High | Assert only on the faulting (early) cycle; gate WE; avoid RMW on pageable data or pin/pre-touch the target |
| Abort after cycle 3 corrupts P | RTI | Medium | Keep RTI operands (stack) pinned; never let the abort logic re-fire during RTI |
| Abort after cycle 2 corrupts PBR/DBR ("abort the abort") | IRQ, NMI, ABORT, BRK, COP sequences | High | Inhibit ABORT while VPB is low; keep vectors/handlers/stack pinned |
| Stack push during abort can itself fault → infinite loop / deadlock | Any abort when the stack page is unmapped | Critical | Permanently pin the stack (PIN=1); kernel refuses to unmap it |
| Committed bus writes are never rolled back | Any faulting store | Critical (for COW) | Hardware WE-gating in the abort cycle |
| Mid-instruction register state; multi-page span | MVN, MVP block moves | High | Forbid on pageable memory; use SW loop or DMA/blitter |
| Cannot trap WAI/STP by unprivileged code | WAI, STP | Medium (privilege only) | Opcode-stream detection in Helium; abort at fetch |
| 65C802 has ABORT vector but no ABORT pin | (wrong part) | N/A | Use W65C816S only (as you do) |
| Tight in-cycle decision at 14 MHz (tPCS = 10 ns) | All faults | Medium | Registered/pipelined ASID-TLB; or clock down |