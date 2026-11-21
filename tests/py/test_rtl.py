"""Borland RTL pieces reimplemented in src/rtl, against the original code."""
import ctypes
from lale import Emu, NAMES, check, fn, dg_read

c_srand = fn('bc_srand', None, ctypes.c_uint16)
c_rand = fn('bc_rand', ctypes.c_int16)


def test_rand_stream():
    e = Emu()
    for seed in (0, 1, 0x1234, 0xFFFF):
        c_srand(seed)
        e.call(NAMES['srand'], seed)
        for i in range(2000):
            a = e.call(NAMES['rand'])[0]
            b = c_rand() & 0xFFFF
            if not check(a == b, f'seed {seed:#x} step {i}: orig {a} C {b}'):
                return
        check(dg_read(0x94C4, 4) == e.dgroup(0x94C4, 4), 'seed state differs')
