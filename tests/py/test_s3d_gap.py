"""ovl303F 3DSYS: the three routines test_s3d_*.py left untested, against
the original (uiworld world, both sides snapshot right after their run).

Names (docs/PROGRESS.md -> analysis/names.tsv / the source):
  s3d_zones         = s3d_draw_portrait   303F:27F7  (src/game/s3d_district.c)
  s3d_debug_buffers = s3d_debug_buffers   303F:23ED  (src/game/s3d_district.c)
  s3d_save_map      = s3d_load_file_error 303F:189D  (src/game/s3d_keys.c)

Reachability in the shipped IST.EXE (raw search of the relocated image
analysis/ghidra_ist/ist_fpu.bin for near CALL/JMP displacements inside
ovl303F, far pointers 1670:xxxx to the overlay thunks and the immediates
of PUSH/MOV 1670h pairs):
  303F:27F7  two near calls (s3d_draw_party_panel 303F:2C92,
             s3d_draw_portrait_hl 303F:7616) and its thunk 1670:00A2 is
             stored as a window callback by INTRFACE (05DC:0A52 ->
             16E0:984C): live, runs at every panel redraw.
  303F:23ED  thunk 1670:0070, referenced nowhere: dead code.
  303F:189D  thunk 1670:0039, referenced nowhere: dead code (no key, no
             menu reaches it).
"""
import ctypes as C
import os
import random
import shutil
import struct

from lale import ROOT, LOAD, check, lib
import helpers
from helpers import pascal_stub, c_set_dir
import uiworld
from uiworld import run

DGSEG_RT = 0x16E0 + LOAD
FATAL_LINE = C.c_int16.in_dll(lib, 'o559A_fatal_line')
FATAL_CODE = C.c_int16.in_dll(lib, 'o559A_fatal_code')
_keep = []


def w16(s, off, *v):
    s.dgw(off, struct.pack('<%dh' % len(v), *v))


# ---- s3d_draw_portrait (the mouse zones) -----------------------------------------------
def test_zones():
    """the 41 zone rectangles (E5F5) and all of DGROUP after
    s3d_draw_portrait: both windows of the game, odd / zero / negative
    window sizes (signed divisions), portrait origins and steps, every
    panel zone index the table can hold"""
    r = random.Random(0x27F7)
    cases = [(100, 100, 0x10A, 4, 0, 0x22, 13), (200, 200, 0x10A, 4, 0, 0x22, 13),
             (200, 100, 8, 0x8C, 0x4C, 0, 13), (100, 200, 8, 0x8C, 0x4C, 0, 13)]
    for _ in range(40):
        cases.append((r.choice([100, 200, 150, 99, 101, 7, 1, 0, -1, -100, 320, 0x7FFF, -0x8000,
                                r.randrange(-400, 400)]),
                      r.choice([100, 200, 120, 99, 3, 0, -7, 0x7FFF, r.randrange(-400, 400)]),
                      r.randrange(-50, 330), r.randrange(-50, 210), r.randrange(-40, 80),
                      r.randrange(-40, 80), r.choice([13, 13, 0, 1, 15, 20, 40, r.randrange(41)])))
    for n, (w, h, px, py, sx, sy, pz) in enumerate(cases):
        fill = bytes(r.randrange(256) for _ in range(41 * 8))

        def setup(s, w=w, h=h, px=px, py=py, sx=sx, sy=sy, pz=pz, fill=fill):
            s.dgw(0xE5F5, fill)                     # the zone table: must be cleared
            w16(s, 0x3AFB, w, h)
            w16(s, 0xBB45, sy, sx, py, px)          # BB45 step y, BB47 step x, BB49 y, BB4B x
            w16(s, 0x965C, pz)

        def op(s):
            if s.side == 'o':
                s.c('s3d_draw_portrait')
            else:
                lib.s3d_draw_portrait()
            return s.dg(0xE5F5, 41 * 8)
        run(f'zones {n} win={w}x{h} portraits=({px},{py})+({sx},{sy}) panel={pz}', op, setup=setup)


# ---- s3d_debug_buffers --------------------------------------------------------------------
def _stop_at_fatal(s, line):
    """the original's fatal stand-in (uiworld: records and returns): stop the
    emulation at the fatal of `line` - what follows it in the original
    (reading the record through a null far pointer) only exists because
    the stand-in returns"""
    e = s.e
    lin = e.lin('16C1:002A')
    old = e._stubs[lin]

    def fatal(em, a):
        r = old(em, a)
        if a(2) == line:
            em.u.emu_stop()
        return r
    e._stubs[lin] = fatal


def test_debug_buffers():
    """s3d_debug_buffers(n): an EMS surface is copied into a conventional
    block that becomes surface 57h (record, size, pixels compared); a
    conventional surface and an empty slot take the fatal paths ("Buf
    no:%d" in the message buffer, fatal line and code)."""
    surfs = uiworld.SURFS + [(8, 200, 150, 1, 'KAPIAN1.CMP'), (6, 400, 200, 1, 'MERDIM.CMP'),
                             (9, 90, 15, 0, 'ITEMBANK.CMP')]
    # 1, 4Bh, 8, 6 (80000 bytes) are EMS, 7 and 9 conventional
    for n, kind in ((0x4B, 'ems'), (8, 'ems'), (1, 'ems'), (6, 'ems'), (7, 'conv'), (9, 'conv'),
                    (0x30, 'empty'), (0x56, 'empty')):
        def setup(s, n=n):
            s.dgw(0xBACC, bytes(0x70))              # g_q3d_errbuf
            s.dgw(0xE553, b'\x11\x22\x33\x44')      # size of 57h

        def op(s, n=n, kind=kind):
            if s.side == 'o':
                if kind == 'empty':
                    _stop_at_fatal(s, 0x51E)
                    try:
                        s.c('s3d_debug_buffers', n)
                        stopped = False
                    except Exception:               # noqa: BLE001 - stopped at the fatal
                        stopped = True
                    check(stopped, f'debug_buffers {n:#x}: the original did not reach fatal 51Eh')
                else:
                    s.c('s3d_debug_buffers', n)
                fat = [tuple(f) for f in s.e.fatals]
                nfat = len(fat)
            else:
                FATAL_LINE.value = FATAL_CODE.value = 0
                lib.s3d_debug_buffers(C.c_uint8(n))
                nfat = s.snap()['fatal']
                fat = [(FATAL_LINE.value & 0xFFFF, FATAL_CODE.value & 0xFFFF)] if nfat else []
            pix = s.read(0x57) if s.rec(0x57) else None
            src = s.read(n) if s.rec(n) else None
            if kind != 'empty':
                check(pix == src, f'debug_buffers {n:#x} ({s.side}): 57h is not a copy of the surface')
                check(s.dg(s.rec(0x57) + 8, 1) == b'\0', f'debug_buffers {n:#x} ({s.side}): 57h not conventional')
            return (fat[-1:], nfat, s.dg(0xBACC, 0x20), s.dg(0xE553, 4), pix,
                    s.dg(s.rec(0x57) + 4, 5) if s.rec(0x57) else None)
        res = run(f'debug_buffers {n:#x} {kind}', op, setup=setup, surfs=surfs)
        want = {'ems': 0, 'conv': 1, 'empty': 2}[kind]
        check(res[0][1] == want, f'debug_buffers {n:#x} {kind}: {res[0][1]} fatals, expected {want}')


# ---- s3d_load_file_error (writes the map bank to a file) --------------------------------
MAP_DIR = os.path.join(ROOT, 'out', 'wp-3dgap', 'savemap')
WRITTEN = {}


def _orig_files(e):
    """the original's file layer (o569A, far pascal; as test_s3d_save's):
    reads from the game data, create (mode 4) into WRITTEN - failing like
    the port's when the folder does not exist"""
    files = {}

    def name_at(seg, off):
        b = bytearray()
        while True:
            c = e.rd(seg, off + len(b), 1, runtime=True)[0]
            if not c:
                return b.decode('cp857')
            b.append(c)

    def op(w):
        name = name_at(w[2], w[1])
        h = max(files, default=4) + 1
        if w[0] == 4:
            if chr(92) in name:
                return 0xFFFF
            files[h] = [bytearray(), 0, name.upper()]
            return h
        path = helpers.find_file(name)
        if not path:
            return 0xFFFF
        files[h] = [open(path, 'rb').read(), 0, None]
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
    pascal_stub(e, '16CD:003E', 1, lambda w: files[w[0]][1])


def test_save_map():
    """the file s3d_load_file_error writes: (u16)(map_w * map_h) * 15 bytes
    (16-bit products) from 2Bh bytes before the cell pointer B9EC.  Sizes
    incl. the 16-bit wraps; a file that cannot be created (fatal line 457h,
    the harness' fatal returns: the write to handle -1 follows on both
    sides)."""
    shutil.rmtree(MAP_DIR, ignore_errors=True)
    os.makedirs(MAP_DIR)
    r = random.Random(0x189D)
    bank = bytes(r.randrange(256) for _ in range(0xFFF0))
    old = uiworld._stub_files
    uiworld._stub_files = _orig_files
    try:
        for k, (mw, mh, name) in enumerate([(4, 4, 'A.MAP'), (20, 17, 'B.MAP'), (40, 40, 'C.MAP'),
                                             (255, 255, 'D.MAP'), (0, 9, 'E.MAP'), (100, 100, 'F.MAP'),
                                             (66, 66, 'G.MAP'), (1, 1, 'H.MAP'), (128, 128, 'I.MAP'),
                                             (7, 7, 'NODIR\\J.MAP')]):
            size = ((mw * mh) & 0xFFFF) * 15 & 0xFFFF
            WRITTEN.clear()

            def setup(s, mw=mw, mh=mh, name=name):
                s.dgw(0xA780, name.encode() + b'\0')
                s.dgw(0xB9EA, bytes([mh, mw]))
                if s.side == 'o':
                    seg = s.e.alloc(len(bank))
                    s.e.wr(seg, 0, bank, runtime=True)
                    s.dgw(0xB9EC, struct.pack('<HH', 0x2B, seg))
                else:
                    c_set_dir(MAP_DIR.encode())
                    b = C.create_string_buffer(bank, len(bank))
                    _keep.append(b)
                    C.c_void_p.in_dll(lib, 's3d_map').value = C.addressof(b) + 0x2B

            def op(s):
                if s.side == 'o':
                    s.c('s3d_load_file_error', 0xA780, DGSEG_RT)
                    return [tuple(f) for f in s.e.fatals]
                FATAL_LINE.value = FATAL_CODE.value = 0
                lib.s3d_load_file_error(C.c_void_p(C.addressof(uiworld.DG) + 0xA780))
                return [(FATAL_LINE.value & 0xFFFF, FATAL_CODE.value & 0xFFFF)] if s.snap()['fatal'] else []
            label = f'save_map {name} {mw}x{mh}'
            res = run(label, op, setup=setup, ignore=range(0xB9EC, 0xB9F0))
            p = os.path.join(MAP_DIR, name)
            port = open(p, 'rb').read() if os.path.exists(p) else None
            orig = WRITTEN.get(name)
            if '\\' in name:
                check(orig is None and port is None, f'{label}: a file was written')
                check(res[0] == [(0x457, 0xFFFF)], f'{label}: fatal {res[0]}')
            else:
                check(orig is not None and orig == port,
                      f'{label}: file {len(orig or b"")} vs {len(port or b"")} bytes')
                check(orig == bank[:size], f'{label}: not the first {size} bytes of the bank')
                check(res[0] == [], f'{label}: fatal {res[0]}')
    finally:
        uiworld._stub_files = old
        c_set_dir(os.path.join(ROOT, 'orig').encode())
