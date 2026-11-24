#!/usr/bin/env python3
"""chd.py - an independent reader of the CHD files the port writes.

  python tools/chd.py info   DISC.chd
  python tools/chd.py verify DISC.chd [--cue IMAGE.cue] [--no-audio]

verify decodes every hunk with code that shares nothing with src/platform:
the deflate streams with Python's zlib, the Mode 1 sectors rebuilt with
cdsim.py's EDC/ECC, the FLAC frames with ffmpeg (they are renumbered and put
behind a "fLaC" header first; --no-audio skips this when ffmpeg is missing).
It then checks
  * the header's two SHA-1 sums (data; data + metadata),
  * the CRC-16 the map holds for every hunk,
  * each track against redump.org's sums (cdsim.REDUMP) and, with --cue,
    against the .bin files of a cue sheet, byte for byte.

Format: CHD v5 (MAME src/lib/util/chd.cpp, chdcodec.cpp, huffman.cpp), CD
codecs cdzl and cdfl, 'CHT2' metadata.  See src/platform/chd.h.
"""
import binascii
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cdsim

FRAME, SECTOR, HUNK_FRAMES = 2448, 2352, 8
HUNK = FRAME * HUNK_FRAMES
SYNC = bytes([0] + [255] * 10 + [0])
(T0, T1, T2, T3, NONE, SELF, PARENT, RLE_SMALL, RLE_LARGE, SELF_0, SELF_1) = range(11)


class Bits:
    """bit reader, most significant bit first"""

    def __init__(self, data):
        self.v = int.from_bytes(data, 'big')
        self.n = len(data) * 8
        self.pos = 0

    def peek(self, bits):
        end = self.pos + bits
        if end <= self.n:
            return (self.v >> (self.n - end)) & ((1 << bits) - 1)
        return ((self.v << (end - self.n)) & ((1 << bits) - 1))

    def read(self, bits):
        v = self.peek(bits)
        self.pos += bits
        return v


def crc16(data):
    """CRC-16/CCITT-FALSE"""
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def crc16_fast(data):
    return binascii.crc_hqx(data, 0xFFFF)     # the same CRC, in C


class Chd:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(124)
        if h[:8] != b'MComprHD':
            raise ValueError('not a CHD file')
        length, version = struct.unpack_from('>II', h, 8)
        if (length, version) != (124, 5):
            raise ValueError('not CHD version 5')
        self.comp = [h[16 + 4 * i:20 + 4 * i] for i in range(4)]
        self.logical, self.mapoffset, self.metaoffset = struct.unpack_from('>QQQ', h, 32)
        self.hunkbytes, self.unitbytes = struct.unpack_from('>II', h, 56)
        self.rawsha1, self.sha1, self.parentsha1 = h[64:84], h[84:104], h[104:124]
        if (self.hunkbytes, self.unitbytes) != (HUNK, FRAME):
            raise ValueError('not a CD image')
        self.hunkcount = (self.logical + HUNK - 1) // HUNK
        self.read_map()
        self.read_meta()

    # -- the map ---------------------------------------------------------------
    def read_map(self):
        self.f.seek(self.mapoffset)
        mh = self.f.read(16)
        mapbytes = struct.unpack_from('>I', mh, 0)[0]
        cur = int.from_bytes(mh[4:10], 'big')
        mapcrc = struct.unpack_from('>H', mh, 10)[0]
        lengthbits, selfbits, parentbits = mh[12], mh[13], mh[14]
        b = Bits(self.f.read(mapbytes))
        # Huffman tree: 16 lengths of 4 bits, 1 = escape
        lens = []
        while len(lens) < 16:
            v = b.read(4)
            if v != 1:
                lens.append(v)
                continue
            v = b.read(4)
            if v == 1:
                lens.append(1)
            else:
                lens += [v] * (b.read(4) + 3)
        assert len(lens) == 16
        # canonical codes: the longest lengths get the lowest code values
        histo = [0] * 33
        for l in lens:
            histo[l] += 1
        start = 0
        for l in range(32, 0, -1):
            nxt = (start + histo[l]) >> 1
            assert l == 1 or nxt * 2 == start + histo[l], 'incomplete Huffman code in the map'
            histo[l] = start
            start = nxt
        codes = {}
        for sym, l in enumerate(lens):
            if l:
                codes[(l, histo[l])] = sym
                histo[l] += 1

        def decode():
            for l in range(1, 9):
                s = codes.get((l, b.peek(l)))
                if s is not None:
                    b.pos += l
                    return s
            raise ValueError('bad Huffman code in the map')

        types, last, rep = [], 0, 0
        for _ in range(self.hunkcount):
            if rep:
                types.append(last)
                rep -= 1
                continue
            v = decode()
            if v == RLE_SMALL:
                types.append(last)
                rep = 2 + decode()
            elif v == RLE_LARGE:
                types.append(last)
                rep = 2 + 16 + (decode() << 4)
                rep += decode()
            else:
                last = v
                types.append(v)
        self.map = []
        raw = bytearray()
        last_self = 0
        for t in types:
            offset, length, crc = cur, 0, 0
            if t <= T3:
                length = b.read(lengthbits)
                cur += length
                crc = b.read(16)
            elif t == NONE:
                length = HUNK
                cur += length
                crc = b.read(16)
            elif t == SELF:
                last_self = offset = b.read(selfbits)
            elif t in (SELF_0, SELF_1):
                last_self += t == SELF_1
                t, offset = SELF, last_self
            else:
                raise ValueError('map type %d (parent) not supported' % t)
            self.map.append((t, length, offset, crc))
            raw += bytes([t]) + length.to_bytes(3, 'big') + offset.to_bytes(6, 'big') + crc.to_bytes(2, 'big')
        assert b.pos <= b.n, 'map bit stream too short'
        if crc16_fast(raw) != mapcrc:
            raise ValueError('map CRC mismatch')

    # -- metadata ----------------------------------------------------------------
    def read_meta(self):
        self.meta, self.tracks = [], []
        off = self.metaoffset
        phys = lba = 0
        while off:
            self.f.seek(off)
            mh = self.f.read(16)
            tag, flags = mh[:4], mh[4]
            length = int.from_bytes(mh[5:8], 'big')
            off = struct.unpack_from('>Q', mh, 8)[0]
            data = self.f.read(length)
            self.meta.append((tag, flags, data))
            if tag == b'CHT2':
                m = re.match(rb'TRACK:(\d+) TYPE:(\S+) SUBTYPE:(\S+) FRAMES:(\d+) PREGAP:(\d+) '
                             rb'PGTYPE:(\S+) PGSUB:(\S+) POSTGAP:(\d+)\x00$', data)
                assert m, data
                frames = int(m.group(4))
                self.tracks.append(dict(n=int(m.group(1)), type=m.group(2).decode(), frames=frames,
                                        lba=lba, phys=phys))
                lba += frames
                phys += (frames + 3) // 4 * 4
        assert phys * FRAME == self.logical, 'tracks do not add up to the logical size'

    # -- hunks --------------------------------------------------------------------
    def hunk_raw(self, h):
        t, length, offset, crc = self.map[h]
        self.f.seek(offset)
        return self.f.read(length)

    def codec(self, h):
        t = self.map[h][0]
        if t <= T3:
            return self.comp[t].decode()
        return {NONE: 'none', SELF: 'self'}[t]


def inflate(data):
    d = zlib.decompressobj(-15)
    out = d.decompress(data)
    assert d.eof, 'deflate stream does not end'
    return out, len(data) - len(d.unused_data)


def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def flac_crc16(data):
    c = 0
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def utf8_number(n):
    if n < 0x80:
        return bytes([n])
    out, lead, limit = [], 0xC0, 0x20
    while n >= limit:
        out.insert(0, 0x80 | (n & 0x3F))
        n >>= 6
        lead, limit = (lead >> 1) | 0x80, limit >> 1
        if len(out) == 1:
            lead, limit = 0xC0, 0x20
        else:
            lead = (0xFF << (7 - len(out))) & 0xFF
            limit = 1 << (6 - len(out))
    out.insert(0, lead | n)
    return bytes(out)


def split_frames(flac, nframes):
    """The FLAC frames of one cdfl hunk: fixed block size, numbered 0..n-1 with
    one-byte numbers, block size as a 16-bit field."""
    starts = [0]
    for k in range(1, nframes):
        pos = starts[-1] + 8
        while True:
            pos = flac.index(b'\xff\xf8', pos)
            if flac[pos + 4] == k and crc8(flac[pos:pos + 7]) == flac[pos + 7]:
                break
            pos += 1
        starts.append(pos)
    starts.append(len(flac))
    return [flac[starts[i]:starts[i + 1]] for i in range(nframes)]


def renumber(frame, number):
    """the frame with a new frame number (both CRCs redone)"""
    assert frame[:2] == b'\xff\xf8' and frame[4] < 0x80
    assert flac_crc16(frame[:-2]) == int.from_bytes(frame[-2:], 'big'), 'FLAC frame CRC-16'
    head = frame[:4] + utf8_number(number) + frame[5:7]
    body = head + bytes([crc8(head)]) + frame[8:-2]
    return body + flac_crc16(body).to_bytes(2, 'big')


def decode_flac(frames, nsamples):
    """all frames as one .flac file through ffmpeg -> s16le bytes"""
    info = bytearray(34)
    info[0:2] = info[2:4] = (2352).to_bytes(2, 'big')
    packed = (44100 << 44) | (1 << 41) | (15 << 36) | nsamples
    info[10:18] = packed.to_bytes(8, 'big')
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, 'a.flac'), os.path.join(d, 'a.raw')
        with open(src, 'wb') as f:
            f.write(b'fLaC' + bytes([0x80, 0, 0, 34]) + bytes(info))
            for fr in frames:
                f.write(fr)
        r = subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', src, '-f', 's16le', '-acodec', 'pcm_s16le', dst],
                           capture_output=True, text=True)
        if r.returncode != 0 or r.stderr.strip():
            raise ValueError('ffmpeg: ' + r.stderr.strip()[:500])
        return open(dst, 'rb').read()


def cmd_info(path):
    c = Chd(path)
    print('%s: %d hunks, logical %d bytes' % (path, c.hunkcount, c.logical))
    print('compressors:', ' '.join(x.decode() if x != b'\0\0\0\0' else '-' for x in c.comp))
    for t in c.tracks:
        print('track %(n)2d  %(type)-10s frames %(frames)6d  lba %(lba)6d' % t)
    use = {}
    for h in range(c.hunkcount):
        k = c.codec(h)
        n, b = use.get(k, (0, 0))
        use[k] = (n + 1, b + c.map[h][1])
    for k, (n, b) in sorted(use.items()):
        print('  %-5s %6d hunks %11d bytes' % (k, n, b))
    print('sha1 %s  data sha1 %s' % (c.sha1.hex(), c.rawsha1.hex()))


def cmd_verify(path, cue=None, audio=True):
    c = Chd(path)
    ok = True
    hunks = [None] * c.hunkcount
    flac_jobs = []            # (hunk, frames) of the cdfl hunks
    flagged = []              # (lba, hunk, sector) of the cdzl sectors stored without sync/ECC
    sub_zero = None
    for h in range(c.hunkcount):
        t, length, offset, crc = c.map[h]
        if t == SELF:
            continue
        data = c.hunk_raw(h)
        kind = c.codec(h)
        if kind == 'none':
            hunks[h] = data
        elif kind == 'cdzl':
            blen = int.from_bytes(data[1:3], 'big')
            base, used = inflate(data[3:3 + blen])
            assert used == blen and len(base) == HUNK_FRAMES * SECTOR
            sub, used = inflate(data[3 + blen:])
            assert used == len(data) - 3 - blen and len(sub) == HUNK_FRAMES * 96
            # sectors flagged in the ECC bitmap: sync and parity come back from
            # the header and data - rebuilt below, in runs, by cdsim's builder
            # (which also redoes the EDC: it must agree with the stored one)
            for k in range(HUNK_FRAMES):
                if data[0] >> k & 1:
                    flagged.append((cdsim_lba(base[k * SECTOR:(k + 1) * SECTOR]), h, k))
            hunks[h] = [bytearray(base), sub]
        elif kind == 'cdfl':
            if not audio:
                continue
            # the subcode stream at the end: found by trying where zlib accepts one
            if sub_zero is None:
                for cut in range(len(data) - 2, 0, -1):
                    try:
                        sub, used = inflate(data[cut:])
                    except Exception:
                        continue
                    if len(sub) == HUNK_FRAMES * 96 and used == len(data) - cut and not any(sub):
                        sub_zero = data[cut:]
                        break
                assert sub_zero, 'no subcode stream found in a cdfl hunk'
            assert data.endswith(sub_zero), 'cdfl hunk %d: unexpected subcode' % h
            flac_jobs.append((h, split_frames(data[:-len(sub_zero)], 2)))
        else:
            raise ValueError('hunk %d: codec %s' % (h, kind))
    flagged.sort()
    i = 0
    while i < len(flagged):
        j = i + 1
        while j < len(flagged) and j - i < 4096 and flagged[j][0] == flagged[j - 1][0] + 1:
            j += 1
        user = np.empty((j - i, 2048), np.uint8)
        for n, (lba, h, k) in enumerate(flagged[i:j]):
            user[n] = np.frombuffer(bytes(hunks[h][0][k * SECTOR + 16:k * SECTOR + 2064]), np.uint8)
        full = cdsim.mode1_sectors(user, flagged[i][0])
        for n, (lba, h, k) in enumerate(flagged[i:j]):
            f = full[n].tobytes()
            if f[12:2076] != bytes(hunks[h][0][k * SECTOR + 12:k * SECTOR + 2076]):
                print('hunk %d sector %d: header/EDC differ from a rebuilt Mode 1 sector' % (h, k))
                ok = False
            hunks[h][0][k * SECTOR:(k + 1) * SECTOR] = f
        i = j
    for h in range(c.hunkcount):
        if isinstance(hunks[h], list):
            base, sub = hunks[h]
            hunks[h] = b''.join(bytes(base[k * SECTOR:(k + 1) * SECTOR]) + sub[k * 96:(k + 1) * 96]
                                for k in range(HUNK_FRAMES))
    print('data: %d sectors rebuilt from header + user data (sync, ECC)' % len(flagged))
    if flac_jobs:
        frames = [renumber(fr, i) for i, fr in enumerate(f for _, fs in flac_jobs for f in fs)]
        pcm = decode_flac(frames, len(flac_jobs) * HUNK_FRAMES * SECTOR // 4)
        per = HUNK_FRAMES * SECTOR
        assert len(pcm) == per * len(flac_jobs), 'ffmpeg returned %d bytes' % len(pcm)
        be = np.frombuffer(pcm, '<i2').astype('>i2').tobytes()      # the CHD keeps big-endian samples
        for i, (h, _) in enumerate(flac_jobs):
            base = be[i * per:(i + 1) * per]
            hunks[h] = b''.join(base[k * SECTOR:(k + 1) * SECTOR] + bytes(96) for k in range(HUNK_FRAMES))
        print('audio: %d cdfl hunks, %d FLAC frames decoded by ffmpeg' % (len(flac_jobs), len(frames)))
    for h in range(c.hunkcount):
        if c.map[h][0] == SELF:
            hunks[h] = hunks[c.map[h][2]]
    if any(x is None for x in hunks):
        print('audio hunks not decoded (--no-audio): the SHA-1 and track checks are skipped')
        return 0 if ok else 1

    # CRC-16 of every hunk, SHA-1 of the data, overall SHA-1
    bad = [h for h in range(c.hunkcount) if c.map[h][0] != SELF and crc16_fast(hunks[h]) != c.map[h][3]]
    if bad:
        print('CRC-16 mismatch in %d hunks, first %d' % (len(bad), bad[0]))
        ok = False
    sha = hashlib.sha1()
    left = c.logical
    for x in hunks:
        sha.update(x[:min(left, HUNK)])
        left -= HUNK
    print('data SHA-1    %s  %s' % (sha.hexdigest(), 'ok' if sha.digest() == c.rawsha1 else 'MISMATCH'))
    ok &= sha.digest() == c.rawsha1
    entries = sorted(tag + hashlib.sha1(data).digest() for tag, flags, data in c.meta if flags & 1)
    overall = hashlib.sha1(c.rawsha1 + b''.join(entries)).digest()
    print('overall SHA-1 %s  %s' % (overall.hex(), 'ok' if overall == c.sha1 else 'MISMATCH'))
    ok &= overall == c.sha1

    # the tracks as .bin data
    cue_files = [f for f, _ in cdsim.parse_cue(cue)] if cue else None
    whole = b''.join(hunks)
    for i, t in enumerate(c.tracks):
        frames = [whole[(t['phys'] + k) * FRAME:(t['phys'] + k) * FRAME + SECTOR] for k in range(t['frames'])]
        data = b''.join(frames)
        if t['type'] == 'AUDIO':
            data = np.frombuffer(data, '>i2').astype('<i2').tobytes()
        crc = '%08x' % zlib.crc32(data)
        sha1 = hashlib.sha1(data).hexdigest()
        note = ''
        ref = next((r for r in cdsim.REDUMP['tracks'] if r['n'] == t['n']), None)
        if ref:
            if (crc, sha1) == (ref['crc'], ref['sha1']):
                note = '= redump.org'
            elif ref.get('prefix') and hashlib.sha1(data[:ref['prefix'][0]]).hexdigest() == ref['prefix'][1]:
                note = '= redump.org but for the last %d bytes' % (len(data) - ref['prefix'][0])
            else:
                note = 'differs from redump.org'
        if cue_files:
            same = open(cue_files[i], 'rb').read() == data
            note += '; %s the cue sheet\'s file' % ('=' if same else 'DIFFERS from')
            ok &= same
        print('track %d  %-10s %9d bytes  crc32 %s  sha1 %s  %s' % (t['n'], t['type'], len(data), crc, sha1, note))
    print('verify: %s' % ('ok' if ok else 'FAILED'))
    return 0 if ok else 1


def cdsim_lba(sector):
    """LBA from a Mode 1 header (BCD minute, second, frame)"""
    m, s, f = (10 * (x >> 4) + (x & 15) for x in sector[12:15])
    return (m * 60 + s) * 75 + f - 150


def main(argv):
    if len(argv) >= 3 and argv[1] == 'info':
        cmd_info(argv[2])
        return 0
    if len(argv) >= 3 and argv[1] == 'verify':
        cue = argv[argv.index('--cue') + 1] if '--cue' in argv else None
        return cmd_verify(argv[2], cue, '--no-audio' not in argv)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
