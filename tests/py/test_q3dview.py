"""ovl2700 3DQUICK first-person renderer (src/game/q3dview*.c) against the
original.

Harness notes
* The emulator runs q3dview_common.ovl2700_image(): ist_fpu.bin with
  ovl2700 re-patched (ist_fpu.bin mis-converts 69 8087-emulator sequences
  in this overlay).
* Unicorn's x87 starts with CW 0; Emu.call sets 037Fh (64-bit precision),
  the game's control word.
"""
import ctypes
import os
import random
import struct

from q3dview_common import ovl2700_image
from lale import Emu, NAMES, ROOT, LOAD, check, fn, lib, dg_write, dg_read, dg_from_emu, dg_diff

C = ctypes
i16, u8, u16 = C.c_int16, C.c_uint8, C.c_uint16
DGSEG = 0x16E0 + LOAD

def make_emu(snapshot=None):
    return Emu(snapshot=snapshot, image=ovl2700_image())


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def set_dg(e, off, fmt, *vals):
    data = struct.pack(fmt, *vals)
    e.wr(0x16E0, off, data)
    dg_write(off, data)


# ---- tables ------------------------------------------------------------------
c_pre_a = fn('q3d_precalc_table_a', None, i16, i16, C.c_void_p)
c_pre = {n: fn(n, None) for n in ('q3d_precalc_table_b0', 'q3d_precalc_table_b1',
                                  'q3d_precalc_table_c0', 'q3d_precalc_table_c1')}
TABLES = (0xB490, 0xB9C4)


def test_tables():
    """Whole piece tables (both, B490-B9C3) after the start-up sequence, for
    every view size up to 400 on the diagonal, the sizes the game uses and
    random sizes (including negative and huge)."""
    e = make_emu()
    r = random.Random(0x328)
    sizes = [(100, 100), (200, 200), (100, 200), (200, 100), (0, 0), (1, 1), (14, 14)]
    sizes += [(n, n) for n in range(1, 401, 3)]
    sizes += [(r.randrange(1, 700), r.randrange(1, 700)) for _ in range(150)]
    sizes += [(s16(r.randrange(0x10000)), s16(r.randrange(0x10000))) for _ in range(30)]
    bad = 0
    for w, h in sizes:
        fill = bytes(r.randrange(256) for _ in range(TABLES[1] - TABLES[0]))
        e.wr(0x16E0, TABLES[0], fill)
        dg_write(TABLES[0], fill)
        set_dg(e, 0x3AFF, '<hh', w, h)
        e.call(NAMES['q3d_precalc_table_a'], w & 0xFFFF, h & 0xFFFF, e.far(DGSEG, 0xB72A))
        c_pre_a(w, h, C.addressof(C.c_uint8.from_buffer(lib_dg(), 0xB72A)))
        for name in ('q3d_precalc_table_b1', 'q3d_precalc_table_c1', 'q3d_precalc_table_b0',
                     'q3d_precalc_table_c0') if (w + h) % 2 else ('q3d_precalc_table_b1',
                                                                  'q3d_precalc_table_c1'):
            e.call(NAMES[name])
            c_pre[name]()
        d = dg_diff(e, [TABLES])
        if d:
            bad += 1
            if bad <= 3:
                check(False, f'tables {w}x{h}: {len(d)} bytes differ, first {d[:4]}')
    check(bad == 0, f'tables: {bad} of {len(sizes)} sizes differ')


def lib_dg():
    return (C.c_uint8 * 0x10000).in_dll(lib, 'dg')


# ---- .KOR files ------------------------------------------------------------------
import helpers  # noqa: E402

c_load_kor = fn('q3d_load_kor', i16, C.c_char_p, i16)
KOR_PTRS = (C.c_void_p * 8).in_dll(lib, 'q3d_kor_pieces')
KOR_DG = (0xB3E8, 0xB458)


def _mount():
    helpers.c_mount(b'3D.CKD')


def _kor_setup(e, w, h):
    set_dg(e, 0x3AFF, '<hh', w, h)
    e.call(NAMES['q3d_precalc_table_a'], w, h, e.far(DGSEG, 0xB72A))
    e.call(NAMES['q3d_precalc_table_b1'])
    e.call(NAMES['q3d_precalc_table_c1'])
    c_pre_a(w, h, C.addressof(lib_dg()) + 0xB72A)
    c_pre['q3d_precalc_table_b1']()
    c_pre['q3d_precalc_table_c1']()


def test_load_kor():
    """Every .KOR of 3D.CKD into every slot, both view sizes, missing files;
    then a second load into the same slot (frees the first table)."""
    names = ['DUVAR4', 'DUVAR8', 'KAPI4', 'KAPI5', 'KAPI6', 'KAPI8', 'DUVAR1', 'NOFILE']
    for w, h in ((100, 100), (200, 200), (150, 120)):
        e = helpers.make_emu(c_setup=_mount, emu=make_emu())
        helpers.stub_files(e)
        helpers.reset_c(c_setup=_mount)
        _kor_setup(e, w, h)
        for k in range(8):
            KOR_PTRS[k] = None
            e.wr(0x16E0, 0xB3E8 + 14 * k + 10, b'\0\0\0\0')
        for n, name in enumerate(names * 2):
            slot = n % 8
            pre = struct.pack('<hhhhh', 1, 6, 0, 200, 200)
            set_dg(e, 0xB3E8 + 14 * slot, '<5h', *struct.unpack('<5h', pre))
            s = e.alloc(32)
            e.wr(s, 0, (name + '.CMP').encode() + b'\0', runtime=True)
            ro, _ = e.call(NAMES['q3d_load_kor'], *e.far(s, 0), slot)
            rc = c_load_kor((name + '.CMP').encode(), slot) & 0xFFFF
            check(ro == rc, f'load_kor {name} {w}x{h}: return {ro} vs {rc}')
            ign = set()
            for k in range(8):
                ign |= set(range(0xB3E8 + 14 * k + 10, 0xB3E8 + 14 * k + 14))
            d = dg_diff(e, [KOR_DG], ignore=ign)
            check(not d, f'load_kor {name} {w}x{h}: DGROUP {d[:6]}')
            off, seg = struct.unpack('<HH', e.dgroup(0xB3E8 + 14 * slot + 10, 4))
            po = KOR_PTRS[slot]
            if seg == DGSEG:
                check(po == C.addressof(lib_dg()) + off, f'load_kor {name}: ptr {off:04X} vs C')
                continue
            to = e.rd(seg, off, 37 * 18, runtime=True)
            tc = C.string_at(po, 37 * 18)
            if to != tc:
                bad = [(i, struct.unpack('<9h', to[i*18:i*18+18]), struct.unpack('<9h', tc[i*18:i*18+18]))
                       for i in range(37) if to[i*18:i*18+18] != tc[i*18:i*18+18]]
                check(False, f'load_kor {name} {w}x{h}: {len(bad)} pieces differ, {bad[:3]}')


# ---- automap, piece touch-ups -------------------------------------------------------
import glob  # noqa: E402

import uiworld  # noqa: E402

MAP_PTR = C.c_void_p.in_dll(lib, 's3d_map')
_keep = []


def _uiworld_emu(**k):
    return helpers.make_emu(emu=make_emu(), **k)


def map_data(name):
    """the cells of a district map (extracted/FRPOS/PCn: AMOS bank, byte 20
    width, 21 height, cells from 22) -> (w, h, cells)"""
    d = open(os.path.join(ROOT, 'extracted', 'FRPOS', name), 'rb').read()
    return d[20], d[21], d[22:]


def set_map(s, w, h, cells, x, y, facing):
    """the map and the party on one side of a uiworld world"""
    dx, dy = ((0, -1), (1, 0), (0, 1), (-1, 0))[facing & 3]
    data = struct.pack('<BB', h, w)
    pos = struct.pack('<B', facing) + struct.pack('<hh', y, x)
    if s.side == 'o':
        seg = s.e.alloc(len(cells) + 16)
        s.e.wr(seg, 0, cells, runtime=True)
        s.dgw(0xB9EA, data + struct.pack('<HH', 0, seg) + pos)
        s.dgw(0xB9C4, struct.pack('<hh', dy, dx))
    else:
        buf = C.create_string_buffer(cells, len(cells) + 16)
        _keep.append(buf)
        MAP_PTR.value = C.addressof(buf)
        s.dgw(0xB9EA, data + b'\0' * 4 + pos)
        s.dgw(0xB9C4, struct.pack('<hh', dy, dx))


def test_automap():
    """q3d_draw_automap of every district map (party at a few cells and
    facings), whole A000 and DGROUP compared (the far map pointer B9EC/B9EE
    ignored)."""
    old = uiworld.make_emu
    uiworld.make_emu = _uiworld_emu
    try:
        r = random.Random(0x5F32)
        for f in sorted(glob.glob(os.path.join(ROOT, 'extracted', 'FRPOS', 'PC[0-9]*'))):
            w, h, cells = map_data(os.path.basename(f))
            x, y, fc = r.randrange(w), r.randrange(h), r.randrange(4)
            uiworld.run(f'automap {os.path.basename(f)} ({x},{y},{fc})',
                        lambda s: (s.c('q3d_draw_automap') if s.side == 'o' else lib.q3d_draw_automap()) and None,
                        setup=lambda s: set_map(s, w, h, cells, x, y, fc),
                        ignore=range(0xB9EC, 0xB9F0))
    finally:
        uiworld.make_emu = old


def test_init_tables():
    """q3d_init_tables and q3d_init_a on random piece tables in DGROUP."""
    e = make_emu()
    r = random.Random(0x62EC)
    for name in ('q3d_init_tables', 'q3d_init_a') * 3:
        fill = bytes(r.randrange(256) for _ in range(37 * 18))
        e.wr(0x16E0, 0xB72A, fill)
        dg_write(0xB72A, fill)
        e.call(NAMES[name], *e.far(DGSEG, 0xB72A))
        getattr(lib, name)(C.c_void_p(C.addressof(lib_dg()) + 0xB72A))
        d = dg_diff(e, [(0xB72A, 0xB72A + 37 * 18)])
        check(not d, f'{name}: {len(d)} bytes differ, first {d[:4]}')
