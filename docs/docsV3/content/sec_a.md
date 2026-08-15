# Vision and philosophy
> what it is · what for · under what rules

Project framework: what is being built, what finishing it means, and what principles govern every decision.

- A.1 — What it is: an original portable personal computer built around the W65C816S, designed from scratch as a learning exercise in electronics, PCB design, gateware, computer architecture, and OS design.
- A.2 — The counterfactual it answers: **the 16-bit machine Commodore never built.** Had it not bought Amiga and gone looking elsewhere for a 16-bit future, the obvious move was to follow its own 8-bit line onto the 6502's natural successor. noVa64 is that machine — the Commodore that would have come next.
- A.3 — Which settles why the CPU is a 65816 and not something better behaved. Lineage first: it is where the 6502 family goes. Then the practical case — 6502 simplicity kept, 24-bit addressing, 16-bit registers, and, decisively, the **ABORT** pin. ABORT is what separates a machine that can protect memory from one that merely divides it (→ [sheet E](sec_e)).
- A.4 — Inspiration and goals: two milestones define success. **Apple II Milestone** — the machine boots on its own to a prompt. **Amiga Milestone** — preemptive multitasking with a windowed GUI.
  NOTE: The second is named for the capability, not the lineage: it is the bar the Amiga set, cleared by the machine Commodore would have built instead of buying it.
- A.5 — **Compatible with nothing.** This machine runs its own software; it is not an emulator and no existing binary is expected to work on it. That is a freedom, not a gap — it is what removes any need for cycle-exact behaviour, and therefore what licenses a cache with variable latency, a stalled clock, and a soft core that need not match the silicon cycle for cycle. Several later decisions rest on this one and would collapse without it.
- A.6 — Non-negotiable principles: every component hand-solderable (TQFP / PLCC / TSOP / 0.5 mm max.), everything at 3.3V, open toolchain (KiCad · Yosys · nextpnr · IceStorm · pico-sdk), incremental progress with an explicit verification criterion per stage, and unpopulated footprints to preserve future expansion.
- A.7 — Scope of this document: an index + outline tying all the pieces together. Detail lives in the sheets: `hoja-1-1-alimentacion.md` … `hoja-1-6-conectores.md`, `hoja-pines-ice40.md`, `plan-implementacion-65816.md`, `virtual-memory-concepts.md` (REV A), `fpga-a-control-registers.md` and the diagram `placa-65816.html`.
  NOTE: That diagram is still at REV B and now trails this document badly — it shows HX4K parts, 2 MB of SRAM, 32 MB of SDRAM and the video PSRAM, all of them superseded by [D13–D17](sec_q). Treat it as historical until a REV C exists.
