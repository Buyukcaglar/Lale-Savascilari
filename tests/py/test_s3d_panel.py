"""3DSYS party panel, UI pictures, clock and day/night (src/game/s3d_panel.c,
303F:2C69-3DCF, 75EF) against the original, in the uiworld world.

* Party: orig/GRP.DAT records (test_s3d_charscr.setup_party) with test
  inventories / hands (the status icons are items in hand).
* Surfaces: 47h-4Ah portraits, 4Bh item icons, 4Eh bars (DISDUNYA.CMP),
  50h the interface (PCMAINTH.CMP); the screen is A000.
* Other modules stubbed on both sides (s3d_common.install_ext); the hook
  logs the calls, iso_calc_stat_a stores a value in D5EE.
* The map for s3d_calc_daylight is a conventional buffer (16E0:B9EC /
  s3d_map, as test_frpos.py).
"""
import ctypes as C
import struct
import sys

from lale import check, lib
import uiworld
from uiworld import run, ORIG_POLL_POINTS

# the original's busy loops where the port polls: INTRFACE's (as
# test_intrface.py), gr_fade, s3d_random_view_effect, the camp minute
for _pt in [(0x05DC, o) for o in (0x1415, 0x145C, 0x151D, 0x1564, 0x1D87, 0x1E81, 0x2BFA, 0x2E2D)] +         [(0x0C7F, 0x0854), (0x303F, 0x07A3), (0x303F, 0x4903)]:
    if _pt not in ORIG_POLL_POINTS:
        ORIG_POLL_POINTS.append(_pt)
from s3d_common import install_ext, uninstall_ext, ExtLog
from test_s3d_charscr import setup_party, ITEMS, RIX

SURFS = uiworld.SURFS + [(0x46 + i, 32, 32, 1, f'IKON{i}.CMP') for i in range(1, 5)] + \
    [(0x4E, 320, 200, 1, 'DISDUNYA.CMP'), (0x50, 320, 200, 1, 'PCMAINTH.CMP'),
     (9, 0x5A, 0x0F, 0, 'ITEMBANK.CMP'), (0x0A, 0x0F, 0x0F, 0, 'ITEMBANK.CMP'),
     (0x40, 680, 114, 1, 'GOTY0.CMP')]

_keep = []


def w16(s, off, *v):
    s.dgw(off, struct.pack('<%dh' % len(v), *v))


def call(s, name, *args, restype=None):
    """name(*args) on side s; the 16-bit result when restype is given"""
    if s.side == 'o':
        r = s.c(name, *args)
        return r[0] & 0xFFFF if restype else None
    f = getattr(lib, name)
    f.restype = restype
    r = f(*args)
    return r & 0xFFFF if restype else None


def ext_run(label, op, setup=None, ret=None, effect=None, keys=(), moves=(), clock_step=5,
            every=8, ignore=(), surfs=None, rix=True):
    logs = []

    def su(s):
        # surface 52h = the .RIX blend table of gr_draw_strip (16E0:E52F)
        if rix:
            s.surf(0x52, 0x100, 0xC0, 1)
            s.write(0x52, (RIX + bytes(0xC000))[:0xC000])
        if s.side == 'o':
            s.c('ogr_mouse_init')
        else:
            lib.ogr_mouse_init()
            lib.s3d_register_far_fns()
            lib.demo_register_far_fns()      # the other modules' far entries
            lib.fr_register_far_fns()
            lib.iso_register_far_fns()
        if setup:
            setup(s)
        h = ExtLog(ret, effect)
        logs.append(h.log)
        install_ext(s, h)

    def o(s):
        try:
            return op(s)
        finally:
            if s.side == 'c':
                uninstall_ext()
                restore_far_fns()

    r = run(label, o, setup=su, surfs=surfs or SURFS, keys=keys, moves=moves, clock_step=clock_step,
            every=every, ignore=ignore)
    check(logs[0] == logs[1], f'{label}: ext calls {logs[0][:6]} vs {logs[1][:6]}')
    return r, logs[0]


def tables(s):
    """ui_init_tables and window record 6 (the exploration screen's) made
    current, as s3d_enter_view does"""
    call(s, 'ui_init_tables')
    s.dgw(0x962C, s.dg(0x9674 + 6 * 0x48, 0x48))


def restore_far_fns():
    """The far-function registry is global in the library: give the
    addresses this module registered (the *_register_far_fns of the game modules)
    back to test_intrface's logging stand-ins when that module is loaded
    (later tests in the same run rely on them)."""
    ti = sys.modules.get('test_intrface')
    if ti is not None:
        for (sg, of), f in ti._C_STUBS.items():
            ti.c_register(sg, of, C.cast(f, C.c_void_p))


def stat_a(args, s):
    p = struct.unpack('<h', s.dg(0x2898, 2))[0]
    w16(s, 0xD5EE, 40 + 7 * p)


def panel(label, pre=None, times=1):
    def setup(s):
        setup_party(s, ITEMS, {1: [0x23, 0x24, 0x25], 2: [0x23, 0, 0], 4: [0x2B, 0x2C, 0]})
        w16(s, 0x9650, 0x00D9, 0x05DC + 0x824)      # shape callback = ui_get_9671
        if pre:
            pre(s)

    def op(s):
        for _ in range(times):
            call(s, 's3d_draw_party_panel')
    ext_run(label, op, setup, effect={'iso_calc_stat_a': stat_a})


def rec(p, off):
    return 0x99A6 + 0x5C * p + off


def test_party_panel():
    """full redraw (3D5E bit 10h), then incremental ones with changed
    values; dead / unconscious members; moving party (portrait jitter)"""
    panel('full', lambda s: w16(s, 0x3D5E, 0x10))
    panel('cache empty')
    panel('twice', times=2)

    def changes(s):
        w16(s, rec(2, 0x3A), -6)                    # dead
        w16(s, rec(3, 0x3A), 0)                     # unconscious
        w16(s, rec(4, 0x22), 900)                   # bar > 500
        w16(s, rec(1, 0x2A), 0)
        s.dgw(0xBA88 + 0x11 * 3, b'\x00\x01')        # was unconscious, redraw
    panel('states', changes)
    panel('states full', lambda s: (changes(s), w16(s, 0x3D5E, 0x10)))
    panel('dark', lambda s: (changes(s), s.dgw(0x5BC2, b'\x01')))

    def moving(s):
        w16(s, 0xB9F3, 5, 6)                        # x, y (B9F3, then B9F5 unused)
        w16(s, 0xBB51, 4)
    panel('moving', moving, times=6)


def test_clock():
    """minutes / hours / days roll over; the timed status lists count down
    and drop the expired entries"""
    def setup(s):
        w16(s, 0x3D64, 50, 23, 359)
        for p in range(1, 5):
            w16(s, 0x99D2 + 0x5C * p + 2, *[100 * p * k - 150 for k in range(1, 6)])
            w16(s, 0xA45E + 0x28 * p + 2, *[300 * k for k in range(5, 0, -1)])
    for m in (1, 2, 15, 70, 24 * 60):
        # no return value (AX: the original's DGROUP segment, the port's 0)
        ext_run(f'advance {m}', lambda s, m=m: call(s, 's3d_advance_clock', m), setup)


def map_setup(s, cells, x, y, w):
    buf = bytes(cells)
    if s.side == 'o':
        seg = s.e.alloc(len(buf))
        s.e.wr(seg, 0, buf, runtime=True)
        s.dgw(0xB9EC, struct.pack('<HH', 0, seg))
    else:
        b = C.create_string_buffer(buf, len(buf))
        _keep.append(b)
        C.c_void_p.in_dll(lib, 's3d_map').value = C.addressof(b)
    s.dgw(0xB9EA, bytes([4, w]))
    w16(s, 0xB9F3, x)
    w16(s, 0xB9F1, y)


def test_daylight():
    """s3d_calc_daylight / s3d_apply_daylight over hours, roofed cells and
    the previous state bits"""
    cells = bytearray(15 * 16)
    cells[15 * 5 + 8] = 1                           # (1, 1): roof
    cells[15 * 6 + 10] = 1                          # (2, 1): byte 10
    for hour in (3, 7, 12, 17, 18):
        for (x, y) in ((1, 1), (2, 1), (0, 0)):
            for prev in (0, 1, 2, 3, 0x13, 0x1B):
                def setup(s, hour=hour, x=x, y=y, prev=prev):
                    map_setup(s, cells, x, y, 4)
                    w16(s, 0x3D66, hour)
                    w16(s, 0x3D5E, prev)
                    w16(s, 0x3D6E, 7, 11, 13)
                    w16(s, 0x731E, 60, 50)
                ext_run(f'daylight h{hour} ({x},{y}) {prev:x}',
                        lambda s: [call(s, 's3d_calc_daylight', restype=C.c_uint16) & 0xFFFF,
                                   call(s, 's3d_apply_daylight')][0],
                        setup, ignore=range(0xB9EC, 0xB9F0))


def test_load_ui_images():
    """the seven pictures and the compass copy (surface 15h)"""
    def op(s):
        call(s, 's3d_load_ui_images')
        return [s.read(n) for n in (0x15, 0x50, 0x53, 0x51, 0x4D, 0x4C, 0x4B, 0x4E)]
    ext_run('ui images', op)


def test_portrait_hl():
    """cursor shape over the view thirds / elsewhere; zones re-created when
    the view size changed"""
    for win in (100, 200):
        for (x, y) in ((10, 10), (60, 40), (100, 40), (140, 40), (100, 150), (300, 150)):
            def setup(s, win=win, x=x, y=y):
                w16(s, 0x3AFB, win, win)
                s.dgw(0x9671, b'\x07')
                w16(s, 0x965C, 13)
                w16(s, 0xE5E7, y, x)                # cursor y, x
                if win == 200:
                    call(s, 's3d_draw_portrait')
            ext_run(f'hl win {win} at {x},{y}',
                    lambda s: call(s, 's3d_draw_portrait_hl', restype=C.c_int16),
                    setup)


def test_stub_392d():
    ext_run('392D', lambda s: call(s, 's3d_stub_392D'))
