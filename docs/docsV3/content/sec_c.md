# Power supply
> battery · charger · power-path · state measurement

1S chain with power-path: the machine runs on USB even with the battery depleted or absent, and the RP2040 governs the full sequencing. Detail in `hoja-1-1-alimentacion.md`.

- C.1 — Input: USB-C sink — 5.1 kΩ CC resistors + ESD protection.
- C.2 — MCP73871: 1S Li-Ion charger **with power-path** → **SYS** node. Charging and system consumption are shared without interruption.
- C.3 — Battery: 1S Li-Ion 3000 mAh (estimated runtime >10 h). State measurement: **MAX17048** (fuel gauge, I2C) read by the EC; the RP2040 can perform an orderly shutdown on critical battery even if the OS is hung.
- C.4 — SYS → **TPS63020** buck-boost → **3V3 always-on** rail (RP2040 and logic).
- C.5 — Rails derived from 3V3: **1.2V LDO with EN** for the iCE40 cores · VCCPLL filtered per FPGA · **VPP 2.5V via BAT54** from 3V3 (avoids a dedicated rail) · panel load switch.
- C.6 — Boosts hanging off **SYS** (not VBAT): **PT4110** for the LED backlight (~1.5W reserved; brightness EN/PWM from the RP2040) and **TPS61023** → 5V VBUS for the USB-A host port (500 mA, EN on demand).
  NOTE: LED string current/voltage: pending the module datasheet (→ N).
- C.7 — Safe bring-up: 0Ω jumpers per rail to isolate and measure each block in stage E1.
- C.8 — Sequencing governed by the RP2040: CRESET_B held until rails are stable; panel and host VBUS off by default. PCF8563 (RTC) shares the I2C bus with the fuel gauge.

![Fig. 2 — Power tree: USB-C → MCP73871 → SYS → 3V3 AO and derived rails; backlight and VBUS boosts from SYS.](figures/fig-2-power.svg)
