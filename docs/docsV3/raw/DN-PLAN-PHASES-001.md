# DN-PLAN-PHASES-001 — Three-Phase Development Strategy

**Status:** Draft (Rev A)
**Domain:** PLAN (new domain code — see §9, Convention Notes)
**Supersedes:** none
**Related:** DN-HW-ECIF-001 Rev B, DN-HW-DEBUGPORT-001, DN-SW-EMU-001

---

## Revision History

| Rev | Date | Change |
|-----|------|--------|
| A | 2026-09-09 | Initial draft. Defines Phase 0 (Spartan-6 training rig), Phase 1 (Colorlight prototype), Phase 2 (iCE40 target). |

---

## 1. Purpose

noVa64 development currently has one hardware platform in the plan: the Colorlight i9 v7.2 prototype on a custom carrier, followed by the iCE40 target hardware. The Colorlight carrier is a complex board and will take time to design and fabricate.

A Xilinx Spartan-6 XC6SLX16 board with 2.54 mm vertical headers is already available. A HAT for it — CPU socket, Pico socket, SD socket, VGA — is trivial and cheap to fabricate. This creates an opportunity to begin exercising system-level protocol and software work before the Colorlight carrier exists.

This note defines the three-phase structure, fixes the scope of Phase 0, and records explicitly which Phase 0 artefacts are intended to transfer forward and which are disposable.

---

## 2. Phase Definitions

### Phase 0 — Spartan-6 Training Rig

**Platform:** Xilinx Spartan-6 XC6SLX16-FTG256 board + custom HAT
**Toolchain:** Xilinx ISE 14.7 (WebPACK, VM image)
**Status:** proposed

Not part of noVa64. A disposable rig whose purpose is to de-risk protocol and software work that would otherwise be attempted for the first time on the Colorlight carrier.

### Phase 1 — Colorlight Prototype

**Platform:** Colorlight i9 v7.2 (LFE5U-45F ECP5) on custom carrier
**Toolchain:** Yosys / nextpnr / IceStorm equivalents for ECP5
**Status:** as previously planned; unchanged by this note

Helium and NEON instantiated as two separate top-level modules with declared interface and separate PLL-derived clock domains.

### Phase 2 — iCE40 Target

**Platform:** Helium (FPGA-A) and NEON (FPGA-B), iCE40 TQ144
**Status:** as previously planned; unchanged by this note

Physical separation of Helium and NEON reduces to serializer insertion plus pin reassignment.

---

## 3. Phase 0 Scope — In

| Item | Rationale |
|------|-----------|
| SDRAM controller (HY57V561620FTP-H) | JEDEC SDR protocol is identical to the target AS4C32M16 / IS42S16320. Only column width differs (9 bits vs 10). |
| 65816 bus interface | Address/data/control decode, bank latching, VDA/VPA cycle qualification. |
| PHI2 stalling against real SDRAM latency | Exercises the "PHI2 always stretched on memory wait" invariant under real row-activation and refresh timing. |
| EC mailbox protocol | Wire protocol and firmware, independent of FPGA family. |
| Debug Agent protocol | Separate SPI slave (DBG_CSN); wire protocol and command set. |
| NEON Mode 0 — text only | Character buffer internal to NEON, hardware scroll, two-byte command port. |
| SD via hardware SPI master in the FPGA | Register block; SD init, sector read, FAT32 reader in 65816 assembly. |
| USB HID keyboard on the Pico | Rig usability only — see §5. |

## 4. Phase 0 Scope — Out

| Item | Reason |
|------|--------|
| FPGA configuration over SPI slave | iCE40-specific (SPI slave / NVCM, no JTAG). Spartan-6 configures over JTAG. Not reproducible. |
| MMU, page-table walk, ASID-tagged TLB | Deferred to Phase 1. |
| Cache controller | Deferred to Phase 1. |
| Blitter, graphics modes, compositor | Deferred to Phase 1. NEON Mode 0 only. |
| Power sequencing, charger, fuel gauge | No equivalent on the rig. |
| Embedded RP2040 debug probe | No chassis; debug is direct. See §5. |
| Audio / I2S | Not required for the Phase 0 objectives. |
| RP2354B in its production package | See §5. |

---

## 5. Decisions

### D-1 — Use a Raspberry Pi Pico 2, not a bare RP2354B

The RP2354B is QFN-80 with in-package flash; putting it on the HAT requires designing and fabricating a breakout. Nothing in the Phase 0 scope depends on the package: the mailbox and Debug Agent are protocol and firmware, and pico-sdk is identical across the family. The 2354B-specific properties — 48 GPIO, in-package flash, A4 stepping — are integration questions the rig cannot answer regardless.

Bootrom A/B partition table support (an open item blocking schematic capture) can be verified on a Pico 2: it is the same bootrom.

**Consequence:** the Pico socket on the HAT is a standard Pico 2 footprint.

### D-2 — Debug over UART, not USB CDC

A USB HID keyboard requires the Pico to act as USB **host**. The Pico has a single USB port and cannot simultaneously be a host for the keyboard and a CDC device to the PC.

Resolution: USB port serves the keyboard (TinyUSB host); debug console runs over UART to a USB-serial adapter.

Rejected alternative: Pico-PIO-USB for a second host port. Workable — RP2350 has three PIO blocks — but adds a dependency and complexity for no Phase 0 benefit.

**Consequence:** the human debug path in Phase 0 is PC → USB-serial → UART → Pico → SPI → Helium Debug Agent. This differs from the target path (which inserts an embedded RP2040 probe for chassis-closed recovery), but the EC-as-interpreter role is preserved, which is the part being exercised.

### D-3 — System RAM served from SDRAM, not BRAM

The XC6SLX16 has 72 KB of BRAM, enough to hold system RAM entirely on-chip. This is rejected.

Serving 65816 reads and writes from SDRAM exercises PHI2 stalling under real row-activation, precharge and refresh timing. This is the mechanism from which the consequence follows that cycle counting no longer measures time — and therefore that a fixed frequency reference readable by software, not derived from PHI2, is required. A BRAM-backed rig would never surface it.

**Consequence:** BRAM allocation is limited to text framebuffer, font ROM, boot ROM and controller FIFOs (see §7).

### D-4 — SD register block is the transferable artefact

The SPI master RTL is trivial and will be rewritten for iCE40. The register map is not: `SPI_DATA`, `SPI_STATUS`, `SPI_CTRL`, `SPI_DIV`, `SPI_CS`, the ready/busy flag semantics, and the CPU-side access protocol transfer verbatim into Helium. The SD driver and FAT32 reader written in 65816 assembly against that map transfer without recompilation.

The register map shall be specified in its own design note before Phase 0 RTL is written.

### D-5 — Naming: Helium has two distinct SPI roles

Helium is an SPI **slave** to the EC (configuration and mailbox, discriminated by chip-select) and an SPI **master** to the SD card. Phase 0 exercises the master plus the mailbox/debug slave; it does not exercise the configuration slave (§4).

Documentation shall never refer to "Helium's SPI" without qualification.

### D-6 — Text mode geometry: 80×30 at 640×480

640×480@60 with an 8×16 font yields exactly 80×30 characters and consumes the full active area. Pixel clock is 25.000 MHz — a divide-by-two of a 50 MHz oscillator, requiring no DCM and no fractional synthesis. Monitors tolerate this in place of the nominal 25.175 MHz.

Framebuffer: 80 × 30 × 2 bytes = 4800 bytes.

---

## 6. Phase 0 Pin Budget

SDRAM is routed on the Spartan-6 board and consumes no header pins.

| Block | Signals | Count |
|-------|---------|-------|
| A — CPU bus | A0–A15 | 16 |
| | D0–D7 (bank multiplexed) | 8 |
| | PHI2, RWB, VDA, VPA, RDY, RESB, IRQB, NMIB, BE, MX, E | 11 |
| B — VGA | HSYNC, VSYNC, R[1:0], G[1:0], B[1:0] | 8 |
| C — SD | SCLK, MOSI, MISO, CSN | 4 |
| D — EC link | SCLK, MOSI, MISO, MBOX_CSN, DBG_CSN, HELIUM_ATTN | 6 |
| **Total header pins** | | **53** |

**Prerequisite:** the number of I/O the board actually exposes on headers has not been counted. If fewer than 53 usable pins are available, Block B (VGA) and Block D (EC link) can be time-separated across two HAT revisions, but this should be established before HAT layout.

---

## 7. Phase 0 BRAM Budget

XC6SLX16: 32 × 18 Kb = 576 Kb = 72 KB.

| Use | Size |
|-----|------|
| Text framebuffer (80×30×2) | 4.7 KB |
| Font ROM (256 glyphs × 16 lines) | 4 KB |
| Boot ROM | 8 KB |
| SDRAM controller FIFOs | ~2 KB |
| **Subtotal** | **~19 KB** |
| Spare | ~53 KB |

Headroom is large. The binding Phase 0 resource is expected to be logic (2,278 slices), not memory. Slice utilisation to be tracked from the first synthesis run; if the combined design exceeds the device, NEON Mode 0 and the Helium-side blocks can be split across two bitstreams and tested separately.

---

## 8. Explicitly Non-Transferable

Recorded so that Phase 0 output is not mistaken for rehearsal:

- **All Phase 0 RTL.** Written for Spartan-6 under ISE; UCF constraints, DCM_SP/PLL_BASE clocking and Xilinx primitives have no equivalent in the iCE40/ECP5 open toolchain. The architecture transfers; the source does not.
- **USB HID keyboard firmware.** The target keyboard is expected to be an internal matrix scanned by the EC. See OQ-1.
- **FPGA configuration path.** JTAG on Spartan-6, SPI slave on iCE40.
- **Debug probe topology.** No embedded RP2040 probe in Phase 0.

Transferable: the SD register map (D-4), the mailbox and Debug Agent wire protocols and command sets, the EC firmware structure, the 65816 boot monitor, the SD/FAT32 driver, all simulation testbenches, and the measured SDRAM bandwidth figure.

---

## 9. Open Questions

| Ref | Question |
|-----|----------|
| OQ-1 | Does the target design use USB HID host for the mouse? If yes, Phase 0 keyboard host work transfers directly and should be scoped accordingly. If the mouse is PS/2 or otherwise non-USB, the keyboard remains rig-utility only. |
| OQ-2 | How many I/O does the Spartan-6 board expose on headers? Blocks the HAT layout (§6). |
| OQ-3 | Is `PLAN` an acceptable new domain code, or should this note be filed under an existing domain? |
| OQ-4 | Target PHI2 for Phase 0. Proposal: bring up at 1 MHz, raise toward 8 MHz once SDRAM stalling is stable. |
| OQ-5 | Does the Phase 0 mailbox command set include `MEM_WRITE`? The target uses it to push `bios.bin`; in Phase 0 the boot ROM is in BRAM, so it is exercisable but not required. |

---

## 10. Abandonment Conditions

- If Phase 0 slice utilisation exceeds the XC6SLX16 even after splitting into two bitstreams, NEON Mode 0 is dropped from Phase 0 and text output is deferred to Phase 1. The rig retains value for SDRAM, mailbox, Debug Agent and SD.
- If the Colorlight carrier reaches fabrication before Phase 0 is complete, remaining Phase 0 work is abandoned in place and folded into Phase 1. Phase 0 exists to use calendar time that would otherwise be idle; it does not gate Phase 1.
- If ISE 14.7 proves unworkable in the available VM environment, Phase 0 is abandoned entirely rather than mitigated. The rig is not worth toolchain archaeology.

---

## 11. Sequencing Within Phase 0

Sequential, consistent with the single-thread plan preference. Each step gated on the previous.

1. Blinky with DCM — validates clock and full ISE flow
2. UART transmit — first observability
3. SDRAM controller: init, refresh, single-word read/write
4. SDRAM burst; measure achieved bandwidth against the ~140–170 MB/s NEON estimate
5. 65816 bus interface; CPU executes from BRAM boot ROM
6. PHI2 stalling; system RAM moved to SDRAM (D-3)
7. NEON Mode 0 — VGA text output, hardware scroll, two-byte command port
8. Pico socket populated; mailbox protocol
9. Debug Agent
10. SD register block; card init; sector read
11. FAT32 reader in 65816 assembly
12. USB HID keyboard host; interactive monitor prompt

Steps 1–4 are FPGA training and have standalone value regardless of what follows.
