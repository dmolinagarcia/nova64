# Sheet 1.1 — Power Architecture (REV C)

**Project:** noVa64
**Status:** Design complete, not yet captured in KiCad
**Supersedes:** REV B (this session), `hoja-1-1-alimentacion.md` (REV A)
**Companion documents:** `helium-power-control.md` (register interface), `kernel-power-management.md` (OS interface), `battery-telemetry.md` (battery reporting)

---

## 0. Definitions

Terms used throughout, defined before first use.

- **SYS** — the system power node produced by the charger's power-path, fed from the battery or the adapter, whichever is available. Every downstream converter hangs off SYS.
- **AON (always-on) domain** — the set of loads that are powered whenever SYS exists. Contains the embedded controller and the devices it must reach while the rest of the machine is off.
- **Switched domain** — the set of loads whose rails the embedded controller can enable and disable at will.
- **EC (embedded controller)** — the RP2040, in its role as power sequencer, FPGA loader and system supervisor.
- **Power path** — a charger topology in which system load and battery charge current are supplied independently from the same input, so the system runs from the adapter while the battery charges, and runs correctly with a depleted or absent battery.
- **PD (Power Delivery)** — the USB-C protocol by which a sink requests a voltage above the 5 V default, negotiated over the CC conductors.
- **OTG (On-The-Go)** — a charger mode in which the device sources 5 V onto its own VBUS.
- **Ship mode** — a charger state that disconnects the battery from SYS entirely, reducing standby drain to leakage. The machine's deepest off state.
- **PG (Power Good)** — an open-drain output from a converter asserting that its output has reached regulation. Used here as the sequencing interlock.
- **Buck** — steps voltage down. **Boost** — steps up. **Buck-boost** — either, so the output may sit inside the input range.
- **Iq (quiescent current)** — a converter's own consumption at no load. Decisive for an always-on rail, irrelevant for a switched one.

---

## 1. Revision notes

### From REV A to REV B

| # | Change | Reason |
|---|---|---|
| 1 | Panel stays 10.1" (ER-TFT101-1, RGB TTL parallel); 14" deferred to a second board revision | Project scoping |
| 2 | ANX6345 eDP bridge removed | The 10.1" panel accepts parallel RGB directly, and the bridge consumed parallel RGB on its input, so it saved no FPGA pins while adding a 2.5 V rail, a 1.2 V rail, an I2C init sequence and cost |
| 3 | No 2.5 V rail | Existed only for the bridge's `dvdd25` |
| 4 | MCP73871 replaced by BQ25896 + CH224K | Pack grew to ~10 Ah; linear charging is not viable at the required current |
| 5 | 2× SDRAM added to the 3V3 budget, PSRAM removed | Follows the memory architecture decision |

### From REV B to REV C

| # | Change | Reason |
|---|---|---|
| 6 | **3V3 split into 3V3_AON and 3V3_MAIN** | The EC cannot switch the rail that powers it. Splitting the rail is what makes EC-controlled power management possible at all |
| 7 | **1.2 V changes from an LDO off 3V3 to a buck off SYS** | Required for correct iCE40 sequencing — see below. Also removes the 0.5 W thermal problem and recovers ~0.6 W |
| 8 | **Every switched rail gets an EN line to the EC** | Requirement: the EC powers the machine up and down in order |
| 9 | **Ordered shutdown path added, reaching the 65816** | Requirement: software-initiated shutdown and a physical button, both graceful |
| 10 | GT911 moved to its own I2C bus | Domain-crossing rule; see §5 |

### Corrections carried in this revision

Two errors from earlier drafts are recorded here so they are not reintroduced.

**The OTG boost cannot supply the USB-A host port.** An earlier proposal suggested the BQ25896's integrated OTG boost could replace the TPS61023. It cannot: the boost drives VBUS, physically the same node as the USB-C input, so a 9 V PD contract would place 9 V on any attached peripheral; and OTG only enables when VBUS is below the battery voltage, i.e. only when unplugged, so the host port would die whenever the machine is charging. The TPS61023 is retained. OTG remains available and unused.

**Holding CRESET_B low is not a mitigation for a rail-order violation.** REV B proposed this. It prevents configuration, not parasitic conduction through the I/O clamp structures into an unpowered core rail. The sequencing is fixed properly in this revision.

---

## 2. The iCE40 sequencing constraint

This constraint drives the whole topology, so it is stated before the power tree.

The iCE40 LP/HX datasheet specifies the power-up order as: **VCC and VCCPLL first; then VCC_SPI, any time after those; then VPP_2V5, after VCC, VCCPLL and VCC_SPI.** Remaining VCCIO banks may rise any time after VCC/VCCPLL. Each prior supply must reach 0.5 V before the next is raised, and all ramps must be monotonic. The same order is required when partially powered-down rails are brought back up, which matters directly here because the EC will do exactly that.

REV B violated this twice: VCCIO rose before VCC (the 1.2 V LDO was derived from 3V3), and VPP_2V5 also rose before VCC (the BAT54 was fed from 3V3).

REV C satisfies it by construction. The order the EC produces is:

```
1V2 (VCC + VCCPLL)  →  3V3_MAIN (VCC_SPI + all VCCIO)  →  VPP_2V5 (BAT54 from 3V3_MAIN)
```

`VPP_2V5` follows `3V3_MAIN` automatically through the diode, so it can never lead. Because the 1.2 V converter now sources from SYS rather than from 3V3, the dependency that made the wrong order unavoidable is gone.

---

## 3. Power tree

```
                    ┌─ CC1/CC2 ──► CH224K ──► requests 9 V     [AON]
   USB-C receptacle ┤
                    ├─ D+/D− ────► RP2040 (USB device: console + programming)
                    └─ VBUS ─────► BQ25896 VBUS   [3.9–14 V window, OVP 14 V]
                                       │
                                       ├──► BAT ──► LiPo 1S, 2× 5000 mAh parallel
                                       │              └──► MAX17048   [AON]
                                       │
                                       └──► SYS  [≥3.5 V floor, programmable]
                                              │
   ┌──────────────┬───────────────┬───────────┴────┬──────────────┬─────────────┐
   ▼              ▼               ▼                ▼              ▼             ▼
TPS63900       TLV62568       TPS63020         PT4110        TPS61023
buck-boost     buck           buck-boost       boost         boost
→ 3V3_AON      → 1V2          → 3V3_MAIN       → backlight    → 5 V
NO ENABLE      EN ◄─ EC       EN ◄─ EC         EN/PWM ◄─ EC   EN ◄─ EC
   │              │               │                │              │
   │              │               │            LED string    USB-A VBUS
   │              │               │                          + polyfuse
   │              │               │
   │              │               ├── VCC_SPI + all VCCIO banks, both iCE40
   │              │               ├── IS61WV102416 SRAM
   │              │               ├── 2× AS4C32M16SB SDRAM
   │              │               ├── W65C816S
   │              │               ├── panel VDD (HX8282 logic)
   │              │               ├── PCM5102A, GT911, microSD
   │              │               └── BAT54 + 100 nF ──► VPP_2V5, both iCE40
   │              │
   │              ├── VCC core, both iCE40
   │              └── VCCPLL per chip, via RC filter (100 Ω + 100 nF ‖ 10 µF)
   │
   └── RP2040 + W25Q16 flash
       CH224K VDD
       BQ25896 + MAX17048 I2C bus and its pull-ups
       power button GPIO
```

Everything on the AON rail is there because it must be reachable while the machine is off. Nothing else belongs there.

---

## 4. Block designs

### B1 — USB-C input and PD negotiation (CH224K)

The receptacle is a **sink only**; the board never sources power through it.

- Standalone resistor mode: **6.8 kΩ from CFG1 to GND requests 9 V.** CFG2/CFG3 unused.
- 9 V rather than 12 V: the BQ25896 trips input OVP at 14 V, and a 12 V contract leaves little headroom against hot-plug transients. 9 V × 2 A = 18 W, comfortably above requirement.
- **Do not connect CH224K to D+/D−.** It can negotiate legacy fast-charge protocols over the data lines, but those belong to the RP2040's USB device port. Forfeiting Quick Charge is the correct trade.
- VDD (3.0–3.6 V window, no internal HV regulator) from **3V3_AON**, so negotiation is possible before any switched rail exists.
- CC1/CC2 terminate at the CH224K. Do **not** additionally fit 5.1 kΩ pull-downs; that was the REV A arrangement for a non-PD sink and would conflict.

**Cold-start:** adapter supplies 5 V by default → BQ25896 power-path raises SYS → TPS63900 produces 3V3_AON → CH224K negotiates → adapter steps VBUS to 9 V with the EC already running. The BQ25896 is designed for this, but confirm the input-transition behaviour in the datasheet.

Checklist:
- [ ] ESD array on D+/D− and CC (USBLC6-2SC6 or equivalent)
- [ ] CH224K decoupling, 100 nF + 1 µF
- [ ] CFG1 resistor on an accessible bottom-side footprint so the contract can be changed during bring-up
- [ ] Receptacle shield grounding strategy
- [ ] Evaluate CH224A — pin-compatible successor, CH224K being phased out

### B2 — Charger and power path (BQ25896)

| Parameter | Value | Note |
|---|---|---|
| Input window | 3.9–14 V | 9 V sits mid-range |
| Input OVP | 14 V | Hard ceiling; never negotiate above 12 V |
| Charge current | Up to 3 A | Program ~2 A; 90–92 % efficient |
| Input current limit | 100 mA–3.25 A in 50 mA steps | I2C, plus autonomous optimizer |
| SYS floor | 3.5 V, programmable | System runs with battery flat or absent |
| Integrated ADC | VBUS, BAT, SYS, TS, charge current | Rail telemetry for the EC at no extra cost |
| OTG boost | 4.5–5.5 V, up to 2 A | Present, unused — see §1 |
| Package | WQFN-24, 4×4 mm, thermal pad | Hot air or paste + reflow |

- I2C to the EC on the **AON bus**, shared with the MAX17048 only. INT to an AON GPIO through 10 kΩ.
- 10 kΩ NTC at the battery connector to TS; configure the JEITA profile in firmware.
- **QON** provides the hardware-level power control described in §6.

Checklist:
- [ ] Inductor and capacitors per datasheet (1.5 MHz)
- [ ] Thermal pad vias into a ground pour — 2 A of charge current is real dissipation
- [ ] STAT and PG indicator LEDs
- [ ] I2C address 0x6B; confirm no collision with MAX17048 (0x36)

### B3 — Battery pack and fuel gauge

- **2× 5000 mAh 1S LiPo pouch cells in parallel:** ~10 Ah, ~37 Wh. Below the 100 Wh air-travel threshold.
- Parallel cells at 1S need no balancing, but **must be the same model and production lot and must be at the same state of charge when first connected together.** Charge them individually to matching voltage before the first parallel connection.
- Two cells rather than one 10 Ah cell is a mechanical choice: the pack can be split across the enclosure instead of occupying one thick volume.
- JST-PH connector with the NTC in the same harness.
- **MAX17048** at the battery terminals, I2C on the AON bus, ALRT to an AON GPIO. Retained despite the BQ25896's ADC: the ADC reports voltage, the MAX17048 models state of charge.

Checklist:
- [ ] 100 nF at the MAX17048
- [ ] AON bus pull-ups sized once for the bus, not per device
- [ ] Keyed connector or reverse-polarity protection — a reversed LiPo destroys the charger
- [ ] Fuse or PTC in the battery lead

### B4a — Always-on rail, 3V3_AON (TPS63900)

Buck-boost from SYS, **no enable pin control** — it lives whenever SYS lives.

- Load: ~60 mA typical with the EC active, ~5 mA with the EC in dormant mode.
- **Iq is the selection criterion, not current capability.** This rail is energised for the entire life of a charge, including while the machine is "off". The TPS63900 is specified for ultra-low quiescent current and is the right class of part. The TPS63001 is a conventional alternative if availability forces it, at a cost in standby drain.
- No EN control anywhere in the design: the only way to remove this rail is ship mode, which removes SYS itself.

Checklist:
- [ ] Inductor and capacitors per datasheet
- [ ] Confirm the chosen part's Iq against the standby target in §8
- [ ] 0 Ω isolation jumper and test point

### B4b — Main switched rail, 3V3_MAIN (TPS63020)

Buck-boost from SYS. **EN driven by the EC. PG returned to the EC.**

- Budget below gives ~0.82 A typical, ~1.12 A peak. The TPS63020 supplies ~2 A in this region — roughly 45 % margin.
- PS/SYNC tied for forced PWM: lower ripple at some cost in light-load efficiency. SDRAM and video timing make ripple the priority. Light-load efficiency does not matter on a rail that is switched off when idle.
- Supplies **VCC_SPI and every VCCIO bank** of both FPGAs, and feeds the VPP_2V5 diodes.

Checklist:
- [ ] 1.5 µH inductor per datasheet with adequate saturation rating
- [ ] EN pulled **down** so the rail is off by default at EC reset
- [ ] PG to an AON GPIO
- [ ] Feedback divider routed clear of the switch node
- [ ] 0 Ω isolation jumper and test point

### B5 — Core rail, 1V2 (TLV62568 or TPS62825)

Synchronous buck **from SYS**, not from 3V3. **EN driven by the EC. PG returned to the EC.**

Sourcing from SYS rather than 3V3 is what makes the correct iCE40 order possible (§2), and it eliminates the REV B thermal problem: an LDO dropping 3.3 V to 1.2 V at 250 mA dissipated ~0.5 W, which in SOT-23-5 would have been roughly a 100 °C rise.

- Load: two iCE40 HX8K cores, estimated 150 mA typical, 250 mA peak. Both candidate parts are rated well above this.
- **VCCPLL** on each chip: from 1V2 through an RC filter (100 Ω + 100 nF ‖ 10 µF) at the pin.
- **VPP_2V5** on each chip: BAT54 in series from **3V3_MAIN**, plus 100 nF. Standard iCE40 practice, avoids a dedicated 2.5 V regulator, and the ordering is correct by construction because 3V3_MAIN rises after 1V2.

Checklist:
- [ ] EN pulled **down**, off by default at EC reset
- [ ] PG to an AON GPIO
- [ ] Inductor per datasheet; keep the loop tight
- [ ] Verify measured core current at bring-up stage E1 against the 250 mA assumption
- [ ] 0 Ω isolation jumper and test point

### B6 — Backlight (PT4110)

- Fed from **SYS**, so the backlight works on adapter power with a flat battery.
- 10.1" panel: reserve ~1.5 W. **String voltage and current remain pending the ER-TFT101-1 module datasheet.**
- EN/PWM brightness from the EC. Enabled late in the sequence, disabled early in shutdown, so the user never sees an unconfigured panel.

Checklist:
- [ ] Inductor, Schottky and sense resistor sized once the string spec is known
- [ ] Confirm the module's backlight pins are exposed on the FPC and not internally driven

### B7 — USB-A host 5 V (TPS61023)

- SYS → 5 V, 500 mA. Independent of input state, so the host port behaves identically on battery and on adapter.
- EN from the EC: powered only once the PIO-USB HID stack is up.

Checklist:
- [ ] Polyfuse on the output
- [ ] ESD protection on the USB-A data pair
- [ ] Bulk capacitance at the receptacle to survive peripheral inrush

---

## 5. Domain-crossing rules

Splitting the 3V3 rail introduces a hazard that did not exist in REV B: while the switched domain is down, the EC is still alive at 3.3 V with GPIO connected to unpowered silicon. A driven-high output injects current through the target's ESD clamps and back-feeds the dead rail through an uncontrolled path. Three rules, all mandatory.

**R1 — Tri-state before power-down.** Before de-asserting any rail EN, the EC firmware must place every pin crossing into the switched domain in high-impedance: the FPGA configuration SPI, CRESET_B, the Debug Agent SPI including DBG_CSN, and the power handshake lines of §6. This is firmware, but it is a hardware-correctness requirement, not an optimisation.

**R2 — Pull-ups belong to the destination domain.** Every pull-up or pull-down on a net crossing the boundary must be tied to **3V3_MAIN**, never to 3V3_AON. An AON pull-up on a switched net back-feeds continuously and defeats R1 regardless of what firmware does.

**R3 — Two separate I2C buses.**

| Bus | Domain | Devices |
|---|---|---|
| I2C-AON | 3V3_AON | BQ25896, MAX17048 |
| I2C-SW | 3V3_MAIN | GT911 touch controller |

The charger and gauge must be reachable while the machine is off, so they must sit on AON. The GT911 must not, because its pull-ups would then back-feed it whenever the machine is off. Sharing one bus would require a bus isolator; giving the GT911 its own bus costs two pins on an RP2040 that has peripherals to spare and is strictly simpler.

**Verification during schematic review:** produce a net-by-net list of every connection crossing the AON/switched boundary and check each against R1–R3. This is the single most error-prone area of the sheet.

---

## 6. Power state machine and shutdown paths

### 6.1 Requirement

Shutdown must be initiable both by software running on the 65816 and by a physical button, and in both cases must be **graceful** — the kernel flushes the SD card before rails drop. Neither the CPU nor the kernel can cut power directly; only the EC can. The path from CPU to EC therefore has to be built, and it does not exist in the current Helium design.

### 6.2 Signalling

The 65816 reaches the EC through Helium, which is the only thing it can talk to.

- A **`POWER_CTRL`** write-only register in bank $FF, and a readable **`POWER_STATUS`**. Specified in `helium-power-control.md`.
- **Two dedicated handshake pins** between Helium and the RP2040, costing 2 of Helium's 13–22 spare pins:
  - `EC_PWR_REQ` — EC output, Helium input. The EC is asking the system to shut down.
  - `SYS_PWR_REQ` — Helium output, EC input. The system is asking to be shut down, or is acknowledging.
- The **reason** for an assertion (poweroff vs reboot vs acknowledge) is read by the EC over the existing Debug Agent SPI link. The pins carry attention; SPI carries detail. This avoids inventing a pulse-encoding scheme on a single wire.

### 6.3 The physical button

One momentary button, wired to **two destinations**:

- **BQ25896 QON** — gives wake-from-ship-mode and a firmware-independent hardware reset, both intrinsic to the charger.
- **An AON GPIO on the EC** — gives the EC press-duration discrimination.

Both connections are to the same button. The charger provides the floor of last resort; the EC provides the graceful path.

### 6.4 Three levels of shutdown

| Level | Trigger | Path | Covers |
|---|---|---|---|
| **1 — Ordered** | `poweroff` from the CPU, or a short press | Kernel flushes and confirms → EC drops rails in reverse order → ship mode | Normal operation |
| **2 — Forced** | Long press, ~4 s | EC drops rails without asking | A hung kernel |
| **3 — Hardware** | Very long press on QON | BQ25896 BATFET reset, no firmware involved | A hung EC |

### 6.5 Ordered shutdown, button-initiated

1. EC detects a short press on the AON GPIO.
2. EC asserts `EC_PWR_REQ`.
3. Helium latches the request, sets the pending bit in `POWER_STATUS`, and raises an IRQ to the 65816.
4. Kernel reads `POWER_STATUS`, learns the request came from the button, notifies processes, unmounts and flushes the SD card.
5. Kernel writes `POWER_CTRL ← ACK_POWEROFF`.
6. Helium asserts `SYS_PWR_REQ`.
7. EC reads the reason over SPI, then runs the power-down sequence of §7.

### 6.6 Ordered shutdown, software-initiated

Identical from step 5 onward. Steps 1–4 are replaced by the kernel writing `POWER_CTRL ← POWEROFF` of its own accord, having already flushed.

### 6.7 Watchdog

**Mandatory.** When the EC asserts `EC_PWR_REQ` it starts a **10-second timer**. If no acknowledgement arrives, it escalates to level 2 and drops the rails anyway. Without this, a kernel that hangs during flush leaves a machine that cannot be turned off except by the 4-second press — which is a poor experience and, more importantly, hides the fault.

The same timer applies to reboot.

---

## 7. Sequencing

### 7.1 Power-up

The EC governs everything. It is the first thing alive, running from W25Q16 flash on 3V3_AON.

1. Adapter or battery raises SYS through the charger power path.
2. TPS63900 brings up 3V3_AON. No gating; it simply follows SYS.
3. EC boots. **All switched-rail EN lines are low by default** (pull-downs), so nothing else is powered.
4. EC brings up I2C-AON, configures the BQ25896 (input limit, charge current, JEITA profile), reads the MAX17048. If the battery is below the operating threshold and no adapter is present, the EC stops here and signals the fault, rather than browning out mid-configuration.
5. EC asserts **1V2 EN**. Waits for PG.
6. EC asserts **3V3_MAIN EN**. Waits for PG. VPP_2V5 follows through the BAT54.
7. Rail order now satisfies §2. EC releases **CRESET_B** on both FPGAs and loads bitstreams over SPI.
8. EC verifies **CDONE** on both devices.
8b. EC reads the gauge and charger and pushes an initial battery telemetry commit into Helium, so the kernel does not read zeros on its first look. See `battery-telemetry.md` §10.
9. EC brings up I2C-SW and initialises the GT911.
10. EC enables the **backlight**, then the **USB-A 5 V boost**.
11. EC preloads the BIOS into SRAM, then releases the 65816 from reset.

### 7.2 Power-down

Reverse order. The datasheet does not specify a power-down sequence explicitly; reverse is the conservative choice and costs nothing.

1. 65816 held in reset.
2. USB-A 5 V boost disabled.
3. Backlight disabled — do this early so the user gets immediate feedback that the command was accepted.
4. FPGAs held in reset via CRESET_B.
5. **R1 tri-state pass:** every EC pin crossing into the switched domain goes high-impedance.
6. 3V3_MAIN EN de-asserted. VPP_2V5 falls with it.
7. 1V2 EN de-asserted.
8. EC either enters dormant mode (standby) or commands ship mode over I2C (full off).

Step 5 sits between 4 and 6 deliberately: after the FPGAs are quiescent, before their rails vanish.

---

## 8. Current budget and runtime

3V3_MAIN, 10.1" configuration:

| Load | Typical | Peak |
|---|---|---|
| W65C816S @ ~14 MHz | 15 mA | 25 mA |
| IS61WV102416 SRAM | 70 mA | 90 mA |
| 2× AS4C32M16SB SDRAM | 250 mA | 360 mA |
| 2× iCE40 HX8K, VCCIO banks | 160 mA | 220 mA |
| Panel logic (HX8282) | 200 mA | 250 mA |
| PCM5102A + GT911 + microSD | 95 mA | 130 mA |
| Misc, LEDs, pull-ups | 30 mA | 45 mA |
| **Total 3V3_MAIN** | **~0.82 A** | **~1.12 A** |

3V3_AON:

| Load | Active | EC dormant |
|---|---|---|
| RP2040 + W25Q16 | 50 mA | 3 mA |
| CH224K, MAX17048, BQ25896 logic | 10 mA | 2 mA |
| **Total 3V3_AON** | **~60 mA** | **~5 mA** |

1V2: ~150 mA typical, ~250 mA peak (two HX8K cores).

Drawn from SYS:

| Branch | Typical | Worst case |
|---|---|---|
| 3V3_MAIN (~90 % conversion) | 3.0 W | 4.1 W |
| 3V3_AON | 0.23 W | 0.31 W |
| 1V2 (~88 %) | 0.20 W | 0.34 W |
| Backlight | 1.5 W | 1.8 W |
| USB-A host, when loaded | 0 W | 2.8 W |
| **Total** | **~4.9 W** | **~9.4 W** |

**Runtime**, 37 Wh pack at ~92 % usable (34 Wh): **~7 h** typical, **~3.6 h** with backlight at full and a powered host port.

**Standby**, rails off with the EC dormant: ~17 mW, so the pack self-discharges before it flattens. **Ship mode**: charger leakage only; shelf life is limited by cell chemistry, not by the board.

**Charge time**: 18 W input, less ~5 W running system, but the charger is programmed to 2 A, so ~7.5 W into the cell. Empty to full while in use, **~5 h**; with the machine off, **~2.5 h**.

All figures are datasheet typicals. The SDRAM and FPGA VCCIO rows depend heavily on bus activity and are the two most likely to be wrong. Measure at bring-up stage E1 and revise this table.

---

## 9. Bring-up provisions

Every rail without exception gets:

- A **0 Ω series jumper**, so the converter can be tested into a dummy load before any downstream silicon sees voltage.
- A **labelled test point** on the output side of the jumper.
- Enough separation between test points to land two probes without shorting.

Stage E0 order: charger and SYS alone, no downstream jumpers fitted → 3V3_AON into a resistive load → 1V2 into a resistive load → 3V3_MAIN into a resistive load → fit jumpers one at a time, checking draw against §8 at each step.

Additionally, expose the rail EN lines on a header. Being able to drive a rail manually, independently of EC firmware, is worth a great deal on a board where the EC is also the thing being debugged.

---

## 10. Open items

| # | Item | Blocks |
|---|---|---|
| 1 | ER-TFT101-1 backlight specification (string voltage, current, FPC pin exposure) | PT4110 component selection |
| 2 | Allocation of the two Helium handshake pins (`EC_PWR_REQ`, `SYS_PWR_REQ`) alongside DBG_CSN | Helium pin budget, schematic capture |
| 3 | Verify BQ25896 behaviour when the input steps 5 V → 9 V mid-operation | Confidence in the cold-start path |
| 4 | Confirm TPS63900 availability and its actual Iq; otherwise re-derive the standby figure | Standby target only |
| 5 | Measure 3V3_MAIN and 1V2 draw at E1 and revise §8 | TPS63020 margin; may force a higher-current part |
| 6 | CH224K vs CH224A | BOM |
| 7 | Full net-by-net audit of AON/switched domain crossings against R1–R3 | Schematic review sign-off |
| 8 | Behaviour when the adapter is removed mid-shutdown, or attached mid-shutdown | EC firmware state machine |

Item 7 is the one most likely to produce a board-level bug that is hard to diagnose, and it deserves a dedicated review pass rather than being folded into general ERC.

---

## 11. Bill of materials — active devices

| Ref | Part | Function | Package |
|---|---|---|---|
| U1 | CH224K (or CH224A) | USB-PD sink, requests 9 V | ESSOP-10 |
| U2 | BQ25896 | 3 A switching charger, power path, I2C, QON | WQFN-24 4×4 |
| U3 | MAX17048 | Fuel gauge, I2C | TDFN-8 |
| U4 | TPS63900 | Buck-boost → 3V3_AON, ultra-low Iq | VQFN |
| U5 | TPS63020 | Buck-boost → 3V3_MAIN, EN + PG | VQFN-16 |
| U6 | TLV62568 / TPS62825 | Buck → 1V2, EN + PG | SOT-23-6 / QFN |
| U7 | PT4110 | Boost → backlight LED string | SOT-23-5 |
| U8 | TPS61023 | Boost → 5 V USB-A host | SOT-23-6 |
| D1–D2 | BAT54 | VPP_2V5 derivation, one per FPGA | SOD-323 |
| — | USBLC6-2SC6 | ESD on USB-C and USB-A data | SOT-23-6 |

Assembly note: several of these are QFN packages with thermal pads, assembled with hot air or paste and reflow. This is within the project's established assembly capability and is not a constraint on component selection. The standing constraint remains BGA, excluded unless an assembly house is engaged.

REV C adds one converter relative to REV B (the AON rail) and converts the 1.2 V LDO into a buck. Net cost: one extra inductor and one extra IC, in exchange for correct iCE40 sequencing, full EC power control, ~0.6 W recovered, and the thermal problem removed.
