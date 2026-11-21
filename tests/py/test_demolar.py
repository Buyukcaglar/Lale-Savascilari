"""DEMOLAR.L (ovl3C2D) against the original, in the world of uiworld.py
(MEMMAN/OGRAPHS up, fonts, pictures in surfaces).  Keys come from the
port's BIOS buffer on both sides (uiworld.Script, full key words), the
100 Hz clock advances per pc_poll(); the original is hooked at the loop
heads where the port polls (POLL below)."""
import ctypes
import struct

from lale import check, fn, DG
from uiworld import run, ORIG_POLL_POINTS, pic, fit

# port pc_poll() at these loop heads of the original:
# demo_wait_key_or_delay 002C; demo_show_picture clock waits 03B0, 0438, 04A8
ORIG_POLL_POINTS.extend((0x3C2D, o) for o in (0x002C, 0x03B0, 0x0438, 0x04A8))

C = ctypes
u8, i16, u16, vp = C.c_uint8, C.c_int16, C.c_uint16, C.c_void_p
DGA = C.addressof(DG)

c = {
    'demo_wait_key_or_delay': fn('demo_wait_key_or_delay', i16, u8, u8, u16),
    'demo_show_picture': fn('demo_show_picture', u8, vp, vp, u16, u8, u8, u16, u16),
    'demo_palette_effect': fn('demo_palette_effect', None, u8, i16),
    'demo_stub_2D3E': fn('demo_stub_2D3E', u8, u8),
    'demo_free_music': fn('demo_free_music', u8),
    'demo_play_music': fn('demo_play_music', None, i16, i16, i16),
    'sys_load_rix': fn('sys_load_rix', u8, C.c_char_p),
}


def call(s, name, *args, ret8=False):
    """args: ints or ('rec', dgoff) for a far MmHandle * in DGROUP"""
    if s.side == 'o':
        words = []
        for a in args:
            if isinstance(a, tuple):
                words += [a[1], 0x1F04]
            else:
                words.append(a & 0xFFFF)
        r = s.c(name, *words)[0]
    else:
        r = c[name](*[(DGA + a[1]) if isinstance(a, tuple) else a for a in args])
    return (r or 0) & (0xFF if ret8 else 0xFFFF)


def rix(s):
    if s.side == 'o':
        h, a = s.string(b'xdintro.rix')
        s.c('sys_load_rix', *a)
    else:
        c['sys_load_rix'](b'xdintro.rix')


# ======================================================================================
def test_wait():
    def op(s):
        return [call(s, 'demo_wait_key_or_delay', 1, 0x20, 200),
                call(s, 'demo_wait_key_or_delay', 2, 0x20, 0)]
    run('wait timeout', op, clock_step=7)
    run('wait key', op, clock_step=7, keys=[0x011B, 0x1C0D], every=5)
    run('wait click', op, clock_step=7, mouse=(10, 10, 1))


# mm_change_type(b, 0) at the end goes through a temporary record (no. 8,
# freed): the original leaves the far pointer of the new block in it, the
# port's MEMMAN (no far pointers for conventional blocks) its old EMS handle.
FREED_REC8 = range(0xDA90, 0xDA94)

PIC_SURFS = [(0x20, 320, 50, 1, 'HAR2.CMP'), (0x21, 320, 50, 0, 'HAR4.CMP'),
             (0x22, 320, 50, 1, 'TRADE.CMP')]


def test_show_picture():
    def op(s):
        rix(s)
        a, b = ('rec', s.rec(0x20)), ('rec', s.rec(0x21))
        r = [call(s, 'demo_show_picture', a, b, 330, 0, 0, 50, 0x3E80, ret8=True)]
        r.append(call(s, 'demo_show_picture', ('rec', s.rec(0x22)), a, 110, 0, 0, 20, 0,
                      ret8=True))
        return r
    run('show_picture', op, surfs=PIC_SURFS, slots=(0x20, 0x21, 0x22), clock_step=10,
        ignore=FREED_REC8)
    run('show_picture key', op, surfs=PIC_SURFS, slots=(0x20, 0x21, 0x22), clock_step=10,
        ignore=FREED_REC8,
        keys=[0x3920], every=3)
    run('show_picture esc', op, surfs=PIC_SURFS, slots=(0x20, 0x21, 0x22), clock_step=10,
        ignore=FREED_REC8,
        keys=[0x011B, 0x011B], every=2)


def test_zone_outline():
    def op(s):
        zones = [(10, 20, 60, 41), (0, -30, 319, 250), (100, 120, 90, 110), (5, 150, 31, 230)]
        for z, (x1, y1, x2, y2) in enumerate(zones):
            s.dgw(0xE5F5 + 8 * (z + 3), struct.pack('<4h', x1, y1, x2, y2))
        for z in range(4):
            for yoff in (0, 40):
                call(s, 'demo_palette_effect', z + 3, yoff)
        return None
    run('zone outline', op)


def test_stub_2D3E():
    def op(s):
        pal = bytes([(i * 7) % 64 if i % 3 != 1 else (i * 5) % 64 for i in range(768)])
        s.pal_write('target', pal)
        return [call(s, 'demo_stub_2D3E', i, ret8=True) for i in range(256)]
    r = run('stub_2D3E', op)
    check(len(set(r[0])) > 2, 'stub_2D3E: all outcomes exercised')


def test_free_music():
    surfs = [(0x41, 64, 16, 0, 'HAR2.CMP'), (0x42, 64, 16, 0, 'HAR4.CMP'),
             (0x43, 64, 16, 1, 'TRADE.CMP'), (0x44, 32, 8, 1, 'HAR2.CMP')]

    def op(s):
        return [call(s, 'demo_free_music', ret8=True) for _ in range(4)]
    r = run('free_music', op, surfs=surfs, slots=(0x41, 0x42, 0x43, 0x44))
    check(r[0] == [1, 1, 0, 0], f'free_music: {r[0]}')


def test_play_music_silent():
    def op(s):
        call(s, 'demo_play_music', 3, 1, 0)
        return None
    run('play_music (sound off)', op)
