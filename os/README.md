# noVa64 — the console-mode operating system

The first usable state of noVa64, as sheet CN1 of the specification
(`docs/docsV3`) describes it: the machine boots from its own card and shows a
prompt, on its own display, that browses and changes files. This tree is gates
**CON-00 to CON-03** — the monolithic build, with no MMU and no processes.

```
noVa64 BIOS
kernel: loaded from sd0p1
noVa64 kernel (monolithic)
cpu: W65C816S, 16384 KB, text 128x32
root: NVFS on sd0p2
Welcome to noVa64.
/> ls
bin/  etc/
/> cd etc
/etc> cat motd
Welcome to noVa64.
```

## Building and running

Needs `cc65` (for `ca65` and `ld65`), a C compiler, `make` and Python 3.

```
make            # emulator, BIOS, kernel image, a 16 MB card image -> build/
make run        # boot the card in the terminal; Ctrl-] quits
make check      # every gate, in the emulator
```

`make run` mounts `build/card.img` read-write and changes it in place.

## What is here

| Directory | Contents |
|---|---|
| `bios/` | The system BIOS: 8 KB at `$00:E000`, information block, banner, kernel loader (sheet I, CN1.4) |
| `kernel/` | The kernel: console, keymap, tty, block driver, block cache, journal, NVFS, VFS, system calls |
| `shell/` | The shell: loop, tokeniser, built-ins. Calls only the libc surface (CN1.1) |
| `common/` | The SD driver and the MBR scan, one source assembled into both BIOS and kernel (CN1.5) |
| `include/` | `hw.inc` the hardware, `abi.inc` what programs and kernel agree on, `macros.inc` conventions |
| `emu/` | A reference machine: 65816 core, Neon Mode 0, Helium's blocks, HID queue, SD card |
| `tools/nv.py` | Host tools: executable images, card images (`mkfs` + populate), `fsck`, `ls`, `cat`, `tree` |
| `tests/` | The gate scripts, their golden screens, and the power-cut sweep |

About 8,500 lines of 65816 assembly. The kernel image is 19 KB of code and
7 KB of data in bank `$FD`, plus 60 KB of bank `$FC` for the block cache and
the console's scrollback (CN1.26 asks for this number at CON-02).

### The gates

| Gate | State | What `make check` does |
|---|---|---|
| CON-00 | passes | A staged kernel prints its banner and the information block; `panic` prints and halts |
| CON-01 | passes | Scripted keystrokes through every rule of the line discipline and all three keymap tables, dead keys included; `help`, `echo`, `clear`, `sysinfo` in degraded mode |
| CON-02 | passes | BIOS loads the kernel from the boot partition; golden screens over a read-only card that is byte-identical afterwards; a failed mount shows degraded mode |
| CON-03 | passes | The write built-ins and `>` / `>>`; the image then passes `nv.py fsck`; a power cut after every one of the session's block writes recovers |
| CON-04 | not built | Needs the MMU, processes and the `COP` gate |

"Passes" means in this tree's emulator. Nothing here has run on hardware.

## Two decisions that are not the specification's

**The kernel is assembly, not C.** Sheets O.2 and O.6 specify Calypsi C with
assembly only where it hurts. Calypsi was not available where this was
written, so everything is `ca65` assembly. The structure follows the
specification — the layers of CN1's table, the five-function drivers of J.4,
the service numbering of LB1.10 — but CN1.1's "one shell source, built twice"
is not yet true: this shell is assembly and the C one is still to be written.
`shell/libc.s` and `shell/sh_exec.s` are the two files that would change for
the user-mode build, as CN1.26 has it.

**The emulator here is not the project's emulator.** It was written from the
specification sheets because the project's own was not at hand, and it places
its devices where the sheets say rather than where that emulator invented them
(EM4.10): text at `$FE:0000`, Neon's registers at `$FF:8000`, Helium below
`$FF:8000`. Geometry is the specified 128 × 32. It is functional rather than
cycle-exact, has no MMU, no decimal mode and no graphics, and is validated
against no external suite — the warning of EM4.5 applies to it in full.
**Porting to the project's emulator means changing `include/hw.inc`**, and the
SD path if that emulator has only the 512-byte data port: this driver uses the
DMA registers of sheet G.

## Provisional choices

Where the sheets leave something open, this tree picks a value, marks it
PROVISIONAL at its definition, and uses it from one place.

| Open question | Choice here | Defined in |
|---|---|---|
| Q22 — bank `$FF` allocation | counter `$FF:0100`, HID queue `$FF:0200`, SD block `$FF:0300` | `hw.inc` |
| Q168 — capability register, information block | `HE_CAPS` and neighbours at `$FF:0022`; block layout `SIB_*` | `hw.inc`, `abi.inc` |
| Q180, Q4 — HID queue | status, count, head entry in two words, pop by write | `hw.inc` |
| Q177 — boot partition type | `0x7E` | `hw.inc`, `tools/nv.py` |
| Q20 — executable header | magic `7F 'N' '6' '4'`, segment table, CRC-32 of the file | `abi.inc` |
| Q182 — console encoding | ISO-8859-15, as recommended | `kernel/keymap.s` |
| CN1.6 — staging area | `$10:0000` | `hw.inc` |
| L.15 — fixed-frequency counter | 1 kHz, 32 bits, read high-low-high | `hw.inc` |

The emulator also models the SD card's raw-command registers only as far as
the initialisation sequence needs, and not the PIO data port, which pops on
read and so conflicts with D92 (Q181).

## How it fits the specification, and where it stops short

**Boot.** The BIOS writes the information block, prints its banner, takes a
kernel staged at `$10:0000` if its magic and CRC are good (the Debug Agent
bypass of CN1.6) and otherwise reads the boot partition; it loads the segments,
clears the staged magic and jumps. On any failure it prints and halts. It
stages at most 64 KB. There is no monitor (I.3).

**Console.** Control characters are the driver's (D105); the scrollback copy
and `con_repaint` exist from the start (J.4) though nothing switches modes yet.
The kernel does not clear the screen at `con_init`: it carries on below the
BIOS's lines, as the sample session of CN1.24 shows. The keymap is Spanish ISO
with Shift, AltGr, Caps Lock and four dead keys. Tab is not accepted on the
input line.

**Files.** NVFS draft 3 as specified: checksums verified on everything read,
feature policy applied first and the offending bit named, journal recovery,
the orphan list, extents merged on insert, `data=ordered`.

- Each operation is one transaction, committed **and checkpointed** before the
  next begins. Slower than batching, and the reuse rule of Y2.17 then holds by
  construction.
- There is **no vnode cache**. No layer keeps a copy of an inode between
  calls; the block cache holds the only one, which is the property Y1.6 is
  after. `file_t` is still separate from the descriptor.
- One mount. No ext2, no `mount`/`unmount`: a volume mounted read-write keeps
  `s_state = 1`, because nothing ever unmounts the root. A write-protected
  card in that state is refused, as Y2.20 requires.
- The write path grows an extent tree to depth 1 only: a file needing more
  than roughly 500–1000 fragments gets `E_FBIG`. Reads and frees handle depth 3.
  Writing into an `UNWRITTEN` extent is `E_NOTSUP`. DIRHASH is refused at mount.
- `..` is resolved lexically, since NVFS stores no parent (Y2.13).
- `stat` follows symbolic links; there is no `lstat`, so `ls -l` shows a link
  as its target.

**System calls.** The handlers and the numbered table of LB1.10 are in
`kernel/sys.s`. Arguments travel in A, X and the direct-page word `r0`, which
leaves Y free for the service number. That is this tree's convention, not
Calypsi's, and CON-04 will have to reconcile them. `lseek` takes its 32-bit
offset in `r0:r1`.

## A finding: a torn block can defeat recovery

`make check` cuts power after every block write of a session — 194 of them —
and requires a consistent volume from both the host's journal replay and the
kernel's, with identical trees. That passes, 85 of the cuts needing the journal.

The same sweep with the cut falling **inside** a four-sector block write does
not: in the sample `tests/run.py torn` takes, about one cut in nine leaves a
volume neither side will mount. Two blocks are exposed, and they are the two
the format has exactly one copy of with a whole-block checksum:

- the **primary superblock**, rewritten in place at every checkpoint. The
  journal holds a good copy, but mount verifies block 1 (step 2 of Y2.21)
  before it reaches recovery (step 6), and Y2.6 forbids falling back to the
  backup to find the journal;
- the **journal superblock**, rewritten in place to advance the checkpoint.

Every other block is safe, because a torn home write is simply replayed. The
specification's own caveat is Q96 — whether a barrier on SD is a durability
guarantee. This is the same question one level down: whether a four-sector
write is atomic. If it is not, the format needs an answer — recovery located
from the immutable backup layout before the primary is trusted, and an A/B
journal superblock, would be one. The driver here follows the specification as
written and does not paper over it.

## Tests

```
tests/run.py                 # all gates
tests/run.py con03 crash     # some of them
tests/run.py --update        # rewrite the golden screens from this run
tests/run.py torn            # the torn-block sweep above; informative, never fails
```

Key scripts (`tests/*.keys`) are typed text with a few escapes — `{C-c}`,
`{BS}`, `{K:34:2}` for a raw HID usage with modifiers, `{SNAP name}` to dump
the screen once the machine is idle. The emulator feeds a key only while the
guest is waiting for one, so a run does not depend on timing.

There are no host unit tests of the tokeniser or the line discipline (CN1.31):
those presume C sources that compile on the host.
