# noVa64 OS — Power Management

**Project:** noVa64
**Status:** Design, not yet implemented
**Companion documents:** `sheet-1-1-power.md` (REV C), `helium-power-control.md`, `battery-telemetry.md`
**Extends:** noVa64 OS syscall table and driver interface

---

## 1. Scope

The kernel must be able to shut the machine down cleanly, both when a process asks it to and when the user presses the button. This document specifies the driver, the userspace interface, and the shutdown procedure itself.

---

## 2. Design decision: no new syscalls

The established project principle is that drivers expose no custom syscalls and that userspace reaches hardware only through `/dev/` paths, using the unified fd-based I/O calls. Power management honours that. It adds **no entries to the syscall table.**

The alternative — a dedicated `sys_poweroff()` — was considered and rejected. It would be a single-purpose syscall in a table otherwise built from general primitives, and it would need its own privilege check duplicating what the filesystem permission model already provides. Routing through `/dev/power` gets the permission check for free: the node is owned by the kernel and writable only by privileged processes, so an unprivileged program cannot shut the machine down for the same reason it cannot write to any other privileged device node.

---

## 3. The `power` driver

Implements the standard five-function internal kernel driver interface.

| Function | Role |
|---|---|
| `init` | Clear any stale request in `POWER_STATUS`, register the interrupt handler, create `/dev/power` |
| `read` | Return the current power state structure (§5) |
| `write` | Not supported; returns an error. Control is via `ioctl` so that arguments are typed |
| `ioctl` | Commands of §4 |
| `irq_handler` | Service a pending request from the EC (§6) |

The driver owns exclusive access to the Helium `POWER_CTRL` and `POWER_STATUS` registers. No other kernel subsystem may write them.

---

## 4. `/dev/power` ioctl interface

| Command | Argument | Effect |
|---|---|---|
| `PWR_POWEROFF` | none | Begin ordered shutdown |
| `PWR_REBOOT` | none | Begin ordered restart |
| `PWR_GET_STATE` | pointer to state struct | Fill in the structure of §5 |
| `PWR_CANCEL` | none | Cancel a pending request, if the shutdown procedure has not yet passed the point of no return |
| `PWR_ACK` | none | Used by the session manager to signal that userspace has finished its own cleanup |

All except `PWR_GET_STATE` require privilege.

`PWR_CANCEL` exists so that a desktop session can offer a confirmation dialogue on a button press. It is only honoured before the shutdown procedure begins irreversible work, i.e. before step 4 of §7. After that it returns an error, because the machine is by then in a state from which continuing to run would be worse than completing the shutdown.

---

## 5. State structure

```c
struct pwr_state {
    uint8_t  flags;        /* PWR_F_* below                     */
    uint8_t  request;      /* PWR_REQ_NONE / POWEROFF / REBOOT  */
    uint8_t  source;       /* PWR_SRC_* below                   */
    uint8_t  battery_pct;  /* 0-100, from the EC via Helium     */
    uint16_t battery_mv;   /* cell voltage in millivolts        */
};

#define PWR_F_ON_ADAPTER    0x01  /* running from the adapter       */
#define PWR_F_CHARGING      0x02
#define PWR_F_BATTERY_LOW   0x04
#define PWR_F_BATTERY_CRIT  0x08
#define PWR_F_SHUTDOWN_REQ  0x10  /* a request is pending           */

#define PWR_SRC_SOFTWARE    0
#define PWR_SRC_BUTTON      1
#define PWR_SRC_BATTERY     2
#define PWR_SRC_THERMAL     3
```

**Note on the battery fields.** These are populated from the telemetry path specified in `battery-telemetry.md`, which supersedes the structure shown above — see §11 of that document for the revised definition, which adds charge rate, time-to-empty, staleness and two further flags. The driver must latch before reading, and must treat stale telemetry as unknown rather than as zero.

---

## 6. Interrupt handling

`REQ_PENDING` in `POWER_STATUS` raises a level-based interrupt. The handler must be short:

1. Read `POWER_STATUS`, capture request type and source.
2. Record them in the driver's state.
3. Post a message to the session manager's port.
4. Return.

**The handler must not flush the filesystem.** Flushing is unbounded work and cannot run in interrupt context. It happens in the shutdown procedure, in process context.

Because the interrupt is level-based and remains asserted until acknowledged, there is no risk of losing the request between steps 3 and 4.

---

## 7. Shutdown procedure

Runs in process context, in the kernel, after the session manager either acknowledges or the grace period expires.

1. **Notify.** Send a shutdown message to every process that has registered a port for it. Start a grace period.
2. **Wait**, bounded — see §8. Processes save state and exit. `PWR_CANCEL` is still honoured throughout this step.
3. **Point of no return.** `PWR_CANCEL` now fails. Mark the shutdown irreversible.
4. **Terminate** any process still running, in reverse creation order.
5. **Stop the window server**, releasing the framebuffer aperture.
6. **Flush** all dirty filesystem buffers, then unmount.
7. **Quiesce** the MMU: flush the cache with the standard range-flush, then poll `MMU_STATUS` BUSY until clear. Skipping this risks losing dirty cache lines that were never written back to SDRAM, which is exactly the data loss the ordered shutdown exists to prevent.
8. **Signal the EC:** write `POWER_CTRL <- ACK_POWEROFF` (for an EC-initiated request) or `POWER_CTRL <- POWEROFF` (for a software-initiated one).
9. **Halt.** Disable interrupts and enter a tight loop. The EC drops the rails within milliseconds.

Step 7 is the one most likely to be forgotten and the most damaging to omit. The cache is write-back; a shutdown that flushes the filesystem but not the cache can still lose the very writes the filesystem just made.

---

## 8. Timing budget

The EC's watchdog is 10 seconds from `EC_PWR_REQ` to acknowledgement. The kernel must fit inside that, with margin.

| Phase | Budget |
|---|---|
| Interrupt to session manager notified | < 10 ms |
| Userspace grace period (step 2) | 3 s |
| Process termination (step 4) | 500 ms |
| Filesystem flush and unmount (step 6) | 2 s |
| Cache flush and MMU quiesce (step 7) | 100 ms |
| **Total worst case** | **~5.6 s** |

That leaves roughly 4.4 seconds of margin against the watchdog. The grace period is the tunable: if real workloads need longer, the EC watchdog must grow to match, and the two values must be changed together. **A grace period longer than the watchdog turns every shutdown into a forced one** — the failure is silent, since a forced shutdown looks identical to a successful one from the outside, right up until someone loses data.

Record both numbers in one place and treat them as a matched pair.

---

## 9. Battery-critical shutdown

When the telemetry path of §5 exists, the EC will raise a request with source `PWR_SRC_BATTERY` on a critical battery condition. The kernel handles it as an ordinary ordered shutdown with two differences:

- **No user dialogue and no cancellation.** `PWR_CANCEL` fails immediately.
- **Shortened grace period**, 1 second rather than 3.

Whether the kernel should be able to `NAK` a battery-critical request at all is an open policy question. The argument against is that the battery does not negotiate.

---

## 10. Boot-side counterpart

For symmetry, the kernel should clear any stale power request during `init`. A machine that was force-shut with a request latched would otherwise raise a shutdown interrupt immediately on the next boot, producing a machine that appears to refuse to start. Helium's gateware is specified to clear the latch on reset, so this is belt and braces — but the failure mode is bad enough to justify both.

---

## 11. Open items

| # | Item | Blocks |
|---|---|---|
| 1 | ~~Telemetry path from EC to CPU for battery state~~ — **closed**, see `battery-telemetry.md` | — |
| 2 | Session manager protocol for the shutdown message and its acknowledgement | Userspace implementation |
| 3 | Whether battery-critical may be declined | Kernel and EC policy, must agree with `helium-power-control.md` item 5 |
| 4 | Grace period and EC watchdog as a matched pair — where the canonical values live | Both firmware and kernel |
| 5 | Reboot: does the kernel need to distinguish warm from cold restart? | EC firmware |
