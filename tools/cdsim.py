#!/usr/bin/env python3
"""
cdsim.py - build a *simulated* CD image of
    "Istanbul Efsaneleri: Lale Savascilari: Birinci Bolum" (Turkey)
with the exact track layout of the redump.org entry 91232, so the game's CD-audio
code (ovl5737, MSCDEX) and the port's CD-audio replacement can be exercised
until the real disc image is available.

Reference layout (redump.org/disc/91232, write offset 0, no pregaps in the cue):

    #  type          sectors   MSF        bytes        CRC-32    (real disc)
    1  Data/Mode 1   116170    25:48:70   273231840    514b4fe9
    2  Audio           9168    02:02:18    21563136    92e65f9b
    3  Audio          15466    03:26:16    36376032    51036dff
       total         140804    31:17:29   331171008

What is simulated
-----------------
* Track 1: an ISO 9660 file system holding the game files from orig/ (minus the
  user files GRP.DAT and SAKLA001.SAV), padded with zero sectors to exactly
  116170 sectors and written as raw MODE1/2352 sectors (sync, BCD MSF header,
  EDC, ECC P/Q) like the real dump. The PVD dates are the all-'0' (unset)
  values the real disc has. The volume label and file layout of the real disc
  are unknown.
* Tracks 2 and 3: original music synthesised by this script (a drone, a
  plucked oud-like melody in the Hicaz makam, and a frame drum), exactly
  9168 and 15466 sectors long, 44.1 kHz 16-bit stereo little-endian. This audio
  is generated from scratch, uses no samples or recordings, and is dedicated to
  the public domain (CC0 1.0). Same seed -> byte-identical output.

usage:
    cdsim.py build  [OUTDIR]          default OUTDIR = build/cd
    cdsim.py verify CUE               check any dump (real or simulated) against
                                      the redump layout and hashes
    cdsim.py selftest                 EDC/ECC vectorised-vs-scalar check
"""
import hashlib
import io
import os
import struct
import sys
import zlib

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

REDUMP = dict(
    url='http://redump.org/disc/91232/',
    tracks=[
        dict(n=1, type='MODE1/2352', sectors=116170, size=273231840, crc='514b4fe9',
             md5='77363cfffa75e795b1877dcfa6d63129',
             sha1='ee8c6070624f36b6ff40f3bf109fe08f72202a70'),
        dict(n=2, type='AUDIO', sectors=9168, size=21563136, crc='92e65f9b',
             md5='42a49f4e7d507533a61d7fcb22f19dbf',
             sha1='e2923e2c9c2a08185e7f4fdabedaa020cd0a18dc'),
        dict(n=3, type='AUDIO', sectors=15466, size=36376032, crc='51036dff',
             md5='a3c9d7ac93443cff1b7e6bbddc297644',
             sha1='f70cdd125e3f52231a83ec2e401057550ed193f9'),
    ],
    ring='9701056 RAKS', edition='Aktuel Haftalik Kent Rehberi Time Out',
)
BASE = 'Istanbul Efsaneleri - Lale Savascilari - Birinci Boeluem (Turkey) (Simulated)'
SECTOR = 2352
RATE = 44100
FRAMES_PER_SECTOR = SECTOR // 4          # 588 stereo frames per audio sector
EXCLUDE = {'GRP.DAT', 'SAKLA001.SAV'}    # user data, not on the pressed disc

# --------------------------------------------------------------------------
# Mode 1 sector encoding (ECMA-130): EDC = CRC-32/0xD8018001 (reflected),
# ECC = Reed-Solomon product code P (86x24) and Q (52x43) over GF(2^8)/0x11D.
# --------------------------------------------------------------------------
ECC_F = np.array([((i << 1) ^ (0x11D if i & 0x80 else 0)) & 0xFF for i in range(256)], np.uint8)
ECC_B = np.zeros(256, np.uint8)
for _i in range(256):
    ECC_B[_i ^ ECC_F[_i]] = _i
EDC_LUT = np.zeros(256, np.uint32)
for _i in range(256):
    e = _i
    for _ in range(8):
        e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
    EDC_LUT[_i] = e


def bcd(v):
    return ((v // 10) << 4) | (v % 10)


def _ecc_block(s, major_count, minor_count, major_mult, minor_inc, dest):
    """s: (N, 2352) uint8 sectors; computes one ECC layer in place (vectorised)."""
    src = s[:, 0xC:]
    size = major_count * minor_count
    for major in range(major_count):
        index = (major >> 1) * major_mult + (major & 1)
        a = np.zeros(len(s), np.uint8)
        b = np.zeros(len(s), np.uint8)
        for _ in range(minor_count):
            t = src[:, index]
            index += minor_inc
            if index >= size:
                index -= size
            a ^= t
            b ^= t
            a = ECC_F[a]
        a = ECC_B[ECC_F[a] ^ b]
        s[:, dest + major] = a
        s[:, dest + major + major_count] = a ^ b


def mode1_sectors(user, lba0):
    """user: (N, 2048) uint8 -> (N, 2352) raw Mode 1 sectors starting at lba0."""
    n = len(user)
    s = np.zeros((n, SECTOR), np.uint8)
    s[:, 1:11] = 0xFF
    a = np.arange(lba0, lba0 + n) + 150
    s[:, 12] = [bcd(x // 4500) for x in a]
    s[:, 13] = [bcd(x // 75 % 60) for x in a]
    s[:, 14] = [bcd(x % 75) for x in a]
    s[:, 15] = 1
    s[:, 16:2064] = user
    edc = np.zeros(n, np.uint32)
    for i in range(0x810):
        edc = (edc >> 8) ^ EDC_LUT[(edc ^ s[:, i]) & 0xFF]
    s[:, 0x810:0x814] = edc.astype('<u4').view(np.uint8).reshape(n, 4)
    _ecc_block(s, 86, 24, 2, 86, 0x81C)    # P
    _ecc_block(s, 52, 43, 86, 88, 0x8C8)   # Q
    return s


def mode1_scalar(user, lba):
    """Straight scalar reference implementation, used by the self-test."""
    s = bytearray(SECTOR)
    s[1:11] = b'\xff' * 10
    a = lba + 150
    s[12:16] = bytes([bcd(a // 4500), bcd(a // 75 % 60), bcd(a % 75), 1])
    s[16:2064] = user
    edc = 0
    for c in s[:0x810]:
        edc = (edc >> 8) ^ int(EDC_LUT[(edc ^ c) & 0xFF])
    s[0x810:0x814] = struct.pack('<I', edc)
    for (mc, nc, mm, mi, dest) in ((86, 24, 2, 86, 0x81C), (52, 43, 86, 88, 0x8C8)):
        size = mc * nc
        for major in range(mc):
            idx = (major >> 1) * mm + (major & 1)
            ea = eb = 0
            for _ in range(nc):
                t = s[0xC + idx]
                idx += mi
                if idx >= size:
                    idx -= size
                ea ^= t
                eb ^= t
                ea = int(ECC_F[ea])
            ea = int(ECC_B[int(ECC_F[ea]) ^ eb])
            s[dest + major] = ea
            s[dest + major + mc] = ea ^ eb
    return bytes(s)


# --------------------------------------------------------------------------
# ISO 9660 data track
# --------------------------------------------------------------------------
def build_iso(srcdir, volid='ISTANBUL'):
    import pycdlib
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, vol_ident=volid, sys_ident='DOS')
    for name in sorted(os.listdir(srcdir)):
        p = os.path.join(srcdir, name)
        if not os.path.isfile(p) or name.upper() in EXCLUDE:
            continue
        iso.add_file(p, f'/{name.upper()};1')
    buf = io.BytesIO()
    iso.write_fp(buf)
    iso.close()
    data = bytearray(buf.getvalue())
    # PVD (sector 16) dates: the real disc has the 'unset' form (16 x '0', GMT 0)
    pvd = 16 * 2048
    assert data[pvd:pvd + 6] == b'\x01CD001'
    for off in (813, 830, 847, 864):
        data[pvd + off:pvd + off + 17] = b'0' * 16 + b'\0'
    return bytes(data)


def write_data_track(path, iso, sectors):
    assert len(iso) % 2048 == 0
    need = len(iso) // 2048
    if need > sectors:
        raise SystemExit(f'data track needs {need} sectors, layout has {sectors}')
    with open(path, 'wb') as f:
        chunk = 4096
        for base in range(0, sectors, chunk):
            n = min(chunk, sectors - base)
            user = np.zeros((n, 2048), np.uint8)
            lo, hi = base * 2048, min(len(iso), (base + n) * 2048)
            if lo < hi:
                user.reshape(-1)[:hi - lo] = np.frombuffer(iso, np.uint8, hi - lo, lo)
            f.write(mode1_sectors(user, base).tobytes())
            print(f'\r  track 1: {base + n}/{sectors} sectors', end='', flush=True)
    print()
    return need


# --------------------------------------------------------------------------
# Audio: original, procedurally composed music (public domain / CC0)
# --------------------------------------------------------------------------
# Hicaz on D: D Eb F# G A Bb C D (12-TET approximation of the makam)
HICAZ = [0, 1, 4, 5, 7, 8, 10]


def midi_hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def pluck(freq, dur, rng, bright=1.0):
    """Additive oud-like pluck: inharmonic-free partials with per-partial decay."""
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    out = np.zeros(n)
    for k in range(1, 9):
        amp = (1.0 / k ** 1.3) * (bright if k > 2 else 1.0)
        dec = 2.2 + 1.6 * k
        out += amp * np.sin(2 * np.pi * freq * k * t + rng.uniform(0, 6.28)) * np.exp(-dec * t)
    att = np.minimum(1.0, t / 0.004)
    return out * att


def drum(kind, rng):
    n = int(0.35 * RATE)
    t = np.arange(n) / RATE
    if kind == 'dum':                       # low centre stroke of a frame drum
        f = 70 + 50 * np.exp(-t * 30)
        ph = 2 * np.pi * np.cumsum(f) / RATE
        return 0.9 * np.sin(ph) * np.exp(-t * 9)
    noise = rng.standard_normal(n)          # 'tek': bright rim stroke
    noise = np.diff(noise, prepend=0.0)
    return 0.35 * noise * np.exp(-t * 40)


def compose(frames, seed, tempo, rhythm, mood_low):
    rng = np.random.default_rng(seed)
    total = frames / RATE
    L = np.zeros(frames + RATE * 4)
    R = np.zeros_like(L)

    def add(sig, at, pan, gain):
        i = int(at * RATE)
        j = min(len(L), i + len(sig))
        if i >= len(L):
            return
        L[i:j] += sig[:j - i] * gain * (1 - pan)
        R[i:j] += sig[:j - i] * gain * pan

    beat = 60.0 / tempo
    root = 50 if mood_low else 62           # D3 / D4
    # drone: root + fifth, slow swell, the whole length
    t = np.arange(len(L)) / RATE
    sw = 0.5 + 0.5 * np.sin(2 * np.pi * t / 23.0)
    drone = (np.sin(2 * np.pi * midi_hz(root - 12) * t) * 0.5
             + np.sin(2 * np.pi * midi_hz(root - 5) * t) * 0.25
             + np.sin(2 * np.pi * midi_hz(root) * t * 1.002) * 0.15) * (0.6 + 0.4 * sw)
    L += drone * 0.10
    R += drone * 0.10
    # melody: a random walk on the Hicaz scale in 4-bar phrases that resolve to the root
    deg, at, bar = 0, 2 * beat, 0
    while at < total - 4 * beat:
        phrase_len = 16
        for k in range(phrase_len):
            if k == phrase_len - 1:
                deg = 0                      # resolve
            else:
                deg += int(rng.choice([-2, -1, -1, 1, 1, 2, 3, -3]))
                deg = max(-3, min(9, deg))
            octave, step = divmod(deg, 7)
            note = root + 12 * octave + HICAZ[step]
            dur = beat * float(rng.choice([0.5, 1, 1, 1, 1.5, 2]))
            if rng.random() < 0.85:
                add(pluck(midi_hz(note), dur * 2.5 + 0.4, rng), at, 0.55, 0.32)
                if rng.random() < 0.25:      # ornament a step above, oud style
                    up_o, up_s = divmod(deg + 1, 7)
                    add(pluck(midi_hz(root + 12 * up_o + HICAZ[up_s]), 0.4, rng, 0.6),
                        at - 0.06, 0.6, 0.12)
            at += dur
        at += beat                           # breath between phrases
        bar += 1
    # frame drum following the usul pattern (D=dum, T=tek, .=rest), in eighths
    step_t = beat / 2
    i, at = 0, 0.0
    while at < total - 1:
        c = rhythm[i % len(rhythm)]
        if c in 'DT':
            add(drum('dum' if c == 'D' else 'tek', rng), at, 0.45, 0.5)
        at += step_t
        i += 1
    # simple room: two feedback-free echoes
    for d, g in ((0.113, 0.22), (0.271, 0.12)):
        k = int(d * RATE)
        L[k:] += R[:-k] * g
        R[k:] += L[:-k] * g
    x = np.stack([L[:frames], R[:frames]], 1)
    fade = np.ones(frames)
    fi, fo = int(2.0 * RATE), int(4.0 * RATE)
    fade[:fi] = np.linspace(0, 1, fi)
    fade[-fo:] = np.linspace(1, 0, fo)
    x *= fade[:, None]
    x *= 0.89 / np.max(np.abs(x))            # peak at about -1 dBFS
    return (x * 32767).round().astype('<i2').tobytes()


def write_audio_track(path, sectors, **kw):
    pcm = compose(sectors * FRAMES_PER_SECTOR, **kw)
    assert len(pcm) == sectors * SECTOR
    open(path, 'wb').write(pcm)


# --------------------------------------------------------------------------
def hashes(path):
    crc, md5, sha1 = 0, hashlib.md5(), hashlib.sha1()
    with open(path, 'rb') as f:
        while True:
            b = f.read(1 << 22)
            if not b:
                break
            crc = zlib.crc32(b, crc)
            md5.update(b)
            sha1.update(b)
    return f'{crc:08x}', md5.hexdigest(), sha1.hexdigest()


def write_cue(path, files):
    with open(path, 'w', newline='\r\n') as f:
        for n, (fn, ty) in enumerate(files, 1):
            f.write(f'FILE "{fn}" BINARY\n  TRACK {n:02d} {ty}\n    INDEX 01 00:00:00\n')


def parse_cue(path):
    out, cur = [], None
    for line in open(path, encoding='latin-1'):
        w = line.strip().split()
        if not w:
            continue
        if w[0] == 'FILE':
            cur = line.split('"')[1]
        elif w[0] == 'TRACK':
            out.append((os.path.join(os.path.dirname(path), cur), w[2]))
    return out


def cmd_verify(cue):
    tr = parse_cue(cue)
    ok_layout = len(tr) == len(REDUMP['tracks'])
    real = True
    for (p, ty), ref in zip(tr, REDUMP['tracks']):
        size = os.path.getsize(p)
        crc, md5, sha1 = hashes(p)
        lay = ty == ref['type'] and size == ref['size']
        ok_layout &= lay
        match = (crc, md5, sha1) == (ref['crc'], ref['md5'], ref['sha1'])
        real &= match
        print(f"track {ref['n']}: {ty:10s} {size // SECTOR:6d} sectors "
              f"layout {'ok' if lay else 'MISMATCH'}  crc {crc}  "
              f"{'== redump' if match else '(not the redump dump)'}")
    print('layout matches redump 91232' if ok_layout else 'LAYOUT DIFFERS from redump 91232')
    print('this IS the verified redump dump' if real else 'hashes differ: not the original dump')
    return 0 if ok_layout else 1


def cmd_build(out):
    os.makedirs(out, exist_ok=True)
    t1, t2, t3 = REDUMP['tracks']
    names = [f'{BASE} (Track {n}).bin' for n in (1, 2, 3)]
    print('building ISO 9660 file system from orig/ ...')
    iso = build_iso(os.path.join(ROOT, 'orig'))
    used = write_data_track(os.path.join(out, names[0]), iso, t1['sectors'])
    print(f'  data: {used} sectors used, {t1["sectors"] - used} zero padding')
    print('  track 2: composing ...')
    write_audio_track(os.path.join(out, names[1]), t2['sectors'], seed=91232,
                      tempo=96, rhythm='D.T.T.D.D.T.T...', mood_low=False)
    print('  track 3: composing ...')
    write_audio_track(os.path.join(out, names[2]), t3['sectors'], seed=1997,
                      tempo=72, rhythm='D..TD.T.D..TT...', mood_low=True)
    cue = os.path.join(out, f'{BASE}.cue')
    write_cue(cue, [(names[0], 'MODE1/2352'), (names[1], 'AUDIO'), (names[2], 'AUDIO')])
    with open(os.path.join(out, 'SIMULATED.txt'), 'w', encoding='utf-8') as f:
        f.write(__doc__.split('usage:')[0].strip() + '\n\nFiles:\n')
        for n in names:
            crc, md5, sha1 = hashes(os.path.join(out, n))
            f.write(f'  {n}\n    size {os.path.getsize(os.path.join(out, n))}  '
                    f'crc32 {crc}  md5 {md5}\n')
        f.write('\nAudio license: CC0 1.0 Universal (public domain dedication).\n'
                'https://creativecommons.org/publicdomain/zero/1.0/\n')
    print('wrote', cue)
    return cmd_verify(cue)


def cmd_selftest():
    rng = np.random.default_rng(1)
    user = rng.integers(0, 256, (3, 2048), dtype=np.uint8)
    vec = mode1_sectors(user, 4495)          # crosses an MSF minute boundary
    for i in range(3):
        assert vec[i].tobytes() == mode1_scalar(user[i].tobytes(), 4495 + i), i
    z = mode1_sectors(np.zeros((1, 2048), np.uint8), 0)[0]
    assert z[12:16].tolist() == [0x00, 0x02, 0x00, 0x01]     # LBA 0 = 00:02:00
    print('selftest ok')
    return 0


def main(argv):
    if len(argv) >= 2 and argv[1] == 'build':
        return cmd_build(argv[2] if len(argv) > 2 else os.path.join(ROOT, 'build', 'cd'))
    if len(argv) == 3 and argv[1] == 'verify':
        return cmd_verify(argv[2])
    if len(argv) == 2 and argv[1] == 'selftest':
        return cmd_selftest()
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
