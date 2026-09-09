# DN-GPU-CLUT-001 — Palette and Line-Synchronous Register Contract

**Project:** noVa64
**Domain:** GPU (NEON / FPGA-B)
**Status:** Active
**Revision:** A

---

## 1. Revision history

| Rev | Status | Summary |
|-----|--------|---------|
| A | Active | Initial issue. Defines dual-bank CLUT, write protocol, global palette offset, configurable output truncation, cursor palette port, and line comparison. Reserves register space for a future display list processor. Supersedes the deferred-write buffer mechanism proposed during design discussion (see §12). |

---

## 2. Purpose and scope

This note defines the software-visible contract for colour lookup and line-synchronous control in NEON. It is part of the NEON register ABI, not of the schematic.

**In scope:** CLUT storage organisation, register map, write protocol, bank switching semantics, palette offset, output truncation, cursor palette, line comparison and interrupts, reserved space for a display list processor.

**Out of scope:** physical panel interface (see `DN-HW-PANELIF-001`), blitter command format, compositor design, framebuffer memory management.

---

## 3. Decisions

| # | Decision | Section |
|---|----------|---------|
| D1 | Two complete CLUT banks, with atomic switching. | §5, §8 |
| D2 | Entries are stored as 24-bit RGB888 regardless of the routed output depth. | §5, §7 |
| D3 | Writes use an auto-incrementing index port and two 16-bit data writes per entry. | §7 |
| D4 | An 8-bit global offset is added to the index before lookup. | §9 |
| D5 | Output truncation width (8/6/4 bits per channel) is a runtime register field. | §10 |
| D6 | The hardware cursor has a separate, independently-addressed palette port. | §11 |
| D7 | A line comparison register with interrupt is implemented in Rev A. | §13 |
| D8 | Register space and one SDRAM arbiter client are **reserved but not implemented** for a display list processor. | §14 |

---

## 4. Colour model

The framebuffer is 8 bpp indexed. A framebuffer byte is not a colour — it is an index into the CLUT. The colour itself lives in the table, which software writes.

| Property | Value |
|---|---|
| Simultaneous colours | 256 |
| Candidate colours per entry, RGB666 output | 262,144 |
| Candidate colours per entry, RGB888 output | 16,777,216 |
| Available greys, RGB666 output | 64 |

Palette reload is a register operation. Changing the entire palette touches no framebuffer bytes, which makes fades, colour cycling, and selection highlighting free of blitter work. This matters beyond aesthetics: **every effect achieved by moving the palette is an effect that emits no commands to NEON**, and CPU-side command emission is the binding constraint on the subsystem.

### 4.1 Expected palette structure

Software is not expected to use 256 arbitrary colours. The anticipated system layout, recorded here to inform the offset mechanism in §9:

| Range | Purpose |
|---|---|
| ~16 entries | Fixed system colours: desktop, window borders, active/inactive title bars, text, shadow, highlight |
| ~200 entries | General purpose: grey ramp, saturated primaries, coarse colour cube for images |
| Remainder | Reserved: cursor, direct-write scratch |

A palette manager in the OS is required to arbitrate allocation between applications. This is a known component of the GUI design and should be provisioned before application-level colour APIs are defined.

---

## 5. CLUT storage and EBR budget (D1, D2)

Two banks of 256 entries, each entry 24 bits, exploiting the iCE40 EBR's selectable width configurations:

| Block | Configuration | Contents |
|---|---|---|
| 1 | 256 × 16 | Bank A, red + green |
| 2 | 256 × 16 | Bank B, red + green |
| 3 | 512 × 8 | Blue for both banks (A at 0–255, B at 256–511) |

**Total: 3 EBR blocks of 32 on HX8K (~9%).** Packing the blue channel of both banks into one 512×8 block saves one block against the naive 2 + 2 allocation.

Note that this cost is identical for RGB666 and RGB888 storage, because a 256×18 table also requires two blocks. Storing 24 bits is therefore free relative to storing 18, which is the basis of D2.

---

## 6. Register map

Offsets are relative to a CLUT block base address. **The base address is an open item** pending reconciliation with the existing NEON register map (§15, O1).

| Offset | Name | Access | Width | Description |
|---|---|---|---|---|
| +0x00 | `CLUT_INDEX` | W | 16 | Entry index 0–255. Auto-increments after each committed entry. |
| +0x02 | `CLUT_DATA_LO` | W | 16 | `{G[7:0], B[7:0]}` |
| +0x04 | `CLUT_DATA_HI` | W | 16 | `{8'b0, R[7:0]}`. **Write commits the entry and increments `CLUT_INDEX`.** |
| +0x06 | `CLUT_CTRL` | R/W | 16 | See §6.1 |
| +0x08 | `CLUT_OFFSET` | R/W | 16 | Global index offset, bits [7:0]. See §9 |
| +0x0A | `CLUT_STATUS` | R | 16 | See §6.2 |
| +0x0C | `CURSOR_CLUT_INDEX` | W | 16 | Cursor palette index. Auto-increments. See §11 |
| +0x0E | `CURSOR_CLUT_DATA_LO` | W | 16 | `{G[7:0], B[7:0]}` |
| +0x10 | `CURSOR_CLUT_DATA_HI` | W | 16 | `{8'b0, R[7:0]}`. Commits and increments. |
| +0x12 | `LINE_COMPARE` | R/W | 16 | Logical scanline number for interrupt. See §13 |
| +0x14 | `LINE_CURRENT` | R | 16 | Current logical scanline, live |
| +0x16 | `VID_INT_ENABLE` | R/W | 16 | See §13 |
| +0x18 | `VID_INT_STATUS` | R/W1C | 16 | See §13 |
| +0x1A–0x1F | — | — | — | Reserved, read as zero |
| +0x20–0x2F | — | — | — | **Reserved for display list processor.** See §14 |

### 6.1 `CLUT_CTRL`

| Bits | Name | Description |
|---|---|---|
| [0] | `BANK_WR` | Bank targeted by CLUT writes: 0 = A, 1 = B |
| [1] | `BANK_ACTIVE` | Bank used for display: 0 = A, 1 = B |
| [2] | `SWAP_ON_VSYNC` | 0: writes to `BANK_ACTIVE` take effect immediately. 1: `BANK_ACTIVE` is latched at the next VSYNC. |
| [4:3] | `OUT_DEPTH` | 00 = 8 bits/channel, 01 = 6, 10 = 4, 11 = reserved. See §10 |
| [15:5] | — | Reserved, write zero |

### 6.2 `CLUT_STATUS`

| Bits | Name | Description |
|---|---|---|
| [0] | `SWAP_PENDING` | A `BANK_ACTIVE` change is armed and awaiting VSYNC |
| [1] | `IN_VBLANK` | Vertical blanking active |
| [15:2] | — | Reserved, read as zero |

---

## 7. Write protocol (D2, D3)

Per entry: one write to `CLUT_DATA_LO`, then one to `CLUT_DATA_HI`. The high write commits the entry and increments the index. Loading a full palette is one index write followed by 512 data writes — 513 accesses total.

### 7.1 Why not three 8-bit writes

The VGA-style three-byte-per-entry protocol is rejected. It would require 769 accesses for a full palette against 513. At approximately 50 cycles per NEON command this is a difference of roughly 12,800 CPU cycles per palette load, on the subsystem's scarcest resource. Two 16-bit writes also permit the Calypsi compiler to emit word stores directly from a `uint32_t` array.

### 7.2 Why 24-bit storage with an 18-bit pin boundary (D2)

Entries are always written and stored as RGB888. Truncation to the routed width happens in the output pad register (§10).

If the table stored 18 bits, the ABI would be bound to the physical routing, and a future board revision with spare pins would break binary compatibility with all existing software. Storing 24 bits costs no additional EBR (§5), so the decision is free.

### 7.3 Auto-increment

`CLUT_INDEX` increments modulo 256 after each committed entry. It does not wrap into the other bank; bank selection is exclusively via `CLUT_CTRL.BANK_WR`.

---

## 8. Bank switching semantics (D1)

The intended pattern:

1. Set `BANK_WR` to the inactive bank.
2. Write entries at leisure, across as many frames as necessary.
3. Set `SWAP_ON_VSYNC`, then flip `BANK_ACTIVE`.
4. Hardware latches the change at the next VSYNC. `SWAP_PENDING` clears.

The change is **atomic**: there is no intermediate state in which some entries are new and others old, and no copy operation.

Immediate mode (`SWAP_ON_VSYNC` = 0) remains available and is expected to be used during boot, in text mode, and wherever latency matters more than a single-frame artefact. Writing the active bank during active scanout will produce a visible flash on the line being drawn.

---

## 9. Global palette offset (D4)

`CLUT_OFFSET[7:0]` is added to the framebuffer index, modulo 256, before the table is addressed.

This provides sub-palette switching without touching a single entry. With 16 entries per sub-palette, changing the appearance of an entire region is **one register write rather than sixteen**.

Anticipated uses: GUI themes, active/inactive window styling, selection highlighting, flash and fade effects. As with bank switching, the value of this mechanism is that it converts N CPU operations into one, on the constrained resource.

Note that `CLUT_OFFSET` is a candidate target for per-line modification, which is what makes the line-selection approaches in §14 useful.

---

## 10. Output truncation (D5)

`CLUT_CTRL.OUT_DEPTH` selects how many high bits of each stored channel reach the pins. Truncation occurs in the output pad register, after cursor compositing.

| `OUT_DEPTH` | Bits/channel | Output space | Greys |
|---|---|---|---|
| 00 | 8 | 16.7 M | 256 |
| 01 | 6 | 262,144 | 64 |
| 10 | 4 | 4,096 | 16 |

Per `DN-HW-PANELIF-001` §8, the pin boundary carries RGB666, so setting `OUT_DEPTH` = 00 has no effect on current hardware. The field exists so that RGB444 is a software mode rather than a board decision, and so that a future board revision routing RGB888 requires no RTL or ABI change.

Reset value: 01 (6 bits/channel).

---

## 11. Cursor palette (D6)

The hardware cursor holds its own small palette in EBR and is composited in the final stage, by comparing the current (x, y) against cursor position and multiplexing over the framebuffer pixel. Pointer motion therefore costs zero SDRAM bandwidth and zero compositor work.

The cursor palette has **its own independently addressed write port** (`CURSOR_CLUT_*`). Sharing the main CLUT port would put pointer shape changes into contention with palette loads, which is precisely the traffic this design is trying to avoid.

Cursor palette entry count is an open item pending cursor bit depth selection (§15, O2).

---

## 12. Superseded mechanism

A single-bank CLUT with a deferred-write buffer flushed at VSYNC was considered and is **withdrawn**.

Rationale: to defer a full palette load, the pending-write buffer must hold 768 bytes — exactly one CLUT. The EBR cost is therefore identical to dual banking, but with two disadvantages: an additional copy state machine, and 256 cycles of flush activity during blanking. Dual banking makes the buffer *be* the table, giving an atomic switch with less logic and a better result.

Recorded here so that the deferred-write approach is not reintroduced later as an apparent improvement.

---

## 13. Line comparison and interrupts (D7)

| Register | Function |
|---|---|
| `LINE_COMPARE` | Logical scanline number at which an interrupt is raised |
| `LINE_CURRENT` | Live logical scanline, readable at any time |

`VID_INT_ENABLE` and `VID_INT_STATUS` bits:

| Bit | Source |
|---|---|
| [0] | VBLANK start |
| [1] | `LINE_CURRENT` == `LINE_COMPARE` |
| [15:2] | Reserved |

`VID_INT_STATUS` is write-1-to-clear.

Line numbers are **logical**, matching the framebuffer, not panel scanlines. With vertical duplication active, logical line *n* corresponds to panel lines 2*n* and 2*n*+1; the interrupt is raised at the start of the first.

### 13.1 What the CPU can and cannot do with this

The following analysis is recorded because it constrains what software may attempt.

For 1920×1080 @ 60 Hz (2200 × 1125 at 148.5 MHz), against a 65816 at 8 MHz:

| Interval | Duration | 65816 cycles |
|---|---|---|
| Full line | 14.81 µs | ~118 |
| Active region | 12.93 µs | ~103 |
| **HBLANK** | **1.89 µs** | **~15** |
| VBLANK (45 lines) | 666 µs | **~5,330** |

For 1366×768 @ 60 Hz the HBLANK figure lands between roughly 15 and 20 cycles depending on the panel's htotal. The conclusion is unchanged.

The 65816 interrupt sequence consumes 7–8 cycles acknowledging the IRQ, pushing PB/PC/P, and fetching the vector. Adding A/X/Y preservation exhausts the HBLANK budget **before the first useful instruction executes**.

**Therefore: per-line palette modification by the CPU is not possible.** This is not a matter of optimising the handler; the time budget does not exist. The comparison is against a 148.5 MHz beam and an 8 MHz CPU — a ratio roughly forty times worse than the 8-bit machines whose raster effects motivate the technique. The Amiga achieved it with the Copper, a dedicated beam-synchronous coprocessor, not the 68000.

The VBLANK interrupt, at ~5,330 cycles, is comfortable for a full palette reload — and with dual banking (§8) even that urgency is removed.

`LINE_COMPARE` remains useful in Rev A for beam synchronisation, tear-free update scheduling, and knowing where the beam is. It is implemented for those reasons, not for raster effects.

---

## 14. Reserved: display list processor (D8)

Register offsets +0x20 to +0x2F and one SDRAM arbiter client slot are **reserved and not implemented** in Rev A.

The intended future mechanism is a Copper-class state machine within NEON, reading a display list of (line, register, value) entries from NEON SDRAM and executing them during HBLANK **at NEON's clock rate rather than the CPU's**. At 74.25 MHz, the 1.89 µs HBLANK provides approximately 140 cycles — room for dozens of register writes per line.

Its value extends beyond palette effects: a display list processor can write any NEON register per line — framebuffer pointers for smooth scrolling and parallax, beam-synchronised blitter triggers, mid-screen mode changes. More importantly for this project, **it removes work from CPU command emission**, which is the binding constraint.

Estimated cost: on the order of a few hundred LUT4 (pointer, line comparator, fetch state machine, arbiter port). Against the current NEON estimate of 3,300–3,900 against the HX8K ceiling of 7,680 this is affordable but not negligible. SDRAM bandwidth consumed is a few hundred bytes per frame — immaterial.

### 14.1 Why reserved rather than implemented

It is a complete subsystem, not on the critical path to either the Apple II or Amiga milestone, and implementing it now would open exactly the kind of parallel track the single-thread plan deliberately rejects.

What Rev A **does** commit to is not closing the door: leaving the register offsets vacant and provisioning the arbiter client. Adding the processor later with those reservations in place is local work; adding it without them requires reopening a register contract that software will by then depend on.

### 14.2 Alternative under consideration: per-line control table

A cheaper mechanism was identified and is left **open**: a small EBR table indexed by logical scanline, holding 4 bits per line encoding bank selection plus a sub-palette offset. At 540 logical lines this is 2,160 bits — one EBR block configured as 1024×4 — plus one read per line and a multiplexer in the existing index path.

It covers split screens, horizontal bands, and zone tinting, but not per-line distinct colours.

This approach is worth implementing **only if the display list processor is ruled out permanently**, since the latter subsumes it. No decision is required in Rev A.

---

## 15. Open items

| # | Item | Blocks |
|---|---|---|
| O1 | CLUT block base address, pending reconciliation with the existing NEON register map | Register map finalisation, all software |
| O2 | Cursor bit depth and palette entry count | §11 sizing, cursor EBR budget |
| O3 | Syscall ABI wrapper for palette operations (COP signature bytes, Calypsi calling convention) | Kernel colour API |
| O4 | Palette manager design in the OS: allocation policy, system colour reservation, per-application arbitration | GUI colour API, G-series gates |
| O5 | Confirmation of iCE40 EBR read timing at 74.25 MHz for the CLUT lookup path | Timing closure |

---

## 16. Abandonment conditions

| Condition | Action |
|---|---|
| NEON LUT4 utilisation exceeds the HX8K ceiling and dual banking is on the critical path | Drop to a single bank and reinstate the deferred-write buffer from §12, accepting the copy state machine. Do not drop bank switching in favour of per-entry immediate writes. |
| The display list processor is formally ruled out at any point | Close §14, remove the reservation, and evaluate the per-line control table of §14.2 on its own merits. |
| EBR pressure from the line buffer, blitter FIFOs, and command queue leaves fewer than 3 blocks for the CLUT | Reduce to 18-bit storage (2 blocks, dual bank) and record the resulting ABI restriction explicitly. This breaks D2 and must be a documented revision, not a silent change. |
| `LINE_COMPARE` proves unused after the GUI gates close | Retain. Its cost is one comparator and it is a diagnostic asset during bring-up. |

---

## 17. Cross-references

- `DN-HW-PANELIF-001` — physical panel interface, output depth routing, scanout bandwidth
- NEON register map (document number pending) — base address allocation
- `DN-SYS-LIFECYCLE-001` — component lifecycle register (pending)
