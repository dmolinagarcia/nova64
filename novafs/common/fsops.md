# fsops — change log and findings

Sheet [Y4.5](../../docs/docsV3/content/sec_ai_y4.md) asks that every change
to the table be recorded with the reason that forced it, because the log is
the evidence [sheet Y1](../../docs/docsV3/content/sec_ai_y1.md) needs. This
file is that log. The header is [`fsops.h`](fsops.h).

Where the table and Y1's `vnode_ops` disagree, Y1 is the authority
([D99](../../docs/docsV3/content/sec_ai_q.md)). The disagreements are
question [Q172](../../docs/docsV3/content/sec_ai_q.md), which stays open.
The findings below are evidence for it. None of them decides it.

## Starting shape

The table of sheet Y4: the volume, read, directory, file and write groups.
The declarations come from §4.6 of the source note
([DN-FS-FUSE-001](../../docs/docsV3/raw/DN-FS-FUSE-001-filesystem-learning-track.md)):

- the table is keyed on inode numbers;
- `fs_attr_t` is POSIX-shaped, with 64-bit widths;
- `fs_dirent_t` carries a 256-byte name inline;
- cursors and open files are objects the driver owns;
- the write half is present from the start.

## Changes

| # | Gate | Change | Why |
|---|---|---|---|
| 1 | FUS-03 | Conventions written into the header: `readdir` answers `FS_ENOENT` after the last entry; a `NULL` slot means the caller answers `FS_ENOTSUP` (Y1.9); `FS_MOUNT_RDONLY`; the `FS_O_*` access modes; the `FS_S_*` and `FS_DT_*` values. | The starting shape left them open, and a first caller cannot be written without them. The shape is unchanged. |
| 2 | FUS-03 | `FS_EMFILE` added to `fs_err_t`. | Cursors and files are the driver's objects. Without a heap they come from fixed pools, and a pool can run out. See finding 2. |

## Findings from FAT32 (FUS-03)

1. **The entry's name buffer (Q172, Y1.12).** A FAT long name can be up to
   255 UTF-16 units, which is up to 765 bytes of UTF-8, and the inline name
   holds 255. FAT32 returns the 8.3 alias instead, which `lookup` also finds.
   The header is unchanged. Y1's `dirent_out_t` has the same 256 bytes and a
   one-byte `name_len`, so the limit is the same there. Only FAT can hit it,
   and FAT never runs on the machine (D98).

2. **Cursor or cookie (Q172, Y1.3).** FAT32's cursor holds a position in the
   cluster chain and a 520-byte buffer for assembling long names. The whole
   `fat_dir_t` is 568 bytes. A 32-bit cookie can still express the position,
   because a directory holds at most 65 536 entries. Say the cookie names the
   entry after the last one returned. Every call then starts at the boundary
   of a long-name run, so the assembly buffer only has to last one call and
   could be scratch.

   The cookie costs a walk of the cluster chain from the start on every
   call. With 512-byte clusters a 2 MiB directory has 4 096 clusters, so a
   full listing is quadratic, unless the driver keeps the last
   (cookie, cluster) pair per mount. The read path already does that per
   file (FUS-03.k). So FAT32 does not need the cursor. It could live with
   Y1's cookie for the price of that small cache. The cursor's pool is also
   the only reason change 2 exists, and Y1's design has no such error.

3. **`.` and `..` (Y2.13, Y1.11).** Listings start with both, including the
   root's, and `..` carries a real inode number. For a subdirectory, finding
   that number means reading `..`, which only gives a cluster, and then
   scanning the grandparent for the entry that points at it. That is one
   directory scan per listing. NVFS has the caller supply the parent instead
   (Y2.13), and Y1 resolves `..` in the VFS (Y1.11). Either would save FAT32
   the scan.

4. **`open` and `close` (Q172, Y1.10).** FAT32's per-open state is a cached
   position in the cluster chain (`fat_file_t`, 48 bytes). It makes
   sequential reads cost one FAT lookup per cluster. It is there for speed,
   not correctness. Without `open`, a small per-mount cache keyed by inode
   number would do the same job.

5. **Widths (Q172).** FAT32 is 32-bit throughout: files are under 4 GiB and
   sectors are numbered in 32 bits. It gives no evidence for or against the
   64-bit offsets. The binding reads nothing at or past 4 GiB.

6. **Downgraded mounts (Y1.13).** The table cannot report that a read-write
   mount became read-only, because its `statfs` has no field for it; Y1's
   `statfs_out_t` does. So FAT32 refuses a read-write mount with `FS_EROFS`
   rather than downgrading silently.

7. **Where the volume starts (Y1 `vfs_ops.mount`).** Y1's `mount` receives
   the partition's start. This table mounts a device that is already the
   volume. Finding the volume stays below the driver (`fat_probe`,
   `bdev_part_open`), and nothing needs changing.

8. **Ownership, permissions and the clock (Q176, Y4.20).** FAT stores none of
   them. `getattr` reports uid and gid 0 and the modes of Y1.16, with the
   write bits cleared by the read-only attribute. Times are reported with no
   zone applied. Nothing asks the host who the user is, what time it is, or
   what zone it is in. Matching a host mount (`uid=`, `gid=`, `umask=`,
   `tz=`) is the binding's job (FUS-03.m). So the synthesis is the same on
   every host, which is the property Q176 asks for.

9. **What `getattr` costs.** To match the Linux driver, a directory's link
   count is 2 plus its subdirectories, and its size is its cluster chain. So
   `getattr` on a directory scans the directory and walks the chain. The
   kernel pays that once per cached inode. This driver pays it on every call,
   because caching belongs to the VFS (Y1.6).
