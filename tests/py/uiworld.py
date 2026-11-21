"""
uiworld.py - the world shared by test_grtext.py and test_intrface.py (WP11):
MEMMAN + OGRAPHS initialised the same way on both sides (as in
test_graphics.py), the fonts loaded the way main loads them
(fm_load_cmp(1, name, slot) for slots 3, 4, 5, 54h), surfaces filled with
real pictures, the screen (slot 0 = A000) with a picture.

Keyboard: both sides read the port's BIOS buffer - the original's kbhit
(0000:2E45), getch (0000:29D7) and bioskey (0000:1F3F) are stubbed with
the port's pc_kbhit / pc_getch / ui_bioskey, so a test pushes its keys with
pc_key_push before each side runs.  Mouse: pc_mouse_host (INT 33h of both).

Not a test file (run.py only imports test_*.py).
"""
import ctypes
import glob
import os
import struct
import sys

from lale import NAMES, ROOT, check, fn, lib, DG
from helpers import (make_emu, reset_c, FATALS, pc_reset, VgaState, c_mount,  # noqa: F401
                     stub_files as _stub_files, pascal_stub as _pascal_stub)

sys.path.insert(0, os.path.join(ROOT, 'tools'))
import cmpview  # noqa: E402
from unicorn import x86_const as X  # noqa: E402

C = ctypes
u8, u16, i16, u32, i32, vp, cp = C.c_uint8, C.c_uint16, C.c_int16, C.c_uint32, C.c_int32, C.c_void_p, C.c_char_p
DGA = C.addressof(DG)
A000 = C.addressof(C.c_uint8.in_dll(lib, 'pc_a000'))
VGA = C.addressof(C.c_uint8.in_dll(lib, 'pc_vga'))
WORK_PAL = C.c_void_p.in_dll(lib, 'gr_work_pal')
TARGET_PAL = C.c_void_p.in_dll(lib, 'gr_target_pal')

c_mm_alloc = fn('mm_alloc', vp, u32, u8)
c_mm_surface_alloc = fn('mm_surface_alloc', vp, u8, i16, i16, u8)
c_mm_conv_ptr = fn('mm_conv_ptr', vp, vp)
c_EMSLock = fn('EMSLock', vp, i16, i32, u16)
pc_set_virtual_time = fn('pc_set_virtual_time', None, C.c_int, u32)
pc_mouse_host = fn('pc_mouse_host', None, i16, i16, u16)
pc_key_push = fn('pc_key_push', None, u16)
pc_kbhit = fn('pc_kbhit', i16)
pc_getch = fn('pc_getch', i16)
c_fm_load_cmp = fn('fm_load_cmp', None, u8, cp, i16)


def F(name, res, *args):
    return fn(name, res, *args)


def L(v):
    return [v & 0xFFFF, (v >> 16) & 0xFFFF]


def picture(name):
    p = glob.glob(os.path.join(ROOT, 'extracted', '*', name))[0]
    w, h, pix, _ = cmpview.decode(open(p, 'rb').read())
    return w, h, pix[:w * h]


def palette_sonpal():
    return open(os.path.join(ROOT, 'orig', 'SONPAL.PAL'), 'rb').read()[:768]


def fit(w, h, pix, W, H):
    rows = []
    for y in range(H):
        row = pix[(y % h) * w:(y % h + 1) * w]
        rows.append((row * (W // w + 1))[:W])
    return b''.join(rows)


_pics = {}


def pic(name):
    if name not in _pics:
        _pics[name] = picture(name)
    return _pics[name]


class FastPc:
    """lib proxy for the emulator: 3DAh reads toggle the retrace bit"""
    def __init__(self):
        self.n = 0

    def __getattr__(self, name):
        return getattr(lib, name)

    def pc_in_3da(self):
        self.n += 1
        return (lib.vga_in8(VGA, 0x3DA) & ~8) | (8 if self.n & 1 else 0)


def mount():
    c_mount(b'3D.CKD', b'IVIR.CKD')


FONTS = ((3, b'deneyx.cmp'), (4, b'gocuk.cmp'), (5, b'irem6.cmp'), (0x54, b'irem62.cmp'))


class Orig:
    side = 'o'

    def __init__(self, mouse=(160, 100, 0), sound=0):
        pc_reset()
        pc_set_virtual_time(1, 50)
        e = self.e = make_emu(c_setup=mount)
        e.pc = FastPc()
        self.base = self.dg(0, 0x10000)
        self.vol = []

        def int10(e, uc):
            uc.reg_write(X.UC_X86_REG_AX, 0x001A)
            uc.reg_write(X.UC_X86_REG_BX, 0x0008)
            return True
        e.int_hooks[(0x10, 0x1A)] = int10

        def dos_write(e, uc):
            uc.reg_write(X.UC_X86_REG_AX, e.reg('CX'))
            e._cf(uc)
            return True
        e.int_hooks[(0x21, 0x40)] = dos_write
        _pascal_stub(e, '0EF6:1625', 4, lambda w: self.vol.append((w[2] | w[3] << 16, w[0] | w[1] << 16)) or 0)

        def div0(e, uc):
            uc.reg_write(X.UC_X86_REG_EAX, 0xFFFF)
            uc.reg_write(X.UC_X86_REG_IP, (e.reg('IP') + 3) & 0xFFFF)
            return True
        for ah in range(256):
            e.int_hooks[(0, ah)] = div0
        _stub_files(e)
        # keyboard: the port's BIOS buffer
        kbhit = lib.ui_kbhit if hasattr(lib, 'ui_kbhit') else lib.pc_kbhit
        getch = lib.ui_getch if hasattr(lib, 'ui_getch') else lib.pc_getch
        e.stub('0000:2E45', lambda e, a: (kbhit() & 0xFFFF, 0))
        e.stub('0000:29D7', lambda e, a: (getch() & 0xFFFF, 0))
        if hasattr(lib, 'ui_bioskey'):
            e.stub('0000:1F3F', lambda e, a: (lib.ui_bioskey(a(0)) & 0xFFFF, 0))
        self.c('mm_init', *L(0x240000), *L(0x440000))
        e.wr(0x16E0, 0x73D0, bytes([sound]))
        self.c('ogr_init')
        pc_mouse_host(*mouse)

    def c(self, name, *a, ret='int'):
        return self.e.call(NAMES[name], *a, ret=ret)

    def call(self, addr, *a):
        return self.e.call(addr, *a)

    def dg(self, off, n):
        return self.e.dgroup(off, n)

    def dgw(self, off, data):
        self.e.wr(0x16E0, off, data)

    def surf(self, n, w, h, t):
        self.c('mm_surface_alloc', n, w, h, t)

    def rec(self, n):
        return struct.unpack('<H', self.dg(0xE29F + 8 * n, 2))[0]

    def _io(self, recoff, size, data=None):
        e = self.e
        typ = self.dg(recoff + 8, 1)[0]
        off_, seg = struct.unpack('<HH', self.dg(recoff, 4))
        if typ == 0:
            if data is None:
                return b''.join(e.rd(seg + (o >> 4), off_, min(0x8000, size - o), runtime=True)
                                for o in range(0, size, 0x8000))
            for o in range(0, len(data), 0x8000):
                e.wr(seg + (o >> 4), off_, data[o:o + 0x8000], runtime=True)
            return None
        out = bytearray()
        for o in range(0, size, 0x4000):
            n = min(0x4000, size - o)
            ax, dx = e.call('1154:0688', n, *L(o), off_)          # pascal EMSLock
            if data is None:
                out += e.rd(dx, ax, n, runtime=True)
            else:
                e.wr(dx, ax, data[o:o + n], runtime=True)
        return bytes(out)

    def write(self, n, data):
        self._io(self.rec(n), len(data), data)

    def read(self, n):
        return self._io(self.rec(n), self.size(n))

    def read_rec(self, recoff):
        return self._io(recoff, struct.unpack('<I', self.dg(recoff + 4, 4))[0])

    def size(self, n):
        return struct.unpack('<I', self.dg(self.rec(n) + 4, 4))[0]

    def pal_write(self, which, data):
        o, s = struct.unpack('<HH', self.dg(0xE5EB if which == 'work' else 0xE5EF, 4))
        self.e.wr(s, o, data, runtime=True)

    def a000(self):
        return bytes(self.e.u.mem_read(0xA0000, 0x10000))

    def load_cmp(self, slot, name):
        seg = self.e.alloc(len(name) + 1)
        self.e.wr(seg, 0, name + b'\0', runtime=True)
        self.c('fm_load_cmp', 1, 0, seg, slot)

    def string(self, data, size=None):
        """a far string buffer -> (handle, arg words)"""
        n = size or len(data) + 1
        seg = self.e.alloc(n)
        self.e.wr(seg, 0, data + b'\0', runtime=True)
        return seg, [0, seg]

    def string_read(self, h, n):
        return self.e.rd(h, 0, n, runtime=True)

    def snap(self):
        return {'dg': self.dg(0, 0x10000), 'base': self.base, 'a000': self.a000(),
                'fatal': len(self.e.fatals)}


class Port:
    side = 'c'

    def __init__(self, mouse=(160, 100, 0), sound=0):
        reset_c(c_setup=mount)
        pc_set_virtual_time(1, 50)
        if hasattr(lib, 'ui_bioskey_reset'):
            lib.ui_bioskey_reset()
        WORK_PAL.value = None
        TARGET_PAL.value = None
        self.base = self.dg(0, 0x10000)
        F('mm_init', i16, u32, u32)(0x240000, 0x440000)
        DG[0x73D0] = sound
        F('ogr_init', None)()
        pc_mouse_host(*mouse)
        self.bufs = []

    def dg(self, off, n):
        return bytes(DG[off:off + n])

    def dgw(self, off, data):
        for i, b in enumerate(data):
            DG[off + i] = b

    def surf(self, n, w, h, t):
        c_mm_surface_alloc(n, w, h, t)

    def rec(self, n):
        return struct.unpack('<H', self.dg(0xE29F + 8 * n, 2))[0]

    def _io(self, recoff, size, data=None):
        typ = DG[recoff + 8]
        if typ == 0:
            p = c_mm_conv_ptr(DGA + recoff)
            if data is None:
                return C.string_at(p, size)
            C.memmove(p, data, len(data))
            return None
        h = struct.unpack('<H', self.dg(recoff, 2))[0]
        out = bytearray()
        for o in range(0, size, 0x4000):
            n = min(0x4000, size - o)
            p = c_EMSLock(h, o, n)
            if data is None:
                out += C.string_at(p, n)
            else:
                C.memmove(p, data[o:o + n], n)
        return bytes(out)

    def write(self, n, data):
        self._io(self.rec(n), len(data), data)

    def read(self, n):
        return self._io(self.rec(n), self.size(n))

    def read_rec(self, recoff):
        return self._io(recoff, struct.unpack('<I', self.dg(recoff + 4, 4))[0])

    def size(self, n):
        return struct.unpack('<I', self.dg(self.rec(n) + 4, 4))[0]

    def pal_write(self, which, data):
        C.memmove((WORK_PAL if which == 'work' else TARGET_PAL).value, data, 768)

    def a000(self):
        return C.string_at(A000, 0x10000)

    def load_cmp(self, slot, name):
        c_fm_load_cmp(1, name, slot)

    def string(self, data, size=None):
        b = C.create_string_buffer(data, size or len(data) + 1)
        self.bufs.append(b)
        return b, C.addressof(b)

    def string_read(self, h, n):
        return h.raw[:n]

    def snap(self):
        return {'dg': self.dg(0, 0x10000), 'base': self.base, 'a000': self.a000(),
                'fatal': FATALS.value}


def dg_ignore(dgo):
    ign = set(range(0xE5EB, 0xE5F3))
    ign.update((0x7F, 0x80))
    ign.update(range(0x86F0, 0x86F8))
    ign.update(range(0xE7B4, 0xE7B8))
    for i in range(4):
        ign.update(range(0xDA2C + 7 * i, 0xDA30 + 7 * i))
    ign.update(range(0x929E, 0x9460))
    ign.update(range(0xEB90, 0xEBB0))
    ign.update(range(0xE74A, 0xE74E))        # 100 Hz clock: advanced by polls
    for i in range(110):
        r = 0xDA48 + 9 * i
        if dgo[r + 8] == 0 and r != 0xE5D8:
            ign.update(range(r, r + 4))
    return ign


def compare(label, so, sc, slots_o=None, slots_c=None, ignore=()):
    ign = dg_ignore(so['dg'])
    # freed conventional records (type FFh): the original keeps the block's far
    # pointer, the port never stored it (see memman.h)
    for i in range(110):
        r = 0xDA48 + 9 * i
        if so['dg'][r + 8] == 0xFF and sc['dg'][r:r + 4] == bytes(4) and so['dg'][r + 2:r + 4] != bytes(2):
            ign.update(range(r, r + 4))
    ign.update(ignore)
    bo, bc = so['base'], sc['base']
    ign.update(i for i in range(0x10000) if bo[i] != bc[i] and so['dg'][i] == bo[i]
               and sc['dg'][i] == bc[i])
    bad = [i for i in range(0x10000) if so['dg'][i] != sc['dg'][i] and i not in ign]
    check(not bad, f'{label}: DGROUP differs at ' + ', '.join(
        f'{i:04X}:{so["dg"][i]:02X}/{sc["dg"][i]:02X}' for i in bad[:8]))
    if so['a000'] != sc['a000']:
        i = next(k for k in range(0x10000) if so['a000'][k] != sc['a000'][k])
        n = sum(1 for k in range(0x10000) if so['a000'][k] != sc['a000'][k])
        check(False, f'{label}: A000 differs at {i:04X} ({n} bytes)')
    check(so['fatal'] == sc['fatal'], f'{label}: fatal calls {so["fatal"]} vs {sc["fatal"]}')
    for k in (slots_o or {}):
        if slots_o[k] != slots_c[k]:
            a, b = slots_o[k], slots_c[k]
            i = next((j for j in range(min(len(a), len(b))) if a[j] != b[j]), min(len(a), len(b)))
            check(False, f'{label}: surface {k} differs at {i} (len {len(a)}/{len(b)})')


# slot, w, h, type, picture
SURFS = [(1, 320, 200, 1, 'HAR2.CMP'), (7, 100, 60, 0, 'TRADE.CMP'),
         (0x4B, 780, 15, 1, 'ITEMBANK.CMP')]


def world(side, mouse=(160, 100, 0), sound=0, screen='HAR4.CMP', surfs=SURFS, fonts=True):
    s = Orig(mouse, sound) if side == 'o' else Port(mouse, sound)
    if fonts:
        for slot, name in FONTS:
            s.load_cmp(slot, name)
    for n, w, h, t, p in surfs:
        s.surf(n, w, h, t)
        s.write(n, fit(*pic(p), w, h))
    if screen:
        data = fit(*pic(screen), 320, 204)
        if side == 'o':
            s.e.u.mem_write(0xA0000, data)
        else:
            C.memmove(A000, data, len(data))
    s.pal_write('target', palette_sonpal())
    s.pal_write('work', bytes(768))
    return s


PUMP_T = C.CFUNCTYPE(None)
pc_set_host_pump = fn('pc_set_host_pump', None, PUMP_T)


# Code addresses (flat seg, offset) in the original where the port calls
# pc_poll() in a busy loop that the original spins without one; a hook there
# calls pc_poll so both sides see the same number of polls.  Test modules
# add their own.
ORIG_POLL_POINTS = [(0x0C7F, 0x0722)]      # gr_wait_buttons_up, after cursor update


class Script:
    """pc_poll() host pump: the keys are pushed one every `every` polls (the
    BIOS buffer only holds 15), mouse moves (poll, x, y, buttons) are applied
    at their poll count, and the 100 Hz clock (E74A; no timer ISR is
    installed on either side) advances by clock_step per poll."""
    def __init__(self, keys=(), every=4, clock_step=0, moves=()):
        self.keys = list(keys)
        self.moves = sorted(moves)
        self.every = every
        self.step = clock_step
        self.polls = 0
        self.s = None
        self.cb = PUMP_T(self._pump)

    def _pump(self):
        self.polls += 1
        if self.keys and self.polls % self.every == 0:
            pc_key_push(self.keys.pop(0))
        while self.moves and self.moves[0][0] <= self.polls:
            pc_mouse_host(*self.moves.pop(0)[1:])
        if self.step:
            if self.s.side == 'c':
                v = struct.unpack('<I', bytes(DG[0xE74A:0xE74E]))[0]
                DG[0xE74A:0xE74E] = struct.pack('<I', (v + self.step) & 0xFFFFFFFF)
            else:
                lin = (0x16E0 + 0x824) * 16 + 0xE74A
                u = self.s.e.u
                v = struct.unpack('<I', bytes(u.mem_read(lin, 4)))[0]
                u.mem_write(lin, struct.pack('<I', (v + self.step) & 0xFFFFFFFF))

    def start(self, s):
        self.s = s
        if s.side == 'o':
            from unicorn import UC_HOOK_CODE
            for sg, off in ORIG_POLL_POINTS:
                at = (sg + 0x824) * 16 + off
                s.e.u.hook_add(UC_HOOK_CODE, lambda uc, a, n, _: lib.pc_poll(), None, at, at)
        pc_set_host_pump(self.cb)

    @staticmethod
    def stop():
        pc_set_host_pump(C.cast(None, PUMP_T))


def run(label, op, slots=(), keys=(), mouse=(160, 100, 0), sound=0, setup=None, extra=None,
        surfs=SURFS, ignore=(), every=4, clock_step=0, moves=()):
    """op(side) on both sides of a standard world; keys fed into the BIOS
    buffer by a Script pump; everything compared.  Returns the two op results."""
    res = []
    snaps = []
    for side in 'oc':
        s = world(side, mouse, sound, surfs=surfs)
        if setup:
            setup(s)
        sc = Script(keys, every, clock_step, moves)
        sc.start(s)
        try:
            r = op(s)
        finally:
            Script.stop()
        snaps.append((s.snap(), {n: s.read(n) for n in slots}, extra(s) if extra else None))
        res.append(r)
    (so, ro, xo), (sc_, rc, xc) = snaps
    compare(label, so, sc_, ro, rc, ignore)
    if extra:
        if xo != xc:
            i = next((k for k, (a, b) in enumerate(zip(xo, xc)) if a != b), min(len(xo), len(xc)))                 if isinstance(xo, list) and isinstance(xc, list) else 0
            check(False, f'{label}: extra differs at {i} ({len(xo)}/{len(xc)}): '
                         f'{str(xo[max(0, i - 2):i + 4] if i else xo)[:240]} vs '
                         f'{str(xc[max(0, i - 2):i + 4] if i else xc)[:240]}')
    check(res[0] == res[1], f'{label}: result {res[0]} vs {res[1]}')
    return res
