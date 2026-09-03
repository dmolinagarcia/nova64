# Integrated debug and programming port
> a USB-C on the chassis · an RP2040 as the probe · six nets across two supply domains

The EC's single USB controller is committed to host mode and the assignment is one-way ([D1.9](sec_ai_d1#d19)), so **nothing on this board can present itself to a development PC** — `BOOTSEL` went with it. This sheet is the bridge that replaces it: a chassis-mounted **USB-C port backed by an on-board RP2040** running a fork of `debugprobe`, reaching the EC over SWD, a UART and `RUN`. It is not a convenience feature, it is the only path from a host to the EC, and it withdraws the `J-DBG` header and the external probe that [D1.4](sec_ai_d1#d14) and [D1.12](sec_ai_d1#d112) assumed.

- D2.1 — **A USB controller cannot be host and device simultaneously, and that single fact generates this whole sheet.** The controller is spent on the USB2514B hub ([D1.10](sec_ai_d1#d110)); `BOOTSEL` over USB is structurally unavailable rather than merely unsupported, because the USB-A receptacles source VBUS and an A-to-A cable opposes two 5 V supplies. **Without a second device on the board there is no path from a development host to the EC at all.**
  NOTE: The port therefore has to be judged as infrastructure rather than as tooling. A debug feature that is nice to have can be deferred to the second spin; **the only way in cannot**, and that asymmetry is what decided every trade below in favour of the proven option over the clever one.

## The alternatives — and why the one that looked cheapest was rejected on evidence.

| ID | Option | Outcome |
|---|---|---|
| A | Internal header, external Raspberry Pi Debug Probe | Rejected — the machine has to be opened for every debug session |
| B | Chassis-mounted 3-pin connectors, external probe | Rejected — keeps an external tool and a non-standard visible connector; the isolation problem is unchanged |
| C | FT2232H bridge, MPSSE SWD plus UART | **Rejected on evidence** ([D2.2](sec_ai_d2#d22)) |
| D | Purchased Debug Probe mounted inside the chassis | Rejected — micro-USB defeats the port, and its `BOOTSEL` sits under a snap-fit shell |
| E | **On-board RP2040 replicating the Debug Probe, USB-C device port** | **Selected** |

- D2.2 — **The FT2232H was attractive for one real reason and rejected for four.** It configures from EEPROM and has no firmware to corrupt, which appeared to end the "who programs the programmer" recursion. Against it: **SWD is not native to MPSSE** — MPSSE has separate data-in and data-out lines where `SWDIO` is bidirectional and half-duplex, and the standard answer is OpenOCD's `swd-resistor-hack.cfg`, TDI to `SWDIO` through 220–470 Ω with TDO tapped directly. **The RP2350 debug port is SWD multidrop**, needing the dormant-to-SWD sequence and `TARGETSEL` to pick a core; OpenOCD 0.12.0 lists multidrop support *for CMSIS-DAP specifically*, and per-driver mention implies per-driver work — the `ftdi` driver is not named. **Every documented working RP2350 flow uses `interface/cmsis-dap.cfg`** against the Raspberry Pi OpenOCD fork. And there is a public report of an FT2232H failing DAP init against RP2040 — which is not even multidrop — with the resistor hack correctly fitted and no resolution.
  NOTE: **The failure mode of being wrong is a fabricated PCB with a dead debug port and no other way to the EC**, which is precisely the risk the port exists to remove. An unproven transport cannot be bought with saved firmware effort here.
- D2.3 — **The recursion objection against an on-board probe was overstated, and the reason is worth stating plainly: the RP2040 bootrom is mask ROM.** It cannot be erased, corrupted or overwritten, so `BOOTSEL` plus USB device enumeration works regardless of what the probe's SPI flash holds. The recursion is real but it has a hard floor — **and that floor is more dependable than an FTDI part that may simply refuse to enumerate a DAP.**
- D2.4 — **The approach is precedented twice over, and none of it is proprietary.** `red-scorp/RP2040-DebugProbe` is a four-layer RP2040 board with a USB-C host connector, pin-compatible with the official probe and driven by the same software; `picoLink` is an independent second reimplementation. The `debugprobe` firmware is open source and the hardware is the RP2040 minimum reference design plus four series resistors and an input buffer arrangement.
- D2.5 — **The integration also buys the one thing a purchased probe cannot give: the probe's own `BOOTSEL` behind a chassis pinhole**, in the manner of a router reset, instead of under a snap-fit enclosure that has to be pinched apart.

## The decision, in one table.

| Item | Decision |
|---|---|
| Probe MCU | RP2040, QFN-56 |
| Probe flash | 2 MB QSPI, W25Q16-class — the probe's own, not the EC's |
| Host connector | USB-C receptacle, device role, chassis-mounted |
| Probe power | VBUS of the debug port, through a local LDO — rail `3V3_PROBE` |
| Target signals | `SWCLK`, `SWDIO`, UART TX, UART RX, `RUN` |
| Termination | 100 Ω source termination at **both** ends, per the Raspberry Pi 3-pin Debug Connector Specification |
| TX contention | Debug VBUS sensed by an EC GPIO; the EC holds its UART TX Hi-Z while the probe is unpowered |
| Probe recovery | `BOOTSEL` button on a chassis pinhole |
| Firmware | Fork of `debugprobe`, built from source |
| Tier-3 QSPI access | **Test pads only, no probe connection** ([D2.22](sec_ai_d2#d222)) |

- D2.6 — **The probe is powered from the debug port's VBUS and never from the machine, and that is deliberate rather than convenient.** The scenario that justifies the port is *"it will not start and I need to reach the EC"*; **a probe that depends on a rail of the machine under repair cannot serve it.** A local 3.3 V LDO of at least 150 mA, AP2112K-class, feeds `3V3_PROBE`; RP2040 draw is tens of milliamperes, far inside the 500 mA a host grants before negotiation.
  NOTE: This debug VBUS has nothing to do with the PD charging input served by the CH224K ([C.11](sec_ai_c#c11)). **The two USB-C receptacles are independent**, which is exactly why [D2.24](sec_ai_d2#d224) makes them visually different.

| Condition | `3V3_PROBE` |
|---|---|
| Debug cable connected to a host | Present |
| Debug cable connected, machine powered off | Present |
| Debug cable disconnected | Absent |
| Machine running or charging, no debug cable | Absent |

- D2.7 — **`3V3_PROBE` must never be tied, OR-ed or diode-coupled to `3V3_AON`.** No zero-ohm option, no "just for bring-up" bridge. Cross-domain current is bounded by the series termination of [D2.9](sec_ai_d2#d29) and the gating of [D2.13](sec_ai_d2#d213), and both arguments collapse the moment the rails touch.
- D2.8 — **The USB-C receptacle is device role only.** USB 2.0 signalling with the SuperSpeed pairs unused; **5.1 kΩ pull-downs on `CC1` and `CC2` individually**, without which a Type-C host will not recognise the port at all; an ESD array on `D+`/`D−`, USBLC6-2 class, because the port is externally exposed; and **VBUS is consumed only — this port never sources.**

![Fig. 13 — The integrated debug port. The probe hangs off the debug port's own VBUS and is an electrical island: six nets reach the EC, every driven one terminated at both ends, and the only line pointing back into the probe is held high-impedance until the EC sees VBUS on the port.](figures/fig-13-debug-port.svg)
LEGEND: Trace legend: <span class="m">mint = signal</span> · <span class="g">gold = supply, and the two domain boundaries</span> · dashed = gated, high-impedance or without a connector.

## Six nets cross between the domains, and nothing else may join them.

| # | Net | Direction | Crossing type |
|---|---|---|---|
| 1 | `DBG_SWCLK` | Probe → EC | Driven, series terminated |
| 2 | `DBG_SWDIO` | Bidirectional | Driven, series terminated |
| 3 | `DBG_UART_RX` | Probe → EC | Driven, series terminated |
| 4 | `DBG_UART_TX` | EC → probe | Driven, series terminated, **software gated** |
| 5 | `EC_RUN_N` | Probe → EC | **Open-drain only** |
| 6 | `DBG_VBUS_SENSE` | Probe domain → EC | High-impedance divider |
| — | `GND` | — | Common reference, **unsplit** |

- D2.9 — **The termination survives the integration even though the connector and cable do not.** The specification asks for 100 Ω at *both* ends of each link, placed within a few millimetres of the IC pins so the 200 Ω series pair drives only pin capacitance and the shortest possible trace — it is slew-rate limiting for signal integrity and EMC, and short-circuit and ESD current limiting at the same time. **Four extra resistors buy all of that**, so it is kept: 100 Ω each side of `SWCLK`, `SWDIO`, UART RX and UART TX, plus one on `RUN`, all 1 %.
  NOTE: **The official probe routes the read side of `SWDIO` and of UART RX through input buffers**, with separate RP2040 GPIO for drive and for sense. That topology must be replicated, and **the buffer part and its behaviour with an unpowered target are not published in the material to hand** — it comes from the official schematic, which is [[!blocking]] on schematic capture ([Q118](sec_ai_q#q118)).
- D2.10 — **`GND` is deliberately common and must not be split.** All six signals reference it; a split plane, a moat or a stitching ferrite would create a return-path discontinuity, defeat the source termination and make isolation worse rather than better. **"Isolation" here means supply isolation, not galvanic isolation** — one continuous ground pour under both domains.
- D2.11 — **`TX` and `RX` are named from the target's perspective**, following the 3-pin Debug Connector Specification: `DBG_UART_TX` is an EC output and a probe input. The specification puts the unidirectional serial clock on the same pin as UART RX precisely because both travel in the same direction, so mis-plugging a UART into a debug interface is safe. **Get this backwards in the schematic and it costs the same hours [D1.12](sec_ai_d1#d112) was written about.**

| Net | RP2040 pin | RP2354B pin | Note |
|---|---|---|---|
| `DBG_SWCLK` | GPIO12, out | `SWCLK`, dedicated | Official map |
| `DBG_SWDIO` | GPIO14 drive · GPIO13 sense, buffered | `SWDIO`, dedicated | Official map, buffer per [Q118](sec_ai_q#q118) |
| `DBG_UART_RX` | GPIO4, UART1 TX | GPIO0, UART0 TX *(proposed)* | Probe transmits into the EC's receiver |
| `DBG_UART_TX` | GPIO6 direct · GPIO5 buffered, UART1 RX | GPIO1, UART0 RX *(proposed)* | Official map |
| `EC_RUN_N` | GPIO20 **[new]**, open-drain | `RUN`, dedicated | Undriven under stock firmware |
| `DBG_VBUS_SENSE` | — | GPIO *unallocated* | Divider from debug VBUS ([Q123](sec_ai_q#q123)) |
| Probe status LEDs | GPIO2, 7, 8, 15 | — | Official map |
| Tier-3 QSPI | *none — test pads* | `QSPI_SS`, `SCK`, `SD0`–`SD3` | No RP2040 GPIO consumed ([D2.22](sec_ai_d2#d222)) |

- D2.12 — **Where a corresponding function exists, the RP2040 pin is the official Debug Probe's pin**, and the two assignments that are not — `EC_RUN_N` and the LEDs — are chosen so that **loading the stock official binary leaves them inert.** That preserves the ability to flash the shipped firmware as a diagnostic and eliminate the fork as a suspect during bring-up, which is worth more than the pins it costs.
  NOTE: On the EC side only the UART pair and `DBG_VBUS_SENSE` spend GPIO budget: `SWCLK`, `SWDIO` and `RUN` are dedicated pins on RP2350-family parts. The budget itself was rescued once already ([D1.6](sec_ai_d1#d16)) and this is a third claim on it (→ [Q123](sec_ai_q#q123)).

## The one line that points the wrong way — and the state the machine lives in.

- D2.13 — **The normal state of this machine is EC powered, probe unpowered, and unlike a plug-in probe the traces are permanently connected.** The EC's UART TX idles high, so with `3V3_PROBE` absent it drives 3.3 V through 200 Ω into an RP2040 input whose rail is at zero: current through the clamp diode, an attempt to raise `3V3_PROBE`, and a partially-powered RP2040 in an indeterminate state. **At roughly 16 mA continuous this is a genuine backfeed condition, and here it is the everyday case rather than an edge case.**
  NOTE: `SWCLK`, `SWDIO` and `RUN` do not have this problem — they are probe-driven, and the EC only drives `SWDIO` inside transactions that by definition need a live probe.
- D2.14 — **The mitigation is a resistive divider from debug VBUS to an EC GPIO, and the EC's UART TX is an input until it reads high.** `DBG_VBUS_SENSE` is the only thing standing between the design and a permanent backfeed, so it is a functional requirement of the EC firmware and not a nicety: **TX configured as UART only while a probe is detected, Hi-Z otherwise.**
  NOTE: The same signal tells the EC that a debugger is attached, which composes with the Debug Agent's `DEBUG_ENABLE` strap ([R.18](sec_ai_r#r18)) and permits automatic redirection of the kernel console. **What it must not do is quietly become a second, undocumented enable** — the relationship between the two has to be defined rather than discovered (→ [Q122](sec_ai_q#q122)).
- D2.15 — **`EC_RUN_N` is open-drain: the probe pulls low or releases, and never drives high.** The RP2354B's `RUN` has an internal pull-up, so high impedance already means "not in reset" and driving high adds nothing — while **driving high would inject probe-domain current into an unpowered machine domain**, the exact failure this section exists to prevent. It also gives the safe default: under stock firmware GPIO20 is never configured, stays an input, and the EC runs normally.

| State | `3V3_PROBE` | `3V3_AON` | Concern |
|---|---|---|---|
| S1 | Absent | Present | **The normal operating state.** The EC could drive `DBG_UART_TX` into an unpowered probe — answered by the gating of [D2.14](sec_ai_d2#d214) |
| S2 | Present | Present | A debug session. No concern |
| S3 | Present | Absent | Rescue with no battery. The probe could drive four lines into an unpowered EC |
| S4 | Absent | Absent | Fully unpowered. No concern |

- D2.16 — **S1 is where the machine spends its life and it is fully answered by the gating.** `DBG_SWDIO` is not a second offender there — its idle state is governed by the RP2354B's internal pull-up, giving tens of microamperes through the 200 Ω path — **but that pull-up is assumed rather than confirmed, and if the pin floats or pulls down, `SWDIO` needs the same treatment as TX** — which is [[!blocking]] on capture (→ [Q120](sec_ai_q#q120)).
- D2.17 — **S3 is real during early bring-up, when there is no battery in the machine, and it is answered in firmware rather than in copper.** RP2040 GPIOs default to inputs at reset, so every target-facing line is high impedance until the probe firmware configures it; `EC_RUN_N` cannot source; and **the probe firmware shall not configure `DBG_SWCLK` or `DBG_UART_RX` as outputs until a host session is established.**

| Backfeed, per line, driver at 3.3 V through 200 Ω into a clamp | Current |
|---|---|
| Ignoring the clamp forward drop | 16.5 mA |
| With a 0.6 V clamp drop | 13.5 mA |
| Aggregate, state S3, four lines driven | ≈ 54 mA |

- D2.18 — **54 mA will not raise `3V3_AON` against the load of the RP2354B, the CH224K, the BQ25896 and the MAX17048 — and that is not reassuring, it is the problem.** It is more than enough to leave those devices partially biased and behaving strangely, which is why [D2.17](sec_ai_d2#d217) demands firmware-side high impedance instead of trusting the current limit. **Both figures must be checked against the RP2354B's absolute maximum clamp current**; if the margin is thin, termination rises to 220 Ω or 330 Ω per side and the slew-rate cost is re-evaluated against the maximum SWD clock — [[!blocking]] on capture, because it is a schematic change rather than a layout one (→ [Q119](sec_ai_q#q119)).
- D2.19 — **The VBUS divider is itself a crossing, and it is sized as one rather than for power economy** — with the probe connected and the machine dead, its current flows into an EC GPIO clamp. Targets: **total series resistance ≥ 150 kΩ**, well under 50 µA worst case; output between 1.5 V and 3.0 V at VBUS = 5.0 V; below 0.4 V at VBUS = 0 V, guaranteed by the lower leg; optional 100 nF at the GPIO for hot-plug noise. **100 kΩ / 56 kΩ is the worked candidate** — about 1.79 V and 32 µA — with final values fixed at capture against the EC's input threshold.

## Layout — five constraints, and the plot is the review.

- D2.20 — **All eight signal-side 100 Ω resistors within 3 mm of their IC pins**, per the specification's requirement that the series pair drive only pin capacitance and the shortest possible trace. **Continuous ground reference under the whole six-net crossing region** — no split, no moat, no ferrite ([D2.10](sec_ai_d2#d210)). `DBG_SWCLK` and `DBG_SWDIO` routed as a loosely coupled pair, matched within 5 mm, on the same reference plane for their full length. **The `3V3_PROBE` pour confined to the probe area with one clearly identifiable boundary — visual separability of the two pours on the plot is a review criterion**, because it is the only cheap way to catch a crossing that was never meant to exist. And the debug USB pair length-matched at 90 Ω differential, kept clear of the probe's own QSPI flash bus.
  NOTE: **Any seventh crossing introduced during capture is a design error** and must be justified against this sheet rather than merged into it.

## Firmware — a fork, and it was always going to be one.

- D2.21 — **The probe runs a fork of `debugprobe` built from source**, because three things the design needs are not in the stock binary: **`RUN` assertion and release** against the RP2354B, needed for reliable attach and for holding the EC in reset while the tier-3 pads are used; **status LED behaviour** appropriate to an integrated part rather than a dongle; and **reporting of `DBG_VBUS_SENSE`** where it is useful. Host side is the **Raspberry Pi OpenOCD fork with `interface/cmsis-dap.cfg` and `target/rp2350.cfg`** — and the minimum acceptable OpenOCD revision is recorded once it has been verified against hardware, not before.
  NOTE: `picotool` speaks its protocol over USB only and is still unavailable here ([D1.13](sec_ai_d1#d113)); OpenOCD remains the tool for this project.

## Tiers, revised — the numbering survives, the transports and the priorities do not.

| Tier | Mechanism | Depends on | Writes EC flash | Status |
|---|---|---|---|---|
| 1 | SWD through the integrated probe | EC core reachable, debug not OTP-locked | Yes | **Primary** |
| 2 | Bootrom UART boot on `QSPI_SD2`/`SD3`, 1 Mbaud | Bootrom, an OTP enable bit, a RAM flash-writer stub | No — SRAM only | Deprioritised |
| 3 | Direct SPI to the EC flash die, `RUN` held low | Nothing | Yes | **Bring-up diagnostic**, conditional |

- D2.22 — **Tier 3 was written up as the primary recovery path and that is withdrawn.** It was decided before tier 1's reach was properly characterised. **Tier 1 recovers a blank flash, a corrupt flash, a machine with no power of its own, and — through the RP2350 rescue debug port — a device resetting itself in a loop** ([D1.13](sec_ai_d1#d113)), which leaves tier 3 almost no recovery territory. What survives is genuinely valuable and is a different job: **at E0, reading the flash JEDEC ID with `RUN` held low confirms supply integrity, the QFN-80 solder joints and QSPI pad continuity in one step, depending on no probe firmware, no OpenOCD and no multidrop configuration.** When SWD will not attach, that separates an assembly fault from a software fault, and nothing else on the board does.
  NOTE: **The 8-pin `J-QSPI` header of [D1.15](sec_ai_d1#d115) is withdrawn with it.** These six pads are a test-pad footprint beside the package, reached by pogo pin or clip, and **no cable, connector or probe connection shall be fitted to these nets** — the alternative, wiring them to the RP2040 through damping resistors, buys convenience for a test run twice in the project's life at the cost of permanent stubs on an XIP bus and six probe GPIO. It closes [Q108](sec_ai_q#q108).
  NOTE: **Abandonment condition, delegated to layout and not requiring this sheet to be reopened.** If the six pads cannot sit immediately adjacent to the package without either compromising the QSPI bus routing or extending a stub beyond a few millimetres, **tier 3 is dropped entirely** — V9 and V10 go, the QSPI row of the pin table goes, and the tier table reduces to two. **A clean XIP bus is worth more than a diagnostic that may be exercised twice.**
- D2.23 — **Tier 2 is squeezed between two stronger neighbours and is deprioritised accordingly.** It downloads to SRAM and executes from SRAM; the bootrom does not write flash, so reflashing needs the RAM flash-writer stub as a real deliverable. But SWD works against a blank flash and the rescue port recovers a self-resetting device, while tier 3 needs no chip cooperation at all — **tier 2 only contributes where SWD is physically broken but the bootrom is reachable.** The pads cost nothing, since `SD2` and `SD3` are a subset of the six already brought out, so **the hardware stays and the stub comes off the critical path before E0** (→ [Q107](sec_ai_q#q107)).
  NOTE: The bootrom's UART boot path is believed to need an **OTP enable bit** rather than being live by default. If that is confirmed, tier 2 is not a rescue mechanism at all but something that must be provisioned during manufacture, which is a different thing to plan for (→ [Q121](sec_ai_q#q121)).
- D2.24 — **The chassis carries two USB-C receptacles with entirely different functions, and they must be visually differentiated** — icon, colour or recess. **Inserting the charger into the debug port is the obvious user error**, and the debug port never sources VBUS while the other never sinks data. Add the pinhole aligned to the probe's `BOOTSEL`, and place the debug receptacle at a board edge: no internal adapter cable.

| BOM delta | Qty | Note |
|---|---|---|
| RP2040 QFN-56 | 1 | The probe |
| QSPI flash 2 MB | 1 | W25Q16-class, the probe's own boot flash |
| Crystal 12 MHz + load caps | 1 | |
| LDO 3.3 V from VBUS | 1 | ≥ 150 mA, AP2112K-class — `3V3_PROBE` |
| USB-C receptacle, 16-pin UFP | 1 | |
| CC pull-down 5.1 kΩ | 2 | One per CC pin, individually |
| ESD array on `D+`/`D−` | 1 | |
| Series termination 100 Ω 1 % | 9 | 4 probe-side, 4 EC-side, 1 on `RUN` |
| `BOOTSEL` switch + 1 kΩ | 1 | Pinhole access; the 1 kΩ keeps it off the flash chip select |
| VBUS sense divider | 2 | ≥ 150 kΩ total |
| Status LEDs + resistors | 2–5 | |
| Decoupling | — | Per the RP2040 hardware design guide |

## Do not do — the irreversible ones, kept together.

- D2.25 — **Do not burn the OTP bits that disable debug.** They are the only mechanism that closes tier 1 and tier 3 at the same stroke, and this EC has no threat model that asks for them ([D1.19](sec_ai_d1#d119)).
- D2.26 — **Do not bridge `3V3_PROBE` to `3V3_AON`** ([D2.7](sec_ai_d2#d27)) · **do not split the ground between the domains** ([D2.10](sec_ai_d2#d210)) · **do not drive `EC_RUN_N` high** ([D2.15](sec_ai_d2#d215)) · **do not fit a connector to the tier-3 pads** ([D2.22](sec_ai_d2#d222)) · **do not let the EC drive UART TX with the probe unpowered** ([D2.14](sec_ai_d2#d214)).

## Gate plan · V1–V10 — staged, each stage gated on its own.

- [ ] V1 — **Probe enumerates standalone**, EC held in reset. The host sees a CMSIS-DAPv2 interface and a CDC ACM interface.
- [ ] V2 — **OpenOCD attaches to the RP2354B.** Valid `DPIDR` read; both cores examined without error. This is the gate that would have caught option C ([D2.2](sec_ai_d2#d22)).
- [ ] V3 — **A minimal binary flashed and run on the EC over SWD**, with observable output.
- [ ] V4 — **The UART console**, bidirectional between the EC and a host terminal ([R.19](sec_ai_r#r19)).
- [ ] V5 — **Backfeed measured in state S1** — probe unpowered, EC running. `3V3_PROBE` below 0.3 V and the current into the probe domain below the clamp limit.
- [ ] V5b — **Backfeed measured in state S3** — probe powered, battery removed. `3V3_AON` below 0.3 V with the probe idle, and every target-facing probe pin verified Hi-Z before the session starts.
- [ ] V6 — **VBUS sense and TX gating.** EC TX confirmed Hi-Z with the probe absent; UART functional within 100 ms of insertion.
- [ ] V7 — **`RUN` control** — the EC held in reset and released under probe command.
- [ ] V8 — **Probe `BOOTSEL` recovery.** The host mounts `RPI-RP2` and a UF2 load restores the firmware. This is the hard floor of [D2.3](sec_ai_d2#d23), and it is verified rather than assumed.
- [ ] V9 — **Tier-3 bring-up diagnostic** — JEDEC ID read from the EC's internal flash through the test pads, `RUN` held low, with external SPI equipment. Dropped if the abandonment condition of [D2.22](sec_ai_d2#d222) fires.
- [ ] V10 — **XIP integrity with the test-pad stubs present** — the EC executes from flash at maximum clock with no errors over a sustained run. Dropped with V9.
  NOTE: **Schematic capture is gated on V1–V8 being defined and on [Q118](sec_ai_q#q118), [Q119](sec_ai_q#q119) and [Q120](sec_ai_q#q120) being closed** — the buffer part, the clamp current limit and the `SWDIO` idle bias. The remainder ([Q121](sec_ai_q#q121)–[Q124](sec_ai_q#q124)) can close during capture.
