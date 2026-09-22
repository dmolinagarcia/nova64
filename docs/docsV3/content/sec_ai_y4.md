# Filesystems — the host track
> three drivers under FUSE · the vtable · what transfers back

[Sheet Y1](sec_ai_y1) specifies the layer the machine runs and [Y2](sec_ai_y2) and [Y3](sec_ai_y3) the two formats beneath it. This sheet specifies what is built before any of them, on a development PC, and what of it comes back. It is the only filesystem sheet whose deliverable is an interface rather than a format, and the only series in the document that needs no hardware at all.

- Y4.1 — **The track buys three things and only the third leaves an artefact the machine keeps.** Implementing a filesystem from the on-disk bytes up is the only reliable way to know one, and the knowledge is the reason the track exists at all. [FUS-04](sec_ai_y4#fus-04) de-risks [sheet Y3](sec_ai_y3) by building its read path where a debugger, sanitizers and `e2fsck -fn` exist. But the artefact is the vtable: three filesystems behind one internal table is the cheapest way to discover what [Y1](sec_ai_y1)'s interface has to be, and it is the one output that is lifted rather than rewritten.
- Y4.2 — **The order is FAT32, then ext2, then NVFS, chosen so that exactly one thing is hard at a time.** FAT32 has no inodes, no ownership, no mode bits and no hard links, so the only new material is FUSE itself plus long-filename reassembly. ext2 introduces the whole classical Unix vocabulary at once, by which point FUSE is free. NVFS comes last because its format work is done and its gate is purely the binding problem ([D56](sec_ai_q#d56)).
- Y4.3 — **Every read path before any write path.** Write is where ordering, crash consistency and allocation policy live, and all three are decided in the shared layers rather than in a driver. Doing three reads first means a bad decision there is found once, before it has been baked into three write paths.
- Y4.4 — **Four layers, with rules that can be checked rather than intended.** L0 is the block device and the only layer that knows the store is a file; L1 decodes on-disk structures and performs no I/O at all; L2 is filesystem logic and never touches a FUSE type; L3 is the FUSE binding and contains no arithmetic. Each rule is stated as a prohibition because a prohibition can be reviewed against a diff.
- Y4.5 — **`fsops` is the deliverable and the three daemons are how it was discovered.** Every change to the header is recorded with the reason that forced it, because the change log is the evidence [Y1](sec_ai_y1) needs and the header alone is only an assertion. The track is not a search for a good interface in the abstract: it is three implementations arguing with one table until the table stops changing.
- Y4.6 — **The table is keyed on inode number, never on a path.** A path-keyed interface cannot express hard links, cannot survive a rename underneath an open file, and hands every driver the path-resolution problem the VFS already owns. FAT32 has no inode numbers, so the driver synthesises them, and that synthesis is deliberately the first hard thing the track meets rather than the last ([Y1.7](sec_ai_y1#y17), [Q92](sec_ai_q#q92)).
- Y4.7 — **Explicit byte accessors, and no packed structure cast over a sector buffer — on the host.** Three reasons: an unaligned access is undefined behaviour and sanitizers say so; a cast hides which field is being read where an accessor names it; and the eventual target is a different compiler with a 16-bit `int`. [Sheet Y3](sec_ai_y3) specifies the opposite idiom for the machine, which is a contradiction rather than a difference of taste (→ [Q173](sec_ai_q#q173)).
- Y4.8 — **L2 carries its own error enum and L3 maps it at the boundary, once.** The host enum is deliberately narrower than [Y1.15](sec_ai_y1#y115)'s, because a host driver answers to `errno` and the machine's answers to the `COP` ABI. The one value with behaviour attached is the corruption code: it always logs, since a filesystem that reports damage silently is a filesystem whose damage is found by the next write.
- Y4.9 — **L0 is the layer that is replaced wholesale on the machine, and it is written knowing that.** [Sheet G](sec_ai_g)'s SD driver takes its place with nothing above it changed, which is why sector size is a runtime field and never a constant, and why the read and write entry points take a block number and a count rather than a file offset.
- Y4.10 — **A block read range-checks the request and fails it, rather than returning what it could.** Every hang on a corrupt image starts with an unchecked read of a block number decoded out of the damage, and a short read that reports success turns that damage into plausible data. The check costs one comparison in the one layer every access passes through.
- Y4.11 — **Differential testing against the kernel's own driver is the read-path oracle.** Every operation is performed twice, once through the driver under test and once through a reference mount of the same image, and the results are compared field by field. It is what makes a read path testable without a specification of the answers, and it is the mechanism [Y3.15](sec_ai_y3#y315) describes for the machine.
- Y4.12 — **Crash injection and a corruption corpus are the write-path oracles, and a hang is a failure.** The harness hooks a numbered write, truncates the image there, and requires that what remains is either clean or trivially recoverable under `fsck.fat -n`, `e2fsck -fn` or `nvfsck`. The corpus is a set of deliberately damaged images kept as a regression suite, and every run carries a timeout, because without one a hang reads as a slow pass.
- Y4.13 — **The drivers run single-threaded.** libfuse is multi-threaded unless told otherwise, and letting that stand makes its threading model a dependency of a design whose concurrency contract is [Y1.19](sec_ai_y1#y119)'s. Multi-threading is an experiment at the end of the track, not a default at the start of it.
- Y4.14 — **The chain is strictly linear and there is no parallelism in it.** Each gate is built on the one before, which is a calendar cost accepted openly: the track blocks nothing and nothing blocks it, so its only scarce resource is attention, and splitting attention across two gates is how a shared layer acquires two half-designs.
- Y4.15 — **FAT32 earns its gate by what it lacks.** A format with no inodes, no ownership and no permissions forces the driver to synthesise every one of them, and therefore to state what an attribute structure means to a caller before ext2 hands one over for free. It is the cheapest available lesson in the difference between what a filesystem stores and what a caller is told.
- Y4.16 — **Twenty-eight of the track's steps produce nothing that lands on the machine, and that is a price rather than a defect.** [D54](sec_ai_q#d54) chose ext2 as the transfer medium and nothing since revisits it, so both FAT32 gates are teaching order paid for in calendar time ([D98](sec_ai_q#d98)).
- Y4.17 — **[FUS-04](sec_ai_y4#fus-04) is the gate the machine is actually buying.** It is the host-side prototype of [sheet Y3](sec_ai_y3)'s read path, built against the same on-disk structures and the same feature-flag policy, and a bug found there is a bug not found on a 65816 with a serial console.
- Y4.18 — **The NVFS gates are a binding and nothing in them reimplements the format.** All format logic stays in the existing library, where the conformance suite already covers it; what the gates build is the adapter between that library and the vtable, which is the only part the machine has never had.
- Y4.19 — **The binding is written against the format the machine will mount, not the one the note was written against.** The source note pins its NVFS gates to the previous draft, its library and its generated corpora; [Y2.1](sec_ai_y2#y21) broke the format in both directions, so the harnesses survive and their corpora do not. Regenerating them before [FUS-05](sec_ai_y4#fus-05) starts is cheaper than writing the binding twice ([D100](sec_ai_q#d100), → [Q175](sec_ai_q#q175)).
- Y4.20 — **Ownership, permissions and the clock are where the host shape most wants to leak back.** The host attribute structure is POSIX-shaped and the host driver calls the host clock, while [Y1.16](sec_ai_y1#y116) enforces no permissions and [Y1.17](sec_ai_y1#y117) forbids a driver the clock outright. Carrying the fields is harmless; inventing values for them is the habit that has to stop at the boundary (→ [Q176](sec_ai_q#q176)).
- Y4.21 — **htree is the one question the two sides of the work closed in opposite directions.** The track refuses to modify an indexed directory at all; [Y3.6](sec_ai_y3#y36) clears the index flag on first modification and falls back to a linear scan. Both are safe and only one can be the prototype, which is precisely what running the write gate before the target write path exists is for (→ [Q174](sec_ai_q#q174)).
- Y4.22 — **The track owes three write-backs, and one of them has a hardware deadline.** What [FUS-04](sec_ai_y4#fus-04) and [FUS-07](sec_ai_y4#fus-07) reveal about ext2 goes back to [sheet Y3](sec_ai_y3); the vtable change log goes to [sheet Y1](sec_ai_y1); and the block-transfer figure goes to [sheet G](sec_ai_g)'s register map before it is frozen ([Y2.24](sec_ai_y2#y224)).
  NOTE: Only the third has a date attached. If the track has not reached a view on transfer sizes by then, the figure is taken from the existing driver instead — **a learning track does not get to block hardware**.
- Y4.23 — **The track is done when the vtable can be lifted into [sheet Y1](sec_ai_y1) unchanged, not when three daemons mount.** Three mounted daemons are how that was established, and they are discarded afterwards; nothing in the host drivers is meant to survive except what the machine reimplements behind the same table.

## The `fsops` vtable — the shape it starts with, and every change to it is a deliverable rather than a detail

| Group | Slots | Notes |
|---|---|---|
| Volume | mount, unmount, statfs | Opened over an L0 device, never over a path |
| Read | lookup, getattr, read, readlink | `lookup` takes a parent inode and one component |
| Directory | opendir, readdir, closedir | Iteration state is an object the driver owns |
| File | open, close | Per-open state, held by the caller |
| Write | create, mkdir, unlink, rmdir, rename, write, truncate, setattr, sync | Left unimplemented until [FUS-06](sec_ai_y4#fus-06) |

  NOTE: The write half exists in the header from the start and answers "not supported" until its gate, so that the shape of the table is settled while all three read paths are still being written against it.

## Where it disagrees with `vnode_ops` — six rows, each of them a question for [sheet Y1](sec_ai_y1) rather than a defect here

| Subject | This track | [Sheet Y1](sec_ai_y1) |
|---|---|---|
| Directory position | An opaque cursor object the driver allocates | A 32-bit cookie the caller holds ([Y1.3](sec_ai_y1#y13)) |
| Per-open state | `open` and `close` slots | Neither, and the argument is that opening is a VFS act ([Y1.10](sec_ai_y1#y110)) |
| Directory entry buffer | A name buffer inline in the returned structure | Caller-provided, against a 512-byte kernel stack ([Y1.12](sec_ai_y1#y112)) |
| Widths | 64-bit offsets and host-sized lengths | 32 bits throughout, because the machine has nothing wider |
| Missing slots | No link, symlink or per-file flush, although the write gates implement all three | All three present |
| Ownership and the clock | Carried and synthesised | Read and preserved, never enforced, never invented ([Y1.16](sec_ai_y1#y116), [Y1.17](sec_ai_y1#y117)) |

  NOTE: They are not six independent decisions. The cursor and the entry buffer are the same question about who owns decoding state, and the answer to it settles the other four (→ [Q172](sec_ai_q#q172)).

## Phasing · FUS-00–FUS-09 — off the critical path, and the only series that needs no hardware at all

- [ ] FUS-00 — **Environment and lab.** Images build reproducibly, a kernel reference mount works, and the build system runs.
- [ ] FUS-00.a — **Toolchain installed**, with libfuse 3.10 or newer reporting itself through `pkg-config`.
- [ ] FUS-00.b — **Mount plumbing and permissions**: the upstream example mounts, lists and unmounts as an ordinary user.
- [ ] FUS-00.c — **Repository skeleton and build system** — a plain build and a sanitizer build both succeed on an empty entry point.
- [ ] FUS-00.d — **Image generation scripts**, reproducible: running them twice produces byte-identical images.
- [ ] FUS-00.e — **Reference-mount harness** — a populated image enumerates identically across two mounts.
- [ ] FUS-01 — **FUSE mechanics.** A filesystem of one's own mounts read-only, and both libfuse APIs are understood well enough to choose between them.
- [ ] FUS-01.a — **The upstream examples built and run**, as the first thing that proves the installation rather than the code.
- [ ] FUS-01.b — **The protocol traced** — the trace answers how many attribute calls a single `ls -l` costs and why.
- [ ] FUS-01.c — **A pass-through filesystem on the high-level API, written from scratch**, with read-only enforced.
- [ ] FUS-01.d — **The same filesystem on the low-level API**, behaving identically, which is what makes the two comparable.
- [ ] FUS-01.e — **Caching and timeout experiments** — the call count is predicted before the run and the run agrees.
- [ ] FUS-01.f — **Sanitizer and Valgrind baseline**, proven by a deliberately introduced overflow that both catch.
- [ ] FUS-02 — **Shared infrastructure.** Block layer, accessors, logging, the vtable header, the differential harness and the corruption corpus, all tested with no filesystem logic written yet.
- [ ] FUS-02.a — **Block layer**, with every out-of-range case covered by a unit test rather than by an assumption.
- [ ] FUS-02.b — **Endian accessors and a bounds-checked cursor** — a read past the limit fails instead of returning whatever follows.
- [ ] FUS-02.c — **Logging and tracing**, level selectable at mount time and the trace level compiled out of a release build.
- [ ] FUS-02.d — **Shared binding scaffolding** — option parsing, usage, and the mount lifecycle every driver repeats.
- [ ] FUS-02.e — **The `fsops` header**, proven by refactoring the pass-through filesystem behind it with no change in behaviour.
- [ ] FUS-02.f — **Differential harness**, proven by reporting a failure against a deliberately broken driver.
- [ ] FUS-02.g — **Corruption corpus and fuzz scaffold**, one damaged image per named class of damage.
- [ ] FUS-03 — **FAT32 read-only.** Differential match against the kernel driver, and the corruption corpus survived.
- [ ] FUS-03.a — **Boot sector and BPB decoded**, with the parsed geometry matching `fsck.fat -v` on both images.
- [ ] FUS-03.b — **FSInfo parsed**, both signatures validated and absurd values discarded rather than trusted.
- [ ] FUS-03.c — **FAT access layer**, returning correct successors and rejecting out-of-range cluster numbers.
- [ ] FUS-03.d — **Cluster chain iterator with cycle detection** — the cyclic corpus images terminate with an error instead of spinning.
- [ ] FUS-03.e — **Short directory entry iteration**, enumerating the populated image's root exactly.
- [ ] FUS-03.f — **Long filename reassembly**, including orphaned runs and checksum mismatches.
- [ ] FUS-03.g — **Synthetic inode numbering** derived from the entry's position, since the format supplies none.
- [ ] FUS-03.h — **Path resolution**, with a non-directory component reporting the right error rather than a missing file.
- [ ] FUS-03.i — **Attribute synthesis** — mode, ownership and type invented under stated rules, and the differential checks agree.
- [ ] FUS-03.j — **Directory listing**, matching the reference mount in content and in order.
- [ ] FUS-03.k — **Open and read**, with whole-file hashes and partial reads matching.
- [ ] FUS-03.l — **Free-space reporting**, within one block of the kernel driver's.
- [ ] FUS-03.m — **Mount options**, so that the synthesised attributes can be made to match the kernel's exactly.
- [ ] FUS-03.n — **Differential pass** — zero differences across every check on both images.
- [ ] FUS-03.o — **Corruption pass** — no crash, no hang, no unbounded allocation and no sanitizer report.
- [ ] FUS-04 — **ext2 read-only.** Differential match against the kernel driver, and the corruption corpus survived. This is the gate the machine is buying ([Y4.17](sec_ai_y4#y417)).
- [ ] FUS-04.a — **Superblock parsed**, every field agreeing with `dumpe2fs -h` on both images.
- [ ] FUS-04.b — **Feature-flag gate**, refusing an ext4 image by naming the feature that made it unreadable ([Y3.4](sec_ai_y3#y34)).
- [ ] FUS-04.c — **Block group descriptor table**, matching the reference tool group by group.
- [ ] FUS-04.d — **Inode reading**, agreeing with `debugfs` on the root inode before anything depends on it.
- [ ] FUS-04.e — **Block map traversal** through all three levels of indirection, matching the reference block list for every file.
- [ ] FUS-04.f — **Directory entry iteration**, with the zero-length and past-the-end record cases refused rather than followed.
- [ ] FUS-04.g — **Path resolution and lookup**, distinguishing "not a directory" from "does not exist".
- [ ] FUS-04.h — **Attributes**, this time read rather than synthesised, including link counts.
- [ ] FUS-04.i — **Directory listing with entry types**, so that a caller can avoid a stat per entry.
- [ ] FUS-04.j — **Read**, matching hashes and partial reads on every file in the image.
- [ ] FUS-04.k — **Symbolic links**, both the inline form and the one stored in blocks.
- [ ] FUS-04.l — **Hard links and inode identity** — two paths to one inode report one identity, which is what the synthesised FAT numbering could not do.
- [ ] FUS-04.m — **Free-space reporting**, matching the kernel driver within rounding.
- [ ] FUS-04.n — **Sparse superblock backups**, with the computed locations matching the reference tool.
- [ ] FUS-04.o — **Larger-block regression** — the 4 KiB, 256-byte-inode image is clean, proving nothing was hard-coded.
- [ ] FUS-04.p — **Differential pass** — zero differences on both images.
- [ ] FUS-04.q — **Corruption pass**, with a fuzzing run long enough to be evidence rather than a gesture.
- [ ] FUS-05 — **NVFS read-only binding.** The conformance read subset passes through the mount, and the agreement harness runs with three participants.
- [ ] FUS-05.a — **Library API surface audited** against the vtable, every read-side slot marked present, derivable or missing.
- [ ] FUS-05.b — **The binding boundary recorded** — for each piece of logic, which side of the library edge it belongs on, and why.
- [ ] FUS-05.c — **Missing accessors added to the library**, with its existing conformance suite still passing unchanged.
- [ ] FUS-05.d — **Adapter layer**, driven through the vtable by unit tests before FUSE is involved at all.
- [ ] FUS-05.e — **Low-level FUSE binding**, mounting and behaving under ordinary tools.
- [ ] FUS-05.f — **Inode lifetime management**, surviving a stress run that opens and drops a hundred thousand paths.
- [ ] FUS-05.g — **Mount options and parity with the existing host tool**, so both read the same volume the same way.
- [ ] FUS-05.h — **Conformance read subset** passing through the front-end rather than against the library directly.
- [ ] FUS-05.i — **Agreement harness with three participants**, the binding joining the host tools and the target driver.
- [ ] FUS-06 — **FAT32 write.** `fsx` clean, `fsck.fat -n` clean, crash injection clean.
- [ ] FUS-06.a — **Write path enabled in L0**, with read-only mode proven to genuinely refuse.
- [ ] FUS-06.b — **FAT entry writing**, with both copies maintained and the reference checker clean afterwards.
- [ ] FUS-06.c — **Cluster allocation**, never returning a cluster already in a chain.
- [ ] FUS-06.d — **Chain extension and truncation**, with sizes surviving a remount.
- [ ] FUS-06.e — **Directory entry allocation**, reusing deleted slots before growing the directory.
- [ ] FUS-06.f — **Short name generation**, unique within the directory and accepted by the kernel driver.
- [ ] FUS-06.g — **Create, remove and the two directory operations**, each leaving the volume clean.
- [ ] FUS-06.h — **Write and truncate**, clean under a hundred thousand `fsx` operations.
- [ ] FUS-06.i — **Timestamps, attribute setting and free-count maintenance.**
- [ ] FUS-06.j — **`fsx` and the POSIX conformance runner**, the latter for the subset the format can honour.
- [ ] FUS-06.k — **Crash injection** — every injection point leaves a volume the checker calls clean or trivially repairable ([Y4.12](sec_ai_y4#y412)).
- [ ] FUS-07 — **ext2 write.** `fsx` clean, `e2fsck -fn` clean, crash injection clean. The prototype of everything [Y3.9](sec_ai_y3#y39) specifies.
- [ ] FUS-07.a — **Bitmaps and allocation primitives**, clean under synthetic allocate and free cycles.
- [ ] FUS-07.b — **Block group selection policy** — a file lands in its parent's group until the group is full.
- [ ] FUS-07.c — **Indirect block allocation**, through a file grown past the triple-indirect threshold.
- [ ] FUS-07.d — **Directory entry insertion**, with the kernel driver reading back what was written.
- [ ] FUS-07.e — **Link counts**, which is where a checker finds the mistakes an ordinary test does not.
- [ ] FUS-07.f — **Inode and block freeing**, with freed space genuinely reusable.
- [ ] FUS-07.g — **The six create-and-remove operations**, symbolic and hard links included.
- [ ] FUS-07.h — **Rename**, all six cases, which is the single hardest operation in the track.
- [ ] FUS-07.i — **Write, truncate and attribute setting**, clean under a million `fsx` operations.
- [ ] FUS-07.j — **Superblock and backup maintenance**, verified by checking against a backup rather than the primary.
- [ ] FUS-07.k — **Cache flushing and per-file sync**, so that a returned sync means the data is on the device.
- [ ] FUS-07.l — **`fsx` and the POSIX conformance runner.**
- [ ] FUS-07.m — **Crash injection at every ordering point**, which is the only test that finds a write-ordering bug.
- [ ] FUS-08 — **NVFS write and harness completion.** The full conformance suite passes through the mount, and the agreement harness has its third participant on the write side too.
- [ ] FUS-08.a — **Write-side audit of the library**, every write slot classified as the read-side audit classified its own.
- [ ] FUS-08.b — **Write-side adapter and binding** — create, write, rename and delete through the mount.
- [ ] FUS-08.c — **Durability and ordering review**, confirming no callback bypasses an ordering constraint the format requires.
- [ ] FUS-08.d — **Full conformance suite** through the front-end.
- [ ] FUS-08.e — **Agreement harness including write operations.**
- [ ] FUS-08.f — **`fsx` and the POSIX conformance runner for NVFS.**
- [ ] FUS-08.g — **Multi-threaded experiment** — either the driver is thread-safe under the suites or the reason it is not is written down ([Y4.13](sec_ai_y4#y413)).
- [ ] FUS-09 — **Windows and macOS, and this gate is optional.** FAT32 mounts read-only on Windows through a third-party FUSE layer.
- [ ] FUS-09.a — **The Windows FUSE layer evaluated**, with every difference from libfuse enumerated and a plan for each.
- [ ] FUS-09.b — **Build environment**, with L0 and L1 compiling and their unit tests passing there.
- [ ] FUS-09.c — **FAT32 read-only ported first**, as the driver with the least to lose.
- [ ] FUS-09.d — **Permissions and ownership mapped**, so that files are reachable at all.
- [ ] FUS-09.e — **Path and case semantics**, including names that differ only by case and names the platform reserves.
- [ ] FUS-09.f — **Mount semantics**, both as a drive letter and as a directory.
- [ ] FUS-09.g — **ext2 and NVFS ported**, read-only.
- [ ] FUS-09.h — **macOS surveyed on paper only**, because its two mechanisms are moving and neither is worth a driver yet.
