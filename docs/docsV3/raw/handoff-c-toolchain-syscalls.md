# DANI-65816 — Session Handoff: C Toolchain, Syscall Contract & Driver Model

**Project:** DANI-65816 portable computer (W65C816S + 3× iCE40 + RP2040)
**Session scope:** Userland C programming path, OS API contract, driver architecture, build pipeline
**Language policy:** Conversations in Spanish, all documentation in English
**Baseline reminder:** The "one bank per process" model is **discarded**. Every process sees a full **16 MB virtual address space** (per-process paging, 2 KB pages, ABORT-driven fault handling).

---

## 1. Decisions made this session

### 1.1 C compiler: Calypsi C (no custom compiler needed)

- **Selected:** Calypsi C toolchain, WDC65816 target. C99, fully re-entrant code model, integer types up to 64-bit, IEEE-754 float/double, large code/data models with 24-bit pointers and JSL/RTL cross-bank calls. Actively maintained; used by the Foenix community (new-build 65816 machines comparable to ours).
- **Rejected / not viable:**
  - **cc65, vbcc** — 6502 only, no 65816 codegen.
  - **llvm-mos** — accepts `-mcpu=mosw65816` but native 16-bit 65816 codegen is immature; 65816 support was still in RFC/assembler-ergonomics stage. Re-evaluate later.
  - **WDC C** (official) — exists, dated and quirky. Fallback only.
- **Memory model choice:** *large code* (24-bit, JSL/RTL) + preferably *small data* pinned to one bank with fixed DBR, using far pointers only where needed. Rationale: 24-bit pointer dereference is expensive on the 65816 (long addressing or DBR reloads).

### 1.2 What we must build ourselves (the BSP)

The compiler is a solved problem; our work is the runtime around it:

1. **crt0 / startup** — set up stack, direct page, DBR; init `.data`/`.bss`; call `main()`; on return, call `exit()` (via COP).
2. **User linker script** — one canonical script for *all* user binaries (see 1.3).
3. **Kernel linker script** — separate; kernel links against its privileged address space, not the user 16 MB map.
4. **Syscall stubs** — the libc ↔ kernel boundary (see section 3).
5. **Calling-convention audit** — document exactly which registers and how much direct page Calypsi uses as pseudo-registers; the context switch must save/restore precisely that set (A, X, Y, DP, SP, DBR, PBR, P + Calypsi's DP pseudo-registers).

### 1.3 Linking consequence of full 16 MB virtual memory

Because every process sees an identical virtual layout, **all user binaries link at fixed virtual addresses. Zero relocation. No load-time fixups.**

Canonical user memory map (proposal, to be frozen):

| Region | Virtual location | Notes |
|---|---|---|
| Stack + Direct Page | Bank $00 (virtual) | 65816 hardware requirement; demand-backed |
| crt0 + `.text` | From $01:0000 | Large code model, JSL/RTL |
| `.data` | After `.text` | |
| `.bss` | After `.data` | Zero bytes on disk, sized in header |
| Heap | After `.bss`, grows up | `sbrk` just moves a pointer; frames arrive lazily via ABORT |

**Open caveat (flagged, not resolved):** physical bank $00 is pinned in SRAM. Each process's *virtual* bank $00 must either be remapped into that pinned region on context switch, or the active process's stack pages get temporarily pinned. Decide before implementing the context switch.

### 1.4 Kernel implementation language: C, not assembly

Kernel is written in C (Calypsi, kernel linker script). Assembly is reserved for:
- Interrupt / ABORT / COP vectors
- Context switch
- Startup code
- MMU / cache routines with critical timing

Estimated a few hundred lines of assembly total; scheduler, VFS, drivers, page manager are plain C.

---

## 2. Syscall contract (the stable ABI)

Dispatch mechanism: **COP instruction with an inline signature byte** (`COP #SYS_N`). Kernel COP handler reads the signature byte at stacked PC−2, indexes the dispatch table. Arguments are wherever Calypsi's calling convention put them — **the syscall ABI is literally "Calypsi calling convention + signature byte."**

### Dispatch table (v1 target)

**Processes**
| Call | Notes |
|---|---|
| `exec(path)` | Load binary from SD (see pipeline, §5) |
| `exit(code)` | |
| `wait(pid)` | |
| `getpid()` | |
| `yield()` | |
| `sleep(ms)` | Backed by the free-running timer in FPGA-A |
| `kill(pid, sig)` | Minimal signals: KILL, TERM, maybe one user-defined |

**Memory**
| Call | Notes |
|---|---|
| `sbrk(n)` | Moves heap end; frames faulted in via ABORT |
| `mmap(addr, n, prot)` / `munmap` | Simplified; prot maps to PTE R/W/X bits |
| `mshare(pid, addr)` | Optional; shared-memory IPC |

**Files & devices (unified)**
| Call | Notes |
|---|---|
| `open(path, flags)`, `close(fd)` | Path resolves to filesystem *or* device table |
| `read(fd, buf, n)`, `write(fd, buf, n)`, `seek(fd, off)` | |
| `stat`, `unlink`, `rename`, `mkdir`, `readdir` | |
| `ioctl(fd, op, arg)` | Escape valve for device-specific ops |

**Time & IPC**
| Call | Notes |
|---|---|
| `time()` | |
| `pipe()` | Little more than this in v1 |

### libc stubs

Each syscall is a 3-line assembly wrapper; ~20 stubs total connect the whole libc:

```asm
; write(fd, buf, n) — args already placed by Calypsi convention
_write:
    cop  #SYS_WRITE     ; signature byte = syscall number
    rtl                 ; kernel left return value in A
```

The C compiler emits an ordinary `jsl _write`; COP lives only inside the stub. Calypsi's low-level libc hooks (`open`/`read`/`write`/`sbrk`/…) are pointed at these stubs.

---

## 3. Driver model

**Decision: drivers are internal kernel components. They expose NO syscalls of their own.**

- User-visible surface is always the same contract — `open`/`read`/`write`/`ioctl` — via device paths: `/dev/tty` (UART), `/dev/kbd` (key matrix), `/dev/fb` (framebuffer), `/dev/sd`.
- Internally, every driver implements a fixed five-function kernel interface: `init`, `read`, `write`, `ioctl`, `irq_handler`.
- Kernel holds a device table indexed by device number; the minimal VFS only decides whether an `open` routes to the filesystem or the device table.
- **Rationale ("closed but swappable hardware"):** replacing hardware (ANX6345 → native eDP, matrix keyboard → USB HID) means rewriting one driver against the same internal interface. Syscall ABI and libc never change. Per-driver custom syscalls would break the ABI on every hardware change — explicitly rejected.

Special cases:
- **Framebuffer:** per-pixel `write()` would be unusable. Preferred: an `ioctl` that maps framebuffer pages (or a buffer FPGA-B scans out) directly into process virtual space, MMU as guardian.
- **Power/battery:** actual logic lives on the RP2040; kernel sees a plain I2C-translating driver. Same contract, no special case.

---

## 4. Build & execution pipeline (reference)

1. **Compile:** `hola.c` → assembly → relocatable object (`.text`, `.data`, `.bss`, unresolved symbols like `_write`).
2. **Link:** objects + libc + crt0 against the canonical user linker script → fixed virtual addresses, identical for every binary. No relocation records needed.
3. **Output format (custom, to be specified):** header = magic, entry point; per segment: virtual address, file size, memory size (`.bss` = 0 on disk), R/W/X permission bits matching the PTE.
4. **Load (`exec`):** kernel creates a fresh page table → reads header → copies segments from SD into SDRAM frames, maps with permissions → zeroes `.bss` → maps stack pages → places args where crt0 expects them.
5. **Run:** context switch — load process page table into MMU, restore initial registers, RTI into entry point in user mode. crt0 finishes init, calls `main()`; on return crt0 issues `exit()`.

**Deferred optimization (design already supports it):** lazy loading. Mark pages "present-on-SD" in the PTE and let ABORT fault them in on first access — the same path as swap. **v1 loads eagerly.**

---

## 5. Open items / next actions

1. **Freeze the canonical user virtual memory map** (region addresses, stack size, heap start).
2. **Resolve virtual-bank-$00 vs pinned-SRAM interaction** (remap on context switch vs temporary pinning) — blocks context-switch implementation.
3. **Audit Calypsi's calling convention & DP pseudo-register usage** → derive the exact context-switch save set and finalize syscall argument passing.
4. **Specify the binary format header** (field layout, magic value, versioning).
5. **Decide shared read-only text pages** — is libc `.text` duplicated per process or mapped shared read-only across page tables? PTE R/X bits already support sharing; pure memory win, but touches loader and page-refcounting.
6. **Assign syscall signature-byte numbering** (`SYS_*` constants) and write the ~20 libc stubs.
7. Carried over from earlier sessions: ER-TFT101-1 datasheet blocker (KiCad schematic), PTE permission-bit final layout, page-table location in kernel space.
