# DN-SW-EMU-001 — noVa64 Host Emulator

**Domain:** SW (software)
**Topic:** EMU (host-side system emulator)
**Revision:** A (draft)
**Status:** Active
**Language:** English

---

## Revision history

| Rev | Status | Summary |
|-----|--------|---------|
| A   | Draft  | Initial issue. Establishes purpose, fidelity model, parameterised timing model, boot model, debug parity requirement, validation strategy, build order, open items, abandonment conditions. |
| A (amended, unissued) | Draft | SDRAM page-open abandonment condition changed from automatic to explicit revision. §6 broadened from debug parity to host-facing interfaces: adds console transport, HID input model at the mailbox boundary, and host capture hotkey. §5 updated: mailbox is now modelled as an interface. Two new blocking open items recorded (pointer device semantics, target-side character device identity). |
| A (amended, unissued) | Draft | Pointer device resolved: the target has a mouse; the touch panel is discarded. §6.3 restrictive touch model withdrawn; step 7 unblocked. Pointer open item narrowed to event encoding and transport. §6.4 capture elevated to functional requirement under a relative-motion protocol. Cross-domain consequences of GT911 removal noted as out of scope. |

---

## 1. Purpose

The noVa64 host emulator is a PC-hosted functional and timing model of the noVa64
system. Its primary purposes, in priority order, are:

1. **Software development platform.** Provide a target for kernel, OS and GUI
   development that is available before, and independently of, working hardware.
   Removes the E0–E5 prototype bring-up gates from the critical path of K0–K5 and
   G0–G8.
2. **Design parameter exploration.** Provide quantitative answers to memory
   subsystem design questions (cache geometry, TLB depth, command emission cost)
   that are not yet fixed in any design note, and which must be fixed before RTL
   is written.
3. **Specification validation.** Act as a second, independent implementation of
   the DN corpus. Divergence between the emulator and hardware indicates a defect
   in one of the two implementations *or* an ambiguity in the specification. All
   three outcomes are useful.

Purpose 2 is a consequence of the emulator existing before the RTL, and is
treated here as a first-class goal rather than a side effect.

---

## 2. Non-goals

The following are explicitly out of scope for this document and for the
emulator:

- **EC (RP2354B) emulation.** No Cortex-M33 core, no pico-sdk firmware
  execution, no SPI slave configuration sequence, no power sequencing. The
  emulator begins execution in the state that exists immediately after the EC
  releases CPU reset. See §5.

  Note the boundary: the **mailbox interface as Helium presents it to the CPU is
  modelled exactly** (§6.3), because guest software reads input through it. What
  is excluded is the EC core behind that interface. The emulator occupies the
  EC's position at the mailbox and originates traffic directly.
- **Gateware simulation.** The emulator does not execute RTL. Verilator/GHDL
  co-simulation against the emulator as golden reference is a possible future
  activity but is not part of this document and is not required for any gate.
- **Cycle-accurate video timing.** No raster-level modelling, no scanline
  effects, no mid-frame register latching semantics. See §3 and §4.4.
- **Analogue and electrical behaviour.** No signal integrity, no power rails, no
  charger, no touch controller, no panel timing.
- **Bit-accurate audio timing.** Audio is modelled functionally at buffer
  granularity only.

---

## 3. Fidelity model

Fidelity is selected per subsystem according to a single criterion: **model what
software running on the machine can observe.** Anything unobservable to software
is modelled at the coarsest granularity that preserves the observable result.

| Subsystem | Functional fidelity | Timing fidelity | Rationale |
|---|---|---|---|
| W65C816S core | Exact, including decimal mode, emulation mode, all addressing modes, page/bank crossing penalties | Cycle-stepped, bus-cycle granularity | Every 65816 cycle is a bus cycle; a per-cycle state machine costs little more than a per-instruction interpreter and provides the exact hook point for MMU, cache and PHI2 stall. |
| MMU / page table walk | Exact per DN memory model: 2 KB pages, 8192 entries/process, ASID-tagged TLB, hardware walk, ABORTB semantics | Parameterised cost model (§4.2) | Directly observable by the kernel. Fault and protection semantics must be exercised by real kernel code. |
| Cache controller | Exact hit/miss determination | Parameterised cost model (§4.2) | Cache *contents* are not software-visible except through timing and explicit maintenance operations; cache *geometry* drives the timing that is being measured. |
| SRAM (IS61WV102416) | Byte-accurate array, region-partitioned (cache data / page tables / pinned) | Fixed per-access latency parameter | Asynchronous SRAM; latency is a constant, not a state machine. |
| System SDRAM (Helium) | Byte-accurate array | Parameterised burst/latency model (§4.2), optional page-open modelling | Fill latency is the quantity under investigation. |
| Bus arbitration (Helium) | Modelled as contention for the SDRAM port | Parameterised | Contention affects predicted fill latency under concurrent access. |
| NEON blitter | **Bit-exact**: 4 channels, 256-minterm logic, 2-position shifter, edge masks, descending mode, 8-deep command queue | Cost model per command, accumulated per frame | Pixel results are directly software-observable and must match hardware exactly. Blitter *timing* is not observable because there is no raster-synchronised software. |
| NEON compositor (Model C) | Exact: per-window backing stores, damage tracking, damage-limited recomposition, full-recomposition fallback | Frame granularity | Compositor correctness is observable; sub-frame timing is not. |
| Hardware cursor | Exact overlay semantics | Free (EBR-resident, off the composite path) | Matches hardware intent. |
| NEON SDRAM | Byte-accurate array, separate from Helium SDRAM | Bandwidth accounting only | Physically separate device; no contention with CPU fills. |
| Command interface (CPU → NEON) | Exact encoding | **Cycle-accurate emission cost** | Identified as NEON's binding constraint. This is the single most important timing measurement the emulator must produce. |
| Audio | Functionally exact sample generation | Buffer granularity | Not latency-critical for the milestones. |
| SD card (Helium-attached) | Block device backed by a host image file | Fixed latency parameter | Sufficient for NVFS and ext2 layer development. |
| Mailbox (Helium side) | Exact register set, protocol and event semantics as seen by the CPU | Fixed latency parameter | Guest software reads keyboard and pointer input through this path; divergence here would be invisible until hardware bring-up. |
| HID input (keyboard, mouse) | Exact event encoding as delivered over the mailbox | Delivery latency parameter | Host captures physical input and injects it; see §6.3. Mouse semantics (relative motion, buttons, hover) are fixed; encoding is open. |
| Character device (console) | Exact guest-side register interface, identical to that used by the monitor | Fixed latency parameter | Only the host-side transport is emulator-specific; see §6.2. |
| Debug Agent | Exact command set parity (§6) | Not modelled | Tooling reuse is the goal, not fidelity. |

---

## 4. Timing model

### 4.1 Principle

The emulator maintains a PHI2 cycle counter as the master time base at 8 MHz
nominal. All costs are expressed in PHI2 cycles or converted to them. The model
is **parameterised**: every latency, geometry and bandwidth figure below is a
runtime-configurable value, not a compiled-in constant.

Rationale: none of these values is fixed in the DN corpus at the time of
writing. Making them configurable converts the emulator from a consumer of
design decisions into a tool for making them.

### 4.2 Parameter set

Grouped by subsystem. Defaults are placeholders pending §10.

**Cache**

- `cache.size` — total data capacity (default 1 MB, per SRAM partition)
- `cache.line_bytes` — line size (default 32)
- `cache.ways` — associativity (default 2)
- `cache.policy` — replacement policy (LRU / pseudo-LRU / random)
- `cache.write_policy` — write-through / write-back
- `cache.fill_setup_cycles` — fixed overhead per fill, PHI2 cycles
- `cache.hit_cost_cycles` — cost of a hit above the baseline access (default 0)

**TLB**

- `tlb.entries` — number of entries
- `tlb.ways` — associativity
- `tlb.asid_bits` — ASID width
- `tlb.miss_walk_accesses` — SRAM accesses per hardware walk
- `tlb.walk_overhead_cycles` — fixed walk overhead beyond the accesses

**SRAM**

- `sram.access_ns` — access time (default 10 ns)
- `sram.port_width_bits` — 16

**SDRAM (Helium)**

- `sdram.clock_mhz` — FPGA-side SDRAM clock (default 100)
- `sdram.cas_latency` — CL2 / CL3
- `sdram.trcd`, `sdram.trp`, `sdram.trc` — in SDRAM clocks
- `sdram.burst_len` — halfwords per burst
- `sdram.model_page_open` — boolean; if false, every access pays full ACT+CAS
- `sdram.refresh_model` — off / averaged / exact

**NEON command interface**

- `neon.cmd_bytes` — bytes per command (default 12)
- `neon.emit_cycles_per_cmd` — measured/assumed CPU cycles to emit one command
  (working assumption 50)
- `neon.queue_depth` — 8
- `neon.blit_bytes_per_sec` — blitter throughput for cost accounting

**Contention**

- `arb.sdram_port_sharers` — which agents contend for the Helium SDRAM port
- `arb.policy` — arbitration policy model

### 4.3 First-order estimate to be tested

The following estimate motivates the parameter sweep and is recorded here as a
**hypothesis, not a result**.

Assuming a 32-byte line, a ×16 SDRAM at 100 MHz with CL3, tRCD 3, and a 16-beat
burst:

```
ACT (tRCD)        ~3 SDRAM clocks
CAS latency       ~3
burst             16
overhead/precharge ~5
                  --------
total             ~27 SDRAM clocks ≈ 270 ns
```

At 8 MHz, one PHI2 cycle is 125 ns. A cache line fill therefore costs on the
order of **2–3 PHI2 cycles**.

Separately, page tables reside in the SRAM partition (~10 ns access), not in
SDRAM. A hardware page table walk is a small number of SRAM accesses and may
therefore cost **less than one PHI2 cycle** — potentially free.

If both hold, the memory hierarchy is only weakly visible to software, and the
dominant timing term in the system is NEON command emission rather than cache or
TLB behaviour. This inverts the usual ranking of concerns and, if confirmed,
should be recorded and allowed to influence cache geometry selection (a smaller,
simpler cache may be sufficient).

The purpose of the parameterised model is to confirm or refute this. It must not
be assumed true in advance of measurement.

### 4.4 Video timing

The compositor and blitter are advanced at frame granularity. Within a frame the
emulator accumulates blitter and command costs; if accumulated cost exceeds the
frame budget, the frame is reported as over-budget with the overrun quantified.
No attempt is made to model when within the frame each operation occurred.

This is valid because Compositor Model C performs no raster-synchronised work and
no software mechanism exists to observe sub-frame video state.

---

## 5. Boot model

The emulator starts in the post-EC state:

1. Host-side configuration file selects an SD card image, a `bios.bin` file, and
   a parameter set (§4.2).
2. `bios.bin` is loaded into the modelled system memory at the address the EC
   `MEM_WRITE` mailbox sequence would have placed it.
3. Helium and NEON models are initialised to their post-configuration reset
   state.
4. CPU reset is released; execution begins at the reset vector.

No SPI configuration traffic and no power sequencing is modelled.

The **runtime mailbox is modelled**, as an interface rather than as an emulated
EC. The emulator originates mailbox traffic directly — HID events during normal
operation (§6.3), and scripted stimulus for testing. The Helium-side register
set and event semantics must match the specification exactly; the SPI transport
beneath them is not modelled.

---

## 6. Host-facing interfaces

### 6.0 Governing principle

For every interface in this section, the **guest-side interface is
hardware-real** and only the **host-side transport is emulator-specific**. The
emulator must never present the guest with a device, register or event semantic
that has no counterpart in the target machine. Violating this produces software
that runs under emulation and fails on hardware — the exact failure mode this
document exists to prevent.

Host-only facilities that have no guest visibility at all (§6.1, §6.4) are
permitted and are marked as such.

### 6.1 Debug interface parity

**Requirement:** the emulator exposes the same command set as the Helium Debug
Agent (DBG_CSN), as specified in DN-HW-ECIF-001 Rev B and its successors.

Consequence: the monitor, host tooling and any scripted test harness are written
once and run unmodified against both the emulator and real hardware.

This requirement is stated now rather than retrofitted because the Debug Agent
command set is currently being extended (see DN-HW-ECIF-001 open items). Any
command added to the Agent is added to the emulator in the same revision; any
command that cannot be meaningfully emulated is documented as such rather than
silently stubbed.

The emulator additionally exposes a **superset** of host-only facilities that
have no hardware counterpart and are clearly marked as such: full state dump,
deterministic record/replay control, parameter set reload, and instrumentation
readout (§7).

### 6.2 Console

The console is presented to the host in two forms, both driven by the *same*
guest-side device:

- **Attached terminal.** The emulator's own terminal window, available with no
  setup. Default path for step 5 monitor work.
- **Virtual serial port.** The same character stream exposed as a host PTY
  (Linux/macOS) or named pipe / virtual COM port (Windows), so that standard
  terminal software — PuTTY, minicom, screen — can attach. Path is reported at
  startup and configurable.

**The guest side of this device is the character device the monitor already
uses** (build order step 5), not an invented UART. The emulator adds no guest
registers to support serial attachment; PuTTY and minicom see a host transport
wrapped around an existing target device, and any software written against it
runs unmodified on hardware.

The identity of that target-side character device is an open item (§10). Until it
is fixed, the emulator's character device is a placeholder and any guest software
depending on its register layout is provisional.

Line discipline, baud rate and flow control are **not** modelled. The transport
is byte-transparent; baud emulation would model a constraint the target does not
have.

### 6.3 Input model

Physical keyboard and pointer input is captured by the host, translated into the
target's HID event encoding, and delivered to Helium over the modelled mailbox
(§5). This mirrors the hardware path, in which the EC is the origin of HID
traffic; the emulator occupies the EC's position without emulating it.

Requirements:

- Event encoding, ordering and mailbox notification semantics are **exact**.
  Guest software must not be able to distinguish emulator-originated input from
  EC-originated input.
- Delivery latency is a parameter, defaulting to a nominal fixed value.
- Host key events are mapped to target scancodes through an explicit, editable
  mapping table, not through host character codes. Host keyboard layout must not
  leak into the guest.
- Events are recorded and replayed as part of the determinism requirement (§8).
  Input is the principal source of non-determinism in interactive sessions.

**Pointer semantics are fixed: the target has a mouse.** The touch panel is
discarded. The emulator therefore models a pointing device with relative motion,
multiple buttons and a meaningful hover state, and host mouse events pass through
without restriction. The full mouse semantics available to the GUI — hover
highlighting, pull-down menus, secondary-button actions — are hardware-real and
may be relied upon.

Two aspects remain open (§10) and are narrower than the original question:

- **Event encoding and physical transport.** Whether the mouse reaches the EC by
  USB host, PS/2, or an I2C trackpad determines the byte-level event format the
  emulator must reproduce exactly. Semantics are fixed; encoding is not.
- **Position accumulation.** Whether the EC integrates motion and delivers
  absolute cursor position, or delivers deltas for the kernel to integrate. This
  is a mailbox protocol decision with a direct consequence for §6.4.

### 6.4 Host capture hotkey

Because the emulator window competes with the host for keyboard and pointer
input, a **capture toggle** is required.

Under a relative-motion pointer protocol this is not a convenience but a
**functional requirement**: with the pointer uncaptured, the host reports
absolute window coordinates, which cannot be converted into a coherent delta
stream once the host cursor reaches a screen edge. Guest cursor motion would
silently stop while the guest remained otherwise responsive. If §10 resolves in
favour of absolute position delivery, capture reverts to a convenience.

- A configurable host hotkey grabs and releases keyboard and pointer.
- Capture state is displayed unambiguously (window title and on-screen
  indicator); pointer capture additionally hides the host cursor so that only the
  guest's hardware cursor is visible.
- The hotkey itself is never forwarded to the guest, and must be chosen so that
  it does not collide with a plausible target shortcut.
- Release must remain possible if the guest has hung. The hotkey is handled by
  the host event loop, independently of guest execution state.
- The hotkey is a **host-only facility** with no guest visibility. No mailbox
  event is generated on capture or release, since no such event exists in
  hardware.

---

## 7. Instrumentation

The emulator's value for purposes 1 and 2 depends on what it measures. Required
counters, readable at any point and dumpable per run:

- Instructions retired; PHI2 cycles elapsed; effective IPC
- Cache accesses, hits, misses, evictions; miss rate by region
- Cycles stalled on fill
- TLB accesses, hits, misses; walk count; cycles spent walking
- ABORTB events by cause (unmapped, permission, copy-on-write, kernel)
- Page faults per process, by ASID
- Context switches; cycles per context switch
- Syscall count and cycle cost, by COP signature byte
- NEON commands emitted; cycles spent emitting; queue-full stall cycles
- Blitter operations, bytes moved, cost accumulated
- Frames composited; damage-limited vs full recomposition; over-budget frames
- SD block reads/writes
- Mailbox transactions by class; HID events delivered; input-to-response latency
  in PHI2 cycles
- Console bytes in/out

Two measurements are called out as primary deliverables of the emulator:

1. **Cycles per NEON command emitted**, under realistic GUI workloads, and the
   improvement obtained from the server-side string cache and pre-built
   per-window SDRAM command sublists. These optimisations are currently
   first-order priorities on the basis of an estimate; the emulator can test the
   estimate before RTL exists.
2. **Sensitivity of GUI and kernel workloads to cache geometry**, obtained by
   parameter sweep. Output is a recommended geometry for the Helium cache
   controller, to be recorded in the relevant hardware DN.

---

## 8. Validation strategy

**CPU core.** Validated against external test suites, which are the only part of
the system for which a third-party oracle exists.

- Tom Harte / SingleStepTests processor tests (65816 set) — per-instruction
  state and bus-cycle traces.
- Klaus Dormann functional tests — 6502 emulation mode coverage.

Availability and current coverage of both suites must be verified before they are
relied upon; if the 65816 set proves insufficient, the shortfall is documented
and covered by hand-written tests.

**MMU, cache, blitter.** No external oracle. Validated against the DN corpus by
directed tests written from the specification text. Where the specification is
ambiguous, the ambiguity is raised as a DN revision rather than resolved silently
in code.

**Blitter.** The 256-minterm logic, shifter, edge masks and descending mode are
validated exhaustively where the input space permits (minterm coverage is
tractable: all 256 functions against a small set of source patterns).

**NVFS and ext2.** The existing NVFS differential testing and crash-injection
harness runs against the emulator's SD block device unmodified. `e2fsck -fn`
remains the ext2 oracle.

**Determinism.** Record/replay of all non-deterministic inputs is implemented
from step 1 of §9, not added later. Cost is low at the start and high afterwards,
and deterministic replay is the primary tool for debugging a preemptively
multitasking kernel.

---

## 9. Build order

Sequential, consistent with the single-thread plan. Steps are inserted into the
v3 plan ahead of K0, since the emulator unblocks kernel work without depending on
E0–E5.

| Step | Deliverable | Exit criterion |
|---|---|---|
| 1 | 65816 core, cycle-stepped, flat 16 MB, no MMU or cache. Record/replay infrastructure. | External test suites pass. |
| 2 | Memory map layer with per-access hook; SRAM/SDRAM region model. | Regions correctly decoded; hook fires on every access. |
| 3 | Parameter set loader and instrumentation counters (§4.2, §7). | Parameters settable at run time; counters readable. |
| 4 | MMU: hardware walk, ASID-tagged TLB, ABORTB. Cache controller with cost model. | Directed tests from DN memory model pass; counters plausible. |
| 5 | Character device; console attached terminal and virtual serial transport (§6.2). Mailbox interface. Debug Agent command parity (§6.1). | Monitor runs under the emulator, reachable from PuTTY/minicom. **Emulated "Apple II milestone."** |
| 6 | SD block device; NVFS harness runs against emulator. | Existing NVFS test suite passes unmodified. |
| 7 | NEON command interface, blitter (bit-exact), backing stores, damage tracking, frame-granularity compositor, host window output. HID input over mailbox (§6.3), capture hotkey (§6.4). | Windowed GUI composites correctly and responds to keyboard and mouse. **Emulated "Amiga milestone."** Event encoding provisional until §10 resolves. |
| 8 | Parameter sweep campaign; produce cache geometry recommendation and NEON emission cost report. | Reports issued; feeding DNs revised. |

---

## 10. Open items

Blocking full timing fidelity (not blocking steps 1–3):

- **Cache geometry not specified in any DN.** Line size, associativity,
  replacement policy, write policy. Currently emulator parameters with placeholder
  defaults; step 8 is expected to produce the recommendation.
- **TLB depth, associativity and ASID width not specified.**
- **SDRAM controller clock frequency and burst configuration not fixed.**
- **Bus arbitration policy for the Helium SDRAM port not specified**, including
  which agents contend and with what priority.
- **NEON command emission cost of ~50 cycles is an estimate**, not a measurement.
  It is a parameter until step 8 measures it.
- **Debug Agent command set is still being extended**; parity per §6 must track
  it.
- **65816 external test suite coverage unverified** — availability and
  completeness of the 65816 sets must be confirmed at step 1.

Blocking specific build order steps:

- **Pointer event encoding and transport not specified. Blocks step 7
  completion, not step 7 start.** Semantics are fixed (mouse: relative motion,
  buttons, hover). The byte-level mailbox event format depends on the physical
  attachment — USB host on the EC, PS/2, or I2C trackpad — which is undetermined.
  A USB host implementation on the RP2354B consumes either the native controller
  in host mode or a PIO block, and should be costed against the EC pin and PIO
  budget before selection.
- **Pointer position accumulation not specified.** EC integrates motion and
  delivers absolute position, versus EC delivers deltas and the kernel
  integrates. Determines whether §6.4 capture is a functional requirement or a
  convenience, and affects cursor latency under load.
- **Target-side character device identity not specified. Blocks step 5
  completion.** No user-facing UART appears in the DN corpus; the only documented
  UART is probe↔EC. The console's guest-side register interface must be fixed
  before software depends on it (§6.2). Note this is a hardware-side
  specification gap surfaced by the emulator, not an emulator design question.

Out of scope for this document, recorded because the emulator surfaced them:

- **Discarding the touch panel removes the GT911** and its dedicated I2C bus.
  That bus existed specifically to prevent AON backfeed into a powered-down
  domain; the constraint and the associated EC pins are released. Sheet 1.1 REV C
  and the EC pin budget require revision. The panel part number should also be
  checked for a digitiser variant, which would change the FPC and the part
  itself.

Recorded but not blocking:

- **RTL language selection: Verilog recommended** over VHDL, on toolchain
  grounds (Yosys/nextpnr is Verilog-native; VHDL requires the GHDL plugin path;
  Verilator is Verilog-only). This is not an emulator decision but is recorded
  here because §2's exclusion of co-simulation is easier to revisit if the RTL is
  Verilog.

---

## 11. Abandonment conditions

- **Verilator/GHDL co-simulation** is not adopted unless a specific defect class
  emerges that the emulator cannot isolate. It is not scheduled and requires no
  further document.
- **Exact SDRAM refresh modelling** is dropped without reopening this document if
  averaged refresh accounting proves indistinguishable in the step 8 sweep.
- **Page-open/page-close SDRAM modelling** may be dropped if §4.3 is confirmed —
  that is, if fill cost proves small enough in PHI2 cycles that page state cannot
  materially affect software behaviour. **This requires an explicit revision of
  this document**, recording the measured fill cost and the sweep conditions
  under which it was obtained. It is not automatic.

  Rationale for the exception: every other abandonment condition in this corpus
  is predicated on a fixed constraint knowable at the time of writing (layout
  permits the pads or it does not). This one is predicated on a future
  measurement, and a measurement can be wrong, unrepresentative, or taken under
  parameters that later change. The evidence must be on the record.
- **Audio emulation** is deferred indefinitely if it is not required by any G-gate.
- **Cache geometry parameterisation** collapses to fixed constants once the
  hardware geometry is committed to a DN; the sweep infrastructure may be
  retired at that point.

---

## 12. References

- DN-HW-ECIF-001 Rev B — EC interface, mailbox, Debug Agent
- DN-HW-DEBUGPORT-001 — debug port and probe topology
- DN-FS-VFS-001 (pending) — VFS specification
- DN-FS-EXT2-001 — ext2 read-only interoperability layer
- Plan v3 (99 steps), gate series E0–E5 / G0–G8 / K0–K5
