# Peripherals and their location
> which chip · who it hangs off · how the OS sees it

Every peripheral has a clear physical owner and a uniform face toward software: a `/dev/*` node served by a kernel driver. Connectors in `hoja-1-6-conectores.md`.

| Peripheral | Chip / interface | Hangs off | Access from the OS |
|---|---|---|---|
| 10.1” 1024×600 panel | ER-TFT101-1 · HX8282 · RGB-TTL FPC-50 · 51.2 MHz → 60 Hz. **The 60 Hz is physics, not preference**: TFT pixels are capacitors that need refreshing, and polarity inversion becomes visible flicker as the rate drops — 30 Hz is out of spec and ~50 Hz buys only ~17% for a worse image | Neon (FPGA-B) | `/dev/fb` — VRAM mapping via ioctl · 512×300 px-doubling |
| VGA (bring-up) | R-2R ladders hung off the top bits of the panel's own RGB bus through isolating jumpers — so it costs only 2 dedicated sync pins · 640×480 / 800×600 | Neon (FPGA-B) | same framebuffer, same timing generator with different parameters |
| Capacitive touch | GT911 · I2C + INT (module's capacitive variant) | System I2C (Helium) | `/dev/touch` |
| Audio | PCM5102A · I2S · DMA buffers in Neon's SDRAM | Neon (FPGA-B) | `/dev/audio` |
| Keyboard / mouse | USB HID · PIO-USB + hub · VBUS via TPS61023 | RP2040 → EC channel | `/dev/kbd` · `/dev/mouse` |
| Serial console | UART ↔ USB-CDC | Helium ↔ RP2040 | `/dev/tty` |
| Storage | microSD · SPI · '3257 mux | Helium (FPGA-A) | `/dev/sd` → FS |
| Real-time clock | PCF8563 · I2C | System I2C | `/dev/rtc` |
| Battery | MAX17048 · I2C | I2C (read by the EC) | via EC channel |
| Timer + interrupts | 100 Hz timer · prioritized PIC · IRQ/NMI/ABORT | Helium (FPGA-A) | scheduler tick |
| VSync | IRQ line from video | Neon → PIC | GUI sync |
