# Filesystems — ext2, the transfer medium
> reading a host's card · the feature gate · what writing would cost

The second filesystem exists for one purpose: moving files between this machine and a Linux or macOS host by carrying the card, with **no custom tooling on the host side**. It is not, and is not intended to become, the system volume — [sheet Y2](sec_ai_y2) is specified for this machine, and this one is a transfer medium. Both sit behind [sheet Y1](sec_ai_y1)'s single table.

- Y3.1 — **ext2 rather than FAT32, and the reasons are all about the 65816 rather than about licensing.** It is **little-endian on disk**, so every field is a direct load and there is no byte-swap in any structure access — which is not a small advantage when the alternative adds one to every access in the driver. It has a clean, small, well-documented structure set — superblock, group descriptor, inode, directory entry — with **no FAT chain to walk**. It carries a real directory tree, permissions, hard links and symlinks, which map onto [sheet Y1](sec_ai_y1) without loss. And it has no long-filename encoding layer and no patent ambiguity.
  NOTE: The cost is stated plainly: **ext2 has no journal**, so its crash consistency comes entirely from write ordering — the argument [Y2.2](sec_ai_y2#y22) spent a format break to stop making. [Y3.9](sec_ai_y3#y39) is that argument, made again, and it is the reason [Y3.16](sec_ai_y3#y316) recommends what it recommends.
- Y3.2 — **The target is revision 1, 1024-byte blocks, 128-byte inodes.** In scope: read/write mount of one partition, full path resolution, file read, write, extend and truncate, directory create and remove, hard links, rename, symlink read, an MBR partition scan and a write-back buffer cache. Out of scope and stated so: every ext3/ext4 feature — journals, extents, 64-bit block numbers, htree *maintenance*, metadata checksums — plus extended attributes, ACLs, quotas, resize, files above 2 GiB, and **an on-target `fsck`**. Recovery is the host's job and the tool is `e2fsck`.
- Y3.3 — **With 1 KiB blocks the geometry is fixed and small**: 8 inodes per block, 32 group descriptors per block, 8192 blocks per group because one bitmap block covers exactly 1024 × 8 bits, the superblock at byte 1024, and `s_first_data_block` = 1. **That last constant is why the write path is restricted to 1 KiB** — it becomes 0 at any larger block size, and the difference propagates into every group and bitmap index in the driver. Cache footprint scales directly with block size, and a 4 KiB read-modify-write costs four times the CPU on a machine where block copying is already the dominant cost. **The read path handles 1024, 2048 and 4096; the write path refuses anything but 1024**, and that is a restriction on this implementation rather than on the format.
- Y3.4 — **The feature-flag gate is the most safety-critical part of the mount path**, because mounting a volume whose features are not understood and then writing to it is the primary route to silent corruption. The rule is the standard one and the only safe one: **an unknown `INCOMPAT` bit refuses the mount, an unknown `RO_COMPAT` bit mounts read-only, `COMPAT` bits may be ignored for reading.** The downgrade is reported upward, never swallowed ([Y1.13](sec_ai_y1#y113)).

## Feature acceptance policy — the table the mount path implements literally.

| Set | Flag | Policy here |
|---|---|---|
| COMPAT | `DIR_PREALLOC` · `IMAGIC_INODES` · `RESIZE_INODE` | Ignore. Nothing is preallocated and inode 7 is never touched |
| COMPAT | `HAS_JOURNAL` | **Force read-only.** This is an ext3 volume; mounting it read-write as ext2 is legal only if the journal is clean, and verifying that is out of scope |
| COMPAT | `EXT_ATTR` | Ignore on read. On write, a modified inode's `i_file_acl` is preserved untouched |
| COMPAT | `DIR_INDEX` | Read-safe. **On any modification of that directory, clear its index flag** — see [Y3.6](sec_ai_y3#y36) |
| INCOMPAT | `FILETYPE` | **Supported**, and set by every modern `mke2fs`. Decides whether the directory entry's type byte means anything |
| INCOMPAT | `COMPRESSION` · `RECOVER` · `JOURNAL_DEV` · `META_BG` | **Refuse the mount.** `RECOVER` needs a journal replay only the host can do; `META_BG` moves the descriptor table |
| RO_COMPAT | `SPARSE_SUPER` | **Accept read-write.** It affects only where backup superblocks live, and this driver never writes backups and never resizes |
| RO_COMPAT | `LARGE_FILE` | **Accept read-write with a guard**: refuse to open for writing any regular file whose size-high field is non-zero |
| RO_COMPAT | `BTREE_DIR` | **Force read-only** |

- Y3.5 — **Twelve direct pointers and three levels of indirection, and the level that looks optional is not.** Twelve direct blocks reach 12 KiB, single indirect reaches 268 KiB, double indirect reaches 64.26 MiB and triple reaches 16.06 GiB. **A single-indirect implementation covers 268 KiB and is not sufficient; double indirect is mandatory.** Triple is needed only above 64 MiB and is implemented anyway, because the recursive walker handles all three levels with the same code and **omitting level 3 saves nothing.**
  NOTE: That walker is where [Y1.12](sec_ai_y1#y112)'s stack budget is spent, and the bound is explicit: **the read walker recurses at most three deep, and the truncate walker must be written to the same bound and not deeper.** It is easy to write truncation as the naturally deeper recursion and hard to notice until a process's kernel stack has already overflowed into its neighbour's.
- Y3.6 — **`DIR_INDEX` is the dangerous one, and the danger is that reading works.** An htree directory is deliberately backward-compatible for reading: the index root in block 0 hides inside a fake entry whose length spans the whole block, and interior nodes hide the same way, so a linear scan sees `.`, `..` and every real entry and sees the index blocks as empty space. **Writing is not backward-compatible.** Insert an entry into a leaf without updating the hash index and the host will subsequently fail to find that file — it exists, it occupies space, it appears in `readdir`, and it is not openable by name.
  NOTE: The fix is one field: **clear the directory's index flag on its first modification**, after which the host falls back to a linear scan for that directory. The orphaned index blocks stay allocated and harmless, and `e2fsck` reports and reclaims them.
  NOTE: The problem is removable at source, and the recommended host-side formatting is `mke2fs -t ext2 -b 1024 -I 128 -O ^dir_index,^resize_inode,^ext_attr`. Whether the driver is required to handle arbitrary host-created volumes or only that one invocation decides how much of the table above must actually be implemented (→ [Q98](sec_ai_q#q98)).

## Layer architecture — each layer calls only the one below it, with one deliberate exception: L3 reads the superblock through L0 directly, before the cache's geometry is known.

| | Layer | Contents |
|---|---|---|
| L12 | syscall / VFS | [sheet Y1](sec_ai_y1) |
| L11 | vnode operations | the shared table — NVFS and ext2 behind one surface |
| L10 | path resolution | `namei`, `nameiparent`, symlink resolution — **VFS-owned, not the driver's** |
| L9 | directory operations | lookup, add, remove, iterate, mkdir, rmdir, rename |
| L8 | file I/O | read, write, truncate |
| L7 | allocators | block and inode allocate and free |
| L6 | block map | `bmap`, allocating `bmap`, the indirect walk |
| L5 | inode layer | get, put, update, the in-memory inode table |
| L4 | group descriptors | fetch and dirty |
| L3 | superblock and mount | geometry, the feature policy of [Y3.4](sec_ai_y3#y34) |
| L2 | buffer cache | get, dirty, release, pin, sync |
| L1 | primitives | 32-bit arithmetic, 32/16 division, bitmap operations — mostly assembly |
| L0 | block device | read, write, flush, the MBR scan |

- Y3.7 — **The read-only subset is exactly L0–L6, L8 read, L9 iterate and lookup, and L10** — gates [E2.0](sec_ai_y3#e20) and [E2.1](sec_ai_y3#e21). It is well bounded, low risk, and it makes any Linux-formatted card readable. **The write path adds the allocators, truncation, record-length manipulation, the counter invariants, the whole ordering discipline of [Y3.9](sec_ai_y3#y39) and the crash campaign that validates it** — realistically three to four times the work, and carrying the risk of destroying data rather than merely failing to read it.
- Y3.8 — **The buffer cache and the in-memory inode table are both mandatory, and for different reasons.** Without a cache a single file creation is 15–20 device transactions. Without a refcounted inode table, two descriptors on one file diverge silently — the same failure [Y1.6](sec_ai_y1#y16) moved into the VFS, which is where it now lives for both drivers.
- Y3.9 — **With no journal, consistency is entirely a property of the order blocks reach the card, and one asymmetry generates every rule.** A block or inode may be **allocated but unreferenced** — that leaks space and `e2fsck` reclaims it. A block or inode must **never be free while still referenced** — that is corruption, and `e2fsck` cannot always repair it without data loss. In one line: **detach before freeing, allocate before referencing.**

## Write ordering — the flush points are where the guarantees live.

| Operation | Order |
|---|---|
| Create | allocate the inode → write the inode → **flush** → add the directory entry → **flush**. A crash between leaves an orphan `e2fsck` moves to `lost+found`; the reverse order leaves an entry pointing at an uninitialised inode, which is unrecoverable garbage |
| Extend | allocate the block → **write the data** → **flush** → store the pointer → update size and count, write the inode → **flush**. A crash before the pointer leaks a block; between pointer and size leaves a block past the end, which `e2fsck` corrects. Neither loses existing data. An indirect block is zeroed and written **before** the pointer to it is stored |
| Truncate | clear the pointer → write that inode or indirect block → **flush** → clear the bitmap bits and counters. **Detach first.** The reverse order leaves a live pointer to a block the allocator may immediately hand to another file — two files sharing a block, the worst outcome in the whole failure space |
| Delete | remove the directory entry → **flush** → decrement links, write the inode → **flush** → if zero, set the deletion time, truncate, free the inode |
| mkdir | allocate the inode → allocate the first block → write `.` and `..` → **flush** → write the inode with two links → **flush** → increment the parent's links → count the directory in the group → add the entry → **flush** |

  NOTE: **The volume is marked dirty for the entire duration of a read-write mount, deliberately.** Any crash therefore leaves it dirty and the host runs `e2fsck` at its next mount. That is the recovery mechanism; writing an on-target one would be a larger project than this driver.
  NOTE: **If the card's write cache cannot be reliably flushed, none of the above guarantees anything.** That is a hardware property, it is the same assumption [Y2.16](sec_ai_y2#y216) makes for the journal's barriers, and it should be **verified during SD bring-up and recorded, not assumed** (→ [Q96](sec_ai_q#q96)).

- Y3.10 — **On a structural inconsistency or a device error in a write path, the mount is downgraded to read-only immediately and the condition is latched**, and the console says so loudly. This is [Y1.15](sec_ai_y1#y115) stated where it is first needed. The cheap sanity checks that must be present, because without them the failure mode is silent corruption rather than a clean error: the superblock magic; the block-size and per-group counts non-zero; **the group count derived from blocks agreeing with the one derived from inodes**; each group's bitmap and table blocks inside that group's range; every block pointer in range; every inode number in range; and a directory's size a whole number of blocks.
  NOTE: **The directory-entry checks matter most of all** — a record length of zero turns directory iteration into an infinite loop, and one larger than the block turns it into an out-of-bounds read. Length at least 8, four-byte aligned, not running past the end of the block, and name length plus 8 within it. **This is the class of bug [Y2.12](sec_ai_y2#y212) designed away with fixed-size slots**, and having to defend against it here is the clearest measurement of what that decision was worth.
- Y3.11 — **Semantics the driver does not get to choose.** uid, gid and mode are read, preserved and never enforced ([Y1.16](sec_ai_y1#y116)). Timestamps come from `vfs_now()`, which is zero until the board has a clock ([Y1.17](sec_ai_y1#y117), [Q88](sec_ai_q#q88)). Names are byte strings compared bytewise, with no encoding assumption. `i_atime` is never updated on read. **Backup superblocks are never written** — `e2fsck` reconciles them.
- Y3.12 — **Three 65816 consequences, and only the third is a request on somebody else's sheet.** Endianness costs nothing, as [Y3.1](sec_ai_y3#y31) said. **A 1 KiB cache block must not straddle a bank boundary**, or the copy and the directory walk have to handle the wrap: align every cache block to 1 KiB out of a bank-aligned region, the same rule [Y2.23](sec_ai_y2#y223) states at 2 KiB, and let the assembly bitmap scans take a 24-bit pointer and use indirect-long indexed addressing rather than assuming a data bank.
  NOTE: **Block transfer is the dominant performance term and it is not close.** A 1 KiB block moved byte by byte through the accumulator at 8 MHz costs roughly 8–10 K cycles, about 1 ms; a 1 MB file copy is then 1000 blocks × 2 = **two seconds of pure copying** before any filesystem overhead. So [sheet G](sec_ai_g)'s register map must move a block into SDRAM **without CPU involvement**, or at minimum through a burst FIFO wide enough to avoid a per-byte handshake — which is the request [Y2.24](sec_ai_y2#y224) makes with a concrete number attached, and which [G.7](sec_ai_g#g7)'s DMA path already answers if it survives to implementation.

## Budgets — Calypsi C with hand-written assembly at L1.

| Layer | Code | | RAM item | Size |
|---|---|---|---|---|
| L0–L1 | 1.5 KB | | Buffer cache, 8 × 1 KiB | 8 KB |
| L2 buffer cache | 1.5 KB | | Buffer headers | 128 B |
| L3–L5 | 3 KB | | Pinned superblock | 1 KB |
| L6 block map | 2 KB | | **Pinned group descriptors, 2 GiB volume** | **8 KB** |
| L7 allocators | 2 KB | | In-memory inodes, 16 × 144 B | 2.3 KB |
| L8 file I/O and truncate | 3 KB | | Mount state, path and entry scratch | 1 KB |
| L9 directories | 4 KB | | | |
| L10 path resolution | 1.5 KB | | | |
| **Total read-write** | **≈ 18–19 KB** | | **Total** | **≈ 20.5 KB** |
| **Read-only subset** | **≈ 9 KB** | | | |

- Y3.13 — **The code budget is the term to check early.** If the driver is resident it competes with the kernel for a bank ([J.1](sec_ai_j#j1)); if it is not, it needs a loadable-module story that does not exist. That question should be answered before [E2.1](sec_ai_y3#e21) rather than after (→ [Q99](sec_ai_q#q99)).
- Y3.14 — **The RAM budget has one term that scales badly and it is the pinned descriptor table.** A 2 GiB volume has 256 groups and needs 8 KB; **a 32 GiB card has 4096 groups and needs 128 KB**, which is not payable. Either an LRU sub-cache over the descriptors or a stated maximum supported volume size, and the choice has to be made before the write path exists (→ [Q100](sec_ai_q#q100)). All of this lives in SDRAM: **the SRAM is a hardware-managed cache over SDRAM and is not carved up for filesystem buffers** ([F.5](sec_ai_f#f5)).
- Y3.15 — **Testing is differential against the kernel's own driver, and that is what makes this tractable at all.** Every operation is performed twice — once through this driver and once through a reference mount — and the results compared, with `e2fsck -fn` run on the image after every gate. **Crash injection hooks a specific write number** and truncates the image there, then checks that what remains is either clean or trivially recoverable. The host track of [the host track](sec_ai_p#p26) is where all of it is built, under a debugger and sanitizers rather than over a serial console.
- Y3.16 — **The recommendation is read-only, and it is a recommendation rather than a decision.** F0 and F1 give the interoperability the machine actually needs — getting files from a host onto this machine — at roughly a quarter of the cost and with none of the corruption risk. The write path is specified here in full so that the choice stays open and so that nothing has to be redesigned if it is taken, but **it should be revisited only when a concrete need for on-target writing to ext2 appears** (→ [Q101](sec_ai_q#q101)).
  NOTE: **Whichever path is taken, one operational rule holds.** During bring-up this machine will hang frequently, and a half-debugged read-write ext2 driver pointed at a card holding real data is a reliable way to lose it. **Use a dedicated card with nothing of value on it, and run `e2fsck` on the host after every session.**

## Phasing · E2.0–E2.7 — the gates are lettered for the filesystem, not for [sheet E](sec_ai_e). E2.0 to E2.5 are almost entirely host-side and are **not** blocked by the SD register map; only the on-target validation from E2.1 onward is.

- [ ] E2.0 — **L0–L1, the MBR scan, the host image harness.** An arbitrary LBA reads on host and target; the division and bitmap routines pass unit tests.
- [ ] E2.1 — **L2–L6, read, directory iteration and lookup, path resolution.** Any file on an `mke2fs` image reads correctly and a tree walk matches the host's.
- [ ] E2.2 — **Buffer cache write-back, superblock state, sync.** Mount read-write, touch nothing, unmount; `e2fsck -fn` clean and the state transitions correct.
- [ ] E2.3 — **The allocators, allocating `bmap`, `write`.** Files of every size up to double indirect created and written; `e2fsck -fn` clean after each.
- [ ] E2.4 — **Truncate, create, unlink, directory entry insert and remove.** A thousand operations of create/delete/truncate churn; `e2fsck -fn` clean.
- [ ] E2.5 — **mkdir, rmdir, link, rename, symlink read.** Full tree manipulation, and a host mount agrees with it.
- [ ] E2.6 — **Crash injection.** Every truncation point recoverable per [Y3.9](sec_ai_y3#y39).
- [ ] E2.7 — **Unification with NVFS behind [sheet Y1](sec_ai_y1)** — both filesystems on one syscall path. This is [V4](sec_ai_y1#v4) seen from the other side.
