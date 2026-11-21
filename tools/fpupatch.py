#!/usr/bin/env python3
"""
fpupatch.py - turn Borland 8087-emulator interrupts back into x87 opcodes by
a linear sweep over each code block, skipping switch tables.

FixFPU.java (patching inside Ghidra at instruction starts found by flow)
was found to patch CD 3x bytes inside operands reached by wrong flows and
to miss real sites (ovl2700, 2026-11-21); this sweep is what the pipeline
now imports instead (build/ist_fpu_lin.bin).

usage: fpupatch.py [--diff] [out.bin]
  Reads build/ist_flat.bin + build/ist_map.json + build/ist_segments.json,
  patches CD 34..3D sequences found by a linear capstone sweep of every code
  block, writes out.bin (default build/ist_fpu_lin.bin).  --diff lists the
  bytes where the result differs from analysis/ghidra_ist/ist_fpu.bin.
"""
import json
import os
import sys

import capstone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def code_blocks():
    seg = json.load(open(os.path.join(ROOT, 'build', 'ist_segments.json')))
    for b in seg['segments']:
        name = b['name']
        if b['kind'] in ('code', 'ovl', 'asm') and name.startswith(('seg', 'ovl', 'asm'))                 and name not in ('seg1351', 'seg15C9'):          # the emulator itself
            lin = b['seg'] * 16 + b['start']
            yield name, lin, lin + b['size']


def data_ranges():
    """Switch jump tables (build/ist_switches.tsv) - data inside code."""
    p = os.path.join(ROOT, 'build', 'ist_switches.tsv')
    out = []
    if os.path.exists(p):
        for line in open(p).read().splitlines()[1:]:
            f = line.split('	')
            if len(f) >= 4 and f[3] and f[2]:
                s, o = (int(x, 16) for x in f[3].split(':'))
                out.append((s * 16 + o, s * 16 + o + 2 * int(f[2])))
    return out


DATA = []


def patch(img, start, end):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    a, n = start, 0
    while a < end:
        skip = next((hi for lo, hi in DATA if lo <= a < hi), None)
        if skip:
            a = skip
            continue
        i = next(md.disasm(bytes(img[a:a + 16]), a), None)
        if i is None:
            a += 1
            continue
        b = i.bytes
        if b[0] == 0xCD and len(b) == 2 and 0x34 <= b[1] <= 0x3D:
            v = b[1]
            if v <= 0x3B:
                img[a], img[a + 1] = 0x9B, 0xD8 + v - 0x34
            elif v == 0x3C:
                img[a], img[a + 1] = 0x9B, 0x26
            else:
                img[a], img[a + 1] = 0x90, 0x9B
            n += 1
            a += 1 if v != 0x3D else 2        # re-decode from the FWAIT/prefix
            continue
        a += i.size
    return n


def main(argv):
    diff = '--diff' in argv
    argv = [x for x in argv if x != '--diff']
    out = argv[1] if len(argv) > 1 else os.path.join(ROOT, 'build', 'ist_fpu_lin.bin')
    img = bytearray(open(os.path.join(ROOT, 'build', 'ist_flat.bin'), 'rb').read())
    DATA.extend(data_ranges())
    total = 0
    for name, s, e in code_blocks():
        total += patch(img, s, e)
    open(out, 'wb').write(img)
    print(f'fpupatch: {total} sites -> {out}')
    if diff:
        g = open(os.path.join(ROOT, 'analysis', 'ghidra_ist', 'ist_fpu.bin'), 'rb').read()
        for name, s, e in code_blocks():
            d = [i for i in range(s, min(e, len(g))) if img[i] != g[i]]
            if d:
                print(f'  {name}: {len(d)} bytes differ, first at {d[0] >> 4:04X}:{d[0] & 15:04X}+ '
                      f'(lin {d[0]:X})')


if __name__ == '__main__':
    main(sys.argv)
