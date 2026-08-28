# Documento de Diseño — Ordenador Personal Portátil basado en W65C816S (iteración iCE40)

## 1. RESUMEN EJECUTIVO

Este documento describe el diseño completo de un ordenador personal portátil **original** construido alrededor del microprocesador físico WDC W65C816S (CPU de 16 bits, bus de datos de 8 bits, direccionamiento de 24 bits / 16 MB), reorientado desde una arquitectura de dos FPGA Lattice ECP5-25F (BGA256) hacia la familia Lattice iCE40, por el requisito ineludible de que todos los FPGA sean **soldables a mano**. No es un clon ni un emulador de SNES/Apple IIgs: es una máquina nueva, concebida como ejercicio de aprendizaje y como dispositivo funcional tipo portátil.

La arquitectura final propone **tres FPGA iCE40HX8K en encapsulado TQFP-144**. Según la hoja de datos oficial *iCE40 LP/HX Family Data Sheet DS1040 v3.4 (October 2017)* de Lattice Semiconductor, el HX8K ofrece **7.680 LUTs, 206 I/O, 128 Kbit de RAM y núcleo a 1,2 V** (Tabla 1-1); cada bloque sysMEM EBR es de "4 kbit in size" (32 bloques × 4 Kbit = 128 Kbit). Estos FPGA se reparten en: FPGA-A = MMU + controlador de caché + arbitraje de memoria; FPGA-B = periféricos (controlador de interrupciones, timer, UART, SPI-SD, I2C, matriz de teclado); FPGA-C = subsistema de vídeo. El W65C816S se ejecuta como CPU física con su bus también cableado a GPIOs del FPGA-A, de modo que un softcore (referencias srg320/P65816 en VHDL y FT816) pueda tomar el control mediante BE=0 sin desoldar nada. PHI2 lo genera el FPGA. Toda la lógica funciona a 3,3 V; el núcleo del iCE40 a 1,2 V.

La jerarquía de memoria se mantiene en tres niveles pero se amplía: caché en SRAM asíncrona de 512 KB (líneas de 256 bytes, mapeo directo, write-back con bits dirty); memoria principal inicial en PSRAM (2× APS6404L, 16 MB, QSPI); y **nueva SDRAM SDR de 3,3 V (Winbond W9825G6KH, 256 Mbit = 32 MB, TSOP-II 54) como memoria "extendida" ampliable**. El swap reside en la microSD. Se conservan las páginas de 2 KB, el banco 0 fijado (pinned) en SRAM y el banco $FF reservado para E/S privilegiada auto-protectora.

**Cambios clave respecto a la iteración ECP5:** (1) el paso a iCE40 reduce drásticamente la BRAM disponible (128 Kbit por HX8K frente a ~1 Mbit del ECP5-25F), de modo que la tabla de páginas plana de 4.096 entradas ya no cabe en BRAM y pasa a residir en SRAM/SDRAM con una TLB pequeña en EBR y un walker hardware; (2) hacen falta tres FPGA en lugar de dos por la menor densidad lógica; (3) el subsistema de vídeo de alta resolución sobre eDP se degrada a una fase posterior: el camino principal pasa a ser VGA/DPI de baja resolución (640×480 / 800×600) por las limitaciones de reloj de píxel y ancho de banda del iCE40; (4) el rail de core del FPGA cambia de 1,1 V (ECP5) a **1,2 V** (iCE40).

**Riesgos principales:** cierre de temporización del walker de MMU y del controlador SDRAM cerca del techo práctico del iCE40 (≈50 MHz para lógica compleja); ancho de banda de framebuffer insuficiente para resoluciones altas; complejidad del bring-up del panel eDP de 14"; e inmadurez del toolchain C para 65816. **Estado actual:** fase de diseño; el diagrama de bloques HTML/SVG estilo serigrafía de PCB (bloques futuros en trazo discontinuo, componentes clave con borde dorado) se mantiene como documento vivo y este documento puede referenciarlo.

---

## 2. DISEÑO HARDWARE

### 2.1 Elección de FPGA iCE40 y justificación cuantitativa por recursos

La familia iCE40 ofrece varias opciones soldables a mano. Sus recursos, verificados en las hojas de datos de Lattice, son:

- **iCE40HX8K-TQ144:** 7.680 LUTs, 32 bloques EBR de 4 Kbit = **128 Kbit de EBR**, 2 PLL, hasta 206 I/O (menos en el TQ144 real). Encapsulado TQFP-144 (pitch 0,5 mm), soldable a mano con práctica. Toolchain abierto yosys/nextpnr/IceStorm plenamente soportado (Project IceStorm documenta el HX8K explícitamente; el trabajo original se hizo sobre HX1K-TQ144 y HX8K-CT256).
- **iCE40HX4K-TQ144:** nominalmente 3.520 celdas, pero es el **mismo die que el HX8K**; con yosys/nextpnr se acceden los 7.680 LUT y los 128 Kbit de EBR. Es decir, HX4K-TQ144 y HX8K-CT256 son eléctricamente el mismo silicio bajo el toolchain abierto (esto lo confirma la documentación de la placa BlackIce MX: "As the icestorm tools treat the HX4K device as an HX8K device, the specs of the HX8K are most relevant").
- **iCE40UP5K-SG48 (UltraPlus, QFN-48):** 5.280 LUT4, 120 Kbit de EBR **más 1 Mbit de SPRAM**. La *iCE40 UltraPlus Family Data Sheet FPGA-DS-02008* confirma literalmente que "the iCE40 UltraPlus devices also feature four 256 kb SPRAM blocks that can be cascaded to create up to 1 Mb block"; cada bloque es 16K × 16. QFN-48 con 39 I/O, 1 PLL. Su gran ventaja es la SPRAM; su gran desventaja es la Fmax baja (osciladores internos de 48 MHz y, en la práctica del toolchain abierto, cierres de temporización frecuentemente por debajo de ~20 MHz para lógica no trivial; picosoc sobre UP5K se ha reportado cerrando a ~11-12 MHz).

**Decisión:** el proyecto usará **iCE40HX8K en TQFP-144** como dispositivo principal por su mayor densidad lógica (7.680 LUT) y sus I/O, imprescindibles para los buses paralelos (bus del 65816, SRAM, SDRAM, RGB de vídeo). El TQFP-144 es el único encapsulado del HX8K razonablemente soldable a mano (el resto del HX8K solo existe en BGA/csBGA/ucBGA). Se reservará **un iCE40UP5K** como opción para el FPGA de vídeo si se necesita su SPRAM de 1 Mbit como framebuffer/line-buffer, aceptando su menor Fmax.

**Presupuesto de recursos y partición propuesta en tres FPGA:**
- **FPGA-A (HX8K) — MMU + caché + memoria:** walker de tabla de páginas, TLB en EBR, controlador de caché SRAM, controladores PSRAM QSPI y SDRAM, arbitraje. Es el que más presión de recursos sufre. Presupuesto EBR: la TLB de 64 entradas × ~5 bytes ≈ 2,5 Kbit + buffers de línea de caché (256 B = 2 Kbit por buffer) + FIFOs de controlador → holgado dentro de 128 Kbit, precisamente porque la tabla de páginas ya **no** vive aquí.
- **FPGA-B (HX8K) — periféricos + CPU glue:** generación de PHI2, decodificación del banco $FF, controlador de interrupciones, timer, UART, SPI-SD, I2C (fuel gauge, ANX6345), matriz de teclado, y GPIOs hacia el bus del 65816 para el softcore.
- **FPGA-C (HX8K o UP5K) — vídeo:** generador de temporización, framebuffer/line-buffer, salida RGB paralela (DPI) y DAC R-2R para VGA.

Por qué tres y no dos: en la iteración ECP5, dos FPGA de ~24.000 LUT y ~1 Mbit de BRAM absorbían MMU+caché+periféricos en uno y vídeo en otro. Con solo 7.680 LUT y 128 Kbit de EBR por iCE40, la suma de MMU con walker, caché, dos controladores de memoria y todos los periféricos excede holgadamente un HX8K; separar periféricos en un tercer FPGA descarga al FPGA-A y deja margen de temporización.

**Enlace inter-FPGA:** bus paralelo síncrono de propósito general entre los tres iCE40 (por ejemplo, 8-16 bits de datos + control, registrado en ambos extremos), más señales dedicadas. El enlace de vídeo A→C transporta las escrituras del framebuffer; el enlace A/B comparte el bus de sistema. Dado el número limitado de I/O útiles en TQ144, este enlace debe dimensionarse con cuidado (multiplexado si es necesario).

### 2.2 CPU física + softcore

El W65C816S físico (TQFP-44, 3,3 V) es la CPU principal. Según la Tabla 5-2 *W65C816S AC Characteristics* del datasheet de WDC, a **3,3 V ±10% el grado especificado es de 8 MHz** (VDD 3,0-3,6 V, tiempo de ciclo tCYC mínimo 125 ns); los 14 MHz oficiales corresponden a 5,0 V ±5% (tCYC 70 ns). Como toda la lógica del sistema es a 3,3 V, el objetivo de reloj de la CPU física es del orden de 8 MHz. PHI2 lo genera el FPGA-B (no un oscilador externo), lo que permite estirar el reloj (clock-stretching) durante fallos de caché o de TLB manteniendo la CPU completamente estática (el 65816 es CMOS totalmente estático y puede detenerse indefinidamente).

El pin **BE (Bus Enable)** permite poner en alta impedancia los buffers de dirección/datos/RWB; con BE=0 el softcore en FPGA puede conducir el bus sin desoldar el chip físico. El pin **ABORTB** interrumpe la instrucción en curso sin modificar registros internos ("The ABORTB input can interrupt the currently executing instruction without modifying internal register, thus allowing virtual memory system..." — datasheet WDC), base del soporte de memoria virtual y de la protección. Los softcores de referencia son srg320/P65816 (VHDL, del proyecto FpgaSnes / usado en MiSTer SNES) y FT816. Aprendizaje conservado: en el softcore, **reducir CPI es más rentable que subir Fmax**, y la ejecución desde Block RAM es crítica para el rendimiento.

### 2.3 Subsistema de memoria y mapa físico

**Tres niveles:**

1. **Caché — SRAM asíncrona de 512 KB:** líneas de 256 bytes, mapeo directo, write-back con bits dirty. El **banco 0 está fijado (pinned) residente en SRAM** porque la pila y la Direct Page siempre direccionan el banco 0. Un controlador de SRAM asíncrona sobre iCE40 puede alcanzar accesos muy rápidos; el ejemplo de SRAM de la placa BlackIce-II (mystorm-org) demuestra un controlador que "can access the SRAM at one transaction every 2 clock cycles while still running at 100MHz", empleando ambos flancos del reloj de 100 MHz — evidencia directa de que la ruta de E/S de memoria de esta clase de FPGA llega a 100 MHz con el toolchain abierto.

2. **Memoria principal — PSRAM QSPI (2× APS6404L, 16 MB) en dual QSPI:** La restricción **tCEM** gobierna la longitud máxima de ráfaga y, por tanto, el tamaño de línea de caché, no el tamaño de página. Según *APM QSPI PSRAM Datasheet Rev. 4.0 (Jan 05, 2024)*, Tabla 10: "tCEM — CE# low pulse width — 3 µs Extended grade / 8 µs Standard grade"; la frecuencia máxima es "144 MHz" en las operaciones generales (tCLK mín. 7 ns) y 66 MHz para lectura QPI con el comando 'h0B. En la práctica a 3,3 V el datasheet da 109 MHz para "all other operations". El tamaño de página (2 KB) y el tamaño de línea de caché (256 bytes) son **parámetros independientes** (aprendizaje conservado); solo la línea de caché queda gobernada por tCEM.

3. **Memoria extendida — SDRAM SDR (Winbond W9825G6KH, 256 Mbit = 32 MB, TSOP-II 54):** nueva. Según el datasheet Winbond *W9825G6KH 4M×4 BANKS×16 BITS SDRAM* (release Mar. 20, 2017): "organized as 4M words x 4 banks x 16 bits... up to 200M words per second. The -6/-6I/-6L grade parts are compliant to the 166MHz/CL3 specification"; 3,0-3,6 V, encapsulado TSOP-II de 54 pines. En DigiKey el W9825G6KH-6 se lista en torno a **2,32-5,74 USD/unidad** según grado y cantidad. Candidatas alternativas, todas TSOP-II 54 soldable a mano y ~3,3 V: **ISSI IS42S16160** (16M×16, grado -7 a 143 MHz) y **Alliance AS4C16M16SA** (256 Mbit, ~5-7 USD en DigiKey).

**Controladores SDRAM open-source probados en iCE40 (hallazgos de investigación dirigida):**
- `lawrie/blackicemx_nmigen_examples` (Lawrie Griffiths): incluye controladores `sdram` (8-bit dual port) y `sdram16` (16-bit single port) sobre BlackIce MX (HX4K→HX8K), construidos con "yosys, nextpnr-ice40, and icepack".
- `niklasnisbeth/ice40-litedram`: port de LiteDRAM al HX8K EVB que "instantiates a LiteDRAM controller that talks to the Labitat SDRAM expansion... It uses the AS4C16M16 SDR SDRAM chip from Alliance" (nota: requiere parche para sintetizar).
- `knielsen/ice40-stm32-sdram` + placa `niklasnisbeth/lattice-ice40-hx8kevb-sdram`: usa "a modified version of Lattice's example SDRAM controller", "Tested and working" sobre HX8K con la misma expansión AS4C16M16.
- `ZipCPU/zipstormmx` (Dan Gisselquist): SoC iCE40 con SDRAM "up and running and working nicely on the hardware" en BlackIce MX.

**Frecuencia realista de SDRAM en iCE40 HX8K:** una máquina de estados de controlador SDRAM sencilla cierra timing con holgura a **~50 MHz**; alcanzar un controlador completo a 100 MHz con lógica circundante es el techo práctico de la fabric y depende del diseño. Referencia de calibración: el ZipCPU se documenta "at 50MHz on an iCE40 HX8k" y ~25-30 MHz en LP8k. Diseño recomendado: **SDRAM a 50 MHz, CL2/CL3**, con refresco cumpliendo 8.192 ciclos/64 ms.

**Mapa de memoria física propuesto (cada proceso ve un espacio virtual completo de 16 MB):**

| Rango virtual (banco) | Uso | Respaldo físico |
|---|---|---|
| Banco $00 | Pila, Direct Page, kernel crítico | SRAM (pinned, siempre residente) |
| Bancos $01–$FE | Espacio de usuario paginado (páginas de 2 KB) | Frames en PSRAM (nivel 0 de RAM) y SDRAM (extensión del mismo nivel); swap en microSD |
| Banco $FF | E/S privilegiada (registros de periféricos) | Decodificado por FPGA-B; no paginable, auto-protector |

La PSRAM y la SDRAM se tratan como **un único pool de frames físicos del mismo nivel jerárquico**: los frames de 2 KB se asignan primero en PSRAM (menor latencia de acceso aleatorio) y, al agotarse o según política del asignador, en SDRAM. La tabla de páginas y el mecanismo de swap **no cambian**: solo crece el número de frames disponibles. Espacio físico total: 16 MB (PSRAM) + 32 MB (SDRAM) = 48 MB → 24.576 frames de 2 KB → se requieren 15 bits de número de frame (32.768 frames, cubre 64 MB con margen para crecimiento).

### 2.4 MMU / TLB adaptada a los recursos del iCE40

En la iteración ECP5 la tabla de páginas de 4.096 entradas residía en BRAM. Con solo 128 Kbit de EBR por HX8K esto ya no cabe cómodamente: 4.096 entradas × 4 bytes = 16 KB = 128 Kbit, exactamente **toda** la EBR del chip, dejando cero para el resto del diseño. **Solución adoptada:**

- La **tabla de páginas plana** (~32 KB por proceso, ver §4.1) reside en SRAM/SDRAM, no en EBR.
- En la EBR del FPGA-A solo se implementa una **TLB pequeña** (32-64 entradas, totalmente asociativa o mapeo directo) que evita las búsquedas recursivas en PSRAM/SDRAM.
- El fallo de TLB se resuelve con un **walker hardware** (máquina de estados que lee la PTE de la SRAM/SDRAM), **preferido sobre el trap software** porque el trap recursivo en el 65816 es costoso en ciclos y complejo de reentrar (habría que traducir la propia dirección de la tabla de páginas).
- El pin **ABORTB** se activa ante fallo de página (present=0) o violación de permisos, imponiendo la protección de memoria por hardware.

Cuestiones abiertas del proyecto ahora resueltas en este diseño: formato de PTE (§4.1), page-table walk por **hardware** (no trap software) y diseño del cambio de contexto (§4.2).

### 2.5 Subsistema de vídeo: opción realista + camino futuro

**Análisis honesto de límites del iCE40:** el iCE40 no tiene salidas LVDS/TMDS verdaderas ni PLLs de gigahercios; las "salidas LVDS" se emulan con dos LVCMOS y tres resistencias externas montadas junto a los pines (documentado por Dan O'Shea y otros). Un reloj de píxel para 1366×768@60 requiere ~72 MHz y 1080p requiere 148,5 MHz — **ambos descartados** en iCE40, tanto por el techo de reloj de la lógica como por el ancho de banda de framebuffer.

**Camino principal (fase de vídeo inicial):** salida **VGA por DAC R-2R a 640×480@60**. Según Project F (*Video Timings*), el modeline es "640x480_60 25.175 640 656 752 800 480 490 492 525 -HSync -VSync" con "Pixel Clock 25.175 MHz... Horizontal Freq. 31.469 kHz"; muchos diseños usan 25 MHz prácticos. Alternativa **800×600@60**, para el que "the pixel clock for 800x600 is precisely 40 MHz" (Project F), ya cerca del techo. El framebuffer para 640×480×8 bpp = 307 KB cabe holgadamente en la SDRAM de 32 MB; el ancho de banda necesario a 25 MHz × 1 byte/píxel = 25 MB/s es perfectamente asumible por un controlador SDRAM de 50 MHz (100 MB/s teóricos a 16 bits). Como salida digital directa alternativa, **DPI/TTL RGB** hacia un panel LCD de menor resolución.

**Camino futuro (fase posterior):** panel eDP de 14" mediante el puente **ANX6345**. Es un "ultra-low power Full-HD DisplayPort/eDP transmitter designed for portable devices" que "transforms the LVTTL RGB output of an application processor to eDP or DisplayPort" con **link training autónomo en hardware**. Se prefiere sobre el **RTD2556** (descartado por requerir firmware bajo NDA de Realtek). El ANX6345 se controla por I2C (direcciones 0x38/0x39) y su secuencia de init la ejecuta el **RP2040** antes de arrancar el 65816; el driver de referencia es `analogix-anx6345.c` de Vasily Khoruzhick (mainline Linux `drivers/gpu/drm/bridge/analogix/`), con la secuencia `anx6345_tx_initialization()` → `anx6345_dp_link_training()` → `anx6345_config_dp_output()`, más las cabeceras anx98xx-edp.h. El binding de device-tree confirma los rails necesarios: **dvdd12 (1,2 V) y dvdd25 (2,5 V)**, más panel-supply. Ejemplos reales del ANX6345 con paneles de 14"/13": Pinebook y Olimex Teres-I (panel Innolux N116BGE eDP).

**Advertencia honesta:** los paneles eDP suelen exigir su timing nativo (1366×768 → ~72 MHz de reloj de píxel). Si el iCE40 no puede generar ese reloj de píxel con ancho de banda de framebuffer suficiente, el panel eDP de 14" se convierte en un objetivo de fase avanzada que puede requerir reducir profundidad de color, usar un modo reducido, o incluso migrar el FPGA de vídeo a un dispositivo más capaz. El modeline del panel se fija en el gateware de vídeo.

### 2.6 Audio

Solución sencilla y de bajo coste en recursos: **salida sigma-delta / PWM de 1 bit** generada en el FPGA-B, filtrada con un RC pasivo y amplificada a un altavoz/auriculares (cero componentes críticos, mínimo consumo de LUTs). Alternativa de mayor calidad: **DAC I2S externo** (códec I2S de bajo coste) con un maestro I2S en el FPGA. Recomendación: empezar por PWM/sigma-delta y migrar a I2S si se requiere mejor SNR.

### 2.7 RP2040 y su papel

El microcontrolador embebido **RP2040** es el "supervisor" del sistema: (1) **programación de los tres FPGA** (JTAG + slave-SPI, cargando el bitstream desde microSD); (2) carga de bitstream/BIOS desde microSD; (3) **USB** (programación + consola serie); (4) **secuenciación de alimentación**; (5) **carga de batería** vía MCP73871 y **fuel gauge** MAX17048 por I2C; (6) ejecución de la **secuencia de init del ANX6345** por I2C antes de liberar el RESET del 65816.

### 2.8 Alimentación y batería

- **Rail principal 3,3 V:** convertidor buck-boost **TPS63020** desde batería LiPo 1S (tolera que la tensión de celda caiga por debajo o suba por encima de 3,3 V).
- **Rail de core 1,2 V del iCE40:** LDO dedicado. **Cambio obligado por el paso a iCE40:** el iCE40 usa VCC de núcleo de 1,2 V, frente a los 1,1 V del ECP5. Convenientemente, el ANX6345 también necesita 1,2 V (dvdd12), lo que puede compartir rail.
- **Rail 2,5 V:** LDO, necesario para dvdd25 del ANX6345 y para bancos I/O si aplica.
- **Carga de batería:** MCP73871 (carga LiPo + power path). **Fuel gauge:** MAX17048 por I2C hacia el RP2040.

### 2.9 Teclado, almacenamiento, USB y conexionado/buses

- **Teclado matricial:** escaneado por el FPGA-B, con antirrebote y buffer de eventos leído por el kernel.
- **Almacenamiento SD:** microSD en modo SPI (SPI-SD) controlado por el FPGA-B; contiene bitstreams, BIOS, imagen del SO y el área de swap.
- **USB de aplicación:** chip externo **MAX3421E** (o similar) por SPI, **no implementado en HDL** por la complejidad del protocolo USB. El RP2040 aporta además su propio USB para programación/consola.
- **Buses entre chips:** bus del 65816 (dirección A0-A15 + bank address BA0-BA7 multiplexado en la primera mitad de ciclo sobre D0-D7) hacia FPGA-A/B; bus paralelo SRAM; bus SDRAM (dirección multiplexada filas/columnas + control JEDEC); QSPI dual a PSRAM; enlaces inter-FPGA registrados; I2C compartido (ANX6345, MAX17048); SPI (SD, MAX3421E, slave-SPI de configuración FPGA).

---

## 3. DISEÑO DE LA BIOS

**Dónde reside:** la BIOS del 65816 se almacena en la microSD y/o en una flash SPI dedicada; el **RP2040 la precarga en la SRAM** (banco 0, pinned) antes de liberar el RESET del 65816. Este esquema evita necesitar una ROM paralela dedicada y aprovecha que el RP2040 ya es dueño de la microSD y del bus de configuración.

**Secuencia completa de arranque desde power-on:**
1. **Secuenciación de alimentación:** el RP2040 arranca primero (desde su propia flash), habilita el TPS63020 (3,3 V), luego los LDO de 1,2 V y 2,5 V en el orden requerido por los iCE40.
2. **Configuración de los tres iCE40:** el RP2040 carga los bitstreams desde microSD por slave-SPI (o supervisa la autoconfiguración desde flash SPI de cada FPGA). Verifica CDONE de cada dispositivo.
3. **Inicialización de memoria:** el FPGA-A inicializa la SDRAM (secuencia JEDEC: espera de estabilización de 100-200 µs, precarga de todos los bancos, refrescos de auto-refresh iniciales, programación del Mode Register con CAS latency 2/3 y burst length) y calibra la PSRAM QSPI (salida de modo SPI a QPI, ajuste de wait states).
4. **Inicialización de vídeo:** el RP2040 ejecuta por I2C la secuencia de init del ANX6345 (power-up → `tx_initialization` → `dp_link_training`) si el panel eDP está presente; si no, el FPGA-C activa la salida VGA/DPI directamente. El modeline se fija en el gateware.
5. **Precarga de BIOS y vectores:** el RP2040 escribe la BIOS en SRAM (banco 0), incluyendo el **vector RESET en $00FFFC/$00FFFD** apuntando al arranque de la BIOS.
6. **Liberación del RESET del 65816:** el FPGA-B genera PHI2 y libera RESB (manteniéndolo bajo ≥2 ciclos de reloj tras estabilización de VDD, según datasheet WDC). El 65816 arranca en modo emulación, salta al vector RESET y ejecuta la BIOS.

**Cómo la BIOS localiza y lanza el SO:** la BIOS inicializa la consola serie (UART), monta el sistema de ficheros de la microSD (vía el driver SPI-SD del FPGA-B), localiza la imagen del kernel del SO, la carga en memoria (frames de PSRAM/SDRAM vía MMU) y salta a su punto de entrada, conmutando a modo nativo del 65816.

**Servicios mínimos al SO:** la BIOS ofrece rutinas de arranque temprano (E/S de consola por polling, lectura de bloques SD, tabla de descripción del hardware/memoria detectada). Una vez el SO toma el control, sus propios drivers en kernel sustituyen a los de la BIOS: se adopta un modelo de **handoff completo**; la BIOS no permanece como capa residente de servicios.

---

## 4. DISEÑO DEL SO

Multitarea preemptiva con procesos de usuario aislados del hardware. Syscalls vía instrucción COP; fallos hardware vía ABORT. Los drivers son internos al SO; la capa de syscalls es la API de cara al usuario. Cada proceso ve un **espacio virtual completo de 16 MB** (el modelo "un banco por proceso" fue descartado).

### 4.1 Memoria virtual y paginación

**Formato de PTE propuesto (32 bits / 4 bytes por entrada):**
- Bits 0-14: **número de frame físico** (15 bits → 32.768 frames de 2 KB = 64 MB físicos direccionables; cubre PSRAM 16 MB + SDRAM 32 MB con margen).
- Bit 15: **P** (present) — si 0, fallo de página → ABORT.
- Bit 16: **R** (readable).
- Bit 17: **W** (writable).
- Bit 18: **X** (executable).
- Bit 19: **D** (dirty) — puesto por hardware en escritura.
- Bit 20: **A** (accessed) — puesto por hardware en cualquier acceso.
- Bit 21: **U/S** (user/supervisor).
- Bits 22-31: reservados / flags de swap (bit "en swap" + índice de bloque SD).

Con páginas de 2 KB: offset de 11 bits y número de página virtual de 13 bits (8.192 páginas × 2 KB = 16 MB). La tabla de páginas plana ocupa 8.192 × 4 B = **32 KB por proceso** (reside en SDRAM/SRAM, no en EBR).

**TLB:** 32-64 entradas en EBR del FPGA-A. En fallo de TLB, el **walker hardware** lee la PTE de la tabla en memoria y la carga en la TLB. En fallo de página (P=0) o violación de permisos, el FPGA-A activa **ABORTB**; el 65816 salta al vector ABORT y el kernel gestiona el fallo (asigna frame, trae la página del swap SD, actualiza PTE, invalida la entrada TLB). **Swap a SD:** con granularidad de página (2 KB); el kernel elige víctima según bits A/D (algoritmo de reloj / segunda oportunidad).

### 4.2 Cambios de contexto

Guardado/restauración del **estado completo del 65816**: A, X, Y, SP, DP (Direct Page), DBR (Data Bank), PBR (Program Bank), P (status) y PC. Al conmutar de proceso, el kernel: (1) guarda estos registros en el PCB (Process Control Block) del proceso saliente; (2) **conmuta la tabla de páginas** actualizando el puntero base de tabla en el FPGA-A e **invalidando la TLB** (o usando un ASID si se implementa para evitar el vaciado); (3) notifica al core de gestión (FPGA-A) el cambio de proceso activo para conmutar las tablas de permisos; (4) restaura los registros del proceso entrante y retorna. **Coste estimado:** decenas de ciclos para guardar/restaurar registros (el 65816 tiene pocos registros arquitectónicos), más la penalización de la invalidación de TLB (los primeros accesos del nuevo proceso sufrirán fallos de TLB que el walker resolverá en pocos ciclos cada uno). El clock-stretching de PHI2 absorbe estas latencias sin perder ciclos de CPU útiles.

### 4.3 Planificador preemptivo

El timer del FPGA-B genera interrupciones periódicas (IRQ) que disparan el planificador. Algoritmo round-robin con prioridades como base; el quantum se ajusta según la frecuencia del timer.

### 4.4 Modelo de drivers en kernel

Los **drivers son internos al SO** (no hay drivers en espacio de usuario). Drivers para: **vídeo** (framebuffer en FPGA-C), **audio** (PWM/I2S en FPGA-B), **teclado** (matriz escaneada por FPGA-B), **almacenamiento SD** (SPI-SD), **serie** (UART). Los procesos de usuario acceden a ellos únicamente vía syscalls.

### 4.5 Capa de syscalls vía COP

La API de cara al usuario son las **syscalls vía la instrucción COP** del 65816. Los fallos hardware llegan vía ABORT; las syscalls vía COP. Tabla de llamadas propuesta:

| Nº COP | Syscall | Descripción |
|---|---|---|
| $00 | exit | Terminar proceso |
| $01 | read | Leer de descriptor |
| $02 | write | Escribir a descriptor |
| $03 | open | Abrir fichero SD |
| $04 | close | Cerrar descriptor |
| $05 | mmap/sbrk | Solicitar páginas |
| $06 | fork/spawn | Crear proceso |
| $07 | exec | Cargar ejecutable |
| $08 | yield | Ceder CPU |
| $09 | gfx | Operación de vídeo |
| $0A | snd | Operación de audio |
| $0B | kbd | Leer evento de teclado |
| $0C | time | Leer timer/reloj |

---

## 5. DISEÑO SOFTWARE

### 5.1 Modelo de procesos

Espacio virtual de 16 MB por proceso. **Formato de ejecutable propuesto:** cabecera con magic, tamaños de segmentos (código/datos/BSS), punto de entrada y tabla de secciones; segmentos mapeados por la MMU. **Por qué la MMU elimina la necesidad de relocalización:** como cada proceso ve su propio espacio virtual completo empezando en direcciones fijas, el enlazador puede generar código para direcciones virtuales fijas conocidas; la MMU traduce a frames físicos arbitrarios, de modo que el código no necesita relocalizarse en tiempo de carga (el requisito previo de "código relocalizable" se relaja a "código enlazado a direcciones virtuales fijas"). El código relocalizable sigue siendo útil para el kernel/BIOS que corren sin traducción.

### 5.2 Toolchain de programación (estado real del soporte 65816)

- **cc65 / ca65:** cc65 (compilador C) **no tiene un target 65816 real** — genera código 6502/65C02. Su ensamblador **ca65 sí soporta la sintaxis y opcodes del 65816**. Útil como ensamblador, no como compilador C nativo del '816. Confirmado en foros de 6502.org: "It's really too bad there is no actual 65816 target for cc65".
- **llvm-mos:** **no soporta 65816** actualmente (issue #32 abierta discutiendo estrategias, p. ej. "lie to codegen and claim that the 65816 is actually a 32-bit processor"). Solo 6502/65C02.
- **llvm-65816 (jeremysrand):** intento experimental de añadir 65816 a LLVM; el propio autor advierte "Don't even try to use it yet" — compila apenas un par de opcodes (LDA, RTL). No usable en producción.
- **WDC C compiler (WDCTools):** compilador C oficial de Western Design Center para 65816; funcional pero con limitaciones conocidas. Es la opción C más completa hoy para '816 nativo.
- **Ensambladores maduros y otras vías:** **64tass**, **ca65** y **Merlin32** soportan bien el 65816. **LCC-816** (port de LCC en github.com/lcc-816) es una alternativa de compilador C, también existe el port histórico de LCC de Toshiyasu Morita para Apple IIgs.

**Recomendación:** empezar con desarrollo en **ensamblador (ca65 o 64tass)** para kernel y drivers, y evaluar **WDCTools C** o **LCC-816** para aplicaciones de usuario. Aprendizaje conservado: en la ISA del 65816, **reducir CPI del softcore es más rentable que subir Fmax**.

### 5.3 ABI de syscalls, bibliotecas y flujo de desarrollo

**ABI:** parámetros de syscall en registros (A/X/Y) y/o en pila/Direct Page; número de syscall en el operando de COP; retorno en A/X con flag de error en C (carry). **Bibliotecas de sistema:** libc mínima (crt0, stdio sobre syscalls, malloc sobre mmap/sbrk). **Flujo típico:** compilar/ensamblar en PC → generar ejecutable → transferir por USB (vía RP2040) o copiar a microSD → el SO lo carga vía `exec` y lo ejecuta en su espacio virtual.

---

## 6. PLAN DE CONSTRUCCIÓN POR ETAPAS

**E0 — Placa mínima y free-run.** Objetivo: RP2040 + un iCE40HX8K + SRAM + 65816 en free-run (NOP slide). HW: PCB mínima, alimentación 3,3 V/1,2 V. Gateware: generación de PHI2, decodificación básica, bus a SRAM. SW: firmware RP2040 que configura el FPGA. **Test:** el 65816 recorre el bus de direcciones ejecutando NOPs ($EA) cableados; verificación con analizador lógico. **Entregable:** contador de direcciones incrementando a PHI2 conocido.

**E1 — Memoria y monitor serie.** Objetivo: SRAM funcional + UART. Gateware: controlador SRAM, UART en FPGA-B. SW: monitor en BIOS (examinar/modificar memoria, cargar por serie). **Test:** escribir/leer patrones en SRAM por el monitor; eco serie. **Entregable:** monitor interactivo por consola.

**E2 — PSRAM + caché.** Objetivo: PSRAM QSPI operativa como memoria principal con caché SRAM. Gateware: controlador QSPI (2× APS6404L dual), caché de líneas de 256 B write-back con bits dirty. **Test:** memtest sobre 16 MB; medición de hit rate y verificación de respeto de tCEM. **Entregable:** 16 MB accesibles con caché + benchmark de ancho de banda.

**E3 — MMU / TLB y protección.** Objetivo: memoria virtual con walker HW y ABORT. Gateware: TLB en EBR (32-64 entradas), walker hardware, generación de ABORTB. SW: gestión de fallos de página y de permisos. **Test:** proceso que provoca fallo de página y violación de permisos; verificar traps ABORT y recuperación. **Entregable:** traducción virtual→física con protección demostrada.

**E4 — SDRAM extendida.** Objetivo: SDRAM W9825G6KH integrada como extensión del pool de frames. Gateware: controlador SDRAM (init JEDEC, refresco 8.192/64 ms) a ~50 MHz. **Test:** memtest sobre los 32 MB adicionales; asignación de frames en SDRAM y coherencia con la caché. **Entregable:** 48 MB de RAM física total operativos.

**E5 — Vídeo VGA/DPI.** Objetivo: salida 640×480@60 (reloj 25,175 MHz) por DAC R-2R, o 800×600@60 (40 MHz) / DPI. Gateware: temporización de vídeo en FPGA-C, framebuffer en SDRAM. **Test:** patrón de barras y contenido de framebuffer en pantalla; verificar ausencia de tearing con doble buffer. **Entregable:** consola gráfica de baja resolución.

**E6 — Teclado + SD + SO mínimo.** Objetivo: entrada de teclado, sistema de ficheros SD y kernel mínimo. Gateware: escaneo de matriz, SPI-SD. SW: kernel con carga de ejecutables, syscalls COP básicas. **Test:** cargar y ejecutar un programa de usuario desde SD con entrada de teclado. **Entregable:** SO monotarea usable.

**E7 — Multitarea completa.** Objetivo: planificador preemptivo, cambios de contexto, procesos aislados. SW: timer-IRQ, scheduler round-robin, conmutación de tabla de páginas. **Test:** dos o más procesos concurrentes aislados entre sí; medir el coste real del cambio de contexto en ciclos. **Entregable:** SO multitarea preemptivo.

**E8 — Panel eDP + batería + chasis portátil.** Objetivo: portátil completo. HW: ANX6345 + panel eDP 14", batería LiPo + MCP73871 + MAX17048, chasis. SW: init del ANX6345 por RP2040 (I2C 0x38/0x39, link training), driver de vídeo eDP. **Test:** link training eDP correcto, imagen estable, autonomía de batería, lectura del fuel gauge. **Entregable:** portátil funcional autónomo. **Advertencia:** si el reloj de píxel / ancho de banda del iCE40 no alcanza el timing nativo del panel (≈72 MHz para 1366×768), esta etapa puede quedar limitada a un modo reducido, menor profundidad de color, o requerir rediseño del subsistema de vídeo con un FPGA más capaz.

---

## 7. CAVEATS

- El techo de reloj útil del iCE40 HX8K para lógica compleja con el toolchain abierto ronda **50 MHz** (calibración: ZipCPU documentado "at 50MHz on an iCE40 HX8k"; algunos diseños cierran solo a 40 MHz). Los controladores de SDRAM y el walker de MMU deben diseñarse para cerrar temporización a esa frecuencia. La ruta pura de E/S sí llega a 100 MHz (ejemplo SRAM de BlackIce-II), pero eso no aplica a la lógica de control alrededor.
- El **panel eDP de 14" a resolución nativa es el mayor riesgo técnico** del proyecto; el camino principal realista es VGA/DPI de baja resolución (640×480 / 800×600).
- El **soporte C nativo para 65816 es inmaduro** (cc65 y llvm-mos no lo soportan; llvm-65816 es experimental; WDCTools es la mejor opción C pero limitada); el desarrollo inicial será mayoritariamente en ensamblador.
- Ninguno de los proyectos SDRAM open-source de referencia en iCE40 documenta verbatim una frecuencia de SDRAM concreta en su README; la cifra de ~50 MHz es una estimación de ingeniería basada en el techo del fabric y en cifras de proyectos comparables, no una medida publicada. Debe validarse empíricamente en la etapa E4.
- Las **cifras de precio y disponibilidad** de componentes (W9825G6KH ~2,3-5,7 USD, AS4C16M16 ~5-7 USD, iCE40HX8K-TQ144, ANX6345) fluctúan y algunos aparecen intermitentemente sin stock; verificar antes de compra y prever segundas fuentes.
- El número de I/O útiles del HX8K en encapsulado **TQFP-144** es notablemente inferior a los 206 del die completo; el reparto de pines entre los buses paralelos (65816, SRAM, SDRAM, RGB, enlaces inter-FPGA) debe planificarse cuidadosamente y puede forzar multiplexado.