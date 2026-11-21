"""SYSTEMS (seg0AC6), NGRAPHS (seg0C7F) and OGRAPHS (ovl5511) against the
original.  Both sides run on the port's emulated PC (EMS, mouse, VGA ports,
INT 10h mode set); MEMMAN is initialised the same way on both, the surfaces
are filled with real pictures (extracted/*.CMP) and palettes, then one
operation runs and DGROUP, the surfaces, A000, EMS memory and the VGA state
are compared."""
import ctypes
import glob
import os
import random
import struct
import sys

from lale import NAMES, LOAD, ROOT, check, fn, lib, DG
from helpers import (make_emu, reset_c, FATALS, pc_reset, VgaState, c_mount,  # noqa: F401
                     stub_files as _stub_files, pascal_stub as _pascal_stub)

sys.path.insert(0, os.path.join(ROOT, 'tools'))
import cmpview  # noqa: E402
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE  # noqa: E402
from unicorn import x86_const as X  # noqa: E402

C = ctypes
u8, u16, i16, u32, i32, vp, cp = C.c_uint8, C.c_uint16, C.c_int16, C.c_uint32, C.c_int32, C.c_void_p, C.c_char_p
RT_DG = 0x1F04
DGA = C.addressof(DG)
A000 = C.addressof(C.c_uint8.in_dll(lib, 'pc_a000'))
FRAME = C.addressof(C.c_uint8.in_dll(lib, 'pc_ems_frame'))
VGA = C.addressof(C.c_uint8.in_dll(lib, 'pc_vga'))
ESI_HI = C.c_uint16.in_dll(lib, 'asm_esi_hi')
WORK_PAL = C.c_void_p.in_dll(lib, 'gr_work_pal')
TARGET_PAL = C.c_void_p.in_dll(lib, 'gr_target_pal')

c_mm_init = fn('mm_init', i16, u32, u32)
c_mm_alloc = fn('mm_alloc', vp, u32, u8)
c_mm_surface_alloc = fn('mm_surface_alloc', vp, u8, i16, i16, u8)
c_mm_conv_ptr = fn('mm_conv_ptr', vp, vp)
c_EMSLock = fn('EMSLock', vp, i16, i32, u16)
pc_set_virtual_time = fn('pc_set_virtual_time', None, C.c_int, u32)
pc_mouse_host = fn('pc_mouse_host', None, i16, i16, u16)
vga_bios_set_mode = fn('vga_bios_set_mode', C.c_int, vp, u8)
vga_out8 = fn('vga_out8', None, vp, u16, u8)
vga_mem_write8 = fn('vga_mem_write8', None, vp, u32, u8)
vga_mem_read8 = fn('vga_mem_read8', u8, vp, u32)


def F(name, res, *args):
    return fn(name, res, *args)


def cfn(name):
    """native address of a lib function (row routine argument)"""
    return C.cast(getattr(lib, name), vp).value


def L(v):
    return [v & 0xFFFF, (v >> 16) & 0xFFFF]


def far_rt(flat):
    s, o = flat.split(':')
    return [int(o, 16), (int(s, 16) + LOAD) & 0xFFFF]


def vga_state(planes=True):
    """VGA state bytes; without the planes and latches (chained mode: the
    port pushes pc_a000 into the planes at retrace, the emulator's A000 is
    separate - A000 is compared on its own)"""
    st = C.string_at(VGA, C.sizeof(VgaState))
    if not planes:
        st = bytes(VgaState.misc_output.offset) + st[VgaState.misc_output.offset:]
    return st


def vga_diff(a, b):
    if a == b:
        return None
    for i in range(len(a)):
        if a[i] != b[i]:
            for name, t in VgaState._fields_:
                off = getattr(VgaState, name).offset
                if off <= i < off + C.sizeof(t):
                    return f'VGA {name}+{i - off:X}: {a[i]:02X} vs {b[i]:02X}'
    return 'VGA differs'


# ---- data -----------------------------------------------------------------------
def picture(name):
    p = glob.glob(os.path.join(ROOT, 'extracted', '*', name))[0]
    w, h, pix, _ = cmpview.decode(open(p, 'rb').read())
    return w, h, pix[:w * h]


def palette(name):
    if name == 'SONPAL.PAL':
        return open(os.path.join(ROOT, 'orig', 'SONPAL.PAL'), 'rb').read()[:768]
    p = glob.glob(os.path.join(ROOT, 'extracted', '*', name))[0]
    return open(p, 'rb').read()[:768]


def rix(name='XDINTRO.RIX'):
    return open(glob.glob(os.path.join(ROOT, 'extracted', '*', name))[0], 'rb').read()


def fit(w, h, pix, W, H):
    """tile/crop a w x h picture into W x H"""
    rows = []
    for y in range(H):
        row = pix[(y % h) * w:(y % h + 1) * w]
        rows.append((row * (W // w + 1))[:W])
    return b''.join(rows)


def stream(w, h, pix, n):
    """n bytes of the picture, repeated"""
    return (pix * (n // len(pix) + 1))[:n]


# ---- the two sides -----------------------------------------------------------------
FATAL_C = FATALS


def vga_mmio(e):
    """A000-AFFF of the emulated original -> the port's VGA (planar modes)"""
    def on_read(uc, access, addr, size, value, _):
        for i in range(size):
            uc.mem_write(addr + i, bytes([vga_mem_read8(VGA, addr + i)]))

    def on_write(uc, access, addr, size, value, _):
        for i in range(size):
            vga_mem_write8(VGA, addr + i, (value >> (8 * i)) & 0xFF)
    e.u.hook_add(UC_HOOK_MEM_READ, on_read, None, 0xA0000, 0xAFFFF)
    e.u.hook_add(UC_HOOK_MEM_WRITE, on_write, None, 0xA0000, 0xAFFFF)


class FastPc:
    """lib proxy for the emulator: 3DAh reads toggle the retrace bit at every
    read (the real timing costs ~1 ms of Python per IN); the attribute
    flip-flop reset of the read still happens on pc_vga."""
    def __init__(self):
        self.n = 0

    def __getattr__(self, name):
        return getattr(lib, name)

    def pc_in_3da(self):
        self.n += 1
        return (lib.vga_in8(VGA, 0x3DA) & ~8) | (8 if self.n & 1 else 0)


def mount():
    """the port's file layer (the original's is helpers.stub_files)"""
    c_mount(b'3D.CKD', b'IVIR.CKD')


def fast_emu():
    e = make_emu(c_setup=mount)
    e.pc = FastPc()
    return e


class Orig:
    side = 'o'

    def __init__(self, mouse=(160, 100, 0), sound=0):
        pc_reset()
        pc_set_virtual_time(1, 50)
        e = self.e = fast_emu()
        self.base = self.dg(0, 0x10000)
        self.vol = []

        def int10(e, uc):                 # INT 10h AX=1A00h: VGA present
            uc.reg_write(X.UC_X86_REG_AX, 0x001A)
            uc.reg_write(X.UC_X86_REG_BX, 0x0008)
            return True
        e.int_hooks[(0x10, 0x1A)] = int10

        def dos_write(e, uc):             # printf of the error paths: swallowed
            uc.reg_write(X.UC_X86_REG_AX, e.reg('CX'))
            e._cf(uc)
            return True
        e.int_hooks[(0x21, 0x40)] = dos_write
        # ampSetTrackVolume(long track, long vol), far pascal: words vol, track
        _pascal_stub(e, '0EF6:1625', 4, lambda w: self.vol.append((w[2] | w[3] << 16, w[0] | w[1] << 16)) or 0)

        def div0(e, uc):                  # ctl_div0_isr: AX = FFFFh, resume 3 bytes on
            uc.reg_write(X.UC_X86_REG_EAX, 0xFFFF)
            uc.reg_write(X.UC_X86_REG_IP, (e.reg('IP') + 3) & 0xFFFF)
            return True
        for ah in range(256):
            e.int_hooks[(0, ah)] = div0
        _stub_files(e)                    # FILEMAN's file layer from extracted/, orig/
        self.c('mm_init', *L(0x240000), *L(0x440000))
        e.wr(0x16E0, 0x73D0, bytes([sound]))
        self.c('ogr_init')
        pc_mouse_host(*mouse)
        self.scr = e.alloc(0x8000)

    def c(self, name, *a, ret='int'):
        return self.e.call(NAMES[name], *a, ret=ret)

    def dg(self, off, n):
        return self.e.dgroup(off, n)

    def dgw(self, off, data):
        self.e.wr(0x16E0, off, data)

    def surf(self, n, w, h, t):
        self.c('mm_surface_alloc', n, w, h, t)

    def alloc(self, size, t):
        ax, dx = self.c('mm_alloc', *L(size), t)
        return ax

    def rec(self, n):
        return struct.unpack('<H', self.dg(0xE29F + 8 * n, 2))[0]

    def _io(self, recoff, size, data=None):
        e = self.e
        typ = self.dg(recoff + 8, 1)[0]
        off_, seg = struct.unpack('<HH', self.dg(recoff, 4))
        if typ == 0:
            if data is None:
                return e.rd(seg, off_, size, runtime=True) if size <= 0x10000 else b''.join(
                    e.rd(seg + (o >> 4), off_, min(0x8000, size - o), runtime=True)
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

    def write_rec(self, recoff, data):
        self._io(recoff, len(data), data)

    def read_rec(self, recoff):
        return self._io(recoff, struct.unpack('<I', self.dg(recoff + 4, 4))[0])

    def size(self, n):
        return struct.unpack('<I', self.dg(self.rec(n) + 4, 4))[0]

    def pal(self, which):          # 'work' E5EB / 'target' E5EF
        o, s = struct.unpack('<HH', self.dg(0xE5EB if which == 'work' else 0xE5EF, 4))
        return o, s

    def pal_write(self, which, data):
        o, s = self.pal(which)
        self.e.wr(s, o, data, runtime=True)

    def pal_read(self, which):
        o, s = self.pal(which)
        return self.e.rd(s, o, 768, runtime=True)

    def a000(self):
        return bytes(self.e.u.mem_read(0xA0000, 0x10000))

    def snap(self):
        """state right after the operation (before reading surfaces back)"""
        return {'dg': self.dg(0, 0x10000), 'base': self.base, 'vga': vga_state(False), 'a000': self.a000(),
                'fatal': len(self.e.fatals), 'vol': list(self.vol)}


class Port:
    side = 'c'

    def __init__(self, mouse=(160, 100, 0), sound=0):
        reset_c(c_setup=mount)
        pc_set_virtual_time(1, 50)
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

    def alloc(self, size, t):
        return c_mm_alloc(size, t) - DGA

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

    def write_rec(self, recoff, data):
        self._io(recoff, len(data), data)

    def read_rec(self, recoff):
        return self._io(recoff, struct.unpack('<I', self.dg(recoff + 4, 4))[0])

    def size(self, n):
        return struct.unpack('<I', self.dg(self.rec(n) + 4, 4))[0]

    def pal_write(self, which, data):
        C.memmove((WORK_PAL if which == 'work' else TARGET_PAL).value, data, 768)

    def pal_read(self, which):
        return C.string_at((WORK_PAL if which == 'work' else TARGET_PAL).value, 768)

    def a000(self):
        return C.string_at(A000, 0x10000)

    def snap(self):
        return {'dg': self.dg(0, 0x10000), 'base': self.base, 'vga': vga_state(False), 'a000': self.a000(),
                'fatal': FATAL_C.value}


# DGROUP bytes that legitimately differ: the palette far pointers (native in
# the port) and the far pointers of conventional MEMMAN records (unused in
# the port, see memman.h).
def dg_ignore(dgo):
    ign = set(range(0xE5EB, 0xE5F3))
    ign.update((0x7F, 0x80))                 # RTL errno (mm_init's rmdir fails)
    ign.update(range(0x86F0, 0x86F8))        # EMS far pointers (see test_ems)
    ign.update(range(0xE7B4, 0xE7B8))
    for i in range(4):                       # mm_map slot records: far pointer to the
        ign.update(range(0xDA2C + 7 * i, 0xDA30 + 7 * i))   # caller's len (stack)
    ign.update(range(0x929E, 0x9460))        # stdio FILE table: file layer stubbed in the
                                             # original, printf of error paths
    ign.update(range(0xEB90, 0xEBB0))        # 2599:0018 scaled blitter's DS variables
                                             # (blit386.c keeps them local)
    for i in range(110):
        r = 0xDA48 + 9 * i
        if dgo[r + 8] == 0 and r != 0xE5D8:
            ign.update(range(r, r + 4))
    return ign


def compare(label, so, sc, slots_o=None, slots_c=None):
    ign = dg_ignore(so['dg'])
    # bytes that already differ in the fresh images (RTL/DDS state) and that
    # neither side touched
    bo, bc = so['base'], sc['base']
    ign.update(i for i in range(0x10000) if bo[i] != bc[i] and so['dg'][i] == bo[i]
               and sc['dg'][i] == bc[i])
    bad = [i for i in range(0x10000) if so['dg'][i] != sc['dg'][i] and i not in ign]
    check(not bad, f'{label}: DGROUP differs at ' + ', '.join(
        f'{i:04X}:{so["dg"][i]:02X}/{sc["dg"][i]:02X}' for i in bad[:8]))
    d = vga_diff(so['vga'], sc['vga'])
    check(d is None, f'{label}: {d}')
    if so['a000'] != sc['a000']:
        i = next(k for k in range(0x10000) if so['a000'][k] != sc['a000'][k])
        check(False, f'{label}: A000 differs at {i:04X}')
    check(so['fatal'] == sc['fatal'], f'{label}: fatal calls {so["fatal"]} vs {sc["fatal"]}')
    for k in (slots_o or {}):
        if slots_o[k] != slots_c[k]:
            a, b = slots_o[k], slots_c[k]
            i = next((j for j in range(min(len(a), len(b))) if a[j] != b[j]), min(len(a), len(b)))
            check(False, f'{label}: surface {k} differs at {i} (len {len(a)}/{len(b)})')


# ---- a standard world: screen, cursor strip, EMS and conventional surfaces -------
SURFS = [  # slot, w, h, type, picture
    (1, 320, 200, 1, 'HAR2.CMP'), (2, 320, 204, 1, 'PCMAINTH.CMP'), (3, 323, 214, 0, 'KAPI1.CMP'),
    (4, 320, 200, 0, 'HAR3.CMP'), (5, 680, 114, 1, 'GOTY0.CMP'), (6, 400, 200, 1, 'MERDIM.CMP'),
    (0x4B, 780, 15, 1, 'ITEMBANK.CMP'), (7, 100, 60, 0, 'TRADE.CMP'), (8, 200, 150, 1, 'KAPIAN1.CMP'),
]
_pics = {}


def pic(name):
    if name not in _pics:
        _pics[name] = picture(name)
    return _pics[name]


def world(side, mouse=(160, 100, 0), sound=0, screen='HAR4.CMP', surfs=SURFS):
    s = Orig(mouse, sound) if side == 'o' else Port(mouse, sound)
    for n, w, h, t, pic in surfs:
        s.surf(n, w, h, t)
        if pic not in _pics:
            _pics[pic] = picture(pic)
        s.write(n, fit(*_pics[pic], w, h))
    if screen:
        if screen not in _pics:
            _pics[screen] = picture(screen)
        data = fit(*_pics[screen], 320, 204)
        if side == 'o':
            s.e.u.mem_write(0xA0000, data)
        else:
            C.memmove(A000, data, len(data))
    pal = palette('SONPAL.PAL')
    s.pal_write('target', pal)
    s.pal_write('work', bytes(768))
    return s


def run(label, op_o, op_c, slots=(), mouse=(160, 100, 0), sound=0, setup=None, extra=None, surfs=SURFS):
    """run op on both sides in a standard world and compare everything"""
    o = world('o', mouse, sound, surfs=surfs)
    if setup:
        setup(o)
    op_o(o)
    so = o.snap()
    ro = {n: o.read(n) for n in slots}
    xo = extra(o) if extra else None
    c = world('c', mouse, sound, surfs=surfs)
    if setup:
        setup(c)
    op_c(c)
    sc = c.snap()
    rc = {n: c.read(n) for n in slots}
    xc = extra(c) if extra else None
    compare(label, so, sc, ro, rc)
    if extra:
        check(xo == xc, f'{label}: extra {str(xo)[:80]} vs {str(xc)[:80]}')
    return o, c, so, sc


# ======================================================================================
# OGRAPHS
# ======================================================================================
def test_vga_regs():
    seq = [('ogr_set_map_mask', 0x0F), ('ogr_set_map_mask', 0x05), ('ogr_screen_off',),
           ('ogr_set_split_line', 0x12F), ('ogr_set_split_line', 0x1FF), ('ogr_set_split_line', 7),
           ('ogr_set_start_addr', 0x12345), ('ogr_set_start_addr', 0xFFFF), ('ogr_screen_on',)]
    types = {'ogr_set_map_mask': (u8,), 'ogr_set_split_line': (u16,), 'ogr_set_start_addr': (u32,),
             'ogr_screen_on': (), 'ogr_screen_off': ()}
    for mode in (0x13, 0x12):
        res = []
        for side in 'oc':
            pc_reset()
            pc_set_virtual_time(1, 50)
            vga_bios_set_mode(VGA, mode)
            if side == 'o':
                e = fast_emu()
                for name, *a in seq:
                    words = L(a[0]) if name == 'ogr_set_start_addr' else list(a)
                    e.call(NAMES[name], *words)
            else:
                for name, *a in seq:
                    F(name, None, *types[name])(*a)
            res.append(vga_state())
        d = vga_diff(*res)
        check(d is None, f'vga regs mode {mode:X}: {d}')


def test_ogr_init():
    o = Orig()
    so = o.snap()
    po, pw = o.pal('target'), o.pal('work')
    c = Port()
    sc = c.snap()
    compare('ogr_init', so, sc)
    check(DG[0xE29F] == 0xD8 and DG[0xE29F + 1] == 0xE5, 'slot 0 record')
    check(c_mm_conv_ptr(DGA + 0xE5D8) == A000, 'slot 0 -> pc_a000')
    check(TARGET_PAL.value and WORK_PAL.value, 'palettes allocated')
    # clear screen / black palette on a filled screen
    for name in ('ogr_clear_screen', 'ogr_black_palette'):
        run(name, lambda o: o.c(name), lambda c: F(name, None)())


def test_copy_pal_entries():
    r = random.Random(5)
    data = bytes(r.randrange(256) for _ in range(0x100))
    e = make_emu()
    seg = e.alloc(0x100)
    e.wr(seg, 0, data, runtime=True)
    e.call(NAMES['ogr_copy_pal_entries'], seg and 0, seg)
    buf = C.create_string_buffer(data, 0x100)
    F('ogr_copy_pal_entries', None, vp)(C.addressof(buf))
    check(e.rd(seg, 0, 0x100, runtime=True) == buf.raw, 'ogr_copy_pal_entries')


def test_mouse():
    def op(s):
        if s.side == 'o':
            s.c('ogr_mouse_init')
            s.c('ogr_mouse_set_pos', 100, 50)
            s.c('ogr_mouse_set_window', 10, 20, 200, 100)
            s.c('ogr_mouse_set_pos', 300, 190)
            px = s.e.alloc(16)
            s.c('gr_mouse_read', 0, px, 2, px, 4, px)
            s.res = s.e.rd(px, 0, 6, runtime=True)
            s.c('gr_wait_buttons_up')
        else:
            F('ogr_mouse_init', None)()
            F('ogr_mouse_set_pos', None, i16, i16)(100, 50)
            F('ogr_mouse_set_window', None, i16, i16, i16, i16)(10, 20, 200, 100)
            F('ogr_mouse_set_pos', None, i16, i16)(300, 190)
            b = (i16 * 3)()
            F('gr_mouse_read', i16, vp, vp, vp)(C.addressof(b), C.addressof(b) + 2, C.addressof(b) + 4)
            s.res = bytes(b)
            F('gr_wait_buttons_up', None)()
    o, c, so, sc = run('mouse', op, op)
    check(o.res == c.res, f'gr_mouse_read {o.res.hex()} vs {c.res.hex()}')


def test_step_fade():
    # bit 1 set (no file), delay 0; full and single-step variants
    for flags in (0x02, 0x03, 0x06, 0x07, 0x0A, 0x1B, 0x16, 0x22, 0x23, 0x3B):
        def op(s, flags=flags):
            if s.side == 'o':
                s.res = [s.c('ogr_palette_step_fade', flags, 0, 0, 0)[0] & 0xFF for _ in range(3)]
            else:
                f = F('ogr_palette_step_fade', u8, u8, cp, u16)
                s.res = [f(flags, None, 0) for _ in range(3)]
        o, c, so, sc = run(f'step_fade {flags:02X}', op, op,
                           setup=lambda s: s.pal_write('work', palette('HAR2.PAL') if glob.glob(
                               os.path.join(ROOT, 'extracted', '*', 'HAR2.PAL')) else bytes(768)),
                           extra=lambda s: (s.pal_read('work'), s.pal_read('target'), s.res))
    # without bit 1: the target is loaded with fm_load_pal and bit 0 flipped
    for flags in (0x00, 0x01, 0x20, 0x09):
        def op(s, flags=flags):
            if s.side == 'o':
                nm = s.e.alloc(16)
                s.e.wr(nm, 0, b'HAR2.PAL\0', runtime=True)
                s.res = [s.c('ogr_palette_step_fade', flags, 0, nm, 0)[0] & 0xFF for _ in range(2)]
            else:
                f = F('ogr_palette_step_fade', u8, u8, cp, u16)
                s.res = [f(flags, b'HAR2.PAL', 0) for _ in range(2)]
        run(f'step_fade load {flags:02X}', op, op,
            setup=lambda s: s.pal_write('work', palette('PAL3D.PAL')),
            extra=lambda s: (s.pal_read('work'), s.pal_read('target'), s.res))


# ======================================================================================
# NGRAPHS
# ======================================================================================
def test_set_dac():
    pal = palette('SONPAL.PAL')
    for start, count, chunks in ((0, 256, 1), (0, 256, 4), (16, 100, 3), (250, 6, 2), (0, 7, 7)):
        def op(s, a=(start, count, chunks)):
            if s.side == 'o':
                seg = s.e.alloc(768)
                s.e.wr(seg, 0, pal, runtime=True)
                s.c('gr_set_dac', a[0], a[1], 0, seg, a[2])
            else:
                F('gr_set_dac', None, u8, u16, cp, u16)(a[0], a[1], pal, a[2])
        run(f'gr_set_dac {start},{count},{chunks}', op, op)

    def op(s):
        if s.side == 'o':
            seg = s.e.alloc(768)
            s.e.wr(seg, 0, pal, runtime=True)
            s.c('gr_set_palette', 0, seg)
        else:
            F('gr_set_palette', None, cp)(pal)
    run('gr_set_palette', op, op)


def test_zones():
    r = random.Random(7)
    zones = b''.join(struct.pack('<4h', *(lambda x, y: (x, y, x + r.randrange(60), y + r.randrange(40)))(
        r.randrange(-10, 320), r.randrange(-10, 200))) for _ in range(41))
    for shape, cx, cy in ((0, 100, 50), (0x21, 30, 40), (0x2C, 200, 150), (5, 0, 0), (0x30, 310, 190)):
        def setup(s, shape=shape, cx=cx, cy=cy):
            s.dgw(0xE5F5, zones)
            s.dgw(0xE5E3, struct.pack('<h', shape))
            s.dgw(0xE5E7, struct.pack('<hh', cy, cx))

        def op(s, cx=cx, cy=cy):
            rects = ((90, 45), (0, 0), (cx - 20, cy - 10), (cx + 16, cy), (cx - 20, cy + 16))
            if s.side == 'o':
                s.res = [s.c('gr_zone_under_cursor')[0]] + [s.c('gr_cursor_in_zone', n)[0] & 0xFF
                                                          for n in range(0, 41, 2)]
                s.res += [s.c('gr_cursor_in_rect', x & 0xFFFF, y & 0xFFFF, 20, 10)[0] & 0xFF for x, y in rects]
                s.c('gr_zones_clear')
                s.res.append(s.c('gr_zone_under_cursor')[0])
            else:
                s.res = [F('gr_zone_under_cursor', i16)() & 0xFFFF] + [
                    F('gr_cursor_in_zone', u8, u8)(n) for n in range(0, 41, 2)]
                s.res += [F('gr_cursor_in_rect', u8, i16, i16, i16, i16)(x, y, 20, 10) for x, y in rects]
                F('gr_zones_clear', None)()
                s.res.append(F('gr_zone_under_cursor', i16)() & 0xFFFF)
        run(f'zones {shape:X}', op, op, setup=setup, extra=lambda s: s.res)


CB_SHAPE = C.CFUNCTYPE(i16)


def test_cursor():
    steps = [('init',), ('shape', 0), ('move', 50, 60, 0), ('update', 0), ('update', 3),
             ('move', 315, 195, 0), ('update', 3), ('shape', 12), ('update', 12), ('hide',),
             ('update', 20), ('move', 0, 0, 1), ('update', 50), ('shape', 50), ('window',),
             ('move', 200, 100, 0), ('update', 7), ('call', 9), ('shape', 9), ('hide',), ('hide',),
             ('update', 0x33)]
    keep = []

    def op(s):
        for st in steps:
            k = st[0]
            if k == 'move':
                pc_mouse_host(st[1] * 2, st[2], st[3])
            elif s.side == 'o':
                if k == 'init':
                    s.c('ogr_mouse_init')
                elif k == 'shape':
                    s.c('gr_cursor_set_shape', st[1])
                elif k == 'update':
                    s.c('gr_cursor_update', st[1])
                elif k == 'hide':
                    s.c('gr_cursor_hide')
                elif k == 'window':
                    s.c('ogr_mouse_set_window', 5, 5, 300, 180)
                else:                               # a far routine returning the shape
                    seg = s.e.alloc(16)
                    s.e.wr(seg, 0, bytes([0xB8, st[1], 0, 0xCB]), runtime=True)   # MOV AX,n; RETF
                    s.c('gr_call_then_cursor', 0, seg)
            else:
                if k == 'init':
                    F('ogr_mouse_init', None)()
                elif k == 'shape':
                    F('gr_cursor_set_shape', None, u8)(st[1])
                elif k == 'update':
                    F('gr_cursor_update', None, u8)(st[1])
                elif k == 'hide':
                    F('gr_cursor_hide', None)()
                elif k == 'window':
                    F('ogr_mouse_set_window', None, i16, i16, i16, i16)(5, 5, 300, 180)
                else:
                    cb = CB_SHAPE(lambda n=st[1]: n)
                    keep.append(cb)
                    F('gr_call_then_cursor', None, vp)(C.cast(cb, vp))
    run('cursor', op, op, slots=(9, 10))


def test_fade():
    # ms = 0: only the end values (the original's clock does not run in the
    # emulator); clamps in both directions.  Sound off (the port's AMP is
    # real); the original's ampSetTrackVolume calls are checked separately.
    cases = [(0, -64, -40), (0, 30, 20), (0, 63, 0), (0, -5, 100), (0, 0, -3), (0, -100, 0)]
    for ms, db, dv in cases:
        def setup(s):
            s.dgw(0x7318, struct.pack('<hhhhh', 40, 30, 0, 63, 64))   # 7318..7321

        def op(s, a=(ms, db, dv)):
            if s.side == 'o':
                s.c('gr_fade', a[0], a[1], a[2])
            else:
                F('gr_fade', None, u16, i16, i16)(*a)
        run(f'gr_fade {ms},{db},{dv}', op, op, setup=setup, extra=lambda s: s.pal_read('work'))
        o = world('o', sound=1)
        setup(o)
        o.c('gr_fade', ms, db, dv)
        v = max(-30, min(dv, 64 - 30))
        check(o.vol == ([(0xFFFFFFFF, 30 + v)] if v else []), f'gr_fade {db},{dv}: volume {o.vol}')


def test_blend3():
    tab = rix()
    for t1, t2, t3, size in ((0, 0, 0, 64000), (1, 0, 1, 64000), (0, 1, 1, 0x12345), (1, 1, 0, 40000),
                             (0, 0, 1, 0xFFF0), (0, 1, 0, 70000)):
        # (0, 0, 1, >FFFBh) differs: two conventional sources get one 0FFFFh
        # piece and the original's 16-bit source offsets (block at xxxx:0004)
        # wrap after FFFBh bytes - not modelled, see gr_blend3
        recs = {}

        def setup(s, a=(t1, t2, t3, size)):
            recs[s.side] = [s.alloc(a[3], t) for t in a[:3]]
            for k, rec in enumerate(recs[s.side][:2]):
                s.write_rec(rec, stream(*pic(('MERDIM.CMP', 'HAR8.CMP')[k]), a[3]))
            if s.side == 'o':
                s.tab = s.e.alloc(len(tab))
                s.e.wr(s.tab, 0, tab, runtime=True)
            else:
                s.tab = C.create_string_buffer(tab, len(tab))

        def op(s):
            rr = recs[s.side]
            if s.side == 'o':
                s.c('gr_blend3', rr[0], RT_DG, rr[1], RT_DG, rr[2], RT_DG, 0, s.tab)
            else:
                F('gr_blend3', None, vp, vp, vp, vp)(DGA + rr[0], DGA + rr[1], DGA + rr[2], C.addressof(s.tab))
        o, c, so, sc = run(f'gr_blend3 {t1}{t2}{t3} {size:X}', op, op, setup=setup, surfs=(),
                           extra=lambda s: s.read_rec(recs[s.side][2]))



def test_draw_strip():
    tab = rix()
    for slot, x, y, w, h, col in ((1, 10, 20, 100, 50, 3), (0, 0, 0, 320, 204, 0x40), (2, 300, 190, 20, 14, 7),
                                  (5, 0, 0, 680, 114, 1), (3, 5, 5, 0, 10, 2), (6, 100, 0, 300, 200, 200),
                                  (1, 300, 0, 30, 10, 5)):
        def setup(s):
            rec = s.alloc(0xC001, 1)
            s.dgw(0xE52F, struct.pack('<HH', rec, RT_DG))
            s.write_rec(rec, tab)

        def op(s, a=(x, y, w, h, slot, col)):
            if s.side == 'o':
                s.c('gr_draw_strip', *a)
            else:
                F('gr_draw_strip', None, u16, u16, u16, u16, u8, u8)(*a)
        run(f'gr_draw_strip {slot} {x},{y},{w},{h}', op, op, slots=(slot,) if slot else (), setup=setup)


# ======================================================================================
# SYSTEMS
# ======================================================================================
ROW = {'copy': ('2599:02D0', 'sys_rect_copy'), 'key': ('2599:0341', 'asm_copy_rect_key'),
       'mirror': ('2599:03C4', 'asm_copy_rect_key_mirror')}


def test_blit():
    cases = [(0, 0, 320, 200, 0, 0, 1, 0, 'copy'), (10, 20, 100, 80, 50, 60, 3, 2, 'key'),
             (0, 0, 780, 15, 0, 0, 0x4B, 5, 'copy'), (5, 5, 150, 100, 160, 100, 8, 1, 'mirror'),
             (100, 50, 220, 150, 0, 0, 6, 4, 'key'), (0, 0, 680, 114, 0, 50, 5, 6, 'copy'),
             (0, 0, 0, 10, 0, 0, 1, 2, 'copy'), (20, 0, 300, 200, 10, 0, 2, 4, 'copy'), (0, 0, 100, 60, 0, 0, 7, 8, 'key'),
             (0, 0, 320, 204, 0, 0, 0, 2, 'copy')]
    for a in cases:
        def op(s, a=a):
            if s.side == 'o':
                s.c('sys_blit', *a[:8], *far_rt(ROW[a[8]][0]))
            else:
                F('sys_blit', None, *[u16] * 6, u8, u8, vp)(*a[:8], cfn(ROW[a[8]][1]))
        run(f'sys_blit {a}', op, op, slots=(a[7],) if a[7] else ())


def test_blit_recolor():
    for a in ((0, 0, 100, 15, 10, 10, 0x4B, 1, 0, 0x0F), (5, 5, 300, 190, 0, 0, 2, 3, 0x20, 0x21),
              (0, 0, 680, 114, 0, 0, 5, 6, 0x10, 1)):
        def op(s, a=a):
            if s.side == 'o':
                s.c('sys_blit_recolor', *a)
            else:
                F('sys_blit_recolor', None, *[u16] * 6, u8, u8, u8, u8)(*a)
        run(f'sys_blit_recolor {a}', op, op, slots=(a[7],))


def test_fill_rect():
    for a in ((10, 10, 100, 50, 1, 7), (0, 0, 320, 200, 4, 0xAB), (0, 0, 680, 114, 5, 3),
              (300, 190, 20, 10, 0, 9), (0, 0, 320, 204, 2, 0x55), (100, 100, 0, 3, 1, 1),
              (50, 0, 350, 200, 6, 0x42)):
        def op(s, a=a):
            if s.side == 'o':
                s.c('sys_fill_rect', *a)
            else:
                F('sys_fill_rect', None, u16, u16, u16, u16, u8, u8)(*a)
        run(f'sys_fill_rect {a}', op, op, slots=(a[4],) if a[4] else ())


def test_fill_slot():
    for a in ((1, 64000, 3), (4, 0xFFFF, 0x80), (0, 64000, 0), (5, 1000, 7), (6, 0xFFFF, 1), (3, 5, 9)):
        def op(s, a=a):
            if s.side == 'o':
                s.c('sys_fill_slot', *a)
            else:
                F('sys_fill_slot', None, u16, u16, u8)(*a)
        run(f'sys_fill_slot {a}', op, op, slots=(a[0],) if a[0] else ())


def test_lines():
    r = random.Random(3)
    lines = [(0, 0, 319, 199), (319, 0, 0, 199), (10, 10, 10, 10), (5, 100, 300, 100), (300, 100, 5, 100),
             (100, 5, 100, 190), (100, 190, 100, 5), (0, 50, 200, 52)] + \
        [(r.randrange(320), r.randrange(200), r.randrange(320), r.randrange(200)) for _ in range(12)]
    for slot in (0, 1, 4):
        def op(s, slot=slot):
            for i, ln in enumerate(lines):
                if s.side == 'o':
                    s.c('sys_fill_slot_rect', *ln, i + 1, slot)
                else:
                    F('sys_fill_slot_rect', None, i16, i16, i16, i16, u8, u8)(*ln, i + 1, slot)
            for x, y, w, h in ((10, 10, 100, 50), (0, 0, 319, 199), (200, 150, -50, -30)):
                if s.side == 'o':
                    s.c('sys_fill_rect_checked', x & 0xFFFF, y & 0xFFFF, w & 0xFFFF, h & 0xFFFF, slot, 0xEE)
                else:
                    F('sys_fill_rect_checked', None, i16, i16, i16, i16, u16, u8)(x, y, w, h, slot, 0xEE)
        run(f'lines slot {slot}', op, op, slots=(slot,) if slot else ())


def test_blit_scaled():
    cases = [(0, 0, 320, 200, 0, 0, 160, 100, 1, 0, 0), (0, 0, 100, 60, 10, 10, 300, 180, 7, 2, 1),
             (0, 0, 680, 114, 0, 0, 320, 200, 5, 6, 0), (20, 20, 150, 100, 5, 5, 100, 75, 8, 4, 1), (0, 0, 200, 150, 0, 0, 320, 200, 8, 1, 1),
             (0, 0, 323, 214, 0, 0, 320, 204, 3, 1, 0)]
    for a in cases:
        def op(s, a=a):
            if s.side == 'o':
                s.c('sys_blit_scaled', *a)
            else:
                ESI_HI.value = 0              # call() starts with ESI = 0
                F('sys_blit_scaled', None, *[u16] * 8, u8, u8, u8)(*a)
        run(f'sys_blit_scaled {a}', op, op, slots=(a[9],) if a[9] else ())


def test_blit_0615():
    # two big EMS blocks do not fit the page frame together (EMSLockRegion
    # maps whole blocks: the original prints, waits for a key, fatal 10h)
    for a, f in (((4, 1, 64000, 0), ('09D4:0006', 'sys_span_memcpy')),
                 ((4, 1, 64000, 0x20), ('09D4:004C', 'mm_copy_transparent')),
                 ((1, 3, 64000, 0x10), ('09D4:004C', 'mm_copy_transparent')),
                 ((7, 4, 6000, 0x00), ('09D4:0006', 'sys_span_memcpy'))):
        def op(s, a=a, f=f):
            if s.side == 'o':
                s.c('sys_blit_0615', a[0], a[1], a[2], a[3], *far_rt(f[0]))
            else:
                F('sys_blit_0615', None, u8, u8, u16, u8, vp)(*a, cfn(f[1]))
        run(f'sys_blit_0615 {a} {f[1]}', op, op, slots=(a[1],))


def test_blit_0F21():
    strips = ['sys_line_10F1', 'sys_line_1185', 'sys_line_1261', 'sys_line_1307']
    for name in strips:
        for a in ((0, 0, 40, 60, 100, 20, 300, 2, 1, 2), (10, 5, 8, 100, 200, 0, 250, 1, 6, 4),
                  (0, 0, 30, 114, 160, 0, 320, 0, 5, 6), (0, 0, 4, 90, 300, 10, 310, 3, 3, 1)):
            def op(s, a=a, name=name):
                if s.side == 'o':
                    s.c('sys_blit_0F21', *a[:7], a[7], a[8], a[9], *far_rt(NAMES[name]))
                else:
                    F('sys_blit_0F21', None, *[u16] * 7, u8, u8, u8, vp)(*a, cfn(name))
            run(f'sys_blit_0F21 {name} {a}', op, op, slots=(a[9],))


def test_putpixel_and_colors():
    pals = [palette(p) for p in ('SONPAL.PAL', 'PAL3D.PAL', 'HAR2.PAL', 'IKON0.PAL')]
    e = fast_emu()
    seg = e.alloc(0x1000)
    for pal in pals:
        e.wr(seg, 0, pal + pals[0], runtime=True)
        for a in range(0, 256, 17):
            for b in (0, 5, 128, 255):
                o = e.call(NAMES['sys_find_color'], 0, seg, a, b)[0]
                cc = F('sys_find_color', u16, cp, u16, u16)(pal, a, b)
                check(o == cc, f'find_color {a},{b}: {o:X} vs {cc:X}')
            o = e.call(NAMES['sys_find_glyph'], 0, seg, a, 0x300, seg)[0]
            cc = F('sys_find_glyph', u16, cp, u16, cp)(pal, a, pals[0])
            check(o == cc, f'find_glyph {a}: {o:X} vs {cc:X}')
    # not found: all-63 palette vs a 0 reference
    e.wr(seg, 0, bytes(3) + b'\x3f' * 765 + b'\x3f' * 768, runtime=True)
    o = e.call(NAMES['sys_find_glyph'], 0, seg, 0, 0x300, seg)[0]
    cc = F('sys_find_glyph', u16, cp, u16, cp)(bytes(3) + b'\x3f' * 765, 0, b'\x3f' * 768)
    check(o == cc, f'find_glyph not found {o:X} {cc:X}')
    # putpixel (16-bit offset wrap included)
    big = e.alloc(0x10000)
    buf = C.create_string_buffer(0x10000)
    for x, y, col, pitch in ((0, 0, 1, 320), (319, 199, 2, 320), (100, 300, 3, 320), (5, 7, 4, 0x2000)):
        e.call(NAMES['sys_putpixel'], x, y, col, 0, big, pitch)
        F('sys_putpixel', None, u16, u16, u8, vp, u16)(x, y, col, C.addressof(buf), pitch)
    check(e.rd(big, 0, 0x10000, runtime=True) == buf.raw[:0x10000], 'putpixel')


def test_build_aa_table():
    for n in (0, 1, 7, 128, 254, 255):
        def op(s, n=n):
            if s.side == 'o':
                t = s.e.alloc(0x8000)
                s.c('sys_build_aa_table', 0, t, n)
                s.res = s.e.rd(t, 0, 0x8000, runtime=True)
            else:
                b = C.create_string_buffer(0x8000)
                F('sys_build_aa_table', None, vp, i16)(C.addressof(b), n)
                s.res = b.raw
        run(f'build_aa_table {n}', op, op, extra=lambda s: s.res)


def test_rix():
    def op(s):
        if s.side == 'o':
            nm = s.e.alloc(32)
            s.e.wr(nm, 0, b'XDINTRO.RIX\0', runtime=True)
            nm2 = s.e.alloc(32)
            s.e.wr(nm2, 0, b'XD.RIX\0', runtime=True)
            s.res = [s.c('sys_load_rix', 0, nm)[0] & 0xFF, s.c('sys_load_rix', 0, nm)[0] & 0xFF,
                     s.c('sys_load_rix', 0, nm2)[0] & 0xFF]
            s.res.append(s.read_rec(struct.unpack('<H', s.dg(0xE52F, 2))[0]))
            s.e.call('0AC6:1B58')
        else:
            f = F('sys_load_rix', u8, cp)
            s.res = [f(b'XDINTRO.RIX'), f(b'XDINTRO.RIX'), f(b'XD.RIX')]
            s.res.append(s.read_rec(struct.unpack('<H', s.dg(0xE52F, 2))[0]))
            F('sys_free_rix', None)()
    run('rix', op, op, setup=lambda s: s.dgw(0xE56C, b'\x11\x22\x33\x44'), extra=lambda s: s.res)


def test_rowcopy_planar():
    r = random.Random(11)
    w, h, pix = picture('HAR9.CMP')
    src = bytes(pix[:0x4000])
    cases = [(0, 0, 320, 20, 640, 0, 0, 640), (3, 0, 101, 10, 640, 3, 5, 640), (8, 2, 64, 7, 640, 16, 30, 640),
             (5, 1, 37, 4, 640, 13, 100, 640), (2, 0, 6, 3, 640, 10, 7, 640)]
    for rd_map in (0, 2):
        for a in cases:
            res = []
            for side in 'oc':
                pc_reset()
                vga_bios_set_mode(VGA, 0x12)
                vga_out8(VGA, 0x3CE, 4)
                vga_out8(VGA, 0x3CF, rd_map)
                for p in range(4):
                    pl = (C.c_uint8 * 0x10000).from_address(VGA + 0x10000 * p)
                    for i in range(0, 0x9600, 7):
                        pl[i] = (i * (p + 3)) & 0xFF
                if side == 'o':
                    e = make_emu()
                    vga_mmio(e)
                    seg = e.alloc(0x4000)
                    e.wr(seg, 0, src, runtime=True)
                    e.wr(seg, 0xFFFF, b'Z', runtime=True)  # src[-1] at offset 0 wraps to FFFFh
                    ret = e.call(NAMES['sys_rowcopy_planar'], 0, seg, a[0], a[1], a[2], a[3], a[4],
                                 0, 0xA000, a[5], a[6], a[7], 0xFE, ret='long')
                else:
                    buf = C.create_string_buffer(b'Z' + src)   # src[-1]: as the original's
                    ret = F('sys_rowcopy_planar', i32, vp, u16, u16, u16, u16, u16, vp, u16, u16, u16, u8)(
                        C.addressof(buf) + 1, a[0], a[1], a[2], a[3], a[4], A000, a[5], a[6], a[7], 0xFE)
                res.append((ret, vga_state()))
            check(res[0][0] == res[1][0], f'rowcopy {a} ret {res[0][0]} vs {res[1][0]}')
            d = vga_diff(res[0][1], res[1][1])
            check(d is None, f'rowcopy {a} rd{rd_map}: {d}')
