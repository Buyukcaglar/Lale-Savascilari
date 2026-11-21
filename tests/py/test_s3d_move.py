"""3DSYS movement (src/game/s3d_move.c, 303F:0000 s3d_try_move, 0BDB-ish
s3d_scan_cells) against the original, in the uiworld world.

A synthetic 6x6 map (15 bytes per cell: [2i] wall type, [2i+1] wall kind
for the four sides, [8] roof ...) in a conventional buffer (16E0:B9EC /
s3d_map); the wall-set types (9F10) are set per test.  FRPOS's
fr_run_event is stubbed and logged on both sides (s3d_common), and can
cancel the move (BBA8).  Paths that animate (door, passage, breakable
wall) need a rendered district: test_s3d_district.test_try_move_animated.
"""
import ctypes as C
import struct

from lale import lib
from test_s3d_panel import ext_run, w16, map_setup, _keep

W = 6


def make_map():
    cells = bytearray(15 * W * W)
    for y in range(W):
        for x in range(W):
            c = 15 * (y * W + x)
            for i in range(4):
                cells[c + 2 * i] = 1 + (x + y + i) % 4       # wall type slot 1..4
                cells[c + 2 * i + 1] = (x * 3 + y + i) % 4   # kind 0..3
    return cells


def map_read(s):
    n = 15 * W * W
    if s.side == 'o':
        o, sg = struct.unpack('<HH', s.dg(0xB9EC, 4))
        return bytes(s.e.rd(sg, o, n, runtime=True))
    return C.string_at(C.addressof(_keep[-1]), n)


def setup_for(x, y, facing, types=(1, 2, 8, 9), cancel=None):
    def setup(s):
        map_setup(s, make_map(), x, y, W)
        s.dgw(0xB9F0, bytes([facing]))
        sx, sy = [(0, -1), (1, 0), (0, 1), (-1, 0)][facing]
        w16(s, 0xB9C4, sy, sx)
        w16(s, 0x9F10, *types)
        w16(s, 0xB9C8, 1)
    return setup


def cancel_effect(when):
    def eff(args, s):
        if args[1] == when:
            w16(s, 0xBBA8, 1)
    return eff


def test_try_move():
    """open walls, blocked walls, type-8 doors, events cancelling the move,
    side steps"""
    n = 0
    for (x, y) in ((1, 1), (2, 3), (4, 2)):
        for facing in range(4):
            for wall in range(4):
                dx, dy = [(0, -1), (1, 0), (0, 1), (-1, 0)][wall]
                cells = make_map()
                c = 15 * (y * W + x)
                kind = cells[c + 2 * wall + 1]
                typ = (1, 2, 8, 9)[cells[c + 2 * wall] - 1]
                # skip the animating paths
                if typ == 9 and kind != 0 and (dx, dy) == [(0, -1), (1, 0), (0, 1), (-1, 0)][facing]:
                    continue
                if kind == 3 or (kind == 2 and (dx, dy) == [(0, -1), (1, 0), (0, 1), (-1, 0)][facing]):
                    continue
                for cancel in (None, 3, 1):
                    eff = {'fr_run_event': cancel_effect(cancel)} if cancel else {}

                    def op(s, dx=dx, dy=dy, wall=wall):
                        if s.side == 'o':
                            r = s.c('s3d_try_move', dx & 0xFFFF, dy & 0xFFFF, wall)[0] & 0xFF
                        else:
                            f = lib.s3d_try_move
                            f.restype = C.c_uint8
                            f.argtypes = (C.c_int16, C.c_int16, C.c_uint8)
                            r = f(dx, dy, wall)
                        return r, map_read(s)
                    ext_run(f'move ({x},{y}) f{facing} wall {wall} cancel {cancel}', op,
                            setup_for(x, y, facing), effect=eff, ignore=range(0xB9EC, 0xB9F0))
                    n += 1
    assert n > 20


def test_scan_cells():
    for (x, y) in ((1, 1), (3, 2), (4, 4)):
        for facing in range(4):
            for v in (0, 1, 3):
                def op(s, v=v):
                    if s.side == 'o':
                        s.c('s3d_scan_cells', v)
                    else:
                        lib.s3d_scan_cells(C.c_uint8(v))
                    return map_read(s)
                ext_run(f'scan ({x},{y}) f{facing} {v}', op, setup_for(x, y, facing),
                        ignore=range(0xB9EC, 0xB9F0))
