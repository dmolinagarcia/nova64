# DN-SW-LIBC-001 — The C Library: libnova, the Syscall Layer and the SDK

**Revision A** — draft for review, not frozen
**Project:** noVa64
**Date:** 2026-09-25
**Scope:** The C runtime every native noVa64 program links against: which parts
come with the compiler and which are ours, the system-call stubs and their ABI,
the POSIX surface, program startup and memory layout, and how a program is
built, packaged and put on the card.

**Depends on:**

- Sheet O (O.2–O.7) — Calypsi, the memory model, the BSP, the syscall stubs
- Sheet J (J.1, J.3, the v1 dispatch table) and D90 — `COP` and the numbered
  service table
- Sheet N (N.4–N.6) — the canonical user layout, the binary format, the loader
- Sheet L (L.10, L.11) — the virtual bank map and bank `$00`
- Sheet Y1 (Y1.12, Y1.15, Y1.20) and DN-FS-VFS-001 §5, §13 — the error space,
  `vattr_t`, `dirent_out_t`, the syscall mapping
- Sheet CN1 and D103 — the two console builds and the "libc surface"

**Related:** sheet VM4 (the NVM32 toolchain; its C library is PDCLib over vbcc,
VM4.22, and is out of scope here, but it shares the ABI headers of §6.1), sheet
V7 (the development environment), sheet P3 item E0.8 (the toolchain audit).

**Blocks:** CON-04 (the shell as a user process), P5.k, and every native user
program.

---

## Revision history

| Rev | Date | Change |
|---|---|---|
| A | 2026-09-25 | Initial draft. All decisions are proposals for review. |

---

## 1. Purpose and scope

The document says "libc" in several places — O.7, J.3, CN1.1, CN1.18 — and
means a slightly different thing in each. O.7 describes it as twenty syscall
stubs wired into Calypsi's hooks. CN1 describes it as the "libc surface" the
shell calls, which the monolithic build links to kernel functions. Neither says
where `printf` comes from, how a program starts, where its data lives, or how a
programmer builds one. This note answers those questions and fixes the pieces
the kernel and the loader must agree on.

The short answer is that **the C library is three layers, and noVa64 writes
only the two thin ones.** The ISO C library — `printf`, `strlen`, `malloc`,
`fopen` — comes with the compiler. noVa64 writes the POSIX layer above the
kernel, the glue Calypsi's library needs, the startup code, and the stubs that
cross into the kernel.

### In scope

- The three layers and who owns each (§4).
- What Calypsi's C library provides and how it is attached (§5).
- The raw syscall layer: the service table and its v1 numbering, the stub, the
  dispatcher contract, pointers, results and errors (§6).
- The POSIX layer, libnova: headers, functions and translation (§7).
- A user program's memory layout and data model (§8).
- Program startup and exit (§9).
- The two console builds, the source tree, the executable and the SDK (§10).
- Running Calypsi on the ARM64 development host (§11).
- The licence of the runtime (§12).
- Gates LIB-00 to LIB-05 (§13).

### Out of scope

- The kernel side of a syscall beyond the dispatcher contract of §6.4.
- The NVM32 C library (VM4.22). It shares the ABI headers and nothing else.
- The executable header's field layout (Q20). §10.3 states only what this note
  needs from it.
- Shared libraries and shared text (Q21). v1 links statically (D-12).
- The windowing toolkit (sheet V), which is a library above this one.

---

## 2. Background — how an existing system organises it

Measured on a Linux x86-64 system with glibc, a ten-line program that calls
`malloc`, `printf` and a failing `open` shows the whole structure:

1. **Headers only declare.** `stdio.h` contains the prototype of `printf` and no
   code. It lets the compiler check the call.
2. **The library is an archive of small objects.** `libc.a` holds 2,211 object
   files, roughly one per function. The linker takes only the objects the
   program needs, and the objects those need.
3. **The program is never linked alone.** The link line places the startup
   object `crt1.o` before the program and the library after it.
4. **The program does not start in `main`.** `_start`, in `crt1.o`, collects
   `argc` and `argv` from the stack where the kernel left them and calls
   `__libc_start_main`, which initialises the library, calls `main`, and passes
   its result to `exit`.
5. **Few functions reach the kernel.** Traced with `strace`, the program makes
   one `brk` (malloc moving the heap's end once; later allocations do not reach
   the kernel), one `openat` that returns `ENOENT`, two `write`s and one
   `exit_group`. `printf` did not call the kernel when it was called: stdio
   buffered the text and wrote it on the newline when the output was a
   terminal, and at `exit` when it was a pipe.
6. **The stub is four instructions.** glibc's `write` loads the service number
   into `rax`, executes `syscall`, and treats a result between −4095 and −1 as
   an error: it negates it into `errno` and returns −1. The arguments are not
   touched, because the kernel takes them in the same registers the C calling
   convention put them in; the number goes in `rax`, which the convention does
   not use for arguments.
7. **The kernel has one table.** Linux keeps its services in a single file,
   `arch/x86/entry/syscalls/syscall_64.tbl` — `1 common write sys_write` — and
   generates the numbers and the dispatch table from it.

Small systems use a variant of this called the **newlib model**: the library
ships complete for ISO C and asks the port for about twenty glue functions
(`_write`, `_read`, `_sbrk`, `_exit` …). **Calypsi's C library follows the same
model**, with its glue functions named `_Stub_*` (§5).

Three facts carry over to noVa64 directly. The stub must not move the
arguments, so the service number needs a register the convention leaves free.
Errors travel as negative numbers and the library turns them into `errno`,
holding every message string itself. And one table, written once, should
generate everything that depends on the numbering.

---

## 3. Decisions

All proposed in this revision.

| # | Decision | Rationale | § |
|---|---|---|---|
| D-1 | The C library is three layers: ISO C is Calypsi's `clib`; the POSIX layer, the `_Stub_*` glue and `crt0` are libnova, ours; the raw `sys_*` layer is generated. | The ISO library is as solved as the compiler (O.4's framing). What only noVa64 can write is the part that knows its kernel. | 4, 5 |
| D-2 | The raw layer uses the kernel handlers' own names and signatures, `sys_*`. The user build links them to `COP` stubs; the monolithic build links them to the kernel itself. | D103 then costs nothing: the POSIX layer is the same objects in both builds, and the only difference is which object defines `sys_*`. | 6, 10.1 |
| D-3 | One table, `syscalls.def`, generates the `SYS_*` constants, the prototypes, the stubs and the kernel's dispatch table. This note fixes the v1 numbering. | Numbering is the prerequisite O.7 names, and a number written in two places will eventually disagree. | 6.2 |
| D-4 | The service number travels in **Y**, not A. The signature byte is kept and equals the number. | Calypsi passes the first argument in A, so loading the number into A destroys it. | 6.3 |
| D-5 | The dispatcher copies a fixed argument window to the kernel's direct page and stack. It never interprets it. Every service takes at most four parameters of at most 32 bits. | The dispatcher must switch D and S (L.11), and Calypsi's direct-page and stack arguments are relative to them. | 6.4 |
| D-6 | Every pointer in the ABI is `__far24`, including the entries of `spawn`'s argument vector. | One pointer width for the kernel, whatever data model the caller was built with. O.3 already prefers `__far24` to the 32-bit default. | 6.5 |
| D-7 | A service returns ≥ 0 or −`E_*`. One transfer moves at most 32,767 bytes. libnova translates `E_*` to `errno`. The error space gains nine codes. | With a 16-bit `int`, the cap keeps every negative result an error. | 6.6 |
| D-8 | The ABI headers are ours, in fixed-width types, and no compiler vendor's header reaches the kernel. libnova translates at the edge (`errno`, `O_*`). | The same kernel serves Calypsi, the NVM32 libc and host tools, and each vendor numbers `errno` and `O_*` its own way. | 6.1, 7.3 |
| D-9 | Programs use large code and **small data**. Small data lives in the process's own virtual bank `$00`, which gets a NULL-guard page and a stack-guard page. | Calypsi's small data model runs with DBR = 0. In noVa64, bank `$00` is already private per process, so each process gets its own near data bank. | 8 |
| D-10 | `malloc` runs over Calypsi's fixed heap block. The block is a demand-zero segment of the executable, and C programs need no `sbrk` in v1. | Calypsi has no `sbrk` hook. With lazy frames (N.4) an unused heap costs no memory. | 5.4, 8.2 |
| D-11 | `spawn(path, argv)` replaces `exec(path)`. Every other POSIX name keeps its POSIX meaning: `lseek`, and `sleep` in seconds. | `exec` means "replace this process" in every program ever ported. The service creates a process and returns its PID. | 6.2, 7.5 |
| D-12 | Static linking only in v1. | Q21's shared text needs page reference counting in the loader for a memory saving nothing yet needs. | 10 |
| D-13 | The code lives in `sw/` in this repository. The SDK installs to `/opt/nova64/sdk`. `elf2nova` turns Calypsi's ELF into the native executable. | One repository holds the design and what implements it. The same packer serves the kernel image (D107). | 10 |
| D-14 | libnova, `crt0` and the ABI headers are GPLv3 with a linking exception. | Programs that link the runtime may carry any licence, and the runtime itself stays GPLv3, as the README requires of everything. | 12 |
| D-15 | Calypsi runs on the ARM64 devbox under `qemu-user`, behind wrapper scripts. | Calypsi 5.18 publishes x86-64 Linux packages only, and the devbox is ARM64 (sheet V7). | 11 |

---

## 4. Position in the system

```
 +---------------------------------------------------------------------+
 |  program   main.c                                                   |
 +---------------------------------------------------------------------+
 |  ISO C     <stdio.h> <string.h> <stdlib.h> <math.h> ...             |  Calypsi clib
 |               needs  _Stub_open _Stub_read _Stub_write ...          |  (not ours)
 +---------------------------------------------------------------------+
 |  POSIX     <unistd.h> <fcntl.h> <dirent.h> <sys/stat.h> <nova/*.h>  |  libnova
 |            _Stub_* glue, errno and O_* translation                  |  (ours, C,
 |                                                                     |   both builds)
 +---------------------------------------------------------------------+
 |  raw       sys_open sys_read sys_write ...        <nova/sys.h>      |  generated
 +----------------------------------+----------------------------------+
 |  user build: COP stubs + crt0    |  monolithic build: no library —  |  <- the only
 |                                  |  the kernel's own sys_*          |     difference
 +----------------------------------+----------------------------------+
 |  kernel    COP dispatcher -> service table -> sys_* handlers        |
 +---------------------------------------------------------------------+
```

A program may call any layer. Ordinary code uses ISO C and POSIX. A system
tool that must tell `E_CORRUPT` from `E_IO` calls `sys_*` directly, because
`errno` cannot carry the difference (§7.3).

---

## 5. The ISO layer — Calypsi's C library

### 5.1 What it provides

Calypsi ships its C library as static archives, one per code and data model —
`clib-lc-sd.a` is large code and small data. They hold the ISO C headers and
functions: stdio, string, stdlib (including `malloc`), ctype, math, setjmp,
stdarg, time. They are binaries under Calypsi's licence, free to use and not
open source. Programs link them statically, and only the objects used are
pulled in.

### 5.2 The hooks

The library reaches the operating system only through a fixed set of functions
the port provides. The list and signatures below are the ones implemented by
the public Calypsi 65816 board support for the Atari (Calypsi 5.18). OI-3
confirms the complete list against the installed `calypsi/stubs.h`.

| Hook | Signature | libnova implementation |
|---|---|---|
| `_Stub_open` | `int (const char *path, int oflag, ...)` | Translate `O_*` to `NV_O_*` (§7.3), `sys_open` |
| `_Stub_close` | `int (int fd)` | `sys_close` |
| `_Stub_read` | `size_t (int fd, void *buf, size_t count)` | `sys_read`, count capped at 32,767 |
| `_Stub_write` | `size_t (int fd, const void *buf, size_t count)` | `sys_write`, looped in chunks of at most 32,767 |
| `_Stub_lseek` | `long (int fd, long offset, int whence)` | `sys_lseek` |
| `_Stub_fgetpos` · `_Stub_fsetpos` | `int (int fd, fpos_t *pos)` · `int (int fd, const fpos_t *pos)` | Through `sys_lseek` |
| `_Stub_remove` | `int (const char *path)` | `sys_unlink` |
| `_Stub_rename` | `int (const char *old, const char *new)` | `sys_rename` |
| `_Stub_exit` | `void (int status)` | `sys_exit`; never returns |
| `_Stub_environ` | `char **(void)` | An empty environment, until environment variables exist |
| `_Stub_assert` | `void (const char *file, int line)` | `assert: file:line` on descriptor 2, then `sys_exit(255)` |

The Atari board support returns errors from its hooks as a negative `errno`
(`return -EBADF`). The older Foenix one sets `errno` and returns −1. OI-3
settles which convention the installed version expects.

### 5.3 How it is attached

The startup module and the hooks carry runtime-model attributes, and the linker
selects them by name:

```
ln65816 --rtattr cstartup=nova --rtattr stubs=nova --hosted ...
```

libnova's startup declares `.rtmodel cstartup,"nova"`, and its hook objects are
compiled with `--rtattr stubs=nova`. `--hosted` enables the stdio that runs over
the hooks. **libnova must come before `clib` on the link line.** The Atari
board support documents this as a known tool-chain issue.

### 5.4 The heap

Calypsi's `malloc` has no `sbrk` hook. It manages a fixed block, the section
`heap`, whose size the linker file declares. The startup module passes the
block's start and size to `__heap_initialize`. **This corrects O.7**, which
lists `sbrk` among Calypsi's hooks.

On noVa64 this suits the MMU. The heap block is declared in the executable as a
segment with a memory size and no file bytes, like `.bss`, and its pages arrive
on first touch (N.4). A program declaring a 32 KB heap and using 2 KB of it
costs the frames it touches — one or two, not sixteen. The `sbrk` service
stays in the table (§6.2) for the large data model and for the NVM32 libc.

### 5.5 What cannot be changed

`clib` is a binary, so a bug inside it can only be worked around. The Atari
board support found one in Calypsi 5.18: a code-generation error on
`p->a = p->b OP x` through a pointer kept on the stack, which made
`__fs_fdopen` corrupt the heap on a 256-byte `fwrite`. That is the reason for
the first abandonment condition in §18.

---

## 6. The raw layer — `sys_*`

### 6.1 The ABI headers

```
sw/abi/
  syscalls.def         the service table — the only place a number is written
  nova/abi/syscall.h   generated: SYS_* constants
  nova/abi/errno.h     E_* (Y1.15, extended in §6.6)
  nova/abi/fcntl.h     NV_O_*
  nova/abi/stat.h      struct nv_stat, struct nv_dirent
  nova/abi/ioctl.h     ioctl class bytes, TTY_*
```

Rules:

- **Fixed-width types only.** `int` is 16 bits under Calypsi and 32 bits in
  NVM32 and on the host. No ABI field or parameter has a width that depends on
  the compiler, except `int` in the service prototypes, which Calypsi's own
  headers force.
- **Static assertions on every structure size**, passing under host GCC and
  under Calypsi, as V0 requires of `vfs.h`.
- **No vendor header is included.** The kernel, libnova, the host tools and the
  NVM32 libc all compile these headers. Only libnova ever sees Calypsi's.
- **`NV_FAR` expands to `__far24` under Calypsi and to nothing elsewhere**, so
  the same prototypes compile on the host for LIB-02.

### 6.2 The service table and the v1 numbering

`syscalls.def` is an X-macro list, one line per service:

```c
/*        num  name    return    parameters                                  */
SYSCALL(  34,  read,   int,      (int fd, void NV_FAR *buf, uint16_t n))
SYSCALL(  35,  write,  int,      (int fd, const void NV_FAR *buf, uint16_t n))
```

`tools/gensys.py` generates from it:

- `nova/abi/syscall.h`, with the `SYS_*` constants;
- `nova/sys.h`, with the `sys_*` prototypes;
- `sys_cop.s`, with one stub per service (§6.3);
- the kernel's dispatch table, as a C array of handler addresses indexed by
  number, with `sys_nosys` in every unused slot;
- and later the NVM32 marshalling table (VM1.39).

Numbers are grouped by sixteens, with room in each group. The table has 128
entries, so every number fits the signature byte.

| # | Service | Prototype, without the `sys_` prefix and `NV_FAR` | Returns | First needed |
|---|---|---|---|---|
| 0 | — | Reserved. A zero number is a stub bug. | −`E_NOSYS` | — |
| 1 | `exit` | `void exit(int status)` | Never returns | CON-04 |
| 2 | `spawn` | `int spawn(const char *path, const char *const *argv)` | PID | CON-04 |
| 3 | `wait` | `int wait(int pid, int *status)` | PID | CON-04 |
| 4 | `getpid` | `int getpid(void)` | PID | CON-04 |
| 5 | `yield` | `int yield(void)` | 0 | E-series |
| 6 | `sleep` | `int sleep(uint16_t ms)` | 0 | E-series |
| 7 | `kill` | `int kill(int pid, int sig)` | 0 | CON-04 |
| 16 | `sbrk` | `int32_t sbrk(int32_t delta)` | Previous break | E-series |
| 17 | `mmap` | `int32_t mmap(uint32_t addr, uint32_t len, uint16_t prot)` | Address | E-series |
| 18 | `munmap` | `int munmap(uint32_t addr, uint32_t len)` | 0 | E-series |
| 19 | `mshare` | `int mshare(int pid, uint32_t addr)` | 0 | E-series |
| 32 | `open` | `int open(const char *path, uint16_t flags)` | Descriptor | CON-02 |
| 33 | `close` | `int close(int fd)` | 0 | CON-02 |
| 34 | `read` | `int read(int fd, void *buf, uint16_t n)` | Bytes | CON-01 |
| 35 | `write` | `int write(int fd, const void *buf, uint16_t n)` | Bytes | CON-01 |
| 36 | `lseek` | `int32_t lseek(int fd, int32_t off, int whence)` | New offset | LIB-04 |
| 37 | `ioctl` | `int ioctl(int fd, uint16_t op, void *arg)` | Per op | CON-01 |
| 38 | `dup` | `int dup(int fd)` | Descriptor | CON-04 |
| 39 | `dup2` | `int dup2(int fd, int fd2)` | `fd2` | CON-04 |
| 40 | `stat` | `int stat(const char *path, struct nv_stat *st)` | 0 | CON-02 |
| 41 | `fstat` | `int fstat(int fd, struct nv_stat *st)` | 0 | LIB-04 |
| 42 | `unlink` | `int unlink(const char *path)` | 0 | CON-03 |
| 43 | `rename` | `int rename(const char *from, const char *to)` | 0 | CON-03 |
| 44 | `mkdir` | `int mkdir(const char *path)` | 0 | CON-03 |
| 45 | `rmdir` | `int rmdir(const char *path)` | 0 | CON-03 |
| 46 | `chdir` | `int chdir(const char *path)` | 0 | CON-02 |
| 47 | `getcwd` | `int getcwd(char *buf, uint16_t size)` | Length | CON-02 |
| 48 | `sync` | `int sync(void)` | 0 | CON-03 |
| 49 | `opendir` | `int opendir(const char *path)` | Descriptor | CON-02 |
| 50 | `readdir` | `int readdir(int fd, struct nv_dirent *ent)` | 1, or 0 at the end | CON-02 |
| 64 | `time` | `int32_t time(void)` | Seconds since 1970; 0 until an RTC exists (Q88) | LIB-04 |
| 65 | `uptime` | `int32_t uptime(void)` | Milliseconds since boot, from the fixed-frequency counter (L.15) | LIB-04 |
| 80 | `pipe` | `int pipe(int *fds)` | 0 | After CON-04 |
| 81–85 | — | Reserved for sheet V's `port_create`, `port_send`, `port_recv`, `task_wait`, `task_signal` | | G-series |
| 96–127 | — | Reserved | | |

Notes on the table:

- **This reconciles sheet J with Y1.20 and CN1.26 (Q183).** It adds `chdir`,
  `getcwd`, `rmdir`, `dup`, `dup2`, `sync` and `opendir`. It also adds `fstat`
  and `uptime`, which libnova needs.
- **`closedir` is not a service.** A directory descriptor is a `file_t` with
  the directory flag (Y1.20), so `sys_close` releases it.
- **`mkdir` and `open` take no mode.** Permissions are never enforced (Y1.16).
  The POSIX wrappers accept a mode and ignore it.
- **`sleep` takes milliseconds up to 65,535**, and libnova loops for longer
  waits.
- **The memory group's signatures are provisional.** Nothing before the
  E-series calls them, and they are settled there.

### 6.3 The stub

Every stub is the same three instructions, generated:

```asm
; sys_cop.s — generated from syscalls.def by gensys.py; do not edit
              .section code
              .public sys_write
sys_write:    ldy     ##SYS_WRITE     ; the number in Y: A carries the first argument
              cop     #SYS_WRITE      ; signature byte = number, for tracers only (J.3)
              rtl                     ; result in A, or X:A for 32 bits
```

**Why Y and not A.** J.3 puts the number in A, loaded immediately before the
`COP`. Calypsi passes the first argument in A. Two pieces of Calypsi's own code
show it: the Foenix startup module calls `main` after `lda ##0 ; argc = 0`, and
a compiled listing of `void add(int *p)` in the Atari board support stores `p`
from A as its first instruction (`sta 1,s`). So `write(fd, buf, n)` would reach
the kernel with its `fd` replaced by the service number. **Y is the proposal,
and LIB-00 confirms that Calypsi passes no argument in Y.** If it does, the
fallback is the earlier mechanism: the dispatcher reads the signature byte from
the stack, which costs 20–30 cycles per call (§18).

Two more rules:

- **C symbols have no leading underscore.** The C function `sys_write` is the
  assembly label `sys_write`, as Calypsi's startup code references `main` and
  `exit` directly. The handoff's `_write` label does not apply.
- **The stub assumes Calypsi's register state**: native mode, with 16-bit
  accumulator and index registers. `RTI` restores P, so the dispatcher may
  change the widths freely.

`gensys.py --check` disassembles the generated stubs and verifies that each
signature byte equals its `ldy` operand. A tracer that trusts the byte depends
on it.

### 6.4 The dispatcher contract (a requirement on the kernel)

In the monolithic build a service is an ordinary `JSL` to the kernel's
`sys_write`, and nothing in this subsection applies. In the user build the
`COP` handler must make the call look exactly like that `JSL`:

1. **Validate the number.** Y ≥ 128, or an empty slot, returns −`E_NOSYS`.
2. **Keep A and X.** They carry arguments exactly as Calypsi left them.
3. **Copy the argument window.** Calypsi also passes arguments in direct-page
   pseudo-registers and on the stack. Both are relative to the caller's D and
   S, and the handler must switch both before doing real work (L.11). So the
   dispatcher copies:
   - Calypsi's direct-page parameter area, from the user's direct page into the
     kernel's, at the same offsets. LIB-00 fixes its size.
   - The 16 bytes above the `COP` frame and the stub's return address, from the
     user stack onto the kernel stack, in the same order. With at most four
     parameters of at most 32 bits, no service passes more.

   It then calls the handler with `JSL`, so the handler sees its arguments
   where Calypsi's convention says they are.
4. **Return the result** in A, or X:A for a 32-bit result, through `RTI`.

**This refines J.3 rather than contradicting it.** No argument is converted,
reordered or interpreted, and J.3's "nothing is marshalled" holds in that
sense. What the dispatcher adds is a fixed-size copy, and the copy exists
because the kernel changes stacks.

Two further rules bind the handlers in the user build:

- **Every pointer is checked before it is used.** It must be in banks
  `$02`–`$EF` or in the user part of bank `$00`, and never in `$F0`–`$FF`
  (L.10). A failing check returns −`E_FAULT`.
- **No handler keeps a pointer into user memory after it returns.**

Two rules bind the prototypes:

- At most four parameters, none wider than 32 bits.
- No structure by value, and no pointer inside a structure, except the argument
  vector of §6.5.

### 6.5 Pointers

**Every pointer parameter is `__far24` (`NV_FAR`).** A small-data caller holds
16-bit pointers, and Calypsi widens them at the call because the prototype asks
for a far pointer. A large-data caller holds 32-bit far pointers, which narrow
to 24 bits. Either way the kernel receives one width, and never needs to know
how the caller was compiled.

`spawn`'s argument vector is the one ABI structure that contains pointers.
**Its entries are `__far24` too**, so libnova's `spawn()` builds the vector from
the program's `char *argv[]` on its own stack: at most 32 entries plus the
terminator, 99 bytes. The kernel copies the vector and the strings into the new
process before `spawn` returns (§9.1).

### 6.6 Results and errors

- **A result ≥ 0 is success. A result from −1 to −127 is an error, −`E_*`.**
- **One `read` or `write` moves at most 32,767 bytes.** The kernel clamps the
  count, so a non-negative 16-bit result is never mistaken for an error. The
  hooks and the POSIX wrappers loop for larger requests. stdio never asks for
  more than its buffer.
- **32-bit results** (`lseek`, `sbrk`, `mmap`, `time`, `uptime`) are negative
  only for errors, since offsets and addresses stay below 2³¹.

The error space of Y1.15 stays the only one. The filesystem's codes −1 to −21
are unchanged, and the process, memory and dispatch services add these:

| Code | Meaning |
|---|---|
| `E_NOMEM` −22 | No memory or no address space |
| `E_FAULT` −23 | A pointer argument outside the caller's memory |
| `E_NOEXEC` −24 | Not a valid executable |
| `E_CHILD` −25 | No such child to wait for |
| `E_SRCH` −26 | No such process |
| `E_NOTTY` −27 | `ioctl` not meant for this device |
| `E_PIPE` −28 | Write to a pipe with no reader |
| `E_NOSYS` −29 | No such service |
| `E_2BIG` −30 | Argument vector too long |

### 6.7 Flags, structures and ioctl numbers

**Open flags**:

| Flag | Value |
|---|---|
| `NV_O_RDONLY` | `$0000` |
| `NV_O_WRONLY` | `$0001` |
| `NV_O_RDWR` | `$0002` |
| `NV_O_ACCMODE` | `$0003` |
| `NV_O_CREAT` | `$0100` |
| `NV_O_EXCL` | `$0200` |
| `NV_O_TRUNC` | `$0400` |
| `NV_O_APPEND` | `$0800` |

There is no text mode: `\n` ends a line everywhere, so `O_BINARY` and `O_TEXT`,
where Calypsi defines them, are accepted and ignored.

**`struct nv_stat` is `vattr_t`** (DN-FS-VFS-001 §5), field for field:

- `mode` and `nlink`, 16 bits;
- `size`, `blocks`, `atime`, `mtime`, `ctime` and `file_id`, 32 bits;
- `uid` and `gid`, 16 bits.

**`struct nv_dirent` is `dirent_out_t`**: `file_id`, `type`, `name_len` and a
256-byte name, 262 bytes in all.

The kernel copies both structures without translating them, and `mode` is
already POSIX (Y2.9).

**`ioctl` numbers are 16 bits.** The high byte names a device class, so an
operation sent to the wrong device fails with `E_NOTTY` instead of meaning
something else there. `'T'` (`$54`) is the tty:

| Operation | Value |
|---|---|
| `TTY_GETMODE` | `$5401` |
| `TTY_SETMODE` | `$5402` |
| `TTY_GETSIZE` | `$5403` |
| `TTY_CLEAR` | `$5404` |
| `TTY_SETFG` | `$5405` |

`'P'` (`$50`) is reserved for `/dev/power` (S.11).

---

## 7. The POSIX layer — libnova

### 7.1 Headers and contents

| Header | Contents | First needed |
|---|---|---|
| `<nova/sys.h>` | The raw layer, generated | CON-01 |
| `<unistd.h>` | `read` `write` `close` `lseek` `dup` `dup2` `unlink` `rmdir` `chdir` `getcwd` `sync` `getpid` `isatty` `sleep` `usleep` `_exit`; `ssize_t` | CON-01 to CON-04 |
| `<fcntl.h>` | `open` and `O_*`: Calypsi's header, if it ships one (§7.2) | CON-02 |
| `<sys/stat.h>` | `stat` `fstat` `mkdir`; `struct stat` with the layout of `nv_stat` and POSIX field names | CON-02 |
| `<dirent.h>` | `opendir` `readdir` `closedir`; `struct dirent` with the layout of `nv_dirent` | CON-02 |
| `<sys/ioctl.h>` | `ioctl` | CON-01 |
| `<nova/tty.h>` | `TTY_*`, and the mode and size structures | CON-01 |
| `<nova/sysinfo.h>` | `nv_sysinfo()`, a pointer to the system information block | CON-01 |
| `<nova/proc.h>` · `<sys/wait.h>` | `spawn` · `wait` `waitpid` `kill` `yield` | CON-04 |
| `<nova/time.h>` | `nv_uptime()` | LIB-04 |
| `<nova/mem.h>` | `sbrk` `mmap` `munmap` `mshare` | E-series |

**One function per source file**, as Calypsi's library and glibc do, because
the linker pulls whole objects: a program that never lists a directory never
carries `opendir`'s buffer pool.

**Not provided:** `fork`, the `exec` family, signals beyond `kill`, environment
variables, locale, wide characters, threads, sockets and `system()`.

### 7.2 Headers that Calypsi may already ship

**libnova never ships a header with the same name as one of Calypsi's.** Which
one the compiler would find would then depend on include order, and the two
would disagree about values.

LIB-00 takes an inventory. Where Calypsi ships a header, libnova uses it and
its values. `fcntl.h` is the likely case, since the hook interface uses `O_*`.
Where Calypsi ships none, libnova provides it. A prototype that Calypsi's header
lacks, such as `open` in its `fcntl.h`, is declared in libnova's `<unistd.h>`.

### 7.3 Translation — the only place vendor values meet ABI values

Every POSIX wrapper has the same shape:

```c
ssize_t write(int fd, const void *buf, size_t n)
{
    int r = sys_write(fd, buf, n > 32767 ? 32767 : n);
    return r < 0 ? __nv_fail(r) : r;          /* errno = __nv_errno[-r]; return -1 */
}
```

POSIX allows a short write, so the wrapper clamps. The hooks, which must write
everything, loop instead.

**`E_*` → `errno`** is one table indexed by −r. Every code maps to the value of
the same name in Calypsi's `errno.h`, except these four:

| Code | Maps to |
|---|---|
| `E_NOFILE` | `ENOSPC` |
| `E_CORRUPT` | `EIO` |
| `E_FEATURE` | `ENOTSUP` |
| `E_NOTMOUNTED` | `ENODEV` |

Where Calypsi's header lacks a name, the nearest one it has is used (OI-4).
`strerror` and `perror` are Calypsi's, so **the message text lives in user
space and the kernel carries none**, as CN1.21 requires. A tool that must
distinguish `E_CORRUPT` from a plain `E_IO` calls `sys_*` and reads the raw
code.

**`O_*` → `NV_O_*`** is one function, used by `_Stub_open` and `open`.

**Nothing else is translated.** `struct stat` and `struct dirent` are the ABI
layouts, and the `ioctl` numbers are the ABI's.

### 7.4 Directories

A `DIR` holds a descriptor and one `nv_dirent`, 264 bytes. They come from a
static pool of four. There is no `malloc` here, because the shell uses none
(CN1.24).

`readdir` passes the `DIR`'s own entry to `sys_readdir`, which satisfies Y1.12:
the 262-byte buffer is always the caller's. `closedir` returns the slot and
calls `sys_close`.

### 7.5 Processes

- **`spawn(path, argv)`** builds the `__far24` vector (§6.5) and calls
  `sys_spawn`. The child inherits descriptors 0–2, which is what the console
  assumes (Q178).
- **`waitpid(pid, &status, 0)`** and **`wait(&status)`** call `sys_wait`.
- **`exit(status)`** is Calypsi's. It runs the `atexit` handlers, flushes the
  streams, and ends in `_Stub_exit` → `sys_exit`.
- **`_exit(status)`** calls `sys_exit` directly, without flushing.

### 7.6 Time

- **`sleep(s)`** calls `sys_sleep(1000)` `s` times.
- **`usleep(us)`** calls `sys_sleep` with the time rounded up to milliseconds.
- **`time()`** returns 0 until an RTC exists (Q88). Every file the machine
  writes already carries that zero (Y1.17).
- **`nv_uptime()`** is the monotonic clock, in milliseconds since boot.
- **`clock()`**, if Calypsi routes it through a hook, maps to `sys_uptime`
  (OI-3).

### 7.7 The system information block

**`nv_sysinfo()` needs no syscall.** The block is a read-only page mapped
identically in every process at the top of bank `$00` (L.11), so the function
returns a constant pointer. Its layout belongs to sheet I, and libnova only
includes it.

---

## 8. Program memory layout

### 8.1 The data model

**Calypsi's small data model runs with the data bank register at zero.** Its
own Foenix startup module sets DBR to 0 when `__CALYPSI_DATA_MODEL_SMALL__` is
defined, and to the bank of `_NearBaseAddress` otherwise. The Atari board
support says it outright: in the small data model, bank `$00` "is where a
small-model program's plain pointers point".

It follows from the processor. The stack is always in bank `$00`, and a plain
16-bit pointer must be able to address a local variable. LIB-00 confirms it
against compiled output (OI-2).

**That corrects O.3 and N.4.** Small data is not "pinned to one bank with a
fixed DBR" placed after `.text`. It is bank `$00`.

On noVa64 this is a good fit. Virtual bank `$00` already belongs to each
process (L.10), so **every process gets a private 64 KB near-data bank**, less
the reserved top, while its code runs from `$02` upward. The default model for
every program is therefore large code and small data, with the libraries
suffixed `-lc-sd`.

### 8.2 Bank `$00`

| Range | Contents | Mapping |
|---|---|---|
| `$0000`–`$07FF` | Unmapped | **NULL guard**: a NULL dereference faults instead of corrupting the direct page |
| `$0800`–`$08FF` | Direct page, 256 bytes | Read-write |
| `$0900` upward | Near constants, `.data`, `.bss`, then the heap block | Read-write; `.bss` and the heap are demand-zero |
| Below the stack | One unmapped page | **Stack guard**: an overflow faults instead of overwriting the heap |
| `$D000`–`$DFFF` | The stack, 4 KB by default, set in the header; the argument block at its top (§9.1) | Read-write, **pinned** (L.11) |
| `$E000`–`$FFFF` | Reserved: vectors, trampolines, kernel stack, system information block | Privileged, except the information block, which is read-only |

The last row assumes 8 KB until Q18 sizes the privileged pages. That leaves
about **48 KB for the direct page, data, `.bss` and heap** together. A program
that needs more uses the large data model (§8.4). Both guard pages cost address
space, not memory: an unmapped page has no frame.

### 8.3 Code

**Code starts at `$02:0000`** (N.4) and occupies as many banks as it needs.
**The linker file declares one memory per bank**, so no function straddles a
bank boundary. The program counter wraps inside its bank, as L.10 says of the
kernel, and the Atari board support links the same way for the same reason.

These sections go with the code:

- far constants (`cfar`);
- switch tables;
- Calypsi's `code` and `libcode` sections.

Code pages are read and execute only.

### 8.4 The large data model — deferred

A program whose data outgrows bank `$00` is built with `--data-model=large`
against `-lc-ld` libraries. Default pointers are then 32 bits, and its data
lives in banks after its code. libnova's sources support both models, since
they are compiled twice, but v1 builds only `-lc-sd`. The layout of the large
model is settled when the first program needs it (OI-9).

### 8.5 The linker file

`nova-user.scm`, one copy for every program (O.4), follows the table in §8.2:

- one memory for the direct page;
- one memory for the near sections, holding the heap block;
- one memory for the stack block;
- and one memory per code bank.

It requests **no data initialisation table**, because the loader has already
placed `.data` and zeroed `.bss` (§9.2). It is written at LIB-04, against the
installed Calypsi, with the Atari board support's `atari-far.scm` as the
reference for syntax.

---

## 9. Startup and exit

### 9.1 What the loader leaves (a requirement on N.6)

When the loader returns into user mode at the entry point, a program finds:

- its segments mapped with their permissions, `.data` copied, and `.bss` and
  the heap demand-zero;
- the two guard pages unmapped, and the stack mapped and pinned;
- native mode, with 16-bit registers;
- D and DBR unspecified, because `crt0` sets them;
- **the argument block at the top of the stack**, where Q178 recommends it,
  with S just below it.

The argument block is laid out like this:

```
S+1   argc               uint16
      argv[0] … argv[argc−1], then 0     __far24 each
      the strings, NUL-terminated
```

The block uses `__far24` pointers, the same format `spawn` passes in (§6.5).
The kernel therefore copies it without knowing the new program's data model.

### 9.2 `crt0`

`nova-startup.s` declares `.rtmodel cstartup,"nova"`. It is modelled on
Calypsi's own Foenix startup module, minus the section initialisation the
loader already did:

1. D ← `_DirectPageStart`, DBR ← 0.
2. `__initialize_global_streams` (stdio).
3. `__heap_initialize` with the heap block's start and size.
4. Compact `argv` in place, from 3-byte far entries to 2-byte near ones. A
   near entry never overtakes the far entry it is read from.
5. `argc` and `argv` placed as Calypsi passes a function's first two
   parameters. `call main`, then `jump exit` with the result.

### 9.3 Exit

- **Returning from `main` is `exit`.** `exit` runs the `atexit` handlers and
  flushes stdio, then `_Stub_exit` calls `sys_exit`.
- **A failed `assert`** writes one line to descriptor 2 and exits with status
  255.
- **A fault ends the process** with status 255 as well, and the shell reports
  it (CN1.23).

---

## 10. The two builds, the source tree and the SDK

### 10.1 The two console builds

| | Monolithic build | User-mode build |
|---|---|---|
| POSIX layer and hooks | `nova-posix-lc-sd.a` | `nova-posix-lc-sd.a`, the same objects |
| Defines `sys_*` | The kernel | `nova-rt-lc-sd.a`: the generated `COP` stubs |
| Startup | The kernel's; the shell is `sh_main()` | `crt0` from `nova-rt-lc-sd.a` |
| Data model | The kernel's (OI-1) | Small data, in bank `$00` |
| stdio | Kept out of the image (CN1.18) | Available |

**This is D103 exactly**: one library, one entry point and one source file
(`sh_exec.c`) differ. The "library" that differs is a set of generated stubs,
and the monolithic build replaces it with nothing at all.

The one condition is that the kernel's handlers carry exactly the prototypes of
§6.2. If a handler ever needs a different signature, for example an extra
context parameter, the monolithic build gets a shim library, which is the shape
CN1 originally described (§18).

### 10.2 The source tree

```
sw/
  abi/                     §6.1 — shared by kernel, libnova, host tools, NVM32 libc
  libnova/
    include/               unistd.h dirent.h sys/*.h nova/*.h   (nova/sys.h generated)
    src/posix/             one function per file: write.c open.c opendir.c …
    src/stubs/             _Stub_open.c … _Stub_assert.c
    src/errno.c            the E_* → errno table and the O_* translation
    src/rt/                nova-startup.s                        (user build only)
    gen/                   sys_cop.s                             (generated, user build only)
    linker-files/          nova-user.scm
    test/host/             LIB-02 unit tests, over a test double of sys_*
    test/emu/              hello.c readwrite.c args.c nullguard.c
    Makefile               nova-posix-lc-sd.a · nova-rt-lc-sd.a · install
  tools/
    gensys.py              syscalls.def → headers, stubs, dispatch table; --check
    elf2nova               Calypsi ELF → noVa64 native executable
```

The kernel will sit beside it, in `sw/kernel/`. The existing `C/` directory is a
W65C816SXB demonstration for WDC's compiler, under a non-commercial licence, and
none of this is built on it.

### 10.3 The executable

`ln65816` produces an ELF file. **`elf2nova` converts it into the native
executable** of N.5, and the same tool packs the kernel image, which uses the
same format (D107). It needs these things from the header Q20 will define:

- a magic value and a format version;
- the entry point;
- per segment, the virtual address, the file size, the memory size and the
  R/W/X bits;
- the stack size.

The heap is an ordinary segment with a memory size and no file bytes.

**Freeze the native header knowing that NVX exists** (N.5, VM1.41), so the two
share their shape.

### 10.4 The SDK, and how a program is built

`make install` in `sw/libnova` produces:

```
/opt/nova64/sdk/
  include/        libnova's headers and the ABI headers
  lib/            nova-posix-lc-sd.a   nova-rt-lc-sd.a
  linker-files/   nova-user.scm
  bin/            elf2nova
  nova.mk
```

A program needs three lines of Makefile:

```make
PROGRAM = hello
OBJS    = hello.o
include $(NOVA_SDK)/nova.mk
```

and `nova.mk` runs:

```sh
cc65816 --core=65816 --code-model=large --data-model=small \
        -I$NOVA_SDK/include -O2 -o hello.o hello.c
ln65816 --rtattr cstartup=nova --rtattr stubs=nova --hosted -o hello.elf \
        hello.o $NOVA_SDK/lib/nova-rt-lc-sd.a $NOVA_SDK/lib/nova-posix-lc-sd.a \
        clib-lc-sd.a $NOVA_SDK/linker-files/nova-user.scm
elf2nova hello.elf hello
```

The program is ordinary C:

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char buf[128];
    int  fd, n;

    printf("hello from %s\n", argv[0]);
    if ((fd = open("/etc/motd", O_RDONLY)) < 0) {
        perror("motd");
        return 1;
    }
    while ((n = read(fd, buf, sizeof buf)) > 0)
        write(1, buf, n);
    close(fd);
    return 0;
}
```

The card's host tool (CN1.25) copies the result into `/bin`, where the shell
finds it (CN1.23).

---

## 11. The development host

Calypsi 5.18 is published as Linux packages for x86-64 only (`.deb`, `.rpm`,
`.pkg.tar.zst`), plus a macOS package and a Windows archive. **The devbox of
sheet V7 is ARM64.** The decision is:

1. **Run the x86-64 binaries under `qemu-user` inside the devcontainer.** Call
   them through wrapper scripts named `cc65816`, `as65816`, `ln65816` and
   `nlib`, each of which calls `qemu-x86_64` with an x86-64 runtime root. The
   Makefiles then see a native tool. Explicit invocation needs nothing from the
   host kernel, which registering binfmt would. LIB-00 establishes which shared
   libraries the binaries need (OI-10).
2. **If that proves unreliable**, run the toolchain in an x86-64 container of
   its own, through Docker's platform emulation.
3. **As a last resort**, build in GitHub Codespaces, which is x86-64, and whose
   limits sheet V7 already describes.

Whichever works goes into the repository's `.devcontainer/Dockerfile`, which
already carries the emulator's toolchain for both architectures.

---

## 12. Licensing

**libnova, `crt0`, the ABI headers and the generator are GPLv3 with a linking
exception**, in the form eCos and GNU Guile use. Linking them into a program
does not by itself make the program subject to the GPL. Modified versions of
the runtime itself remain GPLv3.

Without the exception, every program that links the runtime statically, which
is every program, would have to be GPLv3. With it, the README's principle holds
for the runtime's own code and stops at the program boundary.

Calypsi's `clib` is under Calypsi's licence, and every program contains parts of
it. It has the same status as the compiler that produced the program, and this
note changes nothing there. It is worth noting from here, though, that the
masthead's "100% open toolchain" does not hold for the 65816 C path while
Calypsi is the compiler (O.2). The abandonment path of §18, PDCLib, is also the
path to an open ISO layer.

---

## 13. Gates

| Gate | Deliverable | Pass criterion | Target |
|---|---|---|---|
| **LIB-00** | The toolchain, installed and audited | Calypsi runs in the devcontainer (§11). The audit answers OI-1 to OI-5 against compiled output, not the manual: argument registers, the direct-page parameter area, stack-argument order and who removes it, 32-bit returns, DBR under small data, the hook convention, the header inventory. Written as the ABI part of E0.8. | Host |
| **LIB-01** | ABI headers and generator | `gensys.py` produces `syscall.h`, `sys.h`, `sys_cop.s` and the dispatch-table skeleton from `syscalls.def`. The ABI headers compile under host GCC and Calypsi, with every size assertion passing on both. `gensys.py --check` passes. | Host |
| **LIB-02** | libnova on the host | The POSIX layer and the translation tables, built with host GCC against a test double of `sys_*` over host POSIX, pass unit tests: `errno` and flag translation, the 32,767 clamp, `DIR` pool exhaustion, `spawn`'s vector packing. | Host |
| **LIB-03** | The monolithic link | The monolithic console links `nova-posix-lc-sd.a` against the kernel's `sys_*`. CON-02's golden screens pass with the shell calling POSIX functions through libnova. The resident size is measured and recorded. Coincides with CON-02. | Emulator, monolithic |
| **LIB-04** | User programs | `crt0`, the stubs, `nova-user.scm` and `elf2nova` work. `hello` (`printf`), `readwrite` (`fopen` · `fwrite` · `fseek` · `fread`), `args` (`argv` and the exit status) and `nullguard` (a NULL write ends that process while the shell survives) pass. Coincides with CON-04. | Emulator, user mode |
| **LIB-05** | The SDK | `make install` populates `/opt/nova64/sdk`. A program outside the tree builds from the three-line Makefile of §10.4 and runs from `/bin`. Then the same on hardware at P5.k. | Emulator, then hardware |

Each gate that runs a program passes in the emulator first, then on hardware.

---

## 14. Verification

- **Host unit tests** for every pure-C part of libnova (LIB-02), in plain C99
  with fixed-width types. The test double of `sys_*` returns chosen `E_*` codes,
  so the error paths are exercised without a kernel.
- **Emulator programs with expected output.** Each test program in `test/emu/`
  prints a transcript on descriptor 1. The emulator's console capture (sheet
  EM3) records it, and it is compared with a stored file.
- **Generated-code checks.** `gensys.py --check` verifies the stubs (§6.3). A
  dispatcher test calls every service number, including the reserved ones, and
  expects `E_NOSYS` from the empty slots.
- **ABI conformance.** One test compiles a call to each `sys_*` under Calypsi,
  disassembles it, and checks the argument placement against the audit of
  LIB-00. If a future Calypsi release changes its convention, this test is
  where it shows.

---

## 15. Requirements on other sheets and notes

- **The kernel (J.3):** the dispatcher contract of §6.4, the number in Y, the
  extended error space of §6.6, and handler prototypes identical to §6.2.
- **The loader (N.6):**
  - the state of §9.1: the argument block, the demand-zero heap segment and
    both guard pages;
  - `spawn` semantics, with descriptors 0–2 inherited (Q178).
- **The user map (Q17, Q18):** the bank `$00` layout of §8.2 as the input to
  both questions. In particular, the reserved top is assumed to be 8 KB.
- **The executable header (Q20):** the fields of §10.3.
- **The error space (Y1.15):** codes −22 to −30.
- **The development environment (sheet V7):** the Calypsi installation of §11.
- **The console (CN1):** the shell includes libnova's headers. Its own error
  table (CN1.21) may stay, keyed on the raw codes, or give way to `strerror`.
  Either is compatible with this note.

---

## 16. Discrepancies found with existing documents

1. **J.3, O.7 and the FPGA-A register notes put the service number in A.**
   Calypsi passes the first argument there (§6.3). This note moves the number
   to Y.
2. **O.7 lists `sbrk` among Calypsi's hooks.** There is none: `malloc` manages
   a fixed heap block (§5.4). The actual hooks are the `_Stub_*` set of §5.2.
3. **O.3 and N.4 place small data in a bank of its own after `.text`.** Under
   Calypsi, small data is bank `$00` (§8.1). N.4's ".data, then .bss, then the
   heap after .text" describes the large data model.
4. **J.3 says nothing is marshalled.** Because the dispatcher switches D and S,
   it copies a fixed argument window without interpreting it (§6.4).
5. **Sheet J's v1 table** has `exec(path)` with no argument vector, `seek` for
   `lseek`, `sleep(ms)` under a POSIX name, and lacks the Q183 services. §6.2
   supersedes it, and `closedir` is not a service.
6. **The toolchain handoff names the stub `_write`.** Calypsi's C symbols carry
   no leading underscore (§6.3).
7. **L.11 has no NULL guard.** The unmapped page at `$00:0000` is new, and so
   is the stack guard (§8.2).

---

## 17. Open items

| # | Item | Blocks | Recommendation |
|---|---|---|---|
| OI-1 | Calypsi's calling convention in full: which registers carry arguments (A and X shown, Y assumed free), the size of the direct-page parameter area, stack-argument order and who removes it, 32-bit returns in X:A, and the kernel's own data model | LIB-00, the dispatcher | E0.8 and Q19, against compiled output |
| OI-2 | Small data implies DBR = 0 | §8 | Confirm at LIB-00. If false, §8 is rewritten, not patched |
| OI-3 | The hook return convention (−`errno`, or `errno` plus −1) and the complete hook list, including whether `time` or `clock` go through a hook | LIB-04 | Read the installed `calypsi/stubs.h` |
| OI-4 | Calypsi's header inventory, and the values of its `O_*` and `errno` | LIB-02 | Inventory at LIB-00 (§7.2) |
| OI-5 | Library naming and the `--target` attribute. Since 5.18.2 the distributed Atari libraries are built with `--target Atari`, and a system with no Calypsi target uses the generic libraries | LIB-00 | Confirm the generic archive names |
| OI-6 | The sizes of the bank `$00` layout: reserved top, default stack, default heap | LIB-04 | Q17 and Q18. Defaults of §8.2 until then |
| OI-7 | The native executable header | LIB-04 | Q20, with §10.3's fields |
| OI-8 | stdout buffering on the console: line-buffered is expected, and CN1.10 assumes no buffering below the call | LIB-04 | Confirm Calypsi's policy and whether it asks for a tty test |
| OI-9 | The large data model's layout | First program over 48 KB of data | Data banks directly after code |
| OI-10 | Calypsi's runtime dependencies under `qemu-user` | LIB-00 | §11 |
| OI-11 | Shared read-only text (Q21) | — | Deferred (D-12) |

---

## 18. Abandonment conditions

- **Calypsi's `clib` (D-1).** Replace the ISO layer with PDCLib, compiled by
  Calypsi over the same `sys_*` layer, if either of these happens:
  - a `clib` bug cannot be worked around in libnova;
  - the hook interface cannot be served for more than two weeks.

  PDCLib is already the chosen NVM32 libc (VM4.22), so noVa64 would then carry
  one ISO library for both toolchains. The POSIX layer is unaffected either
  way.
- **Service number in Y (D-4).** If LIB-00 shows Calypsi passing an argument in
  Y, the dispatcher reads the signature byte instead, at 20–30 cycles per call.
  Verify its stacked position first (Q75).
- **Shared names (D-2).** If a kernel handler needs a signature different from
  its user prototype, the monolithic build gets a shim library, and D103 is
  unchanged.
- **Small data in bank `$00` (D-9).** If typical programs outgrow the 48 KB,
  make the large data model the default.
- **The fixed heap (D-10).** If sizing the heap per program becomes a nuisance,
  libnova provides `malloc`, `free`, `realloc` and `calloc` over `sys_sbrk`.
  Linked before `clib`, they replace Calypsi's.

---

## References

- Calypsi tool chains — https://www.calypsi.cc/ and releases at
  https://github.com/hth313/Calypsi-tool-chains/releases
- Calypsi 65816 board support for the Foenix C256 (startup module, hooks,
  linker files) — https://github.com/hth313/Calypsi-65816-Foenix
- Calypsi 65816 board support for the Atari XL/XE with a 65C816 (hooks,
  `.rtattr` selection, link order, the 5.18 code-generation bug) —
  https://github.com/slaapliedje/Calypsi-65816-Atari
- Linux system call table —
  `arch/x86/entry/syscalls/syscall_64.tbl` in the kernel source
- newlib, the system calls a port must provide — the "System Calls" chapter of
  the newlib C library manual, https://sourceware.org/newlib/
- PDCLib — https://github.com/DevSolar/pdclib
