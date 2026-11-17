#!/usr/bin/env python3
"""The console gates CON-00 to CON-03 (sheet CN1), run in the reference emulator.

    tests/run.py [--update] [--full] [name ...]

Golden screens (CN1.32): keystrokes are scripted through the emulated HID
queue, the text buffer is dumped and compared with a stored screen, and the
card image is built from a directory tree by the host tools. --update rewrites
the stored screens from what the run produced; read the diff before keeping it.

Every image the kernel has written is then checked by tools/nv.py fsck, an
independent implementation of the NVFS specification. The power-cut test stops
the card after N sector writes, for a sweep of N, and requires a consistent
volume both from the host's journal replay and from the kernel's own
(--full sweeps every N instead of a sample).
"""
import hashlib, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OS = os.path.dirname(HERE)
B = os.path.join(OS, 'build')
sys.path.insert(0, os.path.join(OS, 'tools'))
import nv  # noqa: E402

EMU = os.path.join(B, 'nova64')
BIOS = os.path.join(B, 'bios.bin')
KERNEL = os.path.join(B, 'kernel.nx')
GOLD = os.path.join(HERE, 'golden')
UPDATE = '--update' in sys.argv
FULL = '--full' in sys.argv
ONLY = [a for a in sys.argv[1:] if not a.startswith('--')]
TMP = tempfile.mkdtemp(prefix='nova64-test-')
failures = []


def emu(*args, keys=None, text=None):
    """Run the emulator; returns (exit code, stderr). Snapshots and the final screen land in TMP."""
    cmd = [EMU, '--bios', BIOS, '--snap-dir', TMP, '--screen', os.path.join(TMP, 'final.screen')]
    if text is not None:
        keys = os.path.join(TMP, 'inline.keys')
        with open(keys, 'w') as f:
            f.write(text)
    if keys:
        cmd += ['--keys', keys]
    r = subprocess.run(cmd + list(args), capture_output=True, text=True)
    return r.returncode, r.stderr


def golden(name, produced='final'):
    got = open(os.path.join(TMP, produced + '.screen')).read().rstrip('\n') + '\n'
    path = os.path.join(GOLD, name + '.screen')
    if UPDATE:
        with open(path, 'w') as f:
            f.write(got)
        return
    want = open(path).read() if os.path.exists(path) else ''
    if got != want:
        fail('%s: screen differs from %s' % (name, os.path.relpath(path, OS)))
        with open(os.path.join(B, name + '.got'), 'w') as f:
            f.write(got)


def golden_text(name, got):
    path = os.path.join(GOLD, name)
    if UPDATE:
        with open(path, 'w') as f:
            f.write(got)
    elif not os.path.exists(path) or open(path).read() != got:
        fail('%s differs' % name)
        with open(os.path.join(B, name + '.got'), 'w') as f:
            f.write(got)


def fail(msg):
    failures.append(msg)
    print('  FAIL ' + msg)


def card(name, size_mb=16, kernel=KERNEL):
    path = os.path.join(TMP, name)
    nv.make_card(path, kernel, os.path.join(HERE, 'tree'), size_mb, 256)
    return path


def fsck(path, replay=False):
    import io
    out = io.StringIO()
    vol = nv.open_volume(path, 'r+b' if replay else 'rb')
    ok = nv.Fsck(vol, out).run(replay=replay, allow_dirty=True)
    vol.f.close()
    return ok, out.getvalue()


def tree(path):
    import io
    out = io.StringIO()
    vol = nv.open_volume(path)
    vol.load_sb()
    nv.Fsck(vol, out).tree()
    vol.f.close()
    return out.getvalue()


def patch_sb(path, fn):
    """Edit the primary superblock in place and reseal it."""
    vol = nv.open_volume(path, 'r+b')
    vol.load_sb()
    fn(vol.sb)
    vol.store_sb(1)
    vol.f.close()


def digest(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


# ------------------------------------------------------------------- the gates
def con00():
    """The kernel speaks: a staged image prints its banner and the information block."""
    rc, err = emu('--stage', KERNEL, '--no-kbd')
    golden('con00')
    rc, err = emu('--stage', KERNEL, '--no-clock')
    if 'STP' not in err:
        fail('con00: panic did not halt the CPU: ' + err.strip())
    golden('con00_panic')
    rc, err = emu()                       # no kernel anywhere: the BIOS says so and halts
    if 'STP' not in err:
        fail('con00: the BIOS did not halt without a kernel')
    golden('con00_nokernel')


def con01():
    """An interactive console with no filesystem: line discipline, keymap, degraded built-ins."""
    emu('--stage', KERNEL, keys=os.path.join(HERE, 'con01.keys'))
    golden('con01a', 'con01a')
    golden('con01b', 'con01b')
    golden('con01c', 'con01c')
    long_line = 'echo ' + 'x' * 260 + '\necho after the long line\n'
    emu('--stage', KERNEL, text=long_line + '{SNAP con01_long}')
    golden('con01_long', 'con01_long')


def con02():
    """A read-only prompt: BIOS loads the kernel from the boot partition, NVFS read-only."""
    img = card('con02.img')
    before = digest(img)
    emu('--card-ro', img, keys=os.path.join(HERE, 'con02.keys'))
    golden('con02a', 'con02a')
    golden('con02', 'con02')
    if digest(img) != before:
        fail('con02: a read-only session changed the card')

    # a failed mount shows degraded mode
    bad = card('badsb.img')
    vol = nv.open_volume(bad, 'r+b')
    blk = bytearray(vol.read(1))
    blk[0x10] ^= 0xFF                     # break the superblock under its checksum
    vol.write(1, bytes(blk))
    vol.f.close()
    emu('--card', bad, text='ls\necho still here\n{SNAP con02_badsb}')
    golden('con02_badsb', 'con02_badsb')

    # the feature policy names the bit it did not recognise (Y2.20)
    inc = card('incompat.img')
    patch_sb(inc, lambda s: s.update(f_incompat=0x20))
    emu('--card', inc, text='ls\n{SNAP con02_incompat}')
    golden('con02_incompat', 'con02_incompat')
    ro = card('rocompat.img')
    patch_sb(ro, lambda s: s.update(f_ro_compat=3 | 0x100))
    before = digest(ro)
    emu('--card', ro, text='cat /etc/motd\necho x > /f\n{SNAP con02_rocompat}')
    golden('con02_rocompat', 'con02_rocompat')
    if digest(ro) != before:
        fail('con02: an unknown ro_compat bit did not keep the card untouched')

    # a read-only mounter refuses a volume that was not cleanly unmounted
    dirty = card('dirty.img')
    patch_sb(dirty, lambda s: s.update(state=1))
    emu('--card-ro', dirty, text='ls\n{SNAP con02_dirty}')
    golden('con02_dirty', 'con02_dirty')

    # no boot partition: the BIOS halts with a message
    nob = os.path.join(TMP, 'noboot.img')
    shutil.copy(img, nob)
    with open(nob, 'r+b') as f:
        f.seek(446 + 4)
        f.write(b'\x83')
    rc, err = emu('--card', nob)
    golden('con02_noboot')


def con03():
    """A read-write console."""
    img = card('con03.img')
    emu('--card', img, keys=os.path.join(HERE, 'con03.keys'))
    golden('con03a', 'con03a')
    golden('con03', 'con03')
    ok, out = fsck(img)
    if not ok:
        fail('con03: nvfsck: ' + out.strip().replace('\n', ' | '))
    golden_text('con03.tree', tree(img))
    emu('--card', img, text='ls -l /moved\ncat /docs/fox.txt\n{SNAP con03_reboot}')
    golden('con03_reboot', 'con03_reboot')
    ok, out = fsck(img)
    if not ok:
        fail('con03: nvfsck after the second boot: ' + out.strip().replace('\n', ' | '))


CRASH_SCRIPT = '''mkdir a a/b
echo one > a/f
cp /docs/lines.txt a/l
mv a/f a/b/g
echo two >> a/b/g
rm a/l
rmdir a/b
rm a/b/g
rmdir a/b a
'''


def crash(option='--power-cut', per=4, strict=True):
    """Power cuts: after any number of block writes the volume recovers to a consistent state."""
    base = card('crash.img', size_mb=9)
    img = os.path.join(TMP, 'cut.img')
    shutil.copy(base, img)
    rc, err = emu('--card', img, text=CRASH_SCRIPT)
    total = int(err.rsplit(',', 1)[1].split()[0]) // per
    ok, out = fsck(img)
    if not ok:
        fail('crash: the uncut run is not clean: ' + out.strip())
    if tree(img) != tree(base):
        fail('crash: the script should leave the tree as it found it')
    step = 1 if FULL or strict else 7
    cuts = list(range(0, total, step))
    replayed = refused = 0
    for n in cuts:
        shutil.copy(base, img)
        emu('--card', img, option, str(n), text=CRASH_SCRIPT)
        host = os.path.join(TMP, 'host.img')
        shutil.copy(img, host)
        ok, out = fsck(host, replay=True)             # the host's recovery
        if 'replayed' in out:
            replayed += 1
        if not ok:
            refused += 1
            if strict:
                fail('crash: cut at %d, host replay: %s' % (n, out.strip().replace('\n', ' | ')))
            continue
        rc, err = emu('--card', img, text='ls\n')      # the kernel's recovery
        ok, out = fsck(img)
        if not ok:
            refused += 1
            if strict:
                fail('crash: cut at %d, kernel replay: %s' % (n, out.strip().replace('\n', ' | ')))
        elif tree(img) != tree(host):
            fail('crash: cut at %d: kernel and host recovered different trees' % n)
        if strict and len(failures) > 5:
            break
    print('  %d cuts over %d writes: %d needed the journal replayed, %d could not be recovered'
          % (len(cuts), total, replayed, refused))


def torn():
    """Torn blocks: the same sweep with the cut falling inside a four-sector write (informative)."""
    crash('--power-cut-sectors', 1, strict=False)


TESTS = [('con00', con00), ('con01', con01), ('con02', con02), ('con03', con03), ('crash', crash), ('torn', torn)]

for name, fn in TESTS:
    if ONLY and name not in ONLY:
        continue
    print('%s: %s' % (name, fn.__doc__.split('\n')[0]))
    before = len(failures)
    fn()
    if len(failures) == before:
        print('  ok' + (' (golden files rewritten)' if UPDATE else ''))
shutil.rmtree(TMP, ignore_errors=True)
if failures:
    print('%d failure(s); what each run produced is in build/*.got' % len(failures))
    sys.exit(1)
print('all gates pass')
