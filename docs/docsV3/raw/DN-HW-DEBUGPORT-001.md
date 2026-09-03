# DN-HW-DEBUGPORT-001 — Integrated Debug and Programming Port

**Project:** noVa64 (DANI-65816)
**Domain:** Hardware
**Status:** Decided, with open items blocking schematic capture
**Supersedes:** Tier definitions in the programming/debug section of the execution roadmap (tier numbering retained, transport revised)

---

## 1. Purpose

This note specifies the external debug and programming interface of the noVa64 machine: a chassis-mounted USB-C port backed by an on-board RP2040 acting as a CMSIS-DAP probe and USB-to-UART bridge for the RP2354B embedded controller (EC).

It defines the decision, the rejected alternatives and why they were rejected, the electrical interface, the power-domain arrangement, the firmware fork requirements, and the verification criteria. It is intended as a direct input to KiCad schematic capture.

---

## 2. Problem statement

The RP2354B EC has a single USB controller, and that controller is committed as a Full Speed **host** for the on-board USB2514B hub. A USB controller cannot operate as host and device simultaneously. Consequently the EC cannot present itself to a development PC as a programmable device.

BOOTSEL was previously removed as a recovery path because the USB-A receptacles source VBUS, and an A-to-A connection would place two 5 V supplies in opposition.

The result is that **without a second on-board device, no path exists from a development host to the EC**. The debug port is therefore not a convenience feature; it is the only bridge.

---

## 3. Options considered

| ID | Option | Outcome |
|----|--------|---------|
| A | Internal header, external Raspberry Pi Debug Probe | Rejected — requires opening the machine for every debug session |
| B | Chassis-mounted 3-pin connectors, external probe | Rejected — retains an external tool and a visible non-standard connector; the isolation problem is unchanged |
| C | FT2232H bridge (MPSSE SWD + UART) | **Rejected — see 3.1** |
| D | Purchased Debug Probe mounted inside the chassis | Rejected — the official probe uses micro-USB, defeating the port aesthetic; BOOTSEL sits under a snap-fit enclosure |
| E | **On-board RP2040 replicating the Debug Probe, USB-C device port** | **Selected** |

### 3.1 Why FT2232H was rejected

The FT2232H was initially attractive because it has no firmware that can be corrupted — it configures from EEPROM — which appeared to eliminate the "who programs the programmer" recursion.

It was rejected on evidence:

- SWD is not native to MPSSE. MPSSE is an SPI-like engine with separate data-in and data-out lines, whereas SWDIO is bidirectional and half-duplex. The standard workaround (OpenOCD `swd-resistor-hack.cfg`) connects TDI to SWDIO through a 220–470 Ω series resistor and TDO directly to SWDIO. This works in general, and OpenOCD ships `minimodule-swd.cfg` for FT2232H.
- The RP2350/RP2354 debug port is **SWD multidrop**, requiring the dormant-to-SWD sequence and TARGETSEL to select between cores. OpenOCD 0.12.0 release notes list SWD multidrop support for CMSIS-DAP specifically; per-driver mention implies per-driver work, and the `ftdi` driver is not named.
- Every documented working RP2350 flow found uses `interface/cmsis-dap.cfg` against the Raspberry Pi OpenOCD fork.
- A public report exists of FT2232H failing DAP init against RP2040 — which is not even multidrop — with the resistor hack correctly fitted, and no resolution in the thread.

The failure mode of being wrong is a fabricated PCB with a dead debug port and no alternative path to the EC. That is precisely the risk this port exists to eliminate, so the unproven transport was rejected in favour of the transport Raspberry Pi maintains.

### 3.2 Correction to the earlier "recursion" objection

An earlier concern held that an on-board RP2040 introduces a single point of failure, because its own firmware could be corrupted, cutting off all access to the machine.

This overstates the risk. **The RP2040 bootrom is mask ROM.** It cannot be erased, corrupted or overwritten. BOOTSEL plus USB device enumeration always works regardless of the state of the SPI flash. The recursion exists but has a hard floor, and that floor is more dependable than an FTDI part that may simply fail to enumerate a DAP.

### 3.3 Precedent

The approach is well precedented. `red-scorp/RP2040-DebugProbe` is a four-layer RP2040 board carrying all components required to function identically to the official Debug Probe, with a USB Type-C host connector, pin-compatible with the official probe and usable with the same picoprobe software. `picoLink` (MCU on Eclipse) is a second independent reimplementation.

The `debugprobe` firmware is open source. The hardware being replicated is essentially the RP2040 minimum reference design plus four series resistors and an input buffer arrangement. No proprietary content is involved.

---

## 4. Decision summary

| Item | Decision |
|------|----------|
| Probe MCU | RP2040, QFN-56 |
| Probe flash | 2 MB QSPI (W25Q16-class) |
| Host connector | USB-C receptacle, device role, chassis-mounted |
| Probe power source | VBUS of the debug USB-C port |
| Target signals | SWCLK, SWDIO, UART TX, UART RX, RUN |
| Termination | 100 Ω source termination at both ends, per Raspberry Pi 3-pin Debug Connector Specification |
| TX contention mitigation | VBUS presence sensed by EC GPIO; EC holds UART TX Hi-Z when probe unpowered |
| Probe recovery | BOOTSEL button, chassis pinhole access |
| Firmware | Fork of `debugprobe`, built from source |
| QSPI tier-3 access | Test pads only, no probe connection — see 11.4 |

---

## 5. Architecture

### 5.1 Block arrangement

```
  Development host
        |
     USB 2.0 FS
        |
  [USB-C receptacle]  <-- chassis
        |  D+/D-, VBUS, CC1/CC2
        |
  [ESD array]
        |
  [RP2040 probe]  <-- 3V3_PROBE, from VBUS via LDO
        |
        |  SWCLK, SWDIO, UART TX/RX, RUN
        |  (100 R source termination both ends)
        |
  [RP2354B EC]  <-- 3V3_AON
```

The probe is electrically an island. It shares only GND with the rest of the machine, plus the five signal lines and the VBUS-sense divider.

### 5.2 Power domains

The probe is powered from the debug port's VBUS, supplied by the development host, through a local 3.3 V LDO designated **3V3_PROBE**.

This is deliberate. The scenario that justifies the port is "the machine will not start and I need to reach the EC". A probe dependent on a rail of the machine under repair cannot serve that purpose.

Resulting behaviour:

| Condition | 3V3_PROBE |
|-----------|-----------|
| Debug cable connected to host | Present |
| Debug cable connected, machine powered off | Present |
| Debug cable disconnected | Absent |
| Machine running or charging, no debug cable | Absent |

Note that the debug port's VBUS is unrelated to the PD charging port served by the CH224K. The two USB-C receptacles are independent.

RP2040 current draw is in the tens of milliamperes, well within the 500 mA a USB host guarantees before negotiation.

3V3_PROBE must **not** be tied, OR-ed or diode-coupled to 3V3_AON. Cross-domain current is bounded by the series termination described in 5.3 and the gating described in 5.4.

### 5.3 Signal interface and termination

The Raspberry Pi 3-pin Debug Connector Specification requires source termination resistors of 100 Ω at **both** ends of each link — at the host IC pins and at the target IC pins — placed very close to the IC pins so that the 200 Ω series combination only has to drive pin capacitance and the shortest possible trace capacitance. The resistors provide slew-rate limiting (benefiting signal integrity and EMC) together with short-circuit and ESD current limiting.

Although the connector and cable are eliminated by integration, **the termination is retained**. It costs four additional resistors and preserves the protective properties.

| Signal | Direction | Probe-side R | EC-side R |
|--------|-----------|--------------|-----------|
| SWCLK | Probe → EC | 100 Ω | 100 Ω |
| SWDIO | Bidirectional | 100 Ω | 100 Ω |
| UART RX (EC input) | Probe → EC | 100 Ω | 100 Ω |
| UART TX (EC output) | EC → Probe | 100 Ω | 100 Ω |
| RUN | Probe → EC | 100 Ω | — |

All resistors 1 %, placed within a few millimetres of their respective IC pins.

The official probe routes the read side of SWDIO and of UART RX through input buffers, using separate RP2040 GPIO for drive and sense. This topology must be replicated; the buffer part and arrangement are to be taken from the published Raspberry Pi schematic (see 11.1).

### 5.4 UART TX contention and VBUS sensing

The normal operating state of the machine is **EC powered, probe unpowered**, and unlike a plug-in probe the traces are permanently connected.

The EC's UART TX idles high. With 3V3_PROBE absent, that pin drives approximately 3.3 V through 200 Ω into an RP2040 input whose supply rail is at zero. Current flows through the input clamp diode and attempts to raise 3V3_PROBE, potentially leaving the RP2040 in an indeterminate partially-powered state. At roughly 16 mA continuous this is a genuine backfeed condition, and here it is the normal case rather than an edge case.

SWCLK, SWDIO and RUN do not exhibit this problem: they are driven by the probe, and the EC only drives SWDIO during transactions that by definition occur with the probe alive.

**Mitigation.** A resistive divider from debug-port VBUS to an EC GPIO (`DBG_VBUS_SENSE`). The EC holds its UART TX pin in high-impedance input mode whenever the divider reads low, and configures it as UART TX only when a probe is detected.

This signal carries a second benefit: it informs the EC that a debugger is attached, which composes naturally with the existing `DEBUG_ENABLE` strap of the Debug Agent and permits automatic redirection of the kernel console.

Divider sized for 5 V input to a 3.3 V-tolerant GPIO with adequate noise margin; exact values to be fixed at schematic capture.

### 5.5 USB-C device port

- Receptacle wired for **device (UFP) role only**. USB 2.0 signalling; SuperSpeed pairs unused.
- **5.1 kΩ pull-downs on CC1 and CC2 to GND**, individually. Without these the port will not be recognised by a Type-C host.
- ESD protection array on D+/D− (USBLC6-2 class or equivalent). The port is externally exposed.
- VBUS is consumed only; the port never sources.

### 5.6 Probe recovery

BOOTSEL is implemented as a momentary switch pulling QSPI_SS low through 1 kΩ, sampled by the mask-ROM bootrom at reset. The 1 kΩ prevents contention with the flash chip select during normal operation.

The button is accessible through a **chassis pinhole**, in the manner of a router reset. This is one of the concrete advantages of integration over the purchased probe, whose BOOTSEL button sits under a snap-fit enclosure that must be pinched apart.

SWD test pads for the RP2040 itself are provided for probe-firmware development, where drag-and-drop UF2 cycles are inefficient compared to breakpoint debugging. These are pads, not a connector.

---

## 5.7 Net-level interconnection and domain isolation

### 5.7.1 Domain crossing inventory

Exactly **six** electrical paths cross between the probe domain (3V3_PROBE) and the machine domain (3V3_AON), plus the shared ground reference. Nothing else crosses. Any additional crossing introduced during capture is a design error and must be justified against this note.

| # | Net | Direction | Crossing type |
|---|-----|-----------|---------------|
| 1 | `DBG_SWCLK` | Probe → EC | Driven, series terminated |
| 2 | `DBG_SWDIO` | Bidirectional | Driven, series terminated |
| 3 | `DBG_UART_RX` | Probe → EC | Driven, series terminated |
| 4 | `DBG_UART_TX` | EC → Probe | Driven, series terminated, **software gated** |
| 5 | `EC_RUN_N` | Probe → EC | **Open-drain only** |
| 6 | `DBG_VBUS_SENSE` | Probe domain → EC | High-impedance divider |
| — | `GND` | — | Common reference, unsplit |

**Ground is deliberately common and must not be split.** All six signals reference GND; a split plane or ferrite between domains would create a return-path discontinuity, defeat the source termination, and worsen rather than improve isolation. "Isolation" in this note means *supply* isolation, not galvanic isolation. Single continuous ground pour under both domains.

**No supply rail crosses.** 3V3_PROBE and 3V3_AON are never tied, diode-OR-ed, or bridged by a zero-ohm option. Debug-port VBUS never reaches the machine domain except through the divider of net 6.

### 5.7.2 Pin assignment

RP2040 assignments follow the official Debug Probe map wherever a corresponding function exists, per section 6. Assignments outside that map are marked **[new]** and are chosen so that loading the stock official binary leaves them undriven and inert.

| Net | RP2040 pin | RP2354B pin | Note |
|-----|-----------|-------------|------|
| `DBG_SWCLK` | GPIO12 (out) | SWCLK (dedicated) | Official map |
| `DBG_SWDIO` | GPIO14 (drive) / GPIO13 (sense, buffered) | SWDIO (dedicated) | Official map; buffer per 11.1 |
| `DBG_UART_RX` | GPIO4 — UART1 TX | GPIO0 — UART0 TX *(proposed)* | Probe transmits into EC receiver |
| `DBG_UART_TX` | GPIO6 (direct) / GPIO5 (buffered, UART1 RX) | GPIO1 — UART0 RX *(proposed)* | Official map; see naming note below |
| `EC_RUN_N` | GPIO20 **[new]**, open-drain | RUN (dedicated) | Undriven under stock firmware |
| `DBG_VBUS_SENSE` | — | GPIO *TBD* | Divider from debug VBUS |
| Probe status LEDs | GPIO2, 7, 8, 15 | — | Official map |
| QSPI tier-3 access | *none — test pads only* | QSPI_SS, SCK, SD0–SD3 | Option (a) per 11.4; no RP2040 GPIO consumed |

SWCLK, SWDIO and RUN are dedicated pins on RP2350-family devices and are not subject to GPIO allocation. Only the UART pair and `DBG_VBUS_SENSE` consume EC GPIO budget.

**Naming convention.** Following the Raspberry Pi 3-pin Debug Connector Specification, `TX` and `RX` are named *from the target's perspective*. `DBG_UART_TX` is therefore an EC output and a probe input. The specification places the unidirectional SC (serial clock, driven into the target) on the same pin as UART RX because both travel in the same direction, so that mis-plugging a UART into a serial debug interface is safe.

### 5.7.3 Link topologies

Unidirectional probe-to-EC links (`DBG_SWCLK`, `DBG_UART_RX`):

```
   3V3_PROBE domain          |          3V3_AON domain
                             |
  RP2040                     |                    RP2354B
  GPIOnn ---[100R]-----------+-----------[100R]--- pin
            ^                |             ^
            within a few mm  |             within a few mm
            of RP2040 pin    |             of RP2354B pin
```

Bidirectional `DBG_SWDIO`:

```
  RP2040                                          RP2354B
  GPIO14 (drive) ---[100R]---+------[100R]------- SWDIO
                             |
  GPIO13 (sense) <--[buf]<---+
                    ^
                    input buffer, topology per 11.1
```

EC-to-probe `DBG_UART_TX`, the only line driven from the machine domain:

```
  RP2354B                                         RP2040
  GPIO1 ---[100R]---+------[100R]---+-----------> GPIO6 (direct)
     ^              |               |
     |              |               +--[buf]----> GPIO5 (UART1 RX)
     |
   Hi-Z unless DBG_VBUS_SENSE asserted
```

`EC_RUN_N`, open-drain:

```
  RP2040                                          RP2354B
  GPIO20 ---[100R]------------------------------- RUN
    |                                              |
  drives LOW or Hi-Z only                      internal pull-up
  never drives HIGH
```

`DBG_VBUS_SENSE`:

```
  VBUS (debug port, 5V) ---[R_top]---+--- EC GPIO
                                     |
                                  [R_bot]
                                     |
                                    GND
```

### 5.7.4 Open-drain requirement on RUN

`EC_RUN_N` **must** be driven open-drain: the probe asserts low or releases to high impedance, and never drives high. Two reasons:

1. The RP2354B RUN pin has an internal pull-up, so high impedance already means "not in reset". Driving high adds nothing.
2. Driving high would inject probe-domain current into an unpowered machine domain, which is precisely the failure mode this section exists to prevent.

This also yields a safe default: under the stock official firmware, GPIO20 is never configured, the RP2040 leaves it as an input at reset, RUN floats to its internal pull-up, and the EC runs normally.

### 5.7.5 Power-state behaviour

| State | 3V3_PROBE | 3V3_AON | Concern |
|-------|-----------|---------|---------|
| S1 | Absent | Present | **Normal operating state.** EC could drive `DBG_UART_TX` into an unpowered probe — mitigated by gating (5.4) |
| S2 | Present | Present | Normal debug session. No concern |
| S3 | Present | Absent | Rescue with no battery. Probe could drive four lines into an unpowered EC |
| S4 | Absent | Absent | Machine fully unpowered. No concern |

**S1** is the state the machine spends its life in and is fully addressed by `DBG_VBUS_SENSE` gating. `DBG_SWDIO` is not a concern here: the EC only drives it during transactions, which by definition require a live probe. Its idle state is governed by the RP2354B internal pull-up, giving a current on the order of tens of microamperes through the 200 Ω series path — negligible, but the presence and value of that pull-up is to be confirmed against the datasheet.

**S3** occurs only with the battery absent or fully depleted, since 3V3_AON is an always-on rail. It is nonetheless real during early bring-up. Mitigations: RP2040 GPIOs default to inputs at reset, so all target-facing lines are high impedance until firmware configures them; `EC_RUN_N` is open-drain and cannot source; and the probe firmware shall not configure `DBG_SWCLK` or `DBG_UART_RX` as outputs until a host session is established.

### 5.7.6 Backfeed current budget

Worst case per line, driver at 3.3 V into a clamp diode across 200 Ω of series termination:

| Assumption | Current |
|------------|---------|
| Ignoring clamp forward drop | 16.5 mA |
| With 0.6 V clamp drop | 13.5 mA |

Aggregate for state S3 with four lines simultaneously driven: approximately 54 mA into 3V3_AON. This is not sufficient to bring up the rail against the load of the RP2354B, CH224K, BQ25896 and MAX17048, but it is sufficient to place devices in an indeterminate partially-biased condition, which is why 5.7.5 requires firmware-side high impedance rather than relying on the current limit alone.

Both figures must be checked against the RP2354B absolute maximum clamp current — see open item 11.2. If margin is inadequate, termination rises to 220 Ω or 330 Ω per side and the slew-rate consequences are re-evaluated against maximum SWD clock rate.

### 5.7.7 VBUS sense divider sizing

The divider is itself a domain crossing and must be high impedance for that reason, not merely for power economy. With the probe connected and the machine unpowered, divider current flows into the EC GPIO clamp.

Design targets:

- Total series resistance ≥ 150 kΩ, giving well under 50 µA in the worst case
- Output between 1.5 V and 3.0 V at VBUS = 5.0 V
- Output below 0.4 V at VBUS = 0 V, guaranteed by the lower leg to GND
- Optional 100 nF to GND at the GPIO for noise immunity on hot-plug

A worked candidate is 100 kΩ / 56 kΩ, yielding approximately 1.79 V at 5.0 V VBUS and 32 µA. Final values fixed at capture against the EC input threshold.

### 5.7.8 Layout constraints

- All eight signal-side 100 Ω resistors within 3 mm of their respective IC pins, per the Raspberry Pi specification requirement that the series combination drive only pin capacitance and the shortest possible trace capacitance.
- Continuous ground reference under the entire six-net crossing region. No plane split, no moat, no stitching ferrite.
- `DBG_SWCLK` and `DBG_SWDIO` routed as a loosely coupled pair, matched to within 5 mm, referenced to the same plane for their full length.
- 3V3_PROBE pour confined to the probe area, with a single clearly identifiable boundary. Visual separability of the two pours on the plot is a review criterion.
- Debug USB-C differential pair length-matched, 90 Ω differential, kept clear of the probe's own QSPI flash bus.

---

## 6. Firmware

The probe runs a **fork of `debugprobe`, built from source**. A fork is required regardless, because the following capabilities are not present in the stock binary:

- `RUN` assertion and de-assertion against the RP2354B — needed for reliable attach, and to hold the EC in reset while tier-3 test pads are accessed by external equipment
- Status LED behaviour appropriate to an integrated part
- Status reporting of `DBG_VBUS_SENSE` state, where useful

Where it costs nothing to do so, the RP2040 GPIO assignment should follow the official Debug Probe pinout. This preserves the ability to load the stock official binary as a diagnostic, allowing custom firmware to be eliminated as a suspect during bring-up.

Host-side toolchain: Raspberry Pi OpenOCD fork with `interface/cmsis-dap.cfg` and `target/rp2350.cfg`. The minimum acceptable OpenOCD revision must be recorded once verified against hardware.

---

## 7. Revised programming and debug tiers

The tier numbering of the roadmap is retained. Transports and relative priority are revised.

| Tier | Mechanism | Depends on | Can write EC flash | Status |
|------|-----------|------------|--------------------|--------|
| 1 | SWD via integrated probe | EC core reachable, debug not OTP-locked | Yes | **Primary** |
| 2 | Bootrom UART boot on QSPI SD2/SD3, 1 Mbaud | Bootrom, OTP enable bit, RAM flash-writer stub | Not directly — downloads to SRAM only | Deprioritised |
| 3 | Direct SPI to EC flash, RUN held low | Nothing | Yes | **Diagnostic** — conditional, see 7.1.1 |

### 7.1 Effect of in-package flash on tier 3

The RP2354 datasheet states that the six dedicated QSPI pads on the RP2350 die (CSn, SCK, SD0–SD3) connect to **both** the internal flash die and the external package pins, making them behave similarly to flashless RP2350 devices.

Two consequences:

1. **There will never be an external flash chip on the RP2354B.** Any reference to a discrete W25Q16JV as the EC's boot flash is obsolete and must be removed from other project documents.
2. **Tier 3 remains electrically possible.** The flash is internal but externally addressable. With RUN held low the core is in reset and its QSPI pads are high-impedance, allowing an external master to treat the internal die as an ordinary SPI flash: JEDEC ID, erase, page program, read. This requires no bootrom, no firmware, no OTP configuration and no core execution.

#### 7.1.1 Tier 3 is a diagnostic, not a recovery path

An earlier draft of this note elevated tier 3 to primary recovery mechanism. That was written before the capabilities of tier 1 were fully characterised, and it is **withdrawn**.

Tier 1 recovers the EC when the flash is blank, when the flash is corrupt, when the machine has no power of its own, and — via the RP2350 rescue DP — when the device is resetting itself in a loop. It leaves tier 3 with very little recovery territory of its own.

What remains genuinely valuable:

1. **Bring-up diagnostic (the principal justification).** At E0, with a freshly assembled board, reading the flash JEDEC ID with RUN held low simultaneously confirms supply integrity, QFN-80 solder joints, and QSPI pad continuity — and does so without depending on probe firmware, OpenOCD, or multidrop configuration. When SWD fails to attach, this test separates "assembly fault" from "software fault" in one step. That diagnostic value is not available from any other tier.
2. **Physically damaged SWD lines.** ESD, a lifted trace, a solder bridge under the package. Low probability, and in that situation rework is already under way.
3. **Core-independent flash read or erase.** Byte-level verification, or returning the device to a fully blank state without cooperation from anything.

Tier 3 is therefore retained as a **bring-up diagnostic aid**, and its inclusion is conditional on the layout outcome described in 11.4. Recovery is the responsibility of tier 1.

### 7.2 Why tier 2 was deprioritised

Tier 2 downloads to SRAM and executes from SRAM; the bootrom does not write flash. Reflashing therefore requires transmitting a RAM-resident flash-writer stub as a real software deliverable.

It is squeezed between two stronger neighbours. SWD works against a blank flash and the RP2350 rescue DP recovers a self-resetting device; tier 3 requires no chip cooperation at all. Tier 2 only contributes in the narrow window where SWD is physically broken but the bootrom is reachable.

The pads cost nothing — SD2 and SD3 are a subset of the six brought out for tier 3 — so the hardware is retained. **The RAM flash-writer stub is removed from the critical path before E0.**

### 7.3 OTP policy

The OTP bits that disable debug must **never** be burned on this design. They are the only mechanism capable of closing tier 1 and tier 3 simultaneously.

---

## 8. Chassis requirements

- Two USB-C receptacles with dissimilar functions (PD charging input via CH224K; debug device port). They must be **visually differentiated** — icon, colour or recess — because inserting the charger into the debug port is the obvious user error.
- Pinhole aligned to the probe BOOTSEL button.
- Debug USB-C positioned for a board-edge receptacle; no internal adapter cable.

---

## 9. Bill of materials delta

| Item | Qty | Note |
|------|-----|------|
| RP2040 QFN-56 | 1 | |
| QSPI flash 2 MB | 1 | W25Q16-class, probe's own boot flash |
| Crystal 12 MHz + load caps | 1 | |
| LDO 3.3 V from VBUS | 1 | ≥150 mA, AP2112K-class |
| USB-C receptacle, 16-pin UFP | 1 | |
| CC pull-down 5.1 kΩ | 2 | |
| ESD array D+/D− | 1 | |
| Series termination 100 Ω 1 % | 9 | 4 probe-side, 4 EC-side, 1 on RUN |
| BOOTSEL switch + 1 kΩ | 1 | |
| VBUS sense divider | 2 | |
| Status LEDs + resistors | 2–5 | |
| Decoupling | — | Per RP2040 hardware design guide |

---

## 10. Verification and exit criteria

Staged, each stage independently gated.

| ID | Test | Pass criterion |
|----|------|----------------|
| V1 | Probe standalone enumeration, EC held in reset | Host enumerates CMSIS-DAPv2 and CDC ACM interfaces |
| V2 | OpenOCD attach to RP2354B | Valid DPIDR read; both cores examined without error |
| V3 | Flash and run a minimal binary on EC via SWD | Binary executes; observable output |
| V4 | UART console | Bidirectional traffic between EC and host terminal |
| V5 | Backfeed measurement, state S1 (probe unpowered, EC running) | 3V3_PROBE below 0.3 V; current into probe domain below stated clamp limit |
| V5b | Backfeed measurement, state S3 (probe powered, battery removed) | 3V3_AON below 0.3 V with probe idle; all target-facing probe pins verified Hi-Z before session start |
| V6 | VBUS sense and TX gating | EC TX confirmed Hi-Z with probe absent; UART functional within 100 ms of probe insertion |
| V7 | RUN control | EC held in reset and released under probe command |
| V8 | Probe BOOTSEL recovery | Host mounts `RPI-RP2`; UF2 load restores firmware |
| V9 | Tier 3 bring-up diagnostic | JEDEC ID read from EC internal flash via test pads, RUN held low, using external SPI equipment |
| V10 | XIP integrity with test-pad stubs present | EC executes from flash at maximum clock with no errors over a sustained run |

**Exit criterion for schematic capture:** V1–V8 defined and all items in section 11 closed.

---

## 11. Open items

### 11.1 Official schematic retrieval — **blocks schematic capture**
Raspberry Pi publishes the Debug Probe schematic and mechanical drawing. The input buffer topology on the SWDIO and UART RX read paths must be taken from it: part number, supply, and behaviour when the target is unpowered. Zephyr's board documentation confirms separate GPIO for direct and buffered paths but does not identify the device.

### 11.2 RP2354B clamp diode limit — **blocks schematic capture**
The calculated worst-case backfeed of approximately 16 mA per line through 200 Ω must be checked against the RP2354B absolute maximum clamp current. If insufficient margin exists, termination values increase and slew-rate implications must be re-examined.

### 11.3 Tier 2 OTP gate
The RP2350 bootrom UART boot path is believed to require an OTP enable bit rather than being active by default. If confirmed, tier 2 is not a rescue mechanism but something that must be provisioned in advance, and this must be reflected in the manufacturing flow. To be verified against the datasheet.

### 11.4 QSPI access scope — **decided, conditional on layout**

Bringing the six QSPI pads out creates six transmission-line stubs on a bus that runs XIP against the in-package flash at tens of megahertz. Unlike the previous external-flash arrangement, these stubs serve no functional purpose during normal operation and are pure parasitic load.

Options considered:

| | Arrangement | Stub length | Outcome |
|---|---|---|---|
| (a) | Test pads immediately adjacent to the package; pogo pin or clip access; no probe connection | millimetres | **Selected** |
| (b) | As (a), plus probe connection through 22–33 Ω series resistors | tens of mm | Rejected — the probe connection buys convenience for a test run twice in the project's life, at the cost of permanent stubs and six RP2040 GPIO |
| (c) | No QSPI access; tier 3 removed | none | Fallback — see abandonment condition |

**Decision: option (a)**, justified by the bring-up diagnostic case of 7.1.1 rather than by recovery. **The previously specified 8-pin `J-QSPI` header is withdrawn** — this is a test-pad footprint, not a connector, and no cable or connector shall be fitted to these nets.

**Abandonment condition.** If during layout the six pads cannot be placed immediately adjacent to the package without either (i) compromising the routing of the QSPI bus itself, or (ii) extending any stub beyond a few millimetres, then **option (c) shall be adopted without further review**. A clean XIP bus is worth more than a diagnostic that may be exercised twice in the lifetime of the project. This condition is delegated to the layout engineer and does not require reopening this note.

Consequences of adopting (c): verification items V9 and V10 are removed; the QSPI row of the pin assignment table in 5.7.2 is deleted; tier 3 disappears from section 7 and the tier table reduces to two entries.

### 11.5 DEBUG_ENABLE interaction
`DBG_VBUS_SENSE` and the Debug Agent's `DEBUG_ENABLE` strap overlap in purpose. Their relationship must be defined: whether probe presence gates Debug Agent availability, whether either can disable the other, and what the production posture is.

### 11.6 RP2354B SWDIO idle bias — **blocks schematic capture**
Section 5.7.5 assumes the RP2354B SWDIO pin carries an internal pull-up, making its idle backfeed contribution in state S1 negligible. If instead it floats, or carries a pull-down, the analysis changes and `DBG_SWDIO` may require the same gating treatment as `DBG_UART_TX`. To be confirmed against the datasheet.

### 11.7 EC UART pin allocation
`DBG_UART_RX` and `DBG_UART_TX` are proposed on GPIO0/GPIO1 (UART0 default). `DBG_VBUS_SENSE` is unallocated. Both must be reconciled against the full EC pin budget — battery I2C, SD mux, Helium SPI, regulator enables, USB host, keyboard matrix — before capture.

### 11.8 Probe MCU alternative
RP2354A would consolidate the BOM by removing the external flash. Deferred: `debugprobe` firmware support for RP2350 as *host* is unverified, and the proven path was selected deliberately in section 3. Revisit only if BOM pressure justifies the verification effort.

---

## 12. References

- Raspberry Pi 3-pin Debug Connector Specification, RP-003139-SP
- Raspberry Pi Debug Probe documentation and product brief (February 2023)
- Zephyr Project, `rpi_debug_probe` board documentation — GPIO mapping and buffer paths
- RP2350 datasheet, QSPI pad description for RP2354 variants
- OpenOCD 0.12.0 release notes — SWD multidrop support
- OpenOCD `minimodule-swd.cfg`, `swd-resistor-hack.cfg` — FTDI SWD reference (rejected path)
- `red-scorp/RP2040-DebugProbe` — integration precedent
- `raspberrypi/debugprobe` — firmware source

---

## 13. Revision history

| Rev | Date | Change |
|-----|------|--------|
| 001 | 2026-09-03 | Initial issue |
| 001a | 2026-09-03 | Added §5.7 net-level interconnection and domain isolation; open-drain requirement on RUN; power-state matrix S1–S4; open items 11.6–11.7. Tier 3 reframed from recovery mechanism to bring-up diagnostic (§7.1.1); 11.4 closed on option (a) with an explicit abandonment condition |
