#!/usr/bin/env python3
"""montage.py <dump_dir> <out.png> [cols] [name-filter] - contact sheet of dumps (320x200 cells)."""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vcompare as V  # noqa: E402


def main():
    d, out = sys.argv[1], sys.argv[2]
    cols = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    filt = sys.argv[4] if len(sys.argv) > 4 else ''
    names = sorted(f[:-4] for f in os.listdir(d) if f.endswith('.vga') and filt in f)
    cw, ch = 320, 200
    rows = (len(names) + cols - 1) // cols
    W, H = cols * (cw + 4), rows * (ch + 4)
    img = bytearray(W * H * 3)
    for k, n in enumerate(names):
        w, h, idx, dac = V.render(V.load(d, n))
        if not w:
            continue
        pal = bytes(min(255, (c << 2) | (c >> 4)) for c in dac)
        sx, sy = w // cw, h // ch
        ox, oy = (k % cols) * (cw + 4), (k // cols) * (ch + 4)
        for y in range(ch):
            for x in range(cw):
                c = idx[(y * sy) * w + x * sx] * 3
                o = ((oy + y) * W + ox + x) * 3
                img[o:o + 3] = pal[c:c + 3]
    raw = b''.join(b'\0' + bytes(img[y * W * 3:(y + 1) * W * 3]) for y in range(H))

    def chunk(t, data):
        c = struct.pack('>I', len(data)) + t + data
        return c + struct.pack('>I', zlib.crc32(t + data) & 0xFFFFFFFF)
    open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
                          + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
    print(out, len(names), 'cells:', ' '.join(names))


if __name__ == '__main__':
    main()
