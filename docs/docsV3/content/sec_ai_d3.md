# The EC boundary — signals, configuration and the link
> what crosses · how the FPGAs are configured · the link protocol

Every signal that crosses between the RP2354B and the rest of the machine, and the two protocols that ride on the shared SPI bus: configuration, which happens once, and the link, which never stops. It is the input to schematic capture on one side and to EC firmware on the other — and it is where the claim that *the SPI link is the only connection between the EC and noVa64* gets qualified rather than repeated. **Since REV C there is one runtime chip select rather than two, and the mailbox is no longer the link — it is a set of endpoints on it** ([D3.9](sec_ai_d3#d39)).

- D3.1 — **The EC is not a bus master, and that is the load-bearing statement of this sheet.** It never drives address, data or control lines of the 65816 bus. **All EC-originated data reaches system memory through Helium**, which issues the accesses as a peer of the CPU port ([D1.3](sec_ai_d1#d13)) — so the arbiter, the cache and the protection model see one more client rather than a second master with its own rules.
  NOTE: **Three planes cross the boundary and the "SPI only" claim holds in exactly one of them.** In the **data** plane it is true: the link is the whole of it. In the **control and configuration** plane it is false — chip selects, `CRESET_B`, `CDONE`, `RESB`. In the **power** plane it is false — the EC owns rail sequencing and is the only thing that can cut a rail ([D1.7](sec_ai_d1#d17), [sheet C](sec_ai_c)). **This sheet does not try to make it true in the other two**; a boot sequencer that cannot assert a reset is not a boot sequencer.
- D3.2 — **The EC can write system memory by design, and it does so over a path no strap is in front of.** `bios.bin` must be in SRAM before the 65816 leaves reset and nothing else can put it there ([D1.22](sec_ai_d1#d122)), so EC write access to system memory is a **structural necessity rather than a debug affordance** — and [R.25](sec_ai_r#r25)'s `WRITE_BURST` takes an absolute address and arbitrary data, because that is what loading an image requires.
  NOTE: **Boot memory loading and debug memory writes are now one mechanism rather than two, and that closes the escape route this item used to have** ([R.25](sec_ai_r#r25)). REV B had them as two slaves — `MEM_WRITE` behind `MBX_CSN` and the Debug Agent behind `DBG_CSN` — so it was possible to say that the strap gated real capability and merely failed to gate memory. **With one link and one select, the boot load rides endpoint `0x02`, which is the Debug Agent**: a strap that gated it would stop the machine booting ([D80](sec_ai_q#d80), [D82](sec_ai_q#d82)).
  NOTE: [[open]] **So the strap's justification is now undivided, and it is small.** Every reading of `DEBUG_ENABLE` as *protecting memory* is finished, and the two-slave argument that kept half of it alive is gone with the second chip select. What remains is [R.18](sec_ai_r#r18)'s own claim — that the privileged path should be *an explicit, visible, deliberate state of the board* — and, if the strap is kept, it must now gate **selectively inside one endpoint**: halt, step and trace refused while `WRITE_BURST` is allowed, which is a dispatch convention rather than a gate. **Whether that is worth a pin, a jumper and a footprint is a schematic-capture decision and it is deliberately still open** (→ [Q125](sec_ai_q#q125)); [D25](sec_ai_q#d25), [Q38](sec_ai_q#q38) and [Q122](sec_ai_q#q122) are all written against the gate existing and all move with it.
  NOTE: **What does not move is the isolation property, because it never rested on the pin.** The Debug Agent is unreachable from software if and only if its request port has exactly one source and no CPU-writable register can drive it — a connectivity constraint, stated as **INV-1** and verified structurally ([R.4](sec_ai_r#r4), [R.26](sec_ai_r#r26)). Chip-select count is irrelevant to it.

## Boundary signals — the whole of what crosses, on the target board.

| Signal | Direction | Purpose |
|---|---|---|
| `SPI_SCK` | EC → all | Shared clock |
| `SPI_MOSI` | EC → all | Shared data out |
| `SPI_MISO` | Helium → EC | **Driven only by Helium**; unused during configuration |
| `CFG_SS_A/B/C` | EC → each FPGA | Configuration select, one per device |
| `LINK_CSN` | EC → Helium | The one runtime slave select — every endpoint rides behind it ([D3.9](sec_ai_d3#d39)). Was `MBX_CSN`; `DBG_CSN` is withdrawn ([D80](sec_ai_q#d80)) |
| `CRESET_B[A..C]` | EC → each FPGA | Configuration reset |
| `CDONE[A..C]` | each FPGA → EC | Configuration complete |
| `HELIUM_ATTN` | Helium → EC | Attention line ([D3.13](sec_ai_d3#d313)) |
| `RESB` | EC → W65C816S | Released last, at `S6` |
| `EN_1V2` · `EN_3V3_MAIN` · `EN_BL` · `EN_5V_HOST` | EC → regulators | REV C sequencing |
| `PG_1V2` · `PG_3V3_MAIN` | regulators → EC | Power-good, waited on at `S1` |
| `UART_TX` · `UART_RX` | EC ↔ embedded probe | Console ([D3.14](sec_ai_d3#d314)) |
| `SWDIO` · `SWCLK` · `RUN` | embedded probe → EC | Programming and recovery ([sheet D2](sec_ai_d2)) |

  NOTE: **The microSD is not in this table and that is the finding, not an omission.** It connects permanently and exclusively to Helium — no mux, no ownership handoff, no SD driver in EC firmware ([G.1](sec_ai_g#g1)). The EC has no wires to it.

- D3.3 — **Configuration and runtime data share one physical SPI bus, told apart by chip select**, and this is possible for one specific reason: **iCE40 configuration pins revert to user I/O once `CDONE` rises**, so the wires that carried three bitstreams become the wires that carry the link. Fourteen EC pins cover the whole of it.
- D3.4 — **Tri-state discipline, and it is a gateware requirement rather than a board one.** Once Helium is configured its SPI pins are user I/O, and **Helium must hold them high-impedance whenever `LINK_CSN` is inactive** — otherwise the EC cannot configure FPGA-C on the same wires afterwards, and a second Helium build cannot be loaded at all without a power cycle. It costs a tri-state enable driven by one chip select — it used to be the OR of two — and forgetting it produces contention that looks like a bad bitstream.
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

  NOTE: **Order is Neon, then Helium, then FPGA-C** ([D1.22](sec_ai_d1#d122)) — Neon first so that everything after it is reportable on the panel ([D1.23](sec_ai_d1#d123)), Helium before the `bios.bin` load can begin because the link does not exist until it is configured.
  NOTE: **FPGA-C's absence costs nothing because configuration is per chip select rather than a chain.** Its `CDONE` is simply never observed and `S4` skips on timeout. **No jumper, no footprint provision, no strapping** — which is the one clear advantage the individual-select arrangement has over a daisy chain, and it is worth more than the chain's saved pin.

## Pin budget — the two sides of it, and the optimisation that was taken instead.

| EC side | Pins | | Helium side | Pins |
|---|---|---|---|---|
| Shared SPI, `LINK_CSN`, `CFG_SS_A/B/C`, `CRESET_B`, `CDONE`, `ATTN` | 14 | | SPI — `SCK`, `MOSI`, `MISO`, shared with configuration | 3 |
| `RESB` | 1 | | `LINK_CSN` | 1 |
| Rail enables | 4 | | `HELIUM_ATTN` | 1 |
| Power-good returns | 2 | | microSD, permanent | 6 |
| UART | 2 | | | |
| SWD + `RUN`, from the probe | 3 | | | |
| **Subtotal, this sheet** | **26** | | **Charged to Helium user I/O** | **11** |

  NOTE: **Both totals dropped by one at REV C**, and the pin is `DBG_CSN`: the Debug Agent is now endpoint `0x02` on the link rather than a second slave with its own select ([D3.9](sec_ai_d3#d39), [D80](sec_ai_q#d80)).
  NOTE: **Neither column is the whole story and neither should be read as one.** The EC's 26 sit against the RP2354B's 48, with USB host and the hub's reset, touch I2C, battery I2C and backlight PWM still to come — and **there is no keyboard matrix**, which some older accounting still carries ([D1.5](sec_ai_d1#d15)). Helium's 11 are **additive to** the CPU bus, SRAM, SDRAM and the 32-signal multiplexed Neon link, and that total is what has to close on a TQ144 (→ [Q8](sec_ai_q#q8), [Q126](sec_ai_q#q126)).
- D3.8 — **The remaining merge — `CFG_SS_A` as the runtime select — is now rejected outright rather than held in reserve, and on hardware grounds rather than diagnostic ones.** REV B noted it as the cheapest relief if the TQ144 count came out short, at the cost of harder bring-up diagnosis. **The iCE40 samples `SPI_SS_B` on exit from configuration reset to choose between slave and master configuration mode**, so a line that idles high during normal operation brings the part up in *master* mode after any unintended reset — a rail dropout, a glitch on `CRESET_B` — where it drives `SCK` and contends with the EC on a bus that has one master by design ([D3.6](sec_ai_d3#d36)). **A pin saved this way buys a contention failure that presents as a dead link with no obvious cause.**
  NOTE: **Which produces a rule that costs nothing and is worth having whether or not the merge was ever attempted.** **The EC parks each `CFG_SS` line low once the corresponding `CDONE` rises, and holds it low for the lifetime of the session.** Any accidental reset of any of the three FPGAs then falls into slave mode, which is passive. **Helium's gateware must not drive `CFG_SS_A` as an output after configuration** — the same class of requirement as the tri-state discipline of [D3.4](sec_ai_d3#d34), and the same class of omission if it is forgotten. To be confirmed against the iCE40 programming and configuration guide (→ [Q151](sec_ai_q#q151)).
  NOTE: **The pin economy the merge was held in reserve for arrived from the other direction.** `DBG_CSN` going away saves the same one pin per side that the merge would have, and costs nothing in diagnosis: a board that configures but will not talk is still a link problem and a board that will not configure is still a configuration problem ([Q126](sec_ai_q#q126)).

## The link — one chip select, one front end, and a router behind it.

- D3.9 — **EC is master; Helium presents one slave, and an internal router dispatches each frame to an endpoint named in its header.** REV B had two independent slaves behind `MBX_CSN` and `DBG_CSN`. **REV C has one select, `LINK_CSN`, one SPI front end, and a header byte where the second select used to be** ([D80](sec_ai_q#d80)). `SPI_MISO` is still driven by exactly one device, so the tri-state discipline of [D3.4](sec_ai_d3#d34) remains about the *configuration* wires rather than about two blocks answering at once.

```
                  ┌───────────────────── Helium ─────────────────────┐
 EC ── SPI_SCK ──▶│  ┌───────────┐   ┌──────────┐   EP 0x01 ─▶ LINK  │
    ── SPI_MOSI ─▶│  │ SPI slave │──▶│  Router  │─▶ EP 0x02 ─▶ DBG   │
    ◀─ SPI_MISO ──│  │ front end │◀──│  header  │   EP 0x03 ─▶ HID   │
    ── LINK_CSN ─▶│  └───────────┘   │ FSM + CRC│   EP 0x04 ─▶ SYS   │
    ◀─ HELIUM_ATTN│                  └──────────┘                    │
                  └──────────────────────────────────────────────────┘
```

  NOTE: **Why the EC is master rather than Helium.** Consistency — it is already the configuration master and owns boot sequencing, so there is one master on this bus in every phase of the machine's life — and cost: **a slave SPI implementation in Helium is substantially cheaper in LUT4 than a master with arbitration**, on a device whose pin and logic budgets are both under pressure.
  NOTE: **The mailbox is no longer the link.** It is a set of endpoints on it, at the same level as the Debug Agent, and the rename of `MBX_CSN` to `LINK_CSN` is what says so. Where this document says *mailbox* it now means endpoints `0x03` and `0x04` ([D3.12](sec_ai_d3#d312)).
- D3.9a — **The second chip select bought addressing, not concurrency, and that is the whole of the argument for removing it.** `SCK`, `MOSI` and `MISO` were always shared, so `MBX_CSN` and `DBG_CSN` could never be asserted at the same time: **the second select provided no parallelism and no isolation of bandwidth, only a way to say which block a transaction was for.** A header byte says the same thing at the cost of gateware rather than a pin — and gateware is the budget that is not under pressure on this device.
  NOTE: **It was already written down as the contingency, and it is adopted here by design rather than by necessity.** [D3.8](sec_ai_d3#d38) and [Q126](sec_ai_q#q126) carried a chip-select merge as the cheapest relief if Helium's TQ144 count came out short. Taking it now means the count closes one pin better on both sides without the question being forced (→ [Q8](sec_ai_q#q8)).
  NOTE: **Area is roughly neutral and the real cost is new failure modes.** Removed: one SPI front end, one `SCK`→core clock-domain crossing, the `MISO` output mux. Added: the header state machine, two CRC-8 generators and the free-space comparison of [D3.12a](sec_ai_d3#d312a). **Each new failure mode is closed by a named rule, and [D3.12c](sec_ai_d3#d312c) is the list of them** — a merge that only removed a pin and left them implicit would not be worth taking.
- D3.10 — **Full duplex is exploited rather than wasted, and it removes most of the need for a protocol.** SPI moves a byte in each direction per transaction, so **Helium's outbound buffer drains for free on any inbound traffic** — every HID frame the EC pushes carries status and queued bytes back. An explicit drain frame is needed only when the EC is otherwise idle, and **a 1 kHz poll is ample for LED and modifier state**.

| Direction | Content | Volume |
|---|---|---|
| EC → Helium | `WRITE_BURST` at boot ([R.25](sec_ai_r#r25)), HID events at runtime | Majority |
| Helium → EC | Keyboard LED state, modifier state, link and endpoint status | Minority |
| Helium → EC | Firmware update payload ([D1.24](sec_ai_d1#d124)) | Occasional, bulk |

  NOTE: **A read is a frame whose `MOSI` payload is padding and a write is a frame whose `MISO` payload is padding**, which is why there is no read opcode and no turnaround anywhere in the format. Padding is `0x00` and is still covered by the frame CRC.

## The frame — three header bytes each way, then payload, then a CRC.

- D3.11 — **A frame is one assertion of `LINK_CSN`, and its length is `4 + LEN` bytes for a payload of `LEN`.** `LEN` is one byte, so the maximum frame is 259 bytes; [D3.12a](sec_ai_d3#d312a) is why that is a deliberate ceiling rather than a limitation.

```
byte     0        1        2       3 … 3+LEN-1      3+LEN
MOSI     EP       LEN      HCRC    payload out      FCRC
MISO     STATUS   LASTRES  EPSTAT  payload in       0x00
```

| Byte | `MOSI` | `MISO` |
|---|---|---|
| 0 | `EP` — destination endpoint | `STATUS` — global link status, latched at the falling edge of `LINK_CSN` |
| 1 | `LEN` — payload length, 0–255 | `LASTRES` — the result of the **previous** frame, latched at its rising edge |
| 2 | `HCRC` — CRC-8 over bytes 0–1 | `EPSTAT` — status of the endpoint addressed in *this* frame |

  NOTE: **The asymmetry in what the two byte-0 slots can carry is a timing fact rather than a design choice.** `MISO` byte 0 is clocked out while the EC is still transmitting `EP`, so **it cannot depend on the destination** and must be global. Byte 2 can be endpoint-specific, because `EP` completed a full byte earlier. Anything that needs to know the endpoint lands at byte 2 or later, and that is the constraint the whole header layout is derived from.
  NOTE: **`LASTRES` reports the previous frame rather than this one, for the same reason.** A verdict on the current frame does not exist until its last byte has been clocked in, so it is delivered in the next frame — and is also readable from endpoint `0x01` for an EC that would rather ask than remember ([D3.12](sec_ai_d3#d312)).
- D3.11a — **`STATUS` and `EPSTAT` are the descendants of REV B's single status byte, and they cost nothing for the same reason it did** — the `MISO` slots of the header bytes would otherwise be wasted, so status is free and never stale.

| `STATUS` bit | Meaning |
|---|---|
| 7 | `LINK_READY` — router configured and operational |
| 6 | `ERR_STICKY` — at least one frame rejected since it was last cleared through endpoint `0x01` |
| 5–0 | Pending bitmap — bit *n* set means endpoint `0x01 + n` has outbound data |

  NOTE: **`EPSTAT` is REV B's `IN_SPACE` widened to a byte, and the flow control it exists for is unchanged.** For a sink or a duplex endpoint it is **inbound free space in bytes, saturating at `0xFF`**; for a source endpoint it is outbound bytes available. A 65816 at 8 MHz servicing IRQs **can** fall behind a burst of HID events — a fast typist on a machine already handling a page fault is not a hypothetical — and a free-space count is what lets the EC throttle rather than overrun. **The alternative is dropped keystrokes under load**, which is the class of bug that gets attributed to the keyboard for months.
  NOTE: **`EPSTAT` may under-report free space and must never over-report it.** It is generated on the `SCK` side from a Gray-coded read pointer synchronised out of the core domain ([D3.12b](sec_ai_d3#d312b)), so it lags by the synchroniser latency. **Under-reporting costs a frame the EC did not have to abandon; over-reporting overruns a queue** — so the implementation rounds in one direction and the direction is specified rather than left to the gateware.
  NOTE: **The pending bitmap is what caps the endpoint count at six**, and the ceiling is accepted: growth past it is a status-format revision rather than a transport revision (→ [Q152](sec_ai_q#q152)).
- D3.11b — **Two CRC-8s, and they protect two different things.** Both are polynomial `0x07`, initial value `0xFF`, no reflection and no final XOR. **`HCRC` covers the header and protects the routing decision**, which is the catastrophic failure mode: *a corrupted `EP` byte turns a keystroke into a halt or a memory write.* **`FCRC` covers bytes 0 through `2 + LEN` — the whole frame, `HCRC` included — and decides whether the frame is committed.**
  NOTE: **Initial value `0xFF` rather than `0x00` is deliberate: an all-zeros frame must not validate.** Stuck-low and stuck-high `MOSI` are caught a second time by the reserved `EP` values of [D3.12](sec_ai_d3#d312), and **the two mechanisms are independent**, which is the property worth having on the one link that carries memory writes.
- D3.11c — **Acceptance is seven rules in a fixed order, and any failure aborts the frame with no effect on any endpoint.**

| # | Rule | Result if it fails |
|---|---|---|
| 1 | `EP` is neither `0x00` nor `0xFF` | `NACK_EP_RESERVED` |
| 2 | `HCRC` matches — the frame is drained without dispatch | `NACK_HCRC` |
| 3 | `EP` is assigned and implemented | `NACK_EP_UNKNOWN` |
| 4 | The endpoint is enabled and not held in reset | `NACK_EP_RESET` |
| 5 | `EPSTAT ≥ LEN`, for sink and duplex endpoints | `NACK_FULL` |
| 6 | `LINK_CSN` stayed asserted for all `4 + LEN` bytes | `NACK_SHORT` |
| 7 | `FCRC` matches | `NACK_FCRC` |

  NOTE: **Rules 6 and 7 are evaluated after the payload has been clocked in, so an endpoint may already have consumed bytes — which forces a commit discipline, declared per endpoint.** **Buffered**: the endpoint stages the frame and commits on `ACK`, required for anything whose payload is not idempotent, which is every queue endpoint, at the cost of one buffer of its maximum frame size. **Streaming**: the endpoint commits incrementally, permitted only where the payload is idempotent and absolutely addressed. **[R.25](sec_ai_r#r25)'s `WRITE_BURST` is the only streaming case, and its per-frame absolute address exists to make it one.**

## Endpoints — the mailbox is one of them, and so is the Debug Agent.

- D3.12 — **The endpoint map, which is where the command classes of REV B went.**

| `EP` | Name | Class | Commit | Purpose |
|---|---|---|---|---|
| `0x00` | reserved | — | — | Stuck-low `MOSI` detection. Always rejected |
| `0x01` | `LINK` | duplex | buffered | ID and version, gateware build ID, echo, endpoint enumeration, error counters, `ERR_STICKY` clear |
| `0x02` | `DBG` | duplex | streaming | Debug Agent — memory read and write, halt/step/run, registers, trace, TLB. **Carries the `bios.bin` load** ([sheet R](sec_ai_r)) |
| `0x03` | `HID` | sink | buffered | Keyboard and mouse events, EC → CPU |
| `0x04` | `SYS` | duplex | buffered | Power, battery, thermal, RTC; EC ↔ CPU ([sheet S](sec_ai_s)) |
| `0x05`–`0x06` | reserved | — | — | Growth, inside the pending bitmap ([D3.11a](sec_ai_d3#d311a)) |
| `0x07`–`0xFE` | unassigned | — | — | `NACK_EP_UNKNOWN` |
| `0xFF` | reserved | — | — | Stuck-high `MOSI` detection. Always rejected |

  NOTE: **`LINK` must answer whenever the router is alive, including while every other endpoint is held in reset, and it is the only endpoint carrying that obligation.** It is what makes a mis-programmed or partially configured Helium a diagnosable condition rather than a hang — which is why the boot sequence now reads it before it loads anything ([D1.22](sec_ai_d1#d122)).
  NOTE: **Inbound HID events are still buffered in Helium and signalled to the 65816 by IRQ, one interrupt for the queue rather than one per event.** At the rates a keyboard and a mouse produce, per-event interrupts on this CPU would spend more time in entry and exit than in the handler. The transport changed; the CPU-side contract did not.
  NOTE: **`FLASH_BEGIN` · `FLASH_DATA` · `FLASH_COMMIT` are endpoint `0x04` traffic in the Helium → EC direction** ([D1.24](sec_ai_d1#d124)), and they are the one bulk flow that runs that way. Whether `0x04` also absorbs the rest of the CPU → EC direction or a dedicated source endpoint is warranted is open (→ [Q152](sec_ai_q#q152)).
- D3.12a — **Dispatch happens at header completion, into a per-endpoint sink, and there is no shared payload buffer anywhere in the router.** This is the load-bearing rule of the whole merge. **A shared inbound FIFO with a dispatcher behind it produces head-of-line blocking in precisely the worst circumstance**: the CPU hangs, stops draining the HID queue, a keyboard frame stalls at the head — and debug frames queue behind it exactly when they are needed. **A full endpoint rejects its own frames under rule 5 and affects no other endpoint.**
  NOTE: **Rejection is pre-emptive rather than retrospective, which is what makes rule 5 cheap.** `EPSTAT` is on the wire at `MISO` byte 2, **before the EC has sent any payload**, so an EC that reads it abandons the frame by releasing `LINK_CSN` before byte 3 — three bytes rather than 259. **EC firmware should do this; `NACK_FULL` exists for the case where it does not.**
  NOTE: **259 bytes is therefore the bound on the delay one endpoint can impose on another**, which at 10 MHz is about 207 µs. **This is the reason `LEN` is one byte**: a 16-bit length would let a bulk memory transfer lock HID out for tens of milliseconds, and the framing overhead the short length costs is four bytes per frame — 1.6 % at [R.25](sec_ai_r#r25)'s 251-byte payloads.
  NOTE: **The head-of-line property has to be tested deliberately, because it will not appear by accident.** Fill the HID queue, stop draining it, and confirm Debug Agent throughput on endpoint `0x02` is unaffected ([E1.7](sec_ai_p3#e17)). **This is the failure the architecture exists to prevent**, and an implementation that quietly grew a shared buffer would pass every other test on the sheet.
- D3.12b — **The header FSM, both CRC generators and `EPSTAT` live in the `SCK` domain; endpoint delivery crosses into the core domain through one asynchronous FIFO per endpoint with Gray-coded pointers.** There is **one SPI front end and one crossing per endpoint**, replacing REV B's two front ends and two crossings — and the crossing is now specified once rather than twice, which closes an open item the Debug Agent specification had been carrying on its own ([R.26](sec_ai_r#r26)).
  NOTE: **All frame-level state — byte counter, CRC accumulators, header registers — is reset by the rising edge of `LINK_CSN`.** So an aborted frame costs nothing beyond itself and **there is no resynchronisation procedure to write, test or get wrong**.
- D3.12c — **The router must outlive its endpoints, and this is a reset-domain rule rather than a preference.** **The router sits in the same reset domain as the Debug Agent front end, and in no domain any endpoint can assert.** If a Debug Agent command resets Helium's system side and the router falls with it, *the command destroys the transport carrying it*. Under this rule the router survives and reports `NACK_EP_RESET` on behalf of whatever is down; its own state clears only on FPGA reconfiguration.

| Failure mode | Consequence | Closed by |
|---|---|---|
| Corrupted `EP` byte | A keystroke interpreted as a halt or a memory write | `HCRC` ([D3.11b](sec_ai_d3#d311b)) |
| Corrupted payload | Bad bytes committed to memory or a queue | `FCRC` plus the commit discipline ([D3.11c](sec_ai_d3#d311c)) |
| `MOSI` stuck low or high | Frames addressed `0x00` or `0xFF` | Reserved endpoints ([D3.12](sec_ai_d3#d312)), independently `HCRC` |
| `LINK_CSN` released mid-frame | A partial frame | `NACK_SHORT`; frame state cleared on the rising edge ([D3.12b](sec_ai_d3#d312b)) |
| An endpoint reset by a debug command | The transport destroyed by the command carrying it | The router's reset domain, this item |
| An endpoint queue full | Head-of-line blocking of every other endpoint | Per-endpoint sinks ([D3.12a](sec_ai_d3#d312a)) |
| A router bug | Total loss of the machine including debug | Bring-up order ([E1.7](sec_ai_p3#e17)) |
| Accidental Helium reset while running | The FPGA comes up in SPI master mode and contends | `CFG_SS` parking ([D3.8](sec_ai_d3#d38)) |

  NOTE: **The router is a single point of failure and the Debug Agent cannot be used to debug the router, so the mitigation is ordering rather than redundancy** — front end, router and endpoint `0x01` alone first, and nothing else until an ID read and three deliberately malformed frames behave ([E1.7](sec_ai_p3#e17)). **REV B's two front ends offered less isolation than they appeared to**: they would have been two instances of the same module carrying the same bugs.
- D3.13 — **`HELIUM_ATTN` lets Helium ask for service without waiting for a poll, and [D3.10](sec_ai_d3#d310) makes it largely unnecessary.** It is routed anyway, and the pending bitmap of [D3.11a](sec_ai_d3#d311a) now says *which* endpoint wants attention once the EC does come asking. **The pin costs one; discovering it was needed after fabrication costs a board spin** — and the asymmetry between those two numbers is the entire argument.

## The console — one subsystem, two modes, and the EC is an interpreter.

- D3.14 — **The `S_FAIL` reporting of [D1.23](sec_ai_d1#d123) and the interactive monitor are the same subsystem**: a boot mode that reports status and failures, and a monitor mode that takes commands. The path is `PC → USB → embedded probe → UART → EC → LINK_CSN → endpoint 0x02 → Helium's Debug Agent`, and **the EC parses text, issues Debug Agent transactions, and formats results as text. The human never speaks to Helium directly.**
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
