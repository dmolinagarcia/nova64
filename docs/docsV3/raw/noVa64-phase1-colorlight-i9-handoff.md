# noVa64 — Phase 1 Prototype Handoff

**Colorlight i9 development platform: Helium + NEON in a single FPGA**

Document status: design proposal, pending hardware verification
Date: 2026-08-17

---

## 0. Purpose of this document

This handoff defines a two-phase development strategy for the noVa64 project.

**Phase 1** places both FPGA-A (Helium) and FPGA-B (NEON) inside a single
Lattice ECP5 LFE5U-45F on a commercial Colorlight i9 v7.2 module. The
W65C816S CPU, the external SRAM, and the RP2040 embedded controller remain
outside the FPGA and connect through the module's SODIMM I/O.

**Phase 2** is the full custom board with Helium and NEON as two separate
physical devices, as previously specified.

The document exists to answer one question: *what must be true of the Phase 1
gateware so that Phase 2 is a re-partitioning rather than a rewrite?*

Everything in Section 4 (Partitioning Discipline) and Section 5 (Interface
Contract) is written to serve that single goal. If those two sections are
respected, Phase 2 costs a serializer and a pin assignment. If they are
violated, Phase 2 costs a rewrite of the video and memory paths.

---

## 1. Why this phase exists

The current project state has schematic capture blocked on several unresolved
items (panel selection, NEON pin budget, DBG_CSN allocation, BE routing).
Meanwhile the entire software roadmap — kernel, scheduler, MMU exercise,
window server, toolkit, stages G0 through G8 — is blocked behind hardware
bring-up stages E0 through E5.

Phase 1 breaks that serialization. The software roadmap proceeds on a
platform that costs roughly EUR 60 and requires no PCB fabrication, while the
Phase 2 hardware questions are resolved in parallel.

Secondary benefits:

- The MMU, cache controller, blitter, and compositor get exercised against
  real software long before they are committed to a PCB.
- Bugs found in Phase 1 are fixed with a bitstream rebuild, not a board spin.
- The Phase 2 schematic is captured with working gateware as its reference,
  not with a specification document.

---

## 2. Platform facts

### 2.1 Colorlight i9 v7.2 module

| Item | Value |
|---|---|
| FPGA | Lattice LFE5U-45F-6BG381C |
| Logic capacity | 44,000 LUT4 |
| Block RAM (EBR) | 108 × 18 kbit ≈ 243 KB |
| DRAM | M12L64322A, 8 MB SDR SDRAM, 512K × 32 bit × 4 banks |
| Configuration flash | W25Q64JVSIQ, 8 MB SPI |
| Ethernet | 2 × Broadcom B50612D PHY (unused in this project) |
| Interface | DDR2 SODIMM edge connector |

The i5 v7.0 module shares the same SODIMM pinout and can be swapped in, but
carries a 25F (24,000 LUT) and only 56 EBR blocks. See Section 3.2 for why
the 45F is required rather than merely preferred.

### 2.2 Extension board

- On-board DAPLink debugger: JTAG programming plus USB-CDC serial
- 6 × dual-PMOD headers, 100+ usable I/O
- USB Type-C
- HDMI connector

The USB-CDC channel provides the development console with no additional
hardware.

### 2.3 Toolchain

Unchanged from project baseline: Yosys + nextpnr-ecp5 + prjtrellis,
openFPGALoader or ecpdap for programming. No proprietary tools. This is the
reason the i9plus (Xilinx Artix-7, requires Vivado) is excluded from
consideration.

---

## 3. Resource budget

### 3.1 Logic

| Block | LUT4 estimate |
|---|---|
| Helium: MMU, TLB, cache controller, arbiter, peripherals | 3,000 – 4,000 |
| NEON: scanout, blitter, compositor, audio DMA | 3,300 – 3,900 |
| Shared SDRAM controller and QoS arbiter | 1,200 – 1,800 |
| TMDS encoder and serializer (Phase 1 only) | 400 – 600 |
| Debug agent, trace buffer, instrumentation | 800 – 1,200 |
| Clock domain crossing, glue | 400 – 600 |
| **Total** | **9,100 – 12,100** |

Against 44,000 LUT4 this is **21 – 28 % occupancy**. nextpnr closes timing
reliably in this range and compile times stay short enough for a tight
edit-build-test loop.

The headroom is not waste. It is the budget for instrumentation that will not
fit in the Phase 2 silicon: wide trace buffers, bus transaction counters,
cache hit/miss histograms, an embedded logic analyzer on the 65816 bus. Build
that instrumentation now, because Phase 1 is the only time it is free.

### 3.2 EBR — the actual constraint

Logic capacity is comfortable. Block RAM is not, and it is the reason the 25F
is rejected.

Proposed partitioning of the 243 KB available:

| Consumer | Size | Notes |
|---|---|---|
| Text Mode 0 character + attribute buffer | 8 KB | No SDRAM dependency, required at E1 |
| Text Mode 0 font ROM | 4 KB | 256 glyphs × 8 × 16 |
| Scanout FIFO | 2 KB | 512 words × 32 bit |
| Hardware cursor bitmap | 4 KB | 64 × 64 × 8 bpp |
| Blitter line buffers | 8 KB | 4 channels |
| Cache tag array | 2 KB | Sized for 128 KB cache, 256 B lines |
| TLB (ASID-tagged) | 1 KB | |
| Page walker cache | 2 KB | |
| Debug trace buffer | 16 KB | Phase 1 luxury, sized generously |
| Command FIFOs, misc | 8 KB | |
| **Subtotal** | **55 KB** | |
| **Available for cache data emulation** | **~185 KB** | If external SRAM is absent |

Two observations follow.

**First: the 1 MB SRAM cache does not fit in EBR on any ECP5 in this family.**
Phase 1 must either attach the external SRAM (preferred, and it is on the
critical path anyway) or run with a reduced cache.

**Second, and this is a design consequence, not a workaround: the cache
controller must be parameterized by size from the first line of code.** Cache
size, associativity, and line count become synthesis parameters. Hardcoding
1 MB anywhere in the design is a Phase 2 defect introduced in Phase 1.

The 25F's 126 KB of EBR would force a choice between the debug trace buffer
and a usable cache. That is the disqualifying argument, not the LUT count.

---

## 4. Partitioning discipline

**This is the most important section in the document.**

Phase 1 merges two devices into one die. The temptation — and it is a strong
one, because it makes everything easier and faster in the short term — is to
let Helium and NEON share signals freely because they happen to be adjacent
in the netlist. Every such shortcut is a Phase 2 rewrite.

### Rule 1 — Two top-level modules, always

`helium.v` and `neon.v` are instantiated side by side in a Phase 1 top level
whose only job is to wire them together and to the pads. Neither module ever
references a signal inside the other. There is no `helium_neon_shared.v`.

### Rule 2 — All communication through the declared interface

The only connection between the two modules is the interface defined in
Section 5. If a new signal is needed, it is added to the interface definition
and to the Phase 2 link budget in the same commit. A signal added to the
interface is a cost; a signal smuggled around it is a defect.

### Rule 3 — Separate clock domains from day one

Phase 1 could run both modules from a single PLL output. **Do not.** Derive
two clocks from separate PLL outputs and implement proper CDC on every
interface signal: async FIFOs for data, two-flop synchronizers plus
handshake for control.

This costs a small amount of latency and logic in Phase 1. It buys the
certainty that Phase 2 does not surface a class of metastability bugs at the
worst possible moment — after the PCB exists.

### Rule 4 — The memory port is an abstraction, not a wire

NEON's memory accesses go through a memory port interface on NEON's boundary.
In Phase 1 that port is routed to the shared SDRAM arbiter. In Phase 2 it is
routed to NEON's own dedicated SDRAM controller. **NEON contains no knowledge
of which.** No signal named `shared_`, no arbitration logic inside NEON, no
awareness that Helium exists as a competing requester.

### Rule 5 — RGB is canonical, TMDS is a wrapper

NEON's video output is parallel RGB plus PCLK, HSYNC, VSYNC, DE — exactly the
signals the ANX6345 will consume in Phase 2. The TMDS encoder used to drive
the extension board's HDMI connector is a separate module that consumes those
signals from outside NEON.

Phase 2 deletes the wrapper and routes the same signals to pads. NEON does
not change.

### Rule 6 — The aperture semantics are the specification

The write-only aperture at bank `$FE`, with a slow diagnostic read path, is
preserved exactly even though a single die could trivially support fast
bidirectional access. The aperture model is not a workaround for the
inter-FPGA link; it is the architecture. Software written against fast reads
in Phase 1 breaks in Phase 2.

---

## 5. Helium ↔ NEON interface contract

This interface is a **superset of the Phase 2 inter-FPGA link**. Anything
crossing it in Phase 1 must be carriable by that link in Phase 2, either as
parallel signals or through a serializer.

### 5.1 Command channel (Helium → NEON)

Posted writes into the `$FE` aperture.

| Signal | Width | Direction | Description |
|---|---|---|---|
| `cmd_addr` | 16 | H → N | Offset within the aperture |
| `cmd_data` | 8 | H → N | Write data |
| `cmd_valid` | 1 | H → N | Transaction strobe |
| `cmd_ready` | 1 | N → H | Backpressure |

Writes are posted: Helium does not stall waiting for completion. Backpressure
is asserted only when NEON's command FIFO is full, which is a condition the
software must be able to tolerate.

### 5.2 Diagnostic read channel (Helium → NEON → Helium)

Deliberately slow. Intended for debug and for reading NEON status registers,
never for pixel data on a performance path.

| Signal | Width | Direction | Description |
|---|---|---|---|
| `peek_addr` | 16 | H → N | Register or aperture offset |
| `peek_req` | 1 | H → N | Request strobe |
| `peek_data` | 8 | N → H | Return data |
| `peek_valid` | 1 | N → H | Return strobe |

A per-transaction timeout in Helium prevents a hung NEON from stalling the
CPU. This mirrors the watchdog already specified for the Debug Agent.

### 5.3 Event channel (NEON → Helium)

| Signal | Width | Description |
|---|---|---|
| `evt_vblank` | 1 | Vertical blanking interval start |
| `evt_blit_done` | 1 | Blitter command queue drained |
| `evt_audio_half` | 1 | Audio DMA buffer half-empty |
| `evt_error` | 1 | NEON error condition, details via peek |

Pulses, synchronized into Helium's clock domain. Helium converts these into
CPU interrupts according to its own mask registers.

### 5.4 Memory port (NEON boundary)

Not part of the inter-FPGA link. This is NEON's interface to *its* memory,
wherever that memory lives.

| Signal | Width | Description |
|---|---|---|
| `mem_addr` | 23 | Word address |
| `mem_wdata` | 32 | Write data |
| `mem_rdata` | 32 | Read data |
| `mem_we` | 1 | Write enable |
| `mem_be` | 4 | Byte enables |
| `mem_req` | 1 | Request |
| `mem_ack` | 1 | Acknowledge |
| `mem_prio` | 2 | QoS class: 3 = scanout, 2 = audio, 1 = blitter, 0 = bulk |

The `mem_prio` field exists in both phases. In Phase 1 it drives the shared
arbiter. In Phase 2 it drives NEON's local arbitration between scanout,
blitter, and audio DMA. Same signal, same meaning, different consumer.

### 5.5 Phase 2 link cost

Summing the crossing signals: 16 + 8 + 1 + 1 command, 16 + 1 + 8 + 1 peek,
4 event = 56 signals. This exceeds the 13 – 22 spare pins previously
identified in the Helium TQ144 budget, which confirms that Phase 2 needs
either a multiplexed/serialized link or the larger package that the BGA
assembly decision would unlock.

**This is a useful finding.** Phase 1 makes the inter-FPGA link cost concrete
and measurable before the Phase 2 package decision is locked.

---

## 6. Pin budget

### 6.1 Allocation

| Group | Signals | Count |
|---|---|---|
| **65816 address** | A0 – A15 | 16 |
| **65816 data** | D0 – D7 (multiplexed with bank address) | 8 |
| **65816 control out** | PHI2, RESB, ABORTB, IRQB, NMIB, BE | 6 |
| **65816 control in** | RWB, VPA, VDA, VPB, MLB, E, MX | 7 |
| **65816 bidirectional** | RDY | 1 |
| **SRAM address** | A11 – A19 (A0 – A10 shared with CPU bus) | 9 |
| **SRAM control** | CEB, OEB, WEB, UB, LB | 5 |
| **EC link (RP2040)** | SCK, MOSI, MISO, DBG_CSN, IRQ, READY | 6 |
| **EC straps** | DEBUG_ENABLE, FPGA_READY | 2 |
| **Audio** | I2S BCLK, LRCK, DATA, MCLK | 4 |
| **Video** | 4 × TMDS differential pairs | 8 |
| **Total** | | **72** |

Against 100+ usable I/O on the extension board. Comfortable margin for a
handful of debug pins and a logic analyzer header.

### 6.2 Level compatibility

The W65C816S operates natively at 3.3 V. The IS61WV102416 operates at 3.3 V.
No level translation is required anywhere in the Phase 1 setup. This is a
meaningful simplification for a wire-wrapped prototype.

### 6.3 Blocking verification item

**The extension board's PMOD headers must be verified as direct, unbuffered,
bidirectional FPGA I/O.** Several Colorlight products (notably the 5A-75B)
route GPIO through unidirectional 74HC245 buffers. If the i9 extension board
does the same, the 65816 data bus cannot be connected and the entire Phase 1
plan requires a custom breakout.

This must be resolved before purchase. Sources: the wuxx
`Colorlight-FPGA-Projects` repository (extension board schematic) or direct
continuity measurement on the board.

---

## 7. Shared SDRAM: bandwidth and arbitration

### 7.1 Capacity

8 MB, 512K × 32 × 4 banks.

| Allocation | Size |
|---|---|
| Framebuffer, front (1024 × 600 × 8 bpp) | 600 KB |
| Framebuffer, back | 600 KB |
| Window backing stores | 2 MB |
| Physical page pool (kernel + processes) | 4 MB |
| Audio DMA buffers | 128 KB |
| Reserve | ~700 KB |

Sufficient for the entire G0 – G8 roadmap. The 16 MB per-process virtual
address space is exercised against a small physical pool, which is
*preferable* for development: it forces the paging and eviction paths to run
constantly rather than lying dormant behind abundant memory.

### 7.2 Bandwidth

At 100 MHz, a ×32 SDR interface has a theoretical peak of 400 MB/s. Realistic
sustained throughput after refresh, precharge, and bank-conflict overhead is
250 – 300 MB/s.

| Requester | Demand |
|---|---|
| Scanout (1024 × 600 × 8 bpp @ 60 Hz) | 37 MB/s |
| Blitter (worst case) | 60 – 100 MB/s |
| Helium cache line fills and writebacks | 25 MB/s |
| Audio DMA | negligible |
| **Total** | **122 – 162 MB/s** |

Roughly 50 – 65 % utilization. Adequate, with the caveat in Section 7.4.

### 7.3 Arbitration

Fixed-priority with a starvation guard:

1. Scanout — absolute priority, must never underrun
2. Audio DMA — hard real-time but tiny
3. Blitter — high throughput, latency-tolerant
4. Helium cache fills — latency-sensitive but guarded by PHI2 stalling
5. Bulk transfers

The starvation guard promotes any requester that has waited beyond a
programmable threshold, preventing the blitter from being locked out during
heavy scanout activity. Instrument this: per-requester wait-time histograms
in EBR, readable over the debug channel.

### 7.4 Phase 2 warning

**The Phase 1 memory topology is not the Phase 2 topology.** Phase 2 has two
independent ×16 SDRAMs with separate controllers. Do not allow Phase 1
bandwidth measurements to become Phase 2 assumptions, and do not let software
develop a dependency on the coherency that a shared controller accidentally
provides. In Phase 2, Helium and NEON share nothing but the declared
interface.

---

## 8. Video path

```
NEON scanout ──> RGB[23:0] + PCLK + HSYNC + VSYNC + DE
                        │
                        ├── Phase 1: TMDS encoder ──> HDMI connector
                        │
                        └── Phase 2: ──> pads ──> ANX6345 ──> eDP panel
```

The TMDS encoder is a standard ECP5 implementation (5× PCLK serializer using
ODDRX drivers). At 1024 × 600 @ 60 Hz the pixel clock is approximately
50 MHz, giving a 250 MHz serial rate — well within ECP5 capability.

**The panel selection question does not block Phase 1.** Any HDMI monitor
accepting 1024 × 600, or a standard 1024 × 768 mode with a letterboxed active
area, is sufficient to develop the entire compositor and window server. Panel
selection remains a Phase 2 item.

Text Mode 0 must render from EBR alone with no SDRAM dependency, exactly as
specified for the final hardware. This is the E1 exit criterion and it
provides a working console before the memory controller is trusted.

---

## 9. Debug and the embedded controller

The RP2040 remains external and retains its full specified role, minus power
sequencing (the i9 module handles its own rails) and minus FPGA
configuration during development (JTAG via DAPLink is faster for the
edit-build-test loop).

Retained RP2040 responsibilities:

- Debug Agent SPI master, exposed over USB-CDC
- BIOS and kernel image preload from microSD into SRAM
- USB HID host via PIO-USB
- Release of the 65816 from reset once the system is initialized

The Debug Agent's two access axes (physical vs. virtual, internal vs.
external bus cycle) collapse partially in Phase 1, because NEON's memory is
reachable through the shared arbiter. **Implement both axes anyway.** The
external-bus-cycle path must exist and be tested, or Phase 2 will discover
it is missing at the worst time.

The ANX6345 initialization sequence has no Phase 1 equivalent and is deferred
in full to Phase 2.

---

## 10. Physical construction

### Stage A — wire-wrap

65816 in a PLCC-44 socket on perfboard, SRAM adjacent, ribbon to the
extension board PMODs. Run PHI2 at **1 – 2 MHz**. Signal integrity on flying
leads will not meet the 65816's data setup requirement before the PHI2
falling edge at higher frequencies, and chasing intermittent faults on a
wire-wrapped bus is a poor use of time.

Purpose: prove the concept works at all.

### Stage B — carrier PCB

As soon as Stage A executes code, fabricate a small two-layer carrier:

- PLCC-44 socket
- IS61WV102416 SRAM
- Series termination on the bus
- Decoupling
- Headers matching the extension board PMOD pitch
- Optional: PCM5102A I2S DAC, microSD

This is inexpensive, and it rehearses the most timing-sensitive section of
the Phase 2 schematic — the CPU-to-SRAM-to-FPGA bus — under conditions where
a mistake costs a board respin of a EUR 15 PCB rather than the full design.

The Stage B carrier is effectively a Phase 2 schematic fragment, validated
early. Capture it in KiCad using the same symbols and net names intended for
the final design.

---

## 11. Exit criteria

Stages are Phase 1 instances of the existing E-series roadmap. Each has an
unambiguous pass condition.

### E0 — Platform alive
- Bitstream loads via openFPGALoader
- Blinky confirms clocking and PLL lock
- USB-CDC console echoes
- SDRAM memory test passes across all 8 MB, walking ones and address-in-address patterns

### E1 — Console before memory
- Text Mode 0 renders a character grid over HDMI, sourced entirely from EBR
- Console accepts input from USB-CDC and displays it
- No SDRAM access anywhere in the path
- *Pass condition: a working terminal exists that does not depend on the memory controller*

### E2 — CPU attached
- 65816 in socket, released from reset, free-running NOP test executes
- Bus cycles captured in the trace buffer and readable over the debug channel
- VPA/VDA decoding confirmed against expected instruction fetch patterns
- Native mode entry verified

### E3 — Translation active
- Helium MMU and TLB operational, CPU executes through translation
- Cache controller running against external SRAM, hit/miss counters plausible
- PHI2 stalling exercised on cache fill, bounded and measured
- ABORTB fault raised on an unmapped page, handled, and the instruction resumed correctly
- Context switch between two ASIDs with correct CTX_SET_PTBASE / CTX_SET_ASID ordering
- *Pass condition: the virtual memory system is real, not simulated*

### E4 — Graphics
- Framebuffer scanout from SDRAM, stable at target resolution
- Blitter executes all four channels, minterm logic verified against a reference implementation
- Hardware cursor overlay tracks without composite path involvement
- Arbiter wait-time histograms show no scanout underrun under blitter load

### E5 — System
- Audio DMA to PCM5102A, continuous playback without underrun
- Compositor Model C with damage-limited recomposition
- Kernel boots to a prompt — **the Apple II milestone, achieved in Phase 1**

Beyond E5, work transitions to the G-series GUI roadmap, running entirely on
the Phase 1 platform while Phase 2 hardware design proceeds in parallel.

---

## 12. Open items

### Blocking, resolve before purchase
- **PMOD buffering.** Are the extension board headers direct bidirectional
  FPGA I/O, or unidirectional buffered outputs? Determines whether the plan
  is viable as written.

### Blocking, resolve before gateware
- SODIMM pin assignment: map the 72 signals of Section 6 onto specific i9
  pins, respecting ECP5 bank voltage grouping and keeping the 65816 bus
  within as few banks as practical
- Clock plan: PLL outputs for Helium domain, NEON domain, pixel clock, and
  5× TMDS clock; confirm ECP5 PLL count is sufficient
- Cache controller parameterization: define the synthesis parameters before
  writing the first module

### Carried forward from prior sessions, unaffected by Phase 1
- Panel selection (Phase 2 only)
- BGA assembly house decision (Phase 2 only, but now informed by the Section
  5.5 link cost finding)
- Calypsi calling convention and direct-page pseudo-register audit
- Virtual bank `$00` / pinned region interaction across context switches
- ASID width decision

### Resolved or deferred by Phase 1
- NEON pin budget: deferred, no longer blocking software work
- SDRAM pin multiplexing for NEON: deferred to Phase 2
- DBG_CSN allocation: trivially resolved in Phase 1, informs Phase 2

---

## 13. What must not leak from Phase 1 to Phase 2

A checklist for review at the Phase 1 to Phase 2 transition. Each item is a
Phase 1 convenience that becomes a Phase 2 defect if it survives.

| Phase 1 property | Why it must not survive |
|---|---|
| Shared SDRAM controller | Phase 2 has two independent memories |
| Single clock domain | Phase 2 has genuinely separate clocks |
| Fast bidirectional access to NEON | Phase 2 aperture is write-only |
| Hardcoded 1 MB cache assumptions | Cache size is a parameter |
| TMDS output inside NEON | Phase 2 outputs parallel RGB |
| Any direct signal between Helium and NEON outside Section 5 | Phase 2 has a pin-limited link |
| Bandwidth figures derived from ×32 shared memory | Phase 2 memory topology differs |
| Debug access to NEON via the internal path only | Phase 2 requires the external bus cycle path |

Review this table at every major merge, not only at the transition.

---

## 14. Summary

Phase 1 converts a blocked serial dependency into two parallel tracks at the
cost of one commercial module and a small carrier PCB. It delivers the Apple
II milestone on hardware that already exists, and it hands the Phase 2
schematic capture a validated gateware reference instead of a specification.

The plan succeeds or fails on Section 4. The merged FPGA is not the design —
it is a temporary packaging of the design, and the gateware must be written
as though the two devices were already separate.
