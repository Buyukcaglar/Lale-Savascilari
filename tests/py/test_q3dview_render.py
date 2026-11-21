"""ovl2700 3DQUICK view composition (src/game/q3dview_render.c, 2700:3932-5F31)
against the original.

Both sides run in test_graphics' world (MEMMAN on the emulated PC, real
pictures in the surfaces).  The 3D surfaces are filled as 3DSYS's
s3d_load_district does (303F:1C7C-1F9E): wall set i of the map (map header
byte 3+i) -> surface 18h+i = name[4+type], door 1Ch+i = name[0Eh+type],
floors/ceilings 23h-4Eh from the name table at 2944h (missing names get
YER1/TAV1A), horizon 4Fh = UFUK1.  Maps are the PCn banks of FRPOS.  The
piece tables come from the original start-up sequence on both sides.  After
each operation the destination surface, DGROUP (piece tables, B394 text,
3AF8/3AF9 flags ...) and the fatal count are compared.
"""
import ctypes
import os
import random
import struct

import helpers
import test_graphics as tg
from q3dview_common import ovl2700_image
from lale import Emu, NAMES, ROOT, LOAD, check, lib, DG

C = ctypes
DGSEG = 0x16E0 + LOAD
DGA = C.addressof(DG)
DST = 0x12
KOR_PTRS = (C.c_void_p * 8).in_dll(lib, 'q3d_kor_pieces')
S3D_MAP = C.c_void_p.in_dll(lib, 's3d_map')
pc_key_push = lib.pc_key_push
pc_key_push.argtypes = [C.c_uint16]
pc_getch = lib.pc_getch
pc_getch.restype = C.c_int16


# the emulated original with the fixed ovl2700 image (test_graphics' world
# calls make_emu(c_setup=...))
def _make_emu(**kw):
    e = helpers.make_emu(emu=Emu(image=ovl2700_image()), **kw)
    e.stub('0000:29D7', lambda e, a: (pc_getch() & 0xFFFF, 0))     # getch
    return e


tg.make_emu = _make_emu


def _names():
    d = bytes(DG[0x2944:0x2944 + 13 * 0x40])
    out = {}
    for n in range(0x40):
        s = d[13 * n:13 * n + 13].split(b'\0')[0]
        if s and s[:1].isalpha():
            out[n] = s.decode().upper()
    return out


NAMES_TEX = _names()


def maps():
    d = os.path.join(ROOT, 'extracted', 'FRPOS')
    return {n: open(os.path.join(d, n), 'rb').read()[20:] for n in sorted(os.listdir(d))
            if n.startswith('PC') and not n.startswith('PCS')}


def surfs_for(m, vw, vh):
    sets = list(m[3:7])
    s = [(DST, vw, vh, 0, 'GOTY0.CMP')]
    for i, t in enumerate(sets):
        w = NAMES_TEX.get(4 + t, 'DUVAR1.CMP')
        k = NAMES_TEX.get(0x0E + t, 'KAPI1.CMP')
        pw, ph, _ = tg.pic(w)
        s.append((0x18 + i, pw, ph, 1, w))
        pw, ph, _ = tg.pic(k)
        s.append((0x1C + i, pw, ph, 1, k))
    for n in range(0x23, 0x4F):
        name = NAMES_TEX.get(n, 'YER1.CMP' if n < 0x37 else 'TAV1A.CMP')
        if name == 'BOSCH.CMP':
            name = 'YER2.CMP'
        pw, ph, _ = tg.pic(name)
        s.append((n, pw, ph, 1, name))
    s.append((0x4F, 200, 200, 1, 'UFUK1.CMP'))
    return s


class Side:
    """one side's world with a map and the piece tables"""

    def __init__(self, side, m, vw, vh, ox, hdr0):
        self.side = side
        self.w = tg.world(side, surfs=surfs_for(m, vw, vh), screen=None)
        w = self.w
        w.dgw(0x3AFF, struct.pack('<hh', vw, vh))
        w.dgw(0x3B09, struct.pack('<h', ox))
        w.dgw(0xB9EA, bytes([m[1], m[0]]))
        w.dgw(0x9F10, struct.pack('<4h', *m[3:7]))
        if side == 'o':
            e = w.e
            e.call(NAMES['q3d_precalc_table_a'], vw, vh, e.far(DGSEG, 0xB72A))
            e.call(NAMES['q3d_precalc_table_b1'])
            e.call(NAMES['q3d_precalc_table_c1'])
            seg = e.alloc(len(m) + 0x100)
            e.wr(seg, 0, m, runtime=True)
            w.dgw(0xB9EC, struct.pack('<HH', 0, seg))
        else:
            lib.q3d_precalc_table_a(C.c_int16(vw), C.c_int16(vh), C.c_void_p(DGA + 0xB72A))
            lib.q3d_precalc_table_b1()
            lib.q3d_precalc_table_c1()
            self.buf = C.create_string_buffer(m + bytes(0x100))
            S3D_MAP.value = C.addressof(self.buf)
        for n in range(8):
            # 3DSYS (303F:1CF3 / 1E0B): preset, then q3d_load_kor(texture name, slot)
            w.dgw(0xB3E8 + 14 * n, struct.pack('<5h', 1, 6, 0, 200, 200))
            if side == 'c':
                KOR_PTRS[n] = None
            t = m[3 + n % 4]
            name = NAMES_TEX.get((4 if n < 4 else 0x0E) + t, 'NOFILE.CMP').lower().encode()
            if side == 'o':
                e = w.e
                e.wr(0x16E0, 0xB3E8 + 14 * n + 10, bytes(4))
                sn = e.alloc(32)
                e.wr(sn, 0, name + b'\0', runtime=True)
                e.call(NAMES['q3d_load_kor'], *e.far(sn, 0), n)
            else:
                lib.q3d_load_kor(name, n)
            if side == 'o':
                nokor = struct.unpack('<H', w.dg(0xB3E8 + 14 * n + 10, 2))[0] == 0xB72A
            else:
                nokor = KOR_PTRS[n] == DGA + 0xB72A
            if hdr0[n] and nokor:          # no .KOR: force the strip-b path
                w.dgw(0xB3E8 + 14 * n + 4, struct.pack('<h', hdr0[n]))

    def pos(self, x, y, f):
        dx, dy = ((0, -1), (1, 0), (0, 1), (-1, 0))[f]
        self.w.dgw(0xB9F0, bytes([f]))
        self.w.dgw(0xB9F1, struct.pack('<hh', y, x))
        self.w.dgw(0xB9C4, struct.pack('<hh', dy, dx))

    def call(self, name, *args):
        if self.side == 'o':
            self.w.e.call(NAMES[name], *args)
        else:
            tg.ESI_HI.value = 0
            getattr(lib, name)(*args)


def pair(m, vw=200, vh=200, ox=0, hdr0=(0,) * 8):
    return Side('o', m, vw, vh, ox, hdr0), Side('c', m, vw, vh, ox, hdr0)


def same(label, o, c, slots=(DST,)):
    so, sc = o.w.snap(), c.w.snap()
    ro = {n: o.w.read(n) for n in slots}
    rc = {n: c.w.read(n) for n in slots}
    # the kor slots' far pointers differ (native in the port)
    for i in range(8):
        r = 0xB3E8 + 14 * i + 10
        so['dg'] = so['dg'][:r] + sc['dg'][r:r + 4] + so['dg'][r + 4:]
    so['dg'] = so['dg'][:0xB9EC] + sc['dg'][0xB9EC:0xB9F0] + so['dg'][0xB9F0:]
    tg.compare(label, so, sc, ro, rc)


def run_both(o, c, label, name, *args, slots=(DST,)):
    try:
        o.call(name, *args)
    except Exception as ex:                    # noqa: BLE001
        raise RuntimeError(f'{label}: original: {ex}') from ex
    c.call(name, *args)
    same(label, o, c, slots)


def test_render_views():
    """Whole views (q3d_render_view) of every PCn map at random and border
    positions, all facings, view offsets 0/3, with and without .KOR-style
    tables (the real .KOR files; hdr0 != 0 forced on some slots without one)."""
    r = random.Random(0x3932)
    ms = maps()
    for k, (mn, m) in enumerate(ms.items()):
        vw = 200      # 100x100 tables: the layers ignore the pieces' scale flag and
                      # draw the floor strips at texture size -> the original runs away
        hdr0 = tuple(r.choice((0, 0, 1)) for _ in range(8))
        o, c = pair(m, vw, vw, ox=(0, 3)[k % 3 == 2], hdr0=hdr0)
        W, H = m[0], m[1]
        spots = [(r.randrange(W), r.randrange(H)) for _ in range(4)] + [(0, 0), (W - 1, H - 1), (1, H // 2)]
        for x, y in spots:
            for f in range(4):
                o.pos(x, y, f)
                c.pos(x, y, f)
                run_both(o, c, f'{mn} {vw} ({x},{y}) f{f}', 'q3d_render_view', DST)


def test_layers():
    """Each layer alone, also outside the map (negative / huge positions)."""
    m = maps()['PC2']
    o, c = pair(m, 200, 200, hdr0=(1, 0, 1, 0, 0, 1, 0, 1))
    r = random.Random(0x43C9)
    pts = [(r.randrange(-4, 34), r.randrange(-4, 34)) for _ in range(12)]
    for x, y in pts:
        for f in range(4):
            o.pos(x, y, f)
            c.pos(x, y, f)
            for name in ('q3d_render_layer_far', 'q3d_render_layer_mid', 'q3d_render_layer_near',
                         'q3d_render_layer_side', 'q3d_render_layer_front'):
                run_both(o, c, f'{name} ({x},{y}) f{f}', name, DST)


def test_draw_cell_wall():
    """Every piece x side x kind x wall type, facing = side or not, hdr0 0 /
    != 0, mirrored-piece substitution (13/17/22/24), kind 3 + piece 23,
    error texts (type 0, type > 4, unset wall set)."""
    m = bytearray(maps()['PC2'])
    m[3:7] = bytes([1, 2, 0, 9])
    m = bytes(m)
    o, c = pair(m, 200, 200, hdr0=(0, 1, 0, 1, 1, 0, 1, 0))
    cell = 15 * 40
    r = random.Random(0x3969)
    cases = []
    for piece in range(0x25):
        for side in range(4):
            for kind in (0, 1, 2, 3, 4):
                for typ in (0, 1, 2, 3, 4, 5):
                    cases.append((piece, side, kind, typ))
    r.shuffle(cases)
    for piece, side, kind, typ in cases[:500] + [(p, s, k, t) for p in (13, 17, 22, 23, 24)
                                                for s in (2, 3) for k in (1, 2, 3) for t in (1, 2, 4)]:
        f = side if r.random() < 0.6 else r.randrange(4)
        for s in (o, c):
            s.pos(5, 5, f)
            mm = bytes([typ, kind])
            if s.side == 'o':
                seg = struct.unpack('<HH', s.w.dg(0xB9EC, 4))[1]
                s.w.e.wr(seg, cell + 2 * side, mm, runtime=True)
            else:
                C.memmove(C.addressof(s.buf) + cell + 2 * side, mm, 2)
        run_both(o, c, f'wall p{piece} s{side} k{kind} t{typ} f{f}', 'q3d_draw_cell_wall',
                 piece, cell, side, DST)


def test_debug_dump():
    """2700:5E7F onto slot 0 (the screen surface of the world) + getch."""
    m = maps()['PC1']
    o, c = pair(m, 200, 200)
    for s in (o, c):
        pc_key_push(0x1C0D)
        s.call('q3d_debug_dump')
    same('debug_dump', o, c, slots=(DST,))
