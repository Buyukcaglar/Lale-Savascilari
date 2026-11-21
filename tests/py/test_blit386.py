"""asm2599 (hand-written 386 blits, src/engine/blit386.c) against the original."""
import ctypes
import glob
import os
import random

from lale import DG, Emu, NAMES, ROOT, check, fn, lib

U8P = ctypes.POINTER(ctypes.c_uint8)
u16 = ctypes.c_uint16
u8 = ctypes.c_uint8
SZ = 0x10000

c_flip = fn('asm_blit_scaled_key_flip', None, U8P, u16, u16, u16, u16, u16, u16, U8P,
            u16, u16, u16, u16, u16, u16, u8, u8)
c_scaled = fn('asm_blit_scaled_key', None, U8P, u16, u16, u16, u16, u16, u16, U8P,
              u16, u16, u16, u16, u16, u16, u16, u8)
c_span = fn('asm_span_scaled_key', None, U8P, U8P, u16, u16, u8)
c_memset = fn('asm_memset32', None, U8P, ctypes.c_uint32, u16)
c_memcpy = fn('asm_memcpy32', None, U8P, U8P, u16)
c_rect = fn('asm_copy_rect', ctypes.c_int16, U8P, u16, u16, u16, u16, u16, U8P, u16, u16, u16)
c_rect_key = fn('asm_copy_rect_key', ctypes.c_int16, U8P, u16, u16, u16, u16, u16, U8P,
                u16, u16, u16, u8)
c_rect_mir = fn('asm_copy_rect_key_mirror', ctypes.c_int16, U8P, u16, u16, u16, u16, u16,
                U8P, u16, u16, u16, u8)
c_tint = fn('asm_tint_rect', None, U8P, U8P, u16, u16, u16, u16, u16, u8)
c_blend = fn('asm_blend_span', None, U8P, U8P, U8P, U8P, u16)
c_upscale = fn('asm_upscale_view2x', None, U8P, U8P)
c_cmp = fn('fm_cmp_decode', u16, ctypes.c_char_p, u16, U8P)
ESI_HI = ctypes.c_uint16.in_dll(lib, 'asm_esi_hi')
A000 = (ctypes.c_uint8 * SZ).in_dll(lib, 'pc_a000')


class Bufs:
    """n 64 KB buffers, each both in the emulator (seg:0000) and in C."""

    def __init__(self, e, n, small=()):
        e._heap = 0x6200
        self.e = e
        # buffers listed in `small` are 32 KB in the emulator (blend tables:
        # index <= 32639); the scratch heap holds only three 64 KB blocks
        self.segs = [e.alloc(0x8000 if i in small else SZ) for i in range(n)]
        self.small = small
        self.c = [(ctypes.c_uint8 * SZ)() for _ in range(n)]

    def set(self, i, data):
        data = bytes(data).ljust(SZ, b'\0')[:SZ]
        self.e.wr(self.segs[i], 0, data, runtime=True)
        ctypes.memmove(self.c[i], data, SZ)

    def far(self, i):
        return self.e.far(self.segs[i], 0)

    def p(self, i):
        return ctypes.cast(self.c[i], U8P)

    def same(self, i):
        a = self.e.rd(self.segs[i], 0, SZ, runtime=True)
        b = bytes(self.c[i])
        if a == b:
            return True, ''
        d = [k for k in range(SZ) if a[k] != b[k]]
        return False, f'{len(d)} bytes differ, first at {d[0]:04X}: orig {a[d[0]]:02X} C {b[d[0]]:02X}'


def rnd_bytes(r, n, alphabet=None):
    if alphabet:
        return bytes(r.choice(alphabet) for _ in range(n))
    return r.randbytes(n)


def sentinel():
    return bytes((0xA5 ^ (i * 7)) & 0xFF for i in range(SZ))


def cmp_pictures(limit):
    """Decoded real CMP pictures [(w, h, pixels)] that fit in 64 KB."""
    out = []
    files = sorted(glob.glob(os.path.join(ROOT, 'extracted', '*', '*.CMP')))
    for path in files:
        d = open(path, 'rb').read()
        w, h = d[0] | d[1] << 8, d[2] | d[3] << 8
        if not (0 < w * h <= 0xF000):
            continue
        buf = (ctypes.c_uint8 * 0x10000)()
        src = d[4:] + bytes(512)
        c_cmp(src, len(d) - 4, buf)
        out.append((w, h, bytes(buf[:w * h])))
        if len(out) >= limit:
            break
    return out


def _scaled_case(e, b, kind, a, esi_hi):
    """a: dict sx sy sw sh sp dx dy dw dh dp key flags."""
    regs = {'ESI': esi_hi << 16, 'ECX': 0}
    # CS:0000-0017 of segment 2599 = DS:EB90-EBA7 at run time (CS:0018 is
    # this code): the same bytes on both sides before
    work = bytes((0x3C + 11 * i) & 0xFF for i in range(0x18))
    e.wr(0x16E0, 0xEB90, work)
    DG[0xEB90:0xEBA8] = work
    if kind == 'flip':
        e.call(NAMES['asm_blit_scaled_key_flip'], b.far(0), a['sx'], a['sy'], a['sw'], a['sh'],
               a['sp'], 0x1234, b.far(1), a['dx'], a['dy'], a['dw'], a['dh'], a['dp'], 0x5678,
               a['key'], a['flags'], regs=regs)
        ESI_HI.value = esi_hi
        c_flip(b.p(0), a['sx'], a['sy'], a['sw'], a['sh'], a['sp'], 0x1234, b.p(1), a['dx'],
               a['dy'], a['dw'], a['dh'], a['dp'], 0x5678, a['key'], a['flags'])
    else:
        e.call(NAMES['asm_blit_scaled_key'], b.far(0), a['sx'], a['sy'], a['sw'], a['sh'],
               a['sp'], 0x1234, b.far(1), a['dx'], a['dy'], a['dw'], a['dh'], a['dp'], 0x5678,
               0x9ABC, a['key'], regs=regs)
        ESI_HI.value = esi_hi
        c_scaled(b.p(0), a['sx'], a['sy'], a['sw'], a['sh'], a['sp'], 0x1234, b.p(1), a['dx'],
                 a['dy'], a['dw'], a['dh'], a['dp'], 0x5678, 0x9ABC, a['key'])
    ok, msg = b.same(1)
    check(ok, f'{kind} {a} esi_hi={esi_hi:04X}: {msg}')
    hi = e.reg('ESI') >> 16
    check(hi == ESI_HI.value, f'{kind} {a}: esi_hi orig {hi:04X} C {ESI_HI.value:04X}')
    o, c = e.dgroup(0xEB90, 0x18), bytes(DG[0xEB90:0xEBA8])
    check(o == c, f'{kind} {a}: work variables DS:EB90 orig {o.hex()} C {c.hex()}')


def test_scaled_blits():
    e = Emu()
    b = Bufs(e, 2)
    r = random.Random(2599)
    pics = cmp_pictures(6)
    check(len(pics) >= 3, 'no CMP pictures (run tools/bootstrap.sh)')
    for kind in ('flip', 'plain'):
        for it in range(70):
            if it < len(pics):
                w, h, pix = pics[it]
                b.set(0, pix)
                a = dict(sx=0, sy=0, sw=w, sh=h, sp=w)
            else:
                b.set(0, rnd_bytes(r, SZ, [0xFE, 1, 2, 3, 0x80] if it & 1 else None))
                sp = r.randrange(1, 320)
                a = dict(sx=r.randrange(0, 40), sy=r.randrange(0, 40), sw=r.randrange(0, 200),
                         sh=r.randrange(0, 120), sp=sp)
            a.update(dx=r.randrange(0, 50), dy=r.randrange(0, 30), dw=r.randrange(1, 260),
                     dh=r.randrange(1, 150), dp=r.choice([320, 100, 256, 300]),
                     key=r.choice([0xFE, 0, 1]), flags=r.choice([0, 1, 2, 3]))
            b.set(1, sentinel())
            _scaled_case(e, b, kind, a, r.choice([0, 0, 0x8000, r.randrange(0x10000)]))
    # edge: 1x1, dw > sw (upscale), sw = 0
    for sw, sh, dw, dh in ((1, 1, 1, 1), (3, 2, 300, 190), (0, 0, 7, 5), (319, 199, 1, 1)):
        b.set(0, rnd_bytes(r, SZ))
        b.set(1, sentinel())
        a = dict(sx=1, sy=1, sw=sw, sh=sh, sp=320, dx=2, dy=3, dw=dw, dh=dh, dp=320, key=0xFE,
                 flags=1)
        _scaled_case(e, b, 'flip', a, 0)
        b.set(1, sentinel())
        _scaled_case(e, b, 'plain', a, 0x1234)


def test_span_scaled():
    e = Emu()
    b = Bufs(e, 2)
    r = random.Random(234)
    for _ in range(60):
        b.set(0, rnd_bytes(r, SZ, [0xFE, 7, 9]))
        b.set(1, sentinel())
        sw, dw, key = r.randrange(0, 400), r.randrange(1, 400), r.choice([0xFE, 7])
        e.call(NAMES['asm_span_scaled_key'], b.far(0), b.far(1), sw, dw, key,
               regs={'ESI': 0xDEAD0000})
        c_span(b.p(0), b.p(1), sw, dw, key)
        ok, msg = b.same(1)
        check(ok, f'span sw={sw} dw={dw}: {msg}')
        check(e.reg('ESI') >> 16 == ESI_HI.value, f'span sw={sw} dw={dw}: esi_hi')


def test_memset_memcpy():
    e = Emu()
    b = Bufs(e, 2)
    r = random.Random(28)
    for count in (0, 1, 2, 3, 4, 5, 7, 8, 63, 64000, 0xFFF0) + tuple(r.randrange(1, 3000) for _ in range(10)):
        v = r.randrange(1 << 32)
        b.set(0, sentinel())
        e.call(NAMES['asm_memset32'], b.far(0), [v & 0xFFFF, v >> 16], count)
        c_memset(b.p(0), v, count)
        ok, msg = b.same(0)
        check(ok, f'memset32 count={count}: {msg}')
    for count in (0, 1, 3, 4, 6, 9, 64000) + tuple(r.randrange(1, 3000) for _ in range(10)):
        b.set(0, rnd_bytes(r, SZ))
        b.set(1, sentinel())
        e.call(NAMES['asm_memcpy32'], b.far(1), b.far(0), count)
        c_memcpy(b.p(1), b.p(0), count)
        ok, msg = b.same(1)
        check(ok, f'memcpy32 count={count}: {msg}')
    # overlapping copies inside one buffer (dst = src + k)
    for k in (1, 2, 3, 5, -1, -3):
        b.set(0, rnd_bytes(r, SZ))
        so, do = 1000, 1000 + k
        e.call(NAMES['asm_memcpy32'], e.far(b.segs[0], do), e.far(b.segs[0], so), 37)
        c_memcpy(ctypes.cast(ctypes.byref(b.c[0], do), U8P),
                 ctypes.cast(ctypes.byref(b.c[0], so), U8P), 37)
        ok, msg = b.same(0)
        check(ok, f'memcpy32 overlap {k}: {msg}')


def test_copy_rects():
    e = Emu()
    b = Bufs(e, 2)
    r = random.Random(0x2D0)
    pics = cmp_pictures(4)
    cases = []
    for w, h, pix in pics:
        cases.append((pix, dict(sx=0, sy=0, w=w, h=h, sp=w, dx=5, dy=3, dp=320)))
    for _ in range(40):
        cases.append((rnd_bytes(r, SZ, [0, 0xFE, 5, 6]),
                      dict(sx=r.randrange(0, 64), sy=r.randrange(0, 64), w=r.randrange(0, 90),
                           h=r.randrange(0, 60), sp=r.randrange(1, 320), dx=r.randrange(0, 64),
                           dy=r.randrange(0, 64), dp=r.randrange(1, 320))))
    # overflow error (sy*spitch > FFFFh) and h = 0
    cases.append((bytes(SZ), dict(sx=0, sy=300, w=4, h=4, sp=320, dx=0, dy=0, dp=320)))
    cases.append((bytes(SZ), dict(sx=0, sy=0, w=4, h=4, sp=320, dx=0, dy=300, dp=320)))
    cases.append((rnd_bytes(r, SZ), dict(sx=0, sy=0, w=4, h=0, sp=320, dx=0, dy=0, dp=320)))
    for pix, a in cases:
        for name, cf in (('asm_copy_rect', c_rect), ('asm_copy_rect_key', c_rect_key),
                         ('asm_copy_rect_key_mirror', c_rect_mir)):
            if name == 'asm_copy_rect_key' and a['w'] == 0 and a['h']:
                continue                       # 64 KB per row: tested separately
            key = r.choice([0, 0xFE, 5])
            b.set(0, pix)
            b.set(1, sentinel())
            args = [a['sx'], a['sy'], a['w'], a['h'], a['sp']]
            dargs = [a['dx'], a['dy'], a['dp']]
            extra = [] if name == 'asm_copy_rect' else [key]
            ax = e.call(NAMES[name], b.far(0), *args, b.far(1), *dargs, *extra)[0]
            rc = cf(b.p(0), *args, b.p(1), *dargs, *extra) & 0xFFFF
            ok, msg = b.same(1)
            if name == 'asm_copy_rect_key' and rc == 0:
                ok, msg = True, ''             # error path: DS corrupted, compare ax only
            check(ok and ax == rc, f'{name} {a} key={key}: ax {ax:04X} vs {rc:04X} {msg}')
    # copy_rect_key with w = 0: 65536 bytes per row, one row
    b.set(0, rnd_bytes(r, SZ, [0, 1, 2]))
    b.set(1, sentinel())
    ax = e.call(NAMES['asm_copy_rect_key'], b.far(0), 0, 0, 0, 1, 0, b.far(1), 0, 0, 0, 1)[0]
    rc = c_rect_key(b.p(0), 0, 0, 0, 1, 0, b.p(1), 0, 0, 0, 1) & 0xFFFF
    ok, msg = b.same(1)
    check(ok and ax == rc, f'copy_rect_key w=0: {msg}')


def test_tint_blend():
    e = Emu()
    b = Bufs(e, 4, small=(2,))
    r = random.Random(0x440)
    table = rnd_bytes(r, SZ)
    b.set(2, table)
    for _ in range(40):
        b.set(0, rnd_bytes(r, SZ, None if r.random() < .5 else [3, 9, 200, 255, 0]))
        w, h = r.choice([0, 1, 2, r.randrange(0, 120)]), r.choice([0, 1, r.randrange(0, 300)])
        x, y, pitch, col = r.randrange(0, 100), r.randrange(0, 50), r.choice([320, 100, 200]), \
            r.choice([0, 3, 255, r.randrange(256)])
        e.call(NAMES['asm_tint_rect'], b.far(0), b.far(2), x, y, w, h, pitch, col)
        c_tint(b.p(0), b.p(2), x, y, w, h, pitch, col)
        ok, msg = b.same(0)
        check(ok, f'tint x={x} y={y} w={w} h={h} p={pitch} c={col}: {msg}')
    for count in (0, 1, 2, 17, 320, 64000) + tuple(r.randrange(1, 2000) for _ in range(10)):
        alpha = None if r.random() < .5 else [0, 1, 254, 255, 100]
        b.set(0, rnd_bytes(r, SZ, alpha))
        b.set(1, rnd_bytes(r, SZ, alpha))
        b.set(3, sentinel())
        e.call(NAMES['asm_blend_span'], b.far(0), b.far(1), b.far(3), b.far(2), count)
        c_blend(b.p(0), b.p(1), b.p(3), b.p(2), count)
        ok, msg = b.same(3)
        check(ok, f'blend_span count={count}: {msg}')


def test_upscale_view2x():
    e = Emu()
    b = Bufs(e, 2)
    r = random.Random(0x500)
    b.set(1, rnd_bytes(r, SZ))                 # blend table
    for it in range(8):
        alpha = [0xFE, 0xFE, 1, 2, 3, 200] if it & 1 else None
        view = rnd_bytes(r, 10000, alpha)
        if it == 0:
            view = bytes(10000)
        b.set(0, view)
        screen = rnd_bytes(r, SZ, [0xFE, 4, 5, 6] if it & 2 else None)
        e.wr(0xA000, 0, screen, runtime=True)
        ctypes.memmove(A000, screen, SZ)
        e.call(NAMES['asm_upscale_view2x'], b.far(0), b.far(1))
        c_upscale(b.p(0), b.p(1))
        a, c = e.rd(0xA000, 0, SZ, runtime=True), bytes(A000)
        # A000:EFxx is the emulator's stack (SS:SP = 9F00:FFF0); the
        # routine neither reads nor writes A000:EF80-EFFF (row 191, x 192..319)
        d = [k for k in range(SZ) if a[k] != c[k] and not 0xEF80 <= k < 0xF000]
        check(not d, f'upscale it={it}: {len(d)} bytes differ' + (f', first {d[0]:04X}' if d else ''))
