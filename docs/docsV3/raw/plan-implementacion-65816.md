# Plan de implementación incremental — Ordenador 65816

**Documento vivo.** Se actualiza conforme avanza el diseño. Cada paso tiene un *criterio de éxito* verificable antes de pasar al siguiente.

**Filosofía:** una sola PCB con todos los componentes, pero diseñada para **montaje y arranque por etapas**. Nunca se puebla ni se activa un bloque hasta que el anterior funciona. Los FPGAs nos permiten que el hardware sea fijo pero la funcionalidad crezca poco a poco.

## Estrategia de encapsulados (actualizada 2026-08-02)

**Restricción**: sin BGA (no hay medios). QFN con pad térmico sí es viable. **Toda la placa es soldable a mano.**

| Componente | Encapsulado | Estrategia |
|---|---|---|
| iCE40HX4K ×3 | TQFP-144 | Chips desnudos. Nota: el HX4K es un die HX8K capado por software; con Yosys/nextpnr se usan los **~7.680 LUTs** reales |
| W65C816S | PLCC-44 | **En zócalo** (extraíble para depurar y para ceder el bus al softcore) |
| RP2040 | QFN-56 + pad | Chip desnudo en la PCB |
| Panel 10.1" 1024×600 RGB TTL | FPC 50 pines | **Conexión directa a FPGA-B** (sin bridge). Conector FPC + driver boost de backlight |
| TPS63020 / MCP73871 / MAX17048 | QFN/DFN | Chips desnudos en la PCB |
| APS6404L | SOIC-8 | Directo |
| SRAM 512 KB | SOP/DIP | Valorar DIP-32 zocalable (AS6C4008) |

**Topología de 3 FPGAs:**
- **FPGA-A** — MMU + caché + periféricos (acompaña al W65C816S físico).
- **FPGA-B** — vídeo.
- **FPGA-C (opcional)** — softcore 65816. Footprint en placa desde el día 1, **sin poblar** hasta la fase correspondiente. Cuelga del mismo bus y entra con BE=0.

**Cambio arquitectónico derivado (16 KB de BRAM por chip):** la tabla de páginas completa ya no cabe en BRAM → **tabla de páginas en la SRAM externa, TLB en BRAM**. El walk de TLB miss lee de SRAM (rápido, sin recursión a PSRAM), así que se mantiene en hardware. Esto cierra la antigua cuestión abierta nº 3.

---

## Etapa 0 — Preparación (sin soldador)

- [ ] **0.1 Instalar herramientas**: KiCad 8, toolchain abierto para iCE40 (Yosys + nextpnr-ice40 + IceStorm, todo en oss-cad-suite), pico-sdk para el RP2040, un simulador (Verilator o GHDL según el HDL elegido).
  - ✔ Éxito: sintetizas un "blinky" para iCE40 en simulación y compilas un "hello" para RP2040.
- [ ] **0.2 Elegir HDL definitivo** (Verilog vs VHDL — condiciona qué softcore de referencia usas: srg320/P65816 es VHDL).
- [ ] **0.3 Reunir datasheets y librerías**: W65C816S, iCE40HX4K (TQ144), APS6404L, ANX6345, RP2040, TPS63020, MCP73871, MAX17048. Crear librería KiCad del proyecto con símbolos y footprints verificados uno a uno.
  - ✔ Éxito: cada footprint impreso a escala 1:1 en papel y comparado con el componente físico (o con el datasheet si aún no lo tienes).
- [ ] **0.4 Confirmar el panel — candidato: ER-TFT101-1 (BuyDisplay)**, 10.1" IPS 1024×600, controlador HX8282(+HX8696), FPC 50 pines ZIF, táctil resistivo/capacitivo opcional. Datos ya verificados del HX8282-A11:
  - Interfaz RGB paralelo 24 bits (o 18 bits atando los 2 LSB a GND), TTL/LVDS por pin, modo DE/SYNC por pin. Defectos de fábrica = lo que queremos (TTL posible, modo DE, 1024×600): sin configuración.
  - Lógica 2.3–3.6 V → 3.3 V directo desde el iCE40, sin level shifters.
  - **Modeline (modo DE)**: DCLK 40.8–67.2 MHz, típ. 51.2 MHz; th típ. 1344 DCLK; tv típ. 635 líneas → 60 Hz. ~50 Hz posible en el extremo bajo; 30 Hz fuera de espec.
  - ⚠ Latch de datos en flanco de **bajada** de DCLK por defecto → el FPGA cambia datos en flanco de subida.
  - **Modo BIST por pin**: patrones de test sin reloj externo — el panel se prueba sin gateware.
  - Backlight: driver boost sugerido PT4110.
  - Pendiente antes de cerrar: datasheet del *módulo* (qué straps expone el FPC, spec exacta del backlight, pines del táctil).
  - **Modos de vídeo (gateware, no PCB)**: el panel siempre escanea 1024×600@60; el framebuffer varía:
    - *Consola*: texto 1024×600 nativo desde charROM en BRAM (0 MB/s de framebuffer).
    - *Modo estrella*: 512×300 @ 8 bpp con paleta, pixel doubling (~9.2 MB/s) — cabe en 1× QSPI.
    - *Extendidos (futuros, requieren 2ª PSRAM)*: 512×300 @ 16 bpp o 1024×600 @ 8 bpp.
- [x] **0.5 ⚠ BLOQUEANTE — Hoja de asignación de pines** → **RESUELTO: cabe con holgura.** Ver `hoja-pines-ice40.md`. Con bus compartido: FPGA-A 93/107 (~13 % margen), FPGA-B 79/107, FPGA-C 43/107. Queda solo la asignación de números de pin físicos (GBIN para PHI2/DCLK/QSPI-CLK), que se hará con el símbolo KiCad en 1.4/1.5.
- [ ] **0.6 Congelar el diagrama de bloques** (el HTML/SVG que ya mantenemos) como referencia jerárquica del esquemático, actualizado a 3× iCE40.

## Etapa 1 — Esquemático por hojas jerárquicas

Cada hoja se diseña, se revisa y se cierra antes de abrir la siguiente. Orden pensado para aprender de lo simple a lo complejo:

- [x] **1.1 Alimentación** → **DISEÑADA**, ver `hoja-1-1-alimentacion.md` (pendiente pasarla a KiCad). Rails: 3V3 always-on (TPS63020/21), 1.2 V cores con EN (LDO), filtros VCCPLL por FPGA, VPP vía BAT54, **5 V boost para USB host (nuevo, TPS61023)**, backlight PT4110 — ambos boosts **desde SYS** (power-path: funciona por USB sin batería), load switch del panel, USB-C con CC 5.1 k + ESD. Secuenciado completo gobernado por RP2040 (CRESET_B bajo hasta rails estables). Jumpers 0 Ω por rail para bring-up aislado.
- [x] **1.2 RP2040** → **DISEÑADA**, ver `hoja-1-2-rp2040.md` (pendiente KiCad). Núcleo estándar (cristal 12 MHz, W25Q128 16 MB con bitstreams de respaldo, USB con 27 Ω, BOOTSEL/RUN, header SWD) + presupuesto de GPIO cerrado en 28/30 gracias a un **expander I2C MCP23017** para señales lentas (CDONE×3, STAT×3, ALRT, EN×3). Buses SPI separados para SD y config; CRESET individual por FPGA (reconfigurar B sin tumbar A). Descubrimiento: el táctil debe ser **capacitivo (GT911)** — el resistivo exigiría 4 ADC que no existen.
- [x] **1.3 Configuración de FPGAs** → **DISEÑADA**, ver `hoja-1-3-config-fpga.md`. SPI1 en estrella con 33 Ω, SS y CRESET individuales, CDONE al expander. Sin flash de config por FPGA: el EC configura siempre (SD primaria, su flash como respaldo).
- [x] **1.4 FPGA-A + bus + memorias** → **DISEÑADA**, ver `hoja-1-4-fpga-a-bus.md`. Tabla net-por-net del bus con sus 4 habitantes y estado seguro sin bitstream (RESB pull-down, resto pull-ups); CPU verificado a 3.3 V (Fmax ~8 MHz, PHI2 inicial 1–4 MHz); SRAM SOP-32 privada (caché, fuera del bus); oscilador único 25 MHz en estrella a los 3 FPGA por GBIN, PLLs sintetizan el resto.
- [x] **1.5 FPGA-B + vídeo/audio** → **DISEÑADA**, ver `hoja-1-5-fpga-b-video-audio.md`. FPC 50 con 33 Ω serie, bloque paramétrico hasta datasheet del módulo; GT911 en FPC propio de 6 → I2C del EC; PCM5102A con SCK a GND (PLL interno) + jack; footprint opcional de ampli PAM8302 + altavoz para la Etapa 7; VGA por jumpers desde R/G/B[7:5].
- [x] **1.5b FPGA-C** → **DISEÑADA** (en `hoja-1-6-conectores.md`): footprint completo con jumpers de alimentación, sin poblar hasta 4.7.
- [x] **1.6 Conectores y misceláneos** → **DISEÑADA**, ver `hoja-1-6-conectores.md`. Tabla completa de conectores; botón de usuario a FPGA-A como NMI de depuración; LEDs y test points serigrafiados.
- [ ] **1.7 Revisión cruzada completa**: ERC limpio + repaso manual de cada pin de los TQFP contra el datasheet.
  - ✔ Éxito de la etapa: esquemático completo revisado dos veces en días distintos.

**Diseñar para el bring-up desde el esquemático:**
- Test points en todos los rails, PHI2, RESB, ABORTB, líneas QSPI.
- Resistencias de 0 Ω o jumpers para poder aislar bloques (p. ej. cortar alimentación a FPGA-B, aislar el bus del CPU).
- El W65C816S en zócalo o con BE controlable para que el softcore pueda tomar el bus sin desoldar (ya decidido).
- LED por rail y algún GPIO libre con LED en cada FPGA.

## Etapa 2 — Layout

- [ ] **2.1 Stackup**: **2 capas** con TQFP-144 es viable; valorar 4 si el ruteo del RGB/QSPI/bus compartido se enreda. Decidir fabricante (JLC/PCBWay) antes de rutear.
- [ ] **2.2 Colocación y fanout de TQFP/QFN** con decoupling pegado a los pines; pad térmico del RP2040/ANX6345 con vías a GND. FPGA-A, C y el zócalo PLCC agrupados en torno al bus del CPU.
- [ ] **2.3 Rutas críticas**: QSPI de PSRAM cortas y agrupadas, bus RGB de 24 bits al FPC con longitudes razonablemente parejas (a ~51 MHz de pclk no es crítico, pero sí ordenado), trazas del boost de backlight separadas de señales sensibles.
- [ ] **2.4 Resto del ruteo, plano de masa continuo, DRC limpio.**
- [ ] **2.5 Revisión final** (idealmente con ojos frescos o pidiéndome un repaso de la exportación).
  - ✔ Éxito: gerbers generados y verificados en un visor externo.

## Etapa 3 — Montaje y arranque por fases

**Regla de oro: poblar solo lo que toca en cada fase.**

- [ ] **3.1 Solo alimentación.** Medir 3.3 V y 1.2 V sin carga, verificar secuenciado y carga de batería.
- [ ] **3.2 RP2040.** Enumera por USB, consola serie, lee la microSD, parpadea un LED.
- [ ] **3.3 FPGA-A.** El RP2040 lo configura por SPI esclavo (CRESET_B → bitstream → CDONE alto) con un blinky; después carga desde la SD.
- [ ] **3.4 Memorias desde FPGA-A.** Test de patrones sobre SRAM; luego driver QSPI y test de las dos APS6404L.
- [ ] **3.5 W65C816S vivo.** PHI2 lento (1 MHz) desde el FPGA, "ROM" en BRAM con un bucle de NOPs, observar el bus. Luego un programa que parpadee un LED vía una dirección decodificada.
- [ ] **3.6 FPGA-B y vídeo.** Paso 0: alimentar el panel y activar su **BIST por pin** → patrones de color sin gateware (valida panel, FPC y backlight aislados). Después VGA por R-2R (barras de color), luego timing DE 1024×600 @ 51.2 MHz hacia el FPC → barras generadas por el FPGA. Por último, framebuffer 512×300 con pixel doubling desde PSRAM.

## Etapa 4 — Gateware incremental (la parte donde el FPGA brilla)

Cada paso es un bitstream nuevo sobre el mismo hardware:

- [ ] **4.1 Sistema plano**: CPU + BRAM como ROM/RAM, sin MMU, UART como primer periférico MMIO en banco $FF.
- [ ] **4.2 SRAM en el mapa** del CPU (acceso directo, sin caché).
- [ ] **4.3 Caché SRAM↔PSRAM** (líneas de 256 B, write-back) con la MMU en *identity mapping*.
- [ ] **4.4 Paginación real**: **tabla de páginas en SRAM externa** (páginas de 2 KB, 4096 entradas), **TLB en BRAM**, walk hardware sobre SRAM en el miss. Formato de PTE pendiente de definir (bits de frame, R/W/X, present/dirty/accessed).
- [ ] **4.5 ABORT**: fallo de página por hardware, primer handler que carga una página desde SD (swap).
- [ ] **4.6 Periféricos restantes**: controlador de interrupciones, timer, SPI-SD, interfaz I2C-HID con el RP2040 (teclado/ratón), **acceso a VRAM por bus compartido** (apertura banco $FE, FIFO de escritura en FPGA-B, RDY para lecturas), **DMA de FPGA-A → VRAM** (BE=0, mismo arbitraje que el softcore), **motor de audio en FPGA-B** (DMA de muestras desde PSRAM → I2S, doble buffer con interrupción de "buffer vacío").
- [ ] **4.7 Softcore opcional en FPGA-C**: poblar el tercer iCE40, cargar P65816/FT816, BE=0 en el CPU físico y el softcore toma el bus — misma placa, CPU intercambiable. Fmax del iCE40 (~30–50 MHz) limita la ambición del softcore: asumido.

## Etapa 5 — OS y software: hito "Apple II"

**Meta del hito**: la máquina arranca sola, muestra un prompt en su propia pantalla, acepta su propio teclado y ejecuta programas desde la SD. Hasta 5.6, toda la interacción es por UART desde el PC.

- [ ] **5.0 Toolchain cruzado en el PC**: ensamblador 65816 (64tass o WDC), scripts de build, y un cargador serie (RP2040/UART) para inyectar programas sin tocar la SD. Es la herramienta que acelera todo lo demás.
  - ✔ Éxito: editas en el PC, pulsas un botón y el programa corre en la máquina.
- [ ] **5.1 Monitor/BIOS**: consola por UART, peek/poke, volcado de memoria, ejecutar en dirección, carga de binarios.
- [ ] **5.2 Kernel mínimo mono-proceso**: syscalls vía COP (primeras: putchar, getchar, read_sector), drivers internos (UART, SD), separación usuario/privilegiado con ABORT + banco $FF.
- [ ] **5.3 Sistema de ficheros**: **FAT32 de solo lectura** primero (interoperable con el PC: copias ficheros desde tu ordenador y la máquina los ve). Escritura después.
- [ ] **5.4 Formato de ejecutable y loader**: binario relocatable con el modelo "un banco por proceso" (DBR/PBR), cabecera simple (magic, tamaño, entry point).
- [ ] **5.5 Shell tipo CLI** (espíritu AmigaDOS/ProDOS): `dir`, `type`, `run`, `free`. Corre como proceso de usuario, no dentro del kernel.
- [ ] **5.6 La máquina se vuelve autónoma**: driver de teclado (eventos HID desde el RP2040) + modo texto en FPGA-B (80×30 sobre VGA/eDP) conectados a la consola del OS. **← Hito Apple II conseguido.**
- [ ] **5.7 Escritura en FAT32** y utilidades (`copy`, `del`, redirección `>`).
- [ ] **5.8 Primer programa "de verdad"**: un editor de texto a pantalla completa. Ejercita teclado, vídeo, ficheros y syscalls a la vez — es el mejor test de integración que existe.

## Etapa 6 — OS y software: hito "Amiga"

**Meta del hito**: multitarea preemptiva visible y una GUI de ventanas manejada con ratón.

- [ ] **6.1 Multitarea preemptiva**: scheduler round-robin por timer, cambio de contexto completo (estado CPU + notificar al gestor el proceso activo para conmutar tablas de permisos), syscalls `spawn`/`exit`/`yield`.
  - ✔ Éxito: dos programas contando en pantalla a la vez, y si uno hace un acceso ilegal, ABORT lo mata sin tumbar al resto (¡mejor que el Amiga real!).
- [ ] **6.2 IPC básico**: mensajes entre procesos (el modelo de puertos de mensajes del Exec de Amiga es un buen espejo, y encaja con 65816).
- [ ] **6.3 API gráfica**: el servidor de pantalla mapea la apertura de VRAM (banco $FE) vía MMU y dibuja directo; syscalls de blit/copia usan el DMA de FPGA-A. Doble buffer conmutando el registro de base de la apertura.
- [ ] **6.4 Entrada de puntero**: ratón por el hub HID del RP2040 + cursor por hardware en FPGA-B (sprite overlay, como hacía el Amiga — evita redibujar el framebuffer al mover el ratón).
- [ ] **6.5 Sistema de ventanas**: un proceso "servidor de pantalla" (espíritu Intuition) que posee el framebuffer; las apps piden ventanas y reciben eventos por IPC. Ventanas sin solapamiento primero (tiling), solapadas después.
- [ ] **6.6 Escritorio mínimo**: lanzador de programas, reloj, y las apps de 5.x corriendo en ventanas. **← Hito Amiga conseguido.**
- [ ] **6.7 Sonido**: driver + syscall de audio sobre el motor DMA de FPGA-B; PSG chiptune en gateware si sobran LUTs; un reproductor de módulos sería el homenaje Amiga definitivo.
- [ ] **6.8 (Aspiracional) Self-hosting**: ensamblador nativo corriendo en la propia máquina — el punto en el que ya no necesitas el PC.

## Etapa 7 — Integración física (portátil)

*(pendiente de detallar: carcasa, panel, teclado físico, batería — se abrirá cuando el hito Apple II esté cerca)*

---

## Cuestiones abiertas (heredadas + nuevas)

1. ~~Panel eDP de 14"~~ — **cerrada**: panel 10.1" 1024×600 RGB TTL directo. Pendiente solo confirmar part number/datasheet (0.4). El eDP de 14" con bridge externo queda como idea de fase 2, sin reserva en placa.
2. Formato de PTE (4.4).
3. ~~TLB miss~~ — **cerrada**: walk hardware sobre la tabla en SRAM.
4. ~~ANX6345~~ — **cerrada**: eliminado del proyecto al pasar a panel RGB directo.
5. Resultado de la hoja de pines (0.5): decidir qué se recorta si FPGA-A no cabe en ~107 I/O.
6. ~~Rails adicionales / PLL del iCE40~~ — **cerrada** en la hoja 1.1: VCCPLL desde 1.2 V con filtro RC por chip; VPP_2V5 vía BAT54 desde 3V3. Sin rail de 2.5 V.
7. Plataforma de validación previa: un iCEBreaker o similar (iCE40UP5K) sirve para practicar el toolchain, aunque el UP5K no es el mismo chip; valorar si merece la pena.
8. ~~Sonido~~ — **cerrada**: DAC I2S PCM5102 colgado de FPGA-B (3 pines); muestras por DMA desde la PSRAM de vídeo ("chip RAM" estilo Amiga). PSG en gateware como opción futura.
9. ~~Ratón~~ / ~~teclado matriz~~ — **cerrada**: se elimina la matriz de FPGA-A. El RP2040 es el **hub HID** (teclado y ratón) vía USB host con PIO-USB y/o PS/2, publicando eventos a FPGA-A por I2C. Un teclado interno futuro (Etapa 7) sería una matriz escaneada por el RP2040, no por el FPGA.
10. Ensamblador cruzado definitivo (5.0): 64tass vs. herramientas WDC.
11. ~~PS/2~~ — **cerrada**: solo USB (PIO-USB, puerto USB-A host).
12. ~~Ancho de banda PSRAM de vídeo~~ — **cerrada**: resuelto con pixel doubling (512×300 @ 8 bpp ≈ 9.2 MB/s en 1× QSPI) + modo texto sin framebuffer. 2º footprint PSRAM sin poblar deja abiertos los modos extendidos.
13. Datasheet del **módulo** ER-TFT101-1: straps expuestos en el FPC de 50, spec del backlight (¿PT4110?), y si el módulo necesita algo más que 3.3 V + backlight. Táctil: **decidido capacitivo (GT911, I2C)** — el resistivo exigiría 4 ADC que el RP2040 no tiene libres (hoja 1.2). Pedir la variante capacitiva del módulo.
14. Batería 1S concreta: capacidad, formato, si trae NTC (condiciona R_PROG del MCP73871 y THERM).
15. Confirmar VPP_2V5 de los iCE40 a 3.3 V en el datasheet de Lattice (práctica común en placas de referencia).

## Registro de decisiones

- **2026-08-02** — Descartado soldar BGA; QFN con pad térmico viable.
- **2026-08-02** — Reducción de ambición: se abandona el ECP5 (BGA-only) y se pasa a **3× iCE40HX4K en TQFP-144**, todos soldables a mano: FPGA-A (MMU/caché/periféricos), FPGA-B (vídeo), FPGA-C (softcore, opcional y sin poblar inicialmente). Se explota que el HX4K es un die HX8K (~7.680 LUTs con toolchain abierto). Consecuencia arquitectónica: tabla de páginas en SRAM externa + TLB en BRAM, walk hardware. W65C816S en PLCC-44 con zócalo. PCB única a 2 capas.
- **2026-08-02** — Definido el objetivo de experiencia en dos hitos: **"Apple II"** (arranque autónomo a prompt con teclado y pantalla propios, Etapa 5) y **"Amiga"** (multitarea preemptiva + GUI de ventanas, Etapa 6). Destapa dos decisiones de hardware pendientes antes de congelar la PCB: sonido y ratón.
- **2026-08-02** — **Audio**: DAC I2S PCM5102 en FPGA-B; la PSRAM de vídeo actúa como "chip RAM" (framebuffer + buffers de audio con DMA, homenaje a Paula). **HID**: eliminada la matriz de teclado de FPGA-A (−16 pines); el RP2040 pasa a ser hub HID (USB host vía PIO-USB y/o PS/2) y publica eventos por I2C. El puerto USB nativo del RP2040 queda reservado para consola/programación.
- **2026-08-02** — **Vídeo redefinido**: panel 10.1" IPS 1024×600 RGB TTL conectado **directo** a FPGA-B (ANX6345 y eDP eliminados del proyecto; eDP 14" relegado a idea de fase 2). **PS/2 descartado**, entrada solo por USB-A host (PIO-USB). Táctil resistivo del panel al RP2040 como posible puntero.
- **2026-08-02** — **Pixel doubling como estrategia de vídeo**: el panel escanea siempre 1024×600@60, el framebuffer trabaja a 512×300 (modo estrella: 8 bpp paleta, ~9.2 MB/s). Resolución y bpp son modos de gateware, no decisiones de PCB. En placa: **2 footprints de APS6404L en FPGA-B, 1 poblado** — el segundo habilita modos extendidos futuros (16 bpp o 1024×600 nativo).
- **2026-08-02** — **Panel candidato fijado: ER-TFT101-1** (10.1" IPS 1024×600, HX8282). Datasheet del HX8282-A11 verificado: TTL 24-bit compatible 3.3 V, modo DE por defecto, modeline típ. 51.2 MHz / 1344×635 → 60 Hz, latch en flanco de bajada, modo BIST para probar el panel sin gateware. Backlight con boost tipo PT4110.
- **2026-08-02** — **Hoja de pines (0.5) resuelta: la arquitectura cabe en 3× TQ-144.** FPGA-A 101/107 (crítico, palancas documentadas: reutilizar SPI de config, enlace a 4 bits DDR, quitar NMIB/VPB, SRAM de 256 KB); FPGA-B 61/107; FPGA-C 43/107. Decisión colateral: la SD del sistema se queda en FPGA-A (el swap necesita camino rápido); los relojes QSPI se comparten entre las dos PSRAM de cada FPGA; el VGA de bring-up reutiliza los bits altos del bus RGB del panel vía jumpers (+2 pines solo de syncs).
- **2026-08-02** — **Eliminado el enlace inter-FPGA dedicado: bus compartido estilo Amiga.** FPGA-B cuelga del bus del CPU (D/BA, A[15:0], RWB, PHI2) y el CPU escribe VRAM directamente vía apertura de 64 KB en banco $FE (registro de base para ventanear los 8 MB). La MMU asierta VRAM_SEL tras traducir y comprobar permisos (la protección se conserva: solo procesos con el mapeo ven la VRAM). Contención con el escaneo: FIFO de escritura + RDY/BWAIT en lecturas. FPGA-A puede hacer DMA a VRAM tomando el bus (BE=0), reutilizando el arbitraje del softcore. Pines: FPGA-A 101→93 (~13 % margen), FPGA-B 61→79.
- **2026-08-02** — **Hoja 1.1 (alimentación) diseñada.** Descubrimientos del diseño: hace falta un boost de 5 V para el VBUS del puerto USB-A host (en batería no hay 5 V); los boosts de backlight y VBUS cuelgan de SYS (no de VBAT) para que todo funcione por USB con batería agotada; USB-C necesita CC1/CC2 con 5.1 kΩ; VPP_2V5 de los iCE40 vía BAT54 desde 3V3 (evita rail de 2.5 V) — cierra la cuestión abierta 6. Secuenciado: power → RP2040 → 1.2 V → FPGAs → panel → backlight → 5 V host, todo gobernado por el RP2040 como EC.
- **2026-08-02** — **Hoja 1.2 (RP2040/EC) diseñada.** GPIO cerrado en 28/30 con expander I2C MCP23017 para señales lentas (sin él: ~38, no cabía). SPI separados para SD y config de FPGAs; CRESET individual por chip para reconfigurar gateware de un FPGA sin tumbar los demás. Flash de 16 MB con bitstreams/BIOS de respaldo (arranque sin SD). Decisión forzada: táctil **capacitivo GT911** (el resistivo necesitaba 4 ADC inexistentes).
- **2026-08-03** — **Diseño de todas las hojas del esquemático (1.1–1.6) completado a nivel de documento.** Decisiones nuevas de la tanda: estado seguro del bus sin bitstream (RESB pull-down, CPU parado — la placa se alimenta sin gateware sin conflictos); W65C816S confirmado a 3.3 V con PHI2 inicial 1–4 MHz; oscilador único de 25 MHz en estrella a los 3 FPGA (PLLs derivan core/pclk/softcore); SRAM en SOP-32 soldada (privada de FPGA-A: es caché, no vive en el bus del CPU); PCM5102A en modo PLL interno (SCK a GND); footprint opcional de ampli PAM8302 para el altavoz del futuro portátil; botón de usuario a FPGA-A como NMI de depuración del OS. **Queda: dibujar todo en KiCad (con el datasheet del módulo del panel en mano para 1.5) y la revisión 1.7.**
- **2026-08-02** — **Hoja 1.1 (alimentación) diseñada a nivel de bloque** (`hoja-1-1-alimentacion.md`): 3V3 always-on + rails secuenciados por el RP2040 vía GPIOs (1.2 V, panel, backlight, 5 V host). **Rail nuevo descubierto**: boost de 5 V (TPS61023) para el VBUS del puerto USB-A host. Filtro RC en VCCPLL de cada iCE40. Bring-up aislable por jumpers 0 Ω por rail.
