# DN-FS-FUSE-001 — noVa64 Filesystem Learning Track

**Host-side FUSE implementations of FAT32, EXT2 and NVFS**

| Field | Value |
|---|---|
| Document ID | DN-FS-FUSE-001 |
| Revision | 1.0 |
| Status | Draft for execution |
| Language | English (project convention) |
| Target platform | Ubuntu Linux, x86-64, C11 |
| Related documents | DN-FS-EXT2-001 (ext2 support layer), DN-FS-VFS-001 (VFS layer spec, *not yet written*), NVFS Specification draft 2 |
| Supersedes | — |
| Depends on | libfuse 3.x, dosfstools, e2fsprogs, existing `libnvfs` |

---

## 0. Document control

### 0.1 Purpose of this document

This is an execution plan, not a tutorial. It defines a strictly sequential
sequence of 107 steps grouped into ten gates (F0–F9). Each step states an
objective, a concrete deliverable, an acceptance criterion, and a difficulty
rating. The plan is written so that a step can be picked up cold: if the
previous step's acceptance criterion passed, the next step is well defined.

The document also carries the reference material needed to execute those
steps without leaving the document: on-disk field tables for FAT32 and EXT2,
the FUSE 3 API surface actually used, exact command lines, and code
skeletons for the layers that are shared across all three filesystems.

### 0.2 Relationship to the noVa64 project

This work is **host-side only**. It runs on a normal PC and produces three
FUSE daemons. It is not part of the E/K/G gate series and does not block the
Apple II or Amiga milestones. It exists for three reasons:

1. **Understanding.** Implementing a filesystem from the on-disk bytes upward
   is the only reliable way to know a filesystem rather than to have read
   about one.
2. **De-risking DN-FS-EXT2-001.** The ext2 support layer planned for noVa64
   is specified as a layered L0–L12 architecture with a read-only-first
   recommendation. Gate F4 of this plan is effectively a host-side prototype
   of L0–L8 of that note, executed where a debugger, sanitizers and
   `e2fsck -fn` are all available. Bugs found here are bugs not found on a
   65816 with a serial console.
3. **Feeding DN-FS-VFS-001.** The VFS layer spec is flagged as required before
   ext2 is implemented on noVa64, to avoid rewriting the NVFS call surface.
   Three independent filesystems behind one internal vtable is the cheapest
   possible way to discover what that vtable must look like. See §4.6.

**Non-goal:** this plan does not port anything to the 65816. Layer mapping for
that eventual port is documented in §4.7 so the code is written in a portable
shape, but no noVa64 work is scheduled here.

### 0.3 Closed decisions carried into this plan

These were settled before this document and are not reopened by it:

- Order is **FAT32 → EXT2 → NVFS**. Rationale in §2.
- Each filesystem gets a **read-only gate first**, then a **separate write
  gate**. No filesystem gets write support before all three are readable.
- **NVFS reuses the existing specification (draft 2) and `libnvfs`.** The FUSE
  layer is a binding, not a reimplementation. No new NVFS spec is written.
- Windows/macOS is an **optional final gate (F9)**, attempted only after F8.

---

## 1. Objectives and success criteria

### 1.1 Primary objective

Produce three working FUSE filesystem drivers in C, each able to mount a disk
image read-only and later read-write, each validated by differential testing
against the corresponding Linux kernel driver (or, for NVFS, against the
existing conformance and cross-implementation harnesses).

### 1.2 Learning objectives (the actual point)

By the end of F8 the following should be knowledge rather than reference
lookups:

- How a VFS request becomes a filesystem operation, and what the kernel
  caches on your behalf.
- The difference between a table-based allocation model (FAT) and an
  inode-plus-bitmap model (ext2), and what each costs.
- Why indirect block trees exist, where they hurt, and what extents replaced.
- Why write ordering is the hard part of a filesystem, and what a journal buys.
- Where your own NVFS design sits relative to two real designs, and which of
  its decisions look better or worse in that light.

### 1.3 Definition of done

The track is complete when:

- `mount.fat32fuse`, `mount.ext2fuse` and `mount.nvfsfuse` all mount and pass
  differential/conformance testing read-write.
- All three run clean under ASan + UBSan and under Valgrind.
- All three survive the corruption corpus without crash, hang or unbounded
  allocation.
- The crash-injection harness produces only images that `fsck` declares clean
  or trivially recoverable.
- The `fsops` vtable (§4.6) is stable enough to be lifted into DN-FS-VFS-001.

---

## 2. Ordering rationale

The order is not arbitrary. Each filesystem introduces exactly one new class
of difficulty, so that at every point there is one hard thing being learned
rather than three.

**FAT32 first.** FAT32's on-disk model is a reserved region, N copies of a
single allocation table, and a data region of fixed-size clusters. There are
no inodes, no permissions, no ownership, no hard links, no sparse files, and
file size is capped at 4 GiB − 1. Metadata is a flat 32-byte record. This
means the *only* genuinely new material in F3 is FUSE itself plus long
filename reassembly. It is the cheapest possible way to get a real filesystem
mounted.

The one non-trivial thing FAT32 teaches early is **synthesis**: FAT has no
inode numbers, no uid/gid and no mode bits, so the driver must invent them.
That forces an early, explicit understanding of what `struct stat` actually
means to the kernel — which is exactly the knowledge F4 then uses.

**EXT2 second.** EXT2 introduces, in one filesystem, the entire classical Unix
vocabulary: inodes, block and inode bitmaps, block groups, direct/indirect/
double-indirect/triple-indirect block trees, real permission bits, hard links,
sparse files, and fast versus slow symlinks. Every one of those maps almost
one-to-one onto a FUSE callback or a `struct stat` field, so having done F3
the *FUSE* side is free and full attention goes to the format. It is also the
filesystem OSTEP's "File System Implementation" chapter describes (vsfs is
essentially a simplified ext2), which makes the book directly usable.

**NVFS last.** By F5 there are two reference points to judge NVFS against, and
the format work is already done — spec draft 2 and `libnvfs` exist. So F5 is
purely about the *binding* problem: what API surface does a filesystem library
need to expose to be mountable, how do inode lifetimes work, and how do you
avoid duplicating logic between a FUSE front-end and existing host tools. That
is a different skill from format parsing and deserves its own gate rather than
being tangled up with learning what a superblock is.

**Write after all three reads.** Write support is where the interesting
failure modes live (ordering, crash consistency, allocation policy). Doing
all three read paths first means the write gates can be compared against each
other while the read code is still fresh, and it means a bad architectural
decision in the shared layers is discovered before it has been baked into
three write paths.

---

## 3. Gate series overview

| Gate | Name | Steps | Exit condition |
|---|---|---|---|
| **F0** | Environment and lab | 1–6 | Images build, kernel reference mounts, `make` works |
| **F1** | FUSE mechanics | 7–13 | Own read-only in-memory FS mounts, both APIs understood |
| **F2** | Shared infrastructure | 14–21 | Block layer + harnesses tested, no FS logic yet |
| **F3** | FAT32 read-only | 22–37 | Differential match vs kernel `vfat`; corruption-safe |
| **F4** | EXT2 read-only | 38–55 | Differential match vs kernel `ext2`; corruption-safe |
| **F5** | NVFS read-only binding | 56–65 | Passes NVFS conformance read subset + agreement harness |
| **F6** | FAT32 write | 66–77 | `fsx` clean, `fsck.fat` clean, crash-injection clean |
| **F7** | EXT2 write | 78–91 | `fsx` clean, `e2fsck -fn` clean, crash-injection clean |
| **F8** | NVFS write + harness | 92–99 | Full 2 350-check conformance; 3rd agreement participant |
| **F9** | Windows / macOS (optional) | 100–107 | FAT32 read-only mounts on Windows via WinFsp |

**Dependency chain:** strictly linear F0 → F1 → … → F9. No parallelism. This
matches the project-wide preference for a single clear thread of progress and
accepts the calendar cost.

### 3.1 Difficulty scale

| Rating | Meaning |
|---|---|
| ★ | Mechanical. Follow the commands. |
| ★★ | Straightforward but needs care; one new concept. |
| ★★★ | Real design work or fiddly arithmetic; expect debugging. |
| ★★★★ | Hard. Expect a full session or more, and expect to be wrong once. |
| ★★★★★ | The genuinely difficult parts. Ordering, crash consistency, lifetimes. |

---

## 4. Shared architecture

All three drivers use the same four-layer split. This is deliberate: it is the
same shape as `libnvfs`, it is what makes the differential harness reusable,
and it is what makes a future 65816 port a bottom-layer replacement rather
than a rewrite.

```
  +---------------------------------------------------------------+
  |  L3  FUSE binding        fat32fuse.c / ext2fuse.c / nvfsfuse.c |
  |      struct fuse_operations  or  struct fuse_lowlevel_ops      |
  |      errno mapping, mount options, session lifecycle           |
  +---------------------------------------------------------------+
  |  L2  Filesystem logic    fat32/*.c  ext2/*.c  (libnvfs for NVFS)|
  |      path resolution, dir iteration, block mapping, allocation |
  +---------------------------------------------------------------+
  |  L1  On-disk decoding    fat32_ondisk.c / ext2_ondisk.c        |
  |      field accessors, validation, endian handling. NO I/O.     |
  +---------------------------------------------------------------+
  |  L0  Block device        bdev.c                                |
  |      bdev_read / bdev_write over a file, loop dev or raw dev   |
  +---------------------------------------------------------------+
```

### 4.1 Rules that make the split real

These are enforced by review, and by the fact that L1 has no `#include` of
anything that does I/O:

1. **L1 never performs I/O.** It takes a `const uint8_t *` buffer and an
   offset and returns decoded values. This makes L1 unit-testable against a
   static byte array with no filesystem and no mount.
2. **L2 never touches FUSE types.** No `struct fuse_file_info`, no
   `fuse_ino_t`, no `-errno` returns leaking upward as FUSE semantics. L2 has
   its own error enum; L3 maps it.
3. **L3 contains no arithmetic.** If a cluster-to-LBA calculation appears in
   the FUSE binding, it is in the wrong layer.
4. **L0 is the only layer that knows the backing store is a file.**

### 4.2 Endianness and struct discipline

FAT32 and EXT2 are both little-endian on disk, and the development host is
little-endian. It is therefore tempting to declare
`struct __attribute__((packed)) fat_bpb { ... };`, cast the sector buffer to
it, and read fields directly. **Do not do this.** Three reasons:

- **Alignment.** Taking the address of a member of a packed struct produces an
  unaligned pointer; passing it anywhere is undefined behaviour, and UBSan
  will (correctly) complain. Some of the offsets involved genuinely are
  unaligned — `BPB_BytsPerSec` at offset 11 is a 16-bit field on an odd byte.
- **Portability.** The eventual noVa64 target is a different compiler
  (Calypsi) with 16-bit `int` and 24-bit pointers. Explicit accessors port;
  packed-struct casts do not.
- **Auditability.** An explicit `rd16(sec + 11)` next to a table of offsets is
  checkable against the specification line by line. A struct definition hides
  a silent padding bug behind a `sizeof` that happens to be right.

Use bounds-checked byte accessors instead:

```c
/* endian.h — explicit little-endian accessors, alignment-safe */
#ifndef NOVA_ENDIAN_H
#define NOVA_ENDIAN_H
#include <stdint.h>

static inline uint8_t  rd8 (const uint8_t *p) { return p[0]; }

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t rd32(const uint8_t *p)
{
    return  (uint32_t)p[0]        | ((uint32_t)p[1] <<  8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static inline void wr8 (uint8_t *p, uint8_t  v) { p[0] = v; }
static inline void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8);
}
static inline void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);        p[1] = (uint8_t)(v >>  8);
    p[2] = (uint8_t)(v >> 16);  p[3] = (uint8_t)(v >> 24);
}
#endif
```

Field offsets live in a header as named constants, one per specification line,
so the code reads as a direct transcription of the tables in §A and §B:

```c
/* fat32_ondisk.h — offsets, not structs */
#define BPB_BytsPerSec_OFF   11
#define BPB_SecPerClus_OFF   13
#define BPB_RsvdSecCnt_OFF   14
#define BPB_NumFATs_OFF      16
/* ... */
```

### 4.3 The block layer (L0)

```c
/* bdev.h */
#ifndef NOVA_BDEV_H
#define NOVA_BDEV_H
#include <stdint.h>
#include <stddef.h>

typedef struct bdev {
    int       fd;
    uint32_t  sector_size;   /* logical unit of transfer, bytes */
    uint64_t  sector_count;  /* total addressable sectors       */
    int       read_only;
    uint64_t  reads, writes; /* instrumentation for the harness */
} bdev_t;

int  bdev_open (bdev_t *bd, const char *path, uint32_t sector_size, int rw);
void bdev_close(bdev_t *bd);

/* Both return 0 on success, negative bdev error code on failure.
 * Reads/writes are whole sectors; partial access is the caller's job. */
int  bdev_read (bdev_t *bd, uint64_t lba, void *buf, uint32_t nsec);
int  bdev_write(bdev_t *bd, uint64_t lba, const void *buf, uint32_t nsec);
int  bdev_flush(bdev_t *bd);

#define BDEV_OK        0
#define BDEV_EIO      -1
#define BDEV_ERANGE   -2
#define BDEV_EROFS    -3
#endif
```

Notes:

- `bdev_read` **must** range-check `lba + nsec` against `sector_count` and
  return `BDEV_ERANGE` rather than short-reading. Every corrupt-image hang
  found in practice starts with an unchecked read of a garbage block number.
- Use `pread`/`pwrite`, not `lseek` + `read`. This keeps L0 thread-safe if the
  multi-threaded FUSE loop is ever enabled.
- The `reads`/`writes` counters exist so that the crash-injection harness in
  F6–F8 can hook a specific write number.
- On the 65816 this file is replaced wholesale by the Helium SD block driver.
  Nothing above it changes. Sector size is a runtime field, not a `#define`,
  for exactly that reason.

### 4.4 Error model

L2 uses its own error enum so that filesystem semantics do not get confused
with POSIX semantics too early:

```c
typedef enum {
    FS_OK = 0,
    FS_ENOENT,      /* name not found                         */
    FS_ENOTDIR,     /* path component is not a directory      */
    FS_EISDIR,
    FS_EIO,         /* backing store failure                  */
    FS_ECORRUPT,    /* on-disk structure violates the format  */
    FS_ENOSPC,
    FS_EROFS,
    FS_EEXIST,
    FS_ENOTEMPTY,
    FS_ENAMETOOLONG,
    FS_EINVAL,
    FS_ENOTSUP,     /* format feature this driver refuses     */
    FS_ELOOP,       /* symlink or structural cycle            */
} fs_err_t;
```

Mapping at L3 is a single table. `FS_ECORRUPT` maps to `-EIO`, and **always**
logs, because a corrupt-image return that silently looks like an empty
directory is the single most misleading failure mode in this kind of code.

### 4.5 Repository layout

```
novafs/
  Makefile
  common/
    bdev.c bdev.h
    endian.h
    logging.c logging.h
    fsops.h            <- the internal vtable, see 4.6
    fuse_common.c      <- shared option parsing, session setup
  fat32/
    fat32_ondisk.h fat32_ondisk.c   (L1)
    fat32_fat.c fat32_dir.c fat32_lfn.c fat32_alloc.c (L2)
    fat32_fs.c fat32_fs.h
    fat32fuse.c                     (L3)
  ext2/
    ext2_ondisk.h ext2_ondisk.c     (L1)
    ext2_sb.c ext2_inode.c ext2_blockmap.c ext2_dir.c ext2_alloc.c (L2)
    ext2_fs.c ext2_fs.h
    ext2fuse.c                      (L3)
  nvfs/
    nvfs_fuse_adapter.c nvfs_fuse_adapter.h
    nvfsfuse.c                      (L3, low-level API)
  tests/
    unit/            <- L1 unit tests, no mount required
    images/          <- generated, gitignored
    scripts/
      mkimages.sh difftest.sh crashinject.sh corrupt.sh
    corpus/          <- fuzzing seeds and known-bad images
  docs/
    notes/           <- per-gate findings, kept as you go
```

### 4.6 The `fsops` vtable — prototype for DN-FS-VFS-001

This is the highest-value artefact of the whole track. Three filesystems
implemented behind one interface will reveal the interface's real shape far
better than designing it up front. Start with the minimum below and let F3–F5
force changes; record every change and the reason.

```c
/* fsops.h — internal filesystem interface. NOT a FUSE interface.
 * This deliberately uses 32-bit inode numbers and an explicit context
 * pointer so it can survive the move to a 16-bit-int, 24-bit-pointer
 * target without redesign. */

typedef uint32_t fs_ino_t;
#define FS_INO_ROOT 1u

typedef struct {
    fs_ino_t  ino;
    uint32_t  mode;        /* POSIX-style, synthesised if the FS lacks one */
    uint32_t  nlink;
    uint32_t  uid, gid;
    uint64_t  size;
    uint64_t  blocks;      /* 512-byte units, POSIX convention             */
    int64_t   atime, mtime, ctime;
} fs_attr_t;

typedef struct fs_dirent {
    fs_ino_t  ino;
    uint8_t   type;        /* DT_* style */
    uint16_t  name_len;
    char      name[256];
} fs_dirent_t;

typedef struct fs_dir_cursor fs_dir_cursor_t;   /* opaque, FS-defined */
typedef struct fs_file       fs_file_t;         /* opaque, FS-defined */

typedef struct fsops {
    const char *name;

    fs_err_t (*mount)   (void *ctx, bdev_t *bd, unsigned flags);
    fs_err_t (*unmount) (void *ctx);
    fs_err_t (*statfs)  (void *ctx, uint64_t *blocks, uint64_t *bfree,
                         uint64_t *files, uint64_t *ffree, uint32_t *bsize);

    fs_err_t (*getattr) (void *ctx, fs_ino_t ino, fs_attr_t *out);
    fs_err_t (*lookup)  (void *ctx, fs_ino_t parent,
                         const char *name, fs_ino_t *out);
    fs_err_t (*readlink)(void *ctx, fs_ino_t ino, char *buf, size_t bufsz);

    fs_err_t (*opendir) (void *ctx, fs_ino_t ino, fs_dir_cursor_t **out);
    fs_err_t (*readdir) (void *ctx, fs_dir_cursor_t *c, fs_dirent_t *out);
    fs_err_t (*closedir)(void *ctx, fs_dir_cursor_t *c);

    fs_err_t (*open)    (void *ctx, fs_ino_t ino, int flags, fs_file_t **out);
    fs_err_t (*read)    (void *ctx, fs_file_t *f, void *buf,
                         size_t len, uint64_t off, size_t *got);
    fs_err_t (*close)   (void *ctx, fs_file_t *f);

    /* Phase 2 (F6-F8) — left NULL until then */
    fs_err_t (*write)   (void *ctx, fs_file_t *f, const void *buf,
                         size_t len, uint64_t off, size_t *put);
    fs_err_t (*create)  (void *ctx, fs_ino_t parent, const char *name,
                         uint32_t mode, fs_ino_t *out);
    fs_err_t (*mkdir)   (void *ctx, fs_ino_t parent, const char *name,
                         uint32_t mode, fs_ino_t *out);
    fs_err_t (*unlink)  (void *ctx, fs_ino_t parent, const char *name);
    fs_err_t (*rmdir)   (void *ctx, fs_ino_t parent, const char *name);
    fs_err_t (*rename)  (void *ctx, fs_ino_t op, const char *on,
                                    fs_ino_t np, const char *nn);
    fs_err_t (*truncate)(void *ctx, fs_ino_t ino, uint64_t size);
    fs_err_t (*setattr) (void *ctx, fs_ino_t ino,
                         const fs_attr_t *in, unsigned mask);
    fs_err_t (*sync)    (void *ctx);
} fsops_t;
```

Deliberate choices worth noting now, because they are the ones DN-FS-VFS-001
will have to ratify or reject:

- **Inode-keyed, not path-keyed.** Path-keyed is simpler for FAT but does not
  survive hard links, and the noVa64 kernel will want inode identity anyway.
  FAT32 therefore has to synthesise inode numbers (step 27), which is the
  right forcing function.
- **Explicit cursor object for readdir** rather than an offset. FAT32 and
  ext2 both want to keep decoding state between calls; an opaque cursor lets
  each keep what it needs.
- **`fs_attr_t` is POSIX-shaped.** FAT has to synthesise most of it. That is
  accepted: the alternative — a union of per-filesystem attribute types —
  pushes the synthesis into every caller.
- **No `void *` payload in `fs_dirent_t`.** Fixed 256-byte name inline.
  Wasteful on the host, correct on a machine without a heap worth using.

### 4.7 Layer mapping to a future noVa64 port

Not scheduled here, recorded so the code is written in the right shape.

| Layer | Host | noVa64 |
|---|---|---|
| L0 `bdev` | `pread`/`pwrite` on a file | Helium SD block registers; see open item OI-5 on burst transfer |
| L1 on-disk | Portable C11, no allocation | Portable; `int` is 16-bit under Calypsi — audit every implicit int promotion |
| L2 logic | malloc-based caches | Static pools; recursion depth bounded |
| L3 FUSE | libfuse | Replaced by the kernel VFS built from `fsops` |

Concrete portability rules to follow from step 1, so they never have to be
retrofitted:

- Never rely on `int` being 32 bits. Use `uint32_t`/`int32_t` explicitly for
  anything holding a block, cluster or inode number.
- No recursion whose depth is a function of on-disk data. Directory tree
  descent and indirect-block traversal are both iterative or depth-bounded.
- Allocation is confined to L2 and goes through one wrapper, so it can later
  be swapped for a pool allocator.
- No `long`, no `size_t` in on-disk arithmetic.

---

## 5. Execution plan — 107 sequential steps

Format for every step:

> **N. Title** — *Difficulty*
> **Objective:** what this step is for.
> **Deliverable:** the artefact that exists afterwards.
> **Acceptance:** the observable check that says it is done.

---

### Gate F0 — Environment and lab (steps 1–6)

**Gate intent:** nothing filesystem-specific. When F0 exits you can produce a
formatted image, mount it with the kernel driver, and build an empty project.

---

> **1. Install the toolchain** — ★
> **Objective:** get libfuse 3, the mkfs tools and the introspection tools.
> **Deliverable:** a provisioning script `tests/scripts/provision.sh`.
> **Acceptance:** `pkg-config --modversion fuse3` prints ≥ 3.10.

```bash
sudo apt update
sudo apt install -y \
    build-essential pkg-config git \
    fuse3 libfuse3-dev \
    dosfstools mtools \
    e2fsprogs \
    valgrind gdb \
    xxd bsdextrautils

pkg-config --modversion fuse3
pkg-config --cflags --libs fuse3     # -I/usr/include/fuse3 -lfuse3
fusermount3 --version
```

Note: libfuse in the Ubuntu archive lags upstream. Anything ≥ 3.10 is fine for
this plan; the 3.18 features (io_uring transport, statx) are explicitly out of
scope. If you want the newest, build from source with Meson — but do not, on
the first pass. A distribution package removes one variable.

---

> **2. Verify FUSE permissions and mount plumbing** — ★
> **Objective:** confirm an unprivileged user can mount and unmount, and know
> where the escape hatches are when a mount goes stale.
> **Deliverable:** notes in `docs/notes/F0-fuse-env.md`.
> **Acceptance:** you can mount, list and unmount `hello` as your own user.

```bash
ls -l /dev/fuse                       # crw-rw-rw- root root
ls -l $(which fusermount3)            # -rwsr-xr-x root root  (setuid)
grep -n user_allow_other /etc/fuse.conf
```

Uncomment `user_allow_other` in `/etc/fuse.conf` now. You will need it the
first time you try `sudo ls /mnt/fuse` and get "Permission denied" — by
default only the mounting user can see a FUSE mount, which is correct
behaviour and surprising the first time.

Escape hatches, write these down:

```bash
fusermount3 -u  /mnt/fuse           # normal unmount
fusermount3 -uz /mnt/fuse           # lazy, for a wedged daemon
ls /sys/fs/fuse/connections/        # find the connection number
echo 1 | sudo tee /sys/fs/fuse/connections/NN/abort   # force-fail all I/O
```

---

> **3. Repository skeleton and build system** — ★
> **Objective:** a Makefile that builds nothing, correctly.
> **Deliverable:** the tree from §4.5 with a working `make`.
> **Acceptance:** `make` and `make asan` both succeed on an empty `main`.

```make
# Makefile
CC       ?= gcc
FUSE_CFLAGS := $(shell pkg-config --cflags fuse3)
FUSE_LIBS   := $(shell pkg-config --libs   fuse3)

CFLAGS  := -std=c11 -O2 -g -Wall -Wextra -Wpedantic \
           -Wshadow -Wconversion -Wsign-conversion \
           -Wpointer-arith -Wcast-align \
           -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=31 \
           -Icommon $(FUSE_CFLAGS)
LDFLAGS := $(FUSE_LIBS)

SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all asan clean test
all:  fat32fuse ext2fuse nvfsfuse
asan: CFLAGS += $(SAN)
asan: LDFLAGS += $(SAN)
asan: all
```

`-Wconversion` and `-Wsign-conversion` are not optional here. Most on-disk
parsing bugs are silent narrowing or sign-extension bugs, and these two flags
catch a large share of them at compile time. They are noisy at first; fix the
noise rather than disabling them, because the same discipline is what keeps
the code correct under Calypsi's 16-bit `int`.

---

> **4. Image generation scripts** — ★★
> **Objective:** reproducible test images, deterministically populated.
> **Deliverable:** `tests/scripts/mkimages.sh`.
> **Acceptance:** running it twice produces byte-identical images.

Critical constraint, which bites everyone once: **FAT type is determined
solely by cluster count.** A volume is FAT32 only if the data region holds at
least 65 525 clusters. A 64 MiB image with 4 KiB clusters yields ~16 000
clusters and `mkfs.vfat -F 32` will refuse or produce something unusual. Size
the image so you are comfortably clear of the 4 085 / 65 525 boundaries.

```bash
#!/usr/bin/env bash
set -euo pipefail
IMG=tests/images
mkdir -p "$IMG"

# ---- FAT32: 512 MiB, 512-byte sectors, 8 sectors/cluster (4 KiB clusters)
#      -> 512 MiB / 4 KiB ~= 131 000 clusters, safely above 65 525
truncate -s 512M "$IMG/fat32.img"
mkfs.vfat -F 32 -S 512 -s 8 -n NOVA64 "$IMG/fat32.img"

# ---- FAT32 small-cluster variant: exercises long chains
truncate -s 64M "$IMG/fat32-small.img"
mkfs.vfat -F 32 -S 512 -s 1 -n NOVASMALL "$IMG/fat32-small.img"

# ---- EXT2: true ext2 only. No journal, no htree, no ext4 features.
truncate -s 128M "$IMG/ext2.img"
mke2fs -q -F -t ext2 -b 1024 -I 128 \
       -O none,filetype,sparse_super \
       -L NOVA64 "$IMG/ext2.img"

# ---- EXT2 4 KiB-block variant: different indirect-block fanout
truncate -s 256M "$IMG/ext2-4k.img"
mke2fs -q -F -t ext2 -b 4096 -I 256 \
       -O none,filetype,sparse_super \
       -L NOVA4K "$IMG/ext2-4k.img"
```

Why each flag matters:

- `-O none,filetype,sparse_super` clears every default feature and re-adds
  only two. Without this, modern `mke2fs` enables `dir_index` (htree
  directories), `ext_attr`, `resize_inode` and possibly others, and your
  reader will meet structures it was not built for. `filetype` is kept
  because it is the only INCOMPAT feature the Linux ext2 driver itself
  supports, and it makes `readdir` able to return a `d_type` without reading
  every inode.
- `-b 1024` puts `s_first_data_block` at 1, which is the awkward case. Build
  the awkward case first; the 4 KiB variant (where it is 0) is then a free
  regression test of your arithmetic.
- `-I 128` gives the classic 128-byte inode; the 4 KiB image uses 256 to force
  the code to honour `s_inode_size` rather than assuming.

---

> **5. Reference-mount harness** — ★★
> **Objective:** be able to populate an image and mount it with the *kernel*
> driver, which is the ground truth for every later comparison.
> **Deliverable:** `tests/scripts/refmount.sh` and a deterministic populator.
> **Acceptance:** `find /mnt/ref | sort` is identical across two runs.

```bash
#!/usr/bin/env bash
# refmount.sh <image> <mountpoint>
set -euo pipefail
IMGFILE=$1; MNT=$2
sudo mkdir -p "$MNT"
LOOP=$(sudo losetup --find --show -P "$IMGFILE")
sudo mount "$LOOP" "$MNT"
echo "$LOOP"
```

The populator must be deterministic — fixed content, fixed order, fixed
timestamps — or differential testing will produce false failures:

```bash
populate() {                    # populate <mountpoint>
  local M=$1
  sudo mkdir -p "$M/dir_a/dir_b/dir_c"
  printf 'hello\n'                  | sudo tee "$M/small.txt"    >/dev/null
  head -c 4095  /dev/zero           | sudo tee "$M/just_under.bin" >/dev/null
  head -c 4096  /dev/zero           | sudo tee "$M/exactly.bin"    >/dev/null
  head -c 4097  /dev/zero           | sudo tee "$M/just_over.bin"  >/dev/null
  # crosses single -> double indirect on a 1 KiB ext2 (12 + 256 blocks)
  head -c 300000 /dev/urandom       | sudo tee "$M/indirect.bin"   >/dev/null
  # crosses double -> triple indirect on a 1 KiB ext2
  head -c 70000000 /dev/urandom     | sudo tee "$M/triple.bin"     >/dev/null
  sudo sh -c "echo x > '$M/a name with spaces and a very long tail.txt'"
  sudo sh -c ": > '$M/empty'"
  sudo find "$M" -exec touch -d '2020-01-02 03:04:05' {} +
}
```

The four sizes around the block boundary are not padding: off-by-one errors in
block mapping almost always show up exactly there. `indirect.bin` and
`triple.bin` sizes are chosen for a 1 KiB-block ext2, where 256 pointers fit
in a block: 12 direct + 256 single = 268 blocks ≈ 274 KiB before double
indirect starts, and double indirect covers a further 256×256 blocks ≈ 64 MiB
before triple. Recompute both for the 4 KiB image (1024 pointers per block).

For ext2 add the cases FAT cannot express, and skip them for FAT:

```bash
  sudo ln    "$M/small.txt" "$M/hardlink.txt"        # ext2 only
  sudo ln -s small.txt      "$M/fastlink"            # short -> inline target
  sudo ln -s "$(python3 -c 'print("d/"*40 + "target")')" "$M/slowlink"
  sudo dd if=/dev/zero of="$M/sparse.bin" bs=1 count=1 seek=1000000 2>/dev/null
```

---

> **6. GATE F0 EXIT** — ★
> **Objective:** confirm the lab works end to end.
> **Deliverable:** `docs/notes/F0-exit.md` recording tool versions.
> **Acceptance:** all four of the following succeed:
> (a) `mkimages.sh` runs clean;
> (b) both images mount with the kernel driver and populate;
> (c) `fsck.fat -n -v` and `e2fsck -fn` both report a clean filesystem;
> (d) `make` builds an empty target.

```bash
fsck.fat -n -v tests/images/fat32.img
e2fsck -fn      tests/images/ext2.img
dumpe2fs -h     tests/images/ext2.img
```

Record the `dumpe2fs -h` output verbatim in the notes. You will read it a
hundred times during F4 and having a known-good baseline saved is worth the
thirty seconds.

---

### Gate F1 — FUSE mechanics (steps 7–13)

**Gate intent:** learn FUSE with zero on-disk complexity. Every filesystem in
this gate is synthetic. When F1 exits, no FUSE surprise should remain.

---

> **7. Build and run the upstream examples** — ★
> **Objective:** see a working FUSE filesystem before writing one.
> **Deliverable:** locally built `hello`, `hello_ll` and `passthrough`.
> **Acceptance:** `cat /mnt/fuse/hello` prints the expected string.

```bash
git clone https://github.com/libfuse/libfuse
cd libfuse && meson setup build && cd build && ninja
mkdir -p /tmp/mnt
./example/hello -f -s /tmp/mnt &      # -f foreground, -s single-threaded
ls -l /tmp/mnt ; cat /tmp/mnt/hello
fusermount3 -u /tmp/mnt
```

Adopt `-f -s` as the default for the entire track. Foreground keeps `printf`
debugging usable and lets Ctrl-C work; single-threaded removes concurrency as
a variable and is what makes Valgrind and ASan reports readable. Re-enable
threading only in F8 as a deliberate experiment.

---

> **8. Trace the protocol** — ★★
> **Objective:** understand what the kernel actually asks for, and when.
> **Deliverable:** `docs/notes/F1-request-trace.md`.
> **Acceptance:** you can answer, from the trace: how many `GETATTR` calls
> does `ls -l` on a 3-entry directory generate, and why?

```bash
./example/hello -f -s -d /tmp/mnt      # -d implies -f and dumps every op
```

Run each of `ls`, `ls -l`, `cat`, `stat`, `cp` against the mount and write
down the request sequence. Things worth noticing and recording:

- `FUSE_INIT` happens once, before your `init()`, and negotiates the protocol
  version and capability flags.
- `LOOKUP` is issued per path component and its result is cached by the kernel
  for `entry_timeout` seconds. A second `ls` may generate almost no traffic.
- `GETATTR` is issued far more often than expected. It is the hottest callback
  in almost every filesystem, which is why it is worth making cheap.
- `OPEN` returns a handle you choose; `READ` carries that handle back. If you
  put a pointer in `fi->fh`, the kernel hands it back to you verbatim.
- `RELEASE` is asynchronous and its return value is ignored. Never report an
  error from `release` and expect anyone to hear it.

---

> **9. Write `nullfs` — high-level API, from scratch** — ★★
> **Objective:** own the boilerplate rather than copying it.
> **Deliverable:** `tests/nullfs/nullfs.c` — a read-only in-memory tree.
> **Acceptance:** `ls -lR`, `cat` and `stat` all behave; `-o ro` enforced.

Write it without looking at `hello.c` beyond the first attempt. The static
tree should have at least: a root, one subdirectory, two files of different
sizes, and a symlink — enough to exercise `readlink`.

```c
#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <string.h>
#include <errno.h>

static void *nf_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void)conn;
    cfg->kernel_cache    = 1;   /* content never changes underneath us   */
    cfg->attr_timeout    = 300;
    cfg->entry_timeout   = 300;
    cfg->negative_timeout= 300;
    cfg->use_ino         = 1;   /* we supply real st_ino values          */
    return NULL;
}

static int nf_getattr(const char *path, struct stat *st,
                      struct fuse_file_info *fi)
{
    (void)fi;
    memset(st, 0, sizeof *st);
    const node_t *n = lookup(path);
    if (!n) return -ENOENT;
    st->st_ino   = n->ino;
    st->st_mode  = n->mode;
    st->st_nlink = n->nlink;
    st->st_size  = (off_t)n->size;
    st->st_uid   = fuse_get_context()->uid;
    st->st_gid   = fuse_get_context()->gid;
    return 0;
}

static int nf_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t off, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags)
{
    (void)off; (void)fi; (void)flags;
    const node_t *d = lookup(path);
    if (!d)                       return -ENOENT;
    if (!S_ISDIR(d->mode))        return -ENOTDIR;
    filler(buf, ".",  NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    for (const node_t *c = d->child; c; c = c->next)
        filler(buf, c->name, NULL, 0, 0);
    return 0;
}

static int nf_open(const char *path, struct fuse_file_info *fi)
{
    const node_t *n = lookup(path);
    if (!n)                              return -ENOENT;
    if (S_ISDIR(n->mode))                return -EISDIR;
    if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EROFS;
    fi->fh = (uint64_t)(uintptr_t)n;     /* handed back on every read */
    return 0;
}

static int nf_read(const char *path, char *buf, size_t size, off_t off,
                   struct fuse_file_info *fi)
{
    (void)path;
    const node_t *n = (const node_t *)(uintptr_t)fi->fh;
    if ((uint64_t)off >= n->size) return 0;             /* EOF */
    size_t avail = n->size - (uint64_t)off;
    size_t want  = size < avail ? size : avail;
    memcpy(buf, n->data + off, want);
    return (int)want;                                    /* bytes, not 0! */
}

static const struct fuse_operations nf_ops = {
    .init     = nf_init,
    .getattr  = nf_getattr,
    .readdir  = nf_readdir,
    .open     = nf_open,
    .read     = nf_read,
    .readlink = nf_readlink,
    .statfs   = nf_statfs,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &nf_ops, NULL);
}
```

Four mistakes to make deliberately, once each, so you recognise them later:

1. Return `+ENOENT` instead of `-ENOENT` from `getattr` and watch the kernel
   interpret it as success. This is the single most common FUSE bug.
2. Return `0` from `read` when data is available and watch every file appear
   empty. `read` returns a byte count, not a status.
3. Report an `st_size` larger than what `read` will supply, and watch the
   tail of every file become zeros — the kernel trusts `getattr`.
4. Omit `.` and `..` from `readdir` and watch `find` behave strangely.

---

> **10. Write `nullfs_ll` — low-level API** — ★★★
> **Objective:** learn inode identity and the lookup-count contract, which is
> the part of FUSE that has no analogue in the high-level API.
> **Deliverable:** `tests/nullfs/nullfs_ll.c`.
> **Acceptance:** mounts, behaves identically to `nullfs`, and a deliberate
> `forget` accounting bug is observable as ESTALE.

The contract, stated precisely because getting it wrong produces bugs that
appear hours later:

- Every successful `lookup` (and `create`, `mknod`, `mkdir`, `symlink`, `link`)
  increments the kernel's reference count on that inode by one. You must
  track it.
- `forget(ino, nlookup)` tells you to decrement by `nlookup`, not by one.
- An inode may only be freed when its count reaches zero. Freeing early gives
  `ESTALE`; never freeing is a leak.
- `FUSE_ROOT_ID` is 1 and the root's count is not tracked; never free it.
- The kernel may `forget` in bulk at unmount, and may not forget at all before
  the session ends. Handle `destroy` by dropping everything.

```c
#define FUSE_USE_VERSION 31
#include <fuse_lowlevel.h>

static void nl_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    struct fuse_entry_param e;
    node_t *n = find_child(parent, name);
    if (!n) { fuse_reply_err(req, ENOENT); return; }

    memset(&e, 0, sizeof e);
    e.ino           = n->ino;
    e.generation    = 1;
    e.attr_timeout  = 300.0;
    e.entry_timeout = 300.0;
    fill_stat(&e.attr, n);
    n->lookup_count++;                  /* <-- the contract */
    fuse_reply_entry(req, &e);
}

static void nl_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup)
{
    node_t *n = node_of(ino);
    if (n && ino != FUSE_ROOT_ID) {
        n->lookup_count -= nlookup;     /* by nlookup, not by 1 */
        if (n->lookup_count == 0) maybe_free(n);
    }
    fuse_reply_none(req);               /* note: reply_none, not reply_err */
}

static void nl_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                       off_t off, struct fuse_file_info *fi)
{
    (void)fi;
    char  *buf = malloc(size);
    size_t pos = 0;
    for (entry_t *e = entry_at(ino, off); e; e = e->next, off++) {
        struct stat st = { .st_ino = e->ino, .st_mode = e->mode };
        size_t need = fuse_add_direntry(req, NULL, 0, e->name, NULL, 0);
        if (pos + need > size) break;
        pos += fuse_add_direntry(req, buf + pos, size - pos,
                                 e->name, &st, off + 1);
    }
    fuse_reply_buf(req, buf, pos);       /* pos == 0 means end of directory */
    free(buf);
}
```

Session setup, which replaces `fuse_main`:

```c
int main(int argc, char *argv[])
{
    struct fuse_args           args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_cmdline_opts   opts;
    struct fuse_session       *se;
    int                        ret = 1;

    if (fuse_parse_cmdline(&args, &opts) != 0) return 1;

    se = fuse_session_new(&args, &nl_ops, sizeof nl_ops, NULL);
    if (!se) goto out1;
    if (fuse_set_signal_handlers(se) != 0) goto out2;
    if (fuse_session_mount(se, opts.mountpoint) != 0) goto out3;

    fuse_daemonize(opts.foreground);
    ret = fuse_session_loop(se);        /* single-threaded */

    fuse_session_unmount(se);
out3: fuse_remove_signal_handlers(se);
out2: fuse_session_destroy(se);
out1: free(opts.mountpoint);
      fuse_opt_free_args(&args);
    return ret ? 1 : 0;
}
```

---

> **11. Caching and timeout experiments** — ★★
> **Objective:** know what the kernel caches, so later performance and
> correctness surprises are explicable.
> **Deliverable:** a table in `docs/notes/F1-caching.md`.
> **Acceptance:** you can predict, before running, how many `GETATTR` and
> `READ` requests a given command will produce under a given configuration.

Vary, one at a time, and record request counts from `-d`:

| Knob | Effect to observe |
|---|---|
| `cfg->attr_timeout = 0` vs `300` | `GETATTR` frequency on repeated `stat` |
| `cfg->entry_timeout = 0` vs `300` | `LOOKUP` frequency on repeated `ls` |
| `cfg->negative_timeout` | repeated `stat` of a *nonexistent* path |
| `cfg->kernel_cache = 1` | `READ` on the second `cat` of the same file |
| `fi->direct_io = 1` | `READ` sizes and page-cache bypass |
| `fi->keep_cache = 1` | cache retention across `open` |

Conclusion to write down explicitly, because it drives F3/F4 configuration: a
read-only mount of a static image file can safely use long timeouts and
`kernel_cache`, because nothing can change underneath the driver. A read-write
mount cannot, and F6/F7 will have to revisit every one of these settings.

---

> **12. Sanitizer and Valgrind baseline** — ★★
> **Objective:** establish that the tooling works on a FUSE daemon *before*
> there is real code to blame.
> **Deliverable:** `make asan` target verified; a Valgrind recipe in the notes.
> **Acceptance:** a deliberately introduced 1-byte heap overflow in `nullfs`
> is reported by ASan with a usable stack trace.

```bash
make asan
./nullfs -f -s /tmp/mnt &            # -s is what makes the report readable
valgrind --leak-check=full --track-origins=yes ./nullfs -f -s /tmp/mnt
```

Caveats worth recording: FUSE daemons that `fork` confuse both tools, so
always pass `-f`; ASan's leak check fires at process exit, which for a FUSE
daemon means after unmount, so unmount cleanly rather than killing the process.

---

> **13. GATE F1 EXIT** — ★
> **Objective:** confirm FUSE holds no remaining mysteries.
> **Acceptance:** all of:
> (a) `nullfs` and `nullfs_ll` both mount and behave identically under
> `ls -lR`, `cat`, `stat`, `find`;
> (b) both are clean under ASan/UBSan and Valgrind;
> (c) `docs/notes/F1-*.md` answers: what does `use_ino` change; why must root
> be inode 1; what does `forget` decrement by; what does a `read` return
> value of 0 mean; why does `release` have no useful error path.

---

### Gate F2 — Shared infrastructure (steps 14–21)

**Gate intent:** everything reusable, built and tested with no filesystem
knowledge in it at all. Resist the urge to start FAT32 here.

---

> **14. Block layer implementation** — ★★
> **Objective:** L0 as specified in §4.3.
> **Deliverable:** `common/bdev.c`, `common/bdev.h`.
> **Acceptance:** unit tests pass, including every out-of-range case.

Required test cases, all of which must return errors rather than misbehave:
read at `sector_count - 1` (ok); read at `sector_count` (ERANGE); read of
`nsec` spanning the end (ERANGE); read with `nsec == 0`; `lba` near
`UINT64_MAX` where `lba + nsec` overflows — check as `lba > count - nsec`,
never as `lba + nsec > count`.

---

> **15. Endian accessors and a bounds-checked cursor** — ★★
> **Objective:** make it impossible to read past the end of a decoded buffer.
> **Deliverable:** `common/endian.h` plus a `cursor_t`.
> **Acceptance:** a unit test that a cursor read past the limit fails rather
> than returning garbage.

```c
typedef struct { const uint8_t *base; size_t len; size_t pos; int err; } cursor_t;

static inline uint32_t cur_rd32(cursor_t *c, size_t off)
{
    if (off + 4 > c->len) { c->err = 1; return 0; }
    return rd32(c->base + off);
}
```

The `err` sticky flag is deliberate: it lets a decode function read a whole
structure and check validity once at the end, instead of branching after every
field. Check it before using any decoded value.

---

> **16. Logging and tracing** — ★
> **Objective:** one logging path with levels, so `FS_ECORRUPT` is never silent.
> **Deliverable:** `common/logging.[ch]` with `LOG_ERR/WARN/INFO/DEBUG/TRACE`.
> **Acceptance:** level selectable by `-o loglevel=N`; `TRACE` compiles out
> entirely at `-DNDEBUG`.

---

> **17. Shared FUSE binding scaffolding** — ★★
> **Objective:** option parsing and session setup written once.
> **Deliverable:** `common/fuse_common.c` with the shared `--image=` option.
> **Acceptance:** `./fat32fuse --help` prints both custom and FUSE options.

```c
struct nova_opts {
    const char *image;
    int   uid, gid;
    unsigned umask;
    int   loglevel;
    int   show_help, show_version;
};

#define NOVA_OPT(t, p) { t, offsetof(struct nova_opts, p), 1 }

static const struct fuse_opt nova_opt_spec[] = {
    NOVA_OPT("--image=%s",   image),
    NOVA_OPT("-o image=%s",  image),
    NOVA_OPT("-o loglevel=%d", loglevel),
    NOVA_OPT("-h",           show_help),
    NOVA_OPT("--help",       show_help),
    FUSE_OPT_END
};
/* fuse_opt_parse(&args, &opts, nova_opt_spec, NULL); */
```

Also decide and document the standard mount option set now, since all three
drivers will share it: `ro`, `allow_other`, `default_permissions`,
`fsname=`, `subtype=`. `default_permissions` is worth understanding: it tells
the kernel to perform standard permission checks against the `st_mode`,
`st_uid` and `st_gid` your driver reports, rather than leaving access control
entirely to the driver. For FAT32, where those values are synthesised, this is
what makes them actually mean something.

---

> **18. `fsops` vtable header** — ★★
> **Objective:** freeze the v0 internal interface from §4.6.
> **Deliverable:** `common/fsops.h` plus `docs/notes/F2-fsops-v0.md`.
> **Acceptance:** `nullfs` is refactored to implement `fsops` and still works.

Refactoring `nullfs` onto the vtable is the acceptance test for the vtable
itself. If it is awkward for a trivial in-memory filesystem it will be worse
for a real one. Start a change log in the notes: every subsequent modification
to `fsops.h` gets one line saying which filesystem forced it and why. That
change log is the raw material for DN-FS-VFS-001.

---

> **19. Differential test harness** — ★★★
> **Objective:** the primary correctness instrument for F3 and F4.
> **Deliverable:** `tests/scripts/difftest.sh`.
> **Acceptance:** it reports a failure when run against a deliberately broken
> driver, and passes against the kernel driver compared with itself.

```bash
#!/usr/bin/env bash
# difftest.sh <ref-mount> <fuse-mount>
set -uo pipefail
REF=$1; FUS=$2; FAIL=0

cmp_out() {                      # cmp_out <label> <cmd>
  local label=$1; shift
  if ! diff -u <(cd "$REF" && eval "$@" 2>&1) \
               <(cd "$FUS" && eval "$@" 2>&1) > "/tmp/dt-$label.diff"; then
      echo "FAIL: $label   (see /tmp/dt-$label.diff)"; FAIL=1
  else
      echo "ok:   $label"
  fi
}

cmp_out tree       "find . | sort"
cmp_out types      "find . -printf '%y %p\n' | sort"
cmp_out sizes      "find . -type f -printf '%s %p\n' | sort"
cmp_out modes      "find . -printf '%m %p\n' | sort"
cmp_out links      "find . -printf '%n %p\n' | sort"
cmp_out symlinks   "find . -type l -printf '%p -> %l\n' | sort"
cmp_out hashes     "find . -type f -exec sha256sum {} + | sort -k2"
cmp_out readdirord "ls -U ."

# stat, minus the fields that legitimately differ (device, blocks on FAT)
cmp_out stats "find . -exec stat -c '%n %F %s %f %u %g %h' {} + | sort"

exit $FAIL
```

Two refinements to add as you go: a partial-read test that reads each file at
several offsets and lengths with `dd` and compares, because a whole-file
`sha256sum` will not catch an offset bug that only manifests on a non-zero
`off`; and a large-file test that reads a file backwards in 4 KiB chunks.

---

> **20. Corruption corpus and fuzz scaffold** — ★★★
> **Objective:** make robustness testable from day one rather than retrofitted.
> **Deliverable:** `tests/scripts/corrupt.sh` and a `tests/corpus/` seed set.
> **Acceptance:** a corrupted image is generated for each named class below.

```bash
#!/usr/bin/env bash
# corrupt.sh <image> <out> <offset> <bytes...>
cp "$1" "$2"
printf "$3" | dd of="$2" bs=1 seek="$4" conv=notrunc 2>/dev/null
```

Named corruption classes, each of which must be represented in the corpus:

| Class | FAT32 instance | EXT2 instance |
|---|---|---|
| Bad magic | `0xAA55` signature zeroed | `s_magic` != `0xEF53` |
| Impossible geometry | `BPB_BytsPerSec = 0` | `s_log_block_size = 30` |
| Self-referential chain | cluster N points to N | dir `..` points to a descendant |
| Chain cycle | A → B → A | double-indirect block points to itself |
| Out-of-range pointer | chain entry = `0x0FFFFFF0` in a small volume | block pointer > `s_blocks_count` |
| Zero-length record | — | dirent with `rec_len = 0` |
| Overrunning record | dirent past cluster end | `rec_len` past block end |
| Truncated image | file cut to half its length | same |
| Inconsistent counts | `FSI_Free_Count` absurd | `s_free_blocks_count` > `s_blocks_count` |
| Refused feature | — | `s_feature_incompat` includes `EXTENTS` |

The `rec_len = 0` case deserves special attention. It is the classic ext2
directory-parsing infinite loop: a naive `p += rec_len` loop never advances.
Every directory iterator in this project must reject `rec_len < 8`,
`rec_len % 4 != 0`, and `rec_len` extending past the end of the block.

An AFL++ harness is worth adding here in skeleton form even though it will not
be exercised until F3:

```c
/* fuzz_target.c — link with afl-clang-fast or -fsanitize=fuzzer */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bdev_t bd; fs_ctx_t ctx;
    if (bdev_open_mem(&bd, data, size, 512) != 0) return 0;
    if (fs->mount(&ctx, &bd, MOUNT_RO) != FS_OK) { bdev_close(&bd); return 0; }
    walk_tree(&ctx, FS_INO_ROOT, /*depth*/ 0);   /* getattr+readdir+read all */
    fs->unmount(&ctx);
    bdev_close(&bd);
    return 0;
}
```

This is why `bdev` needs an in-memory backend variant: fuzzing through a real
file is far too slow.

---

> **21. GATE F2 EXIT** — ★
> **Objective:** confirm the shared base is solid before any format work.
> **Acceptance:**
> (a) `bdev` unit tests pass, including overflow-safe range checks;
> (b) `nullfs` runs on the `fsops` vtable;
> (c) `difftest.sh` detects an injected fault;
> (d) `corrupt.sh` produces one image per class in the table above;
> (e) everything is clean under ASan/UBSan.

---

### Gate F3 — FAT32 read-only (steps 22–37)

**Gate intent:** a complete, robust, read-only FAT32 driver. Field tables are
in Appendix A; this section is the sequence, not the reference.

---

> **22. Boot sector and BPB decoding** — ★★
> **Objective:** parse and validate the BPB; determine FAT type correctly.
> **Deliverable:** `fat32/fat32_ondisk.c` with `fat32_parse_bpb()`.
> **Acceptance:** parsed values match `fsck.fat -v` output for both images;
> every geometry field is validated; a zeroed `BPB_BytsPerSec` is rejected.

Validation is not optional. Every field below is attacker-controlled from the
driver's point of view:

```c
if (bytspersec != 512 && bytspersec != 1024 &&
    bytspersec != 2048 && bytspersec != 4096)   return FS_ECORRUPT;
if (secperclus == 0 || (secperclus & (secperclus - 1)))  return FS_ECORRUPT;
if (bytspersec * secperclus > 32768)            return FS_ECORRUPT;
if (numfats == 0 || numfats > 2)                return FS_ECORRUPT;
if (rsvdseccnt == 0)                            return FS_ECORRUPT;
if (fatsz32 == 0)                               return FS_ECORRUPT;
if (rootclus < 2)                               return FS_ECORRUPT;
```

**FAT type determination.** This is the one algorithm in FAT that people get
wrong, usually by trusting the `BS_FilSysType` ASCII string at offset 82.
That string is documentation, not data — Microsoft's own specification says
so explicitly. The only correct method:

```
RootDirSectors  = ((BPB_RootEntCnt * 32) + (BPB_BytsPerSec - 1)) / BPB_BytsPerSec
                  /* == 0 for FAT32, since BPB_RootEntCnt is 0 */
FATSz           = (BPB_FATSz16 != 0) ? BPB_FATSz16 : BPB_FATSz32
TotSec          = (BPB_TotSec16 != 0) ? BPB_TotSec16 : BPB_TotSec32

FirstDataSector = BPB_RsvdSecCnt + (BPB_NumFATs * FATSz) + RootDirSectors
DataSec         = TotSec - FirstDataSector
CountOfClusters = DataSec / BPB_SecPerClus

if      (CountOfClusters <  4085)  -> FAT12
else if (CountOfClusters < 65525)  -> FAT16
else                               -> FAT32
```

Refuse to mount anything that is not FAT32. Supporting FAT12/16 is a small
extension and a large distraction; note it as a possible later exercise.

Also compute and store, once at mount:

```
MaxCluster      = CountOfClusters + 1     /* valid clusters are 2..MaxCluster */
FirstSectorOfCluster(N) = ((N - 2) * BPB_SecPerClus) + FirstDataSector
```

`MaxCluster` is the bound that makes every later chain traversal safe.

---

> **23. FSInfo parsing** — ★
> **Objective:** read the free-cluster hints and treat them as hints.
> **Deliverable:** `fat32_parse_fsinfo()`.
> **Acceptance:** both signatures validated; absurd values discarded rather
> than propagated; the driver mounts an image with a corrupt FSInfo.

The FSInfo sector holds `FSI_Free_Count` and `FSI_Nxt_Free`. Both are advisory
and both are routinely stale, because an operating system that crashes never
gets to update them. Rules: verify the three signatures; if `FSI_Free_Count`
exceeds `CountOfClusters`, or `FSI_Nxt_Free` is outside `2..MaxCluster`, treat
the field as `0xFFFFFFFF` (unknown). In read-only mode a wrong value only
affects `statfs`; in F6 it affects allocation start position, never
correctness.

---

> **24. FAT access layer** — ★★
> **Objective:** read a FAT entry, with caching and full validation.
> **Deliverable:** `fat32/fat32_fat.c` — `fat_get_next(fs, cluster, *next)`.
> **Acceptance:** returns correct successors; rejects out-of-range clusters;
> a sector-level cache measurably reduces `bdev` reads on a long chain.

```c
/* Entry E lives at byte offset (cluster * 4) into the FAT.
 * Only the low 28 bits are meaningful; the top 4 are reserved. */
fs_err_t fat_get_next(fat_fs_t *fs, uint32_t cluster, uint32_t *next)
{
    if (cluster < 2 || cluster > fs->max_cluster) return FS_ECORRUPT;

    uint64_t byte   = (uint64_t)cluster * 4u;
    uint64_t sector = fs->fat_start_lba + byte / fs->bytes_per_sector;
    uint32_t within = (uint32_t)(byte % fs->bytes_per_sector);

    const uint8_t *sec;
    fs_err_t e = fatcache_get(fs, sector, &sec);
    if (e) return e;

    *next = rd32(sec + within) & 0x0FFFFFFFu;
    return FS_OK;
}
```

Entry value meanings, all after masking to 28 bits:

| Value | Meaning |
|---|---|
| `0x0000000` | free |
| `0x0000001` | reserved / never valid as a successor |
| `0x0000002` .. `MaxCluster` | next cluster in the chain |
| `MaxCluster+1` .. `0xFFFFFF6` | reserved; treat as corrupt |
| `0x0FFFFFF7` | bad cluster |
| `0x0FFFFFF8` .. `0x0FFFFFFF` | end of chain |

Test `next >= 0x0FFFFFF8` for end-of-chain rather than equality with any
single value. Entries 0 and 1 are reserved: entry 0 holds the media descriptor
byte, entry 1 holds dirty/clean shutdown flags. Neither is a cluster.

A small direct-mapped sector cache over the FAT (say 8 or 16 sectors) is worth
building here rather than later: chain traversal has excellent locality, and
the cache turns an O(n) chain walk from n disk reads into roughly n/128.

---

> **25. Cluster chain iterator with cycle detection** — ★★★
> **Objective:** walk a chain safely on a hostile image.
> **Deliverable:** `fat_chain_iter_t` with a hard bound.
> **Acceptance:** the self-referential and cyclic corpus images terminate with
> `FS_ECORRUPT` in bounded time, and never allocate unboundedly.

Two independent safeguards, both required:

```c
typedef struct {
    fat_fs_t *fs;
    uint32_t  cur;
    uint64_t  steps;        /* hard bound: never exceed max_cluster */
    uint32_t  slow;         /* Floyd tortoise for exact cycle detection */
    int       phase;
} fat_chain_iter_t;

fs_err_t fat_chain_next(fat_chain_iter_t *it, uint32_t *out)
{
    if (it->cur >= 0x0FFFFFF8u) return FS_ENOENT;        /* clean EOC */
    if (++it->steps > it->fs->max_cluster) return FS_ECORRUPT; /* bound */
    *out = it->cur;
    fs_err_t e = fat_get_next(it->fs, it->cur, &it->cur);
    if (e) return e;
    if (it->cur == 0 || it->cur == 1 || it->cur == 0x0FFFFFF7u)
        return FS_ECORRUPT;                              /* free/bad in chain */
    if (it->phase ^= 1) {                                 /* advance slow half */
        uint32_t n; if (fat_get_next(it->fs, it->slow, &n)) return FS_ECORRUPT;
        it->slow = n;
    }
    if (it->slow == it->cur && it->cur < 0x0FFFFFF8u) return FS_ELOOP;
    return FS_OK;
}
```

The step bound alone is sufficient for safety — a chain longer than the total
cluster count must contain a cycle. Floyd's algorithm is added because it
detects the cycle in roughly the length of the cycle rather than the length of
the volume, which matters when the fuzzer is running millions of cases.

Also implement `fat_chain_nth(fs, first, n, *cluster)` for random access
during `read`. Naively this is O(n) per call, which makes a large sequential
read O(n²). Cache the last (logical index, cluster) pair per open file so
sequential access is O(1) amortised; that single optimisation is the
difference between usable and unusable, and it is worth measuring before and
after.

---

> **26. Short directory entry iteration** — ★★
> **Objective:** enumerate the 32-byte entries of a directory cluster chain.
> **Deliverable:** `fat32/fat32_dir.c` — `fat_dir_iter_*`.
> **Acceptance:** enumerating the populated image's root yields exactly the
> names the kernel driver reports, ignoring LFN for now.

Rules that must be encoded, in this order:

1. `DIR_Name[0] == 0x00` — this entry and all subsequent entries in the whole
   directory are free. **Stop scanning entirely**, do not merely skip.
2. `DIR_Name[0] == 0xE5` — deleted; skip this entry, keep scanning.
3. `DIR_Name[0] == 0x05` — the real first byte is `0xE5`. This exists because
   `0xE5` is a valid lead byte in some East Asian code pages and would
   otherwise be indistinguishable from a deletion marker.
4. `(DIR_Attr & 0x0F) == 0x0F` — this is an LFN entry, not a file. Handle in
   step 27.
5. `DIR_Attr & 0x08` (volume ID) with the LFN check already excluded — the
   volume label. Skip it; it is not a file.
6. Anything else is a real short entry.

Short-name reconstruction: the 11 bytes are 8 characters of base name and 3 of
extension, space-padded, with no stored dot. Trailing spaces are stripped from
each part, a dot is inserted only if the extension is non-empty, and the
`DIR_NTRes` byte's bits 3 and 4 indicate that the base name and/or extension
should be displayed lowercase — a Windows NT extension that Linux honours and
that you should too, or `readdir` output will not match the kernel.

---

> **27. Long filename reassembly** — ★★★
> **Objective:** reconstruct UTF-8 names from VFAT LFN entry runs.
> **Deliverable:** `fat32/fat32_lfn.c`.
> **Acceptance:** all names in the populated image, including the long one
> with spaces, match the kernel driver byte for byte.

Structure and rules:

- LFN entries **precede** their short entry and appear in **reverse** order:
  the entry with the highest ordinal comes first on disk, ordinals descend to
  1, and the first entry on disk (the last logical fragment) has bit `0x40`
  set in `LDIR_Ord`.
- Each LFN entry carries 13 UTF-16 code units, split across three
  non-contiguous fields at offsets 1 (5 units), 14 (6 units) and 28 (2 units).
  This awkward split exists so that the byte at offset 11 lines up with
  `DIR_Attr` and the byte at 26 lines up with `DIR_FstClusLO`, letting old
  DOS see the entry as a harmless volume-label entry with cluster 0.
- The name is terminated by `0x0000`; remaining slots are padded with
  `0xFFFF`. Neither is part of the name.
- Every LFN entry in a run carries the same checksum of the short name it
  belongs to. Verify it.

```c
uint8_t lfn_checksum(const uint8_t *short_name /* 11 bytes */)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_name[i]);
    return sum;
}
```

Failure handling, which is where a naive implementation goes wrong: if the
ordinals are not a descending run ending at 1, or the `0x40` flag is missing
on the first, or the checksum does not match the following short entry, the
LFN run is **orphaned**. Discard it and fall back to the short name. Do not
error out — orphaned LFN entries occur in the wild after crashes, and the
kernel tolerates them.

UTF-16 to UTF-8 conversion must handle surrogate pairs (`0xD800`–`0xDBFF`
followed by `0xDC00`–`0xDFFF`) rather than encoding each half separately;
unpaired surrogates should be replaced with `U+FFFD`. Write this by hand
rather than using `iconv`, so the code stays free of external dependencies for
the eventual port.

The related patent question, since it comes up: the two Microsoft VFAT
long-filename patents (US 5,758,352 and US 5,579,517) have expired. There is
no longer any licensing concern in implementing LFN, and in any case reading
an existing name was never the contested part.

---

> **28. Synthetic inode numbering** — ★★★
> **Objective:** give FAT32 stable inode numbers, which it has no concept of.
> **Deliverable:** `fat_ino_of_entry()` and a documented scheme.
> **Acceptance:** `find -inum` works; two hard-linked-looking paths never
> collide; the root reports inode 1.

FAT has no inode. The vtable needs one. Options considered:

| Scheme | Stability | Memory | Verdict |
|---|---|---|---|
| Byte offset of the short dirent, divided by 32 | Stable while the file is not moved | Zero | **Chosen** |
| First cluster number | Breaks for empty files (cluster 0) and collides | Zero | Rejected |
| Hash table keyed by path | Stable across moves | O(files) | Rejected for the port |
| Monotonic counter assigned on first lookup | Stable per mount | O(files) | Rejected |

Chosen scheme, stated precisely:

```
ino(entry) = (FirstSectorOfCluster(dir_cluster) * bytes_per_sector
              + offset_within_cluster) / 32
ino(root)  = 1                      /* FUSE_ROOT_ID, special-cased */
```

Guarantees and limits to record:

- Unique while the file exists, because two directory entries cannot occupy
  the same disk offset.
- Not stable across a rename or move, which is acceptable for a read-only
  driver and must be revisited in F6.
- Must be checked for accidental collision with 1. Sector 0 is in the reserved
  region and never holds a directory entry, so offsets that would produce 0 or
  1 cannot occur for a real entry; assert this rather than assume it.
- `st_nlink` is 1 for all files and 2 for all directories, because FAT has no
  hard links and, unlike ext2, subdirectory `..` entries are not counted the
  same way. The kernel `vfat` driver reports 1 for directories too in some
  configurations — check against `difftest.sh` and match whatever it does.

Set `cfg->use_ino = 1` so the kernel honours the values you report.

---

> **29. Path resolution** — ★★
> **Objective:** resolve a `/`-separated path to an inode and its entry.
> **Deliverable:** `fat_lookup(fs, parent_ino, name, *out)` per the vtable.
> **Acceptance:** deep paths resolve; a non-directory component returns
> `FS_ENOTDIR`, not `FS_ENOENT`.

Two details that matter:

- **Case-insensitive comparison.** FAT is case-insensitive and
  case-preserving. Comparison must fold case, and for LFN names that means
  folding non-ASCII too — or deciding, and documenting, that folding is
  ASCII-only. The kernel `vfat` driver's behaviour depends on the
  `iocharset`/`utf8` mount options; pick ASCII-only folding, document it as a
  known deviation, and verify with `difftest.sh` using ASCII names only.
- **Depth bound.** Cap directory descent at, say, 64 levels and return
  `FS_ELOOP` beyond it. FAT directories cannot legitimately nest that deeply
  in any real use, and a corrupt `..` chain otherwise recurses forever.

---

> **30. `getattr` — attribute synthesis** — ★★
> **Objective:** turn a 32-byte dirent into a full `struct stat`.
> **Deliverable:** `fat_getattr()`.
> **Acceptance:** `difftest.sh`'s `modes`, `sizes` and `types` checks pass.

Synthesis rules:

| `stat` field | Source |
|---|---|
| `st_ino` | step 28 |
| `st_mode` | `S_IFDIR` if `DIR_Attr & 0x10` else `S_IFREG`, OR'd with base perms |
| base perms | `0777 & ~umask`, then clear all `w` bits if `DIR_Attr & 0x01` |
| `st_uid` / `st_gid` | mount option, defaulting to `fuse_get_context()` |
| `st_nlink` | 1 for files, 2 for directories |
| `st_size` | `DIR_FileSize` for files; 0 for directories |
| `st_blocks` | `ceil(size / cluster_size) * cluster_size / 512` |
| `st_mtime` | `DIR_WrtDate`/`DIR_WrtTime`, 2-second resolution |
| `st_atime` | `DIR_LstAccDate`, 1-day resolution, time 00:00 |
| `st_ctime` | `DIR_CrtDate`/`DIR_CrtTime` + `DIR_CrtTimeTenth` |

Date and time decoding:

```c
/* Date: bits 15..9 = year - 1980, 8..5 = month (1-12), 4..0 = day (1-31) */
/* Time: bits 15..11 = hour, 10..5 = minute, 4..0 = second / 2            */
static time_t fat_time(uint16_t date, uint16_t time, uint8_t tenth)
{
    struct tm tm = {0};
    tm.tm_year = ((date >> 9) & 0x7F) + 80;      /* years since 1900 */
    tm.tm_mon  = ((date >> 5) & 0x0F) - 1;
    tm.tm_mday =  (date       & 0x1F);
    tm.tm_hour = ((time >> 11) & 0x1F);
    tm.tm_min  = ((time >>  5) & 0x3F);
    tm.tm_sec  = ((time        & 0x1F) * 2) + (tenth >= 100 ? 1 : 0);
    tm.tm_isdst = -1;
    return mktime(&tm);          /* FAT timestamps are local time, not UTC */
}
```

The local-time detail is a real and irritating property of FAT: timestamps
carry no timezone. Linux applies the `tz=` / `time_offset=` mount options.
Match whatever the reference mount does, and note the deviation if you do not.

Validate the decoded fields — month 0 or 13, day 0 or 32 all occur in corrupt
images — and substitute the epoch rather than passing nonsense to `mktime`.

---

> **31. `readdir`** — ★★
> **Objective:** enumerate a directory through the vtable and into FUSE.
> **Deliverable:** `fat_opendir`/`readdir`/`closedir` and the L3 binding.
> **Acceptance:** `difftest.sh`'s `tree` and `readdirord` checks pass.

Synthesise `.` and `..` for the root, which has neither on disk in FAT32.
Subdirectories do have both, and the on-disk `..` entry of a first-level
subdirectory points to cluster 0 rather than to the root cluster — a FAT
quirk that must be translated back to the root inode. Getting this wrong makes
`cd ..` from a top-level directory land nowhere.

---

> **32. `open` and `read`** — ★★★
> **Objective:** correct byte-range reads including all boundary cases.
> **Deliverable:** `fat_open`/`read`/`close` plus the L3 binding.
> **Acceptance:** the `hashes` and partial-read checks in `difftest.sh` pass
> for every file, including the `4095/4096/4097` set and `triple.bin`.

```
cluster_size = bytes_per_sector * sectors_per_cluster
logical_cluster = off / cluster_size
offset_in_cluster = off % cluster_size
physical = fat_chain_nth(first_cluster, logical_cluster)
lba = FirstSectorOfCluster(physical) + offset_in_cluster / bytes_per_sector
```

Boundary conditions to test explicitly, each of which has broken a real
implementation: `off >= size` returns 0; `off + len > size` returns
`size - off`; a read spanning a cluster boundary; a read spanning a sector
boundary within a cluster; a read of length 0; a file with `first_cluster == 0`
(a legitimately empty file, which has no chain at all and must not be walked).

That last one is the most common FAT32 read bug: an empty file stores cluster
0, and cluster 0 is not a valid cluster. Check for it before starting any
chain traversal.

---

> **33. `statfs`** — ★
> **Objective:** make `df` work.
> **Deliverable:** `fat_statfs()`.
> **Acceptance:** `df` output is within one block of the kernel driver's.

Use `FSI_Free_Count` when it validates; otherwise scan the FAT once at mount
and cache the result. Scanning 512 MiB of FAT is fast; scanning it on every
`statfs` call is not.

---

> **34. Mount options** — ★★
> **Objective:** the options needed for `difftest.sh` to compare like with like.
> **Deliverable:** `uid=`, `gid=`, `umask=`, `fmask=`, `dmask=`, `ro`.
> **Acceptance:** with matching options, the `modes` check passes exactly.

`fmask` and `dmask` override `umask` for files and directories respectively,
matching the kernel `vfat` driver's option set. Implementing them is trivial
and it removes a whole class of spurious `difftest` failures.

---

> **35. Differential testing pass** — ★★★
> **Objective:** prove equivalence with the kernel driver.
> **Deliverable:** a passing `difftest.sh` run recorded in the notes.
> **Acceptance:** zero diffs across all checks on both `fat32.img` and
> `fat32-small.img`.

```bash
./tests/scripts/refmount.sh tests/images/fat32.img /mnt/ref
./fat32fuse --image=tests/images/fat32.img -o ro,uid=$(id -u),gid=$(id -g) \
            -f -s /mnt/fuse &
./tests/scripts/difftest.sh /mnt/ref /mnt/fuse
```

Expect failures on the first run. The usual culprits, in order of frequency:
`st_mode` bits, `st_nlink` on directories, `st_blocks`, timestamp timezone,
and `readdir` ordering. Resolve each by deciding which behaviour is correct
and documenting the decision — sometimes the kernel's behaviour is a historical
artefact and matching it is still the right call, because matching is what
makes the harness useful.

---

> **36. Corruption robustness pass** — ★★★★
> **Objective:** survive every image in the corpus.
> **Deliverable:** a passing run of the corpus plus ≥ 1 hour of AFL++.
> **Acceptance:** no crash, no hang, no unbounded allocation, no ASan report;
> every failure is a clean `FS_ECORRUPT` or a refusal to mount.

```bash
make asan
for img in tests/corpus/fat32-*.img; do
    timeout 10 ./fat32fuse --image="$img" -o ro -f -s /mnt/fuse &
    sleep 0.3; timeout 10 find /mnt/fuse -exec cat {} + >/dev/null 2>&1
    fusermount3 -uz /mnt/fuse 2>/dev/null; wait
done
```

The `timeout` is the point: a hang is a failure, and without a timeout a hang
looks like a slow pass.

---

> **37. GATE F3 EXIT** — ★★
> **Objective:** close out FAT32 read-only.
> **Acceptance:**
> (a) `difftest.sh` clean on both FAT32 images;
> (b) corruption corpus clean under ASan/UBSan with timeouts;
> (c) ≥ 1 hour AFL++ with no new crashes;
> (d) `docs/notes/F3-exit.md` records: the inode scheme and its limits, every
> deliberate deviation from the kernel driver, and any `fsops.h` change and
> why FAT32 forced it.

---

### Gate F4 — EXT2 read-only (steps 38–55)

**Gate intent:** the classical Unix filesystem, done properly. Field tables in
Appendix B. This gate is the direct prototype of DN-FS-EXT2-001 layers L0–L8.

---

> **38. Superblock parsing** — ★★
> **Objective:** decode and validate the superblock at byte offset 1024.
> **Deliverable:** `ext2/ext2_sb.c`.
> **Acceptance:** every field matches `dumpe2fs -h` for both ext2 images.

The superblock always lives at byte offset 1024 and is always 1024 bytes,
regardless of block size. With 1 KiB blocks that makes it block 1 and
`s_first_data_block` is 1; with larger blocks it sits inside block 0 and
`s_first_data_block` is 0. Both cases must work, which is why step 4 built
both images.

```c
uint32_t block_size = 1024u << sb->s_log_block_size;
```

Validation:

```c
if (sb->s_magic != 0xEF53)                       return FS_ENOTSUP;
if (sb->s_log_block_size > 6)                    return FS_ECORRUPT; /* >64K */
if (sb->s_blocks_per_group == 0)                 return FS_ECORRUPT;
if (sb->s_inodes_per_group == 0)                 return FS_ECORRUPT;
if (sb->s_inodes_count == 0)                     return FS_ECORRUPT;
if (sb->s_free_blocks_count > sb->s_blocks_count)return FS_ECORRUPT;
if (sb->s_free_inodes_count > sb->s_inodes_count)return FS_ECORRUPT;
if (sb->s_first_data_block != (block_size == 1024 ? 1u : 0u))
                                                 return FS_ECORRUPT;
```

Revision handling: if `s_rev_level == EXT2_GOOD_OLD_REV (0)`, inode size is
fixed at 128, `s_first_ino` is 11, and the feature-flag fields do not exist —
treat them as zero. If `s_rev_level == 1` (`EXT2_DYNAMIC_REV`), read
`s_inode_size` and `s_first_ino` from the superblock. Never hard-code 128.

---

> **39. Feature flag gate** — ★★★
> **Objective:** refuse, correctly and loudly, anything this driver cannot read.
> **Deliverable:** `ext2_check_features()`.
> **Acceptance:** an ext4 image is refused with a clear message naming the
> offending feature; the plain ext2 images mount.

This is a correctness requirement, not a nicety. The three-way split exists
precisely so that an old implementation can make a safe decision:

- **COMPAT** — unknown bits may be ignored. Mount read-write.
- **RO_COMPAT** — unknown bits mean the driver cannot safely *write*. Mount
  read-only.
- **INCOMPAT** — unknown bits mean the driver cannot safely *read*. Refuse.

```c
#define EXT2_FEATURE_INCOMPAT_SUPP    0x0002u   /* FILETYPE only */
#define EXT2_FEATURE_RO_COMPAT_SUPP   (0x0001u|0x0002u) /* SPARSE_SUPER|LARGE_FILE */

uint32_t bad_i  = sb->s_feature_incompat  & ~EXT2_FEATURE_INCOMPAT_SUPP;
uint32_t bad_ro = sb->s_feature_ro_compat & ~EXT2_FEATURE_RO_COMPAT_SUPP;

if (bad_i) { LOG_ERR("unsupported incompat features 0x%08x", bad_i);
             return FS_ENOTSUP; }
if (bad_ro) { LOG_WARN("unsupported ro_compat 0x%08x, forcing read-only", bad_ro);
              fs->force_ro = 1; }
```

The reason this matters concretely: a real ext4 volume reports
`s_feature_incompat` of roughly `0x2C2` — `FILETYPE | EXTENTS | 64BIT |
FLEX_BG`. `EXTENTS` in particular means the `i_block` array is not an array of
block pointers at all but an extent tree header. A driver that ignores the
flag and walks `i_block` as pointers will read arbitrary blocks and return
garbage that *looks* like data. Refusing is not conservatism; it is the
difference between a clean error and silent corruption.

Log the human-readable name of each offending bit, not just the hex. Appendix
B has the full table.

---

> **40. Block group descriptor table** — ★★
> **Objective:** locate and decode the BGDT.
> **Deliverable:** `ext2_read_bgd()`.
> **Acceptance:** all descriptors match `dumpe2fs` group output.

```
group_count = ceil(s_blocks_count - s_first_data_block) / s_blocks_per_group)
            = also  ceil(s_inodes_count / s_inodes_per_group)   /* cross-check */
bgdt_block  = s_first_data_block + 1
```

Cross-check the two group-count computations against each other and reject the
image if they disagree; that mismatch is a reliable corruption signal.

Each descriptor is 32 bytes. Validate that `bg_block_bitmap`,
`bg_inode_bitmap` and `bg_inode_table` all fall inside the group's block range
and inside the volume, because these are the pointers a malicious image will
aim somewhere interesting.

Cache the whole BGDT at mount. It is small — a 128 MiB volume with 1 KiB
blocks and 8192 blocks per group has 16 groups, 512 bytes of descriptors — and
it is consulted constantly.

---

> **41. Inode reading** — ★★★
> **Objective:** locate and decode an inode from its number.
> **Deliverable:** `ext2/ext2_inode.c` — `ext2_read_inode()`.
> **Acceptance:** `debugfs -R "stat <2>"` and your decode of inode 2 agree on
> every field, on both images (128- and 256-byte inodes).

```
/* Inode numbers start at 1. This -1 is the single most common bug here. */
group  = (ino - 1) / s_inodes_per_group;
index  = (ino - 1) % s_inodes_per_group;
byte   = (uint64_t)index * s_inode_size;
block  = bgd[group].bg_inode_table + byte / block_size;
offset = byte % block_size;
```

Bounds: reject `ino == 0` and `ino > s_inodes_count` before any arithmetic.

`i_mode` splits into a file type in the high nibble and permissions in the low
12 bits:

| Type bits | Meaning |
|---|---|
| `0x1000` | FIFO |
| `0x2000` | character device |
| `0x4000` | directory |
| `0x6000` | block device |
| `0x8000` | regular file |
| `0xA000` | symbolic link |
| `0xC000` | socket |

These are the same values as POSIX `S_IFMT`, which is not a coincidence and
means `st_mode = i_mode` directly, no translation.

**Two gotchas that must be handled here and not later:**

1. **`i_blocks` is in 512-byte units**, not filesystem blocks, and it counts
   indirect blocks as well as data blocks. This is why it maps directly to
   `st_blocks` — POSIX uses the same unit — but *not* to `i_size / block_size`.
   Never compute the number of data blocks from `i_blocks`.
2. **File size is 64-bit for regular files.** `i_size` holds the low 32 bits;
   `i_dir_acl` at offset 108 holds the high 32 bits when the `LARGE_FILE`
   RO_COMPAT feature is set. For directories the same field really is
   `i_dir_acl`. Combine only for regular files, and only when the flag is set.

---

> **42. Block map traversal** — ★★★★
> **Objective:** map a logical block index to a physical block, through up to
> three levels of indirection.
> **Deliverable:** `ext2/ext2_blockmap.c` — `ext2_bmap(ino, lblk, *pblk)`.
> **Acceptance:** for every file in the image, your block list matches
> `debugfs -R "blocks <ino>"` exactly, including `triple.bin`.

This is the single most error-prone routine in the gate. Write it, then verify
it against `debugfs` before writing anything that depends on it.

```
k = block_size / 4          /* pointers per indirect block: 256 or 1024 */

n <  12                     -> i_block[n]                        (direct)
n <  12 + k                 -> via i_block[12]                   (single)
n <  12 + k + k*k           -> via i_block[13]                   (double)
n <  12 + k + k*k + k*k*k   -> via i_block[14]                   (triple)
else                        -> beyond the format's reach
```

With the indices computed as:

```c
fs_err_t ext2_bmap(ext2_fs_t *fs, const ext2_inode_t *in,
                   uint32_t n, uint32_t *out)
{
    const uint32_t k = fs->block_size / 4u;

    if (n < 12) { *out = in->i_block[n]; return FS_OK; }
    n -= 12;

    if (n < k)              return ind1(fs, in->i_block[12], n, out);
    n -= k;

    if (n < k * k)          return ind2(fs, in->i_block[13], n / k, n % k, out);
    n -= k * k;

    if (n < k * k * k)      return ind3(fs, in->i_block[14],
                                        n / (k * k), (n / k) % k, n % k, out);
    return FS_ECORRUPT;
}
```

Points that are easy to get wrong:

- **`n < k*k*k` overflows.** With `k = 1024`, `k*k*k` is 2³⁰, which fits in
  `uint32_t`, but `12 + k + k*k + k*k*k` does not comfortably. Do the
  subtractions as shown rather than building a running sum, and use `uint64_t`
  for any intermediate that could exceed 32 bits.
- **A pointer of 0 is a hole**, not an error. Return success with `*out = 0`,
  and have the caller supply zeros. `sparse.bin` from step 5 exercises this.
  A hole can appear at any level: `i_block[13] == 0` means the entire
  double-indirect range is a hole.
- **Every pointer must be range-checked** against `s_first_data_block` and
  `s_blocks_count` before it is used as a block address.
- **Indirect blocks must be cached.** A sequential read of a 64 MiB file
  through double indirection re-reads the same indirect block for 256
  consecutive logical blocks. Without a cache this is catastrophic; with a
  two-entry cache (one per level) it disappears.

Verification recipe, which is the actual acceptance test:

```bash
INO=$(debugfs -R "stat triple.bin" tests/images/ext2.img 2>/dev/null | \
      head -1 | sed 's/.*Inode: \([0-9]*\).*/\1/')
debugfs -R "blocks <$INO>" tests/images/ext2.img > /tmp/ref-blocks.txt
./tools/dumpblocks tests/images/ext2.img "$INO"  > /tmp/my-blocks.txt
diff /tmp/ref-blocks.txt /tmp/my-blocks.txt
```

Build `tools/dumpblocks` as a tiny L2-only program. It needs no FUSE and it
will pay for itself ten times over during this step.

---

> **43. Directory entry iteration** — ★★★
> **Objective:** walk the linked list of `ext2_dir_entry_2` records safely.
> **Deliverable:** `ext2/ext2_dir.c`.
> **Acceptance:** enumeration matches the kernel driver; the `rec_len = 0` and
> overrunning-record corpus images terminate cleanly.

A directory's data blocks each contain a packed list of variable-length
records. Records never span a block boundary, and the final record of each
block has its `rec_len` extended to reach the end of the block.

```c
while (off < block_size) {
    uint32_t ino     = rd32(blk + off + 0);
    uint16_t rec_len = rd16(blk + off + 4);
    uint8_t  name_len= rd8 (blk + off + 6);
    uint8_t  ftype   = rd8 (blk + off + 7);

    /* ---- validation, all four checks required ---- */
    if (rec_len < 8)                     return FS_ECORRUPT;  /* no progress */
    if (rec_len % 4 != 0)                return FS_ECORRUPT;
    if (off + rec_len > block_size)      return FS_ECORRUPT;
    if (8u + name_len > rec_len)         return FS_ECORRUPT;

    if (ino != 0) { emit(ino, ftype, blk + off + 8, name_len); }
    off += rec_len;
}
```

The `rec_len < 8` check is what prevents the classic infinite loop. The
`ino != 0` skip is what makes deleted entries invisible — deletion in ext2
works by absorbing the dead record into the previous record's `rec_len`, or,
for the first record in a block, by zeroing its inode field.

`file_type` at offset 7 is only meaningful when the `FILETYPE` INCOMPAT
feature is set. Without it, offsets 6–7 together form a 16-bit `name_len`.
Since step 4 always sets `FILETYPE`, handle the flag-clear case by treating
`file_type` as unknown (`0`) and reading `name_len` as 16-bit — a few lines,
and it is the difference between a driver that works on arbitrary ext2 images
and one that works on yours.

**Hashed (htree) directories.** This is flagged in the project's existing
notes as the highest correctness risk in ext2 work, so it is worth stating
exactly what is and is not safe:

- `dir_index` is a **COMPAT** feature, so a reader is permitted to ignore it.
- A directory using an htree has `EXT2_INDEX_FL` (`0x1000`) set in `i_flags`.
- In such a directory, block 0 contains `.` and `..` as normal records, with
  `..`'s `rec_len` extended to cover the rest of the block — hiding the tree
  root behind what looks like one long record.
- Interior tree nodes occupy whole blocks and begin with a fake record whose
  `inode` is 0 and whose `rec_len` spans the entire block.
- Leaf blocks are ordinary directory blocks.

Therefore **a linear scan of every block of the directory, skipping records
with `inode == 0`, enumerates every name correctly** even on an htree
directory. Read-only support is free. What is *not* free is writing: inserting
a name without updating the hash tree leaves the directory internally
inconsistent and lookups through the tree will miss the new file. F7 must
therefore refuse to modify any directory with `EXT2_INDEX_FL` set. Record this
now, as a closed decision, so it is not rediscovered in F7.

---

> **44. Path resolution and lookup** — ★★
> **Objective:** `ext2_lookup()` per the vtable.
> **Deliverable:** lookup with a directory-block cache.
> **Acceptance:** deep paths resolve; `FS_ENOTDIR` vs `FS_ENOENT` correct.

ext2 is case-**sensitive**, unlike FAT — a straight `memcmp` on `name_len`
bytes, with no folding. Note the asymmetry explicitly in the notes; it is one
of the concrete places where the two formats' models diverge and it will
matter to the VFS layer.

---

> **45. `getattr`** — ★★
> **Objective:** a real, not synthesised, `struct stat`.
> **Deliverable:** `ext2_getattr()`.
> **Acceptance:** `difftest.sh`'s `stats`, `modes` and `links` checks pass.

Almost everything maps directly: `st_mode = i_mode`, `st_uid = i_uid`,
`st_gid = i_gid`, `st_nlink = i_links_count`, `st_blocks = i_blocks`,
`st_atime/mtime/ctime` are Unix epoch seconds already. The only assembly
required is the 64-bit size from step 41.

The contrast with step 30 is worth writing down: FAT required inventing seven
of eleven fields; ext2 requires inventing none. That is the entire difference
between a filesystem designed for a single-user DOS machine and one designed
for Unix, and it is visible in about forty lines of code.

---

> **46. `readdir` with `d_type`** — ★★
> **Objective:** enumerate through the vtable, populating the type byte.
> **Deliverable:** `ext2_opendir/readdir/closedir`.
> **Acceptance:** `difftest.sh` `tree` and `types` pass; `find -type f` works
> without triggering a `getattr` storm.

The `file_type` byte lets `readdir` report a type without reading each child's
inode. Verify with `-d` that `find -type f` produces roughly one request per
directory rather than one per file; that measurable difference is the whole
reason the field exists.

---

> **47. `read`** — ★★
> **Objective:** byte-range reads on top of `ext2_bmap`.
> **Deliverable:** `ext2_open/read/close`.
> **Acceptance:** `hashes` and partial-read checks pass on every file
> including `sparse.bin` and `triple.bin`.

Holes read as zeros. A read that starts in a hole and ends in real data must
produce the correct mixture; test it explicitly rather than assuming.

---

> **48. `readlink` — fast and slow symlinks** — ★★
> **Objective:** both symlink storage forms.
> **Deliverable:** `ext2_readlink()`.
> **Acceptance:** `symlinks` check passes for both `fastlink` and `slowlink`.

A **fast symlink** stores the target string inline in the 60 bytes of the
`i_block` array; the discriminator is `i_blocks == 0` (some implementations
also check `i_size < 60`, which is equivalent in practice but less robust —
use `i_blocks == 0`). A **slow symlink** stores the target in ordinary data
blocks and is read like a file. The target is not NUL-terminated on disk;
`i_size` is the length. `readlink` in FUSE 3 expects a NUL-terminated buffer,
so terminate it yourself and be careful with the off-by-one.

---

> **49. Hard links and inode identity** — ★★
> **Objective:** demonstrate that inode identity is real, not synthesised.
> **Deliverable:** nothing new — a test.
> **Acceptance:** `small.txt` and `hardlink.txt` report the same `st_ino` and
> `st_nlink == 2`, matching the kernel driver.

This is the step where the `fsops` decision to key on inode rather than path
(§4.6) pays off, and it is worth pausing on: FAT could have been implemented
path-keyed and nothing would have broken. ext2 cannot. Record it in the
`fsops.h` change log.

---

> **50. `statfs`** — ★
> **Objective:** `df` correctness.
> **Deliverable:** `ext2_statfs()`.
> **Acceptance:** `df` matches the kernel driver within rounding.

Straight from the superblock: `s_blocks_count`, `s_free_blocks_count`,
`s_inodes_count`, `s_free_inodes_count`, block size. Remember
`s_r_blocks_count` (reserved for root) when computing `f_bavail` versus
`f_bfree` — the kernel does, and `difftest` will notice.

---

> **51. Sparse superblock backups** — ★★
> **Objective:** understand and verify the backup superblock layout.
> **Deliverable:** a validation routine comparing the primary superblock with
> a backup.
> **Acceptance:** your computed backup locations match `dumpe2fs` output.

With `SPARSE_SUPER` set, superblock and BGDT backups exist only in group 0,
group 1, and groups that are powers of 3, 5 or 7 (3, 5, 7, 9, 25, 27, 49, …).
Without it, every group has a backup. Compute the set and cross-check against
`dumpe2fs`. This is not needed for reading, but it is needed for F7 (every
backup must be updated when the superblock changes) and it is much cheaper to
understand now.

---

> **52. Larger-block image regression** — ★★
> **Objective:** prove no hard-coded block size or inode size crept in.
> **Deliverable:** a passing run against `ext2-4k.img`.
> **Acceptance:** `difftest.sh` clean on the 4 KiB / 256-byte-inode image.

This step reliably finds two or three bugs: a hard-coded 256 pointers per
indirect block, a hard-coded 128-byte inode, and an assumption that
`s_first_data_block` is 1.

---

> **53. Differential testing pass** — ★★★
> **Objective:** prove equivalence with the kernel `ext2` driver.
> **Acceptance:** zero diffs on both ext2 images.

---

> **54. Corruption robustness pass** — ★★★★
> **Objective:** survive the ext2 corpus.
> **Acceptance:** no crash, hang, or unbounded allocation; ≥ 1 hour AFL++
> clean; every failure is a clean error.

Additional ext2-specific hazards beyond the shared corpus: an inode whose
`i_links_count` is 0 but which is referenced by a directory entry; a directory
whose `..` points to one of its own descendants (structural cycle — the depth
bound catches it); an indirect block that points to itself; an inode table
pointer that lands inside the superblock.

---

> **55. GATE F4 EXIT** — ★★
> **Objective:** close out EXT2 read-only.
> **Acceptance:**
> (a) `difftest.sh` clean on both ext2 images;
> (b) block maps verified against `debugfs blocks` for every file;
> (c) corpus and AFL++ clean under ASan/UBSan;
> (d) an ext4 image is refused with the offending feature named;
> (e) `docs/notes/F4-exit.md` records the htree read-safety argument, the
> feature-flag policy, and any `fsops.h` changes ext2 forced;
> (f) DN-FS-EXT2-001 is reviewed against what was actually learned and updated
> where the design note and reality disagree.

Point (f) is the one that justifies the gate's existence to the wider project.
Do not skip it.

---

### Gate F5 — NVFS read-only FUSE binding (steps 56–65)

**Gate intent:** a binding, not a filesystem. The NVFS specification (draft 2)
and `libnvfs` already exist and are authoritative. Nothing in this gate
reimplements the format, and nothing in this gate changes the spec.

---

> **56. `libnvfs` API surface audit** — ★★
> **Objective:** determine what `libnvfs` already exposes and what is missing
> for a mountable front-end.
> **Deliverable:** `docs/notes/F5-libnvfs-audit.md` — a table of every entry
> point mapped against the `fsops` vtable.
> **Acceptance:** every `fsops` read-side slot is marked *present*, *derivable*
> or *missing*, with a one-line note for each.

Audit against the vtable, not against a wish list. The table should look like:

| `fsops` slot | `libnvfs` equivalent | Status | Note |
|---|---|---|---|
| `mount` | | | does it accept an fd, or does it open a path itself? |
| `getattr` | | | does it return everything `fs_attr_t` needs? |
| `lookup` | | | parent-inode-keyed, or path-keyed? |
| `opendir`/`readdir` | | | is there a resumable cursor? |
| `open`/`read` | | | is there a handle, or is read inode-keyed? |
| `statfs` | | | are free counts cheap or does it scan? |
| `readlink` | | | does NVFS have symlinks at all? |

The two questions that determine how much work this gate is:

1. **Does `libnvfs` take a block-device abstraction, or does it own its I/O?**
   If it owns its I/O, the cleanest fix is to give it a callback-based device
   interface matching `bdev` and adapt the existing callers. That is a change
   to `libnvfs`, and it should be made deliberately rather than worked around
   in the binding.
2. **Is the internal model inode-keyed or path-keyed?** FUSE's low-level API
   is inode-keyed. If `libnvfs` is path-keyed, either add an inode-keyed layer
   to `libnvfs` or use the high-level FUSE API instead. Decide here; do not
   discover it in step 60.

---

> **57. Decide and record the binding boundary** — ★★★
> **Objective:** prevent logic duplication between the FUSE front-end and the
> existing `nv` CLI, `mkfs.nvfs` and `nvfsck`.
> **Deliverable:** a decision record in `docs/notes/F5-boundary.md`.
> **Acceptance:** the record states, for each candidate piece of logic, which
> component owns it, and no piece is owned by two.

The governing rule, which should be stated as such: **any code that
understands the NVFS on-disk format lives in `libnvfs` and nowhere else.** If
the FUSE binding needs behaviour that does not exist, the change goes into
`libnvfs` where the conformance suite already covers it — not into the
binding where it would be untested and would immediately diverge.

The FUSE binding is therefore a **fourth front-end** alongside `nv`,
`mkfs.nvfs` and `nvfsck`, not a replacement for any of them. `nv` remains
useful precisely because it works without a mount, which matters for CI, for
scripted image construction, and for debugging a volume the FUSE driver
refuses to mount.

---

> **58. Extend `libnvfs` with any missing accessors** — ★★★
> **Objective:** close the gaps found in step 56, inside `libnvfs`.
> **Deliverable:** new `libnvfs` entry points plus tests in the existing suite.
> **Acceptance:** the existing conformance suite still passes at its full
> check count; new entry points have coverage.

Every addition must come with conformance coverage before the binding uses it.
The conformance suite is the reason NVFS is trustworthy; adding untested
surface area to it is how that stops being true.

---

> **59. Adapter layer** — ★★
> **Objective:** map `libnvfs` onto `fsops`.
> **Deliverable:** `nvfs/nvfs_fuse_adapter.c`.
> **Acceptance:** `nullfs`-style unit tests drive NVFS through the vtable with
> no FUSE involved.

Being able to exercise NVFS through `fsops` without mounting is worth the small
extra effort: it makes the adapter testable in CI without root, without
`/dev/fuse`, and without a session loop.

---

> **60. Low-level FUSE binding** — ★★★★
> **Objective:** implement `struct fuse_lowlevel_ops` over the adapter.
> **Deliverable:** `nvfs/nvfsfuse.c`.
> **Acceptance:** mounts; `ls -lR`, `cat`, `stat`, `find` all behave.

The low-level API is used here deliberately, for three reasons: NVFS inode
numbers map directly onto `fuse_ino_t` with no synthesis; the lookup-count
discipline is the same discipline the noVa64 kernel will need for its own
inode cache; and the low-level API gives access to invalidation and
notification primitives that a real OS integration eventually wants.

```c
static const struct fuse_lowlevel_ops nvfs_ll_ops = {
    .init       = nv_init,
    .destroy    = nv_destroy,
    .lookup     = nv_lookup,
    .forget     = nv_forget,
    .forget_multi = nv_forget_multi,
    .getattr    = nv_getattr,
    .readlink   = nv_readlink,
    .opendir    = nv_opendir,
    .readdir    = nv_readdir,
    .releasedir = nv_releasedir,
    .open       = nv_open,
    .read       = nv_read,
    .release    = nv_release,
    .statfs     = nv_statfs,
};
```

Implement `forget_multi` as well as `forget`; the kernel uses it at unmount
and under memory pressure, and a missing implementation silently leaks every
inode the kernel drops in bulk.

---

> **61. Inode lifetime management** — ★★★★
> **Objective:** a correct lookup-count implementation for NVFS inodes.
> **Deliverable:** an inode table with reference counting.
> **Acceptance:** a stress test that opens, stats and drops 100 000 paths
> shows a bounded table size and zero leaks under ASan.

The test that actually proves this is: mount, run `find /mnt -exec stat {} +`
over a large tree, unmount, and check that the inode table reached zero
entries before `destroy` and that ASan reports no leak. Anything less will not
catch a `forget` accounting error, because errors here are invisible until
memory pressure or unmount.

---

> **62. Mount options and `nv` parity** — ★★
> **Objective:** consistent behaviour between the CLI and the mount.
> **Deliverable:** shared option names where they overlap.
> **Acceptance:** a volume that `nv ls` reads is readable through the mount,
> and vice versa; `nvfsck` reports clean before and after a mount cycle.

That last check — `nvfsck` clean after a *read-only* mount cycle — is worth
running explicitly. A read-only mount that modifies anything is a bug, and
this catches it immediately.

---

> **63. Conformance suite integration** — ★★★
> **Objective:** run the existing NVFS conformance suite against the mount.
> **Deliverable:** a conformance runner target for the FUSE front-end.
> **Acceptance:** the read-path subset of the 2 350 checks passes through
> FUSE.

Not all 2 350 checks apply to a read-only mount; the write and crash-injection
checks are F8. Partition the suite explicitly into read-path and write-path
sets rather than leaving "some fail" as the status, and record the counts.

---

> **64. Cross-implementation agreement harness** — ★★★
> **Objective:** add the FUSE binding as a third participant alongside the host
> tools and the 65816 read-path driver.
> **Deliverable:** a harness adapter that drives operations through the mount.
> **Acceptance:** all 488 agreement checks pass with three participants.

This is the highest-value deliverable of F5. Three independent implementations
agreeing on the same volume is much stronger evidence of specification clarity
than two, and disagreements found here are almost always specification
ambiguities rather than implementation bugs — which is exactly the feedback
the spec needs before it is frozen further.

Any disagreement must be resolved by amending the specification to say which
behaviour is correct, not by patching whichever implementation is easier to
change. Record each one.

---

> **65. GATE F5 EXIT** — ★★
> **Objective:** close out NVFS read-only.
> **Acceptance:**
> (a) read-path conformance subset passes through FUSE;
> (b) 488 agreement checks pass with three participants;
> (c) inode lifetime stress test clean under ASan;
> (d) `nvfsck` clean after a read-only mount cycle;
> (e) `docs/notes/F5-exit.md` records every `libnvfs` addition, every
> specification ambiguity found, and any `fsops.h` change NVFS forced.

---

### Gate F6 — FAT32 write support (steps 66–77)

**Gate intent:** the first write path. FAT32 is chosen first for the same
reason it was chosen first for reading: its allocation model is a single
table, so the *ordering* problem can be studied without the *allocation
policy* problem alongside it.

**Governing invariant for the whole of F6–F8**, carried over from the NVFS
work and applying identically here:

> **Allocated-but-unreferenced is acceptable. Free-but-referenced is not.**

A crash that leaves a cluster marked in use but reachable from nothing costs
space until `fsck` runs. A crash that leaves a directory entry pointing at a
cluster marked free costs data, because that cluster will be handed to the
next allocation. Every write sequence in this gate must be ordered so that
only the first failure mode is possible.

---

> **66. Enable the write path in L0** — ★
> **Objective:** `bdev_write` and `bdev_flush` live.
> **Deliverable:** write support plus a write-counter hook for crash injection.
> **Acceptance:** unit tests pass; read-only mode genuinely refuses writes.

---

> **67. FAT entry writing** — ★★★
> **Objective:** modify a FAT entry correctly.
> **Deliverable:** `fat_set_next(fs, cluster, value)`.
> **Acceptance:** `fsck.fat -n` clean after a synthetic chain modification.

Two rules that are easy to miss and both cause real corruption:

1. **Preserve the top 4 bits.** FAT32 entries are 32 bits on disk but only 28
   are the cluster number. The high nibble is reserved and must be carried
   through unchanged:
   ```c
   uint32_t old = rd32(sec + within);
   wr32(sec + within, (old & 0xF0000000u) | (value & 0x0FFFFFFFu));
   ```
2. **Mirror to every FAT copy**, unless `BPB_ExtFlags` bit 7 is set, in which
   case only the FAT selected by bits 0–3 is live. Implement both cases; the
   mirroring case is the common one.

---

> **68. Cluster allocation** — ★★★
> **Objective:** find and claim free clusters.
> **Deliverable:** `fat_alloc_cluster()`, `fat_alloc_chain(n)`.
> **Acceptance:** allocation never returns a cluster already in a chain;
> exhaustion returns `FS_ENOSPC` cleanly.

Start the search at `FSI_Nxt_Free` if it validates, else at cluster 2, and
wrap. Maintain a running free count in memory, seeded at mount, rather than
rescanning.

For multi-cluster allocations, allocate all clusters and link them *before*
publishing the chain head anywhere. The intermediate state is
allocated-but-unreferenced, which the invariant permits.

---

> **69. Chain extension and truncation** — ★★★
> **Objective:** grow and shrink a file's cluster chain.
> **Deliverable:** `fat_chain_extend()`, `fat_chain_truncate()`.
> **Acceptance:** `fsck.fat -n` clean after each; sizes match after remount.

Ordering for **truncation** matters and is counter-intuitive: update the
directory entry's size and, if truncating to zero, its first-cluster field
*first*, then free the now-unreferenced clusters. Freeing first would create a
window in which the directory entry references free clusters — the forbidden
state.

---

> **70. Directory entry allocation** — ★★★
> **Objective:** find or create space for a new entry, including an LFN run.
> **Deliverable:** `fat_dir_alloc_slots(dir, n)`.
> **Acceptance:** entries are created in reusable deleted slots when available;
> the directory grows by a cluster when not.

A name needing an LFN requires `ceil(name_len / 13) + 1` **contiguous** slots.
Contiguity is required because the LFN run must immediately precede its short
entry. Scanning for a contiguous run of free-or-deleted slots is the whole
problem; growing the directory by one cluster is the fallback.

Note that a FAT32 root directory is an ordinary cluster chain and can grow,
unlike FAT12/16 where it is a fixed-size region. This is one of FAT32's
genuine improvements and it means the root needs no special case.

---

> **71. Short name generation** — ★★★★
> **Objective:** derive a unique, legal 8.3 name for a long name.
> **Deliverable:** `fat_gen_short_name()`.
> **Acceptance:** generated names are unique within the directory and are
> accepted by `fsck.fat` and by the kernel driver after remount.

The algorithm, which is more finicky than it looks:

1. Uppercase the long name.
2. Strip all spaces and all characters illegal in a short name; the illegal
   set is `" * + , / : ; < = > ? [ \ ] |` plus control characters, and `.`
   except as the final separator.
3. Take up to the first 6 surviving characters as the base, and the characters
   after the last dot (up to 3) as the extension.
4. Append `~1` to the base. If that name already exists in the directory,
   try `~2`, `~3`, and so on.
5. Beyond `~4` (or whatever threshold you pick), Windows switches to a hashed
   base plus `~1`. Matching that exactly is unnecessary; documenting the
   deviation is not.

The uniqueness check requires scanning the directory, which makes creation
O(entries). Accept it and note it.

Also set `DIR_NTRes` bits 3 and 4 appropriately when the name is losslessly
representable in 8.3 with a uniform case — that is what lets a lowercase name
round-trip without needing an LFN at all.

---

> **72. `create`, `mkdir`, `unlink`, `rmdir`** — ★★★★
> **Objective:** namespace mutation with correct ordering.
> **Deliverable:** the four vtable slots.
> **Acceptance:** `fsck.fat -n` clean after each; kernel driver agrees after
> remount.

Ordering for **create**:
1. Allocate and initialise data clusters, if any.
2. Write the LFN run and short entry into the directory.
3. Update FSInfo.

Ordering for **mkdir** — the awkward one, because a new directory must contain
`.` and `..` before it is linked:
1. Allocate one cluster.
2. Write `.` and `..` into it and flush.
3. Only then write the parent's directory entry pointing at it.

Reversing 2 and 3 leaves a window in which the parent references a directory
with no `.` or `..`, which `fsck` will flag and which some readers will choke
on.

Ordering for **unlink**:
1. Mark the directory entry deleted (`DIR_Name[0] = 0xE5`) and flush.
2. Only then free the cluster chain.

`rmdir` must verify the directory contains nothing but `.` and `..` first, and
return `FS_ENOTEMPTY` otherwise.

---

> **73. `write` and `truncate`** — ★★★
> **Objective:** file data modification.
> **Deliverable:** `fat_write()`, `fat_truncate()`.
> **Acceptance:** `fsx` runs clean for ≥ 100 000 operations.

Partial-cluster writes require read-modify-write of the affected cluster.
Extending a file past its end must zero the gap — writing at offset 10 000 in
an empty file creates a real 10 000-byte file of zeros, because FAT has no
sparse-file concept.

Enforce the 4 GiB − 1 size limit explicitly and return `FS_EINVAL` (mapping to
`-EFBIG`) rather than silently truncating `DIR_FileSize`.

---

> **74. Timestamps, `setattr`, FSInfo maintenance** — ★★
> **Objective:** metadata updates.
> **Deliverable:** `utimens`, `chmod` (read-only bit only), FSInfo updates.
> **Acceptance:** `difftest.sh` `stats` passes after modification.

`chmod` on FAT can only express one thing: whether the write bits are set,
which maps to `DIR_Attr` bit 0. Everything else must be accepted and silently
ignored, or the kernel's `default_permissions` handling produces surprising
failures. `chown` is a no-op that returns success.

---

> **75. `fsx` and `pjdfstest`** — ★★★★
> **Objective:** exercise the write path far harder than by hand.
> **Deliverable:** a runner script and recorded results.
> **Acceptance:** `fsx` clean for ≥ 1 000 000 operations; the applicable
> `pjdfstest` subset passes or every failure is explained.

```bash
fsx -N 1000000 -S 0 /mnt/fuse/fsxfile
```

`pjdfstest` will report failures for everything FAT cannot express — ownership,
permission bits, hard links, special files. Those are expected. Produce a
documented exclusion list rather than a pass/fail number, and make sure every
exclusion is a genuine format limitation rather than a bug.

---

> **76. Crash-injection testing** — ★★★★★
> **Objective:** verify the ordering invariant survives failure at any point.
> **Deliverable:** `tests/scripts/crashinject.sh` and results.
> **Acceptance:** for every injection point, `fsck.fat -n` reports either a
> clean filesystem or only lost-cluster warnings — never a cross-linked chain,
> never a directory entry referencing a free cluster.

The technique, which is the same one that found two real ordering bugs in the
NVFS work and is the reason it is applied here:

1. Instrument `bdev_write` with a counter and a configurable abort point.
2. Run a workload (create, write, rename, delete) with abort disabled and
   record the total write count *N*.
3. For *i* in 1..*N*: copy the pristine image, re-run the workload with the
   abort point set to *i*, then run `fsck.fat -n` on the result.
4. Classify each outcome.

For *N* in the thousands this is slow but entirely automatable, and it is the
only way to actually test the invariant rather than to assert it. Run it
overnight.

The acceptance classification matters: lost clusters are *allowed* — they are
the allocated-but-unreferenced state the invariant permits. Cross-linked
chains and dangling references are *not*, and each one is a real ordering bug
with a specific write sequence to fix.

---

> **77. GATE F6 EXIT** — ★★★
> **Acceptance:**
> (a) `fsx` clean for ≥ 1 000 000 operations;
> (b) `pjdfstest` subset passes with a documented exclusion list;
> (c) crash injection produces no forbidden state at any injection point;
> (d) after every test, the kernel driver mounts the image and `difftest.sh`
> still passes;
> (e) `docs/notes/F6-exit.md` records the ordering rules as an explicit list.

---

### Gate F7 — EXT2 write support (steps 78–91)

**Gate intent:** the hardest gate. ext2 has no journal, so ordering is the
entire correctness argument, and allocation policy has real consequences.

---

> **78. Bitmap read/write and allocation primitives** — ★★★
> **Objective:** allocate and free blocks and inodes.
> **Deliverable:** `ext2/ext2_alloc.c`.
> **Acceptance:** `e2fsck -fn` clean after synthetic allocate/free cycles.

The bitmaps are one block each per group; bit *n* corresponds to the *n*th
block or inode of that group. Block numbering within a group starts at
`s_first_data_block + group * s_blocks_per_group`; inode numbering starts at
`group * s_inodes_per_group + 1`. Both off-by-ones are traditional sources of
bugs and both should have a unit test.

Every allocation must update three counters consistently: the bit in the
bitmap, `bg_free_blocks_count` (or `bg_free_inodes_count`) in the descriptor,
and `s_free_blocks_count` (or `s_free_inodes_count`) in the superblock. A
mismatch between them is exactly what `e2fsck` checks first.

---

> **79. Block group selection policy** — ★★★
> **Objective:** decide where to allocate, and understand why it matters.
> **Deliverable:** a linear allocator plus a documented comparison.
> **Acceptance:** files are allocated in the same group as their parent
> directory where possible; fragmentation is measurable and recorded.

Implement the simple policy first: allocate a new inode in the parent
directory's group if it has room, otherwise the first group with room;
allocate data blocks in the inode's own group, otherwise search outward.

Then read about and document what Linux actually does — the Orlov allocator,
which spreads *directories* across groups to avoid filling one group with a
whole subtree, while keeping *files* near their parent directory. Do not
implement it. Do write down what problem it solves and measure your
allocator's fragmentation against `e2freefrag` so the comparison is concrete.

This is the step where OSTEP's FFS chapter becomes directly useful: the
group-locality idea is FFS's cylinder groups, and the reasoning transfers
exactly.

---

> **80. Indirect block allocation** — ★★★★
> **Objective:** grow a file across indirection levels.
> **Deliverable:** `ext2_bmap_alloc()` — the allocating counterpart to step 42.
> **Acceptance:** a file grown from 0 to past the triple-indirect threshold has
> a block list identical to what the kernel driver produces for the same size,
> and `e2fsck -fn` is clean.

A newly allocated indirect block **must be zeroed before it is linked**, or the
garbage in it becomes a set of block pointers the moment the parent points at
it. Order: allocate, zero, flush, then link.

`i_blocks` must be incremented for the indirect blocks too, not only for data
blocks — this is where the 512-byte-unit gotcha from step 41 becomes a write
bug rather than a read bug.

---

> **81. Directory entry insertion** — ★★★★
> **Objective:** add a name to a directory.
> **Deliverable:** `ext2_dir_insert()`.
> **Acceptance:** `e2fsck -fn` clean; the kernel driver reads the new name.

Insertion works by finding a record whose `rec_len` exceeds what it actually
needs — either a live record with slack, or a hole — and splitting it:

```
needed = 8 + name_len, rounded up to a multiple of 4
for each record:
    actual = 8 + name_len(record), rounded up to 4
    slack  = rec_len - actual
    if record.inode == 0 and rec_len >= needed:  reuse whole record
    if slack >= needed:                          split
```

Splitting means shortening the existing record's `rec_len` to `actual` and
writing the new record at `off + actual` with `rec_len = slack`. If no block
has room, append a new block to the directory, initialise it as one record
spanning the whole block with `inode = 0`, and insert there.

**Refuse to insert into any directory with `EXT2_INDEX_FL` set** (§43). Return
`FS_ENOTSUP`. Since step 4 formats with `^dir_index`, this should never fire
on your own images, but it will fire on any image made by a default `mke2fs`,
and silently corrupting such an image is exactly the failure this gate must
not have.

---

> **82. Link counts** — ★★★
> **Objective:** maintain `i_links_count` correctly.
> **Deliverable:** correct increment/decrement across all namespace operations.
> **Acceptance:** `e2fsck -fn` reports no link-count mismatches.

The rules, which must be applied consistently:

- A new regular file has `i_links_count == 1`.
- A new directory has `i_links_count == 2` — one for its name in the parent,
  one for its own `.`.
- Creating a subdirectory increments the *parent's* count by 1, for the child's
  `..`.
- `unlink` decrements by 1; the inode is freed when the count reaches 0 **and**
  no handle is open.
- `rmdir` decrements the directory's count by 2 and the parent's by 1.

Link-count errors are the most common thing `e2fsck` finds in a homegrown
implementation, and they are invisible until it runs. Run it after every test.

---

> **83. Inode and block freeing** — ★★★
> **Objective:** release resources in the correct order.
> **Deliverable:** `ext2_free_inode()`, `ext2_free_blocks()`.
> **Acceptance:** freed blocks are reusable; `e2fsck -fn` clean.

Ordering for **unlink**, which is the canonical illustration of the invariant:

1. Remove the directory entry and flush. *Now the inode is unreferenced.*
2. Decrement `i_links_count`. If it is still non-zero, stop.
3. Set `i_dtime`, then free the data and indirect blocks, then free the inode.

A crash between 1 and 3 leaves an orphaned inode and its blocks marked in use
— allocated-but-unreferenced, permitted, and exactly what `e2fsck` reports as
"unattached inode" and repairs into `lost+found`. Doing it the other way round
would leave a directory entry pointing at a free inode, which is the forbidden
state.

Note that step 3 must free *deepest first*: free the blocks an indirect block
points to before freeing the indirect block itself, or the pointers are lost.

---

> **84. `create`, `mkdir`, `unlink`, `rmdir`, `symlink`, `link`** — ★★★★
> **Objective:** the full namespace operation set, which ext2 can express fully.
> **Deliverable:** the vtable slots.
> **Acceptance:** `e2fsck -fn` clean after each; the kernel driver agrees.

`mkdir` ordering mirrors F6: allocate the inode, allocate and initialise its
first block with `.` and `..`, flush, then insert the entry into the parent and
increment the parent's link count.

`symlink` chooses fast or slow storage by target length: ≤ 59 bytes goes
inline into `i_block` with `i_blocks = 0`; longer targets get a data block.
Test both, and test the boundary at exactly 59 and 60 bytes.

`link` is the one operation FAT could not express at all. Creating it is a
directory insertion plus an `i_links_count` increment — and noticing how
little code that is, compared with what it enables, is part of the point of
this gate.

---

> **85. `rename`** — ★★★★★
> **Objective:** the hardest namespace operation.
> **Deliverable:** `ext2_rename()`.
> **Acceptance:** `e2fsck -fn` clean for all six cases below; `pjdfstest`
> rename tests pass.

The cases, each of which needs its own test:

1. Rename within a directory, target does not exist.
2. Rename across directories, target does not exist.
3. Rename over an existing file — the target must be unlinked atomically
   enough that a crash never loses both.
4. Rename a directory across directories — the moved directory's `..` must be
   updated, and both parents' link counts adjusted.
5. Rename a directory into its own descendant — must return `FS_EINVAL`. This
   requires walking up from the destination to the root checking for the source
   inode, which is an O(depth) check that must not be skipped.
6. Rename where source and destination are the same file — must be a no-op
   returning success, not an unlink.

Ordering that satisfies the invariant: insert the new entry first, then remove
the old one. A crash between them leaves the file reachable by both names with
a link count of 1, which `e2fsck` repairs by fixing the count. The reverse
order would leave a window in which the file is reachable by neither name.

---

> **86. `write`, `truncate`, `setattr`** — ★★★
> **Objective:** file data and metadata modification.
> **Deliverable:** the remaining vtable slots.
> **Acceptance:** `fsx` clean for ≥ 1 000 000 operations.

ext2, unlike FAT, supports **sparse files**: writing at offset 10 000 000 in
an empty file allocates only the blocks actually written and leaves holes
elsewhere. `truncate` to a larger size creates a hole, not zeros on disk.
`i_blocks` must reflect only the blocks actually allocated, which is what
makes `du` differ from `ls -l` on a sparse file — verify that it does, against
the kernel driver.

Truncation to a smaller size must free blocks deepest-first, as in step 83,
and must handle the case where truncation removes an entire indirection level.

---

> **87. Superblock and backup maintenance** — ★★★
> **Objective:** keep the primary and backup superblocks consistent.
> **Deliverable:** superblock write-back including backups.
> **Acceptance:** `e2fsck -fn -b <backup>` succeeds against a backup
> superblock after a write session.

Update the backups on unmount rather than on every change; that is what the
kernel does and it avoids turning every allocation into N writes. Use the
sparse-superblock group set computed in step 51.

Also maintain `s_state`: clear the `EXT2_VALID_FS` bit on mount-for-write and
set it on clean unmount. That single bit is how `e2fsck` and the kernel know
whether a volume was cleanly unmounted, and implementing it is what makes the
mount/unmount cycle honest.

---

> **88. Cache flushing and `fsync`** — ★★★
> **Objective:** durability semantics that mean something.
> **Deliverable:** `ext2_sync()`, `fsync`, `fsyncdir`.
> **Acceptance:** after `fsync` returns, killing the process leaves the file's
> data and metadata intact.

Test it by actually doing that: write, `fsync`, `kill -9`, remount, verify.
An `fsync` that returns before the data is on disk is worse than no `fsync`,
because callers rely on it.

Revisit the FUSE caching settings from step 11 here. `writeback_cache` lets the
kernel batch and reorder writes, which is a performance win and an ordering
hazard; for a first correct implementation, leave it off and note the decision.

---

> **89. `fsx` and `pjdfstest`** — ★★★★
> **Acceptance:** `fsx` clean for ≥ 1 000 000 operations; `pjdfstest` passes
> substantially more than it did for FAT32, with every remaining failure
> explained.

ext2 can express nearly everything POSIX requires, so the exclusion list here
should be very short. A long exclusion list is a signal that something is
wrong, not that ext2 is limited.

---

> **90. Crash-injection testing** — ★★★★★
> **Objective:** the ordering argument, tested rather than asserted.
> **Acceptance:** at every injection point, `e2fsck -fn` reports either clean,
> or only unattached inodes / incorrect counts — never a block claimed by two
> inodes, never a directory entry pointing at a free inode.

Same method as step 76, with `e2fsck -fn` as the oracle. Classify outcomes:

| `e2fsck` finding | Verdict |
|---|---|
| Clean | pass |
| Unattached inode | pass — allocated-but-unreferenced |
| Free blocks count wrong | pass — recomputable |
| Free inodes count wrong | pass — recomputable |
| **Block claimed by two inodes** | **fail — ordering bug** |
| **Entry points to free/unused inode** | **fail — ordering bug** |
| **Directory corrupted** | **fail — ordering bug** |

Then write the conclusion in the notes: this is precisely the set of failures
a journal eliminates, and having generated them by hand is the most direct
possible answer to why ext3 exists. Tie it back to OSTEP's crash-consistency
chapter.

---

> **91. GATE F7 EXIT** — ★★★
> **Acceptance:**
> (a) `fsx` and `pjdfstest` clean per steps 89;
> (b) crash injection produces no forbidden state;
> (c) `e2fsck -fn` clean after every test scenario;
> (d) an htree directory is refused for writes, not corrupted;
> (e) `docs/notes/F7-exit.md` records the ordering rules, the allocator
> comparison, and the journal argument;
> (f) DN-FS-EXT2-001 updated with everything the write path revealed.

---

### Gate F8 — NVFS write support and harness completion (steps 92–99)

---

> **92. Write-side `libnvfs` audit** — ★★
> **Deliverable:** the step 56 table extended to the write slots.
> **Acceptance:** every write-side `fsops` slot classified.

---

> **93. Write-side adapter and binding** — ★★★
> **Deliverable:** the remaining `fuse_lowlevel_ops` write callbacks.
> **Acceptance:** files can be created, written, renamed and deleted through
> the mount; `nvfsck` clean afterwards.

---

> **94. Durability and ordering review** — ★★★★
> **Objective:** confirm the FUSE binding does not weaken NVFS's existing
> ordering guarantees.
> **Deliverable:** a review note mapping each FUSE write callback to the
> `libnvfs` ordering guarantee it relies on.
> **Acceptance:** no callback bypasses an ordering constraint the spec requires.

The specific risk: FUSE's caching can reorder or batch writes in ways the
`nv` CLI never did, so guarantees that held for the CLI may not hold through
a mount. Check each one explicitly rather than assuming the library's
guarantees carry through unchanged.

---

> **95. Full conformance suite** — ★★★
> **Acceptance:** all 2 350 checks pass through the FUSE front-end, including
> the crash-injection subset.

---

> **96. Agreement harness, write operations** — ★★★
> **Acceptance:** all 488 checks pass with three participants, now including
> write operations where all three participants support them.

---

> **97. `fsx` and `pjdfstest` for NVFS** — ★★★
> **Objective:** apply the same external pressure the other two received.
> **Acceptance:** `fsx` clean for ≥ 1 000 000 operations; `pjdfstest`
> exclusions documented as format limitations.

Running the *same* external suites against all three filesystems is what makes
the comparison meaningful: where NVFS's exclusion list differs from ext2's,
that difference is a design decision, and it is worth being able to defend
each one.

---

> **98. Multi-threaded loop experiment** — ★★★★
> **Objective:** find out what breaks when `-s` is removed.
> **Deliverable:** results in `docs/notes/F8-threading.md`.
> **Acceptance:** either the driver is thread-safe and passes the suites under
> `fuse_session_loop_mt`, or the required locking is documented as future work.

Do this last, deliberately, and only after everything passes single-threaded.
In libfuse 3.12 and later the loop configuration is opaque:

```c
struct fuse_loop_config *cfg = fuse_loop_cfg_create();
fuse_loop_cfg_set_clone_fd(cfg, 1);
fuse_loop_cfg_set_max_threads(cfg, 4);
ret = fuse_session_loop_mt(se, cfg);
fuse_loop_cfg_destroy(cfg);
```

Note that `max_idle_threads` is deprecated in favour of `max_threads`.

Whatever the outcome, this is a legitimate stopping point: "single-threaded,
documented" is a fine answer for a learning implementation, and knowing
exactly what would need locking is most of the value.

---

> **99. GATE F8 EXIT — track complete** — ★★★
> **Acceptance:**
> (a) all three filesystems mount read-write and pass their respective suites;
> (b) all three are clean under ASan, UBSan and Valgrind;
> (c) all three survive their corruption corpora and AFL++;
> (d) crash injection clean for all three;
> (e) `fsops.h` change log complete and reviewed;
> (f) **DN-FS-VFS-001 drafted** from the `fsops` change log — this is the
> deliverable that feeds back into the noVa64 critical path.

---

### Gate F9 — Windows and macOS (optional, steps 100–107)

**Gate intent:** find out how portable the layered design actually was. Attempt
only after F8. Skipping this gate costs the project nothing.

---

> **100. WinFsp evaluation** — ★★
> **Objective:** understand what the FUSE compatibility layer provides.
> **Deliverable:** `docs/notes/F9-winfsp.md`.
> **Acceptance:** the differences below are enumerated with a plan for each.

WinFsp ships a FUSE compatibility layer with both a FUSE 2.8 header
(`fuse/fuse.h`) and a FUSE 3.2 header (`fuse3/fuse.h`), supports Windows 7
through 11 on x86, x64 and ARM64, and is licensed GPLv3 with an exception for
free and open source software. Target the FUSE 3 header.

---

> **101. Build environment** — ★★★
> **Objective:** get the codebase compiling on Windows.
> **Deliverable:** a working build with either MinGW-w64 or MSVC.
> **Acceptance:** L0 and L1 compile and their unit tests pass on Windows.

Note that with MSVC the compatibility layer uses `struct FUSE_STAT` rather
than `struct stat`, so the L3 binding needs a typedef shim. L0 needs `_open`,
`_read`, `_lseeki64` or the Win32 file API instead of `pread`. L1 and L2
should need no changes at all — and if they do, that is a design finding worth
recording, because it means the layering leaked.

---

> **102. Port FAT32 read-only first** — ★★★
> **Objective:** the simplest driver as the porting pilot.
> **Acceptance:** the image mounts as a drive letter and files are readable
> from Explorer and `cmd`.

```
fat32fuse-x64.exe --image=C:\images\fat32.img -o uid=-1 -F NTFS -m X:
```

---

> **103. Permissions and ownership mapping** — ★★★
> **Objective:** make the Unix permission model behave on Windows.
> **Acceptance:** files are accessible without permission errors.

`-o uid=-1` instructs the WinFsp FUSE layer to present all files as owned by
the user who launched the filesystem, which is almost always what you want for
an image-backed driver. Symbolic links become NTFS reparse points.

---

> **104. Path and case semantics** — ★★★
> **Objective:** handle backslashes, case-insensitivity and reserved names.
> **Acceptance:** files whose names differ only by case, and files named `CON`,
> `NUL`, `AUX` or `COM1`, behave predictably or are reported cleanly.

Windows is case-insensitive and case-preserving, which happens to match FAT32
and to conflict with ext2. An ext2 image containing `README` and `readme` in
the same directory is legal on Linux and cannot be represented on Windows;
decide the policy — expose one and hide the other, or mangle — and document it.

---

> **105. Mount semantics** — ★★
> **Objective:** drive-letter and directory mounts.
> **Acceptance:** both work; the mount directory is cleaned up on exit.

Setting the filesystem name to `NTFS` (`-F NTFS`) is required for some
Administrator scenarios, because Windows will only run executables as
Administrator from a filesystem identifying as NTFS. WinFsp creates the mount
directory itself and deletes it on exit, because NTFS does not allow reparse
points on non-empty directories.

---

> **106. Port EXT2 and NVFS** — ★★
> **Objective:** confirm the pilot's lessons generalise.
> **Acceptance:** both mount read-only on Windows.

If step 101 was done properly, these two should be nearly free — which is the
actual result being tested. Measure the effort and record it; a large number
here is a finding about the architecture, not about Windows.

---

> **107. macOS survey (documentation only)** — ★
> **Objective:** record the options without implementing.
> **Deliverable:** a short section in the F9 notes.

macFUSE provides both libfuse2 and libfuse3 reference APIs and, on recent
macOS, an FSKit backend (FSKit is available from macOS 15.4) that runs
supported filesystems in user space without a kernel extension, selected with
`-o backend=fskit`. That backend is supported only with libfuse3 and carries
caveats: mount points outside `/Volumes` are unsupported, and
`fuse_context_t` is unavailable. FUSE-T is a kernel-extension-free alternative
that bridges FUSE to a local NFSv4 server; it is a drop-in for macFUSE's API,
but that API tracks the older macFUSE variant rather than current Linux
libfuse. Neither is on the critical path.

---

## Appendix A — FAT32 on-disk reference

All offsets are in bytes, all multi-byte fields little-endian. Verify against
a real image with `xxd` before implementing; this table is a transcription and
transcriptions have errors.

### A.1 Boot sector / BPB (sector 0)

| Off | Sz | Name | Notes |
|---:|---:|---|---|
| 0 | 3 | `BS_jmpBoot` | `EB xx 90` or `E9 xx xx` |
| 3 | 8 | `BS_OEMName` | informational |
| 11 | 2 | `BPB_BytsPerSec` | 512, 1024, 2048 or 4096 |
| 13 | 1 | `BPB_SecPerClus` | power of two, 1–128 |
| 14 | 2 | `BPB_RsvdSecCnt` | ≥ 1; typically 32 for FAT32 |
| 16 | 1 | `BPB_NumFATs` | typically 2 |
| 17 | 2 | `BPB_RootEntCnt` | **0 for FAT32** |
| 19 | 2 | `BPB_TotSec16` | **0 for FAT32** |
| 21 | 1 | `BPB_Media` | `0xF8` fixed, `0xF0` removable |
| 22 | 2 | `BPB_FATSz16` | **0 for FAT32** |
| 24 | 2 | `BPB_SecPerTrk` | legacy geometry |
| 26 | 2 | `BPB_NumHeads` | legacy geometry |
| 28 | 4 | `BPB_HiddSec` | sectors before this partition |
| 32 | 4 | `BPB_TotSec32` | total sectors |
| 36 | 4 | `BPB_FATSz32` | sectors per FAT |
| 40 | 2 | `BPB_ExtFlags` | bits 0–3 active FAT; bit 7 disables mirroring |
| 42 | 2 | `BPB_FSVer` | must be 0 |
| 44 | 4 | `BPB_RootClus` | first cluster of root; normally 2 |
| 48 | 2 | `BPB_FSInfo` | sector number of FSInfo; normally 1 |
| 50 | 2 | `BPB_BkBootSec` | backup boot sector; normally 6 |
| 52 | 12 | `BPB_Reserved` | zero |
| 64 | 1 | `BS_DrvNum` | `0x80` |
| 65 | 1 | `BS_Reserved1` | zero |
| 66 | 1 | `BS_BootSig` | `0x29` if the next three fields are present |
| 67 | 4 | `BS_VolID` | volume serial |
| 71 | 11 | `BS_VolLab` | space-padded label |
| 82 | 8 | `BS_FilSysType` | `"FAT32   "` — **informational only** |
| 510 | 2 | signature | `0x55 0xAA` |

### A.2 FSInfo sector

| Off | Sz | Name | Value |
|---:|---:|---|---|
| 0 | 4 | `FSI_LeadSig` | `0x41615252` |
| 4 | 480 | `FSI_Reserved1` | zero |
| 484 | 4 | `FSI_StrucSig` | `0x61417272` |
| 488 | 4 | `FSI_Free_Count` | free clusters, or `0xFFFFFFFF` if unknown |
| 492 | 4 | `FSI_Nxt_Free` | hint for next free cluster |
| 496 | 12 | `FSI_Reserved2` | zero |
| 508 | 4 | `FSI_TrailSig` | `0xAA550000` |

Both counts are advisory. Never trust them without validating against
`CountOfClusters`.

### A.3 FAT entry values (after masking to 28 bits)

| Value | Meaning |
|---|---|
| `0x0000000` | free cluster |
| `0x0000001` | reserved; never a valid successor |
| `0x0000002`–`MaxCluster` | next cluster in chain |
| `MaxCluster+1`–`0xFFFFFF6` | reserved; treat as corrupt |
| `0x0FFFFFF7` | bad cluster |
| `0x0FFFFFF8`–`0x0FFFFFFF` | end of chain |

Entry 0 holds the media byte in its low 8 bits; entry 1 holds shutdown/error
flags. Neither is a cluster. On write, preserve bits 28–31.

### A.4 Short directory entry (32 bytes)

| Off | Sz | Name | Notes |
|---:|---:|---|---|
| 0 | 11 | `DIR_Name` | 8+3, space-padded, no dot stored |
| 11 | 1 | `DIR_Attr` | see A.5 |
| 12 | 1 | `DIR_NTRes` | bit 3 = lowercase base, bit 4 = lowercase ext |
| 13 | 1 | `DIR_CrtTimeTenth` | 0–199, tenths of a second |
| 14 | 2 | `DIR_CrtTime` | |
| 16 | 2 | `DIR_CrtDate` | |
| 18 | 2 | `DIR_LstAccDate` | date only, 1-day resolution |
| 20 | 2 | `DIR_FstClusHI` | high 16 bits of first cluster |
| 22 | 2 | `DIR_WrtTime` | |
| 24 | 2 | `DIR_WrtDate` | |
| 26 | 2 | `DIR_FstClusLO` | low 16 bits of first cluster |
| 28 | 4 | `DIR_FileSize` | bytes; 0 for directories |

`first_cluster = (DIR_FstClusHI << 16) | DIR_FstClusLO`. A value of 0 means an
empty file with no chain.

### A.5 Attribute byte

| Bit | Value | Meaning |
|---:|---:|---|
| 0 | `0x01` | read-only |
| 1 | `0x02` | hidden |
| 2 | `0x04` | system |
| 3 | `0x08` | volume ID |
| 4 | `0x10` | directory |
| 5 | `0x20` | archive |
| — | `0x0F` | LFN entry (RO\|HID\|SYS\|VOL together) |

### A.6 First-byte special values of `DIR_Name`

| Value | Meaning |
|---|---|
| `0x00` | free, and all following entries are free — stop scanning |
| `0xE5` | deleted — skip and continue |
| `0x05` | real first byte is `0xE5` (code-page lead-byte escape) |

### A.7 LFN entry (32 bytes)

| Off | Sz | Name | Notes |
|---:|---:|---|---|
| 0 | 1 | `LDIR_Ord` | ordinal; `0x40` set on the first entry on disk |
| 1 | 10 | `LDIR_Name1` | UTF-16 units 1–5 |
| 11 | 1 | `LDIR_Attr` | always `0x0F` |
| 12 | 1 | `LDIR_Type` | always 0 |
| 13 | 1 | `LDIR_Chksum` | checksum of the associated short name |
| 14 | 12 | `LDIR_Name2` | UTF-16 units 6–11 |
| 26 | 2 | `LDIR_FstClusLO` | always 0 |
| 28 | 4 | `LDIR_Name3` | UTF-16 units 12–13 |

13 units per entry, maximum 255 characters, terminated by `0x0000` and padded
with `0xFFFF`. Entries appear in reverse order immediately before the short
entry.

### A.8 Date and time encoding

```
Date:  bits 15..9 = year - 1980   bits 8..5 = month (1-12)   bits 4..0 = day (1-31)
Time:  bits 15..11 = hour (0-23)  bits 10..5 = minute (0-59) bits 4..0 = second / 2
```

Resolution: write time 2 s, create time 10 ms via `DIR_CrtTimeTenth`, access
date 1 day. All timestamps are local time with no timezone recorded.

### A.9 Cluster arithmetic

```
RootDirSectors  = 0                                            (FAT32)
FirstDataSector = BPB_RsvdSecCnt + BPB_NumFATs * BPB_FATSz32
DataSec         = BPB_TotSec32 - FirstDataSector
CountOfClusters = DataSec / BPB_SecPerClus
MaxCluster      = CountOfClusters + 1
FirstSectorOfCluster(N) = (N - 2) * BPB_SecPerClus + FirstDataSector
FATOffset(N)    = N * 4
FATSector(N)    = BPB_RsvdSecCnt + FATOffset(N) / BPB_BytsPerSec
FATEntryOff(N)  = FATOffset(N) % BPB_BytsPerSec
```

### A.10 Limits

| Property | Value |
|---|---|
| Max file size | 4 GiB − 1 (`DIR_FileSize` is 32-bit) |
| Max filename | 255 UTF-16 units |
| Max path | not defined by the format |
| Cluster size | 512 B – 32 KiB (`BytsPerSec × SecPerClus ≤ 32768`) |
| FAT32 cluster count | ≥ 65 525 |
| Hard links | not supported |
| Symlinks | not supported |
| Permissions | not supported (read-only bit only) |
| Sparse files | not supported |
| Timestamps | local time, 2-second resolution |

---

## Appendix B — EXT2 on-disk reference

### B.1 Superblock (byte offset 1024, size 1024)

| Off | Sz | Name | Notes |
|---:|---:|---|---|
| 0 | 4 | `s_inodes_count` | total inodes |
| 4 | 4 | `s_blocks_count` | total blocks |
| 8 | 4 | `s_r_blocks_count` | reserved for root |
| 12 | 4 | `s_free_blocks_count` | |
| 16 | 4 | `s_free_inodes_count` | |
| 20 | 4 | `s_first_data_block` | 1 if block size is 1024, else 0 |
| 24 | 4 | `s_log_block_size` | block size = `1024 << value` |
| 28 | 4 | `s_log_frag_size` | unused in practice |
| 32 | 4 | `s_blocks_per_group` | |
| 36 | 4 | `s_frags_per_group` | |
| 40 | 4 | `s_inodes_per_group` | |
| 44 | 4 | `s_mtime` | last mount time |
| 48 | 4 | `s_wtime` | last write time |
| 52 | 2 | `s_mnt_count` | mounts since last check |
| 54 | 2 | `s_max_mnt_count` | |
| 56 | 2 | `s_magic` | **`0xEF53`** |
| 58 | 2 | `s_state` | 1 = `EXT2_VALID_FS`, 2 = `EXT2_ERROR_FS` |
| 60 | 2 | `s_errors` | 1 continue, 2 remount-ro, 3 panic |
| 62 | 2 | `s_minor_rev_level` | |
| 64 | 4 | `s_lastcheck` | |
| 68 | 4 | `s_checkinterval` | |
| 72 | 4 | `s_creator_os` | 0 = Linux |
| 76 | 4 | `s_rev_level` | 0 = GOOD_OLD, 1 = DYNAMIC |
| 80 | 2 | `s_def_resuid` | |
| 82 | 2 | `s_def_resgid` | |
| 84 | 4 | `s_first_ino` | first non-reserved inode; 11 |
| 88 | 2 | `s_inode_size` | 128 or larger; **read it, never assume** |
| 90 | 2 | `s_block_group_nr` | which group this copy lives in |
| 92 | 4 | `s_feature_compat` | |
| 96 | 4 | `s_feature_incompat` | |
| 100 | 4 | `s_feature_ro_compat` | |
| 104 | 16 | `s_uuid` | |
| 120 | 16 | `s_volume_name` | |
| 136 | 64 | `s_last_mounted` | |
| 200 | 4 | `s_algo_bitmap` | compression, unused |
| 204 | 1 | `s_prealloc_blocks` | |
| 205 | 1 | `s_prealloc_dir_blocks` | |
| 208 | 16 | `s_journal_uuid` | ext3 |
| 224 | 4 | `s_journal_inum` | ext3; journal inode, normally 8 |
| 228 | 4 | `s_journal_dev` | |
| 232 | 4 | `s_last_orphan` | |
| 236 | 16 | `s_hash_seed[4]` | htree |
| 252 | 1 | `s_def_hash_version` | htree |
| 256 | 4 | `s_default_mount_opts` | |
| 260 | 4 | `s_first_meta_bg` | |

Fields from offset 84 onward exist only when `s_rev_level == 1`.

### B.2 Feature flags

**COMPAT** — unknown bits may be ignored; mount read-write.

| Value | Name |
|---|---|
| `0x0001` | `DIR_PREALLOC` |
| `0x0002` | `IMAGIC_INODES` |
| `0x0004` | `HAS_JOURNAL` (ext3) |
| `0x0008` | `EXT_ATTR` |
| `0x0010` | `RESIZE_INO` |
| `0x0020` | `DIR_INDEX` (htree) |

**INCOMPAT** — unknown bits mean refuse to mount.

| Value | Name | Effect on an ext2 reader |
|---|---|---|
| `0x0001` | `COMPRESSION` | refuse |
| `0x0002` | `FILETYPE` | **supported** — dirent type byte |
| `0x0004` | `RECOVER` | journal needs replay; refuse |
| `0x0008` | `JOURNAL_DEV` | refuse |
| `0x0010` | `META_BG` | refuse |
| `0x0040` | `EXTENTS` | **refuse — `i_block` is an extent tree, not pointers** |
| `0x0080` | `64BIT` | refuse |
| `0x0100` | `MMP` | refuse |
| `0x0200` | `FLEX_BG` | refuse |
| `0x0400` | `EA_INODE` | refuse |
| `0x1000` | `DIRDATA` | refuse |
| `0x2000` | `CSUM_SEED` | refuse |
| `0x4000` | `LARGEDIR` | refuse |
| `0x8000` | `INLINE_DATA` | refuse |
| `0x10000` | `ENCRYPT` | refuse |

**RO_COMPAT** — unknown bits mean mount read-only.

| Value | Name | Notes |
|---|---|---|
| `0x0001` | `SPARSE_SUPER` | **supported** — backups in groups 0, 1, 3ⁿ, 5ⁿ, 7ⁿ |
| `0x0002` | `LARGE_FILE` | **supported** — `i_dir_acl` is `i_size_high` |
| `0x0004` | `BTREE_DIR` | |
| `0x0008` | `HUGE_FILE` | |
| `0x0010` | `GDT_CSUM` | |
| `0x0020` | `DIR_NLINK` | |
| `0x0040` | `EXTRA_ISIZE` | |
| `0x0100` | `QUOTA` | |
| `0x0200` | `BIGALLOC` | |
| `0x0400` | `METADATA_CSUM` | plain ext2 code would not maintain checksums |

### B.3 Block group descriptor (32 bytes)

| Off | Sz | Name |
|---:|---:|---|
| 0 | 4 | `bg_block_bitmap` |
| 4 | 4 | `bg_inode_bitmap` |
| 8 | 4 | `bg_inode_table` |
| 12 | 2 | `bg_free_blocks_count` |
| 14 | 2 | `bg_free_inodes_count` |
| 16 | 2 | `bg_used_dirs_count` |
| 18 | 2 | `bg_pad` |
| 20 | 12 | `bg_reserved` |

### B.4 Inode (128 bytes for rev 0; `s_inode_size` in rev 1)

| Off | Sz | Name | Notes |
|---:|---:|---|---|
| 0 | 2 | `i_mode` | type nibble + permission bits |
| 2 | 2 | `i_uid` | |
| 4 | 4 | `i_size` | low 32 bits |
| 8 | 4 | `i_atime` | Unix epoch seconds |
| 12 | 4 | `i_ctime` | |
| 16 | 4 | `i_mtime` | |
| 20 | 4 | `i_dtime` | deletion time; non-zero means deleted |
| 24 | 2 | `i_gid` | |
| 26 | 2 | `i_links_count` | |
| 28 | 4 | `i_blocks` | **512-byte units**, includes indirect blocks |
| 32 | 4 | `i_flags` | see B.6 |
| 36 | 4 | `i_osd1` | |
| 40 | 60 | `i_block[15]` | 12 direct, 1 single, 1 double, 1 triple |
| 100 | 4 | `i_generation` | NFS |
| 104 | 4 | `i_file_acl` | extended attribute block |
| 108 | 4 | `i_dir_acl` | **= `i_size_high` for regular files with LARGE_FILE** |
| 112 | 4 | `i_faddr` | |
| 116 | 12 | `i_osd2` | |

### B.5 `i_mode` type bits

| Value | Type |
|---|---|
| `0x1000` | FIFO |
| `0x2000` | character device |
| `0x4000` | directory |
| `0x6000` | block device |
| `0x8000` | regular file |
| `0xA000` | symbolic link |
| `0xC000` | socket |

Permission bits `0x0FFF`, plus `0x0200` sticky, `0x0400` setgid, `0x0800`
setuid. These match POSIX `S_IF*` exactly.

### B.6 `i_flags` (selected)

| Value | Name | Relevance |
|---|---|---|
| `0x00000008` | `SYNC_FL` | |
| `0x00000010` | `IMMUTABLE_FL` | |
| `0x00000020` | `APPEND_FL` | |
| `0x00000080` | `NOATIME_FL` | |
| `0x00001000` | `INDEX_FL` | **htree directory — read-safe, write-unsafe** |
| `0x00002000` | `IMAGIC_FL` | |
| `0x00004000` | `JOURNAL_DATA_FL` | ext3 |

### B.7 Reserved inodes

| Number | Purpose |
|---:|---|
| 1 | bad blocks |
| 2 | **root directory** |
| 3 | user quota |
| 4 | group quota |
| 5 | boot loader |
| 6 | undelete directory |
| 7 | reserved group descriptors (resize) |
| 8 | journal (ext3) |
| 11 | `lost+found` by convention; `s_first_ino` |

Inode numbering starts at 1. There is no inode 0.

### B.8 Directory entry (`ext2_dir_entry_2`)

| Off | Sz | Name | Notes |
|---:|---:|---|---|
| 0 | 4 | `inode` | 0 means unused |
| 4 | 2 | `rec_len` | to next record; multiple of 4; ≥ 8 |
| 6 | 1 | `name_len` | |
| 7 | 1 | `file_type` | only if `FILETYPE` is set |
| 8 | `name_len` | `name` | not NUL-terminated |

`file_type`: 0 unknown, 1 regular, 2 directory, 3 char dev, 4 block dev,
5 FIFO, 6 socket, 7 symlink.

### B.9 Block map arithmetic

```
k = block_size / 4                    /* 256 for 1 KiB, 1024 for 4 KiB */

n <  12                        -> i_block[n]
n <  12 + k                    -> i_block[12] -> [n - 12]
n <  12 + k + k²               -> i_block[13] -> [(n-12-k)/k] -> [(n-12-k)%k]
n <  12 + k + k² + k³          -> i_block[14] -> three levels
```

Maximum file size by block size, from the block map alone:

| Block size | k | Max blocks | Max size |
|---:|---:|---:|---:|
| 1 KiB | 256 | 16 843 020 | ~16.06 GiB |
| 2 KiB | 512 | 134 480 396 | ~256.5 GiB |
| 4 KiB | 1024 | 1 074 791 436 | ~4 TiB |

A pointer value of 0 at any level is a hole, not an error.

### B.10 Inode location

```
group  = (ino - 1) / s_inodes_per_group
index  = (ino - 1) % s_inodes_per_group
block  = bgd[group].bg_inode_table + (index * s_inode_size) / block_size
offset = (index * s_inode_size) % block_size
```

### B.11 Limits

| Property | Value |
|---|---|
| Max filename | 255 bytes |
| Max file size | see B.9 |
| Hard links | yes, `i_links_count` |
| Symlinks | yes, fast (inline, ≤ 59 bytes) and slow |
| Sparse files | yes |
| Permissions | full POSIX |
| Timestamps | UTC, 1-second resolution, no birth time |
| Journal | none — this is the whole point of ext3 |

---

## Appendix C — Command cookbook

### C.1 Image creation

```bash
truncate -s 512M fat32.img
mkfs.vfat -F 32 -S 512 -s 8 -n NOVA64 fat32.img

truncate -s 128M ext2.img
mke2fs -q -F -t ext2 -b 1024 -I 128 -O none,filetype,sparse_super -L NOVA64 ext2.img
```

### C.2 Mounting

```bash
# kernel driver, for reference
LOOP=$(sudo losetup --find --show -P image.img)
sudo mount "$LOOP" /mnt/ref
sudo umount /mnt/ref && sudo losetup -d "$LOOP"

# your driver
./fat32fuse --image=image.img -o ro,uid=$(id -u),gid=$(id -g) -f -s /mnt/fuse
fusermount3 -u /mnt/fuse
fusermount3 -uz /mnt/fuse                       # lazy, for a wedged mount
echo 1 | sudo tee /sys/fs/fuse/connections/*/abort   # nuclear option
```

### C.3 Inspection

```bash
xxd -s 0     -l 512  image.img            # boot sector
xxd -s 1024  -l 1024 ext2.img             # ext2 superblock
hexdump -C -s $((512*32)) -n 512 fat32.img

fsck.fat -n -v fat32.img
dumpe2fs ext2.img
dumpe2fs -h ext2.img
e2fsck -fn ext2.img
```

### C.4 `debugfs` — the ext2 oracle

```bash
debugfs -R "stat <2>"            ext2.img   # root inode, all fields
debugfs -R "stat /path/to/file"  ext2.img
debugfs -R "imap </12>"          ext2.img   # which block/offset holds inode 12
debugfs -R "blocks </12>"        ext2.img   # every block of the file, in order
debugfs -R "dump /file /tmp/out" ext2.img   # extract for byte comparison
debugfs -R "ls -l /"             ext2.img
debugfs -R "ncheck 12"           ext2.img   # inode -> path
debugfs -R "icheck 1234"         ext2.img   # block -> inode
debugfs -R "htree /somedir"      ext2.img   # dump an htree, if present
debugfs -R "features"            ext2.img
```

`blocks` is the single most useful command in the entire toolkit for step 42.

### C.5 Testing

```bash
./tests/scripts/difftest.sh /mnt/ref /mnt/fuse
fsx -N 1000000 -S 0 /mnt/fuse/fsxfile
fio --name=rand --rw=randread --size=64M --directory=/mnt/fuse

# sanitizers
make asan && ./ext2fuse --image=ext2.img -f -s /mnt/fuse
valgrind --leak-check=full --track-origins=yes ./ext2fuse --image=ext2.img -f -s /mnt/fuse

# fuzzing
afl-fuzz -i tests/corpus -o /tmp/afl-out -- ./fuzz_ext2 @@
```

---

## Appendix D — Test matrix

| Test | F3 | F4 | F5 | F6 | F7 | F8 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|
| `difftest.sh` tree / types / sizes | ✔ | ✔ | — | ✔ | ✔ | — |
| `difftest.sh` modes / links / stats | ✔ | ✔ | — | ✔ | ✔ | — |
| `difftest.sh` hashes | ✔ | ✔ | — | ✔ | ✔ | — |
| Partial reads at boundaries | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| Block map vs `debugfs blocks` | — | ✔ | — | — | ✔ | — |
| Corruption corpus | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| AFL++ ≥ 1 h | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| ASan / UBSan | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| Valgrind | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| `fsx` ≥ 10⁶ ops | — | — | — | ✔ | ✔ | ✔ |
| `pjdfstest` subset | — | — | — | ✔ | ✔ | ✔ |
| `fsck` / `e2fsck` / `nvfsck` clean | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| Crash injection | — | — | — | ✔ | ✔ | ✔ |
| NVFS conformance (2 350) | — | — | read | — | — | ✔ |
| NVFS agreement (488, 3-way) | — | — | ✔ | — | — | ✔ |
| Inode lifetime stress | — | — | ✔ | — | — | ✔ |

---

## Appendix E — OSTEP chapter mapping

The book is the primary OS reference for this project; these are the chapters
that pay off directly, and when to read them.

| Read before | Chapter | What it explains about this work |
|---|---|---|
| F0 | I/O Devices | why a block layer exists at all; the device abstraction |
| F0 | Hard Disk Drives | LBA, why sequential access matters, geometry's ghost in the BPB |
| F1 | Files and Directories | the exact `stat`/`readdir`/`open` model FUSE exposes |
| F3 | Files and Directories (again) | re-read after implementing; it reads differently |
| F4 | File System Implementation | vsfs *is* simplified ext2: superblock, bitmaps, inodes, indirect blocks |
| F7 | Locality and the Fast File System | block groups, the allocator, why locality is the whole design |
| F6–F8 | Crash Consistency: FSCK and Journaling | the ordering invariant, why `fsck` is slow, why ext3 exists |
| F8 | Log-structured File System | the road not taken; useful contrast for NVFS's own choices |
| optional | Flash-based SSDs | relevant to the eventual SD-card-backed noVa64 storage |

The crash-consistency chapter is the one to read *twice*: once before F6, to
know what you are trying to avoid, and once after F7's crash injection, when
the failure modes it describes have become things you have personally
produced. That second reading is where the chapter stops being abstract.

---

## Appendix F — Risk register

| ID | Risk | Impact | Mitigation |
|---|---|---|---|
| R1 | ext4-formatted test image used by accident | Silent garbage from extent-tree misread | Feature gate (step 39) refuses; `mkimages.sh` pins features |
| R2 | Directory parsing infinite loop on `rec_len == 0` | Hang, DoS | Validation in step 43; corpus case; timeouts in all corpus runs |
| R3 | Cluster chain cycle | Hang or unbounded memory | Step bound + Floyd detection (step 25) |
| R4 | `i_blocks` misread as filesystem blocks | Wrong `st_blocks`, wrong allocation accounting in F7 | Called out in steps 41 and 80; `difftest` `stats` catches it |
| R5 | Write into an htree directory | Silent directory corruption invisible to linear readers | Refuse when `INDEX_FL` set (step 81); closed decision |
| R6 | FUSE `read` returns 0 on available data | Every file appears empty | Deliberate mistake in step 9; caught by `hashes` check |
| R7 | Positive errno returned | Errors read as success | Deliberate mistake in step 9 |
| R8 | `forget` accounting error in low-level API | Leak or `ESTALE` | Stress test in step 61 |
| R9 | `libnvfs` model is path-keyed, not inode-keyed | F5 redesign mid-gate | Resolved in step 56 before any binding code |
| R10 | Write ordering bug invisible to normal testing | Data loss on crash | Crash injection (steps 76, 90, 95) |
| R11 | Logic duplicated between FUSE binding and `nv` CLI | Divergence, untested code | Boundary decision recorded in step 57 |
| R12 | Layering leaks, discovered only at F9 | Windows port far costlier than expected | L1/L2 must compile on Windows unchanged (step 101) |
| R13 | Test images sized near a FAT type boundary | Confusing `mkfs` behaviour | Step 4 sizes well clear of 4 085 / 65 525 |
| R14 | Calendar overrun; track competes with noVa64 gates | Project stall | Track is off the E/K/G critical path; F5 and F8 are the only steps that feed back |

---

## Appendix G — Decisions and open items

### G.1 Closed decisions

| ID | Decision |
|---|---|
| CD-1 | Target libfuse 3.x, `FUSE_USE_VERSION 31`, `_FILE_OFFSET_BITS=64` |
| CD-2 | Order FAT32 → EXT2 → NVFS; read-only gate then write gate for each |
| CD-3 | High-level FUSE API for FAT32 and EXT2; low-level for NVFS |
| CD-4 | Four-layer architecture L0–L3; L1 does no I/O, L2 knows no FUSE |
| CD-5 | Explicit byte accessors; no packed-struct casting anywhere |
| CD-6 | Develop and sanitize single-threaded (`-s`); threading is a step-98 experiment |
| CD-7 | EXT2 restricted to true ext2 images; unknown INCOMPAT bits refuse the mount |
| CD-8 | EXT2 read-only tolerates htree directories via linear scan; **write refuses them** |
| CD-9 | NVFS is a binding over `libnvfs`; all format logic stays in `libnvfs` |
| CD-10 | The FUSE front-end is a fourth tool alongside `nv`, not a replacement |
| CD-11 | The FUSE binding becomes the third participant in the 488-check agreement harness |
| CD-12 | FAT32 inode numbers derive from the short dirent's byte offset ÷ 32; root is 1 |
| CD-13 | Write ordering invariant: allocated-but-unreferenced acceptable, free-but-referenced not |
| CD-14 | Differential testing against the kernel driver is the primary read-path oracle |
| CD-15 | `fsck.fat` / `e2fsck -fn` / `nvfsck` are the write-path oracles for crash injection |
| CD-16 | F9 (Windows/macOS) is optional and attempted only after F8 |
| CD-17 | WinFsp preferred over Dokany, via its FUSE 3 compatibility header |
| CD-18 | `fsops.h` change log is a required deliverable; it becomes DN-FS-VFS-001 |

### G.2 Open items

| ID | Item | Resolve by |
|---|---|---|
| OI-A | Does `libnvfs` accept a block-device abstraction, or does it own its I/O? | Step 56 |
| OI-B | Is the `libnvfs` internal model inode-keyed or path-keyed? Determines high- vs low-level API | Step 56 |
| OI-C | Does NVFS have symlinks, and if so what storage form? Affects `readlink` in the vtable | Step 56 |
| OI-D | Charset policy: ASCII-only case folding for FAT LFN, or full Unicode? | Step 29 |
| OI-E | FAT timestamp timezone handling: match the kernel's `tz=` behaviour, or document a deviation? | Step 30 |
| OI-F | In-driver block/inode caching: how much, and measured against what? | After F4; do not pre-optimise |
| OI-G | Short-name generation beyond `~4`: match Windows' hashed form or document deviation? | Step 71 |
| OI-H | `writeback_cache`: leave off for correctness, or enable and handle the reordering? | Step 88 |
| OI-I | Which driver to port first in F9, and drive-letter vs directory mount? | Step 102 |
| OI-J | Windows case-collision policy for ext2 images containing names differing only by case | Step 104 |
| OI-K | Does the `fsops` vtable need an explicit transaction/barrier slot, given what F6–F7 reveal about ordering? | Step 99, feeds DN-FS-VFS-001 |
| OI-L | Should FAT12/FAT16 support be added as an extension, or left as a documented non-goal? | After F6 |

### G.3 Cross-document actions

| Action | Target document | Trigger |
|---|---|---|
| Update the ext2 layer design with what the host implementation revealed | DN-FS-EXT2-001 | Gate F4 exit (f), Gate F7 exit (f) |
| Draft the VFS layer specification from the `fsops` change log | DN-FS-VFS-001 | Gate F8 exit (f) |
| Record any NVFS specification ambiguities found by three-way agreement | NVFS Specification | Step 64 |
| Feed block-transfer requirements back to the Helium SD register map | Helium SD block spec (open item OI-5) | Gate F8 exit |

The last row is the one with a real deadline attached: the Helium SD register
map is on the critical path for schematic capture, and whether burst block
transfer to SDRAM belongs in it is much cheaper to decide before the map is
frozen than after. If this track has not reached a view on transfer sizes by
the time that decision is needed, decide it from the existing NVFS driver
instead and note the assumption — do not let a learning track block hardware.

---

## Appendix H — Effort and sequencing notes

Difficulty ratings by gate, as a rough shape rather than a schedule:

| Gate | Steps | Predominant difficulty | Where the time actually goes |
|---|---:|---|---|
| F0 | 6 | ★ | Nothing surprising. Deterministic image generation takes longest. |
| F1 | 7 | ★★ | The low-level API and lookup counts; everything else is quick. |
| F2 | 8 | ★★ | The differential harness is worth over-investing in. |
| F3 | 16 | ★★★ | LFN reassembly and the inode scheme; then `difftest` reconciliation. |
| F4 | 18 | ★★★ | The block map (step 42) alone is a large share of the gate. |
| F5 | 10 | ★★★ | Almost entirely determined by the step-56 audit result. |
| F6 | 12 | ★★★★ | Short-name generation and crash injection. |
| F7 | 14 | ★★★★★ | `rename`, link counts, and the crash-injection run. |
| F8 | 8 | ★★★ | Mostly harness integration; the ordering review is the real work. |
| F9 | 8 | ★★★ | Build environment, then the permission and case models. |

Two sequencing observations worth acting on:

**Steps 42 and 85 are the two that will dominate their gates.** Step 42 (ext2
block map) and step 85 (`rename`) are each worth more careful design up front
than the surrounding steps. If a session is going to be interrupted, do not
start either at the end of one.

**The crash-injection runs (76, 90, 95) are wall-clock, not effort.** They run
unattended for hours. Start them at the end of a session and classify the
results at the start of the next; do not sit and watch them.

---

## Appendix I — Reference sources

**FUSE**
- libfuse repository and Doxygen documentation; the `example/` directory —
  `hello`, `hello_ll`, `passthrough`, `passthrough_hp`, `null`.
- Joseph Pfeiffer's FUSE tutorial (BBFS) — written against FUSE 2.9, so adapt
  the callback signatures, but the structure is sound.
- Vangoor et al., "To FUSE or Not to FUSE: Performance of User-Space File
  Systems" — for what the kernel/userspace boundary actually costs.

**FAT32**
- Microsoft, "FAT: General Overview of On-Disk Format" (the EFI FAT32 File
  System Specification) — the authoritative source for the `CountOfClusters`
  algorithm and the field tables.
- Elm-Chan's FAT documentation — a clearer presentation of the same material.
- OSDev wiki, FAT page.
- Linux kernel `fs/fat/` — for what a real implementation tolerates.
- `mtools` — a userspace FAT implementation worth reading.

**EXT2**
- Dave Poirier, "The Second Extended File System: Internal Layout" — the
  reference this appendix's tables were checked against.
- Linux kernel `Documentation/filesystems/ext2.rst` and `fs/ext2/`.
- OSDev wiki, Ext2 page.
- `e2fsprogs` source, particularly `debugfs` — the ground truth for every
  question the documentation leaves ambiguous.
- The `fuse-ext2` project — a worked example of an ext2 FUSE driver; read its
  write path critically rather than as a model.

**Windows and macOS**
- WinFsp documentation, particularly its FAQ on the FUSE compatibility layer
  and the `uid=-1` / `-F NTFS` behaviours.
- Dokany, for comparison; its FUSE wrapper covers FUSE 2 only.
- macFUSE, including the FSKit backend notes; FUSE-T as the
  kernel-extension-free alternative.

**General**
- Arpaci-Dusseau, *Operating Systems: Three Easy Pieces* — see Appendix E.
- Tanenbaum, *Modern Operating Systems* — supplementary.

---

*End of DN-FS-FUSE-001 rev 1.0.*
