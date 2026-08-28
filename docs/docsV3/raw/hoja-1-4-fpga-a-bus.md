# Hoja 1.4 — FPGA-A, bus compartido del CPU y memorias

La hoja más densa: aquí vive el bus con sus cuatro habitantes.

## El bus compartido (net por net)

| Señal | CPU (PLCC-44) | FPGA-A | FPGA-B | FPGA-C | Reposo (resistencia) |
|---|---|---|---|---|---|
| D/BA[7:0] | I/O | I/O | I/O | I/O | pull-up débil 47 kΩ (bus nunca flotante) |
| A[15:0] | out (tri-state con BE) | in/out (DMA) | in | in/out | — |
| RWB | out (tri-state) | in/out | in | in/out | pull-up 10 kΩ |
| PHI2 | in | **out (siempre)** | in | in | — |
| RDY | I/O (open-drain en WAI) | I/O | — | I/O | pull-up 3.3 kΩ |
| RESB | in | out | — | in | **pull-down 10 kΩ** (CPU en reset hasta que FPGA-A configure) |
| ABORTB, IRQB, NMIB | in | out | — | in | pull-up 10 kΩ (inactivos si FPGA-A no configurado) |
| BE | in | out | — | — | pull-up 10 kΩ |
| VDA, VPA, VPB | out | in | — | out | — |
| VRAM_SEL | — | out | in | — | pull-down 10 kΩ |
| BWAIT | — | in | out | — | pull-down 10 kΩ |

Notas de diseño:
- El estado seguro sin FPGAs configurados: RESB abajo (CPU parado), todo lo demás inactivo por pull-ups. La placa puede alimentarse sin ningún bitstream sin que nada pelee por el bus.
- W65C816S funciona a 3.3 V sin problema (rango 1.8–5 V); a 3.3 V el Fmax del CPU ronda ~8 MHz — PHI2 objetivo inicial 1–4 MHz, de sobra.
- Ruteo: los cuatro habitantes del bus agrupados físicamente (PLCC en el centro, A/B/C alrededor); trazas de bus < ~10 cm.
- Nota de gateware (no de PCB): ABORTB debe asertarse antes del flanco de bajada de PHI2 del ciclo abortado — la traducción MMU tiene que resolverse durante PHI2 alto. Con PHI2 lento, holgura de sobra.

## Zócalo del CPU

- PLCC-44 through-hole. VDD con 100 nF + 1 µF pegados. VPB/MLB/E/MX: E y MX sin conectar (test point opcional), MLB sin conectar.

## SRAM (privada de FPGA-A — es la caché, no está en el bus del CPU)

- AS6C4008-55 (512 K×8, 55 ns, 3.3 V). Valorar zócalo DIP-32 (facilita sustitución) vs SOP-32 (compacto). Decisión por defecto: **SOP-32 soldada** (el zócalo DIP abulta mucho; la SRAM no es un componente de riesgo).
- A[18:0], D[7:0], CE/OE/WE a FPGA-A. 55 ns permite acceso en 1 ciclo a ~50 MHz de core con timing cuidado, o 2 ciclos relajados.

## PSRAM (privada de FPGA-A)

- 2× APS6404L-3SQR SOIC-8. CLK compartido, CS/SIO independientes. 100 nF por chip pegado a VDD.
- Trazas QSPI < 3 cm, agrupadas, sin vías si es posible.

## Reloj

- **Oscilador único de 25 MHz (3.3 V)** distribuido en estrella a los 3 FPGA con 33 Ω por rama, entrando por pin GBIN en cada uno.
- Cada FPGA sintetiza lo suyo con su PLL: FPGA-A core (~25–50 MHz según cierre de timing) y PHI2 por división; FPGA-B ~50 MHz de pclk (dentro del rango 40.8–67.2 del panel); FPGA-C el clock del softcore.

## Decoupling FPGA-A (patrón para los tres TQ-144)

- 100 nF por par de pines de alimentación (≈8–10 uds), 1 µF por rail cerca del chip, 10 µF bulk.
- VCCPLL: 100 Ω + 100 nF + 10 µF (de la hoja 1.1).

## Test points
PHI2, RESB, ABORTB, RDY, BE, VRAM_SEL, BWAIT, un SIO de cada QSPI.

## Bring-up asociado (fases 3.4–3.5)
✔ 3.4: test de patrones SRAM y PSRAM desde gateware de FPGA-A (resultado por UART).
✔ 3.5: con CPU en zócalo — PHI2 a 1 MHz, ROM de NOPs en BRAM, bus observado consistente; luego LED parpadeando por dirección decodificada.
