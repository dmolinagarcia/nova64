# Debug agent and console
> a requester inside Helium · SPI-only control · the console that drives it

Instrumentation, not a product feature. A hardware block inside Helium performs memory and bus accesses on command, and a text monitor on the RP2040 drives it — so the board is observable from the first stage at which anything can be powered, long before a BIOS or a kernel exists. It is expected to stay in the design permanently, gated off rather than removed.

- R.1 — Five capabilities define it, and everything else on this sheet is their cost: read and write **any physical location** (SRAM and SDRAM), read and write **through the MMU** once translation exists, generate **real bus cycles** other devices can see, **halt · single-step · trace** the W65C816S, and do all of it from a terminal over USB-CDC or a physical UART.
- R.2 — **The RP2040 is not a bus master.** It sends commands; the debug agent performs the access. Three reasons, none of them stylistic. **Pins**: mastering directly needs ~24 address plus 8 data lines plus control, well beyond what the EC has left after its committed functions — and its budget already had to be rescued once (→ [D.6](sec_d#d6)). **Reuse**: Helium already owns the SRAM controller, the SDRAM controller with refresh, the cache and the arbiter; a second path to memory would duplicate all of it and become a second source of truth about memory state. **Coherence**: an external master bypassing the cache reads stale data whenever a dirty sub-block is resident in SRAM.
  NOTE: The consequence is the whole design in one line — the debug agent is **a peer of the CPU port in Helium's arbiter**, not a special case bolted to the side of it.
- R.3 — **Helium drives `BE`**, not an RP2040 GPIO. Taking the bus requires two things in a defined order: the CPU releases its drivers, then Helium begins driving. Across the SPI link that ordering is a race across a clock-domain crossing with millisecond latency; inside Helium it is a synchronous state transition. Schematic consequence: `BE` routes Helium → CPU socket, and Helium's CPU-side address and data pins must be **bidirectional**, which lands on the I/O bank plan (→ [Q27](sec_q#q27)).
- R.4 — **The register file is SPI-only and is not in bank `$FF`.** The agent is unconditionally privileged — it writes `$FF`, bypasses permission checks and halts the CPU — so exposing it to software would make the entire protection model depend on kernel correctness. Keeping the control path physically separate means no software failure can reach it, and the console stays usable precisely when software has failed, which is when it is needed (→ [M.1](sec_m#m1), [Q22](sec_q#q22)).
  NOTE: **There is a second debug path on this board now, and the two are worth seeing as a pair.** Neon reuses its four configuration SPI pins as a **service port** once `CDONE` rises, reaching its register file, its text buffer and font in block RAM, and its SDRAM ([T.46](sec_t#t46)). This agent gives the EC memory and bus visibility from inside *Helium*; the service port gives it a **screen** and Neon's state from inside *Neon*. They share no logic and no chip select, so a fault in one does not take the other — and between them a machine with a dead CPU still has a way to say what is wrong, with nothing attached to it.
- R.5 — Two orthogonal axes characterise every transaction: **which address space** and **how far the cycle reaches**. They compose freely, and the console shows both in the prompt because getting them wrong is the commonest source of confusing results.

## The two axes — address space and transaction scope, chosen independently.

| Axis | Mode | What it means | Available from |
|---|---|---|---|
| Address space | Physical | 27-bit raw address, straight to cache and memory controllers. What is needed while the MMU is itself under test | E2 |
| Address space | Virtual | 24-bit address + ASID, through translation and, on a miss, the walker. Shows what the kernel believes it is looking at | E4 |
| Scope | Internal | SRAM, SDRAM and Helium's own registers. Nothing appears on the CPU bus | E2 |
| Scope | External cycle | Everything the CPU can reach — the `$FE` VRAM aperture and the rest of `$FF` included. Indistinguishable, from outside, from a cycle issued by the 65816 | E5 |

- R.6 — Link: **SPI mode 0, RP2040 controller, Helium peripheral, 16 MHz initially** — the limit is the synchroniser depth on Helium's side, not the wire. It shares `SCK`/`MOSI`/`MISO` with the configuration SPI but takes a **dedicated `DBG_CSN`**: sharing data lines is acceptable, sharing the chip select is not, because the configuration path has to keep working when the agent is in an unknown state.
- R.7 — Frame: one header byte — bit 7 is `0` write / `1` read, bits 6:0 the register address — then data, **least-significant byte first**. `DBG_CSN` deasserts between frames; a frame commits on its rising edge, and a truncated one is discarded and sets `ERR_FRAME`. Two registers, `DBG_FIFO` and `TRC_FIFO`, stream for as long as `DBG_CSN` stays low, so dumping a kilobyte of memory does not cost a frame per byte.
- R.8 — `DBG_ID` reads `$6516`, and the console reads it first: a correct value proves the SPI link, Helium's configuration and the clock all at once, before anything else is attempted. `$0000` or `$FFFF` means the bitstream did not load. A second read in the same frame returns the gateware build revision, which is bumped on every synthesis.
- R.9 — Two disciplines the console must observe. **Never assume a command completes inside the frame that issued it** — an SDRAM access with a page miss and an intervening refresh takes far longer than an SRAM hit, so poll `BUSY`. And **the agent latches the first error and refuses further commands until `DBG_ERR` is cleared**, which is what stops a scripted burst from quietly producing a page of garbage after its first failure.
- R.10 — Access width, corrected against the memory the board actually has. The system SRAM is **1 MB ×8 with D0–D7 shared with the CPU nets** (→ [D15](sec_q#d15)), so it has no upper/lower byte enables and a 16-bit access is simply two consecutive byte cycles. Byte granularity does bite on the SDRAM side, which is ×16 and masks with `DQM`. `DBG_WIDTH` therefore selects **8-bit low · 8-bit high · 16-bit**, and the E2 memory test must still exercise byte writes explicitly: a byte write that corrupts its neighbour is a very specific and very confusing failure, and it is cheaper to find it with `mt` than through a kernel bug.
  NOTE: The source document specified an IS61WV102416 ×16 SRAM with `UB#`/`LB#`. That part predates [D15](sec_q#d15) and is not on this board; the register keeps its encoding, its justification changes.

## Register map — SPI address space, 7 bits, entirely separate from the `$FF` block of [sheet M](sec_m).

| Addr | Name | Access | Width | Meaning |
|---|---|---|---|---|
| `0x00` | `DBG_ID` | RO | 16 | Magic `$6516`; second read returns the build revision |
| `0x01` | `DBG_CTRL` | RW | 8 | `ENABLE` · `VIRT` · `EXTCYC` · `AUTOINC` — advances `DBG_ADDR` by the access width after each access, and **not** after an error, so a failed burst leaves the address on the element that failed · `NOCACHE` · `STALL` — `0` steals cycles where it can, `1` stalls PHI2 for every access |
| `0x02` | `DBG_STATUS` | RO | 8 | `BUSY` · `DONE` · `ERROR` · `FIFO_EMPTY` · `FIFO_FULL` · `CPU_HALTED` · `TRC_TRIGD` · `DBG_READY` |
| `0x03` | `DBG_ERR` | RO/W1C | 8 | `TIMEOUT` · `UNMAPPED` · `PERM` · `ARB` · `CMD` · `STATE` · `FRAME` · `ALIGN` |
| `0x04` | `DBG_ADDR` | RW | 32 | 27-bit physical, or 24-bit virtual; unused high bits must be zero |
| `0x08` | `DBG_DATA` | RW | 16 | Data port for single accesses |
| `0x0A` | `DBG_WIDTH` | RW | 8 | `1` low byte · `2` high byte · `3` 16-bit (→ [R.10](sec_r#r10)) |
| `0x0B` | `DBG_ASID` | RW | 16 | ASID for virtual accesses. Narrows when [Q24](sec_q#q24) lands |
| `0x0C` | `DBG_CMD` | WO | 8 | Command trigger; rejected with `ERR_STATE` while `BUSY` |
| `0x0D` | `DBG_COUNT` | RW | 16 | Burst length in elements |
| `0x10` | `CPU_CTRL` | RW | 8 | `RESET` · `HALT` · `STEP` · `STEP_INSN` · `IRQ_MASK` · `NMI_MASK` |
| `0x11` | `CPU_STATUS` | RO | 8 | CPU state |
| `0x12` | `CPU_CYCLES` | RO | 32 | Free-running bus-cycle counter |
| `0x18` | `TRC_CTRL` | RW | 8 | `EN` · `ARM` · `ONESHOT` · `POS` · `HALT_ON_TRIG` · `FILTER_OPFETCH` |
| `0x19` | `TRC_STATUS` | RO | 8 | Trace state and fill level |
| `0x1A` | `TRC_TRIG_ADDR` | RW | 32 | Trigger address, with `RWB`/`VPA`/`VDA` qualifiers in the upper bits |
| `0x1E` | `TRC_TRIG_MASK` | RW | 32 | Trigger don't-care mask |
| `0x1F` | `TRC_FIFO` | RO | burst | Trace record readout |
| `0x20` | `DBG_FIFO` | RW | burst | 512-byte bulk data port, sized to stay ahead of the link across an SDRAM page miss. **The agent never stretches the SPI clock**, so the console is the side that honours `FIFO_FULL` on writes and `FIFO_EMPTY` on reads |
| `0x30` | `TLB_INDEX` | RW | 16 | TLB entry selector |
| `0x31` | `TLB_ENTRY` | RO | 64 | TLB entry readout |
| `0x38` | `MMU_MIRROR` | RO | 8 | Read-only view of `MMU_STATUS` (→ [sheet M](sec_m)) |
| `0x40` | `EC_PWR_STATE` | RO | 8 | **Always accessible**, `DEBUG_ENABLE` notwithstanding: `SYS_REQ` · `REQ_REBOOT` · `IS_ACK` · `IS_NAK` · `CPU_ACK_SEEN` (→ [S.9](sec_s#s9)) |
| `0x41` | `TELEM_CTRL` | WO | 8 | **Always accessible**: `TELEM_COMMIT` · `TELEM_EVENT_SET` |
| `0x42+` | `TELEM_STAGE` | WO | 16 B | **Always accessible**: the staging bank, written byte by byte in any order (→ [S.16](sec_s#s16)) |

## Commands — written to `DBG_CMD`, one at a time.

| Code | Name | Action |
|---|---|---|
| `0x00` | `CMD_NOP` | No operation; clears `DONE` |
| `0x01` · `0x02` | `CMD_READ` · `CMD_WRITE` | One element through `DBG_DATA` |
| `0x03` · `0x04` | `CMD_READ_BURST` · `CMD_WRITE_BURST` | `DBG_COUNT` elements through the FIFO |
| `0x05` | `CMD_FILL` | Write `DBG_DATA` to `DBG_COUNT` consecutive elements |
| `0x10` · `0x11` | `CMD_CACHE_FLUSH` · `CMD_CACHE_INVAL` | Flush the whole cache to SDRAM · invalidate it without writeback. **`INVAL` discards dirty data** and exists only to recover a wedged cache during gateware bring-up |
| `0x12` | `CMD_CACHE_FLUSH_LINE` | Flush the line holding `DBG_ADDR` — the same operation the kernel needs before paging a frame out (→ [F.7](sec_f#f7)) |
| `0x20` · `0x21` | `CMD_TLB_PROBE` · `CMD_TLB_FLUSH` | Translate `DBG_ADDR` into `TLB_ENTRY` · invalidate the whole TLB |
| `0x22` | `CMD_PTWALK` | Full page-table walk, bypassing the TLB |
| `0x30` | `CMD_ABORT` | Cancel the in-flight command |

- R.11 — `CMD_PTWALK` earns its place by differing from `CMD_TLB_PROBE` in exactly one way: it always reads the table from memory. **Comparing the two is the direct test for a stale TLB entry** — which is the precise failure the `CTX_SET_PTBASE` before `CTX_SET_ASID` ordering invariant exists to prevent, and which otherwise has no symptom until something is corrupted (→ [M.8](sec_m#m8)).
- R.12 — The access path: `IDLE` → decode → translate if `VIRT` → arbitrate → then either an internal access through the cache, or `BUS_TAKE` · `BUS_CYCLE` · `BUS_RELEASE` for an external one → complete. Only the external branch touches the CPU, and its sequence is fixed: request the bus, stall PHI2 at a clean cycle boundary, assert `BE` low, wait the CPU's tristate turnaround, drive the cycle, release the drivers, wait turnaround, deassert `BE`, release PHI2 and the arbiter.
  NOTE: The restart must meet the minimum PHI2 pulse width. The existing glitch-free gating logic is reused unchanged — the agent is one more client of it, not a second implementation (→ [D16](sec_q#d16)).

![Fig. 8 — The debug agent as a requester inside Helium. The RP2040 commands over SPI and never drives the bus; internal accesses go through the cache, external cycles are taken with PHI2 stalled and BE low.](figures/fig-8-debug-agent.svg)
LEGEND: Trace legend: <span class="m">mint = command and data path</span> · <span class="g">gold = CPU clock and bus control</span> · dashed = optional.

- R.13 — Coherence, four rules. **One: the agent goes through the cache controller**, exactly as the CPU port does, so a physical read of an SDRAM address whose sub-block is dirty in SRAM returns the cached value. This is not negotiable — a debug tool that reports memory contents differing from what the CPU sees is worse than no tool at all, because it produces confident wrong answers during exactly the sessions where the engineer can least detect them. **Two: `NOCACHE` is for gateware bring-up only** — its one legitimate use is validating the cache itself (write through the cache, flush, read with `NOCACHE`, compare), and the console prints a warning banner whenever it is set. **Three: external cycles are coherent by construction**, being decoded by the same logic as a CPU cycle. **Four: writing a PTE does not invalidate the TLB** — the console's `pt write` issues `CMD_TLB_FLUSH` afterwards; anyone driving the registers directly must do the same (→ [M.7](sec_m#m7)).
- R.14 — CPU control is an **indefinite PHI2 stall**, legal because the core is fully static (→ [E.4](sec_e#e4), [D16](sec_q#d16)). `STEP` advances one bus cycle, `STEP_INSN` runs to the next opcode fetch — which `VPA & VDA` identifies, the same pins the MMU already needs for the `X` permission (→ [E.3](sec_e#e3)). `IRQ_MASK` and `NMI_MASK` matter more than they look: a 100 Hz timer that keeps running while the CPU is stopped presents a backlog of pending interrupts on resume, which looks exactly like a kernel bug and is not.
- R.15 — **The agent cannot read the CPU's registers.** The W65C816S has no scan chain and no debug port; A, X, Y, S, DP, DBR, PBR and P are not observable from outside, and no amount of gateware changes that. Two workarounds, in order of preference: **inferred from trace** — a halt at an instruction boundary plus the preceding cycles lets the console reconstruct PC and much of the state, passively and always; and a **debug stub** in bank `$00` that pushes every register to a known location, entered either by forcing an instruction onto the data bus during a fetch or from a `BRK` breakpoint trap (→ [J.3](sec_j#j3)). The console exposes the first as `reg` and the second as `reg -f`, and **the distinction stays visible to the user**: silently perturbing state during inspection is unacceptable in a debugger.
- R.16 — Trace: a ring buffer in EBR capturing, per bus cycle, address (24), data (8), `RWB`, `VPA`, `VDA`, `VPB`, `MLB`, `E` and a cycle-counter delta — about 48 bits per record. The trigger compares `(address & ~TRC_TRIG_MASK)` against `TRC_TRIG_ADDR`, optionally qualified by `RWB` and `VPA`/`VDA`, and can position the capture at the start, centre or end of the window, halt the CPU when it fires, or filter down to opcode fetches only. **This is a logic analyser for one EBR block and a comparator, and it must be built with the CPU stage rather than retrofitted after the first hard kernel bug.** [[open]]
  NOTE: 1024 records is the starting proposal, but the depth is a synthesis parameter against an EBR budget that the TLB and the cache tags are already spending (→ [Q30](sec_q#q30)). The source document costed it on an HX4K; Helium is an **HX8K** (→ [B.2](sec_b#b2)).
- R.17 — Every transaction is covered by a watchdog — 100 µs at the Helium core clock as a starting value, generous enough for a worst-case SDRAM access with an intervening refresh and short enough that a human reads it as a delay. On expiry the agent abandons the transaction, releases the bus and PHI2, sets `ERR_TIMEOUT` and returns to idle. **The agent must never be able to hang the console**, and that constraint outranks completing any transaction: if it hangs, the engineer loses the only view into a board that is, by hypothesis, already misbehaving.
  NOTE: The reverse interaction has to be handled too — the system watchdog that turns a hung fill into NMI or ABORT must be **suppressed while the CPU is halted by the agent**, or every debug session ends in a spurious abort.
- R.18 — `DEBUG_ENABLE`, a Helium input on a jumper, gates the whole thing: deasserted, it forces `DBG_CTRL.ENABLE` to zero, ignores writes to it, and rejects every command with `ERR_STATE` — while still answering `DBG_ID` and `DBG_STATUS`, so the console can report *why* it is refusing rather than appearing broken. The point is not security against physical access, which is unachievable and not a goal, but making the privileged path an **explicit, visible, deliberate state of the board**.
  NOTE: The gate now has a **documented hole, and it has to be one**: `EC_PWR_STATE` and the telemetry staging registers sit outside it. Power management is a production function, not a debug function, and putting it behind the strap would break shutdown and battery reporting on every board built with debug disabled. Neither register can initiate a bus cycle or reach memory, so the region grants nothing the strap exists to withhold — but it must be carved out of the map deliberately, and confirming it can be without disturbing what is already there is [Q38](sec_q#q38).
- R.19 — The console runs on both transports at once: **USB-CDC** over the RP2040's native USB and a **physical UART**, 115200 8N1 on a 3-pin header. The UART is not redundant. At the first stage there is no way to tell a firmware fault from a USB stack fault, and three pins remove the ambiguity; it also survives the USB peripheral being reconfigured for PIO-USB HID work (→ [D.5](sec_d#d5)). Output mirrors to both, input is taken from either, first come.

## Console grammar — hexadecimal by default, `#` prefixes decimal, `p:` and `v:` qualify an address.

| Group | Commands | Notes |
|---|---|---|
| Memory | `md` · `mw` · `mf` · `mc` · `ms` · `mt` | dump · write · fill · compare · search · test (march-c, addr-in-addr, walking-1) |
| Mode | `mode phys / virt` · `int / ext` · `asid <n>` · `width 8 / 16` · `nocache on / off` | bare `mode` prints the current flags; `nocache` prints a warning |
| CPU | `halt` · `run` · `step [n]` · `stepi [n]` · `reset [hold]` · `reg` · `reg -f` · `dis` | `reg` infers from trace, `reg -f` forces the stub and perturbs state (→ [R.15](sec_r#r15)) |
| Trace | `trace arm [addr] [mask]` · `on / off` · `pos` · `dump [n]` · `filter opfetch` | `dump` disassembles the records |
| MMU | `tlb dump / probe / flush` · `pt walk / dump / write` · `cache flush / inval / stats` | `pt walk` shows each level; `pt write` flushes the TLB after it |
| Storage | `ld <file> <addr>` · `sv <addr> <len> <file>` · `boot <file>` | straight from the microSD the EC already owns (→ [G.2](sec_g#g2)) |
| System | `id` · `status` · `err` · `help [cmd]` | `status` decodes every status bit; `err` decodes and clears |

- R.20 — Five behavioural requirements, each of them a lesson rather than a preference. **Every error is reported with its decoded cause** — never a bare failure. **Long operations show progress**: a march test over 64 MB of SDRAM takes minutes, and a silent console for that long is indistinguishable from a hang. **Ctrl-C aborts** through `CMD_ABORT`. **`md` output is annotated** with the region it fell in — cached SRAM, pinned SRAM, SDRAM, VRAM aperture, `$FF` — derived from the physical map of [L.6](sec_l#l6). **The prompt carries the mode**: `p16>` for physical 16-bit, `v8:0042>` for virtual 8-bit under ASID `$42`.
- R.21 — What the agent settles that was open before it: **[Q25](sec_q#q25) — how the RP2040 reaches Helium's control registers with the CPU held in reset.** It does not master the bus; it issues an internal access, and an internal write to a `$FF:00xx` address meets the same address comparator a CPU store would. So the whole "address as opcode" command model of [sheet M](sec_m) — TLB flushes, context loads, cache flushes and their `BUSY` discipline — becomes testable at E4 with no CPU, no BIOS and no kernel in the picture (→ [M.2](sec_m#m2), [D18](sec_q#d18)).

## Staging — the agent's capability mapped onto this document's stages ([sheet P](sec_p)).

| Stage | Debug agent capability | What it validates |
|---|---|---|
| E1.6 · RP2040 alone | Console up on USB-CDC and UART; microSD; bitstream load | EC firmware, SD stack, both transports |
| E1.7 · the three FPGAs | `DBG_ID` reads `$6516`; and on Neon's side, **a correct text screen plus glyphs written over the service port** — Neon stage N0 ([T.64](sec_t#t64)) | SPI link, Helium clock, bitstream actually loaded — and, on Neon, the PLL, the timing generator, block-RAM initialisation, the panel and the FPC |
| E2 · SRAM + monitor | Physical peek/poke; `mt` march test; byte-write check | SRAM controller, shared-net timing, `md`/`mw` end to end |
| E3 · SDRAM | Physical SDRAM access; `CMD_CACHE_FLUSH`/`INVAL`; `cache stats` | SDRAM controller, refresh under load, cache coherence |
| E4 · MMU + cache + protection | Halt · step · trace; virtual mode; `TLB_PROBE` vs `PTWALK`; internal writes to `$FF` | PHI2 gating, `BE` handoff, arbiter, walker, ASID, [sheet M](sec_m)'s command model |
| E5 · video + audio | External cycles into the `$FE` aperture, **reads included** — with `BE` low there is no PHI2 to stall, so the agent simply waits for `NEON_BUS_BSY` to clear before sampling D0–D7 ([T.18](sec_t#t18)) | Inter-FPGA bus visibility, the VRAM window, Neon's decode — and framebuffer dumps, which is how blitter output gets compared byte-for-byte against a software reference with no kernel in existence |

- R.22 — **E2 is the stage that pays for the whole design.** Being able to march-test SRAM before any CPU exists converts the hardest class of bring-up bug — intermittent memory faults surfacing as random software misbehaviour — into a direct, reproducible measurement, and it does so at the exact moment the board is least able to explain itself.
  NOTE: The free-run stage before it ([E1.8](sec_p#e18)) gets a dividend too, if the trace buffer is ready by then: capturing a NOP free-run is an analyser reading of PHI2, reset and the address bus with no analyser attached.
- R.23 — Honest cost. In pins: `DBG_CSN` on both ends, `DEBUG_ENABLE`, `BE` from Helium, optionally `DBG_IRQN` — which asserts on a trace trigger or a CPU halt, and buys only the difference between an interrupt and a poll — the 3-pin UART header, and bidirectional CPU-side pins on Helium. That lands on a budget which **does not close as written** — [Q8](sec_q#q8) already had the free 17 pins failing to cover SPI-SD, the console UART and this port. [[!blocking]]
  NOTE: In gateware the open items are the SPI-to-core clock-domain crossing (→ [Q28](sec_q#q28)), the `BE` turnaround count at 3.3 V (→ [Q27](sec_q#q27)), where the agent sits in the arbiter's priority order (→ [Q29](sec_q#q29)), and the trace depth against remaining EBR (→ [Q30](sec_q#q30)).
- R.24 — Deferred, not ruled out: **breakpoint comparators** — address match forcing a halt, a natural extension of the trace trigger, held back to keep the CPU stage small · **watchpoints on data value** as well as address · and a **GDB remote serial protocol stub** on the RP2040, so a host debugger drives the agent directly. The last is attractive and cheap to get wrong early; it is worth doing only once the command set above has stopped moving. [[open]]
