# Handoff — Virtual Memory / Paging Design (DANI-65816)

**Session topic:** Introducing full paged virtual memory into the management core (MMU).
**Language note:** Project conversations are in Spanish; all documentation (including this handoff) is in English.

---

## 1. Decision made this session

**Commit to full paging.** Each process sees the entire 24-bit address space (`00.0000`–`FF.FFFF`, 16 MB) as its own private, flat virtual space, regardless of where its data physically lives. This replaces the earlier lightweight "one bank per process" relocation model (DBR/PBR).

**Page size chosen: 2 KB** (4 KB left open as an alternative — see §8).

---

## 2. Motivation

The stated trigger was relocatable code, but relocation was already achievable cheaply with bank-granularity DBR/PBR. Full paging is justified by what it adds *on top* of relocation:

- Processes **larger than a single 64 KB bank** — a process can use its full 16 MB virtual space while physically living wherever frames are free.
- **True location independence** — every process sees identical virtual addresses, not just bank-relocated ones.
- **Demand paging and swap to SD**, and future copy-on-write, become natural extensions of the page-fault path.
- Per-process **isolation** with fine-grained R/W/X permissions.

---

## 3. Core architectural principle established

**The page and the cache line are two independent units. Do not conflate them.**

- **Cache line = 256 B.** Governed by the PSRAM `tCEM` constraint (~8 µs max CS-low → burst size ceiling). This is the unit moved between PSRAM and SRAM. Unchanged from prior design.
- **Page = 2 KB.** The unit of *translation* and *protection*. It is what the page table maps and what carries permission bits. It never participates in a PSRAM burst, so `tCEM` does **not** constrain it.

One 2 KB page = 8 cache lines of 256 B. These are two separate knobs.

A second foundational point (carried from the prior turn): the management core currently indexes the SRAM cache by **physical** address. Virtual memory inserts a **translation stage in front of** the cache:

```
CPU emits VIRTUAL address (bank:page:offset)
  → translate via page table  → PHYSICAL address
  → cache lookup (existing FSM)
      hit  → serve
      miss → load 256 B line from PSRAM
```

Because translation happens on **every** access, and the page table itself lives in PSRAM, a **TLB is mandatory** — you cannot read PSRAM to translate an access that is itself trying to read PSRAM. The TLB (a small BRAM-backed cache of translations) resolves the common case in one cycle; only a TLB miss triggers a page-table walk.

---

## 4. Numbers (2 KB pages)

| Quantity | Value |
|---|---|
| Virtual address space | 24 bits = 16 MB |
| Page size | 2 KB → 11-bit offset |
| Page number | 13 bits → **8,192 pages / process** |
| Flat page table size | ~16 KB / process (assuming ~2 B/PTE — PTE width still open, see §8) |
| Physical PSRAM | 16 MB / 2 KB = **8,192 frames** (13-bit frame number) |
| TLB reach (32 entries) | 64 KB working set (vs 8 KB with 256 B pages — **×8 improvement**) |

The ×8 gains vs the old 256 B page size — smaller table (16 KB vs ~128 KB/process, flat table stays viable, no multilevel needed) and 8× larger TLB reach — are the concrete payoff of moving to 2 KB.

---

## 5. Three-tier memory hierarchy

```
SD (swap)          ← unit: PAGE (2 KB). Page fault → trap to OS (ms latency)
   ↓
PSRAM (main)       ← paged into 2 KB frames; page table + TLB translate here
   ↓
SRAM (cache)       ← unit: LINE (256 B). Cache miss → hardware FSM (µs latency)
```

Two distinct fault types with different granularity, latency, and handler:

- **Cache miss** (line not in SRAM): resolved entirely by the management core's state machine. Fast (µs).
- **Page fault** (page absent from PSRAM — only on SD, or unmapped): raised as an exception to the OS. Slow (ms). This is the hook for demand paging, swap, COW.

---

## 6. Critical gotcha — dirty-line flush before page eviction

The cache sits **in front of** PSRAM. Therefore, to swap a 2 KB page out to SD, the OS must **first flush any dirty cache lines belonging to that page's frame** back to PSRAM, then write the page to SD. Skipping this writes a stale version of the page.

**Rule:** page evict ⇒ flush the (up to 8) dirty 256 B lines of that frame ⇒ then write to SD.

**Action item:** the management core needs a "flush all cache lines belonging to frame N" mechanism (a bounded range flush).

---

## 7. Constraints carried in from prior work (context)

- Physical backing store: **16 MB QSPI PSRAM** + **512 KB SRAM** as a direct-mapped, write-back cache (dirty bits), 256 B lines.
- Management core is a **pure state machine** (cache + QSPI controller), no soft CPU in the critical path.
- **Bank `$FF` is I/O, privileged only** — not general RAM. The "full 16 MB" a process sees excludes `$FF` as usable memory; translation treats it specially or leaves it unmapped for user processes.
- **Bank 0 is special** — 65816 stack and Direct Page always land there. Its frames must stay **pinned/resident** to avoid a page fault mid-stack-push.
- Physical reality: 16 MB PSRAM = exactly **one** full virtual space. Multiple processes coexist only because their tables are sparse (few real pages mapped) and/or via swap to SD.
- Protection enforcement remains via the permission table in `$FF` and the **ABORT** pin on violations.

---

## 8. Open decisions / next steps

1. **PTE format** — exact bit layout: frame-number bits + permission bits (R / W / X), plus Present, Dirty, Accessed. This sets the final PTE width and therefore the exact page-table size.
2. **TLB miss handling** — hardware page-table walk (management core walks the table itself) vs software trap (fault to OS, which fills the TLB). Trade-off: hardware = fast, more logic; software = simpler HW, slower misses, more flexible policy.
3. **Page size confirmation** — 2 KB is the current pick. 4 KB remains defensible (smaller table ~8 KB/process, even larger TLB reach) at the cost of more internal fragmentation. Decision hinges on how small typical processes are.
4. **Frame-range flush mechanism** — design the management-core operation from §6.

**Immediate next step:** lock the PTE format and the TLB-miss policy, since together they define both the data structure and the hardware/software split of the walk.
