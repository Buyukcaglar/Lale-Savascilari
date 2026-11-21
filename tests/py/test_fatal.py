"""ovl559A (src/engine/fatal.c) against the original.

o559A_clear_tables: whole DGROUP.  o559A_load_trdos_font: font bytes handed
to INT 10h AX=1100h and the far heap afterwards (original allocator from the
main() snapshot, see test_farheap).  o559A_fatal: the printed text and the
flags, both paths; on the original the shutdown callees are stubbed and their
calls recorded, printf/puts are captured (formatted here: %d %u %s only).
"""
import ctypes
import os
import re
import struct
import tempfile

import unicorn.x86_const as X

from lale import DG, Emu, NAMES, ROOT, check, dg_from_emu, fn, lib
import helpers as fh

C = ctypes
_i16, _u16, _cp, _vp = C.c_int16, C.c_uint16, C.c_char_p, C.c_void_p
c_clear = fn('o559A_clear_tables', None)
c_font = fn('o559A_load_trdos_font', None)
c_fatal = fn('o559A_fatal', None, _cp, _i16, _cp, _i16)
c_stdio_reset = fn('bc_stdio_reset', None)
c_setupio = fn('bc_setupio', None)
c_set_dir = fn('pc_set_game_dir', None, _cp)
c_pc_reset = fn('pc_reset', None)
c_shift = fn('pc_shift_flags', C.c_uint8)
c_set_shift = fn('pc_set_shift_flags', None, C.c_uint8)
c_vfs_reset = fn('vfs_reset', None)
c_vfile_reset = fn('vfile_reset', None)
FONT = (C.c_uint8 * 4096).in_dll(lib, 'o559A_text_font')
MODE = _i16.in_dll(lib, 'o559A_test_mode')
CONSOLE_T = C.CFUNCTYPE(None, C.c_char_p, C.c_uint16)
console_ptr = C.c_void_p.in_dll(lib, 'fatal_console')
_out = []
_cb = CONSOLE_T(lambda s, n: _out.append(C.string_at(s, n)))


def test_clear_tables():
    e = Emu()
    for off in range(0xDE00, 0xE600, 2):                # fill around both tables
        e.ww(0x16E0, off, off ^ 0x5A5A)
    dg_from_emu(e)
    e.call(NAMES['o559A_clear_tables'])
    c_clear()
    a, b = e.dgroup(0, 0x10000), bytes(DG)
    d = [hex(i) for i in range(0x10000) if a[i] != b[i]]
    check(not d, f'DGROUP differs at {d[:8]}')


def _emu_str(e, seg, off):
    s = bytearray()
    while True:
        c = e.rd(seg, off + len(s), 1, runtime=True)[0]
        if not c:
            return bytes(s)
        s.append(c)


def _font_run(game_dir):
    """(font, coreleft, arena, cs vars, printed) for the original and the port."""
    e, mem = fh.setup_main_heap()
    # the snapshot's heap area covers the image's overlay code: put ovl559A back
    # (the font routine's own code; the heap stays below it)
    lo = e.lin('559A:0000')
    e.u.mem_write(lo, bytes(Emu().u.mem_read(lo, 0x310)))
    arena0 = bytes(e.u.mem_read(fh.ARENA_LIN, fh.ARENA_END - fh.ARENA_LIN))
    e.file_root = game_dir
    got = {}

    def int10(e, uc):
        if uc.reg_read(X.UC_X86_REG_AX) != 0x1100:
            return False
        got['regs'] = tuple(uc.reg_read(getattr(X, f'UC_X86_REG_{r}')) for r in ('BX', 'CX', 'DX'))
        got['font'] = e.rd(uc.reg_read(X.UC_X86_REG_ES), uc.reg_read(X.UC_X86_REG_BP), 4096, runtime=True)
        return True
    e.int_hooks[(0x10, 0x11)] = int10

    def getattr_(e, uc):                               # INT 21h AX=4300h (Borland open)
        name = e._asciiz(uc.reg_read(X.UC_X86_REG_DS), uc.reg_read(X.UC_X86_REG_DX))
        if os.path.isfile(os.path.join(game_dir, name)):
            uc.reg_write(X.UC_X86_REG_CX, 0x20)
            e._cf(uc)
        else:
            e._cf(uc, 2)
        return True

    def ioctl(e, uc):                                  # INT 21h AX=4400h: a disk file
        uc.reg_write(X.UC_X86_REG_DX, 0)
        e._cf(uc)
        return True
    e.int_hooks[(0x21, 0x43)] = getattr_
    e.int_hooks[(0x21, 0x44)] = ioctl
    printed = []
    e.stub('0000:4630', lambda e, a: printed.append(_emu_str(e, a(1), a(0)) + b'\n') or (0, 0))
    e.call(NAMES['o559A_load_trdos_font'])
    orig = (got.get('font'), got.get('regs'), e.call('0000:2987', ret='long'),
            bytes(e.u.mem_read(fh.ARENA_LIN, fh.ARENA_END - fh.ARENA_LIN)), fh.emu_cvars(e), printed)
    # the port: same DGROUP, same arena
    C.memmove(fh.arena(), arena0, fh.ARENA_END - fh.ARENA_LIN)
    c_stdio_reset()
    c_set_dir(game_dir.encode())
    c_setupio()
    _out.clear()
    console_ptr.value = C.cast(_cb, C.c_void_p).value
    for i in range(4096):
        FONT[i] = 0
    c_font()
    port = (bytes(FONT) if got.get('font') else None, got.get('regs'), fh.c_coreleft(),
            bytes(fh.arena()), fh.cvars(), [b''.join(_out)] if _out else [])
    return orig, port


def test_load_trdos_font():
    o, p = _font_run(os.path.join(ROOT, 'orig'))
    check(o[0] == open(os.path.join(ROOT, 'orig', 'TRDOS.DAT'), 'rb').read()[:4096], 'original font != file')
    check(o[1] == (0x1000, 0x0100, 0), f'INT 10h regs {o[1]}')
    check(o[0] == p[0], 'font bytes differ')
    check(o[2] == p[2], f'farcoreleft {o[2]:#x} vs {p[2]:#x}')
    # Not compared: the arena bytes.  The original's fopen/fread take the
    # stream's 512-byte buffer from the far heap (block 4BD7h, so the font
    # buffer lands at 4BF8h); the port's bc_stdio buffers are host memory, so
    # its font buffer is at 4BD7h.  Same break and free list at the end.
    check(o[4] == p[4], f'heap CS vars {o[4]} vs {p[4]}')
    with tempfile.TemporaryDirectory() as d:          # no TRDOS.DAT
        o, p = _font_run(d)
        check(o[0] is None, 'original loaded a font without the file')
        check(b''.join(o[5]) == b''.join(p[5]), f'text {o[5]} vs {p[5]}')
        check(o[2] == p[2], 'farcoreleft without the file')


# ---- o559A_fatal ------------------------------------------------------------------
STUBS = {'0EF6:1014': 'ampClose', '0DF2:09EE': 'DDSUnloadDriver', '16CD:0052': 'unmount',
         '10EF:00DF': 'tsClose', '16BA:0020': 'ogr_int10', '16D3:002F': 'cd_stop',
         '16D3:0020': 'cd_nop', '559A:01F3': 'font', '09D4:095A': 'mm_shutdown',
         '0DB7:00B3': 'ctl_uninstall', '0000:1207': 'exit'}


def _bc_format(e, fmt, args):
    out, k = bytearray(), 0
    for m in re.split(rb'(%[dus])', fmt):
        if m == b'%d':
            v = args(k); k += 1
            out += str(v - 0x10000 if v & 0x8000 else v).encode()
        elif m == b'%u':
            out += str(args(k)).encode(); k += 1
        elif m == b'%s':
            out += _emu_str(e, args(k + 1), args(k)); k += 2
        else:
            out += m
    return bytes(out)


def _fatal_orig(flags, sound, code, msg):
    e = Emu()
    e.ww(0x16E0, 0x750A, flags)
    e.wr(0x16E0, 0x73D0, bytes([sound]))
    e.wr(0x40, 0x17, b'\x5F', runtime=True)          # BDA shift flags
    calls, text = [], []
    for addr, name in STUBS.items():
        e.stub(addr, lambda e, a, n=name: calls.append((n, a(0))) or (0, 0))
    e.stub('0000:4253', lambda e, a: text.append(
        _bc_format(e, _emu_str(e, a(1), a(0)), lambda i: a(2 + i))) or (0, 0))
    e.stub('0000:4630', lambda e, a: text.append(_emu_str(e, a(1), a(0)) + b'\n') or (0, 0))
    s = e.alloc(64)
    e.wr(s, 0, b'T:TEST\\FILE.L\0', runtime=True)
    e.wr(s, 32, (msg or b'') + b'\0', runtime=True)
    e.call(NAMES['o559A_fatal'], *e.far(s, 0), 0x1234, *(e.far(s, 32) if msg else [0, 0]), code)
    return (b''.join(text), [c[0] for c in calls], e.dgroup(0x750A, 1)[0],
            e.rd(0, 0x417, 1, runtime=True)[0])


def _fatal_c(flags, sound, code, msg):
    fn('dg_reset', None)()
    c_pc_reset()
    c_vfs_reset()
    c_vfile_reset()
    for i in range(8):                                 # no archives mounted
        struct.pack_into('<h', DG, 0xE86C + 12 * i, -1)
    for i in range(110):                               # no MEMMAN records
        DG[0xDA48 + 9 * i + 8] = 0xFF
    DG[0x750A] = flags
    DG[0x73D0] = sound
    c_set_shift(0x5F)
    fh.c_reset()
    c_stdio_reset()
    c_set_dir(os.path.join(ROOT, 'orig').encode())
    c_setupio()
    _out.clear()
    console_ptr.value = C.cast(_cb, C.c_void_p).value
    MODE.value = 2
    c_fatal(b'T:TEST\\FILE.L', 0x1234, msg, code)
    MODE.value = 1
    return b''.join(_out), DG[0x750A], c_shift()


def test_fatal_text_and_flags():
    for flags, code, msg in ((0x01, 0x40, None), (0x01, 0x3F, b'ignored'), (0x01, 0x10, None),
                             (0x01, -1, b'Dosya a\x87ma hatas\x8d!'), (0x01, -1, None),
                             (0x04, 0x41, None), (0x04, -1, b'ikinci'), (0x04, -1, None)):
        to, calls, fo, bda = _fatal_orig(flags, 0, code, msg)
        tc, fc, bdc = _fatal_c(flags, 0, code, msg)
        label = f'fatal(flags {flags:#x}, code {code}, msg {msg})'
        check(to == tc, f'{label}: text {to!r} vs {tc!r}')
        if flags & 4:
            check(calls == [] and fo == fc == flags and bda == 0x5F and bdc == 0x5F,
                  f'{label}: double fault must only print ({calls}, {fo:#x}/{fc:#x})')
        else:
            check(calls == ['unmount'] * 8 + ['tsClose', 'ogr_int10', 'font', 'mm_shutdown',
                                               'ctl_uninstall', 'exit'], f'{label}: calls {calls}')
            check(fo == fc == (flags | 4), f'{label}: flags {fo:#x} vs {fc:#x}')
            check(bda == bdc == 0x5C, f'{label}: BDA 0417 {bda:#x} vs {bdc:#x}')
