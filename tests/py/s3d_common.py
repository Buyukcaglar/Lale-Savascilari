"""s3d_common.py - shared helpers of the 3DSYS tests (test_s3d*.py).

Calls into the other game modules (DEMOLAR, FRPOS, ISOSYS) are stubbed on
both sides: the port's calls are redirected to the hook with lale.patch
(the real modules are linked); the original gets e.stub()s on the same
routines that call the same hook.  install_ext(side, hook) sets both up;
hook(name, args, side) gets Ptr objects for pointer arguments and returns
the routine's result (int) or None (0).  ExtLog is a ready hook that logs
every call (strings read from char * arguments) and answers from a table.

Not a test file (run.py only imports test_*.py).
"""
import ctypes
import struct

from lale import lib, DG, LOAD, patch, unpatch  # noqa: F401

C = ctypes
DGA = C.addressof(DG)
DGSEG_RT = 0x16E0 + LOAD

# name, flat address, argument kinds ('i' int16 word, 'b' byte (pushed as a
# word, high byte undefined), 'p' far pointer)
S3D_EXT = [
    ('demo_game_over', '3C2D:19F1', ''),
    ('demo_ending', '3C2D:1B76', ''),
    ('demo_play_music', '3C2D:1F32', 'iii'),
    ('demo_credits_exit', '3C2D:217C', ''),
    ('demo_city_map_travel', '3C2D:2DE2', ''),
    ('fr_run_event', '3FD1:0000', 'ib'),
    ('fr_redraw', '3FD1:20CE', ''),
    ('fr_item_exchange', '3FD1:2F21', 'b'),
    ('fr_flag_test', '3FD1:4AAC', 'i'),
    ('fr_show_icon', '3FD1:4B57', 'bb'),
    ('fr_draw_money', '3FD1:5550', ''),
    ('iso_draw_text_shadow', '4595:019F', 'piiii'),
    ('iso_clamp_add', '4595:0202', 'piii'),
    ('iso_show_picture', '4595:0BE1', 'i'),
    ('iso_has_status', '4595:20E0', 'ii'),
    ('iso_speak_vecize', '4595:235B', 'i'),
    ('iso_step_sfx', '4595:2C7B', 'i'),
    ('iso_cmd_vecize', '4595:50BD', ''),
    ('iso_party_to_battle', '4595:6345', ''),
    ('iso_calc_weapon', '4595:6828', ''),
    ('iso_calc_stat_a', '4595:6AC2', ''),
    ('iso_calc_stat_b', '4595:6B79', ''),
    ('iso_calc_stat_c', '4595:6CD6', ''),
    ('iso_calc_ap_by_load', '4595:6E4D', ''),
    ('iso_use_item', '4595:7A03', ''),
    ('iso_draw_textbox', '4595:7F29', 'pii'),
    ('iso_refresh_desc', '4595:80D6', ''),
    ('iso_battle_to_party', '4595:8111', ''),
    ('iso_cast_vecize', '4595:84E9', ''),
    ('iso_battle', '4595:B40A', ''),
]



def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Ptr:
    """A pointer argument on either side.  .dg = DGROUP offset or None."""
    def __init__(self, side, e=None, seg=0, off=0, addr=0):
        self.side, self.e, self.seg, self.off, self.addr = side, e, seg, off, addr
        if side == 'o':
            self.dg = off if seg == DGSEG_RT else None
            self.null = seg == 0 and off == 0
        else:
            self.dg = addr - DGA if DGA <= addr < DGA + 0x10000 else None
            self.null = addr == 0

    def read(self, n):
        if self.side == 'o':
            return self.e.rd(self.seg, self.off, n, runtime=True)
        return C.string_at(self.addr, n)

    def write(self, data):
        if self.side == 'o':
            self.e.wr(self.seg, self.off, data, runtime=True)
        else:
            C.memmove(self.addr, bytes(data), len(data))

    def cstr(self, limit=256):
        if self.null:
            return None
        b = self.read(limit)
        return b.split(b'\0')[0]

    def i16(self):
        return struct.unpack('<h', self.read(2))[0]

    def set_i16(self, v):
        self.write(struct.pack('<h', s16(v)))


def install_ext(s, hook):
    """Stub the other modules' routines on side s (uiworld Orig/Port)."""
    if s.side == 'o':
        for name, addr, kinds in S3D_EXT:
            def fn(e, a, name=name, kinds=kinds):
                args, k = [], 0
                for c in kinds:
                    if c == 'p':
                        args.append(Ptr('o', e, a(k + 1), a(k)))
                        k += 2
                    else:
                        args.append(a(k) & 0xFF if c == 'b' else s16(a(k)))
                        k += 1
                r = hook(name, args, s)
                return (r or 0) & 0xFFFF, 0
            s.e.stub(addr, fn)
    else:
        for name, _, kinds in S3D_EXT:
            def cb(*a, name=name, kinds=kinds):
                args = [Ptr('c', addr=a[i] or 0) if c == 'p' else
                        (a[i] & 0xFF if c == 'b' else s16(a[i])) for i, c in enumerate(kinds)]
                return hook(name, args, s) or 0
            patch(name, cb, C.c_int16,
                  *[C.c_void_p if c == 'p' else (C.c_uint8 if c == 'b' else C.c_int16)
                    for c in kinds])


def uninstall_ext():
    for name, _, _ in S3D_EXT:
        unpatch(name)


class ExtLog:
    """Hook that logs calls [(name, args)] (strings for char * arguments,
    DGROUP offsets for other pointers) and returns ret[name] (a value or a
    callable(args, side)); side effects via effect[name](args, side)."""
    def __init__(self, ret=None, effect=None):
        self.log = []
        self.ret = ret or {}
        self.effect = effect or {}

    def __call__(self, name, args, s):
        shown = []
        for a in args:
            if isinstance(a, Ptr):
                shown.append(('dg', a.dg) if a.dg is not None and name != 'iso_draw_text_shadow'
                              and name != 'iso_draw_textbox' else a.cstr())
            else:
                shown.append(a)
        self.log.append((name, tuple(shown)))
        if name in self.effect:
            self.effect[name](args, s)
        r = self.ret.get(name, 0)
        return r(args, s) if callable(r) else r
