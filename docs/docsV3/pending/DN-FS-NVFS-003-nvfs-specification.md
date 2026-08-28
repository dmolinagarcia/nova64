# DN-FS-NVFS-003 — NVFS On-Disk Format and Behaviour Specification

**Project:** noVa64 (DANI-65816)
**Document:** Specification, Draft 3
**Status:** For review — **not frozen**
**Supersedes:** NVFS specification draft 2 (format-incompatible — see §0.2 and §25)
**Scope:** Complete on-disk format, mount/unmount procedure, crash-consistency
model, and normative behaviour of the noVa64 native file system.

---

## 0. Reader's guide

### 0.1 What this document is

This is a from-scratch redesign of NVFS. It is written to be implementable
twice — once as a host-side C99 reference library (`libnvfs`) and once as a
65816 kernel driver under Calypsi — from this document alone, with no reference
to draft 2 and no reference to any existing implementation.

Everything in §3–§19 is normative. §20–§24 are informative but binding as
acceptance criteria. §25–§26 are project management, not format.

### 0.2 THIS IS A FORMAT BREAK — read before implementing

Draft 3 is **not** compatible with draft 2, in either direction. There is no
migration path in the file system itself. The following existing assets are
affected:

| Asset | Impact |
|---|---|
| NVFS specification draft 2 | Superseded, retained only as history |
| `libnvfs` (C99 reference) | Structure layer rewritten; API surface largely survivable (§21.6) |
| `mkfs.nvfs` | Rewritten — layout is different |
| `nvfsck` | Rewritten — invariants are different, journal recovery is new |
| `nv` CLI | Front-end mostly survives; anything format-aware moves to `libnvfs` |
| 65816 read-path driver | Rewritten — extents replace whatever draft 2 used |
| 65816 assembly CRC-32 | **Reusable unchanged** (§Appendix A pins the same polynomial) |
| Conformance suite (2,350 checks) | Test *harness* reusable; expected values and many checks are format-specific and must be regenerated |
| Crash-injection harness | **Reusable and more valuable than before** — it now has a journal to attack |
| Cross-implementation agreement harness (488 checks) | Harness reusable; corpus regenerated |

Existing draft 2 volumes must be evacuated by copying files out with the old
tooling before reformatting. Draft 3 refuses to mount a draft 2 volume: the
superblock is at a different block, and the magic/version check fails cleanly.

**This cost is real and it is the main argument against draft 3.** The main
argument *for* draft 3 is §0.3.

### 0.3 What draft 3 changes, and why

Five decisions differ from draft 2. Each is justified in the section named.

1. **A metadata journal replaces write-ordering discipline.** (§13, §14)
   Draft 2 obtained crash consistency purely from write ordering, which is why
   the crash-injection harness found ordering bugs. Ordering rules are a
   correctness argument spread across every mutating operation; a journal is a
   correctness argument in one place. On a machine with no on-target `fsck`,
   bounded, idempotent, single-pass recovery is worth its cost.

2. **Fixed 2048-byte block size, equal to the MMU page size.** (§4)
   A file system block is exactly one virtual-memory page and exactly four SD
   sectors. Demand paging an executable becomes one block read. Every
   block-number-to-index conversion in the driver becomes a shift, which matters
   on a CPU with no divide and a 16-bit ALU.

3. **Extents replace indirect block trees.** (§10)
   At roughly 2–2.5 ms per block transfer at 8 MHz, an indirect block read is a
   visible cost. Extents remove it for the common case of a file laid down in
   one or a few runs.

4. **Fixed-size 64-byte directory slots replace variable-length records.** (§11)
   Variable-length records with an in-band length field are the single largest
   source of directory corruption bugs in ext2-class file systems. Fixed slots
   make insert and delete trivially reversible and trivially checkable.

5. **`i_mode` and `de_type` are bit-identical to POSIX.** (§9.2, §11.3)
   The FUSE binding and the eventual noVa64 VFS pass these through with no
   conversion table.

### 0.4 Relationship to other project documents

- **DN-FS-VFS-001** (not yet written) will define the `fsops` vtable shared by
  NVFS and ext2. §21.6 of this document proposes a provisional surface; it is
  expected to change and this specification does not depend on it.
- **DN-FS-EXT2-001** defines the ext2 interoperability layer. NVFS remains the
  native read/write file system; ext2 remains a transfer medium.
- The Helium SD block register map is **not yet frozen**. §20.4 states what this
  specification needs from it. This is on the critical path.

---

## 1. Purpose and design goals

NVFS is the native file system of noVa64. It is the volume the machine boots
from, stores its kernel and applications on, and pages from. It is designed for
one specific machine and makes no attempt at generality.

**Goals, in priority order:**

1. **Recoverable without human intervention.** After any power loss, mount must
   either succeed with a consistent volume or fail with a specific reason. It
   must never succeed with a silently corrupt volume.
2. **Implementable on a 65816.** Every hot-path index computation must be a
   shift or a mask. Steady-state RAM must fit in single-digit kilobytes.
3. **Cheap in block transfers.** Transfers dominate cost; algorithmic elegance
   that costs an extra block read is a bad trade.
4. **Verifiable.** Every metadata block carries a checksum, or is protected by
   one that does. Every structural invariant must be checkable by a host-side
   `nvfsck` in a single pass over the volume.
5. **Faithful to POSIX where it is free.** Match POSIX encodings when matching
   costs nothing, so that the FUSE binding and the VFS are thin.

**Explicit non-goals:** compression, encryption, snapshots, quotas, extended
attributes, access control lists, multi-device volumes, online resize, block
sizes other than 2048, big-endian hosts.

---

## 2. Scope

### 2.1 In scope

- The complete on-disk byte layout of an NVFS volume.
- Mount, unmount, and journal recovery procedures.
- The crash-consistency model and its normative ordering rules.
- Semantics of every file and directory operation.
- Feature-flag mechanism and acceptance policy.
- Conformance obligations for any implementation.

### 2.2 Out of scope

- The host tooling command-line interface (`mkfs.nvfs`, `nvfsck`, `nv`).
- The FUSE binding.
- The kernel VFS layer and syscall surface (DN-FS-VFS-001).
- The SD card driver and Helium SD register map.
- Buffer cache policy, beyond the durability requirements in §14.

---

## 3. Conventions

### 3.1 Notation

- All numeric constants are decimal unless prefixed `0x`.
- `u8`, `u16`, `u32` denote unsigned integers of 8, 16 and 32 bits.
- Structure offsets are given in bytes from the start of the structure.
- "MUST", "MUST NOT", "SHOULD" and "MAY" carry their usual normative force.
- "Block" always means a 2048-byte NVFS block. "Sector" always means a
  512-byte SD sector. The two are never used interchangeably.

### 3.2 Byte order and alignment

All multi-byte fields are **little-endian**. This matches both the 65816 and
every intended host. **No implementation performs byte swapping.** An
implementation that finds itself needing a swap accessor has a bug elsewhere.

Every multi-byte field is naturally aligned within its containing structure.
The 65816 imposes no alignment requirement, but natural alignment lets host
implementations map structures directly and lets `_Static_assert` catch layout
drift (§21.5).

### 3.3 Reserved fields

Every field marked *reserved* MUST be written as zero and MUST be ignored on
read. An implementation MUST NOT refuse a volume because a reserved field is
non-zero; forward compatibility is governed exclusively by the feature flags in
§16.

### 3.4 Checksums

NVFS uses one checksum algorithm everywhere: **CRC-32 as defined in Appendix A**
(IEEE 802.3, reflected, polynomial 0xEDB88320, initial value 0xFFFFFFFF, final
XOR 0xFFFFFFFF). No other hash function appears in this specification. The
directory name hash (§11.4) is derived from the same routine.

**Block checksum placement rule.** Every 2048-byte metadata block that carries a
block-level checksum stores it as a `u32` at offset **0x7FC**, computed over the
full 2048 bytes of the block with those four bytes treated as zero. There is
exactly one such routine in any implementation.

Two structures are exceptions, deliberately:

- **Inode table blocks** carry no block-level checksum. Each 128-byte inode
  carries its own CRC-32 (§9.1), which gives finer-grained torn-write detection
  than a block checksum would.
- **Block bitmap and summary blocks** carry no checksum at all. See §7.4 for
  the rationale and the accepted consequence.

### 3.5 Time

All timestamps are `u32` seconds since 1970-01-01T00:00:00Z, unsigned. The
representable range ends in 2106. A volume whose host has no reliable wall clock
MUST write zero rather than a fabricated value; zero means "unknown" and
`nvfsck` MUST NOT report it as an error. See **OI-7**.

### 3.6 Names

File names are byte strings. NVFS assigns them no encoding and performs no
normalisation, no case folding and no validation beyond the following:

- A name MUST be between 1 and 56 bytes long.
- A name MUST NOT contain byte 0x00 or byte 0x2F (`/`).
- The names `.` and `..` MUST NOT appear in a directory slot; they are
  synthesised (§11.5).

Implementations SHOULD treat names as UTF-8 when presenting them to users but
MUST NOT depend on it. Two names that differ in any byte are different names.

---

## 4. Fundamental parameters

| Parameter | Value | Rationale |
|---|---|---|
| Block size | **2048 bytes, fixed** | = MMU page size; = 4 SD sectors; all index maths become shifts |
| Sectors per block | 4 | `lba = partition_lba + (block << 2)` |
| Inode size | **128 bytes, fixed** | 16 per block; `slot = (ino-1) & 15` |
| Block number width | `u32` | Same width as the SD LBA; two 16-bit adds on the 65816 |
| Inode number width | `u32` | 1-based; 0 means "none" |
| File size width | `u32` | 4 GiB − 1; the machine has a 16 MiB address space |
| Directory slot size | **64 bytes, fixed** | 30 slots per block after the header |
| Maximum name length | 56 bytes | Fits the fixed slot with no spill mechanism |
| Maximum extent length | 65535 blocks | 128 MiB per extent |
| Maximum extent tree depth | 3 | §10.4 |

Block size is **not** configurable in version 3.0. `s_block_size` exists in the
superblock so that a future version can relax this, and MUST be exactly 2048;
an implementation MUST refuse to mount a volume where it is not.

### 4.1 Derived index formulae

These are normative, and are the reason the constants above were chosen:

```
lba_of_block(b)          = s_partition_lba + (b << 2)

bitmap_block_of(b)       = s_bitmap_start  + (b >> 14)
bitmap_byte_of(b)        = (b >> 3) & 0x7FF
bitmap_bit_of(b)         = b & 7

summary_block_of(i)      = s_summary_start + (i >> 10)     /* i = bitmap block index */
summary_offset_of(i)     = (i & 0x3FF) << 1

itable_block_of(ino)     = s_itable_start  + ((ino - 1) >> 4)
itable_offset_of(ino)    = ((ino - 1) & 15) << 7

ibitmap_block_of(ino)    = s_ibitmap_start + ((ino - 1) >> 14)
```

Every one is a shift and a mask. There is no division anywhere in the NVFS
data path.

### 4.2 Limits

| Limit | Value |
|---|---|
| Maximum volume size | 2^32 − 1 blocks = 8 TiB (practically limited by SD media) |
| Maximum file size | 4 294 967 295 bytes (4 GiB − 1) |
| Maximum file blocks | 2 097 152 |
| Maximum inodes | 2^32 − 1, fixed at mkfs time |
| Maximum hard links per inode | 65 535 |
| Maximum directory entries | bounded by directory file size |
| Maximum extents per file | 6 × 169³ = 28 960 854 (depth 3) |
| Maximum blocks per journal transaction | 507 |
| Maximum name length | 56 bytes |

---

## 5. Volume layout

An NVFS volume occupies one MBR partition of type **0x7F** (§22). All block
numbers in this specification are relative to the first block of the partition.

```
block 0                                   Boot / reserved (NVFS never writes it)
block 1                                   Primary superblock
blocks 2 .. s_first_data_block-1           (see below)
last block of volume                       Backup superblock
```

The regions between the reserved area and the data area appear in this fixed
order. Their start blocks and lengths are recorded in the superblock and MUST
be consistent with this ordering; `nvfsck` MUST verify it.

| # | Region | Superblock fields |
|---|---|---|
| 1 | Reserved (blocks 0..7, includes the primary superblock at block 1) | fixed, 8 blocks |
| 2 | Journal superblock + journal ring | `s_journal_start`, `s_journal_blocks` |
| 3 | Block bitmap | `s_bitmap_start`, `s_bitmap_blocks` |
| 4 | Bitmap free-count summary | `s_summary_start`, `s_summary_blocks` |
| 5 | Inode bitmap | `s_ibitmap_start`, `s_ibitmap_blocks` |
| 6 | Inode table | `s_itable_start`, `s_itable_blocks` |
| 7 | Data | `s_first_data_block` .. `s_total_blocks - 2` |
| 8 | Backup superblock | `s_backup_sb` = `s_total_blocks - 1` |

The 8-block (16 KiB) reserved area exists so that a boot loader can live at the
front of the partition without a hole in the file system. Blocks 0 and 2..7 are
never read or written by an NVFS implementation.

### 5.1 mkfs sizing rules

These are the defaults `mkfs.nvfs` uses. They are informative; any layout that
satisfies §5 is valid.

```
journal_ring   = clamp(total_blocks / 64, 256, 8192)
s_journal_blocks = journal_ring + 1              /* + journal superblock */
inode_count    = total_blocks / 8                /* 1 inode per 16 KiB */
s_bitmap_blocks   = ceil(total_blocks / 16384)
s_summary_blocks  = ceil(s_bitmap_blocks / 1024)
s_ibitmap_blocks  = ceil(inode_count / 16384)
s_itable_blocks   = ceil(inode_count / 16)
```

A worked example is in Appendix C.

### 5.2 Blocks marked in-use at mkfs time

Every block from 0 through `s_first_data_block - 1` inclusive, and the backup
superblock block, MUST be marked allocated in the block bitmap. Bits beyond
`s_total_blocks` in the last bitmap block MUST be set to 1 so that the allocator
can never return an out-of-range block.

---

## 6. Superblock

The primary superblock occupies block 1. A byte-identical backup occupies the
last block of the volume. Both are 2048 bytes; all offsets beyond those listed
are reserved and zero.

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x00 | 4 | `s_magic` | bytes `'N' 'V' 'F' 'S'` (0x4E 0x56 0x46 0x53) |
| 0x04 | 2 | `s_version_major` | 3 |
| 0x06 | 2 | `s_version_minor` | 0 |
| 0x08 | 4 | `s_block_size` | MUST be 2048 |
| 0x0C | 4 | `s_total_blocks` | total blocks in the volume |
| 0x10 | 4 | `s_free_blocks` | advisory; authoritative value is the bitmap |
| 0x14 | 4 | `s_total_inodes` | fixed at mkfs time |
| 0x18 | 4 | `s_free_inodes` | advisory |
| 0x1C | 4 | `s_first_data_block` | first block usable for file data |
| 0x20 | 4 | `s_bitmap_start` | |
| 0x24 | 4 | `s_bitmap_blocks` | |
| 0x28 | 4 | `s_summary_start` | |
| 0x2C | 4 | `s_summary_blocks` | |
| 0x30 | 4 | `s_ibitmap_start` | |
| 0x34 | 4 | `s_ibitmap_blocks` | |
| 0x38 | 4 | `s_itable_start` | |
| 0x3C | 4 | `s_itable_blocks` | |
| 0x40 | 4 | `s_journal_start` | block of the journal superblock |
| 0x44 | 4 | `s_journal_blocks` | journal superblock + ring |
| 0x48 | 4 | `s_root_inode` | MUST be 1 |
| 0x4C | 4 | `s_backup_sb` | MUST be `s_total_blocks - 1` |
| 0x50 | 4 | `s_feature_compat` | §16 |
| 0x54 | 4 | `s_feature_incompat` | §16 |
| 0x58 | 4 | `s_feature_ro_compat` | §16 |
| 0x5C | 16 | `s_uuid` | random at mkfs time |
| 0x6C | 32 | `s_label` | volume label, NUL-padded, may be empty |
| 0x8C | 4 | `s_mkfs_time` | |
| 0x90 | 4 | `s_mount_time` | |
| 0x94 | 4 | `s_write_time` | |
| 0x98 | 4 | `s_mount_count` | |
| 0x9C | 4 | `s_max_mount_count` | 0 = never force a check |
| 0xA0 | 2 | `s_state` | 0 = clean, 1 = mounted/dirty, 2 = error |
| 0xA2 | 2 | `s_error_action` | 0 = continue, 1 = remount read-only, 2 = panic |
| 0xA4 | 4 | `s_last_check` | |
| 0xA8 | 4 | `s_next_generation` | seed for `i_generation` |
| 0xAC | 4 | `s_alloc_hint` | rotating allocation start point (advisory) |
| 0xB0 | 4 | `s_orphan_head` | head of the orphan inode list, 0 = empty (§15.7) |
| 0xB4 | 4 | `s_partition_lba` | informative copy of the partition start LBA |
| 0xB8 | — | reserved | zero through 0x7FB |
| 0x7FC | 4 | `s_crc32` | §3.4 |

`s_partition_lba` is informative only. An implementation MUST use the LBA it
obtained from the partition table, not this field, and SHOULD warn if they
disagree.

### 6.1 Advisory counters

`s_free_blocks` and `s_free_inodes` are advisory. They exist so that `statfs`
does not have to scan the bitmaps. They MUST be maintained inside the same
transaction as the bitmap change that alters them, so a crash cannot desynchronise
them. `nvfsck` recomputes them from the bitmaps and corrects them silently.

### 6.2 The backup superblock

The backup is written only by `mkfs.nvfs` and by `nvfsck`. It is **not** kept in
sync during normal operation: it records the immutable layout, which never
changes. Its counters and timestamps are those written at mkfs time and MUST be
ignored on recovery. Its purpose is to recover the layout of a volume whose
block 1 has been destroyed.

An implementation MUST NOT silently fall back to the backup superblock. If block
1 fails its magic or CRC check, mount fails; recovery from the backup is an
explicit `nvfsck` operation.

---

## 7. Block allocation

### 7.1 The block bitmap

The block bitmap is a flat array of bits, one per block of the volume, starting
at `s_bitmap_start`. Bit *n* corresponds to block *n*. **1 means allocated.**

Each bitmap block holds exactly 2048 × 8 = 16384 bits, which is why the index
formulae in §4.1 are shifts. Bits corresponding to blocks at or beyond
`s_total_blocks` MUST be 1.

There are no block groups. A flat bitmap is simpler, and at the volume sizes this
machine will see (typically 8–32 GiB, i.e. 256–1024 bitmap blocks) the locality
benefit of groups does not pay for their bookkeeping.

### 7.2 The free-count summary

The summary is a flat array of `u16`, one per bitmap block, starting at
`s_summary_start`. Entry *i* holds the number of **free** blocks in bitmap block
*i*, in the range 0..16384. Note that 16384 does not fit in 16 bits: the value
16384 is encoded as 0xFFFF, and 0xFFFF is therefore reserved for exactly that
meaning. An implementation MUST special-case it.

*(A cleaner alternative would be a `u32` per entry, at four times the cost. The
0xFFFF encoding was chosen because a completely free bitmap block only occurs on
a fresh volume and the special case is two instructions.)*

The summary lets the allocator skip a full bitmap block without reading it. On a
1024-bitmap-block volume the entire summary is one block.

### 7.3 Allocation policy

Policy is not normative — any implementation that returns a free block is
correct — but the following is what `libnvfs` and the 65816 driver SHOULD do,
because it is what the test corpus assumes:

1. If the caller supplies a goal block (the previous block of the same file),
   test that block; if free, take it. This produces contiguous extents for
   sequentially written files, which is the whole point of §10.
2. Otherwise scan forward through the summary from `s_alloc_hint`, wrapping
   once, for the first bitmap block with a non-zero free count.
3. Within that bitmap block, scan for the first zero bit.
4. Update, in one transaction: the bitmap bit, the summary entry,
   `s_free_blocks`.

`s_alloc_hint` is advanced past the allocated block. It is advisory and need not
be crash-consistent.

### 7.4 Why bitmap and summary blocks carry no checksum

Adding a 4-byte CRC to a bitmap block would leave 2044 usable bytes = 16352 bits,
which is not a power of two. `bitmap_block_of()` would become a division by
16352 on a CPU with no divide instruction, in the hottest path in the allocator.

The accepted consequence: a torn or bit-flipped bitmap block is **not** detected
at mount. It is detected by `nvfsck`, which recomputes the bitmap from the inode
extents and compares. Between the journal (which guarantees that a bitmap block
is only ever written from a CRC-protected transaction) and `nvfsck`, this is
judged acceptable.

This is **documented debt**, not an oversight. If a future revision moves to a
larger block size the arithmetic changes and this decision should be revisited.

---

## 8. Inode allocation

### 8.1 The inode bitmap

Identical in structure to the block bitmap: flat, 1 = allocated, bit *n*
corresponds to inode number *n + 1* (inodes are 1-based). Bits beyond
`s_total_inodes` MUST be 1.

### 8.2 The inode table

A flat array of 128-byte inodes starting at `s_itable_start`, 16 per block.
Inode *n* lives at block `s_itable_start + ((n-1) >> 4)`, byte offset
`((n-1) & 15) << 7`.

### 8.3 Reserved inode numbers

| Inode | Meaning |
|---|---|
| 0 | Not a valid inode; means "none" in every field where an inode number appears |
| 1 | Root directory. Always allocated, always a directory, `i_links` ≥ 2 |
| 2..7 | Reserved for future use. Marked allocated at mkfs time, type 0, `i_links` = 0 |
| 8 and above | Allocatable |

### 8.4 Generation numbers

Each newly allocated inode takes `i_generation` from `s_next_generation`, which
is then incremented. The pair (inode number, generation) uniquely identifies an
inode over the life of the volume, which the FUSE binding needs for `fuse_ino_t`
validity and which the eventual VFS will need if it ever caches file handles
across an unlink.

---

## 9. Inode format

An inode is exactly 128 bytes.

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x00 | 2 | `i_mode` | POSIX-identical type + permission bits (§9.2) |
| 0x02 | 2 | `i_links` | hard link count; 0 = free or orphaned |
| 0x04 | 4 | `i_size` | file size in bytes |
| 0x08 | 4 | `i_blocks` | allocated blocks, **in NVFS blocks** |
| 0x0C | 4 | `i_mtime` | last data modification |
| 0x10 | 4 | `i_ctime` | last inode change |
| 0x14 | 4 | `i_atime` | last access (§9.5) |
| 0x18 | 4 | `i_generation` | §8.4 |
| 0x1C | 2 | `i_flags` | §9.3 |
| 0x1E | 1 | `i_ext_count` | number of valid entries in `i_ext[]`, 0..6 |
| 0x1F | 1 | `i_ext_depth` | extent tree depth, 0..3 (§10) |
| 0x20 | 72 | `i_ext[6]` | six 12-byte extent entries (§10.2) |
| 0x68 | 2 | `i_uid` | |
| 0x6A | 2 | `i_gid` | |
| 0x6C | 4 | `i_rdev` | device number for character/block devices, else 0 |
| 0x70 | 4 | `i_orphan_next` | next inode in the orphan list, 0 = end (§15.7) |
| 0x74 | 1 | `i_dirhash_order` | DIRHASH feature only (§19), else 0 |
| 0x75 | 3 | reserved | zero |
| 0x78 | 4 | reserved | zero |
| 0x7C | 4 | `i_crc32` | CRC-32 over bytes 0x00..0x7B of this inode |

`i_blocks` counts **NVFS blocks**, not 512-byte units. This differs from ext2
and is deliberate: the ext2 convention of counting 512-byte units is a
well-known source of bugs, and there is no reason to inherit it. `i_blocks`
counts every block charged to the file, including extent tree nodes, and
excluding holes.

### 9.1 Free inodes

A free inode (bit clear in the inode bitmap) MUST have `i_mode` = 0 and
`i_links` = 0. Its remaining fields are undefined but SHOULD be zeroed.
`i_crc32` MUST still be valid, so that a free inode is distinguishable from a
corrupt one.

### 9.2 `i_mode`

`i_mode` is bit-identical to the low 16 bits of POSIX `st_mode`.

| Bits 15..12 | Type | Value |
|---|---|---|
| 0x1 | FIFO | 0x1000 |
| 0x2 | Character device | 0x2000 |
| 0x4 | Directory | 0x4000 |
| 0x6 | Block device | 0x6000 |
| 0x8 | Regular file | 0x8000 |
| 0xA | Symbolic link | 0xA000 |

Any other value in bits 15..12 makes the inode invalid.

Bits 11..9 (setuid, setgid, sticky) are **reserved and MUST be zero** in version
3.0. noVa64 is a single-user machine; these bits have no meaning and accepting
them would create a security surface with no corresponding benefit. An
implementation MUST reject an attempt to set them.

Bits 8..0 are the standard `rwxrwxrwx` permission bits. The noVa64 kernel
interprets only the owner triplet; group and other exist so that the FUSE
binding round-trips correctly and so that a future multi-user story is not
foreclosed.

### 9.3 `i_flags`

| Bit | Name | Meaning |
|---|---|---|
| 0x0001 | `NV_FL_IMMUTABLE` | file may not be modified, renamed or unlinked |
| 0x0002 | `NV_FL_APPEND` | file may only be opened for append |
| 0x0004 | `NV_FL_SYNC` | every mutation commits its transaction before returning |
| all others | reserved | MUST be zero |

### 9.4 Type-specific interpretation

| Type | `i_size` | Extent area |
|---|---|---|
| Regular file | bytes | extent tree (§10) |
| Directory | bytes, always a multiple of 2048 | extent tree, no holes permitted |
| Symbolic link, `i_size` ≤ 72 | target length in bytes | target bytes stored inline at 0x20, `i_ext_count` = 0, `i_ext_depth` = 0, `i_blocks` = 0 |
| Symbolic link, `i_size` > 72 | target length in bytes | extent tree, target stored as file data |
| Character/block device | 0 | unused, zero; device number in `i_rdev` |
| FIFO | 0 | unused, zero |

The inline symbolic link ("fast symlink") is detected purely by
`i_size` ≤ 72 on a link inode. No flag bit is needed and none is defined.

### 9.5 `i_atime`

Maintaining `i_atime` turns every read into a write. On this machine that is not
affordable. Implementations MUST default to *not* updating `i_atime` on read
(the equivalent of `noatime`). `i_atime` is updated by `mkfs`, on inode creation,
and on explicit `utimes`. A mount option MAY enable relative-atime behaviour;
the format supports it and nothing else in this specification depends on it.

---

## 10. Extents

### 10.1 Model

A file's logical blocks are mapped to physical blocks by a tree of extents. The
root of the tree lives in the inode (`i_ext[]`, up to 6 entries). If
`i_ext_depth` is 0 the root entries are leaves; otherwise they are index entries
pointing to extent nodes.

A logical block not covered by any extent is a **hole**. Reading a hole yields
zero bytes. Holes are represented by absence of coverage, never by a special
extent value.

Extents at every level MUST be sorted by ascending logical block and MUST NOT
overlap. This is what makes lookup a binary or linear search and what makes
`nvfsck` able to validate the tree in one pass.

### 10.2 Extent entry (12 bytes)

**Leaf entry** (appears in `i_ext[]` when `i_ext_depth` = 0, or in a node with
`xn_depth` = 0):

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x00 | 4 | `ex_lblk` | first logical block of the file covered |
| 0x04 | 4 | `ex_pblk` | first physical block; MUST be ≥ `s_first_data_block` |
| 0x08 | 2 | `ex_len` | length in blocks, 1..65535; 0 is invalid |
| 0x0A | 2 | `ex_flags` | bit 0 = `NV_EX_UNWRITTEN`; all others reserved |

**Index entry** (appears in `i_ext[]` when `i_ext_depth` > 0, or in a node with
`xn_depth` > 0):

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x00 | 4 | `ix_lblk` | first logical block covered by the subtree |
| 0x04 | 4 | `ix_pblk` | physical block of the child node |
| 0x08 | 2 | reserved | zero |
| 0x0A | 2 | reserved | zero |

`NV_EX_UNWRITTEN` marks preallocated space: the blocks are charged to the file
and MUST NOT be allocated to anything else, but their contents are undefined and
a read MUST return zeros. Writing into an unwritten extent splits it. Version
3.0 implementations are not required to *create* unwritten extents, but MUST
handle them correctly on read, because the host-side `libnvfs` will create them
for `fallocate`.

### 10.3 Extent node (2048 bytes)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x000 | 4 | `xn_magic` | bytes `'N' 'V' 'X' 'N'` |
| 0x004 | 2 | `xn_entries` | number of valid entries, 1..169 |
| 0x006 | 2 | `xn_depth` | 0 = this node holds leaf entries |
| 0x008 | 4 | `xn_inode` | owning inode number (back-reference for `nvfsck`) |
| 0x00C | 4 | reserved | zero |
| 0x010 | 2028 | entries | 169 × 12 bytes; entries beyond `xn_entries` MUST be zero |
| 0x7FC | 4 | `xn_crc32` | §3.4 |

169 entries per node is exact: 0x010 + 169 × 12 = 0x7FC.

### 10.4 Depth and capacity

| `i_ext_depth` | Structure | Maximum extents |
|---|---|---|
| 0 | 6 leaf entries in the inode | 6 |
| 1 | 6 nodes, each 169 leaves | 1 014 |
| 2 | 6 → 169 → 169 | 171 366 |
| 3 | 6 → 169 → 169 → 169 | 28 960 854 |

`i_ext_depth` MUST NOT exceed 3. A maximum-size file (2 097 152 blocks) in which
every extent is a single block requires depth 3, so depth 3 is sufficient for
every representable file.

### 10.5 Lookup

To map logical block `L`:

```
entries = i_ext[]; count = i_ext_count; depth = i_ext_depth
while depth > 0:
    find the last entry E with E.ix_lblk <= L
    if none: L is a hole -> return hole
    read node at E.ix_pblk; verify xn_magic, xn_crc32, xn_inode
    verify node.xn_depth == depth - 1        /* structural check, mandatory */
    entries = node entries; count = node.xn_entries; depth -= 1
find the last entry E with E.ex_lblk <= L
if none or L >= E.ex_lblk + E.ex_len: return hole
return E.ex_pblk + (L - E.ex_lblk), E.ex_flags
```

The `xn_depth == depth - 1` check is mandatory, not advisory: it is what stops a
corrupted `ix_pblk` from causing an unbounded walk. Combined with the depth cap
of 3, the walk is bounded by construction.

### 10.6 Insertion, splitting and merging

Normative requirements only:

- An implementation MUST merge an extent with its predecessor when
  `pred.ex_pblk + pred.ex_len == new.ex_pblk` and
  `pred.ex_lblk + pred.ex_len == new.ex_lblk` and both have identical
  `ex_flags` and the combined length does not exceed 65535. Without this,
  sequential writes produce one extent per block and defeat the design.
- When a node is full, it is split at the midpoint and a new entry is inserted
  in the parent. If the parent is the inode and `i_ext_count` is already 6, the
  tree grows a level: a new node is allocated, the six inode entries move into
  it, and `i_ext_depth` is incremented.
- All structural changes to a file's extent tree, together with the inode and
  the affected bitmap and summary blocks, MUST be committed in a single
  transaction (§14.2).

Merging on delete is not required. A tree that becomes sparse after truncation
MAY be left sparse; `nvfsck` MUST NOT report it.

### 10.7 Truncation

Truncation to a smaller size frees, in one transaction:

1. every extent entirely beyond the new end;
2. the tail of the extent that straddles the new end;
3. every extent node that becomes empty;
4. levels of the tree that become redundant (`i_ext_depth` decreases when a
   single node remains and its entries fit in the inode).

Truncation to a *larger* size creates a hole and allocates nothing. `i_size`
increases; `i_blocks` does not.

---

## 11. Directories

A directory is a file whose data is an array of directory blocks. Directories
MUST NOT contain holes: every logical block from 0 to `i_size / 2048 - 1` MUST
be mapped.

### 11.1 Directory block (2048 bytes)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x000 | 4 | `db_magic` | bytes `'N' 'V' 'D' 'B'` |
| 0x004 | 4 | `db_inode` | owning directory inode (back-reference) |
| 0x008 | 2 | `db_used` | occupied slots, 0..30 |
| 0x00A | 2 | `db_flags` | reserved, zero |
| 0x00C | 4 | `db_next` | overflow chain, DIRHASH only (§19); else 0 |
| 0x010 | 48 | reserved | zero |
| 0x040 | 1920 | slots | 30 × 64 bytes |
| 0x7C0 | 60 | reserved | zero |
| 0x7FC | 4 | `db_crc32` | §3.4 |

### 11.2 Directory slot (64 bytes)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x00 | 4 | `de_inode` | inode number; **0 = free slot** |
| 0x04 | 2 | `de_hash` | low 16 bits of CRC-32 of the name bytes |
| 0x06 | 1 | `de_namelen` | 1..56 |
| 0x07 | 1 | `de_type` | §11.3 |
| 0x08 | 56 | `de_name` | name bytes, not NUL-terminated; unused bytes MUST be zero |

There is no length-linked record chain and therefore no way for a corrupted
length field to desynchronise a block. A free slot is a slot with
`de_inode` = 0; deletion is a single-field write followed by zeroing the rest of
the slot. Slots within a block are unordered.

`de_hash` exists so that a lookup compares two bytes before it compares up to 56.
It is a fast reject only; a match MUST still be confirmed by comparing
`de_namelen` and the name bytes.

### 11.3 `de_type`

Identical to the Linux `DT_*` constants, so the FUSE binding passes it through:

| Value | Type |
|---|---|
| 0 | unknown |
| 1 | FIFO |
| 2 | character device |
| 4 | directory |
| 8 | regular file |
| 10 | symbolic link |
| 6 | block device |

`de_type` MUST agree with the type nibble of the target inode's `i_mode`.
`nvfsck` reports disagreement as an error and repairs it from the inode, which
is authoritative.

### 11.4 The name hash

```
de_hash = crc32(name_bytes, namelen) & 0xFFFF
```

The same CRC-32 routine as everywhere else. The upper 16 bits of the same value
are used for bucket selection under DIRHASH (§19), so a single CRC pass over the
name serves both purposes.

### 11.5 `.` and `..`

Neither `.` nor `..` occupies a directory slot. Both are synthesised:

- `.` resolves to the directory's own inode.
- `..` resolves to the parent. The parent inode number is **not stored in the
  directory**; it is supplied by the caller, which necessarily knows it because
  it reached the directory through the parent. For the root directory, `..`
  resolves to the root.

This removes two slots of overhead per directory and, more importantly, removes
the `..` link-count bookkeeping that is a persistent source of `fsck` errors. Its
cost is that a directory inode found by inode number alone has no path back to
its parent; `nvfsck` reconstructs parenthood by full traversal, which it does
anyway.

`i_links` for a directory is therefore simply: 1 for the entry in its parent,
plus 1 for itself (`.`), i.e. **always exactly 2** for a directory with no hard
links. Subdirectories do **not** increment their parent's link count.

*This differs from POSIX convention, where a directory's link count is
2 + number of subdirectories. The FUSE binding MUST synthesise the POSIX value
by counting subdirectories if any tool depends on it; `find -noleaf` semantics
apply otherwise. This is a deliberate trade and is called out in §26 as **OI-8**.*

### 11.6 Lookup

Linear scan: for each directory block in logical order, for each of the 30 slots,
skip if `de_inode` = 0, skip if `de_hash` differs, skip if `de_namelen` differs,
otherwise compare name bytes. First match wins. Duplicate names within a
directory are a corruption; `nvfsck` reports them.

Worst case is one block read per 30 entries. A 300-entry directory costs 10 block
reads ≈ 25 ms. This is the cost that the optional DIRHASH feature (§19) exists to
remove; version 3.0 implementations are not required to support it.

### 11.7 Insertion

Scan for the first free slot in the first block that has `db_used` < 30. If every
block is full, append a new directory block: allocate a block, initialise the
header, zero all slots, extend the extent tree, increase `i_size` by 2048 and
`i_blocks` by 1. All of this is one transaction.

### 11.8 Deletion

Set `de_inode` = 0, zero the remaining 60 bytes of the slot, decrement `db_used`,
recompute `db_crc32`. Empty directory blocks are **not** freed and the directory
is **not** shrunk. A directory only shrinks when it is removed.

### 11.9 `readdir` cookies

The offset used by `readdir` to resume iteration is defined as:

```
cookie = block_index * 30 + slot_index + 1
```

Cookie 0 means "start". Cookies are stable across the lifetime of the directory
as long as no block is added, which is sufficient for POSIX `readdir` semantics
and is what the FUSE binding relies on.

---

## 12. Special files

### 12.1 Symbolic links

See §9.4. A link whose target is 72 bytes or shorter stores the target bytes at
inode offset 0x20 with no NUL terminator. Longer targets are stored as ordinary
file data. `i_size` is the target length in both cases.

Symbolic link resolution is the responsibility of the caller (the VFS or the
FUSE binding), not of NVFS. NVFS imposes no limit on resolution depth and
performs no resolution itself.

### 12.2 Device nodes and FIFOs

Character and block device inodes store their device number in `i_rdev` and have
no data blocks. The encoding of `i_rdev` is `(major << 16) | minor`, both 16-bit.

Version 3.0 defines these types so that the format does not have to change when
the noVa64 OS grows a device namespace, but no NVFS implementation is required to
do anything with them beyond storing and returning them. See **OI-9**.

### 12.3 Sockets

`S_IFSOCK` (0xC000) is **not** a valid NVFS type. An inode with that type nibble
is invalid.

---

## 13. The journal

### 13.1 Model

NVFS uses a **physical, redo-only, metadata-only, circular journal**. Every
metadata block that a transaction modifies is written in full to the journal
before it is written to its home location.

Three properties follow, and they are the entire reason for the design:

1. **Replay is idempotent.** Replay copies whole blocks to fixed destinations.
   Running it twice produces the same volume as running it once, so a crash
   *during* recovery is harmless.
2. **Replay is order-only.** There is no undo, no dependency graph, no
   compensating record. Recovery is a forward scan and a sequence of block
   copies — a few hundred bytes of 65816 code.
3. **A transaction needs one block of RAM.** Metadata blocks are streamed into
   the journal as they are modified (§13.6), not buffered. If the same block is
   modified twice in one transaction it is simply logged twice; replay applies
   entries in order, so the later value wins.

File **data** is never journaled. See §14.1 for the ordering rule that replaces
it.

### 13.2 Journal superblock

Located at `s_journal_start`. The ring occupies the following
`s_journal_blocks - 1` blocks; ring index *i* is block
`s_journal_start + 1 + i`.

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x000 | 4 | `js_magic` | bytes `'N' 'V' 'J' 'S'` |
| 0x004 | 4 | `js_ring_blocks` | MUST equal `s_journal_blocks - 1` |
| 0x008 | 4 | `js_checkpoint` | ring index of the oldest transaction that may need replay |
| 0x00C | 4 | `js_seq` | sequence number expected at `js_checkpoint` |
| 0x010 | 4 | `js_flags` | reserved, zero |
| 0x014 | — | reserved | zero through 0x7FB |
| 0x7FC | 4 | `js_crc32` | §3.4 |

`js_checkpoint` and `js_seq` together define where recovery starts. They are
allowed to be stale — pointing further back than strictly necessary — because
replaying an already-checkpointed transaction is harmless. They MUST NOT be
allowed to point *forward* of a transaction that has not been checkpointed.

### 13.3 Descriptor block

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x000 | 4 | `jd_magic` | bytes `'N' 'V' 'J' 'D'` |
| 0x004 | 4 | `jd_seq` | transaction sequence number |
| 0x008 | 2 | `jd_count` | number of data blocks following, 1..507 |
| 0x00A | 2 | `jd_flags` | reserved, zero |
| 0x00C | 4 | reserved | zero |
| 0x010 | 2028 | `jd_dest[507]` | `u32` destination block per logged block; entries beyond `jd_count` MUST be zero |
| 0x7FC | 4 | `jd_crc32` | §3.4 |

### 13.4 Commit block

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x000 | 4 | `jc_magic` | bytes `'N' 'V' 'J' 'C'` |
| 0x004 | 4 | `jc_seq` | MUST equal the descriptor's `jd_seq` |
| 0x008 | 4 | `jc_payload_crc32` | CRC-32 over the descriptor block followed by all `jd_count` logged blocks, as stored, 2048 × (`jd_count` + 1) bytes |
| 0x00C | 4 | `jc_time` | |
| 0x010 | — | reserved | zero through 0x7FB |
| 0x7FC | 4 | `jc_crc32` | §3.4 |

A transaction occupies `jd_count + 2` consecutive ring slots, modulo the ring
size. A transaction MAY wrap the end of the ring.

### 13.5 There is no escape mechanism

ext3 needs to escape logged blocks whose first four bytes collide with a journal
magic. NVFS does not, because recovery never has to guess where a block boundary
falls: the descriptor states `jd_count`, so recovery reads exactly that many
blocks and then expects a commit block at a computed position. It never
*interprets* a logged block. The scan for the next descriptor resumes at a
position derived arithmetically, never by searching for a magic.

This is why `jd_count` is validated against 507 before it is used, and why a
descriptor with an out-of-range count terminates recovery rather than being
skipped.

### 13.6 Commit procedure

For a transaction that modifies metadata blocks M₀…M_{n−1} with home
destinations P₀…P_{n−1}:

```
 1. Reserve ring index D for the descriptor.
 2. For i in 0..n-1:
       write M_i to ring[(D + 1 + i) mod R]
       record P_i in dest[i]
 3. Ensure every file data block written by this transaction is durable.   (§14.1)
 4. Write the descriptor to ring[D] with jd_count = n, jd_seq = seq.
 5. Barrier.
 6. Write the commit block to ring[(D + 1 + n) mod R] with jc_payload_crc32.
 7. Barrier.                       <-- the transaction is durable at this point
 8. Checkpoint: write each M_i to P_i.
 9. Barrier.
10. Set js_checkpoint = (D + n + 2) mod R, js_seq = seq + 1; write the
    journal superblock. Barrier.
```

Steps 8–10 MAY be deferred and batched across several transactions. Steps 1–7
MUST NOT be reordered.

"Barrier" means: every write issued before it is durable on the medium before
any write issued after it. On SD this means waiting for the card to leave the
busy state and confirming with CMD13. See **OI-10** for the limits of that
guarantee.

### 13.7 Recovery procedure

```
 1. Read and validate the journal superblock (magic, CRC). On failure, mount
    fails: recovery requires nvfsck.
 2. idx = js_checkpoint;  seq = js_seq
 3. Loop:
      a. Read ring[idx]. If jd_magic wrong, or jd_crc32 wrong, or
         jd_seq != seq, or jd_count is 0 or > 507  ->  stop.
      b. n = jd_count.
      c. Read ring[(idx + 1 + n) mod R]. If jc_magic wrong, or jc_crc32 wrong,
         or jc_seq != seq  ->  stop.
      d. Compute the CRC-32 over ring[idx] followed by ring[idx+1..idx+n].
         If it differs from jc_payload_crc32  ->  stop.
      e. For i in 0..n-1, in ascending i:
            copy ring[(idx + 1 + i) mod R] to jd_dest[i]
         Validate each jd_dest[i] first: it MUST be < s_total_blocks and MUST
         NOT lie inside the journal region. A violation aborts recovery and
         fails the mount.
      f. idx = (idx + n + 2) mod R;  seq = seq + 1
 4. Barrier.
 5. Write js_checkpoint = idx, js_seq = seq. Barrier.
 6. Re-read the primary superblock (recovery may have replaced it).
```

Step (e) MUST apply entries in ascending `i`, because a block logged twice in one
transaction relies on the later copy winning.

Step 5 MUST NOT be performed before step 4 completes. Advancing the checkpoint
over a transaction whose replayed blocks are not yet durable is the one way to
lose data in this design.

### 13.8 Block reuse constraint

**A block that appears as a `jd_dest` entry in a transaction that has not yet
been checkpointed MUST NOT be reallocated for file data.**

Without this rule, a replay could write stale metadata over freshly written file
data. ext3 solves the same problem with revoke records; NVFS solves it by not
reusing freed blocks until the freeing transaction is checkpointed. Revoke
records were considered and rejected: they add a second record type, a second
scan pass, and a hash table to recovery — all on a CPU where recovery must stay
under a few hundred bytes of code.

The practical implementation is a small in-memory set of "recently freed,
not yet reusable" blocks, cleared at every checkpoint. Because transactions are
short and checkpointing is frequent, the set is small; an implementation MAY
simply refuse to reuse *any* freed block until the next checkpoint, at the cost
of some transient free-space under-reporting.

### 13.9 Journal exhaustion

If a transaction cannot fit in the free portion of the ring, the implementation
MUST checkpoint outstanding transactions and advance `js_checkpoint` until it
fits. A single transaction larger than the ring is a bug: with a minimum ring of
256 blocks and a maximum transaction of 507 blocks, an implementation MUST cap
its own transaction size at `min(507, ring_size / 2)`.

The 65816 driver SHOULD cap transactions at **64 blocks**, which bounds the
in-RAM destination array to 256 bytes (§20.2).

---

## 14. Crash consistency

### 14.1 The one data ordering rule

File data is not journaled. Therefore:

> **Every file data block written by a transaction MUST be durable before that
> transaction's commit block is written.**

This is `data=ordered` semantics. It guarantees that a file's metadata never
references a block whose contents are stale — that is, a crash can never expose
the previous occupant of a reallocated block through a newly written file.

`data=writeback` (allowing data to lag the commit) would be faster and is
*not* permitted, because on a single-user machine with no other protection the
stale-data exposure is both a correctness and a privacy failure. See **OI-11**.

### 14.2 Transaction atomicity requirements

The following groups MUST each be a single transaction. This list is normative
and is the checklist `nvfsck` and the crash-injection harness test against.

| Operation | Blocks that must commit together |
|---|---|
| Block allocation | bitmap block, summary block, superblock counters, and the metadata block that now references the block |
| Block free | bitmap block, summary block, superblock counters, and the metadata block that no longer references it |
| Inode allocation | inode bitmap block, inode table block, superblock counters |
| Create | inode allocation group + directory block + directory inode (`i_size`, `i_mtime`) + any new directory block's allocation |
| Unlink | directory block + target inode (`i_links`) + orphan list insertion if `i_links` reaches 0 |
| Link | directory block + target inode (`i_links`, `i_ctime`) |
| Rename | source directory block + destination directory block + both directory inodes + replaced target inode if any |
| Write extending a file | inode + every modified extent node + allocation group for each new block |
| Truncate | inode + every modified extent node + free group for every released block |
| Directory grow | directory inode + new directory block + extent tree change + allocation group |

A `rename` that spans two directory blocks is the largest common transaction and
comfortably fits the 64-block cap.

### 14.3 What a crash can leave behind

With the journal in place, the complete list of post-crash states is:

1. **Transaction not committed.** Nothing of it is visible. Any data blocks it
   wrote are unreferenced and their bitmap bits are clear — no leak.
2. **Transaction committed, not checkpointed.** Recovery replays it. Fully
   visible.
3. **Transaction committed and checkpointed, `js_checkpoint` not advanced.**
   Recovery replays it again, idempotently. Fully visible.

There is no fourth state. In particular there is no state in which a directory
entry points at a free inode, or a bitmap bit disagrees with an extent, because
those pairs are never split across transactions.

The one thing a crash *can* leak is an **orphan**: an inode with `i_links` = 0
that was still open when power was lost. §15.7 handles it.

### 14.4 `fsync` and `NV_FL_SYNC`

`fsync(fd)` MUST: flush the file's dirty data blocks, then commit the transaction
containing the file's metadata (steps 1–7 of §13.6), then return. It need not
checkpoint.

`fsync` on a directory MUST commit the transaction containing that directory's
modifications. This is what makes "create a file, fsync the directory" durable,
and the FUSE binding and the VFS MUST expose it.

`NV_FL_SYNC` on an inode makes every mutating operation on it behave as if
followed by `fsync`.

---

## 15. Operation semantics

Only behaviour that is not obvious from POSIX is stated. Where this
specification is silent, POSIX applies.

### 15.1 `lookup`

§11.6. Returns inode number and generation. A lookup of `.` or `..` is answered
without touching the directory (§11.5).

### 15.2 `create`

1. Verify the name does not already exist.
2. Allocate an inode; initialise every field; set `i_links` = 1,
   `i_generation` from `s_next_generation`.
3. Insert a directory slot.
4. Update the directory inode's `i_mtime` and `i_ctime`.

All in one transaction. If the directory has no free slot, the directory-grow
work joins the same transaction.

### 15.3 `mkdir`

As `create`, with `i_mode` type 0x4, `i_links` = 2 (§11.5), and one directory
block allocated and initialised (header written, all 30 slots zeroed). The
parent's `i_links` is **not** incremented.

### 15.4 `rmdir`

Permitted only if the directory contains no occupied slot in any block. Frees all
directory blocks, frees the inode, removes the parent's slot — one transaction.

### 15.5 `rename`

`rename(olddir, oldname, newdir, newname)`:

1. If `newname` exists, it MUST be of a compatible type (directory over
   directory only, and the target directory MUST be empty).
2. Insert the new slot, remove the old slot, and decrement the replaced inode's
   `i_links` — one transaction. Because it is one transaction, the classic
   "rename lost the file" failure mode does not exist.
3. If a directory moved between parents, nothing needs adjusting: `..` is not
   stored (§11.5) and parent link counts are not maintained.
4. If the replaced inode reaches `i_links` = 0 and is open, it joins the orphan
   list in the same transaction.

An implementation MUST reject a rename that would make a directory its own
ancestor. The check is a walk from `newdir` toward the root comparing against the
moved inode; the caller (VFS or FUSE binding) holds the path and MUST perform it.
NVFS itself cannot perform this check, because it does not store parent pointers.
This is a consequence of §11.5 and is recorded as **OI-8**.

### 15.6 `unlink`

Remove the slot, decrement `i_links`. If `i_links` reaches 0 and no handle is
open, free the inode and all its blocks in the same transaction. If a handle is
open, put the inode on the orphan list instead.

### 15.7 The orphan list

`s_orphan_head` is the inode number at the head of a singly linked list threaded
through `i_orphan_next`. An inode is on the list when its link count has reached
zero but it is still open.

- Insertion happens in the same transaction as the final `unlink`.
- Removal (and the actual freeing of blocks) happens when the last handle closes,
  also in one transaction.
- **At mount, after journal recovery, an implementation MUST walk the orphan list
  and free every inode on it, then clear `s_orphan_head`.** This is the mechanism
  that prevents crashes from leaking space, and it is why NVFS does not need an
  on-target `fsck` for the common case.

The orphan walk MUST bound itself: it MUST stop after `s_total_inodes` steps and
MUST reject an inode number outside 8..`s_total_inodes`. A cycle in the list is a
corruption and MUST fail the mount rather than loop.

### 15.8 `read`

Reading a hole or an unwritten extent returns zero bytes. Reading beyond `i_size`
returns a short count. `i_atime` is not updated (§9.5).

### 15.9 `write`

Writing beyond `i_size` extends the file. Writing into a hole allocates. A
partial first or last block requires read-modify-write. `i_mtime` and `i_ctime`
are updated.

### 15.10 `statfs`

Reports from `s_free_blocks` and `s_free_inodes` (§6.1). Block size is 2048;
fragment size is 2048.

---

## 16. Feature flags

Three 32-bit masks in the superblock control forward compatibility. The
acceptance policy is normative and MUST be enforced at mount, before any other
validation.

| Mask | Unknown bit set → |
|---|---|
| `s_feature_incompat` | **Refuse to mount.** Report the offending bit number. |
| `s_feature_ro_compat` | **Mount read-only.** Report the offending bit number. |
| `s_feature_compat` | **Ignore.** Mount normally. |

An implementation MUST report *which* bit it did not recognise. A mount failure
that says only "unsupported features" is not acceptable; it is the single most
useful diagnostic a file system can emit.

### 16.1 Defined bits

**`s_feature_incompat`**

| Bit | Name | Meaning |
|---|---|---|
| 0x00000001 | `NV_INCOMPAT_DIRHASH` | Directories may use the bucket layout of §19 |
| all others | — | reserved, MUST be zero |

**`s_feature_ro_compat`**

| Bit | Name | Meaning |
|---|---|---|
| 0x00000001 | `NV_ROCOMPAT_METACRC` | Metadata checksums are present and MUST be maintained on write. **Always set in version 3.0.** |
| 0x00000002 | `NV_ROCOMPAT_JOURNAL` | A journal is present and MUST be used for writes. **Always set in version 3.0.** |
| all others | — | reserved, MUST be zero |

**`s_feature_compat`**

| Bit | Name | Meaning |
|---|---|---|
| all | — | reserved, MUST be zero |

Both defined `ro_compat` bits are always set by `mkfs.nvfs`. Placing them in the
read-only mask rather than the incompatible mask is deliberate: a minimal
read-only implementation (a boot loader, for instance) that neither verifies
checksums nor replays the journal can still mount and read a *cleanly unmounted*
volume. A read-only mounter that encounters `s_state != 0` or a non-empty journal
MUST refuse.

---

## 17. Mount and unmount

### 17.1 Mount

```
 1. Read the partition table; locate the NVFS partition (type 0x7F).
 2. Read block 1. Verify s_magic, s_crc32.
 3. Verify s_version_major == 3 and s_block_size == 2048.
 4. Apply the feature flag policy of §16.
 5. Sanity-check the layout: every region start/length must be in range, the
    regions must be ordered as in §5, and none may overlap.
 6. Read the journal superblock. Run recovery (§13.7).
 7. If mounting read-write:
       a. Walk and free the orphan list (§15.7).
       b. Set s_state = 1, increment s_mount_count, set s_mount_time.
          Commit.
 8. Read the root inode (number 1). Verify it is a directory with i_links >= 2.
```

If `s_state` is 2 (error) the implementation MUST mount read-only and report that
`nvfsck` is required. If `s_max_mount_count` is non-zero and `s_mount_count`
exceeds it, the implementation SHOULD warn but MUST still mount.

### 17.2 Unmount

```
 1. Commit any open transaction.
 2. Checkpoint every committed transaction; advance js_checkpoint past all of
    them. The journal is now logically empty.
 3. Set s_state = 0, s_write_time. Commit and checkpoint that too.
```

A cleanly unmounted volume has an empty journal and `s_state` = 0. This is the
state a read-only mounter requires (§16.1).

### 17.3 Error handling

On detecting corruption during operation, the implementation acts per
`s_error_action`. In all cases it MUST set `s_state` = 2 and attempt to persist
that, so the next mount knows.

---

## 18. `nvfsck` obligations

`nvfsck` is a host-side tool. There is no on-target `fsck` and none is planned.

A single pass MUST verify, at minimum:

1. Superblock magic, CRC, version, and internal layout consistency; agreement
   with the backup superblock on immutable layout fields.
2. Journal superblock validity; journal emptiness (a non-empty journal means
   recovery was never run — `nvfsck` MUST replay it first, or refuse).
3. Every allocated inode's `i_crc32`, type validity, and flag validity.
4. Every extent tree: node magic, CRC, `xn_inode` back-reference, `xn_depth`
   consistency, sorted non-overlapping entries, `ex_pblk` in range and outside
   metadata regions, depth ≤ 3.
5. A reconstructed block bitmap compared against the stored one. Blocks marked
   allocated but referenced by nothing are leaks (correctable). Blocks
   referenced by more than one inode are cross-links (serious). Blocks
   referenced but marked free are the fatal case.
6. A reconstructed inode bitmap and link count per inode, compared against the
   stored ones.
7. Every directory block: magic, CRC, `db_inode` back-reference, `db_used`
   agreement with occupied slots, `de_namelen` in range, no embedded `/` or NUL,
   `de_hash` agreement, `de_type` agreement with the target inode, no duplicate
   names.
8. Full connectivity from the root: every allocated inode reachable, no cycles
   among directories, no directory with more than one parent.
9. The orphan list: bounded, in range, every member has `i_links` = 0.
10. `s_free_blocks` and `s_free_inodes` against the reconstructed bitmaps.

Repairs 5, 6 and 10 are silent and automatic. Everything else requires
confirmation.

---

## 19. Optional feature: DIRHASH

`NV_INCOMPAT_DIRHASH` is defined here so that it can be added later without a
format change. **Version 3.0 implementations are not required to support it, and
`mkfs.nvfs` MUST NOT set the bit by default.**

When the feature is enabled and a directory inode has `i_dirhash_order` > 0, the
directory's blocks are organised as 2^`i_dirhash_order` buckets:

- Logical block *b* is bucket *b* for *b* < 2^order. Blocks at or beyond
  2^order are overflow blocks, reached only through `db_next`.
- A name's bucket is `(crc32(name) >> 16) & (2^order - 1)`. Note that this uses
  the *upper* 16 bits of the same CRC whose lower 16 bits are `de_hash`, so a
  single CRC pass serves both.
- Lookup reads bucket block, then follows `db_next` while it is non-zero.
- Insertion goes into the bucket block, or a new overflow block chained to it.
- When the average chain length exceeds 2, the directory is **rebuilt**: order
  is incremented, a new block array is allocated, every entry is rehashed into
  it, and the old blocks are freed — all in one transaction (or a bounded
  sequence of transactions, each of which leaves the directory consistent).

Incremental bucket splitting is deliberately not specified. A bulk rebuild is
easier to make crash-safe and easier to verify, and directories large enough to
need it are rare on this machine.

A directory with `i_dirhash_order` = 0 is a plain linear directory even on a
DIRHASH volume, so the two layouts coexist.

---

## 20. 65816 implementation notes

Informative, but the RAM budget and the register-map requirement are binding
constraints on the rest of the project.

### 20.1 Why every constant is a power of two

Restated from §4.1 because it is the single most important implementation
property: block size 2048, inode size 128, 16384 bits per bitmap block, 16 inodes
per table block, 30 slots per directory block. The first four are powers of two,
so every index conversion is a shift and a mask. The 65816 has no divide
instruction and a 16-bit ALU; a division by a non-power-of-two in the allocator
or the inode path would be a subroutine call in the hottest loop in the driver.

(30 slots per directory block is not a power of two, but it appears only in the
`readdir` cookie calculation (§11.9), which is not a hot path and multiplies
rather than divides.)

### 20.2 RAM budget

Steady-state working set for a single-mount, single-open-file driver:

| Buffer | Size | Purpose |
|---|---|---|
| Metadata block buffer | 2048 | superblock, bitmap, inode table, directory, extent node — one at a time |
| Data block buffer | 2048 | file data read-modify-write |
| Journal destination array | 256 | 64 × `u32`, bounding transactions to 64 blocks (§13.9) |
| Inode working copy | 128 | |
| Cached superblock fields | 64 | layout constants only; the full 2048-byte block is not held |
| Name comparison buffer | 56 | |
| Path component scratch | 64 | |
| **Total** | **≈ 4.7 KiB** | |

This fits comfortably in the kernel's bank `$01` allocation. A second data buffer
would double sequential throughput and costs 2 KiB; that is an implementation
choice, not a format requirement.

**Bank boundary rule:** the 2048-byte buffers MUST NOT straddle a 64 KiB bank
boundary. Aligning each to a 2048-byte boundary satisfies this automatically and
also makes the SD DMA destination address trivially computable. The same rule was
stated for ext2 in DN-FS-EXT2-001 and applies unchanged here.

### 20.3 Cost model

Extrapolated from the figure in DN-FS-EXT2-001 of roughly 8–10 K cycles per KiB
at 8 MHz. **These are budget targets, not measurements. They must be measured on
real hardware before this specification is frozen.**

| | Blocks transferred | Estimated time |
|---|---|---|
| One block transfer | 1 | ≈ 2.5 ms |
| Clean mount | 3 | ≈ 8 ms |
| Path lookup, 2 components, small directories | 4 | ≈ 10 ms |
| Demand-page one 2 KiB page of an already-open executable | 1 | ≈ 2.5 ms |
| Create a file in a directory with a free slot | ≈ 13 | ≈ 33 ms |
| Sequential write of 64 KiB into a contiguous extent | ≈ 44 | ≈ 110 ms (≈ 580 KiB/s) |

Two observations follow.

**The journal roughly doubles metadata write cost.** For a metadata-heavy
workload (unpacking an archive) that is a real slowdown. For bulk data it is
negligible, because data is not journaled. Transaction batching — grouping
several operations into one commit — recovers most of it and SHOULD be
implemented with a commit interval of about one second, plus an immediate commit
on `fsync` or `NV_FL_SYNC`.

**Demand paging costs exactly one block transfer**, because a file system block
is exactly a page. This is the payoff for fixing the block size at 2048 and is
worth more than any other optimisation in this document.

### 20.4 What this specification needs from the Helium SD register map

The SD block register map is not yet frozen and is on the critical path for
schematic capture. This specification's requirement is short and specific:

> **The natural transfer unit is 4 consecutive 512-byte sectors (2048 bytes) into
> a 2048-byte-aligned buffer.** The register map SHOULD support issuing a
> multi-block read or write of 4 sectors as a single command with a burst
> transfer to SDRAM, rather than four separate single-sector transfers.

This is the same request recorded as **OI-5** in DN-FS-EXT2-001, now with a
concrete number attached. Four single-sector transfers with per-command overhead
is measurably worse than one four-sector burst, and the difference multiplies
through every figure in §20.3. It is much cheaper to design in now than to add
later.

### 20.5 Calypsi notes

- `int` is 16 bits. Every on-disk field and every loop counter that can exceed
  65535 MUST use an explicit `stdint.h` type. `i_size`, block numbers and inode
  numbers are `uint32_t` and their arithmetic is two 16-bit operations.
- The large memory model gives 24-bit pointers and `JSL`/`RTL` cross-bank calls.
  Buffers live in the kernel bank; the driver takes far pointers.
- No byte swapping is required in either direction (§3.2).
- `_Static_assert` on every structure size MUST be present in both the host and
  the Calypsi build (§21.5). A structure that silently gains padding on one of
  the two targets is exactly the class of bug that produces a volume readable by
  one implementation and not the other.

---

## 21. C definitions

### 21.1 Constants

```c
#define NV_BLOCK_SIZE        2048u
#define NV_SECTORS_PER_BLOCK    4u
#define NV_INODE_SIZE         128u
#define NV_INODES_PER_BLOCK    16u
#define NV_DIRENT_SIZE         64u
#define NV_DIRENTS_PER_BLOCK   30u
#define NV_NAME_MAX            56u
#define NV_BITS_PER_BITMAP_BLOCK 16384u
#define NV_EXT_PER_INODE        6u
#define NV_EXT_PER_NODE       169u
#define NV_MAX_EXT_DEPTH        3u
#define NV_MAX_JOURNAL_BLOCKS 507u
#define NV_ROOT_INO             1u
#define NV_FIRST_FREE_INO       8u
#define NV_CRC_OFFSET       0x7FCu
```

### 21.2 On-disk structures

Every structure below is laid out so that **no compiler needs to insert
padding**: all fields are naturally aligned and every structure size is a
multiple of its strictest alignment. Do not add packing pragmas; add the static
assertions in §21.5 instead, which catch the problem rather than hiding it.

```c
#include <stdint.h>

typedef struct {                 /* 12 bytes — leaf */
    uint32_t ex_lblk;
    uint32_t ex_pblk;
    uint16_t ex_len;
    uint16_t ex_flags;
} nv_extent_t;

typedef struct {                 /* 12 bytes — index; same footprint */
    uint32_t ix_lblk;
    uint32_t ix_pblk;
    uint16_t ix_rsv0;
    uint16_t ix_rsv1;
} nv_extidx_t;

typedef struct {                 /* 128 bytes */
    uint16_t    i_mode;
    uint16_t    i_links;
    uint32_t    i_size;
    uint32_t    i_blocks;
    uint32_t    i_mtime;
    uint32_t    i_ctime;
    uint32_t    i_atime;
    uint32_t    i_generation;
    uint16_t    i_flags;
    uint8_t     i_ext_count;
    uint8_t     i_ext_depth;
    nv_extent_t i_ext[NV_EXT_PER_INODE];   /* 72 bytes */
    uint16_t    i_uid;
    uint16_t    i_gid;
    uint32_t    i_rdev;
    uint32_t    i_orphan_next;
    uint8_t     i_dirhash_order;
    uint8_t     i_rsv0[3];
    uint32_t    i_rsv1;
    uint32_t    i_crc32;
} nv_inode_t;

typedef struct {                 /* 64 bytes */
    uint32_t de_inode;
    uint16_t de_hash;
    uint8_t  de_namelen;
    uint8_t  de_type;
    uint8_t  de_name[NV_NAME_MAX];
} nv_dirent_t;

typedef struct {                 /* 64-byte header, then 30 slots */
    uint8_t  db_magic[4];        /* 'N','V','D','B' */
    uint32_t db_inode;
    uint16_t db_used;
    uint16_t db_flags;
    uint32_t db_next;
    uint8_t  db_rsv[48];
} nv_dirblk_hdr_t;

typedef struct {                 /* 16-byte header, then 169 entries */
    uint8_t  xn_magic[4];        /* 'N','V','X','N' */
    uint16_t xn_entries;
    uint16_t xn_depth;
    uint32_t xn_inode;
    uint32_t xn_rsv;
} nv_extnode_hdr_t;

typedef struct {                 /* superblock header; block is 2048 */
    uint8_t  s_magic[4];         /* 'N','V','F','S' */
    uint16_t s_version_major;
    uint16_t s_version_minor;
    uint32_t s_block_size;
    uint32_t s_total_blocks;
    uint32_t s_free_blocks;
    uint32_t s_total_inodes;
    uint32_t s_free_inodes;
    uint32_t s_first_data_block;
    uint32_t s_bitmap_start;
    uint32_t s_bitmap_blocks;
    uint32_t s_summary_start;
    uint32_t s_summary_blocks;
    uint32_t s_ibitmap_start;
    uint32_t s_ibitmap_blocks;
    uint32_t s_itable_start;
    uint32_t s_itable_blocks;
    uint32_t s_journal_start;
    uint32_t s_journal_blocks;
    uint32_t s_root_inode;
    uint32_t s_backup_sb;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    uint8_t  s_label[32];
    uint32_t s_mkfs_time;
    uint32_t s_mount_time;
    uint32_t s_write_time;
    uint32_t s_mount_count;
    uint32_t s_max_mount_count;
    uint16_t s_state;
    uint16_t s_error_action;
    uint32_t s_last_check;
    uint32_t s_next_generation;
    uint32_t s_alloc_hint;
    uint32_t s_orphan_head;
    uint32_t s_partition_lba;
} nv_super_t;                    /* 0xB8 = 184 bytes of defined fields */

typedef struct {
    uint8_t  js_magic[4];        /* 'N','V','J','S' */
    uint32_t js_ring_blocks;
    uint32_t js_checkpoint;
    uint32_t js_seq;
    uint32_t js_flags;
} nv_jsuper_t;

typedef struct {                 /* 16-byte header, then 507 u32 */
    uint8_t  jd_magic[4];        /* 'N','V','J','D' */
    uint32_t jd_seq;
    uint16_t jd_count;
    uint16_t jd_flags;
    uint32_t jd_rsv;
} nv_jdesc_hdr_t;

typedef struct {
    uint8_t  jc_magic[4];        /* 'N','V','J','C' */
    uint32_t jc_seq;
    uint32_t jc_payload_crc32;
    uint32_t jc_time;
} nv_jcommit_hdr_t;
```

### 21.3 Mode and type constants

```c
#define NV_S_IFMT   0xF000u
#define NV_S_IFIFO  0x1000u
#define NV_S_IFCHR  0x2000u
#define NV_S_IFDIR  0x4000u
#define NV_S_IFBLK  0x6000u
#define NV_S_IFREG  0x8000u
#define NV_S_IFLNK  0xA000u

#define NV_DT_UNKNOWN  0u
#define NV_DT_FIFO     1u
#define NV_DT_CHR      2u
#define NV_DT_DIR      4u
#define NV_DT_BLK      6u
#define NV_DT_REG      8u
#define NV_DT_LNK     10u

#define NV_FL_IMMUTABLE 0x0001u
#define NV_FL_APPEND    0x0002u
#define NV_FL_SYNC      0x0004u

#define NV_EX_UNWRITTEN 0x0001u

#define NV_INCOMPAT_DIRHASH   0x00000001ul
#define NV_ROCOMPAT_METACRC   0x00000001ul
#define NV_ROCOMPAT_JOURNAL   0x00000002ul
```

### 21.4 Index macros

```c
#define nv_lba_of_block(sb,b)     ((sb)->s_partition_lba + ((uint32_t)(b) << 2))
#define nv_bitmap_blk(sb,b)       ((sb)->s_bitmap_start  + ((uint32_t)(b) >> 14))
#define nv_bitmap_byte(b)         ((uint16_t)(((b) >> 3) & 0x7FFu))
#define nv_bitmap_bit(b)          ((uint8_t)((b) & 7u))
#define nv_summary_blk(sb,i)      ((sb)->s_summary_start + ((uint32_t)(i) >> 10))
#define nv_summary_off(i)         ((uint16_t)(((i) & 0x3FFu) << 1))
#define nv_itable_blk(sb,ino)     ((sb)->s_itable_start  + (((ino) - 1u) >> 4))
#define nv_itable_off(ino)        ((uint16_t)((((ino) - 1u) & 15u) << 7))
#define nv_ibitmap_blk(sb,ino)    ((sb)->s_ibitmap_start + (((ino) - 1u) >> 14))
```

### 21.5 Mandatory static assertions

These MUST appear in both the host build and the Calypsi build. A build that
omits them does not conform.

```c
_Static_assert(sizeof(nv_extent_t)      ==  12, "nv_extent_t layout");
_Static_assert(sizeof(nv_extidx_t)      ==  12, "nv_extidx_t layout");
_Static_assert(sizeof(nv_inode_t)       == 128, "nv_inode_t layout");
_Static_assert(sizeof(nv_dirent_t)      ==  64, "nv_dirent_t layout");
_Static_assert(sizeof(nv_dirblk_hdr_t)  ==  64, "nv_dirblk_hdr_t layout");
_Static_assert(sizeof(nv_extnode_hdr_t) ==  16, "nv_extnode_hdr_t layout");
_Static_assert(sizeof(nv_jsuper_t)      ==  20, "nv_jsuper_t layout");
_Static_assert(sizeof(nv_jdesc_hdr_t)   ==  16, "nv_jdesc_hdr_t layout");
_Static_assert(sizeof(nv_jcommit_hdr_t) ==  16, "nv_jcommit_hdr_t layout");
_Static_assert(sizeof(nv_super_t)       == 184, "nv_super_t layout");

_Static_assert(offsetof(nv_inode_t, i_ext)   == 0x20, "i_ext offset");
_Static_assert(offsetof(nv_inode_t, i_uid)   == 0x68, "i_uid offset");
_Static_assert(offsetof(nv_inode_t, i_crc32) == 0x7C, "i_crc32 offset");

_Static_assert(64u + NV_DIRENTS_PER_BLOCK * NV_DIRENT_SIZE <= NV_CRC_OFFSET,
               "directory slots overrun the block CRC");
_Static_assert(16u + NV_EXT_PER_NODE * 12u == NV_CRC_OFFSET,
               "extent node entry count");
_Static_assert(16u + NV_MAX_JOURNAL_BLOCKS * 4u == NV_CRC_OFFSET,
               "journal descriptor entry count");
```

### 21.6 Provisional `fsops` surface

**Provisional.** DN-FS-VFS-001 is the authoritative document for this and has not
been written. It is sketched here only so that `libnvfs` is not shaped in a way
that has to be undone. Nothing in §3–§19 depends on it.

```c
typedef struct {
    int (*mount)   (void *fs, nv_bdev_t *dev, uint32_t flags);
    int (*unmount) (void *fs);
    int (*statfs)  (void *fs, fs_statfs_t *out);
    int (*getattr) (void *fs, uint32_t ino, fs_attr_t *out);
    int (*setattr) (void *fs, uint32_t ino, const fs_attr_t *in, uint32_t mask);
    int (*lookup)  (void *fs, uint32_t dir, const char *name, uint8_t nlen,
                    uint32_t *ino);
    int (*readdir) (void *fs, uint32_t dir, uint32_t cookie,
                    fs_dirent_cb_t cb, void *ctx);
    int (*read)    (void *fs, uint32_t ino, uint32_t off, uint16_t len,
                    void *buf, uint16_t *got);
    int (*write)   (void *fs, uint32_t ino, uint32_t off, uint16_t len,
                    const void *buf, uint16_t *put);
    int (*create)  (void *fs, uint32_t dir, const char *name, uint8_t nlen,
                    uint16_t mode, uint32_t *ino);
    int (*mkdir)   (void *fs, uint32_t dir, const char *name, uint8_t nlen,
                    uint16_t mode, uint32_t *ino);
    int (*unlink)  (void *fs, uint32_t dir, const char *name, uint8_t nlen);
    int (*rmdir)   (void *fs, uint32_t dir, const char *name, uint8_t nlen);
    int (*link)    (void *fs, uint32_t ino, uint32_t dir,
                    const char *name, uint8_t nlen);
    int (*rename)  (void *fs, uint32_t odir, const char *oname, uint8_t onlen,
                    uint32_t ndir, const char *nname, uint8_t nnlen);
    int (*truncate)(void *fs, uint32_t ino, uint32_t size);
    int (*readlink)(void *fs, uint32_t ino, char *buf, uint16_t bufsz);
    int (*symlink) (void *fs, uint32_t dir, const char *name, uint8_t nlen,
                    const char *target, uint32_t *ino);
    int (*fsync)   (void *fs, uint32_t ino, int datasync);
    int (*sync)    (void *fs);
} fsops_t;
```

Note `len` is `uint16_t`: with a 16-bit `int` under Calypsi, a `size_t`-shaped
API invites silent truncation. A single call transfers at most 65535 bytes and
the caller loops.

---

## 22. Partitioning and media

### 22.1 Partition type

An NVFS volume lives in an MBR primary partition of type **0x7F**. This value was
selected to avoid collision with the hidden-FAT conventions that cluster around
0x7A–0x7B. GPT is not supported.

The partition MUST begin at an LBA that is a multiple of 4, so that NVFS block
boundaries coincide with SD 4-sector boundaries. `mkfs.nvfs` MUST refuse an
unaligned partition rather than silently working around it, because working
around it would turn every block transfer into two.

### 22.2 Minimum volume size

The smallest sensible volume is one that satisfies §5.1 with a 256-block journal:
8 reserved + 257 journal + 1 bitmap + 1 summary + 1 inode bitmap + inode table +
some data. `mkfs.nvfs` MUST refuse volumes below **4096 blocks (8 MiB)**.

### 22.3 SD card characteristics

NVFS makes no attempt at wear levelling or erase-block alignment; SD cards
contain a flash translation layer that does both. NVFS does not issue ERASE or
any discard command. See **OI-10** for the durability assumption this places on
the card.

---

## 23. Conformance and test obligations

An implementation conforms when it satisfies all of the following. This section
defines the acceptance criteria for the draft 3 test suite.

### 23.1 Structural

1. All static assertions in §21.5 present and passing on both host and Calypsi
   builds.
2. Refuses `s_block_size != 2048`, `s_version_major != 3`, bad superblock magic,
   bad superblock CRC.
3. Implements the feature flag policy of §16 exactly, and names the offending
   bit in every rejection message.
4. Bounds every walk: extent depth ≤ 3, orphan list ≤ `s_total_inodes`,
   `jd_count` ≤ 507, directory blocks ≤ `i_size / 2048`.

### 23.2 Corruption corpus

For every metadata structure, a fuzzed image with a bad magic, a bad CRC, an
out-of-range block pointer, an out-of-range inode number, a cyclic pointer, and a
zero-length extent. Acceptance: **no crash, no hang, no unbounded allocation,
every failure a clean error**. This is the same bar set for ext2 in
DN-FS-EXT2-001 and it is the reason both file systems can share one harness.

### 23.3 Journal-specific

These are new obligations that draft 2 had no equivalent of, and they are where
the crash-injection harness earns its keep:

1. **Replay idempotence.** Replay a dirty image, snapshot it, replay again,
   compare. The images MUST be byte-identical.
2. **Interrupted replay.** Kill the process at every block boundary during
   replay; on restart, the final image MUST equal the uninterrupted result.
3. **Torn commit.** Truncate an image mid-commit-block. The transaction MUST be
   discarded, not partially applied.
4. **Torn payload.** Corrupt one byte of a logged block. `jc_payload_crc32` MUST
   catch it and the transaction MUST be discarded.
5. **Stale checkpoint.** Rewind `js_checkpoint` several transactions and replay.
   The result MUST equal the un-rewound result.
6. **Wrapped transaction.** A transaction that spans the end of the ring must
   commit and replay correctly.
7. **Block reuse violation detector.** A test that deliberately violates §13.8
   MUST produce a detectable corruption, proving the test can see the failure it
   is guarding against.

### 23.4 Crash injection

Power loss simulated at every write boundary of every operation in the §14.2
table. Acceptance: after recovery, `nvfsck` reports a clean volume in every case,
and the file system state matches one of the three outcomes in §14.3.

### 23.5 Cross-implementation agreement

Every image produced by `mkfs.nvfs` and mutated by `libnvfs` MUST be read
identically by the 65816 driver, and vice versa. The agreement harness compares
`getattr`, `lookup`, `readdir` order, `read` contents and `statfs` across
implementations for a generated corpus.

### 23.6 Sanitisers

Host implementations MUST run the full suite clean under ASan and UBSan, and
clean under Valgrind.

---

## 24. Constants quick reference

| | |
|---|---|
| Block size | 2048 bytes (4 SD sectors) |
| Inode size | 128 bytes (16 per block) |
| Directory slot | 64 bytes (30 per block) |
| Extent entry | 12 bytes (6 in inode, 169 per node) |
| Max name | 56 bytes |
| Max file | 4 GiB − 1 |
| Max volume | 8 TiB (2^32 − 1 blocks) |
| Max links | 65 535 |
| Max extent | 65 535 blocks (128 MiB) |
| Max extent depth | 3 |
| Max journal transaction | 507 blocks (65816 driver caps at 64) |
| Root inode | 1 |
| First allocatable inode | 8 |
| Superblock block | 1 (backup: last block) |
| Partition type | 0x7F |
| Checksum | CRC-32, poly 0xEDB88320, at block offset 0x7FC |
| Byte order | little-endian everywhere |

---

## 25. Migration from draft 2

There is no in-place migration and none will be written. The procedure is:

1. Mount the draft 2 volume with the existing draft 2 tooling.
2. Copy its contents out to the host.
3. `mkfs.nvfs` the media under draft 3.
4. Copy the contents back.

The draft 2 tooling MUST be preserved, tagged and archived before draft 3 work
begins, because step 1 depends on it and there will be no way to reconstruct it.
**This is the single action that must happen before any draft 3 code is written.**

Draft 3 detects a draft 2 volume and reports it specifically rather than as
generic corruption: block 1 will fail the magic check, and the implementation
SHOULD additionally check whether a draft 2 signature is present at whatever
offset draft 2 used and, if so, say so.

---

## 26. Open items

| ID | Item | Blocking |
|---|---|---|
| **OI-1** | Document ID. This note is numbered `DN-FS-NVFS-003` to avoid colliding with whatever ID draft 2 carried. If draft 2's ID is known, renumber consistently. | No |
| **OI-2** | **Accept the format break.** §0.2 lists what has to be rewritten. This is the decision that gates everything else in this document. | **Yes — everything** |
| **OI-3** | DN-FS-VFS-001 must be written before either `libnvfs` or the ext2 layer is restructured. §21.6 is a placeholder and will be wrong. | Implementation |
| **OI-4** | Helium SD register map: freeze it with a 4-sector (2048-byte) burst transfer as the primitive (§20.4). Same request as OI-5 in DN-FS-EXT2-001, now with a number. | **Yes — schematic capture** |
| **OI-5** | Journal sizing. The `total/64` default gives 1 MiB on a 64 MiB volume and 128 MiB on an 8 GiB one, which is probably too large. Consider capping at 4096 blocks. | mkfs only |
| **OI-6** | DIRHASH (§19): implement in 3.0, or ship linear-only and add later? Recommendation: ship linear-only. The bit is defined so the decision can be deferred at no cost. | No |
| **OI-7** | **Wall-clock source.** §3.5 requires timestamps, and nothing in the noVa64 hardware architecture as documented provides a real-time clock. Options: an RTC part (board change), the RP2040 holding time across a battery-backed domain, or the user setting the clock at boot. If none, every timestamp is zero and `nvfsck` must treat that as normal. **This is a hardware gap, not a file system question, and it should be resolved before the board is frozen.** | Board design |
| **OI-8** | `..` is not stored (§11.5) and directory link counts do not follow POSIX convention. Consequences: the FUSE binding must synthesise POSIX link counts, and the rename-into-own-descendant check must live in the caller (§15.5). Confirm this trade is acceptable, or add a `i_parent` field to the inode — there are 8 reserved bytes available. | Design |
| **OI-9** | Device nodes and FIFOs (§12.2) are defined but the noVa64 OS has no device namespace yet. Confirm they should stay in the format. | No |
| **OI-10** | **SD durability.** §13.6 assumes that a write is durable once the card leaves the busy state. SD cards have internal write caches and no FLUSH_CACHE command comparable to eMMC. If that assumption is wrong, the journal's ordering guarantees are weaker than stated. Needs investigation, and possibly a power-loss test rig. | Correctness |
| **OI-11** | `data=ordered` (§14.1) costs a barrier per transaction. Measure it before freezing; if it is intolerable, the alternative is not `data=writeback` but journaling small writes with the metadata. | Performance |
| **OI-12** | Calendar impact. Draft 3 plus rewritten tooling is not in the v3 99-step plan. Same class of open item as OI-1 in DN-FS-EXT2-001, and the two compound. | Planning |

---

## Appendix A — CRC-32

The one and only checksum in NVFS.

| | |
|---|---|
| Width | 32 bits |
| Polynomial | 0x04C11DB7 |
| Reflected polynomial | 0xEDB88320 |
| Initial value | 0xFFFFFFFF |
| Reflect input | yes |
| Reflect output | yes |
| Final XOR | 0xFFFFFFFF |
| Check value for the ASCII string `123456789` | **0xCBF43926** |

This is the ubiquitous CRC-32 of IEEE 802.3, zlib, PNG and gzip. It is specified
here so that the existing hand-written 65816 assembly implementation is reusable
without modification, and so that any host implementation can use `zlib`'s
`crc32()` directly.

Every implementation MUST verify the check value 0xCBF43926 in its test suite.

Reference:

```c
uint32_t nv_crc32(const void *buf, uint32_t len)
{
    const uint8_t *p = buf;
    uint32_t crc = 0xFFFFFFFFul;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320ul & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFFul;
}
```

**Block checksum helper.** To compute or verify a block CRC (§3.4): copy the
block, zero bytes 0x7FC..0x7FF in the copy, CRC the full 2048 bytes. An
implementation MAY avoid the copy by CRC-ing 0x000..0x7FB and then four zero
bytes, which is equivalent and is what the 65816 driver SHOULD do.

---

## Appendix B — Magic number registry

| Magic | ASCII | Structure |
|---|---|---|
| 0x4E 0x56 0x46 0x53 | `NVFS` | Superblock (§6) |
| 0x4E 0x56 0x4A 0x53 | `NVJS` | Journal superblock (§13.2) |
| 0x4E 0x56 0x4A 0x44 | `NVJD` | Journal descriptor block (§13.3) |
| 0x4E 0x56 0x4A 0x43 | `NVJC` | Journal commit block (§13.4) |
| 0x4E 0x56 0x44 0x42 | `NVDB` | Directory block (§11.1) |
| 0x4E 0x56 0x58 0x4E | `NVXN` | Extent node (§10.3) |

The prefix `NV` is reserved for NVFS at offset 0 of any metadata block. Bitmap
blocks, summary blocks, inode table blocks and file data blocks carry no magic.

---

## Appendix C — Worked layout example

A 64 MiB volume: 32768 blocks.

```
journal_ring     = clamp(32768/64, 256, 8192) = 512
s_journal_blocks = 513
inode_count      = 32768/8                    = 4096
s_bitmap_blocks  = ceil(32768/16384)          = 2
s_summary_blocks = ceil(2/1024)               = 1
s_ibitmap_blocks = ceil(4096/16384)           = 1
s_itable_blocks  = ceil(4096/16)              = 256
```

| Blocks | Count | Region |
|---|---|---|
| 0 | 1 | Boot / reserved |
| 1 | 1 | **Primary superblock** |
| 2 – 7 | 6 | Reserved |
| 8 | 1 | Journal superblock (`s_journal_start` = 8) |
| 9 – 520 | 512 | Journal ring |
| 521 – 522 | 2 | Block bitmap (`s_bitmap_start` = 521) |
| 523 | 1 | Summary (`s_summary_start` = 523) |
| 524 | 1 | Inode bitmap (`s_ibitmap_start` = 524) |
| 525 – 780 | 256 | Inode table (`s_itable_start` = 525) |
| 781 – 32766 | 31986 | Data (`s_first_data_block` = 781) |
| 32767 | 1 | Backup superblock (`s_backup_sb` = 32767) |

Metadata overhead: 782 blocks of 32768, or **2.4 %**, of which the journal is
513 blocks (1.6 %). See **OI-5** — on larger volumes the journal fraction stays
constant at 1.6 % under the current sizing rule, which is why a cap is proposed.

After `mkfs.nvfs`:

- Block bitmap: bits 0..780 (metadata), bit 781 (the root directory's first
  data block) and bit 32767 (backup superblock) set. This volume needs no
  padding bits, because 32768 = 2 × 16384 exactly.
- Summary entry 0 = 16384 − 782 = 15602; entry 1 = 16384 − 1 = 16383.
  (Neither is the 0xFFFF special case; §7.2.)
- Inode bits 0..6 set (inodes 1..7); root inode 1 initialised as an empty
  directory with `i_links` = 2, `i_size` = 2048, one data block at 781.
- `s_free_blocks` = 32768 − 782 − 1 = 31985 (781 metadata blocks + backup
  superblock + the root's first directory block).
- Journal ring zeroed; `js_checkpoint` = 0, `js_seq` = 1.
- `s_state` = 0.

---

*End of DN-FS-NVFS-003, Draft 3.*
