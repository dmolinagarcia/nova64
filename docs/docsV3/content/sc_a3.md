# How to read this document
> How the content is structured within the document

- A3.1 — This document is organized following a well-defined hierarchy of four levels.
  1. The **part** is the broadest division. The *Introduction*, which you are reading right now, presents the project, its goals, and how the documentation is organized. *Hardware* describes the physical aspects of the noVa64 laptop, while *Software* focuses on the development of **noVaOS**, the operating system noVa64 uses. The *Emulator* part introduces *noVa64-emu*, a web-based emulator for our computer. It will also guide you through coding and running your own emulator. *Project* contains the breakdown of the noVa64 build, with the development paths and a log of all open and closed questions during the lifetime of the project. The *Specifications* part contains all detailed reference documents, and finally *Appendices* house the glossary, the table and figure indices, and a changelog.
  NOTE: There is an extra, temporary part, *AI*. It currently contains all AI-generated content, and it is under human review.
  2. **Areas** split bigger parts into more manageable pieces of information.
  3. A **sheet** is an individual document within an area. Each sheet is identified by a unique letter.
  4. Finally, within each sheet, we have a list of **items**. Each item is designated by its parent sheet and a unique sequence number. This item is A3.1 (the first item in sheet A3). Each item can be individually linked from anywhere in the whole documentation.

- A3.2 — **All content in the *AI* part** is the result of a dialogue with Claude AI lasting several weeks. As of v0.2.5, **8.1% of the whole documentation** has been human-reviewed and migrated outside that part, with everything else still residing in **AI**. This item is updated as the review process advances.

- A3.3 — **The letter for each sheet is its unique identifier.** Every cross-reference in this document points to a sheet's letter or a unique item within it — [sheet V7](sc_v7) for *Emulator environment*, [A3.3](sc_a3#a33) for this item. A sheet's letter never changes: changing it would require rewriting every reference to it and, more importantly, would break every saved link to the documentation. The only exception is the *AI* part, as its placement is temporary and its sheets are eventually moved to their final location. Human-reviewed sheets keep their letter permanently.
  NOTE: **A numeric suffix indicates a relation between sheets.** It is used in two different cases. It may indicate a close relation between contents, such as the disclaimer in [A1](sc_a1), which together with the Vision and philosophy in [A2](sc_a2) and this sheet [A3](sc_a3) form the *Introduction* part of the documentation. But this scheme is also used when a sheet's content is very extensive, and splitting it into different sheets is more convenient, such as in *Filesystem*, *Emulation*, *Neon* and some others.

- A3.4 — **Every item is numbered and can be addressed individually.** This item is `A3.4`, the fourth item of sheet A3. Every item is a claim, a fact about noVa64. A reference like `(→ [Q41](sec_ai_q#q41))` means an open question exists about the item, and the item may change depending on how the question closes.
  NOTE: Each item number is a fixed reference and never changes. Use it to reference an item if needed, never a page number.

- A3.5 — **A `TEST ▸` line is a verification criterion**, not commentary: it says what has to be observed before the item is considered true. A dimmed line below an item is a note — context, a caveat, or the reason a decision moved.

- A3.6 — **Chips mark the state of a question.** An outline chip [[open]] marks an open question that does not block progress; a filled chip [[!blocking]] is a contradiction or a dependency that stops work until it closes. Open questions are registered in [sheet X2](sc_x2), each as a numbered item. Whenever an open question is resolved, the resolution is noted in the same item. The decision itself is logged in [sheet X1](sc_x1) as an item of its own, and a decision later replaced by another is marked [[!superseded by X1.12]].

- A3.7 — **The project roadmap is described in sheets [P1](sec_ai_p1)–[P4](sec_ai_p4)** with its build steps, the expected result of each one, and the goal it serves. Banners mark the milestones from [A2.4](sc_a2#a24).
  NOTE: These four sheets are a project plan without dates: each step builds on the results and lessons of the previous ones.

- A3.8 — **Figures and tables are numbered across the whole document**, not per sheet. [Z2](sec_ai_z2) lists all figures and tables. [Z3](sec_ai_z3) is the **glossary**, organized by domain. Every acronym listed there is also expanded where it is first used.

- A3.9 — **Text conventions.**
  - An address is written `$FF:8000` when the bank is relevant.
  - An address is written flat, `$FF8000`, when it represents an operand.
  - Register and signal names are in `CODE` font and UPPERCASE.
  - **Signal polarity is active low by default**, but always stated. Active low signals are named `SIGNAL_N`. Pin names given by the manufacturer are honoured, though. The `ABORTB` CPU pin is driven by the `ABORT_N` net.

- A3.10 — **No required reading order.** The noVa64 specification is conceived as a reference guide, so there is no best way to read it. The **Introduction** part and the **Architecture** area provide a general view of the system, and contain the knowledge needed to make sense of the rest of the documentation. Having read that, all other areas can be read on their own, in any order. In any case, to get the best understanding of the system, a full front-to-back read is recommended.

- A3.11 — **Build work is organized in series.** A series is an ordered sequence of steps that builds one thing on one target. Each series is named by a three-letter code, and each of its steps by that code and two digits, `ABC-nn`. No sheet is identified by three letters alone, so a step is never mistaken for an item. Steps run in order, each starting from the result of the one before, and a step is the smallest piece of work whose result can be verified.
  NOTE: **A series explains how a machine or software is built** but it does not specify how it works. That belongs to the sheet that specifies it.
  NOTE: **Not every series builds noVa64, and not every series needs hardware.** A series may run on a board that is not noVa64 at all, or on a development machine.

- A3.12 — **Every sheet specifies the target machine.** A series may run on hardware that is not noVa64 at all — a training rig, a prototype carrier, a development machine — and none of that changes what these sheets describe. **Prototypes are temporary packaging of the same design, not variants of it.** Where a target cannot exercise part of the design, because it has no battery, no charger or no panel, the difference is recorded as a gap by the series that hit it, never as a second specification competing with the sheet it came from.
  NOTE: **The revision line in the document footer carries the document's own version first, then a revision per subsystem.** The subsystems revise on their own schedule, independently of the document version, and **REV A means first issue rather than absent**: a subsystem is listed at REV A because nothing in it has been superseded yet.
  NOTE: **A superseded revision is not silently overwritten.** The correction lives where a reader meets the problem, in the sheet that owns it, with the reasoning in the decision log.

- A3.13 — **Colour in the document is decorative** and only text decorations, such as weight, fill, etc., have meaning. This allows every sheet to be printed in black and white without losing meaning. The following table explains every mark.
  NOTE: **The same rule applies to figures**, where the width and pattern of a trace determine the meaning and type of the net represented.

| Mark | Form | Meaning |
|---|---|---|
| Item number | Bold, at the head of the item | The anchor a cross-reference points at (`sc_a3#a313`) |
| `TEST ▸` line | The ▸ prefix, on its own line | The verification criterion for the item above it |
| Note | Dimmed, on its own line | Context, a caveat, or the history of a decision |
| Chip [[open]] | Outline | Open, tracked in [sheet X2](sc_x2), not blocking |
| Chip [[!blocking]] | Filled | Blocking — a contradiction or an unresolved dependency |
| Milestone banner | Framed band, ■ at its head | Apple II, or Amiga |


