# Step-by-step build · P0–P5 · E0–E8 · L1–L8
> two boards · small steps · verifiable tests · golden milestones

Two tracks now, not one. The **P-series** builds a prototype carrier around a commercial Colorlight i9 module, with Helium and Neon merged into a single ECP5 and everything else — CPU, SRAM, EC, video, audio — outside it; the **E-series** builds the machine this document specifies, three iCE40 parts on a board of our own. The P-series exists because the entire software roadmap was queued behind hardware that has not been drawn yet, and because gateware debugged on a bitstream costs a rebuild where the same bug on a PCB costs a respin. It is temporary packaging of the design, not a variant of it (→ [D40](sec_q#d40)).

![Fig. 7 — Two build tracks: the prototype path above, the target path below with both milestones at their exact point — Apple II on closing E6, Amiga on closing E8.](figures/fig-7-stages.svg)

## The prototype board — one ECP5 carrying two devices

- P.01 — **What it is.** One Colorlight i9 v7.2 module — Lattice LFE5U-45F-6BG381C, 44,000 LUT4, 108 × 18 kbit EBR ≈ 243 KB, 8 MB SDRAM (M12L64322A, 512K × 32 × 4 banks), 8 MB SPI flash, 25 MHz oscillator on ball P3 — on a four-layer carrier of our own holding the W65C816S in its PLCC-44 socket, the SRAM, the RP2040, a VGA output and the PCM5102A. Power is USB. The toolchain is the same open one, with `nextpnr-ecp5` beside the `nextpnr-ice40` that E0.1 already requires.
  NOTE: The module's two Broadcom B50612D Ethernet PHYs are never used and **share a reset line on ball P4**: assert it at configuration and leave it asserted. It reclaims a few hundred milliamps and removes a switching-noise source next to the CPU bus.
- P.02 — **What it deliberately is not.** No battery, no charger, no fuel gauge, no rail sequencing, no eDP, no touch, no 10.1" panel. Sheets [C](sec_c) and [S](sec_s) are simply not exercised here, and that is a **gap to note rather than to close** — the RP2040's power-sequencing role, the whole of the `$FF` power block and the shutdown path get their first hardware on the target board (→ [Q73](sec_q#q73)).
- P.03 — **Logic budget — comfortable, and the slack is the instrument.**

| Block | LUT4 |
|---|---|
| Helium: MMU, TLB, cache controller, arbiter, peripherals | 3,000 – 4,000 |
| Neon: scanout, blitter, compositor, audio DMA | 3,300 – 3,900 |
| Shared SDRAM controller and QoS arbiter | 1,200 – 1,800 |
| VGA timing generator and palette lookup | 150 – 250 |
| Debug agent, trace buffer, instrumentation | 800 – 1,200 |
| Clock domain crossing, glue | 400 – 600 |
| **Total** | **8,850 – 11,750** |

  NOTE: 20 – 27 % of 44,000. The unused capacity buys what will never fit in an HX8K: wide trace buffers, bus transaction counters, cache hit/miss histograms, an embedded logic analyser on the 65816 bus. Build it now — it is free exactly once.
- P.04 — **EBR is where the prototype lies to you, and it is the most dangerous number on this sheet.** The ECP5 has ~243 KB. **Each target iCE40 HX8K has 16 KB** — 32 blocks of 4 kbit ([D01](sec_q#d01), [T.3](sec_t#t3)) — and Neon's text buffer and font already spend 12 KB of Neon's 16 ([T.12](sec_t#t12) calls the budget fully allocated with zero margin). A 16 KB trace buffer, which is nothing here, is *larger than the whole of Helium's block RAM there*. Every EBR consumer therefore carries a synthesis parameter whose **default is the target value**, and the prototype only ever raises it for instrumentation, never for function (→ [P.18](sec_p#p18)).
- P.05 — **Pin budget.** Counted from the wuxx i9 v7.2 pinout, excluding GND, 5 V, NC and the Ethernet pairs, **106 FPGA balls** reach the SODIMM connector — raw balls, no buffers on the module. None of them collides with the SDRAM (52), the PHYs (~27), the SPI flash (4) or the clock on P3.

| Block | SODIMM | Contents | Pins | Free |
|---|---|---|---|---|
| A | 57 – 104, dense | 65816 bus | 38 | 10 |
| B | 109 – 156, dense | SRAM 13, VGA 20, I2S 3 | 36 | 12 |
| C | 41 – 54, scattered | RP2040 EC link | 8 | 2 |
| | | **Total** | **82 / 106** | **24** |

  NOTE: **The CPU bus occupies one contiguous block and must not be split.** Synchronous bus signals spread across distant connector regions and different ECP5 banks is where skew problems are manufactured. The 10 spare pins in block A become a probe header beside the CPU — the place they will actually be wanted. Block C's scattering is harmless for SPI at a few MHz and fatal for anything parallel, which is why the EC link is the correct occupant.
- P.06 — **The 65816 bus, 38 pins.** A0–A15 (16) · D0–D7 multiplexed with the bank byte (8) · FPGA outputs PHI2, RESB, ABORTB, IRQB, NMIB, BE (6) · FPGA inputs RWB, VPA, VDA, VPB, MLB, E, MX (7) · RDY bidirectional (1).
  NOTE: **A0–A10 and D0–D7 must be bidirectional FPGA pins, and this has to be settled before ball assignment.** During CPU cycles the FPGA observes them; during a walk, a fill or the EC's preload it drives them with `BE` low to tristate the CPU. That is [F.4](sec_f#f4) verbatim, and it is the single most layout-critical net here as it is there.
- P.07 — **SRAM — the same 1 MB ×8 part as the target board**, AS7C38096A-10TIN ([D15](sec_q#d15), [F.5](sec_f#f5)): 13 pins, A11–A19 (9) plus CEB, OEB, WEB (3) and the shared-net arrangement of [F.2](sec_f#f2), where A0–A10 and D0–D7 cost nothing because they are already on the bus. **A ×16 part would be a mistake dressed as convenience** — it would hand the prototype a byte-lane topology the target does not have, and the whole point of the carrier is to rehearse the topology that does (→ [D43](sec_q#d43)).
- P.08 — **Video — VGA through an R-2R ladder, 18 bits.** The ECP5 has no DAC; conversion is 6 bits per channel into 1 % ladders terminated against the monitor's 75 Ω, plus HSYNC and VSYNC, into a DE-15. Six bits is not a compromise: the target panel is driven at **18 bpp over direct RGB-TTL** ([D05](sec_q#d05), [E0.4](sec_p#e04)), so the palette entry is 18 bits on both boards and there is no truncation, no format conversion and no software difference. RGB444 would save six pins that are not needed and buy a divergence.
  NOTE: Bring-up runs **640 × 480 at 25.175 MHz**, the most tolerant mode in existence and the safest way to prove a scanout path. Move to 1024 × 600 at ~50 MHz once the timing generator is trusted, and develop the compositor there. 1024 × 600 is also where the console geometry lands exactly: 128 columns × 8 px = 1024, with the 32 rows of [T.25](sec_t#t25) centred inside 600 lines and a border above and below.
- P.09 — **Audio — 3 pins.** BCLK, LRCK, DATA into the PCM5102A, which synthesises its own internal clock and needs no MCLK.
- P.10 — **The EC — 8 pins**, SCK, MOSI, MISO, DBG_CSN, IRQ, READY, DEBUG_ENABLE, FPGA_READY. It keeps its specified role minus power sequencing: debug-agent SPI master over USB-CDC, BIOS and kernel preload from microSD, USB HID through PIO-USB, and release of the 65816 from reset. It **gains ECP5 JTAG over four GPIO**, because a carrier hosting the SODIMM socket directly has no DAPLink on it — a natural extension of the configuration role it already owns (→ [sheet D](sec_d)).
  NOTE: The debug agent's two access axes — physical against virtual, internal against external bus cycle — partly collapse on one die, since Neon's memory is reachable through the shared arbiter. **Implement both anyway.** The external-bus-cycle path of [R.5](sec_r#r5) must exist and be tested here, or the target board discovers it missing at the worst possible time.
- P.11 — **Memory — 8 MB shared, and the scarcity is a feature.**

| Allocation | Size |
|---|---|
| Framebuffer, front (1024 × 600 × 8 bpp) | 600 KB |
| Framebuffer, back | 600 KB |
| Window backing stores | 2 MB |
| Physical page pool | 4 MB |
| Audio DMA buffers | 128 KB |
| Reserve | ~700 KB |

  NOTE: Enough for the whole G0–G8 roadmap, and the 16 MB per-process virtual space exercised against a small physical pool is *preferable* during development: paging and eviction run constantly instead of lying dormant behind abundant memory. Bandwidth: ×32 SDR at 100 MHz peaks at 400 MB/s and sustains 250–300; scanout 37, blitter 60–100, Helium fills 25, audio negligible — 122–162 MB/s, roughly 50–65 % utilisation. **None of those figures transfer**: the target has two independent ×16 devices with separate controllers ([D13](sec_q#d13), [D14](sec_q#d14)).
  NOTE: Arbitration is fixed-priority with a starvation guard — scanout, audio, blitter, Helium fills, bulk — and the guard promotes anything waiting past a programmable threshold. Instrument it with per-requester wait-time histograms in EBR, readable over the debug channel. It is the same shape as [D39](sec_q#d39) with one more claimant, so what is learned here is worth carrying.

## Partitioning discipline — the section the plan succeeds or fails on

The prototype merges two devices the target separates. Every shortcut taken because Helium and Neon happen to share a die is a rewrite later, and the rules below are what stop that. Review them at every major merge, not only at the transition.

- P.12 — **Rule 1 — two top-level modules, always.** `helium.v` and `neon.v` are instantiated side by side in a top level whose only job is to wire them to each other and to the pads. Neither module references a signal inside the other. There is no shared-internals file.
- P.13 — **Rule 2 — the interface is the shared bus and three control nets, and nothing else.** This is the rule the source handoff got wrong, and it is worth saying why. It proposed a dedicated 56-signal command/peek/event link between the two devices, then multiplexed it down to 32 and reported the pin cost as a finding for the package decision. **That link does not exist**: [D04](sec_q#d04) deleted it. Neon taps the CPU bus directly and the only dedicated wires are `NEON_BUS_SEL`, `NEON_BUS_BSY` and `NEON_IRQ` ([B.6](sec_b#b6)). So the prototype's `neon.v` takes A0–A15, D0–D7, PHI2, RWB and `NEON_BUS_SEL`, and drives `NEON_BUS_BSY` and `NEON_IRQ` — every one of which is a PCB net on the target board. A signal added to that list is a cost paid twice; a signal smuggled around it is a defect.
- P.14 — **Rule 3 — separate clock domains from day one.** Both modules could run from one PLL output. **Do not.** Derive two clocks from separate outputs and do proper CDC on every crossing: async FIFOs for data, two-flop synchronisers plus handshake for control. On the target the two devices take the same 25 MHz star ([B.7](sec_b#b7)) into different PLLs, which is asynchronous in practice — [T.18](sec_t#t18) already reasons that way. This costs a little latency now and buys the certainty that metastability does not surface after the PCB exists.
- P.15 — **Rule 4 — the memory port is an abstraction.** Neon's accesses leave through a memory port on Neon's boundary — `mem_addr[22:0]`, `mem_wdata`/`mem_rdata[31:0]`, `mem_we`, `mem_be[3:0]`, `mem_req`, `mem_ack`, `mem_prio[1:0]`. Here it routes to the shared arbiter; on the target it routes to Neon's own 64 MB device. **Neon contains no knowledge of which** — no arbitration logic inside it, no awareness that Helium exists as a competing requester. `mem_prio` keeps its meaning in both: 3 scanout, 2 audio, 1 blitter, 0 bulk.
- P.16 — **Rule 5 — palette entries are 18 bits, always.** Six bits per channel on the VGA ladder is the same depth the target panel takes over parallel RGB-TTL, so the palette format is byte-identical on both boards and no conversion layer ever exists (→ [P.08](sec_p#p08), [D05](sec_q#d05)).
- P.17 — **Rule 6 — what the aperture must preserve is the stall protocol, not a fake restriction.** The source handoff protected a write-only aperture with a slow `PEEK` register path; [T.15](sec_t#t15) deleted that mechanism and returned its address space, because `NEON_BUS_BSY` already stalls PHI2 and makes `$FE` genuinely readable at about six times the speed. So on one die, where a read could trivially answer in zero wait states, **implement the four-step protocol of [T.18](sec_t#t18) anyway**: busy asserted by default, the read issued after the stall rather than racing it, the meaningful edge being the *deassertion*, and Helium's watchdog turning a hung or absent Neon into a fault instead of a frozen machine. All four requirements of [T.19](sec_t#t19) are testable here, including the write-FIFO drain — the one that otherwise ships as an intermittent bug — and requirement 4 still holds: **the software model stays write-only** even though the hardware answers reads.
- P.18 — **Rule 7 — the EBR abundance is the leak nobody catches.** Every buffer — text, font, palette, cursor, scanout FIFO, blitter line buffers, cache tags, TLB, walker cache, command FIFO, trace — is declared with a size parameter defaulting to what fits an HX8K, and a build with the target defaults must synthesise and pass the same tests before any merge. A hardcoded size anywhere is a defect, and this applies to the cache first: **the cache controller is parameterised by size, line count and associativity from its first line of code**, because the target is 768 KB of 4-way cache in 2 KB lines with tags in EBR ([F.5](sec_f#f5)) and this board carries whatever the carrier carries.

## Construction

- P.19 — **Stage A — extension board plus wire-wrap.** 65816 in a PLCC-44 socket on perfboard, SRAM adjacent, ribbon to the extension board's PMOD headers. **Run PHI2 at 1–2 MHz**: flying leads will not meet the 65816's data setup requirement before the falling edge any faster, and chasing intermittent faults on a wire-wrapped bus is a poor use of a week. Purpose: prove the concept and reach P2 without waiting for a PCB.
  NOTE: [[!blocking]] **The PMOD headers must be verified as direct, unbuffered, bidirectional FPGA I/O before anything is soldered.** Several Colorlight products route GPIO through unidirectional 74HC245 buffers; the module itself does not, so this concerns the extension board only, and its described layout — I/O in groups of eight, each with its own 3V3 and GND — suggests direct connection. Confirm with a continuity meter on arrival, or skip Stage A entirely and the question is moot (→ [Q67](sec_q#q67)).
- P.20 — **Stage B — the carrier PCB, and this is the real deliverable.** Four layers: DDR2 SODIMM 200-pin socket, PLCC-44 socket and W65C816S, AS7C38096A SRAM, RP2040 with W25Q16 and microSD, R-2R ladders and DE-15, PCM5102A and 3.5 mm, USB-C, probe header on the ten spare block-A pins. Four rather than two because the CPU bus needs a ground plane and the ladder outputs need a clean return. Hosting the socket directly bypasses the buffering question, reaches all 106 pins, and gives controlled impedance on the bus.
  NOTE: **PHI2 targets 8 MHz here**, the target board's figure ([B.1](sec_b#b1)). Every stall, fill and cache measurement taken at 1–2 MHz is an existence proof and nothing more; the numbers that matter are the ones taken at the real clock (→ [Q72](sec_q#q72)).
  NOTE: **Capture it in KiCad with the symbols and net names intended for the target board.** The CPU-to-SRAM-to-FPGA section is the most timing-sensitive part of the eventual design and this is the chance to validate it where a mistake costs a cheap respin. USB-C at 5 V / 3 A or a barrel jack; a legacy 500 mA USB-A port is marginal and may not start the board. **Measure the module's actual draw before finalising the schematic** — 400–600 mA for the module, ~500–700 mA total with the PHYs held down, ~700 mA–1.1 A with them running, and all of those are estimates (→ [Q70](sec_q#q70)).
- P.21 — **The order of the two stages, which is the one thing easy to get backwards.** Stage A exists *because* the carrier does not, so it comes first or not at all: wire-wrap at 1–2 MHz reaches [P2](sec_p#p2) while the PCB is being drawn, and if the decision is to go straight to the carrier then [Q67](sec_q#q67) is moot and Stage A is simply skipped. The carrier's own sequence is then fixed — **schematic captured with the target's symbols and net names → four-layer layout with controlled impedance on the CPU bus → DRC clean and fabrication outputs → assembly → [P0](sec_p#p0) and [P1](sec_p#p1) re-validated on the board before [P2](sec_p#p2) runs on it at 8 MHz.**
  NOTE: Re-running two stages that already passed on temporary wiring is not ceremony. It is what separates a carrier defect from a gateware defect for the rest of the project, and it costs an afternoon.
  NOTE: [E0.3](sec_p#e03)'s symbol-by-symbol pass covers every part on this board and happens **before** the schematic, not alongside it. A footprint error found by a printed sheet of paper costs nothing; the same error found by a delivered board costs the board.

## What must not leak to the target board

Each row is a convenience here that becomes a defect there.

| Property of the prototype | Why it must not survive |
|---|---|
| Shared SDRAM controller | The target has two independent memories |
| A single clock domain | The target has genuinely separate PLL domains |
| 243 KB of EBR | Each HX8K has 16 KB, and Neon's is already fully allocated |
| Zero-wait access to Neon | The target stalls PHI2 and the model stays write-only |
| A ×16 SRAM, or any unshared byte lane | The target shares D0–D7 and A0–A10 with the CPU |
| Hardcoded cache or buffer sizes | Every one of them is a synthesis parameter |
| PHI2 at 1–2 MHz | The target runs 8 MHz and the margins are different |
| VGA timing assumptions | The target drives one fixed-resolution panel |
| Any Helium–Neon signal outside [P.13](sec_p#p13) | The target has three control nets and a shared bus |
| Bandwidth figures from ×32 shared memory | The target's memory topology differs |
| Debug reaching Neon by the internal path only | The target needs the external bus cycle path |
| The absence of Argon | The softcore option stays untested until the target board |
| The absence of a power path | The target sequences rails before configuration |

## Prototype stages · P0–P5

- [ ] P0 — **Platform alive.** Bitstream through openFPGALoader; blinky proving clocking and PLL lock; USB-CDC console echoing.
  TEST: SDRAM march across all 8 MB — walking ones and address-in-address · Ethernet PHYs confirmed held in reset on P4 · current draw measured both ways and written into [Q70](sec_q#q70).
- [ ] P0.a — **BOM ordered first**, because the lead times are the only part of this stage nobody can compress: Colorlight i9 v7.2, W65C816S and PLCC-44 socket, the AS7C38096A-10TIN of [P.07](sec_p#p07), RP2040 board, DE-15 and 1 % ladder resistors, PCM5102A, microSD holder, passives and headers.
- [ ] P0.b — **Blinky through openFPGALoader**, with PLL lock observable on a pin rather than inferred.
- [ ] P0.c — **PHY reset asserted at configuration** and left asserted; module draw measured with the PHYs both ways and the numbers written down.
- [ ] P0.d — **EC bring-up** — LED, build-and-flash loop, USB-CDC console echo. A host console attached to the system.
- [ ] P0.e — **EC ↔ FPGA SPI link** with a scratch register file in the fabric, giving host-side read and write of FPGA state. This is the debug backbone and every stage after it is easier for existing.
- [ ] P0.f — **SDRAM controller exercised** across all 8 MB — main memory proven and characterised, not assumed.
- [ ] P0.g — **SRAM on temporary interconnect**, validated by the same march. What is being proved is the asynchronous parallel interface and its pin mapping, not the part.
- [ ] P1 — **Console before memory.** Text Mode 0 rendering a character grid over VGA at 640 × 480 from EBR alone, accepting USB-CDC input.
  TEST: no SDRAM access anywhere in the path · the buffer is **128 × 32 with [T.25](sec_t#t25)'s arithmetic unchanged**, displayed as an 80 × 30 window at 640 × 480 and in full at 1024 × 600.
  NOTE: This is [T.1](sec_t#t1) and [E1.7](sec_p#e17)'s N0 on different silicon, and it earns its place for the same reason: a bitstream putting correct characters on a screen has proved the PLL, the timing generator, EBR initialisation and the output pins in one shot, and every stage after it is easier to debug for having a display. **The geometry does not become a parameter** — one console driver serves both boards (→ [D44](sec_q#d44)).
- [ ] P1.a — **R-2R ladders assembled and the timing generator producing colour bars** at 640 × 480 / 25.175 MHz — a monitor displaying output from your own gateware.
- [ ] P1.b — **Mode 0 text engine in EBR** with the font ROM and the 128 × 32 buffer, [T.25](sec_t#t25)'s `TEXT_START + text_row*128 + column` unchanged ([D44](sec_q#d44)).
- [ ] P1.c — **The buffer written from the EC over SPI**, with console input echoing to the screen — a working terminal that never touches the memory controller.
- [ ] P2 — **CPU attached.** 65816 in its socket, released from reset, free-running NOP test executing.
  TEST: bus cycles captured in the trace buffer and readable over the debug channel · VPA/VDA decoding confirmed against expected fetch patterns · native mode entry verified.
- [ ] P2.a — **Two top-level modules from the first commit** — `helium.v` and `neon.v` side by side, separate PLL outputs, CDC on every crossing, the interface of [P.13](sec_p#p13) and nothing else. This is decided at the start or paid for later as a rewrite ([P.12](sec_p#p12)–[P.15](sec_p#p15)).
- [ ] P2.b — **Helium skeleton** — PLL, reset sequencer, PHI2 generation, bus-cycle FSM, bank-byte latch, VPA/VDA decode. Every CPU bus cycle correctly framed and classified.
- [ ] P2.c — **Trace buffer in EBR with a raw debug path** — direct access to EBR and SRAM, no cache anywhere in it, so cycles are capturable the moment the CPU is attached. It is re-routed through the cache at [P3.f](sec_p#p3f), once there is a cache to be coherent with.
- [ ] P2.d — **CPU boots from EBR-hosted memory** with real reset vectors, and enters native mode — the CPU executing your code out of memory you control.
- [ ] P2.e — **Bank `$FF` I/O decode** with a write-only LED register. The privileged bank exists and the CPU can command a peripheral.
- [ ] P2.f — **Neon's text aperture written by the CPU** across the shared bus, with the four-step stall protocol of [P.17](sec_p#p17) implemented rather than shortcut. The CPU prints to the screen.
- [ ] P2.g — **CPU-visible UART in bank `$FF`** bridged to the EC's USB-CDC — bidirectional interactive I/O between CPU code and your terminal.
- [ ] P2.h — **crt0, user and kernel linker scripts and a minimal C runtime**; a C hello world printing through the UART register. The convention audited at [E0.8](sec_p#e08) now reaches real hardware.
- [ ] P2.i — **Native 65816 monitor resident in SRAM** — examine, deposit, go, disassemble. The machine is self-hosting for inspection, with no host attached.
- [ ] P2.j — **microSD boot path** — the EC reads an image from the card into memory and releases the CPU from reset.
- [ ] P2.k — **USB HID keyboard** on the EC, delivered to a CPU-visible register.
- [ ] P2.l — **Text console driver** — Mode 0 plus the keyboard, wired to the monitor. Power on and the board reaches its own prompt on its own display.
  NOTE: **[P2.h](sec_p#p2h)–[P2.l](sec_p#p2l) are a checkpoint, not a milestone.** Text mode, no MMU, no graphics — worth having for the debugging and for the morale, and every piece of it is needed by [P3](sec_p#p3) and [P5](sec_p#p5) anyway, so nothing here is built twice. It is not [E6](sec_p#e6) and must not be recorded as it.
- [ ] P3 — **Translation active.** Helium's MMU, TLB and walker operational with the CPU executing through translation; cache controller against the external SRAM with plausible hit/miss counters.
  TEST: PHI2 stalling on a fill, bounded and measured · ABORTB raised on an unmapped page, handled, instruction resumed · context switch between two ASIDs with correct CTX_SET_PTBASE / CTX_SET_ASID ordering · the fill watchdog tripped deliberately instead of freezing the board.
  NOTE: *Pass condition: the virtual memory system is real, not simulated.* This is the stage the whole prototype was worth building for — [E4](sec_p#e4) arrives with its hardest gateware already debugged.
- [ ] P3.a — **SRAM exposed to the CPU as raw uncached memory** — real off-chip memory usable by CPU code.
- [ ] P3.b — **The shared SDRAM controller and its arbiter**, fixed priority with the starvation guard of [P.11](sec_p#p11) and **per-requester wait-time histograms instrumented from the first version**, not added later when a number is finally wanted.
- [ ] P3.c — **SDRAM reachable by the CPU through the arbiter**, uncached and slow — large main memory addressable before it is fast.
- [ ] P3.d — **Cache controller against the SRAM** — 2 KB tagged lines, 4-way, 256 B fill sub-blocks with valid and dirty bits per sub-block ([F.5](sec_f#f5), [F.6](sec_f#f6)), write-back, PHI2 stalled with BE=0 for bounded fills, hit and miss counters. **Size, line, sub-block and associativity are synthesis parameters from the first line of code** ([P.18](sec_p#p18)).
- [ ] P3.e — **Flush and invalidate with the BUSY polling protocol** — what software needs to enforce coherence, and the prerequisite for both DMA and paging.
- [ ] P3.f — **Debug agent re-routed through the cache controller**, with a per-transaction watchdog and the DEBUG_ENABLE strap. Host memory access is now coherent with the CPU's own view.
- [ ] P3.g — **Halt, single-step, resume and register readback**, with the fill watchdog suppressed while the CPU is halted — otherwise every session ends in a spurious abort ([R.17](sec_r#r17)).
- [ ] P3.h — **EC monitor front end** over USB-CDC — `load`, `go`, `dump`, `halt`, `step`, `trace` ([R.22](sec_r#r22)). A complete bring-up workbench: binaries load and run in seconds.
- [ ] P3.i — **ASID-tagged TLB in EBR with a single identity mapping** — every access translated and behaviour unchanged, which is precisely what proves the datapath before anything depends on it.
- [ ] P3.j — **Hardware page-table walker** reading 32-bit PTEs from the fixed, uncached SRAM region of [F.5](sec_f#f5).
- [ ] P3.k — **ABORTB on unmapped pages and permission violations**, an assembly handler that reports, and **instruction resumption verified correct** — faults detected, delivered to software, and recoverable.
- [ ] P3.l — **Context registers** with the `CTX_SET_PTBASE` → `CTX_SET_ASID` ordering invariant enforced in hardware; two contexts switched from the monitor, two distinct 16 MB spaces coexisting.
- [ ] P3.m — **Protection policy enforced** — bank `$FF` privileged and never user-mappable, bank `$00`'s vector and stub pages pinned in every context ([L.11](sec_l#l11)), bank `$01` the resident kernel ([J.1](sec_j#j1)), vectors mapped identically everywhere.
- [ ] P4 — **Graphics.** Framebuffer scanout from SDRAM stable at 1024 × 600, 18-bit palette lookup, blitter, hardware cursor.
  TEST: minterm logic verified against a reference implementation · cursor overlay tracking without involving the composite path · arbiter wait-time histograms showing no scanout underrun under blitter load · **a CPU read of the framebuffer returning exactly what was written, including immediately after a write** — the write-FIFO drain of [T.19](sec_t#t19) — and **a read with Neon held in reset timing out into `$FF` plus a fault, not into a frozen machine**.
- [ ] P4.a — **Framebuffer scanout from SDRAM at 1024 × 600, 8 bpp**, through the 18-bit palette of [P.16](sec_p#p16) — Neon stage N1 ([T.63](sec_t#t63)), in the same pixel format the production RGB path uses.
- [ ] P4.b — **Hardware cursor overlay in EBR** — pointer motion costs nothing in the composite path.
- [ ] P4.c — **Blitter, register-driven, straight copy only** ([T.37](sec_t#t37)) — rectangles move without the CPU.
- [ ] P4.d — **The full blitter, its raster-operation logic verified against a reference software model** — barrel shifter, edge masks, descending mode, queue. **Which engine is built is [Q58](sec_q#q58)**, still open, and this is the board where trying it both ways costs a rebuild instead of a respin.
- [ ] P4.e — **Command FIFO** — Neon stage N4, and the CPU stops polling `BLIT_BUSY`. SDRAM command lists are N5 and the compositor does not need them ([V.35](sec_v#v35)).
- [ ] P4.f — **Arbiter wait-time histograms captured under sustained blitter load** — no scanout underrun, with evidence rather than assumption.
- [ ] P5 — **System.** Audio DMA to the PCM5102A without underrun; compositor with damage-limited recomposition; kernel booting to a prompt.
  NOTE: The milestone is reached on temporary hardware, which is the point and also the caveat: it proves the software and the gateware, not the machine. **[E6](sec_p#e6) is still the milestone**, and closing it costs the P-series work plus a board that exists.
- [ ] P5.a — **Audio DMA to the PCM5102A over I2S**, continuous playback with no underrun — the machine makes sound reliably.
- [ ] P5.b — **Kernel skeleton in C** — startup, vector table, direct-page conventions, IRQ dispatch. A kernel image that owns the machine.
- [ ] P5.c — **COP syscall dispatcher** with `write` and `exit`, against the ABI documented at [E0.8](sec_p#e08) — the user–kernel boundary as a contract rather than a habit.
- [ ] P5.d — **Physical frame allocator and page-table construction** — the kernel can build an address space.
- [ ] P5.e — **First user process** loaded into its own space at reduced privilege, demonstrably unable to reach bank `$FF` or the kernel.
- [ ] P5.f — **Timer IRQ, scheduler and the assembly context switch** — two user processes running preemptively and concurrently.
- [ ] P5.g — **Demand paging and copy-on-write** on top of ABORT — lazy allocation and cheap sharing, against the deliberately small physical pool of [P.11](sec_p#p11).
- [ ] P5.h — **Ports, signals and messages** — the IPC substrate [sheet V](sec_v) is written against.
- [ ] P5.i — **Driver framework** — init/read/write/ioctl/irq with fd-based device paths, and `/dev/tty`, which turns the console into a driver like any other.
- [ ] P5.j — **microSD block driver and a read-only filesystem** — files on the card visible to the OS.
- [ ] P5.k — **Program loader and a shell** — type a name and a program runs in its own address space.
- [ ] P5.l — **`/dev/fb` with an ioctl mapping framebuffer pages into a process**, plus `/dev/audio` — a user program draws at memory speed and makes sound through the ordinary device contract.
  NOTE: **The compositor's cost model is measured at G2, not here** ([V.36](sec_v#v36), [V.37](sec_v#v37)). The number wanted is the blitter's own time for a full-screen pass, separated from emission and from whatever the client tasks are doing — and it is the project's first real software gate, so it is instrumented deliberately rather than inferred from a frame rate.

!!! APPLE II · ON THE PROTOTYPE — the software boots to a prompt on borrowed silicon.

Beyond P5 the G-series of [sheet V](sec_v) runs entirely on this board while the target hardware proceeds in parallel — with the exception of everything resting on the `$FF` power block, which has no prototype ([P.02](sec_p#p02)).

## Target board · E0–E8

Nine stages, each with concrete hardware/gateware/software and a verification criterion before moving on. E0 and E1 are broken out below, because everything before a working board exists is where the project actually is today: the pin budget is closed, all six schematic sheets are designed *as documents*, and nothing has been drawn in KiCad. **E0 is also where the project-wide work lives** — the toolchains, the audited 65816 ABI, the simulator and CI — together with the two things that have to be finished on borrowed hardware before a layout is committed: the iCE40 resource proof and the power path on eval boards.

- [ ] E0.1 — **Toolchain.** KiCad 8, oss-cad-suite (Yosys + nextpnr-ice40 + IceStorm), pico-sdk, and a simulator matching the HDL choice. **The P-series adds `nextpnr-ecp5` and prjtrellis to the same suite** — one more target for the same flow, no proprietary tools on either board ([P.01](sec_p#p01)).
  TEST: a simulated iCE40 blinky and a compiled RP2040 "hello".
- [ ] E0.2 — **HDL decision** — Verilog or VHDL. Not a preference: it decides which reference softcore is usable at all (→ [Q2](sec_q#q2)).
  NOTE: **This now blocks the P-series rather than the schematic**, and it is the first thing the prototype makes urgent: every line of gateware written for the carrier is the same line that ships on the target board, so the choice is made before P0 rather than before E1.7.
- [ ] E0.3 — **Project KiCad library.** Symbol and footprint for every component, verified one at a time.
  TEST: each footprint printed 1:1 on paper and laid against the physical part.
  NOTE: The parts the prototype never carries are the ones with nothing to catch an error first — **the power devices, the panel FPC, the touch connector, the audio DAC and the iCE40 TQ144** — so those get the paper pass twice, on different days. The carrier's own parts are checked here too, and that check blocks its schematic ([P.21](sec_p#p21)).
- [~] E0.4 — **Panel.** Candidate fixed and its controller verified — HX8282, 24-bit TTL at 3.3V with no level shifters, driven at 18 bpp on pin budget, DE mode by default, data latched on the *falling* DCLK edge so gateware changes it on the rising one. **The modeline has moved**: not ~51.2 MHz for 1344×635 but **51.5625 MHz for 1344×640 → 59.95 Hz**, which is what the 25 MHz oscillator can actually reach through one iCE40 PLL ([T.4](sec_t#t4)). Still missing the *module* datasheet, and the blanking values remain provisional until it arrives (→ [Q1](sec_q#q1), [Q43](sec_q#q43)).
- [x] E0.5 — **Pin budget across the three TQ-144 parts** — closed, it fits. This is what proved the architecture buildable, and what forced the shared-bus topology when it did not.
- [ ] E0.6 — **Freeze the block diagram** as the schematic's hierarchical reference.
  NOTE: **This is not Fig. 1, and the distinction is what the stage is for.** [Sheet B](sec_b)'s figure is the *architecture* — who inhabits the shared bus and who governs it from outside — and it is current. What E0.6 owes is the *schematic's* hierarchy: the same machine cut along the six sheets of [E1.1](sec_p#e11), so that every block on the drawing is a page in KiCad and nothing on the board sits outside one. The REV B drawing this used to track has been retired rather than brought forward.
- [ ] E0.7 — **Repository and documentation skeleton** — gateware, firmware, kernel, hardware, docs, with the decision log at the centre of it. One place where every artifact lands, which is cheap on day one and expensive on day two hundred.
- [ ] E0.8 — **The 65816 toolchain audited, not merely installed.** Calypsi C and ca65, with the calling convention and the direct-page pseudo-register usage checked against actual compiled output and written down as the ABI the kernel is built on.
  NOTE: [[!blocking]] **No kernel work starts before this exists.** A calling convention discovered by experiment halfway through [P5](sec_p#p5) is the kind of blocker that stops a project for a fortnight, and it is the one known blocker on the software side (→ [sheet O](sec_o)).
- [ ] E0.9 — **Simulator harness** for the 65816, with an I/O stub at the real bank `$FF` UART address so the same binary runs on the simulator and on hardware unchanged.
  NOTE: The point is discrimination rather than convenience. From here on a kernel bug is separable from a gateware bug — which is exactly the confusion that consumes weeks during bring-up.
- [ ] E0.10 — **CI** building both gateware targets, the RP2040 firmware and the 65816 binaries, and running the simulator suite on every commit.
  TEST: a deliberately broken commit is rejected by the pipeline, not by a person.
- [ ] E0.11 — **Gateware split into two independent builds.** `helium.v` and `neon.v` leave the shared top level and become separate projects communicating over the shared CPU bus and the three control nets of [P.13](sec_p#p13) — every one of which stops being a wire inside a die and becomes a PCB net.
  NOTE: There is no serializer and no dedicated inter-FPGA link to design here: [D04](sec_q#d04) deleted that link, and [P.13](sec_p#p13) is written the way it is precisely so that this step costs a build-system change. **If it costs more than that, the discipline section was not enforced** and the cost is being paid now instead of then.
- [ ] E0.12 — **Both halves ported to iCE40 HX8K on dev boards, and measured.** Argon's slot stays empty ([B.4](sec_b#b4)), so what has to fit is Helium and Neon: LUT, EBR and Fmax against the estimates of [P.03](sec_p#p03) and [P.04](sec_p#p04), built with the target EBR defaults and none of the prototype's instrumentation.
  TEST: **103.125 MHz closing on Neon's half** with the blitter placed beside the SDRAM controller, or the single-rate fallback taken deliberately and its bandwidth consequence accepted (→ [Q44](sec_q#q44)) · Helium fitting with the 768 KB cache's tags inside 16 KB of EBR ([F.5](sec_f#f5)).
  NOTE: [[!blocking]] **This blocks [E1.1](sec_p#e11).** The one question the prototype cannot answer is whether the target devices are large enough and fast enough, because it has neither the fabric nor the block RAM of the parts that ship. Resizing a part here costs a line in the BOM; discovering it after a layout costs the layout.
- [ ] E0.13 — **The full software stack run on the two-device configuration** — kernel, drivers and the G-series as far as it has reached — before a board is drawn.
  TEST: nothing in the stack turns out to have depended on the single-FPGA prototype.
- [ ] E0.14 — **Power subsystem validated on eval hardware.** CH224K PD sink, BQ25896 charger, TPS63020 and the dedicated core buck, each rail brought up on the vendor's own board into resistive loads at target current; then the EC sequencer firmware written against them — always-on domain, staged enables, fault handling, MAX17048 readout.
  NOTE: This is the part of [sheet C](sec_c) and [sheet S](sec_s) the carrier deliberately does not exercise ([P.02](sec_p#p02)), so without this stage its first hardware would be REV A itself. Proving the iCE40's core-before-IO order and a monotonic ramp on an eval rig costs days; discovering a violation on a populated board costs a respin and looks, for months, like an FPGA that configures unreliably ([C.8](sec_c#c8)).
- [ ] E0.15 — **Battery pack characterised on eval hardware** — protection, charge and discharge cycles, fuel-gauge learning — so the energy path is understood before it is designed into a layout rather than after.
- [~] E1.1 — **Six hierarchical schematic sheets**, designed simplest to hardest: 1.1 power · 1.2 RP2040 / EC · 1.3 FPGA configuration · 1.4 Helium, bus and memories · 1.5 Neon, panel and audio · 1.6 Argon and connectors.
  NOTE: All six exist as design documents; none is drawn in KiCad yet. Sheet 1.5 is held by the panel datasheet.
  NOTE: **Capture them in the order they are designed, not the order they are numbered**, and review the BOM and the DNP list against the pin-budget sheets as each one closes. The DNP that matters most is Argon: its 3V3 and 1V2 sit behind their own 0Ω jumpers so that an empty footprint cannot load the bus ([E.5](sec_e#e5)).
- [ ] E1.2 — **Cross-review before layout.**
  TEST: clean ERC, plus a manual pin-by-pin pass of every TQFP against its datasheet — twice, on different days.
- [ ] E1.3 — **Layout.** Stackup decided against the router's difficulty, PLCC socket centred with the FPGAs around it and bus traces under ~10 cm, decoupling hard against the pins, continuous ground plane.
  TEST: clean DRC and gerbers checked in an external viewer, not in KiCad's own.
  NOTE: **Route in order of difficulty, not in order of the netlist.** The CPU bus first, then both SDRAM interfaces, then the parallel RGB to the panel FPC — and only once those are done, power distribution and everything that is left. The hard nets get the room while there is still room to give them.
- [ ] E1.4 — **Staged population — the golden rule:** never populate a block until the previous one works. The board is designed for this, with a test point and an LED per rail and 0Ω jumpers to isolate every branch.
  NOTE: **The rails are not the only thing worth probing, and the bus has no provision at all today.** `PHI2`, `RESB`, `ABORTB`, `RDY`, `BE` and the three Neon control nets each need a labelled test point, spaced to land two probes without shorting — they are precisely the signals [E1.8](sec_p#e18) and [E5](sec_p#e5) are debugged through, and a scope lead soldered to a TQFP pin during bring-up is how boards get killed.
  NOTE: **Open the errata list at this stage and keep it as a document, not as a memory.** Every stage from here to [E8](sec_p#e8) adds to it, and [L7](sec_p#l7) is built from nothing else. A defect noticed during bring-up and not written down is a defect rediscovered on REV B.
- [ ] E1.5 — **Power alone**, in the order of [C.19](sec_c#c19): charger and SYS with no downstream jumper fitted, then each rail into a resistive load, then the jumpers one at a time.
  TEST: 3V3_AON and 3V3_MAIN within ±2% and 1V2 within ±3% under dummy load · PD contract measured at 9 V on VBUS · SYS holding with the battery disconnected entirely · charging with correct STAT lines · draw at each jumper checked against the budget table · rails driven by hand from the EN header, with no EC firmware in the picture.
  NOTE: This stage also has to prove the **order**, not just the levels: 1V2 before 3V3_MAIN before VPP_2V5, each ramp monotonic, on a two-channel capture. A rail-order violation does not show up as a bad voltage — it shows up months later as an FPGA that configures unreliably (→ [C.8](sec_c#c8)).
- [ ] E1.6 — **RP2040 alone.**
  TEST: enumerates as USB-CDC and answers on the console · mounts the microSD and lists files · reads the gauge and charger over I2C-AON and reports converted units · generates a measurable backlight PWM, with no FPGA populated · sequences every rail up and down under firmware control, including the R1 tri-state pass, measured with the switched domain dead.
  NOTE: The **crossings audit** of [C.14](sec_c#c14) belongs before this stage, not during it — a net-by-net list of every AON↔switched connection checked against R1–R3. It is the one review pass on the board that cannot be recovered by rework if it is skipped.
- [ ] E1.7 — **The three FPGAs — and the screen, which arrives here rather than at E5.**
  TEST: the EC configures each one separately with a blinky · CDONE high on all three · reconfiguring Neon without disturbing Helium demonstrated · **Neon stage N0**: a correct 128 × 32 blank text screen the instant Neon's CDONE rises, then glyphs written from the EC over the service port, then `TEXT_START` scrolling by exactly one row.
  NOTE: The debug agent's `DBG_ID` reading `$6516` belongs to this stage too — one SPI frame that proves link, bitstream and Helium clock at once (→ [R.8](sec_r#r8)).
  NOTE: **N0 needs no SDRAM, no CPU, no Helium and no bus** — only Neon, the panel and the EC's SPI — so there is nothing to gain by holding it to E5 and a great deal to lose. It is also a far better acceptance test than a blinky: a bitstream putting correct characters on the panel has demonstrated the PLL, the timing generator, block-RAM initialisation, the RGB pins, the FPC and the backlight in one shot. **Bringing it forward gives the project a screen three stages earlier than planned, and every stage after it is easier to debug for having one** (→ [T.64](sec_t#t64)).
  NOTE: **Inherited from [P1](sec_p#p1)**, where the same text mode ran against a VGA monitor. What is new here is everything the prototype could not carry: the panel's own timing, the FPC, the backlight and the 51.5625 MHz modeline of [T.4](sec_t#t4). The character generator, the buffer geometry and the console driver arrive already debugged, so a failure at this stage is a *board* failure — which is exactly the discrimination the P-series was built to buy.
- [ ] E1.8 — **CPU alive — free-run.** Bus forced to NOP, PHI2 from Helium.
  TEST: the address counter advances consistently on the analyser · an LED blinks from a decoded address.
- [ ] E2 — **SRAM + serial monitor.** SRAM on the bus; BIOS preload by the RP2040; UART console; peek/poke monitor.
  TEST: console echo · read/write arbitrary memory · checksum of the loaded BIOS.
  NOTE: The peek/poke does not have to wait for a BIOS: the debug agent gives physical read/write and a march test with the CPU still in reset, which is where most of its value is (→ [R.22](sec_r#r22)).
  NOTE: **Inherited from [P2](sec_p#p2)** — the monitor, the linker scripts, the C runtime and the microSD load path all arrive already debugged, so a failure at this stage is a board failure or a timing failure and cannot be anything else. That discrimination is what the carrier was built to buy.
- [ ] E3 — **SDRAM.** Controller (adapted open-source candidate) as a DMA/paging engine behind Helium, with auto-refresh interleaved into the fill state machine.
  TEST: pseudorandom pattern over the full 64 MB with no errors for hours · refresh holds during sustained fills.
- [ ] E4 — **MMU + cache + protection.** Walker, TLB with ASID, 4-way cache with tags in EBR, PHI2 stall with BE=0 and its watchdog, ABORTB, FAULT_* registers at `$FF`.
  TEST: an illegal access triggers ABORT with correct FAULT_ADDR/CAUSE · a cache miss stalls and resumes cleanly · a deliberately hung fill trips the watchdog instead of freezing the board · measured cache hit rate.
  NOTE: The debug agent reaches its full extent here — halt, step and trace over the `BE` handoff, virtual-mode access, and `TLB_PROBE` against `PTWALK` to catch a stale entry. The same stage owes it one negative test: the fill watchdog must be suppressed while the CPU is halted, or every session ends in a spurious abort (→ [R.17](sec_r#r17)).
  NOTE: **Inherited from [P3](sec_p#p3)** — walker, TLB, ASID switching, the PHI2 stall and its watchdog all arrive as working gateware. Two things do not transfer and have to be re-earned: the **cache size**, which was a synthesis parameter on the prototype and is 768 KB of 4-way here ([P.18](sec_p#p18), [F.5](sec_f#f5)), and the **timing**, because the prototype's shared nets sat on a carrier at a lower PHI2 and this board runs 8 MHz with three loads ([F.4](sec_f#f4)).
- [ ] E5 — **Video + audio.** Neon on the bus with its own SDRAM, `$FE` window; VGA pattern → framebuffer; tone over I2S. This is now **Neon stages N1 and N2** ([T.63](sec_t#t63)): the SDRAM controller and arbiter, Mode 1 scanout, the register-driven blitter, then Modes 2a/2b with the 256-entry palette, `COPY_KEYED` and double buffering. The text console of E1.7 is the instrument the whole stage is debugged with. ((Start below the gateware: the panel's own **BIST** mode generates test patterns with no external clock and no bitstream, validating panel, FPC and backlight in isolation before anything can be blamed on logic.))
  TEST: BIST patterns on the panel · stable VGA image · a CPU write shows up on screen · **a CPU read of the framebuffer returning exactly what was written, including immediately after a write, which is the write-FIFO drain of [T.19](sec_t#t19)** · **a read with Neon deliberately held in reset timing out into `$FF` plus a fault, not into a frozen machine** · **worst-case read latency measured with the blitter running a full-screen keyed copy** · a register-driven `FILL_RECT` covering the screen in 0.31 ms measured against `BLIT_BUSY` · an animated multicolour scene holding 60 Hz with `SWAP_BUFFERS` and no tearing · clean 440 Hz tone.
  NOTE: The two riskiest items are here and neither is a gateware detail. **103.125 MHz must close on the HX8K** with the blitter placed beside the SDRAM controller, and the fallback is single-rate at half the bandwidth (→ [Q44](sec_q#q44)). And the **aperture read path** is exercised for the first time: three of the four requirements of [T.19](sec_t#t19) are only observable here, and the write-FIFO one fails *intermittently* if it is wrong — which is exactly the kind of defect that gets attributed to the blitter for a week (→ [D37](sec_q#d37), [Q57](sec_q#q57)).
  NOTE: **[P4](sec_p#p4) moves that sentence.** The four requirements of [T.19](sec_t#t19) are observable on the prototype and are a pass condition there, so the write-FIFO drain is not seen for the first time on a board that costs a respin. What remains genuinely first-time here is the **103.125 MHz closure on the HX8K** and Neon's own SDRAM — neither of which the merged die can rehearse, since it has one memory and a different fabric ([P.13](sec_p#p13)).
- [ ] E6 — **SD + minimal OS + HID.** SD handoff, block driver, FAT, kernel load, serial shell, basic USB keyboard.
  TEST: power on → prompt, with no PC connected.
  NOTE: **The software reached this point at [P5](sec_p#p5)**, on borrowed silicon. That is not this milestone and must not be recorded as it: the machine booting on its own is the claim, and it needs a board. What the prototype removes is the risk that the *software* is what fails here.

!!! APPLE II MILESTONE — the machine boots on its own to a prompt.

- [ ] E7 — **Multitasking.** Scheduler with a 100 Hz tick, syscalls via COP, two processes with MMU isolation.
  TEST: two concurrent processes; one dies from a violation and the other stays intact.
- [ ] E8 — **Laptop + GUI.** 10.1” panel + GT911 + backlight, battery + charging, chassis; window compositor over `/dev/fb`; `/dev/power` with the battery indicator and the shutdown dialogue. On Neon's side this is **N3 and N4** — the 1-bpp barrel-shift blitter, `COPY_EXPAND` for glyphs and icons, the graphics cursor, and the command FIFO that stops the CPU polling `BLIT_BUSY` ([T.63](sec_t#t63)). N3 is what makes the milestone reachable; nothing above it is required for it. [[!blocking]] **Which blitter is built here is not settled** — the operation-based engine of sheet T or the four-channel engine of [sheet U](sec_u) — and the same answer decides whether the compositor keeps per-window backing stores and whether the GUI runs at 1 or 8 bpp (→ [Q58](sec_q#q58), [Q59](sec_q#q59), [Q60](sec_q#q60)).
  NOTE: **The software side of this stage is [sheet V](sec_v), staged G0–G8**, and two things in it belong here. "Compositor over `/dev/fb`" is the flat-framebuffer phrasing and survives only if [Q60](sec_q#q60) goes that way; under backing stores the framebuffer is not what a client draws into at all. And the split is not the obvious one — **the compositor is kernel code and the window server is a user task**, because command emission lives in bank `$FF` and no user task can reach it ([Q62](sec_q#q62)). G0–G4 fall inside this stage; G5–G8 — string cache, toolkit, menus, desktop shell — fall after it, and **G2 carries the project's first real software gate**: a measured full-screen recomposite (→ [V.36](sec_v#v36)).
  TEST: touch GUI session running on battery · a window drawn as a dozen commands, with the CPU back in its event loop before the blitter finishes · a window dragged over a second one and both repainted correctly from application state · a short press raises a dialogue and a cancel actually cancels · `poweroff` from the shell flushes and drops the rails · a deliberately hung kernel is recovered by the 4-second press, and a deliberately hung EC by `QON` · the indicator reports *unknown* rather than a number when telemetry is stale.
  NOTE: **The compositor's first implementation should not do precise damage regions.** Repainting a whole window costs ~1 ms, so repainting every window that intersects the dirty area, back to front, is affordable — and it skips the most defect-prone part of any window manager. Optimise only if a specific case demands it (→ [T.57](sec_t#t57)).
- [ ] E8.1 — **GT911 touch on its dedicated I2C bus**, read by the EC and delivered into `wserver` through the same input queue as HID — absolute position, button state as a bitmap, an event that fits in 32 bytes and can be coalesced ([V.30](sec_v#v30)).

!!! AMIGA MILESTONE — preemptive multitasking with a windowed GUI.

- [?] + — Optional extensions outside the critical path: populating Argon (FPGA-C) with the softcore · demand paging + swap to SD · eDP panel variant (ANX6345) as a future revision · a writable filesystem once the read-only path is stable · networking on the driver framework of [P5.i](sec_p#p5i) · `DRAW_GLYPHS` if EBR headroom ever appears, as a complement to the string cache rather than a replacement for it ([V.26](sec_v#v26), [Q61](sec_q#q61)) · **Neon N5 and N6** — display lists in SDRAM with flow control, `WAIT_LINE` and `SWAP_BUFFERS`, which is the games target, then `DRAW_LINE`, `FILL_PATTERN` and `COPY_TILED` ([T.63](sec_t#t63)).
  NOTE: N5 sits here not because it is unimportant but because it is **separable**: N3 delivers the Amiga milestone and N5 delivers autonomous rendering, in that order, and neither depends on the other. A machine that stops after N4 has a complete GUI.

## Beyond E8 · L1–L8 — the portable machine

[E8](sec_p#e8) closes with a working noVa64 on a board that runs on battery and shows a windowed desktop. What follows turns that into something you pick up, open and close, and none of it is on the critical path for either milestone — which is exactly why it is listed separately rather than folded into E8 and allowed to delay it.

- [ ] L1 — **Internal keyboard matrix**, designed and scanned by the EC firmware, delivered through the existing input path of [V.30](sec_v#v30) so that nothing above the HID driver knows the difference. A built-in keyboard replaces USB HID.
- [ ] L2 — **Integrated pointing device** selected and driven into `wserver` through the same queue — pointer input with no external peripheral attached.
- [ ] L3 — **Software power management** — rail gating, sleep and wake, battery status surfaced in the GUI through the `$FF` power block of [sheet S](sec_s).
  NOTE: This is the stage where [Q73](sec_q#q73) stops being theoretical. Everything resting on the power block had no prototype hardware at all ([P.02](sec_p#p02)), so the battery indicator, the shutdown dialogue and the stale-telemetry behaviour get their first real exercise between [E8](sec_p#e8) and here.
- [ ] L4 — **Mechanical CAD** — lid and hinge, mainboard mounting, battery bay, port cutouts, keyboard tray. A complete enclosure model before anything is printed.
- [ ] L5 — **Printed enclosure prototype** assembled, with the fit and cable-routing revisions applied. Everything physically fits and the lid closes.
- [ ] L6 — **Thermal profiling and battery-life characterisation** under sustained GUI load, inside the enclosure rather than on a bench.
  NOTE: Inside the case is the only measurement that counts. A board that is comfortable in open air and a board in a sealed printed shell are different thermal problems, and the second one is the product.
- [ ] L7 — **REV B**, correcting the errata accumulated from [E1.4](sec_p#e14) onward, and final assembly into the enclosure.
- [ ] L8 — **Release** — schematics, gateware, kernel, SDK, disk image, build instructions, user manual.
  TEST: someone who is not you builds it from what is published.

!!! PORTABLE — a noVa64 you can pick up, open and use.
