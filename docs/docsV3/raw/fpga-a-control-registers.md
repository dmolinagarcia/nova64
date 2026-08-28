# DANI-65816 — FPGA-A Control Register Map

**Status:** Draft for review — supersedes the earlier WDM-based proposal
**Scope:** Memory-mapped command interface between the kernel and FPGA-A (MMU, cache controller, bus arbiter)
**Audience:** Gateware implementer (FPGA-A), kernel MMU layer author, RP2040 bring-up firmware author

---

## 1. Purpose

FPGA-A owns three pieces of state the CPU cannot manipulate through ordinary memory
accesses:

- the **TLB** (Translation Lookaside Buffer — the on-FPGA cache of recently used
  virtual-to-physical page translations, held in iCE40 EBR),
- the **active context**, i.e. which process's page table the hardware walker should
  use and which **ASID** (Address Space Identifier — the tag that lets TLB entries from
  different processes coexist without a full flush on context switch) to attach to new
  translations,
- the **write-back cache** state in SRAM (dirty-line tracking, and the ability to force
  dirty lines out to SDRAM).

This document defines the register interface through which the kernel drives that state.

### 1.1 What this interface is *not*

This is an **internal kernel-to-hardware interface**, not a user-facing ABI. It sits
strictly below the syscall layer. Three layers, kept deliberately distinct:

| Layer | Mechanism | Visible to |
|---|---|---|
| System call ABI | `COP #SYS_N` | User processes |
| Kernel MMU/cache logic | C and assembly in bank `$01` | Kernel only |
| Hardware control | Stores to bank `$FF` (this document) | Kernel only |

Operations such as *flush the TLB* or *range-flush a frame* must **never** be exposed as
system calls. They are steps inside `munmap`, inside the context switch, and inside the
page evictor. Exposing them would let any process flush the TLB in a loop and degrade
system-wide performance — a trivial denial of service.

Consequently, **command encodings in this document occupy a numbering space entirely
independent of the syscall numbering space.** The two must not be aligned or cross-referenced.

---

## 2. Design rationale

An earlier proposal captured the `WDM` instruction (opcode `$42`, formally reserved by
WDC for future expansion, and executed by the W65C816S as a two-byte, two-cycle no-op)
and had FPGA-A decode its signature byte as a hardware command. That approach was rejected
in favour of memory-mapped registers for four reasons:

1. **Reuses existing gateware.** FPGA-A already decodes bank `$FF` addresses to expose
   `FAULT_ADDR`, `FAULT_CAUSE`, `CONTEXT` and `MODE`. Adding command registers widens an
   existing comparator. A `WDM` snooper would require a new state machine tracking VPA/VDA
   (the 65816's Valid Program Address / Valid Data Address bus-cycle qualifiers) to
   distinguish an opcode fetch from an operand fetch, then latch the following byte.

2. **Reuses the existing protection keystone.** Bank `$FF` is privileged and never mappable
   into a user page table. A user process attempting to write a command register takes an
   `ABORTB` through the ordinary permission-check path. No new privilege-gating logic is
   needed inside FPGA-A. A `WDM` mechanism would have required its own user/supervisor gate.

3. **Testable before a kernel exists.** During bring-up stages E1–E3 the RP2040 can drive
   the bus and exercise every command directly. A `WDM` snooper can only be tested with the
   65816 fetching real instructions.

4. **No softcore divergence.** The FPGA-C softcore 65816 (deferred, unpopulated initially)
   need only implement the documented instruction set. If commands were carried in `WDM`
   signature bytes, the softcore would have to replicate the exact same decode table or the
   kernel would break on CPU swap.

`WDM` remains **reserved and unimplemented**: a two-cycle no-op, as on the hardware CPU.
The one use case it seemed to serve uniquely — a breakpoint insertable into user code,
which cannot touch bank `$FF` — is already covered by `BRK`, which has its own vector on
the 65816.

---

## 3. Address-as-opcode convention

Each command has its **own address**. The value written is the command's **argument**,
not a command code.

```asm
    lda ASID
    sta f:$FF0010          ; writing here = flush TLB entries for the ASID in A
```

rather than the alternative of a single command register plus a separate argument register.
This choice:

- removes the value decoder inside FPGA-A, leaving only the address comparator that
  already exists;
- removes the argument register entirely;
- reduces every command to a single bus cycle;
- eliminates a whole bug class — there is no intermediate "argument written but command
  not yet triggered" state that an interrupt can split.

---

## 4. Register map

Base block: `$FF:0000`–`$FF:00FF`, reserved for FPGA-A. Addresses not listed are reserved
and must decode as no-ops on write and `$00` on read.

All registers are 16 bits wide and must be accessed with the accumulator in 16-bit mode
(`M=0`) unless noted. Byte-granular access to a 16-bit register is undefined behaviour in
this revision.

### 4.1 Fault reporting (read-only, existing)

| Address | Name | Width | Notes |
|---|---|---|---|
| `$FF:0000` | `FAULT_ADDR_L` | 16 | Faulting virtual address, bits [15:0] |
| `$FF:0002` | `FAULT_ADDR_H` | 16 | Bits [23:16] in low byte; upper byte reserved |
| `$FF:0004` | `FAULT_CAUSE` | 16 | See §4.5 |
| `$FF:0006` | `CONTEXT` | 16 | ASID active at time of fault |
| `$FF:0008` | `MODE` | 16 | Bit 0 = 1 if fault occurred in user mode |

### 4.2 Command registers (write-only)

| Address | Name | Argument written | Completion |
|---|---|---|---|
| `$FF:0010` | `TLB_FLUSH_ASID` | ASID to invalidate | Synchronous |
| `$FF:0012` | `TLB_FLUSH_ALL` | ignored (write `$0000`) | Synchronous |
| `$FF:0014` | `CTX_SET_ASID` | new active ASID | Synchronous |
| `$FF:0016` | `CTX_SET_PTBASE` | SDRAM frame number of page-table base | Synchronous |
| `$FF:0018` | `CACHE_FLUSH_FRAME` | SDRAM frame number to range-flush | **Asynchronous** |
| `$FF:001A` | `TLB_INVAL_PAGE` | virtual page number (VA[23:11]) | Synchronous |
| `$FF:001C` | `CACHE_FLUSH_ALL` | ignored (write `$0000`) | **Asynchronous** |

### 4.3 Status (read-only)

| Address | Name | Bit | Meaning |
|---|---|---|---|
| `$FF:001E` | `MMU_STATUS` | 0 | `BUSY` — an asynchronous command is in progress |
| | | 1 | `WALK_ERR` — page-table walker encountered a malformed PTE |
| | | 15:2 | Reserved, read as 0 |

### 4.4 Command semantics

**`TLB_FLUSH_ASID`** — Invalidates every TLB entry tagged with the given ASID. Used when
tearing down a process, and when recycling an ASID for a different process (see §7).
Entries belonging to other ASIDs are untouched.

**`TLB_FLUSH_ALL`** — Invalidates all entries regardless of tag. Reserved for the global
kernel-mapping change case and for bring-up. Should be rare in normal operation; if it
appears on a hot path, that is a design smell.

**`CTX_SET_ASID`** — Sets the tag FPGA-A attaches to newly filled TLB entries and matches
against on lookup. Does **not** flush.

**`CTX_SET_PTBASE`** — Sets the SDRAM frame number from which the hardware page-table
walker reads PTEs. Note the invariant already settled for this machine: **PTEs always
reference SDRAM frame numbers**, never SRAM locations; SRAM is a transparent cache and never
the source of truth.

`CTX_SET_ASID` and `CTX_SET_PTBASE` must both be written on every context switch, and the
switch is only coherent once both have landed. Write `CTX_SET_PTBASE` **first**, then
`CTX_SET_ASID`: an interrupt taken between the two then sees the old ASID still selected,
whose TLB entries remain valid and consistent with the old page table. The reverse order
creates a window where the new ASID is active but the walker still points at the old table,
which would fill the TLB with entries that are wrong and tagged as correct — silent
corruption. This ordering constraint is not optional.

**`TLB_INVAL_PAGE`** — Invalidates a single translation for the currently active ASID.
The cheap path for `munmap` of one page, and for permission downgrades such as marking a
page read-only for copy-on-write.

**`CACHE_FLUSH_FRAME`** — Writes back every dirty 256-byte cache line belonging to the
given 2 KB SDRAM frame (up to 8 lines), then marks them clean. **Required before evicting
a page to SD**, since the SD path reads from SDRAM and would otherwise see stale data.
Asynchronous; see §5.

**`CACHE_FLUSH_ALL`** — Writes back all dirty lines. For shutdown, suspend, and bring-up.
Asynchronous.

### 4.5 `FAULT_CAUSE` encoding

| Bit | Meaning |
|---|---|
| 0 | Page not present (`P=0`) |
| 1 | Write to read-only page |
| 2 | Execute from non-executable page |
| 3 | User-mode access to supervisor page |
| 4 | Access to bank `$FF` from user mode |
| 5 | Malformed PTE / walker error |
| 15:6 | Reserved |

---

## 5. Completion model

Two classes of command, distinguished by whether they touch SDRAM.

### 5.1 Synchronous commands

Every TLB and context operation completes **within the bus cycle of the write**. A TLB
flush is a clear of validity bits in EBR; a context update is a register load. There is
nothing to wait for and no status polling is required. The effect is visible to the very
next bus cycle.

### 5.2 Asynchronous commands and the `BUSY` protocol

`CACHE_FLUSH_FRAME` and `CACHE_FLUSH_ALL` may take microseconds — a fully dirty 2 KB frame
is eight 256-byte line write-backs to SDRAM, interleaved with SDRAM auto-refresh.

These commands **must not** be implemented by stalling PHI2. Although a bounded hardware
operation nominally satisfies the project's PHI2-stall criterion, holding PHI2 low prevents
interrupts from being serviced, which would inject microseconds of jitter into the scheduler
on every page eviction. The page evictor is already slow kernel code; polling costs nothing
there.

Protocol:

```asm
    lda frame_number
    sta f:$FF0018          ; CACHE_FLUSH_FRAME
wait:
    lda f:$FF001E          ; MMU_STATUS
    and #$0001
    bne wait               ; spin until BUSY clears
```

Rules for the gateware:

- `BUSY` must read as 1 on the **first** read following the triggering write. It may not
  lag; a kernel that samples a stale 0 would proceed against a half-flushed frame.
- A write to an asynchronous command register while `BUSY` is set is **ignored**. It does
  not queue and does not abort the running operation. The kernel is responsible for not
  issuing one.
- Ordinary CPU memory traffic continues to be served while `BUSY` is set. Flush write-backs
  contend for the shared SRAM/SDRAM bus and will slow the CPU, but must not stall it.
- The free-running timer in FPGA-A continues to count, and interrupts continue to be
  delivered, throughout.

A hardware watchdog converting a hung flush into an NMI is desirable but **not yet
specified** — see §9.

---

## 6. Ordering guarantees

The W65C816S has **no prefetch queue and no store buffer**. Every bus cycle is architectural
and appears in program order. Therefore:

- A store to a command register takes effect strictly after all preceding CPU memory accesses
  and strictly before all following ones. **No explicit memory barrier instruction is
  required**, and none is provided.
- A PTE update written to SRAM before a `TLB_INVAL_PAGE` store is guaranteed visible to the
  hardware walker at the time of the invalidation.

One caveat requiring gateware attention: PTEs live in the pinned SRAM region, and the
hardware walker reads SRAM directly. If CPU writes to page-table memory pass through the
write-back cache rather than reaching the pinned region directly, the walker could read a
stale PTE. **The pinned region must be configured as non-cacheable, or write-through,** so
that walker reads and CPU writes see the same data. The `NC` flag already exists in the PTE
format for exactly this purpose. This is an implementation requirement, not an optional
optimisation.

---

## 7. Protection model

No new privilege logic is introduced. Bank `$FF` is I/O-mapped and never present in a user
process's page table; a user-mode access faults through the existing permission check and
raises `ABORTB` with `FAULT_CAUSE` bit 4 set. This is the same self-protection property that
guards the permission tables themselves.

Two invariants the kernel must uphold:

- **ASID recycling.** ASIDs are a finite hardware resource (width TBD, see §9). Before
  assigning a previously used ASID to a new process, the kernel must issue
  `TLB_FLUSH_ASID` for it. Failing to do so leaks the old process's translations into the
  new address space — a complete protection failure with no visible symptom until it
  corrupts something.
- **Bank `$00` and the kernel mapping are never invalidated in a way that unmaps them.**
  Stack, direct page and the interrupt trampolines must remain resident and translatable at
  all times; a fault there is unrecoverable given the 65816's unconditional use of bank `$00`
  on interrupt entry.

---

## 8. Kernel coding conventions

Absolute long addressing (`sta f:$FF0010`) is 4 bytes and 5 cycles. Note the `f:` prefix is
required in ca65 to prevent the assembler from narrowing the access to 16-bit absolute.

For any routine issuing two or more commands, set `DBR` (Data Bank Register) to `$FF` on
entry and use 16-bit absolute addressing instead — 3 bytes, 4 cycles:

```asm
mmu_switch_context:
    phb
    lda #$FF
    pha
    plb                    ; DBR = $FF
    ; ---- 16-bit absolute from here ----
    lda pt_frame
    sta $0016              ; CTX_SET_PTBASE  (first — see §4.4)
    lda new_asid
    sta $0014              ; CTX_SET_ASID
    ; -----------------------------------
    plb
    rtl
```

This pairs cleanly with the project's small-data-model / fixed-DBR compilation strategy: the
MMU layer is one of the few places where `DBR` is deliberately repointed, and it is confined
to leaf assembly routines that restore it before returning.

---

## 9. Open items

- **ASID width.** Not yet fixed. Drives both EBR consumption per TLB entry and how often
  ASID recycling (and its mandatory flush) occurs. 6 bits / 64 contexts is a reasonable
  starting point but should be validated against the EBR budget alongside the 32–64 TLB
  entries already planned.
- **Watchdog.** A counter in FPGA-A converting a flush that fails to complete within a
  threshold into NMI or `ABORTB`, with a distinct `FAULT_CAUSE` code. Currently a hung SDRAM
  controller hangs the kernel in the `BUSY` spin loop with no recovery.
- **SDRAM auto-refresh interleaving** during multi-line flush bursts — shares the arbitration
  question already open for cache fills.
- **`CACHE_FLUSH_FRAME` granularity.** Frame-level flush is correct but coarse. If profiling
  shows eviction dominating, a line-level variant taking a physical address may be worth
  adding at `$FF:0020`.
- **RP2040 access path.** The bring-up firmware must be able to drive these registers with
  the 65816 held in reset. Whether that reuses the config SPI path or requires bus mastering
  by the RP2040 is unresolved and affects the E2 stage test plan.

---

## 10. Changes to prior decisions

- The `COP` handler retrieves the syscall number from the **accumulator**, loaded by the
  libc stub immediately before the `COP`. The signature byte is retained purely for
  disassembly, tracing and static binary verification — the handler does not read it. This
  removes ~20–30 cycles of stack-walking and 24-bit pointer construction from every syscall.
- **Verify against the WDC manual:** the previously recorded assumption that the signature
  byte sits at stacked `PC−2` appears to be off by one. `COP` increments PC past the
  signature byte before pushing, placing the signature at stacked `PC−1`. This is only
  relevant to a tracer or verifier now that the handler no longer reads it, but an incorrect
  constant would produce a silent misdispatch rather than an obvious failure.
- `WDM` (`$42`) is documented as a **reserved no-op**. FPGA-C's softcore must implement it
  as a two-byte, two-cycle no-op and decode nothing.
