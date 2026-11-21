#!/usr/bin/env python3
"""
flib.py - list / extract "FLiB" archives (*.CKD) used by
          'Istanbul Efsaneleri - Lale Savascilari' (DOS, 1995-2005).

Container layout (all little-endian), reverse engineered from the data files:

    +0x00  char[4]  "FLiB"
    +0x04  u32      number of entries
    +0x08  u32      total payload size (sum of entry headers+data, informational)
    +0x0C  entries[] -- packed back to back, each:
           +0x00 char[4]   "FiLE"
           +0x04 char[32]  name (NUL padded, 8.3 upper case)
           +0x24 char[16]  name again (8.3 short copy)
           +0x34 u8        flags (0 = stored; non-zero never observed)
           +0x35 u32       absolute file offset of the data
           +0x39 u32       stored size
           +0x3D u32       original size (equal to stored size in every file)
           +0x41 data[stored size]
    trailer: a verbatim copy of every entry header (count * 0x41 bytes) is
             appended after the last entry's data -- a directory the game can
             read in one go from (filesize - count*0x41).

usage:  flib.py list    ARCHIVE.CKD
        flib.py extract ARCHIVE.CKD OUTDIR
"""
import os
import struct
import sys

ENTRY_HDR = 0x41


def read_entries(path):
    out = []
    with open(path, 'rb') as f:
        sig, count, total = struct.unpack('<4sII', f.read(12))
        if sig != b'FLiB':
            raise ValueError(f'{path}: not a FLiB archive ({sig!r})')
        pos = 12
        for i in range(count):
            f.seek(pos)
            e = f.read(ENTRY_HDR)
            if e[:4] != b'FiLE':
                raise ValueError(f'{path}: bad entry {i} at {pos:#x}')
            name = e[4:0x24].split(b'\0')[0].decode('cp857')
            short = e[0x24:0x34].split(b'\0')[0].decode('cp857')
            flags = e[0x34]
            off, size, orig = struct.unpack_from('<III', e, 0x35)
            if off != pos + ENTRY_HDR:
                raise ValueError(f'{path}: entry {i} offset mismatch')
            out.append(dict(index=i, name=name, short=short, flags=flags,
                            offset=off, size=size, orig=orig))
            pos = off + size
        # verify the trailing directory copy
        f.seek(pos)
        trailer = f.read()
        if len(trailer) != count * ENTRY_HDR:
            print(f'warning: {path}: trailer is {len(trailer)} bytes, expected '
                  f'{count * ENTRY_HDR}', file=sys.stderr)
        else:
            for i, e in enumerate(out):
                f.seek(e['offset'] - ENTRY_HDR)
                if f.read(ENTRY_HDR) != trailer[i * ENTRY_HDR:(i + 1) * ENTRY_HDR]:
                    print(f'warning: {path}: trailer entry {i} differs',
                          file=sys.stderr)
    return out


def main(argv):
    if len(argv) < 3 or argv[1] not in ('list', 'extract'):
        print(__doc__)
        return 1
    ents = read_entries(argv[2])
    if argv[1] == 'list':
        for e in ents:
            print(f"{e['index']:4d} {e['offset']:#10x} {e['size']:10d} "
                  f"{e['flags']:2d} {e['name']}")
        return 0
    outdir = argv[3]
    os.makedirs(outdir, exist_ok=True)
    with open(argv[2], 'rb') as f:
        for e in ents:
            f.seek(e['offset'])
            left = e['size']
            with open(os.path.join(outdir, e['name']), 'wb') as o:
                while left:
                    chunk = f.read(min(left, 1 << 22))
                    o.write(chunk)
                    left -= len(chunk)
    print(f'{len(ents)} files -> {outdir}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
