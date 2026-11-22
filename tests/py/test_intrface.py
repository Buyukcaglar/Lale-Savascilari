"""INTRFACE.L (seg05DC) against the original, in the world of uiworld.py
(MEMMAN/OGRAPHS up, fonts loaded as main loads them, pictures in the
surfaces).  The routines of other modules INTRFACE calls (through overlay
thunks or window record far pointers) are stubbed with the same behaviour
on both sides and their calls logged: in the emulator at the thunk
address, in the port through ui_register_far_fn.  Keys come from the
port's BIOS buffer on both sides (uiworld.Script), the mouse from
pc_mouse_host."""
import ctypes
import struct

from lale import NAMES, Emu, check, fn, lib, DG
from uiworld import run, L, ORIG_POLL_POINTS

# the port's pc_poll() calls in INTRFACE's busy loops (return addresses of the
# call they follow): the event loop's four button waits (cursor update), ui_wait_voice
# (idle callback), ui_wait_frames and ui_message_box (cursor update),
# ui_wait_click_or_key (gr_call_then_cursor)
ORIG_POLL_POINTS.extend(_pt for _pt in [(0x05DC, o) for o in (0x1415, 0x145C, 0x151D, 0x1564, 0x1D87, 0x1E81, 0x2BFA,
                                                0x2E2D)]
                        if _pt not in ORIG_POLL_POINTS)   # once, whatever the import order

C = ctypes
u8, i16, u16, u32, vp = C.c_uint8, C.c_int16, C.c_uint16, C.c_uint32, C.c_void_p
DGA = C.addressof(DG)
P16 = C.POINTER(C.c_int16)


def F(name, res, *args):
    return fn(name, res, *args)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def w16(s, off, v):
    s.dgw(off, struct.pack('<h', v))


def r16(s, off):
    return struct.unpack('<h', s.dg(off, 2))[0]


c_register = F('ui_register_far_fn', None, u16, u16, vp)

# ---- stubs of the other modules --------------------------------------------------
LOG = []
V = C.CFUNCTYPE(None)
CLAMP = C.CFUNCTYPE(None, P16, i16, i16, i16)
SHADOW = C.CFUNCTYPE(None, C.c_char_p, i16, i16, i16, i16)
IDLE = C.CFUNCTYPE(None, u32)
FILL = C.CFUNCTYPE(None, i16, i16, i16, i16, i16, i16)
SHAPE = C.CFUNCTYPE(i16)


def clamp(v, d, lo, hi):
    v = s16((v + d) & 0xFFFF)
    return max(lo, min(hi, v))


def _c_clamp(p, d, lo, hi):
    LOG.append(('clamp', p[0], d, lo, hi))
    p[0] = clamp(p[0], d, lo, hi)


_C_STUBS = {
    (0x1694, 0x0025): CLAMP(_c_clamp),
    (0x1694, 0x0052): SHADOW(lambda s, x, y, c, k: LOG.append(('shadow', s, x, y, c, k))),
    (0x1670, 0x009D): IDLE(lambda a: LOG.append(('idle', a))),
    (0x1670, 0x003E): V(lambda: LOG.append(('panel',))),
    (0x1682, 0x002F): V(lambda: LOG.append(('over',))),
    (0x1689, 0x003E): FILL(lambda *a: LOG.append(('fill',) + a)),
    (0x1670, 0x0084): SHAPE(lambda: LOG.append(('shape',)) or 0x2C),
    (0x1689, 0x0098): SHAPE(lambda: LOG.append(('shape',)) or 0x2C),
}
for (sg, of), f in _C_STUBS.items():
    c_register(sg, of, C.cast(f, vp))


def asciiz(e, off, seg):
    out = bytearray()
    while True:
        b = e.rd(seg, off + len(out), 1, runtime=True)[0]
        if b == 0:
            return bytes(out)
        out.append(b)


def stub_orig(s):
    e = s.e

    def o_clamp(e, a):
        p = e.rw(a(1), a(0), runtime=True)
        d, lo, hi = s16(a(2)), s16(a(3)), s16(a(4))
        LOG.append(('clamp', s16(p), d, lo, hi))
        e.ww(a(1), a(0), clamp(s16(p), d, lo, hi), runtime=True)
    e.stub('1694:0025', o_clamp)
    e.stub('1694:0052', lambda e, a: LOG.append(('shadow', asciiz(e, a(0), a(1)), s16(a(2)), s16(a(3)),
                                                 s16(a(4)), s16(a(5)))))
    e.stub('1670:009D', lambda e, a: LOG.append(('idle', a(0) | a(1) << 16)))
    e.stub('1670:003E', lambda e, a: LOG.append(('panel',)))
    e.stub('1682:002F', lambda e, a: LOG.append(('over',)))
    e.stub('1689:003E', lambda e, a: LOG.append(('fill',) + tuple(s16(a(i)) for i in range(6))))
    e.stub('1670:0084', lambda e, a: LOG.append(('shape',)) or (0x2C, 0))
    e.stub('1689:0098', lambda e, a: LOG.append(('shape',)) or (0x2C, 0))
    # CD audio: the port's CD layer on both sides
    e.stub('16D3:0039', lambda e, a: (lib.o5737_cd_play() & 0xFF, 0))
    e.stub('16D3:0034', lambda e, a: (F('o5737_cd_play_track', i16, u8, C.c_int32)(
        a(0) & 0xFF, s16(a(1)) | (s16(a(2)) << 16)) & 0xFFFF, 0))


def log_extra(s):
    out = list(LOG)
    LOG.clear()
    return out


# ---- the port's functions -----------------------------------------------------------
c = {
    'ui_init_tables': F('ui_init_tables', None),
    'ui_memcpy': F('ui_memcpy', None, vp, vp),
    'ui_get_9671': F('ui_get_9671', i16),
    'ui_rect_op_0568': F('ui_rect_op_0568', None, i16, i16, i16, i16),
    'ui_rect_op_05E4': F('ui_rect_op_05E4', None, i16, i16, i16, i16),
    'ui_ask_evet_hayir': F('ui_ask_evet_hayir', u8),
    'ui_format_text': F('ui_format_text', u8, u8, vp, i16, i16, u8, vp, u8),
    'ui_text_measure_0004': F('ui_text_measure_0004', None, vp, i16, i16, u8, i16),
    'ui_save_background': F('ui_save_background', None),
    'ui_restore_background': F('ui_restore_background', None),
    'ui_button_press_anim': F('ui_button_press_anim', None, vp, i16),
    'ui_button_release_anim': F('ui_button_release_anim', None, vp, i16),
    'ui_draw_panel_027B': F('ui_draw_panel_027B', None, i16, i16, i16, i16),
    'ui_draw_panel_0308': F('ui_draw_panel_0308', None, i16, i16, i16, i16),
    'ui_setup_hotspots': F('ui_setup_hotspots', None),
    'ui_update_hover': F('ui_update_hover', None, i16, i16, i16),
    'ui_unhighlight': F('ui_unhighlight', None),
    'ui_free_panel': F('ui_free_panel', None),
    'ui_cd_music': F('ui_cd_music', None, u32),
    'ui_event_loop': F('ui_event_loop', None, i16, i16, i16, i16, i16, i16, i16, i16),
    'ui_poll_input': F('ui_poll_input', u8, u8),
    'ui_dialog_2198': F('ui_dialog_2198', None, vp, vp),
    'ui_message_box': F('ui_message_box', u8, vp, i16, i16),
    'ui_wait_frames': F('ui_wait_frames', None, i16),
    'ui_wait_click_or_key': F('ui_wait_click_or_key', None, i16),
    'ui_wait_voice': F('ui_wait_voice', i16),
    'ui_play_voice': F('ui_play_voice', u8, i16, u16),
    'ui_load_voice': F('ui_load_voice', None, i16, u32, u16, u16),
    'ui_init_voices': F('ui_init_voices', None),
    'ui_call_9650': F('ui_call_9650', i16),
}


def call(s, name, *args, ret8=False):
    """args: ints, or ('str', handle) pairs already converted by the caller"""
    if s.side == 'o':
        words = []
        for a in args:
            words.extend(a if isinstance(a, list) else [a & 0xFFFF])
        r = s.c(name, *words)[0]
        return r & (0xFF if ret8 else 0xFFFF)
    r = c[name](*[(a if not isinstance(a, list) else a[0]) for a in args])
    return (r or 0) & (0xFF if ret8 else 0xFFFF)


def ptr(s, data, size=None):
    """far pointer argument for a fresh buffer: orig -> [off, seg], port -> [addr]"""
    h, a = s.string(data, size)
    return h, (a if s.side == 'o' else [a])


def dgptr(s, off):
    return [off, 0x1F04] if s.side == 'o' else [DGA + off]


def setup_tables(s, rec=0x9824):
    """main's sys_load_rix("xdintro.rix") (the blend table gr_draw_strip
    needs), ui_init_tables, then window record `rec` made current (as the
    game does)"""
    if s.side == 'o':
        stub_orig(s)
        h, a = s.string(b'xdintro.rix')
        s.c('sys_load_rix', *a)
    else:
        F('sys_load_rix', None, C.c_char_p)(b'xdintro.rix')
    call(s, 'ui_init_tables')
    call(s, 'ui_memcpy', dgptr(s, 0x962C), dgptr(s, rec))
    LOG.clear()


PANEL_SURFS = [(1, 320, 200, 1, 'HAR2.CMP'), (0x4B, 780, 15, 1, 'ITEMBANK.CMP'),
               (0x4E, 320, 40, 1, 'PCMAINTH.CMP'), (0x4D, 320, 20, 1, 'MERDIM.CMP'),
               (0x47, 200, 40, 0, 'KAPI1.CMP'), (0x48, 200, 40, 1, 'TRADE.CMP'),
               (0x49, 200, 40, 0, 'HAR3.CMP'), (0x4A, 200, 40, 1, 'GOTY0.CMP')]


def prun(label, op, rec=0x9824, **kw):
    pre = kw.pop('pre', None)

    def setup(s):
        setup_tables(s, rec)
        if pre:
            pre(s)
    return run(label, op, setup=setup, extra=log_extra, surfs=kw.pop('surfs', PANEL_SURFS), **kw)


# ======================================================================================
def test_init_tables():
    e = Emu()
    ob = e.dgroup(0, 0x10000)
    e.call(NAMES['ui_init_tables'])
    o = e.dgroup(0, 0x10000)
    lib.dg_reset()
    cb = bytes(DG)
    c['ui_init_tables']()
    cc = bytes(DG)
    # bytes either side wrote (the images differ in their relocated far pointers)
    touched = [i for i in range(0x10000) if o[i] != ob[i] or cc[i] != cb[i]]
    bad = [i for i in touched if o[i] != cc[i]]
    check(len(touched) > 0x100, f'ui_init_tables: only {len(touched)} bytes written')
    check(not bad, 'ui_init_tables: DGROUP differs at ' + ', '.join(
        f'{i:04X}:{o[i]:02X}/{cc[i]:02X}' for i in bad[:8]))


def test_small():
    def op(s):
        out = []
        s.dgw(0x9671, b'\x37')
        out.append(call(s, 'ui_get_9671'))
        for n, a, b, h in ((1, 2, 3, 11), (5, 30, 7, 7), (9, -2, -5, 11)):
            w16(s, 0x9658, 0x94)
            call(s, 'ui_rect_op_0568', n, a, b, h)
            call(s, 'ui_rect_op_05E4', n + 1, a, b, h)
        out.append(call(s, 'ui_ask_evet_hayir', ret8=True))
        out.append(call(s, 'ui_ask_evet_hayir', ret8=True))
        return out
    prun('small', op, keys=[0x1E61, 0x1265, 0x2D78, 0x2368])


FMT = [b'Tek satir', b'Iki\xb6satir uzun', b'Bir\nIki\xb6Uc', b'\xb6bas', b'son\n', b'',
       b'\x98stanbul \x80ok g\x81zel']


def test_format_text():
    def op(s):
        out = []
        s.dgw(0x9668, b'\x2a')
        for i, t in enumerate(FMT):
            for flags in range(4):
                h, a = ptr(s, t)
                ph, pa = ptr(s, b'\x05\x00')
                r = call(s, 'ui_format_text', 0, a, 40 + 30 * (flags & 1), 10 + 20 * i + 4 * flags,
                         0x0F + flags, pa, flags, ret8=True)
                out.append((r, s.string_read(h, len(t) + 1), s.string_read(ph, 2)))
        return out
    prun('ui_format_text', op)


def test_list_items():
    def op(s):
        call(s, 'ui_save_background')
        for i, (t, h) in enumerate(((b'Kili\x87', 11), (b'Yay ve ok', 7), (b'\x98ksir', 11))):
            hh, a = ptr(s, t)
            call(s, 'ui_text_measure_0004', a, 2 + i, 1 + i, 0x20 + i, h)
        return None
    prun('ui_text_measure_0004', op, slots=(0x17,), pre=lambda s: (w16(s, 0x965A, 0x40), w16(s, 0x9658, 0x88)))


def panel_ops(s):
    out = []
    ch, cp = ptr(s, b'\x07\x00')
    call(s, 'ui_button_press_anim', cp, 4)
    out.append(s.string_read(ch, 2))
    call(s, 'ui_draw_panel_027B', 1, 2, 11, 1)
    call(s, 'ui_update_hover', 2, 0, 11)
    call(s, 'ui_unhighlight')
    call(s, 'ui_draw_panel_0308', 1, 2, 11, 0)
    call(s, 'ui_draw_panel_027B', 1, 2, 11, 0)
    call(s, 'ui_free_panel')
    call(s, 'ui_button_release_anim', cp, 9)
    out.append(s.string_read(ch, 2))
    call(s, 'ui_draw_panel_0308', 1, 2, 11, 1)
    call(s, 'ui_update_hover', 2, 0, 11)
    call(s, 'ui_free_panel')
    return out


def test_panel():
    prun('panel', panel_ops)
    prun('panel (cursor over it)', panel_ops, mouse=(100, 160, 0))


def test_hotspots():
    def pre(s):
        w16(s, 0x2898, 2)
        for i, v in enumerate((0, 3, 105, 0, 47)):
            w16(s, 0x99D2 + 0x5C * 2 + 2 * (i + 1), v)
        s.dgw(0x9666, b'\x21')

    def op(s):
        ch, cp = ptr(s, b'\x07\x00')
        call(s, 'ui_button_press_anim', cp, 4)
        call(s, 'ui_setup_hotspots')
        call(s, 'ui_free_panel')
        return None
    prun('ui_setup_hotspots', op, pre=pre)


def mouse_on(s):
    """main's mouse setup: driver found, cursor shown (shape 3)"""
    if s.side == 'o':
        s.c('ogr_mouse_init')
        s.c('gr_cursor_set_shape', 3)
    else:
        F('ogr_mouse_init', None)()
        F('gr_cursor_set_shape', None, u8)(3)


def list_setup(s):
    for n in range(1, 8):
        s.dgw(0xA5D8 + 0x64 * n, b'Secenek %d \x87' % n + b'\0')
    w16(s, 0xD5DC, 1)


def event_op(n, x, y, cnt, style=1):
    def op(s):
        call(s, 'ui_event_loop', n, x, y, 0x0F, 0x2E, 0x31, cnt, style)
        return [r16(s, o) for o in (0xD5D6, 0xD5D8, 0xD5DC, 0xD5E4)]
    return op


def test_event_loop():
    down, up, enter, esc = 0x5000, 0x4800, 0x1C0D, 0x011B
    kw = dict(pre=list_setup, every=6)
    prun('event_loop keys', event_op(6, 1, 0, 3), keys=[down] * 4 + [up, enter], **kw)
    prun('event_loop esc', event_op(6, 1, 0, 3), keys=[down, esc], **kw)
    prun('event_loop font 2', event_op(7, 3, 1, 4, 0), keys=[down] * 5 + [up] * 2 + [enter], **kw)
    prun('event_loop no scroll', event_op(3, 1, 0, 3), keys=[up, down, down, down, enter], **kw)
    # mouse: a left click on item 2's hotspot (zones from ui_rect_op_05E4)
    # mouse (Script moves at poll counts): over item 3's hotspot (zones from
    # ui_rect_op_05E4: y = row*11 + 94h - 8), then a left click, released later
    kw = dict(pre=lambda s: (list_setup(s), mouse_on(s)), every=6)
    # (the driver's x is doubled in mode 13h: host x = 2 * pixel x)
    y3 = 3 * 11 + 0x94 - 8 + 4
    ya = 0x94 + 0x1A + 4            # hotspot 39 (down arrow, ui_update_hover)
    prun('event_loop hover click', event_op(4, 1, 0, 3),
         moves=[(3, 40, y3, 0), (12, 40, y3, 1), (20, 40, y3, 0)], **kw)
    prun('event_loop arrow click', event_op(6, 1, 0, 3),
         moves=[(3, 10, ya, 1), (8, 10, ya, 0), (12, 10, ya, 1),
                (16, 10, ya, 0), (24, 400, 20, 2), (30, 400, 20, 0)], **kw)
    prun('event_loop right button', event_op(4, 1, 0, 3),
         moves=[(3, 400, 20, 2), (9, 400, 20, 0)], **kw)


def test_poll_input():
    def op(s):
        return call(s, 'ui_poll_input', 3, ret8=True)
    for keys in ([0x3C00], [0x011B], [0x1E61, 0x3E00], [0x0100]):
        prun(f'ui_poll_input {keys}', op, keys=keys)
    # the buttons are read only while the cursor is shown (E5F3); a hidden
    # cursor keeps the last state, here a right press
    prun('ui_poll_input right', op, pre=lambda s: s.dgw(0xE5E5, b''))


def test_dialog():
    def op(s):
        ha, a = ptr(s, b'EVET', 16)
        hb, b = ptr(s, b'Hay\x8dr', 16)
        call(s, 'ui_dialog_2198', a, b)
        return s.string_read(ha, 6), s.string_read(hb, 6), r16(s, 0xBB9E)
    prun('ui_dialog e', op, keys=[0x1265])
    prun('ui_dialog h', op, keys=[0x2D78, 0x2348])
    prun('ui_dialog H twice', op, keys=[0x2348], pre=lambda s: s.dgw(0xBBAA, b'\x05'))


def test_message_box():
    text = b'merhaba yolcu!\xb6bu satir ikinci\nve \x9fu \x87\x81\x87\x81nc\x81\xb6d\x94rt'

    def pre(s):
        for p, hp in ((1, 0), (2, 5), (3, -1), (4, 9)):
            w16(s, 0x99E0 + 0x5C * p, hp)
        s.dgw(0x9665, b'\x40')
        s.dgw(0x966C, b'\x50')

    def op_mode(mode, who):
        def op(s):
            h, a = ptr(s, text, 80)
            r = call(s, 'ui_message_box', a, mode, who, ret8=True)
            return r, s.string_read(h, len(text) + 1)
        return op
    prun('ui_message_box mode 1', op_mode(1, 0), pre=pre)
    prun('ui_message_box mode 1 outlined', op_mode(1, 0),
         pre=lambda s: (pre(s), s.dgw(0x9673, b'\x02')))
    prun('ui_message_box mode 0 key', op_mode(0, 1), pre=pre, keys=[0x3920] * 6, every=40,
         clock_step=10)
    prun('ui_message_box mode 0 all', op_mode(0, 2), pre=pre, clock_step=25)


def test_waits():
    def op(s):
        s.dgw(0xBB35, b'\x03')
        call(s, 'ui_wait_frames', 3)
        call(s, 'ui_wait_click_or_key', 30)
        return call(s, 'ui_call_9650')
    prun('waits', op, clock_step=5)
    prun('waits key', op, clock_step=5, keys=[0x3920] * 3 + [0x1C0D], every=200)
    prun('waits over panel', op, clock_step=5, mouse=(100, 160, 0))


def test_voices_silent():
    def op(s):
        out = [call(s, 'ui_wait_voice'), call(s, 'ui_play_voice', 3, 0, ret8=True)]
        s.dgw(0x73D1, struct.pack('<I', 0x500))
        s.dgw(0x73D5, struct.pack('<H', 0x200))
        out.append(call(s, 'ui_wait_voice'))
        s.dgw(0x73D1, struct.pack('<I', 0x400))
        out.append(call(s, 'ui_play_voice', 3, 0, ret8=True))
        call(s, 'ui_init_voices')
        call(s, 'ui_load_voice', 3, *([L(8000)] if s.side == 'o' else [8000]), 100, 0x18)
        return out
    prun('voices (sound off)', op, clock_step=7)


def test_cd_music():
    def op(s):
        call(s, 'ui_cd_music', *([L(0x12345678)] if s.side == 'o' else [0x12345678]))
        return None
    prun('ui_cd_music', op)
