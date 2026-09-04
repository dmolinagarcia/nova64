# The EC boundary — signals, configuration and the mailbox
> what crosses · how the FPGAs are configured · the mailbox protocol

Every signal that crosses between the RP2354B and the rest of the machine, and the two protocols that ride on the shared SPI bus: configuration, which happens once, and the mailbox, which never stops. It is the input to schematic capture on one side and to EC firmware on the other — and it is where the claim that *the SPI link is the only connection between the EC and noVa64* gets qualified rather than repeated.

- D3.1 — **The EC is not a bus master, and that is the load-bearing statement of this sheet.** It never drives address, data or control lines of the 65816 bus. **All EC-originated data reaches system memory through Helium**, which issues the accesses as a peer of the CPU port ([D1.3](sec_ai_d1#d13)) — so the arbiter, the cache and the protection model see one more client rather than a second master with its own rules.
  NOTE: **Three planes cross the boundary and the "SPI only" claim holds in exactly one of them.** In the **data** plane it is true: the mailbox is the whole of it. In the **control and configuration** plane it is false — chip selects, `CRESET_B`, `CDONE`, `RESB`. In the **power** plane it is false — the EC owns rail sequencing and is the only thing that can cut a rail ([D1.7](sec_ai_d1#d17), [sheet C](sec_ai_c)). **This sheet does not try to make it true in the other two**; a boot sequencer that cannot assert a reset is not a boot sequencer.
- D3.2 — **The EC can write system memory by design, and it does so over a path no strap is in front of.** `bios.bin` must be in SRAM before the 65816 leaves reset and nothing else can put it there ([D1.22](sec_ai_d1#d122)), so EC write access to system memory is a **structural necessity rather than a debug affordance** — and `MEM_WRITE` takes arbitrary address and data, because that is what loading an image requires.
  NOTE: **`MEM_WRITE` and the Debug Agent are two different slaves, and this is what keeps the question open rather than closing it** ([D3.9](sec_ai_d3#d39)). Arbitrary memory writes ride the **mailbox**, behind `MBX_CSN`, **which [R.18](sec_ai_r#r18)'s `DEBUG_ENABLE` does not gate and cannot gate** — the machine would not boot. Halt, step, trace and register access ride the **Debug Agent**, behind `DBG_CSN`, which the strap does gate, and **the mailbox offers no equivalent of any of them.** So the strap withholds real capability; what it does not withhold is memory.
  NOTE: [[open]] **Which cuts the strap's justification in half rather than through it.** Any reading of `DEBUG_ENABLE` as *protecting memory* is finished — an ungated arbitrary-write channel sits beside it in the same FPGA. [R.18](sec_ai_r#r18)'s own claim, that the privileged path should be *an explicit, visible, deliberate state of the board*, is untouched and now carries the whole weight. **Whether that is enough to keep a pin, a jumper and a footprint is a schematic-capture decision and it is deliberately still open** (→ [Q125](sec_ai_q#q125)); [D25](sec_ai_q#d25), [Q38](sec_ai_q#q38) and [Q122](sec_ai_q#q122) are all written against the gate existing and all move with it.

## Boundary signals — the whole of what crosses, on the target board.

| Signal | Direction | Purpose |
|---|---|---|
| `SPI_SCK` | EC → all | Shared clock |
| `SPI_MOSI` | EC → all | Shared data out |
| `SPI_MISO` | Helium → EC | **Driven only by Helium**; unused during configuration |
| `CFG_SS_A/B/C` | EC → each FPGA | Configuration select, one per device |
| `MBX_CSN` | EC → Helium | Mailbox slave select |
| `DBG_CSN` | EC → Helium | Debug Agent slave select ([sheet R](sec_ai_r)) |
| `CRESET_B[A..C]` | EC → each FPGA | Configuration reset |
| `CDONE[A..C]` | each FPGA → EC | Configuration complete |
| `HELIUM_ATTN` | Helium → EC | Attention line ([D3.13](sec_ai_d3#d313)) |
| `RESB` | EC → W65C816S | Released last, at `S6` |
| `EN_1V2` · `EN_3V3_MAIN` · `EN_BL` · `EN_5V_HOST` | EC → regulators | REV C sequencing |
| `PG_1V2` · `PG_3V3_MAIN` | regulators → EC | Power-good, waited on at `S1` |
| `UART_TX` · `UART_RX` | EC ↔ embedded probe | Console ([D3.14](sec_ai_d3#d314)) |
| `SWDIO` · `SWCLK` · `RUN` | embedded probe → EC | Programming and recovery ([sheet D2](sec_ai_d2)) |

  NOTE: **The microSD is not in this table and that is the finding, not an omission.** It connects permanently and exclusively to Helium — no mux, no ownership handoff, no SD driver in EC firmware ([G.1](sec_ai_g#g1)). The EC has no wires to it.

- D3.3 — **Configuration and runtime data share one physical SPI bus, told apart by chip select**, and this is possible for one specific reason: **iCE40 configuration pins revert to user I/O once `CDONE` rises**, so the wires that carried three bitstreams become the wires that carry the mailbox. Fifteen EC pins cover the whole of it.
- D3.4 — **Tri-state discipline, and it is a gateware requirement rather than a board one.** Once Helium is configured its SPI pins are user I/O, and **Helium must hold them high-impedance whenever `MBX_CSN` and `DBG_CSN` are both inactive** — otherwise the EC cannot configure FPGA-C on the same wires afterwards, and a second Helium build cannot be loaded at all without a power cycle. It costs a tri-state enable driven by the OR of two chip selects, and forgetting it produces contention that looks like a bad bitstream.
- D3.5 — **The post-configuration window: 49 clock cycles after `CDONE` rises before user I/O is active**, per the datasheet. **The EC must not begin the next device's configuration inside it.** It is 49 cycles — two microseconds of doing nothing — and it is the kind of omission that yields a board which configures two of three FPGAs reliably and the third intermittently.

## Configuration — SPI slave, because the part offers nothing better.

- D3.6 — **All three FPGAs are configured by the EC in SPI slave mode, with the EC as the only master on the bus. JTAG is not available and this is not a choice.** The iCE40 LP/HX family **has no JTAG interface at all** — no TAP, no boundary scan, no IDCODE. The configuration modes are SPI slave, SPI master, and the one-time-programmable NVCM, and that is the complete list. Headers labelled "JTAG" on some iCE40 evaluation boards are documentation residue and are not connected to a JTAG port.
  NOTE: **Three consequences, accepted because there is no alternative to accept instead.** **No IDCODE scan** — device presence and identity cannot be verified electrically, so a wrong part or a dead one is indistinguishable from a bad bitstream until something functional runs. **No boundary scan** — solder joints cannot be tested before working gateware exists, which removes the standard first move on a freshly assembled board. **`CDONE` is the only configuration status indication there is.**
  NOTE: **Which is why the E0 assembly checks that do not need gateware are worth more here than they would be on a board with a TAP.** [D1.15](sec_ai_d1#d115)'s JEDEC ID read with `RUN` low proves rails, QFN-80 joints and QSPI continuity with no probe firmware and no OpenOCD; on the FPGA side there is nothing equivalent, and the first proof of a good solder joint is a bitstream that loads (→ [D3.7](sec_ai_d3#d37)).
- D3.7 — **Per-device sequence**, and every step of it has a datasheet number attached.

| # | Step |
|---|---|
| 1 | Assert `CRESET_B` — **minimum 200 ns** low pulse |
| 2 | Release `CRESET_B` with `CFG_SS` asserted, selecting slave mode |
| 3 | Wait for the internal configuration memory to clear — **1200 µs** for LP/HX8K |
| 4 | Clock the bitstream in |
| 5 | Wait for `CDONE` to rise |
| 6 | Clock **49 further cycles** before user I/O is valid ([D3.5](sec_ai_d3#d35)) |

  NOTE: **Order is Neon, then Helium, then FPGA-C** ([D1.22](sec_ai_d1#d122)) — Neon first so that everything after it is reportable on the panel ([D1.23](sec_ai_d1#d123)), Helium before `MEM_WRITE` can begin because the mailbox does not exist until it is configured.
  NOTE: **FPGA-C's absence costs nothing because configuration is per chip select rather than a chain.** Its `CDONE` is simply never observed and `S4` skips on timeout. **No jumper, no footprint provision, no strapping** — which is the one clear advantage the individual-select arrangement has over a daisy chain, and it is worth more than the chain's saved pin.

## Pin budget — the two sides of it, and the optimisation not taken.

| EC side | Pins | | Helium side | Pins |
|---|---|---|---|---|
| Shared SPI, chip selects, `CRESET_B`, `CDONE`, `ATTN` | 15 | | SPI — `SCK`, `MOSI`, `MISO`, shared with configuration | 3 |
| `RESB` | 1 | | `MBX_CSN`, `DBG_CSN` | 2 |
| Rail enables | 4 | | `HELIUM_ATTN` | 1 |
| Power-good returns | 2 | | microSD, permanent | 6 |
| UART | 2 | | | |
| SWD + `RUN`, from the probe | 3 | | | |
| **Subtotal, this sheet** | **27** | | **Charged to Helium user I/O** | **12** |

  NOTE: **Neither column is the whole story and neither should be read as one.** The EC's 27 sit against the RP2354B's 48, with USB host and the hub's reset, touch I2C, battery I2C and backlight PWM still to come — and **there is no keyboard matrix**, which some older accounting still carries ([D1.5](sec_ai_d1#d15)). Helium's 12 are **additive to** the CPU bus, SRAM, SDRAM and the 32-signal multiplexed Neon link, and that total is what has to close on a TQ144 (→ [Q8](sec_ai_q#q8), [Q126](sec_ai_q#q126)).
- D3.8 — **One pin per side is available and is deliberately not taken.** `CFG_SS_A` and `MBX_CSN` could be the same pin — configuration and mailbox use are disjoint in time, so nothing is ever ambiguous about which protocol a Helium select means. **It is noted rather than adopted because it makes the bring-up failure modes harder to reason about**: with two selects, a board that configures but will not talk is a mailbox problem and a board that will not configure is a configuration problem, and merging them puts both on one wire at the exact stage where telling them apart is the whole job. **Revisit only if the TQ144 count comes out short**, where it is the cheapest relief available (→ [Q126](sec_ai_q#q126)).

## The mailbox — the data plane, and the only one the "SPI only" claim describes.

- D3.9 — **EC is master; Helium presents two independent slaves sharing no state**, selected by `MBX_CSN` and `DBG_CSN`. Both are internal to Helium, so **`SPI_MISO` is driven by exactly one device and there is no external bus contention to arbitrate** — the tri-state discipline of [D3.4](sec_ai_d3#d34) is about the *configuration* wires, not about two devices answering at once.
  NOTE: **Why the EC is master rather than Helium.** Consistency — it is already the configuration master and owns boot sequencing, so there is one master on this bus in every phase of the machine's life — and cost: **a slave SPI implementation in Helium is substantially cheaper in LUT4 than a master with arbitration**, on a device whose pin and logic budgets are both under pressure.
- D3.10 — **Full duplex is exploited rather than wasted, and it removes most of the need for a protocol.** SPI moves a byte in each direction per transaction, so **Helium's outbound buffer drains for free on any inbound traffic** — every `HID_EVENT` the EC pushes carries a status byte and a queued byte back. An explicit drain command is needed only when the EC is otherwise idle, and **a 1 kHz poll is ample for LED and modifier state**.

| Direction | Content | Volume |
|---|---|---|
| EC → Helium | `MEM_WRITE` at boot, HID events at runtime | Majority |
| Helium → EC | Keyboard LED state, modifier state, status | Minority |
| Helium → EC | Firmware update payload ([D1.24](sec_ai_d1#d124)) | Occasional, bulk |

- D3.11 — **The status byte occupies the MISO slot of the first byte of every transaction**, which would otherwise be wasted, so status costs nothing and is never stale.

| Bits | Field | Meaning |
|---|---|---|
| 7 | `OUT_PENDING` | Outbound buffer non-empty |
| 6 | `ERR` | Last command rejected |
| 5:4 | reserved | |
| 3:0 | `IN_SPACE` | Free slots in the inbound FIFO |

  NOTE: **`IN_SPACE` is flow control and it exists for one specific machine.** A 65816 at 8 MHz servicing IRQs **can** fall behind a burst of HID events — a fast typist on a machine already handling a page fault is not a hypothetical — and four bits of free-slot count let the EC throttle rather than overrun. **The alternative is dropped keystrokes under load**, which is the class of bug that gets attributed to the keyboard for months.
- D3.12 — **Command classes**, which is the whole protocol.

| Command | Direction | Phase |
|---|---|---|
| `MEM_WRITE` | EC → Helium | boot |
| `HID_EVENT` | EC → Helium | runtime |
| `HID_STATE` | Helium → EC | runtime |
| `FLASH_BEGIN` · `FLASH_DATA` · `FLASH_COMMIT` | Helium → EC | update ([D1.24](sec_ai_d1#d124)) |
| `STATUS` | Helium → EC | implicit in every transaction |

  NOTE: **Inbound HID events are buffered in Helium and signalled to the 65816 by IRQ, one interrupt for the queue rather than one per event.** At the rates a keyboard and a mouse produce, per-event interrupts on this CPU would spend more time in entry and exit than in the handler.
- D3.13 — **`HELIUM_ATTN` lets Helium ask for service without waiting for a poll, and [D3.10](sec_ai_d3#d310) makes it largely unnecessary.** It is routed anyway. **The pin costs one; discovering it was needed after fabrication costs a board spin** — and the asymmetry between those two numbers is the entire argument.

## The console — one subsystem, two modes, and the EC is an interpreter.

- D3.14 — **The `S_FAIL` reporting of [D1.23](sec_ai_d1#d123) and the interactive monitor are the same subsystem**: a boot mode that reports status and failures, and a monitor mode that takes commands. The path is `PC → USB → embedded probe → UART → EC → SPI (DBG_CSN) → Helium's Debug Agent`, and **the EC parses text, issues Debug Agent transactions, and formats results as text. The human never speaks to Helium directly.**
  NOTE: **That decision buys three things and costs one.** **No framing protocol on the UART** — text in, text out, console and Debug Agent sharing one channel in one format, with no SLIP, no COBS and no escaping. **The SPI-side protocol is unconstrained in form**, because no human reads it: fixed opcode, address, length, no delimiters ([sheet R](sec_ai_r)'s register map is free to be as terse as it likes). And **bring-up needs only a serial terminal**. The cost is EC firmware — parser, command table, hex formatting, help — several KB of code that has to be written and maintained.
  NOTE: **Bulk transfers are the one case the text format handles badly.** 64 KB in ASCII hex is ~200 KB on the wire, about 2 s at 1 Mbaud. **Raise the UART to 3 Mbaud** — viable over these trace lengths and recommended — and if bulk dumps become routine, **add a binary or XMODEM command rather than reworking the protocol**, which would cost the three things above to fix the one.
  NOTE: **GDB's Remote Serial Protocol is also text over the same channel**, so source-level debugging of Calypsi-compiled code needs no hardware change at all. Noted as a direction, not a commitment ([sheet O](sec_ai_o)).

| Command | Purpose |
|---|---|
| `md <bank:addr> <len>` | Memory dump |
| `mw <bank:addr> <bytes>` | Memory write |
| `halt` · `run` | CPU control |
| `regs` | Register dump |
| `stat` | Rails, battery, `CDONE` status |
| `slot` | Active flash slot and version |

  NOTE: A wozmon-class monitor, and deliberately so — it is the set that makes the machine debuggable, not the set somebody will want eventually. What comes after it is [Q131](sec_ai_q#q131).
