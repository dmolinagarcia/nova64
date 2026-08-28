# Hoja 1.1 — Alimentación (documento de diseño)

## Topología

```
USB-C (consola RP2040) 5V ──► MCP73871 ──► VBAT (1S LiPo + PCM)
  (CC1/CC2: 5.1 kΩ a GND)       │              │
                                │              ├─► MAX17048 (fuel gauge, I2C → RP2040)
                                │              │
                                └── SYS ◄──────┘   (power-path: USB o batería)
                                     │
                                     ├─► TPS63020 ──► 3V3  (always-on)
                                     │        │
                     ┌───────────────│────────┤
                     │               │        │
               LDO 1.2V (EN←RP2040)  │        ├─► RP2040, CPU, SRAM,
                     │               │        │   PSRAM, DAC, lógica panel*
                     └─► RC ─► VCCPLL (×3)    │
                                     │        └─► load switch (EN←RP2040) ──► panel 3V3
                                     ├─► PT4110 boost (EN/PWM) ──► LED backlight
                                     └─► TPS61023 boost 5V (EN←RP2040) ──► VBUS USB-A host
```

⚠ Corrección importante: los boosts de backlight y VBUS cuelgan de **SYS**, no de VBAT. SYS es la salida del power-path del MCP73871 — así todo funciona conectado a USB aunque la batería esté agotada o ausente. Colgarlos de VBAT rompería exactamente el caso de uso de desarrollo en mesa.

\* VPP_2V5 de los iCE40, dos opciones válidas: (a) atado a 3V3 directo (así lo hace el iCEBreaker), o (b) **BAT54 en serie desde 3V3 + 100 nF** — el diodo baja ~0.3 V y deja VPP cómodamente centrado en rango. Preferencia: (b), coste un diodo por chip. Verificar rango exacto en el datasheet Lattice al dibujar.

### B0 — Conector USB-C
- Solo sumidero de 5 V: **CC1 y CC2 con 5.1 kΩ a GND cada uno** (sin esto, un cargador/puerto USB-C no entrega VBUS — error clásico).
- D+/D− → RP2040 (USB device). VBUS → entrada del MCP73871.
- ESD array (p. ej. USBLC6-2) en D+/D− y VBUS.

## Bloques y valores

### B1 — Carga y power-path: MCP73871
- Entrada: VBUS del conector USB-C de consola (5 V).
- Power-path: el sistema corre de USB mientras carga — sin "apagón" al enchufar/desenchufar.
- Corriente de carga: R_PROG según tabla del datasheet para **~500 mA** (1S genérica; ajustable cuando se elija batería).
- THERM: al NTC de la batería si lo trae; si no, divisor fijo de 10 kΩ para no inhibir la carga.
- VPCC: divisor para regulación de entrada (~4.4 V) — evita colapsar puertos USB débiles.
- STAT1/STAT2/PG → GPIOs del RP2040 (estado de carga visible por software, no solo LEDs).

### B2 — Fuel gauge: MAX17048
- CELL → VBAT, I2C → bus del RP2040, ALRT → GPIO (aviso de batería baja).

### B3 — Rail principal 3.3 V: TPS63020 (buck-boost)
- Siempre encendido (EN → VIN). Modo power-save activo: ~50 µA en reposo.
- L = 1.5 µH (según datasheet), Cin 10 µF, Cout 2× 22 µF.
- Alternativa que reduce errores: **TPS63021** (versión fija 3.3 V, sin divisor de feedback).
- Presupuesto de carga: ~0.6–0.8 A pico (3 FPGA I/O + RP2040 + CPU + memorias + lógica panel + DAC). El TPS6302x da ~2 A a 3.3 V desde 1S: margen ×2.5.

### B4 — Rail de cores 1.2 V: LDO
- 3.3 V → 1.2 V, budget ≥ 300 mA (3× iCE40 core). Disipación ~0.3 W a plena carga → SOT-223/SOT-89.
- Candidatos soldables a mano: AMS1117-1.2 (clásico) o un LDO moderno con EN (p. ej. AP7361C-12) — **EN desde RP2040** es requisito del secuenciado, así que preferir LDO con enable.
- Por cada FPGA: filtro **100 Ω + 100 nF + 10 µF → VCCPLL** (recomendación Lattice para el PLL).

### B5 — Backlight: PT4110 (boost LED)
- Entrada desde **SYS** (funciona con USB sin batería; eficiencia similar a VBAT directo).
- Corriente de LED y tensión de string: **pendiente del datasheet del módulo ER-TFT101-1** (cuestión abierta 13). Reservar ~1.5 W.
- EN/PWM de brillo desde RP2040 (o FPGA-B; decidir en 1.5 — RP2040 más simple).

### B6 — 5 V para USB host: TPS61023 (boost)
- **SYS** → 5 V, 500 mA. EN desde RP2040 (solo se enciende cuando el hub HID lo pide).

### B7 — Load switch del panel
- 3.3 V → panel a través de load switch (o P-MOSFET + driver) con EN desde RP2040: permite secuenciar el panel según el power-on del HX8282 y apagarlo en suspensión.

## Secuenciado de arranque (gobernado por RP2040)

| Paso | Acción | Condición |
|---|---|---|
| 0 | VBAT presente → 3V3 sube solo | — |
| 1 | RP2040 arranca; CRESET_B de los 3 FPGA **bajo** | 3V3 estable |
| 2 | EN del LDO 1.2 V | RP2040 vivo |
| 3 | Suelta CRESET_B y configura FPGAs por SPI | 1.2 V estable (delay o ADC) |
| 4 | Load switch del panel | FPGA-B configurado |
| 5 | Backlight (rampa PWM) | Timing de vídeo activo |
| 6 | Boost 5 V host | Bajo demanda |

Apagado: orden inverso (backlight → panel → FPGAs en reset → 1.2 V off). El 3V3 nunca se apaga (reposo µA).

## Diseño para bring-up (fase 3.1)

- Test point + LED en cada rail: 3V3, 1V2, 5V_HOST, VBAT, salida backlight.
- Jumper/0 Ω en serie con: entrada del LDO 1.2 V, load switch del panel, boost 5 V, boost backlight → cada rail se prueba aislado y se mide su consumo.
- ✔ Éxito 3.1: con solo esta hoja poblada — 3V3 = 3.3 V ±2 % con carga ficticia, 1V2 = 1.2 V ±3 %, carga de batería funcionando con los STAT correctos, MAX17048 respondiendo por I2C (vía RP2040 en 3.2).

## Cuestiones abiertas de esta hoja

- A. Tensión/corriente exacta del string de backlight (bloqueada por datasheet del módulo).
- B. Capacidad y formato de la batería 1S (condiciona R_PROG y autonomía; el fuel gauge exige perfil).
- C. Confirmar VPP_2V5 @ 3.3 V en datasheet iCE40.
- D. ¿El módulo del panel necesita algo más que 3.3 V + backlight? (la mayoría con HX8282 generan AVDD/VGH/VGL internamente — verificar).
