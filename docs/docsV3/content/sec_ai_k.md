# Virtual memory — concepts and glossary
> first principles · why the field widths · reference figures

Explanatory groundwork, not normative: it explains paging from first principles and gives the reasoning behind the field widths, so that the machinery in [sheet L](sec_l) can be read without stopping to decode acronyms. Term-by-term definitions live in the [glossary, sheet Z1](sec_z1). It introduces no design decision — where it disagrees with an architecture sheet, the architecture sheet wins.

- K.1 — Why it exists — three problems it solves at once: **relocation** (the program no longer needs to know what address it will be loaded at), **protection** (no process can touch another's memory or the kernel's) and **fragmentation** (free memory no longer breaks into holes nothing fits into). The process lives in a fiction: it believes it owns the full 16 MB, starting at zero.
- K.2 — The core idea: the address space is cut into fixed-size chunks — **pages** on the virtual side, **frames** on the physical side, both 2 KB. Translation answers a single question: which physical frame holds virtual page N? The offset within the page is never translated; the low bits pass straight through.
  NOTE: Fixed chunks trade one waste for another: external fragmentation goes, internal arrives — 1 KB wasted per allocation on average, and no compaction is needed.
- K.3 — The translation path in one line: virtual address → **TLB** lookup (ASID + VPN together, so another process's entry can never produce a false hit) → on a hit the frame is available immediately, on a miss the **hardware walker** reads the PTE from SRAM in ≈100 ns.
  NOTE: A TLB miss is not an error — it costs one table walk. Detail and figure in [sheet L](sec_l) / Fig. 6.
- K.4 — Why a TLB is not optional: translation is itself a memory lookup, so without a cache of translations every access would cost two — read the PTE, then do the access it was for. The TLB settles the common case in a cycle and only a miss pays the walk. What the page size buys is its **reach**, the working set one TLB-full of entries covers: 32 entries × 2 KB = **64 KB**, against 8 KB had pages stayed at 256 B.
  NOTE: Reach is entries × page size: 64 × 2 KB = 128 KB of working set before entries evict each other. Striding through more costs a ~100 ns walk per access.
- K.5 — Only two situations are genuine failures: **P=0**, the page is not in physical memory — recoverable, the kernel allocates a frame, fills it, writes the PTE and retries (this is the *page fault*, deferred while v1 loads eagerly); and a **permission violation** — a write with W=0, a fetch with X=0, a user access with U=0 — which is a program error with no retry. Both ride the ABORTB pin.
  NOTE: Everything else that looks like a failure is routine: a TLB miss costs a walk, and a write to a copy-on-write page is a violation the kernel *resolves* rather than reports.
- K.6 — Why FRAME is 16 bits against VPN's 13 — the two fields measure different things: **VPN measures how much memory one process sees** (set by the CPU), **FRAME measures how much memory the machine has** (set by the MMU design). There is no reason for them to match, and here they deliberately do not.
- K.7 — The virtual side is not a choice: the 65816 emits 24-bit addresses, so 24 − 11 of offset = 13 bits of VPN. The number falls out of a subtraction. The physical side *is* a choice: Helium assembles the physical address after translation and drives the memory bus itself, so only the FRAME width sets the ceiling. **The MMU breaks the 65816's architectural barrier at system level without breaking it at process level.**
- K.8 — The 3-bit margin is what makes the multitasking real: 128 MB ÷ 16 MB = up to **8 complete virtual address spaces resident simultaneously** in physical memory — exactly what preemptive multitasking with resident processes needs.
  NOTE: Eight is a limit on how many address spaces are **resident**, not on how many processes exist. More can exist and be scheduled; they cannot all keep their pages in physical memory at once, which is what swap is for.
- K.9 — **Why 16 and not 15 — where the width stops being arithmetic and becomes a choice.** The PTE splits into exact half-words and the upper one was already full: the flags are eight (`P W X U D A NC PIN`) plus eight (`SW[7:0]`), sixteen bits precisely, so the lower half-word was going to be underused whatever FRAME did. Rounding it up to 16 costs nothing and buys the 128 MB ceiling. **Fifteen would not have been the cheaper answer but the tighter one**: 64 MB of SDRAM is 32,768 frames, exactly 2¹⁵ — the memory actually fitted lands precisely on the fifteen-bit ceiling, with nothing left over.
  NOTE: The same split earns its keep a second time in the walker. With FRAME in the low half-word and the flags in the high one, Helium reads 32 bits from SRAM and sends the lower sixteen to the address bus while the upper sixteen go to the permission logic — in parallel, with no shifting and no masking cycle. [L.5](sec_l#l5) fixes the format; this is why the cut falls where it does (→ [L.8](sec_l#l8)).

![Fig. 5 — Field-width asymmetry. The virtual split falls out of the CPU's 24 bits; the physical one is chosen by the MMU, and the offset crosses untranslated.](figures/fig-5-translation.svg)

## Summary of numbers — every value the two virtual-memory sheets settle on, in one place.

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
