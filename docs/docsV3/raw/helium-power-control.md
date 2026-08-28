# Helium — Power Control Interface

**Project:** noVa64
**Status:** Specification, not yet implemented in gateware
**Companion documents:** `sheet-1-1-power.md` (REV C), `kernel-power-management.md`, `battery-telemetry.md`
**Extends:** Helium bank $FF register map; Helium Debug Agent register map

---

## 1. Purpose

The 65816 must be able to request an ordered shutdown, and must be able to learn that the embedded controller (EC, the RP2040) has requested one on the user's behalf. Neither party can reach the other today: the CPU can only talk to Helium, and the EC talks to Helium over the Debug Agent SPI link. This document specifies the bridge.

The design principle throughout: **the pins carry attention, the SPI link carries detail.** A single wire encoding several meanings by pulse width is a source of bugs that are painful to diagnose on a board where both endpoints are also being brought up.

---

## 2. Physical signals

Two dedicated pins between Helium and the RP2040, in addition to the Debug Agent SPI link that already exists.

| Signal | Direction | Polarity | Meaning |
|---|---|---|---|
| `EC_PWR_REQ` | RP2040 → Helium | Active high, level | The EC is requesting that the system shut down |
| `SYS_PWR_REQ` | Helium → RP2040 | Active high, level | The system has a power request outstanding for the EC to service |

Both are level signals held until the transaction completes, not pulses. A level survives a missed edge; a pulse does not.

**Pin cost:** 2 of Helium's 13–22 spare I/O. Allocation is an open item in the pin budget, to be resolved together with `DBG_CSN`.

**Domain:** both pins cross from the AON domain (RP2040) to the switched domain (Helium). They are subject to rules R1–R3 of the power sheet: the EC must tri-state `EC_PWR_REQ` before dropping 3V3_MAIN, and any pull resistor on either net must be tied to 3V3_MAIN, not to 3V3_AON.

**Clock domain crossing:** `EC_PWR_REQ` is asynchronous to Helium's core clock and must pass through a two-flop synchroniser before use. The same applies to the acknowledgement path in the opposite direction inside the RP2040 firmware.

---

## 3. CPU-visible registers (bank $FF)

Consistent with the existing Helium control interface: commands are issued as writes where the address selects the command and the written value is the argument. Synchronous commands complete within the write cycle.

### 3.1 `POWER_CTRL` — write-only

Single-byte command register. All commands are synchronous and complete within the write cycle; none set BUSY.

| Value | Command | Effect |
|---|---|---|
| `$01` | `POWEROFF` | Kernel-initiated shutdown. Helium asserts `SYS_PWR_REQ` and records reason = poweroff |
| `$02` | `REBOOT` | Kernel-initiated restart. Helium asserts `SYS_PWR_REQ` and records reason = reboot |
| `$03` | `ACK_POWEROFF` | Acknowledges a pending EC-initiated request; the kernel has flushed and is ready |
| `$04` | `ACK_REBOOT` | As above, for a reboot request |
| `$05` | `NAK` | The kernel refuses or cannot service the request now |
| `$10` | `CLEAR_PENDING` | Clears the pending bit and any latched request. Privileged; intended for the kernel to reset state after a NAK |
| `$20` | `TELEM_LATCH` | Snapshot the battery telemetry bank for reading. **Not privileged** — see `battery-telemetry.md` §7 |
| `$21` | `CLEAR_TELEM_EVENT` | Clear `TELEM_EVENT` in `POWER_STATUS` |

Writes are privileged, with the documented exception of `TELEM_LATCH`. A write from a non-privileged context raises the standard permission fault via ABORTB, exactly as for other bank $FF registers, and has no effect on power state. This matters: an unprivileged process must not be able to shut the machine down.

`NAK` is deliberately provided. A kernel mid-way through an operation that must not be interrupted needs a way to decline, and the EC's watchdog (§6) bounds how long a refusal can delay things.

### 3.2 `POWER_STATUS` — read-only

| Bit | Name | Meaning |
|---|---|---|
| 0 | `REQ_PENDING` | An EC-initiated request is outstanding and awaiting acknowledgement |
| 1 | `REQ_REBOOT` | The pending request is a reboot; clear means poweroff |
| 2 | `SRC_BUTTON` | The request originated from the physical button |
| 3 | `SRC_BATTERY` | The request originated from a critical battery condition |
| 4 | `SRC_THERMAL` | Reserved for a future thermal trip |
| 5 | `ACK_SENT` | An acknowledgement has been issued and the EC has not yet acted |
| 6 | `TELEM_EVENT` | A discrete power/battery event has occurred since last cleared — see `battery-telemetry.md` |
| 7 | — | Reserved, read as zero |

Bits 2–4 are mutually exclusive and meaningful only while `REQ_PENDING` is set. They exist so the kernel can behave differently by cause: a button press deserves a user-visible dialogue and a chance to cancel, whereas a critical-battery request should flush immediately and without prompting.

### 3.3 Interrupt

Assertion of `REQ_PENDING` raises an interrupt to the 65816 through Helium's existing interrupt aggregation. The kernel's power driver services it.

The interrupt is level-based and remains asserted while `REQ_PENDING` is set, so a kernel that masks interrupts briefly cannot miss the request.

---

## 4. EC-visible registers (Debug Agent SPI)

The EC reads the reason for a `SYS_PWR_REQ` assertion over the existing SPI link.

### 4.1 `EC_PWR_STATE` — read-only over SPI

| Bit | Name | Meaning |
|---|---|---|
| 0 | `SYS_REQ` | Mirrors the `SYS_PWR_REQ` pin |
| 1 | `REQ_REBOOT` | The outstanding system request is a reboot |
| 2 | `IS_ACK` | This assertion is an acknowledgement of an EC request, not a fresh system-initiated one |
| 3 | `IS_NAK` | The kernel declined the EC's request |
| 4 | `CPU_ACK_SEEN` | The CPU has written to `POWER_CTRL` at least once since the request was raised |
| 5–7 | — | Reserved |

### 4.2 Access gating

**This register must be readable regardless of the `DEBUG_ENABLE` strap.** The Debug Agent's privileged register set is gated by that strap by design, but power management is a production function, not a debug function. Placing `EC_PWR_STATE` behind the strap would break shutdown on any board built with debug disabled.

`EC_PWR_STATE` is therefore specified as part of a small always-readable region of the SPI register map, alongside `DBG_ID`. It is read-only from SPI and carries no ability to initiate bus cycles, so exposing it grants no capability that the strap is meant to withhold.

---

## 5. Transaction sequences

### 5.1 EC-initiated ordered shutdown (button)

```
EC:      short press detected on AON GPIO
EC:      assert EC_PWR_REQ, start 10 s watchdog
Helium:  synchronise, latch request, set REQ_PENDING + SRC_BUTTON
Helium:  raise IRQ to 65816
Kernel:  read POWER_STATUS, identify source
Kernel:  notify processes, flush and unmount SD
Kernel:  write POWER_CTRL <- ACK_POWEROFF
Helium:  set ACK_SENT, assert SYS_PWR_REQ with IS_ACK
EC:      read EC_PWR_STATE over SPI, confirm IS_ACK
EC:      cancel watchdog, run power-down sequence (sheet 1.1 §7.2)
```

### 5.2 CPU-initiated ordered shutdown (software)

```
Kernel:  flush and unmount SD
Kernel:  write POWER_CTRL <- POWEROFF
Helium:  assert SYS_PWR_REQ, IS_ACK clear
EC:      read EC_PWR_STATE, see a fresh system request
EC:      run power-down sequence
```

No watchdog applies here: the kernel has already done its work before signalling.

### 5.3 Kernel declines

```
EC:      assert EC_PWR_REQ, start watchdog
Helium:  set REQ_PENDING, raise IRQ
Kernel:  write POWER_CTRL <- NAK
Helium:  assert SYS_PWR_REQ with IS_NAK
EC:      read EC_PWR_STATE, see IS_NAK
EC:      de-assert EC_PWR_REQ, cancel watchdog, take no further action
```

A second short press restarts the sequence. Whether a repeated refusal should escalate to a forced shutdown is an EC firmware policy question, left open.

### 5.4 Watchdog expiry

```
EC:      assert EC_PWR_REQ, start watchdog
Helium:  set REQ_PENDING, raise IRQ
Kernel:  (hung — no write to POWER_CTRL)
EC:      watchdog expires at 10 s
EC:      escalate to forced shutdown; drop rails without acknowledgement
```

The tri-state pass of rule R1 still runs. Forced does not mean careless.

### 5.5 Reboot

Identical to poweroff throughout, except that after rails are down the EC re-runs the power-up sequence instead of entering ship mode or dormant mode. From Helium's and the kernel's point of view the only difference is the `REQ_REBOOT` bit.

---

## 6. Timing

| Parameter | Value | Rationale |
|---|---|---|
| EC watchdog | 10 s | Long enough for an SD flush under load, short enough that a hang is not mistaken for a dead machine |
| Short press | < 1 s | Ordered shutdown |
| Long press | ~4 s | Forced shutdown at the EC, bypassing the CPU entirely |
| QON long press | Per BQ25896 datasheet | Hardware BATFET reset, bypassing the EC entirely |
| Synchroniser depth | 2 flops | Standard for an asynchronous level input |

The three press durations must be checked against the BQ25896's own QON timing so that the EC's 4-second forced shutdown does not collide with the charger's hardware reset threshold. If they overlap, the EC's threshold moves, not the charger's — the charger's is fixed in silicon.

---

## 7. Gateware notes

- The request latch must be **reset-safe**: a Helium reset while a request is pending must clear `REQ_PENDING` rather than leave a phantom request that immediately re-interrupts the kernel on boot.
- `SYS_PWR_REQ` must remain asserted across a CPU reset. If the kernel acknowledges and the CPU then resets before the EC services the pin, the shutdown must still complete.
- The power registers are small and must not depend on the cache controller, the arbiter, or SDRAM. They are pure register logic, so that shutdown works even when the memory subsystem is wedged — which is precisely when a user reaches for the button.

That last point is the design constraint that matters most in this document. The power path must be the most robust thing in Helium, because it is the escape hatch for everything else.

---

## 8. Open items

| # | Item | Blocks |
|---|---|---|
| 1 | Pin allocation for `EC_PWR_REQ` and `SYS_PWR_REQ`, resolved with `DBG_CSN` | Schematic capture |
| 2 | Confirm the always-readable SPI region can be carved out without disturbing the existing Debug Agent map | Gateware |
| 3 | EC policy on repeated NAK | EC firmware |
| 4 | BQ25896 QON press-duration thresholds, measured against the EC's 4 s | EC firmware, user experience |
| 5 | Whether critical-battery shutdown should be able to bypass NAK | Kernel and EC policy |
