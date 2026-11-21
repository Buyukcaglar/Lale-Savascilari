#!/usr/bin/env python3
"""
strrefs.py - attach DGROUP string references to functions.

Borland large-model code passes string literals as  PUSH DS / PUSH imm16  (or
MOV reg,imm16), so Ghidra records no reference.  We match every 16-bit
immediate in the exported listings against the start offsets of NUL
terminated strings in DGROUP.

Writes analysis/strrefs_<exe>.tsv :  function  block  string_offset  text
and prints a per-block summary of source-file names (assert __FILE__ strings).

usage: strrefs.py [exe]
"""
import collections
import json
import os
import re
import sys

exe = sys.argv[1] if len(sys.argv) > 1 else 'ist'
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
img = open(os.path.join(ROOT, 'build', f'{exe}_flat.bin'), 'rb').read()
segj = json.load(open(os.path.join(ROOT, 'build', f'{exe}_segments.json')))
dg = int(segj['dgroup'], 16)
base = dg * 16
data = img[base:base + 0x10000]

# string starts: printable run (>=3 chars) preceded by NUL (or start) and ending in NUL
strings = {}
for m in re.finditer(rb'(?<=\x00)[\x09\x0a\x0d\x20-\x7e\x80-\xfe]{3,}\x00', data):
    strings[m.start()] = m.group()[:-1].decode('cp857', 'replace')

# Offsets >= 0x8000 are often printed as negative immediates (PUSH -0x7869 = 8797h).
# Also match MOV word ptr [..],imm (string pointers stored into locals/structs).
imm = re.compile(r'(?:PUSH|MOV\s+(?:\w+\s+ptr\s+\[[^\]]*\]|\w+),)\s*(-?)0x([0-9a-f]{1,4})\b', re.I)
rows = []
blocks = collections.defaultdict(collections.Counter)
asmdir = os.path.join(ROOT, 'analysis', f'ghidra_{exe}', 'asm')
for fn in sorted(os.listdir(asmdir)):
    blk = fn[:-4]
    func = None
    for line in open(os.path.join(asmdir, fn), encoding='utf-8', errors='replace'):
        if line.startswith('; FUNCTION '):
            func = line.split()[2]
            continue
        for neg, h in imm.findall(line):
            off = (0x10000 - int(h, 16)) & 0xFFFF if neg else int(h, 16)
            if off in strings and func:
                s = strings[off]
                rows.append((func, blk, f'{off:04X}', s))
                if re.search(r'CODE\\|\.L$|\.C$', s):
                    blocks[blk][s.split('\\')[-1]] += 1

with open(os.path.join(ROOT, 'analysis', f'strrefs_{exe}.tsv'), 'w', encoding='utf-8') as o:
    o.write('function\tblock\toffset\ttext\n')
    for r in rows:
        o.write('\t'.join([r[0], r[1], r[2], r[3].replace('\n', '\\n').replace('\r', '\\r')]) + '\n')
print(f'{len(rows)} string references')
for blk in sorted(blocks):
    print(f'{blk:10s}', dict(blocks[blk].most_common(4)))
