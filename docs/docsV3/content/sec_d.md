# RP2040 — embedded controller
> boot · FPGA programming · console · EC

It's the laptop's "management microcontroller": it handles everything that happens before a live CPU exists, and stays on watch afterward. Detail in `hoja-1-2-rp2040.md` and `hoja-1-3-config-fpga.md`.

- D.1 — Triple role: **programmer** (bitstreams, BIOS, its own firmware — all over USB), **bootstrapper** (full sequence in Fig. 3), and permanent **embedded controller**. Its W25Q16 is the board's only mandatory flash.
- D.2 — 9-step boot (Fig. 3): button → rails → SSPI configuration of Helium/Neon(/Argon) with CRESET_B/CDONE → SDRAM initialized by gateware → BIOS from SD to SRAM → SD handoff → RESB↑ → `$FFFC` vector.
- D.3 — Debugging: a debug port into FPGA-A (Helium) — live read/write of system memory, a free "ICE" for the kernel.
- D.4 — Console: USB-CDC ↔ FPGA-A (Helium) UART. It's the terminal for the monitor (E2) and the OS until there's a screen of its own.
- D.5 — HID host: PIO-USB + hub — USB keyboard and mouse (PS/2 and matrix removed). Powers up the TPS61023 on demand and translates HID → events to the kernel over the EC channel.
- D.6 — Its own pin budget nearly failed too, and was rescued the same way the FPGA's was. Straight onto GPIO the EC needed roughly 38 pins against 30 available; an **MCP23017 I2C expander** absorbs everything with no timing requirement — the three CDONE lines, the charger's STAT and PG, the fuel gauge's alert, and the enables for the 1.2V rail, the panel and the host boost — bringing it to 28 of 30.
  NOTE: The pattern has now appeared twice: both Helium and the RP2040 ran out of pins, and both were saved by moving slow signals onto a cheaper transport — the keyboard matrix onto USB HID, the enables and status lines onto I2C. Expect it a third time.
- D.7 — EC in operation: power button, MAX17048 readout, RTC, safe shutdown with the OS hung. [[open]]
  NOTE: EC↔kernel protocol yet to be defined (→ N).

![Fig. 3 — Boot sequence. Steps 1–6 are executed by the RP2040 (gold); from step 7 onward the 65816 (mint) takes over.](figures/fig-3-rp2040.svg)
