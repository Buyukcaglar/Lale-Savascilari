#!/usr/bin/env python3
"""strings.py - dump printable strings (cp857, Turkish DOS code page) from a
linear range of the flat image.  usage: strings.py FLAT SEG [END_SEG] [MINLEN]"""
import re, sys
m = open(sys.argv[1], 'rb').read()
seg = int(sys.argv[2], 16)
end = int(sys.argv[3], 16) * 16 if len(sys.argv) > 3 else len(m)
n = int(sys.argv[4]) if len(sys.argv) > 4 else 4
lo = seg * 16
out = open(sys.stdout.fileno(), 'w', encoding='utf-8', closefd=False)
for mo in re.finditer(rb'[\x20-\x7e\x80-\xfe\r\n]{%d,}' % n, m[lo:end]):
    s = mo.group().decode('cp857', errors='replace').replace('\r', '\r').replace('\n', '\n')
    out.write(f'{seg:04X}:{mo.start():04X}  {s}\n')
