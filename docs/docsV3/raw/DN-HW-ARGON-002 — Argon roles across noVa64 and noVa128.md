# DN-HW-ARGON-002 — Argon roles across noVa64 and noVa128

*noVa64 · noVa128 · Rev A (draft for discussion)* · 2026-11-24 · @Someone

## Revision history

| Rev | Date | Change |
| --- | --- | --- |
| A | 2026-11-24 | Initial draft. Fixes what Argon is in each machine and the compatibility rule between them. Amends DN-HW-ARGON-001 and DN-HW-VMCORE-001. |
| A.1 | 2026-11-24 | Bus interface in noVa64: shared pins, independent bus enables, a separate CPU clock, interrupts left to the 65816. Closes the memory-path question. |

*Review cycle: reviewers, dates and decisions to be recorded here before this note leaves draft status.*

## Scope

The name Argon has carried three roles over the life of the project. It started as a 65816 softcore on FPGA-C that could take over from the physical CPU. It then became an NVM32 bytecode coprocessor beside that CPU (DN-HW-VMCORE-001). Finally it became Argon 2, the tri-mode CPU of noVa128 (DN-HW-ARGON-001). Each step was recorded, but the earlier roles stayed in the text, and the documents disagreed on three questions:

- Can anything replace the W65C816S in noVa64?
- Does a physical 65816 exist in noVa128?
- Which modes of Argon 2 can be given up?

This note answers all three. It defines no new hardware. It states which role belongs to which machine, the compatibility rule that ties the two machines together, and what that rule retires from the earlier notes.

## The decision

| | noVa64 | noVa128 |
| --- | --- | --- |
| CPU | W65C816S, **mandatory in every configuration** | Argon 2, **the only processor** |
| Argon | Optional FPGA-C holding the NVM32 coprocessor, beside the 65816 | Not present as a separate role: Argon 2 is the CPU |
| NVM32 execution | On Argon when present, otherwise on the 65816 interpreter | Argon 2's NVM32 native mode |
| Native 65816 code | On the W65C816S | Argon 2's 65816 native mode, unchanged binaries |
| 6502 emulation | The W65C816S's own `E` = 1 mode | Argon 2's 6502 emulation mode |
| Kernel | 65816 native | The same 65816 binary |

### noVa64: the 65816 stays, and Argon is invisible to software

1. **The W65C816S is mandatory.** No noVa64 configuration runs without the physical part, and nothing on the board replaces it. There is no 65816 softcore in noVa64, on Argon or anywhere else.
2. **Argon is optional and runs only NVM32.** It is the bytecode coprocessor of DN-HW-VMCORE-001. It masters the shared bus under Helium's arbitration and shares the MMU, the TLB and the page tables with the 65816. It never runs the native kernel, and does not need to, because the physical CPU is always present.
3. **The software is identical with or without Argon.** The same NVX binaries and the same kernel run on both configurations, with no recompilation, no configuration and no build flag. At boot the BIOS runs the CPU probe, reads Helium's capability register, and resolves the VM gate (DN-SW-VMINTERP-001, "The gate"). When Argon is present, NVM32 processes run on it. When it is absent, they run on the 65816 interpreter. Because the two executors share architectural state, a process can also move between them mid-execution.
4. **Argon changes how fast NVM32 code runs, never whether it runs.** Whether Argon is worth building is still decided by the measured threshold in DN-HW-VMCORE-001. If the threshold is not met, the machine loses speed and nothing else.

### noVa128: one processor, full backward compatibility

5. **Argon 2 is the only processor.** noVa128 has no physical W65C816S. Argon 2 runs three modes over one memory system: 6502 emulation, 65816 native and NVM32 native. The compatibility contract of DN-HW-ARGON-001 is unchanged: instruction-compatible, not cycle-compatible.
6. **noVa64 software runs unchanged, native 65816 code included.** This is the purpose of noVa128, not a side benefit. The four portability requirements of DN-HW-ARGON-001 are what make it hold, and they bind noVa64 software from now on:
   - no cycle counting;
   - no self-modifying code;
   - registers idempotent on read;
   - a stable register map.

### The rule between them: backward compatibility, as on x86

7. **Compatibility runs one way.** Everything above the BIOS that runs on noVa64 runs on noVa128. The reverse is not promised. Software written for noVa128 may use what only Argon 2 offers: syscalls at mode-switch cost, the 16-bit inter-FPGA path, higher clock rates and the graphics modes that these make viable.
8. **New features are discovered at run time, never assumed at build time.** A program finds out what the machine offers from the system information block and the `WDM #WDM_CPUID` probe. It then either keeps a path for the machine that lacks the feature, or declares itself noVa128 software. Old software never has to know that the new features exist.
9. **The BIOS is the only layer allowed to differ.** Reset, hardware initialisation and clocking differ between the two machines. The system information block that the BIOS hands to the kernel does not.

## Bus interface in noVa64

12. **Argon sits on the 65816's own bus pins as a second master.** It shares the following with the CPU:
    - A0–A15;
    - D0–D7, with the bank byte multiplexed on them during the clock's low phase, as the 65816 does;
    - `RWB` and `VDA`/`VPA`;
    - `ABORTB`, `IRQB` and `NMIB`;
    - the bus clock.

    To Helium and Neon, Argon's cycles are indistinguishable from the CPU's, so neither needs new logic to serve them. This answers the memory-path question left open in DN-HW-VMCORE-001: Argon uses the standard bus, not a dedicated link into Helium's cache.
13. **Two independent bus enables, both driven by Helium.**
    - The CPU's `BE` keeps the pin's own polarity: high means the CPU drives the bus.
    - Argon's grant is active low with a pull-up. An unconfigured Helium or an empty footprint can therefore never hand Argon the bus.
    - Helium never asserts both enables at once, and leaves a dead cycle at every handover so that one driver is off before the other starts.
    - With both enables released, the bus belongs to Helium itself, for cache fills and the debug agent's external cycles.
14. **The W65C816S gets its own clock.** This clock is a copy of the bus clock, and Helium holds it low whenever the CPU does not own the bus. The separate clock is needed for three reasons:
    - `BE` only tristates the CPU's drivers. The core keeps executing, and its cycles would be lost.
    - The project never uses `RDY` to freeze the CPU, because some W65C816S revisions ignore `RDY` during write cycles. `RDY` is the CPU's own `WAI` output and nothing else drives it.
    - A clock held low is the stall the machine already uses for cache misses and for Neon reads, and the core is fully static.

    The bus clock itself still reaches Neon and Argon unchanged, so Neon's bus tap keeps a single clock. As a side effect, the bus clock's period can differ by owner. Argon's cycles are not bound by the 65816's 8 MHz grade or by its abort timing budget.
15. **Argon ignores `IRQB` and `NMIB`. The 65816 handles every interrupt.** When Helium's interrupt controller has an interrupt pending, it returns the bus to the CPU. The CPU takes the interrupt, and after a context switch the kernel decides whether Argon resumes or is stopped through its doorbell. While Argon does not hold the grant, it waits on the bus, mid-instruction if need be, and loses no state. The arbitration policy between the two masters is still open, but this rule bounds it: a pending interrupt always wins.

**What it costs.**
- **Pins.** Helium needs two more pins, for Argon's grant and for the CPU's separate clock, and its pin budget does not close yet.
- **Bandwidth.** The path is an 8-bit bus. At 8 MHz an NVM32 instruction costs about five bus cycles, which gives roughly 1.5M instructions per second without a cache: 4–10× the 65816 interpreter. With a local instruction cache hitting about 90%, the estimate is roughly 3M per second, 8–20×. **A local instruction cache in Argon's own EBR is therefore mandatory**, and DN-HW-VMCORE-001's threshold of ≥20× is reachable only at the top of that band. Two levers can move the band up: shorter bus cycles while Argon owns the bus, and a data cache or store buffer. All figures are estimates.

## The kernel

16. **The kernel is 65816 native code on both machines, and it is the same binary.** It has to be native on noVa64, because the 65816 is the only processor every noVa64 has. Rule 6 then carries that same binary to noVa128. A noVa128 kernel may use what Argon 2 adds through the same capability discovery as any other program, but it never requires it.
17. **Moving the kernel to NVM32, or splitting it between the two, is withdrawn rather than deferred.** DN-HW-ARGON-001 left this open because Argon 2 made all three options possible. The compatibility rule closes it.

## What this retires

| Item | Source | Status |
| --- | --- | --- |
| 65816 softcore on FPGA-C as a replacement for the physical CPU | Original FPGA-C description | **Retired.** The 65816 is mandatory in noVa64 |
| 65816 softcore kept as a fallback should the VM programme be dropped | Synthesis spec, CPU sheet | **Retired.** Beside a mandatory 65816, a second one has nothing to do and is not faster, because the ceiling is memory |
| NVM32 standalone mode: the core as the only CPU, with a bytecode kernel and a privileged extension | DN-HW-VMCORE-001 | **Withdrawn.** No machine in the family needs a bytecode kernel. Opcodes `$F0–$FE` stay reserved and trap as `ILLEGAL_OP` |
| Degradation path: drop 6502 emulation mode, then 65816 native, to fit Argon 2 on an iCE40 | DN-HW-ARGON-001, abandonment conditions | **Withdrawn.** See below |
| Open question: does a physical W65C816S still exist in noVa128? | DN-HW-ARGON-001 | **Closed: no.** Its consequence remains open; see below |
| Open question: does the kernel stay 65816, move to NVM32, or split? | DN-HW-ARGON-001 | **Closed: it stays 65816 native** |

**Why the modes cannot be traded for fit.** The three modes are the compatibility contract, not features. Without 65816 native mode the noVa64 kernel does not boot. 6502 emulation mode is not even separable from it: every 65816 leaves reset in emulation mode, and the boot code runs there until `XCE`. A core that sheds its 65816 modes is the bytecode-only core of DN-HW-VMCORE-001, which is a noVa64 coprocessor, not a noVa128 CPU. If the ECP5 is unacceptable for the product, the device changes and the modes stay.

## Amendments to earlier notes

- **DN-HW-ARGON-001.**
  - In the Scope table, "FPGA-C optional" means the NVM32 coprocessor.
  - The first abandonment condition is replaced by the paragraph above.
  - The open questions on the physical W65C816S and on the kernel are closed by this note.
  - The open question on the mode-switch mechanism was already settled in Rev A.1 and should be ticked.
- **DN-HW-VMCORE-001.**
  - The "Discrepancy to record" paragraph stands. Its sentence about standalone mode being "the only path to a softcore-only machine" no longer describes a path the project takes.
  - The standalone mode section is withdrawn.
- **DN-SW-VMINTERP-001.** No change. Its gate, its CPU probe and its system information block are already the mechanism that rule 3 relies on.

## Consequences and open questions

- [ ] **Memory-wait semantics in noVa128 (blocking).** With no physical 65816 there is no PHI2 to stretch, so the mechanism noVa64 uses to wait for a cache miss disappears. Helium 2's arbiter, the cache fill, the debug halt and every timing statement inherited from noVa64 must be restated against a handshake between Argon 2 and Helium 2. In that handshake, `ABORTB`, `VPA`/`VDA` and `VPB` survive as interface signals rather than pins. `BE` has no meaning once there is a single master, and the held clock is the mechanism that has to be replaced. This was the consequence the closed question warned about, and it is now the largest open item for noVa128.
- [x] **Argon's memory path in noVa64.** Settled by rules 12–15: the standard bus, shared with the 65816.
- [ ] **How short a bus cycle can be while Argon owns the bus.** This is limited by Helium's cache path on a hit, by Neon's bus tap and its busy protocol, and by the loading of a bus shared by three FPGAs and a socketed CPU. Apart from the cache, it is the only lever on Argon's bandwidth.
- [ ] **Arbitration policy between the 65816 and Argon**: fixed priority, round robin or quota. It must stay within rule 15.
- [ ] **Net names** for Argon's grant and the CPU's separate clock.
- [ ] **Trial synthesis of the NVM32 core on iCE40 HX8K.** This replaces the earlier plan to measure P65816's Fmax, which no longer decides anything. Argon 2's 65816 modes are measured on the ECP5 they are specified for.
- [ ] **Unchanged and still open from DN-HW-ARGON-001:**
  - instruction-cache policy in the 6502 and 65816 modes;
  - whether all memory-mapped registers are idempotent on read, which is needed before the Neon command port is frozen;
  - the package and pin budget for three FPGAs with 16-bit paths.

## Spec mapping

| This note | Synthesis document |
| --- | --- |
| The decision, rules 1–9 | D131 |
| Bus interface, rules 12–15 | D133 (Q14 closed); Q194 |
| The kernel, rules 16–17 | D132 |
| Memory-wait semantics in noVa128 | Q193, blocking (Q159 closed) |
| Kernel location | Q164 closed |
| Sheets carrying it | B.1, B.4, E (intro, E.5, E.13), VM3.2–3.3, VM3.10–3.13, VM3.18, VM3.23–3.24, VM3.28, NV1 (intro, NV1.1, NV1.9, NV1.30, NV1.34–35), A2.3, A2.5 |
