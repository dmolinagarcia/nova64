# DANI-65816 — Handoff: BIOS / OS / User Process Layering

**Scope of this session:** clarifying the three software layers of the machine — what each
must provide, where each lives in cold storage, and where each lives at runtime. Ends with a
settled virtual-memory layout for banks `$00` and `$01`.

---

## 1. The three layers

### 1.1 BIOS / firmware

**Provides:** hardware bring-up (leave the machine in a known state) and a mechanism to load
the next stage from storage. Optionally, primitive I/O routines usable during early boot.

**Distributed role in this design.** Most of the classic BIOS job belongs to the RP2040:
power sequencing, FPGA configuration over the config SPI, and loading the initial binary
image from microSD into SRAM before releasing CPU reset. What the W65C816S sees as "BIOS"
is only the code the RP2040 deposits in memory beforehand.

**Cold storage:** RP2040 firmware in its W25Q16 flash; the 65816-side boot image on the
microSD card.

**Runtime:** the 65816 reset vector is fetched from bank `$00`, so the 65816-side boot code
must be resident in SRAM (bank `$00`, already pinned) at reset time. Once the kernel takes
control, this code can be discarded — the kernel talks to hardware directly through bank
`$FF` and does not call back into BIOS routines.

**Open design point:** whether the RP2040 remains an active management engine at runtime
(battery/power reporting, HID hub) or goes passive after boot. If active, a runtime channel
between kernel and RP2040 must be defined.

---

### 1.2 Operating system (kernel)

**Provides:**

- Process management: scheduler, context switch.
- Memory management: per-process page tables, swap to SD, fault handling via `ABORTB`.
- Drivers: the only code permitted to touch bank `$FF`.
- The syscall ABI via `COP` — the sole contract exposed to user processes.

**Cold storage:** a binary on the microSD filesystem.

**Runtime:** always resident. Kernel code, active page table, kernel stack and interrupt
vectors live in physical frames pinned in SRAM and are never paged. This is a hard
requirement, not an optimization: if the page-fault handler could itself fault, the
recursion is unrecoverable.

---

### 1.3 User processes

**Provide:** everything the user cares about. They never touch hardware; all access is via
syscalls.

**Cold storage:** executables on the SD filesystem.

**Runtime:** in virtual memory. Pages live physically in SDRAM (or in SRAM when hot in
cache) and may be swapped to SD. The process sees a clean address space; FPGA-A translates
and asserts `ABORTB` on any attempt to reach bank `$FF` or another process's pages.

---

## 2. Correction carried into this session

The earlier "one bank per process" model is **discarded**. Each process sees a full 16 MB
virtual address space, with page-level assignment managed by the kernel.

**Consequence — relocation disappears as a problem.** All binaries link against identical
virtual addresses (e.g. code starting at `$01:0000`). No DBR/PBR games are needed to
relocate; the MMU translates, and two processes can occupy the same virtual address without
conflict. DBR and PBR revert to being ordinary ISA registers, not an isolation mechanism.

**Consequence — per-process page table.** ~16 KB per process (2 KB pages → 8,192 entries).
Context switch = save CPU state, switch the active table pointer in the MMU (notify FPGA-A),
invalidate or re-tag the TLB. ASID-tagged TLB entries avoid a full flush on every switch.

---

## 3. Settled virtual memory layout

### 3.1 Bank `$00` — process stack/DP plus a minimal kernel presence

Bank `$00` cannot be left entirely to the process. Two architectural facts force a kernel
footprint there:

**Vectors and interrupt entry are unconditionally in bank `$00`.** The CPU fetches native-mode
vectors from `$00:FFE4`–`$00:FFEE` and forces `PBR = 0` on entry, so every handler *begins*
executing in bank `$00`. Solution: a trampoline — one or two privileged 2 KB pages in bank
`$00` holding the vectors plus short stubs that `JSL` into the real kernel in bank `$01`.

**The interrupt frame is pushed onto the current stack.** Hardware pushes PBR, PC and P to
the stack pointed to by S (always bank `$00`) *before* the kernel can intervene. Two
consequences:

1. The page S points into must be present unconditionally. An `ABORT` during interrupt entry
   would be a fault inside the fault mechanism. **Decision: each process's stack page(s) are
   pinned for the lifetime of the process and never swapped.**
2. The stub must switch S to a kernel stack (also in bank `$00`, inside the privileged pages)
   before doing real work — so the kernel neither depends on the user stack nor leaks kernel
   data back to the process on return.

**Resulting bank `$00` map:**

| Region | Owner | Notes |
|---|---|---|
| Direct page | Process | User-accessible |
| Stack | Process | User-accessible, **pinned** |
| 1–2 × 2 KB pages | Kernel | Privileged: vectors, trampolines, kernel stack. Identical mapping in every process's page table |

### 3.2 Bank `$01` — the fixed kernel

64 KB for the resident kernel: syscall dispatcher, scheduler, fault handler, drivers.
Mapped identically and marked privileged in every process page table.

64 KB is generous for 65xx code — complete kernels of the era (ProDOS, GEOS) fit in
considerably less.

### 3.3 Page tables of inactive processes

At ~16 KB each, the page tables of non-running processes will not all fit in bank `$01`
alongside the code. They can live in pageable kernel memory in other virtual banks of the
kernel's space; only the active table must be reachable by FPGA-A's walker. Tables of
long-dormant processes may themselves be swapped out.

---

## 4. Boot flow (end to end)

1. RP2040: power sequencing → FPGA configuration → load bank `$00` boot image from SD into SRAM.
2. Release reset; the 65816 executes the minimal boot code in bank `$00`.
3. Boot code loads the kernel from SD and jumps to it. *(Alternative: the RP2040 loads the
   kernel directly, collapsing this stage. Open decision.)*
4. Kernel initializes MMU, page tables, scheduler.
5. Kernel loads the first user process and enters it unprivileged.

---

## 5. Open items leaving this session

- Whether the RP2040 remains an active management engine at runtime, and if so, the
  kernel↔RP2040 channel.
- Whether the 65816-side boot stage is kept as a distinct stage or collapsed into the RP2040 load.
- Kernel stack sizing, and whether one kernel stack is shared or one exists per process.
- Exact split of the 1–2 privileged bank `$00` pages between vectors, trampolines and kernel stack.
