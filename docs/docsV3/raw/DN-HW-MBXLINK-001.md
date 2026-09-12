# DN-HW-MBXLINK-001 — EC ↔ Helium Serial Link: Router, Frame Format and Endpoint Map

**Revision A** — proposed
**Project:** noVa64
**Supersedes:** the dual chip-select scheme of DN-HW-ECIF-001 Rev B §3.1, §3.5
**Depends on:** DN-HW-ECIF-001 (boundary, configuration, flash layout), Helium Debug Agent — Register Map & Console Protocol

---

## 1. Purpose and scope

This note specifies the single serial link between the EC (RP2354B) and Helium
(FPGA-A) at the transport level: frame format, endpoint addressing, flow control,
integrity, and reset domains.

It replaces the arrangement in which the mailbox and the Debug Agent were two
independent SPI slaves inside Helium, selected by `MBX_CSN` and `DBG_CSN`
respectively. Under this note there is one slave, one chip select, one front end,
and an internal router that dispatches frames to endpoints by an address carried
in the frame header.

**In scope:** wire format, routing, integrity, flow control, endpoint enumeration,
reset behaviour, bring-up order, revised boot sequence.

**Out of scope:** the internal register map and command semantics of any endpoint.
The Debug Agent's register map is unchanged and remains specified in its own
document; this note only redefines how its transactions are carried, and adds the
bulk-write payload format of §9.

---

## 2. Decision and rationale

### 2.1 The second chip select bought addressing, not concurrency

`SCK`, `MOSI` and `MISO` were always shared between the two slaves. `MBX_CSN` and
`DBG_CSN` could therefore never be asserted simultaneously. The second select
provided no parallelism and no isolation of bandwidth — only a way to say which
block a transaction was for. A header byte says the same thing at the cost of
gateware rather than a pin.

DN-HW-ECIF-001 Rev B already carried this merge as the contingency for open item
#1 (Helium TQ144 pin shortfall). It is adopted here by design rather than by
necessity.

### 2.2 The security rationale for `DBG_CSN` was overstated

Rev B justified `DBG_CSN` on the grounds that it made "SPI-only, not CPU-visible"
a structural guarantee rather than a dispatch convention. That is not what the pin
delivered.

The property is a connectivity property, not a decode property. It holds if and
only if the Debug Agent's request port has exactly one source — the link — and no
CPU-writable register in Helium can reach it. That invariant is unaffected by how
many chip selects exist, and is restated here as **INV-1** (§8.4).

On privilege, the reasoning that retired `DEBUG_ENABLE` applies unchanged: the EC
writes system memory by structural necessity during bootstrap, so a separate
select gates nothing the link does not already expose.

### 2.3 What the merge actually costs

Roughly neutral in area. Removed: one SPI front end, one `SCK`→core clock-domain
crossing, the `MISO` output mux. Added: the header state machine, two CRC-8
generators, and the free-space comparison of §7.

The real cost is new failure modes, each closed by a rule in §7, §8 and §12. Two
of the three urgent open items in the Debug Agent specification are dissolved:
`DBG_CSN` allocation disappears, and the SPI→core CDC is now specified once.

### 2.4 Naming

`MBX_CSN` is renamed `LINK_CSN`. The mailbox is no longer the link; it is a set of
endpoints on it, at the same level as the Debug Agent.

---

## 3. Architecture

```
                  ┌──────────────────────── Helium ────────────────────────┐
                  │                                                        │
 EC ── SPI_SCK ──►│  ┌───────────┐   ┌──────────┐   EP 0x01 ─► LINK        │
    ── SPI_MOSI ─►│  │ SPI slave │──►│  Router  │─► EP 0x02 ─► Debug Agent │
    ◄─ SPI_MISO ──│  │ front end │◄──│ (header  │   EP 0x03 ─► HID queue   │
    ── LINK_CSN ─►│  └───────────┘   │  FSM +   │   EP 0x04 ─► SYS mailbox │
    ◄─ HELIUM_ATTN│                  │  CRC)    │                          │
                  │                  └──────────┘                          │
                  └────────────────────────────────────────────────────────┘
```

The router is not a shared queue with a dispatcher behind it. Dispatch happens on
completion of the header, directly into a per-endpoint sink. See §7.1.

---

## 4. Boundary signal table (delta against DN-HW-ECIF-001 Rev B)

| Signal | Rev B | Rev C | Note |
|---|---|---|---|
| `SPI_SCK` | shared | unchanged | |
| `SPI_MOSI` | shared | unchanged | |
| `SPI_MISO` | shared | unchanged | |
| `MBX_CSN` | present | **renamed `LINK_CSN`** | |
| `DBG_CSN` | present | **removed** | |
| `HELIUM_ATTN` | present | unchanged | |
| `CFG_SS_A/B/C` | present | unchanged, parking rule added (§10.2) | |
| `CRESET_B[A..C]`, `CDONE[A..C]` | present | unchanged | |

| Pin budget | Rev B | Rev C |
|---|---|---|
| EC total (ECIF §3.4) | 25 | **24** |
| Helium user I/O charged to EC boundary + SD (ECIF §3.5) | 12 | **11** |

---

## 5. Frame format

### 5.1 Structure

A frame is one assertion of `LINK_CSN`.

```
byte     0        1        2       3 … 3+LEN-1      3+LEN
MOSI     EP       LEN      HCRC    payload out      FCRC
MISO     STATUS   LASTRES  EPSTAT  payload in       0x00
```

Total frame length is `4 + LEN` bytes. `LEN` is the payload length in bytes,
0–255, identical in both directions. Maximum frame is 259 bytes.

The link is symmetric and full duplex. A read is a frame whose `MOSI` payload is
padding; a write is a frame whose `MISO` payload is padding. Padding is `0x00` and
is still covered by `FCRC`.

### 5.2 MOSI header

| Byte | Field | Definition |
|---|---|---|
| 0 | `EP` | Destination endpoint, §6. `0x00` and `0xFF` are reserved and always rejected. |
| 1 | `LEN` | Payload length in bytes, 0–255. |
| 2 | `HCRC` | CRC-8 over bytes 0–1. |

### 5.3 MISO header

`MISO` byte 0 is clocked out while the EC is still transmitting `EP`, so it cannot
depend on the destination. Byte 2 can, because `EP` completed a full byte earlier.

| Byte | Field | Definition |
|---|---|---|
| 0 | `STATUS` | Global link status, latched at the falling edge of `LINK_CSN`. |
| 1 | `LASTRES` | Result of the **previous** frame, latched at its rising edge of `LINK_CSN`. |
| 2 | `EPSTAT` | Endpoint-specific status for the endpoint addressed in this frame. |

`STATUS`:

| Bit | Meaning |
|---|---|
| 7 | `LINK_READY` — router configured and operational |
| 6 | `ERR_STICKY` — at least one frame rejected since last cleared via EP `0x01` |
| 5–0 | Pending bitmap: bit *n* set means endpoint `0x01 + n` has outbound data |

`EPSTAT` semantics follow the endpoint's class (§6):

| Class | `EPSTAT` |
|---|---|
| SINK | Inbound free space in bytes, saturating at `0xFF` |
| SOURCE | Outbound bytes available, saturating at `0xFF` |
| DUPLEX | Inbound free space in bytes, saturating at `0xFF` |

For DUPLEX endpoints, outbound availability is signalled by the `STATUS` pending
bit; the count, if the endpoint needs one, is carried in its own payload.

`EPSTAT` is generated on the `SCK` side from a Gray-coded read pointer
synchronised from the core domain. It may therefore under-report free space by the
synchroniser latency. Under-reporting is safe; over-reporting is not, and the
implementation must round in that direction.

### 5.4 CRC

Both CRCs are CRC-8 with polynomial `x⁸ + x² + x + 1` (`0x07`), initial value
`0xFF`, no input or output reflection, no final XOR.

- `HCRC` covers bytes 0–1. It protects the routing decision, which is the
  catastrophic failure mode: a corrupted `EP` byte turns a keystroke into a halt
  or a memory write.
- `FCRC` covers bytes 0 through `2 + LEN` inclusive — the whole frame including
  `HCRC`. It decides whether the frame is committed.

Initial value `0xFF` rather than `0x00` is deliberate: an all-zeros frame must not
validate. Stuck-low and stuck-high `MOSI` are additionally caught by the reserved
`EP` values of §6, so the two mechanisms are independent.

### 5.5 Acceptance rules

Evaluated in order. Any failure aborts the frame with no effect on any endpoint.

1. `EP` is not `0x00` and not `0xFF`, else `NACK_EP_RESERVED`.
2. `HCRC` matches, else `NACK_HCRC`. The frame is drained without dispatch.
3. `EP` is assigned and implemented, else `NACK_EP_UNKNOWN`.
4. The endpoint is not held in reset and is enabled, else `NACK_EP_RESET`.
5. `EPSTAT ≥ LEN` for SINK and DUPLEX endpoints, else `NACK_FULL`.
6. `LINK_CSN` remains asserted for the full `4 + LEN` bytes, else `NACK_SHORT`.
7. `FCRC` matches, else `NACK_FCRC`.

On success the result is `ACK`. The result is reported in `LASTRES` of the
following frame, and also readable from endpoint `0x01`.

| Code | Name |
|---|---|
| `0x00` | `ACK` |
| `0x01` | `NACK_HCRC` |
| `0x02` | `NACK_FCRC` |
| `0x03` | `NACK_EP_RESERVED` |
| `0x04` | `NACK_EP_UNKNOWN` |
| `0x05` | `NACK_EP_RESET` |
| `0x06` | `NACK_FULL` |
| `0x07` | `NACK_SHORT` |

### 5.6 Commit and retraction

Rules 6 and 7 are evaluated after the payload has been clocked in, so an endpoint
may already have consumed bytes. Two disciplines are permitted, declared per
endpoint in §6:

**Buffered.** The endpoint stages the frame and commits on `ACK`. Required for any
endpoint whose payload is not idempotent — all queue endpoints. Costs one buffer
of the endpoint's maximum frame size.

**Streaming.** The endpoint commits incrementally. Permitted only where the
payload is idempotent and absolutely addressed, so that verbatim retransmission of
a rejected frame is harmless. The Debug Agent bulk write of §9 is the only such
case, and §9.2 exists to make it true.

---

## 6. Endpoint map

| `EP` | Name | Class | Commit | Reset domain | Purpose |
|---|---|---|---|---|---|
| `0x00` | reserved | — | — | — | Stuck-low `MOSI` detection. Always rejected. |
| `0x01` | `LINK` | DUPLEX | buffered | router | ID and version, gateware build ID, echo, endpoint enumeration, error counters, `ERR_STICKY` clear |
| `0x02` | `DBG` | DUPLEX | streaming (§9) | router front / system core | Debug Agent: memory read and write, CPU halt/step/run, registers, trace, TLB. Carries the `bios.bin` load. |
| `0x03` | `HID` | SINK | buffered | system | Keyboard and mouse events, EC → CPU |
| `0x04` | `SYS` | DUPLEX | buffered | system | Power, battery, thermal, RTC; EC ↔ CPU |
| `0x05`–`0x06` | reserved for growth | — | — | — | Covered by the `STATUS` pending bitmap |
| `0x07`–`0xFE` | unassigned | — | — | — | `NACK_EP_UNKNOWN` |
| `0xFF` | reserved | — | — | — | Stuck-high `MOSI` detection. Always rejected. |

The pending bitmap in `STATUS` bits 5–0 caps the practical endpoint count at six.
This is judged sufficient; growth beyond it requires a status-format revision, not
a transport revision.

`LINK` must answer whenever the router is alive, including while every other
endpoint is held in reset. It is the only endpoint with that obligation.

---

## 7. Flow control and head-of-line rules

### 7.1 Dispatch is at header completion, not after a shared queue

A shared inbound FIFO with a dispatcher behind it produces head-of-line blocking
in exactly the worst circumstance: the CPU hangs, stops draining the HID queue, a
keyboard frame stalls at the head, and debug frames queue behind it precisely when
they are needed.

**Rule.** The router dispatches on completion of the header, into a per-endpoint
sink. There is no shared payload buffer. A full endpoint rejects its own frames
via rule 5 of §5.5 and affects no other endpoint.

### 7.2 Rejection is pre-emptive, not retrospective

`EPSTAT` is presented in `MISO` byte 2, before the EC has sent any payload. An EC
that reads it can abandon the frame by releasing `LINK_CSN` before byte 3, costing
three bytes rather than a full frame. The EC firmware should do this; the
`NACK_FULL` path in §5.5 exists for the case where it does not.

### 7.3 Latency bound

The maximum frame of 259 bytes is the bound on the delay one endpoint can impose
on another. At 10 MHz that is approximately 207 µs. This is the reason `LEN` is
one byte: a 16-bit length would allow a bulk memory transfer to lock out HID for
tens of milliseconds.

### 7.4 Overhead

Four bytes of framing per payload. At the 250-byte payloads of §9 this is 1.6 %.

---

## 8. Clock and reset domains

### 8.1 Domains

The header FSM, both CRC generators and `EPSTAT` generation live in the `SCK`
domain. Endpoint delivery crosses into the core domain through a per-endpoint
asynchronous FIFO with Gray-coded pointers. There is one such crossing per
endpoint and exactly one SPI front end, replacing the two crossings of Rev B.

### 8.2 Frame state reset

All frame-level state — byte counter, CRC accumulators, header registers — is
reset by the rising edge of `LINK_CSN`. An aborted frame therefore costs nothing
beyond itself, and there is no resynchronisation procedure.

### 8.3 The router must outlive its endpoints

**Rule.** The router is in the same reset domain as the Debug Agent front end. It
must not be in any reset domain that an endpoint can assert.

Rationale: if the Debug Agent resets Helium's system side and the router falls with
it, the command destroys the transport carrying it. Under this rule the router
survives, and reports `NACK_EP_RESET` on behalf of any endpoint that is down.
Router state clears only on FPGA reconfiguration.

### 8.4 INV-1 — Debug Agent isolation

The Debug Agent's request port has exactly one source: endpoint `0x02` of the
router. No CPU-writable register, and no path reachable from the 65816 address
space, may drive it. This is the invariant that Rev B attributed to `DBG_CSN`; it
is a connectivity constraint and is to be verified structurally, not by
inspection of the decode logic.

---

## 9. Endpoint `0x02` — Debug Agent, and the boot memory load

### 9.1 Consolidation

Boot memory loading and Debug Agent memory writes are the same operation: a
requester in Helium's arbiter writing physical memory through the cache
controller. Rev B expressed them as two mechanisms — the `MEM_WRITE` mailbox
command class and the Debug Agent register path. They are unified here into the
Debug Agent, which will in any case need to load memory later in the machine's
life.

Consequences:

- One fewer requester in the arbiter.
- Every boot exercises the debug write path, so it cannot rot unnoticed.
- A failed `bios.bin` load produces a `DBG_ERR` code the EC can report on the
  console, rather than a silent bad boot.
- The boot path now depends on the Debug Agent state machine. See §15.

The `MEM_WRITE` mailbox command class is withdrawn.

### 9.2 `WRITE_BURST` payload format

The register-at-a-time model of the existing Debug Agent map (`DBG_ADDR`,
`DBG_DATA`, `DBG_WIDTH`, `DBG_CMD`) is unusable for tens of kilobytes. A bulk form
is added:

```
payload  0        1..3               4 … LEN-1
         OPCODE   ADDR[23:16..7:0]   data bytes
```

`OPCODE = 0x10` (`WRITE_BURST`). Address is absolute, 24 bits, big-endian.
Maximum data per frame is 251 bytes.

**The address is absolute in every frame and no address state persists between
frames.** Auto-increment operates within a frame only. This is what makes the
streaming commit of §5.6 sound: a frame rejected by `FCRC` is retransmitted
verbatim and rewrites the same bytes to the same addresses. There is no pointer to
desynchronise.

`OPCODE = 0x11` (`READ_BURST`) is symmetric: the `MOSI` payload carries opcode,
address and a length byte, the `MISO` payload returns the data.

### 9.3 Load-time budget

At 10 MHz with 251-byte data frames, throughput is approximately 1.23 MB/s. A
64 KB `bios.bin` loads in about 53 ms; 256 KB in about 213 ms. Boot time is not a
constraint on this decision at any plausible image size. The measured size remains
open item #2 of DN-HW-ECIF-001.

### 9.4 Watchdog

The Debug Agent's per-transaction watchdog now fires during boot. Its timeout must
accommodate worst-case SDRAM latency — row activation coinciding with a refresh
cycle — or the boot load will abort spuriously under load. See §14 item 3.

---

## 10. Revised boot sequence

### 10.1 Sequence

| Step | Action |
|---|---|
| 1 | EC reads its own in-package flash |
| 2 | EC sequences rails per Sheet 1.1 REV C |
| 3 | EC configures Helium, NEON and FPGA-C via SPI slave, `CFG_SS_A/B/C` |
| 4 | EC parks all `CFG_SS` lines low after the corresponding `CDONE` (§10.2) |
| 5 | EC observes the 49-cycle post-`CDONE` window before further bus activity |
| 6 | EC reads endpoint `0x01`; verifies magic, protocol version and build ID |
| 7 | EC loads `bios.bin` via `WRITE_BURST` frames to endpoint `0x02`, CPU in reset |
| 8 | EC optionally verifies via `READ_BURST` |
| 9 | EC releases `RESB` |

Step 6 is new and cheap, and turns a mis-programmed or partially configured Helium
into a diagnosable condition rather than a hang at step 9.

### 10.2 `CFG_SS` parking rule

Because iCE40 configuration pins revert to user I/O after `CDONE`, `CFG_SS_A`
could be reused as the runtime select and save a further pin per side. **This is
rejected.**

The iCE40 samples `SPI_SS_B` on exit from configuration reset to choose between
slave and master configuration mode. With that line idling high during normal
operation, an unintended reset of Helium — a rail dropout, a glitch on `CRESET_B`
— would bring it up in master mode, where it drives `SCK` and contends with the EC.

**Rule.** The EC parks each `CFG_SS` line low once the corresponding `CDONE`
rises, and holds it low for the lifetime of the session. Any accidental reset of
any of the three FPGAs then falls into slave mode, which is passive. Helium's
gateware must not drive `CFG_SS_A` as an output after configuration.

To be confirmed against the iCE40 programming and configuration guide — §14
item 7.

---

## 11. Bring-up order

The router is a single point of failure, and the Debug Agent cannot be used to
debug the router. Mitigation is ordering, not redundancy. Note that the two
independent front ends of Rev B offered less isolation than they appeared to: they
would have been two instances of the same module carrying the same bugs.

| Step | Deliverable | Success criterion |
|---|---|---|
| 1 | SPI front end + router + endpoint `0x01` only | ID read returns magic and version; deliberate `HCRC` corruption returns `NACK_HCRC`; `EP = 0x00`, `0xFF` and an unassigned value each return the correct code |
| 2 | Endpoint `0x02`, `WRITE_BURST`/`READ_BURST` against a scratch region | Write-then-read of a pseudorandom block matches; a deliberately corrupted `FCRC` frame retransmitted verbatim converges |
| 3 | Endpoint `0x03` | HID events reach the CPU queue; a full queue returns `NACK_FULL` without disturbing endpoint `0x02` |
| 4 | Endpoint `0x04` | — |

Unimplemented endpoints return `NACK_EP_UNKNOWN` throughout, so each step is
testable in isolation.

The head-of-line property must be tested explicitly at step 3: fill the HID queue,
stop draining it, and confirm that Debug Agent throughput on endpoint `0x02` is
unaffected. This is the failure the architecture exists to prevent, and it will
not appear by accident.

---

## 12. Failure modes

| Mode | Consequence | Mitigation |
|---|---|---|
| Corrupted `EP` byte | Keystroke interpreted as halt or memory write | `HCRC`, §5.4 |
| Corrupted payload | Bad bytes committed to memory or a queue | `FCRC` plus the commit discipline of §5.6 |
| `MOSI` stuck low or high | Frames with `EP = 0x00` / `0xFF` | Reserved values, §6; independently caught by `HCRC` |
| CS released mid-frame | Partial frame | `NACK_SHORT`; frame state cleared on CS rising edge, §8.2 |
| Endpoint reset by a Debug Agent command | Transport destroyed by the command carrying it | Router reset domain, §8.3 |
| Endpoint queue full | Head-of-line blocking of other endpoints | Per-endpoint sinks, §7.1; pre-emptive `EPSTAT`, §7.2 |
| Router bug | Total loss including debug | Bring-up order, §11 |
| Accidental Helium reset during operation | FPGA enters SPI master mode and contends for the bus | `CFG_SS` parking, §10.2 |
| Debug Agent FSM fault during boot | System does not boot | Fallback endpoint, §15 |

---

## 13. Impact on other design notes

### DN-HW-ECIF-001, Revision C

| Section | Edit |
|---|---|
| §3.1 | Remove `DBG_CSN`. Rename `MBX_CSN` to `LINK_CSN`. EC pins 15 → 14. Add the `CFG_SS` parking rule of §10.2. |
| §3.4 | EC subtotal 25 → 24. |
| §3.5 | Chip selects 2 → 1. Helium total 12 → 11. |
| §5 (mailbox) | Replace the mailbox command-class model with a reference to this note. Withdraw the `MEM_WRITE` command class; it is now §9.2 of this note. |
| §7 (boot) | Replace the boot sequence with §10.1. |
| §11 | Open item #1: the chip-select merge is no longer a contingency; restate the remaining TQ144 count against 11 pins. Add items 2, 3 and 7 of §14 below. |
| §13 | `DBG_CSN`: "Implemented. Debug Agent is a separate SPI slave inside Helium" → "Withdrawn. Single link with header-based routing, DN-HW-MBXLINK-001." |
| §12 | Revision history: record that Rev B's `DBG_CSN` rationale (structural isolation) was overstated, and that the property is preserved by INV-1. The decision is reversed for reasons of correctness of argument, not only pin economy. |

### Helium Debug Agent — Register Map & Console Protocol

Transport section only: single link, endpoint `0x02`. `DBG_CSN` removed from the
physical interface. Add `WRITE_BURST` and `READ_BURST` (§9.2). Register map
otherwise unchanged. The `DBG_CSN` allocation open item and the SPI→core CDC open
item are both closed by this note.

### DN-PLAN-PHASES-001

Phase 0 scope table, Debug Agent row: "Separate SPI slave (`DBG_CSN`); wire
protocol and command set" → "Endpoint `0x02` on the unified link; router, frame
format and command set". Add the router and endpoint `0x01` to the Phase 0
sequencing as its own step, ahead of the Debug Agent. HAT pin budget 53 → 52.

### Colorlight carrier

Block C is recorded as 8 pins for the RP2040 SPI debug link. If `DBG_CSN` is among
them, Block C becomes 7 and Block C spare increases by one. To be confirmed —
§14 item 5.

---

## 14. Open items

| # | Item | Blocks |
|---|---|---|
| 1 | Measured `bios.bin` size, for the §9.3 boot-time budget and the ECIF §6.4 flash layout | Flash layout |
| 2 | Cache controller state during the boot load: whether Debug Agent writes at boot bypass the cache, write through, or allocate. The CPU is in reset so coherence is trivial, but the state must be defined before the first `WRITE_BURST` | Gateware |
| 3 | Debug Agent per-transaction watchdog timeout against worst-case SDRAM latency (row activation coinciding with refresh), now that it is boot-critical | Gateware |
| 4 | Achievable `SCK` ceiling for the iCE40 SPI slave plus the §8.1 crossing. 10 MHz assumed throughout until measured | Boot budget, §9.3 |
| 5 | Whether Colorlight carrier Block C's 8 pins include `DBG_CSN` | Carrier |
| 6 | Whether endpoint `0x04` absorbs the CPU → EC direction or a dedicated SOURCE endpoint is warranted | Endpoint map |
| 7 | Confirmation against the iCE40 programming guide that `SPI_SS_B` sampling at reset exit behaves as §10.2 assumes, and that parking low is safe for all three devices | Schematic capture |

---

## 15. Abandonment conditions

**Boot dependency on the Debug Agent.** If the Debug Agent state machine proves
too heavy or too unstable to be boot-critical by gate E1, `bios.bin` loading falls
back to a dedicated write-only SINK endpoint at `0x05`, using the `WRITE_BURST`
payload format of §9.2 unchanged. The transport is unaffected and this note is not
reopened. The cost of reverting is one small gateware block and one line in the
endpoint table.

**Router as a Phase 0 blocker.** If the router obstructs Phase 0 bring-up on the
Spartan-6 rig, `DBG_CSN` may be reinstated **on the HAT only**, where pins are
plentiful. Phase 0 RTL is disposable by DN-PLAN-PHASES-001, so this does not
propagate to the target design.

**`FCRC` on every frame.** Retained unconditionally. If frame-level integrity is
ever judged too costly, the correct response is to raise `SCK`, not to weaken
integrity on a link that carries memory writes.

---

## 16. Revision history

**Revision A — this document.** Single serial link with header-based endpoint
routing, replacing the `MBX_CSN` / `DBG_CSN` pair of DN-HW-ECIF-001 Rev B. Frame
format with `HCRC` and `FCRC`. Per-endpoint sinks with pre-emptive flow control.
Router reset domain fixed above all endpoints. Boot memory load consolidated into
the Debug Agent as endpoint `0x02`; `MEM_WRITE` mailbox class withdrawn.
`CFG_SS` parking rule added.

---

## 17. Decisions closed

| Decision | Resolution |
|---|---|
| `DBG_CSN` | Withdrawn. One chip select, `LINK_CSN`. |
| Debug Agent isolation | Preserved by INV-1 (§8.4), a connectivity constraint, not by a chip select. |
| Dispatch point | At header completion, into per-endpoint sinks. No shared payload buffer. |
| Header integrity | `HCRC` mandatory. Routing corruption is the catastrophic case. |
| Frame integrity | `FCRC` mandatory on every frame, every endpoint. |
| Maximum frame | 259 bytes. `LEN` is one byte, deliberately. |
| Router reset domain | Above all endpoints. Survives any endpoint reset. |
| Boot memory load | Debug Agent, endpoint `0x02`. `MEM_WRITE` mailbox class withdrawn. |
| Bulk write addressing | Absolute per frame. No cross-frame pointer state. |
| Reuse of `CFG_SS_A` as runtime select | Rejected. Parked low instead, §10.2. |
| Endpoint count ceiling | Six, set by the `STATUS` pending bitmap. Judged sufficient. |
