#!/usr/bin/env python3
"""
names_merge.py - merge analysis/names/*.tsv into analysis/names.tsv
                 (the file ApplyNames.java loads into Ghidra).

Checks: address format, C identifier syntax, duplicate addresses (first file
in sort order wins, conflicts reported), duplicate names (made unique with
an address suffix, reported).

usage: names_merge.py
"""
import collections
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADDR = re.compile(r'^[0-9A-F]{4}:[0-9A-F]{4}$')
IDENT = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')


def main():
    rows = {}
    src = {}
    problems = []
    for path in sorted(glob.glob(os.path.join(ROOT, 'analysis', 'names', '*.tsv'))):
        group = os.path.basename(path)[:-4]
        for n, line in enumerate(open(path, encoding='utf-8'), 1):
            line = line.rstrip('\r\n')
            if not line.strip() or line.startswith('#') or line.lower().startswith('address'):
                continue
            cols = line.split('\t')
            if len(cols) < 2:
                problems.append(f'{group}:{n}: too few columns')
                continue
            addr, name = cols[0].strip().upper(), cols[1].strip()
            comment = cols[2].strip() if len(cols) > 2 else ''
            if not ADDR.match(addr):
                problems.append(f'{group}:{n}: bad address {cols[0]!r}')
                continue
            if not IDENT.match(name):
                problems.append(f'{group}:{n}: bad identifier {name!r}')
                continue
            if addr in rows:
                if rows[addr][0] != name:
                    problems.append(f'{group}:{n}: {addr} already named {rows[addr][0]} '
                                    f'by {src[addr]}, ignoring {name}')
                continue
            rows[addr] = (name, f'[{group}] {comment}'.strip())
            src[addr] = group
    byname = collections.defaultdict(list)
    for a, (nm, _) in rows.items():
        byname[nm].append(a)
    for nm, addrs in byname.items():
        if len(addrs) > 1:
            problems.append(f'duplicate name {nm} at {", ".join(sorted(addrs))} - suffixed')
            for a in sorted(addrs)[1:]:
                rows[a] = (f'{nm}_{a.replace(":", "_")}', rows[a][1])
    out = os.path.join(ROOT, 'analysis', 'names.tsv')
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('address\tname\tcomment\n')
        for a in sorted(rows, key=lambda s: (int(s[:4], 16), int(s[5:], 16))):
            f.write(f'{a}\t{rows[a][0]}\t{rows[a][1]}\n')
    for p in problems:
        print('warning:', p, file=sys.stderr)
    per = collections.Counter(src.values())
    print(f'{len(rows)} names -> {out}  ({dict(per)}; {len(problems)} warnings)')


if __name__ == '__main__':
    main()
