"""CHARGEN.L (ovl39E5, src/game/chargen.c) against the original, in the
world of uiworld.py (MEMMAN/OGRAPHS up, fonts loaded).  The ISOSYS
helpers CHARGEN calls (iso_draw_text_shadow, iso_clamp_add,
iso_draw_sprite, iso_show_picture) run as the original's code in the
emulator and as src/game/iso_util.c in the port, so their output is
compared too.

Files: the port reads/writes a private game folder (out/test_chargen_game:
the archives linked, GRP.DAT/GRPHAZIR.DAT copied); the original's file
layer (o569A) is a Python stand-in over the same folder and the extracted
archives, which captures what it writes.

The idle callback of the UI window record (16E0:964C) is pointed at
1670:009D and stubbed (logged) on both sides.  Keys and mouse come from a
scripted pc_poll() pump (KeyScript), the 100 Hz clock advances per poll;
both sides poll at the same points (the original's busy loops through
ORIG_POLL_POINTS), so their random(1000) streams stay in step.
"""
import ctypes
import os
import random
import shutil
import struct

import uiworld
from lale import NAMES, ROOT, check, fn, lib, DG
from helpers import (pascal_stub, c_stdio_reset, c_setupio, c_vfile_reset, c_vfs_reset,
                     c_vfs_init, c_vfs_mount, c_set_dir, vga_dac)
from uiworld import run, Script, ORIG_POLL_POINTS, pc_key_push, pc_mouse_host

C = ctypes
u8, i16, u16, u32, vp, cp = C.c_uint8, C.c_int16, C.c_uint16, C.c_uint32, C.c_void_p, C.c_char_p

GAME = os.path.join(ROOT, 'out', 'test_chargen_game')
ARCHIVES = (b'3D.CKD', b'IVIR.CKD', b'SAVAS.CKD')
SEARCH = [os.path.join(ROOT, 'extracted', a) for a in ('IVIR', 'SAVAS', '3D')] + [GAME]

# the port's pc_poll() points in busy loops the original spins without one
POLLS = [(0x0C7F, 0x0854),      # gr_fade: end of the loop body, before the clock read
         (0x39E5, 0x204C)]      # cg_main: button release wait, after gr_cursor_update


def game_dir():
    """(re)create the private game folder: archives linked, party files copied"""
    os.makedirs(GAME, exist_ok=True)
    for a in ARCHIVES:
        dst = os.path.join(GAME, a.decode())
        if not os.path.exists(dst):
            try:
                os.link(os.path.join(ROOT, 'orig', a.decode()), dst)
            except OSError:
                shutil.copy(os.path.join(ROOT, 'orig', a.decode()), dst)
    for f in ('GRP.DAT', 'GRPHAZIR.DAT'):
        shutil.copy(os.path.join(ROOT, 'orig', f), os.path.join(GAME, f))


def cg_mount():
    """port side: fresh stdio + vfs over the private game folder"""
    c_stdio_reset()
    c_vfile_reset()
    c_vfs_reset()
    c_set_dir(GAME.encode())
    c_setupio()
    c_vfs_init(20)
    for a in ARCHIVES:
        check(c_vfs_mount(a, None) >= 0, f'mount {a}')


def find(name):
    for d in SEARCH:
        for n in os.listdir(d):
            if n.lower() == name.lower():
                return os.path.join(d, n)
    return None


WRITTEN = {}


def cg_stub_files(e):
    """o569A open/close/read/write for the original (pascal), over SEARCH;
    files opened for writing (mode 4) are captured in WRITTEN"""
    files = {}

    def name_at(seg, off):
        s = bytearray()
        while True:
            b = e.rd(seg, off + len(s), 1, runtime=True)[0]
            if not b:
                return s.decode('cp857')
            s.append(b)

    def op(w):                                      # (name far, int mode)
        name = name_at(w[2], w[1])
        h = max(files, default=0) + 1
        if w[0] == 4:
            files[h] = [bytearray(), 0, name.upper()]
            return h
        p = find(name)
        if not p:
            return 0xFFFF
        files[h] = [open(p, 'rb').read(), 0, None]
        return h

    def cl(w):
        f = files.pop(w[0], None)
        if f and f[2]:
            WRITTEN[f[2]] = bytes(f[0])
        return 0

    def rd(w):                                      # (h, buf far, long n)
        f = files.get(w[4])
        if f is None:
            return 0xFFFFFFFF
        n = w[0] | w[1] << 16
        data = f[0][f[1]:f[1] + n]
        f[1] += len(data)
        e.u.mem_write(w[3] * 16 + w[2], bytes(data))
        return len(data)

    def wr(w):
        f = files.get(w[4])
        if f is None:
            return 0xFFFFFFFF
        n = w[0] | w[1] << 16
        f[0] += bytes(e.u.mem_read(w[3] * 16 + w[2], n))
        return n
    def sk(w):                                      # (h, long off, int whence)
        f = files[w[3]]
        off = w[1] | w[2] << 16
        off -= (1 << 32) if off & 0x80000000 else 0
        f[1] = [0, f[1], len(f[0])][w[0]] + off
        return 0

    def tl(w):
        return files[w[0]][1]
    pascal_stub(e, '16CD:0039', 4, sk)
    pascal_stub(e, '16CD:003E', 1, tl)
    pascal_stub(e, '16CD:0025', 3, op)
    pascal_stub(e, '16CD:002A', 1, cl)
    pascal_stub(e, '16CD:002F', 5, rd)
    pascal_stub(e, '16CD:0034', 5, wr)


# uiworld builds both sides with these, during this module's tests only
# (the wrapper at the end; other modules share uiworld)
_CG_WORLD = (cg_mount, cg_stub_files)

# ---- the idle callback (window record +20h) ----------------------------------------
LOG = []
IDLE_T = C.CFUNCTYPE(None, u32)
_c_idle = IDLE_T(lambda a: LOG.append(('idle', a)))
c_register = fn('ui_register_far_fn', None, u16, u16, vp)
IDLE_FAR = ((0x1670 + 0x824) << 16) | 0x009D


def setup_common(s):
    """main's state when it calls cg_main: mouse on, idle callback set"""
    if s.side == 'o':
        s.e.stub('1670:009D', lambda e, a: LOG.append(('idle', a(0) | a(1) << 16)))
        s.c('ogr_mouse_init')
        s.c('gr_cursor_set_shape', 3)
    else:
        c_register(0x1670, 0x009D, C.cast(_c_idle, vp))
        fn('ogr_mouse_init', None)()
        fn('gr_cursor_set_shape', None, u8)(3)
    s.dgw(0x964C, struct.pack('<I', IDLE_FAR))
    s.dgw(0x9660, struct.pack('<I', 0x12345678))
    s.dgw(0x9666, b'\x0F')                          # text colour (ui_init_tables value)
    LOG.clear()


def extra(s):
    out = (list(LOG), vga_dac(), dict(WRITTEN), port_file() if s.side == 'c' else None)
    LOG.clear()
    WRITTEN.clear()
    return out


def port_file():
    """the port's GRP.DAT if it was written (differs from the copy made by
    game_dir), else None"""
    d = open(os.path.join(GAME, 'GRP.DAT'), 'rb').read()
    return None if d == open(os.path.join(ROOT, 'orig', 'GRP.DAT'), 'rb').read() else d


def norm(x):
    """extra() of both sides made comparable: the port's GRP.DAT is a real
    file, the original's is captured"""
    log, dac, written, pf = x
    return log, dac, written.get('GRP.DAT', pf)


class KeyScript(Script):
    """keys pushed at given poll numbers [(poll, word)], mouse moves
    [(poll, x, y, buttons)], clock + clock_step per poll"""
    def __init__(self, keys=(), clock_step=0, moves=()):
        super().__init__((), 1, clock_step, moves)
        self.timed = sorted(keys)

    def _pump(self):
        while self.timed and self.timed[0][0] <= self.polls + 1:
            pc_key_push(self.timed.pop(0)[1])
        super()._pump()


def call(s, name, *args, ret='int'):
    if s.side == 'o':
        return s.e.call(NAMES[name], *args, max_insns=3_000_000_000)[0]
    r = fn(name, i16 if ret == 'int' else None, *([vp] * len(args)))(*args)
    return r


# the cursor shapes (main loads them into slot 4Bh; any 15-pixel strip will do)
CURSOR_SURF = (0x4B, 780, 15, 1, 'ITEMBANK.CMP')


def crun(label, op, keys=(), moves=(), clock_step=40, surfs=(), setup=None):
    """op(side) on both sides with KeyScript input; DGROUP, A000, DAC,
    GRP.DAT and the idle log compared"""
    res, snaps = [], []
    for side in 'oc':
        game_dir()
        WRITTEN.clear()
        s = uiworld.world(side, surfs=list(surfs) + [CURSOR_SURF])
        setup_common(s)
        if setup:
            setup(s)
        sc = KeyScript(keys, clock_step, moves)
        old = list(ORIG_POLL_POINTS)
        ORIG_POLL_POINTS.extend(POLLS)
        sc.start(s)
        try:
            r = op(s)
        finally:
            Script.stop()
            ORIG_POLL_POINTS[:] = old
        snaps.append((s.snap(), norm(extra(s))))
        res.append(r)
    (so, xo), (sc_, xc) = snaps
    uiworld.compare(label, so, sc_)
    check(xo[0] == xc[0], f'{label}: idle log {len(xo[0])} vs {len(xc[0])} calls')
    check(xo[1] == xc[1], f'{label}: DAC differs')
    check(xo[2] == xc[2], f'{label}: GRP.DAT differs ({len(xo[2] or b"")} / {len(xc[2] or b"")} bytes)')
    check(res[0] == res[1], f'{label}: result {res[0]} vs {res[1]}')
    return res, xo


def stats(s, p):
    return struct.unpack('<46h', s.dg(0x99A6 + 0x5C * p, 0x5C))


def w16(s, off, v):
    s.dgw(off, struct.pack('<h', v))


# =====================================================================================
WORK_SURFS = [(1, 320, 200, 1, 'KARAKTER.CMP'), (6, 320, 200, 0, 'HAR2.CMP'),
              (0x45, 200, 20, 1, 'SEMATIK.CMP')]


def test_roll_stats():
    """cg_roll_stats (+ cg_random_gender, cg_draw_stats) for every race,
    gender and member, several seeds and garbage in the other words"""
    cases = []
    rnd = random.Random(14)
    for race in range(1, 9):
        for gender in (1, 2):
            for p in (1, 4):
                cases.append((race, gender, p, rnd.randrange(1 << 32),
                              [rnd.randrange(-200, 200) for _ in range(46)]))
    for _ in range(40):
        cases.append((rnd.randrange(1, 9), rnd.randrange(1, 3), rnd.randrange(1, 5),
                      rnd.randrange(1 << 32), [rnd.randrange(-32768, 32768) for _ in range(46)]))

    def op(s):
        out = []
        for race, gender, p, seed, junk in cases:
            s.dgw(0x99A6 + 0x5C * p, struct.pack('<46h', *junk))
            w16(s, 0x99A6 + 0x5C * p + 2, race)
            w16(s, 0x99A6 + 0x5C * p + 4, gender)
            w16(s, 0x2898, p)
            s.dgw(0x94C4, struct.pack('<I', seed))
            call(s, 'cg_roll_stats')
            out.append((stats(s, p), s.dg(0x9B72 + 0x24 * p, 0x24), s.dg(0x94C4, 4),
                        s.dg(0x9F0A, 8), s.dg(0xBB7C, 11)))
        return out
    res, _ = crun('cg_roll_stats', op, surfs=WORK_SURFS)


def test_draw():
    """the small drawing routines and the animation step, with blink states"""
    def op(s):
        out = []
        for p, race, gender, port in ((1, 1, 1, 1), (2, 8, 2, 16), (3, 4, 2, 6), (4, 6, 1, 11)):
            w16(s, 0x2898, p)
            w16(s, 0x99A6 + 0x5C * p + 2, race)
            w16(s, 0x99A6 + 0x5C * p + 4, gender)
            w16(s, 0x99A6 + 0x5C * p + 0x42, port)
            s.dgw(0x9C26 + 0x94 * p, b'Ay\x9fe %d\0' % p)
            call(s, 'cg_random_gender')
            call(s, 'cg_load_char_graphics')
            call(s, 'cg_draw_header')
            call(s, 'cg_draw_field_a')
            call(s, 'cg_draw_field_b')
            call(s, 'cg_draw_stats')
            for blink in (0, 1, 2, 3, 1):
                w16(s, 0x48EA, blink)
                call(s, 'cg_roll_misc')
                out.append(s.dg(0x48E8, 4))
            out.append(s.read(6))
        return out
    crun('cg_draw', op, surfs=WORK_SURFS)


def test_load_party():
    def op(s):
        out = []
        for name in (b'grp.dat', b'grphazir.dat', b'GRP.DAT'):
            h, a = s.string(name)
            call(s, 'cg_load_party', *(a if s.side == 'o' else [a]))
            out.append(s.dg(0x99A6, 0x5C * 5 + 0x24 * 5 + 0x94 * 5))
        return out
    crun('cg_load_party', op)


def test_save_party():
    """random party records -> GRP.DAT; then read back with cg_load_party"""
    rnd = random.Random(1414)
    blob = bytes(rnd.randrange(256) for _ in range(0x5C * 5 + 0x24 * 5 + 0x94 * 5))
    races = [rnd.randrange(1, 9) for _ in range(5)]

    def op(s):
        s.dgw(0x99A6, blob)
        for p in range(5):
            w16(s, 0x99A6 + 0x5C * p + 2, races[p])
        call(s, 'cg_save_party')
        out = [s.dg(0x99A6, len(blob))]
        s.dgw(0x99A6, bytes(len(blob)))
        h, a = s.string(b'grp.dat')
        a = a if s.side == 'o' else [a]
        if s.side == 'o':                       # the stand-in reads the captured file
            open(os.path.join(GAME, 'GRP.DAT'), 'wb').write(WRITTEN['GRP.DAT'])
        call(s, 'cg_load_party', *a)
        out.append(s.dg(0x99A6, len(blob)))
        return out
    crun('cg_save_party', op)


# ---- the whole screen ---------------------------------------------------------------
# key words (scan << 8 | ASCII)
RIGHT, LEFT, UP, DOWN = 0x4D00, 0x4B00, 0x4800, 0x5000
ENTER, ESC, SPACE, TAB, KEY_C = 0x1C0D, 0x011B, 0x3920, 0x0F09, 0x2E63


def text(t):
    sc = {'a': 0x1E, 'b': 0x30, 'c': 0x2E, 'd': 0x20, 'e': 0x12, 'f': 0x21, 'g': 0x22,
          'h': 0x23, 'i': 0x17, 'k': 0x25, 'l': 0x26, 'm': 0x32, 'n': 0x31, 'o': 0x18,
          'r': 0x13, 's': 0x1F, 't': 0x14, 'u': 0x16, 'y': 0x15, 'z': 0x2C}
    return [(sc[ch.lower()] << 8) | ord(ch) for ch in t]


def timed(seq, start=100, gap=70):
    """[(poll, key)] one key every `gap` polls"""
    return [(start + i * gap, k) for i, k in enumerate(seq)]


def main_op(s):
    if s.side == 'o':
        r = s.e.call(NAMES['cg_main'], max_insns=4_000_000_000)[0]
    else:
        r = fn('cg_main', i16)()
    return r & 0xFFFF, s.dg(0x99A6, 0x5C * 5 + 0x24 * 5 + 0x94 * 5), s.dg(0x2898, 2)


def seed_names(s):
    """non-empty names for members 1..4.  grt_input_line on an empty string
    does farmalloc(0) = NULL and _fstrcpy's into it: the original writes a
    NUL to 0000:0000 (INT 0 vector), the port crashes (engine issue,
    reported); so the flows edit existing names."""
    for p in range(1, 5):
        s.dgw(0x9C26 + 0x94 * p, b'k\0')


def member(name, extra=()):
    return list(extra) + [TAB] + text(name) + [ENTER, SPACE]


def test_main_cancel():
    """Esc on member 1: nothing written, returns 0"""
    res, _ = crun('cg_main cancel', main_op, keys=timed([ESC]))
    check(res[0][0] == 0, f'cg_main cancel: returned {res[0][0]}')


def test_main_party():
    """four members with every keyboard command, a step back, GRP.DAT"""
    seq = (member('Ali', [RIGHT, RIGHT, LEFT, ENTER, KEY_C, UP, UP, DOWN])
           + member('Ayse', [KEY_C, UP, RIGHT])
           + [ESC]                                      # back to member 2 (kept name)
           + [SPACE]
           + [UP] * 7 + [UP]                            # wraps 8 -> 1
           + member('Rifki', [LEFT, LEFT, ENTER, ENTER])
           + [SPACE]                                    # no name: ignored
           + member('Zeki', [DOWN, KEY_C, KEY_C]))
    res, xo = crun('cg_main party', main_op, keys=timed(seq), setup=seed_names)
    check(res[0][0] == 1, f'cg_main party: returned {res[0][0]}')
    check(len(xo[2] or b'') == 664, f'cg_main party: GRP.DAT {len(xo[2] or b"")} bytes')


def zone_xy(z):
    """host mouse position (x doubled in mode 13h) in the middle of zone z"""
    x1, y1, x2, y2 = {1: (0x11, 0x35, 0x41, 0x83), 2: (0x44, 0x35, 0x64, 0x55),
                      3: (0x5A, 0x57, 0x63, 0x6A), 4: (0x5A, 0x6E, 0x63, 0x81),
                      5: (0x45, 0x88, 0x6B, 0x9C), 6: (0x0F, 0x88, 0x35, 0x9C),
                      7: (0, 0, 0x14, 0x14), 8: (0x9A, 0xA5, 0x136, 0xBC)}[z]
    return (x1 + x2), (y1 + y2) // 2


def clicks(seq, start=100, gap=70):
    """[(zone, buttons)] -> mouse moves: go there, press, release"""
    out = []
    for i, (z, b) in enumerate(seq):
        x, y = zone_xy(z)
        t = start + i * gap
        out += [(t, x, y, 0), (t + 5, x, y, b), (t + 12, x, y, 0)]
    return out


def test_main_mouse():
    """the same commands with the mouse; name typed after a click on zone 5"""
    cl = [(1, 1), (2, 1), (1, 2), (3, 1), (3, 2), (4, 1), (4, 2), (6, 1), (5, 1)]
    n0 = 100 + len(cl) * 70
    keys = [(n0 + 20 + 8 * i, k) for i, k in enumerate(text('Bob') + [ENTER])]
    after = [(8, 1), (7, 1), (8, 1), (7, 1), (7, 1)]    # accept 1, back, accept, back, cancel
    moves = clicks(cl) + clicks(after, start=n0 + 200)
    res, _ = crun('cg_main mouse', main_op, keys=keys, moves=moves, setup=seed_names)
    check(res[0][0] == 0, f'cg_main mouse: returned {res[0][0]}')


def test_main_blink():
    """long idle stretches with a fast clock: the walking sprite and the
    random(1000) blink, cursor over zones 1 and 2 (hidden while drawing)"""
    moves = [(50, *zone_xy(1), 0), (3000, *zone_xy(2), 0), (6000, 300, 20, 0)]
    res, _ = crun('cg_main blink', main_op, keys=[(9000, ESC)], moves=moves, clock_step=50)
    check(res[0][0] == 0, 'cg_main blink: returned 1')


def _in_cg_world(f):
    def run_test():
        old = uiworld.mount, uiworld._stub_files
        uiworld.mount, uiworld._stub_files = _CG_WORLD
        try:
            return f()
        finally:
            uiworld.mount, uiworld._stub_files = old
    run_test.__name__, run_test.__doc__ = f.__name__, f.__doc__
    return run_test


for _n, _f in list(globals().items()):
    if _n.startswith('test_') and callable(_f):
        globals()[_n] = _in_cg_world(_f)
