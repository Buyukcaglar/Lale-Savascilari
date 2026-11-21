#!/usr/bin/env python3
"""
cmpview.py - decode CMP images (FILEMAN.L format) and render them to PNG.

CMP: u16 width, u16 height, then an RLE stream to EOF:
    c >  0x80 : run of (c - 0x80) copies of the next byte
    c <= 0x80 : c literal bytes follow (c = 0 is a no-op)
Pixels are 8-bit palette indices, row-major, width*height of them.

usage: cmpview.py check  DIR...           verify every *.CMP decodes exactly
       cmpview.py png    FILE.CMP PAL OUT.png
"""
import glob
import os
import struct
import sys


def decode(data):
    w, h = struct.unpack_from('<HH', data, 0)
    out = bytearray()
    i = 4
    n = len(data)
    while i < n:
        c = data[i]
        i += 1
        if c > 0x80:
            out += bytes([data[i]]) * (c - 0x80)
            i += 1
        else:
            out += data[i:i + c]
            i += c
    return w, h, bytes(out), i - n   # i-n > 0 means the stream overran the file


def to_png(w, h, pix, pal, path):
    from PIL import Image
    img = Image.frombytes('P', (w, h), pix[:w * h].ljust(w * h, b'\0'))
    img.putpalette(bytes(min(255, (v << 2) | (v >> 4)) for v in pal[:768]))
    img.save(path)


def main(argv):
    if len(argv) >= 3 and argv[1] == 'check':
        ok = bad = 0
        for d in argv[2:]:
            for p in sorted(glob.glob(os.path.join(d, '*.CMP'))):
                data = open(p, 'rb').read()
                w, h, pix, over = decode(data)
                if len(pix) == w * h and over == 0:
                    ok += 1
                else:
                    bad += 1
                    print(f'MISMATCH {p}: {w}x{h}={w*h} decoded {len(pix)} overrun {over}')
        print(f'{ok} ok, {bad} mismatched')
        return 1 if bad else 0
    if len(argv) == 5 and argv[1] == 'png':
        w, h, pix, _ = decode(open(argv[2], 'rb').read())
        to_png(w, h, pix, open(argv[3], 'rb').read(), argv[4])
        print(f'{argv[2]}: {w}x{h} -> {argv[4]}')
        return 0
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
