#!/usr/bin/env python3
"""
dgrefs.py - which code blocks touch which DGROUP offsets.

Scans analysis/ghidra_ist/asm/*.asm for DS-relative memory operands
(`[0x1234]`, `[BX + 0x1234]`, `word ptr [0x..]`, and Ghidra's
`; DAT_16e0_xxxx` / `s_..._16e0_xxxx` annotations) and writes
analysis/dgroup_refs.tsv:

    offset  width  blocks(count)  first_function

Width is the operand size seen most often (1/2/4, 0 = address taken / unknown).
The module that owns a variable (defines its DGxx() macro) is normally the
block with most references; see docs/DECOMPILING.md.

usage: dgrefs.py
"""
import collections
import glob
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANN = re.compile(r'_16e0_([0-9a-f]{4})')
WIDTH = {'byte': 1, 'word': 2, 'dword': 4}


def main():
    refs = collections.defaultdict(collections.Counter)
    width = collections.defaultdict(collections.Counter)
    first = {}
    for p in sorted(glob.glob(os.path.join(ROOT, 'analysis', 'ghidra_ist', 'asm', '*.asm'))):
        blk = os.path.basename(p)[:-4]
        func = '?'
        for line in open(p, encoding='utf-8', errors='replace'):
            if line.startswith('; FUNCTION '):
                func = line[11:].strip()
                continue
            m = ANN.search(line)
            if not m or 'CS:' in line or 'SS:' in line:
                continue
            off = int(m.group(1), 16)
            # 'label+0x28' annotations: take the real operand offset when present
            mm = re.search(r'\[(?:[A-Z]{2} \+ )?0x([0-9a-f]+)\]', line)
            if mm and '+0x' in line.split(';', 1)[-1]:
                off = int(mm.group(1), 16)
            refs[off][blk] += 1
            w = re.search(r'\b(byte|word|dword) ptr', line)
            width[off][WIDTH[w.group(1)] if w else 0] += 1
            first.setdefault(off, f'{blk}:{func}')
    out = os.path.join(ROOT, 'analysis', 'dgroup_refs.tsv')
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('offset\twidth\tblocks\tfirst_function\n')
        for off in sorted(refs):
            ws = [w for w, _ in width[off].most_common() if w] or [0]
            bl = ','.join(f'{b}({n})' for b, n in refs[off].most_common())
            f.write(f'{off:04X}\t{ws[0]}\t{bl}\t{first[off]}\n')
    print(f'dgrefs: {len(refs)} DGROUP offsets -> {out}')


if __name__ == '__main__':
    main()
