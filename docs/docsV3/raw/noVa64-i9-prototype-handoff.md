# noVa64 — i9 Prototype Board Handoff

**Single Colorlight i9 carrier: Helium + NEON in one FPGA, external CPU, SRAM, EC and VGA**

Document status: design proposal, pending hardware verification
Date: 2026-08-17
Supersedes: `noVa64-phase1-colorlight-i9-handoff.md` (see Section 1.2 for corrections)

---

## 1. Scope

### 1.1 What this document covers

A single development board built around one Colorlight i9 v7.2 module. The
ECP5 carries both Helium and NEON. Outside the FPGA, on the carrier PCB:

- W65C816S CPU in a PLCC-44 socket
- IS61WV102416 SRAM
- RP2040 embedded controller
- VGA output via resistor ladder DAC
- PCM5102A I2S DAC
- USB-C power and host connection

Explicitly **not** in scope: battery, charging circuit, fuel gauge
(MAX17048), power sequencing, eDP panel, ANX6345 bridge, GT911 touch
controller. Power comes directly from USB.

The eventual noVa64 target board — two separate FPGAs, 64 MB SDRAM each,
battery operation, eDP panel — remains the project goal. This board exists to
unblock software development while that design is completed. Section 4 exists
entirely to protect that transition.

### 1.2 Corrections to the superseded document

Three errors in the previous handoff:

1. **HDMI attributed to the i9 extension board.** Incorrect. The wuxx
   extension board sold with the i9 provides DAPLink JTAG, USB-CDC, six
   dual-PMOD headers and USB-C. The HDMI connector documented by Tom Verbeure
   belongs to the Muse Lab development board for the i5, a different product.
   Video output is now via VGA on the carrier PCB.

2. **"100+ usable I/O" used as an approximation.** The exact figure is now
   known: **106 FPGA balls** reach the SODIMM connector. See Section 5.1.

3. **A two-module variant was considered and is now rejected.** Two i9
   modules would give Helium and NEON separate SDRAM, but locks each at 8 MB
   with no expansion path. The single-module board reaches the same software
   milestones at lower cost and complexity.

---

## 2. Platform

### 2.1 Colorlight i9 v7.2

| Item | Value |
|---|---|
| FPGA | Lattice LFE5U-45F-6BG381C |
| Logic | 44,000 LUT4 |
| Block RAM (EBR) | 108 × 18 kbit ≈ 243 KB |
| SDRAM | M12L64322A, 8 MB, 512K × 32 bit × 4 banks |
| SPI flash | W25Q64JVSIQ, 8 MB |
| Ethernet | 2 × Broadcom B50612D (unused; hold in reset — see Section 8) |
| Clock | 25 MHz on ball P3 |
| Interface | DDR2 SODIMM 200-pin edge connector |

### 2.2 Toolchain

Yosys + nextpnr-ecp5 + prjtrellis; openFPGALoader or ecpdap for programming.
No proprietary tools.

---

## 3. Resource budget

### 3.1 Logic

| Block | LUT4 estimate |
|---|---|
| Helium: MMU, TLB, cache controller, arbiter, peripherals | 3,000 – 4,000 |
| NEON: scanout, blitter, compositor, audio DMA | 3,300 – 3,900 |
| Shared SDRAM controller and QoS arbiter | 1,200 – 1,800 |
| VGA timing generator and palette lookup | 150 – 250 |
| Debug agent, trace buffer, instrumentation | 800 – 1,200 |
| Clock domain crossing, glue | 400 – 600 |
| **Total** | **8,850 – 11,750** |

**20 – 27 % of 44,000 LUT4.** nextpnr closes timing comfortably and compile
times stay short.

The unused capacity is the instrumentation budget: wide trace buffers, bus
transaction counters, cache hit/miss histograms, an embedded logic analyzer
on the 65816 bus. None of this fits in the eventual target silicon. Build it
now.

### 3.2 EBR partitioning

243 KB available.

| Consumer | Size | Notes |
|---|---|---|
| Text Mode 0 character + attribute buffer | 8 KB | No SDRAM dependency |
| Text Mode 0 font ROM | 4 KB | 256 glyphs × 8 × 16 |
| Palette RAM | 1 KB | 256 entries × 18 bit |
| Scanout FIFO | 2 KB | 512 words × 32 bit |
| Hardware cursor bitmap | 4 KB | 64 × 64 × 8 bpp |
| Blitter line buffers | 8 KB | 4 channels |
| Cache tag array | 2 KB | Sized for 128 KB cache, 256 B lines |
| TLB (ASID-tagged) | 1 KB | |
| Page walker cache | 2 KB | |
| Debug trace buffer | 16 KB | Generously sized; this board can afford it |
| Command FIFOs, misc | 8 KB | |
| **Total committed** | **56 KB** | |
| **Free** | **~187 KB** | |

**Design consequence: the cache controller must be parameterized by size from
the first line of code.** The target board has 1 MB of SRAM cache; this board
has whatever the carrier PCB carries. Cache size, line count and
associativity are synthesis parameters. A hardcoded 1 MB anywhere is a defect.

---

## 4. Partitioning discipline

**This is the most important section in the document.**

This board merges two devices that the target design separates. Every
shortcut taken because Helium and NEON happen to share a die is a rewrite
later.

### Rule 1 — Two top-level modules, always

`helium.v` and `neon.v` are instantiated side by side in a top level whose
only job is to wire them together and to the pads. Neither module references
a signal inside the other. There is no shared internals file.

### Rule 2 — All communication through the declared interface

The only connection between the two modules is the interface in Section 6. A
new signal is added to the interface definition and to the link budget in the
same commit. A signal added to the interface is a cost; a signal smuggled
around it is a defect.

### Rule 3 — Separate clock domains from day one

Both modules could run from a single PLL output. **Do not.** Derive two
clocks from separate PLL outputs and implement proper CDC on every interface
signal: async FIFOs for data, two-flop synchronizers plus handshake for
control.

This costs a little latency and logic now. It buys the certainty that the
target board does not surface metastability bugs after the PCB exists.

### Rule 4 — The memory port is an abstraction

NEON's memory accesses go through a memory port on NEON's boundary. Here it
routes to the shared SDRAM arbiter; on the target board it routes to NEON's
own controller. **NEON contains no knowledge of which.** No arbitration logic
inside NEON, no awareness that Helium exists as a competing requester.

### Rule 5 — Palette entries are 18-bit, always

The VGA DAC on this board is 6 bits per channel, which is the same depth the
target panel accepts over parallel RGB666. Palette entries are 18 bits in
both cases. **The palette format never changes.** See Section 7.2.

### Rule 6 — The aperture semantics are the specification

The write-only aperture at bank `$FE`, with a slow diagnostic read path, is
preserved exactly, even though a single die could trivially support fast
bidirectional access. Software written against fast reads breaks on the
target board.

---

## 5. Pin budget

### 5.1 What the SODIMM exposes

Counted from the wuxx i9 v7.2 pinout, excluding GND, 5 V, NC and the Ethernet
pairs: **106 FPGA balls**.

| SODIMM range | Pins | Character |
|---|---|---|
| 41 – 54 | 10 | Scattered, NC interleaved; ball L2 shared with LED D2 |
| 57 – 104 | **48** | Dense, contiguous |
| 109 – 156 | **48** | Dense, contiguous |

Verified: none of these 106 collide with the SDRAM (52 balls), the Ethernet
PHYs (~27), the SPI flash (4) or the 25 MHz clock input on P3.

The SODIMM carries **raw FPGA balls** — the pinout table lists literal ball
designators. There are no buffers on the module itself.

### 5.2 Allocation

| Block | Contents | Pins | Free |
|---|---|---|---|
| **A** (57–104) | 65816 bus | 38 | 10 |
| **B** (109–156) | SRAM 14, VGA 20, I2S 3 | 37 | 11 |
| **C** (41–54) | RP2040 EC link | 8 | 2 |
| | **Total** | **83 / 106** | **23** |

### 5.3 Detail

**65816 bus — 38 pins, Block A**

| Group | Signals | Count |
|---|---|---|
| Address | A0 – A15 | 16 |
| Data | D0 – D7 (multiplexed with bank address) | 8 |
| FPGA outputs | PHI2, RESB, ABORTB, IRQB, NMIB, BE | 6 |
| FPGA inputs | RWB, VPA, VDA, VPB, MLB, E, MX | 7 |
| Bidirectional | RDY | 1 |

The bus occupies one contiguous block. Do not split it across SODIMM zones:
synchronous bus signals distributed across distant connector regions and
different FPGA banks is where skew problems appear.

The 10 spare pins in Block A are reserved as a probe header adjacent to the
CPU bus — the place they will actually be wanted.

**SRAM — 14 pins, Block B**

A11 – A19 (9) plus CEB, OEB, WEB, UB, LB (5). A0 – A10 and D0 – D7 are not
counted: they are shared nets with the CPU bus, exploiting the 2 KB page
offset, exactly as specified for the target board.

**Consequence: the FPGA pins carrying A0 – A10 and D0 – D7 must be
bidirectional.** During CPU cycles the FPGA observes them. During a
hardware cache fill the FPGA drives them, with BE asserted low to tri-state
the CPU. This is consistent with the architecture but must be explicit before
ball assignment.

**VGA — 20 pins, Block B**

18 colour bits (RGB666) plus HSYNC and VSYNC. See Section 7.

**I2S — 3 pins, Block B**

BCLK, LRCK, DATA. The PCM5102A synthesizes its own internal clock and does
not require MCLK, saving a pin.

**RP2040 link — 8 pins, Block C**

SCK, MOSI, MISO, DBG_CSN, IRQ, READY, DEBUG_ENABLE strap, FPGA_READY.

SPI at a few MHz is indifferent to the scattering and skew of the 41–54 zone.
Any parallel bus placed there would be a mistake; this is the correct
occupant.

---

## 6. Helium ↔ NEON interface

A superset of the eventual inter-FPGA link. Anything crossing it here must be
carriable by that link on the target board.

### 6.1 Command channel (Helium → NEON)

Posted writes into the `$FE` aperture.

| Signal | Width | Direction |
|---|---|---|
| `cmd_addr` | 16 | H → N |
| `cmd_data` | 8 | H → N |
| `cmd_valid` | 1 | H → N |
| `cmd_ready` | 1 | N → H |

Helium does not stall waiting for completion. Backpressure asserts only when
NEON's command FIFO is full — a condition software must tolerate.

### 6.2 Diagnostic read channel

Deliberately slow. For debug and NEON status registers, never for pixel data
on a performance path.

| Signal | Width | Direction |
|---|---|---|
| `peek_addr` | 16 | H → N |
| `peek_req` | 1 | H → N |
| `peek_data` | 8 | N → H |
| `peek_valid` | 1 | N → H |

A per-transaction timeout in Helium prevents a hung NEON from stalling the
CPU, mirroring the Debug Agent watchdog.

### 6.3 Event channel (NEON → Helium)

`evt_vblank`, `evt_blit_done`, `evt_audio_half`, `evt_error` — single-cycle
pulses, synchronized into Helium's domain. Helium converts these into CPU
interrupts under its own mask registers.

### 6.4 Memory port (NEON boundary)

Not part of the inter-FPGA link. NEON's interface to *its* memory, wherever
that lives.

| Signal | Width |
|---|---|
| `mem_addr` | 23 |
| `mem_wdata` | 32 |
| `mem_rdata` | 32 |
| `mem_we` | 1 |
| `mem_be` | 4 |
| `mem_req` | 1 |
| `mem_ack` | 1 |
| `mem_prio` | 2 |

`mem_prio` encodes the QoS class: 3 = scanout, 2 = audio, 1 = blitter,
0 = bulk. Here it drives the shared arbiter; on the target board it drives
NEON's local arbitration. Same signal, same meaning, different consumer.

### 6.5 Link cost for the target board

Naively, 56 signals cross this interface. That exceeds the 13 – 22 spare pins
in the Helium TQ144 budget.

A multiplexed encoding brings it to 32: shared 16-bit address, shared 8-bit
bidirectional data, two command strobes, two peek strobes, four event lines.
The cost is one extra cycle on the peek path, which is diagnostic and already
slow by design; the posted command channel loses nothing.

**This is a useful finding produced by this board.** The link cost is now a
measured quantity rather than an estimate, and it feeds directly into the
package decision for the target design.

---

## 7. Video

### 7.1 VGA via resistor ladder

The ECP5 has no DAC. Conversion is done with R-2R ladders directly on the
GPIO, terminated against the monitor's 75 Ω inputs.

- 6 bits per channel × 3 channels = 18 pins
- HSYNC, VSYNC = 2 pins
- DE-15 connector
- 1 % resistors; 6-bit ladders need the tolerance

### 7.2 Why 6 bits per channel

The framebuffer is 8 bpp indexed. Colour depth is set by the palette entry
width, not the DAC. The target board's parallel RGB path to the ANX6345 is
RGB666 — 18 bits per palette entry.

Matching that here means **the palette format is identical in both designs**.
No truncation logic, no format change, no software difference. Rule 5 exists
for this reason.

RGB444 would save six pins and there are pins to spare, but it would put a
format conversion between this board and the target for no benefit.

### 7.3 Timing modes

1024 × 600 is not a standard VGA mode, though most monitors accept it via
CVT. For early bring-up use **640 × 480 at 25.175 MHz** — the most tolerant
mode in existence, and the safest way to prove the scanout path.

Move to 1024 × 600 at approximately 50 MHz once the timing generator is
trusted. The compositor and window server should be developed at the target
resolution.

### 7.4 Text Mode 0

Renders from EBR alone with no SDRAM dependency, exactly as specified for the
target board. This is the E1 exit criterion and it guarantees a working
console before the memory controller is trusted.

---

## 8. Power

USB only. No battery, no charger, no fuel gauge, no sequencing.

### 8.1 Budget

| Consumer | Estimate at 5 V |
|---|---|
| i9 module (FPGA core, I/O, SDRAM, regulators) | 400 – 600 mA |
| Ethernet PHYs, if left running | 200 – 400 mA |
| W65C816S at 1 – 4 MHz | ~10 mA |
| RP2040 | ~50 mA |
| PCM5102A and analogue | ~30 mA |
| **Total, PHYs running** | **~700 mA – 1.1 A** |
| **Total, PHYs held in reset** | **~500 – 700 mA** |

### 8.2 Hold the PHYs in reset

Both B50612D devices share a reset line on ball **P4**. The project never
uses Ethernet. Assert reset at configuration and leave it asserted: it
reclaims a few hundred milliamps and removes a source of switching noise
next to the CPU bus.

### 8.3 Connector

USB-C with 5 V / 3 A capability, or a dedicated 5 V barrel input. A legacy
USB-A port at 500 mA is marginal and may not start the board.

**Measure the actual module consumption before finalizing the schematic.**
These are estimates.

### 8.4 Sequencing

The i9 module manages its own rails from the SODIMM 5 V. No external
sequencer is needed. This is a genuine simplification over the target board,
and it means the RP2040's power-sequencing role is not exercised here — a
gap to note, not to close.

---

## 9. Embedded controller

The RP2040 remains external and retains most of its specified role.

Retained:

- Debug Agent SPI master, exposed over USB-CDC
- BIOS and kernel image preload from microSD into SRAM
- USB HID host via PIO-USB
- Release of the 65816 from reset once the system is initialized

Added for this board:

- **JTAG programming of the ECP5.** If the carrier PCB hosts the SODIMM
  socket directly rather than sitting on the extension board, the DAPLink
  programmer is not present. The RP2040 can drive ECP5 JTAG over four GPIO,
  which is a natural extension of its FPGA-configuration role.

Not exercised here:

- Power sequencing (Section 8.4)
- ANX6345 initialization
- Battery management

The Debug Agent's two access axes — physical vs. virtual, internal vs.
external bus cycle — partially collapse on a single die, since NEON's memory
is reachable through the shared arbiter. **Implement both axes anyway.** The
external-bus-cycle path must exist and be tested, or the target board will
discover it missing at the worst time.

---

## 10. Physical construction

### Stage A — extension board plus wire-wrap

65816 in a PLCC-44 socket on perfboard, SRAM adjacent, ribbon to the
extension board PMOD headers. **Run PHI2 at 1 – 2 MHz.** Flying leads will
not meet the 65816's data setup requirement before the PHI2 falling edge at
higher frequencies, and chasing intermittent faults on a wire-wrapped bus is
a poor use of time.

Purpose: prove the concept and reach E2 without waiting for a PCB.

**Verification required before this stage:** the extension board's PMOD
headers must be direct, unbuffered, bidirectional FPGA I/O. Several Colorlight
products route GPIO through unidirectional 74HC245 buffers. The module itself
does not (Section 5.1), so this concerns the extension board only, and the
described layout — I/O in groups of eight, each with its own 3V3 and GND —
suggests direct connection. Confirm with a continuity meter on arrival.

### Stage B — carrier PCB

The real deliverable. A four-layer board hosting:

- DDR2 SODIMM 200-pin socket
- PLCC-44 socket and W65C816S
- IS61WV102416 SRAM
- RP2040 with W25Q16 flash and microSD slot
- R-2R ladders and DE-15 VGA connector
- PCM5102A and 3.5 mm output
- USB-C
- Probe header on the 10 spare Block A pins

Four layers rather than two: the CPU bus needs a ground plane, and the R-2R
ladder outputs need a clean return path.

Hosting the SODIMM socket directly bypasses the extension board buffering
question entirely and gives access to all 106 pins with controlled impedance
on the CPU bus.

**Capture this in KiCad using the same symbols and net names intended for the
target board.** The CPU-to-SRAM-to-FPGA section is the most timing-sensitive
part of the eventual design, and this is the opportunity to validate it where
a mistake costs a cheap respin.

---

## 11. Exit criteria

### E0 — Platform alive
- Bitstream loads via openFPGALoader
- Blinky confirms clocking and PLL lock
- USB-CDC console echoes
- SDRAM memory test passes across all 8 MB: walking ones, address-in-address
- Ethernet PHYs confirmed held in reset; current draw measured

### E1 — Console before memory
- Text Mode 0 renders a character grid over VGA at 640 × 480, sourced
  entirely from EBR
- Console accepts USB-CDC input and displays it
- No SDRAM access anywhere in the path
- *Pass condition: a working terminal exists that does not depend on the
  memory controller*

### E2 — CPU attached
- 65816 in socket, released from reset, free-running NOP test executes
- Bus cycles captured in the trace buffer, readable over the debug channel
- VPA/VDA decoding confirmed against expected instruction fetch patterns
- Native mode entry verified

### E3 — Translation active
- Helium MMU and TLB operational, CPU executes through translation
- Cache controller running against external SRAM, hit/miss counters plausible
- PHI2 stalling exercised on cache fill, bounded and measured
- ABORTB raised on an unmapped page, handled, instruction resumed correctly
- Context switch between two ASIDs with correct CTX_SET_PTBASE /
  CTX_SET_ASID ordering
- *Pass condition: the virtual memory system is real, not simulated*

### E4 — Graphics
- Framebuffer scanout from SDRAM, stable at 1024 × 600
- Palette lookup operational, 18-bit entries
- Blitter executes all four channels; minterm logic verified against a
  reference implementation
- Hardware cursor overlay tracks without involving the composite path
- Arbiter wait-time histograms show no scanout underrun under blitter load

### E5 — System
- Audio DMA to PCM5102A, continuous playback without underrun
- Compositor Model C with damage-limited recomposition
- Kernel boots to a prompt — **the Apple II milestone**

Beyond E5, work transitions to the G-series GUI roadmap, running entirely on
this board while the target hardware design proceeds in parallel.

---

## 12. Memory

### 12.1 SDRAM allocation

8 MB, shared between Helium and NEON through one controller.

| Allocation | Size |
|---|---|
| Framebuffer, front (1024 × 600 × 8 bpp) | 600 KB |
| Framebuffer, back | 600 KB |
| Window backing stores | 2 MB |
| Physical page pool | 4 MB |
| Audio DMA buffers | 128 KB |
| Reserve | ~700 KB |

Sufficient for the entire G0 – G8 roadmap. The 16 MB per-process virtual
address space is exercised against a small physical pool, which is
*preferable* during development: the paging and eviction paths run constantly
rather than lying dormant behind abundant memory.

### 12.2 Bandwidth

At 100 MHz a ×32 SDR interface peaks at 400 MB/s; realistic sustained
throughput after refresh, precharge and bank-conflict overhead is
250 – 300 MB/s.

| Requester | Demand |
|---|---|
| Scanout (1024 × 600 × 8 bpp @ 60 Hz) | 37 MB/s |
| Blitter, worst case | 60 – 100 MB/s |
| Helium cache fills and writebacks | 25 MB/s |
| Audio DMA | negligible |
| **Total** | **122 – 162 MB/s** |

Roughly 50 – 65 % utilization.

### 12.3 Arbitration

Fixed priority with a starvation guard:

1. Scanout — absolute priority, must never underrun
2. Audio DMA — hard real-time, tiny
3. Blitter — high throughput, latency-tolerant
4. Helium cache fills — latency-sensitive, guarded by PHI2 stalling
5. Bulk transfers

The starvation guard promotes any requester waiting beyond a programmable
threshold. Instrument it: per-requester wait-time histograms in EBR, readable
over the debug channel.

---

## 13. Open items

### Blocking, before Stage A
- Extension board PMOD buffering (Section 10). Moot if Stage A is skipped in
  favour of going straight to the carrier PCB.

### Blocking, before gateware
- Ball assignment: map the 83 signals of Section 5 onto specific i9 balls,
  respecting ECP5 bank grouping and keeping the CPU bus within as few banks
  as practical
- Clock plan: PLL outputs for Helium domain, NEON domain and pixel clock;
  confirm ECP5 PLL count is sufficient
- Cache controller parameterization: define synthesis parameters before
  writing the first module

### Blocking, before carrier PCB
- Measured current draw of the i9 module, PHYs running and held in reset
- R-2R ladder values and termination verified against 75 Ω monitor input

### Carried forward, unaffected by this board
- Panel selection
- BGA assembly house decision, now informed by the Section 6.5 link cost
- Calypsi calling convention and direct-page pseudo-register audit
- Virtual bank `$00` / pinned region interaction across context switches
- ASID width decision

---

## 14. What must not leak to the target board

Review at every major merge, not only at transition. Each item is a
convenience here that becomes a defect there.

| Property of this board | Why it must not survive |
|---|---|
| Shared SDRAM controller | Target has two independent memories |
| Single clock domain | Target has genuinely separate clocks |
| Fast bidirectional access to NEON | Target aperture is write-only |
| Hardcoded cache size | Cache size is a parameter |
| VGA timing assumptions | Target drives a fixed-resolution panel |
| Any Helium↔NEON signal outside Section 6 | Target link is pin-limited |
| Bandwidth figures from ×32 shared memory | Target memory topology differs |
| Debug access to NEON via the internal path only | Target needs the external bus cycle path |
| Absence of power sequencing | Target sequences rails before FPGA config |

---

## 15. Summary

One commercial module, one four-layer carrier PCB, USB power. The board
reaches the Apple II milestone on hardware that can exist within weeks, and
hands the target schematic a validated gateware reference instead of a
specification document.

The plan succeeds or fails on Section 4. The merged FPGA is not the design —
it is temporary packaging, and the gateware must be written as though the two
devices were already separate.
