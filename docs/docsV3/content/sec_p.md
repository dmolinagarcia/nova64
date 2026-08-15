# Step-by-step build · E0–E8
> small steps · verifiable tests · golden milestones

Nine stages, each with concrete hardware/gateware/software and a verification criterion before moving on. E0 and E1 are broken out below, because everything before a working board exists is where the project actually is today: the pin budget is closed, all six schematic sheets are designed *as documents*, and nothing has been drawn in KiCad. Live detail in `plan-implementacion-65816.md`.

![Fig. 7 — The E0–E8 path with both milestones at their exact point: Apple II on closing E6, Amiga on closing E8.](figures/fig-7-stages.svg)

- [ ] E0.1 — **Toolchain.** KiCad 8, oss-cad-suite (Yosys + nextpnr-ice40 + IceStorm), pico-sdk, and a simulator matching the HDL choice.
  TEST: a simulated iCE40 blinky and a compiled RP2040 "hello".
- [ ] E0.2 — **HDL decision** — Verilog or VHDL. Not a preference: it decides which reference softcore is usable at all (→ [Q2](sec_q#q2)).
- [ ] E0.3 — **Project KiCad library.** Symbol and footprint for every component, verified one at a time.
  TEST: each footprint printed 1:1 on paper and laid against the physical part.
- [~] E0.4 — **Panel.** Candidate fixed and its controller verified — HX8282, 24-bit TTL at 3.3V with no level shifters, DE mode by default, modeline ~51.2 MHz for 1344×635 → 60 Hz, data latched on the *falling* DCLK edge so gateware changes it on the rising one. Still missing the *module* datasheet (→ [Q1](sec_q#q1)).
- [x] E0.5 — **Pin budget across the three TQ-144 parts** — closed, it fits. This is what proved the architecture buildable, and what forced the shared-bus topology when it did not.
- [ ] E0.6 — **Freeze the block diagram** as the schematic's hierarchical reference.
  NOTE: Currently at REV B and well behind this document (→ [A.7](sec_a#a7)).
- [~] E1.1 — **Six hierarchical schematic sheets**, designed simplest to hardest: 1.1 power · 1.2 RP2040 / EC · 1.3 FPGA configuration · 1.4 Helium, bus and memories · 1.5 Neon, panel and audio · 1.6 Argon and connectors.
  NOTE: All six exist as design documents; none is drawn in KiCad yet. Sheet 1.5 is held by the panel datasheet.
- [ ] E1.2 — **Cross-review before layout.**
  TEST: clean ERC, plus a manual pin-by-pin pass of every TQFP against its datasheet — twice, on different days.
- [ ] E1.3 — **Layout.** Stackup decided against the router's difficulty, PLCC socket centred with the FPGAs around it and bus traces under ~10 cm, decoupling hard against the pins, continuous ground plane.
  TEST: clean DRC and gerbers checked in an external viewer, not in KiCad's own.
- [ ] E1.4 — **Staged population — the golden rule:** never populate a block until the previous one works. The board is designed for this, with a test point and an LED per rail and 0Ω jumpers to isolate every branch.
- [ ] E1.5 — **Power alone.**
  TEST: 3V3 within ±2% and 1V2 within ±3% under dummy load · battery charging with correct STAT lines · every branch measured in isolation through its jumper.
- [ ] E1.6 — **RP2040 alone.**
  TEST: enumerates as USB-CDC and answers on the console · mounts the microSD and lists files · reports I2C telemetry · generates a measurable backlight PWM, with no FPGA populated.
- [ ] E1.7 — **The three FPGAs.**
  TEST: the EC configures each one separately with a blinky · CDONE high on all three · reconfiguring Neon without disturbing Helium demonstrated.
  NOTE: The debug agent's `DBG_ID` reading `$6516` belongs to this stage too — one SPI frame that proves link, bitstream and Helium clock at once (→ [R.8](sec_r#r8)).
- [ ] E1.8 — **CPU alive — free-run.** Bus forced to NOP, PHI2 from Helium.
  TEST: the address counter advances consistently on the analyser · an LED blinks from a decoded address.
- [ ] E2 — **SRAM + serial monitor.** SRAM on the bus; BIOS preload by the RP2040; UART console; peek/poke monitor.
  TEST: console echo · read/write arbitrary memory · checksum of the loaded BIOS.
  NOTE: The peek/poke does not have to wait for a BIOS: the debug agent gives physical read/write and a march test with the CPU still in reset, which is where most of its value is (→ [R.22](sec_r#r22)).
- [ ] E3 — **SDRAM.** Controller (adapted open-source candidate) as a DMA/paging engine behind Helium, with auto-refresh interleaved into the fill state machine.
  TEST: pseudorandom pattern over the full 64 MB with no errors for hours · refresh holds during sustained fills.
- [ ] E4 — **MMU + cache + protection.** Walker, TLB with ASID, 4-way cache with tags in EBR, PHI2 stall with BE=0 and its watchdog, ABORTB, FAULT_* registers at `$FF`.
  TEST: an illegal access triggers ABORT with correct FAULT_ADDR/CAUSE · a cache miss stalls and resumes cleanly · a deliberately hung fill trips the watchdog instead of freezing the board · measured cache hit rate.
  NOTE: The debug agent reaches its full extent here — halt, step and trace over the `BE` handoff, virtual-mode access, and `TLB_PROBE` against `PTWALK` to catch a stale entry. The same stage owes it one negative test: the fill watchdog must be suppressed while the CPU is halted, or every session ends in a spurious abort (→ [R.17](sec_r#r17)).
- [ ] E5 — **Video + audio.** Neon on the bus with its own SDRAM, `$FE` window; VGA pattern → framebuffer; tone over I2S. ((Start below the gateware: the panel's own **BIST** mode generates test patterns with no external clock and no bitstream, validating panel, FPC and backlight in isolation before anything can be blamed on logic.))
  TEST: BIST patterns on the panel · stable VGA image · a CPU write shows up on screen · clean 440 Hz tone.
- [ ] E6 — **SD + minimal OS + HID.** SD handoff, block driver, FAT, kernel load, serial shell, basic USB keyboard.
  TEST: power on → prompt, with no PC connected.

!!! APPLE II MILESTONE — the machine boots on its own to a prompt.

- [ ] E7 — **Multitasking.** Scheduler with a 100 Hz tick, syscalls via COP, two processes with MMU isolation.
  TEST: two concurrent processes; one dies from a violation and the other stays intact.
- [ ] E8 — **Laptop + GUI.** 10.1” panel + GT911 + backlight, battery + charging, chassis; window compositor over `/dev/fb`.
  TEST: touch GUI session running on battery.

!!! AMIGA MILESTONE — preemptive multitasking with a windowed GUI.

- [?] + — Optional extensions outside the critical path: populating Argon (FPGA-C) with the softcore · demand paging + swap to SD · eDP panel variant (ANX6345) as a future revision.
