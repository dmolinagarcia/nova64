# Hoja de pines — 3× iCE40HX4K TQ-144 (tarea 0.5)

**Presupuesto por chip**: el HX4K en TQ-144 ofrece **107 I/O de usuario**. CRESET_B y CDONE son dedicados (no cuentan). Los 4 pines del SPI de configuración sí cuentan, pero son reutilizables tras la carga del bitstream.

**Regla de colocación**: PHI2, DCLK del panel, y los relojes QSPI deben ir en pines con capacidad GBIN (entrada de reloj global). Se fija en el pinout físico, no afecta al recuento.

---

## FPGA-A — MMU / caché / periféricos

| Bloque | Señales | Pines |
|---|---|---:|
| Bus W65C816S | D/BA[7:0] (8), A[15:0] (16), RWB, PHI2, RESB, ABORTB, IRQB, NMIB, RDY, BE, VDA, VPA, VPB | **35** |
| SRAM 512 KB (AS6C4008) | A[18:0] (19), D[7:0] (8), CE/OE/WE (3) | **30** |
| 2× APS6404L (dual QSPI) | 2× (CS + SIO[3:0]) + CLK compartido | **11** |
| Control A↔B | VRAM_SEL (out, tras traducción MMU) + BWAIT (in) | **2** |
| Config SPI esclavo | SCK, SI, SO, SS (reutilizables post-config) | **4** |
| SD del sistema (SPI) | CLK, MOSI, MISO, CS | **4** |
| I2C-HID ↔ RP2040 | SDA, SCL + IRQ de evento | **3** |
| UART depuración | TX, RX | **2** |
| Reloj de sistema (osc.) | 1 | **1** |
| LED debug | 1 | **1** |
| **Total** | | **93 / 107** |

**Margen: 14 pines (~13 %)** — cumple el objetivo del 10 % sin usar palancas. Las palancas anteriores quedan en reserva:
1. Reutilizar los 4 pines del SPI de config como UART+LED post-carga (+3).
2. Enlace inter-FPGA a 4 bits DDR en vez de 8 (+4, más lógica).
3. Prescindir de NMIB y VPB del CPU (+2).
4. SRAM de 256 KB (+1).

**Descartado como palanca**: mover la SD al RP2040. El swap de páginas (4.5) necesita camino rápido SD→FPGA-A; pasar por el puente I2C lo arruinaría.

---

## FPGA-B — vídeo / audio

| Bloque | Señales | Pines |
|---|---|---:|
| Panel RGB TTL | R/G/B[7:0] (24) | **24** |
| Control panel | DCLK, DEN, HSD, VSD | **4** |
| **Tap del bus CPU** | D/BA[7:0] (8), A[15:0] (16), RWB, PHI2 | **26** |
| Control A↔B | VRAM_SEL (in) + BWAIT (out) | **2** |
| 2× APS6404L (footprints) | 2× (CS + SIO[3:0]) + CLK compartido | **11** |
| I2S (PCM5102) | BCK, LRCK, DIN | **3** |
| VGA bring-up | Solo HSYNC+VSYNC (los R-2R cuelgan de los mismos R/G/B[7:5] del panel) | **2** |
| PWM backlight | 1 | **1** |
| Config SPI esclavo | 4 | **4** |
| Reloj de sistema | 1 | **1** |
| LED debug | 1 | **1** |
| **Total** | | **79 / 107** |

**Margen: 28 pines.** Holgado. Arquitectura de acceso: apertura de VRAM de 64 KB en banco dedicado (propuesta: $FE) con registro de base para ventanear los 8 MB; registros de vídeo/audio dentro de la apertura. Escrituras del CPU → FIFO (sin espera); lecturas estiran RDY vía BWAIT. Truco del VGA: las redes R-2R se conectan a los bits altos del mismo bus RGB del panel (con jumpers para aislar), así el bring-up VGA no gasta pines propios.

---

## FPGA-C — softcore (opcional, sin poblar)

| Bloque | Señales | Pines |
|---|---|---:|
| Bus CPU compartido | Las mismas ~35 señales del bus (ruteadas en paralelo al PLCC y a FPGA-A) | **35** |
| Config SPI esclavo | 4 | **4** |
| Reloj de sistema | 1 | **1** |
| UART propia + LED | 3 | **3** |
| **Total** | | **43 / 107** |

**Margen: 64 pines.** Sin problema.

---

## Veredicto

**Cabe con holgura.** Tras eliminar el enlace inter-FPGA dedicado (decisión: FPGA-B cuelga del bus del CPU, arquitectura de bus compartido estilo Amiga), FPGA-A queda en 93/107 (~13 % de margen, cumple objetivo), FPGA-B en 79/107 y FPGA-C en 43/107. Los tres FPGAs y el CPU comparten el mismo bus físico; el arbitraje (BE=0 + RDY) sirve a la vez para el softcore de FPGA-C y para DMA de FPGA-A hacia VRAM.

## Pendiente para cerrar 0.5 del todo

- Asignar números de pin físicos (con PHI2/DCLK/QSPI-CLK en GBIN) — se hace con el símbolo KiCad delante.
- Confirmar en el datasheet de Lattice qué pines del TQ-144 son GBIN y repartir bancos de I/O.
