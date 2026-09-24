# DN-SW-CONSOLE-001 — Console Mode: OS Integration and Shell

**Revision A** — draft for review, not frozen
**Project:** noVa64
**Date:** 2026-09-24
**Scope:** How the BIOS, kernel, drivers and file system are assembled into a
text-mode system that boots to a shell prompt, and the design of that shell.

**Depends on:**

- DN-HW-ECIF-001 Rev B, as amended by DN-HW-MBXLINK-001 Rev A — boot sequence,
  `bios.bin` load through the Debug Agent, HID endpoint `0x03`
- DN-FS-VFS-001 Draft 1 — vnode model, path resolution, syscall mapping (§13)
- DN-FS-NVFS-003 — native file system
- NEON GPU specification, Mode 0 text; DN-HW-TEXT-001 (not yet written)
- OS contract handoff (Calypsi toolchain, syscall table, driver model)
- DN-PLAN-PHASES-001 — Phase 0 scope

**Related:** DN-SW-EMU-001 (the emulator is the first target for every gate in
this note), DN-FS-EXT2-001, DN-SW-VMINTERP-001 (NVM32; out of scope here).

**Blocks:** the BIOS kernel-load path; E2.5 checkpoint software; K4–K5 console
and shell.

---

## Revision history

| Rev | Date | Change |
|---|---|---|
| A | 2026-09-24 | Initial draft. All decisions are proposals for review. |

---

## 1. Purpose and scope

The first usable state of noVa64 is a console. The machine is powered on, boots
from its own SD card, and shows a prompt on its own display that accepts
commands to browse and change files. The pieces that produce this state are
already specified separately: EC boot, NEON Mode 0, the VFS, NVFS and the
syscall contract. This note specifies how they fit together and fills the gaps
between them:

- how the kernel image gets from the card into memory;
- the kernel's initialisation order;
- the console device (tty);
- the shell.

The design follows one constraint. **The console is built twice.** The first
build is a single monolithic image with no MMU and no processes. It is the E2.5
checkpoint and fits the Phase 0 testbench. The second build is a kernel with
the shell running as the first user process, which is the K5 pass. The design
makes the second build a relink of the first, not a rewrite.

### In scope

- The boot chain, from the EC releasing CPU reset to the shell prompt.
- Where the kernel image lives on the SD card, and the BIOS load path.
- Kernel initialisation order and degraded modes.
- The tty layer: backends, keymap, line discipline, interrupt character.
- Device-node resolution for `/dev/tty`.
- The shell: main loop, parser, built-ins, redirection, external commands.
- The syscall slice the console needs, and the switch between direct calls and
  COP.
- Requirements this note places on notes not yet written: DN-HW-TEXT-001 and
  the Helium CPU-side HID and SD interfaces.
- Gates CN0–CN4.

### Out of scope

- Graphics modes, the compositor and the GUI (G-series).
- Pipes, job control, environment variables, globbing and scripting.
- NVM32. The shell and all console software are native 65816 code compiled
  with Calypsi. NVM32 has its own reopening condition: the kernel running its
  first user process, which corresponds to gate CN4 here.
- ext2. §9 reserves a partition for it, which is mounted once DN-FS-EXT2-001
  reaches F1.

---

## 2. Decisions

All proposed in this revision.

| # | Decision | Rationale | § |
|---|---|---|---|
| D-1 | The shell calls only the libc surface. The monolithic build links it to kernel functions; the user-mode build links it to COP stubs. | The two builds then differ in one library, one entry point and one source file. | 8.1, 10 |
| D-2 | Line editing (echo, erase, kill line, Enter) lives in the kernel tty layer, not in the shell. | Every program receives complete lines without doing anything, and the shell stays a parser. | 6.4 |
| D-3 | The tty driver interprets control characters. NEON is a cell engine with cursor and scroll. | Tab stops, erase behaviour and a future ANSI subset are software policy. Implemented in gateware, each change would need a bitstream rebuild. | 6.2 |
| D-4 | The EC sends raw key events (HID usage, modifiers, press/release, repeat flag), and the EC generates typematic repeat. Keymap translation happens in the kernel. | The GUI will need raw events on the same endpoint. USB HID keyboards do not repeat keys on their own, and the EC already owns the HID stack and a timer. | 6.3, 12.2 |
| D-5 | The kernel image lives in a dedicated boot partition, in the same executable format as user programs. | The BIOS needs only a block read, an MBR parse and the shared loader, and no file system code. See OI-1. | 4.3 |
| D-6 | `open()` resolves `/dev/<name>` against the kernel device table before the VFS. | This is the smallest mechanism that serves the console. devfs waits until something needs to enumerate devices. See OI-2. | 7.2 |
| D-7 | The console runs in a degraded mode when there is no file system. | Separates "the console works" from "SD and NVFS work" during bring-up. | 5 |
| D-8 | Read-only before read-write. | Mirrors ext2 F0–F1: the first SD writes happen only after reads are proven. | 11 |
| D-9 | Drivers implement the five-function interface (`init`, `read`, `write`, `ioctl`, `irq`) from the monolithic build onward, even where `irq` is unused. | K4 ("driver framework, `/dev/tty`") then becomes integration, not a rewrite. | 10.1 |

---

## 3. Position in the system

```
 +------------------------------------------------------------+
 |  shell   sh_main.c  sh_parse.c  sh_builtin.c  [sh_exec.c]   |  shell
 +------------------------------------------------------------+
 |  libc surface   open read write close readdir chdir ...    |
 +-----------------------------+------------------------------+
 |  monolithic: direct call    |  user-mode: COP stub         |  <- only the
 +-----------------------------+------------------------------+     link differs
 |  syscall layer   sys_open  sys_read  sys_write ...         |  kernel
 +------------------------------------------------------------+
 |  fd table  ->  file_t { VNODE | DEVICE }                   |
 +-----------------------------+------------------------------+
 |  tty                        |  VFS  (namei, mounts, cwd)   |
 |   line discipline           |  NVFS driver                 |
 |   out: NEON Mode 0 backend  |  block cache                 |
 |   in:  HID queue + keymap   |  SD driver, MBR scan         |
 +-----------------------------+------------------------------+
 |  NEON text cells   |   Helium HID queue   |   Helium SD    |  hardware
 +------------------------------------------------------------+
```

The shell never touches the SD card, NEON or the keyboard. For the shell,
screen, keyboard and files are all file descriptors.

---

## 4. Boot chain

### 4.1 Overview

| Stage | Runs on | Reads from | Leaves behind |
|---|---|---|---|
| EC | RP2354B | Its own flash | FPGAs configured; `bios.bin` in memory; CPU out of reset |
| BIOS | 65816 | Memory; SD boot partition | SIB written; NEON text on; kernel loaded; jump to entry |
| Kernel init | 65816 | SD (NVFS) | Drivers up; root mounted; fds 0–2 on the console |
| Shell | 65816 | — | Prompt |

### 4.2 EC stage (existing, unchanged)

As specified in DN-HW-ECIF-001 Rev B and DN-HW-MBXLINK-001: sequence rails,
configure the FPGAs, write `bios.bin` with Debug Agent `WRITE_BURST` frames on
endpoint `0x02`, release CPU reset. **The EC never touches the SD card.**

On prototype platforms the EC role goes to whatever is practical: the Pico on
the Phase 0 HAT, or the PC over the debug path. In the emulator, the machine
starts in the post-reset state with `bios.bin` loaded from a file
(DN-SW-EMU-001). Nothing below depends on which of these is used.

### 4.3 BIOS stage

Console-relevant duties, in order:

1. Native mode, stack, direct page.
2. Write the system information block (SIB). The SIB was introduced with the
   NVM32 gate design and its format is not yet frozen. It carries CPU type,
   platform capabilities and register map version. **This note adds the text
   geometry (columns, rows).** The kernel reads the geometry from the SIB,
   never from constants.
3. Initialise NEON Mode 0 and print a one-line banner. From here on, BIOS
   failures are visible.
4. Check the staging area for a pre-loaded kernel image (§4.4). If one is
   valid, skip to step 8.
5. Initialise the SD card through Helium.
6. Read LBA 0, parse the MBR, and find the boot partition by its
   project-assigned type code.
7. Read the image header, load its segments, verify the CRC.
8. Jump to the kernel entry. No parameters are passed, because the SIB is at a
   fixed address.

On any failure: message on screen, then halt. **No reboot loop**, because a
reboot loop erases the message.

**Where the kernel image lives (OI-1).** Three options:

| | Location | BIOS needs | Update from the host | Cost |
|---|---|---|---|---|
| **A** | Boot partition on the SD card | Block read, MBR parse, loader | Write the partition with a host tool | One small partition |
| B | File `/boot/kernel` on NVFS | Read-only NVFS lookup and extent walk | Copy a file | A second NVFS reader in the BIOS, which has to follow every NVFS format change |
| C | EC flash, pushed together with `bios.bin` | Nothing | Reflash the EC | Kernel iteration tied to the EC update path |

**Recommendation: A.** The kernel image uses the same executable format as user
programs (outlined in the OS contract handoff). The BIOS loader is therefore
the kernel's `exec` loader compiled a second time, and the BIOS gains no code
that would not exist anyway. The MBR scan is shared with the kernel as well
(§7.3).

*Abandon A for B* if NVFS gains a reserved boot area of its own, or if a
separate partition proves awkward when updating from the host.

### 4.4 Bring-up bypass

The Debug Agent can write memory before the CPU leaves reset. During bring-up,
the host writes the kernel image straight into the BIOS staging area over the
debug path. BIOS step 4 finds a valid header (magic and CRC) there and skips
the SD path. This separates "the kernel works" from "the SD boot path works",
and gives a fast iteration loop before the card layout exists.

**Rule.** The BIOS clears the staging header's magic once it has used the
image. Otherwise a stale image would survive a warm reset and be booted in
place of the card's.

---

## 5. Kernel initialisation

The order is the same in both builds.

| # | Step | On failure |
|---|---|---|
| 1 | crt0: native mode, DP, stack, clear `.bss`, initialise `.data` | — |
| 2 | `con_init`: geometry from the SIB, clear screen, banner. `kprintf` and `panic` work from here. | Nothing visible; the Debug Agent is the only witness |
| 3 | `clock_init`: fixed-frequency counter. Every timeout uses it. | `panic` |
| 4 | `kbd_init`: drain the HID queue, load the keymap | Continue as an output-only console, with a "no keyboard" message |
| 5 | `blk_init`: SD card | Continue in degraded mode |
| 6 | `vfs_init`; MBR scan; mount NVFS at `/` (read-only until CN3) | Continue in degraded mode |
| 7 | `tty_init`; fds 0, 1, 2 → `/dev/tty` | `panic` |
| 8 | Monolithic: call `sh_main()`, and call it again if it returns. User-mode: spawn `/bin/sh` as PID 1 and respawn it when it exits. If it is missing, `panic` with the reason. | — |

**Degraded mode.** There is no root file system. The shell still starts, the
prompt shows `(no root)`, and the built-ins that need no file system work
(`help`, `echo`, `clear`, `sysinfo`). Built-ins that need files report
"no root file system".

**Panic.** Message on NEON, interrupts off, halt. Post-mortem analysis goes
through the Debug Agent, which is independent of the CPU. The kernel needs no
built-in debugger.

**Interrupts.** The monolithic build needs none. Input is polled, and time is
read from the fixed-frequency counter.

---

## 6. The console device (tty)

### 6.1 Structure

A tty is a line discipline plus an output backend plus an input backend. The
Rev A backends are:

- out: NEON Mode 0;
- in: the Helium HID queue with the kernel keymap.

Where a CPU-visible UART exists (the prototype has one), a serial backend can
provide `/dev/ttyS0` with the same line discipline. It is optional and not
required by this note. `/dev/tty` is the only tty the console needs.

### 6.2 Output

The driver keeps the authoritative cursor (row, column) in software. The NEON
primitives it uses are listed in §12.1. Control characters:

| Char | Action |
|---|---|
| `\n` | Column 0, next row; scroll at the bottom row |
| `\r` | Column 0 |
| `\b` | Back one column without erasing; at column 0, last column of the previous row |
| `\t` | Next multiple of 8 |
| BEL | Ignored in Rev A |
| ESC | Reserved. Full-screen programs will need a small ANSI subset; not in Rev A |
| Other `< 0x20` | Ignored |

Output wraps at the last column.

Cost: the emulator measured about 17 cycles per character through text mode,
and zero CPU cost for a scroll. The driver's own per-character overhead will
dominate, so the fast path for printable characters must stay short. No output
buffering beyond the call is needed.

### 6.3 Input and keymap

Events come from the Helium HID queue (§12.2). Each event carries a device
(keyboard or mouse), the HID usage (page `0x07`), the modifier bitmap,
press/release, and a repeat flag. The console discards mouse events and key
releases. Modifier state is carried in every event, so releases are not
needed.

The keymap has three tables: base, Shift and AltGr. A small state machine
handles dead keys for accents, and Caps Lock state is tracked in the kernel.
The output is bytes in the console character encoding (OI-7).

- Enter → `\n`.
- Backspace → `0x08`; Delete → `0x7F`. Both erase in canonical mode.
- Ctrl + letter → `0x01`–`0x1A`.
- Arrow and function keys produce no bytes in Rev A. They will become escape
  sequences together with the ANSI subset.

The first keymap to write is the one for the keyboard in use (Spanish ISO is
expected). The Caps Lock LED needs a path from the CPU to the EC and is not
part of Rev A.

### 6.4 Line discipline

**Canonical mode** (the default):

| Input | Effect |
|---|---|
| Printable | Append if there is room, and echo |
| BS or DEL | Remove the last character; echo `\b \b` |
| Ctrl-U | Discard the line and erase it on screen |
| Enter | Append `\n`; the line becomes available to `read()` |
| Ctrl-C | Discard the line, echo `^C\n`, raise an interrupt (§6.5) |
| Ctrl-D on an empty line | `read()` returns 0 (EOF) |

The line buffer holds 255 bytes plus a terminator. When it is full, further
printable characters are dropped. `read(fd, buf, n)` returns at most `n`
bytes, so one line can be consumed over several reads.

**Raw mode** (set by `ioctl`): no echo and no editing, and each byte is
available immediately. Rev A defines the ioctl, but nothing in the console
uses it yet.

`tty_poll()` moves pending hardware events into the line discipline and
detects the interrupt character. `read()` calls it. In the monolithic build,
long-running kernel loops call it as well. In the user-mode build, `read()`
blocks and the process sleeps, which needs an HID interrupt (OI-9).

### 6.5 Interrupt character

- **Monolithic build:** Ctrl-C sets a `tty_intr` flag. Long-running built-ins
  (`cat`, `ls`, `cp`) call `tty_poll()` between blocks and stop when the flag
  is set.
- **User-mode build:** Ctrl-C sends `kill(fg, TERM)` to the foreground process.
  The shell sets itself as foreground again once `wait` returns.

### 6.6 ioctl

| Request | Purpose |
|---|---|
| `TTY_GETMODE` / `TTY_SETMODE` | Canonical or raw |
| `TTY_GETSIZE` | Columns and rows |
| `TTY_CLEAR` | Clear the screen and home the cursor (used by `clear`, since Rev A has no escape sequences) |
| `TTY_SETFG` | Set the foreground PID (user-mode build) |

---

## 7. File system path

### 7.1 Stack

`open` → device table or VFS → NVFS → block cache → SD driver → Helium. All of
it follows DN-FS-VFS-001. This note adds only device resolution (§7.2) and the
boot-time mount (§7.3).

Block size is a mount property. For NVFS it is 2048 B, which means four
512-byte sectors per request, so the SD interface should support multi-sector
transfers (§12.3).

### 7.2 Device nodes (OI-2)

Proposal: `sys_open` checks for the `/dev/` prefix and looks up the rest of the
path in the device table (name → driver). `file_t` gains a `kind` field:

- `VNODE`: `read`, `write` and `close` go to `vnode_ops`;
- `DEVICE`: they go to the driver's five-function interface.

`/dev` does not exist on disk, and `ls /dev` fails in Rev A. That is accepted.
Rev A registers only `tty`, and the raw SD card is not exposed.

*Replace with a devfs mount* once something needs to enumerate devices
(`ls /dev`, hot-plugged devices). DN-FS-VFS-001 already lists devfs as a
future driver (§3) and expects `vnode_ops` to need `open` and `close` at that
point (§6).

### 7.3 Mount at boot

The kernel runs the same MBR scan as the BIOS and mounts the first NVFS
partition at `/`. It is mounted read-only up to CN2 and read-write from CN3.
The ext2 partition stays unmounted until DN-FS-EXT2-001 F1.

### 7.4 Current directory

- **Monolithic build:** a single global current directory (vnode reference
  plus path string).
- **User-mode build:** kept per process in the PCB as a string plus a vnode
  reference. This adopts the recommendation in DN-FS-VFS-001 §13.

`chdir` updates both; `getcwd` is a copy.

---

## 8. The shell

### 8.1 Principle

| File | Content | Builds |
|---|---|---|
| `sh_main.c` | Loop, prompt, dispatch | Both |
| `sh_parse.c` | Tokeniser and redirection | Both |
| `sh_builtin.c` | Built-in commands | Both |
| `sh_exec.c` | External command launch | User-mode build only; the monolithic build links a stub that answers "not found" |

The shell includes one project header that declares the libc surface. There is
no `#ifdef` in the three shared files. The shell prints through `write()` and
small helpers such as `sh_puts` and `sh_putu`. It does not use stdio, which
keeps the monolithic image small. stdio can be allowed later if code size
permits.

### 8.2 Main loop (illustrative, not normative)

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
        if (n <= 0) continue;                   /* EOF is ignored by the login shell */
        line[n] = '\0';
        int argc = sh_parse(line, argv, &redir);
        if (argc > 0)
            sh_run(argc, argv, &redir);         /* built-in, else sh_exec() */
    }
}
```

Error-return conventions follow the unified error space of DN-FS-VFS-001.

### 8.3 Parsing

- Split on spaces and tabs. Tokens are cut in place in the line buffer, with no
  copies.
- `"..."` groups characters into one token; `\` escapes the next character.
- `> file` and `>> file` redirect output. Only the last redirection counts.
- Not in Rev A: `|`, `;`, `&`, `<`, variables, globbing.
- Limits: 255-character line, 16 arguments. Going over either limit is an
  error, never a silent truncation.

### 8.4 Built-ins

Signature: `int bi_name(int argc, char **argv, int out);`, where `out` is the
output fd after redirection. The return value is 0 or an error code, and the
shell prints errors as `name: arg: message`.

| Command | Gate | Notes |
|---|---|---|
| `help` | CN1 | Lists the built-ins |
| `echo [args]` | CN1 | |
| `clear` | CN1 | `TTY_CLEAR` |
| `sysinfo` | CN1 | SIB contents: CPU type, platform, register map version, text geometry |
| `pwd` | CN2 | |
| `cd [dir]` | CN2 | Defaults to `/`. Always a built-in, in both builds, because it changes the shell's own directory |
| `ls [-l] [path]` | CN2 | Directories marked with `/`; `-l` adds size |
| `cat file...` | CN2 | |
| `stat path` | CN2 | |
| `mkdir dir` | CN3 | |
| `rmdir dir` | CN3 | |
| `rm file` | CN3 | |
| `mv src dst` | CN3 | `rename`; fails when source and destination are on different mounts |
| `cp src dst` | CN3 | Read/write loop with a 2 KB buffer (one NVFS block) |
| `sync` | CN3 | |
| `exit` | CN4 | User-mode build only. Kernel respawns PID 1 |

In the user-mode build, any built-in except `cd`, `exit` and `help` can move
to `/bin` without changing the shell.

### 8.5 Redirection

`>` and `>>` apply to built-ins in both builds: the shell opens the target and
passes its fd as `out`. In the user-mode build they also apply to external
commands, using `dup2` around the spawn. There is no input redirection and no
pipes in Rev A. Pipes need processes and `pipe()`; reconsider them after CN4.

### 8.6 External commands (user-mode build only)

1. Look up the built-in table.
2. If `argv[0]` contains `/`, use it as a path; otherwise look in `/bin`. The
   search path is fixed to `/bin` because Rev A has no environment variables.
3. `pid = exec(path, argv)`, spawn semantics (OI-3).
4. `ioctl(0, TTY_SETFG, pid)`, `status = wait(pid)`, then set the shell as
   foreground again.
5. Report a non-zero status.

### 8.7 Error messages

The shell keeps a short table that maps codes from the VFS error space to text
(`not found`, `not a directory`, `read-only file system`, and so on). The
kernel carries no message strings.

### 8.8 Memory discipline

- All buffers are static. There is no `malloc` in Rev A.
- The directory entry buffer is static and provided by the shell.
  DN-FS-VFS-001 §5.3 requires `dirent_out_t` (262 bytes) to be provided by the
  caller, never a stack local inside the VFS.
- In the monolithic build the shell runs on the kernel stack, so it has no
  large locals.
- Path buffers use the VFS header's maximum path length.

### 8.9 Example session (illustrative)

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

---

## 9. SD card layout

The card uses an MBR partition table. GPT would add parsing to the BIOS for no
benefit at these card sizes.

| # | Type | Content |
|---|---|---|
| 1 | noVa64 boot (project-assigned code, TBD) | Kernel image in the executable format |
| 2 | NVFS | Root file system |
| 3 (optional) | Linux (`0x83`), ext2 | Exchange with a host; mounted read-only once ext2 F1 exists |

The root tree in Rev A is `/bin` (empty in the monolithic build) and
`/etc/motd`. `/dev` is synthetic (§7.2).

**Assumption to verify:** NVFS lives inside a partition. If DN-FS-NVFS-003
assumes the whole device, the partition offset is added in the block device
layer, and NVFS itself does not change.

Card images are built on the host. NVFS content is built with the existing
`nv` tool. The boot partition is written by a host tool, either an `nv`
subcommand or a separate tool.

---

## 10. Build configurations and syscall slice

### 10.1 The two builds

| Aspect | Monolithic build | User-mode build |
|---|---|---|
| libc backend | Direct calls to `sys_*` | COP stubs |
| Shell entry | `sh_main()` called by the kernel | User `crt0` → `main` |
| `sh_exec.c` | Stub | Real |
| Waiting for input | Polling | Blocking read, HID interrupt |
| Current directory | Global | Per process |
| MMU | Off | On |
| Ctrl-C | Flag | `TERM` to the foreground process |
| Platforms | Emulator, Phase 0 testbench, Phase 1 | Emulator, Phase 1 onward |

Identical in both: the shell sources except `sh_exec.c`, tty, keymap, VFS,
NVFS, block cache and the SD driver. Phase 0's scope (NEON Mode 0, SD, HID
keyboard on the Pico, Debug Agent) covers everything the monolithic build
needs.

### 10.2 Syscall slice

| Call | Used by | First needed |
|---|---|---|
| `read`, `write` | tty, every built-in | CN1 |
| `ioctl` | `clear`, `sysinfo`, foreground control | CN1 |
| `open`, `close` | `cat`, redirection | CN2 |
| `opendir`, `readdir`, `closedir` | `ls` | CN2 |
| `stat` | `ls -l`, `stat` | CN2 |
| `chdir`, `getcwd` | `cd`, `pwd`, prompt | CN2 |
| `mkdir`, `rmdir`, `unlink`, `rename`, `sync` | CN3 built-ins | CN3 |
| `exec` (spawn), `wait`, `exit` | External commands | CN4 |
| `dup`, `dup2` | Redirection of external commands | CN4 |

In the monolithic build every row is a direct function call. The COP mechanism
is hidden inside the stubs, so this note does not depend on how the syscall
number is passed (see §14, item 3).

---

## 11. Gates

| Gate | Deliverable | Pass criterion | Build |
|---|---|---|---|
| **CN0** | Kernel speaks | Kernel image (loaded through the §4.4 bypass) prints its banner and SIB summary. `panic()` demonstrably prints and halts. | Monolithic |
| **CN1** | Interactive console, no file system | A scripted keystroke test exercises every rule in §6.4 and every keymap table (Shift, AltGr, dead keys). `help`, `echo`, `clear` and `sysinfo` work in degraded mode. | Monolithic |
| **CN2** | Read-only prompt | The BIOS loads the kernel from the boot partition, and NVFS is mounted read-only. A golden-screen script over a reference image passes. The mount-failure path shows degraded mode. **Matches the E2.5 checkpoint:** boots to its own prompt, in text mode, from SD, with no MMU. | Monolithic |
| **CN3** | Read-write console | The CN3 built-ins and `>`/`>>` pass golden-screen scripts. The NVFS conformance and crash-injection suites pass against the kernel build of NVFS, in the emulator. A card written on hardware verifies with the NVFS host tooling. | Monolithic |
| **CN4** | User-mode shell | Same shell sources linked against COP stubs. `/bin/sh` runs as PID 1. An external program in `/bin` runs with `argv`, and its exit status is reported. Ctrl-C ends it and returns to the prompt. **Matches the K5 pass condition.** | User-mode |

Each gate is passed in the emulator first, then on hardware.

---

## 12. Requirements on other notes

### 12.1 DN-HW-TEXT-001 (NEON Mode 0), not yet written

The console needs these primitives from NEON:

1. Write a glyph at the cursor and advance.
2. Set the cursor position.
3. Scroll up one row, with the new row cleared (ring-origin scroll).
4. Clear the screen.
5. A visible cursor.
6. Geometry published through the SIB.
7. No register with side effects on read.

Control characters are **not** interpreted in gateware (D-3). Automatic scroll
when the cursor passes the last row is allowed; the driver works either way.
The glyph store must contain every character the keymap can produce in the
chosen encoding (OI-7). The command port is two bytes wide, as already decided.

### 12.2 Helium CPU-side HID interface

- An event queue fed by endpoint `0x03`. Each entry carries: device, HID usage,
  modifier bitmap, press/release, repeat flag.
- **An explicit write pops an entry, never a read** (portability contract,
  §12.5).
- A count or not-empty status, and a sticky overflow flag.
- An interrupt line to the CPU, needed for the user-mode build (OI-9).
- The EC generates typematic repeat (D-4). Delay and rate are EC parameters,
  set later through the `SYS` endpoint.

### 12.3 Helium CPU-side SD interface

- Block transfer from the card into memory, multi-sector, with a completion
  status and an error code. This matches DN-FS-EXT2-001 OI-5: design for burst
  transfer before the register map is frozen.
- No data port with read side effects (§12.5).
- A completion interrupt is optional in the monolithic build and required for
  the user-mode build.

### 12.4 Syscall contract

See §14, items 1–3.

### 12.5 Portability contract (noVa64 / noVa128)

All console code follows the four requirements set in the NVM32 work:

- **No cycle counting for time.** Timeouts and repeat timing use the
  fixed-frequency counter.
- **No self-modifying code.**
- **I/O registers are idempotent on read.** This is why §12.2 and §12.3 forbid
  pop-on-read and data ports.
- **A stable register map versioned through the SIB.**

---

## 13. Verification

- **Host unit tests** (plain C99): tokeniser, line discipline (synthetic key
  events in, byte stream and screen operations out), keymap tables. Code uses
  fixed-width types. The emulator build catches any remaining difference
  caused by Calypsi's 16-bit `int`.
- **Emulator golden screens:** keystrokes are scripted through the emulated
  HID queue. After each command the NEON text buffer is dumped and compared
  with a stored screen. The SD image is built from a directory tree with the
  host tools.
- **NVFS suites against the kernel build:** CN3 requires the NVFS inside the
  kernel to pass the existing conformance and crash-injection suites. The
  simplest way is to build the kernel driver from the same source as the
  reference implementation behind the VFS operation tables. If the two
  sources diverge, the suites run against the kernel build in the emulator.
- **Hardware:** the EC is the HID source, so it can inject the same scripted
  keystrokes sent from the PC over the debug path. On hardware, screens are
  checked by eye, unless DN-HW-TEXT-001 gives the text buffer a diagnostic
  read path.

---

## 14. Discrepancies found with existing documents

1. **Syscall table.** The original OS contract handoff lists `open`, `close`,
   `read`, `write`, `seek`, `stat`, `unlink`, `rename`, `mkdir`, `readdir` and
   `ioctl`. It has no `chdir`, `getcwd`, `rmdir`, `dup`, `dup2`, `sync`,
   `opendir` or `closedir`. DN-FS-VFS-001 §13 maps all of them. This note
   treats the VFS mapping as authoritative, and the contract handoff should be
   updated to match.
2. **`exec`.** The contract defines `exec(path)` without an argument vector,
   and no `fork`. Since `wait(pid)` exists and nothing else creates a process,
   this note assumes spawn semantics (`exec` creates a process and returns its
   PID) and needs an `argv` parameter. See OI-3.
3. **Syscall number location.** The project overview and the contract handoff
   describe COP with an inline signature byte. The FPGA-A register map session
   moved the syscall number into the accumulator to avoid walking the stack.
   DN-FS-VFS-001 §13 mentions both. This note is unaffected because the stubs
   hide the mechanism, but the documents should be reconciled.
4. **Roadmap E2.5, SD boot step.** The roadmap's E2.5 step "microSD boot path:
   RP2040 reads an image from the card into memory and releases the CPU"
   predates DN-HW-ECIF-001 Rev B. Since Rev B, the SD card belongs exclusively
   to Helium and the EC has no SD driver, so the CPU (BIOS) is the only SD
   reader. §4.3 replaces that step.
5. **Text geometry.** The NEON specification defines Mode 0 as 128×32 cells of
   8×16 pixels (1024×512, 4096 cells, 8 KB EBR). The emulator implements
   128×75 (about 9.6 KB). DN-HW-TEXT-001 must settle this. Because the console
   reads geometry from the SIB, the software works with either.
6. **Device names.** The contract handoff defines `/dev/tty` as the UART and
   `/dev/kbd` as a key matrix. Both predate the EC HID endpoint. In this note,
   `/dev/tty` is the console (NEON + HID) and a UART, where one exists, is
   `/dev/ttyS0`. Raw `/dev/kbd` and `/dev/mouse` are left to the G-series,
   with the rule that only one consumer owns the HID queue at a time.

---

## 15. Open items

| # | Item | Blocks | Recommendation |
|---|---|---|---|
| OI-1 | Where the kernel image lives: boot partition, NVFS file or EC flash | BIOS load path, CN2 | A, boot partition (§4.3) |
| OI-2 | Device-node resolution: prefix check or devfs | CN1 | Prefix check now; devfs when enumeration is needed |
| OI-3 | `exec` signature: `argv`, spawn semantics, fd inheritance (children inherit 0–2), where `argv` lands in the new address space | CN4 | Spawn semantics with `argv`; place `argv` above the user stack in bank `$00` |
| OI-4 | DN-HW-TEXT-001: geometry, register map, cursor, diagnostic read path | CN0 on hardware (the emulator can proceed with its current map) | Write it against §12.1 |
| OI-5 | Helium HID interface: event format, pop by write, overflow | CN1 on hardware | §12.2 |
| OI-6 | Helium SD interface: burst to memory, completion, errors | CN2 on hardware | §12.3, together with DN-FS-EXT2-001 OI-5 |
| OI-7 | Character encoding shared by glyph store, keymap and file names | CN1 | ISO-8859-15: one byte per character, covers Spanish, includes €. UTF-8 names from ext2 hosts display as raw bytes, accepted for Rev A |
| OI-8 | Resident kernel code size compared with the 64 KB of bank `$01` | CN4 memory layout | Measure at CN2: the monolithic build gives the real number. If it does not fit, kernel code spans more than one bank. This is layout, not correctness, because the large model already uses `JSL` |
| OI-9 | CPU interrupt source for the HID queue and SD completion | CN4 | Part of the Helium interrupt design |
| OI-10 | Reconcile the syscall contract (§14, items 1–3) | CN4 | Update the contract handoff from DN-FS-VFS-001 §13 |
| OI-11 | Update the roadmap E2.5 step text (§14, item 4) | — | Editorial |

---

## 16. Abandonment conditions

- **Same shell source in both builds (D-1).** Abandon if keeping it requires
  `#ifdef` inside the shared shell files beyond the libc backend, the entry
  point and `sh_exec.c`. The shell then forks at CN4, and the monolithic copy
  is frozen.
- **Boot partition (D-5).** Move to option B if NVFS gains its own boot area,
  or if updating the kernel from the host becomes a recurring nuisance.
- **Prefix device resolution (D-6).** Replace with devfs when anything needs to
  list devices.
- **Polled input.** Valid only while the system is single-threaded. It must be
  gone before CN4, because a blocking `read()` in a multitasking kernel cannot
  spin.
- **Control characters in software (D-3).** Revisit only if a measurement
  shows the driver's per-character cost dominating console throughput in real
  use. Even then, move only the `\n` fast path into NEON.
