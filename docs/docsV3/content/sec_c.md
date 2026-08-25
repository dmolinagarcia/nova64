# Power supply
> domains · sequencing · charger and pack · budget

REV C, and the revision that changes the shape of the sheet rather than its parts. A 1S pack behind a power-path charger still means the machine runs on USB with the battery flat or absent — but the 3.3 V rail is now **split in two**, and that split is what turns the RP2040 from a sequencer into an actual power controller. The registers, the shutdown paths and the battery telemetry that ride on it are [sheet S](sec_s).

- C.1 — The tree in one line: **USB-C sink → CH224K negotiates 9 V → BQ25896 charger with power-path → SYS → five converters.** Everything hangs off SYS and nothing off the battery terminals, so no rail cares whether the pack is charged, flat or missing.
  NOTE: It also makes the board bring-uppable with no pack at all: [C.5](sec_c#c5)'s SYS floor runs the machine from the adapter alone, which is what [E1.4](sec_p#e14) needs.
- C.2 — **The rail split is the whole revision.** REV B had one 3V3, which the EC ran from — and a controller cannot switch the rail that powers it. So 3V3 becomes **3V3_AON**, permanently on and carrying only what must be reachable while the machine is off, and **3V3_MAIN**, switched, carrying everything else. Every switched rail gets an `EN` line from the EC and returns `PG` to it.
  NOTE: The AON rail's tenants are exactly: the RP2040 and its W25Q16, the CH224K, the charger and gauge I2C bus with its pull-ups, and the power button GPIO. Nothing else belongs there, and the selection criterion for its converter is **Iq**, not current — it is energised for the entire life of a charge.
- C.3 — **Input and PD.** CH224K in standalone resistor mode, **6.8 kΩ from CFG1 to GND = 9 V**; CC1/CC2 terminate at it and the REV A 5.1 kΩ pull-downs come off, since a PD sink and a passive sink are different terminations and fitting both conflicts. 9 V rather than 12 because the BQ25896 trips input OVP at 14 V and 12 V leaves no headroom for hot-plug transients; 9 V × 2 A = 18 W is comfortably over requirement.
  NOTE: **Do not connect the CH224K to D+/D−.** It can negotiate legacy fast-charge protocols over the data lines, but those lines are the RP2040's USB device port — console and programming. Forfeiting Quick Charge is the correct trade. Its VDD comes from 3V3_AON, so negotiation is possible before any switched rail exists.
  NOTE: **Both USB connectors need ESD protection, and it is the only protection this sheet asks for that is not a rail provision.** On the USB-C input, `D+`, `D−` and VBUS go through a low-capacitance TVS array — a USBLC6-2 or equivalent — because they are nets a user physically touches and the RP2040's USB device port sits directly behind them. **The USB-A host port needs its own array for the same reason**, and its polyfuse is not a substitute: the fuse answers a peripheral that draws too much, the array answers a discharge, and the two fail in different directions.
- C.4 — **Cold start** is the one sequence nobody controls: the adapter supplies 5 V by default → the power-path raises SYS → the AON converter comes up → the EC boots → the CH224K negotiates → the adapter steps VBUS to 9 V **with the EC already running**. The BQ25896 is built for that input transition; confirming it in the datasheet is [Q32](sec_q#q32).
  NOTE: The order matters in one direction only: it is the *step* to 9 V that has to find the EC awake. An adapter that never negotiates leaves the machine on 5 V — inside the charger's 3.9–14 V window — so it still runs and still charges, only more slowly.
- C.5 — **Charger: BQ25896**, replacing the MCP73871. The pack grew to ~10 Ah and linear charging at that current is not viable — it is a switching charger at 90–92 %, 3 A capable and programmed to ~2 A, input window 3.9–14 V, input current limit in 50 mA steps over I2C, and a programmable **SYS floor of 3.5 V** so the system runs with the battery flat or absent. Its integrated ADC (VBUS, BAT, SYS, TS, charge current) gives the EC rail telemetry at no extra part cost.
  NOTE: The **OTG boost is present and deliberately unused.** An earlier proposal had it replace the TPS61023 for the host port; it cannot. The boost drives VBUS, physically the same node as the USB-C input, so a 9 V contract would put 9 V on an attached peripheral — and OTG only enables when VBUS is below the battery, i.e. only when unplugged, so the host port would die whenever the machine was charging.
- C.6 — **Pack: 2× 5000 mAh 1S LiPo in parallel** — ~10 Ah, ~37 Wh, under the 100 Wh air-travel line. Two cells rather than one thick one is a mechanical choice: the pack splits across the enclosure. Parallel 1S cells need no balancing, but **must be the same model and production lot, and must be at the same state of charge when first joined** — charge them individually to matching voltage before the first parallel connection. NTC in the same JST-PH harness, into the charger's TS pin, JEITA profile in firmware.
  NOTE: The NTC is not decoration — the charger's TS pin is what applies the JEITA profile, and an absent or open thermistor must read as a fault, not as room temperature.
- C.7 — **Fuel gauge: MAX17048**, kept despite the charger's ADC — the ADC reports a voltage, the gauge models a state of charge, and those are different claims. I2C on the AON bus, ALRT to an AON GPIO, `CONFIG.ATHD` set for the low-battery threshold plus the 1 % SOC-change alert, so crossings arrive without polling for them.
  NOTE: The gauge is where [S.19](sec_s#s19)'s rule earns its keep. Its reading reaches the CPU as a snapshot in Helium ([S.16](sec_s#s16)) carrying a `VALID` bit and an age, because a battery frozen at a plausible value is worse than one admitting it does not know.

## The iCE40 sequencing constraint — the reason the tree looks the way it does.

- C.8 — The datasheet order is **VCC and VCCPLL first · then VCC_SPI, any time after · then VPP_2V5, after all three**, each supply reaching 0.5 V before the next is raised, every ramp monotonic — and the same order again whenever partially powered-down rails are brought back up, which is precisely what the EC does on every resume. REV B violated it twice: VCCIO rose before VCC because the 1.2 V LDO hung off 3V3, and VPP_2V5 rose before VCC because the BAT54 did too.
  NOTE: REV B proposed holding `CRESET_B` low as the mitigation. It is not one — it prevents configuration, not parasitic conduction through the I/O clamp structures into an unpowered core rail. The order is fixed properly instead, and the fix is one wire: **the 1.2 V converter sources from SYS, not from 3V3**, which deletes the dependency that made the wrong order unavoidable.
- C.9 — The order the EC therefore produces is **1V2 (VCC + VCCPLL) → 3V3_MAIN (VCC_SPI + every VCCIO bank) → VPP_2V5**, and the last of those is automatic: VPP_2V5 is a BAT54 from 3V3_MAIN, so it can never lead. The constraint is satisfied by construction rather than by firmware discipline.

## The rails — five converters off SYS, one of them unswitchable by design.

| Rail | Part | Topology | Control | Load |
|---|---|---|---|---|
| **3V3_AON** | TPS63900 | buck-boost | **none** — lives whenever SYS lives | RP2040 + W25Q16 · CH224K VDD · charger/gauge I2C · power button |
| **1V2** | TLV62568 / TPS62825 | buck **from SYS** | `EN` ← EC · `PG` → EC | VCC of both iCE40s · VCCPLL through an RC filter per chip |
| **3V3_MAIN** | TPS63020 | buck-boost | `EN` ← EC · `PG` → EC | VCC_SPI + all VCCIO · SRAM · 2× SDRAM · W65C816S · panel logic · PCM5102A · GT911 · microSD · the VPP_2V5 diodes |
| **backlight** | PT4110 | boost | `EN`/PWM ← EC | LED string, ~1.5 W reserved |
| **5 V host** | TPS61023 | boost | `EN` ← EC | USB-A VBUS, 500 mA, behind a polyfuse |

- C.10 — Two details in that table carry weight. **`EN` is pulled down on every switched rail**, to 3V3_MAIN where the net crosses domains, so the machine is off by default at EC reset rather than relying on firmware to keep it off. And the TPS63020 runs **forced PWM** (PS/SYNC tied): ripple matters to SDRAM and to video timing, light-load efficiency does not matter on a rail that is switched off when idle.
- C.11 — Killing the LDO was worth more than the sequencing fix alone. Dropping 3.3 V to 1.2 V at 250 mA dissipated **~0.5 W** in a SOT-23-5 — roughly a 100 °C rise — and burned it as heat on a battery-powered machine. The buck recovers ~0.6 W and the thermal problem disappears with it.
- C.12 — The two boosts stay on **SYS, not VBAT** ([D09](sec_q#d09)), so backlight and host port behave identically on adapter and on battery. The backlight comes up **late** in the power-up order and goes down **early** in shutdown, so the user never sees an unconfigured panel and gets immediate feedback that a shutdown command was accepted.
  NOTE: The LED string's voltage and current are still pending the ER-TFT101-1 module datasheet, and with them the PT4110's inductor, Schottky and sense resistor (→ [Q1](sec_q#q1)).

## Domain crossings — three mandatory rules, and the sheet's most error-prone area.

- C.13 — Splitting the rail creates a hazard REV B could not have: while the switched domain is down, the EC is alive at 3.3 V with GPIO wired into unpowered silicon. A driven-high output injects current through the target's ESD clamps and back-feeds the dead rail through a path nobody designed. **R1 — tri-state before power-down**: before de-asserting any rail `EN`, the EC places every pin crossing into the switched domain in high-impedance — configuration SPI, `CRESET_B`, the debug agent's SPI including `DBG_CSN`, and the two power handshake lines. It is firmware, but it is a hardware-correctness requirement, not an optimisation. **R2 — pull resistors belong to the destination domain**: every pull on a crossing net ties to 3V3_MAIN, never to 3V3_AON, because an AON pull-up back-feeds continuously and defeats R1 whatever firmware does. **R3 — two separate I2C buses**, and [sheet X](sec_x) has since made it three.
  NOTE: **The third bus belongs to Helium and changes nothing about these two.** `I2C-EXP` ([X.22](sec_x#x22)) is the slow expansion tier, it lives wholly in the switched domain, its pulls tie to 3V3_MAIN under R2 like any other, and it crosses no domain boundary at all — so it adds no row to the crossings audit. What R3 forbids is a bus with two masters, not a third bus: the EC keeps I2C-AON and I2C-SW, Helium gets its own, and no arbitration logic is written anywhere.

| Bus | Domain | Devices | Why there |
|---|---|---|---|
| I2C-AON | 3V3_AON | BQ25896 · MAX17048 | Must be reachable while the machine is off |
| I2C-SW | 3V3_MAIN | GT911 touch | Its pull-ups would back-feed it whenever the machine is off |
| I2C-EXP | 3V3_MAIN | Helium's slow expansion tier — RTC candidate, slot connector ([X.22](sec_x#x22)) | A different **master**: Helium, not the EC. Two pins already carried in [Q8](sec_q#q8)'s allocation |

- C.14 — Sharing one bus would need a bus isolator; a second bus costs two pins on an RP2040 that has peripherals to spare, and is strictly simpler. **The audit that closes this sheet is a net-by-net list of every AON↔switched crossing checked against R1–R3** — not folded into general ERC, a pass of its own, because it is the likeliest source of a board-level bug that is hard to diagnose (→ [Q31](sec_q#q31)). [[!blocking]]

## Sequencing — the EC governs both directions; the CPU-facing half is [sheet S](sec_s).

| # | Power-up | # | Power-down |
|---|---|---|---|
| 1 | Adapter or battery raises SYS through the power path | 1 | 65816 held in reset |
| 2 | TPS63900 brings up 3V3_AON — no gating, it follows SYS | 2 | USB-A 5 V boost disabled |
| 3 | EC boots; every switched `EN` is low by its pull-down | 3 | Backlight disabled — **early**, so the user sees the command land |
| 4 | I2C-AON up; charger configured, gauge read. Below the operating threshold with no adapter, the EC **stops here and signals the fault** rather than browning out mid-configuration | 4 | Both FPGAs held in reset via `CRESET_B` |
| 5 | **1V2 EN** → wait for `PG` | 5 | **R1 tri-state pass** over every crossing pin |
| 6 | **3V3_MAIN EN** → wait for `PG`; VPP_2V5 follows through the BAT54 | 6 | 3V3_MAIN `EN` de-asserted; VPP_2V5 falls with it |
| 7 | `CRESET_B` released, bitstreams loaded over SPI | 7 | 1V2 `EN` de-asserted |
| 8 | `CDONE` verified on both devices | 8 | EC enters dormant mode, or commands **ship mode** over I2C for a true off |
| 8b | EC reads gauge and charger and **commits an initial telemetry snapshot** into Helium, so the kernel's first read is not zeros (→ [S.16](sec_s#s16)) | | |
| 9 | I2C-SW up, GT911 initialised | | |
| 10 | Backlight, then the USB-A 5 V boost | | |
| 11 | BIOS preloaded into SRAM, 65816 released from reset | | |

- C.15 — Power-down step 5 sits between 4 and 6 deliberately: **after** the FPGAs are quiescent, **before** their rails vanish. The datasheet specifies no power-down order, so reverse is taken as the conservative choice, and it costs nothing.
- C.16 — **The button is one switch wired to two destinations**: the charger's `QON` pin, which gives wake-from-ship-mode and a firmware-independent hardware reset intrinsic to the silicon, and an AON GPIO, which gives the EC press-duration discrimination. The charger is the floor of last resort; the EC is the graceful path. Three levels of shutdown fall out of that, in [S.6](sec_s#s6).

## Budget and runtime — datasheet typicals, to be replaced by measurement at E1.

| 3V3_MAIN load | Typ | Peak | | Drawn from SYS | Typ | Worst |
|---|---|---|---|---|---|---|
| W65C816S @ ~14 MHz | 15 mA | 25 mA | | 3V3_MAIN (~90 %) | 3.0 W | 4.1 W |
| System SRAM | 70 mA | 90 mA | | 3V3_AON | 0.23 W | 0.31 W |
| 2× AS4C32M16SB SDRAM | 250 mA | 360 mA | | 1V2 (~88 %) | 0.20 W | 0.34 W |
| 2× iCE40 HX8K VCCIO | 160 mA | 220 mA | | Backlight | 1.5 W | 1.8 W |
| Panel logic (HX8282) | 200 mA | 250 mA | | USB-A host, loaded | 0 W | 2.8 W |
| PCM5102A + GT911 + microSD | 95 mA | 130 mA | | **Total** | **~4.9 W** | **~9.4 W** |
| Misc, LEDs, pulls | 30 mA | 45 mA | | 3V3_AON, EC active | 60 mA | — |
| **Total 3V3_MAIN** | **~0.82 A** | **~1.12 A** | | 3V3_AON, EC dormant | 5 mA | — |

- C.17 — Against ~0.82 A typical the TPS63020 supplies about 2 A in this region — roughly 45 % margin. **The SDRAM and VCCIO rows are the two most likely to be wrong**, both being dominated by bus activity, and they are the two that would spend that margin; measuring them at E1 and revising this table is [Q33](sec_q#q33). 1V2 is ~150 mA typical and ~250 mA peak for two HX8K cores, well inside either candidate part.
  NOTE: Two rows are deliberately pessimistic against the rest of this document. The SRAM row was costed on the ×16 IS61WV102416, which [D15](sec_q#d15) replaced with a 1 MB ×8 part; and the SDRAM row counts **two** devices where [D14](sec_q#d14) leaves the second footprint unpopulated. Both err toward more current than the board will draw, which is the right direction for a converter sizing, but neither figure should be carried into a thermal or runtime claim without being re-derived.
- C.18 — **Runtime**: 37 Wh at ~92 % usable ≈ 34 Wh → **~7 h** typical, **~3.6 h** with the backlight at full and a powered host port. **Standby** with the rails off and the EC dormant is ~17 mW, so the pack self-discharges before it flattens; **ship mode** leaves charger leakage only, and shelf life becomes a property of the chemistry rather than of the board. **Charging** at the programmed 2 A: ~5 h empty-to-full while in use, ~2.5 h with the machine off.
  NOTE: These are bench figures derived from the table above, [C.17](sec_c#c17)'s two pessimistic rows included. The number that counts is measured inside the closed enclosure under sustained load, which is what [L6](sec_p#l6) exists for.
  NOTE: Ship mode exists because 17 mW is still a draw.
- C.19 — **Bring-up provisions, every rail without exception**: a 0 Ω series jumper so the converter can be tested into a dummy load before any downstream silicon sees voltage, a labelled test point on its output side, and enough separation between test points to land two probes without shorting. E0 order is charger and SYS alone → each rail into a resistive load → jumpers fitted one at a time, checking draw against the table above at every step (→ [E1.5](sec_p#e15)).
  NOTE: **Bring the `EN` lines out to a header too.** Driving a rail by hand, independently of EC firmware, is worth a great deal on a board where the EC is itself one of the things under test.
  NOTE: The jumpers stay fitted after bring-up. They are the isolation [E1.4](sec_p#e14) relies on to attribute a fault to one block, and pulling one is how a suspect rail leaves the machine without anything being unsoldered.
- C.20 — Net cost of REV C against REV B: one extra IC and one extra inductor — the AON converter — plus the 1.2 V LDO becoming a buck. Bought with it: correct iCE40 sequencing, full EC power control, ~0.6 W recovered and a 0.5 W hot spot deleted. [[open]]
  NOTE: Still open on this sheet, beyond the crossings audit: TPS63900 availability and its actual Iq (→ [Q34](sec_q#q34)), CH224K versus its pin-compatible successor CH224A as the K is phased out, and where the PCF8563 RTC now lives — [sheet H](sec_h) records it as unassigned and [X.22](sec_x#x22) offers `I2C-EXP` as the third answer (→ [Q35](sec_q#q35)).

![Fig. 2 — Power tree REV C: USB-C + CH224K → BQ25896 power-path → SYS, and the five converters hanging off it. The AON rail has no enable; every other rail is switched by the EC and reports PG back.](figures/fig-2-power.svg)
LEGEND: Trace legend: <span class="g">gold = power</span> · <span class="m">mint = EC control and telemetry</span> · dashed = I2C / status.
