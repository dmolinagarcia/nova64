# DANI-65816 — Handoff: Display Subsystem for 14"+ Panel

**Date:** 2026-08-11
**Scope:** Re-evaluation of the video output path after raising the target panel size from 10.1" to ≥14".
**Status:** Architecture decision reached (ANX6345 bridge). Panel selection and schematic capture still open.

---

## 1. Why this conversation happened

The previously settled display path was a 10.1" panel (ER-TFT101-1, FPC-50, HX8282) driven **directly by RGB TTL** from FPGA-B, with pixel doubling (512×300 internal framebuffer → 1024×600 output).

Requirement changed: the target is now a panel of **at least 14 inches**.

This invalidates the direct RGB-TTL assumption. Parallel RGB-TTL interfaces are effectively limited to panels of roughly 7–10". Panels of 14"+ are laptop panels, and laptop panels use either **LVDS** (pre-~2013 generation) or **eDP** (everything manufactured since ~2014).

---

## 2. Options evaluated

### 2.1 RGB TTL direct — RULED OUT
No 14" panel accepts parallel RGB TTL. Path is dead for this size class.

### 2.2 LVDS direct from FPGA-B — VIABLE, NOT SELECTED
- Targets 14–15.6" laptop panels of the LVDS era, typically 1024×768 or 1366×768.
- ECP5 supports native 7:1 output serialization (ODDRX71 primitives) designed exactly for this. 4–5 differential pairs, no external chip.
- 1366×768 @ 60 Hz ≈ 72 MHz pixel clock — comfortable margin.
- Connector typically 30/40 pin. Backlight commonly requires 12–19 V (separate boost + inverter).
- **Downside:** restricts the machine to obsolete panels with poor availability going forward.

### 2.3 Native eDP generation from FPGA — VIABLE, DEFERRED TO PHASE 2
Analyzed in detail. Requirements:
- **PHY:** eDP is 1–2 differential lanes at 1.62 or 2.7 Gbps with 8b/10b coding and AC coupling. Generic ECP5 I/O cannot reach this — the DDR 7:1 gearing tops out around 800 Mbps–1 Gbps (enough for LVDS, not eDP). **Hard SerDes is mandatory.**
- **FPGA change required:** LFE5U-25F has no SerDes. Would need **LFE5UM or LFE5UM5G**, which carry the hard four-port PCS/SERDES block and are only available in caBGA381 — denser BGA, almost certainly a 6-layer board.
- **Toolchain:** ECP5-5G SerDes is supported by the open flow (Yosys/nextpnr/Trellis), but there is a substantial documentation gap between Lattice's docs and what the open tools expose; low-level DCU configuration must be done by hand.
- **Link layer gateware to implement:** stream scrambling, DP framing (BS/BE symbols, transfer units with stuffing to match video bandwidth to link bandwidth), periodic MSA (Main Stream Attributes) insertion. Large but deterministic FSM work.
- **AUX channel:** a second sub-project. Bidirectional half-duplex 1 Mbps Manchester-coded differential pair. Required to read panel DPCD, read EDID, and run **link training** (TPS1 clock recovery → TPS2 symbol alignment, with drive-level negotiation via DPCD read/write at each step). Without completed training the panel shows nothing.
- **Mitigating factors for a fixed laptop panel:** DPCD can be read once and the training result hardcoded; RBR (1.62 Gbps) single-lane is sufficient for 1366×768; none of the genericity needed for external monitors applies.
- **Precedent:** Mike Field's ("Hamster") open DisplayPort core (VHDL, Artix-7 transceivers) proves one person can do it — months of work plus serious scope time.
- **Verdict:** excellent Phase 2 project on a daughterboard with LFE5UM5G. Not the critical path.

### 2.4 Closed scaler board (RTD2556) — ACCEPTABLE FALLBACK, NOT SELECTED
Commercial monitor scaler boards, sold bare for €10–25.
- **Inputs:** HDMI only, or HDMI+VGA, depending on variant. Some modern boards add USB-C with DisplayPort Alt Mode (single-cable operation). Many include 3.5 mm audio in and amplified speaker out (audio extracted from HDMI).
- **Power:** typically 12 V barrel jack; some variants run from 5 V USB. Note that a 14" backlight boost draws meaningful current — do not assume a modest 5 V rail suffices.
- **Outputs:** 30-pin eDP connector (standard panels) or 40-pin on touch-capable versions (pins 1–30 panel, 33–40 touch), plus a PWM-dimmable LED backlight driver.
- **Firmware dependency:** resolution support is determined by the firmware flashed to the board. Sellers request the exact panel model at purchase time. **Not plug-and-play universal.**
- **Control mechanisms:**
  1. 5-button OSD keypad (brightness, contrast, volume, language). Buttons are simple GPIO — could be driven electronically from FPGA-A or the RP2040, but it is a blind, stateless method.
  2. **DDC/CI over I²C** — the interesting one. The RTD2556 supports an I²C/DDC-CI interface: the I²C channel embedded in the SDA/SCL pins of the HDMI/VGA connector (same lines used for EDID reads). MCCS protocol runs on top, providing standard brightness/contrast commands and often input selection. FPGA-A already has an I²C master, so this integrates cleanly. Same mechanism `ddcutil` uses on Linux.
  - **Caveat:** on cheap boards, DDC/CI availability depends on the flashed firmware. The silicon supports it; verify with the board in hand (`ddcutil detect` from a PC) before integrating.

### 2.5 RTD2556 as a bare chip on our own PCB — RULED OUT
The RTD2556 is a Realtek multifunction display controller in LQFP156 — hand-solderable, so the package is not the obstacle. The **firmware is**:
- It contains an internal 8051 MCU executing firmware from SPI flash. All scaling, eDP negotiation, OSD and DDC/CI live in that firmware.
- Firmware development requires Realtek's SDK, released to manufacturers under NDA. The datasheet itself is marked Realtek Confidential.
- Without a toolchain, we would be limited to flashing third-party binaries extracted from other boards, tied to specific panels, with nothing adjustable.
- **Functionally redundant:** a scaler exists to adapt arbitrary input timings. FPGA-B can generate the panel's native timing directly. We would be paying for a black box that solves a problem we do not have.

---

## 3. DECISION: Analogix ANX6345 bridge

**Selected path:** `FPGA-B → parallel RGB bus → ANX6345 → 14" eDP panel`

The correct chip class for our case is a **transmitter bridge**, not a scaler. The ANX6345 converts LVTTL RGB output into eDP or DisplayPort, is configured over I²C, and executes link training autonomously in hardware.

### Why it fits
- Input is exactly what FPGA-B already produces for the RGB-TTL connector: 24-bit (or 18-bit) RGB + HSYNC/VSYNC/DE/PCLK.
- Keeps FPGA-B as the existing LFE5U-25F in BG256 — no SerDes, no BGA381, no 6-layer board.
- Opens the entire modern panel catalogue (14" FHD IPS, €30–50).
- Backlight brightness stays under our own PWM control, directly from the FPGA — finer control than DDC/CI would give.
- **Open reference designs exist:** used in the Pine64 Pinebook and the Olimex TERES-I open-hardware laptop, whose full KiCad schematics are public and serve as a direct reference design.

### Directional warning
The **Chrontel CH7511B** appears frequently in searches but runs the opposite direction — it receives eDP and outputs LVDS, intended for the panel side, not the video-generator side. Do not confuse the two.

---

## 4. ANX6345 integration details

Source of truth used: the U-Boot driver `drivers/video/bridge/anx6345.c` by Vasily Khoruzhick (423 lines, GPL-2.0+). This is **bare-metal I²C with no DRM framework**, which makes it a near-direct template for RP2040 firmware.

Register map is shared with the ANX9804. Although the official datasheet does not circulate freely, the `anx98xx-edp.h` headers in U-Boot/Linux document every required register with descriptive names — in practice, sufficient open documentation.

### Electrical
| Item | Value |
|---|---|
| I²C addresses | **Two consecutive 7-bit addresses**: 0x38 (DP TX registers) and 0x39 (system registers) |
| Supplies | 1.2 V digital core (`dvdd12`), 2.5 V (`dvdd25`) |
| Control | Active-low reset GPIO |
| Input | Parallel RGB (18 or 24 bit) + PCLK / HSYNC / VSYNC / DE |
| Output | 1–2 eDP lanes, AC-coupled with 100 nF series caps per line, plus AUX pair |
| Backlight | Separate — our own boost + PWM. Driver's `set_backlight` returns `-ENOSYS` (not handled by the chip) |

### Initialization sequence (distilled from the driver, ~40 register writes)
1. Bring up rails, release reset → soft reset via register, wait 100 ms.
2. General power-up (`POWERD_CTRL = 0`); read chip ID — must return **0x63**.
3. Poll for stable clock, then three fixed analog configuration writes (PLL control, analog debug, link debug).
4. Force HPD; power up lanes; zero the training-set registers for all 4 lanes.
5. Reset AUX channel; power down audio and HDCP (unused).
6. **Read panel EDID via I²C-over-AUX** (device address 0x50) — the chip acts as the bridge; we only read its registers.
7. Set color depth (6-bit → 0x00, 8-bit → 0x10 in `VID_CTRL2`); read max link rate and max lane count from panel DPCD (0x001, 0x002 masked with 0x1f); write both into the link BW / lane count registers.
8. **Trigger link training**: one write to `LINK_TRAINING_CTRL`, then poll until the busy bit (0x80) clears — tens of ms. *This is the decisive advantage: all TPS1/TPS2 training we would have had to implement in gateware runs in hardware here.*
9. Enable video (`VID_CTRL1`: VID_EN + EDGE), then force stream valid (`SYS_CTRL3`: F_HPD | HPD_CTRL | F_VALID | VALID_CTRL).

### Architectural placement
Run this sequence **on the RP2040 at power-on, before the W65C816S is released from reset** — mirroring the role of an embedded controller in a real laptop. When the CPU wakes, the panel is already trained and waiting for pixels.

Since the panel is fixed and known, the modeline can be hardcoded in FPGA-B gateware, with the EDID read used only as a boot-time sanity check.

**Known tuning point:** the `EDGE` bit in step 9 selects which PCLK edge latches the data. Expect to verify this empirically once the board exists (symptom of a wrong setting: image shifted by one pixel).

---

## 5. Open items

**Blocking:**
- **Panel selection.** Concrete 14" eDP panel must be chosen to fix resolution, bandwidth, lane count, and connector pinout (30-pin vs 40-pin touch variant).
- Consequence for the memory/video architecture: a 14" FHD or 1366×768 panel changes the framebuffer bandwidth budget versus the previous 1024×600 pixel-doubled design. Internal framebuffer resolution and scaling factor must be re-derived once the panel is fixed.

**Next design steps:**
- ANX6345 schematic block: decoupling, 1.2 V / 2.5 V rail generation, reset GPIO routing, AC-coupling caps, differential pair routing rules.
- **Power sequencing** for the eDP panel: VDD → signal → backlight ordering, with panel-specific timing requirements.
- Backlight boost design for a 14" panel (higher current than the 10.1" PT4110 sizing).
- FPGA-B pin budget check: parallel RGB (18 vs 24 bit) plus sync signals, against remaining I/O after SDRAM and other functions.
- Decide whether the existing HDMI differential pairs remain as external output only.

**Superseded by this conversation:**
- ER-TFT101-1 10.1" panel path and its blocking datasheet question are no longer on the critical path.
- Touch input: previously GT911 over I²C on the 10.1" panel. Must be re-specified for the chosen 14" panel (laptop panels are usually non-touch; a touch variant implies the 40-pin eDP connector or a separate USB/I²C touch layer).

**Deferred to Phase 2:**
- Native eDP generation in FPGA (LFE5UM5G daughterboard). Retains its appeal as an owned-stack achievement; not on the critical path.

---

## 6. Decision summary

| Option | Cost | Effort | Ownership of stack | Verdict |
|---|---|---|---|---|
| RGB TTL direct | — | — | Full | Impossible at 14" |
| LVDS direct from ECP5 | €0 chips | Weeks | Full to connector | Viable, obsolete panels only |
| **ANX6345 bridge** | **~€5** | **Weeks** | **Up to bridge** | **SELECTED** |
| Closed RTD2556 board | €10–25 | Days | None | Fallback only |
| RTD2556 bare chip | ~€10 | Blocked | None (NDA firmware) | Ruled out |
| Native eDP in FPGA | €30–40 + 6-layer | Months | Total | Phase 2 |
