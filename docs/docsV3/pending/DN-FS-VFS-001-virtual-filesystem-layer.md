# DN-FS-VFS-001 — Virtual File System Layer

**Project:** noVa64 (DANI-65816)
**Document:** Design Note, Draft 1
**Status:** For review — not frozen
**Scope:** The interface between the syscall layer and the file system drivers.
Defines the vnode object model, the operation tables, path resolution, the mount
table, and the concurrency contract.

**Depends on:** DN-FS-NVFS-003 (native file system), DN-FS-EXT2-001 (foreign file
system), DN-FS-FUSE-001 (host-side learning track).
**Blocks:** ext2 implementation (gate F1 onward), and any second consumer of NVFS.

---

## 1. Purpose

noVa64 is about to have two file systems: NVFS, the native system volume, and
ext2, a foreign format used to exchange files with a host. NVFS currently has a
bespoke call path invoked directly. If ext2 is added the same way, the result is
two parallel access paths and a syscall layer that branches on file system type at
every call site.

This note specifies the layer that prevents that. It is written **before** the ext2
implementation deliberately: the cost of introducing it now is one small module and
a mechanical rework of the NVFS driver's call surface; the cost of introducing it
after ext2 exists is the same rework applied to two drivers plus a syscall layer
that has already grown branches.

A secondary and less obvious purpose: the VFS is where the decisions that *cannot*
live in a file system driver go — mount point traversal, symlink loop bounds,
vnode identity, and the concurrency discipline required by the Amiga milestone.
Those have no correct home inside either driver.

## 2. Scope and non-goals

### In scope

- The vnode object model and its lifecycle.
- Two operation tables: per-mount (`vfs_ops`) and per-vnode (`vnode_ops`).
- Path resolution, owned by the VFS, including symlinks and mount crossing.
- The mount table.
- Open-file objects and the per-process descriptor table.
- A single unified error space.
- The concurrency contract, specified now and implemented later.

### Out of scope

- Permissions and ownership enforcement. noVa64 has no user model; see §11.1.
- A unified on-disk format. NVFS and ext2 stay exactly as specified.
- Network file systems, layered/union mounts, file locking, `mmap`.
- A page cache. The buffer cache question is addressed in §9, but a
  page-level cache tied to the MMU is a separate and much later problem.

### Explicit non-goal

This is **not** a Unix VFS. It models exactly what NVFS and ext2 both have, and
nothing else. Every operation in §6 is one that both drivers can implement without
emulation. The temptation to add operations "because Unix has them" should be
resisted; the interface is cheap to extend and expensive to shrink.

---

## 3. Position in the system

```
   syscall dispatch  (COP, number in A — per existing ABI)
        |
   file descriptor table          per process
        |
   open file objects              offset, flags, vnode reference
        |
   ============ VFS ============   this document
        |  path resolution, mount table, vnode cache,
        |  concurrency contract, error space
        |
   +----+----------------+
   |                     |
  NVFS driver         ext2 driver         (future: devfs, romfs)
   |                     |
   +----+----------------+
        |
   block cache                            §9
        |
   block device  (Helium SD)
```

---

## 4. What is actually being unified

The interface must be narrow enough that neither driver has to emulate the other's
structure. The divergences are substantial and worth stating plainly, because they
determine what cannot appear in the interface.

| Property | NVFS (DN-FS-NVFS-003) | ext2 (DN-FS-EXT2-001) | Consequence for the VFS |
|---|---|---|---|
| Block size | 2048 B, fixed (= MMU page size) | 1024 B for the write path | Block size is a **mount property**, never a VFS constant. See §9. |
| File extent representation | Extents | 12 direct + triple indirect | Never exposed. `read`/`write` take byte offsets only. |
| Directory entries | Fixed 64-byte slots | Variable `rec_len`, 4-byte aligned | `readdir` returns a **copied-out** entry, never a pointer into a raw block. |
| Directory position | Slot index | Byte offset within the directory | The `readdir` cookie is an **opaque `uint32_t`**, interpreted only by the driver. |
| Metadata consistency | Physical redo-only journal | Ordered writes, no journal | Writeback ordering must remain under driver control. See §9.3. |
| Mode / type encoding | POSIX-identical | POSIX (native) | **No translation needed.** See below. |
| Free-space accounting | Per NVFS spec | Bitmaps + three coupled counters | Hidden entirely behind `statfs`. |
| Recovery | On-target journal replay | Host `e2fsck` only | `mount` is allowed to fail or downgrade to read-only; the VFS must handle both. |

The mode/type encoding row is the one that pays off immediately. Draft 3 of NVFS
adopted POSIX-identical `mode` and type encodings, which means `vattr.mode` passes
through both drivers unchanged and the VFS needs no type translation table at all.
Had the two formats disagreed, every `getattr`, every `readdir` entry and every
`create` would need a conversion in both directions. This is worth noting as a
decision that turned out better than it needed to.

---

## 5. Object model

Four objects, with clearly separated ownership.

```
  mount_t   ── one per mounted file system.        Owned by VFS.
     |         Holds vfs_ops + driver private state.
     |
  vnode_t   ── one per *in-use file*, cached.      Owned by VFS.
     |         Identity = (mount, file_id). Refcounted.
     |         Holds vnode_ops + driver private state.
     |
  file_t    ── one per open() call.                Owned by VFS.
     |         Holds offset + flags + a vnode reference.
     |
  fd table  ── per process, index -> file_t*.      Owned by the process.
```

### 5.1 Why `file_t` is separate from `vnode_t`

Two `open()` calls on the same path must have independent read offsets; a `dup()`
of one descriptor must **share** the offset with its original. That requires
exactly this three-level structure, and retrofitting it later means auditing every
call site that assumed the offset lived in the vnode. It costs about 10 bytes per
open file to get right now.

### 5.2 Why `vnode_t` is cached and refcounted

If two open files on the same underlying file hold two separate copies of the
inode, an append through one silently discards the size and block pointers written
by the other. Both DN-FS-EXT2-001 (§7.5) and NVFS require an in-memory inode
identity map for this reason. **That map belongs in the VFS, not duplicated in each
driver** — it is the same logic, it needs the same LRU pressure, and mount-point
traversal needs to inspect it.

### 5.3 Structures

```c
#include <stdint.h>

typedef enum {
    VNON = 0, VREG, VDIR, VLNK, VCHR, VBLK, VFIFO, VSOCK
} vtype_t;

/* vnode flags */
#define VF_BUSY      0x01   /* locked; see §12                          */
#define VF_ROOT      0x02   /* root of its mount                        */
#define VF_MOUNTED   0x04   /* another mount is grafted onto this vnode */
#define VF_DIRTY     0x08   /* driver metadata pending                  */

typedef struct vnode {
    struct mount           *mnt;
    uint32_t                file_id;      /* stable per-mount identity  */
    uint16_t                refcnt;
    uint8_t                 type;         /* vtype_t                    */
    uint8_t                 flags;
    struct mount           *mounted_here; /* valid iff VF_MOUNTED       */
    const struct vnode_ops *ops;
    void                   *fs_private;   /* inode_t* / nvfs_inode_t*   */
    struct vnode           *hash_next;
    struct vnode           *lru_prev, *lru_next;
} vnode_t;

typedef struct mount {
    char                    mountpoint[32];
    vnode_t                *covered;      /* vnode we are grafted onto;
                                             NULL for the root mount    */
    const struct vfs_ops   *ops;
    void                   *fs_private;   /* ext2_fs_t* / nvfs_fs_t*    */
    uint16_t                block_size;
    uint8_t                 read_only;
    uint8_t                 flags;
} mount_t;

/* open file flags */
#define O_RDONLY  0x0001
#define O_WRONLY  0x0002
#define O_RDWR    0x0003
#define O_CREAT   0x0004
#define O_TRUNC   0x0008
#define O_APPEND  0x0010
#define O_EXCL    0x0020
#define O_DIRECTORY 0x0040

typedef struct file {
    vnode_t  *vn;
    uint32_t  offset;
    uint16_t  refcnt;      /* dup() increments this, not vnode->refcnt */
    uint16_t  flags;
} file_t;

typedef struct vattr {
    uint16_t  mode;        /* POSIX, passes through unmodified          */
    uint16_t  nlink;
    uint32_t  size;
    uint32_t  blocks;      /* 512-byte units, as ext2 counts them       */
    uint32_t  atime, mtime, ctime;
    uint32_t  file_id;
    uint16_t  uid, gid;    /* read and preserved, never enforced        */
} vattr_t;

typedef struct dirent_out {
    uint32_t  file_id;
    uint8_t   type;        /* vtype_t                                   */
    uint8_t   name_len;
    char      name[256];   /* NUL-terminated on return                  */
} dirent_out_t;

typedef struct statfs_out {
    uint16_t  block_size;
    uint32_t  blocks_total, blocks_free;
    uint32_t  files_total,  files_free;
    uint8_t   read_only;
    char      fstype[8];   /* "nvfs", "ext2"                            */
} statfs_out_t;
```

`dirent_out_t` at 262 bytes is not small on a 65816. It is a **caller-provided**
buffer, never allocated by the VFS, and the syscall layer keeps exactly one per
open directory descriptor rather than one per process. See §13.

---

## 6. Operation tables

Split into per-mount and per-vnode, which is the standard division and the correct
one here: `mount`/`unmount`/`sync`/`statfs` operate on a file system, everything
else on a file.

```c
typedef struct vfs_ops {
    int  (*mount)  (mount_t *mp, blkdev_t *dev, uint32_t part_lba,
                    uint32_t part_sectors, uint8_t want_write);
    int  (*unmount)(mount_t *mp);
    int  (*root)   (mount_t *mp, vnode_t **out);   /* get root vnode  */
    int  (*sync)   (mount_t *mp);
    int  (*statfs) (mount_t *mp, statfs_out_t *out);
    /* Called by the VFS when it needs a vnode it does not have cached. */
    int  (*vget)   (mount_t *mp, uint32_t file_id, vnode_t *vn);
    /* Called on the last release. Driver frees fs_private and, if the
       link count is zero, performs the deferred delete.               */
    void (*vput)   (vnode_t *vn);
} vfs_ops_t;

typedef struct vnode_ops {
    /* --- read path --- */
    int     (*lookup)  (vnode_t *dvn, const char *name, uint8_t nlen,
                        uint32_t *file_id_out);
    int32_t (*read)    (vnode_t *vn, uint32_t off, uint32_t len, void *buf);
    int     (*readdir) (vnode_t *dvn, uint32_t *cookie, dirent_out_t *out);
    int     (*getattr) (vnode_t *vn, vattr_t *out);
    int     (*readlink)(vnode_t *vn, char *buf, uint16_t bufsize);

    /* --- write path; NULL on a read-only driver --- */
    int32_t (*write)   (vnode_t *vn, uint32_t off, uint32_t len, const void *buf);
    int     (*truncate)(vnode_t *vn, uint32_t newsize);
    int     (*setattr) (vnode_t *vn, const vattr_t *in, uint16_t mask);
    int     (*create)  (vnode_t *dvn, const char *name, uint8_t nlen,
                        uint16_t mode, uint32_t *file_id_out);
    int     (*mkdir)   (vnode_t *dvn, const char *name, uint8_t nlen, uint16_t mode);
    int     (*rmdir)   (vnode_t *dvn, const char *name, uint8_t nlen);
    int     (*unlink)  (vnode_t *dvn, const char *name, uint8_t nlen);
    int     (*link)    (vnode_t *dvn, const char *name, uint8_t nlen, vnode_t *vn);
    int     (*symlink) (vnode_t *dvn, const char *name, uint8_t nlen,
                        const char *target);
    int     (*rename)  (vnode_t *odvn, const char *on, uint8_t onl,
                        vnode_t *ndvn, const char *nn, uint8_t nnl);
    int     (*fsync)   (vnode_t *vn);
} vnode_ops_t;
```

### 6.1 Design rules baked into the shape

**`lookup` returns a `file_id`, not a vnode.** The driver has no business creating
vnodes — it does not know whether one is already cached, and it cannot know whether
a mount is grafted onto the result. The VFS takes the `file_id`, consults its cache,
and calls `vget` only on a miss. This is the single most important line in the
interface; getting it backwards leaks the vnode cache into both drivers.

**Directory-mutating operations take the parent vnode plus a name, not a path.**
Path resolution happens once, in the VFS. A driver never sees a `/`.

**`readdir` returns one entry per call, through an opaque cookie.** NVFS advances a
slot index, ext2 advances a byte offset; the VFS stores whatever it is given and
passes it back. No shared iteration state, no callback into the VFS from inside a
driver's block walk.

**Write operations may be `NULL`.** A read-only ext2 mount populates only the read
half of the table, which makes the read-only subset (DN-FS-EXT2-001 gates F0–F1) a
genuinely smaller deliverable rather than a stubbed-out full one. The VFS checks
for `NULL` and returns `E_ROFS`.

**No `open`/`close` in `vnode_ops`.** Opening is a VFS-level act: resolve, check the
type, allocate a `file_t`. Neither driver has per-open state. If a device file
system is added later it will need them; adding two slots to the table at that
point is trivial.

---

## 7. Path resolution

Owned entirely by the VFS. Drivers provide `lookup` on a single component and
nothing more.

```c
int vfs_namei      (const char *path, vnode_t *cwd, vnode_t **out);
int vfs_nameiparent(const char *path, vnode_t *cwd,
                    vnode_t **dvn_out, char *name_out, uint8_t *nlen_out);
```

Algorithm:

1. Absolute path → start at the root mount's root vnode. Otherwise start at `cwd`.
2. Split on `/`. Skip empty components and `.`.
3. For `..`: if the current vnode is the root of a mount (`VF_ROOT`) and that mount
   has a `covered` vnode, **step out to `covered` first**, then look up `..` there.
   At the root of the root mount, `..` is the root.
4. Otherwise call `ops->lookup`, obtain a `file_id`, and get the vnode via the cache.
5. If the resulting vnode has `VF_MOUNTED`, **descend into `mounted_here`'s root**.
6. If the result is a symlink and it is not the final component (or the caller asked
   to follow), read the target and restart resolution from it. Bounded at
   **8 traversals per call**; beyond that, `E_LOOP`.

Steps 3 and 5 are the reason path resolution cannot live in a driver: neither NVFS
nor ext2 can know that a mount is grafted onto one of its directories, and neither
can see across the boundary.

Symlink handling is centralised for the same reason — a symlink stored on ext2 may
resolve to a path on NVFS. `readlink` is per-driver (NVFS and ext2 store short
targets differently: ext2 packs targets under 60 bytes into `i_block[]`), but
following is not.

`vfs_nameiparent` exists because every mutating syscall needs the containing
directory plus the final component, and resolving the full path first would fail on
`create`.

---

## 8. Mount table

```c
int vfs_mount  (const char *mountpoint, const char *fstype,
                blkdev_t *dev, uint32_t part_lba, uint32_t part_sectors,
                uint8_t want_write);
int vfs_unmount(const char *mountpoint);
int vfs_sync_all(void);
```

- A fixed table. **Four entries** is the proposed initial size: root (NVFS), one
  removable ext2, and two spare.
- The root mount is established at boot and cannot be unmounted.
- `vfs_mount` resolves the mountpoint to a vnode, requires `VDIR`, sets
  `VF_MOUNTED`, and pins that vnode for the lifetime of the mount.
- `vfs_unmount` fails with `E_BUSY` if any vnode belonging to that mount has
  `refcnt > 0` other than its root. This check is what prevents a card being pulled
  while a file on it is open, and it is cheap: one scan of the vnode cache.
- `want_write` may be **downgraded** by the driver — ext2 does this whenever the
  feature policy (DN-FS-EXT2-001 §5) demands it. The VFS records the result in
  `mount_t.read_only` and reports it through `statfs`. The caller must be told; a
  silent downgrade to read-only produces confusing failures much later.

Proposed layout: NVFS at `/`, removable media at `/sd`. Nothing in this design
depends on that choice.

---

## 9. The block cache question

This is the second decision in this note with consequences beyond it.

### 9.1 The problem

DN-FS-EXT2-001 specifies a buffer cache as ext2 layer L2. NVFS has its own. Left
alone, noVa64 gets two independent caches, two SDRAM pools sized by guesswork, no
global LRU, and two implementations of the same 300 lines of code. When one mount
is idle its buffers sit unused while the other thrashes.

### 9.2 The complications

The two file systems do not agree on block size — 2048 for NVFS (chosen to equal
the MMU page size) versus 1024 for ext2 — and NVFS's redo-only journal requires
that a dirty metadata block **must not** reach the card before its journal record
has been committed. A naive shared cache with a global LRU will happily evict and
write back a metadata block at an arbitrary moment, which silently breaks the
journal's central invariant.

### 9.3 Proposal

A **shared block cache below the VFS**, with:

- **Uniform 2048-byte buffers**, with block size as a per-mount property. An ext2
  mount uses 1024 of each 2048-byte buffer. This wastes 50% of the cache while an
  ext2 mount is active, which is acceptable because ext2 is a transfer medium and
  NVFS is the resident root. The alternative — a two-size slab allocator — costs
  more code than the memory it saves at this scale.
- **A per-buffer `commit_seq`** and a per-mount `flushed_seq`. The cache refuses to
  write back a buffer whose `commit_seq` exceeds its mount's `flushed_seq`, and
  instead calls `ops->sync` to advance it first. ext2 leaves `commit_seq` at zero
  and is unaffected; NVFS gets exactly the ordering guarantee its journal needs,
  expressed in four bytes per buffer and one comparison in the eviction path.
- **Explicit pinning** (`bpin`/`bunpin`), used by both drivers for superblocks and
  descriptor tables.

The `commit_seq` mechanism is the part worth arguing about. It is proposed here
rather than the alternative of giving each driver its own cache, because the
alternative's cost is permanent and this one's cost is a single field. But it does
mean the shared cache has a hook whose semantics only one driver uses, which is a
smell. See OI-2.

### 9.4 What is not proposed

No page cache, no read-ahead beyond the multi-block transfers already specified in
DN-FS-EXT2-001 §7.0, no unification of the cache with the MMU's SRAM cache. The
SRAM is a hardware-managed cache over SDRAM and must not be statically carved up
for file system buffers; all of this lives in SDRAM.

---

## 10. Unified error space

NVFS and ext2 currently define separate error enumerations. A single space is
required, since these values reach userland through the COP syscall ABI.

```c
#define E_OK             0
#define E_IO            -1    /* device failure                          */
#define E_NOENT         -2
#define E_EXIST         -3
#define E_NOTDIR        -4
#define E_ISDIR         -5
#define E_NOTEMPTY      -6
#define E_INVAL         -7
#define E_NOSPC         -8
#define E_NOFILE        -9    /* inodes / file records exhausted         */
#define E_ROFS         -10
#define E_BUSY         -11
#define E_LOOP         -12
#define E_NAMETOOLONG  -13
#define E_MFILE        -14    /* fd table full                           */
#define E_BADF         -15
#define E_XDEV         -16    /* cross-mount link or rename              */
#define E_CORRUPT      -17    /* structural inconsistency detected       */
#define E_FEATURE      -18    /* unsupported on-disk feature             */
#define E_NOTSUP       -19    /* operation NULL in this driver's table   */
#define E_FBIG         -20
#define E_NOTMOUNTED   -21
```

Each driver maps its internal codes at its `vnode_ops` boundary; the internal
enumerations in DN-FS-EXT2-001 §9 and the NVFS equivalent remain, since they carry
detail useful in the driver that has no meaning to a caller.

**`E_XDEV`** is a VFS-level check. `link` and `rename` compare
`odvn->mnt == ndvn->mnt` before dispatching; neither driver can detect the
violation itself, and a driver handed a vnode belonging to a different mount would
dereference the wrong `fs_private`.

**`E_CORRUPT` is sticky.** When a driver reports it, the VFS marks the mount
read-only and latches the condition. Continuing to write to a file system that has
already failed a consistency check is how a recoverable problem becomes an
unrecoverable one. This is stated in DN-FS-EXT2-001 §9 for ext2; it belongs here so
it applies to both.

---

## 11. Semantics the VFS fixes

### 11.1 Permissions

`uid`, `gid` and `mode` are read from disk, preserved across modification, reported
through `getattr`, and **never enforced**. noVa64 has no user model, so there is
nothing to enforce them against. Files created by noVa64 use `uid = gid = 0`, mode
`0644` for regular files and `0755` for directories.

This is a decision to revisit only if noVa64 ever grows multiple users, which is
not on any roadmap. Enforcing permissions with a single implicit root user produces
nothing but obstacles.

### 11.2 Timestamps

The VFS supplies the timestamp to drivers; drivers never call the clock. With no
RTC in the current design, `vfs_now()` returns 0 (per DN-FS-EXT2-001 §13.4). When an
RTC is added, one function changes.

`atime` is never updated on read, in either driver. Doing so would turn every read
into a write.

### 11.3 Deferred delete

`unlink` on a file with open descriptors decrements the link count and returns. The
actual release of blocks happens in `vput` when the last vnode reference goes away
and the link count is zero. Both drivers implement this in their `vput`; the VFS
guarantees the ordering by holding the vnode reference for as long as any `file_t`
points at it.

This is the behaviour that makes "delete a file that is currently open" safe rather
than catastrophic, and it costs nothing to have from the start.

---

## 12. Concurrency — designing now for the Amiga milestone

### 12.1 The problem, stated early

The Apple II milestone is single-threaded: one program, one call into the file
system at a time, no locking needed. The Amiga milestone is **preemptive
multitasking**, at which point multiple processes call into the VFS concurrently and
every shared structure here — the vnode cache, the mount table, the block cache,
each driver's allocator state — becomes a race.

There are two options and they are not symmetric in cost:

- **(a)** Ignore concurrency now, retrofit locking at the Amiga milestone.
- **(b)** Specify the locking discipline now, implement the primitives as no-ops,
  and turn them on later.

Retrofitting locking into a vnode cache is notoriously painful, and the failure
mode is intermittent corruption rather than a clean crash. The cost of (b) is a
handful of macros that compile to nothing and a lock-ordering rule that has to be
written down anyway. **Recommendation: (b).**

### 12.2 The discipline

```c
void vlock  (vnode_t *vn);    /* sleep while VF_BUSY, then set it     */
void vunlock(vnode_t *vn);    /* clear VF_BUSY, wake sleepers         */
```

Under the Apple II milestone these are `#define vlock(v)   ((void)0)`.

Rules, which hold whether or not the macros do anything yet:

1. A vnode must be locked across any operation that reads or writes its metadata.
2. **Lock ordering: parent before child, and when two vnodes are siblings in the
   ordering, lower `file_id` first.** This is the rule that makes cross-directory
   `rename` — which holds two directory locks — deadlock-free.
3. A vnode lock is never held across a block device wait that is not part of the
   operation itself.
4. `refcnt` and `VF_BUSY` are independent. Holding a reference does not imply
   holding the lock.

Writing rule 2 down now is most of the value. It is the rule that is expensive to
discover later, because the deadlock it prevents appears only under concurrent
renames in a running GUI.

### 12.3 Blocking I/O

Once processes are preemptive, an SD read of several milliseconds must not spin.
The calling process sleeps and the scheduler runs someone else, which means VFS
operations execute in process context on a **per-process kernel stack**.

On the 65816 that stack lives in bank 0 and is addressed by a 16-bit stack pointer,
so every process's kernel stack competes for the same 64 KB — the scarcest address
space in the machine. Consequences for this layer:

- **Path resolution must be iterative**, not recursive per component.
- Symlink following is bounded at 8 (§7), which bounds its stack use.
- The ext2 indirect-block walker recurses at most 3 deep, which is acceptable;
  the *truncate* walker must be written to the same bound and not deeper.
- Proposed budget: **512 bytes of kernel stack per process** for any VFS operation.
  `dirent_out_t` at 262 bytes must therefore never be a stack local inside the VFS —
  it is always caller-provided (§5.3).

That last point is easy to violate accidentally and produces stack overflow into the
neighbouring process's kernel stack, which is close to undebuggable. It is worth a
comment in the header.

---

## 13. Syscall mapping

Existing ABI: `COP` with an inline signature byte, syscall number in the
accumulator, arguments per the Calypsi calling convention. The VFS changes nothing
about that; it changes what sits behind the dispatch table.

| Syscall | VFS path |
|---|---|
| `open(path, flags, mode)` | `vfs_namei` (or `nameiparent` + `create` for `O_CREAT`), allocate `file_t`, install in fd table |
| `close(fd)` | `file_t` refcount--, then `vput` |
| `read/write(fd, buf, len)` | `ops->read` / `ops->write` at `file_t.offset`, advance |
| `pread/pwrite` | same, offset not advanced |
| `lseek(fd, off, whence)` | VFS only; `SEEK_END` needs `getattr` |
| `stat/fstat` | `ops->getattr` |
| `truncate/ftruncate` | `ops->truncate` |
| `mkdir/rmdir/unlink/rename/link/symlink` | `vfs_nameiparent` then the matching op |
| `readlink` | `ops->readlink` |
| `opendir/readdir/closedir` | `file_t` with `O_DIRECTORY`; cookie stored in `file_t.offset` |
| `chdir/getcwd` | VFS only; see below |
| `dup/dup2` | fd table only, `file_t` refcount++ |
| `sync/fsync` | `vfs_sync_all` / `ops->fsync` |
| `mount/umount` | `vfs_mount` / `vfs_unmount` |

**`getcwd` deserves a note.** Reconstructing a path from a vnode requires walking
`..` upward and, at each level, scanning the parent directory for an entry whose
`file_id` matches the child — an O(depth × entries) operation with no cache. The
alternative is to store the current directory **as a string** in the process control
block alongside the vnode reference, updated on every `chdir`. That costs 64–128
bytes per process and makes `getcwd` a memcpy.

Recommendation: store the string. The reverse-lookup machinery is real code with a
real failure mode (hard links to directories aside, it simply cannot resolve an
unlinked cwd) in exchange for saving 128 bytes per process, and noVa64 will not have
many processes.

---

## 14. Relationship to the FUSE track — a discrepancy to resolve

DN-FS-FUSE-001 builds FAT32, EXT2 and NVFS implementations under libfuse as a
learning vehicle, and defines an internal `fsops.h` vtable intended to feed this
specification. That intent is sound, but there is a shape mismatch that will cost
a rewrite if it is not caught now.

**libfuse offers two APIs.** The *high-level* API is **path-based**: every callback
receives a full path string and the library does no identity caching. The *low-level*
API is **inode-based**: callbacks receive an inode number and a parent inode plus a
name, which is exactly the shape of §6.

If `fsops.h` has been modelled on the high-level API, it will have path-based
signatures throughout, and none of it transfers — because path resolution, symlink
following and mount crossing are all VFS responsibilities here, and a driver in
noVa64 never sees a path.

**Recommendation:** shape `fsops.h` after the **low-level** FUSE API, with
`lookup(parent_ino, name)` and operations keyed on inode number. Then a noVa64
driver is a thin adapter over the same functions, and the FUSE track validates the
actual production call surface rather than a parallel one. This is worth checking
against the current state of DN-FS-FUSE-001 before the FAT32 implementation gets far,
since it is much cheaper to change at 1 implementation than at 3.

Two further FUSE shapes that must **not** leak into this layer:

- FUSE supplies `uid`/`gid` and expects permission checks. noVa64 has neither (§11.1).
- FUSE is multithreaded unless run with `-s`. The concurrency contract here is §12's,
  not libfuse's. Run the learning implementations single-threaded so that the threading
  model does not quietly become a dependency.

---

## 15. Budgets

### 15.1 Code

| Component | Estimate |
|---|---|
| vnode cache + lifecycle | 1.5 KB |
| Path resolution (incl. symlinks, mount crossing) | 1.5 KB |
| Mount table | 0.5 KB |
| `file_t` / fd table / syscall glue | 1.5 KB |
| Error mapping, `statfs`, misc | 0.5 KB |
| **Total** | **~5.5 KB** |

Against roughly 19 KB for ext2 read-write, or 9 KB read-only, plus NVFS. If the
whole file system stack must be resident, address space in bank terms is the real
constraint and should be checked before F1 rather than after.

### 15.2 RAM

| Item | Size |
|---|---|
| Vnode cache, 24 × 28 B | 672 B |
| Mount table, 4 × 48 B | 192 B |
| Open file objects, 24 × 10 B | 240 B |
| fd table, per process, 16 × 3 B | 48 B/proc |
| cwd path string, per process | 128 B/proc |
| Path resolution scratch | 320 B |
| **VFS total (excluding per-process)** | **~1.4 KB** |

Small compared with the block cache (~20 KB per DN-FS-EXT2-001 §14.5). The vnode
cache size of 24 is a guess; it should exceed the maximum number of simultaneously
open files by a comfortable margin, since directory vnodes along active paths are
also held.

---

## 16. Phasing

| Gate | Content | Exit criterion |
|---|---|---|
| **V0** | Structures, error space, header freeze | `vfs.h` compiles under both host GCC and Calypsi; static assertions pass |
| **V1** | Vnode cache, mount table, `vfs_namei` | A stub in-memory file system mounts at `/` and resolves paths, on host |
| **V2** | NVFS driver reworked behind `vnode_ops` | Existing NVFS test suite passes unchanged through the VFS |
| **V3** | `file_t`, fd table, syscall dispatch | `open`/`read`/`close` on NVFS through the real syscall path |
| **V4** | ext2 read-only driver behind the same table (= DN-FS-EXT2-001 F1) | Both file systems mounted simultaneously; paths resolve across the mount point |
| **V5** | Shared block cache (§9), `commit_seq` ordering | NVFS journal invariant holds under the shared cache; crash-injection suite passes |
| **V6** | Locking primitives activated | Deferred to the Amiga milestone; no work before then beyond §12's rules |

**V2 is the item that has to happen before ext2 implementation starts**, and it is
the one with the least visible payoff — it is a rework of code that already works.
That is precisely why it is easy to skip and expensive to skip.

V5 can trail V4: ext2 running on its own private cache is a valid intermediate state,
since the `commit_seq` mechanism exists for NVFS's benefit.

---

## 17. Open items

| ID | Item | Blocks |
|---|---|---|
| **OI-1** | Does NVFS (DN-FS-NVFS-003) expose a **stable 32-bit file identifier** usable as `vnode.file_id`? The vnode cache is keyed on it. If NVFS identifies files by extent location or by any value that changes on modification, the cache key breaks and either NVFS or this design must change. **Highest priority — verify before V0.** | V0, V1 |
| **OI-2** | Shared block cache with a `commit_seq` hook (§9.3) versus two private caches. The hook is used by one driver only, which is a smell; the duplication is permanent. | V5 |
| **OI-3** | Rework cost of the existing NVFS driver's call surface (V2). Needs a concrete estimate before V0–V6 can be slotted into the sequential plan. | Scheduling |
| **OI-4** | Where do V0–V5 go in the v3 sequential 99-step plan? Same question as DN-FS-EXT2-001 OI-1, and the two must be answered together since V2 gates F1. | Scheduling |
| **OI-5** | `fsops.h` in DN-FS-FUSE-001: is it path-based or inode-based? (§14). Cheap to fix now, expensive after three implementations. | FUSE track |
| **OI-6** | Vnode cache size (24 proposed) and fd table size (16 per process) — both guesses that should be revisited once the GUI's file access patterns are known. | V1 |
| **OI-7** | Is a `devfs` needed for the console and other character devices, or do those stay outside the file system namespace? Affects whether `open`/`close` slots are needed in `vnode_ops`. | V3 |
| **OI-8** | Kernel stack budget of 512 B per process (§12.3) needs validation against the deepest real call chain once ext2 truncate exists. | Amiga milestone |

## 18. Closed decisions

| Decision | Rationale |
|---|---|
| A VFS layer exists, specified before ext2 implementation | Introducing it after ext2 means the same rework applied to two drivers plus a branched syscall layer |
| Two tables: `vfs_ops` per mount, `vnode_ops` per vnode | Standard split; matches the natural division of the operations |
| `lookup` returns a `file_id`, never a vnode | Drivers cannot know about cached vnodes or grafted mounts; returning vnodes leaks the cache into every driver |
| Path resolution, symlink following and mount crossing are VFS-owned | Neither driver can see across a mount boundary |
| `readdir` uses an opaque `uint32_t` cookie | NVFS advances a slot index, ext2 a byte offset; neither is expressible in the other's terms |
| `file_t` is separate from `vnode_t` | Required for independent offsets across `open` and shared offsets across `dup` |
| Vnode cache is refcounted and VFS-owned, not per driver | Two copies of one inode diverge silently on append; the logic is identical in both drivers |
| Write ops may be `NULL`; VFS returns `E_NOTSUP` | Makes read-only ext2 a genuinely smaller deliverable |
| Block size is a per-mount property, never a constant | NVFS 2048 (= MMU page), ext2 1024 |
| Single unified error space; drivers map at their boundary | These values reach userland through the COP ABI |
| `E_CORRUPT` forces the mount read-only and is sticky | Writing to a file system that failed a consistency check turns recoverable into unrecoverable |
| `E_XDEV` checked in the VFS for `link` and `rename` | A driver handed a foreign vnode dereferences the wrong `fs_private` |
| No permission enforcement; `uid`/`gid`/`mode` read and preserved only | No user model exists |
| Timestamps supplied by the VFS via `vfs_now()`; drivers never call the clock | One function to change when an RTC appears |
| Deferred delete on last `vput` with zero link count | Makes unlink-while-open safe; free at design time |
| Locking discipline specified now, primitives no-op until the Amiga milestone | Retrofitting locking into a vnode cache fails intermittently rather than cleanly; rule 2 (lock ordering) is the expensive part to discover late |
| Path resolution iterative; `dirent_out_t` always caller-provided | Per-process kernel stacks all live in bank 0 — the scarcest address space in the machine |
| cwd stored as a string per process | `getcwd` by reverse lookup is real code with a real failure mode, to save 128 bytes |
| POSIX mode/type encodings pass through unmodified | NVFS draft 3 already adopted them; no translation table needed in either direction |

---

*End of DN-FS-VFS-001 Draft 1.*
