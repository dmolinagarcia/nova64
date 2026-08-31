# DN-FS-EXT2-001 — ext2 Support Layer (Read/Write)

**Project:** noVa64 (DANI-65816)
**Document:** Design Note, Draft 1
**Status:** For review — not frozen
**Scope:** Operating-system-side support for mounting, reading and writing an ext2
partition on the SD card, and its relationship to NVFS.

---

## 1. Purpose

noVa64 already has a native file system (NVFS) with a complete specification,
reference implementation, host tooling and a conformance suite. This note specifies
a **second, foreign file system driver** for ext2, whose purpose is interoperability:
the ability to exchange files with a Linux or macOS host by moving the SD card,
without any host-side custom tooling.

ext2 is a deliberate choice over FAT32 for this role:

- No patent ambiguity, no long-filename encoding layer.
- Little-endian on-disk layout — the 65816 reads every field directly, with no
  byte-swapping code path.
- A clean, well-documented, small structure set (superblock, group descriptor,
  inode, directory entry) with no FAT chain walking.
- Native support for a real directory tree, permissions, hard links and symlinks,
  which maps onto the eventual noVa64 VFS without loss.

The cost is that ext2 has **no journal**, so crash consistency must be obtained
entirely from write ordering — the same discipline already applied during NVFS
development.

## 2. Scope and non-goals

### In scope

- Read/write mount of a single ext2 partition, revision 1 (`EXT2_DYNAMIC_REV`),
  block size 1024 bytes, inode size 128 bytes.
- Full path resolution, file read/write/extend/truncate, directory create/remove,
  hard links, rename, symlink read.
- A block-device abstraction with an MBR partition scan.
- A write-back buffer cache.
- Definition of a VFS interface shared with NVFS.

### Out of scope

- ext3/ext4 features: journals, extents, 64-bit block numbers, htree directory
  maintenance, metadata checksums.
- Extended attributes, ACLs, quotas.
- File system resize.
- An on-target `fsck`. **Recovery is a host responsibility** (`e2fsck`).
- Block sizes other than 1024 bytes for the *write* path (see §5.3).
- Files larger than 2 GiB − 1 (`LARGE_FILE`, see §5.2).

### Non-goal, stated explicitly

ext2 is **not** intended to become the noVa64 system volume. NVFS remains the
native file system: it is specified for this machine, its crash-consistency model
has been validated against a 2,350-check conformance suite, and its read path is
already implemented in 65816 assembly. ext2 is a transfer medium.

## 3. Dependencies and blocking items

| Dependency | State | Impact |
|---|---|---|
| Helium SD block register map | **Does not exist** | Blocks L0 entirely; blocks all on-target work. Identical blocker to NVFS hardware bring-up. |
| Helium block-move / burst path to SDRAM | Undefined | Determines whether a 1 KiB block transfer costs ~1 K CPU cycles or ~10 K. See §14.3. |
| Real-time clock | Not present in any current sheet | Timestamps cannot be generated. See §13.4 and Open Item OI-6. |
| VFS layer | Not designed | Both NVFS and ext2 currently have direct, bespoke call paths. See §12. |
| Calypsi C struct layout guarantees | Unverified | Static assertions required, see §7.1. |

None of the above blocks specification or host-side implementation work, which can
proceed against a file-backed image on the development host exactly as `libnvfs`
did.

---

## 4. On-disk format, as implemented

All multi-byte fields are little-endian. On the 65816 this means direct load with
no conversion, which is the single largest reason ext2 is cheaper than a
big-endian format on this machine.

### 4.1 Geometry

```
partition_start_lba          from MBR
block_size    = 1024 << s_log_block_size          (write path: 1024 only)
sectors_per_block = block_size / 512               (2 for 1 KiB blocks)
lba(fs_block) = partition_start_lba + fs_block * sectors_per_block

superblock    at byte offset 1024 from partition start
              = fs_block 1 when block_size == 1024
s_first_data_block = 1 when block_size == 1024, else 0
group descriptor table starts at fs_block (s_first_data_block + 1)

groups_count  = ceil(s_blocks_count - s_first_data_block, s_blocks_per_group)
              = ceil(s_inodes_count, s_inodes_per_group)     (must agree)
desc_per_block = block_size / 32
inodes_per_block = block_size / s_inode_size
itable_blocks_per_group = ceil(s_inodes_per_group, inodes_per_block)
```

With 1 KiB blocks and 128-byte inodes: 8 inodes per block, 32 descriptors per
block, `s_blocks_per_group` = 8192 (one block bitmap covers 1024 × 8 bits).

### 4.2 Addressing reach (1 KiB blocks, 256 pointers per indirect block)

| Level | Blocks | Cumulative file size |
|---|---|---|
| 12 direct | 12 | 12 KiB |
| 1× indirect | 256 | 268 KiB |
| 2× indirect | 65,536 | 64.26 MiB |
| 3× indirect | 16,777,216 | 16.06 GiB |

A single-indirect implementation covers 268 KiB and is not sufficient. Double
indirect is mandatory. Triple indirect is required only for files above 64 MiB;
it is specified here and implemented, because the recursive block-map walker
handles all three levels with the same code and omitting level 3 saves nothing.

### 4.3 Reserved inodes

| Inode | Meaning |
|---|---|
| 1 | Bad blocks |
| 2 | **Root directory** — the entry point for all path resolution |
| 7 | Reserved GDT blocks (`resize_inode`) |
| 11 | Conventionally `lost+found` |

`s_first_ino` (normally 11) is the first inode available for allocation. The
allocator must never return an inode below `s_first_ino`.

---

## 5. Feature flag acceptance policy

This is the most safety-critical part of the mount path. Mounting a file system
whose features are not understood, and then writing to it, is the primary route
to silent corruption.

### 5.1 The rule

- `s_feature_incompat` contains a bit not in the known-INCOMPAT set → **refuse to mount**.
- `s_feature_ro_compat` contains a bit not in the known-RO_COMPAT set → **mount read-only**.
- `s_feature_compat` bits may always be ignored for reading, but some require
  action on write (see `DIR_INDEX`).

### 5.2 Policy table

| Set | Bit | Name | noVa64 policy |
|---|---|---|---|
| COMPAT | 0x0001 | `DIR_PREALLOC` | Ignore. We never preallocate. |
| COMPAT | 0x0002 | `IMAGIC_INODES` | Ignore. |
| COMPAT | 0x0004 | `HAS_JOURNAL` | **Force read-only.** This is an ext3 volume. Mounting it RW as ext2 is legal only if the journal is clean, and verifying that is out of scope. |
| COMPAT | 0x0008 | `EXT_ATTR` | Ignore for read. On write, `i_file_acl` of a modified inode must be preserved untouched. |
| COMPAT | 0x0010 | `RESIZE_INODE` | Ignore. Inode 7 is simply never touched. |
| COMPAT | 0x0020 | `DIR_INDEX` | Read-safe (see §5.4). On any directory modification, clear `EXT2_INDEX_FL` (0x1000) in that directory's `i_flags`. |
| INCOMPAT | 0x0001 | `COMPRESSION` | **Refuse mount.** |
| INCOMPAT | 0x0002 | `FILETYPE` | **Supported.** Set by every modern `mke2fs`. Determines whether `dirent.file_type` is meaningful. |
| INCOMPAT | 0x0004 | `RECOVER` | **Refuse mount.** Journal needs replay; only the host can do that. |
| INCOMPAT | 0x0008 | `JOURNAL_DEV` | **Refuse mount.** |
| INCOMPAT | 0x0010 | `META_BG` | **Refuse mount.** Changes descriptor table layout. |
| RO_COMPAT | 0x0001 | `SPARSE_SUPER` | **Accept RW.** Affects only where backup superblocks live; we never write backups and never resize. |
| RO_COMPAT | 0x0002 | `LARGE_FILE` | **Accept RW, with a guard.** Refuse to open for writing any regular file whose `i_dir_acl` (size-high) is non-zero. |
| RO_COMPAT | 0x0004 | `BTREE_DIR` | **Force read-only.** |
| any | other | unknown | INCOMPAT → refuse; RO_COMPAT → read-only; COMPAT → ignore. |

### 5.3 Block size policy

The read path handles 1024, 2048 and 4096. The **write path is restricted to 1024
bytes** for the first implementation, because:

- `s_first_data_block` differs (1 vs 0), and the difference propagates into every
  group and bitmap index calculation.
- Buffer cache footprint scales directly with block size, and the cache lives in
  SDRAM alongside the rest of the kernel.
- A 4 KiB block-fill and read-modify-write costs 4× the CPU time on a machine
  where block copying is already the dominant cost.

Mounting a non-1024 volume for writing is refused with `EXT2_EBLKSIZE`. This is a
restriction on our implementation, not on the format, and can be lifted later.

### 5.4 Why `DIR_INDEX` is the dangerous one

An htree directory is *deliberately* backward-compatible for reading: the index
root in block 0 is hidden inside a fake directory entry whose `rec_len` spans the
whole block, and interior nodes are hidden the same way. A linear scan therefore
sees `.`, `..` and every real entry, and sees the index blocks as empty space.
Reading works, without the reader ever knowing the directory was indexed.

Writing does not. If we insert an entry into a leaf block without updating the
hash index, the host will subsequently fail to find that file, because its lookup
goes through the index. The file exists, occupies space, appears in `readdir`, and
is not openable by name.

The fix is to clear `EXT2_INDEX_FL` in the directory's `i_flags` on the first
modification. The host then falls back to linear scanning for that directory. The
orphan index blocks remain allocated and are harmless; `e2fsck` will report and
reclaim them.

**Recommended host-side formatting**, which removes the problem at source:

```
mke2fs -t ext2 -b 1024 -I 128 -O ^dir_index,^resize_inode,^ext_attr /dev/sdX1
```

---

## 6. Layer architecture

```
  L12  syscall / VFS         open, read, write, close, mkdir, unlink, stat
   |
  L11  vnode operations      shared interface: NVFS and ext2 behind one table
   |
  L10  path resolution       namei, nameiparent, symlink resolution
   |
  L9   directory ops         lookup, add, remove, iterate, mkdir, rmdir, rename
   |
  L8   file I/O              read, write, truncate
   |
  L7   allocators            balloc, bfree, ialloc, ifree
   |
  L6   block map             bmap, bmap_alloc, indirect tree walk
   |
  L5   inode layer           iget, iput, iupdate, in-memory inode table
   |
  L4   group descriptors     gd_get, gd_dirty
   |
  L3   superblock / mount    ext2_mount, ext2_umount, geometry, feature policy
   |
  L2   buffer cache          bget, bdirty, brelse, bpin, bsync
   |
  L1   primitives            32-bit arithmetic, 32/16 division, bitmap ops
   |
  L0   block device          blk_read, blk_write, blk_flush, partition scan
```

Each layer may call only the layer immediately below it, with one deliberate
exception: L3 reads the superblock through L0 directly, before the buffer cache
geometry is known.

---

## 7. Function map

Signatures use `stdint.h` types throughout. On Calypsi, `int` is 16-bit; no
function in this specification relies on `int` being any particular width.

### 7.0 L0 — Block device

```c
typedef struct blkdev blkdev_t;

int  blk_read (blkdev_t *d, uint32_t lba, void *buf);           /* 512 bytes  */
int  blk_write(blkdev_t *d, uint32_t lba, const void *buf);
int  blk_readn (blkdev_t *d, uint32_t lba, uint16_t n, void *buf);
int  blk_writen(blkdev_t *d, uint32_t lba, uint16_t n, const void *buf);
int  blk_flush(blkdev_t *d);                                    /* SD cache   */

int  part_scan(blkdev_t *d, part_entry_t out[4]);               /* MBR        */
```

`blk_readn` / `blk_writen` exist so that a 1 KiB file-system block is a *single*
device transaction rather than two. Once the Helium SD register map exists, these
are the functions that should map onto a multi-block CMD18/CMD25 transfer.

`blk_flush` must translate to whatever guarantees the SD card has committed its
internal write buffer. Without it, the write-ordering guarantees of §11 are
fiction.

### 7.1 L1 — Primitives

```c
/* 32-bit arithmetic — assembly, ca65 */
void     u32_add(uint32_t *acc, uint32_t v);
void     u32_sub(uint32_t *acc, uint32_t v);
int8_t   u32_cmp(uint32_t a, uint32_t b);

/* The only division needed. Divisor is always <= 8192 in practice
   (s_blocks_per_group and s_inodes_per_group are bounded by 8 * block_size),
   so a 32/16 routine is sufficient — no 32/32 division anywhere. */
uint32_t u32_div16(uint32_t num, uint16_t den, uint16_t *rem);

/* Bitmap operations — assembly. bitmap_find_first_zero is called on every
   allocation and is worth hand-optimising: skip 0xFFFF words, then bit-scan. */
uint8_t  bit_test (const uint8_t *bm, uint16_t idx);
void     bit_set  (uint8_t *bm, uint16_t idx);
void     bit_clear(uint8_t *bm, uint16_t idx);
int32_t  bitmap_find_first_zero(const uint8_t *bm, uint16_t nbits);
int32_t  bitmap_find_zero_from (const uint8_t *bm, uint16_t nbits, uint16_t start);
```

`u32_div16` returning a 32-bit quotient and 16-bit remainder covers both call
sites exactly:

```
group = (ino - 1) / s_inodes_per_group      index = (ino - 1) % s_inodes_per_group
group = (blk - s_first_data_block) / s_blocks_per_group
```

Every other division in ext2 with a 1 KiB block size is by a power of two and
compiles to shifts.

### 7.2 L2 — Buffer cache

```c
typedef struct buf {
    uint32_t     block;         /* fs block number                    */
    uint16_t     refcnt;
    uint8_t      flags;         /* VALID | DIRTY | PINNED             */
    struct buf  *lru_prev, *lru_next;
    uint8_t     *data;          /* block_size bytes, in SDRAM         */
} buf_t;

buf_t *bget  (ext2_fs_t *fs, uint32_t block);   /* read if not resident */
buf_t *bget_zero(ext2_fs_t *fs, uint32_t block);/* new block: zero, no read */
void   bdirty(buf_t *b);
void   brelse(buf_t *b);
void   bpin  (buf_t *b);
void   bunpin(buf_t *b);
int    bwrite(ext2_fs_t *fs, buf_t *b);         /* write one, immediately */
int    bsync (ext2_fs_t *fs);                   /* write all dirty        */
void   binvalidate(ext2_fs_t *fs);              /* unmount / error path   */
```

**The buffer cache is not an optimisation.** Creating one file touches: the
superblock, a group descriptor, the inode bitmap, an inode table block, the block
bitmap, a data block, and one or two directory blocks. Without caching that is
15–20 device transactions for a single `creat()`. With eight resident buffers and
the superblock and descriptor table pinned, it is two or three.

`bget_zero` exists so that allocating a fresh indirect block or directory block
does not read 1 KiB from the card only to overwrite it — a common and easily
avoided waste.

**Pinning policy:** the superblock buffer and the group-descriptor-table blocks
are pinned for the lifetime of the mount. With 1 KiB blocks and 32 descriptors per
block, a 2 GiB volume has 256 groups = 8 descriptor blocks = 8 KiB pinned. For
larger cards this must become an LRU sub-cache; see Open Item OI-4.

### 7.3 L3 — Superblock and mount

```c
int ext2_mount  (ext2_fs_t *fs, blkdev_t *dev, uint32_t part_lba,
                 uint32_t part_sectors, uint8_t want_write);
int ext2_umount (ext2_fs_t *fs);
int ext2_sync   (ext2_fs_t *fs);        /* bsync + sb_write + blk_flush */
int ext2_sb_write(ext2_fs_t *fs);
```

`ext2_mount` sequence:

1. `blk_readn(dev, part_lba + 2, 2, sb_raw)` — 1 KiB at byte offset 1024.
2. Validate `s_magic == 0xEF53`. Otherwise `EXT2_ENOTEXT2`.
3. Validate `s_rev_level`. Rev 0 implies `s_inode_size = 128` and
   `s_first_ino = 11`; those fields are not present on disk and must be
   synthesised.
4. Apply the §5.2 feature policy. May downgrade `want_write` to 0.
5. Compute and **range-check** all derived geometry. A corrupt
   `s_log_block_size` or `s_inodes_per_group` of 0 must be rejected here, not
   turned into a division by zero four layers up.
6. Cross-check: the group count derived from block counts must equal the group
   count derived from inode counts.
7. Initialise the buffer cache with the now-known `block_size`.
8. Read and pin the group descriptor table.
9. If mounting read-write: set `s_state = 0` (not clean), increment
   `s_mnt_count`, write the superblock, `blk_flush`.

`ext2_umount`: `bsync`, set `s_state = 1` (`EXT2_VALID_FS`), update
`s_free_blocks_count` / `s_free_inodes_count`, `ext2_sb_write`, `blk_flush`,
`binvalidate`.

Leaving `s_state = 0` while mounted is the mechanism by which an unclean
shutdown is communicated to the host: the next Linux mount will run `e2fsck`.
This is the closest thing ext2 has to a recovery protocol and it must not be
skipped.

Backup superblocks are **never** written. `e2fsck` reconciles them.

### 7.4 L4 — Group descriptors

```c
ext2_gd_t *gd_get  (ext2_fs_t *fs, uint32_t group);   /* pointer into cache */
void       gd_dirty(ext2_fs_t *fs, uint32_t group);
```

### 7.5 L5 — Inodes

```c
typedef struct inode {
    ext2_fs_t   *fs;
    uint32_t     ino;
    uint16_t     refcnt;        /* in-memory references                */
    uint8_t      flags;         /* VALID | DIRTY                       */
    ext2_dinode_t d;            /* the on-disk inode, 128 bytes        */
} inode_t;

inode_t *iget    (ext2_fs_t *fs, uint32_t ino);
void     iput    (inode_t *ip);      /* refcnt--; may trigger delete   */
void     idirty  (inode_t *ip);
int      iupdate (inode_t *ip);      /* flush to its inode table block */
uint8_t  itype   (const inode_t *ip);/* EXT2_FT_* from i_mode          */
```

Location arithmetic:

```
group  = (ino - 1) / s_inodes_per_group
index  = (ino - 1) % s_inodes_per_group
block  = gd[group].bg_inode_table + (index / inodes_per_block)
offset = (index % inodes_per_block) * s_inode_size
```

**The in-memory inode table is mandatory**, not a convenience. If two open file
descriptors refer to the same inode through two separate copies, appends from one
will silently discard the other's size and block pointers. `iget` returns the
existing entry with an incremented refcount when the inode is already resident.

`iput` on the last reference of an inode with `i_links_count == 0` triggers
truncate-to-zero followed by `ifree`. This is what makes "unlink while open"
behave correctly, and it is worth implementing from the start rather than
retrofitting.

**`i_blocks` is counted in 512-byte units, not file-system blocks.** With 1 KiB
blocks, every data block allocated adds 2, and every *indirect* block allocated
also adds 2. This is the single most commonly mis-implemented field in ext2 and
`e2fsck` checks it on every inode.

### 7.6 L6 — Block mapping

```c
/* Resolve. Returns 0 for a hole (valid, means "read as zeros"). */
uint32_t bmap(inode_t *ip, uint32_t file_blk);

/* Resolve, allocating the data block and any missing indirect blocks.
   Returns 0 only on failure. */
uint32_t bmap_alloc(inode_t *ip, uint32_t file_blk);

/* Free all blocks from file_blk onward, including indirect blocks that
   become entirely empty. Used by truncate. */
int bmap_free_from(inode_t *ip, uint32_t file_blk);
```

Index decomposition for 1 KiB blocks (`ptrs = 256`, shift 8):

```
n < 12                       -> i_block[n]
n -= 12;  n < 256            -> i_block[12] -> [n]
n -= 256; n < 65536          -> i_block[13] -> [n >> 8] -> [n & 255]
n -= 65536                   -> i_block[14] -> [n >> 16] -> [(n >> 8) & 255]
                                            -> [n & 255]
```

All divisions here are shifts. Newly allocated indirect blocks **must be zeroed
before the pointer to them is stored** — `bget_zero` — otherwise stale disk
content is interpreted as block pointers.

### 7.7 L7 — Allocators

```c
uint32_t balloc(ext2_fs_t *fs, uint32_t goal);   /* 0 on failure */
void     bfree (ext2_fs_t *fs, uint32_t block);
uint32_t ialloc(ext2_fs_t *fs, uint32_t dir_ino, uint8_t is_dir);
void     ifree (ext2_fs_t *fs, uint32_t ino, uint8_t was_dir);
```

`balloc` strategy, in order:

1. The block immediately following `goal`, if free — this is what produces
   contiguous files and it costs one bit test.
2. Any free block in `goal`'s group.
3. A linear scan of subsequent groups, wrapping.

`goal` is the previously allocated block of the same file, or the first block of
the inode's group for the first allocation. This crude policy is enough to keep
sequential files largely contiguous, which matters more here than on a spinning
disk would suggest, because it is what allows `blk_readn` to fetch several blocks
per SD transaction.

`ialloc` prefers the parent directory's group for regular files, and for
directories prefers the group with the most free inodes (Orlov-lite). Must never
return an inode `< s_first_ino`.

**Counter invariant.** Three counters describe the same fact and must be updated
in the same operation:

```
bitmap bit  <->  gd[group].bg_free_{blocks,inodes}_count  <->  sb.s_free_{blocks,inodes}_count
```

Plus `bg_used_dirs_count` on directory create/remove. `e2fsck` verifies all of
them.

### 7.8 L8 — File I/O

```c
int32_t ext2_read    (inode_t *ip, uint32_t off, uint32_t len, void *buf);
int32_t ext2_write   (inode_t *ip, uint32_t off, uint32_t len, const void *buf);
int     ext2_truncate(inode_t *ip, uint32_t newsize);
```

`ext2_read`: for each block in range, `bmap`; if it returns 0, fill the
destination with zeros rather than reporting an error — holes are legal and
`mke2fs`-created files can contain them.

`ext2_write`: a partial first or last block requires read-modify-write; a fully
covered block uses `bget_zero` and skips the read. Extends `i_size` when
`off + len > i_size`, updates `i_blocks` per allocation, sets `i_mtime` and
`i_ctime`.

`ext2_truncate` is the highest-risk function in the driver. It must walk the
indirect tree from the tail, free leaf blocks, free indirect blocks that have
become empty, clear the corresponding pointers, and keep `i_blocks` exact
throughout. It is also required by `O_TRUNC` and by every file deletion, so it
cannot be deferred. It deserves its own targeted test set (§15).

### 7.9 L9 — Directories

On-disk entry:

```
offset 0  inode      u32     0 means "deleted / free space"
offset 4  rec_len    u16     distance to next entry; 4-byte aligned
offset 6  name_len   u8
offset 7  file_type  u8      valid only if INCOMPAT_FILETYPE
offset 8  name[name_len]     not NUL-terminated
```

Two invariants that drive the whole implementation:

- **An entry never spans a block boundary.** The last entry in a block has a
  `rec_len` that reaches exactly the end of the block.
- **A directory's size is always a whole number of blocks**, and `i_size` equals
  the allocated length exactly.

```c
typedef int (*dir_cb_t)(void *ctx, uint32_t ino, uint8_t type,
                        const char *name, uint8_t name_len);

int  dir_iterate (inode_t *dp, dir_cb_t cb, void *ctx);
int  dir_lookup  (inode_t *dp, const char *name, uint8_t nlen, uint32_t *ino_out);
int  dir_add     (inode_t *dp, const char *name, uint8_t nlen,
                  uint32_t ino, uint8_t type);
int  dir_remove  (inode_t *dp, const char *name, uint8_t nlen);
int  dir_is_empty(inode_t *dp);

int  ext2_create (inode_t *dp, const char *name, uint16_t mode, inode_t **out);
int  ext2_mkdir  (inode_t *dp, const char *name, uint16_t mode);
int  ext2_rmdir  (inode_t *dp, const char *name);
int  ext2_unlink (inode_t *dp, const char *name);
int  ext2_link   (inode_t *dp, const char *name, inode_t *ip);
int  ext2_rename (inode_t *olddp, const char *oldname,
                  inode_t *newdp, const char *newname);
```

`dir_add`: the required size is `ceil(8 + name_len, 4)`. Scan for either a free
entry (`inode == 0`) with `rec_len >= need`, or a live entry whose `rec_len`
exceeds its own actual size by at least `need` — in which case the oversized
entry is split. If no block has room, append a new block, zero it, and lay down a
single entry with `rec_len = block_size`.

Before modifying, if `EXT2_INDEX_FL` is set in `dp->d.i_flags`, clear it and mark
the inode dirty (§5.4).

`dir_remove`: extend the *previous* entry's `rec_len` to absorb the removed one.
If the entry is the first in its block, set its `inode` to 0 instead — there is no
previous entry to extend. Never shrink the directory; ext2 directories do not
shrink and `e2fsck` does not expect them to.

`ext2_mkdir`: allocate an inode with `is_dir = 1`, allocate one block, write `.`
(`rec_len` = 12) and `..` (`rec_len` = `block_size − 12`), set the new directory's
`i_links_count = 2`, increment the parent's `i_links_count`, increment
`bg_used_dirs_count`, and only then add the entry in the parent.

`ext2_rename` must handle: same-directory rename, cross-directory rename (which
requires rewriting the `..` entry and adjusting both parents' `i_links_count`),
and overwrite of an existing target. It should refuse to move a directory into
its own descendant — the check requires walking `..` upward from the destination.

### 7.10 L10 — Path resolution

```c
inode_t *namei      (ext2_fs_t *fs, inode_t *cwd, const char *path);
inode_t *nameiparent(ext2_fs_t *fs, inode_t *cwd, const char *path,
                     char *name_out, uint8_t *nlen_out);
int      ext2_readlink(inode_t *ip, char *buf, uint16_t bufsize);
```

`namei` starts at inode 2 for an absolute path, at `cwd` otherwise; splits on
`/`; treats empty components and `.` as no-ops; `..` at the root resolves to the
root. `nameiparent` returns the containing directory plus the final component,
which is what every mutating operation needs.

Symlinks: if `i_size < 60`, the target is stored **inside `i_block[]`** as raw
bytes ("fast symlink") and `i_blocks` is 0. Otherwise it occupies a data block.
Symlink following must be bounded — a limit of 8 traversals per resolution,
returning `EXT2_ELOOP` beyond that.

### 7.11 L11/L12 — VFS and syscalls

See §12.

---

## 8. C99 data structures (`libext2`)

```c
#include <stdint.h>

#define EXT2_SUPER_MAGIC        0xEF53u
#define EXT2_ROOT_INO           2u
#define EXT2_GOOD_OLD_REV       0u
#define EXT2_DYNAMIC_REV        1u
#define EXT2_GOOD_OLD_INODE_SIZE 128u
#define EXT2_NDIR_BLOCKS        12u
#define EXT2_IND_BLOCK          12u
#define EXT2_DIND_BLOCK         13u
#define EXT2_TIND_BLOCK         14u
#define EXT2_N_BLOCKS           15u
#define EXT2_NAME_LEN           255u

/* i_flags */
#define EXT2_INDEX_FL           0x00001000u

/* i_mode type field */
#define EXT2_S_IFMT             0xF000u
#define EXT2_S_IFSOCK           0xC000u
#define EXT2_S_IFLNK            0xA000u
#define EXT2_S_IFREG            0x8000u
#define EXT2_S_IFBLK            0x6000u
#define EXT2_S_IFDIR            0x4000u
#define EXT2_S_IFCHR            0x2000u
#define EXT2_S_IFIFO            0x1000u

/* dirent file_type */
#define EXT2_FT_UNKNOWN 0u
#define EXT2_FT_REG_FILE 1u
#define EXT2_FT_DIR      2u
#define EXT2_FT_CHRDEV   3u
#define EXT2_FT_BLKDEV   4u
#define EXT2_FT_FIFO     5u
#define EXT2_FT_SOCK     6u
#define EXT2_FT_SYMLINK  7u

typedef struct {
    uint32_t s_inodes_count;         /* 0x00 */
    uint32_t s_blocks_count;         /* 0x04 */
    uint32_t s_r_blocks_count;       /* 0x08 */
    uint32_t s_free_blocks_count;    /* 0x0C */
    uint32_t s_free_inodes_count;    /* 0x10 */
    uint32_t s_first_data_block;     /* 0x14 */
    uint32_t s_log_block_size;       /* 0x18 */
    uint32_t s_log_frag_size;        /* 0x1C */
    uint32_t s_blocks_per_group;     /* 0x20 */
    uint32_t s_frags_per_group;      /* 0x24 */
    uint32_t s_inodes_per_group;     /* 0x28 */
    uint32_t s_mtime;                /* 0x2C */
    uint32_t s_wtime;                /* 0x30 */
    uint16_t s_mnt_count;            /* 0x34 */
    uint16_t s_max_mnt_count;        /* 0x36 */
    uint16_t s_magic;                /* 0x38 */
    uint16_t s_state;                /* 0x3A */
    uint16_t s_errors;               /* 0x3C */
    uint16_t s_minor_rev_level;      /* 0x3E */
    uint32_t s_lastcheck;            /* 0x40 */
    uint32_t s_checkinterval;        /* 0x44 */
    uint32_t s_creator_os;           /* 0x48 */
    uint32_t s_rev_level;            /* 0x4C */
    uint16_t s_def_resuid;           /* 0x50 */
    uint16_t s_def_resgid;           /* 0x52 */
    /* EXT2_DYNAMIC_REV only */
    uint32_t s_first_ino;            /* 0x54 */
    uint16_t s_inode_size;           /* 0x58 */
    uint16_t s_block_group_nr;       /* 0x5A */
    uint32_t s_feature_compat;       /* 0x5C */
    uint32_t s_feature_incompat;     /* 0x60 */
    uint32_t s_feature_ro_compat;    /* 0x64 */
    uint8_t  s_uuid[16];             /* 0x68 */
    char     s_volume_name[16];      /* 0x78 */
    char     s_last_mounted[64];     /* 0x88 */
    uint32_t s_algo_bitmap;          /* 0xC8 */
    uint8_t  s_prealloc_blocks;      /* 0xCC */
    uint8_t  s_prealloc_dir_blocks;  /* 0xCD */
    uint16_t s_reserved_gdt_blocks;  /* 0xCE */
    uint8_t  s_journal_uuid[16];     /* 0xD0 */
    uint32_t s_journal_inum;         /* 0xE0 */
    uint32_t s_journal_dev;          /* 0xE4 */
    uint32_t s_last_orphan;          /* 0xE8 */
    uint8_t  s_pad[788];             /* 0xEC .. 0x3FF */
} ext2_super_t;                      /* 1024 bytes */

typedef struct {
    uint32_t bg_block_bitmap;        /* 0x00 */
    uint32_t bg_inode_bitmap;        /* 0x04 */
    uint32_t bg_inode_table;         /* 0x08 */
    uint16_t bg_free_blocks_count;   /* 0x0C */
    uint16_t bg_free_inodes_count;   /* 0x0E */
    uint16_t bg_used_dirs_count;     /* 0x10 */
    uint16_t bg_pad;                 /* 0x12 */
    uint8_t  bg_reserved[12];        /* 0x14 */
} ext2_gd_t;                         /* 32 bytes */

typedef struct {
    uint16_t i_mode;                 /* 0x00 */
    uint16_t i_uid;                  /* 0x02 */
    uint32_t i_size;                 /* 0x04 */
    uint32_t i_atime;                /* 0x08 */
    uint32_t i_ctime;                /* 0x0C */
    uint32_t i_mtime;                /* 0x10 */
    uint32_t i_dtime;                /* 0x14 */
    uint16_t i_gid;                  /* 0x18 */
    uint16_t i_links_count;          /* 0x1A */
    uint32_t i_blocks;               /* 0x1C  512-byte units!            */
    uint32_t i_flags;                /* 0x20 */
    uint32_t i_osd1;                 /* 0x24 */
    uint32_t i_block[EXT2_N_BLOCKS]; /* 0x28 */
    uint32_t i_generation;           /* 0x64 */
    uint32_t i_file_acl;             /* 0x68 */
    uint32_t i_dir_acl;              /* 0x6C  size-high for regular files */
    uint32_t i_faddr;                /* 0x70 */
    uint8_t  i_osd2[12];             /* 0x74 */
} ext2_dinode_t;                     /* 128 bytes */

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    /* char name[] follows, unterminated */
} ext2_dirent_hdr_t;                 /* 8 bytes */

/* In-memory mount context */
typedef struct ext2_fs {
    blkdev_t     *dev;
    uint32_t      part_lba;
    uint32_t      part_sectors;

    ext2_super_t *sb;                /* points into a pinned buffer */
    buf_t        *sb_buf;

    uint16_t      block_size;
    uint8_t       block_shift;       /* log2(block_size)             */
    uint8_t       sectors_per_block;
    uint16_t      inode_size;
    uint16_t      inodes_per_block;
    uint16_t      ptrs_per_block;
    uint8_t       ptr_shift;         /* log2(ptrs_per_block)         */
    uint32_t      groups_count;
    uint16_t      desc_per_block;
    uint32_t      gd_table_block;

    uint8_t       read_only;
    uint8_t       dirty_sb;
    uint8_t       last_error;
} ext2_fs_t;
```

### 8.1 Layout verification

Every ext2 structure above is naturally aligned field-by-field, so a conforming
compiler inserts no interior padding. This must nevertheless be asserted, because
a silent padding byte in `ext2_dinode_t` shifts `i_block[]` and produces a driver
that corrupts every file it touches:

```c
_Static_assert(sizeof(ext2_super_t)      == 1024, "superblock layout");
_Static_assert(sizeof(ext2_gd_t)         == 32,   "group descriptor layout");
_Static_assert(sizeof(ext2_dinode_t)     == 128,  "inode layout");
_Static_assert(sizeof(ext2_dirent_hdr_t) == 8,    "dirent header layout");
_Static_assert(offsetof(ext2_dinode_t, i_block) == 0x28, "i_block offset");
_Static_assert(offsetof(ext2_super_t, s_magic)  == 0x38, "s_magic offset");
```

If Calypsi does not support `_Static_assert`, the equivalent negative-array-size
trick must be used. **Do not skip this.** Verify it on the host build *and* on the
Calypsi build, since they are different compilers with different layout rules.

---

## 9. Error model

```c
#define EXT2_OK          0
#define EXT2_EIO        -1   /* block device failure                   */
#define EXT2_ENOTEXT2   -2   /* bad magic                              */
#define EXT2_EFEATURE   -3   /* unsupported incompat feature           */
#define EXT2_EBLKSIZE   -4   /* block size unsupported for this mode   */
#define EXT2_ECORRUPT   -5   /* structural inconsistency detected      */
#define EXT2_EROFS      -6   /* write attempted on read-only mount     */
#define EXT2_ENOSPC     -7
#define EXT2_ENOINO     -8   /* inode table exhausted                  */
#define EXT2_ENOENT     -9
#define EXT2_EEXIST    -10
#define EXT2_ENOTDIR   -11
#define EXT2_EISDIR    -12
#define EXT2_ENOTEMPTY -13
#define EXT2_ELOOP     -14
#define EXT2_ENAMETOOLONG -15
#define EXT2_EINVAL    -16
#define EXT2_EFBIG     -17   /* LARGE_FILE guard                       */
```

**On `EXT2_ECORRUPT` or `EXT2_EIO` during a write path, the mount is immediately
downgraded to read-only** and the error is latched in `fs->last_error`. Continuing
to write to a file system that has already failed a consistency check is how a
recoverable problem becomes an unrecoverable one. The console should report this
loudly.

---

## 10. Sanity checks

Cheap checks that must be present, because the failure mode without them is silent
corruption rather than a clean error:

| Check | Where |
|---|---|
| `s_magic == 0xEF53` | mount |
| `s_log_block_size <= 2`, `s_inodes_per_group != 0`, `s_blocks_per_group != 0` | mount |
| group count from blocks == group count from inodes | mount |
| `bg_block_bitmap`, `bg_inode_bitmap`, `bg_inode_table` all within the group's range | gd_get, first use |
| every block pointer in range `[s_first_data_block, s_blocks_count)` | bmap, truncate |
| `ino` in range `[1, s_inodes_count]` | iget |
| `rec_len >= 8`, 4-byte aligned, does not run past end of block | every dirent walk |
| `name_len + 8 <= rec_len` | every dirent walk |
| directory `i_size` is a whole number of blocks | dir open |

The dirent checks matter most: an unvalidated `rec_len` of 0 turns `dir_iterate`
into an infinite loop, and one larger than the block turns it into an out-of-bounds
read.

---

## 11. Write ordering and crash consistency

ext2 has no journal. Consistency after an unexpected power loss depends entirely
on the order in which blocks reach the card, and on `blk_flush` actually meaning
something. This is the same class of analysis performed for NVFS, where two
ordering defects were found only during implementation — the expectation here is
that this section will need revision once the code exists.

### 11.1 The governing invariant

> A block or inode may be **allocated but unreferenced** — this leaks space, and
> `e2fsck` reclaims it.
>
> A block or inode must **never be free while still referenced** — this is
> corruption, and `e2fsck` cannot always repair it without data loss.

Every ordering rule below is a consequence of that asymmetry. When in doubt:
**detach before freeing, allocate before referencing.**

### 11.2 Create a file

```
1. ialloc            (inode bitmap, bg counters, sb counters)
2. write the inode   (i_links_count = 1, i_dtime = 0, i_size = 0)   -> flush
3. dir_add in parent                                                -> flush
```

Crash between 2 and 3: an orphan inode. `e2fsck` moves it to `lost+found`.
The reverse order would produce a directory entry pointing at an uninitialised
inode — unrecoverable garbage.

### 11.3 Extend a file with new data

```
1. balloc the data block
2. write the data into the block                                    -> flush
3. store the pointer (in i_block[] or in an indirect block)
4. update i_size and i_blocks, write the inode                      -> flush
```

Crash before 3: block leaked. Crash between 3 and 4: the block is referenced but
beyond `i_size`, which `e2fsck` reports and corrects. Neither loses existing data.

If an indirect block must be created, it is zeroed and written **before** the
pointer to it is stored.

### 11.4 Truncate / free blocks

```
1. clear the pointer in the inode or indirect block
2. write that inode / indirect block                                -> flush
3. clear the bitmap bits, update counters
```

Detach first, then free. The reverse order leaves a live pointer to a block that
the allocator may immediately hand to another file — two files sharing a block,
which is the worst outcome in the whole failure space.

### 11.5 Delete a file

```
1. dir_remove in parent                                             -> flush
2. i_links_count--; write the inode                                 -> flush
3. if i_links_count == 0:
     set i_dtime
     truncate to 0     (per 11.4)
     ifree             (inode bitmap, counters)
4. sync
```

### 11.6 mkdir

```
1. ialloc (is_dir = 1)
2. balloc the directory's first block
3. write "." and ".." into it                                       -> flush
4. write the new inode (i_links_count = 2, i_size = block_size)     -> flush
5. parent i_links_count++; write parent inode
6. bg_used_dirs_count++
7. dir_add in parent                                                -> flush
```

### 11.7 Flush points

`blk_flush` is called at each `-> flush` above, and at every `ext2_sync`. If the
SD card's write cache cannot be reliably flushed, none of these guarantees hold —
this is a hardware property that should be verified during Helium SD bring-up and
recorded, not assumed.

`s_state` is 0 for the entire duration of a read-write mount. That is deliberate:
any crash leaves the volume marked dirty, and the host runs `e2fsck` on the next
mount. **`e2fsck` on the host is the recovery mechanism.** No on-target fsck is
planned, and writing one would be a larger project than this driver.

---

## 12. VFS integration

There is a structural decision to make now, before the ext2 code exists.

NVFS currently has its own read-path driver called directly. If ext2 is added the
same way, noVa64 ends up with two parallel, incompatible file access paths and a
syscall layer that switches on file system type at every call site. That is a
choice that gets progressively more expensive to reverse.

The proposal is a minimal vnode interface, adopted by both:

```c
typedef struct vnode vnode_t;

typedef struct vfs_ops {
    int (*lookup)  (vnode_t *dir, const char *name, uint8_t nlen, vnode_t **out);
    int32_t (*read)(vnode_t *vn, uint32_t off, uint32_t len, void *buf);
    int32_t (*write)(vnode_t *vn, uint32_t off, uint32_t len, const void *buf);
    int (*truncate)(vnode_t *vn, uint32_t newsize);
    int (*create)  (vnode_t *dir, const char *name, uint16_t mode, vnode_t **out);
    int (*mkdir)   (vnode_t *dir, const char *name, uint16_t mode);
    int (*unlink)  (vnode_t *dir, const char *name);
    int (*rmdir)   (vnode_t *dir, const char *name);
    int (*rename)  (vnode_t *od, const char *on, vnode_t *nd, const char *nn);
    int (*readdir) (vnode_t *dir, uint32_t *cookie, dirent_out_t *out);
    int (*getattr) (vnode_t *vn, vattr_t *out);
    int (*sync)    (vnode_t *vn);
    void (*vput)   (vnode_t *vn);
} vfs_ops_t;

typedef struct mount {
    char         path[32];        /* mount point                */
    const vfs_ops_t *ops;
    void        *fs_private;      /* ext2_fs_t * or nvfs_fs_t *  */
} mount_t;
```

With this in place:

- Syscalls (`COP`-based, per the existing design) are written once.
- The file descriptor table is written once.
- A third file system — an in-memory `/dev`, or a ROM file system — costs one
  ops table.

The interface is deliberately narrow. It does not attempt to model everything a
Unix VFS models; it models exactly what NVFS and ext2 both have.

**This should be specified in its own design note (DN-FS-VFS-001) before ext2
implementation begins**, because retrofitting it means rewriting the NVFS driver's
call surface as well.

---

## 13. Semantic mapping

### 13.1 Ownership and permissions

noVa64 has no user model. Files created by noVa64 use `uid = 0`, `gid = 0`,
mode `0644` for regular files and `0755` for directories. Permission bits on
existing files are read, preserved on modification, and **not enforced** — there
is nothing to enforce them against.

### 13.2 File names

ext2 permits any byte except `/` and NUL, up to 255 bytes. noVa64's console and
NVFS use a more restricted character set. Names are stored and returned verbatim;
the shell is responsible for whatever it can display. Names longer than the
console can render are still valid files and must not be silently truncated —
truncation on the *write* path would create a name that does not match what the
user asked for.

### 13.3 Non-regular files

Character devices, block devices, FIFOs and sockets appear in `readdir` with
their correct type and can be deleted, but cannot be opened. `ext2_create` never
produces them.

### 13.4 Timestamps

There is no RTC in the current design. Options:

- **(a)** Write 0 into all timestamps. Files appear as 1970-01-01 on the host.
  Harmless, obviously wrong, and never misleading.
- **(b)** Maintain a software clock seeded from the console at boot. Correct when
  the user bothers, wrong-but-plausible when they do not.
- **(c)** Add an RTC. Out of scope for this note, but it is a small part and the
  I²C bus already exists.

Recommendation: **(a) for the first implementation**, because a wrong-but-plausible
timestamp is worse than an obviously absent one when debugging, and because build
tools on the host that compare mtimes will behave more predictably with a constant.

`i_atime` is never updated on read. Doing so would turn every read into a write.

---

## 14. 65816-specific considerations

### 14.1 Endianness

ext2 is little-endian; the 65816 is little-endian. Every field is read with a
direct load. This is not a small advantage — a big-endian on-disk format would add
a byte-swap to every structure access in the driver.

### 14.2 24-bit pointers, banks

Under Calypsi's large model with 24-bit pointers, buffer cache blocks may live
anywhere in SDRAM. Two constraints:

- A 1 KiB block must not straddle a bank boundary, or the block-copy and dirent
  walk code must handle the wrap. Simplest fix: **align every cache block to 1 KiB
  and allocate the cache from a bank-aligned region.**
- Bitmap scan routines written in assembly should take a 24-bit pointer and use
  `[dp],y` addressing rather than assuming a fixed data bank.

### 14.3 Block transfer cost

This is the dominant performance term. A 1 KiB block moved byte-by-byte through
the 65816 at 8 MHz costs roughly 8–10 K cycles, or about 1 ms per block. A file
copy of 1 MB is then 1,000 blocks × 2 (read + write) = 2 seconds of pure copying,
before any file-system overhead.

The Helium SD register map — which does not yet exist — should therefore be
designed to support a **block transfer directly into SDRAM without CPU
involvement**, or at minimum a burst FIFO wide enough to avoid a per-byte
handshake. This is a request that belongs in the Helium SD block specification,
and it is worth raising there before that register map is frozen, since it is
much cheaper to include now than to add later.

### 14.4 Code size

Rough estimate, Calypsi C with hand-written assembly for L1:

| Layer | Estimate |
|---|---|
| L0–L1 | 1.5 KB (mostly assembly) |
| L2 buffer cache | 1.5 KB |
| L3–L5 | 3 KB |
| L6 block map | 2 KB |
| L7 allocators | 2 KB |
| L8 file I/O + truncate | 3 KB |
| L9 directories | 4 KB |
| L10 path resolution | 1.5 KB |
| **Total** | **~18–19 KB** |

Read-only subset: approximately 9 KB. This should be validated early; if the
driver is intended to be resident it competes with the kernel for a bank, and if
it is not, it needs a loadable-module story.

### 14.5 RAM budget

| Item | Size |
|---|---|
| Buffer cache, 8 buffers × 1 KiB | 8 KB |
| Buffer headers | 128 B |
| Pinned superblock | 1 KB |
| Pinned group descriptors (2 GiB volume, 256 groups) | 8 KB |
| In-memory inodes, 16 × 144 B | 2.3 KB |
| `ext2_fs_t` + path scratch + dirent scratch | 1 KB |
| **Total** | **~20.5 KB** |

All of this lives in SDRAM, not SRAM. The SRAM is the hardware-managed cache over
SDRAM and must not be statically carved up for file-system buffers.

The pinned descriptor table is the term that scales badly: a 32 GiB card has 4,096
groups and needs 128 KB of descriptors. See Open Item OI-4.

---

## 15. Host tooling and test strategy

The approach that worked for NVFS applies directly and should be reused rather
than reinvented.

### 15.1 Host-side

`libext2` builds and runs on the development host against a file-backed image.
This is where essentially all development and debugging happens; the target is
only for final validation.

Tools:

- `ext2img` — create, inspect and dump a test image.
- `nv ext2 …` — extend the existing `nv` CLI with ext2 subcommands, so both file
  systems share one host-side interface.

### 15.2 Differential testing

The strongest available test: perform an operation sequence with `libext2` on an
image, then run `e2fsck -fn` on that image. Any complaint is a bug in `libext2`.
`e2fsck` is a mature, thorough oracle that checks exactly the invariants that are
easy to get wrong — `i_blocks`, link counts, free counters, `used_dirs_count`,
`rec_len` chains, block double-allocation.

**`e2fsck -fn` after every mutating test case** is the single highest-value item in
this plan.

Second oracle: mount the same image with the host kernel's ext2 driver and compare
the resulting tree against expectations. This catches semantic divergence that
`e2fsck` does not — for example, an entry added to an htree directory without
clearing `EXT2_INDEX_FL` passes `e2fsck` cleanly and is still invisible to lookup.
That specific case belongs in the test set.

### 15.3 Crash injection

Reuse the NVFS crash-injection harness. For each mutating operation, replay the
block write trace truncated at every possible point, and after each truncation
assert:

- `e2fsck -fn` reports at most *recoverable* problems (orphan inode, unreferenced
  block, wrong free count), never a *structural* one (double-allocated block,
  pointer out of range, corrupt dirent chain).
- `e2fsck -fy` then produces a file system that `e2fsck -fn` accepts.
- No previously committed file lost data.

That third assertion is the one that matters, and it is the one that caught both
NVFS ordering bugs.

### 15.4 Targeted sets

- **Block map**: files at every boundary — 12, 13, 268, 269, 65,548 blocks. Sparse
  files. Truncate to each boundary from above and below.
- **Directories**: fill a block exactly; add an entry needing a split; delete first,
  middle and last entries in a block; add after deleting; long names; a directory
  spanning many blocks.
- **Allocators**: fill the volume to `ENOSPC`; exhaust inodes; allocate across a
  group boundary.
- **Compatibility**: images from `mke2fs` with `dir_index` on and off, `sparse_super`
  on and off, block sizes 1024/2048/4096 for the read path, revision 0 and 1.

---

## 16. Phasing proposal

Proposed gates. **These are not yet slotted into the v3 sequential 99-step plan** —
see OI-1.

| Gate | Content | Exit criterion |
|---|---|---|
| **F0** | L0–L1, MBR scan, host image harness | Read an arbitrary LBA on host and target; `u32_div16` and bitmap routines pass unit tests |
| **F1** | L2–L6, L8 read, L9 iterate/lookup, L10 | `cat` and `ls` any file on an `mke2fs` image; tree walk matches host `find` |
| **F2** | Buffer cache write-back, superblock state, `ext2_sync` | Mount RW, touch nothing, unmount; `e2fsck -fn` clean; `s_state` transitions correct |
| **F3** | L7 allocators, `bmap_alloc`, `ext2_write` | Create and write files of all sizes up to double-indirect; `e2fsck -fn` clean after each |
| **F4** | `ext2_truncate`, `ext2_create`, `ext2_unlink`, `dir_add`/`dir_remove` | Create/delete/truncate churn of 1,000 operations; `e2fsck -fn` clean |
| **F5** | `mkdir`, `rmdir`, `link`, `rename`, symlink read | Full tree manipulation; host mount agrees |
| **F6** | Crash injection suite | Every truncation point recoverable per §15.3 |
| **F7** | VFS unification with NVFS | Both file systems behind one syscall path |

F0–F5 are almost entirely host-side and are **not blocked** by the Helium SD
register map. Only the on-target validation of F1 onward is.

---

## 17. Effort and a recommendation

The read-only subset is L0–L6, L8 (read only), L9 (iterate and lookup only) and
L10 — gates F0 and F1. That is a well-bounded, low-risk piece of work with an
excellent effort-to-value ratio: it makes any Linux-formatted SD card readable by
noVa64.

The write path adds the allocators, `truncate`, `rec_len` manipulation, the
counter invariants, the entire ordering discipline of §11, and the crash-injection
campaign that validates it. Realistically **three to four times the work of the
read path**, and it carries the risk of destroying data rather than merely failing
to read it.

**Recommendation:** implement F0–F1 (read-only ext2) as the interoperability
mechanism, and keep NVFS as the read-write native file system. That combination
covers the actual use case — getting files from a host onto noVa64 — at roughly a
quarter of the cost and with none of the corruption risk. Revisit F2 onward only
if a concrete need for on-target writing to ext2 appears.

Whichever path is chosen: during bring-up the machine will hang frequently, and a
half-debugged read-write ext2 driver pointed at a card holding real data is a
reliable way to lose it. **Use a dedicated card with no data of value, and run
`e2fsck` on the host after every session.**

---

## 18. Open items

| ID | Item | Blocks |
|---|---|---|
| **OI-1** | Where do gates F0–F7 belong in the v3 sequential 99-step plan? ext2 is not currently in it, and inserting it before the Apple II milestone has a calendar cost that needs an explicit decision. | Scheduling |
| **OI-2** | Read-only versus read-write scope decision (§17). Everything downstream depends on it. | Effort estimate |
| **OI-3** | DN-FS-VFS-001 must be written before implementation, or NVFS's call surface will need rewriting later. | F7, and arguably F1 |
| **OI-4** | Pinned group descriptor table does not scale past ~4 GiB volumes. Needs an LRU sub-cache or a maximum supported volume size. | F2 |
| **OI-5** | Helium SD register map: does it support block-burst transfer into SDRAM without per-byte CPU involvement? This should be raised **before** that register map is frozen. | F0 on target; performance |
| **OI-6** | Timestamp policy — recommendation is constant zero (§13.4), needs confirmation. RTC decision is separate. | F3 |
| **OI-7** | Is `blk_flush` implementable, i.e. can the SD card's write cache be reliably flushed with the planned command set? If not, §11 provides no guarantees. | F2, crash consistency |
| **OI-8** | Resident versus loadable: ~19 KB of driver competes with the kernel for address space. | F1 |
| **OI-9** | Should `mkfs`-side formatting be constrained (documented `mke2fs` invocation) or should the driver handle arbitrary host-created volumes? Affects how much of §5 must be implemented. | F1 |

## 19. Closed decisions

| Decision | Rationale |
|---|---|
| ext2, not FAT32, for host interoperability | Little-endian, no LFN layer, real directory tree, clean small structure set |
| ext2 is a transfer medium; NVFS remains the native system file system | NVFS is specified, validated and implemented for this machine |
| No on-target `fsck`; `e2fsck` on the host is the recovery mechanism | Writing a correct fsck is a larger project than the driver itself |
| Write path restricted to 1024-byte blocks | `s_first_data_block` divergence, cache footprint, block-copy cost |
| Unknown INCOMPAT → refuse mount; unknown RO_COMPAT → read-only | Standard, and the only safe policy |
| `HAS_JOURNAL` → read-only | Journal replay is out of scope; mounting ext3 RW as ext2 requires verifying journal cleanliness |
| `DIR_INDEX` → clear `EXT2_INDEX_FL` on directory modification | Otherwise entries become invisible to host lookup while passing `e2fsck` |
| Triple indirect implemented | Same recursive walker as double indirect; omitting it saves nothing |
| Buffer cache is mandatory, minimum 8 buffers, in SDRAM | Without it a single `creat()` is 15–20 device transactions |
| In-memory inode table with refcounting is mandatory | Otherwise concurrent descriptors on one inode diverge silently |
| Governing invariant: allocated-but-unreferenced is acceptable; free-but-referenced is not | Determines every ordering rule in §11 |
| `i_atime` never updated on read | Turns reads into writes |
| No uid/gid/permission enforcement | No user model exists in noVa64 |
| Backup superblocks never written | `e2fsck` reconciles them |
| `_Static_assert` on every structure size and on `i_block` offset, on both host and Calypsi builds | A single padding byte silently corrupts every file touched |

---

*End of DN-FS-EXT2-001 Draft 1.*
