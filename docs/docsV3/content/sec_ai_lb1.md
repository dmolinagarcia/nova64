# The C library — libnova and the syscall layer
> three layers · the service table and the stub · a program's memory · the SDK · the LIB series

Every native program links against the same runtime, and this document has said "libc" in several places — [O.7](sec_ai_o#o7), [J.3](sec_ai_j#j3), [CN1.1](sec_ai_cn1#cn11) — meaning a slightly different thing in each. This sheet fixes it: which parts come with the compiler and which are ours, the stubs that cross into the kernel and the table they are numbered from, where a program's data lives, how a program starts, and how it is built and put on the card. Everything here is native 65816 code compiled with Calypsi; the NVM32 C library is PDCLib over vbcc ([VM4.22](sec_ai_vm4#vm422)) and shares this sheet's ABI headers and nothing else.

- LB1.1 — **The C library is three layers, and noVa64 writes only the two thin ones** ([D112](sec_ai_q#d112)). ISO C — `printf`, `strlen`, `malloc`, `fopen` — is Calypsi's own library, and it is as solved a problem as the compiler ([O.4](sec_ai_o#o4)'s framing). What only this machine can write is the part that knows its kernel: **libnova**, the POSIX functions, the glue Calypsi's library asks for and the startup code; and **the raw layer**, one generated stub per service.
  NOTE: Consolidated from DN-SW-LIBC-001 Rev A, whose decisions are proposals for review rather than frozen. Its gates are the series [LIB-00](sec_ai_lb1#lib-00)–[LIB-05](sec_ai_lb1#lib-05) here, named as [A3.11](sc_a3#a311) requires.
- LB1.2 — **The raw layer carries the kernel handlers' own names and signatures — `sys_open`, `sys_read`, `sys_write` — and that is what makes the two console builds free** ([D113](sec_ai_q#d113)). In the user-mode build `sys_write` is a three-line `COP` stub; in the monolithic build the linker resolves it to the kernel's own `sys_write`, and there is no library at all. Everything above it is the same objects in both builds, which is [D103](sec_ai_q#d103) with nothing left over.
  NOTE: The condition is that every kernel handler keeps exactly its user prototype. If one ever needs another signature — an extra context parameter, say — the monolithic build gets a thin shim library, which is the shape [CN1.1](sec_ai_cn1#cn11) originally described, and [D103](sec_ai_q#d103) is unaffected.

## Where it sits — a program may call any layer, and only the bottom one differs between builds.

| Layer | Contents | Owner |
|---|---|---|
| Program | `main.c` | |
| ISO C | `<stdio.h>` · `<string.h>` · `<stdlib.h>` · `<math.h>` … | Calypsi's `clib`, not ours |
| POSIX | `<unistd.h>` · `<fcntl.h>` · `<dirent.h>` · `<sys/stat.h>` · `<nova/*.h>`, the `_Stub_*` glue, `errno` and `O_*` translation | libnova — the same objects in both builds |
| Raw | `sys_open` · `sys_read` · `sys_write` … in `<nova/sys.h>` | Generated from one table |
| The link | User-mode: generated `COP` stubs and `crt0` · Monolithic: nothing — the kernel's own `sys_*` | **The only difference** |
| Kernel | `COP` dispatcher → service table → `sys_*` handlers | Kernel |

## How an existing system does it — a ten-line program on Linux.

- LB1.3 — **Measured on Linux x86-64 with glibc, with a program that calls `malloc`, `printf` and a failing `open`, and built statically so the library can be looked at.** Headers only declare: `stdio.h` holds `printf`'s prototype and no code. `libc.a` is an archive of 2,211 objects, about one per function, and the linker takes only what is used. The program is never linked alone — `crt1.o` goes before it, the library after — and it does not start in `main`: `_start` collects `argc` and `argv` where the kernel left them and calls `__libc_start_main`, which calls `main` and hands its result to `exit`. Traced with `strace`, **only a handful of calls reach the kernel**: one `brk` for the heap, the failing `openat`, two `write`s and `exit_group`. `printf` never called the kernel when it was called — stdio buffered the line and wrote it at the newline on a terminal and at `exit` on a pipe.
  NOTE: **The stub is four instructions.** glibc's `write` loads 1 into `rax`, executes `syscall`, and treats a result between −4095 and −1 as an error, negating it into `errno` and returning −1. The arguments are never moved: the kernel takes them in the registers the C convention already put them in. And the kernel keeps its services in one file, `syscall_64.tbl` — `1 common write sys_write` — from which the numbers and the dispatch table are generated.
- LB1.4 — **Three facts carry over unchanged.** The stub must not move the arguments, so **the service number needs a register the calling convention leaves free** — on x86-64 that is `rax`, which carries no argument. **Errors travel as negative numbers**, and the library turns them into `errno` and holds every message string, so the kernel holds none ([CN1.21](sec_ai_cn1#cn121)). And **one table, written once, generates everything that depends on the numbering**. Small systems add a fourth: the library ships complete for ISO C and asks the port for about twenty glue functions — newlib's model, and Calypsi's.

## Calypsi's library — what comes with the compiler, and the dozen hooks it asks for.

- LB1.5 — **`clib` is Calypsi's ISO C library, shipped as static archives, one per code and data model** — `clib-lc-sd.a` is large code, small data. It covers stdio, string, stdlib with `malloc`, ctype, math, setjmp, stdarg and time, and a program carries only the objects it uses. **It is a binary under Calypsi's licence**, free to use and not open source, exactly like the compiler that ships it ([O.2](sec_ai_o#o2)).
- LB1.6 — **The library reaches an operating system only through `_Stub_*` functions, and libnova implements every one over `sys_*`.** The list and signatures are those implemented by the public Calypsi 65816 port for the Atari at Calypsi 5.18; the installed `calypsi/stubs.h` confirms them (→ [Q185](sec_ai_q#q185)).

| Hook | Signature | libnova |
|---|---|---|
| `_Stub_open` | `int (const char *path, int oflag, ...)` | `O_*` translated to `NV_O_*` ([LB1.19](sec_ai_lb1#lb119)), then `sys_open` |
| `_Stub_close` | `int (int fd)` | `sys_close` |
| `_Stub_read` | `size_t (int fd, void *buf, size_t count)` | `sys_read`, the count clamped to 32,767 |
| `_Stub_write` | `size_t (int fd, const void *buf, size_t count)` | `sys_write`, looped in chunks of at most 32,767 |
| `_Stub_lseek` | `long (int fd, long offset, int whence)` | `sys_lseek` |
| `_Stub_fgetpos` · `_Stub_fsetpos` | `int (int fd, fpos_t *pos)` · `int (int fd, const fpos_t *pos)` | Through `sys_lseek` |
| `_Stub_remove` · `_Stub_rename` | `int (const char *path)` · `int (const char *old, const char *new)` | `sys_unlink` · `sys_rename` |
| `_Stub_exit` | `void (int status)` | `sys_exit`; never returns |
| `_Stub_environ` | `char **(void)` | An empty environment, until there are environment variables |
| `_Stub_assert` | `void (const char *file, int line)` | `assert: file:line` on descriptor 2, then `sys_exit(255)` |

  NOTE: How a hook reports an error is version-dependent: the Atari port (5.18) returns a negative `errno`, the older Foenix port sets `errno` and returns −1. [Q185](sec_ai_q#q185) settles which one the installed version expects.
- LB1.7 — **The startup module and the hooks are selected by attribute at link time**: libnova's `crt0` declares `.rtmodel cstartup,"nova"`, its hook objects are compiled with `--rtattr stubs=nova`, and a program links with `--rtattr cstartup=nova --rtattr stubs=nova --hosted`, the last enabling the stdio that runs over the hooks. **libnova must come before `clib` on the link line** — a known tool-chain issue the Atari port documents.
- LB1.8 — **Calypsi's `malloc` has no `sbrk` hook: it manages a fixed block, the section `heap`, whose size the linker file declares** ([D121](sec_ai_q#d121)). The startup module passes its start and size to `__heap_initialize`. **This corrects [O.7](sec_ai_o#o7)**, which listed `sbrk` among the hooks — and on this machine the fixed block suits the MMU: it is a segment with a memory size and no file bytes, like `.bss`, and its pages arrive on first touch ([N.4](sec_ai_n#n4)), so a 32 KB heap of which 2 KB is used costs the frames it touches, not sixteen.
  NOTE: The `sbrk` service stays in the table for the large data model and the NVM32 libc. If sizing a heap per program becomes a nuisance, libnova supplies `malloc`, `free`, `realloc` and `calloc` over `sys_sbrk`, and linked before `clib` they replace Calypsi's.
- LB1.9 — **`clib` is a binary, so a bug inside it can only be worked around.** The Atari port found one in 5.18: `p->a = p->b OP x` through a pointer kept on the stack loads `p->b` with the destination's offset, which made `__fs_fdopen` corrupt the heap on a 256-byte `fwrite`. **If a `clib` bug cannot be worked around in libnova, or the hook interface cannot be served, for more than two weeks, replace the ISO layer with PDCLib compiled by Calypsi over the same `sys_*`.** PDCLib is already the NVM32 libc ([VM4.22](sec_ai_vm4#vm422)), so the machine would then carry one ISO library for both toolchains, and an open one; the POSIX layer is unaffected either way.

## The service table — one file, and the v1 numbering.

- LB1.10 — **`syscalls.def` is the only place a service number is written** ([D114](sec_ai_q#d114)), an X-macro list with one line per service — `SYSCALL(35, write, int, (int fd, const void NV_FAR *buf, uint16_t n))`. `gensys.py` generates from it the `SYS_*` constants, the `sys_*` prototypes in `<nova/sys.h>`, one stub per service, **the kernel's dispatch table** — handler addresses indexed by number, `sys_nosys` in every empty slot — and later the NVM32 marshalling table ([VM1.39](sec_ai_vm1#vm139)). Numbers group by sixteens with room in each group, and **the table has 128 entries**, so every number also fits the signature byte. **Numbering the `SYS_*` constants was the prerequisite [O.7](sec_ai_o#o7) named, and this is it.**

| # | Service | Prototype, without the `sys_` prefix and `NV_FAR` | Returns | First needed |
|---|---|---|---|---|
| 0 | — | Reserved: a zero number is a stub bug | −`E_NOSYS` | — |
| 1 | `exit` | `void exit(int status)` | Never returns | [CON-04](sec_ai_cn1#con-04) |
| 2 | `spawn` | `int spawn(const char *path, const char *const *argv)` | PID | [CON-04](sec_ai_cn1#con-04) |
| 3 | `wait` | `int wait(int pid, int *status)` | PID | [CON-04](sec_ai_cn1#con-04) |
| 4 | `getpid` | `int getpid(void)` | PID | [CON-04](sec_ai_cn1#con-04) |
| 5 | `yield` | `int yield(void)` | 0 | E-series |
| 6 | `sleep` | `int sleep(uint16_t ms)` | 0 | E-series |
| 7 | `kill` | `int kill(int pid, int sig)` | 0 | [CON-04](sec_ai_cn1#con-04) |
| 16 | `sbrk` | `int32_t sbrk(int32_t delta)` | Previous break | E-series |
| 17 | `mmap` | `int32_t mmap(uint32_t addr, uint32_t len, uint16_t prot)` | Address | E-series |
| 18 | `munmap` | `int munmap(uint32_t addr, uint32_t len)` | 0 | E-series |
| 19 | `mshare` | `int mshare(int pid, uint32_t addr)` | 0 | E-series |
| 32 | `open` | `int open(const char *path, uint16_t flags)` | Descriptor | [CON-02](sec_ai_cn1#con-02) |
| 33 | `close` | `int close(int fd)` | 0 | [CON-02](sec_ai_cn1#con-02) |
| 34 | `read` | `int read(int fd, void *buf, uint16_t n)` | Bytes | [CON-01](sec_ai_cn1#con-01) |
| 35 | `write` | `int write(int fd, const void *buf, uint16_t n)` | Bytes | [CON-01](sec_ai_cn1#con-01) |
| 36 | `lseek` | `int32_t lseek(int fd, int32_t off, int whence)` | New offset | [LIB-04](sec_ai_lb1#lib-04) |
| 37 | `ioctl` | `int ioctl(int fd, uint16_t op, void *arg)` | Per operation | [CON-01](sec_ai_cn1#con-01) |
| 38 | `dup` | `int dup(int fd)` | Descriptor | [CON-04](sec_ai_cn1#con-04) |
| 39 | `dup2` | `int dup2(int fd, int fd2)` | `fd2` | [CON-04](sec_ai_cn1#con-04) |
| 40 | `stat` | `int stat(const char *path, struct nv_stat *st)` | 0 | [CON-02](sec_ai_cn1#con-02) |
| 41 | `fstat` | `int fstat(int fd, struct nv_stat *st)` | 0 | [LIB-04](sec_ai_lb1#lib-04) |
| 42 | `unlink` | `int unlink(const char *path)` | 0 | [CON-03](sec_ai_cn1#con-03) |
| 43 | `rename` | `int rename(const char *from, const char *to)` | 0 | [CON-03](sec_ai_cn1#con-03) |
| 44 | `mkdir` | `int mkdir(const char *path)` | 0 | [CON-03](sec_ai_cn1#con-03) |
| 45 | `rmdir` | `int rmdir(const char *path)` | 0 | [CON-03](sec_ai_cn1#con-03) |
| 46 | `chdir` | `int chdir(const char *path)` | 0 | [CON-02](sec_ai_cn1#con-02) |
| 47 | `getcwd` | `int getcwd(char *buf, uint16_t size)` | Length | [CON-02](sec_ai_cn1#con-02) |
| 48 | `sync` | `int sync(void)` | 0 | [CON-03](sec_ai_cn1#con-03) |
| 49 | `opendir` | `int opendir(const char *path)` | Descriptor | [CON-02](sec_ai_cn1#con-02) |
| 50 | `readdir` | `int readdir(int fd, struct nv_dirent *ent)` | 1, or 0 at the end | [CON-02](sec_ai_cn1#con-02) |
| 64 | `time` | `int32_t time(void)` | Seconds since 1970; 0 until an RTC exists ([Q88](sec_ai_q#q88)) | [LIB-04](sec_ai_lb1#lib-04) |
| 65 | `uptime` | `int32_t uptime(void)` | Milliseconds since boot, from the fixed-frequency counter ([L.15](sec_ai_l#l15)) | [LIB-04](sec_ai_lb1#lib-04) |
| 80 | `pipe` | `int pipe(int *fds)` | 0 | After [CON-04](sec_ai_cn1#con-04) |
| 81–85 | — | Reserved for `port_create` · `port_send` · `port_recv` · `task_wait` · `task_signal` ([J.7](sec_ai_j#j7)) | | G-series |
| 96–127 | — | Reserved | | |

- LB1.11 — **The table reconciles [sheet J](sec_ai_j)'s v1 dispatch table with [Y1.20](sec_ai_y1#y120) and [CN1.26](sec_ai_cn1#cn126)**, closing [Q183](sec_ai_q#q183): `chdir`, `getcwd`, `rmdir`, `dup`, `dup2`, `sync` and `opendir` are in; `exec(path)` became `spawn(path, argv)` ([D122](sec_ai_q#d122)); `seek` is `lseek`; `fstat` and `uptime` are new, because libnova needs them. **`closedir` is not a service**: a directory descriptor is a `file_t` with the directory flag ([Y1.20](sec_ai_y1#y120)), so `sys_close` releases it. `open` and `mkdir` take no mode, since permissions are never enforced ([Y1.16](sec_ai_y1#y116)), and the POSIX wrappers accept one and ignore it. `sleep` takes up to 65,535 ms, and libnova loops for more.
  NOTE: **The memory group's signatures are provisional.** Nothing before the E-series calls them, and they are settled there.

## The stub and the dispatcher — the number in Y, and a window copied rather than interpreted.

- LB1.12 — **Every stub is the same three instructions, generated, and the service number travels in Y** ([D115](sec_ai_q#d115)).
  ```asm
  ; sys_cop.s — generated from syscalls.def by gensys.py; do not edit
                .section code
                .public sys_write
  sys_write:    ldy     ##SYS_WRITE     ; the number in Y: A carries the first argument
                cop     #SYS_WRITE      ; signature byte = number, for tracers only (J.3)
                rtl                     ; result in A, or X:A for 32 bits
  ```
  NOTE: **Why not A, as [J.3](sec_ai_j#j3) had it: Calypsi passes a function's first argument in A.** Two pieces of Calypsi's own code show it — its Foenix startup module calls `main` after `lda ##0 ; argc = 0`, and a compiled `void add(int *p)` in the Atari port stores `p` from A as its first instruction — so a stub loading the number into A hands the kernel `write` with its `fd` replaced. It is the x86-64 lesson of [LB1.4](sec_ai_lb1#lb14): the number needs a register that carries no argument. **Y is the proposal, and [LIB-00](sec_ai_lb1#lib-00) confirms against compiled output that Calypsi passes nothing in Y.** If it does, the dispatcher reads the signature byte instead, at 20–30 cycles a call, once its stacked position is verified ([Q75](sec_ai_q#q75)).
  NOTE: Calypsi's C symbols carry no leading underscore — its startup code references `main` and `exit` as they are — so the C function `sys_write` is the label `sys_write`, not the handoff's `_write`. `gensys.py --check` disassembles the stubs and verifies that each signature byte equals its `ldy` operand, which is what a tracer trusting the byte depends on.
- LB1.13 — **The dispatcher makes a `COP` look exactly like the `JSL` the monolithic build makes, and to do that it copies a fixed window of arguments without interpreting any of them** ([D116](sec_ai_q#d116)). Y at or above 128, or an empty slot, returns −`E_NOSYS`. A and X are kept as Calypsi left them. Calypsi also passes arguments in direct-page pseudo-registers and on the stack, both relative to the caller's D and S — and **the handler must switch both before doing real work** ([L.11](sec_ai_l#l11)) — so the dispatcher copies the direct-page parameter area into the kernel's direct page at the same offsets, and the 16 bytes above the `COP` frame and the stub's return address onto the kernel stack in the same order. It then calls the handler with `JSL`, which finds its arguments where the convention says, and returns the result in A, or X:A, through `RTI`.
  NOTE: **This refines [J.3](sec_ai_j#j3)'s "nothing is marshalled" rather than contradicting it**: no argument is converted, reordered or read. The size of the direct-page area, the order of the stack arguments and which side removes them are [LIB-00](sec_ai_lb1#lib-00)'s to establish ([Q19](sec_ai_q#q19)).
  NOTE: **Two rules keep 16 bytes enough and the copy blind**: no service takes more than four parameters or one wider than 32 bits, and no structure travels by value or holds a pointer, except `spawn`'s argument vector ([LB1.14](sec_ai_lb1#lb114)). In the user-mode build every pointer is checked before use — banks `$02`–`$EF` or the user part of `$00`, never `$F0`–`$FF` ([L.10](sec_ai_l#l10)) — failing with −`E_FAULT`, and no handler keeps a user pointer after it returns.
- LB1.14 — **Every pointer in the ABI is `__far24`, including the entries of `spawn`'s argument vector** ([D117](sec_ai_q#d117)). A small-data caller holds 16-bit pointers, which Calypsi widens at the call because the prototype asks for a far one; a large-data caller's 32-bit pointers narrow to 24. **The kernel receives one width and never needs to know how its caller was compiled** — which is [Q114](sec_ai_q#q114)'s recommendation made into an ABI rule. libnova's `spawn()` builds the far vector from the program's `char *argv[]` on its own stack: at most 32 entries and the terminator, 99 bytes, and −`E_2BIG` beyond.
- LB1.15 — **A service returns ≥ 0 for success and −1…−127 for an error, and one `read` or `write` moves at most 32,767 bytes** ([D118](sec_ai_q#d118)). With a 16-bit `int` the kernel's clamp is what keeps a byte count from ever being read as an error; the hooks and the wrappers loop for more, and stdio never asks for more than its buffer. The 32-bit results — `lseek`, `sbrk`, `mmap`, `time`, `uptime` — are negative only for errors, since offsets and addresses stay below 2³¹. **[Y1.15](sec_ai_y1#y115)'s error space stays the only one**; its −1…−21 are unchanged, and the process, memory and dispatch services add nine codes.

| Code | Meaning | Code | Meaning |
|---|---|---|---|
| `E_NOMEM` −22 | no memory or no address space | `E_SRCH` −26 | no such process |
| `E_FAULT` −23 | a pointer argument outside the caller's memory | `E_NOTTY` −27 | `ioctl` not meant for this device |
| `E_NOEXEC` −24 | not a valid executable | `E_PIPE` −28 | write to a pipe with no reader |
| `E_CHILD` −25 | no such child to wait for | `E_NOSYS` −29 | no such service |
| | | `E_2BIG` −30 | argument vector too long |

- LB1.16 — **The ABI headers are ours, in fixed-width types, and no compiler vendor's header ever reaches the kernel** ([D119](sec_ai_q#d119)). `sw/abi/` holds `syscalls.def` and `nova/abi/` — `syscall.h` (generated), `errno.h`, `fcntl.h`, `stat.h`, `ioctl.h` — compiled by the kernel, libnova, the host tools and the NVM32 libc, each of which numbers `errno` and `O_*` its own way. Every structure size carries a static assertion that passes under host GCC and Calypsi alike, as [V0](sec_ai_y1#v0) requires of `vfs.h`, and `NV_FAR` expands to `__far24` under Calypsi and to nothing on the host.
  NOTE: **Open flags**: `NV_O_RDONLY` `$0000` · `NV_O_WRONLY` `$0001` · `NV_O_RDWR` `$0002` · `NV_O_CREAT` `$0100` · `NV_O_EXCL` `$0200` · `NV_O_TRUNC` `$0400` · `NV_O_APPEND` `$0800`. There is no text mode — `\n` ends a line everywhere — so `O_BINARY` and `O_TEXT`, where Calypsi defines them, are accepted and ignored.
  NOTE: **`struct nv_stat` is `vattr_t` and `struct nv_dirent` is `dirent_out_t`**, field for field — the latter 262 bytes — so the kernel copies both without translating, and `mode` is already POSIX ([Y2.9](sec_ai_y2#y29)). **`ioctl` numbers are 16 bits with a device class in the high byte**, so an operation sent to the wrong device fails with `E_NOTTY` instead of meaning something else there: `'T'` for the tty — `TTY_GETMODE` `$5401` · `TTY_SETMODE` `$5402` · `TTY_GETSIZE` `$5403` · `TTY_CLEAR` `$5404` · `TTY_SETFG` `$5405` ([CN1.13](sec_ai_cn1#cn113)) — and `'P'` reserved for `/dev/power` ([S.11](sec_ai_s#s11)).

## libnova — the POSIX layer, and the only place vendor values meet ABI values.

- LB1.17 — **libnova provides the POSIX headers a program needs, one function per source file**, as `clib` and glibc do, because the linker pulls whole objects: a program that never lists a directory never carries `opendir`'s buffer pool.

| Header | Contents | First needed |
|---|---|---|
| `<nova/sys.h>` | The raw layer, generated | [CON-01](sec_ai_cn1#con-01) |
| `<unistd.h>` | `read` `write` `close` `lseek` `dup` `dup2` `unlink` `rmdir` `chdir` `getcwd` `sync` `getpid` `isatty` `sleep` `usleep` `_exit`; `ssize_t` | [CON-01](sec_ai_cn1#con-01)–[CON-04](sec_ai_cn1#con-04) |
| `<fcntl.h>` | `open` and `O_*` — Calypsi's header if it ships one ([LB1.18](sec_ai_lb1#lb118)) | [CON-02](sec_ai_cn1#con-02) |
| `<sys/stat.h>` | `stat` `fstat` `mkdir`; `struct stat` with `nv_stat`'s layout and POSIX field names | [CON-02](sec_ai_cn1#con-02) |
| `<dirent.h>` | `opendir` `readdir` `closedir`; `struct dirent` with `nv_dirent`'s layout | [CON-02](sec_ai_cn1#con-02) |
| `<sys/ioctl.h>` · `<nova/tty.h>` | `ioctl` · `TTY_*` and the mode and size structures | [CON-01](sec_ai_cn1#con-01) |
| `<nova/sysinfo.h>` | `nv_sysinfo()`, a pointer to the system information block | [CON-01](sec_ai_cn1#con-01) |
| `<nova/proc.h>` · `<sys/wait.h>` | `spawn` · `wait` `waitpid` `kill` `yield` | [CON-04](sec_ai_cn1#con-04) |
| `<nova/time.h>` | `nv_uptime()` | [LIB-04](sec_ai_lb1#lib-04) |
| `<nova/mem.h>` | `sbrk` `mmap` `munmap` `mshare` | E-series |

- LB1.18 — **libnova never ships a header with the same name as one of Calypsi's**, because which one the compiler found would then depend on include order and the two would disagree about values. [LIB-00](sec_ai_lb1#lib-00) takes the inventory (→ [Q185](sec_ai_q#q185)): where Calypsi ships a header — `fcntl.h` is likely, since the hooks take `O_*` — libnova uses it and its values; where it ships none, libnova provides one; and a prototype Calypsi's header lacks, such as `open`, is declared in libnova's `<unistd.h>`.
- LB1.19 — **Every POSIX wrapper has one shape, and translation happens in exactly two tables.**
  ```c
  ssize_t write(int fd, const void *buf, size_t n)
  {
      int r = sys_write(fd, buf, n > 32767 ? 32767 : n);   /* POSIX allows a short write */
      return r < 0 ? __nv_fail(r) : r;          /* errno = __nv_errno[-r]; return -1 */
  }
  ```
  NOTE: **`E_*` → `errno`** maps each code to the value of the same name in Calypsi's `errno.h`, except `E_NOFILE` → `ENOSPC`, `E_CORRUPT` → `EIO`, `E_FEATURE` → `ENOTSUP` and `E_NOTMOUNTED` → `ENODEV`, or the nearest name Calypsi has. `strerror` and `perror` are Calypsi's, so **every message string lives in user space**. A tool that must tell `E_CORRUPT` from a plain `E_IO` calls `sys_*` and reads the raw code, which is why `<nova/sys.h>` is public.
  NOTE: **`O_*` → `NV_O_*`** is one function, used by `_Stub_open` and `open`. Nothing else is translated: `struct stat`, `struct dirent` and the `ioctl` numbers are the ABI's own.
- LB1.20 — **A `DIR` is a descriptor and one 262-byte `nv_dirent`, from a static pool of four**, with no `malloc`, because the shell uses none ([CN1.24](sec_ai_cn1#cn124)). `readdir` hands the `DIR`'s own entry to `sys_readdir`, which is [Y1.12](sec_ai_y1#y112)'s rule kept for free: the buffer is always the caller's.
- LB1.21 — **`spawn(path, argv)` creates a process and returns its PID; the child inherits descriptors 0–2** ([D122](sec_ai_q#d122)). `wait(&status)` and `waitpid(pid, &status, 0)` are `sys_wait`. `exit` is Calypsi's — `atexit` handlers, streams flushed, then `_Stub_exit` and `sys_exit` — and `_exit` calls `sys_exit` directly. **There is no `exec` family, because nothing replaces a running process**, and a function by that name would mislead every program ever ported.
- LB1.22 — **Time**: `sleep(s)` is `s` calls of `sys_sleep(1000)`, `usleep` rounds up to milliseconds, `time()` returns 0 until an RTC exists — the same zero every file already carries ([Y1.17](sec_ai_y1#y117)) — and `nv_uptime()` is the monotonic clock. **`nv_sysinfo()` needs no syscall at all**: the system information block is a read-only page mapped identically into every process ([L.11](sec_ai_l#l11)), so the function returns a constant pointer, and the layout it points at is [sheet I](sec_ai_i)'s.
- LB1.23 — **Not provided**: `fork`, the `exec` family, signals beyond `kill`, environment variables, locale, wide characters, threads, sockets and `system()`.

## A program's memory — small data, in the process's own bank `$00`.

- LB1.24 — **Calypsi's small data model runs with DBR = 0, so a native program's data lives in its own virtual bank `$00`** ([D120](sec_ai_q#d120)). Calypsi's Foenix startup module sets DBR to 0 under `__CALYPSI_DATA_MODEL_SMALL__` and to `_NearBaseAddress`'s bank otherwise, and the Atari port says it outright: bank `$00` "is where a small-model program's plain pointers point". It follows from the processor — the stack is always in bank `$00`, and a plain pointer must reach a local. **That corrects [O.3](sec_ai_o#o3) and [N.4](sec_ai_n#n4)**, which put small data in a bank of its own after `.text`. And it fits this machine well: bank `$00` already belongs to each process ([L.10](sec_ai_l#l10)), so **every process gets a private near-data bank** while its code runs from `$02` upward (→ [Q184](sec_ai_q#q184)).

| Range | Contents | Mapping |
|---|---|---|
| `$0000`–`$07FF` | Unmapped | **NULL guard** — a NULL dereference faults instead of corrupting the direct page |
| `$0800`–`$08FF` | Direct page, 256 bytes | Read-write |
| `$0900` upward | Near constants, `.data`, `.bss`, then the heap block | Read-write; `.bss` and the heap demand-zero |
| Below the stack | One unmapped page | **Stack guard** — an overflow faults instead of overwriting the heap |
| `$D000`–`$DFFF` | The stack, 4 KB by default and sized in the header, with the argument block at its top ([LB1.27](sec_ai_lb1#lb127)) | Read-write, **pinned** ([L.11](sec_ai_l#l11)) |
| `$E000`–`$FFFF` | Vectors, trampolines, kernel stack, system information block | Privileged; the information block read-only |

  NOTE: The last row assumes 8 KB until [Q18](sec_ai_q#q18) sizes the privileged pages, which leaves **about 48 KB for the direct page, data, `.bss` and heap together**. Both guard pages cost address space, not memory: an unmapped page has no frame.
- LB1.25 — **Code starts at `$02:0000` and takes as many banks as it needs, with one linker memory per bank**, so no function straddles a boundary — the program counter wraps inside its bank, which is [L.10](sec_ai_l#l10)'s rule for the kernel and the Atari port's for the same reason. Far constants, switch tables and Calypsi's `code` and `libcode` sections go with it, read and execute only. `nova-user.scm`, the one linker file every program shares ([O.4](sec_ai_o#o4)), declares those memories plus one each for the direct page, the near sections with the heap block, and the stack block, and asks for **no data-initialisation table**, since the loader has already placed `.data` and zeroed `.bss`.
- LB1.26 — **A program whose data outgrows bank `$00` uses the large data model** — `--data-model=large`, `-lc-ld` libraries, default pointers of 32 bits, data in banks after its code. libnova's sources support both models, but v1 builds only `-lc-sd`, and the large model's layout is settled when the first program needs it (→ [Q187](sec_ai_q#q187)).

## Startup and exit — what the loader leaves, and what `crt0` does with it.

- LB1.27 — **The loader returns into user mode with the process ready for `crt0`** ([N.6](sec_ai_n#n6)): segments mapped with their permissions, `.data` copied, `.bss` and the heap demand-zero, both guard pages unmapped, the stack mapped and pinned, native mode with 16-bit registers, D and DBR left for `crt0` — and **the argument block at the top of the stack**, where [Q178](sec_ai_q#q178) recommended it, with S just below it.
  ```
  S+1   argc                                  uint16
        argv[0] … argv[argc−1], then 0        __far24 each
        the strings, NUL-terminated
  ```
  NOTE: The block uses the same `__far24` format `spawn` passes in ([LB1.14](sec_ai_lb1#lb114)), so the kernel copies it without knowing the new program's data model, and `crt0` adapts it.
- LB1.28 — **`crt0` is Calypsi's own Foenix startup module minus the section initialisation the loader already did.** It sets D to `_DirectPageStart` and DBR to 0; calls `__initialize_global_streams` for stdio and `__heap_initialize` with the heap block's start and size; compacts `argv` in place from 3-byte far entries to 2-byte near ones, which is safe because a near entry never overtakes the far entry it is read from; places `argc` and `argv` as Calypsi passes a function's first two parameters; and calls `main`, then jumps to `exit` with its result.
- LB1.29 — **Returning from `main` is `exit`**: the `atexit` handlers, the stream flush, then `_Stub_exit` and `sys_exit`. A failed `assert` writes one line to descriptor 2 and exits with status 255, and so does a process ended by a fault, which the shell reports ([CN1.23](sec_ai_cn1#cn123)).

## Building a program — the two builds, the tree, the executable and the SDK.

| | Monolithic build | User-mode build |
|---|---|---|
| POSIX layer and hooks | `nova-posix-lc-sd.a` | The same objects |
| Defines `sys_*` | The kernel | `nova-rt-lc-sd.a`: the generated `COP` stubs |
| Startup | The kernel's; the shell is `sh_main()` | `crt0`, in `nova-rt-lc-sd.a` |
| Data model | The kernel's | Small data, in bank `$00` |
| stdio | Kept out of the image ([CN1.18](sec_ai_cn1#cn118)) | Available |

- LB1.30 — **The table is [D103](sec_ai_q#d103) exactly**: one library, one entry point and `sh_exec.c` differ, and the library that differs is a set of generated stubs that the monolithic build replaces with nothing. **Linking is static only in v1** ([D123](sec_ai_q#d123)), so every program carries its own copy of the objects it uses and [Q21](sec_ai_q#q21)'s shared text waits until memory pressure asks for it.
- LB1.31 — **The code lives in `sw/` in this repository**, beside `kicad/` and `verilog/`, with the kernel to come in `sw/kernel/` ([D124](sec_ai_q#d124)). The existing `C/` directory is a W65C816SXB demonstration for WDC's compiler under a non-commercial licence, and nothing here is built on it.
  ```
  sw/
    abi/                     syscalls.def · nova/abi/*.h — kernel, libnova, host tools, NVM32 libc
    libnova/
      include/               unistd.h dirent.h sys/*.h nova/*.h   (nova/sys.h generated)
      src/posix/             one function per file: write.c open.c opendir.c …
      src/stubs/             _Stub_open.c … _Stub_assert.c
      src/errno.c            the E_* → errno table and the O_* translation
      src/rt/                nova-startup.s                       (user build only)
      gen/                   sys_cop.s                            (generated, user build only)
      linker-files/          nova-user.scm
      test/host/  test/emu/  LIB-02's unit tests · LIB-04's programs
      Makefile               nova-posix-lc-sd.a · nova-rt-lc-sd.a · install
    tools/
      gensys.py              syscalls.def → headers, stubs, dispatch table; --check
      elf2nova               Calypsi's ELF → the native executable
  ```
- LB1.32 — **`ln65816` produces ELF, and `elf2nova` converts it into the native executable of [N.5](sec_ai_n#n5)** — for programs and for the kernel image alike, since both use the one format ([D107](sec_ai_q#d107)). What it needs of the header [Q20](sec_ai_q#q20) will define: a magic value and a format version, the entry point, per segment the virtual address, file size, memory size and R/W/X bits, and the stack size. The heap is an ordinary segment with no file bytes.
  NOTE: **Freeze the native header knowing NVX exists** ([N.5](sec_ai_n#n5), [VM1.41](sec_ai_vm1#vm141)), so the two share their shape.
- LB1.33 — **`make install` puts the SDK in `/opt/nova64/sdk`** — `include/`, `lib/`, `linker-files/nova-user.scm`, `bin/elf2nova` and `nova.mk` — **and a program needs a three-line Makefile.** The result goes into `/bin` on the card through the host tool of [CN1.25](sec_ai_cn1#cn125), where the shell finds it ([CN1.23](sec_ai_cn1#cn123)).
  ```make
  PROGRAM = hello
  OBJS    = hello.o
  include $(NOVA_SDK)/nova.mk
  ```
  ```sh
  # what nova.mk runs
  cc65816 --core=65816 --code-model=large --data-model=small \
          -I$NOVA_SDK/include -O2 -o hello.o hello.c
  ln65816 --rtattr cstartup=nova --rtattr stubs=nova --hosted -o hello.elf \
          hello.o $NOVA_SDK/lib/nova-rt-lc-sd.a $NOVA_SDK/lib/nova-posix-lc-sd.a \
          clib-lc-sd.a $NOVA_SDK/linker-files/nova-user.scm
  elf2nova hello.elf hello
  ```
- LB1.34 — **Calypsi publishes Linux packages for x86-64 only, and the devbox of [sheet V7](sc_v7) is ARM64, so the toolchain runs under `qemu-user`** ([D126](sec_ai_q#d126)): wrapper scripts named `cc65816`, `as65816`, `ln65816` and `nlib` call `qemu-x86_64` with an x86-64 runtime root, so the Makefiles see native tools, and explicit invocation needs nothing from the host kernel, which registering binfmt would. If that proves unreliable, an x86-64 container through Docker's platform emulation; as a last resort, Codespaces. Whichever works goes into `.devcontainer/Dockerfile` (→ [Q188](sec_ai_q#q188)).
- LB1.35 — **libnova, `crt0`, the ABI headers and the generator are GPLv3 with a linking exception**, in the form eCos and GNU Guile use ([D125](sec_ai_q#d125)). Linking them into a program does not by itself bring the program under the GPL, and modified versions of the runtime itself stay GPLv3. Without it every program — each links the runtime statically — would have to be GPLv3; with it, [A2](sc_a2)'s principle holds for the runtime's own code and stops at the program boundary.
  NOTE: **`clib` is under Calypsi's licence and every program contains part of it**, the same status as the compiler that produced the program. It also means the masthead's "100% open toolchain" does not hold for the 65816 C path while Calypsi is the compiler ([O.2](sec_ai_o#o2)), and [LB1.9](sec_ai_lb1#lb19)'s PDCLib fallback is also the path to an open ISO layer.

## Verification — the host first, then the emulator, then the board.

- LB1.36 — **Four kinds of check.** Host unit tests for every pure-C part of libnova, in plain C99 with fixed-width types, over a test double of `sys_*` that returns chosen `E_*` codes so the error paths run without a kernel. Emulator programs that print a transcript on descriptor 1, captured through [EM3](sec_ai_em3)'s console capture and compared with a stored file. `gensys.py --check` on the stubs, and a dispatcher test that calls every number, the reserved ones included. And **an ABI conformance test** that compiles a call to each `sys_*` under Calypsi and checks the argument placement against [LIB-00](sec_ai_lb1#lib-00)'s audit — which is where a future Calypsi release that changes its convention shows up first.

## Series · LIB-00–LIB-05 — each gate that runs a program passes in the emulator first and on hardware second.

- [ ] LIB-00 — **The toolchain, installed and audited.** Calypsi runs in the devcontainer ([LB1.34](sec_ai_lb1#lb134)), and the audit is written as the ABI half of [E0.8](sec_ai_p3#e08), against compiled output rather than the manual.
  TEST: argument registers — Y free or not ([D115](sec_ai_q#d115)) · the direct-page parameter area, stack-argument order and who removes them · 32-bit returns in X:A · DBR = 0 under small data ([Q184](sec_ai_q#q184)) · the hook convention, the header inventory and the library names ([Q185](sec_ai_q#q185)).
- [ ] LIB-01 — **The ABI headers and the generator.** `gensys.py` produces `syscall.h`, `sys.h`, `sys_cop.s` and the dispatch-table skeleton from `syscalls.def`.
  TEST: the ABI headers compile under host GCC and Calypsi with every size assertion passing on both · `gensys.py --check` passes.
- [ ] LIB-02 — **libnova on the host.** The POSIX layer and the translation tables built with host GCC against the test double of `sys_*`.
  TEST: `errno` and flag translation · the 32,767 clamp and the hooks' loop · `DIR` pool exhaustion · `spawn`'s vector packing and `E_2BIG`.
- [ ] LIB-03 — **The monolithic link.** The monolithic console links `nova-posix-lc-sd.a` against the kernel's `sys_*`. **Coincides with [CON-02](sec_ai_cn1#con-02).**
  TEST: CON-02's golden screens pass with the shell's calls going through libnova · the resident size is measured and recorded.
- [ ] LIB-04 — **User programs.** `crt0`, the stubs, `nova-user.scm` and `elf2nova`. **Coincides with [CON-04](sec_ai_cn1#con-04)** and so with [P5.k](sec_ai_p2#p5k).
  TEST: `hello` prints through `printf` · `readwrite` round-trips a file through `fopen`, `fwrite`, `fseek` and `fread` · `args` sees its `argv` and the shell reports its exit status · `nullguard` writes through NULL, dies, and the shell survives.
- [ ] LIB-05 — **The SDK.** `make install` populates `/opt/nova64/sdk`.
  TEST: a program outside the tree builds from [LB1.33](sec_ai_lb1#lb133)'s three-line Makefile and runs from `/bin` — in the emulator, then on hardware at [P5.k](sec_ai_p2#p5k).
