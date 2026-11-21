#!/usr/bin/env python3
"""
gflview.py - decode GFL/GHD videos (IST.EXE ovl5313, CODE\\GFLCODE) to PNG
frames and WAV audio.  Written from the format description, independent of
the C port (src/game/gfl.c); tests/py/test_gfl.py compares the two.

GHD  (header + first second of audio + frame index)
  0x000  3     "GFL"      the player only checks that the 3 signed bytes sum to 0xD9
  0x003  u16   nframes
  0x005  u8    fps        (10 in every file)
  0x006  u16   audio bytes per second-chunk (22050: 8-bit unsigned mono, 22050 Hz); 0 = silent
  0x008  u32   100000 in every file, never read
  0x00C  4     0
  0x010  0x300 header palette, 6-bit VGA DAC RGB
  0x310  abytes audio of second 0
  ...    8*nframes index: u8 type, u16 vsize, u32 offset, u8 flags (packed)
GFL  (payloads) frame i at index offset: [palette 0x300 at offset-0x300 if
  flags & 0x80] [audio abytes if type == 2] [vsize bytes of video]
  type 0 delta frame, 2 keyframe + 1 s audio (frames 0, fps, 2*fps ...),
  1 palette-only frame (no video decode; vsize says 768 but no video bytes
  follow - the next frame's palette starts at offset; SEQ5's fade-out); flags bit 7 =
  palette chunk, bits 0-6 music-sync (n = (flags & 0x7F) >> 1, minus if bit 0).

Video codec: the stream updates a persistent 320x200 buffer, per code byte b
  1nnnnnnn  literal: copy (b & 0x7F) + 1 bytes from the stream
  01ffnnnn  skip L + 1 pixels
  00ffnnnn  fill L + 3 pixels with the next byte
  L = n, 2n + 0x10, 4n + 0x30, 8n + 0x70 for ff = 00, 01, 10, 11
  until vsize bytes are consumed or 64000 pixels are reached (checked after
  each code; a run can overshoot the end).

usage: gflview.py check DIR                 verify every *.GHD/*.GFL pair
       gflview.py png   NAME OUTDIR [FIRST [LAST]]   (NAME = path without extension)
       gflview.py wav   NAME OUT.wav
"""
import glob
import os
import struct
import sys

W, H = 320, 200
FRAME = W * H


def read_ghd(path):
    d = open(path, 'rb').read()
    sig = d[0:3]
    nframes, fps, abytes, magic = struct.unpack_from('<HBHI', d, 3)
    pal = d[0x10:0x310]
    audio0 = d[0x310:0x310 + abytes]
    idx = []
    base = 0x310 + abytes
    for i in range(nframes):
        typ, vsize, off, flags = struct.unpack_from('<BHIB', d, base + 8 * i)
        idx.append((typ, vsize, off, flags))
    return {'sig': sig, 'nframes': nframes, 'fps': fps, 'abytes': abytes, 'magic': magic,
            'pal': pal, 'audio0': audio0, 'index': idx, 'size': len(d), 'end': base + 8 * nframes}


def sig_ok(sig):
    return sum(b - 256 if b & 0x80 else b for b in sig) == 0xD9


def run_len(b):
    n = b & 0x0F
    sel = b & 0x30
    if sel == 0x00:
        return n
    if sel == 0x10:
        return 2 * n + 0x10
    if sel == 0x20:
        return 4 * n + 0x30
    return 8 * n + 0x70


def decode(buf, src):
    """Apply one frame's video data to buf (bytearray >= 64000 + 256).
    Returns (bytes consumed, pixel position reached)."""
    s, d, end = 0, 0, len(src)
    while True:
        b = src[s]
        if b & 0x80:
            n = (b & 0x7F) + 1
            buf[d:d + n] = src[s + 1:s + 1 + n]
            d += n
            s += 1 + n
        elif b & 0x40:
            d += run_len(b) + 1
            s += 1
        else:
            n = run_len(b) + 3
            buf[d:d + n] = bytes([src[s + 1]]) * n
            d += n
            s += 2
        if not (s < end and d < FRAME):
            return s, d


class Video:
    def __init__(self, name):
        self.h = read_ghd(name + '.GHD')
        self.gfl = open(name + '.GFL', 'rb').read()

    def frame_parts(self, i):
        typ, vsize, off, flags = self.h['index'][i]
        pal = self.gfl[off - 0x300:off] if flags & 0x80 else None
        a = self.h['abytes'] if typ == 2 else 0
        audio = self.gfl[off:off + a] if a else None
        video = self.gfl[off + a:off + a + vsize]
        return typ, pal, audio, video

    def frames(self):
        """Yield (index, pixels 64000 bytes, palette 768 bytes) in play order
        with no frame dropped: what the player shows on a fast machine."""
        buf = bytearray(FRAME + 256)
        pal = self.h['pal']
        for i in range(self.h['nframes']):
            typ, p, _, video = self.frame_parts(i)
            pal = p if p is not None else self.h['pal']
            if typ != 1:
                decode(buf, video)
            yield i, bytes(buf[:FRAME]), pal

    def audio(self):
        out = bytearray(self.h['audio0'])
        for i in range(self.h['nframes']):
            _, _, a, _ = self.frame_parts(i)
            if a:
                out += a
        return bytes(out)


def check_pair(name):
    errs = []
    h = read_ghd(name + '.GHD')
    gfl_size = os.path.getsize(name + '.GFL')
    if not sig_ok(h['sig']):
        errs.append('signature')
    if h['end'] != h['size']:
        errs.append(f'GHD size {h["size"]} != {h["end"]}')
    v = Video(name)
    idx = h['index']
    buf = bytearray(FRAME + 256)
    for i, (typ, vsize, off, flags) in enumerate(idx):
        exp_type = 2 if i % h['fps'] == 0 else 0
        if typ != exp_type and not (typ == 1 and exp_type == 0):
            errs.append(f'frame {i}: type {typ}')
        vbytes = 0 if typ == 1 else vsize      # type 1: vsize 768 but no video bytes
        if flags & 0x7F:
            errs.append(f'frame {i}: sync flags {flags:02X}')
        if i + 1 < len(idx):
            nxt = off + (h['abytes'] if typ == 2 else 0) + vbytes + (0x300 if idx[i + 1][3] & 0x80 else 0)
            if nxt != idx[i + 1][2]:
                errs.append(f'frame {i}: next offset {idx[i + 1][2]} != {nxt}')
        else:
            end = off + (h['abytes'] if typ == 2 else 0) + vbytes
            if end != gfl_size:
                errs.append(f'last frame ends at {end}, file {gfl_size}')
        if typ == 1:
            continue
        _, _, _, video = v.frame_parts(i)
        s, d = decode(buf, video)
        if s != vsize or d != FRAME:
            errs.append(f'frame {i}: consumed {s}/{vsize}, reached {d}')
    pals = sum(1 for e in idx if e[3] & 0x80)
    h['type1'] = sum(1 for e in idx if e[0] == 1)
    return h, pals, errs


def to_png(pix, pal, path):
    from PIL import Image
    img = Image.frombytes('P', (W, H), pix)
    img.putpalette(bytes(min(255, (c << 2) | (c >> 4)) for c in pal))
    img.save(path)


def main(argv):
    if len(argv) == 3 and argv[1] == 'check':
        bad = 0
        for p in sorted(glob.glob(os.path.join(argv[2], '*.GHD'))):
            name = p[:-4]
            h, pals, errs = check_pair(name)
            print(f'{os.path.basename(name):10s} {h["nframes"]:5d} frames {h["fps"]} fps '
                  f'audio {h["abytes"]} palettes {pals:3d} type1 {h["type1"]:2d}  {"OK" if not errs else "; ".join(errs[:5])}')
            bad += bool(errs)
        print('all ok' if not bad else f'{bad} file(s) with problems')
        return 1 if bad else 0
    if len(argv) in (4, 5, 6) and argv[1] == 'png':
        v = Video(argv[2])
        os.makedirs(argv[3], exist_ok=True)
        first = int(argv[4]) if len(argv) > 4 else 0
        last = int(argv[5]) if len(argv) > 5 else v.h['nframes'] - 1
        for i, pix, pal in v.frames():
            if first <= i <= last:
                to_png(pix, pal, os.path.join(argv[3], f'{i:05d}.png'))
            if i >= last:
                break
        print(f'{argv[2]}: frames {first}..{last} -> {argv[3]}')
        return 0
    if len(argv) == 4 and argv[1] == 'wav':
        import wave
        v = Video(argv[2])
        with wave.open(argv[3], 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(1)
            w.setframerate(v.h['abytes'] or 22050)
            w.writeframes(v.audio())
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
