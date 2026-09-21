# DN-HW-ARGON-001 — Argon tri-mode softcore

*noVa128 · Rev A (draft for discussion)* · 2026-09-17 · @Someone

## Revision history

| Rev | Date | Change |
| --- | --- | --- |
| A | 2026-09-17 | Initial draft for discussion. Defines the instruction-compatible, not cycle-compatible contract; three execution modes; expected performance; resource budget and the device-selection consequence. |
| A.1 | 2026-09-20 | Mode switch settled: `WDM #VMENTER` gate reached through a boot-time indirect vector (DN-SW-VMINTERP-001). |
| A.2 | 2026-09-21 | Software portability between noVa64 and noVa128: four requirements binding on noVa64 software now, the BIOS system information block as the contract, and `WDM #WDM_CPUID` for CPU identification. |

*Review cycle: reviewers, dates and decisions to be recorded here before this note leaves draft status.*

## Scope

Argon is the CPU of noVa128: a softcore that runs 6502 emulation mode, 65816 native mode and NVM32 bytecode natively, on a 16-bit data path between FPGAs. It replaces the coprocessor model of DN-HW-VMCORE-001, in which a bytecode-only core sat beside a physical W65C816S.

The noVa128 platform assumed here:

| Element | noVa64 | noVa128 |
| --- | --- | --- |
| CPU | W65C816S, 8 MHz PHI2 | Argon softcore, \~50 MHz target |
| FPGAs | Helium (MMU, cache, arbitration, debug), NEON (video/audio), FPGA-C optional | Helium, NEON, Argon |
| Inter-FPGA data path | 8-bit, byte-oriented | 16-bit |
| Execution model | Native 65816 kernel, interpreted or offloaded bytecode | One core, three modes |

This note covers the compatibility contract, the modes, the expected performance envelope, the resource budget and the two correctness hazards the design creates. It does not specify the microarchitecture, the mode-switch instruction encoding, the cache organisation or the pin assignment; those follow once the contract and the device are settled.

Unchanged from noVa64 and assumed here: the 24-bit per-process virtual space with 2 KB pages and ASID-tagged TLB in Helium, ABORTB semantics for faults, and the NVM32 ISA as frozen by DN-SW-VMISA-001.

## Compatibility contract

**Argon is instruction-compatible with the W65C816S, not cycle-compatible.** After every instruction the architectural state matches a real 65816 exactly; the number of clocks taken to get there does not, and neither does the sequence of bus accesses used internally.

This costs noVa64 nothing, because the project already abandoned cycle counting as a measure of time when it adopted PHI2 stretching: memory waits stop the clock, so software already reads the fixed frequency reference instead of counting cycles. Argon makes that permanent rather than introducing it.

| Must match exactly | May diverge |
| --- | --- |
| Architectural state after each instruction: A, X, Y, S, D, DBR, PBR, P, PC | Cycles per instruction |
| All addressing modes, including the awkward wraps: direct page, stack page in emulation mode, `JMP ($xxFF)` on the 6502 | Interrupt latency |
| Every flag on every operation, decimal mode included | Order and number of internal bus accesses |
| `MVN`/`MVP` interruptible per byte with progress in X, Y and C | Prefetch depth and queue behaviour |
| Vectors, `BRK` and `COP` with inline signature byte, `WAI`, `STP`, the `E` flag | Duration of `WAI` |
| ABORTB semantics: no register modification, instruction restarts | — |

What this gives up is retro software whose effects depend on cycle counts — raster tricks, bit-banged I/O — which was never a project goal. If per-line timing is ever needed, the answer is a line interrupt from NEON, not exact cycles.

What it buys is verification: the oracle becomes per-instruction architectural-state comparison against the PC emulator, with no cycle model to build or maintain.

### Software portability between noVa64 and noVa128

The contract above guarantees that 65816 instructions behave identically; it does not by itself guarantee that noVa64 software runs on noVa128. That depends on the layer:

| Layer | Same binary on noVa64 and noVa128? |
| --- | --- |
| NVM32 applications | **Yes, by construction** — same ISA, memory map and semantics; the only exit is `SYSCALL` |
| Kernel and native 65816 code | **Yes, provided the four requirements below hold** |
| BIOS and low-level hardware bring-up | No, and it need not be |

**Requirements on noVa64 software, binding from now** — each is cheap today and silently breaks noVa128 compatibility if violated:

1. **Never count cycles to measure time.** All timing comes from the fixed frequency reference, which must exist in noVa128 at the same address. Already required on noVa64 by PHI2 stretching, so it costs nothing.
2. **No self-modifying code in the kernel**, or Argon must snoop writes into its instruction cache (hazard H1). Forbidding it in project code is the cheaper side; it stays a hazard only for retro software.
3. **Helium and NEON registers are idempotent on read** (hazard H2); otherwise a read-modify-write instruction behaves differently on the two machines.
4. **The software-visible register map does not change.** The 16-bit inter-FPGA path is internal: the CPU must see the same addresses at the same widths. If the map ever has to change, it is versioned and discovered through the system information block, never by build-time conditionals.

**The BIOS absorbs the differences.** Reset, hardware initialisation and clocking will differ between a board with a physical W65C816S and one with Argon. What must be identical is what the BIOS hands over: the system information block defined in DN-SW-VMINTERP-001 — CPU identity, Helium capabilities, coprocessor presence and the resolved VM gate. Two BIOS implementations below, one kernel above.

**CPU identification.** Argon implements `WDM #WDM_CPUID`, loading a non-zero signature into A. On a real 65816 `WDM` is a guaranteed two-byte no-op and A is unchanged, so the probe cannot misreport. This is how the BIOS tells the machines apart; Helium cannot see electrically which CPU is on the bus.

## Execution modes

Three modes over one register file, one bus interface and one cache. Modes change what the decoder does, not what the memory system does.

| Mode | Decoder | Address space | Entered by |
| --- | --- | --- | --- |
| 6502 emulation | 6502 opcodes, `E`=1 semantics, 8-bit stack in page 1 | 64 KB inside bank 0 | `E` flag, as on a real 65816 |
| 65816 native | Full 65816, M/X widths, 24-bit addressing | 24-bit process space | `XCE`, as on a real 65816 |
| NVM32 native | 16-bit code units, 16 × 32-bit registers, flagless compare-and-branch | Same 24-bit process space, same page tables | New mode-switch mechanism |

The first two transitions are the 65816's own and carry no new design. The third is settled as follows, and is specified in full in DN-SW-VMINTERP-001 under "The gate":

- **Entry: `WDM #VMENTER`, with X = the PCB address.** `WDM` (`$42`) is the opcode WDC reserved for expansion — a two-byte no-op on a real 65816 — so using it as the NVM32 mode entry costs nothing and does not violate the compatibility contract above.
- **Entry is a call, not a jump.** The core saves the 65816-mode PC, executes bytecode and, on a trap, returns to the following instruction. That makes it the exact analogue of the interpreter's dispatch-and-return, which is what lets one kernel gate serve both machines. Exit is through `SYSCALL` and through faults, which already halt the VM with a cause in the PCB.
- **The two register sets do not coexist.** Entering NVM32 mode leaves A, X, Y, D and DBR undefined on return, so the gate saves and restores them. Consistent with NVM32 process state being {R0–R15, PC} and nothing else.
- **One kernel source, both machines.** The kernel calls an indirect vector resolved at boot from a Helium capability register: on noVa64 it points at the interpreter's entry, on Argon at the `WDM` gate. A pointer in data, never self-modifying code, which would collide with hazard H1 below.
- **Syscalls stop being expensive.** On noVa64 a bytecode `SYSCALL` means halting the core, waking the 65816, marshalling, `COP` and resuming: thousands of cycles. On Argon it is a mode switch inside one core: tens of cycles. For GUI and file-system load this matters more than clock frequency.

## Expected performance

Against the noVa64 baseline of a W65C816S at 8 MHz PHI2. All figures are engineering estimates from clock ratio, CPI and bus width; none is measured.

| Mode | Versus 8 MHz 65816 | Binding constraint |
| --- | --- | --- |
| 6502 emulation | 6–10× | Clock and cache hit rate; still 8-bit, still 64 KB |
| 65816 native | 6–10× | Same, plus the 16-bit path on 16-bit accesses |
| NVM32 native | 15–40× on 32-bit C code | Bandwidth: roughly 7–12M VM-instructions/s |

Against today's 65816 interpreter, NVM32 native on Argon is the same 20–40× already estimated in DN-HW-VMCORE-001, with less risk of falling short because of the bus.

The more useful comparison is NVM32 native against 65816 native **on the same core**: instruction rates are within 2–4× of each other, but each NVM32 instruction does the work of four to six 65816 instructions once the data is 32-bit. Retro-style 8/16-bit code with direct-page tricks holds up well in 65816 mode; compiled C does not.

Two second-order effects that may matter more than the raw numbers:

- **Syscall cost collapses** from thousands of cycles to tens, because it stops crossing a chip boundary (see "Execution modes").
- **NEON stops being CPU-starved.** Command emission at \~50 cycles per 12-byte command was NEON's binding constraint, not blitter bandwidth. With Argon that constraint moves back to SDRAM bandwidth, where it belongs, and the high-resolution graphics modes held in reserve become viable.

## The 16-bit inter-FPGA data path

The memory devices are already 16 bits wide: the IS61WV102416 SRAM and both SDRAMs. The narrowing happens at the FPGA edge and in the 65816 bus, which splits everything into bytes. Matching the width end to end halves the number of transfers.

| Access | 8-bit path | 16-bit path |
| --- | --- | --- |
| NVM32 code unit | 2 transfers | 1 |
| Typical NVM32 instruction (2 units) | 4 | 2 |
| 32-bit load or store | 4 | 2 |
| 65816 access with M/X = 0 | 2 | 1 |
| Instruction stream in 65816 mode | 1 per byte | 2 bytes per transfer, prefetchable |

That is worth roughly 2× on bandwidth-bound NVM32 code and 20–40% in the 65816 modes, whose ISA remains byte-oriented.

**It does nothing for latency.** A cache miss costs the same nanoseconds as today, but at \~50 MHz those are about six times as many lost cycles. Hit rate against the 1 MB SRAM cache therefore becomes the variable that decides real performance — ahead of Fmax and ahead of bus width. Any effort spent on Argon should go to the cache before it goes to the core.

Pin cost is covered under "Resource budget"; it is the reason this section's benefit is not free.

## Resource budget and device selection

**Argon does not fit an iCE40 HX8K.** Three modes means two CPUs on one die: a hardware 65816 (decimal mode, every addressing mode, all flag semantics, ABORTB) plus the NVM32 FSM, sharing a cache, a bus interface and an EBR register file.

| Block | Estimated LUT4 |
| --- | --- |
| 65816 decoder and control (both modes) | 2,500–3,500 |
| NVM32 decoder and FSM | 1,000–1,500 |
| Shared 32-bit ALU, iterative shifter, multiply and divide | 800–1,200 |
| Cache controller, bus master, mode switching | 1,200–2,000 |
| **Total** | **6,000–8,000** |

Against HX8K's 7,680 logic cells that leaves nothing for the cache itself, and the estimate carries a ±20% band. The ECP5 LFE5U-45F already used for the Phase 1 prototype (44K LUT4) absorbs it comfortably.

The consequence is a decision larger than the performance gain: **noVa128 retracts the iCE40 as the final target for the CPU FPGA.** That, not the clock speed, is what this note is really asking to approve. Helium and NEON may stay on iCE40; Argon cannot.

Pins are the second constraint. The Helium↔NEON link already needed a multiplexed 32-signal encoding to fit the TQ144 spare-pin budget with byte-wide links. Three FPGAs with 16-bit data paths do not fit that budget, so noVa128 needs either larger packages throughout or serialised inter-FPGA links — the latter reintroducing the latency this design is most sensitive to. Resolve before schematic capture.

## Correctness hazards

Two places where "instructions yes, timing no" leaks from performance into correctness. Both need a decision recorded before implementation.

**H1 — Instruction cache versus self-modifying code.** If Argon has an instruction cache or prefetch — and it will, since that is where the performance comes from — a write to code already fetched diverges from a real 65816. Retro 6502 code does this constantly.

| Option | Cost |
| --- | --- |
| No instruction cache in 6502/65816 modes | Gives up most of the gain in exactly the modes that need it |
| Snoop writes and invalidate the line | LUTs and complexity, but cheap on a small cache |
| Explicit software coherence | Breaks the compatibility the modes exist for |

Recommendation: line snooping. It is the only option that keeps the contract and the performance.

**H2 — Phantom accesses in read-modify-write instructions.** A real 65816 performs spurious reads and writes during `INC abs,X`, `ASL` and similar. Over RAM they are harmless; over a NEON or Helium register with a read side effect they are not. Either Argon reproduces the access sequence in I/O space, or the design declares that every memory-mapped register is idempotent on read. The second is cheaper and fits the project's pruning habit, but it must be written into the NEON note before the command port is frozen — otherwise a register with read side effects will be designed in and discovered late.

## Verification

The oracle is per-instruction architectural-state comparison against the PC emulator (DN-SW-EMU-001), with no cycle model on either side. Dropping cycle accuracy from the contract is what makes this affordable.

Three suites, one harness — the differential pattern already proven on NVFS and reused for the NVM32 conformance work:

| Suite | Oracle | Compares |
| --- | --- | --- |
| 6502 / 65816 compatibility | PC emulator's CPU model, plus published 6502 test suites | A, X, Y, S, D, DBR, PBR, P, PC and memory writes after each instruction |
| NVM32 conformance | C11 reference interpreter | {R0–R15, PC} and memory writes, unchanged from DN-SW-VMISA-001 |
| Mode transitions | Hand-written, no existing oracle | State save and restore across every entry and exit path, including faults and syscalls taken mid-sequence |

The third suite is new work with no prior art in the project and is the likeliest source of late defects. Budget for it explicitly rather than treating it as an extension of the other two.

Co-simulation runs in Verilator, as for the bytecode core. Bounded model checking stays worthwhile for the same small, self-contained pieces: decode length and field extraction, the ALU edge cases, and — new here — decimal-mode flag behaviour, which is historically where 65816 implementations diverge.

## Abandonment conditions and open questions

Abandonment conditions, actionable without reopening this note:

- If the ECP5 is unacceptable for the product and Argon must fit an iCE40, **drop 6502 emulation mode first** — it is the cheapest to lose and the least used — and then 65816 native, leaving the bytecode-only core of DN-HW-VMCORE-001.
- If line snooping (H1) cannot be made to meet timing at 40 MHz, run the 65816 modes with the instruction cache disabled and accept their reduced speedup; NVM32 mode keeps its cache, since bytecode is never self-modifying.
- If the 16-bit inter-FPGA path forces serialised links whose latency cancels the bandwidth gain, keep byte-wide links and spend the budget on cache size instead.
- If measured NVM32 throughput on Argon is below 10× the 65816 interpreter, noVa128 does not justify the redesign over noVa64 plus the coprocessor.

Open questions:

- [ ] Does a physical W65C816S still exist in noVa128? If not, PHI2 stretching disappears as the cache-wait mechanism and the whole memory-wait semantics must be redefined — the largest unresolved item here.
- [ ] Instruction cache policy in 6502/65816 modes: snoop, disable, or software coherence (H1).
- [ ] Are all memory-mapped registers idempotent on read (H2)? Needs a decision in the NEON note before the command port is frozen.
- [ ] Mode-switch mechanism: which opcode in the 65816 space enters NVM32 mode, and what happens to the other register set.
- [ ] Package and pin budget for three FPGAs with 16-bit paths.
- [ ] Does the native kernel stay 65816 code, move to NVM32, or split? Argon makes all three possible, which the coprocessor model did not.
