# noVa64 — Full Execution Roadmap (v3, sequential)

**From zero to a 100% functional portable noVa64.**

One strictly linear sequence. Each step depends only on the steps before it and
is executed one at a time. Gate tags from the project's existing conventions
(**E0–E5** prototype bring-up, **K0–K5** kernel, **G0–G8** GUI, **P0–P8**
Phase 2 hardware) are retained as progress markers, but they no longer imply
concurrent tracks.

---

## 0. Notes on this revision

### Corrections carried over from v1

| # | Correction |
|---|---|
| C-1 | **Debug channel split in two.** E2 requires trace-buffer capture at CPU attach, so a raw SPI trace/access channel lands at step 21; it is re-routed through the cache controller for coherence at step 37. v1 had a single agent, placed too late and routed through a cache that did not yet exist. |
| C-2 | **SDRAM arbiter made explicit** (step 33). One controller shared by Helium and NEON, with wait-time counters instrumented from the start, so the E4 histograms have a source. |
| C-3 | **Resolution staged.** 640×480 text from EBR at E1; 1024×600 at E4. |
| C-4 | **Ethernet PHYs** confirmed held in reset and current draw measured (step 7). |
| C-5 | **KiCad library verification early** (step 11), before any board is designed. |

### Consequence of going sequential

The **Stage B carrier PCB moves into the main line, at steps 16–18** — between
E1 and E2. In the parallel version it sat in a side track; with one thread of
execution it has to be an explicit stop, because E2 attaches the CPU at 8 MHz
and that needs controlled impedance on the bus, not wiring.

The cost of sequential execution is calendar time: the Phase 2 board is not
designed until the GUI is finished, so fabrication and assembly lead times land
at the end rather than overlapping the G-series. The benefit is one context at
a time and no half-finished subsystems. Accepted deliberately.

### Open decisions (confirm or reject)

**D-A — The Apple II milestone stays at E5**, as originally defined. An unnamed
checkpoint at steps 27–31 reaches a standalone prompt in text mode, with no MMU
and no graphics, for the early debug and morale payoff, without redefining the
milestone.

**D-B — E5 and G1 overlap on the compositor.** Interpretation used: E5 requires
only the measured proof that a full-screen recomposite fits the frame budget on
real hardware (step 64); the compositor as a software component is G1 (step 67).

---

## Tooling

**1.** Repository and documentation skeleton — English docs, decision log, layout for gateware / firmware / kernel / hardware / docs.
→ *One place where every artifact lands.*

**2.** Both FPGA toolchains verified end to end: Yosys + nextpnr-ecp5 + Project Trellis (prototype, ECP5) and Yosys + nextpnr-ice40 + IceStorm (target, iCE40); plus KiCad and pico-sdk.
→ *The Phase 2 port will not double as a toolchain learning exercise.*

**3.** Calypsi C and ca65 installed; calling convention and direct-page pseudo-register usage audited against compiled output.
→ *The known blocker for kernel work is cleared and documented.*

**4.** 65816 simulator harness with an I/O stub at the real bank `$FF` UART address.
→ *The same binary runs on simulator and hardware; later kernel bugs become separable from gateware bugs.*

**5.** CI building gateware, RP2040 firmware and 65816 binaries, running the simulator test suite on every commit.
→ *Regressions surface the day they are introduced.*

---

## E0 — Platform alive

**6.** Prototype BOM ordered: Colorlight i9 v7.2, W65C816S + PLCC-44 socket, IS61WV102416, RP2040 board, VGA connector, R-2R resistors, passives, headers.
→ *Long-lead items off the critical path.*

**7.** i9 blinky loaded via openFPGALoader; PLL lock confirmed; **Ethernet PHYs verified held in reset; board current draw measured**.
→ *Toolchain-to-silicon proven, and the module's unused subsystems are known to be quiet.*

**8.** RP2040 bring-up: LED, build/flash loop, USB-CDC console echo.
→ *A host console attached to the system.*

**9.** RP2040 ↔ FPGA SPI control link with a scratch register file in the FPGA.
→ *Host-side read/write of FPGA state — the debug backbone.*

**10.** SDRAM controller exercised by memory test across all 8 MB: walking ones, address-in-address.
→ *Main memory proven and characterized.*

**11.** KiCad project library verified symbol by symbol; footprints printed 1:1 and checked against the physical components.
→ *Library errors found before any board exists.*

**12.** External SRAM wired on temporary interconnect and validated by memory test.
→ *The asynchronous parallel interface and its pin mapping are proven.*

**E0 pass:** bitstream loads, PLL locks, console echoes, both memories pass, PHYs quiet.

---

## E1 — Console before memory

**13.** R-2R ladder assembled; timing generator producing colour bars at **640×480**.
→ *A monitor displays output from your gateware.*

**14.** Mode 0 text engine in EBR with font ROM, 128×32 grid.
→ *Characters render from a fixed buffer, entirely out of EBR.*

**15.** Text buffer writable from the host over SPI; console input echoes to screen.
→ *A working terminal that does not touch the memory controller.*

**E1 pass:** terminal operational with **no SDRAM access anywhere in the path**.

---

## Stage B carrier board

**16.** Stage B carrier schematic captured in KiCad using the **same symbols and net names intended for the target board**, hosting the SODIMM socket directly (bypassing the PMOD buffering question entirely).
→ *The most timing-sensitive section of the final design exists as a reviewed schematic.*

**17.** Four-layer carrier layout with controlled impedance on the CPU bus; DRC clean; fabrication outputs generated.
→ *A board that rehearses the hardest part of the target design where a respin is cheap.*

**18.** Carrier fabricated, assembled, and E0/E1 re-validated on it.
→ *All prior results reproduced on real PCB rather than temporary wiring.*

---

## E2 — CPU attached

**19.** *(Optional)* Stage A wire-wrap: 65816 with clock, reset, pull-ups; free-running NOP test on a logic analyzer at 1–2 MHz.
→ *The CPU is alive and its bus behaviour observed directly, before the carrier is trusted.*

**20.** Helium skeleton: PLL, reset sequencer, PHI2 generation, bus-cycle FSM, bank-byte latch, VPA/VDA decode.
→ *Every CPU bus cycle correctly framed and classified.*

**21.** Trace buffer in EBR plus a **raw** SPI debug channel with direct access to EBR and SRAM — no cache in the path.
→ *Bus cycles are capturable the moment the CPU is attached, as E2 requires.*

**22.** CPU boots from EBR-hosted memory with real reset vectors; native mode entry verified.
→ *The CPU executes your code from memory you control.*

**23.** Bank `$FF` I/O decode with a write-only LED register.
→ *The privileged I/O bank exists and the CPU can command peripherals.*

**24.** Helium and NEON instantiated as two separate top-level modules with a declared interface, separate PLL-derived clock domains and CDC — inside the single i9.
→ *Partitioning discipline in place from the first commit; later separation costs a serializer, not a rewrite.*

**25.** NEON text aperture writable by the CPU across the declared interface.
→ *The CPU prints to the screen.*

**26.** CPU-visible UART in bank `$FF` bridged to the RP2040 USB-CDC.
→ *Bidirectional interactive I/O between CPU code and your terminal.*

**E2 pass:** free-run NOP executes, trace buffer readable, VPA/VDA decode confirmed, native mode entered.

---

## Text prompt *(checkpoint — see D-A)*

**27.** crt0, user and kernel linker scripts, minimal C runtime; a C hello world printing over the UART register.
→ *The C toolchain reaches real hardware.*

**28.** Native 65816 monitor resident in SRAM: examine, deposit, go, disassemble.
→ *The machine is self-hosting for inspection, without the host.*

**29.** microSD boot path: RP2040 reads an image from the card into memory and releases the CPU.
→ *Boots from removable storage with no host attached.*

**30.** USB HID keyboard on the RP2040 delivered to a CPU-visible register.
→ *Typed characters reach CPU code.*

**31.** Text console driver (Mode 0 + keyboard) wired to the monitor.
→ *Power on, and the machine boots to its own prompt on its own display — text mode, no MMU, no graphics.*

---

## E3 — Translation active

**32.** External SRAM exposed to the CPU as raw uncached memory.
→ *Real off-chip memory usable by CPU code.*

**33.** **SDRAM arbiter**: one controller shared by Helium and NEON, with wait-time counters instrumented from the outset.
→ *Contention is measurable before anything depends on it.*

**34.** SDRAM reachable by the CPU through the arbiter, uncached.
→ *Large main memory addressable, still slow.*

**35.** Cache controller: 256-byte lines, write-back, SRAM as cache store, PHI2 stall (BE=0) for bounded fills, hit/miss counters.
→ *The CPU runs from SDRAM transparently at full speed, with measurable behaviour.*

**36.** Cache flush/invalidate commands with the BUSY polling protocol.
→ *Software can enforce coherence — prerequisite for DMA and the MMU.*

**37.** Debug agent re-routed through the cache controller; per-transaction watchdog; DEBUG_ENABLE strap.
→ *Host memory access is now coherent with the CPU's view.*

**38.** CPU halt / single-step / resume plus register readback.
→ *You can stop the machine and inspect it.*

**39.** RP2040 monitor front end over USB-CDC and physical UART: `load`, `go`, `dump`, `halt`, `step`, `trace`.
→ *A complete bring-up workbench; load and run binaries in seconds.*

**40.** ASID-tagged TLB in EBR operating with a single identity mapping.
→ *Every access is translated; behaviour unchanged, which proves the datapath.*

**41.** Hardware page-table walker reading 32-bit PTEs from the pinned SRAM region.
→ *Translations populated from real page tables in memory.*

**42.** ABORTB on unmapped pages and permission violations, with an assembly handler that reports, and **instruction resumption verified correct**.
→ *Faults are detected, reach software, and are recoverable.*

**43.** Context registers with the `CTX_SET_PTBASE` → `CTX_SET_ASID` ordering invariant enforced; two contexts switchable from the monitor.
→ *Two distinct 16 MB virtual address spaces coexist.*

**44.** Protection policy enforced: bank `$FF` privileged and never user-mappable, bank `$00` always pinned, bank `$01` resident kernel, vectors mapped identically in every context.
→ *The hardware half of the protection model is complete.*

**E3 pass:** the virtual memory system is real, not simulated.

---

## K0 — Kernel exists

**45.** Kernel skeleton in C: startup, vector table, direct-page conventions, IRQ dispatch.
→ *A kernel image that owns the machine.*

**46.** COP syscall dispatcher with `write` and `exit`, and the documented ABI (Calypsi convention + signature byte).
→ *The user↔kernel boundary exists as a contract.*

## K1 — Address spaces

**47.** Physical frame allocator and page-table construction routines.
→ *The kernel can build an address space.*

**48.** First user process loaded into its own virtual address space at reduced privilege.
→ *Isolation demonstrated: the user program cannot touch bank `$FF` or the kernel.*

## K2 — Multitasking

**49.** Timer IRQ, scheduler, assembly context switch.
→ *Two user processes run preemptively and concurrently.*

**50.** Demand paging and copy-on-write on top of ABORT.
→ *Lazy allocation and cheap sharing.*

## K3 — IPC

**51.** Ports, signals, messages.
→ *The substrate the window server will need.*

## K4 — Devices

**52.** Driver framework (init/read/write/ioctl/irq) with fd-based device paths; `/dev/tty`.
→ *A uniform device model; the console becomes a driver like any other.*

**53.** microSD block driver plus a read-only filesystem.
→ *Files on the card are visible to the OS.*

## K5 — Usable system

**54.** Program loader from the filesystem and a shell that launches programs.
→ *Type a name, a program runs in its own address space.*

**K5 pass:** multiple isolated processes, launched from storage, scheduled preemptively.

---

## E4 — Graphics

**55.** SDRAM framebuffer with 8bpp scanout at **1024×600**, palette lookup with 18-bit entries.
→ *A real graphical display, in the same pixel format as the production RGB path.*

**56.** Hardware cursor overlay in EBR.
→ *Pointer motion costs nothing in the composite path.*

**57.** Blitter, single channel, straight copy only.
→ *Rectangles move without CPU involvement.*

**58.** Full blitter: 4 channels, 256-minterm logic **verified against a reference software model**, barrel shifter, edge masks, descending mode, 8-deep queue.
→ *Sprites, masked blits and overlapping scrolls are all one primitive, provably correct.*

**59.** Command FIFO plus SDRAM command lists.
→ *NEON renders autonomously from lists the CPU builds once.*

**60.** Arbiter wait-time histograms captured under sustained blitter load.
→ *No scanout underrun, with evidence rather than assumption.*

**E4 pass:** stable scanout, verified blitter, cursor tracking, no underrun under load.

---

## E5 — System / **Apple II milestone**

**61.** Audio DMA to the PCM5102A over I2S, continuous playback without underrun.
→ *The machine makes sound reliably.*

**62.** `/dev/fb` with an ioctl mapping framebuffer pages into a process's virtual space.
→ *A user program draws pixels at memory speed.*

**63.** `/dev/audio`.
→ *Sound available through the ordinary device contract.*

**64.** Full-screen recomposite measured on hardware against the frame budget; damage-limited path demonstrated.
→ *The compositor's cost model confirmed on real silicon before G1 depends on it.*

**65.** **Apple II milestone declared:** kernel boots to a prompt on the prototype, standalone, with graphics and audio operational.

---

## G-series — GUI

**66. G0** — `neon` driver, aperture management, command-list builder.
→ *Static pattern on screen from a CPU-built list.*

**67. G1** — Compositor with N static surfaces and vblank-driven swap.
→ *8 rectangles composited, no tearing, hardware cursor tracks the mouse.*

**68. G2** — `wserver`: window create, destroy, move, raise.
→ *Live drag at 60 Hz with measured recomposite cost.*

**69. G3** — Ports, signals, input routing, focus.
→ *Two client tasks receive their own events.*

**70. G4** — Shared 4 KB command buffer, validation, operation set.
→ *A client draws into its own window and cannot draw outside it.*

**71. G5** — Font atlas, string cache, `text()`.
→ *Full text-pane repaint under 0.5 ms of CPU — the binding constraint defeated.*

**72. G6** — `tk`: widget tree, layout, button, label, checkbox.
→ *A working dialog with tab focus and keyboard activation.*

**73. G7** — Menus, listbox, scrollbar, textfield.
→ *A file-open dialog, end to end.*

**74. G8** — Desktop shell.
→ *Icons, drag-and-drop, program launch — **Amiga milestone**, on prototype hardware.*

---

## P-series — Phase 2 target hardware

**75. P0** — Gateware split into two independent builds, with the multiplexed 32-signal Helium↔NEON serializer replacing the wide internal interface.
→ *The interface contract survives physical separation inside the confirmed spare-pin budget.*

**76. P1** — Both halves ported to iCE40 HX8K on separate dev boards; LUT, EBR and Fmax measured against estimates.
→ *The target devices are proven sufficient — or resized before any PCB exists.*

**77. P2** — Full software stack running on the two-device configuration.
→ *Nothing depended on the single-FPGA prototype.*

**78.** Capacitive ER-TFT101-1 received; Q1 resolved — backlight string parameters, FPC strap exposure, GT911 connector.
→ *The blocking unknown for schematic capture is closed.*

**79. P3** — Panel bring-up on the carrier via FPC breakout: RGB TTL timing, backlight, final working resolution fixed.
→ *The real display works and all bandwidth figures are confirmed against it.*

**80. P4** — GT911 touch on its dedicated RP2040 I2C bus, events delivered to `wserver`.
→ *Touch integrated with the GUI.*

**81.** Power subsystem on eval hardware: CH224K PD sink, BQ25896 charger, TPS63020, dedicated core buck — each rail validated at target current with dummy loads.
→ *Every rail proven before layout commits to it.*

**82.** RP2040 power sequencer firmware: always-on domain, staged enables, fault handling, MAX17048 readout.
→ *Deterministic power-up and power-down, with the iCE40 core-before-IO ordering satisfied.*

**83.** Battery pack on eval hardware: protection, charge and discharge cycles, fuel gauge learning.
→ *The energy path is characterized before it is designed in.*

**84.** Footprints verified 1:1 for the target-only parts: power devices, panel FPC, touch connector, audio DAC, iCE40 TQ144.
→ *The library is complete and checked for every part on the target board.*

**85. P5** — Schematic capture sheet by sheet: power, CPU and bus, Helium, NEON, memories, RP2040, display and touch, audio, connectors.
→ *The whole machine as one reviewed schematic.*

**86. P6** — ERC clean, footprints assigned, BOM and DNP reviewed against the pin budget sheets.
→ *Manufacturable on paper.*

**87. P7** — Four-layer stackup defined, placement fixed, timing-critical nets routed first (CPU bus, SDRAM, parallel RGB).
→ *The hard routing done while there is still room to move things.*

**88. P8** — Power distribution and remaining routing complete, DRC clean, fabrication outputs generated.
→ *Ready to order.*

**89.** REV A fabricated and assembled in stages, power section first.
→ *Boards populated in a sequence that limits blast radius.*

**90.** Staged bring-up: rails, RP2040, FPGA configuration, memories, CPU, video, panel, touch, audio; errata list opened.
→ *Every subsystem verified on target hardware, defects documented.*

**91.** Full E + K + G stack running on REV A.
→ *The real board is a working noVa64.*

---

## L-series — Laptop integration

**92.** Internal keyboard matrix designed and scanned by RP2040 firmware, delivered through the existing input path.
→ *A built-in keyboard replaces USB HID.*

**93.** Integrated pointing device selected and driven into `wserver`.
→ *Pointer input without external peripherals.*

**94.** Software power management: rail gating, sleep/wake, battery status surfaced in the GUI.
→ *Battery life is a controllable, measurable property.*

**95.** Mechanical CAD: lid and hinge, mainboard mounting, battery bay, port cutouts, keyboard tray.
→ *A complete enclosure model.*

**96.** Printed enclosure prototype assembled; fit and cable-routing revisions applied.
→ *Everything physically fits and closes.*

**97.** Thermal profiling and battery-life characterization under sustained GUI load, in the enclosure.
→ *Real-use behaviour known and acceptable.*

**98.** REV B board correcting REV A errata; final assembly into the enclosure.
→ *A device you can pick up, open and use.*

**99.** Release: schematics, gateware, kernel, SDK, disk image, build instructions, user manual.
→ ***A 100% functional portable noVa64, documented and reproducible.***

---

## Deferred / optional

| Item | Insert after | Prerequisite |
|---|---|---|
| 14" panel + ANX6345 eDP bridge | 91 | Third board revision |
| FPGA-C softcore 65816 | 76 | Decide whether core frequency or memory bandwidth binds |
| Writable filesystem | 53 | Read-only path stable |
| Networking | 52 | Driver framework stable |
| 2bpp hires mode (N-4) | before 66 | Project identity decision — decide before drawing an icon |
| `DRAW_GLYPHS` in hardware (N-3) | before 71 | LUT headroom against ~1,900 spare; would remove the string cache |

## Blocking gates

- **3** blocks **45**. No kernel work before the Calypsi convention audit.
- **11** blocks **16**. No board design before the symbol library is verified.
- **44** requires the kernel virtual bank `$00` / pinned SRAM interaction across context switches to be formally resolved first.
- **NEON open items N-1 (blitter XOR/invert), N-2 (aperture paging), N-6 (interrupt line)** block **67**; N-6 is specifically blocking for the compositor.
- **76** blocks **85**. No layout before iCE40 resource budgets are measured.
- **78** blocks **85**. No schematic capture without the panel datasheet resolved.
