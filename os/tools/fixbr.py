#!/usr/bin/env python3
"""Lengthen conditional branches that ca65 reports as out of range.

    fixbr.py <ca65 command...>

Runs the assembler; for every "Range error" it names, rewrites that line's
short branch as the long form from ca65's longbranch macro package (bne -> jne,
bra -> jmp) and tries again. A development aid: the sources it leaves behind
assemble on their own.
"""
import re, subprocess, sys

LONG = {'beq': 'jeq', 'bne': 'jne', 'bcc': 'jcc', 'bcs': 'jcs', 'bmi': 'jmi', 'bpl': 'jpl',
        'bvc': 'jvc', 'bvs': 'jvs', 'bra': 'jmp'}
for _ in range(200):
    r = subprocess.run(sys.argv[1:], capture_output=True, text=True)
    hits = re.findall(r'^(.*?)\((\d+)\): Error: Range error', r.stderr, re.M)
    if not hits:
        sys.stderr.write(r.stderr)
        sys.exit(r.returncode)
    for path, line in set(hits):
        src = open(path).read().split('\n')
        i = int(line) - 1
        m = re.search(r'\b(' + '|'.join(LONG) + r')\b', src[i])
        if not m:
            sys.exit('fixbr: no branch on %s:%s: %s' % (path, line, src[i]))
        src[i] = src[i][:m.start()] + LONG[m.group(1)] + src[i][m.end():]
        open(path, 'w').write('\n'.join(src))
sys.exit('fixbr: gave up')
