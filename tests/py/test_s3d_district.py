"""3DSYS district loading and the exploration screen (src/game/s3d_district.c,
303F:1927 s3d_load_district, 303F:2521 s3d_enter_view) against the
original, in the uiworld world with the real 3DQUICK view (q3dview*.c /
ovl2700) on both sides.

Files: the district map PCn and script PCSn come from FRPOS.CKD, the wall,
floor, ceiling and sky pictures from 3D.CKD: the port mounts FRPOS.CKD
besides uiworld's archives, the original's file stand-in also searches
extracted/FRPOS.  Other modules (FRPOS, DEMOLAR, ISOSYS) are stubbed on
both sides (s3d_common.install_ext).  Compared: DGROUP, the screen, the
surfaces the district loads (walls 18h-23h, floors, ceilings, sky 40h).
"""
import ctypes as C
import os
import struct

from lale import ROOT, check, lib, patch
import helpers
import uiworld
from uiworld import ORIG_POLL_POINTS
from test_s3d_panel import ext_run, w16, call, setup_party, ITEMS, SURFS as PANEL_SURFS

# the original's busy waits where the port polls: q3d_anim_door's 150 ms
# step (2700:7ECB..7ED5)
for _pt in ((0x2700, 0x7ED5),):
    if _pt not in ORIG_POLL_POINTS:
        ORIG_POLL_POINTS.append(_pt)

FRPOS_DIR = os.path.join(ROOT, 'extracted', 'FRPOS')
MAP_PTR = C.c_void_p.in_dll(lib, 's3d_map')
KOR_PTRS = (C.c_void_p * 8).in_dll(lib, 'q3d_kor_pieces')


def district_world(s):
    """per-run state of the port outside dg[]"""
    if s.side == 'c':
        # native twins of DGROUP far pointers that the world's dg_reset
        # cleared on the original's side (blocks of an earlier run's heap)
        MAP_PTR.value = None
        for i in range(8):
            KOR_PTRS[i] = None


def _mount_frpos():
    helpers.c_mount(b'3D.CKD', b'IVIR.CKD', b'FRPOS.CKD')


def with_frpos(f):
    """FRPOS.CKD mounted on the port side (in uiworld's start-up, before the
    original's heap is seeded from the port's), extracted/FRPOS searched by
    the original's file stand-in"""
    old, old_mount = list(helpers.SEARCH), uiworld.mount
    helpers.SEARCH.insert(0, FRPOS_DIR)
    uiworld.mount = _mount_frpos
    try:
        return f()
    finally:
        helpers.SEARCH[:] = old
        uiworld.mount = old_mount


# surfaces load_district fills: walls 18h-1Fh, door frames 20h-23h, floors
# 24h-37h, ceilings 38h-3Fh, sky 40h, horizon 4Fh
LOADED = list(range(0x18, 0x41)) + [0x4F]


def surfaces(s, slots):
    """the pictures of the given surface slots (None for an empty slot)"""
    out = []
    for n in slots:
        r = s.rec(n)
        out.append(s.read(n) if r and s.dg(r + 8, 1)[0] in (0, 1) else None)
    return out


def test_load_district():
    """first visit (start position, facing, BBAB/BB88) and reload, day and
    night, districts with and without a horizon picture"""
    for district, first, minute in ((0, 1, 9 * 60), (3, 0, 9 * 60), (6, 1, 23 * 60), (12, 1, 12 * 60)):
        def setup(s, minute=minute):
            district_world(s)
            setup_party(s, ITEMS, {})
            w16(s, 0x3D64, minute % 60, minute // 60, 349)
            w16(s, 0xBB6B, district)

        def op(s, district=district, first=first):
            if s.side == 'o':
                s.c('s3d_load_district', district, first)
            else:
                lib.s3d_load_district(C.c_int16(district), C.c_uint8(first))
            # far pointers that are native in the port: s3d_map (into the EMS
            # frame) and the .KOR slots' piece tables (B3E8 + 0Eh * i + 0Ah)
            s.dgw(0xB9EC, bytes(4))
            for i in range(8):
                s.dgw(0xB3E8 + 0x0E * i + 0x0A, bytes(4))
            return surfaces(s, LOADED)
        with_frpos(lambda: ext_run(f'load_district {district} first={first}', op, setup,
                                   surfs=PANEL_SURFS, rix=False))


def view_tables(s):
    """main's q3d_precalc_table_a/b1/c1 + q3d_init_tables for the view"""
    if s.side == 'o':
        s.c('q3d_precalc_table_a', 200, 200, *s.e.far(0x16E0 + 0x824, 0xB72A))
        for n in ('q3d_precalc_table_b1', 'q3d_precalc_table_c1'):
            s.c(n)
        s.c('q3d_init_tables', *s.e.far(0x16E0 + 0x824, 0xB72A))
    else:
        base = C.addressof(C.c_uint8.in_dll(lib, 'dg')) + 0xB72A
        lib.q3d_precalc_table_a(C.c_int16(200), C.c_int16(200), C.c_void_p(base))
        lib.q3d_precalc_table_b1()
        lib.q3d_precalc_table_c1()
        lib.q3d_init_tables(C.c_void_p(base))


def ui_tables(s):
    """test_intrface.setup_tables (rix, ui_init_tables, record 6 current)
    with its stand-ins of the other modules' UI callbacks on both sides
    (ext_run registered the real ones on the port side)"""
    import test_intrface as ti
    ti.setup_tables(s, 0x9824)
    if s.side == 'c':
        for (sg, of), f in ti._C_STUBS.items():
            ti.c_register(sg, of, C.cast(f, C.c_void_p))


def view_setup(district, minute, big):
    """party, clock, window size, 3D tables, UI tables for s3d_enter_view"""
    def setup(s):
        district_world(s)
        setup_party(s, ITEMS, {})
        w16(s, 0x3D64, minute % 60, minute // 60, 349)
        w16(s, 0xBB6B, district)
        s.dgw(0xBB34, bytes([0 if big else 1, 5, 0, 0, 0, 0, 1 if big else 0]))
        w16(s, 0xBB4D, 2)                           # no ampSetTempo (no music)
        # main's 3D set-up (istanbul.c): 200x200 tables; the small window
        # (PgUp) only halves the window
        w16(s, 0x3AFB, 200 if big else 100, 200 if big else 100, 200, 200)
        view_tables(s)
        ui_tables(s)
        if s.side == 'c':
            # the original reaches s3d_draw_portrait_hl only through its
            # thunk 1670:0084, which ui_tables stubs (shape 2Ch); the port's
            # direct calls get the same stand-in
            patch('s3d_draw_portrait_hl', lambda: 0x2C, C.c_int16)
        call(s, 's3d_load_ui_images')                # compass 15h, interface 50h ...
    return setup


def enter(s, district):
    if s.side == 'o':
        s.c('s3d_load_district', district, 1)
        s.c('s3d_enter_view')
    else:
        lib.s3d_load_district(C.c_int16(district), C.c_uint8(1))
        lib.s3d_enter_view()


def clear_native_ptrs(s):
    """far pointers that are native in the port: s3d_map, the .KOR tables"""
    s.dgw(0xB9EC, bytes(4))
    for i in range(8):
        s.dgw(0xB3E8 + 0x0E * i + 0x0A, bytes(4))


def test_enter_view():
    """a district loaded (first visit), then the exploration screen built:
    palette, interface, party panel, the rendered 3D view, status line,
    daylight, the fade in; small and big view, day and night"""
    for district, minute, big in ((0, 9 * 60, 1), (3, 23 * 60, 0)):
        def op(s, district=district):
            enter(s, district)
            clear_native_ptrs(s)
            return surfaces(s, (0x40, 0x50))
        with_frpos(lambda: ext_run(f'enter_view {district} big={big}', op,
                                   view_setup(district, minute, big), surfs=PANEL_SURFS, rix=True))


def map_cell(s, off, data=None):
    """bytes of the district map at off (read, or write data)"""
    if s.side == 'o':
        o, sg = struct.unpack('<HH', s.dg(0xB9EC, 4))
        if data is None:
            return bytes(s.e.rd(sg, (o + off) & 0xFFFF, 15, runtime=True))
        s.e.wr(sg, (o + off) & 0xFFFF, data, runtime=True)
        return None
    if data is None:
        return C.string_at(MAP_PTR.value + off, 15)
    C.memmove(MAP_PTR.value + off, data, len(data))
    return None


STEPS = [(0, -1), (1, 0), (0, 1), (-1, 0)]


def test_try_move_animated():
    """s3d_try_move's animating paths in a rendered district (test_s3d_move
    covers the others): a breakable wall (kind 3) walked into and looked at
    sideways, a door (kind 2), passages (type 9, kinds 1 and 2)"""
    # (label, wall side relative to the facing, type slot (None: keep;
    # 'nine': a slot made type 9), kind, step through that side)
    cases = [('break fwd', 0, None, 3), ('break side', 1, None, 3), ('door', 0, None, 2),
             ('passage 1', 0, 'nine', 1), ('passage 2', 0, 'nine', 2)]
    for label, rel, slot, kind in cases:
        def op(s, rel=rel, slot=slot, kind=kind):
            enter(s, 0)
            facing = s.dg(0xB9F0, 1)[0] % 4
            y, x = struct.unpack('<hh', s.dg(0xB9F1, 4))
            w = s.dg(0xB9EB, 1)[0]
            off = (y * w + x) * 15
            side = (facing + rel) % 4
            c = bytearray(map_cell(s, off))
            if slot == 'nine':
                w16(s, 0x9F10, 9)                    # KOR slot 1 is a passage
                c[2 * side] = 1
            elif struct.unpack('<h', s.dg(0x9F10 + 2 * (c[2 * side] - 1), 2))[0] in (8, 9):
                c[2 * side] = 1 if struct.unpack('<h', s.dg(0x9F10, 2))[0] not in (8, 9) else 2
            c[2 * side + 1] = kind
            map_cell(s, off, bytes(c))
            dx, dy = STEPS[side]
            if s.side == 'o':
                r = s.c('s3d_try_move', dx & 0xFFFF, dy & 0xFFFF, side)[0] & 0xFF
            else:
                f = lib.s3d_try_move
                f.restype = C.c_uint8
                f.argtypes = (C.c_int16, C.c_int16, C.c_uint8)
                r = f(dx, dy, side)
            cells = map_cell(s, off)
            clear_native_ptrs(s)
            return r, cells, surfaces(s, (0x41, 0x42))
        with_frpos(lambda: ext_run(f'try_move {label}', op, view_setup(0, 9 * 60, 1),
                                   surfs=PANEL_SURFS, rix=True))


def test_show_location():
    """the 9 thirds of the map (message box text), and with event flag 52h
    (a city map owned) the district's map picture har<n>.cmp first; a
    district without a map picture"""
    cases = [(1, x, y, 0) for x in (1, 10, 19) for y in (2, 15, 28)] + [(1, 10, 15, 1), (0, 3, 3, 1)]
    for district, x, y, flag in cases:
        def setup(s, district=district, x=x, y=y, flag=flag):
            district_world(s)
            ui_tables(s)
            w16(s, 0xBB6B, district)
            s.dgw(0xB9EA, bytes([30, 21]))              # map h, w
            w16(s, 0xB9F1, y, x)
            s.dgw(0xB9CA + 0x52 // 8, bytes([flag << (0x52 % 8)]))

        def op(s):
            call(s, 's3d_show_location')
        with_frpos(lambda: ext_run(f'show_location {district} ({x},{y}) flag={flag}', op, setup,
                                   keys=(0x1C0D, 0x1C0D, 0x1C0D), every=40, surfs=PANEL_SURFS))
