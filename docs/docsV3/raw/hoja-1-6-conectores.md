# Hoja 1.6 — FPGA-C, conectores y misceláneos

## FPGA-C (softcore, opcional)

- Footprint TQ-144 idéntico en decoupling/PLL/reloj a los otros dos.
- Conectado al bus compartido según tabla de la hoja 1.4 (incluye poder conducir A/RWB y usar RDY/VDA/VPA cuando actúa de CPU).
- **Jumper en su alimentación** (rail 3V3 e 1V2 de este chip tras jumpers de 0 Ω): sin poblar o apagado no carga el bus.
- Su config (SS_C, CRESET_C) ya prevista en 1.3. UART propia + LED a header.

## Conectores

| Conector | Detalle |
|---|---|
| USB-C | Consola/carga (hoja 1.1, bloque B0) |
| USB-A host | VBUS desde boost 5 V (EN por EC), D+/D− → GPIOs PIO-USB del RP2040, ESD array, polyfuse 500 mA |
| microSD | Push-push, SPI0 del RP2040, card-detect |
| FPC 50 | Panel (hoja 1.5) |
| FPC 6 | Táctil GT911 (hoja 1.5) |
| JST-PH 2/3 pines | Batería 1S (+NTC si la batería lo trae) |
| Jack 3.5 mm | Audio (hoja 1.5) |
| DB15 / header VGA | Bring-up de vídeo |
| Header SWD + 2 GPIO libres | Depuración del EC y expansión |
| Header UART | TX/RX/GND del puente consola (opcional: la consola normal va por USB) |
| Interruptor deslizante | EN del sistema (hoja 1.1) |

## Misceláneos

- LEDs: VSYS, 3V3, 1V2, CDONE×3 (si datasheet lo permite), LED EC, LED usuario por FPGA (A/B/C).
- Botones: BOOTSEL, RUN (RP2040) — y un botón de usuario a FPGA-A (útil como NMI de depuración del OS: entrar al monitor en caliente).
- 4 taladros M3 en esquinas; borde con zona de sujeción para futura carcasa (Etapa 7).
- Serigrafía: nombre de cada bloque y de cada test point (la placa como documento).

## Bring-up
✔ FPGA-C: se deja sin poblar hasta la fase 4.7 — el jumper de alimentación garantiza que su footprint vacío no afecta al bus.
✔ Conectores: continuidad y ESD comprobados en la fase 3.1–3.2 según el rail del que cuelgan.
