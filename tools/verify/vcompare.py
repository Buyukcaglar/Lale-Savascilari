#!/usr/bin/env python3
"""
vcompare.py - compare checkpoint dumps of the port and of the original.

Both sides write <name>.{vga,vram,dg,txt} (layout: src/platform/sdl3_main.c,
"input scripts"; DOSBox-X side: tools/verify/dbx_replay.py).

    python tools/verify/vcompare.py <port_dir> <dbx_dir> [--png <dir>] [--dg-ignore lale|none]
                                    [--first] [--names a,b,...]

Per checkpoint present on both sides: VGA registers, DAC, video memory
(visible area and whole), DGROUP (minus the host-only/pointer ranges of
tests/py/lale.py and uiworld.py when available) and the clock.
render() turns a dump into an indexed image (13h, mode X, 12h) for PNGs.
"""
import argparse
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'py'))


def load(d, name):
    p = os.path.join(d, name)
    vga = open(p + '.vga', 'rb').read()
    vram = open(p + '.vram', 'rb').read()
    dg = open(p + '.dg', 'rb').read() if os.path.exists(p + '.dg') else None
    txt = {}
    if os.path.exists(p + '.txt'):
        for line in open(p + '.txt'):
            k, _, v = line.strip().partition(' ')
            txt[k] = v
    return dict(vga=vga, vram=vram, dg=dg, txt=txt)


def regs(vga):
    return dict(misc=vga[0], seq=vga[1:6], gc=vga[6:15], crtc=vga[15:40], attr=vga[40:61], dac=vga[61:829])


def render(d):
    """-> (w, h, indexed bytes, dac) of the visible picture (simplified scan-out)."""
    r = regs(d['vga'])
    seq, gc, crtc, attr = r['seq'], r['gc'], r['crtc'], r['attr']
    vram = d['vram']
    start = crtc[0x0C] << 8 | crtc[0x0D]
    if not (gc[6] & 1):
        return 0, 0, b'', r['dac']                       # text mode: not rendered
    if attr[0x10] & 0x40:                                # 256 colours
        h = 200 if (crtc[9] & 0x1F) == 1 or (crtc[9] & 0x80) else 400
        if seq[4] & 0x08 and len(vram) == 0x10000:        # chain-4, CPU view
            out = bytes(vram[(start * 4 + i) & 0xFFFF] for i in range(320 * 200))
            return 320, 200, out, r['dac']
        pitch = crtc[0x13] * 2
        planes = [vram[p * 0x10000:(p + 1) * 0x10000] for p in range(4)]
        out = bytearray(320 * h)
        for y in range(h):
            for x in range(320):
                out[y * 320 + x] = planes[x & 3][(start + y * pitch + (x >> 2)) & 0xFFFF]
        return 320, h, bytes(out), r['dac']
    # 16 colours planar (12h)
    w, h = 640, 480 if r['misc'] & 0xC0 == 0xC0 else 350
    pitch = crtc[0x13] * 2
    planes = [vram[p * 0x10000:(p + 1) * 0x10000] for p in range(4)]
    out = bytearray(w * h)
    for y in range(h):
        base = start + y * pitch
        for xb in range(w // 8):
            o = (base + xb) & 0xFFFF
            b0, b1, b2, b3 = planes[0][o], planes[1][o], planes[2][o], planes[3][o]
            for bit in range(8):
                m = 0x80 >> bit
                c = (1 if b0 & m else 0) | (2 if b1 & m else 0) | (4 if b2 & m else 0) | (8 if b3 & m else 0)
                out[y * w + xb * 8 + bit] = (attr[c] & 0x3F) | ((attr[0x14] & 0x0C) << 4)
    return w, h, bytes(out), r['dac']


def write_png(path, w, h, idx, dac):
    pal = bytes(min(255, (c << 2) | (c >> 4)) for c in dac)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            i = idx[y * w + x] * 3
            raw += pal[i:i + 3]

    def chunk(t, data):
        c = struct.pack('>I', len(data)) + t + data
        return c + struct.pack('>I', zlib.crc32(t + data) & 0xFFFFFFFF)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def dg_ignore_ranges(kind, dgo=None):
    """DGROUP ranges that legitimately differ: far pointers to heap/EMS/VGA
    (native pointers in the port) and the clock - the list of
    tests/py/uiworld.py dg_ignore(), computed from the original's DGROUP."""
    if kind == 'none' or dgo is None:
        return []
    ign = set(range(0xE5EB, 0xE5F3))
    ign.update((0x7F, 0x80))
    ign.update(range(0x86F0, 0x86F8))
    ign.update(range(0xE7B4, 0xE7B8))
    for i in range(4):
        ign.update(range(0xDA2C + 7 * i, 0xDA30 + 7 * i))
    ign.update(range(0x929E, 0x9460))
    ign.update(range(0xEB90, 0xEBB0))
    ign.update(range(0xE74A, 0xE74E))        # clock: reported separately
    # whole-program comparison (tools/verify, docs/VERIFY.md):
    ign.update(range(0xEA80, 0x10000))       # past DGROUP: SS = DGROUP+EA8h (stack, then heap)
    ign.update(range(0x0050, 0x0090))        # c0/RTL: saved INT 0/4/5/6 vectors, PSP, env, heap
                                             # segments (DOS environment, not game state)
    ign.update(range(0x7AE3, 0x7BE8))        # AMP's private interrupt stack (0EF6:0F64 switches
                                             # SS:SP to DGROUP:7BE4; nested driver frames reach
                                             # down to 7B70, below the "cdiRegister" string)
    ign.update(range(0xE764, 0xE766))        # int86 flags image after INT 33h: DOSBox-X's FLAGS
                                             # carry IOPL=3/NT (7212h), the port stores 0212h;
                                             # never read
    for i in range(110):
        r = 0xDA48 + 9 * i
        if dgo[r + 8] in (0, 0xFF) and r != 0xE5D8:
            ign.update(range(r, r + 4))
    for lo, hi, _ in HOST_FIELDS:
        ign.update(range(lo, hi))
    out, i = [], 0
    for k in sorted(ign):
        if out and out[-1][1] == k:
            out[-1][1] = k + 1
        else:
            out.append([k, k + 1])
    return [tuple(x) for x in out]


# Host fields: state the port keeps outside dg[] or that belongs to the
# Borland RTL / DOS environment the port replaces (none is read by game code;
# analysis/dgroup_refs.tsv lists only seg0000 = RTL as their users, or the
# module keeps a native mirror).  Ignored.
HOST_FIELDS = [
    (0x8FA0, 0x8FA2, 'RTL _8087: x87 type found by c0 (3 = 387); the port has no detection'),
    (0x918E, 0x9190, 'RTL _atexitcnt: emsHeapInit: atexit(emsHeapClose) goes to the host RTL'),
    (0x9292, 0x929E, 'RTL _exitbuf/_exitfopen/_exitopen: far code pointers into the RTL'),
    (0x94C8, 0x94CA, 'RTL c0 start-up word (stack/heap set-up)'),
    (0x9532, 0x9545, 'RTL conio _video (text window/attribute from _crtinit; no conio in the port)'),
    (0x957E, 0x959A, 'RTL delay() calibration, __brk, getch ungetch, environ far pointer'),
    (0x95E0, 0x9620, 'RTL __realcvt vectors and VROOMM overlay-manager segment table (no overlays)'),
    (0xE8CC, 0xE9CD, 'CD module request block/status: cd5737.c keeps a native mirror (struct d)'),
    (0xEA60, 0xEA80, 'RTL _fperror/harderr vectors, stdio scratch'),
]

# Clock fields: game state timed by the clock or by the host's speed (load
# times cost emulated time in DOSBox-X, none in the port's virtual time), so
# they differ under read-count sync.  Not ignored silently: reported in a
# separate 'clockdg' entry by name.
CLOCK_FIELDS = [
    (0x73D1, 0x73D5, 'voice end time (73D1, absolute clock)'),
    (0x7C38, 0x85B0, 'AMP player state (music position; started at a load-time-dependent clock)'),
    (0x85CE, 0x8620, 'DSMI timer service slots (tick counters)'),
    (0x86F8, 0x8700, 'EMS page-map save (follows GFL ring streaming)'),
    (0xBA82, 0xBA84, 's3d fade time (clock low word)'),
    (0xBBC4, 0xBBC6, 'ISOSYS turn-start timer (clock low word, iso_turn.c)'),
    (0xD534, 0xD536, 'ISOSYS AI decision clock (low word, iso_ai.c)'),
    (0xD5F2, 0xD5F4, 'GFL frame due'),
    (0xD608, 0xD61A, 'GFL file pos/start/frame times/last frames'),
    (0xDA30, 0xDA32, 'MEMMAN current EMS mapping (follows GFL streaming)'),
    (0xE798, 0xE7A4, 'DSMI time'),
    (0xE7AC, 0xE7B4, 'EMS save-state page map (follows GFL streaming)'),
]


def clock_split(rs):
    """-> (differences outside the clock fields, names of clock fields that differ)"""
    rest, names = [], []
    for lo, hi in rs:
        f = next((c for c in CLOCK_FIELDS if c[0] <= lo and hi <= c[1]), None)
        if f is None:
            rest.append((lo, hi))
        elif f[2] not in names:
            names.append(f[2])
    return rest, names


LOAD_SEG = 0x0824          # DOSBox-X load segment (tools/dosbox/ist.conf)


def reloc_ranges(pa, ob):
    """Words where the port holds a flat segment of the image (0546..16E0,
    e.g. DGROUP 16E0 in far pointers to strings) and the original the same
    segment relocated (+LOAD_SEG): equal for the comparison."""
    out = []
    for i in range(0, min(len(pa), len(ob)) - 1):
        x = pa[i] | pa[i + 1] << 8
        y = ob[i] | ob[i + 1] << 8
        if x != y and 0x0546 <= x <= 0x16E0 and y == x + LOAD_SEG:
            out.append((i, i + 2))
    return out


def farptr_ranges(pa, ob):
    """4-byte fields that are a far pointer into DOS memory in the original
    (segment 0800h..F000h, the image/heap/EMS frame) and 0 in the port, which
    keeps such pointers as native C pointers outside dg[] (DECOMPILING.md
    section 2).  Heuristic."""
    out = []
    i, n = 0, min(len(pa), len(ob)) - 3
    while i < n:
        if pa[i:i + 4] == bytes(4) and ob[i:i + 4] != bytes(4):
            seg = ob[i + 2] | ob[i + 3] << 8
            if 0x0800 <= seg <= 0xF000:
                out.append((i, i + 4))
                i += 4
                continue
        i += 1
    return out


def farptr_tail_filter(pa, ob, rs):
    """Second pass over the remaining differences: a short run (<= 2 bytes)
    that is the segment word (or its high byte) of a far pointer whose
    segment is 0 in the port and 0800h..F000h in the original (records that
    keep only the offset or a native low byte, and pointers the first pass
    took at the wrong alignment)."""
    out = []
    for lo, hi in rs:
        hit = False
        if hi - lo <= 3:
            for w in range(hi - 4, lo + 1):
                if w < 0 or w + 4 > min(len(pa), len(ob)) or not (w + 1 <= lo and hi <= w + 4):
                    continue
                seg = ob[w + 2] | ob[w + 3] << 8
                if pa[w + 2:w + 4] == bytes(2) and 0x0800 <= seg <= 0xF000:
                    hit = True
                    break
        if not hit:
            out.append((lo, hi))
    return out


def diff_ranges(a, b, ignore=()):
    out = []
    i, n = 0, min(len(a), len(b))
    ign = bytearray(n)
    for lo, hi in ignore:
        for k in range(max(0, lo), min(n, hi)):
            ign[k] = 1
    while i < n:
        if a[i] != b[i] and not ign[i]:
            j = i
            while j < n and (a[j] != b[j] and not ign[j] or j - i < 1):
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def fmt_ranges(rs, limit=12):
    s = ' '.join(f'{lo:04X}-{hi - 1:04X}' if hi - lo > 1 else f'{lo:04X}' for lo, hi in rs[:limit])
    return s + (f' ... (+{len(rs) - limit})' if len(rs) > limit else '')


def compare(pa, pb, name, ignore, png=None):
    a, b = load(pa, name), load(pb, name)
    ra, rb = regs(a['vga']), regs(b['vga'])
    res = {}
    for k in ('misc', 'seq', 'gc', 'crtc', 'attr'):
        va, vb = ra[k], rb[k]
        if isinstance(va, int):
            va, vb = bytes([va]), bytes([vb])
        if k == 'seq':          # seq 0 (reset) and index-only fields are not compared
            va, vb = va[1:], vb[1:]
        if va != vb:
            res[k] = [(i, va[i], vb[i]) for i in range(min(len(va), len(vb))) if va[i] != vb[i]]
    if ra['dac'] != rb['dac']:
        res['dac'] = [i // 3 for i in range(0, 768, 3) if ra['dac'][i:i + 3] != rb['dac'][i:i + 3]]
    wa, ha, ia, _ = render(a)
    wb, hb, ib, _ = render(b)
    if (wa, ha) != (wb, hb):
        res['size'] = ((wa, ha), (wb, hb))
    elif ia != ib:
        px = [k for k in range(len(ia)) if ia[k] != ib[k]]
        ys = sorted({k // wa for k in px})
        xs = sorted({k % wa for k in px})
        res['pixels'] = dict(count=len(px), box=(xs[0], ys[0], xs[-1], ys[-1]))
    if a['vram'] != b['vram']:
        res['vram_bytes'] = sum(1 for x, y in zip(a['vram'], b['vram']) if x != y)
    if a['dg'] and b['dg']:
        d = diff_ranges(a['dg'], b['dg'], dg_ignore_ranges(ignore, b['dg']) + reloc_ranges(a['dg'], b['dg'])
                           + farptr_ranges(a['dg'], b['dg']))
        d = farptr_tail_filter(a['dg'], b['dg'], d)
        d, clk = clock_split(d)
        if d:
            res['dg'] = d
        if clk:
            res['clockdg'] = [c.split(' (')[0] for c in clk]
    ca, cb = a['txt'].get('clock'), b['txt'].get('clock')
    if ca != cb:
        res['clock'] = (ca, cb)
    if png and (res.get('pixels') or res.get('dac') or res.get('size')):
        os.makedirs(png, exist_ok=True)
        if wa:
            write_png(os.path.join(png, f'{name}_port.png'), wa, ha, ia, ra['dac'])
        if wb:
            write_png(os.path.join(png, f'{name}_orig.png'), wb, hb, ib, rb['dac'])
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('port')
    ap.add_argument('orig')
    ap.add_argument('--png')
    ap.add_argument('--dg-ignore', default='lale')
    ap.add_argument('--names')
    ap.add_argument('--no-dg', action='store_true')
    ap.add_argument('--dg-detail', action='store_true', help='print every differing DGROUP range')
    ap.add_argument('--clock', action='store_true', help='also report clock and clock-field differences')
    args = ap.parse_args()
    names = sorted({f[:-4] for f in os.listdir(args.orig) if f.endswith('.vga')} &
                   {f[:-4] for f in os.listdir(args.port) if f.endswith('.vga')})
    if args.names:
        want = args.names.split(',')
        names = [n for n in names if n in want]
    ignore = args.dg_ignore
    for n in names:
        r = compare(args.port, args.orig, n, ignore, args.png)
        if args.no_dg:
            r.pop('dg', None)
        clk = r.pop('clockdg', None)
        if not args.clock:
            r.pop('clock', None)
        if args.clock and clk:
            r['clockdg'] = clk
        if not r:
            print(f'{n}: identical')
            continue
        if args.dg_detail and r.get('dg'):
            a, b = load(args.port, n)['dg'], load(args.orig, n)['dg']
            for lo, hi in r['dg']:
                print(f'  {lo:04X}-{hi - 1:04X} port {a[lo:hi].hex()[:40]} orig {b[lo:hi].hex()[:40]}')
        parts = []
        for k, v in r.items():
            if k == 'dg':
                parts.append(f'dg[{len(v)}]: {fmt_ranges(v)}')
            elif k == 'dac':
                parts.append(f'dac[{len(v)} entries]: {v[:8]}')
            else:
                parts.append(f'{k}: {v}')
        print(f'{n}: ' + '; '.join(parts))


if __name__ == '__main__':
    main()
