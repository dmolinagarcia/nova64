# Power control, shutdown and telemetry
> two pins and a register block · three levels of off · the battery path

The hardware half of power management is [sheet C](sec_ai_c); this is the half software can see. Three parties have to agree on it and none of them can currently reach the others: the 65816 talks only to Helium, the EC talks to Helium only over the debug agent's SPI link, and only the EC can actually cut a rail. This sheet is that bridge — the `$FF` registers the kernel drives, the SPI registers the EC reads, and the battery telemetry that travels the same path in the opposite direction.

- S.1 — The requirement, stated plainly because everything else is derived from it: **shutdown must be initiable both by software on the 65816 and by a physical button, and must be graceful in both cases** — the kernel flushes the card before any rail drops. Neither the CPU nor the kernel can cut power; only the EC can. So a path from CPU to EC has to exist, and it did not.
  NOTE: Graceful has a bound, and it is [S.7](sec_ai_s#s7)'s: the EC waits ten seconds for an acknowledgement and then stops waiting. So the requirement is ordered shutdown *by default*, not unconditionally — a kernel that never answers loses the argument.
- S.2 — **The pins carry attention, the SPI link carries detail.** Two dedicated wires, both **level-held until the transaction completes, never pulses** — a level survives a missed edge and a pulse does not. `EC_PWR_REQ` (RP2354B → Helium) says the EC wants the system down; `SYS_PWR_REQ` (Helium → RP2354B) says the system has a request outstanding. *Which* request — poweroff, reboot, acknowledge, refusal — is read over SPI. Encoding several meanings in pulse widths on one wire is a bug class nobody should sign up for on a board where both endpoints are also being brought up.
  NOTE: `EC_PWR_REQ` is asynchronous to Helium's core clock and takes a **two-flop synchroniser**; the acknowledgement path takes the equivalent inside RP2354B firmware.
  NOTE: None of this is the floor. A button held long enough reaches the charger's `QON` and resets the BATFET with no firmware anywhere in the path ([C.16](sec_ai_c#c16)), which is what makes a hung EC recoverable and why [S.6](sec_ai_s#s6)'s levels are ordered as they are.
  NOTE: A level also survives a restart at either end: Helium can be *asked* what is outstanding, rather than having had to witness the transition.
- S.3 — Both pins cross the AON↔switched boundary and are therefore subject to [C.13](sec_ai_c#c13) in full: the EC tri-states `EC_PWR_REQ` before dropping 3V3_MAIN, and any pull on either net ties to 3V3_MAIN. **Pin cost is 2 of Helium's spare I/O**, to be allocated together with `DBG_CSN` — which lands on a budget that [Q8](sec_ai_q#q8) already records as not closing (→ [Q27](sec_ai_q#q27)). [[!blocking]]
  NOTE: Those two pins are the only ones this sheet spends. Everything else it needs — the command, the source, the acknowledgement, the telemetry — rides the SPI link that already exists for [sheet R](sec_ai_r).

## `POWER_CTRL` — write-only, one byte, address selects nothing: the value *is* the command.

| Value | Command | Effect |
|---|---|---|
| `$01` | `POWEROFF` | Kernel-initiated shutdown. Helium asserts `SYS_PWR_REQ`, reason = poweroff |
| `$02` | `REBOOT` | Kernel-initiated restart. Same, reason = reboot |
| `$03` | `ACK_POWEROFF` | Acknowledges a pending EC request — the kernel has flushed and is ready |
| `$04` | `ACK_REBOOT` | As above, for a reboot |
| `$05` | `NAK` | The kernel refuses, or cannot service the request now |
| `$10` | `CLEAR_PENDING` | Clears the pending bit and any latched request; for the kernel to reset state after a `NAK` |
| `$20` | `TELEM_LATCH` | Snapshot the telemetry bank for reading. **Not privileged** (→ [S.16](sec_ai_s#s16)) |
| `$21` | `CLEAR_TELEM_EVENT` | Clear `TELEM_EVENT` in `POWER_STATUS` |

- S.4 — This is the one register in the block that departs from [sheet M](sec_ai_m)'s "address as opcode" model, and deliberately: the power commands are a small closed enumeration with no arguments, so a value decoder is cheaper here than eight more addresses. Every command is **synchronous and completes within the write cycle** — none sets `BUSY`. Writes are **privileged**, faulting through ABORTB exactly as any other bank `$FF` write, with `TELEM_LATCH` as the single documented exception: reading the battery level is a legitimate unprivileged operation and the shadow bank is read-only, so the latch grants nothing. **The gateware must implement that exception on purpose rather than arrive at it by accident.**
  TEST: a user-mode power command aborts; a user-mode `TELEM_LATCH` completes.
  NOTE: The enumeration is closed on purpose: a value the gateware does not recognise is a no-op, never an undefined state.

## `POWER_STATUS` — read-only.

| Bit | Name | Meaning |
|---|---|---|
| 0 | `REQ_PENDING` | An EC-initiated request is outstanding, awaiting acknowledgement |
| 1 | `REQ_REBOOT` | The pending request is a reboot; clear means poweroff |
| 2 | `SRC_BUTTON` | It came from the physical button |
| 3 | `SRC_BATTERY` | It came from a critical battery condition |
| 4 | `SRC_THERMAL` | Reserved for a future thermal trip |
| 5 | `ACK_SENT` | An acknowledgement has been issued; the EC has not yet acted |
| 6 | `TELEM_EVENT` | A discrete power or battery event since last cleared (→ [S.20](sec_ai_s#s20)) |
| 7 | — | Reserved, reads zero |

- S.5 — Bits 2–4 are mutually exclusive and meaningful only while `REQ_PENDING` is set. They exist so the kernel can behave differently **by cause**: a button press deserves a dialogue and a chance to cancel, a critical-battery request deserves an immediate flush and no prompt. `REQ_PENDING` raises a **level-based** interrupt through Helium's existing aggregation, held until acknowledged, so a kernel that masks interrupts briefly cannot lose the request.
  NOTE: Mutually exclusive means the kernel switches on the cause and never ranks two of them; a source that is none of the three is a gateware bug, not a fourth case.
- S.6 — **Three levels of off**, and each exists because the level above it can fail.
  NOTE: The levels are ordered by what each can still assume: level 1 needs a working kernel, level 2 only a working EC, level 3 nothing but the charger.

| Level | Trigger | Path | Covers |
|---|---|---|---|
| 1 · Ordered | `poweroff` from the CPU, or a short press (< 1 s) | Kernel flushes and confirms → EC drops rails in reverse order → ship mode | Normal operation |
| 2 · Forced | Long press, ~4 s | EC drops the rails without asking | A hung kernel |
| 3 · Hardware | Very long press on `QON` | BQ25896 BATFET reset, no firmware involved | A hung EC |

  NOTE: The three durations must be checked against the BQ25896's own `QON` timing so the EC's 4 s does not collide with the charger's hardware reset threshold. **If they overlap, the EC's threshold moves** — the charger's is fixed in silicon (→ [Q36](sec_ai_q#q36)).
- S.7 — **The watchdog is mandatory, not defensive polish.** When the EC asserts `EC_PWR_REQ` it starts a **10 s timer**; with no acknowledgement it escalates to level 2 and drops the rails anyway. Without it, a kernel hung during flush leaves a machine that can only be turned off by the 4-second press — a poor experience, and worse, one that hides the fault. The same timer covers reboot.
  NOTE: Forced does not mean careless: the R1 tri-state pass still runs on the way down.
  NOTE: The timer is the EC's, not Helium's — the party that can act on expiry owns it.

## The four sequences — everything the two pins and the register block do.

| Case | Flow |
|---|---|
| **Button, ordered** | EC detects short press → asserts `EC_PWR_REQ`, starts watchdog → Helium synchronises, latches, sets `REQ_PENDING` + `SRC_BUTTON`, raises IRQ → kernel reads `POWER_STATUS`, notifies processes, flushes and unmounts → writes `ACK_POWEROFF` → Helium sets `ACK_SENT`, asserts `SYS_PWR_REQ` with `IS_ACK` → EC confirms over SPI, cancels the watchdog, runs the power-down sequence of [sheet C](sec_ai_c) |
| **Software, ordered** | Kernel flushes and unmounts → writes `POWEROFF` → Helium asserts `SYS_PWR_REQ`, `IS_ACK` clear → EC reads a fresh system request → power-down. **No watchdog**: the work was done before the signal |
| **Kernel declines** | EC asserts and starts the watchdog → kernel writes `NAK` → Helium asserts `SYS_PWR_REQ` with `IS_NAK` → EC de-asserts, cancels the watchdog, takes no further action. A second short press restarts it |
| **Watchdog expiry** | EC asserts → `REQ_PENDING`, IRQ → kernel never writes → 10 s elapse → EC escalates to forced, R1 pass, rails down |

- S.8 — **Reboot is poweroff plus one bit.** From Helium's and the kernel's point of view the only difference is `REQ_REBOOT`; after the rails are down the EC re-runs the power-up sequence instead of entering ship or dormant mode. `NAK` is provided deliberately — a kernel midway through something uninterruptible needs a way to decline, and the watchdog bounds how long a refusal can stall things. Whether a *repeated* refusal should escalate is EC firmware policy and is left open (→ [Q37](sec_ai_q#q37)).
- S.9 — On the EC's side a single read-only SPI register, **`EC_PWR_STATE`**: `SYS_REQ` mirroring the pin · `REQ_REBOOT` · `IS_ACK` (this is an acknowledgement of the EC's own request, not a fresh one) · `IS_NAK` · `CPU_ACK_SEEN`. **It must be readable regardless of the `DEBUG_ENABLE` strap.** The debug agent's privileged registers are gated by that strap by design (→ [R.18](sec_ai_r#r18)), but power management is a production function, not a debug one, and putting it behind the strap would break shutdown on every board built with debug disabled. `EC_PWR_STATE` and the telemetry staging registers therefore live in a small **always-accessible region** of the SPI map alongside `DBG_ID` — read-only, carrying no ability to initiate bus cycles, so nothing the strap exists to withhold is exposed (→ [Q38](sec_ai_q#q38)).
- S.10 — Three gateware requirements, each of them a failure mode rather than a preference. **The request latch is reset-safe**: a Helium reset with a request pending must clear `REQ_PENDING`, not leave a phantom that re-interrupts the kernel the instant it boots. **`SYS_PWR_REQ` survives a CPU reset**: if the kernel acknowledges and the CPU then resets before the EC services the pin, the shutdown must still complete. And **none of this may depend on the cache controller, the arbiter or SDRAM** — it is pure register logic, so that shutdown still works when the memory subsystem is wedged, which is exactly when a user reaches for the button.
  NOTE: That last one is the constraint that outranks the rest of the sheet. **The power path has to be the most robust thing in Helium, because it is the escape hatch for everything else** — and it is why the telemetry below is register logic too, rather than the much cheaper trick of letting the EC drop a structure into SRAM.

## The kernel side — `/dev/power`, and no new syscalls.

- S.11 — The project rule is that drivers expose no syscalls of their own and userspace reaches hardware through `/dev/` (→ [J.4](sec_ai_j#j4)). Power management honours it and **adds nothing to the dispatch table.** A dedicated `sys_poweroff()` was considered and rejected: it would be a single-purpose entry in a table otherwise built from general primitives, and it would need its own privilege check duplicating one the filesystem already performs. Routing through `/dev/power` gets that check for free — the node is writable only by privileged processes, for the same reason any other privileged device node is.

| ioctl | Argument | Effect |
|---|---|---|
| `PWR_POWEROFF` | — | Begin ordered shutdown |
| `PWR_REBOOT` | — | Begin ordered restart |
| `PWR_GET_STATE` | pointer | Fill the state structure — the only unprivileged command |
| `PWR_CANCEL` | — | Cancel a pending request, **only before the point of no return** |
| `PWR_ACK` | — | The session manager signalling that userspace cleanup is done |

- S.12 — The driver implements the standard five functions: `init` clears any stale request and registers the handler, `read` returns the state structure, `write` is unsupported (control goes through `ioctl` so arguments are typed), `ioctl` takes the table above, `irq_handler` services the request. **The interrupt handler must not flush the filesystem** — flushing is unbounded work and cannot run in interrupt context. It captures request type and source, posts a message to the session manager, and returns; the level-based interrupt means nothing is lost between those steps.
- S.13 — The shutdown procedure itself, in process context: **notify** every registered process and start the grace period → **wait**, bounded, `PWR_CANCEL` still honoured → **point of no return**, cancellation now fails → **terminate** stragglers in reverse creation order → **stop the window server**, releasing the framebuffer aperture → **flush and unmount** → **quiesce the MMU** → **halt** with interrupts off, having written `POWER_CTRL` first.
  NOTE: The MMU quiesce — range-flush the cache, then poll `MMU_STATUS.BUSY` until clear (→ [M.6](sec_ai_m#m6)) — is the step most likely to be forgotten and the most damaging to omit. **The cache is write-back: a shutdown that flushes the filesystem but not the cache can still lose the very writes the filesystem just made**, which is precisely the data loss the ordered path exists to prevent.
- S.14 — `PWR_CANCEL` exists so a desktop session can put a confirmation dialogue in front of a button press. It is honoured up to the point of no return and fails after it, because from there onward continuing to run is worse than finishing.

| Phase | Budget |
|---|---|
| Interrupt → session manager notified | < 10 ms |
| Userspace grace period | 3 s |
| Process termination | 500 ms |
| Filesystem flush and unmount | 2 s |
| Cache flush and MMU quiesce | 100 ms |
| **Worst case** | **~5.6 s**, against a 10 s watchdog |

- S.15 — That leaves ~4.4 s of margin, and the grace period is the tunable. **A period longer than the watchdog turns every shutdown into a forced one, and the failure is silent** — a forced shutdown looks identical to a successful one from outside, until someone loses data. The two numbers are a matched pair and must live in one place and change together (→ [Q39](sec_ai_q#q39)). On a **critical battery** the EC raises the request with `SRC_BATTERY`; the kernel treats it as an ordinary ordered shutdown with two differences — no dialogue, no cancellation, and a grace period shortened to 1 s. Whether the kernel may `NAK` it at all is open policy. The argument against is short: the battery does not negotiate.

## Battery telemetry — the same path, travelling the other way.

- S.16 — The instruments are on the AON I2C bus and belong to the EC; the 65816 cannot reach them, and any path that fixes that owes three properties. **The CPU must never block** on an I2C transaction it did not initiate and cannot bound. **Reads must not tear** — a multi-byte value updated asynchronously and read a byte at a time otherwise yields impossible combinations: a low byte from one sample, a high byte from the next. **Stale data must be detectable**, or a hung EC reports a battery frozen at whatever it last said. The answer is **two atomic copies, one in each direction**: the EC dribbles bytes into a **staging** bank over SPI in any order and at any speed, then sets `TELEM_COMMIT` — Helium copies staging → **live** in one cycle and bumps `TELEM_SEQ`. The CPU writes `TELEM_LATCH`, Helium copies live → **shadow** in one cycle, and the kernel reads it as slowly as it likes without tearing. No retry loop, no generation counter: a latch, then a read.
  NOTE: Two alternatives were rejected, both for reasons that recur across this document. A **seqlock** — one bank plus a counter the CPU reads before and after, retrying on mismatch — saves the shadow bank's ~128 flip-flops but moves complexity into kernel code on the constrained processor, and a retry loop against a fast committer has no hard bound; the flip-flops are cheaper than the reasoning. And letting the **EC write telemetry into SRAM** through the debug agent's bus path would drag battery reporting through the cache controller and the arbiter, making it depend on the health of the memory subsystem — which is exactly what [S.10](sec_ai_s#s10) forbids. Total cost of the chosen path is ~16 bytes of register file doubled for the snapshot, about 256 flip-flops.
  NOTE: **The telemetry registers are in the switched domain and lose their contents whenever 3V3_MAIN drops.** So after every power-up and every resume, the EC must repopulate staging and commit **before releasing the 65816 from reset** — step 8b of [sheet C](sec_ai_c)'s power-up table — or the kernel's first read is zeros, indistinguishable from a flat battery. `TELEM_SEQ` resets to zero on Helium reset, so sequence zero with a fresh age means "first commit after boot" rather than a wrap.
  NOTE: The CPU's only write anywhere in this path is `TELEM_LATCH`, and it carries no data — which is what makes the block safe to expose to user space at all.
- S.17 — **The EC publishes human units; the CPU never sees a raw sensor code.** The 65816 has no multiplier and no divider, so making the kernel scale a VCELL code at 78.125 µV/LSB or a CRATE code at 0.208 %/hour/LSB would burn cycles on arithmetic the RP2354B does in nanoseconds. All scaling, sign extension and clamping happen before the push: VCELL → millivolts, SOC → integer percent plus a separate 1/256 fraction byte, CRATE → signed tenths of a percent per hour, the charger's ADC → millivolts and milliamps, `CHRG_STAT` → an enumerated byte.
  NOTE: Human units also fix the ABI. A future gauge that reports differently is the EC's problem.
- S.18 — **`TELEM_AGE` counts eighths of a second since the last commit, saturating at 255** (~32 s). Helium supplies the raw counter and takes no view; the kernel derives `PWR_F_TELEM_STALE` from it, treating anything above the threshold as *unknown* rather than as zero. With a 1 Hz cadence, a threshold of 8 is a sensible default. One byte and one small counter, and it is the whole difference between "the battery is at 47 %" and "the OS does not currently know."
  NOTE: Saturating rather than wrapping is the point: 255 means *at least* 32 s and can never be read as fresh, which is exactly what a wrap would allow.
  NOTE: Eighths are enough resolution to tell a half-second-old reading from a four-second-old one, and coarse enough that no timestamp has to travel with the data.

## Telemetry block — sixteen consecutive read-only addresses, all served from the shadow bank.

| Off | Name | Description | | Off | Name | Description |
|---|---|---|---|---|---|---|
| `+$00` | `TELEM_SEQ` | Increments per commit; wraps | | `+$08` | `PWR_FLAGS` | See below |
| `+$01` | `TELEM_AGE` | Eighths of a second, saturating | | `+$09` | `CHG_STATE` | 0 none · 1 pre · 2 fast · 3 terminated |
| `+$02` | `BATT_SOC` | State of charge, 0–100 % | | `+$0A`·`+$0B` | `VBUS_MV` | Adapter voltage, mV |
| `+$03` | `BATT_SOC_FRAC` | Fraction, 1/256 % | | `+$0C`·`+$0D` | `SYS_MV` | SYS node voltage, mV |
| `+$04`·`+$05` | `BATT_MV` | Cell voltage, mV | | `+$0E`·`+$0F` | `ICHG_MA` | Charge current, mA |
| `+$06`·`+$07` | `BATT_RATE` | Signed tenths of %/hour; negative discharges | | | | |

| `PWR_FLAGS` bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| | `ON_ADAPTER` | `CHARGING` | `BATT_LOW` | `BATT_CRIT` | `PD_ACTIVE` | `GAUGE_FAULT` | `CHARGER_FAULT` | `BATT_ABSENT` |

- S.19 — `GAUGE_FAULT` is the flag that earns its bit: **an I2C failure must be visible as a fault, not as a plausible-looking zero.** The same reasoning as `TELEM_AGE`, applied to the instrument rather than to the link. Where this block sits inside bank `$FF` is still unassigned — it wants its own 256-byte wall on [Q22](sec_ai_q#q22)'s terms, and the base address blocks both the gateware and the kernel headers (→ [Q40](sec_ai_q#q40)).
  NOTE: A fault flag the driver ignores is a fault the user meets as a wrong number.
- S.20 — **The CPU is not interrupted on every commit.** At 1 Hz that is a context switch to observe a number that moved by a fraction of a percent. The EC sets `TELEM_EVENT` only on discrete transitions — adapter in or out, charging started, finished or faulted, a low or critical threshold crossed, a gauge or charger fault appearing or clearing, the battery removed or inserted — and everything else is picked up by polling. Cadence is 1 s awake either side of the adapter, an immediate push after any event, and 60 s gauge-only with the EC dormant; the MAX17048 refreshes VCELL every 250 ms, so nothing is gained by going faster and the I2C traffic costs EC wake time.
  NOTE: The 1 Hz commit is the EC's cadence, not a promise to the kernel. A driver needing a fresher figure latches and reads rather than waiting for the next commit, and `TELEM_SEQ` is what tells it whether anything moved in between.
- S.21 — Time to empty is `minutes_left = (battery_pct * 600) / |battery_rate|` — 16-bit integer arithmetic, one division, which is as much as the CPU should be asked to do. Two cautions, both of which produce nonsense if ignored. **CRATE is noisy**: a load transient swings the instantaneous rate wildly, so the driver keeps a ring of the last 8 samples and uses their mean. **A near-zero rate is not a long runtime**: below about 0.1 %/hour report `0xFFFF`, unknown, rather than a figure in the thousands of minutes. The same formula with a positive rate gives time-to-full, and the driver must not report a time-to-empty while the flags say charging.
  NOTE: That division is the only one in the path, and it is where a zero has to be caught: `BATT_RATE` is signed and sits at zero on a machine resting at full charge, so the driver reports "not discharging" rather than dividing by it.
  NOTE: One division, 16-bit, and no floating point anywhere in the path.
- S.22 — On `TELEM_EVENT` the driver refreshes, clears the event and posts to the session manager so the indicator updates at once; ordinary drift is left to the GUI polling `/dev/power` every 10–30 s, which is ample. **The read path is ordered deliberately**: latch, read `TELEM_AGE` and stop there if it is stale, and only then spend cycles on the rest. There is no point converting data already known to be worthless. [[open]]
  NOTE: What is still open here beyond the pin allocation: the block's base address (→ [Q40](sec_ai_q#q40)), the low and critical thresholds in percent and millivolts, whether `TELEM_AGE` should be visible to the EC too so it can detect that its own pushes are not landing, the BQ25896 ADC's conversion cadence, and how SOC is recovered after a long shelf period — the MAX17048's ModelGauge state is lost with power and its recovery leans on an open-circuit reading taken while the cell is at rest, so a machine woken from ship mode straight into a heavy load may report poorly for its first minutes.

![Fig. 9 — The power control path. Two level pins carry attention between the EC and Helium, the debug agent's SPI link carries the detail, and telemetry travels the same link into a staging bank that is committed and latched into two atomic snapshots before the CPU ever reads it.](figures/fig-9-power-control.svg)
LEGEND: Trace legend: <span class="m">mint = attention pins, SPI and CPU register access</span> · <span class="g">gold = telemetry and rail control</span> · dashed = I2C and the interrupt to the CPU.
