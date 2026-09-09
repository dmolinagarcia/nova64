# The host emulator — purpose and fidelity
> what it models · how faithfully · and what it refuses to model

A PC-hosted functional and timing model of noVa64, written in C11 and running both natively and in a browser. It exists because the software schedule cannot wait for the board, and it has turned out to be worth more than that: it is the **second implementation of this document**, and the places where it and the corpus disagree are the subject of [sheet EM6](sec_ai_em6). This sheet fixes what it models and to what fidelity; [EM2](sec_ai_em2) is its model of time, [EM3](sec_ai_em3) the interfaces it presents to the host, and [EM4](sec_ai_em4) what has actually been built so far.

## Three purposes, in priority order — and the second is the one that pays for the first.

- EM1.1 — **A development platform that does not wait for silicon.** Kernel, OS and GUI work targets the emulator, which takes the prototype bring-up gates of [sheet P3](sec_ai_p3) off the critical path of everything above them. That is why it was written, and it is the least interesting thing about it.
- EM1.2 — **A design instrument.** Cache geometry, TLB depth, replacement policy and the cost of emitting one Neon command are fixed nowhere in this document, and every one of them has to be fixed before RTL is written. The timing model is parameterised precisely so that it can **produce** those numbers instead of consuming them ([EM2.2](sec_ai_em2#em22)). This is a consequence of building the model before the gateware, and it is treated as a first-class goal rather than a side effect.
- EM1.3 — **A second reading of the corpus.** Where the emulator and the hardware disagree, either one of the two implementations is wrong **or this document is ambiguous**, and all three outcomes are worth having. The emulator reached that third outcome repeatedly before any hardware existed to disagree with (→ [EM6.16](sec_ai_em6#em616)).
  NOTE: The ranking is deliberate and it is not the ranking a purist would choose. Fidelity is subordinate to usefulness throughout: every subsystem below is modelled as coarsely as it can be without changing what a program sees.

## What it does not model — and the first boundary is the one that needs care.

- EM1.4 — **No EC.** No Cortex-M33 core, no pico-sdk firmware, no SPI slave configuration sequence, no power sequencing. Execution begins in the state that exists immediately after the EC releases CPU reset ([EM2.13](sec_ai_em2#em213)). **The mailbox is the exception and is modelled exactly** as Helium presents it to the CPU ([sheet D3](sec_ai_d3)), because guest software reads keyboard and pointer input through it and a divergence there would stay invisible until bring-up. What is excluded is the core *behind* that interface: the emulator occupies the EC's position at the mailbox and originates the traffic itself ([EM3.8](sec_ai_em3#em38)).
- EM1.5 — **No gateware simulation.** The emulator does not execute RTL. Verilator or GHDL co-simulation against it as a golden reference is a coherent future activity, is not part of this work, and is not required by any gate ([EM1.11](sec_ai_em1#em111)).
- EM1.6 — **No raster-level video, no analogue behaviour, no bit-accurate audio.** There is no scanline modelling and no mid-frame register latching; nothing electrical is represented at all — no rails, no charger, no panel timing; audio is functional at buffer granularity and currently absent from the build entirely ([EM6.18](sec_ai_em6#em618)).

## The fidelity rule — model what software can observe, and nothing else.

- EM1.7 — **One criterion decides every subsystem in the table below.** Anything a program running on the machine cannot observe is modelled at the coarsest granularity that preserves the observable result. It is written down as a rule rather than applied case by case because the failure it prevents is an expensive one: weeks spent modelling a mechanism no software can detect, in a project where the modelling time is the whole cost.

| Subsystem | Functional fidelity | Timing fidelity |
|---|---|---|
| W65C816S core | Exact — decimal mode, emulation mode, every addressing mode, page and bank crossing penalties | Cycle-stepped at bus-cycle granularity |
| MMU and page table walk | Exact per [sheet L](sec_ai_l) — 2 KB pages, 8192 entries, ASID-tagged TLB, hardware walk, `ABORTB` semantics | Parameterised cost model |
| Cache controller | Exact hit and miss determination; **tags only, no line data** | Parameterised cost model |
| SRAM | Byte-accurate, partitioned into cache data, page tables and pinned regions | Fixed per-access latency |
| System SDRAM | Byte-accurate | Parameterised burst and latency model |
| Bus arbitration | Contention for the Helium SDRAM port | Parameterised |
| Neon blitter | **Bit-exact** — 4 channels, 256 minterms, shifter, edge masks, descending mode, 8-deep queue | Cost per command, accumulated per frame |
| Neon compositor | Exact — backing stores, damage tracking, damage-limited recomposition | Frame granularity |
| Hardware cursor | Exact overlay semantics | Free, as in hardware ([T1.31](sec_ai_t1#t131)) |
| Neon SDRAM | Byte-accurate, a separate array from Helium's | Bandwidth accounting only |
| **Command interface, CPU → Neon** | Exact encoding | **Cycle-accurate emission cost** |
| Audio | Functionally exact sample generation | Buffer granularity |
| SD card | Block device backed by a host image file | Fixed latency |
| Mailbox | Exact register set, protocol and event semantics as the CPU sees them | Fixed latency |
| HID input | Exact event encoding as delivered over the mailbox | Delivery latency a parameter |
| Character device | Exact guest-side register interface | Fixed latency |
| Debug Agent | Exact command set parity with [sheet R](sec_ai_r) | Not modelled |

- EM1.8 — **The blitter is bit-exact and its timing is not, and both halves of that follow from the rule.** Pixels are directly observable and must match [sheet T3](sec_ai_t3) exactly or software developed against the emulator paints the wrong thing on hardware. Blitter *timing* is not observable, because nothing on this machine is raster-synchronised and the compositor performs no mid-frame work — so a frame-granularity accumulator with an over-budget report is the whole of what is needed ([EM2.12](sec_ai_em2#em212)).
- EM1.9 — **The cache holds tags and no data.** The backing arrays are authoritative, and nothing in the system can observe cache contents except through timing and explicit maintenance operations. Duplicating the lines would be a week of work with no detectable consequence, which is [EM1.7](sec_ai_em1#em17) in its most concrete form.
- EM1.10 — **The command interface gets the highest fidelity in the table**, and it is the only row promoted to cycle accuracy on purpose. [T1.2](sec_ai_t1#t12) makes CPU-side command emission the binding constraint on the whole video subsystem, and the ~50 cycles per command that [T2.7](sec_ai_t2#t27) and [T3.3](sec_ai_t3#t33) reason against is an estimate nobody has measured. **Measuring it is the single most important number the emulator exists to produce**, and [EM6.1](sec_ai_em6#em61) is the answer.

## Abandonment conditions — four are ordinary, and the fifth is deliberately not.

- EM1.11 — **Co-simulation is not adopted** unless a defect class emerges that the emulator cannot isolate. It is unscheduled and needs no further document.
  NOTE: It is easier to revisit if the RTL is Verilog, which is recommended on toolchain grounds anyway — Yosys and nextpnr are Verilog-native, VHDL needs the GHDL plugin path, and Verilator is Verilog-only. Not an emulator decision, recorded here because this is where the constraint was noticed.
- EM1.12 — **Exact SDRAM refresh modelling is dropped** without reopening anything if averaged accounting proves indistinguishable in the sweep.
- EM1.13 — [[open]] **Page-open modelling may be dropped only by an explicit revision**, recording the measured fill cost and the sweep conditions that produced it. **This one is not automatic, and the exception is the point.** Every other abandonment condition in this corpus rests on a constraint knowable when it is written — the layout permits the pads or it does not. This one rests on a future measurement, and a measurement can be wrong, unrepresentative, or taken under parameters that later change. The evidence has to be on the record before the model is thrown away.
- EM1.14 — **Audio is deferred indefinitely** if no gate requires it, and none currently does ([sheet W](sec_ai_w) specifies the hardware regardless).
- EM1.15 — **The parameterisation itself collapses** to fixed constants once the geometry is committed to a hardware sheet, and the sweep infrastructure can be retired at that point. The emulator is a measuring instrument for exactly as long as the numbers are unknown.
