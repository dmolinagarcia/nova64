# Console mode — boot to a prompt
> the boot chain · the tty · the shell · one source, built twice

The first usable state of noVa64 is a console. The machine powers on, boots from its own card, and shows a prompt on its own display that browses and changes files. Every piece it needs is specified somewhere else — the EC boot of [sheet D1](sec_ai_d1), Neon's Mode 0 ([T1.53](sec_ai_t1#t153)), the VFS of [sheet Y1](sec_ai_y1), NVFS in [sheet Y2](sec_ai_y2), the syscall contract of [J.3](sec_ai_j#j3) — and this sheet is how they fit together: how the kernel image reaches memory, the order the kernel comes up in, the console device, and the shell. It is the software half of the text-mode checkpoint at [P2.h](sec_ai_p2#p2h)–[P2.l](sec_ai_p2#p2l) and of [P5.k](sec_ai_p2#p5k). Graphics, pipes, job control, environment variables, globbing, scripting and NVM32 are outside it; everything here is native 65816 code compiled with Calypsi.

- CN1.1 — **The console is built twice, and the second build is a relink of the first rather than a rewrite.** The first is one monolithic image with no MMU and no processes, which is the checkpoint of [P2.h](sec_ai_p2#p2h)–[P2.l](sec_ai_p2#p2l) and fits the rig of [sheet P1](sec_ai_p1). The second is the kernel with the shell running as its first user process, which is [P5.k](sec_ai_p2#p5k). **The shell calls only the libc surface**: the first build links that surface to kernel functions, the second to the `COP` stubs of [O.7](sec_ai_o#o7), and the two differ in one library, one entry point and one source file ([D103](sec_ai_q#d103)).
  NOTE: Consolidated from DN-SW-CONSOLE-001 Rev A, whose decisions are proposals for review rather than frozen. Its gates CN0–CN4 are the series [CON-00](sec_ai_cn1#con-00)–[CON-04](sec_ai_cn1#con-04) here, named as [A3.11](sc_a3#a311) requires.
  NOTE: **What the libc surface is, and why the monolithic build needs no library for it, is [sheet LB1](sec_ai_lb1).** The shell's POSIX calls land in libnova, whose objects are the same in both builds, and libnova calls `sys_*` — which the monolithic build resolves to the kernel's own handlers and the user-mode build to generated `COP` stubs ([D113](sec_ai_q#d113)). The "one library" that differs is those stubs, and in the monolithic build it is nothing at all.
- CN1.2 — **The shell never touches the card, Neon or the keyboard.** For the shell, screen, keyboard and files are all file descriptors, and everything below the libc surface is the kernel's.

## Where it sits — only the link between the libc surface and the kernel differs between the two builds.

| Layer | Contents | |
|---|---|---|
| Shell | `sh_main.c` · `sh_parse.c` · `sh_builtin.c` · `sh_exec.c` (second build only) | User side |
| libc surface | `open` · `read` · `write` · `close` · `readdir` · `chdir` … | |
| The link | Monolithic: a direct call · User-mode: a `COP` stub | **The only difference** |
| Syscall layer | `sys_open` · `sys_read` · `sys_write` … | Kernel |
| Descriptors | fd table → `file_t` of kind `VNODE` or `DEVICE` ([CN1.15](sec_ai_cn1#cn115)) | |
| tty and files | Line discipline, the Mode 0 output backend and the HID input backend · beside them the VFS, NVFS, the block cache and the SD driver with its MBR scan | |
| Hardware | Neon's text cells · Helium's HID queue · Helium's SD block | |

## The boot chain — four stages, and the EC never touches the card.

| Stage | Runs on | Reads from | Leaves behind |
|---|---|---|---|
| EC | RP2354B | Its own flash | FPGAs configured · `bios.bin` in memory · CPU out of reset |
| BIOS | 65816 | Memory · the card's boot partition | System information block written · Mode 0 banner · kernel loaded · jump to its entry |
| Kernel init | 65816 | The card's NVFS partition | Drivers up · root mounted · descriptors 0–2 on the console |
| Shell | 65816 | — | A prompt |

- CN1.3 — **The EC stage is unchanged.** Rails in order, FPGAs configured, `bios.bin` written into memory by Debug Agent `WRITE_BURST` frames on endpoint `0x02`, CPU released from reset ([D1.22](sec_ai_d1#d122), [D82](sec_ai_q#d82)). **The EC never touches the card**, which is Helium's alone ([D60](sec_ai_q#d60), [G.1](sec_ai_g#g1)). On the prototypes the EC's part goes to whatever is practical — the Pico on the rig of [sheet P1](sec_ai_p1), or the PC over the debug path — and in the emulator the machine starts in the post-reset state with `bios.bin` loaded from a file. Nothing below depends on which.
- CN1.4 — **The BIOS stage, in order.** (1) Native mode, stack, direct page. (2) Write the system information block. (3) Print a one-line banner on Mode 0, which needs no initialisation ([T1.54](sec_ai_t1#t154)); from here on a failure is visible. (4) Check the staging area for a pre-loaded kernel ([CN1.6](sec_ai_cn1#cn16)) and skip to step 8 if one is valid. (5) Initialise the card through Helium. (6) Read LBA 0, parse the MBR and find the boot partition by its type code. (7) Read the image header, load its segments, verify the CRC. (8) Jump to the kernel's entry, passing nothing, since the information block is at a fixed address.
  NOTE: **On any failure the BIOS prints a message and halts. It never reboots in a loop**, because a reboot loop erases the message.
  NOTE: **The system information block gains the text geometry, columns and rows**, and the kernel reads it from there and never from constants ([I.4](sec_ai_i#i4), [Q168](sec_ai_q#q168)). [T1.53](sec_ai_t1#t153) and [D44](sec_ai_q#d44) fix Mode 0 at 128 × 32 while the emulator models 128 × 75 ([EM4.9](sec_ai_em4#em49)); a console that reads its geometry works on both.
- CN1.5 — **The kernel image lives in a boot partition of its own, in the same executable format as user programs** ([D107](sec_ai_q#d107)). The BIOS then needs a block read, an MBR parse and a loader, and nothing else: the loader is the kernel's own program loader of [N.6](sec_ai_n#n6) — the one `spawn` uses — compiled a second time, and the MBR scan is shared with the kernel ([CN1.16](sec_ai_cn1#cn116)), so the BIOS gains no code that would not exist anyway.
  NOTE: **This supersedes the BIOS mounting NVFS to find `KERNEL.BIN`** ([I.4](sec_ai_i#i4), [G.3](sec_ai_g#g3)). A second NVFS reader in the BIOS would have to follow every format change of [sheet Y2](sec_ai_y2), and keeping the kernel in EC flash instead would tie kernel iteration to the EC update path. **Move to the NVFS file if NVFS gains a boot area of its own**, or if a separate partition proves awkward to update from a host. The eight reserved blocks at the head of the NVFS partition hold 16 KB, which is not a boot area for a kernel, and what they are for is still [Q130](sec_ai_q#q130).
  NOTE: The boot partition's type byte is not assigned yet, and it cannot be `0x7F`, which is NVFS's ([Y2.25](sec_ai_y2#y225), → [Q177](sec_ai_q#q177)).
- CN1.6 — **A bring-up bypass skips the card entirely.** The Debug Agent can write memory while the CPU is in reset, so during bring-up the host writes the kernel image straight into the BIOS's staging area, and step 4 of [CN1.4](sec_ai_cn1#cn14) finds a valid header — magic and CRC — and never touches the card. That separates "the kernel works" from "the card path works" and gives a fast loop before a card layout exists.
  NOTE: **The BIOS clears the staging header's magic once it has used the image.** Otherwise a stale image would survive a warm reset and boot in place of the card's.

## Kernel initialisation — the same order in both builds.

| # | Step | On failure |
|---|---|---|
| 1 | crt0: native mode, direct page, stack, `.bss` cleared, `.data` initialised | — |
| 2 | `con_init`: geometry from the information block, screen cleared, banner. `kprintf` and `panic` work from here | Nothing visible; the Debug Agent is the only witness |
| 3 | `clock_init`: the fixed-frequency counter every timeout uses ([L.15](sec_ai_l#l15)) | `panic` |
| 4 | `kbd_init`: drain the HID queue, load the keymap | An output-only console, with a "no keyboard" message |
| 5 | `blk_init`: the card | Degraded mode |
| 6 | `vfs_init`, the MBR scan, NVFS mounted at `/` — read-only until [CON-03](sec_ai_cn1#con-03) | Degraded mode |
| 7 | `tty_init`; descriptors 0, 1 and 2 → `/dev/tty` | `panic` |
| 8 | Monolithic: call `sh_main()`, and again if it returns. User-mode: spawn `/bin/sh` as PID 1 and respawn it when it exits; if it is missing, `panic` with the reason | — |

- CN1.7 — **Without a root filesystem the console still comes up, in degraded mode** ([D109](sec_ai_q#d109)). The prompt shows `(no root)`, the built-ins that need no files work — `help`, `echo`, `clear`, `sysinfo` — and the rest answer "no root file system". It separates "the console works" from "the card and NVFS work" during bring-up.
- CN1.8 — **A panic prints on Neon, disables interrupts and halts, and the post-mortem goes through the Debug Agent**, which does not depend on the CPU. The kernel carries no debugger of its own. **The monolithic build needs no interrupts at all**: input is polled, and time comes from the fixed-frequency counter.

## The console device — a line discipline between two backends.

- CN1.9 — **A tty is a line discipline, an output backend and an input backend.** The console's are Mode 0 on Neon for output, and Helium's HID queue with the kernel's keymap for input. Where a CPU-visible UART exists ([P2.g](sec_ai_p2#p2g)), a serial backend can offer `/dev/ttyS0` with the same discipline, but the console needs nothing except `/dev/tty`.
  NOTE: The output backend is [T1.55](sec_ai_t1#t155)'s kernel console — the same stores to the same addresses as the BIOS, behind [J.4](sec_ai_j#j4)'s five-function interface. **[J.4](sec_ai_j#j4)'s scrollback requirement lives here too**: the backend keeps its own copy of the screen in system memory and repaints from it, because a graphics mode may reclaim the text buffer's block RAM. The console never switches modes, so nothing exercises it before the G-series, but it belongs to this backend and is not something to retrofit ([Q45](sec_ai_q#q45)).
- CN1.10 — **Neon is a cell engine with a cursor and a scroll, and the control characters are the driver's** ([D105](sec_ai_q#d105)). Tab stops, erase behaviour and a future ANSI subset are software policy; in gateware each change would cost a bitstream. The driver keeps the authoritative cursor, row and column, in software, and output wraps at the last column.
  NOTE: **The printable path must stay short.** The emulator measured 17 cycles per character through text mode and nothing at all for a scroll ([EM6.3](sec_ai_em6#em63)), so the driver's own per-character overhead will dominate. No output buffering beyond the call is needed. Revisit the division only if a measurement shows that overhead dominating in real use, and even then move only the `\n` fast path into Neon.

| Character | Action |
|---|---|
| `\n` | Column 0 of the next row; scroll at the bottom row |
| `\r` | Column 0 |
| `\b` | Back one column without erasing; at column 0, the last column of the previous row |
| `\t` | The next multiple of 8 |
| BEL | Ignored |
| ESC | Reserved for the ANSI subset full-screen programs will need |
| Any other below `0x20` | Ignored |

- CN1.11 — **The EC sends raw key events and the kernel owns the keymap** ([D106](sec_ai_q#d106)). Each event from Helium's HID queue carries the device, the HID usage (page `0x07`), the modifier bitmap, press or release, and a repeat flag. The console drops mouse events and key releases, which it can afford because every event carries the modifier state. The keymap is three tables — base, Shift, AltGr — with a small state machine for dead keys, Caps Lock tracked in the kernel, and bytes out in the console encoding (→ [Q182](sec_ai_q#q182)). Enter gives `\n`; Backspace `0x08` and Delete `0x7F`, both erasing in canonical mode; Ctrl with a letter `0x01`–`0x1A`. Arrow and function keys give nothing until the ANSI subset exists.
  NOTE: **The EC generates typematic repeat.** USB HID keyboards do not repeat on their own, the EC already owns the HID stack and a timer, and the GUI will need the same raw events on the same endpoint ([V.30](sec_ai_v#v30)). Delay and rate become EC parameters, set later through the `SYS` endpoint.
  NOTE: The first keymap written is the keyboard's own, expected to be Spanish ISO. **The Caps Lock LED needs a path from the CPU back to the EC** and is not part of the console yet.
- CN1.12 — **Line editing lives in the kernel's tty, not in the shell** ([D104](sec_ai_q#d104)). Every program then receives complete lines for nothing, and the shell stays a parser. The line buffer holds 255 bytes and a terminator; printable characters that arrive when it is full are dropped. `read(fd, buf, n)` returns at most `n` bytes, so one line can be consumed over several reads.
  NOTE: **Raw mode** — no echo, no editing, every byte available at once — is set by `ioctl` and defined now, although nothing in the console uses it yet.
  NOTE: `tty_poll()` moves pending hardware events into the discipline and spots the interrupt character; `read()` calls it, and so do long-running loops in the monolithic build. **In the user-mode build `read()` blocks and the process sleeps**, which needs an interrupt from the HID queue (→ [Q180](sec_ai_q#q180)). Polling is valid only while the system is single-threaded, and it must be gone before [CON-04](sec_ai_cn1#con-04).

| Canonical input | Effect |
|---|---|
| Printable | Appended if there is room, and echoed |
| BS or DEL | The last character removed; `\b \b` echoed |
| Ctrl-U | The line discarded and erased on screen |
| Enter | `\n` appended; the line becomes available to `read()` |
| Ctrl-C | The line discarded, `^C\n` echoed, the interrupt raised ([CN1.13](sec_ai_cn1#cn113)) |
| Ctrl-D on an empty line | `read()` returns 0 — end of file |

- CN1.13 — **Ctrl-C is a flag in the first build and a signal in the second.** Monolithic: it sets `tty_intr`, and long-running built-ins — `cat`, `ls`, `cp` — poll between blocks and stop. User-mode: it sends `kill(fg, TERM)` to the foreground process, and the shell makes itself foreground again once `wait` returns.

| `ioctl` | Purpose |
|---|---|
| `TTY_GETMODE` · `TTY_SETMODE` | Canonical or raw |
| `TTY_GETSIZE` | Columns and rows |
| `TTY_CLEAR` | Clear the screen and home the cursor — how `clear` works while there are no escape sequences |
| `TTY_SETFG` | Set the foreground PID, in the user-mode build |

## Files — the VFS path, and the two things this sheet adds to it.

- CN1.14 — **`open` reaches the device table or the VFS, and below the VFS it is [sheet Y1](sec_ai_y1) unchanged**: NVFS, the block cache, the SD driver, Helium. Block size is a mount property; NVFS's 2048 bytes are four sectors per request, which is why the SD block must transfer several sectors at once ([G.7](sec_ai_g#g7), [Y2.24](sec_ai_y2#y224)).
- CN1.15 — **`open` resolves `/dev/<name>` against the device table before it reaches the VFS** ([D108](sec_ai_q#d108)). `file_t` gains a kind: `VNODE` sends `read`, `write` and `close` to `vnode_ops`, `DEVICE` sends them to the driver's five functions. `/dev` does not exist on the card, so `ls /dev` fails, and that is accepted; only `tty` is registered, and the raw card is not exposed. **Replace it with a devfs mount when something needs to list devices** — `ls /dev`, or a device that comes and goes — which is when `vnode_ops` needs the `open` and `close` slots [Y1.10](sec_ai_y1#y110) leaves out (→ [Q95](sec_ai_q#q95)).
  NOTE: **`/dev/tty` is the console and the UART is `/dev/ttyS0`.** That reverses the names in the table of [sheet H](sec_ai_h), which called the screen console `/dev/con` and the serial line `/dev/tty`, and agrees with [P5.i](sec_ai_p2#p5i). Raw `/dev/kbd` and `/dev/mouse` are left to the G-series, with one rule settled now: **only one consumer owns the HID queue at a time.**
- CN1.16 — **The kernel runs the BIOS's MBR scan and mounts the first NVFS partition at `/`**, read-only up to [CON-02](sec_ai_cn1#con-02) and read-write from [CON-03](sec_ai_cn1#con-03) ([D110](sec_ai_q#d110)). A read-only mount of a dirty volume or a non-empty journal must refuse, as [Y2.20](sec_ai_y2#y220) requires of any read-only mounter. The ext2 partition stays unmounted until [E2.1](sec_ai_y3#e21).
  NOTE: The source note assumed NVFS lives inside a partition and offered to add the offset in the block layer if not. [Y2.25](sec_ai_y2#y225) already settles it — a primary partition of type `0x7F` starting on a multiple of four — so nothing is added.
- CN1.17 — **The current directory is a global in the first build and per process in the second.** Monolithic: one vnode reference and one path string. User-mode: the same pair in the PCB, as [Y1.20](sec_ai_y1#y120) recommends. `chdir` updates both, and `getcwd` is a copy.

## The shell — a parser over file descriptors.

| File | Contents | Builds |
|---|---|---|
| `sh_main.c` | The loop, the prompt, dispatch | Both |
| `sh_parse.c` | Tokeniser and redirection | Both |
| `sh_builtin.c` | Built-in commands | Both |
| `sh_exec.c` | Launching external commands | User-mode only; the monolithic build links a stub answering "not found" |

- CN1.18 — **One project header declares the libc surface, and the three shared files carry no `#ifdef`.** The shell prints through `write()` and small helpers such as `sh_puts` and `sh_putu`, not through stdio, which keeps the monolithic image small; stdio can come later if the code budget allows.
  NOTE: **Abandon the single source if keeping it needs `#ifdef` in the shared files** beyond the libc backend, the entry point and `sh_exec.c`. The shell then forks at [CON-04](sec_ai_cn1#con-04), and the monolithic copy is frozen.
  NOTE: **That header is libnova's**: `<unistd.h>`, `<fcntl.h>`, `<dirent.h>`, `<sys/stat.h>`, `<sys/ioctl.h>` and `<nova/tty.h>` ([LB1.17](sec_ai_lb1#lb117)). Directories come from libnova's static pool of four, so the shell still needs no `malloc` ([LB1.20](sec_ai_lb1#lb120)).
- CN1.19 — **The main loop reads a line, parses it in place and runs it.** Illustrative, not normative; error returns follow [Y1.15](sec_ai_y1#y115)'s error space.

```c
void sh_main(void)
{
    static char     line[SH_LINE_MAX];          /* 256 */
    static char    *argv[SH_ARGV_MAX + 1];      /* 16 + NULL */
    static sh_redir redir;

    sh_motd();                                  /* prints /etc/motd if present */
    for (;;) {
        sh_prompt();                            /* getcwd() + "> " */
        int n = read(0, line, sizeof line - 1);
        if (n <= 0) continue;                   /* the login shell ignores EOF */
        line[n] = '\0';
        int argc = sh_parse(line, argv, &redir);
        if (argc > 0)
            sh_run(argc, argv, &redir);         /* a built-in, else sh_exec() */
    }
}
```

- CN1.20 — **Parsing splits on spaces and tabs and cuts tokens in place**, with no copies. `"..."` groups characters into one token and `\` escapes the next one; `> file` and `>> file` redirect output, and only the last redirection counts. **A line over 255 characters or more than 16 arguments is an error, never a silent truncation.** Pipes, `;`, `&`, `<`, variables and globbing are not there.
- CN1.21 — **A built-in is `int bi_name(int argc, char **argv, int out)`**, with `out` the output descriptor after redirection. It returns 0 or an error code, and the shell prints errors as `name: arg: message` from a short table of its own — **the kernel carries no message strings.**

| Command | Gate | Notes |
|---|---|---|
| `help` · `echo` · `clear` · `sysinfo` | [CON-01](sec_ai_cn1#con-01) | `clear` is `TTY_CLEAR`; `sysinfo` prints the information block — CPU, platform, register map version, text geometry |
| `pwd` · `cd [dir]` · `ls [-l] [path]` · `cat file…` · `stat path` | [CON-02](sec_ai_cn1#con-02) | `cd` defaults to `/` and is a built-in in both builds, because it changes the shell's own directory. `ls` marks directories with `/`, and `-l` adds sizes |
| `mkdir` · `rmdir` · `rm` · `mv` · `cp` · `sync` | [CON-03](sec_ai_cn1#con-03) | `mv` is `rename` and fails across mounts; `cp` loops with a 2 KB buffer, one NVFS block |
| `exit` | [CON-04](sec_ai_cn1#con-04) | User-mode only; the kernel respawns PID 1 |

  NOTE: In the user-mode build any built-in except `cd`, `exit` and `help` can move to `/bin` without changing the shell.
- CN1.22 — **`>` and `>>` work for built-ins in both builds**: the shell opens the target and passes its descriptor as `out`. In the user-mode build they reach external commands too, through `dup2` around the spawn. There is no input redirection and there are no pipes, which need processes and `pipe()`; they are reconsidered after [CON-04](sec_ai_cn1#con-04).
- CN1.23 — **An external command, in the user-mode build only**: look in the built-in table; use `argv[0]` as a path if it contains `/`, otherwise look in `/bin`, the only search path while there are no environment variables; `pid = spawn(path, argv)`, which creates the process and returns its PID ([D122](sec_ai_q#d122)); `ioctl(0, TTY_SETFG, pid)`, `wait(pid)`, and the shell back in the foreground; a non-zero status reported.
- CN1.24 — **Every buffer is static, and there is no `malloc`.** The directory-entry buffer is the shell's, because [Y1.12](sec_ai_y1#y112) requires the 262-byte entry to be caller-provided and never a stack local inside the VFS. **In the monolithic build the shell runs on the kernel stack**, so it keeps no large locals, and path buffers take the VFS header's maximum path length.

```
noVa64 BIOS
kernel: loaded from sd0p1
noVa64 kernel (monolithic)
root: NVFS on sd0p2, read-only
Welcome to noVa64.
/> ls
bin/  etc/
/> cd etc
/etc> cat motd
Welcome to noVa64.
/etc> cd /nope
cd: /nope: not found
/etc>
```

## The card — one MBR, three partitions.

| # | Type | Contents |
|---|---|---|
| 1 | noVa64 boot, type byte unassigned (→ [Q177](sec_ai_q#q177)) | The kernel image, in the executable format |
| 2 | NVFS, `0x7F` ([Y2.25](sec_ai_y2#y225)) | The root filesystem |
| 3, optional | Linux `0x83`, ext2 | Exchange with a host, mounted read-only once [E2.1](sec_ai_y3#e21) exists |

- CN1.25 — **The root tree starts as `/bin` — empty in the monolithic build — and `/etc/motd`**, with `/dev` synthetic ([CN1.15](sec_ai_cn1#cn115)). An MBR rather than GPT, since GPT would add parsing to the BIOS for no benefit at these card sizes. Card images are built on the host: NVFS content with the existing `nv` tool, the boot partition with a host tool — an `nv` subcommand or one of its own.

## The two builds — and the slice of the syscall table the console needs.

| | Monolithic build | User-mode build |
|---|---|---|
| libc backend | Direct calls to `sys_*` | `COP` stubs |
| Shell entry | `sh_main()`, called by the kernel | User `crt0` → `main` |
| `sh_exec.c` | A stub | Real |
| Waiting for input | Polling | A blocking `read`, woken by the HID interrupt |
| Current directory | Global | Per process |
| MMU | Off | On |
| Ctrl-C | A flag | `TERM` to the foreground process |
| Platforms | Emulator · the rig of [sheet P1](sec_ai_p1) · [sheet P2](sec_ai_p2) | Emulator · [sheet P2](sec_ai_p2) onward |

- CN1.26 — **Everything except `sh_exec.c` is identical in both builds**: the shell, the tty, the keymap, the VFS, NVFS, the block cache and the SD driver. **Drivers implement [J.4](sec_ai_j#j4)'s five functions from the monolithic build onward**, even where `irq` is unused, so that [P5.i](sec_ai_p2#p5i)'s driver framework is integration rather than a rewrite ([D111](sec_ai_q#d111)). The rig's scope — Mode 0, the card, a HID keyboard on the Pico, the Debug Agent — covers everything the monolithic build needs.
  NOTE: **Measure the resident kernel at [CON-02](sec_ai_cn1#con-02)**, since the monolithic build gives the real number first. If it outgrows one bank, the kernel's code spans the banks below `$FD` ([D102](sec_ai_q#d102)) — a matter of layout, not correctness, because the large model already calls with `JSL`.

| Call | Used by | First needed |
|---|---|---|
| `read` · `write` | The tty, every built-in | [CON-01](sec_ai_cn1#con-01) |
| `ioctl` | `clear`, `sysinfo`, foreground control | [CON-01](sec_ai_cn1#con-01) |
| `open` · `close` | `cat`, redirection | [CON-02](sec_ai_cn1#con-02) |
| `opendir` · `readdir` · `closedir` | `ls` | [CON-02](sec_ai_cn1#con-02) |
| `stat` | `ls -l`, `stat` | [CON-02](sec_ai_cn1#con-02) |
| `chdir` · `getcwd` | `cd`, `pwd`, the prompt | [CON-02](sec_ai_cn1#con-02) |
| `mkdir` · `rmdir` · `unlink` · `rename` · `sync` | The [CON-03](sec_ai_cn1#con-03) built-ins | [CON-03](sec_ai_cn1#con-03) |
| `spawn` · `wait` · `exit` | External commands | [CON-04](sec_ai_cn1#con-04) |
| `dup` · `dup2` | Redirection of external commands | [CON-04](sec_ai_cn1#con-04) |

  NOTE: **[J](sec_ai_j)'s v1 dispatch table lacked eight of these, and now has them** ([D114](sec_ai_q#d114)): [LB1.10](sec_ai_lb1#lb110) numbers every call in this table, with `closedir` implemented as `close` because a directory descriptor is a `file_t` like any other, and `exec(path)` became `spawn(path, argv)` ([D122](sec_ai_q#d122)). How the service number reaches the kernel still does not matter here, because the stubs hide it; since [D115](sec_ai_q#d115) it is Y rather than the accumulator.

## What this sheet requires of others.

- CN1.27 — **Of Neon's Mode 0**: write a glyph at the cursor and advance; set the cursor; scroll up one row with the new row cleared; clear the screen; a visible cursor; the geometry published through the information block; and no register with a side effect on read ([M.15](sec_ai_m#m15)). Control characters are **not** interpreted in gateware ([D105](sec_ai_q#d105)), an automatic scroll past the last row is allowed either way, and the command port stays two bytes wide. **The glyph store must hold every character the keymap can produce** (→ [Q179](sec_ai_q#q179), [Q182](sec_ai_q#q182)).
- CN1.28 — **Of Helium's HID interface**: a queue fed by endpoint `0x03`, each entry carrying device, HID usage, modifier bitmap, press or release and the repeat flag; **an explicit write pops an entry, never a read**; a count or not-empty status and a sticky overflow flag; and an interrupt line to the CPU for the user-mode build (→ [Q180](sec_ai_q#q180), [Q4](sec_ai_q#q4)).
- CN1.29 — **Of Helium's SD block**: multi-sector transfers into memory with a completion status and an error code, which [G.7](sec_ai_g#g7)'s DMA path and [G.10](sec_ai_g#g10)'s completion already give; a completion interrupt, optional in the monolithic build and required in the user-mode one; and **no data port with a side effect on read**, which [G.7](sec_ai_g#g7)'s PIO FIFO has (→ [Q181](sec_ai_q#q181)).
- CN1.30 — **All console code follows [D92](sec_ai_q#d92)'s four requirements**: no cycle counting for time, no self-modifying code, I/O registers idempotent on read, and a register map versioned through the information block. The third is why [CN1.28](sec_ai_cn1#cn128) and [CN1.29](sec_ai_cn1#cn129) forbid pop-on-read.

## Verification — the emulator first, then the hardware, gate by gate.

- CN1.31 — **Host unit tests in plain C99** for the tokeniser, the line discipline — synthetic key events in, a byte stream and screen operations out — and the keymap tables, with fixed-width types throughout; the emulator build catches whatever Calypsi's 16-bit `int` still changes.
- CN1.32 — **Golden screens in the emulator.** Keystrokes are scripted through the emulated HID queue, the text buffer is dumped after each command and compared with a stored screen, and the card image is built from a directory tree by the host tools.
- CN1.33 — **The NVFS inside the kernel passes the existing conformance and crash-injection suites** — simplest if the kernel's driver is built from the same source as the reference implementation behind the VFS tables, and otherwise run against the kernel build in the emulator.
- CN1.34 — **On hardware the EC is the HID source**, so it injects the same scripted keystrokes sent from the PC over the debug path. Screens are checked by eye unless Neon's text buffer gains a diagnostic read path (→ [Q179](sec_ai_q#q179)).

## Series · CON-00–CON-04 — each gate passes in the emulator first and on hardware second.

- [ ] CON-00 — **The kernel speaks.** Monolithic. A kernel image loaded through the bypass of [CN1.6](sec_ai_cn1#cn16) prints its banner and a summary of the information block.
  TEST: `panic()` demonstrably prints and halts.
- [ ] CON-01 — **An interactive console with no filesystem.** Monolithic, in degraded mode ([CN1.7](sec_ai_cn1#cn17)).
  TEST: a scripted keystroke test exercises every rule of [CN1.12](sec_ai_cn1#cn112) and every keymap table — Shift, AltGr and dead keys · `help`, `echo`, `clear` and `sysinfo` work.
- [ ] CON-02 — **A read-only prompt.** Monolithic. The BIOS loads the kernel from the boot partition, and NVFS is mounted read-only. **This is the text-mode checkpoint of [P2.h](sec_ai_p2#p2h)–[P2.l](sec_ai_p2#p2l)**: it boots to its own prompt, in text mode, from the card, with no MMU.
  TEST: a golden-screen script over a reference image passes · a failed mount shows degraded mode.
- [ ] CON-03 — **A read-write console.** Monolithic.
  TEST: the [CON-03](sec_ai_cn1#con-03) built-ins and `>`/`>>` pass golden-screen scripts · the NVFS conformance and crash-injection suites pass against the kernel build in the emulator · a card written on hardware verifies with the NVFS host tools.
- [ ] CON-04 — **The shell as a user process.** User-mode. The same shell sources linked against the `COP` stubs, and `/bin/sh` running as PID 1. **This is [P5.k](sec_ai_p2#p5k)'s pass condition.**
  TEST: an external program in `/bin` runs with its `argv` and its exit status is reported · Ctrl-C ends it and returns to the prompt.
