# The emulator's model of time
> a parameter for every latency · a stretched clock · and a cycle that stopped measuring time

The emulator keeps a PHI2 counter at 8 MHz nominal as its master time base and expresses every cost in it. What makes the model worth anything is that **not one of those costs is compiled in**: every latency, geometry and bandwidth figure is a runtime parameter, because none of them is fixed anywhere in this document. That choice is what turns the emulator from a consumer of design decisions into a tool for making them ([EM1.2](sec_ai_em1#em12)). The sheet ends with the boot state the model starts from, which is the state the EC leaves behind.

## Everything is a parameter, because nothing is decided.

- EM2.1 — **Costs are stated in nanoseconds and converted, not stated in cycles.** A cycle count is a derived quantity here, which is what makes [EM2.4](sec_ai_em2#em24) expressible at all — under a stretched clock a 20 ns access and a 270 ns fill differ by more than rounding, and a model that counted whole cycles could not tell them apart.
- EM2.2 — **The parameter set, grouped by subsystem.** Defaults are placeholders and are expected to be replaced by the sweep of [EM6.21](sec_ai_em6#em621).

| Group | Parameters |
|---|---|
| Cache | `size` · `line_bytes` · `ways` · `policy` (LRU / pseudo-LRU / random) · `write_policy` · `fill_setup_cycles` · `hit_cost_cycles` |
| TLB | `entries` · `ways` · `asid_bits` · `miss_walk_accesses` · `walk_overhead_cycles` |
| SRAM | `access_ns` (default 10) · `port_width_bits` (16) |
| SDRAM | `clock_mhz` (default 100) · `cas_latency` · `trcd` · `trp` · `trc` · `burst_len` · `model_page_open` · `refresh_model` (off / averaged / exact) |
| Neon command port | `cmd_bytes` (default 12) · `emit_cycles_per_cmd` (assumed 50) · `queue_depth` (8) · `blit_bytes_per_sec` |
| Contention | `sdram_port_sharers` · arbitration policy |
| PHI2 | `phi2_mhz` · `stall_model` (stretch / wait states) |

- EM2.3 — **The first-order estimate is recorded as a hypothesis and was not allowed to stand as a result.** At a 32-byte line on a ×16 SDRAM at 100 MHz with CL3 and tRCD 3, a fill is roughly `3 + 3 + 16 + 5 ≈ 27` SDRAM clocks, about **270 ns**, or 2–3 PHI2 cycles at 125 ns each. If that holds, the memory hierarchy is barely visible to software and the dominant term in the system is Neon command emission rather than cache behaviour — which inverts the usual ranking of concerns and would license a smaller, simpler cache. **The parameterised model exists to confirm or refute it**, and [EM6.1](sec_ai_em6#em61) reports what it found, including one correction.

## The clock is stretched — decided, and it is this document's own intent.

- EM2.4 — **PHI2 is halted until memory responds, always.** The machine stops its clock rather than free-running and holding the CPU on `RDY`. The other model remains selectable for comparison, and stretching is the default and the design intent. **Nothing here is new**: [E.4](sec_ai_e#e4) already licenses it from the fully static core, [D16](sec_ai_q#d16) already decided it for the cache-miss stall, and [T1.18](sec_ai_t1#t118) already requires it for a read of Neon's aperture. What the emulator adds is the price list.
- EM2.5 — [[!blocking]] **A cycle therefore no longer measures time, and that is a constraint on every piece of software written for this machine.** Nothing in the guest may schedule, time out or delay by counting cycles, because the duration of a cycle is now workload-dependent. Loop-counted delays are a standard idiom on 8-bit machines and every one of them is wrong here.
- EM2.6 — **So the machine needs a fixed-frequency counter software can read, and it must not derive from PHI2.** The emulator implements a free-running microsecond counter, read low byte first, with that read latching the upper three so that a rollover between two reads cannot yield a value that never existed. **This is a hardware requirement created by a timing decision**, and it is the clearest example in the corpus of the emulator earning its keep before any silicon exists (→ [EM6.16](sec_ai_em6#em616)).
  NOTE: The corpus has a **100 Hz timer** for the scheduler tick ([sheet H](sec_ai_h)) and nothing finer. A 100 Hz tick cannot time a device transaction, and the register block that would hold this counter is part of the allocation [Q22](sec_ai_q#q22) is still owed. Where the emulator put it is wrong for a separate reason ([EM4.10](sec_ai_em4#em410)).
- EM2.7 — **Anything needing a steady clock must not hang off PHI2**: timers, serial baud generation, audio sample rates and the EC interface each need their own source. Audio already has one ([W.11](sec_ai_w#w11) derives 44,146 Hz from Neon's own PLL rather than from the CPU clock), which is the pattern the rest must follow.
- EM2.8 — **Helium must generate PHI2 rather than receive it.** The clock generator becomes part of the FPGA and has to be able to extend a phase on demand. This is already implicit in [E.1](sec_ai_e#e1) and [T1.18](sec_ai_t1#t118); stating it as a consequence makes it a requirement on the gateware rather than an accident of the block diagram.
- EM2.9 — **In exchange the CPU interface gets simpler.** There is no `RDY` sequencing to get right, and signals qualified by PHI2 phase stay valid while the phase is held — which is exactly why stretching is the easier way to attach a slow peripheral, and why [T1.18](sec_ai_t1#t118) chose it for the Neon read path independently.

## The alternative, recorded so it is not reopened as an apparent improvement.

| | Wait states | Stretched clock |
|---|---|---|
| PHI2 | free-runs at nominal | halted until memory responds |
| A stall | rounded up to whole cycles | paid exactly |
| Average PHI2 | constant by construction | a real measurement |
| What varies | cycles per instruction | the duration of a cycle |

- EM2.10 — **Rounding costs a wait-state machine about 1.2% of wall time on these workloads, rising to roughly 2% at 16 MHz** — small, because a 270 ns fill against a 125 ns cycle rounds to three either way. **Where it bites is the page table walk**: 20 ns of SRAM rounds up to a whole 125 ns cycle, a **6.25× penalty on the real latency**. That is the strongest argument for the decision, and it is an argument about the MMU rather than about the cache.
- EM2.11 — **The page walk was reported free, and it was not.** An earlier version of this model subtracted the walk on the grounds that 20 ns fits inside a cycle that was going to happen anyway, and concluded that walks cost nothing. **That subtraction was an assumption dressed as arithmetic.** A walk is a genuine extra pair of SRAM reads: 20 ns under stretching, a full cycle under wait states, never zero. The corrected figure is what [EM6.1](sec_ai_em6#em61) reports, and the episode is the reason [EM1.13](sec_ai_em1#em113) refuses to let a measurement retire a model without the evidence being written down.
- EM2.12 — **Compare instruction rate across stall models, never cycles and never CPI.** Under stretching, CPI falls from 3.428 to 3.005 while the machine does identical work in identical time: the cycle has stopped being a unit of work, so any metric with a cycle in the denominator is measuring the clock rather than the machine. Average PHI2 becomes a real, workload-dependent number instead — 7.10 MHz striding through memory, 7.98 MHz emitting text through the blitter, 7.96 MHz in hardware text mode.
  NOTE: The emission figures of [EM6.1](sec_ai_em6#em61) are unaffected by the decision, because those paths are stores to I/O that never wait on memory.

## Video is advanced a frame at a time.

- EM2.13 — **Within a frame the emulator accumulates blitter and command cost, and reports the frame as over budget with the overrun quantified if it exceeds it.** No attempt is made to model when inside the frame anything happened. This is valid for exactly as long as the compositor performs no raster-synchronised work and no software mechanism can observe sub-frame video state — both true of the design in [sheet T3](sec_ai_t3), and both worth rechecking if raster effects are ever taken up ([T2.12](sec_ai_t2#t212)).

## The boot model — the emulator starts where the EC lets go.

- EM2.14 — **Four steps, and none of them is emulated hardware.** A host configuration file selects an SD image, a boot image and a parameter set; the boot image is loaded at the address the EC's `WRITE_BURST` sequence would have placed it ([R.25](sec_ai_r#r25)); the Helium and Neon models are initialised to their post-configuration reset state; CPU reset is released and execution begins at the reset vector. No SPI configuration traffic and no power sequencing exists in the model at all.
- EM2.15 — **There is no ROM in this machine, and that changes what "BIOS" means.** Everything is RAM, so a boot image is only *the image the EC loads*, and its size and placement are a free choice rather than a consequence of a part at a fixed address. The emulator's 8 KB at `$00E000` is a convenience; **the only hard requirement is the native vector block at `$00FFE4`** ([E.12](sec_ai_e#e12)). The browser shell hardcodes the load address, the native build takes it as a switch.
  NOTE: This is the same observation [I.1](sec_ai_i#i1) makes from the software side, reached from the other end. It is worth stating twice because the habit it corrects — assuming a boot image lives where the ROM would have been — costs nothing to keep and is wrong here.
