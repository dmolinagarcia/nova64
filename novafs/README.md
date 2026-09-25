# novafs — the filesystem host track

This is the code of the host track in
[sheet Y4](../docs/docsV3/content/sec_ai_y4.md). So far it holds the first
of its three filesystems: **FAT32, read-only**, which is gate FUS-03. The
FAT32 driver sits behind the `fsops` table, and that table is what the track
delivers (Y4.5). It is discovered by implementing filesystems behind it, and
then lifted into [sheet Y1](../docs/docsV3/content/sec_ai_y1.md) (Y4.23).

FAT32 runs on the host and never on the machine
([D98](../docs/docsV3/content/sec_ai_q.md)). What this gate buys is the
synthesis problem (Y4.15): a format with no inodes, owners or mode bits
still has to answer an inode-keyed, POSIX-shaped table. That also produces
the first evidence about the table itself, recorded in
[`common/fsops.md`](common/fsops.md).

New to it? [HOWTO.md](HOWTO.md) explains, in Spanish and from scratch, how
to plug an SD block reader into the library and call it from a shell.

## Layers (Y4.4)

| Layer | Files | Rule |
|---|---|---|
| L0 block device | `common/bdev.[ch]`, `common/bdev_file.c` | The only layer that knows the store. Checks every request against the device and fails it rather than reading short (Y4.9, Y4.10). |
| L1 on-disk decoding | `fat32/fat32_ondisk.[ch]`, `common/mbr.[ch]`, `common/endian.h` | No I/O. Offsets and accessors, never a packed struct (Y4.7; Q173 is open). |
| L2 filesystem logic | `fat32/fat32_fs.h`, `fat32_{fs,fat,dir,lfn,file,attr}.c` | Its own error enum (`common/fs_err.h`, Y4.8). No FUSE type anywhere. |
| The table | `common/fsops.h`, `fat32/fat32_fsops.[ch]` | Keyed on inode numbers; `lookup` takes a parent and one component (Y4.6). |
| L3 FUSE binding | — | Not written yet: FUS-03.m and FUS-03.n. |

The code follows the track's rules. It never allocates. It uses 32-bit
arithmetic, and `make check16` compiles it with a 16-bit `int`. Every value
read from disk is checked before it becomes an address. It is
single-threaded (Y4.13).

## Building and testing

```sh
make            # build/libnovafs_fat32.a, build/fat32tool, build/test_unit
make test       # unit tests, then the image tests
make san        # the same under AddressSanitizer and UBSan
make check16    # compile the portable sources with a 16-bit int and size_t
```

The image tests need `python3`, `dosfstools` and `mtools`. `check16` needs
`clang`.

## Using it

Through the table, the way a VFS or the FUSE binding would use it. The
caller resolves paths, one component per `lookup` (Y1.11):

```c
static fat32_mount_t m;
const fsops_t *ops = &fat32_fsops;
fs_ino_t ino;
fs_file_t *f;
size_t got;

ops->mount(&m, bd, FS_MOUNT_RDONLY);         /* bd is the volume itself */
ops->lookup(&m, FS_INO_ROOT, "docs", &ino);
ops->lookup(&m, ino, "readme.txt", &ino);
ops->open(&m, ino, FS_O_RDONLY, &f);
ops->read(&m, f, buf, sizeof buf, 0, &got);
ops->close(&m, f);
```

The volume is found below the driver. `fat_probe` checks for a whole-disk
volume first, then for the first MBR partition that holds FAT32.
`bdev_part_open` gives that partition as a device of its own.

L2 can also be called directly: `fat_mount_auto`, `fat_open`,
`fat_file_read`, `fat_opendir`, `fat_dir_read`. This is the FAT view, with
short names and FAT attributes.

`build/fat32tool` does both from a shell. `ls`, `tree`, `stat` and `cat` go
through the table; `info` and `dir` go through L2:

```sh
build/fat32tool card.img ls /docs            # what a caller is told
build/fat32tool card.img dir /docs           # what FAT stores
build/fat32tool card.img stat /docs/readme.txt
build/fat32tool -o 2048 card.img tree        # volume at sector 2048
```

## Where FUS-03 stands

| Step | State |
|---|---|
| a. BPB decoded | Done. Geometry matches `fsck.fat -v` on the three whole-disk images. |
| b. FSInfo | Done. All three signatures are checked, and absurd counts are dropped. |
| c. FAT access | Done. Out-of-range, free and bad successors are refused; a direct-mapped cache sits over the FAT. |
| d. Chain iterator | Done. Bounded by the volume size; cycles caught with Brent's algorithm. The looping images end in an error. |
| e. Short entries | Done. Order matches `mdir`. |
| f. Long names | Done, including orphaned runs, checksum mismatches and surrogates. |
| g. Inode numbers | Done. See below. |
| h. Path resolution | Done. `ENOTDIR` is kept distinct from `ENOENT`. |
| i. Attribute synthesis | The rules are in place (`fat32_attr.c`). Agreement with a kernel mount is untested. |
| j. Listing | Content matches the source tree and order matches `mdir`. No kernel mount yet. |
| k. Open and read | Done. Whole-file CRCs and partial reads match the source tree. |
| l. Free space | Matches `fsck.fat`. No kernel mount yet. |
| m. Mount options | Belong to the FUSE binding, which is not written. |
| n. Differential pass | Not done. It needs a kernel `vfat` mount, which this environment cannot provide. |
| o. Damage | Done. 23 named kinds of damage plus random damage: no crash, no hang, no sanitizer report. |

Several parts of FUS-02 were built on the way, but not every one of its
acceptance tests has run:

- the block layer and its range tests;
- the accessors;
- the corruption logger;
- the `fsops` header, proven through the tool and the unit tests rather
  than a pass-through filesystem;
- a generated damage corpus.

## Behaviour worth knowing

- **Attributes are made up under stated rules** (FUS-03.i, the comment at the
  top of `fat32_attr.c`), and none of them asks the host anything (Y4.20):
  - mode is 0644 for files and 0755 for directories, as in Y1.16. The
    read-only attribute clears the write bits.
  - uid and gid are 0.
  - A directory's link count is 2 plus its subdirectories.
  - A directory's size is its cluster chain.
  - Times are local time with no zone applied. The root has none.
- **Inode numbers** are `2 + (cluster − 2) × entries_per_cluster + index` of
  the short entry, and 1 for the root. They are unique while the file exists
  and stable until it moves. They fit in 32 bits for data regions up to
  128 GiB. The table refuses a larger volume; L2 mounts it and gives every
  node `FAT_INO_NONE`.
- **Listings through the table start with `.` and `..`**, for the root too,
  as the Linux driver's do. `fat_dir_read` in L2 leaves both out.
- **Read-only.** A read-write mount is refused rather than downgraded
  (Y1.13), and every write slot is `NULL` (Y1.9).
- **FAT32 only, decided by cluster count.** A volume with fewer than
  65 525 clusters is refused, including one made with `mkfs.vfat -F 32`,
  which Linux would mount. Clusters are at most 32 KiB. exFAT and GPT are not
  supported.
- **Damage is reported, never absorbed.** Every case returns `FS_ECORRUPT`
  through `fs_corrupt()`, which logs (Y4.8). Chains are bounded and checked
  for cycles. A directory stops at 65 536 entries. A chain shorter than its
  file's size is an error, not a short read. An orphaned long name is not an
  error: it gives way to the short name, as it does in the kernel.
- **Names.** Short names are decoded as code page 437. Long names are UTF-16
  converted to UTF-8. Lookups fold ASCII case only. A long name too big for
  the table's 255 bytes is returned as its 8.3 alias
  ([finding 1](common/fsops.md)).
- **Free space** is counted from the FAT once, then cached. The FSInfo count
  is only a hint, as it is in the Linux driver by default.

## Configuration

| Macro | Default | Effect |
|---|---|---|
| `FAT_CFG_MAX_SECTOR` | 4096 | Largest logical sector; sizes every buffer |
| `FAT_CFG_FAT_CACHE` | 4 | FAT sector cache slots, a power of two |
| `FAT_CFG_NAME_MAX` | 765 | Longest UTF-8 name L2 returns |
| `FAT32_FSOPS_DIRS`, `FAT32_FSOPS_FILES` | 8, 16 | Cursor and open-file pools per mount |

With the defaults, `fat_fs_t` is about 20 KiB. Set to 512, 2 and 255, it is
1.6 KiB. Building with `NOVAFS_FREESTANDING` leaves out stdio and the file
backend.

## Tests

`tests/unit/test_fat32.c` runs on byte arrays, with no image files and no
tools:

- L0: range checks and partition views;
- L1: BPB validation at the FAT12/16/32 boundaries, FSInfo, directory entry
  kinds, short names, timestamps and their conversion to seconds;
- long-name assembly, including orphans and surrogates;
- a FAT32 volume built in memory, mounted whole and inside an MBR, then
  driven through every slot of the table.

`tests/run_tests.py` builds a source tree and writes it with `mkfs.vfat` and
`mtools` into four images:

- 4 KiB clusters;
- 512-byte clusters;
- 4 KiB logical sectors, opened with both sector sizes;
- an MBR disk whose first partition is not FAT.

Each image has a fragmented file and deleted entries. The long names mtools
cannot write are patched in by hand. The tool's output is checked against
three oracles:

- the source tree (names, sizes, CRCs, times) and `mdir` (order);
- `fsck.fat -v` (geometry and cluster usage);
- the library itself: `fat32tool check` compares the table with L2 on every
  entry.

The script also checks the synthesised attributes, partial reads, error
codes, and a FAT with mirroring switched off. Then it damages an image in
23 named ways and at random, and requires a clean error or a clean pass
every time (FUS-03.o, Y4.12).

Just testing branches....