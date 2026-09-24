# novafs — read-only FAT32

A C library that mounts a FAT32 volume and reads it: path resolution,
directory listing with long file names, file reads at any offset, free
space. It never writes.

It is built the way [DN-FS-FUSE-001](../docs/docsV3/raw/DN-FS-FUSE-001-filesystem-learning-track.md)
and [sheet Y4](../docs/docsV3/content/sec_ai_y4.md) lay the host track out:
layered, with no allocation, 32-bit arithmetic throughout and correct with a
16-bit `int`, and hostile to damaged images rather than trusting them. It
covers the library half of gate FUS-03; the FUSE binding and the kernel
differential pass (FUS-03.m, FUS-03.n) are not here yet.

## Layout

| Layer | Files | Rule |
|---|---|---|
| L0 block device | `common/bdev.[ch]`, `common/bdev_file.c` | The only layer that knows what the store is. Range-checks every request. |
| L1 on-disk decoding | `fat32/fat32_ondisk.[ch]`, `common/mbr.[ch]`, `common/endian.h` | No I/O. Offsets and accessors, never a packed struct. |
| L2 filesystem logic | `fat32/fat32_fs.h` (the API), `fat32_fs.c`, `fat32_fat.c`, `fat32_dir.c`, `fat32_lfn.c`, `fat32_file.c` | Its own error enum (`common/fs_err.h`). |
| Tool | `tools/fat32tool.c` | Command-line front end, used by the tests. |
| Tests | `tests/unit/`, `tests/run_tests.py` | See [Testing](#testing). |

## Building

```sh
make            # build/libnovafs_fat32.a, build/fat32tool, build/test_unit
make test       # unit tests, then the image tests
make san        # the same under AddressSanitizer and UBSan
make check16    # type-check the portable sources for a 16-bit-int target
```

The image tests need `python3`, `dosfstools` and `mtools`; `check16` needs
`clang`.

## Using it

```c
#include "bdev.h"
#include "fat32_fs.h"

static fat_fs_t fs;                 /* holds the sector buffers */
bdev_t bd;
fat_file_t f;
uint8_t buf[512];
uint32_t got;

bdev_file_open(&bd, "card.img", 512);
if (fat_mount_auto(&fs, &bd) != FS_OK)          /* whole disk or MBR */
    ...;
if (fat_open(&fs, "/docs/readme.txt", &f) == FS_OK)
    while (fat_file_read(&f, buf, sizeof buf, &got) == FS_OK && got > 0)
        fwrite(buf, 1, got, stdout);
```

Listing a directory:

```c
fat_dir_t d;
fat_dirent_t de;                    /* ~830 bytes: keep it off a small stack */
fs_err_t e;

if (fat_opendir(&fs, "/docs", &d) == FS_OK) {
    while ((e = fat_dir_read(&d, &de)) == FS_OK)
        printf("%-12s %10u %s\n", de.short_name, de.node.size, de.name);
    /* e is FS_ENOENT at the end, anything else is an error */
}
```

Everything is a caller-owned structure and nothing needs releasing except
the device. A node (`fat_node_t`) is a plain value: a decoded copy of a
directory entry. Alongside the path functions there is an inode-keyed set
(`fat_root`, `fat_lookup`, `fat_parent`, `fat_node_from_ino`) for the
`fsops` table sheet Y4 builds.

The `fat32tool` front end does the same from a shell:

```sh
build/fat32tool card.img info
build/fat32tool card.img ls /docs
build/fat32tool card.img tree
build/fat32tool card.img cat /docs/readme.txt
build/fat32tool -o 2048 card.img stat /docs      # volume at sector 2048
```

## Porting

L0 is one read hook: fill a `bdev_t` with `read`, `sector_size` and
`sector_count` and the rest is unchanged. That is where the SD block driver
of sheet G goes. Build with `NOVAFS_FREESTANDING` to drop stdio and the file
backend. Memory is set at compile time:

| Macro | Default | Effect |
|---|---|---|
| `FAT_CFG_MAX_SECTOR` | 4096 | Largest logical sector; sizes every buffer |
| `FAT_CFG_FAT_CACHE` | 4 | FAT sector cache slots, a power of two |
| `FAT_CFG_NAME_MAX` | 765 | Longest UTF-8 name returned |

With the defaults, `fat_fs_t` is about 20 KiB. With 512, 2 and 255 it is
1.6 KiB and `fat_dirent_t` is 320 bytes. `fat_dir_t` is 568 bytes either
way, because it holds the long name being assembled. `make check16`
type-checks the portable sources with a 16-bit `int` and `size_t` under
`-Wconversion -Werror`.

## Behaviour worth knowing

- **FAT32 only, decided by cluster count.** A volume with fewer than 65 525
  clusters is FAT12 or FAT16 whatever its boot sector says, and it is
  refused. This includes a small volume made with `mkfs.vfat -F 32`, which
  Linux would mount.
- **Damage is reported, never absorbed.** Every value read from disk is
  range-checked before it is used as an address. Cluster chains are bounded
  by the volume size and checked for cycles with Brent's algorithm, so they
  cost no extra FAT reads. A directory stops at 65 536 entries. A file whose
  chain is shorter than its size is an error, not a short read. Every such
  case returns `FS_ECORRUPT` through `fs_corrupt()`, which logs. The default
  log goes to stderr, and `fs_set_corrupt_logger()` replaces it.
- **Orphaned long names are not errors.** A long-name run with a broken
  ordinal sequence, or a checksum that does not match its short entry, is
  dropped in favour of the short name, as the kernel does.
- **`fat_dir_read` never returns `.` or `..`.** Neither the root nor
  subdirectories return them, so all directories behave alike. `fat_lookup`
  and paths resolve them to real nodes. For `..`, that means reading the
  grandparent to find the parent's own entry.
- **Inode numbers come from the position of the short entry**:
  `2 + (cluster − 2) × entries_per_cluster + index`, and 1 for the root. They
  are unique while a file exists and stable until it moves. They fit in
  32 bits for data regions up to 128 GiB. Past that, the volume still mounts
  and every number is `FAT_INO_NONE`.
- **Lookups fold ASCII case only.** Both the long and the short name match.
- **Short names are decoded as code page 437**, which is the Linux default.
  The NT lowercase flags are applied. Long names are UTF-16 turned into
  UTF-8, with surrogate pairs joined and unpaired halves replaced by U+FFFD.
- **Timestamps are returned as calendar fields with no zone**, because FAT
  stores local time. An invalid date decodes to 1980-01-01 00:00:00.
- **Free space is counted from the FAT** on the first `fat_statfs` and then
  cached. The FSInfo counter is only kept as a hint (`fs.fsinfo_free`),
  which is also the kernel's default.
- **Mirroring**: when `BPB_ExtFlags` turns it off, only the active FAT is
  read.
- **Sectors**: a logical sector may span several device sectors (a 4 KiB
  sector volume on a 512-byte device) but never the other way round.
- **Partitions**: `fat_mount_auto` takes a whole-disk volume, or else the
  first MBR primary partition that holds FAT32, whatever its type byte says.
  GPT, extended partitions and exFAT are not supported.

## Testing

`tests/unit/test_fat32.c` covers L0 and L1 on static byte arrays: range
checks, BPB validation and FAT type boundaries, FSInfo, directory entry
kinds, short names, timestamps, and long-name assembly including orphans,
surrogates and overflow.

`tests/run_tests.py` builds a source tree and writes it with `mkfs.vfat` and
`mtools` into four images:

- 4 KiB clusters;
- 512-byte clusters;
- 4 KiB logical sectors, opened with both 512- and 4096-byte device sectors;
- an MBR disk whose first partition is not FAT.

Each image has a fragmented file and deleted entries. Some long names are
patched in by hand, because mtools cannot write them. The tool's output is
checked against three oracles:

- the source tree: every name, size, CRC-32 and timestamp, and directory
  order as `mdir` reports it;
- `fsck.fat -v`: geometry and cluster usage;
- the library itself (`fat32tool check`): lookup by long, short and
  case-folded name, inode round trips and uniqueness, parents, and backward
  reads compared with forward ones.

It also checks partial reads at boundaries and error codes.

The same script then damages the image in 23 named ways, and then at random.
It requires the named errors, and never a crash, a hang or a sanitizer
report. The damage includes chain cycles, free, bad or out-of-range
successors, short chains, a directory entry pointing at its own ancestor,
and a truncated image.
