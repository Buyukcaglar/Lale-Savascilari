"""seg08D0 3D quads (src/game/q3d.c) against the original.

Harness notes
* sin/cos: Borland's sin/cos use FSIN/FCOS when _8087 (16E0:8FA0) >= 3, else
  the emulator's INT 3Eh, which the harness does not have: _8087 is set to 3.
* atan2 always goes through INT 3Eh; the harness replaces its body with
  FLD y / FLD x / FPATAN.  Unicorn (QEMU) computes FSIN/FCOS/FPATAN as host
  double sin/cos/atan2, which is what the port uses.
* q3d_draw_quad's MEMMAN calls (lock/map/unlock) are stubbed; the C side is
  q3d_draw_quad_mapped with the same two surface buffers.
"""
import ctypes

STACK_SEG, STACK_TOP = 0x0100, 0x7000    # tools/emu86.py call() stack (SS:SP at entry)
import glob
import math
import os
import random
import struct

import unicorn.x86_const as X

from lale import Emu, NAMES, ROOT, check, fn, lib, dg_write, dg_read

i16 = ctypes.c_int16
U8P = ctypes.POINTER(ctypes.c_uint8)
SZ = 0x10000


class Quad(ctypes.Structure):
    _pack_ = 1
    _fields_ = [('w', i16 * 24)]


c_side = fn('q3d_build_side_quad', None, i16, i16, i16, i16, i16, i16, i16, ctypes.POINTER(Quad))
c_front = fn('q3d_build_front_quad', None, i16, i16, i16, i16, i16, i16, i16, ctypes.POINTER(Quad))
c_window = fn('q3d_set_view_window', None, i16, i16, i16, i16)
c_rotate = fn('q3d_rotate_quad', Quad, i16, i16, ctypes.c_float, Quad)
c_clip = fn('q3d_clip_quad_near', None, ctypes.POINTER(Quad))
c_draw = fn('q3d_draw_quad_mapped', i16, U8P, i16, U8P, i16, i16, i16, i16, i16, i16, i16,
            ctypes.POINTER(Quad), i16, i16, i16, i16)
STALE = ctypes.c_double.in_dll(lib, 'q3d_stale_angle')
c_cmp = fn('fm_cmp_decode', ctypes.c_uint16, ctypes.c_char_p, ctypes.c_uint16, U8P)

WIN = 0xD930


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def make_emu():
    e = Emu()
    e.wr(0x16E0, 0x8FA0, struct.pack('<h', 3))            # _8087 = 387
    # atan2(double y, double x): push bp; mov bp,sp; fld [bp+6]; fld [bp+0Eh];
    # fpatan; pop bp; retf
    e.wr(0x0000, 0x0FDB, bytes.fromhex('55 8BEC DD4606 DD460E D9F3 5D CB'.replace(' ', '')))
    # Unicorn starts with x87 CW = 0000h: the first FLDCW (F_FTOL restoring it)
    # would switch to 24-bit precision.  The game runs with 64-bit precision
    # (Borland CW_DEFAULT 0332h; FNINIT gives 037Fh = same, all masked).
    # FNINIT; FLDCW [CS:0010] (=037Fh); HLT - Unicorn's FNINIT alone does
    # not update its softfloat precision
    e.u.mem_write(0xF7010, bytes.fromhex('7f03'))
    e.u.mem_write(0xF7000, bytes.fromhex('dbe3' '2ed92e1000' 'f4'))
    e.u.reg_write(X.UC_X86_REG_CS, 0xF700)
    e.u.reg_write(X.UC_X86_REG_IP, 0)
    e.u.emu_start(0xF7000, 0xF7007)
    return e


def quad_words(q):
    return [w & 0xFFFF for w in q]


def emu_quad(e, seg, off=0):
    return [s16(x) for x in struct.unpack('<24H', e.rd(seg, off, 0x30, runtime=True))]


def set_window(e, cx, cy, pz, f):
    data = struct.pack('<4h', pz, f, cy, cx)
    e.wr(0x16E0, WIN, data)
    dg_write(WIN, data)


def test_build_and_window():
    e = make_emu()
    r = random.Random(0xD49)
    out = e.alloc(0x40)
    for _ in range(200):
        a = [s16(r.randrange(0x10000)) if r.random() < .3 else r.randrange(-300, 300)
             for _ in range(7)]
        for name, cf in (('q3d_build_side_quad', c_side), ('q3d_build_front_quad', c_front)):
            fill = [r.randrange(-999, 999) for _ in range(24)]
            e.wr(out, 0, struct.pack('<24h', *fill), runtime=True)
            e.call(NAMES[name], *[v & 0xFFFF for v in a], e.far(out, 0))
            q = Quad((i16 * 24)(*fill))
            cf(*a, ctypes.byref(q))
            check(emu_quad(e, out) == list(q.w), f'{name}{tuple(a)}: {emu_quad(e, out)} vs {list(q.w)}')
        e.call(NAMES['q3d_set_view_window'], *[v & 0xFFFF for v in a[:4]])
        c_window(*a[:4])
        check(e.dgroup(WIN, 8) == dg_read(WIN, 8), f'set_view_window{tuple(a[:4])}')


def _angles(r):
    out = [0.0, -0.0, math.pi / 2, -math.pi / 2, math.pi, 1e-7, -1e-7, 0.25, 1.0, 3.0, -3.0,
           100.0, 1e6, math.pi / 4, 3 * math.pi / 4]
    out += [k * math.pi / 40 for k in range(-80, 81)]        # turn-animation style steps
    out += [r.uniform(-7, 7) for _ in range(400)]
    out += [r.uniform(-1e-3, 1e-3) for _ in range(50)]
    return [struct.unpack('<f', struct.pack('<f', a))[0] for a in out]


def test_rotate_quad():
    e = make_emu()
    r = random.Random(0xEA9)
    out = e.alloc(0x40)
    n = 0
    for ang in _angles(r):
        for _ in range(3):
            big = r.random() < .2
            rng = 32000 if big else 400
            q = [r.randrange(-rng, rng) for _ in range(24)]
            cx, cz = r.randrange(-rng, rng), r.randrange(-rng, rng)
            fw = struct.unpack('<2H', struct.pack('<f', ang))
            e.wr(out, 0, bytes(0x30), runtime=True)
            e.call(NAMES['q3d_rotate_quad'], e.far(out, 0), cx & 0xFFFF, cz & 0xFFFF, *fw,
                   *quad_words(q))
            got = emu_quad(e, out)
            res = c_rotate(cx, cz, ang, Quad((i16 * 24)(*q)))
            mine = list(res.w)
            # compare header + x,y,z of each vertex (px/py are stack garbage)
            idx = [0, 1, 2, 3] + [4 + 5 * v + k for v in range(4) for k in range(3)]
            ok = all(got[i] == mine[i] for i in idx)
            n += 1
            check(ok, f'rotate angle={ang!r} c=({cx},{cz}) q={q}: '
                      f'{[got[i] for i in idx]} vs {[mine[i] for i in idx]}')
    check(n > 1500, 'too few rotate cases')


def _rand_window(r):
    return (r.randrange(30, 70), r.randrange(30, 70), r.randrange(250, 340), r.randrange(80, 120))


def _scene_quad(r, win):
    """A wall quad as the turn animation builds it, rotated a little."""
    cx, cy, pz, f = win
    x, y = r.randrange(-80, 180), r.randrange(-20, 60)
    z = r.randrange(pz + f - 260, pz + f + 40)
    w, h = r.randrange(1, 234), r.randrange(1, 214)
    q = Quad()
    (c_side if r.random() < .5 else c_front)(x, y, z, r.randrange(0, 20), r.randrange(0, 10),
                                             w, h, ctypes.byref(q))
    ang = r.uniform(-math.pi / 2, math.pi / 2)
    q = c_rotate(r.randrange(0, 100), pz + f - r.randrange(0, 60), ang, q)
    return list(q.w)


def test_clip_quad_near():
    e = make_emu()
    r = random.Random(0x98C)
    out = e.alloc(0x40)
    for it in range(600):
        win = _rand_window(r)
        set_window(e, *win)
        if it % 3 == 0:
            q = [r.randrange(-400, 600) for _ in range(24)]
        else:
            q = _scene_quad(r, win)
        e.wr(out, 0, struct.pack('<24h', *q), runtime=True)
        try:
            e.call(NAMES['q3d_clip_quad_near'], e.far(out, 0))
        except Exception as ex:                 # divide error in the original: skip
            if 'divide' in str(ex) or 'INT 00' in str(ex):
                continue
            raise
        cq = Quad((i16 * 24)(*q))
        c_clip(ctypes.byref(cq))
        check(emu_quad(e, out) == list(cq.w) and e.dgroup(WIN, 8) == dg_read(WIN, 8),
              f'clip win={win} q={q}: {emu_quad(e, out)} vs {list(cq.w)}')


def _texture():
    for p in sorted(glob.glob(os.path.join(ROOT, 'extracted', '3D', 'DUVAR*.CMP'))):
        d = open(p, 'rb').read()
        w, h = d[0] | d[1] << 8, d[2] | d[3] << 8
        if w * h <= 0xF000:
            buf = (ctypes.c_uint8 * SZ)()
            c_cmp(d[4:] + bytes(512), len(d) - 4, buf)
            return w, bytes(buf)
    return None, None


def test_draw_quad():
    e = make_emu()
    r = random.Random(0x00C)
    tex_w, tex = _texture()
    check(tex is not None, 'no 3D texture (run tools/bootstrap.sh)')
    if tex is None:
        return
    SRC, DST = 0x42, 0x41
    src_seg, dst_seg, hseg = e.alloc(SZ), e.alloc(SZ), e.alloc(0x20)
    e.wr(hseg, 0, struct.pack('<HHIB', 0, 0, 0x100, 0), runtime=True)   # fake MmHandle
    state = {}

    def mm_map(em, a):
        slot = (a(0) - 0xE29F) // 8
        out = (a(3) << 4) + a(2)
        em.u.mem_write(out, struct.pack('<HH', 0, src_seg if slot == SRC else dst_seg))
        return 0, 0

    def unlock(em, a):
        state['unlocked'] = True
        return 0, 0
    e.stub('09D4:0A81', lambda em, a: (0, 0))
    e.stub('09D4:0BB4', mm_map)
    e.stub('09D4:0B22', unlock)
    csrc = (ctypes.c_uint8 * SZ)()
    cdst = (ctypes.c_uint8 * SZ)()
    ctypes.memmove(csrc, tex, SZ)
    e.wr(src_seg, 0, tex, runtime=True)
    nwords = 6 + 24 + 2 + 4
    bp = STACK_TOP - (4 + 2 * nwords) - 2
    drawn = 0
    for it in range(400):
        win = _rand_window(r)
        set_window(e, *win)
        dst_w = r.choice([100, 100, 320, 64])
        for slot, wv in ((SRC, tex_w), (DST, dst_w)):
            e.wr(0x16E0, 0xE29B + 8 * slot, struct.pack('<hhHH', wv, 100, 0, hseg))
        if it % 4 == 0:
            q = [r.randrange(-300, 400) for _ in range(24)]
        else:
            q = _scene_quad(r, win)
            cq = Quad((i16 * 24)(*q))
            c_clip(ctypes.byref(cq))           # project as the game does
            q = list(cq.w)
        if it % 7 == 0:                         # degenerate: v0 == v2 in x/z
            q[4 + 10], q[4 + 12] = q[4], q[6]
        tu, tv = r.randrange(0, 30), r.randrange(0, 30)
        w, h = q[2] if it % 4 else r.randrange(1, 234), q[3] if it % 4 else r.randrange(1, 214)
        ox, oy = r.choice([0, 0, r.randrange(-20, 20)]), r.choice([0, 0, r.randrange(-20, 20)])
        clip = (0, 0, r.choice([dst_w - 1, 99, 50]), r.choice([99, 60]))
        stale = r.uniform(-3, 3)
        e.wr(STACK_SEG, bp - 0x56, struct.pack('<d', stale), runtime=True)
        STALE.value = stale
        sentinel = bytes((i * 13 + 7) & 0xFF for i in range(SZ))
        e.wr(dst_seg, 0, sentinel, runtime=True)
        ctypes.memmove(cdst, sentinel, SZ)
        state.clear()
        args = [tu, tv, w, h, ox, oy] + q + [SRC, DST] + list(clip)
        try:
            e.call(NAMES['q3d_draw_quad'], *[v & 0xFFFF for v in args])
        except Exception as ex:
            if 'divide' in str(ex):
                continue
            raise
        rc = c_draw(ctypes.cast(csrc, U8P), tex_w, ctypes.cast(cdst, U8P), dst_w, tu, tv, w, h,
                    ox, oy, ctypes.byref(Quad((i16 * 24)(*q))), *clip)
        a = e.rd(dst_seg, 0, SZ, runtime=True)
        b = bytes(cdst)
        d = [k for k in range(SZ) if a[k] != b[k]]
        drawn += a != sentinel
        check(not d and rc == (1 if state.get('unlocked') else 0),
              f'draw it={it} win={win} q={q} tu={tu} tv={tv} w={w} h={h}: rc {rc} '
              f'unlock {state.get("unlocked")}, {len(d)} bytes differ'
              + (f' first {d[0]:04X} {a[d[0]]:02X}/{b[d[0]]:02X}' if d else ''))
    check(drawn > 100, f'only {drawn} draws touched the destination')
