"""DSMI CDI (seg0EAE), AMP player (seg0EF6), AMF loader (seg105B/111B)
against the original in Unicorn.

Both sides load the same AMF through the port's file layer (the original's
o569A_* thunks are served by the C vfs_* functions), register a recording CDI
device (emulator: 26 Python-stubbed far routines; C: tests/stubs/wp5_stubs.c)
and fake DDS exports (MCPSampleRealAddress, EMSAlloc/EMSCopyTo/EMSFree), then
play.  Compared: loader results and the module structures, the full CDI call
stream (channel, frequency, volume, pan, position, sample, SAMPLEINFO
contents), and the player state in DGROUP.

Needs tests/stubs/wp5_stubs.c in the DLL:
  tools/testlib.sh out/wp5 src/dsmi/cdi.c src/dsmi/amp.c src/dsmi/amfload.c tests/stubs/wp5_stubs.c
"""
import ctypes as C
import glob
import os
import random
import struct
import tempfile

import unicorn as U
import unicorn.x86_const as X

from lale import Emu, LOAD, ROOT, DG, check, fn, lib, dg_read, dg_write  # noqa: F401
import emu86

MUZIK = os.path.join(ROOT, 'extracted', 'MUZIK')
i16, i32, u8p, vp, cp = C.c_int16, C.c_int32, C.POINTER(C.c_uint8), C.c_void_p, C.c_char_p

c_dg_reset = fn('dg_reset', None)
c_dds_init = fn('DDSInit', i16)
c_set_dir = fn('pc_set_game_dir', None, cp)
c_stdio_reset = fn('bc_stdio_reset', None)
c_setupio = fn('bc_setupio', None)
c_vfile_reset = fn('vfile_reset', None)
c_vfs_reset = fn('vfs_reset', None)
c_vfs_init = fn('vfs_init', i16, i16)
c_vfs_open = fn('vfs_open', i16, cp, i16)
c_vfs_close = fn('vfs_close', i16, i16)
c_vfs_read = fn('vfs_read', i32, i16, vp, i32)
c_vfs_seek = fn('vfs_seek', i16, i16, i32, i16)
c_vfs_tell = fn('vfs_tell', i32, i16)
c_cdi_init = fn('cdiInit', None)
c_reg_dev = fn('wp5_register_device', i16)
c_reg_dds = fn('wp5_register_dds', None, C.c_int)
c_set_module = fn('wp5_set_module', None, vp)
c_wp5_reset = fn('wp5_reset', None)
c_amp_init = fn('ampInit', i16, i32)
c_load = fn('ampLoadModule', i16, cp, i32, C.POINTER(vp))
c_free = fn('ampFreeModule', None, vp)
c_play = fn('ampPlayModule', i16, vp, i32)
c_play_pat = fn('ampPlayPattern', i16, vp, i32, i32)
c_tick = fn('ampTick', None)
c_interrupt = fn('ampInterrupt', None)
c_stop = fn('ampStopModule', i16)
c_dump = fn('wp5_dump_module', i32, vp, u8p, i32)
c_si = fn('wp5_sampleinfo', None, C.c_int, u8p)
c_getters = {
    'ampGetModuleStatus': fn('ampGetModuleStatus', i16),
    'ampGetPattern': fn('ampGetPattern', i16),
    'ampGetRow': fn('ampGetRow', i16),
    'ampGetSync': fn('ampGetSync', i16),
    'ampGetTempo': fn('ampGetTempo', C.c_uint16),
    'ampGetTickLength': fn('ampGetTickLength', i32),
}
c_args1 = {
    'ampGetTrackStatus': fn('ampGetTrackStatus', i16, i32),
    'ampPauseTrack': fn('ampPauseTrack', i16, i32),
    'ampResumeTrack': fn('ampResumeTrack', i16, i32),
    'ampSetTempo': fn('ampSetTempo', i16, i32),
    'ampBreakPattern': fn('ampBreakPattern', i16, i32),
}
c_args0 = {'ampPauseModule': fn('ampPauseModule', i16), 'ampResumeModule': fn('ampResumeModule', i16),
           'ampClose': fn('ampClose', i16)}
c_trackdata = fn('ampGetTrackData', vp, i32)
c_settv = fn('ampSetTrackVolume', i16, i32, i32)


class Call(C.Structure):
    _fields_ = [('slot', i32), ('a', i32), ('b', i32), ('c', i32)]


LOG = (Call * 400000).in_dll(lib, 'wp5_log')
LOG_N = C.c_int32.in_dll(lib, 'wp5_log_n')
DL_RESULT = C.c_int32.in_dll(lib, 'wp5_download_result')
DG_ADDR = C.addressof(DG)

A = {  # original addresses
    'cdiInit': '0EAE:0006', 'cdiRegister': '0EAE:0036',
    'ampTick': '0EF6:0E56', 'ampInterrupt': '0EF6:0F4C', 'ampInit': '0EF6:0F8C',
    'ampClose': '0EF6:1014', 'ampPlayModule': '0EF6:121F', 'ampPlayPattern': '0EF6:125A',
    'ampStopModule': '0EF6:1296', 'ampPauseModule': '0EF6:12FE', 'ampResumeModule': '0EF6:1338',
    'ampGetModuleStatus': '0EF6:1372', 'ampGetTrackStatus': '0EF6:138A',
    'ampPauseTrack': '0EF6:13B9', 'ampResumeTrack': '0EF6:1407', 'ampGetPattern': '0EF6:1458',
    'ampGetRow': '0EF6:146F', 'ampGetSync': '0EF6:1484', 'ampGetTempo': '0EF6:14A0',
    'ampSetTempo': '0EF6:14B9', 'ampGetTrackData': '0EF6:14F6', 'ampBreakPattern': '0EF6:1577',
    'ampGetTickLength': '0EF6:15B9', 'ampSetTrackVolume': '0EF6:1625',
    'ampLoadModule': '105B:07F3', 'ampFreeModule': '111B:0003',
}
# argument words per CDI slot (what the device receives)
SLOT_WORDS = {0x00: 4, 0x04: 6, 0x08: 4, 0x0C: 4, 0x10: 4, 0x14: 4, 0x18: 4, 0x1C: 2, 0x20: 2,
              0x24: 2, 0x28: 2, 0x2C: 0, 0x30: 0, 0x34: 6, 0x38: 2, 0x3C: 0, 0x40: 2, 0x44: 2,
              0x48: 2, 0x4C: 2, 0x50: 2, 0x54: 2, 0x58: 2, 0x5C: 0, 0x60: 4, 0x64: 2}


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def L(v):
    v &= 0xFFFFFFFF
    return [v & 0xFFFF, v >> 16]


def pcall(e, addr, *params, ret='int'):
    words = []
    for p in reversed(params):
        words.extend(p if isinstance(p, (list, tuple)) else [p])
    return e.call(addr, *words, ret=ret)


def pstub(e, lin, nwords, f):
    """far pascal routine at linear lin -> f(words, lowest first) = DX:AX;
    EAX's high word is cleared (the result of a device call is 32-bit)."""
    def hook(uc, address, size, _):
        ss, sp = uc.reg_read(X.UC_X86_REG_SS), uc.reg_read(X.UC_X86_REG_SP)
        w = struct.unpack(f'<{2 + nwords}H', uc.mem_read(ss * 16 + sp, 4 + 2 * nwords))
        r = (f(w[2:]) or 0) & 0xFFFFFFFF
        uc.reg_write(X.UC_X86_REG_EAX, r & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_DX, r >> 16)
        uc.reg_write(X.UC_X86_REG_SP, (sp + 4 + 2 * nwords) & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_CS, w[1])
        uc.reg_write(X.UC_X86_REG_IP, w[0])
    e.u.hook_add(U.UC_HOOK_CODE, hook, begin=lin, end=lin)


class AmpEmu(Emu):
    """The original with the AMP's environment stubbed."""

    def alloc(self, nbytes, fill=0):
        """emu86's scratch heap (6200..9FF0) is too small for UNI.AMF (240 KB
        of samples); this test drives no video, so it continues at A000..F000
        (the call stack is at 0100:7000, the sentinel at F800)."""
        try:
            return super().alloc(nbytes, fill)
        except emu86.EmuError:
            if self._heap < 0xA000:
                self._heap = 0xA000
            seg = self._heap
            self._heap += (nbytes + 15) // 16 + 1
            if self._heap > 0xF000:
                raise
            self.u.mem_write(seg * 16, bytes([fill]) * nbytes)
            return seg

    def __init__(self, ems=False):
        super().__init__()
        self.stub_heap()
        self.log = []
        self.download_result = 0
        self.module = None          # (seg, off) of the MODULE for sample ids
        self.ems_mem = {}           # handle -> runtime seg
        self.ems_next = 1
        fake = self.alloc(0x800, fill=0xCB)     # RETF bytes; every routine is hooked
        self.fake = fake
        funcs = {}

        def at(k):
            return fake * 16 + k * 16, [k * 16, fake]
        # CDI device table
        table = self.alloc(0x68)
        for i, slot in enumerate(range(0, 0x68, 4)):
            lin, far = at(i)
            self.wr(table, slot, struct.pack('<HH', *far), runtime=True)
            pstub(self, lin, SLOT_WORDS[slot], self._slot_fn(slot))
        self.table = table
        lin, far = at(40)
        pstub(self, lin, 4, self._real_address)
        funcs['MCPSampleRealAddress'] = far
        if ems:
            lin, far = at(41); pstub(self, lin, 2, self._ems_alloc); funcs['EMSAlloc'] = far
            lin, far = at(42); pstub(self, lin, 7, self._ems_copyto); funcs['EMSCopyTo'] = far
            lin, far = at(43); pstub(self, lin, 1, self._ems_free); funcs['EMSFree'] = far
        self.funcs = funcs

        def access(w):                          # DDSAccessFunction(name far, out far)
            name = self._str(w[3], w[2])
            if name not in funcs:
                return 9
            self.wr(w[1], w[0], struct.pack('<HH', *funcs[name]), runtime=True)
            return 0
        pstub(self, self.lin('0DF2:0316'), 4, access)
        pstub(self, self.lin('0DF2:011B'), 4, lambda w: 0)   # DDSRegisterFunction
        # file layer thunks -> the port's vfs
        pstub(self, self.lin('16CD:0025'), 3, lambda w: c_vfs_open(self._str(w[2], w[1]).encode(), s16(w[0])))
        pstub(self, self.lin('16CD:002A'), 1, lambda w: c_vfs_close(s16(w[0])))
        pstub(self, self.lin('16CD:002F'), 5, self._read)
        pstub(self, self.lin('16CD:0039'), 4, lambda w: c_vfs_seek(s16(w[3]), s32(w[1] | w[2] << 16), s16(w[0])))
        pstub(self, self.lin('16CD:003E'), 1, lambda w: c_vfs_tell(s16(w[0])))

    def _str(self, seg, off):
        s = bytearray()
        while True:
            b = self.rd(seg, off + len(s), 1, runtime=True)[0]
            if not b:
                return s.decode('latin-1')
            s.append(b)

    def _read(self, w):
        n = s32(w[0] | w[1] << 16)
        buf = (C.c_uint8 * max(n, 1))()
        r = c_vfs_read(s16(w[4]), buf, n)
        got = n - r if 0 <= r <= n else 0
        self.wr(w[3], w[2], bytes(buf[:got]), runtime=True)
        return r

    def ref_id(self, off, seg):
        if off == 0 and seg == 0:
            return 0
        if seg == 0xFFFF:
            return 0x10000 | off
        if self.module:
            ms, mo = self.module
            io, isg = struct.unpack('<HH', self.rd(ms, mo + 0x4B, 4, runtime=True))
            n = self.rd(ms, mo + 0x4A, 1, runtime=True)[0]
            for i in range(n):
                if struct.unpack('<HH', self.rd(isg, io + i * 0x41 + 0x2E, 4, runtime=True)) == (off, seg):
                    return i + 1
        return -1

    def _slot_fn(self, slot):
        n = SLOT_WORDS[slot]

        def f(w):
            d = [s32(w[i] | w[i + 1] << 16) for i in range(0, n - 1, 2)][::-1]   # declared order
            if slot == 0x00:
                ch = d[0]
                si = self.rd(w[1], w[0], 26, runtime=True)
                so, ss = struct.unpack_from('<HH', si, 0)
                ln, ls, le = struct.unpack_from('<III', si, 4)
                mode, sid, rate, freq, vol = struct.unpack_from('<BHHIB', si, 0x10)
                self.log += [(0, ch, self.ref_id(so, ss), s32(ln)), (0x100, s32(ls), s32(le), mode | sid << 8),
                             (0x101, rate, s32(freq), vol)]
                return 0
            if slot == 0x34:
                ln = w[0] | w[1] << 16
                to, ts, so, ss = w[2], w[3], w[4], w[5]
                data = self.rd(ss, so, ln, runtime=True) if (so or ss) else b''
                h = 0
                for b in data:
                    h = (h * 31 + b) & 0xFFFFFFFF
                self.log.append((0x34, s32(h), self.ref_id(to, ts), s32(ln)))
                return self.download_result
            if slot == 0x38:
                self.log.append((0x38, self.ref_id(w[0], w[1]), 0, 0))
                return 0
            d = (d + [0, 0, 0])[:3]
            self.log.append((slot, d[0], d[1], d[2]))
            return 0
        return f

    def _real_address(self, w):                  # (handle, pos) -> far
        h, pos = w[2] | w[3] << 16, w[0] | w[1] << 16
        self.log.append((0x200, s32(h), s32(pos), 0))
        return (self.ems_mem[h & 0xFFFF] << 16) | pos

    def _ems_alloc(self, w):
        size = w[0] | w[1] << 16
        h = self.ems_next
        self.ems_next += 1
        self.ems_mem[h] = self.alloc(size)
        self.log.append((0x201, s32(size), h, 0))
        return h

    def _ems_copyto(self, w):                    # (h, src far, long off, long len)
        ln, off, so, ss, h = w[0] | w[1] << 16, w[2] | w[3] << 16, w[4], w[5], w[6]
        self.wr(self.ems_mem[h], off, self.rd(ss, so, ln, runtime=True), runtime=True)
        self.log.append((0x202, s16(h), s32(off), s32(ln)))
        return 0

    def _ems_free(self, w):
        self.log.append((0x203, s16(w[0]), 0, 0))
        return 0

    # canonical dump of the original's MODULE (same as wp5_dump_module)
    def dump(self, seg, off):
        r = lambda o, n: self.rd(seg, off + o, n, runtime=True)  # noqa: E731
        hdr = r(0, 0x5C)
        out = bytearray()
        out += hdr[0:0x29] + hdr[0x29:0x4A + 1] + hdr[0x4F:0x50] + hdr[0x54:0x56] + hdr[0x5A:0x5C]
        ch, ins, ordn = hdr[0x29], hdr[0x4A], hdr[0x4F]
        ntr = struct.unpack_from('<H', hdr, 0x54)[0]
        to, ts = struct.unpack_from('<HH', hdr, 0x56)
        tracks = [struct.unpack('<HH', self.rd(ts, to + 4 * k, 4, runtime=True)) for k in range(ntr + 1)]
        oo, os_ = struct.unpack_from('<HH', hdr, 0x50)
        for i in range(ordn):
            o = self.rd(os_, oo + i * 0x82, 0x82, runtime=True)
            out += o[0:2]
            for c in range(ch):
                p = struct.unpack_from('<HH', o, 2 + 4 * c)
                idx = 0 if p == (0, 0) else next((k for k in range(1, ntr + 1) if tracks[k] == p), 0xFFFF)
                out += struct.pack('<H', idx)
        io, isg = struct.unpack_from('<HH', hdr, 0x4B)
        for i in range(ins):
            rec = self.rd(isg, io + i * 0x41, 0x41, runtime=True)
            so, ss = struct.unpack_from('<HH', rec, 0x2E)
            ln = struct.unpack_from('<I', rec, 0x32)[0]
            out += rec[0:0x2E] + rec[0x32:0x41]
            if ss == 0xFFFF:
                out += struct.pack('<I', 0x10000 | so) + self.rd(self.ems_mem[so], 0, ln & 0xFFFF, runtime=True)
            elif so or ss:
                out += struct.pack('<I', 1) + self.rd(ss, so, ln & 0xFFFF, runtime=True)
            else:
                out += struct.pack('<I', 0)
        for i in range(1, ntr + 1):
            p = tracks[i]
            first = next(k for k in range(1, i + 1) if tracks[k] == p)
            out += struct.pack('<H', 0 if p == (0, 0) else first)
            if p != (0, 0) and first == i:
                cnt = struct.unpack('<H', self.rd(p[1], p[0], 2, runtime=True))[0]
                out += self.rd(p[1], p[0], (cnt * 3 + 3) & 0xFFFF, runtime=True)
        return bytes(out)


# ---- environment -------------------------------------------------------------
def fresh_c_files(folder):
    c_set_dir(folder.encode())
    c_stdio_reset()
    c_setupio()
    c_vfile_reset()
    c_vfs_reset()
    c_vfs_init(20)


def c_setup(folder, ems=False, time=0):
    c_dg_reset()
    c_wp5_reset()
    fresh_c_files(folder)
    c_dds_init()
    c_reg_dds(1 if ems else 0)
    LOG_N.value = 0
    DL_RESULT.value = 0
    dg_write(0xE7A0, struct.pack('<I', time))        # dsmi_time
    c_cdi_init()
    c_reg_dev()
    return c_amp_init(0)


def emu_setup(folder, ems=False, time=0):
    fresh_c_files(folder)
    e = AmpEmu(ems)
    e.wr(0x16E0, 0xE7A0, struct.pack('<I', time))
    e.call(A['cdiInit'])
    pcall(e, A['cdiRegister'], [0, e.table])
    r = s16(pcall(e, A['ampInit'], L(0))[0])
    return e, r


def c_log():
    return [(LOG[i].slot, LOG[i].a, LOG[i].b, LOG[i].c) for i in range(LOG_N.value)]


def emu_load(e, name):
    nm = e.alloc(16)
    e.wr(nm, 0, name.encode() + b'\0', runtime=True)
    out = e.alloc(4)
    r = s16(pcall(e, A['ampLoadModule'], [0, nm], L(0), [0, out])[0])
    off, seg = struct.unpack('<HH', e.rd(out, 0, 4, runtime=True))
    e.module = (seg, off) if r == 0 else None
    return r, seg, off


def c_load_mod(name):
    p = vp()
    r = c_load(name.encode(), 0, C.byref(p))
    if r == 0:
        c_set_module(p)
    return r, p


def c_dump_mod(p):
    buf = (C.c_uint8 * 0x80000)()
    n = c_dump(p, buf, len(buf))
    return bytes(buf[:n])


# DGROUP compared after playing (pointer slots skipped)
def _ranges():
    skip = set()
    for a in (0x7C33, 0x7C38, 0x7C3E, 0x7C5C, 0x7C60):
        skip.update(range(a, a + 4))
    for t in range(32):
        skip.update(range(0x7C91 + t * 45, 0x7C91 + t * 45 + 4))
    rs = [(0x7694, 0x7796), (0x7AD6, 0x7AD7), (0x7BE8, 0x8231), (0x8571, 0x85A6)]
    return rs, skip


def dg_mismatch(e):
    rs, skip = _ranges()
    a, b = e.dgroup(0, 0x10000), bytes(DG)
    bad = [(hex(i), a[i], b[i]) for lo, hi in rs for i in range(lo, hi) if i not in skip and a[i] != b[i]]
    # SAMPLEINFO (native in the port)
    for ch in range(32):
        buf = (C.c_uint8 * 26)()
        c_si(ch, buf)
        o = e.dgroup(0x8231 + ch * 26, 26)
        so, ss = struct.unpack_from('<HH', o, 0)
        ob = struct.pack('<i', e.ref_id(so, ss)) + o[4:]
        if ob != bytes(buf):
            bad.append(('si', ch, ob.hex(), bytes(buf).hex()))
    return bad


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f'#{i}: orig {x} C {y}'
    return f'length orig {len(a)} C {len(b)}'


# ---- synthetic AMF files -------------------------------------------------------
def make_amf(rnd, ver=14, nch=6, nins=8, nord=4, effects=True, big=False):
    """A random AMF of the given version (layout as ampLoadAMF reads it)."""
    out = bytearray(b'AMF' + bytes([ver]))
    out += bytes(rnd.randrange(32, 127) for _ in range(32))
    ntr_stored = nch * 3
    ntr = ntr_stored + 2                          # two logical tracks share data
    out += bytes([nins, nord]) + struct.pack('<H', ntr)
    if ver >= 9:
        out += bytes([nch])
        out += bytes(rnd.choice([0, 0xC1, 0x3F, 100, 0x20, 0xE0]) for _ in range(32 if ver >= 12 else 16))
    else:
        nch = 4
    if ver >= 13:
        out += bytes([rnd.choice([125, 120, 144, 33]), rnd.choice([6, 3, 4])])
    for i in range(nord):
        if ver >= 14:
            out += struct.pack('<H', rnd.choice([64, 32, 17]))
        out += b''.join(struct.pack('<H', rnd.randrange(0, ntr + 1)) for _ in range(nch))
    lens = []
    sidx = 0
    for i in range(nins):
        has = rnd.random() < 0.8
        if has:
            sidx += 1
        ln = rnd.choice([0, 300, 0x401, 2000, 6000]) if has else 0
        if big and has:
            ln = rnd.choice([0x300, 0x900, 3000])
        if has:
            lens.append(ln)
        rec = bytearray([1]) + bytes(rnd.randrange(65, 90) for _ in range(32 + 13))
        rate = rnd.choice([8363, 11025, 16000, 22050])
        ls = rnd.randrange(0, max(ln, 1))
        le = rnd.choice([0, ln, 0xFFFF]) if ver < 10 else rnd.choice([0, ln])
        if ver >= 10:
            rec += struct.pack('<IIHBII', sidx if has else 0, ln, rate, rnd.randrange(0, 65), ls, le)
        else:
            rec += struct.pack('<IHHBHH', sidx if has else 0, ln, rate, rnd.randrange(0, 65), ls, le)
        out += rec
    tmap = list(range(1, ntr_stored + 1)) + [1, 0]
    out += b''.join(struct.pack('<H', v) for v in tmap)
    cmds = [0x80, 0x81, 0x82, 0x83, 0x84, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E,
            0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0xFF]
    for t in range(ntr_stored):
        ent = []
        row = 0
        track0 = t % nch == 0
        while row < 64 and len(ent) < 200:
            row += rnd.choice([0, 1, 1, 2, 4])
            if row > 63:
                break
            n = rnd.randrange(1, 4)
            for k in range(n):
                if not effects or rnd.random() < 0.45:
                    ent.append((row, rnd.choice([0, 0x7F] + list(range(20, 100))), rnd.choice([0xFF, 0, 40, 64, 70])))
                else:
                    c = rnd.choice(cmds)
                    if c == 0x86 and (k == 0 and track0):
                        c = 0x82          # 0x86 first in track 0: EAX high word not reproducible
                    a = rnd.choice([0, 1, 2, 3, 0x10, 0x80, 0xF0, 0xFF, 0x34, 100, 0xC1, 6]) \
                        if c != 0x80 else rnd.randrange(nins)
                    if c == 0x95:
                        a = rnd.choice([0, 60, 125, 144, 250])
                    if c == 0x81:
                        a = rnd.choice([0, 2, 6])
                    ent.append((row, c, a))
        ent.append((0xFF, 0xFF, 0xFF))
        cnt = len(ent)
        out += struct.pack('<H', cnt) + b'\0' + b''.join(bytes(x) for x in ent)
        if ver == 1:
            out += b'\0\0\0'
    for ln in lens:
        out += bytes(rnd.randrange(256) for _ in range(ln))
    return bytes(out)


# ---- tests ---------------------------------------------------------------------
def _load_both(folder, name, ems=False):
    e, ri = emu_setup(folder, ems)
    ro, seg, off = emu_load(e, name)
    od = e.dump(seg, off) if ro == 0 else b''
    ci = c_setup(folder, ems)
    rc, p = c_load_mod(name)
    cd = c_dump_mod(p) if rc == 0 else b''
    check(ri == 0 and ci == 0, f'ampInit {ri} {ci}')
    check(ro == rc, f'{name}: ampLoadModule orig {ro} C {rc}')
    check(od == cd, f'{name}: module differs at byte {first_diff(od, cd)} ({len(od)}/{len(cd)} bytes)')
    check(e.log == c_log(), f'{name}: EMS calls differ: {first_diff(e.log, c_log())}')
    return e, seg, off, p


def test_amf_load_game_files():
    files = sorted(glob.glob(os.path.join(MUZIK, '*.AMF')))
    check(len(files) == 15, 'AMF files missing (run tools/bootstrap.sh)')
    for f in files:
        _load_both(MUZIK, os.path.basename(f))


def test_amf_load_ems_and_free():
    for name in ('6.AMF', 'PAIN.AMF'):
        e, seg, off, p = _load_both(MUZIK, name, ems=True)
        e.log.clear(); LOG_N.value = 0
        pcall(e, A['ampFreeModule'], [off, seg])
        c_free(p)
        check(e.log == c_log(), f'{name}: ampFreeModule EMS calls {first_diff(e.log, c_log())}')


def test_amf_load_versions():
    rnd = random.Random(105)
    with tempfile.TemporaryDirectory() as d:
        for ver in (1, 8, 9, 10, 11, 12, 13, 14, 7, 15):
            for k in range(2):
                name = f'V{ver}_{k}.AMF'
                open(os.path.join(d, name), 'wb').write(make_amf(rnd, ver, nch=rnd.choice([4, 6, 8]),
                                                                  big=k == 1))
                _load_both(d, name, ems=k == 1)
        open(os.path.join(d, 'BAD.AMF'), 'wb').write(b'XYZ\x0e' + bytes(100))
        _load_both(d, 'BAD.AMF')
        _load_both(d, 'NONE.AMF')


stats = []


def _play_compare(folder, name, ticks, options=0, ems=False, extra=None):
    e, _ = emu_setup(folder, ems)
    ro, seg, off = emu_load(e, name)
    c_setup(folder, ems)
    rc, p = c_load_mod(name)
    check(ro == 0 and rc == 0, f'{name}: load {ro} {rc}')
    if ro or rc:
        return
    e.log.clear(); LOG_N.value = 0
    a = s16(pcall(e, A['ampPlayModule'], [off, seg], L(options))[0])
    b = c_play(p, options)
    check(a == b, f'{name}: ampPlayModule {a} {b}')
    for i in range(ticks):
        e.call(A['ampTick'])
        c_tick()
        if extra and i in extra:
            extra[i](e, p)
        if len(e.log) != LOG_N.value or (i % 200 == 0 and e.log != c_log()):
            break
    ol, cl = e.log, c_log()
    stats.append((name, ticks, len(ol), sum(1 for x in ol if x[0] == 4)))
    check(ol == cl, f'{name}: CDI calls differ ({len(ol)}/{len(cl)}) {first_diff(ol, cl)}')
    bad = dg_mismatch(e)
    check(not bad, f'{name}: DGROUP/SAMPLEINFO differ: {bad[:6]}')
    for g, f in c_getters.items():
        x = e.call(A[g], ret='long')
        y = f()
        if g == 'ampGetTickLength':
            check(x == (y & 0xFFFFFFFF), f'{name}: {g} {x} {y}')
        else:
            check(s16(x) == s16(y), f'{name}: {g} {s16(x)} {y}')
    return e, p, seg, off


def test_amp_play_game_files():
    for f in sorted(glob.glob(os.path.join(MUZIK, '*.AMF'))):
        name = os.path.basename(f)
        _play_compare(MUZIK, name, 2500, options=1)
    for st in stats:
        check(st[3] > 50, f'{st[0]}: only {st[3]} notes in {st[1]} ticks')
    _play_compare(MUZIK, 'THEEND.AMF', 6000, options=0)      # plays to the end and stops
    _play_compare(MUZIK, 'PAIN.AMF', 300, ems=True)


def test_amp_play_synthetic():
    rnd = random.Random(0xEF6)
    with tempfile.TemporaryDirectory() as d:
        for k in range(12):
            name = f'S{k}.AMF'
            open(os.path.join(d, name), 'wb').write(make_amf(rnd, 14, nch=rnd.choice([2, 4, 6, 8, 16]),
                                                              nord=rnd.choice([1, 3, 6])))
            _play_compare(d, name, 1500, options=k & 1)


def test_amp_api():
    """getters/setters, pause/resume, track data, break, stop - same calls on both."""
    rnd = random.Random(1625)
    ops = []

    def op(e, p):
        name, arg = rnd.choice([('ampGetTrackStatus', 'trk'), ('ampPauseTrack', 'trk'),
                                ('ampResumeTrack', 'trk'), ('ampSetTempo', 'tempo'),
                                ('ampBreakPattern', 'dir'), ('ampPauseModule', None),
                                ('ampResumeModule', None), ('trackdata', 'trk'), ('tv', 'trk')])
        v = {'trk': rnd.choice([0, 1, 5, 6, 31, 32, 33, -1]), 'tempo': rnd.choice([0, 0x0300, 0x9004, 7, 0x7D06]),
             'dir': rnd.choice([1, -1, 0, 2])}.get(arg)
        if name in c_args1:
            x = s16(pcall(e, A[name], L(v))[0])
            y = c_args1[name](v)
        elif name in c_args0:
            x = s16(e.call(A[name])[0]); y = c_args0[name]()
        elif name == 'trackdata':
            x = pcall(e, A['ampGetTrackData'], L(v), ret='long')
            y = c_trackdata(v)
            x = x != 0; y = bool(y)
        else:
            vol = rnd.randrange(0, 70)
            x = s16(pcall(e, A['ampSetTrackVolume'], L(v), L(vol))[0])
            y = c_settv(v, vol)
        ops.append((name, v, x, y))
        check(x == y, f'{name}({v}): orig {x} C {y}')

    extra = {}
    for i in range(0, 1200, 37):
        def f(e, p, i=i):
            for _ in range(3):
                op(e, p)
        extra[i] = f
    _play_compare(MUZIK, 'SAVAS.AMF', 1200, options=1, extra=extra)



def test_amp_interrupt_timing():
    """ampInterrupt/ampPoll with a 100 Hz DSMI time: same ticks at the same times."""
    for name, steps in (('ISTANBUL.AMF', 900), ('SAVAS.AMF', 700)):
        e, _ = emu_setup(MUZIK, time=12345)
        ro, seg, off = emu_load(e, name)
        c_setup(MUZIK, time=12345)
        rc, p = c_load_mod(name)
        pcall(e, A['ampPlayModule'], [off, seg], L(1))
        c_play(p, 1)
        e.log.clear(); LOG_N.value = 0
        t = 12345
        rnd = random.Random(steps)
        marks = []
        for i in range(steps):
            t += rnd.choice([100, 100, 100, 200, 0, 1000])
            e.wr(0x16E0, 0xE7A0, struct.pack('<I', t))
            dg_write(0xE7A0, struct.pack('<I', t))
            e.call(A['ampInterrupt'])
            c_interrupt()
            marks.append((len(e.log), LOG_N.value))
        check(all(a == b for a, b in marks), f'{name}: call counts diverge at step '
                                              f'{next((i for i, (a, b) in enumerate(marks) if a != b), -1)}')
        check(e.log == c_log(), f'{name}: CDI calls differ {first_diff(e.log, c_log())}')
        bad = dg_mismatch(e)
        check(not bad, f'{name}: DGROUP differs {bad[:6]}')
        check(e.dgroup(0x85A2, 4) == dg_read(0x85A2, 4), f'{name}: last tick time')


def test_cdi_dispatchers():
    """The CDI wrappers AMP does not use: same device slot and arguments."""
    ops = [('0EAE:0160', 'cdiSetLinearRate', 2), ('0EAE:01DE', 'cdiSetMasterVolume', 2),
           ('0EAE:0250', 'cdiMuteAll', 1), ('0EAE:0272', 'cdiUnmuteAll', 1),
           ('0EAE:02C2', 'cdiUnloadAll', 1), ('0EAE:030A', 'cdiGetVolume', 1),
           ('0EAE:0330', 'cdiGetFrequency', 1), ('0EAE:0356', 'cdiGetPosition', 1),
           ('0EAE:037C', 'cdiGetPanning', 1), ('0EAE:03C8', 'cdiSetupChannels', 2),
           ('0EAE:03EE', 'cdiGetChannelStatus', 1), ('0EAE:0414', 'cdiPoll', 1),
           ('0EAE:043A', 'cdiGetStatus', 2), ('0EAE:0460', 'cdiSetEffect', 3)]
    e, _ = emu_setup(MUZIK)
    c_setup(MUZIK)
    e.log.clear(); LOG_N.value = 0
    rnd = random.Random(0xEAE)
    for addr, name, n in ops:
        f = fn(name, None if name == 'cdiPoll' else i32, *([i32] * n))
        for ch in (0, 5, 31, 40):
            args = [ch] + [s32(rnd.getrandbits(32)) for _ in range(n - 1)]
            x = pcall(e, addr, *[L(a) for a in args], ret='long')
            y = f(*args)
            if name != 'cdiPoll':
                check(s32(x) == y, f'{name}({args}): orig {s32(x)} C {y}')
    norm = {0x44: 0x40, 0x48: 0x40, 0x4C: 0x40, 0x58: 0x40}
    ol = [(norm.get(s, s), a, b, c) for s, a, b, c in e.log]
    check(ol == c_log(), f'CDI calls differ {first_diff(ol, c_log())}')
    check(len(ol) == len(ops) * 4, f'{len(ol)} calls')
