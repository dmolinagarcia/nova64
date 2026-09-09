# Phase 0 · the Spartan-6 training rig · S0–S11
> three phases, not two · a disposable rig · what transfers and what does not

Three phases now, not two. The **S-series** of this sheet builds a training rig on a Xilinx Spartan-6 board that is already on the desk, and it is **not part of noVa64**. Its purpose is to exercise protocol and software work that would otherwise be attempted for the first time on the prototype carrier of [sheet P2](sec_ai_p2) — a four-layer board that has to be designed, fabricated and assembled before a single line of it can run.

The rig exists to use calendar time that would otherwise be idle. It gates nothing. If the carrier arrives first, what is left of the S-series is abandoned in place ([P1.17](sec_ai_p1#p117)).

![Fig. 7a — The three build phases with **Phase 0** highlighted: the Spartan-6 training rig above, the single-ECP5 carrier in the middle, and the target board below with both milestones at their exact point.](figures/fig-7a-stages-phase0.svg)

## The three phases

- P1.1 — **Phase 0 — the Spartan-6 training rig, this sheet.** A Xilinx Spartan-6 XC6SLX16-FTG256 board with 2.54 mm vertical headers, plus a HAT of our own carrying a CPU socket, a Pico socket, an SD socket and VGA. The HAT is trivial and cheap to fabricate. Toolchain is Xilinx ISE 14.7 in a VM. Status: proposed.
  NOTE: **This is the one place in the project where a proprietary toolchain is used, and it is admissible precisely because the rig is not noVa64.** [A2.6](sc_a2#a26) makes the open flow a principle rather than a preference, and nothing built under ISE ships: all Phase 0 RTL is disposable by construction ([P1.14](sec_ai_p1#p114)). The concession is smaller than [P2.25](sec_ai_p2#p225)'s, which contemplates a proprietary analyser checking gateware the open flow actually builds.
- P1.2 — **Phase 1 — the prototype carrier, [sheet P2](sec_ai_p2).** One Colorlight i9 v7.2 module (LFE5U-45F ECP5) on a carrier of our own, with Helium and Neon merged into a single device as two separate top-level modules with a declared interface and separate PLL-derived clock domains. Unchanged by this sheet.
- P1.3 — **Phase 2 — the target board, [sheet P3](sec_ai_p3).** Helium and Neon as separate iCE40 TQ144 parts on a board of our own. Unchanged by this sheet. Physical separation of the two devices reduces to serializer insertion plus pin reassignment ([E0.11](sec_ai_p3#e011)).

## Phase 0 scope — in

The rig earns its place by what it de-risks, and each row is chosen because the mechanism is family-independent.

| Item | Rationale |
|---|---|
| SDRAM controller (HY57V561620FTP-H) | JEDEC SDR protocol is identical to the target AS4C32M16 / IS42S16320. Only column width differs — 9 bits against 10. |
| 65816 bus interface | Address/data/control decode, bank latching, VDA/VPA cycle qualification. |
| PHI2 stalling against real SDRAM latency | Exercises the "PHI2 always stretched on memory wait" invariant under real row-activation and refresh timing. |
| EC mailbox protocol | Wire protocol and firmware, independent of FPGA family ([D3.2](sec_ai_d3#d32)). |
| Debug agent protocol | A separate SPI slave on `DBG_CSN`; wire protocol and command set ([sheet R](sec_ai_r)). |
| Neon Mode 0, text only | Character buffer internal to Neon, hardware scroll, two-byte command port. |
| SD through a hardware SPI master in the FPGA | Register block, SD init, sector read, and a FAT32 reader in 65816 assembly. |
| USB HID keyboard on the Pico | Rig usability only, and it is the one item on this list that may transfer to nothing ([Q147](sec_ai_q#q147)). |

## Phase 0 scope — out

| Item | Reason |
|---|---|
| FPGA configuration over an SPI slave | iCE40-specific — SPI slave and NVCM, no JTAG ([D61](sec_ai_q#d61)). The Spartan-6 configures over JTAG, so it is not reproducible here. |
| MMU, page-table walk, ASID-tagged TLB | Deferred to Phase 1 ([P3](sec_ai_p2#p3)). |
| Cache controller | Deferred to Phase 1 ([P3.d](sec_ai_p2#p3d)). |
| Blitter, graphics modes, compositor | Deferred to Phase 1 ([P4](sec_ai_p2#p4)). Mode 0 only here. |
| Power sequencing, charger, fuel gauge | No equivalent on the rig, as there is none on the carrier either ([P2.02](sec_ai_p2#p202)). |
| Embedded RP2040 debug probe | No chassis, so debug is direct ([P1.5](sec_ai_p1#p15)). |
| Audio and I2S | Not required by any Phase 0 objective. |
| The RP2354B in its production package | See [P1.4](sec_ai_p1#p14). |

## Decisions

- P1.4 — **A Raspberry Pi Pico 2, not a bare RP2354B.** The RP2354B is QFN-80 with in-package flash, so putting it on the HAT means designing and fabricating a breakout. Nothing in the Phase 0 scope depends on the package: the mailbox and the debug agent are protocol and firmware, and pico-sdk is identical across the family. The 2354B-specific properties — 48 GPIO, in-package flash, the A4 stepping — are integration questions the rig cannot answer whatever is soldered to it. **Consequence: the Pico socket on the HAT is a standard Pico 2 footprint** (→ [D74](sec_ai_q#d74)).
  NOTE: Bootrom A/B partition table support, which is an open item blocking schematic capture, is verifiable here: it is the same bootrom (→ [Q128](sec_ai_q#q128)).
- P1.5 — **Debug over UART, not USB CDC.** A USB HID keyboard requires the Pico to be a USB **host**, and it has one USB port: it cannot be a host for the keyboard and a CDC device to the PC at the same time. The port serves the keyboard under TinyUSB host, and the debug console runs over UART to a USB-serial adapter (→ [D75](sec_ai_q#d75)).
  NOTE: Rejected alternative — Pico-PIO-USB for a second host port. It works, and the RP2350 has three PIO blocks to spend on it, but it adds a dependency for no Phase 0 benefit.
  NOTE: **The human debug path becomes PC → USB-serial → UART → Pico → SPI → the Helium debug agent.** That differs from the target path, which inserts an embedded RP2040 probe for chassis-closed recovery ([sheet D2](sec_ai_d2)) — but **the EC-as-interpreter role is preserved, and that is the part being exercised**.
- P1.6 — **System RAM is served from SDRAM, not from BRAM, and this is the decision the rig is worth building for.** The XC6SLX16 has 72 KB of BRAM, which is enough to hold system RAM entirely on-chip. That is rejected. Serving 65816 reads and writes from SDRAM exercises PHI2 stalling under real row-activation, precharge and refresh timing — **the mechanism from which it follows that cycle counting no longer measures time**, and therefore that a fixed frequency reference readable by software and not derived from PHI2 is required. A BRAM-backed rig would never surface it (→ [D76](sec_ai_q#d76)).
  NOTE: Consequence: BRAM is limited to the text framebuffer, the font ROM, the boot ROM and controller FIFOs ([P1.13](sec_ai_p1#p113)).
- P1.7 — **The SD register block is the transferable artefact, and the RTL is not.** The SPI master itself is trivial and will be rewritten for iCE40. The register map is not: `SPI_DATA`, `SPI_STATUS`, `SPI_CTRL`, `SPI_DIV`, `SPI_CS`, the ready/busy flag semantics and the CPU-side access protocol transfer verbatim into Helium, and the SD driver and FAT32 reader written in 65816 assembly against that map transfer without recompilation (→ [D77](sec_ai_q#d77), [sheet G](sec_ai_g)).
  NOTE: [[!blocking]] **The register map is specified in its own design note before any Phase 0 RTL is written.** It is the artefact; the rig is how it was discovered.
- P1.8 — **Helium has two distinct SPI roles and documentation never refers to "Helium's SPI" without qualification.** Helium is an SPI **slave** to the EC, for configuration and the mailbox, discriminated by chip-select; and an SPI **master** to the SD card. Phase 0 exercises the master plus the mailbox and debug slave, and does not exercise the configuration slave at all (→ [D78](sec_ai_q#d78)).
- P1.9 — **Text mode geometry is 80 × 30 at 640 × 480.** With an 8 × 16 font that consumes the full active area exactly. The pixel clock is **25.000 MHz**, a divide-by-two of a 50 MHz oscillator, which needs no DCM and no fractional synthesis; monitors tolerate it in place of the nominal 25.175 MHz. Framebuffer: 80 × 30 × 2 = 4800 bytes (→ [D79](sec_ai_q#d79)).
  NOTE: **This is the one geometry decision that does not transfer.** [P1](sec_ai_p2#p1) runs a 128 × 32 buffer displayed as an 80 × 30 window, because [D44](sec_ai_q#d44) keeps one console driver across both boards. The rig's 80 × 30 is the screen, not a window into anything.

## Budgets

- P1.10 — **Pin budget — 53 header pins, and the count the board actually offers has not been taken.** SDRAM is routed on the Spartan-6 board itself and consumes no header pins.

| Block | Signals | Count |
|---|---|---|
| A — CPU bus | A0–A15 | 16 |
| | D0–D7, bank multiplexed | 8 |
| | PHI2, RWB, VDA, VPA, RDY, RESB, IRQB, NMIB, BE, MX, E | 11 |
| B — VGA | HSYNC, VSYNC, R[1:0], G[1:0], B[1:0] | 8 |
| C — SD | SCLK, MOSI, MISO, CSN | 4 |
| D — EC link | SCLK, MOSI, MISO, MBOX_CSN, DBG_CSN, HELIUM_ATTN | 6 |
| **Total** | | **53** |

- P1.11 — [[!blocking]] **How many I/O the board exposes on headers is not known, and it blocks the HAT layout.** If fewer than 53 usable pins are available, block B and block D can be time-separated across two HAT revisions — but that has to be established before layout rather than discovered during it (→ [Q148](sec_ai_q#q148)).
- P1.12 — **Six bits of colour, as everywhere else in this project.** Two bits per channel here against six on the carrier's R-2R ladder ([P2.08](sec_ai_p2#p208)) — a rig running text has no use for the other twelve pins, and Mode 0 has no palette. The 18-bit palette rule of [P2.16](sec_ai_p2#p216) starts at Phase 1 and is untouched by this.
- P1.13 — **BRAM budget — ~19 KB of 72, and the headroom is not the binding constraint.** The XC6SLX16 has 32 × 18 Kbit = 576 Kbit = 72 KB.

| Use | Size |
|---|---|
| Text framebuffer, 80 × 30 × 2 | 4.7 KB |
| Font ROM, 256 glyphs × 16 lines | 4 KB |
| Boot ROM | 8 KB |
| SDRAM controller FIFOs | ~2 KB |
| **Subtotal** | **~19 KB** |
| Spare | ~53 KB |

  NOTE: **The binding Phase 0 resource is expected to be logic, not memory** — 2,278 slices. Track slice utilisation from the first synthesis run. If the combined design exceeds the device, Neon Mode 0 and the Helium-side blocks split across two bitstreams and are tested separately ([P1.16](sec_ai_p1#p116)).
  NOTE: The same abundance trap as [P2.18](sec_ai_p2#p218), one device earlier and milder: 72 KB here, 243 KB on the carrier, **16 KB on each target HX8K**. A buffer sized against what is spare on the rig is a buffer that does not exist on the machine.

## What transfers, and what does not

- P1.14 — **Recorded so that Phase 0 output is not mistaken for rehearsal.** These four are non-transferable by construction, and knowing that in advance is what stops the rig from being over-invested in.
  NOTE: **All Phase 0 RTL.** Written for Spartan-6 under ISE — UCF constraints, DCM_SP and PLL_BASE clocking, Xilinx primitives — none of which has an equivalent in the iCE40 and ECP5 open flow. **The architecture transfers; the source does not.**
  NOTE: **The USB HID keyboard firmware.** The target keyboard is expected to be an internal matrix scanned by the EC ([L1](sec_ai_p4#l1)), which is a different problem end to end (→ [Q147](sec_ai_q#q147)).
  NOTE: **The FPGA configuration path.** JTAG on Spartan-6, SPI slave on iCE40 ([D61](sec_ai_q#d61)).
  NOTE: **The debug probe topology.** There is no embedded RP2040 probe in Phase 0 ([P1.5](sec_ai_p1#p15)).
- P1.15 — **What does transfer is most of the value.** The SD register map ([P1.7](sec_ai_p1#p17)), the mailbox and debug agent wire protocols and their command sets, the EC firmware structure, the 65816 boot monitor, the SD and FAT32 driver, every simulation testbench, and the measured SDRAM bandwidth figure. **All of it is protocol, firmware or software — which is the same thing the carrier buys one device later, and the reason both are worth building.**

## Abandonment conditions

Written down in advance, because a rig that gates nothing is a rig that can be stopped without a meeting.

- P1.16 — **If slice utilisation exceeds the XC6SLX16 even after splitting into two bitstreams**, Neon Mode 0 is dropped from Phase 0 and text output is deferred to Phase 1. The rig retains its value for SDRAM, the mailbox, the debug agent and SD.
- P1.17 — **If the carrier reaches fabrication before Phase 0 is complete**, the remaining S-series work is abandoned in place and folded into Phase 1. Phase 0 exists to use calendar time that would otherwise be idle; **it does not gate Phase 1**.
- P1.18 — **If ISE 14.7 proves unworkable in the available VM environment, Phase 0 is abandoned entirely rather than mitigated.** The rig is not worth toolchain archaeology, and [P1.1](sec_ai_p1#p11)'s concession only holds while it is cheap.

## Rig stages · S0–S11

Sequential, each gated on the one before it, consistent with the single-thread preference of [P4.4](sec_ai_p4#p44). **S0–S3 are FPGA training and have standalone value regardless of what follows.**

- [ ] S0 — **Blinky with a DCM** — validates the clock and the full ISE flow end to end.
  TEST: the bitstream loads over JTAG, the device ID reads back correct, and DCM lock is observable on a pin rather than inferred.
- [ ] S1 — **UART transmit** — the first observability the rig has.
  TEST: a known string arriving at a terminal on the USB-serial adapter, at the intended baud, with no framing errors over a sustained run.
- [ ] S2 — **SDRAM controller** — init, refresh, single-word read and write.
  TEST: march, walking-ones and address-in-address patterns across the whole device, with refresh proven by leaving data in place for minutes rather than milliseconds.
- [ ] S3 — **SDRAM burst, and the bandwidth measured rather than assumed.**
  TEST: achieved bandwidth reported against the ~150 MB/s effective figure [T1.13](sec_ai_t1#t113) budgets against, and the number written down where the next phase can find it.
- [ ] S4 — **65816 bus interface, with the CPU executing from the BRAM boot ROM.**
  TEST: bus cycles captured and classified, VPA and VDA decoding confirmed against expected fetch patterns, native mode entry verified.
- [ ] S5 — **PHI2 stalling, with system RAM moved to SDRAM** ([P1.6](sec_ai_p1#p16)).
  TEST: the stall observed on a scope against a row activation and against a refresh, bounded in both cases, and the CPU resuming correctly from each.
  NOTE: This is the stage the rig exists for. Everything before it is training; everything after it is protocol work that could in principle have waited.
- [ ] S6 — **Neon Mode 0** — VGA text output at 640 × 480, hardware scroll, the two-byte command port.
  TEST: a correct 80 × 30 character grid on a monitor, scrolled by exactly one row on command, with no SDRAM anywhere in the path.
- [ ] S7 — **Pico socket populated, and the mailbox protocol running.**
  TEST: the mailbox command set exercised end to end from the host console, with the wire protocol matching [sheet D3](sec_ai_d3) rather than a rig-local variant.
- [ ] S8 — **Debug agent** on its own chip-select.
  TEST: `DBG_ID` reading `$6516` in one frame, then physical read and write against SDRAM with the CPU held in reset ([R.8](sec_ai_r#r8), [R.22](sec_ai_r#r22)).
- [ ] S9 — **SD register block, card init and sector read** — the map of [P1.7](sec_ai_p1#p17) before the driver written against it.
  TEST: a sector read back byte-for-byte against the same sector read on a PC.
- [ ] S10 — **FAT32 reader in 65816 assembly**, against the register map and not against the hardware.
  TEST: a file located by name and read out through the UART, matching the host's copy.
- [ ] S11 — **USB HID keyboard host, and an interactive monitor prompt.**
  TEST: a key pressed on the keyboard echoes to the screen through the CPU, with the rig self-hosting for inspection and no PC attached beyond power.
