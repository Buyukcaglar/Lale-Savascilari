#!/usr/bin/env python3
"""
fmtcheck.py - check the statements of docs/FORMATS.md about .KOR, .RIX and
.AMF files against every shipped file.

usage: fmtcheck.py [kor|rix|amf|all]      (run from the repository root;
                                          needs orig/ and extracted/)

kor  3D/*.KOR (wall-set texture rectangles, q3d_load_kor 2700:1E06) and the
     sprite frame tables SAVAS/*.KOR + orig/BOSCH.KOR (iso_draw_sprite
     ovl4595:0387): sizes, header words, record values, boxes inside the
     matching CMP picture.
rix  IVIR/*.RIX (blend table, blend_lookup in src/engine/blit386.c): size and
     how well each table matches "nearest colour to the average" over a
     palette (the rule of sys_find_color, first or last minimum).  Needs numpy.
amf  MUZIK/*.AMF walked as ampLoadAMF (105B:008A) reads them; the walk must
     end exactly at the end of the file.
Exit status 1 if a check fails.
"""
import collections
import glob
import os
import struct
import sys

FAIL = []


def check(cond, msg):
    if not cond:
        FAIL.append(msg)
        print('FAIL:', msg)


def cmp_size(path):
    with open(path, 'rb') as f:
        return struct.unpack('<HH', f.read(4))


def find_cmp(stem):
    for d in ('SAVAS', 'IVIR', '3D', 'IKON'):
        p = os.path.join('extracted', d, stem + '.CMP')
        if os.path.exists(p):
            return p
    return None


# ---- KOR ----------------------------------------------------------------------
def kor():
    files = sorted(glob.glob('extracted/3D/*.KOR'))
    flags = collections.Counter()
    for f in files:
        d = open(f, 'rb').read()
        ver, n = struct.unpack_from('<hH', d, 0)
        check(ver == 1 and n == 6, f'{f}: version {ver}, n {n}')
        hdr = struct.unpack_from('<3h', d, 4)
        check(hdr == (0, 200, 200), f'{f}: header {hdr}')
        check(len(d) in (4 + n, 4 + n + 37 * 14), f'{f}: size {len(d)}')
        for i in range(37 if len(d) > 4 + n else 0):
            r = struct.unpack_from('<7h', d, 4 + n + 14 * i)
            flags[r[6]] += 1
            check(r[2] > 0 and r[3] > 0 and r[0] >= 0 and r[1] >= 0, f'{f}: record {i} {r}')
        check(find_cmp(os.path.splitext(os.path.basename(f))[0]) is not None, f'{f}: no CMP')
        print(f'{os.path.basename(f):12} {len(d):4} bytes  version {ver}  n {n}  header {hdr}'
              f'  records {37 if len(d) > 10 else 0}')
    print(f'3D: {len(files)} files, record flags {dict(flags)}')

    files = sorted(glob.glob('extracted/SAVAS/*.KOR')) + ['orig/BOSCH.KOR']
    outside = collections.Counter()
    for f in files:
        d = open(f, 'rb').read()
        check(len(d) == 72, f'{f}: size {len(d)}')
        stem = os.path.splitext(os.path.basename(f))[0]
        c = find_cmp(stem)
        check(c is not None, f'{f}: no CMP')
        w, h = cmp_size(c) if c else (0, 0)
        for i in range(6):
            x1, y1, x2, y2, ax, ay = struct.unpack_from('<6h', d, 12 * i)
            check(0 <= x1 <= x2 < w and 0 <= y1 <= y2 < h,
                  f'{f}: frame {i} box ({x1},{y1})-({x2},{y2}) outside {c} {w}x{h}')
            if not (x1 <= ax <= x2 and y1 <= ay <= y2):
                outside[i] += 1
    print(f'sprites: {len(files)} files of 72 bytes, boxes inside their CMP;'
          f' anchors outside their box, by record: {dict(outside)}')
    stems = {os.path.splitext(os.path.basename(f))[0] for f in files}
    nokor = sorted(os.path.splitext(os.path.basename(f))[0] for f in glob.glob('extracted/SAVAS/*.CMP')
                   if os.path.splitext(os.path.basename(f))[0] not in stems)
    print('SAVAS CMP without KOR:', ' '.join(nokor))


# ---- RIX ----------------------------------------------------------------------
def rix_index(lo, hi):
    """blend_lookup: row lo of the packed upper triangle (lo < hi)."""
    return ((lo * (509 - lo)) >> 1) - 1 + hi


def rix():
    import numpy as np
    check(rix_index(0, 1) == 0 and rix_index(254, 255) == 32639, 'triangle index ends')
    ix = [rix_index(lo, hi) for lo in range(256) for hi in range(lo + 1, 256)]
    check(ix == list(range(32640)), 'blend_lookup does not enumerate the triangle row by row')
    lo, hi = np.triu_indices(256, 1)          # same order: (0,1), (0,2) ... (254,255)

    def table(pal, last):
        p = np.frombuffer(pal, np.uint8).reshape(256, 3).astype(np.int32)
        ref = (p[lo] + p[hi]) >> 1
        d = np.abs(p[None, :, :] - ref[:, None, :]).sum(2)
        return (255 - d[:, ::-1].argmin(1)) if last else d.argmin(1)

    pairs = (('XD', 'extracted/3D/PAL3D.PAL'), ('XDSON', 'extracted/IVIR/SOOON.PAL'),
             ('XDINTRO', 'extracted/IVIR/ACILISA.PAL'), ('XDEMO', 'extracted/IVIR/DEMO1.PAL'))
    files = sorted(glob.glob('extracted/IVIR/*.RIX'))
    check(len(files) == 4, f'{len(files)} RIX files')
    for f in files:
        check(os.path.getsize(f) == 32640, f'{f}: size')
    for name, pal in pairs:
        r = np.frombuffer(open(f'extracted/IVIR/{name}.RIX', 'rb').read(), np.uint8)
        pd = open(pal, 'rb').read()
        first, last = table(pd, False), table(pd, True)
        bad = np.nonzero(first != r)[0]
        his = sorted(set(hi[bad].tolist()))
        print(f'{name}.RIX vs {os.path.basename(pal)}: first-minimum rule {32640 - len(bad)}/32640,'
              f' last-minimum (sys_build_aa_table) {int((last == r).sum())}/32640'
              + (f'; the {len(bad)} others all pair a colour with {his}' if 0 < len(his) <= 2 else ''))
    print('orig/SONPAL.PAL identical to 3D/PAL3D.PAL:',
          open('orig/SONPAL.PAL', 'rb').read() == open('extracted/3D/PAL3D.PAL', 'rb').read())


# ---- AMF ----------------------------------------------------------------------
def amf():
    files = sorted(glob.glob('extracted/MUZIK/*.AMF'))
    check(len(files) == 15, f'{len(files)} AMF files')
    cmds = collections.Counter()
    tempo = collections.Counter()
    ends = collections.Counter()
    rates = collections.Counter()
    empty = collections.Counter()
    print(f'{"file":13} {"name":18} ins smp ord trk stored ch bytes')
    for f in files:
        d = open(f, 'rb').read()
        check(d[:3] == b'AMF' and d[3] == 14, f'{f}: magic {d[:4]}')
        name = d[4:36].split(b'\0')[0].decode('cp857')
        nins, nord, ntr = d[36], d[37], struct.unpack_from('<H', d, 38)[0]
        nch = d[40]
        pans = struct.unpack_from('32b', d, 41)
        check(pans[:nch] == tuple([-63, 63, 63, -63] * 8)[:nch] and not any(pans[nch:]),
              f'{f}: pans {pans}')
        bpm, spd = d[73], d[74]
        check((bpm, spd) == (125, 6), f'{f}: tempo {bpm}/{spd}')
        p = 75
        refs = []
        for o in range(nord):
            rows = struct.unpack_from('<H', d, p)[0]
            check(rows == 64, f'{f}: order {o} rows {rows}')
            refs += struct.unpack_from(f'<{nch}H', d, p + 2)
            p += 2 + 2 * nch
        check(min(refs) >= 1 and max(refs) <= ntr, f'{f}: track refs {min(refs)}..{max(refs)} of {ntr}')
        ins = []
        for i in range(nins):
            rec = d[p:p + 65]
            p += 65
            ins.append((rec[0],) + struct.unpack_from('<IIHBII', rec, 0x2E))
        nsmp = max(x[1] for x in ins)
        used = [x for x in ins if x[1]]
        # the shipped files number their samples 1, 2, 3 ... in instrument
        # order, so ampLoadAMF's "record a length when the number exceeds the
        # count so far" takes each used instrument's length in turn
        check([x[1] for x in used] == list(range(1, nsmp + 1)), f'{f}: sample numbers')
        rates.update(x[3] for x in used)
        check(all(x[0] == 1 for x in used), f'{f}: type of used instruments')
        check(all(x[0] == 0 and x[2] == 0 for x in ins if not x[1]), f'{f}: empty instruments')
        check(all(0 < x[2] < 0x10000 and x[4] <= 64 and x[5] <= x[6] <= x[2] for x in used),
              f'{f}: instrument fields')
        empty.update([nins - len(used)])
        tmap = struct.unpack_from(f'<{ntr}H', d, p)
        p += 2 * ntr
        check(0 not in tmap, f'{f}: logical track without data')
        for s in range(max(tmap)):
            cnt, typ = struct.unpack_from('<HB', d, p)
            p += 3
            check(typ == 0, f'{f}: track type {typ}')
            ent = [d[p + 3 * k:p + 3 * k + 3] for k in range(cnt)]
            p += 3 * cnt
            if not ent:
                ends['empty'] += 1
                continue
            ends[bytes(ent[-1]).hex()] += 1
            for r, c, a in ent[:-1]:
                check(r < 64, f'{f}: row {r}')
                if c >= 0x80:
                    cmds[c] += 1
                if c == 0x95:
                    tempo[a] += 1
        p += sum(x[2] for x in used)
        check(p == len(d), f'{f}: walk ends at {p}, file has {len(d)} bytes')
        print(f'{os.path.basename(f):13} {name[:18]:18} {nins:3} {nsmp:3} {nord:3} {ntr:3} {max(tmap):6}'
              f' {nch:2} {len(d)}')
    print('instrument rates', dict(rates))
    print('track ends', dict(ends), 'effects', {f'{k:02X}': v for k, v in sorted(cmds.items())},
          'effect 95 args', dict(tempo))


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else 'all'
    for name, fn in (('kor', kor), ('rix', rix), ('amf', amf)):
        if what in (name, 'all'):
            print(f'== {name}')
            fn()
    print('FAILED' if FAIL else 'all checks passed')
    sys.exit(1 if FAIL else 0)


if __name__ == '__main__':
    main()
