#!/usr/bin/env python3
"""nv — host tools for noVa64 card images.

    nv.py image  kernel.bin -o kernel.nx --entry 0xFD0000 --seg ADDR:FILE[:MEMSZ] ...
    nv.py card   -o card.img --kernel kernel.nx --root DIR [--size-mb N] [--boot-kb N]
    nv.py fsck   card.img [--replay] [--allow-dirty]
    nv.py ls     card.img [PATH]
    nv.py cat    card.img PATH
    nv.py tree   card.img

The NVFS half is an independent implementation of DN-FS-NVFS-003 draft 3
(sheet Y2): `card` runs mkfs and populates the volume from a directory tree,
`fsck` checks every invariant of section 18 that a single pass can, and with
--replay runs journal recovery first, as that section requires. It is the
oracle the kernel's own driver is tested against, so it shares no code with it.
"""
import argparse, os, struct, sys, zlib

BS = 2048
SEC = 512
MBR_BOOT = 0x7E          # provisional (Q177), see os/include/hw.inc
MBR_NVFS = 0x7F
EXT_PER_NODE = 169
SLOTS = 30


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def block_crc(b):
    return crc32(b[:0x7FC] + b'\0\0\0\0')


def seal(b):
    b = bytearray(b)
    b[0x7FC:0x800] = b'\0\0\0\0'
    struct.pack_into('<I', b, 0x7FC, crc32(bytes(b)))
    return bytes(b)


# ------------------------------------------------------------------ executable
def make_image(entry, segs, kernel=True):
    """segs: list of (vaddr, data, memsz). Layout in os/include/abi.inc."""
    hdr_len = 0x20 + 20 * len(segs)
    off = hdr_len
    table = b''
    body = b''
    for vaddr, data, memsz in segs:
        memsz = max(memsz, len(data))
        if (vaddr & 0xFFFF) + memsz > 0x10000:
            sys.exit('segment at %06X crosses a bank boundary' % vaddr)
        table += struct.pack('<IIIIHH', vaddr, off, len(data), memsz, 7, 0)
        body += data
        off += len(data)
    total = hdr_len + len(body)
    if total >= 0x10000:
        sys.exit('image is %d bytes; the BIOS stages at most 64 KB' % total)
    hdr = bytearray(0x20)
    hdr[0:4] = b'\x7fN64'
    struct.pack_into('<HHIII', hdr, 4, 1, len(segs), entry, 0x1000, total)
    struct.pack_into('<H', hdr, 0x18, 1 if kernel else 0)
    img = bytearray(hdr + table + body)
    struct.pack_into('<I', img, 0x14, crc32(bytes(img)))
    return bytes(img)


# ------------------------------------------------------------------------ NVFS
SB_FMT = '<4sHHIIIIIIIIIIIIIIIIIIIII16s32sIIIIIHHIIIII'
SB_FIELDS = ('magic vmaj vmin block_size total_blocks free_blocks total_inodes free_inodes first_data '
             'bitmap_start bitmap_blocks summary_start summary_blocks ibitmap_start ibitmap_blocks '
             'itable_start itable_blocks journal_start journal_blocks root_inode backup_sb '
             'f_compat f_incompat f_ro_compat uuid label mkfs_time mount_time write_time mount_count '
             'max_mount_count state error_action last_check next_generation alloc_hint orphan_head '
             'partition_lba').split()
IMMUTABLE = ('block_size total_blocks total_inodes first_data bitmap_start bitmap_blocks summary_start '
             'summary_blocks ibitmap_start ibitmap_blocks itable_start itable_blocks journal_start '
             'journal_blocks root_inode backup_sb').split()
assert struct.calcsize(SB_FMT) == 0xB8


class Volume:
    """A partition inside an image file, addressed in NVFS blocks."""

    def __init__(self, f, lba, sectors):
        self.f, self.lba, self.sectors = f, lba, sectors
        self.total = sectors // 4
        self.sb = None

    def read(self, n):
        self.f.seek((self.lba + n * 4) * SEC)
        b = self.f.read(BS)
        return b + b'\0' * (BS - len(b))

    def write(self, n, data):
        assert len(data) == BS, len(data)
        self.f.seek((self.lba + n * 4) * SEC)
        self.f.write(data)

    def load_sb(self):
        raw = self.read(1)
        self.sb = dict(zip(SB_FIELDS, struct.unpack_from(SB_FMT, raw)))
        return raw

    def store_sb(self, block=1):
        b = bytearray(BS)
        struct.pack_into(SB_FMT, b, 0, *[self.sb[k] for k in SB_FIELDS])
        self.write(block, seal(b))


def pack_inode(mode, links, size, blocks, gen, ext=(), depth=0, inline=b'', orphan_next=0):
    b = bytearray(128)
    struct.pack_into('<HHIIIIIIHBB', b, 0, mode, links, size, blocks, 0, 0, 0, gen, 0, len(ext), depth)
    for i, (l, p, n) in enumerate(ext):
        struct.pack_into('<IIHH', b, 0x20 + 12 * i, l, p, n, 0)
    if inline:
        b[0x20:0x20 + len(inline)] = inline
    struct.pack_into('<I', b, 0x70, orphan_next)
    struct.pack_into('<I', b, 0x7C, crc32(bytes(b[:0x7C])))
    return bytes(b)


def dir_slot(ino, name, dtype):
    return struct.pack('<IHBB56s', ino, crc32(name) & 0xFFFF, len(name), dtype, name)


class Mkfs:
    def __init__(self, vol, label=b''):
        self.v = vol
        t = vol.total
        if vol.lba % 4:
            sys.exit('mkfs: the partition must start on a multiple of 4 sectors (Y2.25)')
        if t < 4096:
            sys.exit('mkfs: a volume needs at least 4096 blocks (Y2.25)')
        ring = max(256, min(8192, t // 64))
        ninodes = t // 8
        s = dict.fromkeys(SB_FIELDS, 0)
        s.update(magic=b'NVFS', vmaj=3, vmin=0, block_size=BS, total_blocks=t, total_inodes=ninodes,
                 root_inode=1, backup_sb=t - 1, f_ro_compat=3, uuid=os.urandom(16), label=label,
                 next_generation=1, partition_lba=vol.lba)
        s['journal_start'], s['journal_blocks'] = 8, ring + 1
        s['bitmap_start'] = 8 + ring + 1
        s['bitmap_blocks'] = -(-t // 16384)
        s['summary_start'] = s['bitmap_start'] + s['bitmap_blocks']
        s['summary_blocks'] = -(-s['bitmap_blocks'] // 1024)
        s['ibitmap_start'] = s['summary_start'] + s['summary_blocks']
        s['ibitmap_blocks'] = -(-ninodes // 16384)
        s['itable_start'] = s['ibitmap_start'] + s['ibitmap_blocks']
        s['itable_blocks'] = -(-ninodes // 16)
        s['first_data'] = s['itable_start'] + s['itable_blocks']
        vol.sb = s
        self.used = bytearray(t)                 # one byte per block while building
        for b in range(s['first_data']):
            self.used[b] = 1
        self.used[t - 1] = 1
        self.next_block = s['first_data']
        self.inodes = {}                         # ino -> 128 bytes
        self.next_ino = 8

    def alloc(self, n=1):
        b = self.next_block
        if b + n > self.v.total - 1:
            sys.exit('mkfs: volume full')
        for i in range(n):
            self.used[b + i] = 1
        self.next_block += n
        return b

    def gen(self):
        g = self.v.sb['next_generation']
        self.v.sb['next_generation'] += 1
        return g

    def new_ino(self):
        i = self.next_ino
        if i > self.v.sb['total_inodes']:
            sys.exit('mkfs: out of inodes')
        self.next_ino += 1
        return i

    def extents_for(self, nblocks):
        """Lay nblocks down contiguously; returns the leaf list."""
        out, l = [], 0
        while nblocks:
            n = min(nblocks, 65535)
            out.append((l, self.alloc(n), n))
            l += n
            nblocks -= n
        return out

    def tree(self, ino, leaves):
        """Root entries, depth and node-block count for a leaf list (depth <= 1 is all mkfs needs)."""
        if len(leaves) <= 6:
            return leaves, 0, 0
        groups = [leaves[i:i + EXT_PER_NODE] for i in range(0, len(leaves), EXT_PER_NODE)]
        if len(groups) > 6:
            sys.exit('mkfs: file too fragmented')
        root = []
        for g in groups:
            nb = self.alloc()
            b = bytearray(BS)
            struct.pack_into('<4sHHI', b, 0, b'NVXN', len(g), 0, ino)
            for i, (l, p, n) in enumerate(g):
                struct.pack_into('<IIHH', b, 0x10 + 12 * i, l, p, n, 0)
            self.v.write(nb, seal(b))
            root.append((g[0][0], nb, 0))
        return root, 1, len(groups)

    def add_file(self, data, frag=1):
        ino = self.new_ino()
        nb = -(-len(data) // BS)
        if frag > 1:                             # test aid: deliberately fragmented layout
            leaves, l = [], 0
            while l < nb:
                n = min(frag, nb - l)
                leaves.append((l, self.alloc(n), n))
                self.alloc()                     # a gap, so neighbours cannot merge
                self.used[self.next_block - 1] = 0
                l += n
        else:
            leaves = self.extents_for(nb)
        for l, p, n in leaves:
            for i in range(n):
                chunk = data[(l + i) * BS:(l + i + 1) * BS]
                self.v.write(p + i, chunk + b'\0' * (BS - len(chunk)))
        root, depth, nodes = self.tree(ino, leaves)
        self.inodes[ino] = pack_inode(0x81A4, 1, len(data), nb + nodes, self.gen(), root, depth)
        return ino

    def add_symlink(self, target):
        ino = self.new_ino()
        if len(target) <= 72:
            self.inodes[ino] = pack_inode(0xA1FF, 1, len(target), 0, self.gen(), inline=target)
        else:
            leaves = self.extents_for(1)
            self.v.write(leaves[0][1], target + b'\0' * (BS - len(target)))
            self.inodes[ino] = pack_inode(0xA1FF, 1, len(target), 1, self.gen(), leaves)
        return ino

    def add_dir(self, entries, ino=None):
        """entries: list of (name, ino, dtype)."""
        if ino is None:
            ino = self.new_ino()
        nb = max(1, -(-len(entries) // SLOTS))
        leaves = self.extents_for(nb)
        for i in range(nb):
            part = entries[i * SLOTS:(i + 1) * SLOTS]
            b = bytearray(BS)
            struct.pack_into('<4sIHHI', b, 0, b'NVDB', ino, len(part), 0, 0)
            for j, (name, eino, dtype) in enumerate(part):
                b[0x40 + 64 * j:0x80 + 64 * j] = dir_slot(eino, name, dtype)
            self.v.write(leaves[0][1] + i, seal(b))
        root, depth, nodes = self.tree(ino, leaves)
        self.inodes[ino] = pack_inode(0x41ED, 2, nb * BS, nb + nodes, self.gen(), root, depth)
        return ino

    def populate(self, path, ino=None, frag=None):
        entries = []
        for name in sorted(os.listdir(path)) if path else []:
            if name == '.keep':                  # keeps an empty directory in git, not on the card
                continue
            full = os.path.join(path, name)
            bname = name.encode('latin-1', 'replace')
            if len(bname) > 56:
                sys.exit('mkfs: name too long: ' + name)
            if os.path.islink(full):
                entries.append((bname, self.add_symlink(os.readlink(full).encode()), 10))
            elif os.path.isdir(full):
                entries.append((bname, self.populate(full, frag=frag), 4))
            else:
                with open(full, 'rb') as fh:
                    data = fh.read()
                f = frag.get(name, 1) if frag else 1
                entries.append((bname, self.add_file(data, f), 8))
        return self.add_dir(entries, ino)

    def finish(self):
        v, s = self.v, self.v.sb
        zero = bytes(BS)
        # journal: superblock, and a zeroed ring
        js = bytearray(BS)
        struct.pack_into('<4sIIII', js, 0, b'NVJS', s['journal_blocks'] - 1, 0, 1, 0)
        v.write(s['journal_start'], seal(js))
        for i in range(1, s['journal_blocks']):
            v.write(s['journal_start'] + i, zero)
        # block bitmap and summary
        nbits = s['bitmap_blocks'] * 16384
        bits = bytearray(nbits // 8)
        for b in range(nbits):
            if b >= v.total or self.used[b]:
                bits[b >> 3] |= 1 << (b & 7)
        summ = bytearray(s['summary_blocks'] * BS)
        for i in range(s['bitmap_blocks']):
            chunk = bits[i * BS:(i + 1) * BS]
            v.write(s['bitmap_start'] + i, bytes(chunk))
            free = 16384 - sum(bin(x).count('1') for x in chunk)
            struct.pack_into('<H', summ, 2 * i, 0xFFFF if free == 16384 else free)
        for i in range(s['summary_blocks']):
            v.write(s['summary_start'] + i, bytes(summ[i * BS:(i + 1) * BS]))
        # inode bitmap and table
        nibits = s['ibitmap_blocks'] * 16384
        ibits = bytearray(nibits // 8)
        for n in range(nibits):
            ino = n + 1
            if ino > s['total_inodes'] or ino < 8 or ino in self.inodes:
                ibits[n >> 3] |= 1 << (n & 7)
        for i in range(s['ibitmap_blocks']):
            v.write(s['ibitmap_start'] + i, bytes(ibits[i * BS:(i + 1) * BS]))
        free_inode = pack_inode(0, 0, 0, 0, 0)
        for i in range(s['itable_blocks']):
            blk = b''.join(self.inodes.get(i * 16 + k + 1, free_inode) for k in range(16))
            v.write(s['itable_start'] + i, blk)
        s['free_blocks'] = v.total - sum(self.used)
        s['free_inodes'] = s['total_inodes'] - 7 - len([i for i in self.inodes if i >= 8])
        s['alloc_hint'] = self.next_block
        v.store_sb(1)
        v.store_sb(v.total - 1)


def partitions(f):
    f.seek(0)
    mbr = f.read(512)
    if mbr[510:512] != b'\x55\xaa':
        sys.exit('no MBR signature')
    out = []
    for i in range(4):
        e = mbr[446 + 16 * i:462 + 16 * i]
        out.append((e[4], struct.unpack_from('<II', e, 8)))
    return out


def open_volume(path, mode='rb'):
    f = open(path, mode)
    for ptype, (lba, n) in partitions(f):
        if ptype == MBR_NVFS:
            return Volume(f, lba, n)
    sys.exit('no NVFS partition (type 0x7F)')


def make_card(out, kernel, root, size_mb, boot_kb, frag=None, label=b'noVa64'):
    boot_lba = 8
    boot_secs = boot_kb * 2
    nvfs_lba = (boot_lba + boot_secs + 3) & ~3
    nvfs_secs = (size_mb * 2048 - nvfs_lba) & ~3
    with open(out, 'w+b') as f:
        f.truncate(size_mb * 1024 * 1024)
        mbr = bytearray(512)

        def entry(i, ptype, lba, n):
            mbr[446 + 16 * i:462 + 16 * i] = struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', ptype, b'\xfe\xff\xff', lba, n)
        entry(0, MBR_BOOT, boot_lba, boot_secs)
        entry(1, MBR_NVFS, nvfs_lba, nvfs_secs)
        mbr[510:512] = b'\x55\xaa'
        f.write(mbr)
        if kernel:
            with open(kernel, 'rb') as k:
                img = k.read()
            if len(img) > boot_secs * SEC:
                sys.exit('kernel image does not fit the boot partition')
            f.seek(boot_lba * SEC)
            f.write(img)
        vol = Volume(f, nvfs_lba, nvfs_secs)
        m = Mkfs(vol, label)
        m.populate(root, ino=1, frag=frag)
        m.finish()


# ------------------------------------------------------------------------ fsck
class Fsck:
    def __init__(self, vol, out=sys.stdout):
        self.v, self.errors, self.notes, self.out = vol, [], [], out

    def err(self, msg):
        self.errors.append(msg)

    def inode(self, ino):
        s = self.v.sb
        raw = self.v.read(s['itable_start'] + ((ino - 1) >> 4))
        return raw[((ino - 1) & 15) << 7:(((ino - 1) & 15) << 7) + 128]

    def replay(self, write):
        """Journal recovery, section 13.7. Returns the number of transactions replayed."""
        v, s = self.v, self.v.sb
        js = v.read(s['journal_start'])
        if js[:4] != b'NVJS' or block_crc(js) != struct.unpack_from('<I', js, 0x7FC)[0]:
            self.err('journal superblock: bad magic or CRC')
            return -1
        ring, idx, seq = struct.unpack_from('<III', js, 4)
        if ring != s['journal_blocks'] - 1:
            self.err('journal superblock: js_ring_blocks disagrees with the superblock')
            return -1
        rb = lambda i: v.read(s['journal_start'] + 1 + i % ring)
        done = 0
        while True:
            d = rb(idx)
            if d[:4] != b'NVJD' or block_crc(d) != struct.unpack_from('<I', d, 0x7FC)[0]:
                break
            dseq, n = struct.unpack_from('<IH', d, 4)
            if dseq != seq or n == 0 or n > 507:
                break
            c = rb(idx + 1 + n)
            if c[:4] != b'NVJC' or block_crc(c) != struct.unpack_from('<I', c, 0x7FC)[0]:
                break
            cseq, pcrc = struct.unpack_from('<II', c, 4)
            if cseq != seq:
                break
            blocks = [rb(idx + 1 + i) for i in range(n)]
            if crc32(d + b''.join(blocks)) != pcrc:
                break
            dests = struct.unpack_from('<%dI' % n, d, 0x10)
            for dest in dests:
                if dest >= s['total_blocks'] or s['journal_start'] <= dest < s['journal_start'] + s['journal_blocks']:
                    self.err('journal: destination %d out of range' % dest)
                    return -1
            if write:
                for dest, b in zip(dests, blocks):
                    v.write(dest, b)
            idx, seq, done = (idx + n + 2) % ring, seq + 1, done + 1
        if done and write:
            struct.pack_into('<II', js := bytearray(js), 8, idx, seq)
            v.write(s['journal_start'], seal(js))
            v.load_sb()
        return done

    def leaves(self, ino, raw):
        """Walk an extent tree; returns (leaf list, node blocks) and reports structural errors."""
        count, depth = raw[0x1E], raw[0x1F]
        s = self.v.sb
        nodes, out = [], []
        if count > 6 or depth > 3:
            self.err('inode %d: bad extent header (count %d, depth %d)' % (ino, count, depth))
            return out, nodes

        def walk(entries, d, where):
            prev_end = -1
            for l, p, n, fl in entries:
                if l <= prev_end:
                    self.err('inode %d: extents unsorted or overlapping in %s' % (ino, where))
                if d == 0:
                    if n == 0 or p < s['first_data'] or p + n > s['total_blocks'] - 1 or fl & ~1:
                        self.err('inode %d: bad extent (%d,%d,%d,%d)' % (ino, l, p, n, fl))
                        continue
                    out.append((l, p, n, fl))
                    prev_end = l + n - 1
                else:
                    if p < s['first_data'] or p >= s['total_blocks'] - 1:
                        self.err('inode %d: index pointer %d out of range' % (ino, p))
                        continue
                    nb = self.v.read(p)
                    magic, ne, nd, nino = struct.unpack_from('<4sHHI', nb, 0)
                    if magic != b'NVXN' or block_crc(nb) != struct.unpack_from('<I', nb, 0x7FC)[0]:
                        self.err('inode %d: extent node %d bad magic or CRC' % (ino, p))
                        continue
                    if nd != d - 1 or nino != ino or not 1 <= ne <= EXT_PER_NODE:
                        self.err('inode %d: extent node %d: depth %d entries %d owner %d' % (ino, p, nd, ne, nino))
                        continue
                    nodes.append(p)
                    sub = [struct.unpack_from('<IIHH', nb, 0x10 + 12 * i) for i in range(ne)]
                    if sub[0][0] != l:
                        self.err('inode %d: index entry %d does not match its node' % (ino, l))
                    walk(sub, d - 1, 'node %d' % p)
                    prev_end = l
        walk([struct.unpack_from('<IIHH', raw, 0x20 + 12 * i) for i in range(count)], depth, 'the inode')
        return out, nodes

    def read_file(self, ino):
        raw = self.inode(ino)
        mode, _, size = struct.unpack_from('<HHI', raw, 0)
        if mode >> 12 == 0xA and size <= 72:
            return raw[0x20:0x20 + size]
        lv, _ = self.leaves(ino, raw)
        nb = -(-size // BS)
        data = bytearray(nb * BS)
        for l, p, n, fl in lv:
            for i in range(n):
                if l + i < nb and not fl & 1:
                    data[(l + i) * BS:(l + i + 1) * BS] = self.v.read(p + i)
        return bytes(data[:size])

    def dir_entries(self, ino, check=False):
        raw = self.inode(ino)
        size = struct.unpack_from('<I', raw, 4)[0]
        lv, _ = self.leaves(ino, raw)
        phys = {}
        for l, p, n, fl in lv:
            for i in range(n):
                phys[l + i] = p + i
        out = []
        if check and size % BS:
            self.err('directory %d: size %d is not a whole number of blocks' % (ino, size))
        for lb in range(size // BS):
            if lb not in phys:
                self.err('directory %d: hole at block %d' % (ino, lb))
                continue
            b = self.v.read(phys[lb])
            magic, owner, used = struct.unpack_from('<4sIH', b, 0)
            if magic != b'NVDB' or block_crc(b) != struct.unpack_from('<I', b, 0x7FC)[0]:
                self.err('directory %d: block %d bad magic or CRC' % (ino, phys[lb]))
                continue
            if owner != ino:
                self.err('directory %d: block %d names owner %d' % (ino, phys[lb], owner))
            n = 0
            for j in range(SLOTS):
                slot = b[0x40 + 64 * j:0x80 + 64 * j]
                eino, h, ln, dt = struct.unpack_from('<IHBB', slot, 0)
                if not eino:
                    if check and any(slot):
                        self.err('directory %d: free slot not zeroed' % ino)
                    continue
                n += 1
                name = slot[8:8 + ln]
                if check:
                    if not 1 <= ln <= 56 or b'/' in name or b'\0' in name or any(slot[8 + ln:]):
                        self.err('directory %d: bad name %r' % (ino, name))
                    if h != crc32(name) & 0xFFFF:
                        self.err('directory %d: bad hash for %r' % (ino, name))
                    if name in (b'.', b'..'):
                        self.err('directory %d: stores %r' % (ino, name))
                out.append((name, eino, dt))
            if check and n != used:
                self.err('directory %d: db_used %d but %d slots occupied' % (ino, used, n))
        if check and len(set(e[0] for e in out)) != len(out):
            self.err('directory %d: duplicate names' % ino)
        return out

    def run(self, replay=False, allow_dirty=False):
        v = self.v
        raw = v.load_sb()
        s = v.sb
        if s['magic'] != b'NVFS' or block_crc(raw) != struct.unpack_from('<I', raw, 0x7FC)[0]:
            self.err('superblock: bad magic or CRC')
            return self.report()
        if s['vmaj'] != 3 or s['block_size'] != BS:
            self.err('superblock: version %d, block size %d' % (s['vmaj'], s['block_size']))
            return self.report()
        if s['f_incompat']:
            self.err('superblock: unknown incompat bits %#x' % s['f_incompat'])
        if s['total_blocks'] > v.total or s['backup_sb'] != s['total_blocks'] - 1 or s['root_inode'] != 1:
            self.err('superblock: bad geometry')
            return self.report()
        order = [8, s['journal_start'], s['journal_start'] + s['journal_blocks'], s['bitmap_start'],
                 s['bitmap_start'] + s['bitmap_blocks'], s['summary_start'], s['summary_start'] + s['summary_blocks'],
                 s['ibitmap_start'], s['ibitmap_start'] + s['ibitmap_blocks'], s['itable_start'],
                 s['itable_start'] + s['itable_blocks'], s['first_data'], s['total_blocks'] - 1]
        if order != sorted(order):
            self.err('superblock: regions out of order or overlapping')
            return self.report()
        braw = v.read(s['backup_sb'])
        bsb = dict(zip(SB_FIELDS, struct.unpack_from(SB_FMT, braw)))
        if braw[:4] != b'NVFS' or block_crc(braw) != struct.unpack_from('<I', braw, 0x7FC)[0]:
            self.err('backup superblock: bad magic or CRC')
        elif any(bsb[k] != s[k] for k in IMMUTABLE):
            self.err('backup superblock disagrees on the immutable layout')

        n = self.replay(write=replay)
        if n < 0:
            return self.report()
        if n and not replay:
            self.err('journal not empty: %d committed transaction(s) await recovery' % n)
            return self.report()
        if n:
            self.notes.append('replayed %d transaction(s)' % n)
            s = v.sb
        if s['state'] == 1:
            (self.notes.append if allow_dirty else self.err)('volume was not cleanly unmounted (s_state = 1)')
        elif s['state'] == 2:
            self.err('volume is in the error state')

        # bitmaps as stored
        bits = b''.join(v.read(s['bitmap_start'] + i) for i in range(s['bitmap_blocks']))
        ibits = b''.join(v.read(s['ibitmap_start'] + i) for i in range(s['ibitmap_blocks']))
        bit = lambda arr, n: arr[n >> 3] >> (n & 7) & 1
        ref = {}                                   # block -> owner inode

        def claim(b, ino):
            if b in ref:
                self.err('block %d is cross-linked between inodes %d and %d' % (b, ref[b], ino))
            ref[b] = ino

        # inodes
        modes, links, alloc = {}, {}, set()
        for ino in range(1, s['total_inodes'] + 1):
            raw = self.inode(ino)
            mode, nlink, size, blocks = struct.unpack_from('<HHII', raw, 0)
            allocated = bit(ibits, ino - 1)
            if struct.unpack_from('<I', raw, 0x7C)[0] != crc32(raw[:0x7C]):
                self.err('inode %d: bad CRC' % ino)
                continue
            if not allocated:
                if mode or nlink:
                    self.err('inode %d: free but mode %#x links %d' % (ino, mode, nlink))
                continue
            if 2 <= ino <= 7:
                if mode or nlink:
                    self.err('reserved inode %d is in use' % ino)
                continue
            alloc.add(ino)
            t = mode >> 12
            if t not in (1, 2, 4, 6, 8, 10) or mode & 0x0E00 or struct.unpack_from('<H', raw, 0x1C)[0] & ~7:
                self.err('inode %d: invalid mode %#06x or flags' % (ino, mode))
                continue
            modes[ino], links[ino] = t, nlink
            if t in (4, 8) or (t == 10 and size > 72):
                lv, nodes = self.leaves(ino, raw)
                charged = sum(n for _, _, n, _ in lv) + len(nodes)
                if charged != blocks:
                    self.err('inode %d: i_blocks %d but %d charged' % (ino, blocks, charged))
                for l, p, n, fl in lv:
                    for i in range(n):
                        claim(p + i, ino)
                for p in nodes:
                    claim(p, ino)
            elif blocks or raw[0x1E] or raw[0x1F]:
                self.err('inode %d: type %d carries blocks' % (ino, t))
        for n in range(s['total_inodes'], s['ibitmap_blocks'] * 16384):
            if not bit(ibits, n):
                self.err('inode bitmap: padding bit %d clear' % n)
                break

        # directory tree from the root
        if modes.get(1) != 4:
            self.err('root inode is not a directory')
            return self.report()
        seen, counted, stack = {1: 0}, {1: 2}, [1]
        while stack:
            d = stack.pop()
            for name, eino, dt in self.dir_entries(d, check=True):
                if eino not in alloc:
                    self.err('directory %d: %r points at free inode %d' % (d, name, eino))
                    continue
                if dt != {1: 1, 2: 2, 4: 4, 6: 6, 8: 8, 10: 10}.get(modes.get(eino)):
                    self.err('directory %d: %r has type %d, inode %d says %s' % (d, name, dt, eino, modes.get(eino)))
                if modes.get(eino) == 4:
                    if eino in seen:
                        self.err('directory %d has two parents (%d and %d)' % (eino, seen[eino], d))
                        continue
                    seen[eino] = d
                    counted[eino] = 2
                    stack.append(eino)
                else:
                    counted[eino] = counted.get(eino, 0) + 1
        # orphan list
        orphans, o, steps = set(), s['orphan_head'], 0
        while o:
            steps += 1
            if not 8 <= o <= s['total_inodes'] or steps > s['total_inodes'] or o in orphans:
                self.err('orphan list: bad link at inode %d' % o)
                break
            orphans.add(o)
            raw = self.inode(o)
            if struct.unpack_from('<H', raw, 2)[0]:
                self.err('orphan inode %d has links' % o)
            o = struct.unpack_from('<I', raw, 0x70)[0]
        if orphans:
            self.notes.append('%d orphan inode(s) await the next read-write mount' % len(orphans))
        for ino in sorted(alloc):
            if ino in orphans:
                continue
            if ino not in counted:
                self.err('inode %d is allocated but unreachable' % ino)
            elif counted[ino] != links[ino]:
                self.err('inode %d: i_links %d but %d reference(s)' % (ino, links[ino], counted[ino]))

        # block bitmap against what the inodes reference
        meta = set(range(s['first_data'])) | {s['total_blocks'] - 1}
        free = 0
        for b in range(s['total_blocks']):
            a = bit(bits, b)
            if not a:
                free += 1
            if b in meta:
                if not a:
                    self.err('block %d: metadata marked free' % b)
            elif b in ref and not a:
                self.err('block %d: in use by inode %d but marked free' % (b, ref[b]))
            elif a and b not in ref:
                self.err('block %d: allocated but referenced by nothing (leak)' % b)
        for b in range(s['total_blocks'], s['bitmap_blocks'] * 16384):
            if not bit(bits, b):
                self.err('block bitmap: padding bit %d clear' % b)
                break
        for i in range(s['bitmap_blocks']):
            chunk = bits[i * BS:(i + 1) * BS]
            fr = 16384 - sum(bin(x).count('1') for x in chunk)
            sm = v.read(s['summary_start'] + (i >> 10))
            got = struct.unpack_from('<H', sm, (i & 1023) << 1)[0]
            if got != (0xFFFF if fr == 16384 else fr):
                self.err('summary entry %d is %d, bitmap says %d' % (i, got, fr))
        if free != s['free_blocks']:
            self.err('s_free_blocks %d, bitmap says %d' % (s['free_blocks'], free))
        fi = s['total_inodes'] - 6 - len(alloc)      # 2..7 reserved; the root is in alloc
        if fi != s['free_inodes']:
            self.err('s_free_inodes %d, bitmap says %d' % (s['free_inodes'], fi))
        return self.report()

    def report(self):
        for n in self.notes:
            print('nvfsck: note: ' + n, file=self.out)
        for e in self.errors[:40]:
            print('nvfsck: ' + e, file=self.out)
        if len(self.errors) > 40:
            print('nvfsck: ... and %d more' % (len(self.errors) - 40), file=self.out)
        if not self.errors:
            print('nvfsck: clean', file=self.out)
        return not self.errors

    # -------- browsing
    def lookup(self, path):
        ino = 1
        for part in [p for p in path.encode('latin-1').split(b'/') if p]:
            for name, eino, dt in self.dir_entries(ino):
                if name == part:
                    ino = eino
                    break
            else:
                sys.exit('not found: ' + path)
        return ino

    def tree(self, ino=1, prefix=''):
        for name, eino, dt in sorted(self.dir_entries(ino)):
            n = name.decode('latin-1')
            raw = self.inode(eino)
            size = struct.unpack_from('<I', raw, 4)[0]
            if dt == 4:
                print('%s%s/' % (prefix, n), file=self.out)
                self.tree(eino, prefix + n + '/')
            elif dt == 10:
                print('%s%s -> %s' % (prefix, n, self.read_file(eino).decode('latin-1')), file=self.out)
            else:
                print('%s%s %d %08x' % (prefix, n, size, crc32(self.read_file(eino))), file=self.out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('image')
    p.add_argument('-o', required=True)
    p.add_argument('--entry', required=True)
    p.add_argument('--seg', action='append', required=True, help='ADDR:FILE[:MEMSZ]; FILE may be empty')
    p = sub.add_parser('card')
    p.add_argument('-o', required=True)
    p.add_argument('--kernel')
    p.add_argument('--root', default='')
    p.add_argument('--size-mb', type=int, default=16)
    p.add_argument('--boot-kb', type=int, default=256)
    p.add_argument('--frag', action='append', default=[], help='NAME:N lay file NAME down in N-block fragments')
    p = sub.add_parser('fsck')
    p.add_argument('img')
    p.add_argument('--replay', action='store_true')
    p.add_argument('--allow-dirty', action='store_true')
    for c in ('ls', 'cat', 'tree'):
        p = sub.add_parser(c)
        p.add_argument('img')
        if c != 'tree':
            p.add_argument('path', nargs='?', default='/')
    a = ap.parse_args()

    if a.cmd == 'image':
        segs = []
        for spec in a.seg:
            parts = spec.split(':')
            data = open(parts[1], 'rb').read() if parts[1] else b''
            segs.append((int(parts[0], 0), data, int(parts[2], 0) if len(parts) > 2 else 0))
        open(a.o, 'wb').write(make_image(int(a.entry, 0), segs))
    elif a.cmd == 'card':
        frag = {k: int(v) for k, v in (x.split(':') for x in a.frag)}
        make_card(a.o, a.kernel, a.root, a.size_mb, a.boot_kb, frag)
    elif a.cmd == 'fsck':
        vol = open_volume(a.img, 'r+b' if a.replay else 'rb')
        sys.exit(0 if Fsck(vol).run(a.replay, a.allow_dirty) else 1)
    else:
        vol = open_volume(a.img)
        vol.load_sb()
        fs = Fsck(vol)
        if a.cmd == 'tree':
            fs.tree()
        elif a.cmd == 'ls':
            for name, eino, dt in sorted(fs.dir_entries(fs.lookup(a.path))):
                print(name.decode('latin-1') + ('/' if dt == 4 else ''))
        else:
            sys.stdout.buffer.write(fs.read_file(fs.lookup(a.path)))


if __name__ == '__main__':
    main()
