"""WP10 sound glue: NSES (seg0D42), OSES (ovl55CC) against the original, and
the audio backend (platform/audio.c) end to end.

Build:  tools/testlib.sh out/wp10 src/engine/nses.c src/engine/oses.c src/platform/audio.c
Run:    python tests/py/run.py out/wp10/lale_testlib.dll test_sound

Environment of the original (tools/emu86.py):
  * EMS, MEMMAN heap model, fatal recorder, DDS: test_ems.make_emu();
  * the virtual file layer o569A_* (thunks 16CD:xxxx) is served by the port's
    own vfs (verified in test_vfs) on the same archive state, so both sides
    read identical bytes with identical results;
  * CDI wrappers (0EAE:xxxx), tsAddRoutine, DDSLoadDriver, kbhit stubbed and
    recorded.  The port gets the wp5 recording CDI device, or a scripted one.
The integration test runs oses_init with the shipped SES.CFG + SDISB16.DSD
(native MCP), plays an AMF in virtual time and checks the DMA byte stream.
"""
import ctypes
import hashlib
import os
import struct
import wave
import zlib

import unicorn as U
import unicorn.x86_const as X

from lale import NAMES, LOAD, ROOT, check, fn, lib, dg_from_emu, dg_diff, DG
from helpers import make_emu, reset_c, ems_mem, FATALS, c_ptr_far, s16, c_heap_reset

C = ctypes
u8, u16, i16, i32, u32, vp, cp = (C.c_uint8, C.c_uint16, C.c_int16, C.c_int32, C.c_uint32,
                                  C.c_void_p, C.c_char_p)
OUT = os.path.join(ROOT, 'out', 'wp10')
ORIG = os.path.join(ROOT, 'orig')

# ---- port functions --------------------------------------------------------------
c_set_dir = fn('pc_set_game_dir', None, cp)
c_pc_reset = fn('pc_reset', None)
c_dg_reset = fn('dg_reset', None)
c_vfile_reset = fn('vfile_reset', None)
c_vfs_reset = fn('vfs_reset', None)
c_vfs_init = fn('vfs_init', i16, i16)
c_vfs_mount = fn('vfs_mount_archive', i16, cp, vp)
c_vfs_open = fn('vfs_open', i16, cp, i16)
c_vfs_close = fn('vfs_close', i16, i16)
c_vfs_read = fn('vfs_read', i32, i16, vp, i32)
c_vfs_seek = fn('vfs_seek', i16, i16, i32, i16)
c_vfs_tell = fn('vfs_tell', i32, i16)
c_mm_init = fn('mm_init', i16, u32, u32)
c_mm_conv_ptr = fn('mm_conv_ptr', vp, vp)
c_EMSAlloc = fn('EMSAlloc', i16, i32)
c_cdiInit = fn('cdiInit', None)
c_cdiRegister = fn('cdiRegister', i16, vp)
c_wp5_reg = fn('wp5_register_device', i16)
c_wp5_reset = fn('wp5_reset', None)
c_DDSInit = fn('DDSInit', i16)
c_tsInit = fn('tsInit', i16)
c_tsClose = fn('tsClose', None)
c_pc_poll = fn('pc_poll', None)
c_pc_virtual = fn('pc_set_virtual_time', None, C.c_int, u32)
c_mcp_reset = fn('mcp_reset', None)
c_mcp_channels = fn('mcpGetChannelCount', i16)
c_amp_load = fn('ampLoadModule', i16, cp, i32, C.POINTER(vp))
c_amp_play = fn('ampPlayModule', i16, vp, i32)
c_amp_stop = fn('ampStopModule', i16)

c_map_sample = fn('snd_ems_map_sample', vp, u32, u32)
c_save_map = fn('snd_ems_save_map', None)
c_restore_map = fn('snd_ems_restore_map', None)
c_identity = fn('snd_identity_addr', u32, u32, u32)
c_t40 = fn('snd_timer_40hz', None)
c_t100 = fn('snd_timer_100hz', None)
c_play = fn('snd_play_sample', None, i16, i32, i32, i32, u8)
c_load_sound = fn('snd_load_sound', None, cp, i32, i32, i32, u8)
c_load_ems = fn('snd_load_sample_ems', i16, u8, i16, i16, i16)
c_nses_reset = fn('nses_reset', None)
c_oses_init = fn('oses_init', i16, i16)
c_oses_load = fn('oses_load_sample', i16, u8, cp, i16, u8, i32, i32)
c_audio_reset = fn('audio_reset', None)
c_audio_take = fn('audio_game_take', C.c_size_t, vp, C.c_size_t)
c_audio_avail = fn('audio_game_available', C.c_size_t)
c_audio_dropped = fn('audio_game_dropped', u32)
c_audio_render = fn('audio_render', C.c_size_t, vp, C.c_size_t)
c_audio_running = fn('audio_dma_running', C.c_int)
c_audio_played = fn('audio_dma_bytes_played', C.c_uint64)
c_audio_cd = fn('audio_set_cd', None, C.c_int)

class Call(C.Structure):
    _fields_ = [('slot', i32), ('a', i32), ('b', i32), ('c', i32)]


WP5_LOG = (Call * 400000).in_dll(lib, 'wp5_log')
WP5_N = i32.in_dll(lib, 'wp5_log_n')
FATAL_CODE = i16.in_dll(lib, 'o559A_fatal_code')
DGA = C.addressof(DG)
# DGROUP as the image has it.  Not bytes(DG) at import: run.py imports this
# file after the tests sorted before it have changed dg[] (that made
# test_integration_amf_playback fail in the shared DLL: a stale RTL stream
# table, so oses_init could not read SES.CFG).
_saved = bytes(DG)
lib.dg_reset()
PRISTINE = bytes(DG)
C.memmove(C.addressof(DG), _saved, 0x10000)
del _saved
MM_SEG = 0x1F04
AUDIO_LAG = 64


def L(v):
    v &= 0xFFFFFFFF
    return [v & 0xFFFF, v >> 16]


def s32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


# ---- original-side helpers ----------------------------------------------------------
def pstub(e, addr, nwords, f):
    """Replace a far PASCAL routine: f(words) with words from the lowest
    (= last parameter) up; returns DX:AX; pops nwords like RETF 2n."""
    lin = e.lin(addr) if isinstance(addr, str) else addr

    def hook(uc, address, size, _):
        ss, sp = uc.reg_read(X.UC_X86_REG_SS), uc.reg_read(X.UC_X86_REG_SP)
        w = struct.unpack(f'<{2 + nwords}H', uc.mem_read(ss * 16 + sp, 4 + 2 * nwords))
        r = (f(w[2:]) or 0) & 0xFFFFFFFF
        uc.reg_write(X.UC_X86_REG_AX, r & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_DX, r >> 16)
        uc.reg_write(X.UC_X86_REG_SP, (sp + 4 + 2 * nwords) & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_CS, w[1])
        uc.reg_write(X.UC_X86_REG_IP, w[0])
    e.u.hook_add(U.UC_HOOK_CODE, hook, begin=lin, end=lin)


def emu_str(e, seg, off):
    s = bytearray()
    while True:
        b = e.rd(seg, off + len(s), 1, runtime=True)[0]
        if not b:
            return bytes(s)
        s.append(b)


def stub_vfs(e):
    """o569A_open/close/read/seek/tell -> the port's vfs (same archive state)."""
    def op(w):
        return c_vfs_open(emu_str(e, w[2], w[1]), s16(w[0])) & 0xFFFF

    def cl(w):
        return c_vfs_close(s16(w[0])) & 0xFFFF

    def rd(w):
        n = w[0] | w[1] << 16
        seg, off, h = w[3], w[2], s16(w[4])
        lin = e.lin(seg, off, runtime=True)
        size = min(n, 0x100000)
        buf = C.create_string_buffer(bytes(e.u.mem_read(lin, size)), max(size, 1))
        r = c_vfs_read(h, buf, s32(n))
        e.u.mem_write(lin, buf.raw[:size])
        return r

    def sk(w):
        return c_vfs_seek(s16(w[3]), s32(w[1] | w[2] << 16), s16(w[0])) & 0xFFFF

    def tl(w):
        return c_vfs_tell(s16(w[0]))
    pstub(e, '16CD:0025', 3, op)
    pstub(e, '16CD:002A', 1, cl)
    pstub(e, '16CD:002F', 5, rd)
    pstub(e, '16CD:0039', 4, sk)
    pstub(e, '16CD:003E', 1, tl)


c_stdio_reset = fn('bc_stdio_reset', None)
c_setupio = fn('bc_setupio', None)
c_vfile_init = fn('vfile_init', i16, i16)


def vfs_setup(game_dir=ORIG, archives=('SOUNDS.CKD',)):
    """The game's file layers as main() sets them up (RTL streams, vfile,
    vfs + archives), on the port side."""
    c_set_dir(game_dir.encode())
    # RTL stream table + _openfd, vfile/vfs DGROUP state back to the start-up image
    for lo, hi in ((0x929E, 0x929E + 20 * 20), (0x9430, 0x9458), (0xE860, 0xE8CC)):
        DG[lo:hi] = PRISTINE[lo:hi]
    c_stdio_reset()
    c_setupio()
    c_vfile_reset()
    c_vfile_init(20)
    c_vfs_reset()
    c_vfs_init(20)
    for a in archives:
        check(c_vfs_mount(a.encode(), None) >= 0, f'mount {a}')


SOUND_FILES = (ORIG, ('SOUNDS.CKD',))


def emu_env(sound=1, mm=True, files=SOUND_FILES):
    """The original; its file layer is the port's vfs (stub_vfs), set up by
    vfs_setup(*files) - which also gives both sides' far heaps their start."""
    e = make_emu(c_setup=lambda: vfs_setup(*files))
    stub_vfs(e)
    if mm:
        e.call(NAMES['mm_init'], *L(0x240000), *L(0x440000))
    e.wr(0x16E0, 0x73D0, bytes([sound]))
    e.cdi = []
    return e


def c_env(sound=1, mm=True, files=SOUND_FILES):
    reset_c(c_setup=lambda: vfs_setup(*files))
    c_nses_reset()
    c_audio_reset()
    if mm:
        c_mm_init(0x240000, 0x440000)
    DG[0x73D0] = sound
    c_cdiInit()
    c_wp5_reset()
    WP5_N.value = 0
    c_wp5_reg()


def stub_cdi(e, getpos=None):
    """Record the CDI wrappers NSES/OSES call (pascal, all dwords)."""
    def si_tuple(seg, off):
        b = e.rd(seg, off, 0x1A, runtime=True)
        so, ss = struct.unpack_from('<HH', b, 0)
        ref = 0 if (so | ss) == 0 else (0x10000 | so) if ss == 0xFFFF else -1
        length, ls, le = struct.unpack_from('<III', b, 4)
        mode, sid, rate, freq, vol = struct.unpack_from('<BHHIB', b, 0x10)
        return [(0x00, ref, s32(length)), (0x100, s32(ls), s32(le), mode | sid << 8),
                (0x101, rate, s32(freq), vol)]

    def set_sample(w):
        ch = s32(w[2] | w[3] << 16)
        t = si_tuple(w[1], w[0])
        e.cdi.append((0x00, ch, t[0][1], t[0][2]))
        e.cdi.append(t[1])
        e.cdi.append(t[2])
        e.last_si = (w[1], w[0])
        return 0
    pstub(e, '0EAE:008F', 4, set_sample)
    pstub(e, '0EAE:01B4', 4, lambda w: e.cdi.append((0x18, s32(w[2] | w[3] << 16),
                                                     s32(w[0] | w[1] << 16), 0)))
    pstub(e, '0EAE:00B8', 6, lambda w: e.cdi.append((0x04, s32(w[4] | w[5] << 16),
                                                     s32(w[2] | w[3] << 16),
                                                     s32(w[0] | w[1] << 16))))
    pstub(e, '0EAE:00E6', 2, lambda w: e.cdi.append((0x28, s32(w[0] | w[1] << 16), 0, 0)))

    def download(w):
        tag_off, tag_seg = w[2], w[3]
        ref = 0 if (tag_off | tag_seg) == 0 else (0x10000 | tag_off) if tag_seg == 0xFFFF else -1
        e.cdi.append((0x34, ref, s32(w[0] | w[1] << 16), s32(w[6] | w[7] << 16)))
        return 0
    pstub(e, '0EAE:0294', 8, download)
    pstub(e, '0EAE:0414', 2, lambda w: e.cdi.append((0x5C, 0, 0, 0)))
    pstub(e, '0EAE:03C8', 4, lambda w: e.cdi.append((0x54, s32(w[0] | w[1] << 16), 0, 0)))
    if getpos:
        pstub(e, '0EAE:0356', 2, lambda w: getpos(s32(w[0] | w[1] << 16)))


def wp5_calls(ignore=(0x40, 0x48)):
    """wp5 log in the stub_cdi tuple format (download: ref, len, ch)."""
    out = []
    for i in range(WP5_N.value):
        c = WP5_LOG[i]
        if c.slot in ignore:
            continue
        if c.slot == 0x34:
            out.append((0x34, c.b, c.c, 0))
        elif c.slot in (0x100, 0x101):
            out.append((c.slot, c.a, c.b, c.c))
        elif c.slot in (0x28, 0x5C, 0x54):
            out.append((c.slot, c.a, 0, 0))
        else:
            out.append((c.slot, c.a, c.b, c.c))
    return out


def emu_fatals(e):
    return [code for line, code in e.fatals]


SLOT_DG = [(0xDE34, 0xE29B)]
TABLE_DG = [(0x22EA, 0x22EA + 0x32 * 0x11)]


def conv_slot_ignore(slots):
    """DE38..DE3B of conventional slots hold a real far pointer in the original."""
    ig = set()
    for s in slots:
        o = 0xDE38 + s * 0x17
        ig.update(range(o, o + 4))
    return ig


def handles_equal(e, label):
    a = e.dgroup(0xDA48, 990)
    b = bytes(DG[0xDA48:0xDA48 + 990])
    for i in range(110):
        x, y = a[9 * i:9 * i + 9], b[9 * i:9 * i + 9]
        if x[4:9] != y[4:9] or (x[8] == 1 and x[0:2] != y[0:2]):
            check(False, f'{label}: MEMMAN record {i} {x.hex()} vs {y.hex()}')
            return


def ems_handle():
    return DG[0xE7AA] | DG[0xE7AB] << 8


# =====================================================================================
# hooks
# =====================================================================================
def test_hooks_ems_map():
    """MCPSampleRealAddress / Enable / DisableVirtualSamples / identity hook."""
    for ems_on in (1, 0):
        e = emu_env()
        h = s16(e.call(NAMES['EMSAlloc'], *L(0x5000))[0])
        if not ems_on:
            e.wr(0x16E0, 0x6E02, bytes(4))
        e.call('0D42:0041')                                   # save map
        ax, dx = e.call('0D42:0005', *L(7), *L(h))            # pascal: pos, handle reversed
        ro = None if (ax | dx) == 0 else (dx << 16) | ax
        e.call('0D42:005E')
        ri = e.call('0D42:007B', *L(0x12345678), *L(0x9ABCDEF0), ret='long')
        c_env()
        hc = c_EMSAlloc(0x5000)
        if not ems_on:
            DG[0x6E02:0x6E06] = [0, 0, 0, 0]
        c_save_map()
        p = c_map_sample(hc, 7)
        rc = c_ptr_far(p)
        c_restore_map()
        check(h == hc, f'EMSAlloc handle {h} {hc}')
        check(ro == rc, f'ems={ems_on}: map_sample orig {ro} C {rc}')
        check(ri == c_identity(0x9ABCDEF0, 0x12345678) == 0, 'identity hook returns 0')
        d = dg_diff(e, [(0x86F8, 0x8704), (0xE7AC, 0xE7B4)])
        check(not d, f'ems={ems_on}: EMS map state {d[:6]}')


def test_timer_routines_guard():
    """snd_timer_40hz / 100hz: guard bytes, poll / AMP call."""
    for busy40, busy100 in ((0, 0), (1, 0), (0, 1), (1, 1)):
        e = emu_env()
        stub_cdi(e)
        amp = []
        pstub(e, '0EF6:0F4C', 1, lambda w: amp.append(1))     # PUSHF + CALLF, IRET
        e.wr(0x16E0, 0x73D8, bytes([busy40]))
        e.wr(0x16E0, 0x73DB, bytes([busy100]))
        e.call('0D42:0093', 0x0202)
        e.call('0D42:0120', 0x0202)
        c_env()
        DG[0x73D8], DG[0x73DB] = busy40, busy100
        WP5_N.value = 0
        c_t40()
        polls_c = sum(1 for i in range(WP5_N.value) if WP5_LOG[i].slot == 0x5C)
        c_t100()
        polls_o = sum(1 for c in e.cdi if c[0] == 0x5C)
        check(polls_o == polls_c == (0 if busy40 else 1), f'poll calls {polls_o} {polls_c}')
        check(len(amp) == (0 if busy100 else 1), 'orig AMP call')
        d = dg_diff(e, [(0x73D8, 0x73DC), (0x86F8, 0x8704)])
        check(not d, f'guards {busy40},{busy100}: {d}')


# =====================================================================================
# sample slots
# =====================================================================================
LOAD_EMS_CASES = [
    # (type, slot, count, unit)
    (1, 0, 100, 1), (1, 5, 0x7530, 2), (1, 0x30, 1000, 70), (1, 3, 0x4000, 4),
    (0, 2, 3000, 1), (0, 0x30, 0x7530, 2), (2, 7, 10, 10), (1, 9, -3, 7),
    (1, 0x21, 0x7FFF, 2), (0, 1, 0, 5),
]


def run_load_ems(side, cases, sound=1):
    res = []
    if side == 'o':
        e = emu_env(sound)
        for t, s, n, u in cases:
            res.append(s16(e.call('0D42:044D', t, s, n, u)[0]))
        return e, res, emu_fatals(e)
    c_env(sound)
    for t, s, n, u in cases:
        res.append(c_load_ems(t, s, n, u))
    return None, res, FATALS.value


def test_load_sample_ems():
    for sound in (1, 0):
        e, ro, fo = run_load_ems('o', LOAD_EMS_CASES, sound)
        _, rc, fc = run_load_ems('c', LOAD_EMS_CASES, sound)
        check(ro == rc, f'sound={sound} results orig {ro} C {rc}')
        check(len(fo) == fc, f'sound={sound} fatals orig {fo} C {fc}')
        ig = conv_slot_ignore([s for t, s, n, u in LOAD_EMS_CASES if t == 0])
        d = dg_diff(e, SLOT_DG, ignore=ig)
        check(not d, f'sound={sound} slot table {[(hex(a), b, c) for a, b, c in d[:10]]}')
        handles_equal(e, f'sound={sound}')
        # a used slot again: fatal 24h/25h
    cases = [(1, 4, 100, 1), (1, 4, 100, 1), (0, 6, 100, 1), (0, 6, 100, 1)]
    e, ro, fo = run_load_ems('o', cases)
    _, rc, fc = run_load_ems('c', cases)
    check(ro == rc and fo == [0x25, 0x24] and fc == 2 and FATAL_CODE.value == 0x24,
          f'slot reuse orig {ro} {fo} C {rc} {fc}')


PLAY_CASES = [
    # (slot, freq, volume, pan, ch)
    (5, 22050, 64, 0, 0), (0x30, 8000, 32, -63, 7), (3, 11025, 255, 100, 31),
    (5, 0x7FFFFFFF, 0, 63, 1), (7, -1, 64, 0, 2), (0, 0, 0, 0, 0),
]


def test_play_sample():
    for sound in (1, 0):
        e = emu_env(sound)
        stub_cdi(e)
        for t, s, n, u in LOAD_EMS_CASES[:4]:
            e.call('0D42:044D', t, s, n, u)
        for slot, f, v, p, ch in PLAY_CASES:
            e.call('0D42:014F', slot, *L(f), *L(v), *L(p), ch)
        c_env(sound)
        for t, s, n, u in LOAD_EMS_CASES[:4]:
            c_load_ems(t, s, n, u)
        WP5_N.value = 0
        for slot, f, v, p, ch in PLAY_CASES:
            c_play(slot, f, v, p, ch)
        co = wp5_calls()
        check(e.cdi == co, f'sound={sound} CDI calls\n orig {e.cdi}\n C    {co}')
        fo = emu_fatals(e)
        check(len(fo) == FATALS.value, f'sound={sound} fatals {fo} {FATALS.value}')


# real .PUR files from SOUNDS.CKD: (type, name, slot, flags, loopstart, loopend)
OSES_CASES = [
    (1, 'ADIM1.PUR', 0x21, 0, 0, 0),
    (1, 'BOSCH.PUR', 3, 2, 0, 0),
    (1, 'FECIZE32.PUR', 0x22, 2, 0, 0),            # 151225 bytes: 3 EMS slots
    (1, 'VECIZE32.PUR', 0x26, 0, 0x100, 0x46E3),   # only the first piece
    (1, 'VECIZE33.PUR', 0x2E, 3, 0, 0),            # reversed, runs into slot 30h
    (1, 'ADIM0.PUR', 0x10, 1, 5, 9),               # reversed
    (0, 'ADIM2.PUR', 0x11, 0, 0, 0),               # conventional
    (0, 'BUUN.PUR', 0x12, 1, 0, 0),                # conventional, reversed
    (1, 'NOFILE.PUR', 0x13, 0, 0, 0),              # cannot open
]


def test_oses_load_sample():
    for sound in (1, 0):
        e = emu_env(sound)
        stub_cdi(e)
        ro = []
        for t, name, slot, fl, ls, le in OSES_CASES:
            nb = e.alloc(16)
            e.wr(nb, 0, name.encode() + b'\0', runtime=True)
            ro.append(s16(e.call('55CC:01F3', t, *e.far(nb, 0), slot, fl, *L(ls), *L(le))[0]))
        eh = struct.unpack('<H', e.dgroup(0xE7AA, 2))[0]
        emem = ems_mem(eh)
        conv_o = {}
        for t, name, slot, fl, ls, le in OSES_CASES:
            if t == 0:
                ro_ = struct.unpack('<HH', e.dgroup(0xDE34 + slot * 0x17, 4))
                if ro_[1]:
                    po, ps = struct.unpack('<HH', e.dgroup(ro_[0], 4))
                    size = struct.unpack('<I', e.dgroup(ro_[0] + 4, 4))[0]
                    conv_o[slot] = e.rd(ps, po, size, runtime=True)
        c_env(sound)
        rc = []
        for t, name, slot, fl, ls, le in OSES_CASES:
            rc.append(c_oses_load(t, name.encode(), slot, fl, ls, le))
        check(ro == rc, f'sound={sound} results orig {ro} C {rc}')
        fo = emu_fatals(e)
        check(len(fo) == FATALS.value, f'sound={sound} fatals orig {fo} C {FATALS.value}')
        ig = conv_slot_ignore([c[2] for c in OSES_CASES if c[0] == 0])
        d = dg_diff(e, SLOT_DG + TABLE_DG, ignore=ig)
        check(not d, f'sound={sound} tables {[(hex(a), b, c) for a, b, c in d[:10]]}')
        handles_equal(e, f'oses sound={sound}')
        cm = ems_mem(ems_handle())
        check(emem == cm, f'sound={sound} EMS contents differ ({len(emem)} / {len(cm)} bytes)')
        for slot, data in conv_o.items():
            ref = DG[0xDE34 + slot * 0x17] | DG[0xDE35 + slot * 0x17] << 8
            p = c_mm_conv_ptr(DGA + ref)
            check(p and C.string_at(p, len(data)) == data, f'conventional slot {slot:#x} data')
        dl_o = [c for c in e.cdi if c[0] == 0x34]
        dl_c = [c for c in wp5_calls() if c[0] == 0x34]
        dl_o = [(s, ref, ln, 0) for s, ref, ln, ch in dl_o]
        check(dl_o == dl_c, f'sound={sound} downloads\n orig {dl_o}\n C    {dl_c}')
        check(sound == 0 or len(dl_c) >= 8, 'downloads happened')


# =====================================================================================
# snd_load_sound: streaming with a scripted play position
# =====================================================================================
class SampleRef(C.Structure):
    _fields_ = [('ptr', vp), ('ems_handle', u16), ('in_ems', u8)]


class SampleInfo(C.Structure):
    _fields_ = [('sample', SampleRef), ('length', u32), ('loopstart', u32), ('loopend', u32),
                ('mode', u8), ('sampleID', u16), ('rate', u16), ('freq', u32), ('volume', u8)]


GENERIC = C.CFUNCTYPE(i32, i32, i32, i32, i32)
F_SETSAMPLE = C.CFUNCTYPE(i32, i32, C.POINTER(SampleInfo))
F_PLAY = C.CFUNCTYPE(i32, i32, u32, u32)
F_PAN = C.CFUNCTYPE(i32, i32, i32)
F_CH = C.CFUNCTYPE(i32, i32)


class ScriptDevice:
    """A CDI device for the port side: records like stub_cdi, scripted
    getPosition."""

    def __init__(self, getpos, log):
        self.log = log
        self.ptr = None
        self.getpos = getpos

        def ss(ch, si):
            s = si.contents
            ref = 0x10000 | s.sample.ems_handle if s.sample.in_ems else (-1 if s.sample.ptr else 0)
            self.log.append((0x00, ch, ref, s32(s.length)))
            self.log.append((0x100, s32(s.loopstart), s32(s.loopend), s.mode | s.sampleID << 8))
            self.log.append((0x101, s.rate, s32(s.freq), s.volume))
            self.ptr = s.sample.ptr
            return 0
        self.cbs = {0x00: F_SETSAMPLE(ss),
                    0x04: F_PLAY(lambda ch, f, v: self.log.append((0x04, ch, s32(f), s32(v))) or 0),
                    0x18: F_PAN(lambda ch, p: self.log.append((0x18, ch, p, 0)) or 0),
                    0x28: F_CH(lambda ch: self.log.append((0x28, ch, 0, 0)) or 0),
                    0x48: F_CH(lambda ch: self.getpos(ch))}
        self.nop = GENERIC(lambda a, b, c, d: 0)
        self.table = (vp * 26)()
        for i in range(26):
            f = self.cbs.get(i * 4, self.nop)
            self.table[i] = C.cast(f, vp).value

    def register(self):
        return c_cdiRegister(C.addressof(self.table))


def scripted_positions(log, bufget, step=9000, limit=4000):
    k = [0]

    def getpos(ch):
        i = k[0]
        k[0] += 1
        buf = bufget()
        log.append((0x48, ch, zlib.crc32(buf) if buf else 0, 0))
        if i >= limit:
            return 0x7FFFFFFF
        return (i * step) % 60000
    return getpos


def test_load_sound_streaming():
    for name, step in (('PARA08.SND', 9000), ('BUUN.PUR', 7000), ('PARA01.SND', 13000)):
        e = emu_env(mm=True)
        e.stub('0000:2E45', lambda e, a: (0, 0))               # kbhit: no key
        olog = e.cdi

        def obuf():
            si = getattr(e, 'last_si', None)
            if not si:
                return None
            po, ps = struct.unpack('<HH', e.rd(si[0], si[1], 4, runtime=True))
            return e.rd(ps, po, 60000, runtime=True)
        stub_cdi(e, scripted_positions(olog, obuf, step))
        nb = e.alloc(16)
        e.wr(nb, 0, name.encode() + b'\0', runtime=True)
        e.call('0D42:01F0', *e.far(nb, 0), *L(11025), *L(64), *L(0), 7)
        c_env(mm=True)
        DG[0x73D7] = 8
        clog = []
        holder = {}

        def cbuf():
            dev = holder.get('dev')
            return C.string_at(dev.ptr, 60000) if dev and dev.ptr else None
        dev = holder['dev'] = ScriptDevice(scripted_positions(clog, cbuf, step), clog)
        c_cdiInit()
        dev.register()
        c_pc_virtual(1, 100)
        c_load_sound(name.encode(), 11025, 64, 0, 7)
        c_pc_virtual(0, 0)
        check(len(olog) > 10, f'{name}: original made {len(olog)} CDI calls')
        first = next((i for i in range(min(len(olog), len(clog))) if olog[i] != clog[i]), None)
        check(olog == clog, f'{name}: CDI sequence differs at {first} ({len(olog)} vs {len(clog)}):'
              f' {olog[first] if first is not None else None} vs {clog[first] if first is not None else None}')
        check(len(emu_fatals(e)) == FATALS.value == 0, f'{name}: fatals {emu_fatals(e)} {FATALS.value}')
        handles_equal(e, f'load_sound {name}')
        d = dg_diff(e, SLOT_DG, ignore=conv_slot_ignore([0x30]))
        check(not d, f'{name}: slot table {d[:6]}')


# =====================================================================================
# oses_init
# =====================================================================================
class FuncNode(C.Structure):
    pass


FuncNode._fields_ = [('name', cp), ('fn', vp), ('next', C.POINTER(FuncNode))]


def c_dds_names():
    head = C.POINTER(FuncNode).in_dll(lib, 'dds_functions')
    out, p = [], head.contents.next
    while C.addressof(p.contents) != C.addressof(head.contents):
        out.append(p.contents.name)
        p = p.contents.next
    return out


def emu_dds_names(e):
    out = []
    hs, ho = struct.unpack('<HH', e.dgroup(0xE77E, 4))[::-1]

    def far(s, o):
        a, b = struct.unpack('<HH', e.rd(s, o, 4, runtime=True))
        return b, a
    s, o = far(hs, ho + 8)
    while (s, o) != (hs, ho):
        ns, no = far(s, o)
        out.append(emu_str(e, ns, no))
        s, o = far(s, o + 8)
    return out


def game_dir(tag, cfg):
    d = os.path.join(OUT, f'game_{tag}')
    os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        os.remove(os.path.join(d, f))
    if cfg is not None:
        open(os.path.join(d, 'SES.CFG'), 'wb').write(cfg)
    open(os.path.join(d, 'SDISB16.DSD'), 'wb').write(open(os.path.join(ORIG, 'SDISB16.DSD'), 'rb').read())
    return d


SES = open(os.path.join(ORIG, 'SES.CFG'), 'rb').read()


def ses_variant(flags=None, drv=None):
    b = bytearray(SES)
    if flags is not None:
        struct.pack_into('<I', b, 13 + 4, flags)
    if drv is not None:
        b[0:13] = drv.encode().ljust(13, b'\0')
    return bytes(b)


OSES_INIT_CASES = [
    ('shipped', SES, 0),
    ('probe', SES, 1),
    ('nopoll', ses_variant(flags=0x4D), 0),
    ('short', b'\x07\x00', 0),
    ('missing', None, 0),
    ('gus', ses_variant(drv='SDIGUS16.DSD'), 0),
]


def run_oses_init_orig(d, probe, drv_ok):
    e = emu_env(sound=0, mm=False, files=(d, ()))
    stub_cdi(e)
    ts = []
    pstub(e, '10EF:0178', 4, lambda w: ts.append(((w[3] - LOAD) & 0xFFFF, w[2], w[0] | w[1] << 16))
          or len(ts))
    sdi = []
    code = e.alloc(64)
    pstub(e, e.lin(code, 0, runtime=True), 2, lambda w: sdi.append(('init', w[0])) or 0)
    pstub(e, e.lin(code, 16, runtime=True), 0, lambda w: sdi.append(('start',)) or 0)
    pstub(e, e.lin(code, 32, runtime=True), 0, lambda w: sdi.append(('stop',)) or 0)

    def load(w):                                          # DDSLoadDriver(name, handle *)
        name = emu_str(e, w[3], w[2]).decode()
        sdi.append(('load', name))
        if not drv_ok(name):
            return 5
        e.wr(w[1], w[0], struct.pack('<I', 1), runtime=True)
        return 0
    pstub(e, '0DF2:0611', 4, load)
    # the stubbed driver's exports, registered up front
    nb = e.alloc(64)
    for i, n in enumerate((b'SDIInitDevice', b'SDIStartOutput', b'SDIStopOutput')):
        e.wr(nb, i * 16, n + b'\0', runtime=True)
        e.call('0DF2:011B', *e.far(code, i * 16), *e.far(nb, i * 16))
    r = s16(e.call('55CC:0000', probe)[0])
    return e, r, ts, sdi


def test_oses_init():
    fn_name = {}
    for nm in ('dsmiTimeISR', 'snd_timer_100hz', 'snd_timer_40hz'):
        fn_name[C.cast(getattr(lib, nm), vp).value] = nm
    orig_name = {(0x1145, 0x0096): 'dsmiTimeISR', (0x0D42, 0x0120): 'snd_timer_100hz',
                 (0x0D42, 0x0093): 'snd_timer_40hz'}
    for tag, cfg, probe in OSES_INIT_CASES:
        d = game_dir(tag, cfg)
        e, ro, ts_o, sdi_o = run_oses_init_orig(d, probe, lambda n: n.upper() == 'SDISB16.DSD')
        # port
        c_dg_reset()
        c_pc_reset()
        c_nses_reset()
        c_audio_reset()
        c_mcp_reset()
        FATALS.value = 0
        c_heap_reset()
        vfs_setup(d, ())                  # before DDSInit, as in emu_env (far heap order)
        c_DDSInit()
        c_tsInit()
        rc = c_oses_init(probe)
        check(ro == rc, f'{tag}: oses_init orig {ro:#x} C {rc:#x}')
        ts_c = []
        routines = (vp * 16).in_dll(lib, 'ts_routine')
        for i in range(1, 16):
            if DG[0x85DA + i * 14]:
                ts_c.append((fn_name.get(routines[i], routines[i]),
                             DG[0x85D2 + i * 14] | DG[0x85D3 + i * 14] << 8))
        ts_o2 = [(orig_name.get((s, o), (s, o)), rate) for s, o, rate in ts_o]
        check(ts_o2 == ts_c, f'{tag}: timer routines orig {ts_o2} C {ts_c}')
        rng = [(0x73D0, 0x73D1), (0x750A, 0x750B), (0x8DDA, 0x8DDC), (0xE7EC, 0xE848),
               (0xE85C, 0xE860), (0xE796, 0xE7A4), (0x7C4E, 0x7C50), (0x8591, 0x8592)]
        dd = dg_diff(e, rng)
        check(not dd, f'{tag}: DGROUP {[(hex(a), b, c) for a, b, c in dd[:10]]}')
        started = ('start',) in sdi_o
        check(started == bool(c_audio_running()), f'{tag}: DMA started orig {started} C {c_audio_running()}')
        setup_o = [c[1] for c in e.cdi if c[0] == 0x54]
        if setup_o:
            check(c_mcp_channels() == setup_o[0], f'{tag}: setupChannels {setup_o} vs {c_mcp_channels()}')
        init_o = [s for s in sdi_o if s[0] == 'init']
        check(not init_o or init_o[0][1] == 0xE7EC, f'{tag}: SDIInitDevice arg {init_o}')
        no = [n for n in emu_dds_names(e)
              if n not in (b'SDIInitDevice', b'SDIStartOutput', b'SDIStopOutput')]
        ncs = set(c_dds_names())
        missing = [n for n in no if n not in ncs]
        check(not missing, f'{tag}: DDS names missing in the port {missing}')
        c_tsClose()


# =====================================================================================
# integration: SES.CFG + SDISB16 (native MCP) + AMF in virtual time
# =====================================================================================
def run_game_audio(amf, seconds, mode, step_us=1000):
    """oses_init + AMF in virtual time.  mode 'raw': the DMA byte stream
    (audio_game_take); 'out': the 44.1 kHz host mix (audio_render) - both
    drain the same FIFO, so one run does one of them."""
    c_dg_reset()
    c_pc_reset()
    c_nses_reset()
    c_audio_reset()
    c_mcp_reset()
    c_heap_reset()
    c_DDSInit()
    c_tsInit()
    vfs_setup(ORIG, ('MUZIK.CKD',))
    r = c_oses_init(0)
    check(r == 0xFF, f'oses_init {r:#x}')
    check(c_audio_running(), 'DMA running')
    m = vp()
    r = c_amp_load(amf.encode(), 0, C.byref(m))
    check(r == 0, f'ampLoadModule {amf} {r}')
    check(c_amp_play(m, 1) == 0, 'ampPlayModule')
    c_pc_virtual(1, step_us)
    data = bytearray()
    obuf = (C.c_int16 * (2 * 4096))()
    tmp = C.create_string_buffer(1 << 16)
    polls = int(seconds * 1e6 / step_us)
    for i in range(polls + 1):
        if i < polls:
            c_pc_poll()
        if i % 20 == 0 or i == polls:
            if mode == 'out':
                n = c_audio_render(obuf, 4096)
                data += bytes(obuf)[:n * 4]
            else:
                c_audio_render(obuf, 0)               # = audio_dma_sync()
                while c_audio_avail():
                    k = c_audio_take(tmp, 1 << 16)
                    data += tmp.raw[:k]
    c_pc_virtual(0, 0)
    c_amp_stop()
    c_tsClose()
    return bytes(data)


def write_wav(path, data, rate):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(data)


def test_integration_amf_playback():
    seconds = 6
    raw1 = run_game_audio('ISTANBUL.AMF', seconds, 'raw')
    dropped = c_audio_dropped()
    raw2 = run_game_audio('ISTANBUL.AMF', seconds, 'raw')
    check(raw1 == raw2, 'game audio (DMA stream) not deterministic')
    check(dropped == 0, f'FIFO dropped {dropped} bytes')
    expect = seconds * 22050 * 4
    check(abs(len(raw1) - expect) <= 4 * 22, f'DMA bytes {len(raw1)} expected ~{expect}')
    samples = struct.unpack(f'<{len(raw1) // 2}h', raw1[:len(raw1) // 2 * 2])
    tail = samples[len(samples) // 4:]
    rms = (sum(s * s for s in tail) / max(len(tail), 1)) ** 0.5
    check(rms > 300, f'game audio silent (rms {rms:.1f})')
    out1 = run_game_audio('ISTANBUL.AMF', seconds, 'out')
    out2 = run_game_audio('ISTANBUL.AMF', seconds, 'out')
    check(out1 == out2, 'host mix not deterministic')
    n_out = len(out1) // 4
    check(abs(n_out - (seconds * 44100 - AUDIO_LAG)) <= 150, f"rendered {n_out} frames")  # output clock starts at the first render (1 ms)
    # 22050 -> 44100: every even output frame is an input frame (lagged)
    o = struct.unpack(f'<{len(out1) // 2}h', out1)
    lag_in = next((k for k in range(0, 200) if
                   all(o[4 * j + c] == samples[2 * (j - k) + c] for j in range(k + 2000, k + 2100)
                       for c in (0, 1))), None)
    check(lag_in is not None, 'output frames are not the DMA frames (linear interpolation)')
    write_wav(os.path.join(OUT, 'istanbul_dma_22050.wav'), raw1, 22050)
    write_wav(os.path.join(OUT, 'istanbul_out_44100.wav'), out1, 44100)
    print(f'    DMA stream {len(raw1)} bytes sha1 {hashlib.sha1(raw1).hexdigest()[:16]} rms {rms:.0f};'
          f' wav: out/wp10/istanbul_dma_22050.wav, istanbul_out_44100.wav (lag {lag_in})')
