# Handoff — moving the NVM32 design notes into the noVa specification

*Brief for Claude Code* · 2026-09-20 · @Someone

## The task

Five design notes for the NVM32 virtual machine have been drafted as Claude Docs and need to be moved into the noVa specification repository as version-controlled Markdown, under the project's existing DN conventions. **This is a transcription and integration job, not a design job.** The content is settled; what is needed is the notes in the repo, cross-referenced correctly, with the open questions carried across as tracked TODOs and the affected existing notes updated to point at them.

Context in one paragraph: noVa64 is a from-scratch 65816 portable computer with FPGA subsystems (Helium for MMU, cache and arbitration; NEON for video and audio) and a custom OS. NVM32 is a 32-bit register virtual machine with a 24-bit address space, designed so the *same binary* runs interpreted on the 8 MHz W65C816S and natively on a future FPGA softcore. The VM programme is **deferred**: noVa64 continues with native 65816 code compiled by Calypsi, and NVM32 is revisited once the kernel runs its first user process. These notes exist so that reopening is implementation work rather than redesign.

The source text lives in the Claude Docs listed in the next section; each needs exporting or copying into the repository.

## Inventory

Five notes, in dependency order. All are Rev A drafts for discussion except the ISA, which is Rev B.

| Note | Rev | Covers |
| --- | --- | --- |
| **DN-SW-VMISA-001** | B | The ISA: 16 × 32-bit registers, 16-bit code units, flagless compare-and-branch, 32-bit-clean pointers, guest memory map, bit-identical semantics tables, ILP32-24 ABI, NVX format, encoding candidates pending measurement |
| **DN-SW-VMINTERP-001** | A | The 65816 interpreter: direct-page register window, dispatch loop, handlers with cycle estimates, the native↔bytecode gate, interrupts and preemption, syscall forwarding |
| **DN-HW-VMCORE-001** | A | The FPGA-C bytecode coprocessor for noVa64: multi-cycle FSM, Helium bus mastering, host integration, verification, standalone-mode sketch |
| **DN-SW-VMCC-001** | A | The C toolchain: vbcc backend, vasm CPU module, vlink packaging, runtime and libc, T0–T5 gates. Its appendices A and B raised the change requests now folded into the two notes above |
| **DN-SW-VMBENCH-001** | A | The measurement harness: metrics, pattern counters, benchmark set, discipline, and the decision criteria that gate the ISA freeze |

Also drafted, and already in the docs set: **DN-HW-ARGON-001 Rev A**, the noVa128 tri-mode core (6502 emulation, 65816 native, NVM32 native), instruction-compatible with the 65816 but not cycle-compatible. It is a future-version note, not part of noVa64, but it shares the ISA, the state-window layout and the gate mechanism, so it moves across with the others.

**Reading order for anyone new:** VMISA, then VMINTERP, then VMCORE; VMCC and VMBENCH are independent of each other and depend only on VMISA.

## Conventions to follow

Match what the repository already does; the points below are the project's established practice, not new rules.

- **Naming:** `DN-[DOMAIN]-[TOPIC]-NNN`. Domains in use: `SW`, `HW`, `FS`, `PLAN`. Place files where the existing notes live and follow their extension and front-matter pattern exactly.
- **Language: English.** All documentation is in English regardless of the language of the conversation it came from.
- **Every note carries:** a revision history table with dates and rationale, interface and boundary tables, alternatives considered with reasons for rejection, and **explicit abandonment conditions**. All five drafts already have these; preserve them rather than trimming.
- **Withdrawn decisions stay visible.** The project records errors and retracted revisions with their rationale rather than deleting them. DN-SW-VMISA-001's revision history contains three corrections to its own Rev A — keep them.
- **Open questions** are carried as checklists in the notes. Mirror them into whatever issue tracker the repo uses, if any, and link back.
- **Cross-references** use the DN identifier, not a file path or URL, so the notes survive reorganisation.

Updates needed to **existing** notes, so the new material is reachable:

| Existing note | Change |
| --- | --- |
| Project overview / architecture | Record that FPGA-C's role changed from "softcore 65816" to bytecode-native, and that the VM is deferred with a stated reopening condition |
| DN-SW-EMU-001 (PC emulator) | Widen scope to host the C11 reference interpreter, the golden model for all executors; keep the softcore in Verilator. **Require `WDM` to be modelled as a two-byte no-op** in noVa64 mode, or the CPU probe misreports |
| NEON note | Memory-mapped registers idempotent on read; software-visible register map stable across noVa64 and noVa128 |
| Helium note | Define the capability register (FPGA-C NVM32 core present, MMU version, cache size); same stability rule for the register map |
| BIOS note (create if absent) | Owns the `WDM` CPU probe, gate selection and the **system information block**, whose format is the BIOS-to-kernel contract. BIOS implementations may differ per machine; the block may not |
| Kernel / syscall notes | Numbered service table; banks `$00`–`$01` and one shared read-only page for the system information block reserved from the first loader; no cycle counting for time; no self-modifying code |
| DN-PLAN-PHASES-001 | Add the VM programme's reopening condition and the T0–T5 toolchain gates alongside the existing E/G/K series |

## Decisions that must survive intact

These are load-bearing. If a transcription choice would weaken one of them, stop and flag it rather than paraphrasing it away.

1. **Architectural state is {R0–R15, PC} and nothing else.** This is what makes a process migratable mid-execution between interpreter and softcore, and what makes preemption cheap. Any future proposal adding architectural state — flags, a window pointer, an implicit accumulator — pays against this and must say so.
2. **Bit-identical semantics across all executors**, including shift counts ≥ 32, divide by zero, `INT_MIN / −1`, illegal opcodes and fault behaviour. Division follows the RISC-V conventions, deliberately diverging from eBPF; that divergence is recorded so nobody "fixes" it later.
3. **Pointers are 32-bit clean**: the top 8 bits must be zero and are checked, not ignored. This is the 68000 lesson applied on purpose.
4. **Guest code never calls 65816 machine code directly.** Only the portable `SYSCALL` trap crosses the boundary, which is what lets the same binary run on a softcore. Native extension is provided as *data* — a numbered service table — never as a callable address.
5. **One kernel source, both machines.** Native code enters the VM through a single indirect vector resolved at boot from a Helium capability register. On noVa64 it points at the interpreter; on a core with native NVM32 mode, at a `WDM #VMENTER` gate. `WDM` (`$42`) is WDC's reserved expansion opcode and a two-byte no-op on a real 65816. Never implement this with self-modifying code.
6. **Preemption does not require VM-instruction atomicity.** The original report's rationale was wrong and is corrected in DN-SW-VMINTERP-001: a full 65816 context switch implicitly saves a half-finished VM instruction. Atomicity is required for faults and for exporting state only.
7. **Cycle counts do not measure time**, because PHI2 is stopped on cache misses. Every throughput figure in these notes is a cycle-model estimate; the scheduler quantum comes from the fixed frequency reference.
8. **The ISA freezes at gate T4, on measured evidence.** The four encoding candidates — fixed-form memory operands, two-address forms, `WITH`/RP, register windows — are decided by the T3 histogram with thresholds written in advance in DN-SW-VMBENCH-001. A stack-based VM was evaluated and rejected outright, with reasons; it is not a pending option.
9. **Calypsi and NVM32 ABIs meet only at the marshalling layer.** Calypsi uses 16-bit `int`; NVM32 is ILP32. Guest C is compiled by the retargeted NVM32 compiler, never by Calypsi, and compiled objects are never shared across that boundary.

**Also load-bearing — portability to noVa128.** NVM32 applications are binary-compatible across noVa64 and noVa128 by construction. The native kernel is binary-compatible only if four requirements hold from now: no cycle counting for time, no self-modifying code, I/O registers idempotent on read, and a stable software-visible register map. The BIOS may differ per machine; the system information block it hands to the kernel may not. These are specified in DN-HW-ARGON-001 and must land in the kernel, NEON, Helium and BIOS notes, not only in the VM notes.

## What not to do

- **Do not implement anything.** No interpreter, no backend, no Verilog, no benchmark harness. The VM programme is deferred; this is documentation work.
- **Do not re-decide settled questions.** If something looks wrong, raise it as a comment or an issue rather than silently changing it. The notes record *why* each choice was made, and several were reversed once already.
- **Do not resolve the open questions.** They are deliberately open and several depend on measurements that do not exist yet.
- **Do not tidy away the corrections.** Errors and withdrawn revisions stay visible with their rationale; that is project practice.
- **Do not treat the cycle and resource figures as measurements.** Interpreter throughput, softcore speedup and LUT counts are estimates with a ±20% band. Keep the caveats attached wherever a figure is quoted.
- **Do not translate the notes.** Documentation is English even though the design conversation was in Spanish.
- **Do not merge the notes.** Five separate notes with distinct domains and identifiers, matching how the project already splits its documentation.

## Open questions and acceptance

The notes carry their own checklists. These are the ones that cut across more than one note and should be tracked at project level rather than inside a single document:

- [ ] **Does a physical W65C816S still exist in noVa128?** If not, PHI2 stretching disappears as the memory-wait mechanism and the whole wait semantics must be redefined. Largest unresolved item in the set.
- [ ] **Instruction-cache coherence**, in two places: the softcore caching guest `.text` across page remapping, and Argon's 65816 modes against self-modifying code. Both need a decision before implementation.
- [ ] **Are all memory-mapped registers idempotent on read?** Needed by Argon's 65816 modes and cheap to guarantee now; expensive after the NEON command port is frozen.
- [ ] **Does floating point get opcodes, or stay soft float?** Blocks the T4 ISA freeze.
- [ ] **Which benchmark set is the project's standard**, and are DN-SW-VMBENCH-001's decision thresholds accepted as written? Both must be ratified before any measurement is used to justify a change.
- [ ] **Does the kernel stay 65816-native, move to NVM32, or split?** Argon makes all three possible; the coprocessor model does not.
- [ ] **Licensing**: a vbcc carrying an NVM32 backend is a modified version, redistributable only with the author's consent. Fine privately; decide before anything is published.

**Acceptance checks** for this handoff:

1. All six notes exist in the repository, in English, under correct DN identifiers, with revision histories and abandonment conditions intact.
2. Cross-references resolve in both directions — each note's dependencies are named, and the existing notes listed above point at the new ones.
3. The four "settle now" items from DN-SW-VMISA-001's status section appear in the kernel, loader, NEON and emulator notes, not only in the VM notes.
4. The open questions above are tracked wherever the project tracks work, linked to their notes.
5. Nothing was implemented.
