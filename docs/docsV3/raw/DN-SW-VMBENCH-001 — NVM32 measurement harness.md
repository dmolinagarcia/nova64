# DN-SW-VMBENCH-001 — NVM32 measurement harness

*noVa64 · Rev A (draft for discussion)* · 2026-09-20 · @Someone

## Revision history

| Rev | Date | Change |
| --- | --- | --- |
| A | 2026-09-20 | Initial draft. Metrics, pattern counters, benchmark set and the decision criteria that gate the ISA freeze at T4. |

*Review cycle: reviewers, dates and decisions to be recorded here before this note leaves draft status.*

## Purpose

Four encoding candidates are competing in DN-SW-VMISA-001 and none can be decided by argument. This harness turns each of them into a query against measured data, so the ISA freezes on evidence rather than taste.

**It needs no hardware and no compiler.** It runs entirely on the PC against the C11 reference interpreter, which is Stage 1 of the VM programme. That makes it the cheapest possible way to retire the biggest open risk in the ISA.

Sequencing: the reference interpreter produces the counters; the counters decide the candidates at gate T3; the ISA freezes at gate T4 (DN-SW-VMCC-001). This ordering is the reason the freeze moved out of Stage 0 — freezing before any measurement existed is how a permanent encoding mistake gets made.

A second job, later: once the 65816 interpreter exists, the same harness calibrates the cost model against real cycle counts and re-runs the whole history.

## Metrics

Four numbers per benchmark. Counting VM instructions alone is not enough: a `MEMCPY` and a `NOP` count the same, and the candidates under evaluation change precisely how many instructions do the same work.

| Metric | What it measures | Source |
| --- | --- | --- |
| VM instructions executed | Dynamic work | Counter in the reference interpreter |
| **Estimated 65816 cycles** | The metric that decides | Instruction counts × a per-opcode cost table |
| `.text` bytes | Code density (priority #2) and Argon's code bandwidth | Section size |
| Guest memory accesses | The softcore's bottleneck, and cache pressure | Counter in load and store handlers |

**The cost model is the core of this note.** A per-opcode table in 65816 cycles, derived from the handlers in DN-SW-VMINTERP-001. It does not need to be accurate; it needs to be **consistent**, because every use compares variants against each other. When the real interpreter exists, calibrate the table against measured cycles and re-run the stored history so old comparisons remain valid.

**Why the fourth metric exists.** On real hardware, cache misses with PHI2 stopped dominate wall-clock time, and the cycle model cannot see that. Guest memory accesses are the available proxy for cache pressure until hardware measurement is possible.

The opcode histogram is not a fifth metric but the harness's main output: it is what answers the pending ISA questions.

## Pattern counters

The part most harnesses skip. The interpreter records **patterns, not just opcodes**, so each pending ISA question is answered before anything is implemented. This is far cheaper than building four ISA variants and comparing them.

| Counter | Answers |
| --- | --- |
| Adjacent load-op and op-store pairs | Value of fixed-form memory operands |
| Instructions where `rd == rs` | Value of two-address forms |
| Runs of N instructions sharing a destination, with the histogram of N | Value of `WITH`/RP |
| Calls per thousand instructions, and mean stack depth | Value of register windows |
| Branch distance distribution | Whether disp16 is the right size |
| Immediate value distribution | Whether smaller immediate forms would pay |
| Register pressure: live values at each point | Whether 16 registers is right, and what windows would cost |

Each counter is a few lines in the reference interpreter and costs nothing at run time on the real machine, because it exists only in the PC model.

The design rule: **before proposing an encoding change, add the counter that would justify it.** A proposal without a counter is not ready to be argued about.

## Benchmark set

Three groups, each answering a different question.

**Standard** — for comparability with published numbers:

- **Dhrystone**, mandatory for comparability but notoriously unrepresentative: heavy on string handling, light on real arithmetic. Report it, never optimize for it.
- **Coremark**, designed specifically to correct Dhrystone's defects.

**Real noVa64 code** — the only group that says whether a change matters:

- **NVFS code**, the best available candidate today: already written, already has a test suite, and exercises pointer and structure handling.
- A GUI workload once one exists.

**Synthetic kernels** — each isolating one axis, to explain *why* a number moved:

- Array traversal (sequential access, loop overhead)
- Linked-list walk (pointer-intensive, cache-hostile)
- Recursion (call density, stack depth)
- Structure copy (block operations)
- Call-heavy code with shallow bodies (frame cost, the register-window question)
- Expression-heavy arithmetic (the `WITH`/RP question)

Until the compiler exists at gate T2, the synthetic kernels are written by hand in NVM32 assembly — tedious but feasible, and they are the group that matters most for encoding decisions. The real programs wait for the compiler.

## Method and discipline

This is where benchmark suites usually rot. Four rules:

1. **The benchmark set is frozen.** Changing it invalidates the whole history. Additions go in a clearly separated second set with its own baseline.
2. **Results are versioned with the code**, as CSV or JSON in the repository. Every interpreter commit produces a row. What is wanted is a trend over months, not isolated figures.
3. **A variant is measured against the baseline from the same source**, recompiled or reassembled. Never compare hand-tuned code for one variant against generic code for another.
4. **Regression is automatic.** If a change worsens a metric by more than a set threshold, the build fails. Same differential harness already used for NVFS.

**Cost-model calibration.** Until the 65816 interpreter exists, the cycle figures are estimates from the datasheet. When it exists, measure per-handler costs on real hardware, update the table, and re-run the stored history. Record the calibration date in the results so nobody compares pre- and post-calibration numbers without noticing.

**What the harness cannot see:** PHI2 stall time on cache misses. Every number here is a cycle-model number, and on hardware the real figure will be worse by an amount that depends on working-set size. State that caveat next to any throughput claim that leaves this project.

## Decision criteria

Written in advance, so the measurement decides rather than the person reading it. Each threshold is a first proposal to be ratified before the counters are run — setting them afterwards is how a benchmark becomes an argument.

| Candidate | Counter | Adopt if | Otherwise |
| --- | --- | --- | --- |
| Fixed-form memory operands | Adjacent load-op / op-store pairs | More than 20% of executed instructions participate in such a pair | Reject; the restart and microcode cost is not repaid |
| Two-address forms | Instructions with `rd == rs` | Any measurable share — the opcodes are already reserved and the cost is zero | Keep them reserved, unused |
| `WITH` / RP | Runs of ≥3 instructions sharing a destination | More than 20–25% of instructions sit in such a run | Reject; two-address forms already captured the benefit |
| Register windows | Calls per thousand instructions, mean depth | Call-dense code with shallow frames, and spill counts that windows would remove | Reject; the 16-register conflict is not worth 5–15% |
| Superinstruction for a hot opcode | Per-opcode cycle share | Any single opcode exceeds 150 interpreter cycles or a large share of total cycles | No action |

The general rule from DN-SW-VMISA-001 applies: a candidate that adds architectural state must clear a higher bar than one that does not, because {R0–R15, PC} is what makes migration and cheap preemption work.

## Abandonment conditions and open questions

- If hand-writing the synthetic kernels in NVM32 assembly proves slower than expected, cut the set to three — array traversal, linked-list walk, call-heavy — and accept narrower evidence rather than delaying gate T3.
- If the cost model cannot be calibrated to within ±25% of measured 65816 cycles, stop reporting estimated cycles as a headline number and use instruction counts plus memory accesses instead.
- If no candidate clears its criterion, freeze the ISA and retire this harness's encoding role; it continues as a regression gate only.

Open questions:

- [ ] What exactly is the benchmark set for the project as a whole? Dhrystone plus Coremark plus two real noVa64 programs is the proposal; it needs ratifying before anything is frozen.
- [ ] Are the decision thresholds above accepted as written?
- [ ] Where do results live — the interpreter's repository, or a separate one shared with the gateware work?
- [ ] Does the harness also measure the softcore through Verilator, or only the reference interpreter and the 65816?
- [ ] Who owns re-running the history after a cost-model calibration?
