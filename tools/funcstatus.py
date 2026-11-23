#!/usr/bin/env python3
"""
funcstatus.py - the "Function status" table of docs/PROGRESS.md, from the
header comments of the port's sources.

    python tools/funcstatus.py            # markdown table
    python tools/funcstatus.py --list     # one line per function

Every decompiled routine has a comment block with "Original: IST.EXE
SSSS:OOOO" and "Verified: ...".  A block counts as
  verified  Verified: names a test, an oracle comparison or a check
            ("tests/...", "against ...", "identical ...", "by ...", ...);
  draft     Verified: no / not verified;
  replaced  Verified: no ... replaced (a host equivalent, nothing of the
            original to compare).
A block without a Verified: line takes the file's (the first comment
block of the file, when it has one: module-wide statements such as
bc_stdio.c's); else it is "unstated".  Routines are grouped by the
segment of their original address, with the source files that hold them.
Only routines with an Original: line are counted: small static helpers
folded into a documented routine are not.
"""
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLOCK = re.compile(r'/\*(.*?)\*/', re.S)
ORIG = re.compile(r'Original:\s*(?:IST\.EXE\s+)?(?:ovl|seg)?([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})')
VER = re.compile(r'Verified:\s*(.*?)(?:\n\s*\*\s*\n|\n\s*\*\s*[A-Z][a-z]+:|$)', re.S)
NAME = re.compile(r'^\s*\*?\s*([a-zA-Z_][a-zA-Z0-9_]*)\s+-\s', re.M)


def classify(block):
    m = VER.search(block)
    if not m:
        return 'unstated', ''
    v = ' '.join(m.group(1).replace('*', ' ').split())
    low = v.lower()
    if low.startswith(('no', 'not ')) or low == 'not':
        if 'replaced' in low:
            return 'replaced', v
        return 'draft', v
    return 'verified', v


def scan():
    out = []
    for d, _, files in os.walk(os.path.join(ROOT, 'src')):
        for f in sorted(files):
            if not f.endswith(('.c', '.h')):
                continue
            p = os.path.join(d, f)
            txt = open(p, encoding='utf-8', errors='replace').read()
            blocks = BLOCK.findall(txt)
            file_st = classify(blocks[0]) if blocks else ('unstated', '')
            for b in blocks:
                m = ORIG.search(b)
                if not m:
                    continue
                n = NAME.search(b)
                st, v = classify(b)
                if st == 'unstated' and file_st[0] != 'unstated':
                    st, v = file_st[0], '(file) ' + file_st[1]
                out.append((m.group(1).upper(), m.group(2).upper(), n.group(1) if n else '?',
                            os.path.relpath(p, ROOT).replace('\\', '/'), st, v))
    # one entry per original address (a routine documented in .h and .c: the .c wins)
    best = {}
    for e in out:
        k = (e[0], e[1])
        if k not in best or (best[k][3].endswith('.h') and e[3].endswith('.c')):
            best[k] = e
    return sorted(best.values())


def main():
    rows = scan()
    if '--list' in sys.argv:
        for seg, off, name, f, st, v in rows:
            print(f'{seg}:{off}  {st:9s} {name:32s} {f}  {v[:80]}')
        return
    by = collections.OrderedDict()
    for seg, off, name, f, st, v in rows:
        by.setdefault((seg, f.split('/')[-1].split('.')[0]), collections.Counter())[st] += 1
    # merge per segment (several files can hold one segment)
    seg = collections.OrderedDict()
    files = collections.defaultdict(set)
    for (s, f), c in by.items():
        seg.setdefault(s, collections.Counter()).update(c)
        files[s].add(f)
    tot = collections.Counter()
    print('| Segment | Sources | Functions | verified | draft | replaced | unstated |')
    print('|---|---|---|---|---|---|---|')
    for s, c in sorted(seg.items()):
        n = sum(c.values())
        tot.update(c)
        print(f'| {s} | {", ".join(sorted(files[s]))} | {n} | {c["verified"]} | {c["draft"]} | '
              f'{c["replaced"]} | {c["unstated"]} |')
    print(f'| **all** | | **{sum(tot.values())}** | **{tot["verified"]}** | **{tot["draft"]}** | '
          f'**{tot["replaced"]}** | **{tot["unstated"]}** |')


if __name__ == '__main__':
    main()
