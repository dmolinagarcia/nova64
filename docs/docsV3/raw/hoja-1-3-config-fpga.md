# Hoja 1.3 — Configuración de los 3 iCE40

## Esquema

Bus SPI1 del RP2040 compartido a los tres FPGA en esclavo SPI:
- **SCK y MOSI**: en estrella desde el RP2040 con 33 Ω en serie por rama.
- **MISO**: compartido (solo lo conduce el FPGA con SS activo).
- **SS_A / SS_B / SS_C**: individuales (RP2040).
- **CRESET_A / CRESET_B / CRESET_C**: individuales (RP2040), con pull-up de 10 kΩ cada uno.
- **CDONE ×3**: al MCP23017 (lectura lenta, suficiente) + LED opcional por chip vía transistor o directamente si CDONE puede con el LED (verificar corriente en datasheet; si no, solo expander).

## Protocolo (firmware EC, por FPGA)

1. CRESET_B bajo, SS bajo.
2. CRESET_B alto, esperar ≥1.2 ms (el iCE40 borra su config y entra en modo esclavo SPI).
3. Volcar el bitstream por MOSI (modo SPI 3, MSB primero).
4. Enviar ~100 clocks extra; comprobar CDONE alto en el expander.

## Decisiones

- **Sin flash de configuración por FPGA**: el RP2040 configura siempre (fuente: microSD, respaldo: su propia flash de 16 MB). Menos chips, y el "cold boot" del sistema es el firmware del EC.
- Los 4 pines SPI de cada iCE40 son reutilizables como I/O de usuario tras la config — reservados como palanca de pines (no usados de momento).
- Test points en SCK, MOSI y cada CRESET.

## Bring-up (fase 3.3)
✔ Éxito: el RP2040 configura cada FPGA por separado con un bitstream blinky; CDONE de los tres leídos en alto; reconfiguración de FPGA-B sin tocar A ni C demostrada.
