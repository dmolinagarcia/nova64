# DN-HW-ECIF-001 — Embedded Controller Interface Contract

**Project:** noVa64 (DANI-65816)
**Domain:** Hardware / System Architecture
**Revision:** B (Revision A withdrawn — see §12)
**Status:** Decisions closed; open items in §11
**Related:** DN-HW-DEBUGPORT-001, REV C power architecture, DN-FS-VFS-001 (pending)

---

## 1. Purpose and scope

This note defines the interface between the Embedded Controller (EC, RP2354B) and
the rest of noVa64 on the **target board**. It is an input to KiCad schematic
capture and to EC firmware development.

It covers every signal crossing the EC boundary, the boot state machine, FPGA
configuration, the SPI mailbox, the Debug Agent transport, EC flash layout, and
the field update mechanism.

### 1.1 Explicitly out of scope: the prototype

The Colorlight i9 v7.2 prototype is programmed from a PC by whatever path is most
practical. **The EC is not in the prototype's configuration path.** If an EC is
present on the prototype carrier at all, FPGA configuration does not depend on
it.

This is a deliberate decoupling. The prototype uses an ECP5; the target board
uses iCE40; their configuration mechanisms differ irreducibly (§4.1). Attempting
to share a configuration path between them imports prototype constraints into
production firmware for no benefit.

### 1.2 Not covered here

Power rail topology (REV C), the Helium↔NEON interface, the internal architecture
of the Debug Agent, and the embedded debug probe (DN-HW-DEBUGPORT-001).

---

## 2. Architectural model

The EC is **not** a bus master on the 65816 bus. It never drives address, data,
or control lines of the CPU bus. All EC-originated data reaches system memory
through Helium.

Three planes cross the EC boundary:

| Plane | Direction | Character |
|---|---|---|
| Data | bidirectional | SPI mailbox, runtime |
| Control / configuration | EC → system | SPI config, resets, straps; boot-time |
| Power | EC → system | rail enables; EC owns sequencing |

The claim "the SPI link is the only connection between the EC and noVa64" holds
**only in the data plane**. It is false in the other two, and this note does not
attempt to make it true there.

### 2.1 The EC can write system memory by design

`bios.bin` must reach system RAM before the 65816 leaves reset, and no other
agent can place it there. EC write access to system memory is therefore a
structural necessity, not a debug affordance.

A `DEBUG_ENABLE` strap gating debug access to memory would protect nothing.
**No such strap is implemented.**

### 2.2 The EC holds the machine's identity

With bitstreams and BIOS resident in EC flash (§6), the EC's in-package flash
contains the gateware for all three FPGAs and the system firmware. It is the most
valuable single object in the machine. §7 and §8 are written accordingly.

---

## 3. Boundary signal table

### 3.1 Shared SPI bus

Configuration and runtime data share one physical SPI bus, distinguished by chip
select. This is possible because iCE40 configuration pins revert to user I/O
after `CDONE`.

| Signal | Direction | Purpose |
|---|---|---|
| `SPI_SCK` | EC → all | Shared clock |
| `SPI_MOSI` | EC → all | Shared data out |
| `SPI_MISO` | Helium → EC | Driven only by Helium; unused during configuration |
| `CFG_SS_A/B/C` | EC → each FPGA | Configuration select |
| `MBX_CSN` | EC → Helium | Mailbox slave select |
| `DBG_CSN` | EC → Helium | Debug Agent slave select |
| `CRESET_B[A..C]` | EC → each FPGA | Configuration reset |
| `CDONE[A..C]` | each FPGA → EC | Configuration complete |
| `HELIUM_ATTN` | Helium → EC | Attention line (§5.5) |

**EC pins: 15.**

Two conditions govern bus sharing:

**Tri-state discipline.** Once Helium is configured, its SPI pins are user I/O.
Helium's gateware must hold them high-impedance whenever `MBX_CSN` and `DBG_CSN`
are both inactive, so that the EC can configure FPGA-B and FPGA-C on the same
wires.

**Post-configuration window.** The iCE40 datasheet specifies 49 clock cycles
after `CDONE` rises before user I/O is active. The EC must not begin another
device's configuration inside that window.

### 3.2 System control

| Signal | Direction | Notes |
|---|---|---|
| `RESB` | EC → W65C816S | Released last |
| `EN_1V2`, `EN_3V3_MAIN`, `EN_BL`, `EN_5V_HOST` | EC → regulators | REV C sequencing |
| `PG_1V2`, `PG_3V3_MAIN` | regulators → EC | Power-good, waited on at `S1_POWER` |
| `UART_TX`, `UART_RX` | EC ↔ embedded probe | Console (§9) |
| `SWDIO`, `SWCLK` | embedded probe → EC | Programming and recovery |
| `RUN` | embedded probe → EC | Reset control for reliable attach |

### 3.3 SD card

The SD card connects **permanently and exclusively to Helium**. The EC has no
connection to it. There is no multiplexer, no ownership handoff, and no SD driver
in EC firmware.

This follows from §6: the EC no longer needs the card for anything.

### 3.4 EC pin budget

| Group | Pins |
|---|---|
| Shared SPI bus, chip selects, CRESET_B, CDONE, ATTN | 15 |
| `RESB` | 1 |
| Rail enables | 4 |
| Power-good returns | 2 |
| UART | 2 |
| SWD + RUN (from probe) | 3 |
| **Subtotal (this note)** | **27** |

USB host and the hub's reset line, touch I2C, battery I2C, and backlight PWM are
outside this note and consume further GPIO from the RP2354B's 48. **There is no
keyboard matrix** — the keyboard is a USB HID device behind the on-board hub, and
the matrix was removed from the design along with PS/2.

### 3.5 Helium pin budget impact

| Group | Pins |
|---|---|
| SPI (SCK, MOSI, MISO — shared with config) | 3 |
| `MBX_CSN`, `DBG_CSN` | 2 |
| `HELIUM_ATTN` | 1 |
| SD (permanent) | 6 |
| **Total charged to Helium user I/O** | **12** |

Additive to the CPU bus, SRAM, SDRAM, and the 32-signal multiplexed NEON link. A
formal TQ144 pin count is required before schematic capture (§11).

**Possible optimisation, not adopted:** `SPI_SS_B` (the configuration select) and
`MBX_CSN` could be the same pin, since configuration and mailbox use are disjoint
in time. This would save one pin on each side. It is noted rather than adopted
because it makes the bring-up failure modes harder to reason about; revisit only
if the pin count in §11 comes out short.

---

## 4. FPGA configuration

### 4.1 Mechanism: SPI slave

All three FPGAs are configured by the EC in **SPI slave mode**. The EC is the
only master on the bus.

**JTAG is not available.** The iCE40 LP/HX family has no JTAG interface at all —
no TAP, no boundary scan, no IDCODE. Configuration modes are SPI slave, SPI
master, and one-time-programmable NVCM. Headers labelled "JTAG" on some iCE40
evaluation boards are documentation residue and are not connected to a JTAG port.

Consequences accepted:

- No IDCODE scan. Device presence and identity cannot be verified electrically.
- No boundary scan. Solder joints cannot be tested before functional gateware
  exists.
- `CDONE` is the only configuration status indication available.

### 4.2 Sequence per device

1. Assert `CRESET_B` (minimum 200 ns low pulse per datasheet)
2. Release `CRESET_B` with `CFG_SS` asserted to select slave mode
3. Wait for internal configuration memory clear (1200 µs for LP/HX8K)
4. Clock the bitstream in
5. Wait for `CDONE` to rise
6. Clock 49 further cycles before user I/O is valid

### 4.3 Device order

**NEON (FPGA-B) is configured first**, then Helium (FPGA-A), then FPGA-C if
populated.

The ordering is a requirement, not a preference. NEON's text mode lives entirely
in its own block RAM and is initialised from the bitstream, so a correct screen
exists the moment `CDONE_B` rises — with Helium unconfigured, no SDRAM, no
arbiter and the 65816 still in reset. Configuring it first makes every
subsequent state of §7 reportable **on the panel** as it happens: Helium
configuring, `bios.bin` transferring, the CPU released. Configuring it second
would leave the whole of boot to be inferred from a dark screen and a serial
line.

The only constraint pointing the other way is that Helium must be alive before
`MEM_WRITE` begins, and Helium is still configured well before that.

FPGA-C is unpopulated initially. Because configuration is by individual chip
select rather than a chain, its absence has no effect on the others. Its `CDONE`
is simply never observed. No jumper or footprint provision is required.

### 4.4 NVCM and dedicated flash: rejected

**NVCM** is one-time-programmable — closer to fuses than to flash. A device
programmed with NVCM is fixed with that bitstream permanently. Unsuitable for a
design that will iterate. Open-toolchain support is additionally uncertain.

**Dedicated SPI flash per FPGA** (master-mode self-configuration) was evaluated.
Rejected: it adds three flash devices and their routing to solve a problem that
EC in-package flash already solves at zero BOM cost (§6). A shared flash in
master mode across three devices was also considered and rejected — it depends on
the `CBSEL` cold-boot multi-image feature, which is a less-travelled path, and it
requires the same staggered `CRESET_B` release as the EC-mastered approach
without removing any complexity.

---

## 5. SPI mailbox

### 5.1 Topology

EC is master. Helium presents two independent slaves, selected by `MBX_CSN` and
`DBG_CSN`, sharing no state. Both are internal to Helium, so `SPI_MISO` is driven
by a single device and there is no external bus contention.

EC mastership is chosen for consistency — the EC is already the configuration
master and owns boot sequencing — and because a slave SPI implementation in
Helium is substantially cheaper in LUT4 than a master with arbitration.

### 5.2 Traffic profile

| Direction | Content | Volume |
|---|---|---|
| EC → Helium | `MEM_WRITE` at boot, HID events at runtime | Majority |
| Helium → EC | Keyboard LED state, modifier state, status | Minority |
| Helium → EC | Firmware update payload (§8) | Occasional, bulk |

### 5.3 Full-duplex exploitation

SPI transfers a byte in each direction per transaction, so Helium's outbound
buffer is drained for free by any inbound traffic. An explicit drain command is
needed only when the EC is otherwise idle; a 1 kHz poll is ample for LED state.

### 5.4 Status byte

The first byte Helium returns in every transaction occupies the MISO slot that
would otherwise be wasted:

| Bits | Field | Meaning |
|---|---|---|
| 7 | `OUT_PENDING` | Outbound buffer non-empty |
| 6 | `ERR` | Last command rejected |
| 5:4 | reserved | |
| 3:0 | `IN_SPACE` | Free slots in inbound FIFO |

`IN_SPACE` provides flow control. The 65816 at 8 MHz servicing IRQs can fall
behind a burst of HID events; the EC throttles rather than overrunning.

### 5.5 Attention line

`HELIUM_ATTN` lets Helium request service without waiting for a poll. Given §5.3
it is expected to be largely unnecessary. **It is routed anyway**: the pin costs
one, and discovering it was needed after fabrication costs a board spin.

### 5.6 Command classes

| Command | Direction | Phase |
|---|---|---|
| `MEM_WRITE` | EC → Helium | boot |
| `HID_EVENT` | EC → Helium | runtime |
| `HID_STATE` | Helium → EC | runtime |
| `FLASH_BEGIN` / `FLASH_DATA` / `FLASH_COMMIT` | Helium → EC | update (§8) |
| `STATUS` | Helium → EC | implicit in every transaction |

### 5.7 Inbound event delivery

HID events are buffered in Helium and signalled to the 65816 by IRQ. One IRQ
covers the queue; Helium does not interrupt per event.

---

## 6. EC flash as the system's configuration store

### 6.1 Rationale

Three iCE40 HX8K bitstreams are approximately 135 KB each — about 405 KB total —
against 2 MB of in-package flash on the RP2354B. Together with `bios.bin` this
fits comfortably alongside EC firmware.

Bitstream size is fixed by the device's configuration SRAM and does not scale
with design utilisation. A trivial design produces a nearly identically sized
bitstream to a full one. Compression helps, and helps most on sparse designs, but
the uncompressed figure is the one to budget against.

### 6.2 What this eliminates

- The SD multiplexer, its analogue switch, and its six EC pins
- The SD ownership handoff, its atomicity requirements, and its state machine
- The SD driver and filesystem in EC firmware
- The SD card from the critical boot path

### 6.3 What this gains

The system boots with no SD card present. Because NEON is configured from EC
flash, a missing or unreadable card can be reported **on the display** rather
than only on a serial line.

### 6.4 Proposed layout

| Region | Size |
|---|---|
| Bootloader / slot selector | 32 KB |
| Slot A: EC firmware + 3 bitstreams + `bios.bin` | ~600 KB |
| Slot B: same | ~600 KB |
| Configuration storage | 64 KB |
| Spare | ~700 KB |

Sizes are provisional pending measurement (§11).

---

## 7. Boot sequence

### 7.1 State machine

| State | Action | Exit |
|---|---|---|
| `S0_POR` | EC boots from in-package flash; bootrom selects active slot. All rails except 3V3_AON off. `CRESET_B` asserted on all FPGAs. `RESB` asserted. | EC firmware running |
| `S1_POWER` | Sequence rails per REV C: 1V2, then 3V3_MAIN, respecting iCE40 VCC-before-VCCIO/VPP ordering | Rails stable |
| `S2_CFG_NEON` | Configure NEON (FPGA-B) from EC flash via SPI slave. **First — from `CDONE_B` onward every state below is reportable on the panel** (§4.3) | `CDONE_B`, or → `S_FAIL` |
| `S3_CFG_HELIUM` | Configure Helium (FPGA-A) | `CDONE_A`, or → `S_FAIL` |
| `S4_CFG_C` | Configure FPGA-C if populated; skip on timeout | `CDONE_C` or skipped |
| `S5_BIOS` | Transfer `bios.bin` from EC flash to system memory via `MEM_WRITE`; verify | Transfer verified |
| `S6_RELEASE` | Release `RESB` | CPU running |
| `S7_RUNTIME` | Service HID, console, power management, update requests | — |
| `S_FAIL` | Report on UART and status LED; halt | Reset only |

### 7.2 Notes

**No circular dependency.** Revision A of this note documented an ordering
constraint in which the EC had to retain SD ownership past FPGA configuration in
order to read `bios.bin`. With both bitstreams and BIOS in EC flash, that
constraint does not exist.

**Boot time.** Three 135 KB bitstreams over SPI at 25 MHz is on the order of 50 ms
of clocking plus per-device setup. Boot in the low hundreds of milliseconds is
expected. Seconds were accepted as tolerable; this is comfortably inside that.

**Failure reporting.** Only a failure at `S1_POWER` or `S2_CFG_NEON` leaves no
display; there `S_FAIL` has the debug UART and a distinguishable LED blink pattern
and nothing else. **Every failure from `S3_CFG_HELIUM` onward is reportable on the
panel**, which is the whole argument for the configuration order of §4.3 — a
machine that cannot configure Helium, or cannot verify `bios.bin`, says so in text
on its own screen rather than being diagnosed from a dark one.

**SD absence is not a boot failure.** The card is Helium's concern and is
discovered by the running system, not by the EC.

---

## 8. Field update

### 8.1 Status

Optional. The primary programming path throughout development is
**USB → embedded debug probe → SWD**. Self-update is treated as an exercise in
making the machine self-sustaining, not as a requirement.

### 8.2 Mechanism

noVa64 reads a new image from the SD card, and pushes it to the EC over the
mailbox in the Helium → EC direction:

| Command | Purpose |
|---|---|
| `FLASH_BEGIN` | Declare target slot and total length |
| `FLASH_DATA` | Payload chunk |
| `FLASH_COMMIT` | CRC verification and slot activation |

Throughput is poor — hundreds of KB through the mailbox with the 65816 emitting
commands at 8 MHz will take minutes. Acceptable for a firmware update.

### 8.3 Writing RP2354B flash from within

The pico-SDK provides `flash_range_erase()` and `flash_range_program()`. Erase
granularity is 4 KB, program granularity 256 B. Endurance is roughly 100k erase
cycles per sector — irrelevant at update frequencies.

The governing constraint is XIP: the core executes from flash and cannot execute
flash-resident code while erasing. The writer must be marked
`__not_in_flash_func()`, interrupts must be disabled (handlers are flash-resident
too), and core 1 must be parked via `multicore_lockout`.

**During a sector erase — 30 to 50 ms — the EC is deaf.** No HID service, no
mailbox polling, no response of any kind. This must be an explicit designed
state, with the running system aware that input will stall.

### 8.4 A/B slots

Two complete slots, written inactive-then-switch. **EC firmware is included in
the scheme.** An earlier revision excluded it on the grounds that a failed update
would require opening the chassis; the embedded debug probe removes that
objection (§8.5).

The RP2350 bootrom provides partition tables with A/B image support and version
numbering. If it behaves as expected, the slot selector need not be written by
hand. **This must be verified against Raspberry Pi documentation before being
relied upon.**

### 8.5 Recovery

A bricked EC is recovered by plugging a USB cable into the chassis debug port.
SWD attaches to the core directly and works against completely blank flash; the
RP2350 additionally provides a rescue DP for a device that resets itself. The
probe is powered from the debug port's VBUS and therefore operates with the
machine unpowered.

Three conditions must hold, and all are load-bearing:

- **The OTP debug-disable bits must never be burned.** Irreversible, and closes
  the only recovery path.
- **The probe is powered from debug-port VBUS**, with backfeed protection toward
  an unpowered RP2354B.
- **The probe's own BOOTSEL is externally accessible** (pinhole). This is the
  recovery path for the recovery path.

See DN-HW-DEBUGPORT-001.

---

## 9. EC console

### 9.1 One subsystem, two modes

The `S_FAIL` reporting of §7 and the interactive monitor are the same subsystem:
a boot mode for status and failure reporting, and a monitor mode for interaction.

### 9.2 Human debug path

```
PC → USB → embedded probe (RP2040) → UART → EC (RP2354B)
   → SPI (DBG_CSN) → Helium Debug Agent
```

The EC is an **interpreter**, not a transparent bridge. It parses text commands,
issues Debug Agent transactions, and formats results as text. The human never
speaks to Helium directly.

### 9.3 Consequences

**No framing protocol on the UART.** Text in, text out. Console and Debug Agent
share one channel in one format — no SLIP, no COBS, no escaping.

**The SPI-side protocol is unconstrained in form.** No human reads it: fixed
opcode, address, length, no delimiters.

**EC firmware grows.** Parser, command table, hex formatting, help. Several KB of
code. The payoff is that bring-up needs only a serial terminal.

**Bulk transfers are a special case.** 64 KB in ASCII hex is ~200 KB on the wire,
about 2 s at 1 Mbaud. If bulk dumps become routine, add a binary or XMODEM
command rather than reworking the protocol. Raising the UART to 3 Mbaud is viable
over short traces and is recommended.

### 9.4 Initial command set

A wozmon-class monitor:

| Command | Purpose |
|---|---|
| `md <bank:addr> <len>` | Memory dump |
| `mw <bank:addr> <bytes>` | Memory write |
| `halt` / `run` | CPU control |
| `regs` | Register dump |
| `stat` | Rails, battery, `CDONE` status |
| `slot` | Active flash slot and version |

### 9.5 Forward path

GDB Remote Serial Protocol is also text over the same channel. Source-level
debugging of Calypsi-compiled code needs no hardware change. Noted as a
direction, not a commitment.

---

## 10. Debug availability model

All debug access to noVa64 passes through EC firmware. SWD reaches the EC's
Cortex-M33 core, not Helium; reaching the Debug Agent by that route still
requires EC firmware to be running.

**If EC firmware hangs, access to Helium, to system memory, and to the console
are lost simultaneously.** This is accepted — it matches essentially any modern
laptop. Mitigation is discipline: the EC's command bridge must be simple,
non-blocking, and watchdogged. A per-transaction watchdog on the Debug Agent side
remains specified.

Note the distinction from §8.5: a *hung* EC loses debug access to the system but
is still recoverable via SWD; a *bricked* EC is likewise recoverable. The
unrecoverable case requires both blank flash and burned OTP debug-disable bits,
which is why §8.5 lists that as a hard prohibition.

---

## 11. Open items

| # | Item | Blocks |
|---|---|---|
| 1 | Formal TQ144 pin count for Helium: the 12 pins of §3.5 plus CPU bus, SRAM, SDRAM, and the 32-signal NEON link. If short, fall back to HX8K in CT256 (changes footprint) or adopt the §3.5 chip-select merge. | Schematic capture |
| 2 | Measured bitstream size from a real `icepack` run on a representative HX8K design, and measured `bios.bin` size | §6.4 flash layout |
| 3 | Verification of RP2350 bootrom partition-table A/B support against Raspberry Pi documentation | §8.4 |
| 4 | Reconciliation of the W25Q16JV external flash in the programming tiers, believed to be residue of the RP2040-era revision | BOM |
| 5 | Reassessment of the RAM flash-writer stub as a pre-E0 deliverable. With SWD working against blank flash, the bootrom UART path is diagnostic rather than rescue, and the stub's priority drops. | Roadmap |
| 6 | Monitor command set beyond §9.4 | EC firmware |

---

## 12. Revision history

**Revision A — withdrawn.** Specified JTAG cascade configuration with IDCODE
chain discovery, and bitstream storage on SD card with a physical multiplexer and
one-way ownership handoff. The JTAG portion was invalid: the iCE40 LP/HX family
has no JTAG interface. The SD portion was superseded once the flash budget showed
that three iCE40 bitstreams fit in the RP2354B's in-package flash alongside EC
firmware.

**Revision B.1 — editorial corrections to Revision B**, made while reconciling
this note against the synthesis document. Three, none of them reopening a
decision: the configuration order is **NEON first** (§4.3, §7.1), which the
synthesis document holds as a requirement rather than a preference and which this
note had inverted; the boundary table gains the two `PG` returns REV C requires,
taking §3.4's subtotal from 25 to 27 (§3.2, §3.4); and the reference to a keyboard
matrix in §3.4 is removed — there is none, the keyboard is USB HID.

**Revision B — this document.** SPI slave configuration. Bitstreams and
`bios.bin` in EC in-package flash. SD card permanently owned by Helium. No
multiplexer, no handoff. A/B update covering EC firmware. Prototype explicitly
out of scope.

---

## 13. Decisions closed

| Decision | Resolution |
|---|---|
| EC as bus master | No. Data reaches the system only through Helium. |
| Prototype configuration path | Out of scope. PC-direct, EC not involved. |
| FPGA configuration mechanism | SPI slave, EC as sole master. JTAG unavailable on iCE40. |
| Configuration bus | Shared with mailbox; distinguished by chip select. |
| Bitstream and BIOS storage | EC in-package flash. |
| SD card ownership | Helium, permanently. No mux, no handoff. |
| `DEBUG_ENABLE` strap | Not implemented; it would protect nothing. |
| `DBG_CSN` | Implemented. Debug Agent is a separate SPI slave inside Helium. |
| Human debug entry point | UART to EC; EC interprets. Never direct to Helium. |
| Field update | Optional. A/B slots including EC firmware. |
| Primary programming path | USB → embedded probe → SWD. |
| NVCM, per-FPGA flash | Rejected. |
