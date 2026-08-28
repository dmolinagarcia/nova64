# HANDOFF — DANI-65816

**Purpose of this file:** let a future session (or a different assistant instance) resume this project with zero re-explanation. Read this first, then the referenced documents.

**Date of handoff:** 2026-08-03 · **Project state:** Stages 0–1 designed on paper; nothing drawn in KiCad yet; no hardware ordered except a pending panel purchase.

---

## 1. What the project is

Dani is building **DANI-65816**, an original portable computer around a W65C816S CPU. It is a **self-directed learning project** — PCB design, FPGA gateware, computer architecture, OS design. Not compatible with, or derived from, any existing 65816 machine (no SNES, no Apple IIGS).

Two success milestones drive all sequencing:
- **"Apple II" milestone** — machine boots on its own to a prompt, on its own screen, with its own keyboard, running programs from SD. No PC attached.
- **"Amiga" milestone** — preemptive multitasking with a windowed, mouse-driven GUI.

Hard constraints, non-negotiable:
- **Everything hand-solderable.** No BGA. QFN with thermal pad is confirmed OK. This constraint has already reshaped the design once (ECP5 → iCE40) and must be respected in any future proposal.
- 3.3 V logic throughout.
- Fully open toolchain.

## 2. Working conventions

- **Conversation in Spanish. Documentation in English** (standing instruction from the first message). ⚠ Note: the design documents produced so far (`hoja-*.md`, the master HTML) ended up in **Spanish**. Worth confirming with Dani whether to keep them Spanish or convert — this handoff follows the original English instruction.
- Dani proposes architectural simplifications iteratively; the assistant's job is to evaluate them honestly — validate, refine, or push back with technical reasoning. Several of Dani's proposals (dropping the inter-FPGA link, pixel doubling, moving the softcore to a third FPGA) were genuinely good and were adopted. Some needed correction (30 Hz refresh, 6 bpp). **Do not just agree.**
- Strong preference for **small incremental steps with explicit, verifiable success criteria**. Every step gets a TEST.
- Documents are living: the plan and the decision log are updated as decisions land, not rewritten from scratch.
- Large HTML/SVG documents are built incrementally with heredoc-appended chunks, then validated with a Python `HTMLParser` balance check.
- Visual identity for HTML deliverables: **PCB silkscreen aesthetic** — dark green soldermask background, cream silkscreen text, gold/copper accents, monospace type, schematic-style title blocks.

## 3. Architecture as settled

**Compute**
- CPU: **W65C816S**, PLCC-44 in a socket (removable — key for debugging and for handing the bus to the softcore). Verified fine at 3.3 V; Fmax ~8 MHz there, initial PHI2 target 1–4 MHz.
- **3× iCE40HX4K in TQ-144.** The HX4K die is really an HX8K; the open toolchain (Yosys/nextpnr/IceStorm) exposes ~7,680 LUTs.
  - **FPGA-A** — MMU, TLB, cache controller, ABORT, IRQ controller, timer, SPI-SD, DMA. Bus arbiter. Generates PHI2 and RESB. **93/107 pins.**
  - **FPGA-B** — video and audio. **79/107 pins.**
  - **FPGA-C** — softcore 65816. Footprint on the board from day one, **unpopulated**, power jumpered off. **43/107 pins.**
- **RP2040** as embedded controller: power sequencing, FPGA configuration, USB-C console, microSD, HID hub, telemetry. **28/30 GPIO** (only fits thanks to an MCP23017 I2C expander for slow signals).

**The single shared bus (Amiga-style)** — this is the architectural centrepiece:
- CPU, FPGA-A, FPGA-B and FPGA-C all sit on one physical bus (D/BA[7:0], A[15:0], RWB, PHI2, RDY, control).
- There is **no dedicated inter-FPGA link** — it was removed deliberately. Only two control lines remain: VRAM_SEL (A→B) and BWAIT (B→A).
- CPU writes VRAM directly through a **64 KB aperture in bank $FE**, with a base register to window the 8 MB.
- **The MMU asserts VRAM_SEL only after translation and permission check** — this is what preserves memory protection. FPGA-B cannot decode banks itself, because the bus carries *virtual* addresses. Never lose this detail.
- Write contention handled by a **write FIFO** in FPGA-B; reads stretch RDY via BWAIT.
- One arbitration mechanism (BE=0 + RDY) serves both the FPGA-C softcore and FPGA-A DMA.
- **Safe state with no bitstream loaded:** RESB pulled down (CPU held in reset), everything else pulled up. The board can be powered with empty FPGAs and nothing fights the bus.

**Memory hierarchy (three tiers)**
- microSD (swap, page granularity) → PSRAM 2× APS6404L 16 MB (main store, 2 KB frames) → SRAM 512 KB (write-back cache, 256-byte lines).
- SRAM is **private to FPGA-A**, not on the CPU bus — it is the cache, not addressable memory.
- **Page table lives in external SRAM; TLB lives in BRAM; hardware page walker.** This was forced by the iCE40's ~16 KB BRAM and closed the "hardware walk vs software trap" question.
- Bank map: `$00` pinned (stack, direct page — 65816 interrupt behaviour makes faults there unrecoverable) · `$01–$FD` paged process space · `$FE` VRAM aperture · `$FF` privileged MMIO, self-protecting (only privileged code can write the permission table — the keystone of the protection scheme).

**Video**
- Panel: **ER-TFT101-1** candidate — 10.1" IPS 1024×600, **RGB TTL direct to FPGA-B**, FPC-50. Controller HX8282 (+HX8696).
- The ANX6345 eDP bridge and the 14" eDP panel were **eliminated from the project** when the panel went RGB-direct. Don't reintroduce them.
- Verified from the HX8282-A11 datasheet: 24-bit RGB (or 18-bit with the 2 LSBs grounded), 2.3–3.6 V logic so no level shifters, DE mode by default, DCLK 40.8–67.2 MHz (typ. 51.2), th 1344 / tv 635 → 60 Hz. **Data is latched on the falling DCLK edge** → gateware changes data on the rising edge. Built-in **BIST mode** generates test patterns with no external clock — used as bring-up step 3.6.0.
- **Pixel doubling is the bandwidth strategy**: the panel always scans 1024×600@60; the framebuffer is 512×300 @ 8 bpp with a palette (~9.2 MB/s, fits one QSPI). Text mode is generated on the fly from a charROM at native 1024×600 and costs no framebuffer bandwidth.
- **Resolution and bpp are gateware modes, not PCB decisions.** The only PCB decision was chip count: **2 PSRAM footprints on FPGA-B, 1 populated.**
- VGA R-2R for bring-up hangs off the top bits of the same RGB bus through jumpers — costs only 2 dedicated sync pins.

**Audio** — PCM5102A I2S DAC on FPGA-B (SCK grounded, internal PLL, so no master clock needed). Samples DMA'd from FPGA-B's PSRAM, which doubles as "chip RAM" — a deliberate Paula homage. Optional PAM8302 amp + speaker footprint reserved, unpopulated.

**Input** — no keyboard matrix on the FPGA (removed, freed ~16 pins). The **RP2040 is the HID hub**: USB-A host via PIO-USB (the native USB port is reserved for console/programming), plus GT911 capacitive touch over I2C. Events published to FPGA-A over I2C. **PS/2 was dropped** (5 V bus, not worth the level shifter).
- ⚠ The touch panel **must be ordered in the capacitive variant** — resistive would need 4 ADC channels the RP2040 doesn't have free.

**Power** — USB-C (CC1/CC2 with 5.1 kΩ pulldowns) → MCP73871 with power-path → **SYS node** → TPS63020 buck-boost to 3V3 always-on, plus PT4110 backlight boost and TPS61023 5 V boost for the USB host port. **Both boosts hang off SYS, not VBAT** — so everything works from USB with a dead or absent battery. From 3V3: a 1.2 V LDO for the FPGA cores (with RC-filtered VCCPLL per chip) and **VPP_2V5 via a BAT54 from 3V3** (avoids a 2.5 V rail entirely). MAX17048 fuel gauge. 0 Ω jumpers on every rail branch so each can be measured in isolation during bring-up.

## 4. Where things stand

| Stage | State |
|---|---|
| E0 Preparation | 0.5 (pin budget) **done**. 0.4 (panel) partial. 0.1/0.2/0.3/0.6 not started. |
| E1 Schematic | All six sheets **designed as documents**. Nothing drawn in KiCad. 1.7 review not started. |
| E2 Layout | Not started. |
| E3 Assembly/bring-up | Not started. |
| E4 Gateware | Not started. |
| E5–E6 OS | Not started. |
| E7 Physical | Deferred until the Apple II milestone is near. |

## 5. Open items

**The only external blocker:** the **ER-TFT101-1 module datasheet** — which straps the FPC-50 exposes, backlight voltage/current spec, whether the module needs anything beyond 3.3 V + backlight, GT911 connector. This blocks finalising sheet 1.5 and the PT4110 block in KiCad. The panel also needs to be *ordered*, in the capacitive variant.

Non-blocking:
- Choose HDL: Verilog vs VHDL (affects which reference softcore is usable — P65816 is VHDL).
- PTE format: frame bits + R/W/X + present/dirty/accessed. Decided during 4.4.
- Cross-assembler: 64tass vs WDC tools.
- Specific 1S battery (capacity/format) → sets charger R_PROG and runtime.
- At physical pinout time: verify GBIN-capable pins on the TQ-144, exact VPP_2V5 range in the Lattice datasheet, and consecutive GPIOs for PIO-USB.
- Optional: buy an iCEBreaker to practise the toolchain before the PCB exists.

## 6. Suggested next moves

1. Order the panel (capacitive variant) and obtain the module datasheet — the only external dependency.
2. Install the toolchain (0.1) and pick the HDL (0.2), so gateware can start in simulation in parallel with KiCad work.
3. Open KiCad: create the project and the verified component library (0.3), then draw sheet 1.1 following its document.

The natural shift ahead is **from architecture to craft** — from decisions to drawing. The most useful help at that point is reviewing each drawn sheet against its design document, not generating more architecture.

## 7. Files (all in `/mnt/user-data/outputs/`)

| File | Contents |
|---|---|
| `DANI-65816-documento-maestro.html` | Master document: 4 SVG diagrams, all stages with tests, pending items, decision log. The bird's-eye view. |
| `plan-implementacion-65816.md` | The living plan: stages E0–E7 with checkboxes, open questions, full decision log. |
| `hoja-pines-ice40.md` | Pin budget for all three FPGAs, with reserve levers. |
| `hoja-1-1-alimentacion.md` | Power tree, sequencing table, current budget. |
| `hoja-1-2-rp2040.md` | EC design, GPIO budget, expander rationale. |
| `hoja-1-3-config-fpga.md` | FPGA configuration over SPI. |
| `hoja-1-4-fpga-a-bus.md` | **The dense one** — net-by-net bus table, memories, clocking. |
| `hoja-1-5-fpga-b-video-audio.md` | Panel, VRAM, audio, VGA bring-up. |
| `hoja-1-6-conectores.md` | FPGA-C, connectors, miscellaneous. |

⚠ These files live in a container that resets between sessions. If they are gone in a future session, the master HTML and the plan are the two worth reconstructing first.

## 8. Reasoning worth preserving

Things that took real work to arrive at, and that would be expensive to rediscover:

- **iCE40 BRAM scarcity drove the memory architecture.** ~16 KB per chip means flat page tables cannot live in BRAM. Tables in SRAM + TLB in BRAM is not a preference, it's a consequence.
- **Page size and cache line size are independent.** 2 KB pages (translation/protection unit) with 256 B cache lines (burst-timing unit).
- **PHI2 stall vs ABORT is a hard boundary.** Stalling PHI2 only for bounded hardware fills; ABORTB for anything needing OS intervention. Confusing the two deadlocks or corrupts.
- **The panel's 60 Hz is physics, not preference.** TFT pixels are capacitors that need refreshing; polarity inversion becomes visible flicker at half rate. 30 Hz is out of spec. ~50 Hz is the realistic floor and buys only ~17 %.
- **6 bpp is a packing problem, not a bandwidth win** — it needs bitplanes (Amiga EHB style) to be clean, which trades gateware simplicity for drawing-code complexity.
- **Removing the inter-FPGA link removed a whole protocol.** One bus, one arbitration scheme, three uses. Less gateware to write and debug.
- **Both the FPGA and the RP2040 ran out of pins**, and both were rescued the same way — moving slow signals to a cheaper transport (RP2040 for the keyboard; MCP23017 for enables and status). Expect this pattern again.
