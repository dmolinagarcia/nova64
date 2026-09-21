# noVa128 — the Argon 2 core
> one core, three modes · instructions yes, cycles no · and the four things noVa64 owes it

Argon 2 is the CPU of noVa128: a softcore that runs **6502 emulation, 65816 native and NVM32 bytecode**, on a 16-bit data path between FPGAs. It replaces the coprocessor model of [sheet VM3](sec_ai_vm3), where a bytecode-only core sits beside a physical W65C816S. This sheet is a future version of the machine, not part of noVa64 — **but four of its requirements bind noVa64 software from now**, and that is the part of it that is not optional.

- NV1.1 — **In noVa128 the FPGAs are numbered, which is also how the names stay unambiguous.** Helium 2, Neon 2 and Argon 2 succeed Helium, Neon and Argon. In noVa64, *Argon* is the optional third device holding an unpopulated softcore slot ([B.4](sec_ai_b#b4), [E.5](sec_ai_e#e5)); in noVa128, **Argon 2 is the CPU itself** and the slot is gone, because there is no physical chip to be optional about.
- NV1.2 — **The platform this sheet assumes:**

| Element | noVa64 | noVa128 |
|---|---|---|
| CPU | W65C816S, 8 MHz PHI2 | Argon 2 softcore, ~50 MHz target |
| FPGAs | Helium, Neon, Argon optional | Helium 2, Neon 2, Argon 2 |
| Inter-FPGA data path | 8-bit, byte-oriented | 16-bit |
| Execution model | Native 65816 kernel, interpreted or offloaded bytecode | One core, three modes |

- NV1.3 — **What this sheet covers is the contract, the modes, the performance envelope, the resource budget and two correctness hazards. What it does not cover** — the microarchitecture, the mode-switch encoding, the cache organisation and the pin assignment — **follows once the contract and the device are settled**, and settling those is what this sheet is asking for.
- NV1.4 — **Unchanged from noVa64 and assumed throughout**: the 24-bit per-process virtual space with 2 KB pages and an ASID-tagged TLB in Helium ([L.1](sec_ai_l#l1), [L.7](sec_ai_l#l7)), `ABORTB` semantics for faults ([E.16](sec_ai_e#e16)), and the NVM32 ISA as frozen by [sheet VM1](sec_ai_vm1).

## The compatibility contract — instruction-compatible, not cycle-compatible.

- NV1.5 — **After every instruction the architectural state matches a real 65816 exactly; the number of clocks taken to get there does not, and neither does the sequence of bus accesses used internally.** That single sentence is the contract, and everything else in this sheet is a consequence of it.
- NV1.6 — **What must match exactly, and what may diverge:**

| Must match exactly | May diverge |
|---|---|
| Architectural state after each instruction: A, X, Y, S, D, DBR, PBR, P, PC | Cycles per instruction |
| Every addressing mode, including the awkward wraps: direct page, the stack page in emulation mode, `JMP ($xxFF)` on the 6502 | Interrupt latency |
| Every flag on every operation, decimal mode included | Order and number of internal bus accesses |
| `MVN`/`MVP` interruptible per byte with progress in X, Y and C | Prefetch depth and queue behaviour |
| Vectors, `BRK` and `COP` with the inline signature byte, `WAI`, `STP`, the `E` flag | Duration of `WAI` |
| `ABORTB` semantics: no register modification, instruction restarts | — |

- NV1.7 — **This costs noVa64 nothing, because the project already abandoned cycle counting as a measure of time when it adopted the PHI2 stall.** Memory waits stop the clock, so software already reads the fixed frequency reference instead of counting cycles ([L.15](sec_ai_l#l15), [E.4](sec_ai_e#e4)). **Argon 2 makes that permanent rather than introducing it** — which is why the contract is affordable here and would not be on a machine that had not already paid.
- NV1.8 — **What it gives up is retro software whose effects depend on cycle counts** — raster tricks, bit-banged I/O — **which was never a project goal.** If per-line timing is ever needed, the answer is a line interrupt from Neon ([T1.7](sec_ai_t1#t17)), not exact cycles.
  NOTE: **What it buys is verification.** The oracle becomes per-instruction architectural-state comparison against the emulator, with no cycle model to build or maintain on either side ([NV1.26](sec_ai_nv1#nv126)). A cycle-exact contract would require a cycle-exact oracle, and the project has neither.

### Software portability between noVa64 and noVa128.

- NV1.9 — **The contract guarantees that 65816 instructions behave identically; it does not by itself guarantee that noVa64 software runs on noVa128. That depends on the layer:**

| Layer | Same binary on both? |
|---|---|
| NVM32 applications | **Yes, by construction** — same ISA, memory map and semantics, and the only exit is `SYSCALL` ([VM1.40](sec_ai_vm1#vm140)) |
| Kernel and native 65816 code | **Yes, provided the four requirements below hold** |
| BIOS and low-level hardware bring-up | No, and it need not be |

- NV1.10 — **Four requirements on noVa64 software, binding from now.** Each is cheap today and silently breaks noVa128 compatibility if violated, which is the worst shape a requirement can have — it fails on a machine that does not exist yet, years after the code was written ([D92](sec_ai_q#d92)).
  1. **Never count cycles to measure time.** All timing comes from the fixed frequency reference, which exists in noVa128 at the same address. Already required by the PHI2 stall, so it costs nothing to make binding.
  2. **No self-modifying code in the kernel**, or Argon 2 must snoop writes into its instruction cache ([NV1.24](sec_ai_nv1#nv124)). Forbidding it in project code is the cheaper side of that hazard; it stays a hazard only for retro software.
  3. **Helium and Neon registers are idempotent on read** ([NV1.25](sec_ai_nv1#nv125)), or a read-modify-write instruction behaves differently on the two machines.
  4. **The software-visible register map does not change.** The 16-bit inter-FPGA path is internal: the CPU sees the same addresses at the same widths. If the map ever has to change, **it is versioned and discovered through the system information block, never by build-time conditionals.**
- NV1.11 — **The BIOS absorbs the differences, and the system information block is what it hands over.** Reset, hardware initialisation and clocking differ between a board with a physical W65C816S and one with Argon 2. What must be identical is the block defined in [VM2.28](sec_ai_vm2#vm228) — CPU identity, Helium capabilities, coprocessor presence and the resolved VM gate. **Two BIOS implementations below, one kernel above** ([I.2](sec_ai_i#i2), [J.2](sec_ai_j#j2)).
- NV1.12 — **Argon 2 implements `WDM #WDM_CPUID`, loading a non-zero signature into A**, and on a real 65816 `WDM` is a guaranteed two-byte no-op leaving A unchanged — **so the probe cannot misreport** ([VM2.25](sec_ai_vm2#vm225)). This is how the BIOS tells the machines apart, and it has to come from the CPU because **Helium cannot see electrically which CPU is on the bus** ([D89](sec_ai_q#d89)).

## Execution modes — three decoders, one memory system.

- NV1.13 — **Three modes over one register file, one bus interface and one cache. Modes change what the decoder does, not what the memory system does:**

| Mode | Decoder | Address space | Entered by |
|---|---|---|---|
| 6502 emulation | 6502 opcodes, `E`=1 semantics, 8-bit stack in page 1 | 64 KB inside bank 0 | The `E` flag, as on a real 65816 |
| 65816 native | Full 65816, M/X widths, 24-bit addressing | The 24-bit process space | `XCE`, as on a real 65816 |
| NVM32 native | 16-bit code units, sixteen 32-bit registers, flagless compare-and-branch | The same 24-bit process space, the same page tables | The mode-switch gate below |

- NV1.14 — **The first two transitions are the 65816's own and carry no new design.** The third is `WDM #VMENTER` with X = the PCB address, specified in full in [VM2.21](sec_ai_vm2#vm221). **Entry is a call, not a jump**: the core saves the 65816-mode PC, executes bytecode, and on a trap returns to the following instruction — the exact analogue of the interpreter's dispatch-and-return, which is what lets one kernel gate serve both machines.
- NV1.15 — **One kernel source, both machines.** The kernel calls an indirect vector resolved at boot from a Helium capability register: on noVa64 it points at the interpreter's entry, on Argon 2 at the `WDM` gate. **A pointer in data, never self-modifying code**, which would collide with [NV1.24](sec_ai_nv1#nv124) ([VM2.16](sec_ai_vm2#vm216)).
  NOTE: The two register sets do not coexist. Entering NVM32 mode leaves A, X, Y, D and DBR undefined on return, so the gate saves them — consistent with NVM32 process state being {R0–R15, PC} and nothing else ([VM1.14](sec_ai_vm1#vm114)).
- NV1.16 — **Syscalls stop being expensive, and for GUI and file-system load this matters more than clock frequency.** On noVa64 a bytecode `SYSCALL` means halting the core, waking the 65816, marshalling, `COP` and resuming — thousands of cycles ([VM3.17](sec_ai_vm3#vm317)). **On Argon 2 it is a mode switch inside one core: tens of cycles.**

## Expected performance — every figure an estimate.

- NV1.17 — **Against the noVa64 baseline of a W65C816S at 8 MHz PHI2.** All figures are engineering estimates from clock ratio, CPI and bus width; **none is measured**:

| Mode | Versus 8 MHz 65816 | Binding constraint |
|---|---|---|
| 6502 emulation | 6–10× | Clock and cache hit rate; still 8-bit, still 64 KB |
| 65816 native | 6–10× | The same, plus the 16-bit path on 16-bit accesses |
| NVM32 native | 15–40× on 32-bit C code | Bandwidth: roughly 7–12M VM-instructions per second |

- NV1.18 — **The more useful comparison is NVM32 native against 65816 native on the same core.** Instruction rates are within 2–4× of each other, **but each NVM32 instruction does the work of four to six 65816 instructions once the data is 32-bit.** Retro-style 8- and 16-bit code with direct-page tricks holds up well in 65816 mode; compiled C does not.
- NV1.19 — **Neon 2 stops being CPU-starved, and this may matter more than the raw numbers.** Command emission at ~50 cycles per 12-byte command is Neon's binding constraint today, not blitter bandwidth ([T1.38](sec_ai_t1#t138)). **With Argon 2 that constraint moves back to SDRAM bandwidth, where it belongs**, and the high-resolution graphics modes held in reserve become viable ([T1.39](sec_ai_t1#t139)).

## The 16-bit inter-FPGA path — bandwidth, and nothing for latency.

- NV1.20 — **The memory devices are already 16 bits wide** — the SRAM and both SDRAMs ([F.5](sec_ai_f#f5), [F.8](sec_ai_f#f8)). **The narrowing happens at the FPGA edge and in the 65816 bus, which splits everything into bytes.** Matching the width end to end halves the number of transfers:

| Access | 8-bit path | 16-bit path |
|---|---|---|
| NVM32 code unit | 2 transfers | 1 |
| Typical NVM32 instruction, two units | 4 | 2 |
| 32-bit load or store | 4 | 2 |
| 65816 access with M/X = 0 | 2 | 1 |
| Instruction stream in 65816 mode | 1 per byte | 2 bytes per transfer, prefetchable |

  NOTE: Worth roughly 2× on bandwidth-bound NVM32 code and 20–40% in the 65816 modes, whose instruction set stays byte-oriented and therefore cannot use the full width.
- NV1.21 — **It does nothing for latency, and that is the more important half.** A cache miss costs the same nanoseconds as today, **but at ~50 MHz those are about six times as many lost cycles.** Hit rate against the SRAM cache therefore becomes the variable that decides real performance — **ahead of Fmax and ahead of bus width. Any effort spent on Argon 2 goes to the cache before it goes to the core** ([E.10](sec_ai_e#e10), [VM3.12](sec_ai_vm3#vm312)).

## Resource budget and device selection — the decision this sheet is really asking for.

- NV1.22 — **Argon 2 does not fit an iCE40 HX8K.** Three modes means two CPUs on one die: a hardware 65816 — decimal mode, every addressing mode, all flag semantics, `ABORTB` — plus the NVM32 FSM, sharing a cache, a bus interface and an EBR register file:

| Block | Estimated LUT4 |
|---|---|
| 65816 decoder and control, both modes | 2,500–3,500 |
| NVM32 decoder and FSM | 1,000–1,500 |
| Shared 32-bit ALU, iterative shifter, multiply and divide | 800–1,200 |
| Cache controller, bus master, mode switching | 1,200–2,000 |
| **Total** | **6,000–8,000** |

- NV1.23 — **Against HX8K's 7,680 logic cells that leaves nothing for the cache itself, and the estimate carries a ±20% band.** The ECP5 LFE5U-45F already used for the Phase 1 prototype absorbs it comfortably at 44K LUT4 ([P2.03](sec_ai_p2#p203)).
  NOTE: **The consequence is larger than the performance gain, and it is what this sheet is asking to approve: noVa128 retracts the iCE40 as the final target for the CPU FPGA** ([D95](sec_ai_q#d95)). [D01](sec_ai_q#d01) chose three iCE40 TQ144 parts because BGA is not hand-solderable and 0.5 mm TQFP is; that argument still holds for **Helium 2 and Neon 2, which may stay on iCE40. Argon 2 cannot.**
- NV1.24 — **Pins are the second constraint and they are not solved here.** The Helium↔Neon link already needed a multiplexed 32-signal encoding to fit the TQ144 spare-pin budget with byte-wide links ([Q12](sec_ai_q#q12)). **Three FPGAs with 16-bit data paths do not fit that budget**, so noVa128 needs either larger packages throughout or serialised inter-FPGA links — **and the latter reintroduces exactly the latency this design is most sensitive to** ([NV1.21](sec_ai_nv1#nv121)). Resolve before schematic capture (→ [Q171](sec_ai_q#q171)).

## Correctness hazards — where "instructions yes, timing no" stops being a performance question.

- NV1.25 — **H1 — the instruction cache against self-modifying code.** If Argon 2 has an instruction cache or prefetch — **and it will, since that is where the performance comes from** — a write to code already fetched diverges from a real 65816. **Retro 6502 code does this constantly.**

| Option | Cost |
|---|---|
| No instruction cache in 6502/65816 modes | Gives up most of the gain in exactly the modes that need it |
| Snoop writes and invalidate the line | LUTs and complexity, but cheap on a small cache |
| Explicit software coherence | Breaks the compatibility the modes exist for |

  NOTE: **Recommendation: line snooping. It is the only option that keeps both the contract and the performance** (→ [Q161](sec_ai_q#q161)). Project code is forbidden self-modifying code regardless ([NV1.10](sec_ai_nv1#nv110)), so the hazard is narrowed to software the project did not write.
- NV1.26 — **H2 — phantom accesses in read-modify-write instructions, and this one has a deadline.** A real 65816 performs spurious reads and writes during `INC abs,X`, `ASL` and similar ([E.18](sec_ai_e#e18)). Over RAM they are harmless; **over a Neon or Helium register with a read side effect they are not.** Either Argon 2 reproduces the access sequence in I/O space, or the design declares that **every memory-mapped register is idempotent on read.**
  NOTE: **The second is cheaper and fits the project's pruning habit, but it must be written into the Neon sheets before the command port is frozen** ([T1.21](sec_ai_t1#t121), [T1.38](sec_ai_t1#t138)) — **otherwise a register with read side effects gets designed in and discovered late** (→ [Q170](sec_ai_q#q170)). It is the one item in this sheet with a date attached to something already in progress.

## Verification — one oracle, three suites, and the third is new work.

- NV1.27 — **The oracle is per-instruction architectural-state comparison against the emulator** ([sheet EM1](sec_ai_em1)), with no cycle model on either side. **Dropping cycle accuracy from the contract is precisely what makes this affordable** ([NV1.5](sec_ai_nv1#nv15)).
- NV1.28 — **Three suites, one harness** — the differential pattern already proven on the file system ([Y2.22](sec_ai_y2#y222)) and reused for the NVM32 conformance work ([VM3.20](sec_ai_vm3#vm320)):

| Suite | Oracle | Compares |
|---|---|---|
| 6502 / 65816 compatibility | The emulator's CPU model, plus published 6502 test suites | A, X, Y, S, D, DBR, PBR, P, PC and memory writes after each instruction |
| NVM32 conformance | The C11 reference interpreter | {R0–R15, PC} and memory writes, unchanged from [sheet VM1](sec_ai_vm1) |
| Mode transitions | Hand-written; **no existing oracle** | State save and restore across every entry and exit path, including faults and syscalls taken mid-sequence |

  NOTE: **The third suite is new work with no prior art in the project and is the likeliest source of late defects. Budget for it explicitly** rather than treating it as an extension of the other two.
- NV1.29 — **Co-simulation runs in Verilator, as for the bytecode core, and bounded model checking stays worthwhile for the same small self-contained pieces**: decode length and field extraction, the ALU edge cases, and — new here — **decimal-mode flag behaviour, which is historically where 65816 implementations diverge** ([EM4.5](sec_ai_em4#em45)).

## Abandonment conditions and what is left open.

- NV1.30 — **If the ECP5 is unacceptable for the product and Argon 2 must fit an iCE40, drop 6502 emulation mode first** — it is the cheapest to lose and the least used — **and then 65816 native**, which leaves the bytecode-only core of [sheet VM3](sec_ai_vm3). The degradation path is deliberate: each step back is a mode, not a redesign.
- NV1.31 — **If line snooping cannot be made to meet timing at 40 MHz, run the 65816 modes with the instruction cache disabled** and accept their reduced speedup. **NVM32 mode keeps its cache, since bytecode is never self-modifying.**
- NV1.32 — **If the 16-bit inter-FPGA path forces serialised links whose latency cancels the bandwidth gain, keep byte-wide links and spend the budget on cache size instead** ([NV1.21](sec_ai_nv1#nv121)).
- NV1.33 — **If measured NVM32 throughput on Argon 2 is below 10× the 65816 interpreter, noVa128 does not justify the redesign** over noVa64 plus the coprocessor.
- NV1.34 — [[!blocking]] **The largest unresolved item: does a physical W65C816S still exist in noVa128?** If not, **PHI2 stretching disappears as the memory-wait mechanism and the whole wait semantics must be redefined** — which touches Helium 2's arbiter, the cache fill, the debug halt and every timing statement inherited from noVa64 ([E.4](sec_ai_e#e4), [F.7](sec_ai_f#f7)). Nothing else in this sheet can be finalised around it (→ [Q159](sec_ai_q#q159)).
- NV1.35 — **Four more stay open**: instruction cache policy in the 6502 and 65816 modes — snoop, disable, or software coherence (→ [Q161](sec_ai_q#q161)) · whether all memory-mapped registers are idempotent on read, which needs a decision in the Neon sheets before the command port is frozen (→ [Q170](sec_ai_q#q170)) · the package and pin budget for three FPGAs with 16-bit paths (→ [Q171](sec_ai_q#q171)) · and whether the native kernel stays 65816 code, moves to NVM32 or splits — **Argon 2 makes all three possible, which the coprocessor model did not** (→ [Q164](sec_ai_q#q164)).
