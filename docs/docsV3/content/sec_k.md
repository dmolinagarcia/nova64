# Virtual memory — concepts and glossary
> first principles · why the field widths · reference figures

Explanatory groundwork, not normative: it explains paging from first principles and gives the reasoning behind the field widths, so that the machinery in [sheet L](sec_l) can be read without stopping to decode acronyms. Term-by-term definitions live in the [glossary, sheet Z](sec_z). It introduces no design decision — where it disagrees with an architecture sheet, the architecture sheet wins. Source: `virtual-memory-concepts.md` (REV A).

- K.1 — Why it exists — three problems it solves at once: **relocation** (the program no longer needs to know what address it will be loaded at), **protection** (no process can touch another's memory or the kernel's) and **fragmentation** (free memory no longer breaks into holes nothing fits into). The process lives in a fiction: it believes it owns the full 16 MB, starting at zero.
- K.2 — The core idea: the address space is cut into fixed-size chunks — **pages** on the virtual side, **frames** on the physical side, both 2 KB. Translation answers a single question: which physical frame holds virtual page N? The offset within the page is never translated; the low bits pass straight through.
- K.3 — The translation path in one line: virtual address → **TLB** lookup (ASID + VPN together, so another process's entry can never produce a false hit) → on a hit the frame is available immediately, on a miss the **hardware walker** reads the PTE from SRAM in ≈100 ns.
  NOTE: A TLB miss is not an error — it costs one table walk. Detail and figure in [sheet L](sec_l) / Fig. 6.
- K.4 — Why a TLB is not optional: translation is itself a memory lookup, so without a cache of translations every access would cost two — read the PTE, then do the access it was for. The TLB settles the common case in a cycle and only a miss pays the walk. What the page size buys is its **reach**, the working set one TLB-full of entries covers: 32 entries × 2 KB = **64 KB**, against 8 KB had pages stayed at 256 B.
- K.5 — Only two situations are genuine failures: **P=0**, the page is not in physical memory — recoverable, the kernel allocates a frame, fills it, writes the PTE and retries (this is the *page fault*, deferred while v1 loads eagerly); and a **permission violation** — a write with W=0, a fetch with X=0, a user access with U=0 — which is a program error with no retry. Both ride the ABORTB pin.
- K.6 — Why FRAME is 16 bits against VPN's 13 — the two fields measure different things: **VPN measures how much memory one process sees** (set by the CPU), **FRAME measures how much memory the machine has** (set by the MMU design). There is no reason for them to match, and here they deliberately do not.
- K.7 — The virtual side is not a choice: the 65816 emits 24-bit addresses, so 24 − 11 of offset = 13 bits of VPN. The number falls out of a subtraction. The physical side *is* a choice: Helium assembles the physical address after translation and drives the memory bus itself, so only the FRAME width sets the ceiling. **The MMU breaks the 65816's architectural barrier at system level without breaking it at process level.**
- K.8 — The 3-bit margin is what makes the multitasking real: 128 MB ÷ 16 MB = up to **8 complete virtual address spaces resident simultaneously** in physical memory — exactly what preemptive multitasking with resident processes needs.

![Fig. 5 — Field-width asymmetry. The virtual split falls out of the CPU's 24 bits; the physical one is chosen by the MMU, and the offset crosses untranslated.](figures/fig-5-translation.svg)

## Summary of figures

| Parameter | Value |
|---|---|
| Page size | 2 KB |
| Virtual address | 24 bits — 16 MB per process |
| VPN | 13 bits — 8,192 pages |
| Offset | 11 bits |
| PTE | 32 bits |
| FRAME field | 16 bits |
| Physical address | 27 bits — 128 MB ceiling |
| Page table | 32 KB per process (8,192 × 4 B) |
| Physical pool fitted | 64 MB SDRAM = 32,768 frames |
| TLB entries | 32–64, in Helium's EBR |
| TLB reach | 64 KB with 32 entries · 128 KB with 64 |
| Page walk cost | ≈ 100 ns — 4 SRAM accesses at 8-bit width |
