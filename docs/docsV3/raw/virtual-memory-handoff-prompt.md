# Handoff prompt — Virtual memory concepts document

Copy everything below the line into the consolidation conversation.

---

## Context

I am integrating a new reference document into the DANI-65816 documentation set.
It was produced in a separate conversation and needs to be reconciled with the
existing material.

DANI-65816 is an original portable computer built around a W65C816S CPU with
three iCE40 FPGAs. FPGA-A implements the MMU, cache controller and memory
arbitration. The machine uses full per-process paging: each process sees a 16 MB
virtual address space.

## What the source conversation produced

A conceptual reference document, `virtual-memory-concepts.md`, explaining
paged virtual memory from first principles and fixing the vocabulary used
throughout the rest of the project documentation. It exists because the acronyms
scattered across the architecture notes (TLB, PTE, ASID, EBR, walker, frame) had
never been defined anywhere in the document set.

The document is **explanatory, not normative**. It does not introduce or change
any design decision. Every number in it is taken from decisions already settled
elsewhere. If it contradicts an architecture document, the architecture document
wins and this one is wrong.

## Document structure

| Section | Content |
|---|---|
| 1 | The three problems virtual memory solves: relocation, protection, fragmentation |
| 2 | Paging: pages, frames, address field split, page table, PTE |
| 3 | The translation path: MMU, TLB, ASID, hardware walker, ASCII flow diagram |
| 4 | Failure cases: page fault (P=0), permission violation, ABORTB mechanism |
| 5 | Why FRAME is 16 bits while VPN is 13 |
| 6 | Glossary — 24 entries |
| 7 | Numeric summary table |

## New reasoning worth preserving

Section 5 is the only part containing argument rather than definition. It answers
why the VPN and FRAME fields have different widths, and the reasoning is not
recorded anywhere else in the project:

1. **The two fields measure different things.** VPN measures how much memory a
   single process sees; FRAME measures how much memory the machine has. There is
   no reason for them to match.
2. **The virtual side is not a choice.** The 65816 emits 24-bit addresses. With
   2 KB pages, 24 − 11 = 13 bits of VPN. The number falls out of a subtraction.
3. **The physical side is a choice.** The physical address is assembled by FPGA-A
   after translation, so its width is set by the FRAME field alone. 16 bits ×
   2 KB gives a 128 MB ceiling — the MMU breaks the 65816's architectural barrier
   at system level without breaking it at process level. The 3-bit margin means
   up to 8 complete virtual address spaces can be resident simultaneously.
4. **16 rather than 15 is free.** The planned 48 MB pool needs only 15 bits, but
   the PTE flags occupy exactly 16 bits, leaving the other half-word unused.
   Rounding FRAME to 16 costs nothing and simplifies the walker: FRAME is the low
   half-word, flags the high half-word, so the FPGA reads 32 bits and splits them
   with no shifting or masking.

## Parameters as stated in the document

| Parameter | Value |
|---|---|
| Page size | 2 KB |
| Virtual address | 24 bits (16 MB per process) |
| VPN | 13 bits (8,192 pages) |
| Offset | 11 bits |
| PTE | 32 bits |
| FRAME field | 16 bits |
| Physical address | 27 bits (128 MB ceiling) |
| Page table | 16 KB per process |
| Planned physical pool | 48 MB = 24,576 frames |
| TLB entries | 32–64, in FPGA-A EBR |
| Page walk cost | ~4 cycles (10 ns SRAM) |

Please verify these against the architecture documents before integrating. The
128 MB ceiling and the 48 MB pool figure in particular should be cross-checked.

## Editorial convention adopted

Every acronym is expanded on first use, in place, with the English expansion and
a translation — not left to the glossary. The glossary is deliberately redundant
and serves as quick reference only. If the rest of the document set adopts a
different convention, this document should follow it instead.

## What I need

1. Tell me where this document belongs in the set, and whether it should be
   referenced from the architecture documents or kept standalone.
2. Check the parameter table above against the settled decisions and flag any
   drift.
3. Identify vocabulary used elsewhere in the documentation that is missing from
   the glossary.
4. Confirm the document does not duplicate material already covered in the
   existing synthesis document.
