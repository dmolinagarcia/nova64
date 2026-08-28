# Handoff — 65816 Softcore on FPGA: Maximum-Speed Study

**Date:** 2026-08-11
**Project:** DANI-65816
**Scope of this session:** Feasibility and speed ceiling of a 65816 soft core, oriented toward "an original computer running as fast as possible," with freedom to modify the core as long as binary/ISA compatibility is preserved.

---

## 1. Question addressed

How fast can a 65816 core run on an FPGA, and is there hard data? Then, narrowed to: a soft core for an **original computer at maximum speed**, where the core may be modified freely provided existing 65816 code still runs.

---

## 2. Key findings — physical chip vs. soft core

- **Physical WDC 65C816** is specified from **1 to 14 MHz** depending on the grade. That is the silicon ceiling, not a core limit.
- **Soft cores** are not bound by that number. However, **published Fmax data is scarce**, because existing cores are built to hit a *native target speed* (console/machine accuracy), not to maximize clock frequency.

### Available cores (starting points)
- **P65816 (srg320)** — VHDL, microcoded, cycle-exact. Basis for the MiSTer SNES core and the derived Apple IIgs core. Most complete/compatible open option.
- **MiSTer Apple IIgs core** (adapted from srg320) — validated in hardware at selectable speeds up to **14.3 MHz** (2.86 stock, plus 3.6 / 4.8 / 7.2 / 14.3). Note: this is a *system compatibility* limit, not the isolated core's Fmax.
- **FT816** — not 100% cycle-compatible but close; conceived more as an accelerator than a faithful emulator. Potentially a better base when speed matters more than cycle exactness — instruction coverage must be verified.
- **WDC official soft IP** (paid) and various academic cores on GitHub.

---

## 3. Strategy for "own computer at maximum speed"

### 3.1 Compatibility model
Only **programmer-visible compatibility** is required — *not* cycle-exact behavior. Preserve:
- The 92 opcodes and all addressing modes
- The **E flag** (6502 emulation mode) and **M/X flags** (register widths)
- The full processor status register
- Interrupt vectors and interrupt-entry semantics

Everything else — microarchitecture, pipelining, cycle counts, external bus protocol — is free to change. Almost all speed gains live in what can be discarded (the faithful reproduction of the original multiplexed bus dance: dummy reads, bank-on-data-bus setup, etc.).

### 3.2 Two distinct bottlenecks (do not conflate)
1. **Core Fmax (clock frequency).** Critical path typically runs through microcode ROM + ALU + address calculation. Mitigations: register the microcode ROM output (free if it lives in BRAM); watch the **BCD/decimal-mode adder**, a common ugly contributor to the critical path.
2. **Cycles per instruction (CPI) — the real prize.** The original 65816 spends 2–7 cycles/instruction, many wasted by the physical bus. Flat internal memory lets several of those accesses collapse. Lowering average CPI yields more effective throughput than raising the clock, and is cheaper on the critical path.

### 3.3 The true ceiling is memory
A fast core is useless if memory can't keep up. Single-cycle execution needs on-chip Block RAM. Once execution goes to external SRAM/SDRAM, latency dominates — the answer is **caching**, which is fully legitimate because a cache is invisible to the ISA.

### 3.4 Honest caveat
The 65816 is an accumulator architecture: memory-traffic-heavy and fairly serial. Even with pipelining, data dependencies cap the gains — do not expect modern-CPU-style scaling; the ISA offers limited ILP. "Fastest possible 65816" is a well-defined goal, but a bounded one.

### 3.5 Ballpark (generic mid-range FPGA)
A clean core at ~80–100 MHz from BRAM with reduced CPI ≈ **20–30× an original 3 MHz machine**, or **~6–7× a physical WDC at 14 MHz**, while keeping binary compatibility.

---

## 4. ⚠️ Project reconciliation — DANI-65816 reality (iCE40)

The numbers in §3.5 were quoted for **mid-range FPGAs (Artix-7, Cyclone V)**. DANI-65816 uses **iCE40 (TQ144)**, where those figures **do not transfer directly**. This section flags the gaps so the next session starts calibrated.

- **Fmax:** The iCE40 realistic ceiling for *complex* logic is ~50 MHz, and a full microcoded 65816 is complex logic. Expect the core to land **below the generic 50–100 MHz range** — plausibly in the low tens of MHz. **Requires synthesis (Yosys/nextpnr) to confirm.** No number should be trusted until placed-and-routed on the target part.
- **"Run from BRAM" is not viable on iCE40.** Total EBR is **128 Kbit (~16 KB)**, shared with everything else in the FPGA. There is no room to execute a meaningful program from local block RAM. The "BRAM-pure" path from §3.3 is off the table for this platform.
- **A softcore here is FPGA-C, not a standalone speed demon.** In the current architecture FPGA-C (the reserved, initially-unpopulated softcore slot) would ride the **existing shared bus and the FPGA-A managed cache** (256 KB SRAM cache over SDRAM, 10 ns). Its effective speed is therefore gated by the *same* memory hierarchy and PHI2 window (~14 MHz) as the physical W65C816S — not by raw core Fmax.
- **Where a softcore could still win:** reduced CPI, added features, or eliminating the physical PLCC chip — **not** raw clock. Any real advantage depends on giving FPGA-C a more direct/wider path to FPGA-A's cache than the physical 65816's multiplexed external bus allows. That path does not currently exist in the design.

---

## 5. Open questions for the next session

1. **Role of FPGA-C:** Does the softcore *replace* the physical W65C816S on the shared bus, or *coexist* with it? This determines everything downstream.
2. **Memory path:** If populated, does FPGA-C use the standard shared bus into FPGA-A (same PHI2/cache constraints as the physical CPU), or does it get a dedicated, wider/faster private link to FPGA-A's cache? Without the latter, the softcore's speed advantage is marginal.
3. **Actual iCE40 Fmax:** Synthesize a candidate core (start from srg320's P65816, stripped of SNES bus-timing fidelity) on the target iCE40 part and read the real placed-and-routed Fmax. Everything above §4 is estimate until this exists.
4. **Base core choice:** srg320 P65816 (clean up, flat memory interface) for maximum compatibility, vs. FT816 for speed-first — pending an instruction-coverage audit of FT816.
5. **Is the softcore even the right lever?** Given that memory (not the core) is the DANI-65816 ceiling, effort may be better spent on the cache/memory subsystem than on the core itself. Worth an explicit decision before investing in FPGA-C.

---

## 6. One-line summary

A modified-but-code-compatible 65816 soft core can far exceed the 14 MHz silicon on a mid-range FPGA — but on DANI-65816's iCE40 platform the wins are bounded by ~50 MHz-class logic, negligible on-chip RAM, and a shared memory hierarchy that already gates the physical CPU. The softcore's value here is CPI/features/part-elimination, not raw clock, and only if given a better path to memory than the physical chip has.
