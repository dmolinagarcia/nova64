# Peripherals and their location
> which chip · who it hangs off · how the OS sees it

Every peripheral has a clear physical owner and a uniform face toward software: a `/dev/*` node served by a kernel driver. Connectors in `hoja-1-6-conectores.md`.

| Peripheral | Chip / interface | Hangs off | Access from the OS |
|---|---|---|---|
| 10.1” 1024×600 panel | ER-TFT101-1 · HX8282 · RGB-TTL FPC-50 · 18 bpp · 51.5625 MHz → 59.95 Hz ([T.5](sec_t#t5)). **The 60 Hz is physics, not preference**: TFT pixels are capacitors that need refreshing, and polarity inversion becomes visible flicker as the rate drops — 30 Hz is out of spec and ~50 Hz buys only ~17% for a worse image | Neon (FPGA-B) | `/dev/fb` — VRAM mapping via ioctl, **for asset upload, not per-pixel drawing** ([J.5](sec_j#j5)) |
| Screen console | Mode 0 · 128 × 32 cells of 8 × 16 px, text buffer and font in Neon's block RAM, both initialised from the bitstream — **it displays before any software exists** ([T.53](sec_t#t53)) | Neon (FPGA-B) | `/dev/con` — the kernel's own console, distinct from the serial `/dev/tty` below (→ [Q47](sec_q#q47)) |
| Video modes | 0 text · 1 hires mono 1024×600 at 1 bpp, the GUI mode · 2a 320×200 and 2b 512×300 at 8 bpp with a 256-entry palette, both replicated to the panel. **Px-doubling is Mode 2b, not a property of the machine** ([T.33](sec_t#t33)) | Neon (FPGA-B) | `ioctl` on `/dev/fb`; drawing goes through the command channel, never through the CPU |
| Drawing engine | Blitter — fill, copy, keyed copy, tiled copy, 1-bpp expand, lines — plus a command processor executing display lists out of Neon's SDRAM with no CPU involvement ([sheet T](sec_t)) | Neon (FPGA-B) | commands written to `CMD_PORT`, or a list the kernel builds once and patches |
| VGA (bring-up) | R-2R ladders hung off the top bits of the panel's own RGB bus through isolating jumpers — so it costs only 2 dedicated sync pins · 640×480 / 800×600. Its pixel clock does not fall out of the video domain and wants Neon's free second PLL (→ [Q54](sec_q#q54)) | Neon (FPGA-B) | same framebuffer, same timing generator with different parameters |
| Capacitive touch | GT911 · I2C + INT (module's capacitive variant) | **I2C-SW**, the switched-domain bus — its pull-ups would back-feed a dead rail on the always-on one ([C.13](sec_c#c13)) | `/dev/touch` |
| Audio | PCM5102A · I2S · DMA buffers in Neon's SDRAM | Neon (FPGA-B) | `/dev/audio` |
| Keyboard / mouse | USB HID · PIO-USB + hub · VBUS via TPS61023 | RP2040 → EC channel | `/dev/kbd` · `/dev/mouse` |
| Serial console | UART ↔ USB-CDC | Helium ↔ RP2040 | `/dev/tty` |
| Storage | microSD · SPI · '3257 mux | Helium (FPGA-A) | `/dev/sd` → FS |
| Real-time clock | PCF8563 · I2C | **Unassigned since the bus split** — it wants to keep time while the machine is off, which argues AON, but that bus is otherwise reserved to the charger and gauge (→ [Q35](sec_q#q35)) | `/dev/rtc` |
| Battery and power | MAX17048 + BQ25896 · I2C-AON, EC-owned | RP2040 → telemetry snapshot in Helium → bank `$FF` ([sheet S](sec_s)) | `/dev/power` — latch, then read; `ioctl` for poweroff and reboot |
| Power button | Momentary, wired to both `QON` and an AON GPIO | RP2040 (duration) and BQ25896 (hardware floor) | Arrives as an interrupt and a `/dev/power` state, not as a key event |
| Timer + interrupts | 100 Hz timer · prioritized PIC · IRQ/NMI/ABORT | Helium (FPGA-A) | scheduler tick |
| VSync, raster and `SIGNAL` | One IRQ line carrying three sources, aggregated inside Neon and demultiplexed by the kernel reading `IRQ_STATUS` ([T.47](sec_t#t47)) | Neon → PIC | GUI sync, raster effects, and the display list telling the CPU a frame is safe to patch |
| Backlight | PT4110 boost · `EN` and PWM. **Owned by the EC, not by Neon** — it is the only party awake both before Neon is configured and while the switched domain comes down, and it already holds every rail enable ([T.52](sec_t#t52)) | RP2040 → MCP23017 | brightness through `/dev/power`, not `/dev/fb` |
