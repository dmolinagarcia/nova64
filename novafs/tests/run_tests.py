#!/usr/bin/env python3
"""
run_tests.py — image-based tests for the read-only FAT32 library.

Builds a source tree, writes it into several FAT32 images with the
reference tools, and checks fat32tool against three oracles:

  - the source tree itself: names, types, sizes, CRC-32 and timestamps
    of every file, and directory order as mtools reports it;
  - fsck.fat -v: geometry and cluster usage;
  - the library's own cross-checks (fat32tool check): lookup by long,
    short and case-folded name, inode numbers, parents, backward reads.

Then it damages the small image in named ways, and at random, and
requires a clean error or a clean pass: never a crash, a hang or a
sanitizer report (FUS-03.o, Y4.12).

Needs python3, dosfstools (mkfs.vfat, fsck.fat) and mtools.
"""
import argparse
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ENV = dict(os.environ,
           LC_ALL="C.UTF-8", TZ="UTC", MTOOLS_SKIP_CHECK="1",
           ASAN_OPTIONS="exitcode=99:detect_leaks=1",
           UBSAN_OPTIONS="halt_on_error=1:exitcode=98:print_stacktrace=1")

TOOL = None
TIMEOUT = 60
BASE_MTIME = 1709214358          # 2024-02-29 13:45:58 UTC, an even second

passed = 0
failed = 0


def ok(cond, what):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print("FAIL:", what, flush=True)
    return cond


def sh(*cmd, check=True):
    r = subprocess.run(cmd, env=ENV, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=600)
    if check and r.returncode != 0:
        sys.exit("command failed: %s\n%s" % (" ".join(cmd),
                                             r.stderr.decode(errors="replace")))
    return r


class Result:
    def __init__(self, rc, out, err):
        self.rc, self.out, self.err = rc, out, err

    def text(self):
        return self.out.decode("utf-8", errors="replace")


def tool(img, *args, sector=None, offset=None):
    cmd = [TOOL]
    if sector:
        cmd += ["-s", str(sector)]
    if offset is not None:
        cmd += ["-o", str(offset)]
    cmd += [img] + [str(a) for a in args]
    try:
        r = subprocess.run(cmd, env=ENV, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=TIMEOUT)
        return Result(r.returncode, r.stdout, r.stderr.decode(errors="replace"))
    except subprocess.TimeoutExpired:
        return Result(124, b"", "timeout after %ds" % TIMEOUT)


def clean_exit(r, what):
    """0 or 1, with nothing from a sanitizer: anything else is a crash,
    a hang or a memory error, whatever the image looked like."""
    good = r.rc in (0, 1) and "Sanitizer" not in r.err and "runtime error" not in r.err
    if not good:
        print(r.err[-2000:])
    return ok(good, "%s: clean exit (rc=%d)" % (what, r.rc))


# ---- The source tree -------------------------------------------------------

def build_source(root, rng):
    """Writes the tree and returns {path: bytes or None for a directory},
    with paths as the FAT volume will show them."""
    expect = {}
    stamp = [BASE_MTIME]

    def d(rel):
        os.makedirs(os.path.join(root, rel), exist_ok=True)
        expect["/" + rel] = None

    def f(rel, data):
        p = os.path.join(root, rel)
        with open(p, "wb") as fh:
            fh.write(data)
        stamp[0] += 2 * rng.randint(1, 100000)
        os.utime(p, (stamp[0], stamp[0]))
        expect["/" + rel] = (data, stamp[0])

    def rnd(n):
        return rng.getrandbits(8 * n).to_bytes(n, "little") if n else b""

    f("hello.txt", b"Hello, noVa64!\n")
    f("empty.bin", b"")
    d("sizes")
    for n in (1, 511, 512, 513, 4095, 4096, 4097, 8191, 8192, 8193,
              12289, 65536, 65537):
        f("sizes/s%d.bin" % n, rnd(n))
    d("big")
    f("big/random.bin", rnd(3 * 1024 * 1024 + 17))
    f("Long File Name With Spaces.txt", b"spaces\n")
    f("mañana ñandú.txt", b"n-tilde\n")
    f("Ελληνικά.txt", b"greek\n")
    f("日本語のファイル名.txt", b"japanese\n")
    f("ÑAND.TXT", b"oem short name\n")
    f("emoji_QZQZ.txt", b"surrogate pair, patched in afterwards\n")
    f("surr_XYXY.txt", b"unpaired surrogate, patched in afterwards\n")
    f("lower.txt", b"lowercase 8.3\n")
    f("abc.TXT", b"lowercase base only\n")
    f("MiXeD.TxT", b"mixed case\n")
    f("UPPER.TXT", b"upper\n")
    f("readme", b"no extension\n")
    f("a.b.c.txt", b"dots\n")
    f("hidden.txt", b"hidden\n")
    f("readonly.txt", b"read-only\n")
    f("L" * 251 + ".txt", b"the longest legal name\n")
    d("deep")
    rel = "deep"
    for i in range(1, 21):
        rel += "/d%02d" % i
        d(rel)
    f(rel + "/leaf.txt", b"twenty levels down\n")
    d("many")
    for i in range(300):
        f("many/entry_%04d_with_a_long_name.dat" % i, rnd(i))
    return expect


def build_image(path, size_mb, mkfs_args, src, expect, rng, offset_mb=0):
    """mkfs, copy the tree in, then fragment a file and leave deleted
    entries behind, then patch in the names mtools cannot write."""
    if not offset_mb:
        sh("truncate", "-s", "%dM" % size_mb, path)
        sh("mkfs.vfat", "-F", "32", "-n", "NOVA64", *mkfs_args, path)
        target = path
    else:
        make_partitioned(path, size_mb, offset_mb, mkfs_args)
        target = "%s@@%dM" % (path, offset_mb)

    names = sorted(os.listdir(src))
    sh("mcopy", "-s", "-m", "-i", target, *[os.path.join(src, n) for n in names], "::/")
    sh("mattrib", "-i", target, "+h", "::/hidden.txt")
    sh("mattrib", "-i", target, "+r", "::/readonly.txt")

    # Fill holes, punch every other one, then write a file big enough
    # that its chain has to run through the holes.
    frag = tempfile.mkdtemp(dir=os.path.dirname(path))
    sh("mmd", "-i", target, "::/frag")
    for i in range(40):
        with open(os.path.join(frag, "f%02d.bin" % i), "wb") as fh:
            fh.write(bytes([i]) * 700)
    sh("mcopy", "-i", target, *[os.path.join(frag, "f%02d.bin" % i) for i in range(40)],
       "::/frag/")
    sh("mdel", "-i", target, *["::/frag/f%02d.bin" % i for i in range(0, 40, 2)])
    blob = rng.getrandbits(8 * 200000).to_bytes(200000, "little")
    with open(os.path.join(frag, "frag.bin"), "wb") as fh:
        fh.write(blob)
    sh("mcopy", "-i", target, os.path.join(frag, "frag.bin"), "::/frag/")
    shutil.rmtree(frag)

    exp = dict(expect)
    exp["/frag"] = None
    for i in range(1, 40, 2):
        exp["/frag/f%02d.bin" % i] = (bytes([i]) * 700, None)
    exp["/frag/frag.bin"] = (blob, None)

    # Non-BMP characters: mtools drops them, so they are written into the
    # LFN entries directly. The checksum covers the short name only. Each
    # marker sits inside one 6-unit field of one LFN entry.
    with open(path, "r+b") as fh:
        img = bytearray(fh.read())
        for old, units, name in (
                ("emoji_QZQZ.txt", "\U0001F680QZ", "emoji_\U0001F680QZ.txt"),
                ("surr_XYXY.txt", "\uD800YXY", "surr_\uFFFDYXY.txt")):
            marker = old[old.index("_") + 1:][:4].encode("utf-16-le")
            hits = [m.start() for m in re.finditer(re.escape(marker), img)]
            assert len(hits) == 1, "marker of %s found %d times" % (old, len(hits))
            img[hits[0]:hits[0] + 8] = units.encode("utf-16-le", "surrogatepass")
            exp["/" + name] = exp.pop("/" + old)
        fh.seek(0)
        fh.write(img)
    return exp


def make_partitioned(path, size_mb, offset_mb, mkfs_args):
    """An MBR with a non-FAT partition first, then the FAT32 one, so that
    probing has to skip one."""
    sh("truncate", "-s", "%dM" % size_mb, path)
    start = offset_mb * 2048
    count = size_mb * 2048 - start
    mbr = bytearray(512)
    mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\0\0\0", 0x83, b"\0\0\0", 64, start - 64)
    mbr[462:478] = struct.pack("<B3sB3sII", 0x80, b"\0\0\0", 0x0C, b"\0\0\0", start, count)
    mbr[510:512] = b"\x55\xaa"
    with open(path, "r+b") as fh:
        fh.write(mbr)
    sh("mkfs.vfat", "-F", "32", "-n", "NOVA64", "--offset", str(start), *mkfs_args,
       path, str(count // 2))


# ---- Checks against the oracles -----------------------------------------------

def parse_tree(text):
    got = {}
    for line in text.splitlines():
        if line.startswith("D "):
            _, _, _, path = line.split(" ", 3)
            got[path] = None
        elif line.startswith("F "):
            _, size, crc, day, hms, path = line.split(" ", 5)
            got[path] = (int(size), int(crc, 16), day + " " + hms)
    return got


def check_tree(img, exp, label, **kw):
    r = tool(img, "tree", **kw)
    if not ok(r.rc == 0, "%s: tree exits 0 (%s)" % (label, r.err.strip())):
        return
    got = parse_tree(r.text())
    missing = sorted(set(exp) - set(got))
    extra = sorted(set(got) - set(exp))
    ok(not missing and not extra,
       "%s: same set of paths (missing %s, extra %s)" % (label, missing[:5], extra[:5]))
    bad = []
    for path in sorted(set(exp) & set(got)):
        e, g = exp[path], got[path]
        if e is None or g is None:
            if (e is None) != (g is None):
                bad.append(path + " (type)")
            continue
        data, mtime = e
        if g[0] != len(data) or g[1] != zlib.crc32(data):
            bad.append(path + " (content)")
        elif mtime is not None and g[2] != time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(mtime)):
            bad.append(path + " (mtime %s)" % g[2])
    ok(not bad, "%s: every file matches the source (%s)" % (label, bad[:5]))


def check_geometry(img, label, fsck_args=(), **kw):
    r = tool(img, "info", **kw)
    if not ok(r.rc == 0, "%s: info exits 0 (%s)" % (label, r.err.strip())):
        return
    info = dict(line.split(None, 1) for line in r.text().splitlines())
    ref = sh("fsck.fat", "-n", "-v", *fsck_args, img, check=False).stdout.decode()

    def num(pattern):
        m = re.search(pattern, ref)
        return int(m.group(1)) if m else None

    bps = int(info["bytes_per_sector"])
    pairs = [
        ("bytes_per_sector", num(r"(\d+) bytes per logical sector")),
        ("cluster_size", num(r"(\d+) bytes per cluster")),
        ("reserved_sectors", num(r"(\d+) reserved sectors")),
        ("num_fats", num(r"(\d+) FATs, 32 bit entries")),
        ("fat_size", num(r"bytes per FAT \(= (\d+) sectors\)")),
        ("data_start", num(r"Data area starts at byte \d+ \(sector (\d+)\)")),
        ("cluster_count", num(r"(\d+) data clusters")),
        ("total_sectors", num(r"(\d+) sectors total")),
        ("root_cluster", num(r"Root directory start at cluster (\d+)")),
    ]
    for key, want in pairs:
        ok(want is not None and int(info[key]) == want,
           "%s: %s = %s, fsck.fat says %s" % (label, key, info[key].strip(), want))
    ok(int(info["fat_start"]) * bps == num(r"First FAT starts at byte (\d+)"),
       "%s: fat_start agrees with fsck.fat" % label)
    m = re.search(r"(\d+)/(\d+) clusters", ref)
    ok(m and int(info["free_clusters"]) == int(m.group(2)) - int(m.group(1)),
       "%s: free clusters %s, fsck.fat says %s" % (label, info["free_clusters"].strip(),
                                                  m and int(m.group(2)) - int(m.group(1))))
    ok(info["label"].strip() == "NOVA64", "%s: volume label" % label)


# dir: attributes, size, date, time, short name, then the name to the end
DIR_LINE = re.compile(r"^\S{5} +\d+ \S+ \S+  \S+ +(.*)$")
# ls: mode, links, uid, gid, size, date, time, then the name to the end
LS_LINE = re.compile(r"^\S{10} +\d+ \d+ \d+ +\d+ \S+ \S+  (.*)$")


def check_order(img, label, dirs, target, **kw):
    """Directory order: L2 against mdir, and the table against L2 with
    '.' and '..' in front."""
    for d in dirs:
        r = tool(img, "dir", d, **kw)
        ours = [DIR_LINE.match(line).group(1) for line in r.text().splitlines()]
        r = tool(img, "ls", d, **kw)
        table = [LS_LINE.match(line).group(1) for line in r.text().splitlines()]
        ok(table == [".", ".."] + ours,
           "%s: the table lists %s as L2 does, after '.' and '..'" % (label, d))
        ref = sh("mdir", "-a", "-b", "-i", target, "::" + d, check=False).stdout.decode()
        theirs = [p.rstrip("/").rsplit("/", 1)[-1] for p in ref.splitlines()
                  if p.startswith("::")]
        # mtools cannot show non-BMP characters or unpaired surrogates.
        pairs = [(a, b) for a, b in zip(ours, theirs)
                 if "\U0001F680" not in a and "\uFFFD" not in a]
        ok(len(ours) == len(theirs) and all(a == b for a, b in pairs),
           "%s: order of %s matches mdir" % (label, d))


def check_self(img, label, **kw):
    r = tool(img, "check", **kw)
    ok(r.rc == 0 and "OK" in r.text(), "%s: self-consistency (%s)" % (label, r.err.strip()[-500:]))


def check_partial_reads(img, exp, label, rng, cluster, **kw):
    for path in ("/big/random.bin", "/sizes/s4097.bin", "/frag/frag.bin",
                 "/many/entry_0299_with_a_long_name.dat"):
        data = exp[path][0]
        n = len(data)
        cases = [(0, 0), (0, 1), (n - 1, 10), (n, 5), (n + 100, 5), (0, n),
                 (cluster - 1, 2), (cluster, cluster), (511, 2), (1, n - 2)]
        cases += [(rng.randrange(n), rng.randrange(1, 70000)) for _ in range(25)]
        bad = [(o, l) for o, l in cases
               if tool(img, "cat", path, o, l, **kw).out != data[o:o + l]]
        ok(not bad, "%s: partial reads of %s (%s)" % (label, path, bad[:3]))


def check_errors(img, label):
    def err(path, cmd="stat"):
        r = tool(img, cmd, path)
        return r.rc, r.err

    def ino(path):
        r = tool(img, "stat", path)
        m = re.search(r"ino\s+(\d+)", r.text())
        return int(m.group(1)) if r.rc == 0 and m else None

    for path, cmd, text in (
            ("/hello.txt/x", "stat", "not a directory"),
            ("/hello.txt/", "stat", "not a directory"),
            ("/nope", "stat", "no such file"),
            ("/deep/nope/x", "stat", "no such file"),
            ("/sizes", "cat", "is a directory"),
            ("/" + "x" * 800, "stat", "name too long")):
        rc, e = err(path, cmd)
        ok(rc == 1 and text in e, "%s: %s %s reports '%s' (%s)" % (label, cmd, path[:20], text, e.strip()))

    hello = ino("/hello.txt")
    ok(hello is not None and ino("/deep/d01/d02/../../../hello.txt") == hello,
       "%s: '..' walks back to the same inode" % label)
    ok(ino("/HELLO.TXT") == hello, "%s: lookup folds case" % label)
    ok(ino("//deep///d01/") == ino("/deep/d01"), "%s: repeated and trailing slashes" % label)
    ok(ino("/deep/d01/..") == ino("/deep"), "%s: '..' of a subdirectory" % label)
    ok(ino("/deep/..") == 1 and ino("/..") == 1 and ino("/") == 1, "%s: root is inode 1" % label)
    ok(ino("/LONGFI~1.TXT") == ino("/Long File Name With Spaces.txt"), "%s: short alias" % label)
    ok("--h-a" in tool(img, "stat", "/hidden.txt").text(), "%s: hidden attribute" % label)
    ok("-r--a" in tool(img, "stat", "/readonly.txt").text(), "%s: read-only attribute" % label)
    ok("ÑAND.TXT" in tool(img, "dir", "/").text(), "%s: code page 437 short name" % label)


def check_attrs(img, exp, label, cluster):
    """Attribute synthesis under the rules of fat32_attr.c (FUS-03.i)."""
    def st(path):
        return stat(img, path)

    def blocks(size):
        return -(-size // cluster) * cluster // 512

    hello = st("/hello.txt")
    ok(hello["mode"].startswith("0100644") and hello["nlink"] == "1" and
       hello["uid"] == "0" and hello["gid"] == "0",
       "%s: a file is 0644, one link, owned by 0:0 (%s)" % (label, hello))
    ok(st("/readonly.txt")["mode"].startswith("0100444"),
       "%s: the read-only attribute clears the write bits" % label)
    big = st("/big/random.bin")
    ok(int(big["blocks"]) == blocks(len(exp["/big/random.bin"][0])),
       "%s: blocks round up to whole clusters" % label)
    ok(big["mtime"] == time.strftime("%Y-%m-%d %H:%M:%S",
                                     time.gmtime(exp["/big/random.bin"][1])),
       "%s: mtime in seconds, no zone applied" % label)
    ok(st("/empty.bin")["size"] == "0" and st("/empty.bin")["blocks"] == "0",
       "%s: an empty file" % label)

    root = st("/")
    top = sum(1 for p, v in exp.items() if v is None and p.count("/") == 1)
    ok(root["mode"].startswith("040755") and root["nlink"] == str(2 + top),
       "%s: the root is 040755 with 2 + %d links (%s)" % (label, top, root["nlink"]))
    ok(root["mtime"] == root["ctime"] == root["atime"] == "1970-01-01 00:00:00",
       "%s: the root has no entry, so no times" % label)
    ok(st("/deep")["nlink"] == "3" and st("/many")["nlink"] == "2",
       "%s: a directory counts its subdirectories" % label)
    im = Image(img)
    many = st("/many")
    ok(int(many["size"]) == len(im.chain(int(many["cluster"]))) * cluster and
       int(many["blocks"]) == int(many["size"]) // 512,
       "%s: a directory's size is its cluster chain" % label)


# ---- Damage -------------------------------------------------------------------------

class Image:
    """Just enough FAT32 to aim damage at, working on a copy that is
    restored after every case."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            self.orig = fh.read(64 * 1024 * 1024)
        b = self.orig
        self.bps, self.spc = struct.unpack_from("<HB", b, 11)
        self.rsvd, self.nfats = struct.unpack_from("<HB", b, 14)
        self.fatsz, = struct.unpack_from("<I", b, 36)
        self.fsinfo, = struct.unpack_from("<H", b, 48)
        self.csize = self.bps * self.spc
        self.data = (self.rsvd + self.nfats * self.fatsz) * self.bps
        self.patches = []

    def fat(self, cl):
        return struct.unpack_from("<I", self.orig, self.rsvd * self.bps + 4 * cl)[0] & 0x0FFFFFFF

    def chain(self, cl):
        out = [cl]
        while self.fat(out[-1]) < 0x0FFFFFF8:
            out.append(self.fat(out[-1]))
        return out

    def entry_off(self, ino):
        per = self.csize // 32
        return self.data + ((ino - 2) // per) * self.csize + ((ino - 2) % per) * 32

    def cluster_off(self, cl):
        return self.data + (cl - 2) * self.csize

    def poke(self, off, data):
        self.patches.append((off, data))

    def set_fat(self, cl, value):
        for i in range(self.nfats):
            self.poke((self.rsvd + i * self.fatsz) * self.bps + 4 * cl, struct.pack("<I", value))

    def apply(self):
        with open(self.path, "r+b") as fh:
            for off, data in self.patches:
                fh.seek(off)
                fh.write(data)

    def restore(self):
        with open(self.path, "r+b") as fh:
            for off, data in self.patches:
                fh.seek(off)
                fh.write(self.orig[off:off + len(data)])
        self.patches = []


def stat(img, path):
    r = tool(img, "stat", path)
    assert r.rc == 0, (path, r.err)
    return {k: v.strip() for k, v in (l.split(None, 1) for l in r.text().splitlines())}


def corruption_suite(img, label, fuzz_rounds, rng):
    im = Image(img)
    hello = stat(img, "/hello.txt")
    big = stat(img, "/big/random.bin")
    many = stat(img, "/many")
    lfn = stat(img, "/Long File Name With Spaces.txt")
    d01 = stat(img, "/deep/d01")
    d02 = stat(img, "/deep/d01/d02")
    deep = stat(img, "/deep")
    big_chain = im.chain(int(big["cluster"]))
    many_chain = im.chain(int(many["cluster"]))
    mid = big_chain[len(big_chain) // 2]
    baseline_free = tool(img, "info").text()
    baseline_free = re.search(r"free_clusters\s+(\d+)", baseline_free).group(1)

    cases = [
        # name, damage, command, expected rc, expected text (stderr or stdout)
        ("bytes per sector 0", lambda: im.poke(11, b"\0\0"), ["info"], 1, "corrupt"),
        ("sectors per cluster 3", lambda: im.poke(13, b"\x03"), ["info"], 1, "corrupt"),
        ("no FATs", lambda: im.poke(16, b"\0"), ["info"], 1, "corrupt"),
        ("root cluster out of range", lambda: im.poke(44, struct.pack("<I", 0x0FFFFFF0)),
         ["info"], 1, "BPB_RootClus"),
        ("newer BPB version", lambda: im.poke(42, b"\x00\x01"), ["info"], 1, "not supported"),
        ("no boot signature", lambda: im.poke(510, b"\0"), ["info"], 1, "not supported"),
        ("FSInfo signature", lambda: im.poke(im.fsinfo * im.bps, b"\0"), ["info"], 0,
         "free_clusters       " + baseline_free),
        ("FSInfo absurd free count",
         lambda: im.poke(im.fsinfo * im.bps + 488, struct.pack("<I", 0xFFFFFFF0)),
         ["info"], 0, "fsinfo_free         unknown"),
        ("directory chain points at itself",
         lambda: im.set_fat(many_chain[0], many_chain[0]), ["tree", "/many"], 1, "loops"),
        ("directory chain loops back",
         lambda: im.set_fat(many_chain[-2], many_chain[1]), ["tree", "/many"], 1, "loops"),
        ("file chain loops back",
         lambda: im.set_fat(big_chain[100], big_chain[10]), ["cat", "/big/random.bin"], 1, "loops"),
        ("free cluster in a chain",
         lambda: im.set_fat(mid, 0), ["cat", "/big/random.bin"], 1, "free cluster"),
        ("successor out of range",
         lambda: im.set_fat(mid, 0x0FFFFF00), ["cat", "/big/random.bin"], 1, "out of range"),
        ("bad cluster in a chain",
         lambda: im.set_fat(mid, 0x0FFFFFF7), ["cat", "/big/random.bin"], 1, "bad cluster"),
        ("chain shorter than the file",
         lambda: im.set_fat(mid, 0x0FFFFFFF), ["cat", "/big/random.bin"], 1, "shorter"),
        ("first cluster out of range",
         lambda: im.poke(im.entry_off(int(hello["ino"])) + 20, b"\xff\x0f"),
         ["cat", "/hello.txt"], 1, "corrupt"),
        ("file larger than the volume",
         lambda: im.poke(im.entry_off(int(hello["ino"])) + 28, b"\xff\xff\xff\xff"),
         ["cat", "/hello.txt"], 1, "larger than the volume"),
        ("LFN checksum mismatch",
         lambda: im.poke(im.entry_off(int(lfn["ino"]) - 1) + 13, b"\x00"),
         ["dir", "/"], 0, "LONGFI~1.TXT  LONGFI~1.TXT"),
        ("LFN run without its short entry",
         lambda: im.poke(im.entry_off(int(lfn["ino"])), b"\xe5"), ["tree"], 0, None),
        ("end marker mid-directory",
         lambda: im.poke(im.cluster_off(many_chain[1]), b"\x00"), ["ls", "/many"], 0, None),
        ("subdirectory pointing at its ancestor",
         lambda: (im.poke(im.entry_off(int(d02["ino"])) + 20,
                          struct.pack("<H", int(deep["cluster"]) >> 16)),
                  im.poke(im.entry_off(int(d02["ino"])) + 26,
                          struct.pack("<H", int(deep["cluster"]) & 0xFFFF))),
         ["tree", "/deep"], 1, "too many levels"),
        ("missing '..' entry",
         lambda: im.poke(im.cluster_off(int(d01["cluster"])) + 32, b"XX"),
         ["stat", "/deep/d01/.."], 1, "without a '..'"),
    ]
    for name, damage, cmd, rc, text in cases:
        damage()
        im.apply()
        r = tool(img, *cmd)
        im.restore()
        clean_exit(r, "%s: %s" % (label, name))
        where = r.err if rc else r.text()
        ok(r.rc == rc and (text is None or text in where),
           "%s: %s -> rc %d%s (got rc %d: %s)" % (label, name, rc,
                                                 ", '%s'" % text if text else "",
                                                 r.rc, (r.err or r.text()).strip()[-200:]))
        if name == "LFN run without its short entry":
            ok("/Long File Name With Spaces.txt" not in r.text(), "%s: orphan run dropped" % label)
        if name == "end marker mid-directory":
            ok(0 < len(r.text().splitlines()) < 300, "%s: listing stops at the end marker" % label)

    # A volume longer than its device.
    trunc = img + ".trunc"
    with open(img, "rb") as src, open(trunc, "wb") as dst:
        dst.write(src.read(8 * 1024 * 1024))
    r = tool(trunc, "info")
    clean_exit(r, "%s: truncated image" % label)
    ok(r.rc == 1 and "beyond" in r.err, "%s: truncated image refused (%s)" % (label, r.err.strip()))
    os.unlink(trunc)

    # Random damage to the metadata: the boot sector, the start of the
    # FAT, and the root, /many and /deep directories.
    regions = [(0, 512), (im.rsvd * im.bps, 8192)]
    for cl in [2] + many_chain + im.chain(int(deep["cluster"])):
        regions.append((im.cluster_off(cl), im.csize))
    worst = []
    for i in range(fuzz_rounds):
        for _ in range(rng.randint(1, 8)):
            start, size = rng.choice(regions)
            im.poke(start + rng.randrange(size), bytes([rng.randrange(256)]))
        im.apply()
        for cmd in (["tree"], ["check"]):
            r = tool(img, *cmd)
            if not clean_exit(r, "%s: fuzz round %d, %s" % (label, i, cmd[0])):
                worst.append(i)
        im.restore()
    ok(not worst, "%s: %d fuzz rounds without a crash or hang" % (label, fuzz_rounds))
    with open(img, "rb") as fh:
        ok(fh.read(len(im.orig)) == im.orig, "%s: image restored after damage" % label)


# ---- Driver --------------------------------------------------------------------------

def main():
    global TOOL
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--tool", required=True, help="path to fat32tool")
    ap.add_argument("--fuzz", type=int, default=100, help="random damage rounds")
    ap.add_argument("--seed", type=int, default=64, help="random seed")
    ap.add_argument("--keep", action="store_true", help="keep the work directory")
    args = ap.parse_args()
    TOOL = os.path.abspath(args.tool)
    for prog in ("mkfs.vfat", "fsck.fat", "mcopy", "mdir"):
        if not shutil.which(prog):
            sys.exit("%s not found: install dosfstools and mtools" % prog)

    rng = random.Random(args.seed)
    work = tempfile.mkdtemp(prefix="novafs-")
    try:
        src = os.path.join(work, "src")
        os.mkdir(src)
        expect = build_source(src, rng)

        images = [
            # label, size MiB, mkfs arguments, sector sizes to open with, MBR offset
            ("4k-clusters", 300, ["-S", "512", "-s", "8"], [512], 0),
            ("512-clusters", 48, ["-S", "512", "-s", "1"], [512], 0),
            ("4k-sectors", 300, ["-S", "4096", "-s", "1"], [512, 4096], 0),
            ("partitioned", 64, ["-S", "512", "-s", "1"], [512], 2),
        ]
        built = {}
        for label, size, mkfs, sectors, offset in images:
            img = os.path.join(work, label + ".img")
            exp = build_image(img, size, mkfs, src, expect, rng, offset)
            built[label] = (img, exp)
            target = "%s@@%dM" % (img, offset) if offset else img
            fsck_args = ()
            for ss in sectors:
                tag = "%s/%d" % (label, ss)
                check_tree(img, exp, tag, sector=ss)
                check_self(img, tag, sector=ss)
                check_order(img, tag, ["/", "/many", "/frag"], target, sector=ss)
                if not offset:
                    check_geometry(img, tag, fsck_args, sector=ss)
            print("%-16s %d passed, %d failed" % (label, passed, failed), flush=True)

        img, exp = built["4k-clusters"]
        check_partial_reads(img, exp, "4k-clusters", rng, 4096)
        check_attrs(img, exp, "4k-clusters", 4096)
        img, exp = built["512-clusters"]
        check_partial_reads(img, exp, "512-clusters", rng, 512)
        check_attrs(img, exp, "512-clusters", 512)
        check_errors(img, "512-clusters")

        # An explicit offset instead of the probe.
        img, exp = built["partitioned"]
        check_tree(img, exp, "partitioned/-o", offset=4096)
        r = tool(img, "info", offset=0)
        ok(r.rc == 1, "partitioned: the MBR itself does not mount")

        # Mirroring off, FAT 1 active and FAT 0 destroyed: still readable.
        img, exp = built["512-clusters"]
        nomirror = os.path.join(work, "nomirror.img")
        shutil.copyfile(img, nomirror)
        im = Image(nomirror)
        im.poke(40, struct.pack("<H", 0x0081))
        im.poke(im.rsvd * im.bps, b"\xAA" * (im.fatsz * im.bps))
        im.apply()
        check_tree(nomirror, exp, "nomirror")
        r = tool(nomirror, "info")
        ok("active_fat          1" in r.text(), "nomirror: reads FAT 1")
        os.unlink(nomirror)
        print("%-16s %d passed, %d failed" % ("variants", passed, failed), flush=True)

        img, _ = built["512-clusters"]
        corruption_suite(img, "damage", args.fuzz, rng)
        print("%-16s %d passed, %d failed" % ("damage", passed, failed), flush=True)
    finally:
        if args.keep:
            print("work directory kept:", work)
        else:
            shutil.rmtree(work, ignore_errors=True)

    print("image tests: %d passed, %d failed" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
