# Boot images

A boot image is a raw 8 KB binary covering `$00E000`–`$00FFFF`, carrying its
own 65816 vectors at the top. That is exactly what the EC would have placed in
system memory before releasing CPU reset, and exactly what the emulator loads.

The load address is fixed at `$E000` in the browser shell. The native build
takes `-a` if you want it elsewhere.

## Build

```
apt install cc65          # or build from github.com/cc65/cc65
cd boot && make           # -> boot.bin, 8192 bytes
```

Then either drag `boot.bin` onto **Load boot image** in the browser, or:

```
./nova64 -c config/default.params -b boot/boot.bin -n 2000000
```

## What the template does

Comes up in emulation mode, switches to native with 16-bit registers, enables
NEON text mode, prints a banner, then loops draining the mailbox and echoing
key presses.

It never issues a `PRESENT`, and it never uploads a font. Both of those are
deliberate: a text mode that needed either would not be a text mode. NEON
scans out continuously and comes up with a glyph set initialised from the
bitstream.

## Files

| | |
|---|---|
| `boot.s` | the template |
| `nova64.inc` | I/O map and command opcodes |
| `nova64.cfg` | `ld65` configuration: memory layout and the vector block |
| `Makefile` | `ca65` + `ld65` |

## Writing your own

Start from `boot.s`. Two things to know that the assembler will not tell you.

**Register width is not tracked by the CPU for you.** `ca65` needs `.a8`,
`.a16`, `.i8`, `.i16` directives to know how to assemble immediate operands,
and they must match what `REP`/`SEP` actually did at run time. Getting this
wrong produces an image that assembles cleanly and executes garbage. It is the
single most common way to lose an afternoon on this architecture.

**`STZ` has no long addressing mode.** Writing zero to an I/O register in bank
`$FE` needs `LDA #0` then `STA`. The assembler catches this one.

## Register map

Everything in `nova64.inc` is PROVISIONAL. The emulator invented all of it
because no design note fixes any of it, and it will change. Code written
against it today will need revision — that is the cost of starting software
before the hardware specification exists, and it is usually worth paying.

The pieces most likely to move: the scancode set, the text register layout,
the NEON command encoding, and the I/O window base itself.

## Scancodes

The browser maps `KeyboardEvent.code` to a scancode by table position, so the
physical key matters and your keyboard layout does not:

| scancode | key |
|---|---|
| 1–26 | `A`–`Z` by position |
| 27–36 | `0`–`9` |
| 37 | Enter |
| 38 | Space |
| 39–41 | Backspace, Tab, Escape |
| 42–45 | arrows, up down left right |
| 46–49 | Shift left, Shift right, Control left, Alt left |
| 50–59 | punctuation, in the order given in `web/index.html` |

## Workflow

`make serve` from the top level assembles `boot.s`, copies `boot.bin` next to
the page and starts the server. After that, editing `boot.s` needs only:

```
make -C boot && cp boot/boot.bin web/
```

then **Reload boot.bin** in the console. No file dialog, no re-picking the
file after every rebuild.
