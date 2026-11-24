#!/usr/bin/env python3
"""
cddump.py - work with a redumper dump of the game CD
            ("Istanbul Efsaneleri: Lale Savascilari", redump.org 91232).

redumper (https://github.com/superg/redumper) leaves, besides the split tracks,
the raw scrambled read of the whole disc (<name>.scram, sample 0 = LBA -45150,
drive read offset already removed) and the TOC (<name>.toc).  This disc was
mastered with an *offset shift*: the data sectors 0..116019 sit 48 samples
early (write offset -48), while the last 150 sectors of the data track (the
post-gap) and both audio tracks sit at offset 0.  redumper splits everything
at -48 unless told otherwise, so its track files do not match redump.org:
the post-gap is left scrambled and the audio tracks start 48 samples early.
This tool follows the sync pattern sector by sector instead.

usage:
    cddump.py info    DUMPDIR            what the dump contains, offsets, hashes
    cddump.py rebuild DUMPDIR [OUTDIR]   write the redump-layout image
                                         (default OUTDIR = build/cd/real)
    cddump.py extract DUMPDIR [OUTDIR]   the files of the data track
                                         (default OUTDIR = orig_cd)
    cddump.py ls      DUMPDIR            ISO 9660 volume and directory

DUMPDIR holds <name>.scram and <name>.toc.  With a generic drive (no lead-out
overread) the last samples of the disc that lie past the drive's read offset
are not in the dump; they are written as zeros and reported.

The image written by `rebuild` is what `cdsim.py verify`, the DOSBox-X
configurations and the port's `cd_image` option take.
"""
import glob
import hashlib
import os
import struct
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cdsim  # noqa: E402  (Mode 1 encoder, redump reference, hashes)

ROOT = cdsim.ROOT
SECTOR = 2352
LEADIN = 45150                       # .scram starts at LBA -45150
SYNC = bytes([0] + [255] * 10 + [0])
BASE = 'Istanbul Efsaneleri - Lale Savascilari - Birinci Boeluem (Turkey)'


def scramble_table():
    """ECMA-130 Annex B: 15-bit LFSR x^15 + x + 1, preset 1, over bytes 12..2351."""
    t = np.zeros(SECTOR, np.uint8)
    reg = 1
    for i in range(12, SECTOR):
        v = 0
        for b in range(8):
            v |= (reg & 1) << b
            fb = (reg ^ (reg >> 1)) & 1
            reg = (reg >> 1) | (fb << 14)
        t[i] = v
    return t


def bcd(v):
    return ((v // 10) << 4) | (v % 10)


def msf_bytes(lba):
    a = lba + 150
    return bytes([bcd(a // 4500), bcd(a // 75 % 60), bcd(a % 75)])


class Dump:
    def __init__(self, d):
        sc = sorted(glob.glob(os.path.join(glob.escape(d), '*.scram')))
        if len(sc) != 1:
            raise SystemExit(f'{d}: expected exactly one .scram file, found {len(sc)}')
        self.dir, self.name = d, os.path.splitext(os.path.basename(sc[0]))[0]
        self.scram = np.memmap(sc[0], np.uint8, 'r')
        self.table = scramble_table()
        self.tracks = self.read_toc(os.path.join(d, self.name + '.toc'))

    @staticmethod
    def read_toc(path):
        """READ TOC (format 0) response: [(track, is_data, lba)], last entry = lead-out."""
        t = open(path, 'rb').read()
        n = struct.unpack_from('>H', t, 0)[0] + 2
        out = []
        for o in range(4, n, 8):
            ctrl, trk = t[o + 1] & 0x0F, t[o + 2]
            out.append((trk, bool(ctrl & 4), struct.unpack_from('>I', t, o + 4)[0]))
        return out

    def pos(self, lba, off=0):
        return (lba + LEADIN) * SECTOR + off * 4

    def raw(self, lba, off, count=1):
        """count sectors at sample offset off; (bytes zero-padded, missing byte count)."""
        a, b = self.pos(lba, off), self.pos(lba + count, off)
        got = np.array(self.scram[max(a, 0):min(b, len(self.scram))])
        miss = (b - a) - len(got)
        if miss:
            got = np.concatenate([np.zeros(max(0, -a), np.uint8), got,
                                  np.zeros(max(0, b - len(self.scram)), np.uint8)])[:b - a]
        return got, miss

    def find_offset(self, lba, guess, span=588):
        """Sample offset of data sector lba: sync + scrambled header, searched around guess."""
        want = SYNC + bytes(np.frombuffer(msf_bytes(lba) + b'\1', np.uint8) ^ self.table[12:16])
        for d in sorted(range(-span, span + 1), key=abs):
            a = self.pos(lba, guess + d)
            if 0 <= a and a + 16 <= len(self.scram) and self.scram[a:a + 16].tobytes() == want:
                return guess + d
        return None

    def data_track(self, lba0, lba1, progress=None):
        """Descrambled Mode 1 sectors [lba0, lba1) following the sync pattern.
        Returns (uint8 array (n, 2352), [(first_lba, count, offset)], bad sector list)."""
        n = lba1 - lba0
        out = np.zeros((n, SECTOR), np.uint8)
        zones, bad = [], []
        off = self.find_offset(lba0, 0)
        if off is None:
            raise SystemExit(f'no data sector found at LBA {lba0}')
        lba = lba0
        CH = 4096
        want_sync = np.frombuffer(SYNC, np.uint8)
        while lba < lba1:
            cnt = min(CH, lba1 - lba)
            blk, miss = self.raw(lba, off, cnt)
            blk = blk.reshape(cnt, SECTOR)
            ok = (blk[:, :12] == want_sync).all(axis=1)
            good = cnt if ok.all() else int(np.argmin(ok))
            if good:
                out[lba - lba0: lba - lba0 + good] = blk[:good] ^ self.table
                if zones and zones[-1][2] == off:
                    zones[-1][1] += good
                else:
                    zones.append([lba, good, off])
                lba += good
                if progress:
                    progress(lba - lba0, n)
                continue
            new = self.find_offset(lba, off)
            if new is None or new == off:
                bad.append(lba)                      # unreadable: left as zeros
                lba += 1
            else:
                off = new
        return out, [tuple(z) for z in zones], bad

    def audio_track(self, lba0, lba1, off):
        blk, miss = self.raw(lba0, off, lba1 - lba0)
        return blk, miss


class Iso:
    """Minimal ISO 9660 reader over (n, 2352) Mode 1 sectors or a 2048-byte image."""

    def __init__(self, sectors):
        self.s = sectors

    def read(self, lba, size):
        cnt = (size + 2047) // 2048
        return self.s[lba:lba + cnt, 16:2064].tobytes()[:size]

    def pvd(self):
        d = self.read(16, 2048)
        if d[:6] != b'\x01CD001':
            raise SystemExit('no ISO 9660 primary volume descriptor at sector 16')
        return d

    def entries(self):
        p = self.pvd()
        root = p[156:190]
        return self.readdir(struct.unpack_from('<I', root, 2)[0],
                            struct.unpack_from('<I', root, 10)[0], '')

    def readdir(self, lba, size, path):
        d = self.read(lba, size)
        i, out = 0, []
        while i < len(d):
            ln = d[i]
            if ln == 0:
                i = (i // 2048 + 1) * 2048
                continue
            r = d[i:i + ln]
            ext, sz = struct.unpack_from('<I', r, 2)[0], struct.unpack_from('<I', r, 10)[0]
            dt, fl, nm = r[18:25], r[25], r[33:33 + r[32]]
            i += ln
            if nm in (b'\0', b'\1'):
                continue
            name = nm.decode('latin-1').split(';')[0]
            date = '%04d-%02d-%02d %02d:%02d:%02d' % (1900 + dt[0], *dt[1:6])
            if fl & 2:
                out += self.readdir(ext, sz, path + name + '/')
            else:
                out.append(dict(name=path + name, lba=ext, size=sz, date=date, raw_date=bytes(dt)))
        return out


def hashes(b):
    b = bytes(b)
    return f'{zlib.crc32(b):08x}', hashlib.md5(b).hexdigest(), hashlib.sha1(b).hexdigest()


def load_tracks(d, quiet=False):
    """[(n, type, bytes, note)] in the redump layout, plus the zone/bad lists of track 1."""
    tr = d.tracks
    res, info = [], {}
    audio_off = 0
    for k, (trk, is_data, lba) in enumerate(tr[:-1]):
        end = tr[k + 1][2]
        if is_data:
            def prog(a, n):
                if not quiet:
                    print(f'\r  track {trk}: {a}/{n} sectors', end='', flush=True)
            sec, zones, bad = d.data_track(lba, end, prog)
            if not quiet:
                print()
            info[trk] = dict(zones=zones, bad=bad, sectors=sec)
            audio_off = zones[-1][2]          # the audio follows the last zone's offset
            res.append((trk, 'MODE1/2352', sec.reshape(-1), ''))
        else:
            blk, miss = d.audio_track(lba, end, audio_off)
            info[trk] = dict(missing=miss)
            res.append((trk, 'AUDIO', blk, f'last {miss} bytes not in the dump (zeros)' if miss else ''))
    return res, info


def cmd_info(dumpdir, write=None):
    d = Dump(dumpdir)
    print(f'{d.name}.scram: {len(d.scram)} bytes = LBA -{LEADIN} .. '
          f'{len(d.scram) / SECTOR - LEADIN:.3f}')
    for trk, is_data, lba in d.tracks:
        print(f'  TOC track {trk if trk != 0xAA else "lead-out":>8} {"data " if is_data else "audio"} LBA {lba}')
    tracks, info = load_tracks(d)
    ref = {t['n']: t for t in cdsim.REDUMP['tracks']}
    all_ok = True
    for trk, ty, data, note in tracks:
        crc, md5, sha1 = hashes(data)
        r = ref.get(trk)
        match = r and (crc, md5, sha1) == (r['crc'], r['md5'], r['sha1'])
        state = '== redump.org' if match else 'differs from redump.org'
        if not match and r and info[trk].get('missing'):
            pre = r.get('prefix')
            if pre and hashlib.sha1(bytes(data[:pre[0]])).hexdigest() == pre[1]:
                state = f'known dump: all but the last {len(data) - pre[0]} bytes present'
        all_ok &= bool(match)
        print(f'track {trk}: {ty:10s} {len(data) // SECTOR:6d} sectors  crc {crc}  sha1 {sha1}  {state}'
              + (f'\n          {note}' if note else ''))
        if ty != 'AUDIO':
            i = info[trk]
            for z in i['zones']:
                print(f'          sectors {z[0]:6d}..{z[0] + z[1] - 1:6d}: write offset {z[2]:+d} samples')
            if i['bad']:
                print(f'          {len(i["bad"])} sectors without sync: {i["bad"][:8]} ...')
            sec = i['sectors']
            wrong = 0
            for base in range(0, len(sec), 8192):
                blk = sec[base:base + 8192]
                gen = cdsim.mode1_sectors(blk[:, 16:2064], d.tracks[0][2] + base)
                wrong += int(np.count_nonzero((gen != blk).any(axis=1)))
            print(f'          sectors that are not valid Mode 1 (header, EDC, ECC): {wrong}')
    if write:
        os.makedirs(write, exist_ok=True)
        files = []
        for trk, ty, data, note in tracks:
            fn = f'{BASE} (Track {trk}).bin'
            open(os.path.join(write, fn), 'wb').write(bytes(data))
            files.append((fn, ty))
        cue = os.path.join(write, f'{BASE}.cue')
        cdsim.write_cue(cue, files)
        print('wrote', cue)
    return 0 if all_ok else 2


def data_sectors(dumpdir):
    d = Dump(dumpdir)
    lba0, lba1 = d.tracks[0][2], d.tracks[1][2]
    sec, zones, bad = d.data_track(lba0, lba1)
    return sec


def cmd_ls(dumpdir):
    iso = Iso(data_sectors(dumpdir))
    p = iso.pvd()
    print(f'system id {p[8:40].decode().strip()!r}  volume id {p[40:72].decode().strip()!r}  '
          f'volume set {p[190:318].decode().strip()!r}  {struct.unpack_from("<I", p, 80)[0]} sectors')
    print(f'path table {struct.unpack_from("<I", p, 132)[0]} bytes at L {struct.unpack_from("<I", p, 140)[0]}, '
          f'M {struct.unpack_from(">I", p, 148)[0]}; root directory at '
          f'{struct.unpack_from("<I", p, 158)[0]}; dates {p[813:829].decode()!r}')
    for e in iso.entries():
        data = iso.read(e['lba'], e['size'])
        print(f"{e['name']:14s} {e['size']:10d}  LBA {e['lba']:6d}  {e['date']}  "
              f"sha1 {hashlib.sha1(data).hexdigest()}")
    return 0


def cmd_extract(dumpdir, out):
    iso = Iso(data_sectors(dumpdir))
    os.makedirs(out, exist_ok=True)
    for e in iso.entries():
        fn = os.path.join(out, *e['name'].split('/'))
        os.makedirs(os.path.dirname(fn), exist_ok=True)
        open(fn, 'wb').write(iso.read(e['lba'], e['size']))
        print(f"  {e['name']:14s} {e['size']:10d}")
    print('extracted to', out)
    return 0


def main(argv):
    if len(argv) >= 3 and argv[1] == 'info':
        return cmd_info(argv[2])
    if len(argv) >= 3 and argv[1] == 'rebuild':
        return cmd_info(argv[2], argv[3] if len(argv) > 3 else os.path.join(ROOT, 'build', 'cd', 'real'))
    if len(argv) >= 3 and argv[1] == 'extract':
        return cmd_extract(argv[2], argv[3] if len(argv) > 3 else os.path.join(ROOT, 'orig_cd'))
    if len(argv) == 3 and argv[1] == 'ls':
        return cmd_ls(argv[2])
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
