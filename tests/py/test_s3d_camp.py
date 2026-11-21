"""3DSYS camping, the encounter phrase and the date line (src/game/s3d_camp.c,
303F:4353-4F39) against the original, in the uiworld world (the panel
test's: party from GRP.DAT, panel pictures, other modules stubbed and
logged by s3d_common.install_ext).

The camp's 50 ms wait per minute spins on the clock; the port polls at its
test (poll point 303F:4903 on the original's side).  The 100 Hz clock
advances 5 ms per poll.  3D5E bit 20h is clear (no wall textures), so
s3d_redraw_view does nothing; 3EDC matches the sky so s3d_redraw_screen
does not reload it.
"""
import ctypes as C
import struct

from lale import check, lib
from uiworld import ORIG_POLL_POINTS
from test_s3d_panel import ext_run, w16, call, setup_party, ITEMS, rec, tables, map_setup

for _pt in ((0x0C7F, 0x0854), (0x05DC, 0x1E81), (0x05DC, 0x1D87), (0x303F, 0x4903)):
    if _pt not in ORIG_POLL_POINTS:
        ORIG_POLL_POINTS.append(_pt)

ESC = 0x011B
MAP_PTR = tuple(range(0xB9EC, 0xB9F0))


def base(s, tired=0, rate=0, flags=0, dflag=0):
    tables(s)
    setup_party(s, ITEMS, {1: [0x23, 0x24, 0x25]})
    cells = bytearray(15 * 16)
    cells[15 * 5 + 8] = 1                           # (1, 1) roofed
    map_setup(s, cells, 1, 1, 4)
    w16(s, 0x965C, 13)
    w16(s, 0x3D5E, flags)
    w16(s, 0x3D64, 40, 21, 100)
    w16(s, 0xBA86, rate)
    w16(s, 0xBB6B, 2)
    s.dgw(0x2E4D + 0x1D * 2, bytes([dflag]))
    for p in range(1, 5):
        mx = struct.unpack('<h', s.dg(rec(p, 0xC), 2))[0]
        w16(s, rec(p, 0x3A), mx - tired * p)
        mxa = struct.unpack('<h', s.dg(rec(p, 0xA), 2))[0]
        w16(s, rec(p, 0x38), mxa - 40 * tired)
        w16(s, rec(p, 0x22), 300 * p)
        w16(s, rec(p, 0x2A), 200 + 50 * p)


def camp(label, keys=(), moves=(), **kw):
    def setup(s):
        base(s, **kw)
    r, log = ext_run(label, lambda s: call(s, 's3d_camp'), setup, keys=keys, moves=moves,
                     ret={'fr_flag_test': 0}, every=40, ignore=MAP_PTR)
    return log


def test_camp():
    """refused (danger, not tired), short rests with and without an
    encounter, Esc, a click, a long rest"""
    camp('danger', flags=8)
    camp('not tired', tired=0)
    camp('rest 3', tired=1)
    camp('rest encounter', tired=2, rate=100)
    camp('encounters off (3D5E bit 2)', tired=2, rate=100, flags=4)
    camp('esc', tired=3, keys=[ESC])
    camp('click', tired=3, moves=[(30, 100, 100, 1), (40, 100, 100, 0)])
    camp('long', tired=3)


def phrase(label, groups, seed_calls=0):
    def setup(s):
        for i, (t, n) in enumerate(groups, 1):
            w16(s, 0x5BD8 + 6 * i, 1 if n else 0, t, n)

    def op(s):
        if s.side == 'o':
            ax, dx = s.c('s3d_make_random_phrase', 1)
            return s.dg(0xB9F8, 0x4B).split(b'\0')[0], ax
        f = lib.s3d_make_random_phrase
        f.restype = C.c_void_p
        f.argtypes = (C.c_int16,)
        from lale import DG
        a = f(1)
        return C.string_at(a), a - C.addressof(DG)
    r, _ = ext_run(label, op, setup)
    return r


def test_random_phrase():
    """one / several monsters, the plural suffixes, line breaks, type 1Fh"""
    for groups in ([(3, 1)], [(3, 2)], [(5, 4), (7, 1)], [(12, 3), (0x1F, 2)],
                   [(0x1F, 1), (9, 5), (11, 2)], [(2, 6), (4, 7)], [(14, 2), (16, 3)],
                   [(101, 3)], [(8, 1), (0, 0)]):
        phrase(f'phrase {groups}', groups)


def test_status_line():
    """the whole line, then only the changed tail (BA42), a full redraw
    (3D5E bit 10h)"""
    for (m, h, d, flags, last) in ((5, 9, 0, 0, b''), (59, 23, 359, 0, b''),
                                   (7, 13, 45, 0, b'16 \x9fubat'), (7, 13, 45, 0x10, b'xx'),
                                   (30, 2, 200, 0, b'21 temmuz pazartesi, 02:29')):
        def setup(s, m=m, h=h, d=d, flags=flags, last=last):
            base(s)
            w16(s, 0x3D64, m, h, d)
            w16(s, 0x3D5E, flags)
            s.dgw(0xBA42, last + b'\0')
        ext_run(f'status {d} {h}:{m} {flags:x} {last}',
                lambda s: call(s, 's3d_draw_status_line', 0xC6, 0xB2), setup, ignore=MAP_PTR)
