"""Borland stdio (src/rtl/bc_stdio.c), ovl5646 (src/engine/vfile.c) and ovl569A
(src/engine/vfs.c) against the original code.

The original runs on a DOS file service (DosEmu below) with the same semantics
as the port's emulated DOS layer in bc_stdio.c: lowest free handle >= 5, 32-bit
wrapping positions, write of 0 bytes truncates, access-mode checks.  So what is
compared is the RTL / file-layer logic: return values, bytes read, file
contents written, and the state the original keeps in DGROUP (FILE table,
_openfd[], errno, the vfs tables) plus the heap tables behind far pointers.
"""
import ctypes
import os
import random
import shutil
import struct
import tempfile

import unicorn.x86_const as X

from lale import Emu, LOAD, DGROUP, NAMES, ROOT, check, fn, lib, dg_read, dg_from_emu  # noqa: F401
from helpers import DosEmu, run_setupio

ORIG = os.path.join(ROOT, 'orig')
STREAMS, NSTREAMS = 0x929E, 20

c_i16, c_u16, c_i32, c_vp, c_cp = ctypes.c_int16, ctypes.c_uint16, ctypes.c_int32, ctypes.c_void_p, ctypes.c_char_p

c_dg_reset = fn('dg_reset', None)
c_set_dir = fn('pc_set_game_dir', None, c_cp)
c_stdio_reset = fn('bc_stdio_reset', None)
c_setupio = fn('bc_setupio', None)
c_fopen = fn('bc_fopen', c_vp, c_cp, c_cp)
c_fclose = fn('bc_fclose', c_i16, c_vp)
c_fflush = fn('bc_fflush', c_i16, c_vp)
c_flushall = fn('bc_flushall', c_i16)
c_fread = fn('bc_fread', c_u16, c_vp, c_u16, c_u16, c_vp)
c_fwrite = fn('bc_fwrite', c_u16, c_vp, c_u16, c_u16, c_vp)
c_fseek = fn('bc_fseek', c_i16, c_vp, c_i32, c_i16)
c_ftell = fn('bc_ftell', c_i32, c_vp)
c_setvbuf = fn('bc_setvbuf', c_i16, c_vp, c_vp, c_i16, c_u16)
c_fgetc = fn('bc_fgetc', c_i16, c_vp)
c_fputc = fn('bc_fputc', c_i16, c_i16, c_vp)
c_file_buffer = fn('bc_file_buffer', c_vp, c_vp)
c_file_curp = fn('bc_file_curp', c_vp, c_vp)

c_vfile_reset = fn('vfile_reset', None)
c_vfile = {
    'init': fn('vfile_init', c_i16, c_i16),
    'open': fn('vfile_open', c_i16, c_cp, c_i16),
    'close': fn('vfile_close', c_i16, c_i16),
    'read': fn('vfile_read', c_i32, c_i16, c_vp, c_i32),
    'write': fn('vfile_write', c_i32, c_i16, c_vp, c_i32),
    'seek': fn('vfile_seek', c_i16, c_i16, c_i32, c_i16),
    'tell': fn('vfile_tell', c_i32, c_i16),
    'flush': fn('vfile_flush', c_i16, c_i16),
    'eof': fn('vfile_eof', c_i16, c_i16),
}
c_vfs_reset = fn('vfs_reset', None)
c_vfs = {
    'init': fn('vfs_init', c_i16, c_i16),
    'mount': fn('vfs_mount_archive', c_i16, c_cp, c_vp),
    'unmount': fn('vfs_unmount_archive', c_i16, c_i16),
    'open': fn('vfs_open', c_i16, c_cp, c_i16),
    'close': fn('vfs_close', c_i16, c_i16),
    'read': fn('vfs_read', c_i32, c_i16, c_vp, c_i32),
    'write': fn('vfs_write', c_i32, c_i16, c_vp, c_i32),
    'seek': fn('vfs_seek', c_i16, c_i16, c_i32, c_i16),
    'tell': fn('vfs_tell', c_i32, c_i16),
    'flush': fn('vfs_flush', c_i16, c_i16),
    'eof': fn('vfs_eof', c_i16, c_i16),
    'check_handle': fn('vfs_check_handle', c_i16, c_i16),
    'toupper': fn('vfs_toupper', c_i16, c_i16),
}
DG_BASE = ctypes.addressof(ctypes.c_uint8.in_dll(lib, 'dg'))


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


def L(v):
    """A long argument: two words, low first."""
    v &= 0xFFFFFFFF
    return [v & 0xFFFF, v >> 16]


# ---- the original's DOS: helpers.DosEmu, helpers.run_setupio -------------------


class Pair:
    """The original (emulated) and the port, with their own file roots."""

    def __init__(self, root_orig, root_c=None):
        self.e = DosEmu(root_orig)
        self.e.stub_heap()
        c_dg_reset()
        c_stdio_reset()
        c_vfile_reset()
        c_vfs_reset()
        c_set_dir((root_c or root_orig).encode())
        run_setupio(self.e)
        c_setupio()
        self.strseg = self.e.alloc(0x1000)
        self.bufseg = self.e.alloc(0x24000)
        self.cbuf = ctypes.create_string_buffer(0x24000)
        self.fail = 0

    def ok(self, cond, msg):
        if not cond:
            self.fail += 1
        return check(cond, msg) or self.fail > 20

    def s(self, text, slot=0):
        """Far pointer (2 words) to a NUL-terminated string in the emulator."""
        b = text.encode('cp857') + b'\0'
        self.e.wr(self.strseg, slot * 0x80, b, runtime=True)
        return self.e.far(self.strseg, slot * 0x80)

    def call(self, name, *args, ret='int'):
        return self.e.call(NAMES[name], *args, ret=ret)

    def pcall(self, name, *args, ret='int'):
        """Pascal: first declared argument is pushed first (highest address)."""
        return self.e.call(NAMES[name], *reversed(args), ret=ret)

    def fp_off(self, ax_dx):
        ax, dx = ax_dx
        return 0 if (ax | dx) == 0 else ax

    def cfp(self, off):
        return DG_BASE + off if off else None

    def cfp_off(self, p):
        return (p - DG_BASE) if p else 0

    def compare_rtl(self, where):
        """FILE table (without the far pointer fields), _openfd[], errno,
        _doserrno, setvbuf flags, fgetc/fputc statics, buffer contents and
        curp positions."""
        a, b = self.e.dgroup(0, 0x10000), dg_read(0, 0x10000)
        bad = []
        for i in range(NSTREAMS):
            o = STREAMS + 20 * i
            for k in list(range(0, 8)) + list(range(16, 20)):
                if a[o + k] != b[o + k]:
                    bad.append(f'FILE[{i}]+{k:X}')
        for lo, hi, what in ((0x9430, 0x9458, '_openfd'), (0x7F, 0x81, 'errno'),
                             (0x945E, 0x9460, '_doserrno'), (0x959C, 0x95A0, 'buffered'),
                             (0xEA70, 0xEA71, 'fgetc char'), (0xEA72, 0xEA73, 'fputc char')):
            if a[lo:hi] != b[lo:hi]:
                bad.append(f'{what} {a[lo:hi].hex()} vs {b[lo:hi].hex()}')
        for i in range(NSTREAMS):
            o = STREAMS + 20 * i
            if struct.unpack_from('<b', a, o + 4)[0] < 0:
                continue
            bsize = struct.unpack_from('<H', a, o + 6)[0]
            boff, bseg, coff, cseg = struct.unpack_from('<4H', a, o + 8)
            orig_cur = (cseg * 16 + coff) - (bseg * 16 + boff)
            pb, pc = c_file_buffer(self.cfp(o)), c_file_curp(self.cfp(o))
            if pb is None:                  # stderr/aux/prn: never set up, NULL in both
                if boff or bseg:
                    bad.append(f'FILE[{i}] no C buffer')
                continue
            if orig_cur != pc - pb:
                bad.append(f'FILE[{i}] curp {orig_cur} vs {pc - pb}')
            level = struct.unpack_from('<h', a, o)[0]
            n = min(orig_cur + max(level, 0), max(bsize, 1))   # bytes holding data (rest: uninitialised)
            if n > 0 and self.e.rd(bseg, boff, n, runtime=True) != ctypes.string_at(pb, n):
                bad.append(f'FILE[{i}] buffer bytes')
        return self.ok(not bad, f'{where}: ' + ', '.join(bad[:8]))

    def emu_read_buf(self, n):
        return self.e.rd(self.bufseg, 0, n, runtime=True)


def scratch_pair(files):
    """Two scratch roots with the same initial files."""
    ra, rb = tempfile.mkdtemp(prefix='wp4a_'), tempfile.mkdtemp(prefix='wp4b_')
    for name, data in files.items():
        for r in (ra, rb):
            with open(os.path.join(r, name), 'wb') as f:
                f.write(data)
    return ra, rb


def read_root(r, name):
    p = os.path.join(r, name)
    return open(p, 'rb').read() if os.path.exists(p) else None


# ---- stdio ---------------------------------------------------------------------
class Stdio:
    """fopen/fread/... on both sides, compared after every operation."""

    def __init__(self, pair):
        self.p = pair

    def fopen(self, name, mode):
        p = self.p
        a = p.fp_off(p.call('fopen', p.s(name, 0), p.s(mode, 1)))
        b = p.cfp_off(c_fopen(name.encode(), mode.encode()))
        p.ok(a == b, f'fopen({name},{mode}) {a:#x} vs {b:#x}')
        p.compare_rtl(f'after fopen({name},{mode})')
        return a

    def fread(self, fp, size, n):
        p = self.p
        ctypes.memset(p.cbuf, 0xCC, size * n + 1)
        p.e.wr(p.bufseg, 0, b'\xcc' * (size * n + 1), runtime=True)
        a = p.call('fread', p.e.far(p.bufseg, 0), size, n, p.e.far(DGROUP + LOAD, fp))[0]
        b = c_fread(p.cbuf, size, n, p.cfp(fp))
        da, db = p.emu_read_buf(size * n + 1), p.cbuf.raw[:size * n + 1]
        p.ok(a == b and da == db, f'fread({size},{n}) -> {a} vs {b}, data equal {da == db}')
        p.compare_rtl(f'after fread({size},{n})')
        return a, da[:a * size]

    def fwrite(self, fp, data, size=1):
        p = self.p
        n = len(data) // size
        p.e.wr(p.bufseg, 0, data, runtime=True)
        ctypes.memmove(p.cbuf, data, len(data))
        a = p.call('fwrite', p.e.far(p.bufseg, 0), size, n, p.e.far(DGROUP + LOAD, fp))[0]
        b = c_fwrite(p.cbuf, size, n, p.cfp(fp))
        p.ok(a == b, f'fwrite({size},{n}) -> {a} vs {b}')
        p.compare_rtl(f'after fwrite({len(data)})')
        return a

    def fgetc(self, fp):
        p = self.p
        a = s16(p.call('fgetc', p.e.far(DGROUP + LOAD, fp))[0])
        b = c_fgetc(p.cfp(fp))
        p.ok(a == b, f'fgetc -> {a} vs {b}')
        p.compare_rtl('after fgetc')
        return a

    def fputc(self, fp, ch):
        p = self.p
        a = s16(p.call('fputc', ch, p.e.far(DGROUP + LOAD, fp))[0])
        b = c_fputc(ch, p.cfp(fp))
        p.ok(a == b, f'fputc({ch}) -> {a} vs {b}')
        p.compare_rtl(f'after fputc({ch})')
        return a

    def fseek(self, fp, off, whence):
        p = self.p
        a = s16(p.call('fseek', p.e.far(DGROUP + LOAD, fp), L(off), whence)[0])
        b = c_fseek(p.cfp(fp), off, whence)
        p.ok(a == b, f'fseek({off},{whence}) -> {a} vs {b}')
        p.compare_rtl(f'after fseek({off},{whence})')
        return a

    def ftell(self, fp):
        p = self.p
        a = s32(p.call('ftell', p.e.far(DGROUP + LOAD, fp), ret='long'))
        b = c_ftell(p.cfp(fp))
        p.ok(a == b, f'ftell -> {a} vs {b}')
        p.compare_rtl('after ftell')
        return a

    def fflush(self, fp):
        p = self.p
        a = s16(p.call('fflush', p.e.far(DGROUP + LOAD, fp))[0])
        b = c_fflush(p.cfp(fp))
        p.ok(a == b, f'fflush -> {a} vs {b}')
        p.compare_rtl('after fflush')
        return a

    def setvbuf(self, fp, typ, size):
        p = self.p
        a = s16(p.call('setvbuf', p.e.far(DGROUP + LOAD, fp), 0, 0, typ, size)[0])
        b = c_setvbuf(p.cfp(fp), None, typ, size)
        p.ok(a == b, f'setvbuf({typ},{size}) -> {a} vs {b}')
        p.compare_rtl(f'after setvbuf({typ},{size})')

    def fclose(self, fp):
        p = self.p
        a = s16(p.call('fclose', p.e.far(DGROUP + LOAD, fp))[0])
        b = c_fclose(p.cfp(fp))
        p.ok(a == b, f'fclose -> {a} vs {b}')
        p.compare_rtl('after fclose')
        return a

    def flushall(self):
        p = self.p
        a = s16(p.call('flushall')[0])
        b = c_flushall()
        p.ok(a == b, f'flushall -> {a} vs {b}')
        p.compare_rtl('after flushall')


def text_sample():
    """Text with CRLF, bare LF, bare CR, a CR as byte 511 (end of the first
    512-byte buffer fill: read() fetches one more byte) and ^Z then junk."""
    body = bytearray()
    rnd = random.Random(4)
    while len(body) < 505:
        body += b'line %d \t abc\r\n' % len(body)
    body += b'x' * (511 - len(body)) + b'\r' + b'\nnext'
    body += b'bare lf\nbare cr\rdouble\r\r\nend\r\n'
    for i in range(60):
        body += bytes(rnd.choice(b'abc \r\n\t') for _ in range(rnd.randrange(1, 40))) + b'\r\n'
    body += b'tail\x1aafter ctrl-z\r\n'
    return bytes(body)


def test_stdio_text_read():
    data = text_sample()
    ra, rb = scratch_pair({'T.TXT': data, 'U.TXT': data[:700]})
    try:
        p = Pair(ra, rb)
        io = Stdio(p)
        for mode in ('rt', 'r'):             # 'r' = _fmode default = text
            fp = io.fopen('t.txt', mode)
            got = b''
            for n in (1, 7, 100, 3, 600, 1, 250, 2000, 5, 5):
                io.ftell(fp)
                cnt, d = io.fread(fp, 1, n)
                got += d
            io.ftell(fp)
            io.fseek(fp, 0, 0)
            io.fread(fp, 1, 40)
            pos = io.ftell(fp)
            io.fread(fp, 1, 30)
            io.fseek(fp, pos, 0)
            io.fread(fp, 1, 30)
            io.fseek(fp, -10, 1)
            io.fread(fp, 1, 20)
            io.fseek(fp, 505, 0)
            for _ in range(12):
                io.fgetc(fp)
                io.ftell(fp)
            io.fseek(fp, -20, 2)
            io.fread(fp, 1, 100)
            io.fgetc(fp)
            io.fclose(fp)
        # unbuffered text stream: fgetc's own CR skipping
        fp = io.fopen('U.TXT', 'rt')
        io.setvbuf(fp, 2, 0)
        for _ in range(40):
            io.fgetc(fp)
        io.ftell(fp)
        io.fread(fp, 1, 600)
        io.fgetc(fp)
        io.fclose(fp)
        # line buffered read stream: fflush drops the buffer
        fp = io.fopen('U.TXT', 'rb')
        io.setvbuf(fp, 1, 64)
        io.fread(fp, 1, 10)
        io.fflush(fp)
        io.ftell(fp)
        io.fread(fp, 1, 10)
        io.fclose(fp)
        p.e.close_all()
    finally:
        c_stdio_reset()
        shutil.rmtree(ra, ignore_errors=True)
        shutil.rmtree(rb, ignore_errors=True)


def test_stdio_text_write():
    ra, rb = scratch_pair({'OLD.TXT': b'0123456789' * 100})
    try:
        p = Pair(ra, rb)
        io = Stdio(p)
        fp = io.fopen('W.TXT', 'wt')
        io.fwrite(fp, b'hello\nworld\n')
        io.ftell(fp)
        io.fputc(fp, ord('\n'))
        io.fputc(fp, 0xE7)                   # cp857 byte, sign-extended argument
        io.fwrite(fp, (b'abc\ndef\r\n' * 80)[:700])
        io.ftell(fp)
        io.fflush(fp)
        io.ftell(fp)
        io.fwrite(fp, b'\n' * 300)
        io.fseek(fp, 3, 0)
        io.fwrite(fp, b'XY\nZ')
        io.fclose(fp)
        fp = io.fopen('OLD.TXT', 'r+t')      # read, then write after fseek
        io.fread(fp, 1, 25)
        io.fseek(fp, 0, 1)
        io.fwrite(fp, b'--\n--')
        io.ftell(fp)
        io.fclose(fp)
        fp = io.fopen('OLD.TXT', 'at')
        io.fwrite(fp, b'appended\nline\n')
        io.ftell(fp)
        io.fclose(fp)
        fp = io.fopen('N.TXT', 'wt')         # unbuffered text: fputc writes "\r\n"
        io.setvbuf(fp, 2, 0)
        for ch in b'a\nb\n':
            io.fputc(fp, ch)
        io.fwrite(fp, b'line\nline\n')
        io.fclose(fp)
        fp = io.fopen('L.TXT', 'w')          # line buffered
        io.setvbuf(fp, 1, 32)
        io.fwrite(fp, b'one\ntwo\rthree' + b'x' * 40)
        io.fclose(fp)
        io.flushall()
        p.e.close_all()
        for name in ('W.TXT', 'OLD.TXT', 'N.TXT', 'L.TXT'):
            a, b = read_root(ra, name), read_root(rb, name)
            p.ok(a == b, f'{name}: file contents differ ({len(a or b"")} vs {len(b or b"")} bytes)')
    finally:
        c_stdio_reset()
        shutil.rmtree(ra, ignore_errors=True)
        shutil.rmtree(rb, ignore_errors=True)


def test_stdio_binary():
    rnd = random.Random(1)
    blob = bytes(rnd.randrange(256) for _ in range(5000))
    ra, rb = scratch_pair({'B.BIN': blob})
    try:
        p = Pair(ra, rb)
        io = Stdio(p)
        fp = io.fopen('b.bin', 'rb')
        for n in (10, 512, 1500, 3, 2000, 700, 700):    # bulk path, buffer path, EOF
            io.fread(fp, 1, n)
            io.ftell(fp)
        io.fread(fp, 4, 10)
        io.fseek(fp, -100, 2)
        io.fread(fp, 7, 20)
        io.fseek(fp, 100, 0)
        io.fread(fp, 2, 0x9000)                         # total >= 64 KB: item loop
        io.fseek(fp, 6000, 0)                           # past the end
        io.fread(fp, 1, 10)
        io.fseek(fp, -7000, 1)                          # before 0: DOS wraps
        io.ftell(fp)
        io.fread(fp, 1, 10)
        io.fclose(fp)
        fp = io.fopen('b.bin', 'r+b')
        io.fread(fp, 1, 30)
        io.fwrite(fp, b'WRITE-WITHOUT-SEEK')            # binary __fputn quirk path
        io.ftell(fp)
        io.fseek(fp, 0, 1)
        io.fwrite(fp, blob[:900])                       # > bsize: straight to the handle
        io.fwrite(fp, blob[:100])
        io.ftell(fp)
        io.fclose(fp)
        fp = io.fopen('c.bin', 'wb')
        for n in (1, 511, 512, 513, 0, 2000):
            io.fwrite(fp, blob[:n])
        io.fputc(fp, 0x41)
        io.fwrite(fp, blob[:256], size=16)
        io.fread(fp, 1, 5)                              # read on a write-only stream
        io.fclose(fp)
        io.fclose(fp)                                   # second fclose
        p.e.close_all()
        for name in ('B.BIN', 'c.bin'):
            a, b = read_root(ra, name), read_root(rb, name)
            p.ok(a == b, f'{name}: file contents differ')
    finally:
        c_stdio_reset()
        shutil.rmtree(ra, ignore_errors=True)
        shutil.rmtree(rb, ignore_errors=True)


def test_stdio_errors():
    ra, rb = scratch_pair({'E.TXT': b'abc'})
    try:
        p = Pair(ra, rb)
        io = Stdio(p)
        io.fopen('MISSING.TXT', 'rb')
        io.fopen('E.TXT', 'q')
        io.fopen('E.TXT', 'rx')
        fps = [io.fopen('E.TXT', 'rb') for _ in range(17)]   # 15 free FILEs / DOS handles
        for fp in fps:
            if fp:
                io.fclose(fp)
        p.e.close_all()
    finally:
        c_stdio_reset()
        shutil.rmtree(ra, ignore_errors=True)
        shutil.rmtree(rb, ignore_errors=True)


# ---- ovl5646 / ovl569A -----------------------------------------------------------------
class Layer:
    """Call a pascal file-layer function on both sides and compare results."""

    def __init__(self, pair, prefix, cfuncs):
        self.p, self.prefix, self.c = pair, prefix, cfuncs
        self.alias = {'mount': 'mount_archive', 'unmount': 'unmount_archive'}

    def __call__(self, name, *args, cmp_buf=0, long=False):
        p = self.p
        long = long or name in ('read', 'write', 'tell')
        eargs, cargs = [], []
        for a in args:
            if isinstance(a, tuple) and a[0] == 'str':
                eargs.append(p.s(a[1], a[2] if len(a) > 2 else 0))
                cargs.append(a[1].encode('cp857'))
            elif isinstance(a, tuple) and a[0] == 'buf':
                eargs.append(p.e.far(p.bufseg, 0))
                cargs.append(p.cbuf)
            elif isinstance(a, tuple) and a[0] == 'long':
                eargs.append(L(a[1]))
                cargs.append(a[1])
            elif a is None:
                eargs.append([0, 0])
                cargs.append(None)
            else:
                eargs.append(a)
                cargs.append(a)
        if cmp_buf:
            ctypes.memset(p.cbuf, 0xCC, cmp_buf)
            p.e.wr(p.bufseg, 0, b'\xcc' * cmp_buf, runtime=True)
        r = p.pcall(f'{self.prefix}_{self.alias.get(name, name)}', *eargs, ret='long' if long else 'int')
        a = s32(r) if long else s16(r[0])
        b = self.c[name](*cargs)
        what = f'{self.prefix}_{name}{tuple(x if not isinstance(x, tuple) else x[-1] for x in args)}'
        p.ok(a == b, f'{what} -> {a} vs {b}')
        if cmp_buf:
            da, db = p.emu_read_buf(cmp_buf), p.cbuf.raw[:cmp_buf]
            p.ok(da == db, f'{what}: data differs')
        return a


def compare_vfs(p, where, nrec):
    """vfs DGROUP state, the record table and the archive directories."""
    a, b = p.e.dgroup(0, 0x10000), dg_read(0, 0x10000)
    bad = []
    for lo, hi in ((0xE860, 0xE862), (0xE866, 0xE868)):
        if a[lo:hi] != b[lo:hi]:
            bad.append(f'{lo:04X}')
    for slot in range(8):
        o = 0xE86C + 12 * slot
        if a[o:o + 4] != b[o:o + 4]:
            bad.append(f'slot {slot} {a[o:o + 4].hex()} vs {b[o:o + 4].hex()}')
    off, seg = struct.unpack_from('<HH', a, 0xE868)
    cptr = ctypes.c_void_p.in_dll(lib, 'vfs_files').value
    if seg and cptr:
        ra = p.e.rd(seg, off, nrec * 0x38, runtime=True)
        rb = ctypes.string_at(cptr, nrec * 0x38)
        if ra != rb:
            i = next(k for k in range(len(ra)) if ra[k] != rb[k])
            bad.append(f'vfs_files differ at record {i // 0x38} +{i % 0x38:X}')
    cdirs = (ctypes.c_void_p * 9).in_dll(lib, 'vfs_arch_dir')
    for slot in range(8):
        o = 0xE86C + 12 * slot
        h, cnt = struct.unpack_from('<hh', a, o)
        if h == -1:
            continue
        doff, dseg = struct.unpack_from('<HH', a, o + 8)
        da = p.e.rd(dseg, doff, cnt * 0x38, runtime=True)
        db = ctypes.string_at(cdirs[slot], cnt * 0x38) if cdirs[slot] else b''
        if da != db:
            bad.append(f'directory of slot {slot} differs')
    return p.ok(not bad, f'{where}: ' + ', '.join(bad[:8]))


def test_vfile_layer():
    blob = bytes(random.Random(7).randrange(256) for _ in range(0x22000))
    ra, rb = scratch_pair({'BIG.BIN': blob, 'TXT.TXT': b'a\r\nb\r\n'})
    try:
        p = Pair(ra, rb)
        v = Layer(p, 'o5646', c_vfile)
        v('init', 0)
        h = v('open', ('str', 'BIG.BIN'), 2)
        v('read', h, ('buf',), ('long', 0x20000), cmp_buf=0x20001)   # 0xFFF0 pieces
        v('tell', h, long=True)
        v('eof', h)
        v('read', h, ('buf',), ('long', 0x3000), cmp_buf=0x3001)     # partial: 1 + count
        v('eof', h)                                                   # _F_EOF -> 20h
        v('seek', h, ('long', -5), 2)
        v('eof', h)
        v('read', h, ('buf',), ('long', 5), cmp_buf=6)
        v('eof', h)
        v('seek', h, ('long', 0), 0)
        v('read', h, ('buf',), ('long', -2), cmp_buf=0xFFFF)          # negative count quirk
        v('tell', h, long=True)
        v('flush', h)
        v('close', h)
        v('close', h)
        v('read', 25, ('buf',), ('long', 1))
        v('tell', -1, long=True)
        w = v('open', ('str', 'OUT.BIN'), 4)
        p.e.wr(p.bufseg, 0, blob[:0x11000], runtime=True)
        ctypes.memmove(p.cbuf, blob, 0x11000)
        v('write', w, ('buf',), ('long', 0x11000))
        v('tell', w, long=True)
        v('flush', w)
        v('eof', w)
        v('read', w, ('buf',), ('long', 4), cmp_buf=5)                # write-only stream
        v('close', w)
        t = v('open', ('str', 'TXT.TXT'), 1)                          # "rt"
        v('read', t, ('buf',), ('long', 10), cmp_buf=11)
        v('tell', t, long=True)
        v('close', t)
        v('open', ('str', 'NOPE.BIN'), 2)
        p.compare_rtl('vfile end')
        p.e.close_all()
        p.ok(read_root(ra, 'OUT.BIN') == read_root(rb, 'OUT.BIN'), 'OUT.BIN differs')
    finally:
        c_stdio_reset()
        shutil.rmtree(ra, ignore_errors=True)
        shutil.rmtree(rb, ignore_errors=True)


ARCHIVES = ['muzik.ckd', 'sounds.ckd', 'frpos.ckd', 'ikon.ckd', 'ivir.ckd', 'savas.ckd', '3d.ckd',
            '0gfl.ckd']


def flib_names(path):
    data = open(path, 'rb')
    hdr = data.read(12)
    count, diroff = struct.unpack_from('<I', hdr, 4)[0], struct.unpack_from('<I', hdr, 8)[0]
    data.seek(diroff)
    d = data.read(count * 0x41)
    names = [d[i * 0x41 + 4:i * 0x41 + 36].split(b'\0')[0].decode('cp857') for i in range(count)]
    sizes = [struct.unpack_from('<I', d, i * 0x41 + 0x39)[0] for i in range(count)]
    return list(zip(names, sizes))


def test_vfs_archives():
    if not check(os.path.isfile(os.path.join(ORIG, '3D.CKD')), 'orig/ missing'):
        return
    p = Pair(ORIG)
    try:
        v = Layer(p, 'o569A', c_vfs)
        NREC = 20
        v('init', NREC)
        compare_vfs(p, 'after init', NREC)
        for name in ARCHIVES:
            v('mount', ('str', name), None)
            compare_vfs(p, f'after mount {name}', NREC)
        v('mount', ('str', 'frpos.ckd'), None)          # no free slot
        rnd = random.Random(569)
        members = []
        for name in ARCHIVES[:7]:
            ent = flib_names(os.path.join(ORIG, name.upper()))
            members += rnd.sample(ent, min(3, len(ent)))
        for name, size in members:
            for variant in (name, name.lower()):
                h = v('open', ('str', variant), 2)
                compare_vfs(p, f'open {variant}', NREC)
                if h < 0:
                    continue
                n = min(size, 0x8000)
                v('read', h, ('buf',), ('long', n // 3), cmp_buf=n // 3 + 1)
                v('tell', h, long=True)
                v('seek', h, ('long', -7), 1)
                v('read', h, ('buf',), ('long', 16), cmp_buf=17)
                v('seek', h, ('long', -10), 2)
                v('read', h, ('buf',), ('long', 30), cmp_buf=31)       # clamped: returns count
                v('tell', h, long=True)
                v('eof', h)                                            # pos == size-1
                v('seek', h, ('long', 0), 2)
                v('read', h, ('buf',), ('long', 0), cmp_buf=1)
                v('eof', h)
                v('seek', h, ('long', size + 5), 0)
                v('seek', h, ('long', -3), 0)
                v('tell', h, long=True)
                v('seek', h, ('long', size), 1)
                v('seek', h, ('long', -(size + 1)), 2)                 # -> -1
                v('seek', h, ('long', 3), 2)
                v('seek', h, ('long', 0), 7)
                v('seek', h, ('long', 0), 0)
                n = min(size, 0x23000)
                v('read', h, ('buf',), ('long', n), cmp_buf=n + 1)
                v('eof', h)
                v('write', h, ('buf',), ('long', 4))
                v('flush', h)
                compare_vfs(p, f'ops on {variant}', NREC)
                v('close', h)
                v('close', h)
        # plain DOS files through the vfs
        h = v('open', ('str', 'TRDOS.DAT'), 2)
        v('read', h, ('buf',), ('long', 0x1001), cmp_buf=0x1002)
        v('eof', h)              # passes the vfs handle to vfile_eof
        v('flush', h)
        v('tell', h, long=True)
        v('seek', h, ('long', 100), 0)
        v('read', h, ('buf',), ('long', 50), cmp_buf=51)
        h2 = v('open', ('str', 'sonpal.pal'), 2)
        v('eof', h2)
        v('read', h2, ('buf',), ('long', 768), cmp_buf=769)
        v('close', h2)
        v('close', h)
        v('open', ('str', 'NOFILE.XYZ'), 2)
        v('open', ('str', ''), 2)
        for h in (-1, 0, NREC, NREC + 1, 3):
            v('check_handle', h)
        # handle table full
        hs = [v('open', ('str', 'bosch.kor'), 2) for _ in range(NREC + 1)]
        compare_vfs(p, 'table full', NREC)
        for h in hs:
            if h >= 0:
                v('close', h)
        v('unmount', 2)
        v('unmount', 2)
        v('unmount', -1)
        v('unmount', 9)
        compare_vfs(p, 'after unmount', NREC)
        v('open', ('str', members[0][0]), 2)
        v('mount', ('str', 'TRDOS.DAT'), None)          # not FLiB: fails, count left behind
        compare_vfs(p, 'after bad mount', NREC)
        v('mount', ('str', 'frpos.ckd'), None)
        compare_vfs(p, 'remount', NREC)
        p.compare_rtl('vfs end')
    finally:
        p.e.close_all()
        c_stdio_reset()


def test_vfs_write_and_checks():
    ra, rb = scratch_pair({'SAKLA001.SAV': open(os.path.join(ORIG, 'SAKLA001.SAV'), 'rb').read()})
    try:
        p = Pair(ra, rb)
        v = Layer(p, 'o569A', c_vfs)
        v('init', 5)
        h = v('open', ('str', 'SAKLA002.SAV'), 4)
        data = bytes(range(256)) * 6
        p.e.wr(p.bufseg, 0, data, runtime=True)
        ctypes.memmove(p.cbuf, data, len(data))
        v('write', h, ('buf',), ('long', 1524))
        v('tell', h, long=True)
        v('flush', h)            # passes vfs handle h to vfile_flush
        v('close', h)
        h = v('open', ('str', 'sakla001.sav'), 2)
        v('read', h, ('buf',), ('long', 1524), cmp_buf=1525)
        v('eof', h)
        v('read', h, ('buf',), ('long', 1), cmp_buf=2)
        v('eof', h)
        v('close', h)
        for c in (0, 0x40, 0x61, 0x7A, 0x7B, -1, -0x1A, 0x80):
            v('toupper', c)
        p.e.close_all()
        p.ok(read_root(ra, 'SAKLA002.SAV') == read_root(rb, 'SAKLA002.SAV'), 'SAKLA002.SAV differs')
    finally:
        c_stdio_reset()
        shutil.rmtree(ra, ignore_errors=True)
        shutil.rmtree(rb, ignore_errors=True)
