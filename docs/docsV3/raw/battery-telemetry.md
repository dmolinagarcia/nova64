# noVa64 — Battery Telemetry Path (EC → CPU)

**Project:** noVa64
**Status:** Specification, not yet implemented
**Companion documents:** `sheet-1-1-power.md` (REV C), `helium-power-control.md`, `kernel-power-management.md`
**Closes:** open item 1 of `kernel-power-management.md`

---

## 1. Problem

The battery instruments live on the AON I2C bus and belong to the EC: the MAX17048 fuel gauge and the BQ25896's internal ADC. The 65816 cannot reach either. It can only read Helium.

A path is therefore needed from EC to CPU. Three properties are required of it:

- **The CPU must never be blocked** waiting on an I2C transaction it did not initiate and cannot bound.
- **Reads must not tear.** Multi-byte values updated asynchronously by the EC and read a byte at a time by an 8-bit CPU will otherwise produce impossible combinations — a voltage's low byte from one sample and its high byte from the next.
- **Stale data must be detectable.** If the EC hangs, the CPU must be able to tell, rather than reporting a battery percentage frozen at whatever it was when the EC died.

---

## 2. Topology

```
MAX17048 ──┐
           ├── I2C-AON ── RP2040 ── Debug Agent SPI ── Helium telemetry
BQ25896  ──┘                                            register block
                                                              │
                                                        bank $FF
                                                              │
                                                          65816
```

The EC polls, converts, and pushes. The CPU only ever performs register reads from bank $FF, which are cheap and bounded.

### Rejected alternative

The EC could write telemetry into SRAM through the Debug Agent's existing bus-access path, and the kernel could read it as ordinary memory. This was rejected: those accesses go through the cache controller and the arbiter, which makes battery reporting depend on the health of the memory subsystem. The power subsystem is specified to be pure register logic precisely so that it keeps working when the memory subsystem does not. Telemetry is part of that subsystem and inherits the constraint.

Cost of the chosen approach: roughly 16 bytes of register file, doubled for the snapshot mechanism of §4, so about 256 flip-flops in Helium. Acceptable against a spare-pin and spare-LUT budget that is no longer tight.

---

## 3. Unit conversion belongs to the EC

**Design rule: the EC publishes values in human units. The CPU never sees a raw sensor code.**

The 65816 has no multiplier and no division. Making the kernel convert a MAX17048 VCELL code at 78.125 µV per LSB, or a CRATE code at 0.208 % per hour per LSB, would burn cycles on arithmetic the RP2040 does in nanoseconds. The EC does all scaling, sign extension and clamping before it pushes.

Conversions performed by the EC:

| Source | Raw form | Published as |
|---|---|---|
| MAX17048 VCELL | 78.125 µV/LSB | millivolts, unsigned 16-bit |
| MAX17048 SOC | upper byte = 1 %, lower byte = 1/256 % | integer percent 0–100, plus a separate 1/256 fraction byte |
| MAX17048 CRATE | 0.208 %/hr per LSB, signed; negative is discharging | tenths of a percent per hour, signed 16-bit |
| BQ25896 ADC | device-specific codes | millivolts / milliamps, unsigned 16-bit |
| BQ25896 CHRG_STAT | 2-bit field | enumerated byte |

---

## 4. Coherence

Two atomic copies, one in each direction. Each is a single-cycle parallel transfer in Helium's core clock domain.

```
   EC writes bytes over SPI
            │
            ▼
      STAGING bank  ──[TELEM_COMMIT]──►  LIVE bank  ──[TELEM_LATCH]──►  SHADOW bank
                                                                             │
                                                                    CPU reads at leisure
```

- The EC writes the staging bank one byte at a time over SPI, in any order, taking as long as it likes. Nothing observes staging.
- When the EC has written every field it intends to update, it sets **`TELEM_COMMIT`** in its SPI control register. Helium copies staging → live in one cycle and increments `TELEM_SEQ`.
- The CPU writes **`POWER_CTRL ← TELEM_LATCH`** before reading. Helium copies live → shadow in one cycle. The CPU then reads the shadow bank byte by byte with no possibility of tearing, however slowly it goes and however many times the EC commits meanwhile.

The kernel needs no retry loop and no comparison of before-and-after generation counters. A latch, then a read.

### Alternative considered

A seqlock — a single bank plus a generation counter that the CPU reads before and after, retrying on mismatch — would save the shadow bank's ~128 flip-flops. It was rejected because it moves complexity into kernel code that runs on the constrained processor, and because a retry loop against an EC that is committing rapidly has no hard bound. The flip-flops are cheaper than the reasoning.

---

## 5. Staleness

`TELEM_AGE` counts eighths of a second since the last commit, saturating at 255 (≈32 s). Helium clears it on every commit and increments it on a divided core clock.

The kernel treats age above a threshold as invalid data. With a nominal 1 Hz update cadence, a threshold of **8** (one second of missed updates) is a reasonable default and leaves room for the EC to be briefly busy.

`PWR_F_TELEM_STALE` in the published flags is derived by the kernel from `TELEM_AGE`, not by Helium. Helium supplies the raw counter and takes no view.

This costs one byte and one small counter, and it is the difference between "the OS reports the battery is at 47 %" and "the OS reports it does not currently know."

---

## 6. Register map — CPU side (bank $FF)

Sixteen consecutive addresses in the power region. All read-only. All read from the shadow bank.

| Offset | Name | Type | Description |
|---|---|---|---|
| `+$00` | `TELEM_SEQ` | u8 | Increments on each commit; wraps. Lets the kernel detect that new data arrived without comparing every field |
| `+$01` | `TELEM_AGE` | u8 | Eighths of a second since last commit, saturating at 255 |
| `+$02` | `BATT_SOC` | u8 | State of charge, 0–100 % |
| `+$03` | `BATT_SOC_FRAC` | u8 | Fractional part, 1/256 % |
| `+$04` | `BATT_MV_L` | u8 | Cell voltage, millivolts, low byte |
| `+$05` | `BATT_MV_H` | u8 | high byte |
| `+$06` | `BATT_RATE_L` | i16 | Charge rate, tenths of a percent per hour, signed; negative is discharging |
| `+$07` | `BATT_RATE_H` | | |
| `+$08` | `PWR_FLAGS` | u8 | See below |
| `+$09` | `CHG_STATE` | u8 | 0 = not charging, 1 = pre-charge, 2 = fast charge, 3 = terminated |
| `+$0A` | `VBUS_MV_L` | u16 | Adapter voltage, millivolts |
| `+$0B` | `VBUS_MV_H` | | |
| `+$0C` | `SYS_MV_L` | u16 | SYS node voltage, millivolts |
| `+$0D` | `SYS_MV_H` | | |
| `+$0E` | `ICHG_MA_L` | u16 | Charge current, milliamps |
| `+$0F` | `ICHG_MA_H` | | |

### `PWR_FLAGS`

| Bit | Name | Meaning |
|---|---|---|
| 0 | `ON_ADAPTER` | Running from the adapter |
| 1 | `CHARGING` | Charge current is flowing |
| 2 | `BATT_LOW` | Below the low threshold |
| 3 | `BATT_CRIT` | Below the critical threshold; shutdown imminent |
| 4 | `PD_ACTIVE` | A high-voltage PD contract is in force |
| 5 | `GAUGE_FAULT` | The EC could not read the MAX17048 |
| 6 | `CHARGER_FAULT` | The BQ25896 is reporting a fault |
| 7 | `BATT_ABSENT` | No battery detected |

`GAUGE_FAULT` matters: an I2C failure must be visible as a fault, not as a plausible-looking zero.

---

## 7. Additions to existing registers

These extend `helium-power-control.md`.

### `POWER_CTRL` — new commands

| Value | Command | Effect |
|---|---|---|
| `$20` | `TELEM_LATCH` | Copy live → shadow. Synchronous, completes within the write cycle |
| `$21` | `CLEAR_TELEM_EVENT` | Clear `TELEM_EVENT` in `POWER_STATUS` |

`TELEM_LATCH` is **not** privileged. Reading the battery level is a legitimate unprivileged operation and the shadow bank is read-only, so exposing the latch grants nothing. This is a deliberate exception to the rule that `POWER_CTRL` writes are privileged, and the gateware must implement it as such rather than by accident.

### `POWER_STATUS` — bit 6

| Bit | Name | Meaning |
|---|---|---|
| 6 | `TELEM_EVENT` | A discrete power event has occurred since last cleared |

### EC-side SPI control

| Bit | Name | Effect |
|---|---|---|
| — | `TELEM_COMMIT` | Copy staging → live, increment `TELEM_SEQ`, clear `TELEM_AGE` |
| — | `TELEM_EVENT_SET` | Set `TELEM_EVENT` and raise the CPU interrupt |

Like `EC_PWR_STATE`, the telemetry staging registers sit in the always-readable/writable region of the SPI map, **outside the `DEBUG_ENABLE` gate**. Battery reporting is a production function.

---

## 8. Interrupts

The CPU is **not** interrupted on every commit. At 1 Hz that would be a needless context switch to observe a number that changed by a fraction of a percent.

The EC sets `TELEM_EVENT_SET` only on discrete transitions:

- adapter connected or removed
- charging started, completed, or faulted
- battery crossed the low or the critical threshold
- gauge or charger fault appeared or cleared
- battery removed or inserted

Everything else is picked up by polling. The interrupt shares Helium's existing aggregation and is level-based until cleared with `CLEAR_TELEM_EVENT`.

---

## 9. Update cadence

| Condition | Interval |
|---|---|
| Normal, on battery | 1 s |
| Normal, on adapter | 1 s |
| Immediately after a discrete event | push at once, then resume |
| EC dormant, machine off | 60 s, gauge only |

The MAX17048 updates VCELL every 250 ms in active mode, so a 1 Hz poll is well inside the instrument's own rate. There is nothing to gain from polling faster, and the I2C traffic costs EC wake time.

The gauge's ALRT pin is wired to an AON GPIO. Configure `CONFIG.ATHD` for the low-battery threshold and enable the 1 % SOC-change alert; the EC then learns about crossings without polling for them.

---

## 10. Power-domain interaction

**Helium's telemetry registers are in the switched domain and lose their contents whenever 3V3_MAIN drops.** After every power-up, and after every resume from a state where the rails were down, the EC must repopulate staging and commit **before releasing the 65816 from reset**. Otherwise the first thing the kernel reads is zeros, and it cannot distinguish that from a flat battery.

This adds a step to the power-up sequence of `sheet-1-1-power.md` §7.1, between step 8 (CDONE verified) and step 11 (release the CPU):

> **8b.** EC reads the gauge and charger, writes staging, sets `TELEM_COMMIT`.

`TELEM_SEQ` should reset to zero on Helium reset, so a kernel that sees sequence zero with a fresh `TELEM_AGE` knows it is reading the first commit after boot rather than a wrap.

---

## 11. Kernel side

### Revised state structure

```c
struct pwr_state {
    uint8_t  flags;        /* PWR_F_*                              */
    uint8_t  request;      /* PWR_REQ_NONE / POWEROFF / REBOOT     */
    uint8_t  source;       /* PWR_SRC_*                            */
    uint8_t  battery_pct;  /* 0-100                                */
    uint16_t battery_mv;   /* cell voltage, millivolts             */
    int16_t  battery_rate; /* tenths of a percent per hour, signed */
    uint16_t minutes_left; /* estimate; 0xFFFF = unknown           */
    uint8_t  charge_state; /* CHG_STATE                            */
    uint8_t  telem_age;    /* eighths of a second                  */
};

#define PWR_F_ON_ADAPTER    0x01
#define PWR_F_CHARGING      0x02
#define PWR_F_BATTERY_LOW   0x04
#define PWR_F_BATTERY_CRIT  0x08
#define PWR_F_SHUTDOWN_REQ  0x10
#define PWR_F_TELEM_STALE   0x20
#define PWR_F_GAUGE_FAULT   0x40
#define PWR_F_BATTERY_ABSENT 0x80
```

### Driver read path

```
1. write POWER_CTRL <- TELEM_LATCH
2. read TELEM_AGE; if above threshold, set PWR_F_TELEM_STALE and stop
3. read the remaining shadow registers
4. compute minutes_left
5. fill the structure
```

Step 2 before step 3 deliberately: there is no point spending cycles on data already known to be stale.

### Time-to-empty

```
minutes_left = (battery_pct * 600) / |battery_rate|
```

with `battery_rate` in tenths of a percent per hour. All 16-bit integer arithmetic, one division.

Two cautions. **CRATE is noisy** — a load transient makes the instantaneous rate swing wildly, and an estimate derived from a single sample will jump around. The driver should keep a small ring of the last 8 rate samples and use their mean. **And a near-zero rate produces a nonsensical answer**: below about 1 tenth of a percent per hour, report `0xFFFF` (unknown) rather than a number in the thousands of minutes.

When charging, the same formula with a positive rate gives time-to-full. The sign of `battery_rate` selects the interpretation; the driver should not report a time-to-empty while the flags say charging.

### Notification

On `TELEM_EVENT`, the driver refreshes its state, clears the event, and posts a message to the session manager port so the GUI's battery indicator updates immediately rather than at the next poll. Ordinary percentage drift is picked up by the indicator polling `/dev/power` at whatever rate it likes — once every 10–30 seconds is ample.

---

## 12. Open items

| # | Item | Blocks |
|---|---|---|
| 1 | Base address of the 16-byte telemetry block within the bank $FF power region | Gateware and kernel headers |
| 2 | Low and critical battery thresholds, in percent and in millivolts | EC firmware; must agree with the battery-critical shutdown policy |
| 3 | Whether `TELEM_AGE` should also be exposed to the EC over SPI, so the EC can detect that its own pushes are not landing | Diagnostics only |
| 4 | Confirm the BQ25896 ADC conversion cadence and whether it must be triggered per reading or free-runs | EC firmware timing |
| 5 | Whether the gauge should be read at all in ship mode, and how SOC is recovered after a long shelf period | EC firmware; relates to MAX17048 quick-start behaviour on battery insertion |

Item 5 is worth attention: the MAX17048's ModelGauge state is lost when the part loses power, and its recovery relies on an open-circuit-voltage reading taken at a moment when the cell is at rest. A machine woken from ship mode straight into a heavy load may report a poor estimate for the first minutes.
