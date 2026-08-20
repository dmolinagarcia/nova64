# Vision and philosophy
> what it is · what for · under what rules · how to read it

Project framework: what is being built, what finishing it means, and what principles govern every decision.

- A.1 — What it is: an original portable personal computer built around the W65C816S, designed from scratch as a learning exercise in electronics, PCB design, gateware, computer architecture, and OS design.
- A.2 — The counterfactual it answers: **the 16-bit machine Commodore never built.** Had it not bought Amiga and gone looking elsewhere for a 16-bit future, the obvious move was to follow its own 8-bit line onto the 6502's natural successor. noVa64 is that machine — the Commodore that would have come next.
- A.3 — Which settles why the CPU is a 65816 and not something better behaved. Lineage first: it is where the 6502 family goes. Then the practical case — 6502 simplicity kept, 24-bit addressing, 16-bit registers, and, decisively, the **ABORT** pin. ABORT is what separates a machine that can protect memory from one that merely divides it (→ [sheet E](sec_e)).
- A.4 — Inspiration and goals: two milestones define success. **Apple II Milestone** — the machine boots on its own to a prompt. **Amiga Milestone** — preemptive multitasking with a windowed GUI.
  NOTE: The second is named for the capability, not the lineage: it is the bar the Amiga set, cleared by the machine Commodore would have built instead of buying it.
- A.5 — **Compatible with nothing.** This machine runs its own software; it is not an emulator and no existing binary is expected to work on it. That is a freedom, not a gap — it is what removes any need for cycle-exact behaviour, and therefore what licenses a cache with variable latency, a stalled clock, and a soft core that need not match the silicon cycle for cycle. Several later decisions rest on this one and would collapse without it.
- A.6 — Non-negotiable principles: every component hand-solderable (TQFP / PLCC / TSOP / 0.5 mm max.), everything at 3.3V, open toolchain (KiCad · Yosys · nextpnr · IceStorm · pico-sdk), incremental progress with an explicit verification criterion per stage, and unpopulated footprints to preserve future expansion.
- A.7 — Scope of this document: an index + outline tying all the pieces together. Detail lives in the sheets: `hoja-1-2-rp2040.md` … `hoja-1-6-conectores.md`, `hoja-pines-ice40.md`, `plan-implementacion-65816.md`, `virtual-memory-concepts.md` (REV A), `helium-debug-agent.md` and the diagram `placa-65816.html`.
  NOTE: **Power is the exception, and the direction of travel.** Sheets [C](sec_c) and [S](sec_s) are no longer an outline of a document held elsewhere — they *are* the power specification at REV C, and `hoja-1-1-alimentacion.md` is superseded by them rather than summarised by them. Everything living hangs here from now on; the remaining external sheets are the ones that have not yet been folded in.
  NOTE: That diagram is still at REV B and now trails this document badly — it shows HX4K parts, 2 MB of SRAM, 32 MB of SDRAM and the video PSRAM, all of them superseded by [D13–D17](sec_q). Treat it as historical until a REV C exists.

## How to read this document — ten areas, twenty-three sheets, and a letter that never moves.

The sheets are grouped into ten **areas**, and the order of the areas is the reading order: Introduction, Architecture, Power, Embedded control and boot, CPU and system, Memory and storage, Video, Software, Project and build, and the Appendix. The sheet *number* follows that order. **The sheet *letter* does not, and that is deliberate.**

- A.8 — **The letter is the sheet's address, not its position.** Letters were handed out in the order the sheets were written, and every cross-reference in the prose points at one — [sheet T](sec_t) for Neon, [T.53](sec_t#t53) for a single item inside it. Renumbering them to match the areas would rewrite several hundred references at once and break every link anyone has kept, so it is not done: a new sheet still takes the next free letter and is placed in whichever area it belongs to.
- A.9 — **Every item is numbered and individually addressable.** `F.4` is the fourth item of sheet F, and it can be linked to directly; the reference `(→ [Q41](sec_q#q41))` means an open question is tracked in [sheet Q](sec_q) and this item depends on how it closes. Items are the unit the document argues in — a sheet is a container, an item is a claim.
- A.10 — **A gold `TEST ▸` line is a verification criterion**, not commentary: it says what has to be observed before the item is considered true. A dimmed line below an item is a note — context, a caveat, or the reason a decision moved.
- A.11 — **Chips mark the state of a question.** A mint chip [[open]] is something undecided but not in the way; a gold chip [[!blocking]] is a contradiction or a dependency that stops work until it closes. Both are resolved in [sheet Q](sec_q), which holds the decision log and the open questions with their numbers.
- A.12 — **The checkboxes belong to [sheet P](sec_p)** and only to it: that sheet is the build, and its boxes carry the four states below. The gold banner marks a milestone from [A.4](sec_a#a4).
- A.13 — **The twelve figures are numbered across the whole document**, not per sheet, and each one lives in the sheet it illustrates. The [appendix](sec_z) lists all twelve with their sheet, and carries the glossary for every acronym used anywhere.

| Mark | Meaning |
|---|---|
| `A.7` in mint | Item number — the anchor a cross-reference points at (`sec_a#a7`) |
| `TEST ▸` in gold | The verification criterion for the item above it |
| Dimmed line | A note: context, caveat, or the history of a decision |
| Mint chip [[open]] | Open, tracked in [sheet Q](sec_q), not blocking |
| Gold chip [[!blocking]] | Blocking — a contradiction or an unresolved dependency |
| Checkbox | Build state in [sheet P](sec_p): pending · done · in progress · optional |
| Gold banner | A milestone: Apple II, or Amiga |
