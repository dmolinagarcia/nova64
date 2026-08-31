# DN-HW-USBDBG-002 — USB Topology, On-Board Hub, and Programming Paths

**Project:** noVa64 (DANI-65816)
**Domain:** Hardware
**Subject:** USB host topology with integrated hub, debug interface, and firmware programming paths
**Revision:** 002 (draft 2 — supersedes 001)
**Status:** Input to KiCad schematic capture, Stage B carrier PCB
**Depends on:** DN-HW-MCU-001, REV C power architecture
**Related:** DN-HW-CPUADAPT-001, DN-HW-PIO-BUDGET-001

---

## 0. Changes from draft 1

| Change | Reason |
|--------|--------|
| **USB BOOTSEL removed as a recovery path** | Structurally unavailable. The USB-A connector sources VBUS as a host; connecting a PC would require an A-to-A cable with two 5 V sources opposing each other |
| Recovery restructured into three USB-independent tiers | SWD (incl. rescue DP) → bootrom UART → direct QSPI |
| Single 8-pin `J-QSPI` header introduced | Serves both bootrom UART boot and direct SPI flash programming |
| **On-board USB hub IC added** (§7) | Multiple USB-A receptacles without an external dongle |
| Host D+/D- bypass jumper added | Separates "host stack works" from "hub works" during bring-up |
| New open items OI-10 through OI-14 | Hub cascade depth, hub IC selection, per-port power, bypass strategy, RAM flash-writer stub |

---

## 1. Scope

Fixes the USB port topology including an on-board hub, the debug and programming
interfaces, boot strapping circuits, and the associated connector inventory for
the noVa64 embedded controller (EC).

Out of scope: HID event transport to Helium, internal keyboard matrix geometry,
FPGA configuration path. See DN-HW-MCU-001 and DN-HW-PIO-BUDGET-001.

---

## 2. Closed decisions

| ID | Decision | Rationale |
|----|----------|-----------|
| D-1 | EC is a single RP2354B (A4 stepping), QFN-80 | 48 GPIO removes the pin pressure that motivated a second MCU; in-package 2 MB flash removes a component from the AON domain; RP2354 never shipped as A2 |
| D-2 | Native USB controller assigned to **host** mode | Hardware SIE handles hub enumeration, PRE packets for Low Speed devices, and bulk transfers in silicon |
| D-3 | **No second USB port.** Debug console is UART | Removes the unverified PIO-USB stack from the critical path |
| D-4 | Programming is **SWD-primary** | Breakpoints and register visibility are required for power-sequencer development |
| D-5 | No FT2232H on the carrier PCB | ~5× the cost of the main MCU, closed silicon; its only strong justification (ECP5 JTAG) is better served by a separate dongle |
| D-6 | USB-C connector carries **power only** | VBUS + CC1/CC2 to CH224K. D+/D- unconnected |
| D-7 | **On-board USB hub IC** feeding 2–3 USB-A receptacles | A portable computer needs multiple ports without a dongle; a known-good on-board hub also removes a variability source during bring-up |
| D-8 | **BOOTSEL USB is not a supported path** | See §0. Recovery is covered by three USB-independent tiers |

---

## 3. Topology

```
        RP2354B  (native USB controller, host mode)
             |
             |  D+ / D-
             |
        [ JP-BYPASS ]  --------------------------------+
             |                                          |
             |  (normal path)                           |  (bring-up path)
             v                                          |
    ==================================                  |
    |  USB hub IC (self-powered)     |                  |
    |  upstream: Full Speed          |                  |
    ==================================                  |
        |        |        |                             |
        v        v        v                             v
    USB-A #1  USB-A #2  internal header   ----------> USB-A #1
                        (expansion)                  (hub bypassed)
```

The hub's upstream port receives no response to its Hi-Speed chirp from the
Full Speed host, so it reverts to Full Speed operation and behaves as a pure
repeater with the Transaction Translator bypassed. This is specification-mandated
behaviour and is exactly what happens when a USB 2.0 hub is plugged into a
USB 1.1 port.

### 3.1 Bus constraints

- Full Speed only, 12 Mbps, shared across every attached device.
- A Full Speed hub is a repeater, not a switch: downstream traffic is
  retransmitted to every enabled port, and only the device matching the token's
  ADDR field responds.
- Low Speed devices behind a hub require the host to emit a PRE packet at Full
  Speed before each LS transaction. Supported in RP2354B hardware, but the
  historically most fragile path in the RP2040/RP2350 host stack. **Must be
  verified early.**
- Interrupt endpoint concurrency is bounded by the controller's 15 interrupt
  endpoint registers.

### 3.2 The cascade problem

The on-board hub consumes the single supported hub level. Any hub the user plugs
into a downstream USB-A creates a second level.

In a portable computer this **will** happen. `CFG_TUH_HUB` must be raised to at
least 2, and multi-level hub handling in TinyUSB must be verified rather than
assumed — it has historically been weak. See OI-10.

If cascading proves unreliable, the fallback is to detect a downstream hub during
enumeration and report it as unsupported over the console, rather than failing
silently.

### 3.3 Bypass jumper

`JP-BYPASS` places a series jumper on D+/D- ahead of the hub, with an alternate
route directly to USB-A #1.

At 12 Mbps Full Speed, signal integrity tolerates a jumper without difficulty.
The value is diagnostic: during bring-up it separates *"does the host stack work
at all"* from *"does the hub work"*. Without it, a board that enumerates nothing
gives no information about which of the two is at fault.

Implement as a 3-pin header with a shunt, or two 0-ohm pads with one populated.
May be replaced by a hard connection on later revisions.

---

## 4. Connector inventory

| Ref | Type | Function | Signals |
|-----|------|----------|---------|
| J-PWR | USB-C receptacle | PD sink input | VBUS, CC1, CC2, GND. D+/D- not connected |
| J-HOST1 | USB-A receptacle | Downstream port 1 | VBUS_P1, D+, D-, GND |
| J-HOST2 | USB-A receptacle | Downstream port 2 | VBUS_P2, D+, D-, GND |
| J-EXP | 4-pin internal header | Downstream port 3, expansion | 5V, D+, D-, GND |
| J-DBG | 5-pin header, 2.54 mm | SWD + UART console | SWCLK, SWDIO, GND, UART_TX, UART_RX |
| **J-QSPI** | 8-pin header | Bootrom UART + direct flash | QSPI_SS, SCLK, SD0, SD1, SD2, SD3, GND, 3V3 |
| JP-BYPASS | 3-pin jumper | Hub bypass | D+/D- routing select |
| SW-RUN | Pushbutton | EC reset | RUN to GND |
| SW-BOOT | Pushbutton | BOOTSEL entry (strap) | QSPI_SS to GND via 1 kohm |
| SW-UBOOT | Pushbutton or pads | UART boot select | QSPI_SD1 to 3V3 via 1 kohm |

A single Raspberry Pi Debug Probe services J-DBG completely: SWD programming and
debugging plus a UART bridge, over one cable.

**Silkscreen the pin order on J-DBG and J-QSPI.** Transposing SWCLK and SWDIO
during bring-up wastes hours.

---

## 5. Programming and recovery

### 5.1 Path summary

| Tier | Path | Interface | Writes to | Survives |
|------|------|-----------|-----------|----------|
| Daily | SWD | J-DBG | Flash + SRAM | — |
| 1 | SWD rescue DP | J-DBG | Chip reset | Hung firmware, code that reconfigures pins, corrupt flash |
| 2 | Bootrom UART boot | J-QSPI | **SRAM only** | Missing or broken SWD probe |
| 3 | Direct QSPI to flash die | J-QSPI + RUN low | Flash die | Everything short of physical damage |
| — | USB BOOTSEL | *not available* | — | See §0 |

None of these paths depend on the USB port. This is a deliberate property of the
design: the USB port is committed to host mode and cannot be borrowed back.

### 5.2 Tier 0 — SWD, the daily path

Two signals plus ground: SWCLK, SWDIO.

The RP2350 debug architecture exposes multiple access points over multi-drop
SWD: core 0, core 1, and a **rescue debug port**. The rescue DP can reset the
chip even when firmware is actively hostile to debugging — a tight loop, a
routine that reconfigures the debug pins, or a crashed power sequencer.

For an EC whose firmware sequences power rails to FPGAs, this matters more than
usual: a bug in the sequencer must never leave the board unreachable.

Flash programming over SWD works by loading a flash algorithm into SRAM which
then calls the bootrom flash routines. OpenOCD handles this transparently.

```
openocd -f interface/cmsis-dap.cfg \
        -f target/rp2350.cfg \
        -c "adapter speed 5000" \
        -c "program ec-firmware.elf verify reset exit"
```

`picotool` is **not** available over SWD — it speaks PICOBOOT over USB only.
OpenOCD is the tool for this project.

**SWD is disabled permanently if OTP security bits are programmed.** See §9.

### 5.3 Tier 2 — bootrom UART boot

A RP2350-family feature not present on RP2040.

**Entry conditions, checked by the bootrom at reset:**

1. QSPI_SS driven low → enter BOOTSEL
2. QSPI_SD1 driven **high** → select UART boot (low, the default via internal
   pull-down, selects USB boot)

**Interface:** 1 Mbaud UART on **QSPI_SD2 (TX) and QSPI_SD3 (RX)**. These are
flash-interface pins, not GPIO, and are not remappable. This is a different UART
from the application console on J-DBG.

**Critical limitation: the UART bootloader writes only to SRAM.** It cannot
program flash directly.

**Entry sequence:** hold SW-BOOT and SW-UBOOT, pulse SW-RUN, release. Place the
three buttons adjacent and document the sequence on silkscreen.

The bootrom initialises chip select as output-disabled and pulled high, so the
in-package flash stays deselected and does not fight the strap. Because the
bootrom never clocks the flash in this state, the flash data pins remain
high-impedance and do not oppose the SD1 strap.

### 5.4 The RAM flash-writer stub

Required to make tier 2 useful. This is a real deliverable and should exist
**before** it is needed.

- Built with `PICO_NO_FLASH 1` so it links to run entirely from SRAM.
- Receives the payload over the same bootrom UART.
- Writes to flash by calling the bootrom flash functions:
  `connect_internal_flash`, `flash_exit_xip`, `flash_range_erase`,
  `flash_range_program`, `flash_flush_cache`.
- Approximately 200 lines. Verify with a CRC-32 readback — the CRC-32 assembly
  implementation from the NVFS track is reusable here.

Archive the stub binary alongside the EC firmware releases. A recovery tool that
must be rebuilt from source during an emergency is not a recovery tool.

### 5.5 Tier 3 — direct QSPI programming

The in-package flash die on RP2354 is a **Winbond W25Q16JV**, which supports
standard single-bit SPI.

Holding the RP2350 die in reset via RUN (active low) places its QSPI pins in
high impedance, allowing an external programmer to address the flash die
directly. Standard SPI needs only four signals: CS (QSPI_SS), CLK (SCLK),
DI (SD0), DO (SD1) — all present on J-QSPI.

Any SPI flash programmer works: `flashrom` with a CH341A, a Raspberry Pi, or the
FT2232H dongle from §8.3.

This path works because RP2354A/B are pin-identical to RP2350A/B including
pinout, with the QSPI pins bonded out to the package.

### 5.6 Hazard: exposed QSPI nets

The QSPI signals are externally exposed on J-QSPI and simultaneously connected to
the in-package flash die. External noise on these nets can interfere with the
internal flash interface and destabilise the system.

**Layout rules:**

- Keep J-QSPI stubs as short as physically possible.
- Route away from the SDRAM, the backlight boost, and the TPS61023 switching
  node.
- Ground-guard the stubs if board space allows.
- Consider series 33-ohm resistors on the header side to damp reflections from
  the unterminated stub.

---

## 6. Strapping circuits

```
QSPI_SS  ----+---- (in-package flash die CS, internal)
             |
             +---- 1 kohm ---- SW-BOOT ---- GND        [BOOTSEL entry]
             |
             +---- J-QSPI pin 1

QSPI_SD1 ----+---- (internal pull-down: default = USB boot)
             |
             +---- 1 kohm ---- SW-UBOOT ---- 3V3       [UART boot select]
             |
             +---- J-QSPI pin 4

RUN      ----+---- 1 kohm pull-up to 3V3_AON
             |
             +---- SW-RUN ---- GND                     [reset]
```

The 1 kohm series resistor on QSPI_SS is required so the button cannot fight the
RP2354B when it drives chip select during normal flash access. Place it close to
the pin.

SW-UBOOT may be reduced to a pad pair for a temporary wire on the production
board.

---

## 7. On-board hub IC

### 7.1 Candidate parts

| Part | Ports | Package | Cost | Assessment |
|------|-------|---------|------|------------|
| **Microchip USB2514B** | 4 | SQFN-36, 6×6 mm, thermal pad | ~$3 | Full public datasheet, per-port power control (PRTPWR) and overcurrent sense, operates standalone from pin strapping with no firmware. **Recommended** |
| Terminus FE1.1S | 4 | SSOP-28, no thermal pad | ~$0.40 | Trivial hand-assembly. Thin documentation, gang power control only. Legitimate prototype alternative |
| WCH CH334F | 4 | QFN-24 | ~$0.50 | Very cheap; documentation primarily in Chinese |

**Selection rationale:** the USB2514B is the only candidate whose documentation
meets the project's no-black-boxes standard, and per-port power control lets the
EC kill a misbehaving port without cycling the whole bus. The cost delta is
irrelevant against the schedule cost of debugging an undocumented part.

The FE1.1S remains defensible if hand-assembly ease dominates on the prototype
board — the SSOP-28 package with no thermal pad is the easiest of the three to
rework.

### 7.2 Configuration

- Strap as **self-powered**. The hub then declares 500 mA per downstream port in
  its hub descriptor, which a bus-powered hub cannot do.
- 24 MHz crystal (USB2514B). Follow the reference load capacitance exactly;
  crystal error is a common cause of intermittent enumeration.
- `RESET_N` to an EC GPIO. The hub must be powered and out of reset **before**
  the EC begins host enumeration, or the upstream port is missed. Sequence the
  hub reset release in the EC startup code, not with an RC.
- Unused downstream ports: strap as non-removable/disabled per the datasheet
  rather than leaving them floating.
- Default pin-strap configuration is sufficient; the SMBus/I2C configuration
  path is not required and should not be populated.

### 7.3 Downstream power

Per-port current-limit switches (TPS2553 or equivalent), driven by the hub's
PRTPWR outputs, with OCS returned to the hub:

```
5V_USB ---+--- switch P1 (EN <- PRTPWR1, /FAULT -> OCS1) ---> J-HOST1 VBUS
          +--- switch P2 (EN <- PRTPWR2, /FAULT -> OCS2) ---> J-HOST2 VBUS
          +--- switch P3 (EN <- PRTPWR3, /FAULT -> OCS3) ---> J-EXP 5V
```

Each port needs 120 uF of bulk capacitance downstream of its switch per USB
specification, to absorb hot-plug transients.

### 7.4 Current budget

| Load | Typical | Worst case |
|------|---------|------------|
| Hub IC itself | 30 mA | 50 mA |
| Port 1 (keyboard) | 50 mA | 500 mA |
| Port 2 (mouse) | 50 mA | 500 mA |
| Port 3 (expansion) | 0 mA | 500 mA |
| **Total at 5 V** | **130 mA (0.65 W)** | **1550 mA (7.75 W)** |

7.75 W from a 1S battery is not realistic for a portable. **Policy decision
required (OI-12):** either

- **(a)** limit the aggregate to ~1 A with a single upstream current-limit
  switch ahead of the hub, letting per-port switches handle faults only; or
- **(b)** enforce a software policy in the EC: read `bMaxPower` from each device's
  configuration descriptor during enumeration, maintain a running total, and
  refuse `SET_CONFIGURATION` for a device that would exceed the budget,
  reporting the refusal over the console.

**(b) is preferable** — it is the honest behaviour, it is visible to the user,
and it is exactly the kind of policy an EC should own. (a) is the fallback if
the boost converter cannot support the transient.

Verify the TPS61023 output capability at the chosen aggregate limit with
Vin = 3.4 V (low battery) before schematic freeze.

`VBUS_EN` for the whole USB subsystem becomes an additional switched rail in the
REV C sequence. The hub and all ports stay dead until the system is up.

### 7.5 Signal integrity

- 27 ohm series resistors on the RP2354B D+/D- pair, close to the pin.
- 90 ohm differential routing, matched length, on all four pairs (upstream plus
  three downstream).
- ESD protection on every downstream D+/D- pair and VBUS — user-accessible
  connectors.
- Host-mode 15 kohm pull-downs are internal to the RP2354B; no external parts.

---

## 8. Rejected alternatives

### 8.1 Native = device, PIO = host

Preserves UF2 drag-and-drop programming, but places the hub, Low Speed PRE
handling, and bulk transfers on a bit-banged port — the most fragile protocol
path on the most fragile implementation. Rejected.

### 8.2 Native = host, PIO = device (CDC console)

Defensible; held as a fallback if a USB console later proves necessary. Blocked
on verifying pico-pio-usb support for RP2350 in device mode (OI-2).

Note this would *not* restore BOOTSEL, which requires the **native** controller
in device mode.

### 8.3 On-board FT2232H

Channel A in MPSSE mode drives either SWD or JTAG; channel B provides a UART.

Rejected for the carrier PCB: roughly 5× the cost of the main MCU plus 93C56
EEPROM, 12 MHz crystal, and another USB connector; closed silicon; SWD over
MPSSE needs a resistor or buffer to make SWDIO bidirectional; and channel A
cannot serve SWD and ECP5 JTAG simultaneously.

The one strong argument is **ECP5 JTAG for gateware iteration**, which
openFPGALoader supports directly. If the bitstream reload cycle proves too slow,
build the FT2232H as a **separate dongle board** with its own JTAG connector.
It doubles as a tier-3 flash programmer (§5.5).

Baseline gateware delivery: the EC configures the ECP5 in slave SPI mode from SD
or over the debug UART. At 1 Mbaud an ECP5-45F bitstream takes on the order of
15 seconds. Measure it (OI-7).

### 8.4 USB role mux for BOOTSEL

An analog USB mux plus a dedicated device connector would restore BOOTSEL. It
adds a part in the differential path, a connector, and VBUS interlock logic, to
recover a capability already covered by three independent tiers. Rejected.

---

## 9. Do not do

- **Do not program OTP.** Secure boot and `DISABLE_BOOTSEL_UART_BOOT` are
  irreversible. Programming security bits disables SWD permanently and removes
  tiers 1 and 2 simultaneously. The EC has no threat model requiring them.
- **Do not route QSPI stubs near the SDRAM, backlight boost, or TPS61023
  switching node.** They connect to the in-package flash die.
- **Do not release the hub from reset with an RC.** Sequence it from EC firmware
  so the ordering against host enumeration is explicit.
- **Do not omit per-port current limiting.** A short on a user-accessible
  connector must not take down the system.
- **Do not populate the hub's SMBus configuration EEPROM.** Pin strapping is
  sufficient and removes a failure mode.

---

## 10. Open items

| ID | Item | Blocking |
|----|------|----------|
| OI-1 | Verify Low Speed device behind hub on real hardware with a real keyboard. Historically the most fragile path in the RP2040/RP2350 host stack | E-series bring-up |
| OI-2 | Determine whether pico-pio-usb supports RP2350 in device mode. Required only if 8.2 is adopted | Not blocking |
| OI-3 | Verify TPS61023 output capability at the chosen aggregate USB current limit with Vin = 3.4 V | Schematic freeze |
| OI-4 | Confirm A4 stepping on received parts by package marking (`RP2354B0A4`). Do not trust the distributor part number | Parts receipt |
| OI-5 | Validate all GPIO assignments against the RP2350B function mux table | Schematic capture |
| OI-6 | Finalise J-QSPI exposure and layout guarding. Trade recovery access against noise coupling to the in-package flash die | Schematic capture |
| OI-7 | Measure ECP5 bitstream load time over 1 Mbaud UART and from SD. If unacceptable, revisit 8.3 as a dongle | G-series gateware |
| OI-8 | Decide internal keyboard: GPIO matrix with autonomous PIO scan (recommended) versus USB | DN-HW-MCU-001 |
| OI-9 | Confirm EC idle current on RP2354B in the AON domain — the one area where RP2350 may be worse than RP2040 for a battery portable | Power budget sign-off |
| **OI-10** | **Verify TinyUSB multi-level hub support with `CFG_TUH_HUB >= 2`.** Determine behaviour when a user plugs an external hub into a downstream port. Define the fallback (graceful refusal with console message) if cascading is unreliable | E-series bring-up |
| **OI-11** | **Select hub IC: USB2514B (recommended) vs FE1.1S.** Confirm Hi-Speed-to-Full-Speed upstream fallback on the actual part before committing | Schematic capture |
| **OI-12** | **Decide downstream power policy:** aggregate hardware limit (a) vs `bMaxPower` accounting in EC firmware (b). (b) preferred | Schematic freeze |
| **OI-13** | **Decide JP-BYPASS implementation:** 3-pin shunt vs 0-ohm pad pair. Decide whether it survives to production or is prototype-only | Schematic capture |
| **OI-14** | **Write and archive the RAM flash-writer stub** (§5.4) with CRC-32 readback verification, before it is needed | Before E0 |

---

## 11. Gate plan

| Gate | Deliverable | Exit criterion |
|------|-------------|----------------|
| U0 | Hub IC selected, power policy decided, J-QSPI layout guarded | OI-11, OI-12, OI-13 closed |
| U1 | Schematic captured in KiCad | All USB and debug nets reviewed against this note |
| U2 | Board powered, EC programmed over SWD | `openocd program` succeeds; console output on J-DBG |
| U3 | RAM flash-writer stub validated | Firmware written to flash over J-QSPI UART, CRC-32 verified |
| U4 | Host stack enumerates with JP-BYPASS in bypass position | A Full Speed device enumerates directly, VID/PID on console |
| U5 | Host stack enumerates through the on-board hub | Hub descriptor read, port power applied, device on port 1 enumerated |
| U6 | Low Speed device behind hub | LS keyboard reports HID data. Closes OI-1 |
| U7 | Cascade behaviour characterised | External hub either works or is refused gracefully. Closes OI-10 |
| U8 | Power policy enforced | Device exceeding budget refused at `SET_CONFIGURATION`, reported on console |
