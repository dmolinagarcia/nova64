# Helium Debug Agent — Register Map & Console Protocol

**Project:** noVa64
**Subsystem:** FPGA-A ("Helium") debug agent + RP2040 serial monitor
**Document status:** Design specification, pre-schematic
**Date:** 2026-08-14

---

## 1. Purpose and scope

This document specifies the **Debug Agent**, a hardware block inside Helium
(FPGA-A) that performs memory and bus accesses on behalf of an external
controller, and the **serial console** running on the RP2040 that drives it.

The goal is to have, from the earliest bring-up stage, a way to:

- read and write any location in physical memory (SRAM and SDRAM),
- read and write through the MMU translation path once it exists,
- generate real bus cycles visible to other devices on the CPU bus,
- halt, single-step and trace the W65C816S,
- do all of the above from a terminal over USB-CDC or a physical UART.

This is instrumentation, not a product feature. It exists to make the board
debuggable. It is expected to remain in the design permanently, gated off
rather than removed.

### 1.1 Terminology used in this document

Terms are defined at first use. A term is never used before it is defined.

- **Helium** — FPGA-A. Owns the MMU, the cache controller, the SRAM and SDRAM
  controllers, the bus arbiter, and the system peripherals.
- **Debug Agent (DA)** — the block specified here. A hardware requester inside
  Helium that issues memory and bus transactions on command.
- **Console** — firmware on the RP2040 that presents a text monitor to the user
  and translates typed commands into Debug Agent register operations.
- **Physical address** — a 27-bit address that identifies a location in the
  system's physical memory map, with no address translation applied.
- **Virtual address** — a 24-bit address as seen by a process, which must be
  translated by the MMU (using the process's ASID and page tables) before it
  identifies a physical location.
- **ASID** — Address Space Identifier. A small integer that names which
  process's translation context the MMU should use.
- **Internal access** — a transaction that is satisfied entirely inside Helium
  (SRAM, SDRAM, or Helium's own registers). No signal appears on the CPU bus.
- **External cycle** — a transaction in which Helium drives the CPU bus pins
  (address, data, control) so that devices outside Helium observe a bus cycle
  indistinguishable from one issued by the W65C816S.
- **PHI2 stall** — the existing mechanism by which Helium freezes the CPU clock
  to steal bus time. The W65C816S is fully static; holding PHI2 is safe and is
  already used for bounded-time cache fills.
- **BE** — Bus Enable, an input to the W65C816S. When low, the CPU tristates its
  address, data and RWB drivers, releasing the bus.

---

## 2. Architectural decisions

### 2.1 The RP2040 is not a bus master

**Decision:** the RP2040 never drives the system bus. It sends commands to the
Debug Agent, which performs the access.

**Rationale:**

1. **Pin budget.** Direct mastering would require ~24 address lines, 16 data
   lines and control — well beyond what remains after the RP2040's committed
   functions (FPGA configuration SPI, microSD, PIO-USB HID, ANX6345 I²C, power
   sequencing, resets).
2. **Reuse.** Helium already contains the SRAM controller, the SDRAM controller
   with refresh, the cache controller, and the arbiter. A second, independent
   path to memory would duplicate all of it and would be a second source of
   truth about memory state.
3. **Coherence.** An external master bypassing the cache controller can read
   stale data whenever a dirty line is resident in the SRAM cache. As a
   requester inside Helium, the Debug Agent goes through the same path as
   everyone else and is coherent by construction.

**Consequence:** the Debug Agent is architecturally a peer of the CPU port and
the FPGA-B port in the existing arbiter. It is not a special case.

### 2.2 Helium owns BE

**Decision:** the `BE` pin of the W65C816S is driven by Helium, not by an RP2040
GPIO.

**Rationale:** taking the bus requires two things to happen in a defined order:
the CPU must release its drivers, and Helium must begin driving. If `BE` is
controlled across the SPI link, that ordering becomes a race across a clock
domain crossing with millisecond-scale latency. With `BE` under Helium's
control, the handoff is a synchronous state transition.

**Schematic impact — must be resolved before capture:**

- `BE` routes from Helium to the CPU socket.
- Helium's CPU-side address and data pins must be bidirectional (`inout`), not
  input-only. This affects I/O bank planning and the pin assignment sheet.

### 2.3 The Debug Agent register file is SPI-only

**Decision:** the Debug Agent's registers are reachable only over the RP2040
link. They are **not** exposed in bank `$FF` or anywhere else in the CPU's
address space.

**Rationale:** the Debug Agent is unconditionally privileged — it can write bank
`$FF`, bypass permission checks, and halt the CPU. Exposing it to software would
make the entire protection model depend on kernel correctness. Keeping the
control path physically separate means no amount of software failure can reach
it, and the console remains usable precisely when software has failed, which is
when it is needed.

### 2.4 Two orthogonal access axes

Every Debug Agent memory transaction is characterised by two independent
choices.

**Address space:**

| Mode | Address | Path | Available from |
|---|---|---|---|
| Physical | 27-bit, raw | Straight to cache/memory controllers | E1 |
| Virtual | 24-bit + ASID | Through MMU translation, page-table walk | E4 |

Physical mode is what is needed while the MMU is itself under test. Virtual mode
exists to inspect what the kernel believes it is looking at.

**Transaction scope:**

| Mode | Reaches | Visible to FPGA-B / external devices |
|---|---|---|
| Internal | SRAM, SDRAM, Helium registers | No |
| External cycle | Everything the CPU can reach | Yes |

External cycles are required to touch anything decoded outside Helium — notably
the VRAM aperture at bank `$FE`, which FPGA-B services by watching the CPU bus
directly.

---

## 3. Physical interface

### 3.1 Link

SPI, RP2040 as controller, Helium as peripheral. Mode 0 (CPOL=0, CPHA=0). Target
clock 16 MHz initially; the limit is the synchroniser depth on Helium's side, not
the link.

The Debug Agent uses a **dedicated chip select**, separate from the one used for
FPGA configuration. Sharing the configuration SPI bus data lines is acceptable;
sharing the chip select is not, because the configuration path must remain
functional when the Debug Agent is in an unknown state.

| Signal | Direction | Notes |
|---|---|---|
| `DBG_SCK` | RP2040 → Helium | Shared with config SPI |
| `DBG_MOSI` | RP2040 → Helium | Shared with config SPI |
| `DBG_MISO` | Helium → RP2040 | Shared with config SPI |
| `DBG_CSN` | RP2040 → Helium | **Dedicated**, active low |
| `DBG_IRQN` | Helium → RP2040 | Optional; asserts on trace trigger or CPU halt |
| `DEBUG_ENABLE` | Strap/jumper | See §9 |

### 3.2 Frame format

Every transaction begins with a one-byte header:

```
bit 7    : 0 = write, 1 = read
bits 6:0 : register address (0x00 – 0x7F)
```

Followed by one or more data bytes. For registers wider than 8 bits, bytes are
transferred **least-significant first**.

`DBG_CSN` must be deasserted between frames. A frame is committed on the rising
edge of `DBG_CSN`; a truncated frame is discarded and sets `ERR_FRAME`.

### 3.3 Burst frames

Two registers support unbounded burst transfer while `DBG_CSN` remains asserted:
`DBG_FIFO` (§4.9) and `TRC_FIFO` (§7.3). This exists so that dumping a kilobyte
of memory or a full trace buffer does not cost one frame per byte.

---

## 4. Register map

All registers reset to zero unless stated. Reserved bits read as zero and must
be written as zero.

### 4.0 Summary

| Addr | Name | Access | Width | Purpose |
|---|---|---|---|---|
| 0x00 | `DBG_ID` | RO | 16 | Magic + version |
| 0x01 | `DBG_CTRL` | RW | 8 | Mode flags |
| 0x02 | `DBG_STATUS` | RO | 8 | Busy / ready / error |
| 0x03 | `DBG_ERR` | RO/W1C | 8 | Error code, write-1-to-clear |
| 0x04 | `DBG_ADDR` | RW | 32 | Address (27 physical / 24 virtual) |
| 0x08 | `DBG_DATA` | RW | 16 | Data port |
| 0x0A | `DBG_WIDTH` | RW | 8 | Access width / byte enables |
| 0x0B | `DBG_ASID` | RW | 16 | ASID for virtual-mode access |
| 0x0C | `DBG_CMD` | WO | 8 | Command trigger |
| 0x0D | `DBG_COUNT` | RW | 16 | Burst length in elements |
| 0x10 | `CPU_CTRL` | RW | 8 | Reset / halt / step |
| 0x11 | `CPU_STATUS` | RO | 8 | CPU state |
| 0x12 | `CPU_CYCLES` | RO | 32 | Free-running bus cycle counter |
| 0x18 | `TRC_CTRL` | RW | 8 | Trace enable / mode |
| 0x19 | `TRC_STATUS` | RO | 8 | Trace state, fill level |
| 0x1A | `TRC_TRIG_ADDR` | RW | 32 | Trigger address |
| 0x1E | `TRC_TRIG_MASK` | RW | 32 | Trigger don't-care mask |
| 0x1F | `TRC_FIFO` | RO | burst | Trace record readout |
| 0x20 | `DBG_FIFO` | RW | burst | Bulk data port |
| 0x30 | `TLB_INDEX` | RW | 16 | TLB entry selector |
| 0x31 | `TLB_ENTRY` | RO | 64 | TLB entry readout |
| 0x38 | `MMU_MIRROR` | RO | 8 | Read-only view of MMU status |

### 4.1 `DBG_ID` (0x00, RO, 16-bit)

Reads `0x6516`. Byte 0 is `0x16`, byte 1 is `0x65`.

Purpose: the console reads this first. A correct value proves the SPI link,
Helium's configuration, and the clock are all working, before anything else is
attempted. A read of `0x0000` or `0xFFFF` means the bitstream did not load.

Bits 15:8 of a second read (register 0x00 read twice in one frame) return the
gateware build revision. Bump it on every synthesis.

### 4.2 `DBG_CTRL` (0x01, RW, 8-bit)

| Bit | Name | Meaning |
|---|---|---|
| 0 | `ENABLE` | Master enable for the Debug Agent |
| 1 | `VIRT` | 0 = physical address space, 1 = virtual |
| 2 | `EXTCYC` | 0 = internal access, 1 = generate external bus cycle |
| 3 | `AUTOINC` | Advance `DBG_ADDR` by the access width after each access |
| 4 | `NOCACHE` | Bypass the SRAM cache (see §6 — hazardous) |
| 5 | `STALL` | 0 = cycle-steal when possible, 1 = always stall PHI2 |
| 7:6 | reserved | |

`ENABLE` gates all command execution. It is cleared by reset and by
`DEBUG_ENABLE` being deasserted.

### 4.3 `DBG_STATUS` (0x02, RO, 8-bit)

| Bit | Name | Meaning |
|---|---|---|
| 0 | `BUSY` | A command is in progress |
| 1 | `DONE` | Last command completed successfully |
| 2 | `ERROR` | Last command failed; see `DBG_ERR` |
| 3 | `FIFO_EMPTY` | Data FIFO empty |
| 4 | `FIFO_FULL` | Data FIFO full |
| 5 | `CPU_HALTED` | CPU is currently halted |
| 6 | `TRC_TRIGD` | Trace trigger has fired |
| 7 | `DBG_READY` | Agent idle and accepting commands |

The console polls `BUSY`. It must never assume a command completes within the
SPI frame that issued it: an SDRAM access with a page miss and an intervening
refresh can take considerably longer than a single-cycle SRAM hit.

### 4.4 `DBG_ERR` (0x03, RO / write-1-to-clear, 8-bit)

| Bit | Name | Cause |
|---|---|---|
| 0 | `ERR_TIMEOUT` | Watchdog expired; memory did not respond |
| 1 | `ERR_UNMAPPED` | Virtual mode: no valid PTE for the address |
| 2 | `ERR_PERM` | Virtual mode: PTE permissions forbid the access |
| 3 | `ERR_ARB` | Arbiter denied the request past its deadline |
| 4 | `ERR_CMD` | Unrecognised or illegal command encoding |
| 5 | `ERR_STATE` | Command requires a state the agent is not in |
| 6 | `ERR_FRAME` | Malformed or truncated SPI frame |
| 7 | `ERR_ALIGN` | Address not aligned for the requested width |

The agent latches the **first** error and refuses further commands until
`DBG_ERR` is cleared. This prevents a scripted burst from silently producing a
page of garbage after the first failure.

### 4.5 `DBG_ADDR` (0x04, RW, 32-bit)

Physical mode: bits 26:0 are the physical address; bits 31:27 must be zero.

Virtual mode: bits 23:0 are the virtual address; bits 31:24 must be zero. The
ASID comes from `DBG_ASID`.

With `AUTOINC` set, the register advances by the access width after each
completed access. It does not advance on error.

### 4.6 `DBG_DATA` (0x08, RW, 16-bit)

Write data source for `CMD_WRITE`; read data destination for `CMD_READ`. For
8-bit accesses only the byte selected by `DBG_WIDTH` is meaningful.

### 4.7 `DBG_WIDTH` (0x0A, RW, 8-bit)

| Value | Meaning |
|---|---|
| 0x01 | 8-bit access, low byte |
| 0x02 | 8-bit access, high byte |
| 0x03 | 16-bit access |

The system SRAM (IS61WV102416) is a ×16 device with separate upper and lower
byte enables. Byte-granular writes require the correct `UB#`/`LB#` assertion.
Getting this wrong produces a very specific and very confusing failure — byte
writes appear to work but corrupt the adjacent byte — and it is worth testing
explicitly in the E1 memory test rather than discovering it later through a
kernel bug.

### 4.8 `DBG_CMD` (0x0C, WO, 8-bit)

Writing this register triggers execution. The write is rejected with `ERR_STATE`
if `BUSY` is set.

| Code | Name | Action |
|---|---|---|
| 0x00 | `CMD_NOP` | No operation; clears `DONE` |
| 0x01 | `CMD_READ` | Read one element into `DBG_DATA` |
| 0x02 | `CMD_WRITE` | Write `DBG_DATA` to memory |
| 0x03 | `CMD_READ_BURST` | Read `DBG_COUNT` elements into the FIFO |
| 0x04 | `CMD_WRITE_BURST` | Write `DBG_COUNT` elements from the FIFO |
| 0x05 | `CMD_FILL` | Write `DBG_DATA` to `DBG_COUNT` consecutive elements |
| 0x10 | `CMD_CACHE_FLUSH` | Flush the entire SRAM cache to SDRAM |
| 0x11 | `CMD_CACHE_INVAL` | Invalidate the cache without writeback |
| 0x12 | `CMD_CACHE_FLUSH_LINE` | Flush the line containing `DBG_ADDR` |
| 0x20 | `CMD_TLB_PROBE` | Translate `DBG_ADDR`; result in `TLB_ENTRY` |
| 0x21 | `CMD_TLB_FLUSH` | Invalidate the entire TLB |
| 0x22 | `CMD_PTWALK` | Full page-table walk, bypassing the TLB |
| 0x30 | `CMD_ABORT` | Cancel the in-flight command |

`CMD_CACHE_INVAL` discards dirty data. It is destructive and is provided only
for recovering a wedged cache during gateware bring-up.

`CMD_PTWALK` differs from `CMD_TLB_PROBE` in that it always reads the page table
from memory. Comparing the two is the direct way to detect a stale TLB entry,
which is the failure mode the `CTX_SET_PTBASE` / `CTX_SET_ASID` ordering
invariant exists to prevent.

### 4.9 `DBG_FIFO` (0x20, RW, burst)

A 512-byte FIFO between the SPI link and the memory path. Read frames drain it;
write frames fill it. Depth is chosen so that a burst can stay ahead of a 16 MHz
SPI link without stalling on SDRAM page misses.

The console must respect `FIFO_FULL` on writes and `FIFO_EMPTY` on reads. The
agent does not stretch the SPI clock.

---

## 5. Access state machine

```
IDLE
 │  write DBG_CMD
 ▼
DECODE ──────── illegal ──────► ERROR
 │
 ├─ VIRT ─► TRANSLATE ─ fault ─► ERROR
 │              │
 │◄─────────────┘
 ▼
ARBITRATE ──── deadline ──────► ERROR
 │  grant
 ▼
 ├─ EXTCYC=0 ─► MEM_ACCESS ─┐
 │                          │
 └─ EXTCYC=1 ─► BUS_TAKE    │
                 │          │
                 ▼          │
              BUS_CYCLE     │
                 │          │
                 ▼          │
              BUS_RELEASE ──┤
                            ▼
                        COMPLETE ──► IDLE
```

**BUS_TAKE / BUS_RELEASE** is the only sequence that touches the CPU. It is:

1. Request the bus from the arbiter.
2. Assert PHI2 stall; wait for the current CPU cycle to reach a clean boundary.
3. Assert `BE` low. Wait the CPU's tristate turnaround time (a fixed number of
   Helium clocks, derived from the W65C816S datasheet at 3.3 V).
4. Drive address, data and control. Run the cycle.
5. Release the drivers, wait turnaround, deassert `BE`.
6. Release PHI2. Release the arbiter.

Step 6 must satisfy the minimum PHI2 pulse width on restart. The existing
glitch-free PHI2 gating logic is reused unchanged; the Debug Agent is simply
another client of it.

---

## 6. Coherence rules

**Rule 1 — the Debug Agent goes through the cache controller.**

A physical read of an SDRAM address whose line is dirty in the SRAM cache must
return the cached value, not the SDRAM value. The Debug Agent therefore issues
its accesses to the cache controller, exactly as the CPU port does.

This is not negotiable. A debug tool that reports memory contents which differ
from what the CPU sees is worse than no debug tool, because it produces
confident wrong answers during exactly the sessions where the engineer has least
ability to detect them.

**Rule 2 — `NOCACHE` is for gateware bring-up only.**

`DBG_CTRL.NOCACHE` routes the access directly to the SDRAM controller. Its only
legitimate use is validating the cache controller itself: write through the
cache, flush, read with `NOCACHE`, compare. The console prints a warning banner
whenever `NOCACHE` is set.

**Rule 3 — external cycles are coherent by construction.**

An external cycle is decoded by exactly the same logic as a CPU cycle. If the
address falls in cacheable space, it goes through the cache. If it falls in the
`$FE` VRAM aperture or bank `$FF`, it does not. No special-casing.

**Rule 4 — writes to page tables require a TLB flush.**

The Debug Agent will happily write a PTE and leave the TLB holding the old
translation. It does not auto-invalidate. The console's `pt write` command
issues `CMD_TLB_FLUSH` afterwards; direct register users must do so themselves.

---

## 7. CPU control and trace

### 7.1 `CPU_CTRL` (0x10, RW, 8-bit)

| Bit | Name | Meaning |
|---|---|---|
| 0 | `RESET` | Hold the W65C816S in reset |
| 1 | `HALT` | Request halt at the next clean bus-cycle boundary |
| 2 | `STEP` | Advance exactly one bus cycle (self-clearing) |
| 3 | `STEP_INSN` | Advance to the next opcode fetch (`VPA & VDA`) |
| 4 | `IRQ_MASK` | Hold IRQB deasserted while halted |
| 5 | `NMI_MASK` | Hold NMIB deasserted while halted |

Halting is implemented as an indefinite PHI2 stall. The W65C816S is fully static
and can be held in this state indefinitely.

Interrupt masking while halted matters: a system timer that keeps running while
the CPU is stopped will present a large backlog of pending interrupts on
resume, which looks like a kernel bug and is not.

### 7.2 What cannot be read

**The Debug Agent cannot read the W65C816S's internal registers.** The part has
no scan chain, no debug port, and no mechanism for external register readout.
A, X, Y, S, DP, DBR, PBR and P are not observable from outside.

Two workarounds, in order of preference:

1. **Inferred from trace.** A halt at an instruction boundary plus a trace
   buffer containing the preceding cycles lets the console reconstruct PC and,
   with disassembly, much of the register state. This is passive and always
   available.
2. **Debug stub.** A short routine in bank `$00` that pushes all registers to a
   known location and spins. The Debug Agent forces entry by driving an
   instruction onto the data bus during a fetch cycle, or the kernel enters it
   from a breakpoint trap. This is intrusive and perturbs the state being
   inspected, but is the only way to get authoritative register values.

The console exposes option 1 as `reg` and option 2 as `reg -f` (force). The
distinction must be visible to the user; silently perturbing state during
inspection is unacceptable in a debugger.

### 7.3 Trace buffer

A ring buffer in EBR capturing, per bus cycle: address (24), data (8), `RWB`,
`VPA`, `VDA`, `VPB`, `MLB`, `E`, and a cycle-counter delta. Approximately 48
bits per record.

Depth is a synthesis parameter. 1024 records is a reasonable starting point and
costs about six EBR blocks on the iCE40HX4K — cheap relative to its value.

`TRC_CTRL` (0x18):

| Bit | Name | Meaning |
|---|---|---|
| 0 | `TRC_EN` | Capture enable |
| 1 | `TRC_ARM` | Arm the trigger |
| 2 | `TRC_ONESHOT` | Stop capture when the buffer fills |
| 4:3 | `TRC_POS` | Trigger position: 00 = start, 01 = centre, 10 = end |
| 5 | `TRC_HALT_ON_TRIG` | Halt the CPU when the trigger fires |
| 6 | `TRC_FILTER_OPFETCH` | Capture only opcode fetches |

Trigger condition: `(address & ~TRC_TRIG_MASK) == (TRC_TRIG_ADDR & ~TRC_TRIG_MASK)`,
optionally qualified by `RWB` and by `VPA`/`VDA` encoded in the upper bits of
`TRC_TRIG_ADDR`.

This is a logic analyser for the price of one EBR block and a comparator. Build
it at E3. Retrofitting it after the first difficult kernel bug is the wrong
order.

---

## 8. Watchdog and failure containment

Every memory transaction is covered by a timeout counter. On expiry the agent:

1. Abandons the transaction,
2. Releases the bus and PHI2 (or leaves the CPU halted if it was already),
3. Sets `ERR_TIMEOUT`,
4. Returns to `IDLE`.

The timeout must be generous enough to cover a worst-case SDRAM access with an
intervening auto-refresh, and short enough that a human notices it as a delay
rather than a hang. 100 µs at the Helium core clock is the starting value.

**The agent must never be able to hang the console.** If it does, the engineer
loses the only view into a board that is, by hypothesis, already misbehaving.
This constraint takes precedence over transaction completion in every case.

A separate consideration: the existing system watchdog that converts hung cache
fills into NMI or ABORT must be **suppressed while the CPU is halted by the
Debug Agent**, or every debug session will end in a spurious abort.

---

## 9. Enable gating

The Debug Agent bypasses every protection mechanism in the system. It requires a
gate.

`DEBUG_ENABLE` is a Helium input. When deasserted, the Debug Agent:

- forces `DBG_CTRL.ENABLE` to zero and ignores writes to it,
- rejects all commands with `ERR_STATE`,
- continues to answer reads of `DBG_ID` and `DBG_STATUS` (so the console can
  report *why* it is not working, rather than appearing broken).

For the development boards, a pull-up and a jumper to ground. The point is not
security against an attacker with physical access — that is unachievable and not
a goal — but to make the privileged path an explicit, visible, deliberate state
of the board.

---

## 10. Console protocol

### 10.1 Transports

Two, both always active:

- **USB-CDC** over the RP2040's native USB.
- **Physical UART** on a 3-pin header, 115200 8N1, 3.3 V.

The physical UART is not redundant. At E0 there is no way to distinguish a
firmware fault from a USB stack fault, and the UART removes that ambiguity for
the cost of three pins. It also survives the RP2040 USB peripheral being
reconfigured for PIO-USB HID work.

Output is mirrored to both. Input is accepted from either, first-come.

### 10.2 Command grammar

Numbers are hexadecimal by default; a `#` prefix denotes decimal. Addresses may
carry a space qualifier: `p:` for physical (default), `v:` for virtual.

**Memory**

```
md  <addr> [len]              dump memory (default 256 bytes)
mw  <addr> <val> [val ...]    write values
mf  <addr> <len> <val>        fill
mc  <addr1> <addr2> <len>     compare two regions
ms  <addr> <len> <pattern>    search
mt  <addr> <len> [test]       memory test: march-c, addr-in-addr, walking-1
```

**Mode**

```
mode                          show current mode flags
mode phys | virt              select address space
mode int | ext                select internal / external cycle
mode asid <n>                 set ASID for virtual accesses
mode width 8 | 16             set access width
mode nocache on | off         cache bypass (prints a warning)
```

**CPU**

```
halt                          halt at next cycle boundary
run                           resume
step [n]                      single bus cycle
stepi [n]                     single instruction
reset [hold]                  pulse or hold CPU reset
reg                           registers inferred from trace
reg -f                        registers via forced debug stub (intrusive)
dis <addr> [n]                disassemble
```

**Trace**

```
trace arm [addr] [mask]       arm trigger
trace on | off
trace pos start|centre|end
trace dump [n]                dump records, disassembled
trace filter opfetch on|off
```

**MMU**

```
tlb dump                      all TLB entries
tlb probe <vaddr>             translate via TLB
tlb flush
pt walk <vaddr>               full table walk, showing each level
pt dump <asid>                summary of mapped ranges
cache flush | inval
cache stats                   hit/miss counters
```

**Storage and files**

```
ld <file> <addr>              load from microSD into memory
sv <addr> <len> <file>        save memory to microSD
boot <file>                   load, set reset vector, release CPU
```

**System**

```
id                            gateware ID and build revision
status                        full status register decode
err                           last error, decoded; clears it
help [cmd]
```

### 10.3 Console behaviour requirements

- **Every error is reported with its decoded cause.** Never a bare failure.
- **Long operations show progress.** A 32 MB SDRAM march test takes minutes; a
  silent console during that time is indistinguishable from a hang.
- **Ctrl-C aborts** the in-flight operation via `CMD_ABORT`.
- **`md` output is annotated** with the region name (SRAM cache, SDRAM, VRAM
  aperture, bank `$FF`) derived from the physical memory map.
- **The prompt shows the mode.** `p16>` for physical/16-bit, `v8:0042>` for
  virtual/8-bit/ASID 0x42. Mode errors are the most common source of confusing
  results, and making the mode ambient removes most of them.

---

## 11. Bring-up staging

| Stage | Debug Agent capability | Validates |
|---|---|---|
| **E0** | RP2040 only: LED, UART, USB-CDC, microSD, bitstream load | Power rails, RP2040 firmware, SD stack |
| **E1** | `DBG_ID` readable; physical SRAM peek/poke; `mt` | SPI link, Helium clock, SRAM controller, byte enables |
| **E2** | Physical SDRAM peek/poke; cache flush/inval; `cache stats` | SDRAM controller, refresh, cache controller, coherence |
| **E3** | CPU out of reset; halt/step/run; trace | PHI2 gating, `BE` handoff, arbiter, bus timing |
| **E4** | Virtual mode; TLB and page-table inspection | MMU, walker, ASID handling, table ordering invariant |
| **E5+** | External cycles to `$FE` and FPGA-B | Inter-FPGA bus visibility, VRAM aperture |

E1 is the stage that pays for the whole design. Being able to march-test SRAM
before any CPU exists converts the hardest class of bring-up bug — intermittent
memory faults appearing as random software misbehaviour — into a direct,
reproducible measurement.

---

## 12. Open items

Blocking schematic capture:

1. **`BE` routing** — confirm Helium drives it; allocate the pin.
2. **Bidirectional CPU-side pins on Helium** — confirm `inout` on address and
   data; check I/O bank and pin-count impact on the TQ144 assignment sheet.
3. **`DBG_CSN` pin allocation** on both RP2040 and Helium.
4. **`DEBUG_ENABLE` strap** — pin, pull, jumper footprint.
5. **Physical UART header** — 3-pin, placement, silkscreen.
6. **`DBG_IRQN`** — decide whether to spend the pin, or poll only.

Blocking gateware:

7. **ASID width** — `DBG_ASID` is specified as 16-bit; narrow it once the MMU
   decision lands. Carried over as a dependency from the MMU work.
8. **Turnaround timing for `BE`** — derive the exact clock count from the
   W65C816S datasheet at 3.3 V and the intended PHI2 frequency.
9. **Clock domain crossing** — the SPI domain and the Helium core domain are
   asynchronous. Specify the synchroniser and the command-handshake protocol
   formally; this is where subtle intermittent failures will otherwise live.
10. **Trace buffer depth** — pick a number against the remaining EBR budget
    after the cache tags are allocated.
11. **Arbiter priority** — where the Debug Agent sits relative to the CPU port,
    the FPGA-B port, and SDRAM refresh. Refresh must remain highest.

Deferred, not ruled out:

12. Breakpoint comparators (address match forcing halt) — a natural extension of
    the trace trigger, deferred to keep E3 small.
13. Watchpoints on data value as well as address.
14. GDB remote serial protocol stub on the RP2040, so a host debugger can drive
    the agent. Attractive, but only worth doing once the command set is stable.

---

## 13. Summary of decisions

| Decision | Rationale |
|---|---|
| RP2040 is not a bus master | Pin budget; reuse of Helium's memory path; coherence |
| Debug Agent is an arbiter peer | No duplicated memory path, no second source of truth |
| Helium drives `BE` | Bus handoff must be a synchronous transition, not a cross-domain race |
| Registers are SPI-only, not CPU-visible | Privileged path must not depend on software correctness |
| Accesses go through the cache controller | A debug tool that disagrees with the CPU is worse than none |
| Per-transaction watchdog | The console must never hang |
| `DEBUG_ENABLE` gate from day one | Make the privileged state explicit and visible |
| Physical UART alongside USB-CDC | Removes firmware/USB ambiguity at E0 for three pins |
| Trace buffer built at E3, not later | Logic analyser for one EBR block; retrofitting is the wrong order |
