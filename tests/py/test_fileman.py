"""FILEMAN (ovl5447) against the original."""
import ctypes
import glob
import os
import random

from lale import Emu, NAMES, ROOT, check, fn

c_cmp_decode = fn('fm_cmp_decode', ctypes.c_uint16, ctypes.c_char_p, ctypes.c_uint16,
                  ctypes.POINTER(ctypes.c_uint8))


def _decode_both(e, data, out_size=0xFFF0):
    # a truncated last run reads past src_len: give both sides the same padding
    padded = data + bytes(range(256)) * 2
    src = e.alloc(len(padded))
    dst = e.alloc(out_size, fill=0xCC)
    e.wr(src, 0, padded, runtime=True)
    n_orig = e.call(NAMES['fm_cmp_decode'], e.far(src, 0), len(data), e.far(dst, 0))[0]
    buf = (ctypes.c_uint8 * out_size)(*([0xCC] * out_size))
    n_c = c_cmp_decode(padded, len(data), buf)
    return n_orig, e.rd(dst, 0, out_size, runtime=True), n_c, bytes(buf)


def test_cmp_decode_game_files():
    files = sorted(glob.glob(os.path.join(ROOT, 'extracted', '*', '*.CMP')))
    check(len(files) > 100, 'CMP files missing (run tools/bootstrap.sh)')
    e = Emu()
    for path in files[::9]:
        data = open(path, 'rb').read()[4:]          # skip u16 w, u16 h
        chunk = data[:0x3000]                        # stay below 64 KB of output
        # cut at a control-byte boundary so the chunk is self-contained
        i = 0
        while i < len(chunk):
            c = chunk[i]
            step = 2 if c > 0x80 else 1 + c
            if i + step > len(chunk):
                break
            i += step
        chunk = chunk[:i]
        e._heap = 0x6200                             # reuse scratch memory
        no, mo, nc, mc = _decode_both(e, chunk)
        check(no == nc and mo == mc, f'{os.path.basename(path)}: n {no} vs {nc}, '
                                     f'mem equal {mo == mc}')


def test_cmp_decode_edge_cases():
    e = Emu()
    rnd = random.Random(5447)
    cases = [b'', b'\x00', b'\x80' + bytes(range(128)), b'\x81\x07', b'\xff\x09', b'\x00\x00\x83\x01']
    for _ in range(40):
        cases.append(bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 300))))
    for data in cases:
        e._heap = 0x6200
        no, mo, nc, mc = _decode_both(e, data)
        check(no == nc and mo == mc, f'{data[:8].hex()}..: n {no} vs {nc}, mem equal {mo == mc}')


# ---- fm_load_cmp / fm_load_file / fm_load_pal / fm_change_ext ----------------------
# The original runs with helpers.make_emu (the port's EMS through attach_pc, the
# real far heap, fatal recorded) and its file layer o569A_* (thunks 16CD:xxxx,
# far pascal) served from extracted/<ARCHIVE>/ and orig/ (helpers.stub_files).
# The port reads the same members through its real vfs, with 3D.CKD and
# IVIR.CKD mounted from orig/; the far heap of both sides starts from the
# port's state after those mounts.
import struct  # noqa: E402

from lale import DG, lib  # noqa: E402
from helpers import (make_emu, reset_c, ems_mem, EMS_DG, pc_reset, stub_files, c_mount,  # noqa: E402
                     FATALS)

C = ctypes
_u8, _u16, _i16, _u32, _vp, _cp = C.c_uint8, C.c_uint16, C.c_int16, C.c_uint32, C.c_void_p, C.c_char_p
c_load_cmp = fn('fm_load_cmp', None, _u8, _cp, _i16)
c_load_file = fn('fm_load_file', _vp, _u8, _cp, _vp)
c_load_pal = fn('fm_load_pal', None, _cp, _i16, _i16)
c_change_ext = fn('fm_change_ext', _vp, _cp, _cp)
c_mm_init = fn('mm_init', _i16, _u32, _u32)
c_mm_conv_ptr = fn('mm_conv_ptr', _vp, _vp)
c_fh_reset = fn('bc_farheap_reset', None)
DGA = C.addressof(DG)
FM_DG = [(0xE29B, 0xE29B + 90 * 8), (0xE7B8, 0xE7B8 + 0x32), (0xDE26, 0xDE30),
         (0x6E02, 0x6E0E), (0xD9F8, 0xDA2C), (0xDE30, 0xDE34)] + EMS_DG


def _mount():
    c_mount(b'3D.CKD', b'IVIR.CKD')


def _L(v):
    return [v & 0xFFFF, (v >> 16) & 0xFFFF]


def _run_orig(ops):
    e = make_emu(c_setup=_mount)
    pc_reset()
    stub_files(e)
    e.call(NAMES['mm_init'], *_L(0x240000), *_L(0x440000))
    palseg = e.alloc(768, fill=0x5A)
    e.ww(0x16E0, 0xE5EF, 0)
    e.ww(0x16E0, 0xE5F1, palseg)
    res, files = [], {}
    for op in ops:
        s = e.alloc(80)
        e.wr(s, 0, op[1].encode() + b'\0', runtime=True)
        if op[0] == 'cmp':
            e.call(NAMES['fm_load_cmp'], op[2], *e.far(s, 0), op[3])
        elif op[0] == 'file':
            blk = files.get(op[3]) if op[3] is not None else None
            r = e.call(NAMES['fm_load_file'], op[2], *e.far(s, 0), *([blk, 0x1F04] if blk else [0, 0]))
            files[len(files)] = r[0]
            res.append(('file', r[0]))
        elif op[0] == 'pal':
            e.call(NAMES['fm_load_pal'], *e.far(s, 0), op[2], op[3])
    dg = e.dgroup(0, 0x10000)
    conv = {}
    for i in range(110):
        off = 0xDA48 + 9 * i
        if dg[off + 8] == 0:
            size = struct.unpack_from('<I', dg, off + 4)[0]
            o, sg = struct.unpack_from('<HH', dg, off)
            conv[off] = e.rd(sg, o, min(size, 0x30000), runtime=True)
    return res, dg, ems_mem(1), conv, e.rd(palseg, 0, 768, runtime=True), len(e.fatals)


def _run_c(ops):
    reset_c(c_setup=_mount)
    c_mm_init(0x240000, 0x440000)
    pal = (C.c_uint8 * 768)(*([0x5A] * 768))
    C.c_void_p.in_dll(lib, 'gr_target_pal').value = C.addressof(pal)
    res, files = [], {}
    for op in ops:
        if op[0] == 'cmp':
            c_load_cmp(op[2], op[1].encode(), op[3])
        elif op[0] == 'file':
            blk = files.get(op[3]) if op[3] is not None else None
            r = c_load_file(op[2], op[1].encode(), DGA + blk if blk else None)
            files[len(files)] = (r - DGA) if r else 0
            res.append(('file', (r - DGA) if r else 0))
        elif op[0] == 'pal':
            c_load_pal(op[1].encode(), op[2], op[3])
    dg = bytes(DG)
    conv = {}
    for i in range(110):
        off = 0xDA48 + 9 * i
        if dg[off + 8] == 0:
            size = struct.unpack_from('<I', dg, off + 4)[0]
            p = c_mm_conv_ptr(DGA + off)
            conv[off] = C.string_at(p, min(size, 0x30000)) if p else b''
    return res, dg, ems_mem(1), conv, bytes(pal), FATALS.value


def _compare(label, ops):
    ro = _run_orig(ops)
    rc = _run_c(ops)
    check(ro[0] == rc[0], f'{label}: results {ro[0]} vs {rc[0]}')
    for i in range(110):                    # handle table: word 0-1 only for EMS records
        x, y = ro[1][0xDA48 + 9 * i:0xDA51 + 9 * i], rc[1][0xDA48 + 9 * i:0xDA51 + 9 * i]
        check(x[4:] == y[4:] and (x[8] != 1 or x[:2] == y[:2]), f'{label}: record {i} {x.hex()} vs {y.hex()}')
    for lo, hi in FM_DG:
        a, b = ro[1][lo:hi], rc[1][lo:hi]
        if a != b:
            i = next(k for k in range(hi - lo) if a[k] != b[k])
            check(False, f'{label}: DGROUP {lo:04X}-{hi:04X} differs at {lo + i:04X} '
                         f'({a[i:i + 8].hex()} vs {b[i:i + 8].hex()})')
    check(ro[2] == rc[2], f'{label}: EMS memory differs')
    check(ro[3] == rc[3], f'{label}: conventional records differ '
          f'{[(k, len(v)) for k, v in ro[3].items()]} vs {[(k, len(v)) for k, v in rc[3].items()]}')
    check(ro[4] == rc[4], f'{label}: palette differs')
    check(ro[5] == rc[5] == 0, f'{label}: fatals {ro[5]} / {rc[5]}')


def test_load_cmp():
    big = ['DUVAR4.CMP', 'COMP4A.CMP', 'KAPI4.CMP', 'KAPIAN4.CMP']   # w*h > FFFFh (COMP4A: data > 64 KB)
    small = ['BOSCH.CMP', 'GOCUK.CMP', 'RAKS.CMP', 'DEVAM.CMP']
    _compare('load_cmp type 1', [('cmp', n, 1, 10 + i) for i, n in enumerate(big + small)])
    # conventional: the far heap (about 330 KB free after the mounts) holds two big ones
    _compare('load_cmp type 0', [('cmp', n, 0, 10 + i) for i, n in enumerate(big[1:3] + small)])
    # existing slots reloaded with the other type (mm_change_type)
    ops = [('cmp', 'RAKS.CMP', 1, 5), ('cmp', 'GOCUK.CMP', 0, 6), ('cmp', 'RAKS.CMP', 0, 5),
           ('cmp', 'GOCUK.CMP', 1, 6), ('cmp', 'RAKS.CMP', 1, 5), ('cmp', 'GOCUK.CMP', 1, 6)]
    # Not tested: a picture over 64 KB into a slot that already has a record.
    # The original then picks the decoder by an uninitialised local (stack
    # garbage, see fm_load_cmp); in the emulator it took the 16-bit decoder,
    # which wraps inside the segment - the port always takes the huge one.
    _compare('load_cmp reload', ops)


def test_load_file_and_pal():
    ops = [('file', 'COMP4A.CMP', 1, None), ('file', 'DUVAR4.CMP', 0, None),
           ('file', 'SONPAL.PAL', 1, None), ('file', 'KAPI4.CMP', 1, 0),
           ('file', 'BOSCH.CMP', 0, 2), ('pal', 'SONPAL.PAL', 0, 256),
           ('pal', 'ACILISA.PAL', 16, 100), ('pal', 'COMP4A.PAL', 250, 6)]
    _compare('load_file/pal', ops)


def test_change_ext():
    e = Emu()
    e.stub_heap()
    c_fh_reset()           # fm_change_ext farmalloc's its result (never freed)
    bs = chr(92)
    cases = [('PIC.CMP', 'PAL'), ('PIC.CMP', '.PAL'), ('NAME', 'KOR'), ('A.B.C', 'X'),
             ('..' + bs + 'DIR' + bs + 'NAME.CMP', 'PAL'), ('..' + bs + 'DIR' + bs + 'NAME', 'PAL'),
             ('DIR.X' + bs + 'NAME.CMP', '.R'), ('.' + bs + 'A', 'B'), ('', 'E'), ('X.', 'Y.Z'),
             ('NAME.CMP', '')]
    for path, ext in cases:
        s = e.alloc(64)
        e.wr(s, 0, path.encode() + b'\0', runtime=True)
        e.wr(s, 32, ext.encode() + b'\0', runtime=True)
        ax, dx = e.call(NAMES['fm_change_ext'], *e.far(s, 0), *e.far(s, 32))
        o = bytearray()
        while True:
            b = e.rd(dx, ax + len(o), 1, runtime=True)[0]
            if not b:
                break
            o.append(b)
        c = C.string_at(c_change_ext(path.encode(), ext.encode()))
        check(bytes(o) == c, f'fm_change_ext({path!r}, {ext!r}): {bytes(o)} vs {c}')
