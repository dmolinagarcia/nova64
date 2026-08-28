# Hoja 1.5 — FPGA-B: panel, VRAM y audio

## Tap del bus CPU
D/BA[7:0], A[15:0], RWB, PHI2 (entradas) + VRAM_SEL (in) / BWAIT (out). Ver tabla completa en hoja 1.4.

## Panel (ER-TFT101-1, HX8282) — bloque paramétrico hasta tener el datasheet del módulo

- Conector **FPC 50 pines ZIF** (paso típico 0.5 mm — confirmar).
- R/G/B[7:0] + DCLK + DEN + HSD + VSD desde FPGA-B, **33 Ω en serie en todas** (pegadas al FPGA).
- Panel latchea en flanco de **bajada** de DCLK → el gateware cambia datos en subida.
- VDD panel: desde el load switch de la hoja 1.1. STBYB/reset del panel: GPIO de FPGA-B o del expander (confirmar en datasheet si existe en el FPC).
- Straps del módulo (modo DE, formato): normalmente fijos en el módulo — confirmar cuáles expone el FPC.
- Backlight: pines LED+/LED− del FPC → bloque PT4110 (hoja 1.1). PWM desde RP2040.
- **Táctil capacitivo GT911**: suele venir en **FPC propio de 6 pines** (VDD, GND, SDA, SCL, INT, RST) → conector aparte ruteado al bus I2C del RP2040 + INT/RST (hoja 1.2). Confirmar en el módulo.

## VRAM
2× footprints APS6404L SOIC-8 (poblar 1), CLK compartido, mismas reglas de ruteo que en 1.4.

## Audio

- **PCM5102A** (TSSOP-20, soldable): BCK, LRCK, DIN desde FPGA-B; **SCK a GND** (modo PLL interno, nos ahorra un reloj); straps FLT/DEMP/FMT a GND, XSMT a 3.3 V vía RC (anti-pop).
- Salida: filtro RC suave → **jack 3.5 mm**.
- **Ampli de altavoz opcional**: footprint PAM8302 (mono, clase D) + altavoz pequeño, con jumper para deshabilitarlo — sin poblar al principio. El futuro portátil querrá altavoz; reservarlo ahora cuesta 1 cm².

## VGA bring-up

- Redes R-2R de 3 bits colgadas de R[7:5], G[7:5], B[7:5] **a través de un bloque de jumpers/0 Ω** (aislables cuando el panel funcione), + HSYNC/VSYNC dedicados con 100 Ω serie → conector DB15 o header.
- El mismo timing generator del gateware sirve para VGA 640×480 (bring-up) y para el panel (producción) — solo cambian los parámetros.

## Decoupling y reloj
Patrón idéntico a la hoja 1.4 (100 nF ×8-10, VCCPLL filtrado, 25 MHz por GBIN).

## Test points
DCLK, DEN, un bit de cada canal de color, BCK/DIN de audio, VRAM_SEL/BWAIT (compartidos con 1.4).

## Bring-up asociado (fase 3.6)
✔ Paso 0: panel alimentado + BIST → patrones sin gateware.
✔ VGA barras de color → panel barras generadas → framebuffer 512×300 con pixel doubling desde PSRAM → tono de audio por el jack.
