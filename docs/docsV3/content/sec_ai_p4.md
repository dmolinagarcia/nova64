# Beyond the board · the laptop L1–L8 · the host filesystem track F0–F9
> what turns a working board into a machine · and the track that needs no hardware

Two things that sit outside the three phases of [sheet P1](sec_ai_p1), for opposite reasons. The **L-series** runs after [E8](sec_ai_p3#e8) has closed and turns a working board into something you pick up, open and close. The **F-series** runs on a development PC and can start today: it blocks no milestone and no milestone blocks it.

Neither is on the critical path for either milestone, which is exactly why they are listed here rather than folded into the stages they follow and allowed to delay them.

## Beyond E8 · L1–L8 — the portable machine

[E8](sec_ai_p3#e8) closes with a working noVa64 on a board that runs on battery and shows a windowed desktop. What follows turns that into something you pick up, open and close, and none of it is on the critical path for either milestone — which is exactly why it is listed separately rather than folded into E8 and allowed to delay it.

- [ ] L1 — **Internal keyboard matrix**, designed and scanned by the EC firmware, delivered through the existing input path of [V.30](sec_ai_v#v30) so that nothing above the HID driver knows the difference. A built-in keyboard replaces USB HID.
  TEST: every key and every two-key combination scanned without ghosting, with the HID driver unmodified above it.
  NOTE: [D1.6](sec_ai_d1#d16) moved a matrix *off* the EC's pins onto USB HID so its budget would close; this stage brings one back, so where it scans is that budget reopened.
- [ ] L2 — **Integrated pointing device** selected and driven into `wserver` through the same queue — pointer input with no external peripheral attached.
  TEST: the pointer crosses the full screen with no external peripheral attached, through the same queue and with no new event type.
- [ ] L3 — **Software power management** — rail gating, sleep and wake, battery status surfaced in the GUI through the `$FF` power block of [sheet S](sec_ai_s).
  TEST: sleep and wake with the display, the card and the audio path all restored, and the battery indicator agreeing with the gauge after the resume.
  NOTE: This is the stage where [Q73](sec_ai_q#q73) stops being theoretical. Everything resting on the power block had no prototype hardware at all ([P2.02](sec_ai_p2#p202)), so the battery indicator, the shutdown dialogue and the stale-telemetry behaviour get their first real exercise between [E8](sec_ai_p3#e8) and here.
- [ ] L4 — **Mechanical CAD** — lid and hinge, mainboard mounting, battery bay, port cutouts, keyboard tray. A complete enclosure model before anything is printed.
  TEST: the model checked against the assembled board's measured outline, with every connector reachable and every fastener placed before anything is printed.
- [ ] L5 — **Printed enclosure prototype** assembled, with the fit and cable-routing revisions applied. Everything physically fits and the lid closes.
  TEST: the lid closes, every port is usable with the case shut, and no cable is under strain in either lid position.
  NOTE: The revisions this stage produces are the ones CAD cannot predict — cable bend radius, connector access with a lid at 100°, and where a hand actually holds the machine.
- [ ] L6 — **Thermal profiling and battery-life characterisation** under sustained GUI load, inside the enclosure rather than on a bench.
  TEST: sustained GUI load to thermal equilibrium inside the closed case, with the battery figure taken on the same run rather than a separate one.
  NOTE: Inside the case is the only measurement that counts. A board that is comfortable in open air and a board in a sealed printed shell are different thermal problems, and the second one is the product.
- [ ] L7 — **REV B**, correcting the errata accumulated from [E1.4](sec_ai_p3#e14) onward, and final assembly into the enclosure.
  TEST: the errata list closed item by item — each one either fixed in REV B or recorded as accepted, with nothing left unclassified.
- [ ] L8 — **Release** — schematics, gateware, kernel, SDK, disk image, build instructions, user manual.
  TEST: someone who is not you builds it from what is published.
  NOTE: That test is also this document's: [A2.7](sc_a2#a27) claims nothing here is written anywhere else, and a stranger building the machine is how that gets checked.

!!! PORTABLE — a noVa64 you can pick up, open and use.

## The host filesystem track · F0–F9 — off the critical path, and the only track that needs no hardware at all.

Ten gates, 107 sequential steps, run entirely on a development PC. It produces three host filesystem drivers under FUSE — FAT32, ext2 and NVFS — and it is **not** part of the P, E or L series: it blocks no milestone and no milestone blocks it. It exists for three reasons, and only the first is the obvious one. **Understanding**: implementing a filesystem from the on-disk bytes upward is the only reliable way to know one rather than to have read about one. **De-risking**: gate F4 is a host-side prototype of [sheet Y3](sec_ai_y3)'s L0–L8, executed where a debugger, sanitizers and `e2fsck -fn` are all available — and **a bug found there is a bug not found on a 65816 with a serial console**. And **specification**: three independent filesystems behind one internal table is the cheapest possible way to discover what [sheet Y1](sec_ai_y1)'s interface actually has to be, rather than designing it and finding out later.

- P4.1 — **The order is FAT32, then ext2, then NVFS, and it is chosen so that exactly one thing is hard at a time.** **FAT32 first** because its model is a reserved region, N copies of one allocation table and a data region of fixed clusters — no inodes, no permissions, no ownership, no hard links, no sparse files. The only genuinely new material in its gate is FUSE itself plus long-filename reassembly, which makes it the cheapest way to get a real filesystem mounted. **ext2 second** because it introduces the whole classical Unix vocabulary at once — inodes, bitmaps, block groups, the indirect tree, permissions, hard links, sparse files, fast and slow symlinks — and by then the FUSE side is free, so the attention goes to the format. **NVFS last** because by that point there are two reference designs to judge it against and its format work is already done: that gate is purely about the *binding* problem, which is a different skill and deserves not to be tangled up with learning what a superblock is.
  NOTE: **Every read path before any write path**, which is the second ordering decision and the one that pays off least visibly. Write support is where the interesting failure modes live — ordering, crash consistency, allocation policy — and doing all three reads first means the write gates can be compared against each other while the read code is still fresh, and that **a bad decision in the shared layers is found before it has been baked into three write paths.**
- P4.2 — **Four layers, and the rules that keep the split real are enforceable rather than aspirational.** **L0** is the block device and is the only layer that knows the backing store is a file — on this machine it is replaced wholesale by [sheet G](sec_ai_g)'s SD block driver and **nothing above it changes**, which is why its sector size is a runtime field rather than a constant. **L1** decodes on-disk structures and **performs no I/O at all**: it takes a byte buffer and an offset and returns values, which makes it unit-testable against a static array with no filesystem and no mount. **L2** is the filesystem logic and **never touches a FUSE type**. **L3** is the FUSE binding and **contains no arithmetic** — a cluster-to-address calculation appearing there means it is in the wrong layer.
  NOTE: **Do not cast a packed structure over a sector buffer, however tempting it is on a little-endian host reading a little-endian format.** Three reasons and the third is the one that matters here: taking the address of a packed member yields an unaligned pointer and some of these offsets genuinely are unaligned; **the eventual target is a different compiler with 16-bit `int`, and explicit accessors port where struct casts do not**; and an explicit read at a named offset beside a table of offsets is checkable against the specification line by line, where a structure definition hides a padding bug behind a `sizeof` that happens to be right.
  NOTE: Four portability rules to follow from the first step rather than retrofit: **never rely on `int` being 32 bits** — block, cluster and inode numbers take explicit widths; **no recursion whose depth is a function of on-disk data**, so directory descent and indirect traversal are iterative or depth-bounded ([Y1.12](sec_ai_y1#y112)); allocation confined to one wrapper so it can become a pool allocator; and no `long` or `size_t` in on-disk arithmetic.
- P4.3 — **The internal table is inode-keyed, not path-keyed, and that decision is the whole point of running this track before [sheet Y1](sec_ai_y1) is frozen.** Path-keyed is simpler for FAT and does not survive hard links, and the kernel wants inode identity anyway — so FAT32 has to **synthesise** inode numbers, ownership and mode bits, which is the right forcing function: it makes explicit, early, what a `stat` structure actually means to a caller. `readdir` gets an opaque cursor rather than an offset, because both formats want to keep decoding state between calls. And the attribute structure is POSIX-shaped even where FAT has to invent most of it, because the alternative pushes the synthesis into every caller.
  NOTE: **This is the shape [Y1.21](sec_ai_y1#y121) requires**, and it is the one thing on this sheet that is cheaper to get right at one implementation than at three.

| Gate | Content | Exit |
|---|---|---|
| **F0** | Environment and lab | Images build, a kernel reference mount works, the build system runs |
| **F1** | FUSE mechanics | An in-memory filesystem of one's own mounts read-only; both libfuse APIs understood, and the low-level one chosen ([P4.3](sec_ai_p4#p43)) |
| **F2** | Shared infrastructure — block layer, accessors, logging, the vtable header, the differential harness, the corruption corpus | All of it tested, with **no filesystem logic written yet** |
| **F3** | FAT32 read-only | Differential match against the kernel driver; survives the corruption corpus |
| **F4** | **ext2 read-only** — the host-side prototype of [sheet Y3](sec_ai_y3)'s read path | Differential match against the kernel driver; survives the corruption corpus |
| **F5** | NVFS read-only binding | The conformance read subset and the cross-implementation agreement harness pass |
| **F6** | FAT32 write | `fsx` clean, `fsck` clean, crash injection clean |
| **F7** | **ext2 write** — the prototype of everything [Y3.9](sec_ai_y3#y39) specifies | `fsx` clean, `e2fsck -fn` clean, crash injection clean |
| **F8** | NVFS write and harness completion | Full conformance suite; a third participant in the agreement harness |
| **F9** | Windows and macOS — **optional** | FAT32 read-only mounts on Windows through a third-party FUSE layer |

- P4.4 — **The chain is strictly linear and there is no parallelism in it**, which matches this project's preference for one clear thread and accepts the calendar cost of that preference openly.
  NOTE: **Run the host implementations single-threaded.** libfuse is multi-threaded unless told otherwise, and letting that stand quietly makes its threading model a dependency of a design whose concurrency contract is [Y1.19](sec_ai_y1#y119)'s and not libfuse's.
  NOTE: **The track is done when the vtable is stable enough to be lifted into [sheet Y1](sec_ai_y1) unchanged** — not when three daemons mount. That is the artefact; the daemons are how it was discovered.

