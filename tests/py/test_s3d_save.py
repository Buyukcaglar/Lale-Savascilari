"""3DSYS saved games (src/game/s3d_save.c, 303F:3DD0 s3d_write_save and
303F:3F95 s3d_read_save) against the original, in the uiworld world.

Files: the port writes / reads a private folder (out/wp16_s3dsave, via
pc_set_game_dir); the original's file layer (o569A, pascal) is a Python
stand-in that captures what it writes and reads the same folder first,
then helpers.SEARCH.  The written bytes are compared, and the file must
match the layout of docs/FORMATS.md (1524 bytes).

s3d_read_save goes on to load the district and enter the view: it runs in
test_s3d_district's world (FRPOS.CKD, the 3D and UI tables), with a save
of the original game (orig/SAKLA001.SAV) and one written by the port.
"""
import ctypes as C
import os
import shutil
import struct

from lale import ROOT, LOAD, DG, check, lib
import helpers
from helpers import pascal_stub, c_set_dir
import uiworld
from uiworld import ORIG_POLL_POINTS
from test_s3d_panel import ext_run, w16, call, setup_party, ITEMS

SAVE_DIR = os.path.join(ROOT, 'out', 'wp16_s3dsave')
WRITTEN = {}
MISSING = []

for _pt in ((0x0C7F, 0x0854), (0x05DC, 0x1E81), (0x05DC, 0x1D87), (0x303F, 0x07A3)):
    if _pt not in ORIG_POLL_POINTS:
        ORIG_POLL_POINTS.append(_pt)


def find(name):
    p = os.path.join(SAVE_DIR, name)
    if os.path.exists(p):
        return p
    for n in os.listdir(SAVE_DIR):
        if n.upper() == name.upper():
            return os.path.join(SAVE_DIR, n)
    p = os.path.join(ROOT, 'extracted', 'FRPOS', name.upper())
    return p if os.path.exists(p) else helpers.find_file(name)


def orig_files(e):
    files = {}

    def name_at(seg, off):
        s = bytearray()
        while True:
            b = e.rd(seg, off + len(s), 1, runtime=True)[0]
            if not b:
                return s.decode('cp857')
            s.append(b)

    def op(w):
        name = name_at(w[2], w[1])
        h = max(files, default=4) + 1
        if w[0] == 4:
            files[h] = [bytearray(), 0, name.upper()]
            return h
        p = find(name)
        if not p:
            MISSING.append(name)
            return 0xFFFF
        files[h] = [open(p, 'rb').read(), 0, None]
        return h

    def cl(w):
        f = files.pop(w[0], None)
        if f and f[2]:
            WRITTEN[f[2]] = bytes(f[0])
        return 0

    def rd(w):
        f = files.get(w[4])
        if f is None:
            return 0xFFFFFFFF
        # o569A_read (vfs_read): 0 when all `n` bytes were read; past the
        # end the rest is read, pos = size - 1 and size - pos is returned
        n = w[0] | w[1] << 16
        data = f[0][f[1]:f[1] + n]
        e.u.mem_write(w[3] * 16 + w[2], bytes(data))
        if len(data) == n:
            f[1] += n
            return 0
        f[1] = len(f[0]) - 1
        return len(data)

    def wr(w):
        f = files.get(w[4])
        if f is None:
            return 0xFFFFFFFF
        n = w[0] | w[1] << 16
        f[0] += bytes(e.u.mem_read(w[3] * 16 + w[2], n))
        return n

    def sk(w):
        f = files[w[3]]
        off = w[1] | w[2] << 16
        off -= (1 << 32) if off & 0x80000000 else 0
        f[1] = [0, f[1], len(f[0])][w[0]] + off
        return 0
    pascal_stub(e, '16CD:0025', 3, op)
    pascal_stub(e, '16CD:002A', 1, cl)
    pascal_stub(e, '16CD:002F', 5, rd)
    pascal_stub(e, '16CD:0034', 5, wr)
    pascal_stub(e, '16CD:0039', 4, sk)
    pascal_stub(e, '16CD:003E', 1, lambda w: files[w[0]][1] if w[0] in files else print('MISSING', MISSING))  # tell


def files_pre(s):
    if s.side == 'c':
        c_set_dir(SAVE_DIR.encode())


def with_files(f):
    """run f() with the original's file layer replaced by orig_files (uiworld
    builds the emulator with uiworld._stub_files)"""
    old = uiworld._stub_files
    uiworld._stub_files = orig_files
    try:
        return f()
    finally:
        uiworld._stub_files = old
        c_set_dir(os.path.join(ROOT, 'orig').encode())


def fresh_dir():
    shutil.rmtree(SAVE_DIR, ignore_errors=True)
    os.makedirs(SAVE_DIR)


DESC = b'kamp yeri\0' + bytes(range(1, 51))         # 60 bytes, garbage after the NUL


def put_desc(s):
    """the description at DGROUP A700 (a free list area) on both sides"""
    s.dgw(0xA700, DESC)
    s.dgw(0xA780, b'TEST01.SAV\0')


def game_state(s):
    setup_party(s, ITEMS, {1: [0x23, 0x24, 0x25]})
    s.dgw(0xB9CA, bytes((7 * k + 3) & 0xFF for k in range(32)))
    s.dgw(0x98FC, bytes((k * 5) & 0xFF for k in range(0xAA)))
    s.dgw(0xBB34, bytes([1, 5, 1, 1, 1, 0, 1]))
    w16(s, 0x3D5E, 0x23)
    w16(s, 0xB9F3, 4)
    w16(s, 0xB9F1, 9)
    s.dgw(0xB9F0, b'\x02')
    w16(s, 0xBB6B, 3)
    w16(s, 0xBB4D, 2)
    w16(s, 0xBB6D, 1, 2, 3, 260)
    w16(s, 0x3D64, 25, 10, 349)
    s.dgw(0xBBAB, bytes(range(0x30, 0x30 + 23)))   # BBAB, BBAC[11], BBB7[11]
    w16(s, 0xBB88, 120, 77)


def lib_dg(off):
    return C.addressof(DG) + off


def test_write_save():
    fresh_dir()
    WRITTEN.clear()

    def setup(s):
        files_pre(s)
        put_desc(s)
        game_state(s)

    def op(s):
        if s.side == 'o':
            s.c('s3d_write_save', 0xA780, 0x16E0 + LOAD, 0xA700, 0x16E0 + LOAD)
        else:
            lib.s3d_write_save(C.c_void_p(lib_dg(0xA780)), C.c_void_p(lib_dg(0xA700)))
    with_files(lambda: ext_run('write save', op, setup, surfs=uiworld.SURFS, rix=False))
    o = WRITTEN.get('TEST01.SAV')
    p = os.path.join(SAVE_DIR, 'TEST01.SAV')
    c = open(p, 'rb').read() if os.path.exists(p) else None
    check(o is not None and o == c, f'write save: file {len(o or b"")} vs {len(c or b"")} bytes')
    check(o is not None and len(o) == 1524 and o[:60] == DESC and o[60:62] == b'\x20\x00',
          'write save: layout')



def test_read_save():
    """a save written by the original game (orig/SAKLA001.SAV) and one
    written by the port (test_write_save's TEST01.SAV, or written here):
    the state blocks, the portraits, the district loaded (not a first
    visit), the view size / speed from the options and the view entered.
    The world is test_s3d_district's (FRPOS.CKD, 3D tables, UI tables)."""
    import test_s3d_district as D
    if not os.path.exists(os.path.join(SAVE_DIR, 'TEST01.SAV')):
        test_write_save()
    shutil.copy(os.path.join(ROOT, 'orig', 'SAKLA001.SAV'), SAVE_DIR)
    for name in (b'SAKLA001.SAV', b'TEST01.SAV'):
        base = D.view_setup(0, 9 * 60, 1)

        def setup(s, name=name):
            base(s)
            files_pre(s)
            s.dgw(0xA780, name + b'\0')

        def op(s):
            if s.side == 'o':
                s.c('s3d_read_save', 0xA780, 0x16E0 + LOAD)
            else:
                lib.s3d_read_save(C.c_void_p(lib_dg(0xA780)))
            D.clear_native_ptrs(s)
            return D.surfaces(s, (0x47, 0x48, 0x49, 0x4A, 0x40))
        # the portraits 47h-4Ah are loaded by s3d_read_save itself ("s" +
        # portrait name, 160x32): no 32x32 stand-ins there
        surfs = [x for x in D.PANEL_SURFS if not 0x47 <= x[0] <= 0x4A]
        with_files(lambda: D.with_frpos(lambda: ext_run(f'read save {name.decode()}', op, setup,
                                                         surfs=surfs, rix=True)))
