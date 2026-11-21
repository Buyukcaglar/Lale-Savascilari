"""GFL/GHD video player (ovl5313, src/game/gfl.c) against the original.

* gfl_decode_frame: the original (unicorn) and the C on real frames of every
  video in extracted/0GFL (start state = the previous frame as decoded by
  tools/gflview.py), plus random streams; the C and gflview.py on every
  frame of every video.
* The player: the original and the C with the same environment (MEMMAN +
  DSMI EMS heap on the port's emulated EMS, test_ems.make_emu), the vfs
  served from the 0GFL archive (C: real vfs on orig/0GFL.CKD; original:
  Python stand-ins of o569A with the same member semantics, and of FILEMAN's
  filelength), the real NGRAPHS palette upload on both sides (the emulated
  VGA DAC is compared), the far heap of both sides starting from the port's
  state after the mount (helpers.FarHeap), CDI recorded (C: wp5 device).
  Time: every pc_poll() of the port (kbhit, the streaming loop) runs a
  scripted pump that advances g_clock_ms (E74A); the original gets the same
  pump at kbhit and at the streaming-loop test (5313:10FF).  Compared after
  every step: GFL/MEMMAN/EMS-heap DGROUP, the EMS memory, the frame buffer,
  surface 0 (the screen), the VGA DAC, the CDI calls, fatal errors.
"""
import ctypes
import os
import random
import struct
import sys
import zlib

import unicorn as U
import unicorn.x86_const as X

from lale import NAMES, ROOT, check, fn, lib, DG
from helpers import make_emu, reset_c, ems_mem, FATALS, EMS_DG, s16, vga_dac

sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gflview  # noqa: E402

C = ctypes
u8, u16, i16, i32, u32, vp, cp = (C.c_uint8, C.c_uint16, C.c_int16, C.c_int32, C.c_uint32,
                                  C.c_void_p, C.c_char_p)
GDIR = os.path.join(ROOT, 'extracted', '0GFL')
ORIG = os.path.join(ROOT, 'orig')
RT_DG = 0x1F04
DGA = C.addressof(DG)

c_decode = fn('gfl_decode_frame', None, vp, vp, u16)
c_load_ghd = fn('gfl_load_ghd', None, cp)
c_open_gfl = fn('gfl_open_gfl', None, cp)
c_close = fn('gfl_close', None)
c_update_clock = fn('gfl_update_frame_clock', None)
c_show = fn('gfl_show_frame', None, u8)
c_fill = fn('gfl_stream_fill', None)
c_play_loop = fn('gfl_play_loop', C.c_char)
c_play = fn('gfl_play', C.c_char, cp)
c_ring_read = fn('gfl_ring_read_far', None, vp, vp, u32, u16, u32, u32)
c_alloc_slot = fn('gfl_alloc_sample_slot', None, i16, u16, i16)

c_mm_init = fn('mm_init', i16, u32, u32)
c_mm_surface_alloc = fn('mm_surface_alloc', vp, u8, i16, i16, u8)
c_mm_conv_ptr = fn('mm_conv_ptr', vp, vp)
c_set_dir = fn('pc_set_game_dir', None, cp)
c_stdio_reset = fn('bc_stdio_reset', None)
c_setupio = fn('bc_setupio', None)
c_vfile_reset = fn('vfile_reset', None)
c_vfs_reset = fn('vfs_reset', None)
c_vfs_init = fn('vfs_init', i16, i16)
c_vfs_mount = fn('vfs_mount_archive', i16, cp, vp)
c_cdi_init = fn('cdiInit', None)
c_wp5_reg = fn('wp5_register_device', i16)
c_wp9_setup = fn('wp9_setup', None)
c_pc_set_host_pump = fn('pc_set_host_pump', None, vp)
c_pc_set_virtual = fn('pc_set_virtual_time', None, C.c_int, u32)

WP5_LOG = (C.c_int32 * 4 * 400000).in_dll(lib, 'wp5_log')
WP5_N = C.c_int32.in_dll(lib, 'wp5_log_n')
WP9_STEPS = (C.c_int32 * 256).in_dll(lib, 'wp9_steps')
WP9_POLLS = C.c_int32.in_dll(lib, 'wp9_polls')
WP9_KEY_POLL = C.c_int32.in_dll(lib, 'wp9_key_poll')
WP9_KEY = C.c_uint16.in_dll(lib, 'wp9_key')
TARGET_PAL = C.c_void_p.in_dll(lib, 'gr_target_pal')
C_PAL = (C.c_uint8 * 0x300)()               # the port's target palette (E5EF)
GFL_INDEX = C.c_void_p.in_dll(lib, 'gfl_index')

VIDEOS = sorted(os.path.basename(p)[:-4] for p in os.listdir(GDIR) if p.endswith('.GHD')) \
    if os.path.isdir(GDIR) else []


def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def L(v):
    return [v & 0xFFFF, (v >> 16) & 0xFFFF]


# ============================================================================
# gfl_decode_frame
# ============================================================================
def emu_decode(e, dseg, sseg, state, src):
    e.wr(dseg, 4, state, runtime=True)
    e.wr(sseg, 0, bytes(src) + bytes(0x200), runtime=True)    # same bytes past the end as the C
    e.call(NAMES['gfl_decode_frame'], e.far(dseg, 4), e.far(sseg, 0), len(src) & 0xFFFF)
    return e.rd(dseg, 4, len(state), runtime=True)


def c_decode_buf(state, src):
    buf = C.create_string_buffer(bytes(state), len(state))
    s = C.create_string_buffer(bytes(src) + b'\0' * 0x200, len(src) + 0x200)
    c_decode(buf, s, len(src) & 0xFFFF)
    return buf.raw


def test_decode_real_frames():
    if not check(VIDEOS, 'extracted/0GFL missing'):
        return
    from lale import Emu
    e = Emu()
    dseg = e.alloc(0x10000)
    sseg = e.alloc(0x10000)
    rnd = random.Random(5313)
    n_emu = 0
    for name in VIDEOS:
        v = gflview.Video(os.path.join(GDIR, name))
        nf = v.h['nframes']
        pick = set(range(min(4, nf))) | set(rnd.sample(range(nf), min(3, nf)))
        buf = bytearray(gflview.FRAME + 256)
        cbuf = C.create_string_buffer(gflview.FRAME + 256)
        for i in range(nf):
            typ, _, _, video = v.frame_parts(i)
            if typ == 1:
                continue
            before = bytes(buf)
            gflview.decode(buf, video)
            src = C.create_string_buffer(video + b'\0' * 0x200, len(video) + 0x200)
            c_decode(cbuf, src, len(video))
            if not check(cbuf.raw == bytes(buf), f'{name} frame {i}: C != gflview'):
                break
            if i in pick:
                n_emu += 1
                o = emu_decode(e, dseg, sseg, before, video)
                check(o == bytes(buf), f'{name} frame {i}: original != gflview')
    check(n_emu > 100, f'only {n_emu} frames through the original')


def test_decode_synthetic():
    """Random code streams (overshooting runs, len 0, literal runs at the
    end) on random start buffers: original vs C, with 256 bytes of slack."""
    from lale import Emu
    e = Emu()
    dseg = e.alloc(0x10000)
    sseg = e.alloc(0x10000)
    rnd = random.Random(390)
    for t in range(60):
        state = bytes(rnd.randrange(256) for _ in range(64000 + 256))
        kind = t % 4
        if kind == 0:
            src = bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 3000)))
        elif kind == 1:               # mostly long skips + fills: reaches the end, overshoots
            src = bytes(rnd.choice([0x7F, 0x3F, 0x3E, 0x2F, rnd.randrange(256)])
                        for _ in range(rnd.randrange(500, 2000)))
        elif kind == 2:               # long literals
            out = bytearray()
            while len(out) < 70000:
                n = rnd.randrange(128)
                out.append(0x80 | n)
                out += bytes(rnd.randrange(256) for _ in range(n + 1))
            src = bytes(out[:rnd.randrange(60000, 65000)])
        else:
            src = bytes([rnd.randrange(256)]) + bytes(rnd.randrange(256) for _ in range(5))
            src = src[:rnd.choice([0, 1, 2, 6])] if t % 8 == 3 else src
        o = emu_decode(e, dseg, sseg, state, src if src else b'\x05\x01')
        if not src:                   # len 0: one code is still decoded (from the buffer)
            src_mem = b'\x05\x01'
            buf = C.create_string_buffer(state, len(state))
            s = C.create_string_buffer(src_mem + b'\0' * 0x200, 0x202)
            c_decode(buf, s, 0)
            e.wr(dseg, 4, state, runtime=True)
            e.call(NAMES['gfl_decode_frame'], e.far(dseg, 4), e.far(sseg, 0), 0)
            o = e.rd(dseg, 4, len(state), runtime=True)
            check(o[:64000] == buf.raw[:64000], f'synthetic {t}: len 0')
            continue
        c = c_decode_buf(state, src)
        # the port drops what the last code writes past pixel 64000 (gfl.c)
        check(o[:64000] == c[:64000], f'synthetic {t} (kind {kind}, {len(src)} bytes): differ at '
              f'{next((i for i in range(64000) if o[i] != c[i]), None)}')
        check(c[64000:] == state[64000:], f'synthetic {t}: C wrote past pixel 64000')


# ============================================================================
# the player: environment
# ============================================================================
GFL_DG = [(0xD5F2, 0xD61C), (0xD620, 0xD930), (0x6BD9, 0x6BE6), (0xE284, 0xE29B),
          (0xE74A, 0xE74E), (0xDE26, 0xDE30)]
MM_DG = [(0x6E02, 0x6E0E), (0x750A, 0x750B), (0xD9F8, 0xDA2C), (0xDE30, 0xDE34),
         (0xE29B, 0xE29B + 90 * 8)] + [(0xDA30 + 7 * i, 0xDA33 + 7 * i) for i in range(4)]
CMP_DG = GFL_DG + MM_DG + EMS_DG
MM_INIT = (0x240000, 0x440000)
CLOCK0 = 0x12345678


def pascal_stub(e, addr, nwords, func):
    """Replace a far pascal routine (callee pops nwords argument words)."""
    s, o = (int(x, 16) for x in addr.split(':'))
    lin = e.lin(s, o)

    def hook(uc, address, size, _):
        sp, ss = uc.reg_read(X.UC_X86_REG_SP), uc.reg_read(X.UC_X86_REG_SS)

        def arg(i):
            return struct.unpack('<H', uc.mem_read(ss * 16 + ((sp + 4 + 2 * i) & 0xFFFF), 2))[0]
        r = func(e, arg) or (0, 0)
        ip, cs = struct.unpack('<HH', uc.mem_read(ss * 16 + sp, 4))
        uc.reg_write(X.UC_X86_REG_AX, r[0] & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_DX, r[1] & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_SP, (sp + 4 + 2 * nwords) & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_CS, cs)
        uc.reg_write(X.UC_X86_REG_IP, ip)
    e.u.hook_add(U.UC_HOOK_CODE, hook, begin=lin, end=lin)


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


class ArchiveFiles:
    """o569A over 0GFL members (the vfs.c member semantics the GFL code
    can reach: read clamping, seek SET/CUR/END, tell)."""
    def __init__(self):
        self.h = {}
        self.next = 1

    def open(self, name):
        p = os.path.join(GDIR, name.upper())
        if not os.path.isfile(p):
            return -1
        h = next(i for i in range(100) if i not in self.h)     # lowest free record (vfs.c)
        self.h[h] = [open(p, 'rb').read(), 0]
        return h

    def read(self, h, n):
        data, pos = self.h[h]
        if pos + n > len(data):
            cnt = len(data) - pos
            self.h[h][1] = len(data) - 1
            return data[pos:pos + cnt], cnt
        self.h[h][1] = pos + n
        return data[pos:pos + n], 0

    def seek(self, h, off, whence):
        data, pos = self.h[h]
        size = len(data)
        if whence == 0:
            pos = min(off, size)
        elif whence == 1:
            pos = min(pos + off, size)
        elif whence == 2:
            if -off > size:
                return -1
            pos = size if off > 0 else size - off
        else:
            return -1
        self.h[h][1] = pos
        return 0


class Script:
    """Pump schedule shared by both sides: clock += steps[polls & 255] per
    poll (the pattern repeated), key pushed at poll key_poll."""
    def __init__(self, steps, key_poll=-1, key=0):
        self.steps = (list(steps) * 256)[:256]          # repeated
        self.key_poll, self.key = key_poll, key


def set_mode13():
    """Retrace timing of port 3DAh for the player's wait loops.  A tiny CRTC
    frame (2 lines, retrace on line 1) instead of mode 13h: src/platform/
    vga.c status1() takes the clock modulo frame*1000 us, i.e. a mode-13h
    retrace cycle lasts 14 s of emulated time (reported), which the
    original's 3DAh loop in unicorn cannot wait through.  Both sides get
    the same VGA state, so the wait loops end after the same reads."""
    a = C.addressof(C.c_uint8.in_dll(lib, 'pc_vga'))
    lib.vga_out8.argtypes = (vp, u16, u8)
    for idx, val in ((0x11, 2), (0x00, 0), (0x06, 0), (0x07, 0), (0x10, 1)):   # 11h: unprotect
        lib.vga_out8(a, 0x3D4, idx)
        lib.vga_out8(a, 0x3D5, val)


def c_files():
    """the port's file layer: stdio, vfile, vfs with 0GFL.CKD mounted"""
    c_stdio_reset()
    c_vfile_reset()
    c_vfs_reset()
    c_set_dir(ORIG.encode())
    c_setupio()
    c_vfs_init(20)
    c_vfs_mount(b'0GFL.CKD', None)


def orig_env(sc, sound=1, channels=8, mode=None, heap_free=None):
    lib.pc_reset()
    c_pc_set_host_pump(None)
    c_pc_set_virtual(1, 0)
    set_mode13()
    e = make_emu(heap_free, c_setup=c_files)
    e.call(NAMES['cdiInit'])           # registers cdiRegister with DDS (far heap), as the C side
    e.cdi, e.amp = [], []
    e.polls, e.keys = 0, []
    e.sc = sc
    files = ArchiveFiles()

    def v_open(e, a):
        return files.open(e._asciiz(a(2), a(1))) & 0xFFFF, 0

    def v_read(e, a):
        n = (a(1) << 16) | a(0)
        data, r = files.read(a(4), n)
        e.wr(a(3), a(2), data, runtime=True)
        return r & 0xFFFF, (r >> 16) & 0xFFFF

    def v_seek(e, a):
        return files.seek(a(3), s32((a(2) << 16) | a(1)), a(0)) & 0xFFFF, 0

    def v_tell(e, a):
        p = files.h[a(0)][1]
        return p & 0xFFFF, p >> 16

    def v_close(e, a):
        files.h.pop(a(0), None)
        return 0, 0
    pascal_stub(e, '16CD:0025', 3, v_open)
    pascal_stub(e, '16CD:002F', 5, v_read)
    pascal_stub(e, '16CD:0039', 4, v_seek)
    pascal_stub(e, '16CD:003E', 1, v_tell)
    pascal_stub(e, '16CD:002A', 1, v_close)

    def filelength(e, a):              # FILEMAN 5447:0873 (the C: fm_filelength over vfs)
        n = len(files.h[a(0)][0])
        return n & 0xFFFF, n >> 16

    def set_sample(e, a):              # as tests/stubs/wp5_stubs.c r_setSample
        si = e.rd(a(1), a(0), 0x1A, runtime=True)
        off, seg, ln, ls, le, mode, sid, rate, freq, vol = struct.unpack('<HHIIIBHHIB', si)
        ref = 0x10000 | off if seg == 0xFFFF else (0 if (off | seg) == 0 else -1)
        ch = s32((a(3) << 16) | a(2))
        e.cdi += [(0x00, ch, ref, s32(ln)), (0x100, s32(ls), s32(le), mode | sid << 8),
                  (0x101, rate, s32(freq), vol)]
    e.stub('16B5:0025', filelength)
    pascal_stub(e, '0EAE:008F', 4, set_sample)
    pascal_stub(e, '0EAE:01B4', 4, lambda e, a: e.cdi.append(
        (0x18, s32((a(3) << 16) | a(2)), s32((a(1) << 16) | a(0)), 0)))
    pascal_stub(e, '0EAE:00B8', 6, lambda e, a: e.cdi.append(
        (0x04, s32((a(5) << 16) | a(4)), s32((a(3) << 16) | a(2)), s32((a(1) << 16) | a(0)))))
    pascal_stub(e, '0EF6:1577', 2, lambda e, a: e.amp.append(s32((a(1) << 16) | a(0))) or (0xFFFF, 0))
    pascal_stub(e, '0EAE:0356', 2, lambda e, a: e.cdi.append((0x40, s32((a(1) << 16) | a(0)), 0, 0)))
    pascal_stub(e, '0EAE:018A', 4, lambda e, a: e.cdi.append(
        (0x14, s32((a(3) << 16) | a(2)), s32((a(1) << 16) | a(0)), 0)))
    pascal_stub(e, '0EAE:00E6', 2, lambda e, a: e.cdi.append((0x28, s32((a(1) << 16) | a(0)), 0, 0)))

    def pump(e):
        c = struct.unpack('<I', e.dgroup(0xE74A, 4))[0]
        c = (c + e.sc.steps[e.polls & 255]) & 0xFFFFFFFF
        e.wr(0x16E0, 0xE74A, struct.pack('<I', c))
        if e.polls == e.sc.key_poll:
            e.keys.append(e.sc.key)
        e.polls += 1
    e.pump = pump

    def kbhit(e, a):
        pump(e)
        return (1 if e.keys else 0), 0

    def getch(e, a):
        return e.keys.pop(0) & 0xFF, 0
    e.stub('0000:2E45', kbhit)
    e.stub('0000:29D7', getch)
    lin = e.lin(0x5313, 0x10FF)
    e.u.hook_add(U.UC_HOOK_CODE, lambda uc, addr, size, _: pump(e), begin=lin, end=lin)

    check(s16(e.call(NAMES['mm_init'], *L(MM_INIT[0]), *L(MM_INIT[1]))[0]) == 1, 'orig mm_init')
    e.call(NAMES['mm_surface_alloc'], 0, 320, 200, 0)
    pseg = e.alloc(0x300)
    e.ww(0x16E0, 0xE5EF, 0)
    e.ww(0x16E0, 0xE5F1, pseg)
    e.pal_seg = pseg
    e.wr(0x16E0, 0x73D0, bytes([sound]))
    e.wr(0x16E0, 0x73D7, bytes([channels]))
    if mode is not None:
        e.ww(0x16E0, 0x8DDA, mode)
    e.wr(0x16E0, 0xE74A, struct.pack('<I', CLOCK0))
    e.strseg = e.alloc(0x40)
    return e


def c_env(sc, sound=1, channels=8, mode=None, heap_free=None):
    reset_c(heap_free, c_setup=c_files)
    set_mode13()
    TARGET_PAL.value = C.addressof(C_PAL)
    c_cdi_init()
    c_wp5_reg()
    WP5_N.value = 0
    check(c_mm_init(*MM_INIT) == 1, 'C mm_init')
    c_mm_surface_alloc(0, 320, 200, 0)
    DG[0x73D0] = sound
    DG[0x73D7] = channels
    if mode is not None:
        DG[0x8DDA], DG[0x8DDB] = mode & 0xFF, mode >> 8
    for i, b in enumerate(struct.pack('<I', CLOCK0)):
        DG[0xE74A + i] = b
    for i in range(256):
        WP9_STEPS[i] = sc.steps[i]
    WP9_KEY_POLL.value = sc.key_poll
    WP9_KEY.value = sc.key
    c_wp9_setup()


def dg16(dg, off):
    return dg[off] | dg[off + 1] << 8


def snap(side, e=None):
    """State after a step: DGROUP ranges, MEMMAN records, EMS memory,
    frame buffer, screen, logs, fatals."""
    if side == 'o':
        dg = e.dgroup(0, 0x10000)
        # the page frame lives in the emulator's memory: hand it to the PC
        C.memmove(C.addressof(e._frame), bytes(e.u.mem_read(0xE0000, 0x10000)), 0x10000)

        def conv(ref):
            rec = dg16(dg, ref)
            if not rec:
                return b''
            return e.rd(dg16(dg, rec + 2), dg16(dg, rec), 64000, runtime=True)
        cdi, fat = list(e.cdi), len(e.fatals)
    else:
        dg = bytes(DG)

        def conv(ref):
            rec = dg16(dg, ref)
            if not rec:
                return b''
            p = c_mm_conv_ptr(DGA + rec)
            return C.string_at(p, 64000) if p else b''
        cdi = [tuple(WP5_LOG[i]) for i in range(WP5_N.value)]
        fat = FATALS.value
    ems_h = dg16(dg, 0xE7AA)
    recs = []
    for i in range(110):
        r = dg[0xDA48 + 9 * i:0xDA48 + 9 * i + 9]
        recs.append(r[4:9] + (r[0:2] if r[8] == 1 else b''))
    return {
        'dg': {lo: dg[lo:hi] for lo, hi in CMP_DG},
        'recs': recs,
        'ems': zlib.crc32(ems_mem(ems_h)) if ems_h else 0,
        'frame': zlib.crc32(conv(0xD5FC)),
        'screen': zlib.crc32(conv(0xE29F)),
        'dac': zlib.crc32(vga_dac()),
        'cdi': [tuple(x) for x in cdi],
        'fatal': fat,
    }


def compare(o, c, where):
    ok = True
    for lo, a in o['dg'].items():
        b = c['dg'][lo]
        if a != b:
            i = next(i for i in range(len(a)) if a[i] != b[i])
            ok &= check(False, f'{where}: DGROUP {lo + i:04X}: orig {a[i]:02X} port {b[i]:02X}')
    for i, (a, b) in enumerate(zip(o['recs'], c['recs'])):
        if a != b:
            ok &= check(False, f'{where}: MEMMAN record {i}: {a.hex()} vs {b.hex()}')
            break
    for k in ('ems', 'frame', 'screen', 'dac', 'fatal'):
        if o[k] != c[k]:
            ok &= check(False, f'{where}: {k} differs ({o[k]} vs {c[k]})')
    if o['cdi'] != c['cdi']:
        ok &= check(False, f'{where}: CDI calls {o["cdi"][:6]} vs {c["cdi"][:6]}')
    return ok


# ============================================================================
# the player: scenarios
# ============================================================================
def run_steps(side, name, plan, sc, env):
    """load, open, then plan = list of clock increments (ms) between loop
    passes (each: frame start, update clock, show(1), stream), close.
    Returns the list of snapshots."""
    out = []
    if side == 'o':
        e = orig_env(sc, **env)
        e.wr(e.strseg, 0, (name + '.GHD').encode() + b'\0', runtime=True)
        e.wr(e.strseg, 0x20, (name + '.GFL').encode() + b'\0', runtime=True)

        def call(fname, *a):
            e.call(NAMES[fname], *a)

        def clock_add(ms):
            c = (struct.unpack('<I', e.dgroup(0xE74A, 4))[0] + ms) & 0xFFFFFFFF
            e.wr(0x16E0, 0xE74A, struct.pack('<I', c))

        def rd(off, n):
            return e.dgroup(off, n)
        load = lambda: call('gfl_load_ghd', e.far(e.strseg, 0))        # noqa: E731
        open_ = lambda: call('gfl_open_gfl', e.far(e.strseg, 0x20))    # noqa: E731
        show = lambda a: call('gfl_show_frame', a)                      # noqa: E731
        upd = lambda: call('gfl_update_frame_clock')                    # noqa: E731
        fill = lambda: call('gfl_stream_fill')                          # noqa: E731
        close = lambda: call('gfl_close')                               # noqa: E731
        sn = lambda: snap('o', e)                                       # noqa: E731
    else:
        c_env(sc, **env)
        e = None

        def clock_add(ms):
            c = (struct.unpack('<I', bytes(DG[0xE74A:0xE74E]))[0] + ms) & 0xFFFFFFFF
            for i, b in enumerate(struct.pack('<I', c)):
                DG[0xE74A + i] = b

        def rd(off, n):
            return bytes(DG[off:off + n])
        load = lambda: c_load_ghd((name + '.GHD').encode())            # noqa: E731
        open_ = lambda: c_open_gfl((name + '.GFL').encode())           # noqa: E731
        show, upd, fill, close = c_show, c_update_clock, c_fill, c_close
        sn = lambda: snap('c')                                          # noqa: E731
    load()
    out.append(('load', sn()))
    open_()
    out.append(('open', sn()))
    # the play loop's start (gfl_play_loop 1126..11E9 without the screen clear/sound)
    for off, v in ((0xD61A, 0), (0xD5F2, 0), (0xD616, 0xFFFF), (0xD618, 0xFFFF)):
        if side == 'o':
            e.ww(0x16E0, off, v)
        else:
            DG[off], DG[off + 1] = v & 0xFF, v >> 8
    show(0)
    out.append(('show0', sn()))
    start = rd(0xE74A, 4)
    if side == 'o':
        e.wr(0x16E0, 0xD60E, start)
    else:
        for i in range(4):
            DG[0xD60E + i] = start[i]
    nframes = struct.unpack('<H', rd(0xD623, 2))[0]
    for k, ms in enumerate(plan):
        clock_add(ms)
        now = rd(0xE74A, 4)
        if side == 'o':
            e.wr(0x16E0, 0xD612, now)
        else:
            for i in range(4):
                DG[0xD612 + i] = now[i]
        upd()
        if struct.unpack('<H', rd(0xD5F2, 2))[0] >= nframes:
            break
        show(1)
        out.append((f'step {k} show', sn()))
        fill()
        out.append((f'step {k} fill', sn()))
    close()
    out.append(('close', sn()))
    if side == 'o':
        out.append(('amp', e.amp))
    return out


def check_steps(name, plan, sc, env, label):
    o = run_steps('o', name, plan, sc, env)
    c = run_steps('c', name, plan, sc, env)
    amp = o.pop()[1]
    check(len(o) == len(c), f'{label}: {len(o)} vs {len(c)} steps')
    for (w, a), (_, b) in zip(o, c):
        if not compare(a, b, f'{label} {w}'):
            break
    check(all(x == 0 for x in amp), f'{label}: ampBreakPattern args {set(amp)}')
    return o, c


def test_player_steps_intro():
    """INTRO (no palettes): regular pacing, then lag (dropped frames and the
    stream skipping forward), memory mode 1 (ring = 1/2 file)."""
    plan = [100] * 12 + [350, 100, 100, 900, 30, 30, 100, 1300] + [100] * 10 + [2000, 100, 100]
    o, c = check_steps('INTRO', plan, Script([3, 1, 4, 1, 5, 9, 2, 6]), {}, 'INTRO')
    shown = {s['dg'][0xD5F2][0x24:0x26] for _, s in o}
    check(len(shown) > 12, f'INTRO: only {len(shown)} different frames shown')


def test_player_steps_palettes():
    """BES-D-IN (31 palette chunks): every frame, memory mode 0 (2/3),
    sound off (no sample slot, audio skipped by seek), slow stream."""
    plan = [100] * 40 + [450] + [100] * 20
    o, _ = check_steps('BES-D-IN', plan, Script([1]), {'mode': 0, 'sound': 0}, 'BES-D-IN')
    check(len({x['dac'] for _, x in o}) >= 5, 'BES-D-IN: too few palette changes')
    check(not o[-1][1]['cdi'], 'BES-D-IN: CDI used with sound off')


def test_player_steps_seq5_tail():
    """SEQ5: palette-only (type 1) frames at the end; start late (the first
    pass drops to a key frame far ahead), mode 2, 6 channels."""
    plan = [44300] + [100] * 50
    o, _ = check_steps('SEQ5', plan, Script([4, 4, 0, 7]), {'mode': 2, 'channels': 6}, 'SEQ5')
    last = max(struct.unpack('<h', x['dg'][0xD5F2][0x24:0x26])[0] for _, x in o)
    check(last == 491, f'SEQ5: last frame shown {last}, not 491 (palette-only frames)')


def run_play(side, name, sc, env):
    if side == 'o':
        e = orig_env(sc, **env)
        e.wr(e.strseg, 0, name.encode() + b'\0', runtime=True)
        key = e.call(NAMES['gfl_play'], e.far(e.strseg, 0))[0] & 0xFF
        return key, e.polls, snap('o', e), e.amp
    c_env(sc, **env)
    key = c_play(name.encode())
    return ord(key) if isinstance(key, bytes) else key & 0xFF, WP9_POLLS.value, snap('c'), None


def test_play_whole():
    """gfl_play from the entry point: INTRO to the end; KAD-N-OT aborted with
    Esc; BEY-N-OT with an 'a' key (consumed, playback goes on)."""
    cases = [('INTRO', Script([7, 3, 11, 2, 5]), {}),
             ('KAD-N-OT', Script([5, 2, 9], key_poll=300, key=0x011B), {'mode': 0}),
             ('BEY-N-OT', Script([20, 1, 30, 2, 2, 2, 60], key_poll=40, key=0x1E61), {'mode': 3})]
    keys = {'INTRO': 0, 'KAD-N-OT': 0x1B, 'BEY-N-OT': 0x61}
    for name, sc, env in cases:
        ko, po, so, amp = run_play('o', name, sc, env)
        check(ko == keys[name], f'{name}: original returned key {ko:02X}')
        check(sum(1 for x in so['cdi'] if x[0] == 0x04) == 2, f'{name}: sample not started twice')
        kc, pc, scs, _ = run_play('c', name, sc, env)
        check(ko == kc, f'{name}: key {ko:02X} vs {kc:02X}')
        check(po == pc, f'{name}: polls {po} vs {pc}')
        compare(so, scs, f'{name} gfl_play')
        check(all(x == 0 for x in amp), f'{name}: ampBreakPattern args {set(amp)}')


def fill_ems(h):
    """Pattern in the whole EMS memory of handle h (both sides start equal)."""
    n = u32()
    p = fn('pc_ems_handle_mem', C.POINTER(u8), u16, C.POINTER(u32))(h, C.byref(n))
    pat = bytes((i * 7 + (i >> 8) * 13) & 0xFF for i in range(0x10000))
    for off in range(0, n.value, 0x10000):
        k = min(0x10000, n.value - off)
        C.memmove(C.addressof(p.contents) + off, pat[:k], k)


def test_ring_and_slot_helpers():
    """gfl_ring_read_far wrap-around pieces and gfl_alloc_sample_slot fatals
    (size > C000h, slot in use) on both sides."""
    sc = Script([0])
    res = {}
    for side in ('o', 'c'):
        if side == 'o':
            e = orig_env(sc)
            e.call(NAMES['gfl_alloc_sample_slot'], 5, 0xC001, 3)
            e.call(NAMES['gfl_alloc_sample_slot'], 5, 100, -2)
            e.call(NAMES['gfl_alloc_sample_slot'], 7, 0x8000, 2)
            rec = dg16(e.dgroup(0, 0x10000), 0xDE34 + 7 * 0x17)
            ems = dg16(e.dgroup(0, 0x10000), rec)
            fill_ems(dg16(e.dgroup(0, 0x10000), 0xE7AA))
            ring = e.alloc(16)               # a fake MmHandle far * with the slot 7 EMS block
            e.wr(ring, 0, struct.pack('<H', ems), runtime=True)
            dst = e.alloc(0x400)
            outs = []
            for pos, dstoff, ln, size in ((0x7FF0, 3, 0x40, 0x8000), (5, 0, 0x100, 0x10000),
                                          (0xFFFF0, 0x10, 0x30, 0x8000), (0x100, 0, 0, 0x8000)):
                e.wr(dst, 0, bytes(0x400), runtime=True)
                e.call(NAMES['gfl_ring_read_far'], e.far(ring, 0), e.far(dst, 0), *L(pos), dstoff,
                       *L(ln), *L(size))
                outs.append(e.rd(dst, 0, 0x400, runtime=True))
            res[side] = (snap('o', e), outs)
        else:
            c_env(sc)
            c_alloc_slot(5, 0xC001, 3)
            c_alloc_slot(5, 100, -2)
            c_alloc_slot(7, 0x8000, 2)
            rec = dg16(bytes(DG), 0xDE34 + 7 * 0x17)
            fill_ems(dg16(bytes(DG), 0xE7AA))
            outs = []
            for pos, dstoff, ln, size in ((0x7FF0, 3, 0x40, 0x8000), (5, 0, 0x100, 0x10000),
                                          (0xFFFF0, 0x10, 0x30, 0x8000), (0x100, 0, 0, 0x8000)):
                buf = C.create_string_buffer(0x400)
                c_ring_read(DGA + rec, buf, pos, dstoff, ln, size)
                outs.append(buf.raw)
            res[side] = (snap('c'), outs)
    compare(res['o'][0], res['c'][0], 'helpers')
    check(res['o'][0]['fatal'] == 2, f'helpers: {res["o"][0]["fatal"]} fatals in the original')
    for i, (a, b) in enumerate(zip(res['o'][1], res['c'][1])):
        check(a == b, f'ring_read_far case {i}')
        check(i == 3 or any(a), f'ring_read_far case {i}: nothing copied')
