"""3DSYS key / mouse commands (src/game/s3d_keys.c, 303F:0FFC
s3d_handle_key) against the original, in the world of test_s3d_camp.py
(party, panel pictures, window record 6, a synthetic map; other modules
stubbed and logged).

Covered: the movement keys (cursor keys, 8/2/4/6/7/9), PgDn, unknown
keys, no key.  The commands that open other screens (F1-F4, K, N, S, T,
Esc) are covered by their own routines' tests.  3D5E bit 20h is clear, so
s3d_redraw_view draws nothing.
"""
import ctypes as C

from lale import lib
from test_s3d_panel import ext_run, w16
from test_s3d_camp import base
from test_s3d_move import make_map, W
from test_s3d_panel import map_setup

KEYS = {'up': 0x4800, 'down': 0x5000, 'left': 0x4B00, 'right': 0x4D00, 'pgdn': 0x5100,
        '8': 0x0938, '2': 0x0332, '5': 0x0635, '4': 0x0534, '6': 0x0736, '7': 0x0837,
        '9': 0x0A39, 'x': 0x2D78, 'f9': 0x4300}


def test_handle_key():
    for name, k in list(KEYS.items()) + [('none', None)]:
        for facing in (0, 1, 3):
            def setup(s, facing=facing):
                base(s)
                map_setup(s, make_map(), 2, 2, W)
                s.dgw(0xB9F0, bytes([facing]))
                sx, sy = [(0, -1), (1, 0), (0, 1), (-1, 0)][facing]
                w16(s, 0xB9C4, sy, sx)
                w16(s, 0x9F10, 1, 2, 8, 9)
                w16(s, 0x3AFB, 100, 100, 100, 100)

            def op(s):
                if s.side == 'o':
                    s.c('s3d_handle_key')
                else:
                    lib.s3d_handle_key()
                return C.c_int16(int.from_bytes(s.dg(0xB9C8, 2), 'little')).value
            ext_run(f'key {name} f{facing}', op, setup, keys=[k] if k else (), every=1,
                    ignore=range(0xB9EC, 0xB9F0))
