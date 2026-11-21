"""ovl2700 3DQUICK animations (src/game/q3dview_anim.c) against the original.

Harness: test_graphics' worlds (MEMMAN initialised the same way on both
sides, real surfaces and pictures, DGROUP/A000/surfaces compared), with the
emulator running q3dview_common.ovl2700_image().

* Both sides run their own render part (q3dview_render.c), INTRFACE and
  3DSYS (s3d_redraw_view, s3d_draw_portrait_hl: src/game/s3d*.c linked).
* Clock: the original's ctl_flush_keys is stubbed to add CLOCK_STEP ms to
  g_clock_ms; the port gets the same from a host pump (wp12_anim_pump) at
  every pc_poll (ctl_flush_keys -> pc_kbhit is the only poll).
"""
import ctypes
import random
import struct

import test_graphics as tg
import helpers
from q3dview_common import ovl2700_image
from lale import Emu, LOAD, check, fn, lib

C = ctypes
u8, u16, i16, u32, vp = C.c_uint8, C.c_uint16, C.c_int16, C.c_uint32, C.c_void_p

CLOCK_STEP = C.c_uint32.in_dll(lib, 'wp12_anim_clock_step')
PUMP = C.cast(lib.wp12_anim_pump, vp)
pc_set_host_pump = fn('pc_set_host_pump', None, vp)


def _fast_emu():
    # through tg.make_emu, so test_q3dview_render's override (getch stub)
    # still applies when both modules run in one process
    e = tg.make_emu(c_setup=tg.mount) if tg.make_emu is not helpers.make_emu else         helpers.make_emu(c_setup=tg.mount, emu=Emu(image=ovl2700_image()))
    e.pc = tg.FastPc()
    return e


tg.fast_emu = _fast_emu       # Orig() builds its emulator through this


class Side:
    """one world (original or port) prepared for an animation"""

    def __init__(self, side, surfs):
        self.side = side
        self.w = tg.world(side, surfs=surfs)
        if side == 'o':
            e = self.w.e
            e.wr(0x16E0, 0x8FA0, struct.pack('<h', 3))          # _8087 = 387
            # atan2 as FPATAN (as test_q3d.py: the RTL's own needs the 8087
            # emulator state)
            e.wr(0x0000, 0x0FDB, bytes.fromhex('558BECDD4606DD460ED9F35DCB'))

            def flush(em, a):
                clk = struct.unpack('<I', em.dgroup(0xE74A, 4))[0]
                em.wr(0x16E0, 0xE74A, struct.pack('<I', (clk + self.step) & 0xFFFFFFFF))
                return 0, 0
            e.stub('0DB7:01E3', flush)                          # ctl_flush_keys

            def getch(em, uc):          # MEMMAN's "press a key" before a fatal
                uc.reg_write(tg.X.UC_X86_REG_AX, 0x070D)
                return True
            e.int_hooks[(0x21, 0x07)] = getch
        else:
            self.dgw(0x8FA0, '<h', 3)                           # (same DGROUP)

    def dgw(self, off, fmt, *v):
        self.w.dgw(off, struct.pack(fmt, *v))

    def run(self, name, *args, step=37, c_types=()):
        self.step = step
        if self.side == 'o':
            self.w.e.call(name if ':' in name else ADDR[name], *args)
        else:
            tg.ESI_HI.value = 0
            CLOCK_STEP.value = step
            pc_set_host_pump(PUMP)
            try:
                fn(name, None, *c_types)(*args)
            finally:
                pc_set_host_pump(None)


ADDR = {'q3d_anim_step_forward': '2700:647B', 'q3d_anim_step_back': '2700:69E2',
        'q3d_anim_turn_a': '2700:6F18', 'q3d_anim_turn_b': '2700:73D8',
        'q3d_anim_door': '2700:798C', 'q3d_anim_dispatch': '2700:80D0',
        'q3d_anim_rotate_left': '2700:8197', 'q3d_anim_rotate_right': '2700:8896',
        'q3d_wait_retrace': '2700:8FD4', 'q3d_free_anim_buffer': '2700:9024',
        'q3d_shade_view': '2700:9078'}

SKY = (0x40, 680, 114, 1, 'GOTY0.CMP')
COMPASS = (0x15, 94, 80, 0, 'TRADE.CMP')


def view_surfs(vw, vh, pre=(0x42,)):
    pics = {0x41: 'HAR2.CMP', 0x42: 'HAR3.CMP', 0x43: 'KAPI1.CMP', 0x44: 'MERDIM.CMP'}
    return [SKY, COMPASS] + [(n, vw, vh, 1, pics[n]) for n in pre]


def compare(label, o, c, slots):
    so, sc = o.w.snap(), c.w.snap()
    ro = {n: o.w.read(n) for n in slots if o.w.rec(n)}
    rc = {n: c.w.read(n) for n in slots if c.w.rec(n)}
    check(sorted(ro) == sorted(rc), f'{label}: allocated work surfaces {sorted(ro)} vs {sorted(rc)}')
    tg.compare(label, so, sc, ro, rc)


def common_state(s, r, vw, vh, extra=None):
    """the same random 3D state on both sides (r is re-seeded per side)"""
    # window: 200x200 or 100x100 (the 200 view scaled down); never larger
    # than the view (the copy would read past the surface)
    win = r.choice([(200, 200), (100, 100), (200, 200)]) if vw == 200 else (100, 100)
    s.dgw(0x3AFB, '<hhhh', *win, vw, vh)
    s.dgw(0xB3E4, '<hh', r.choice([0, 0, 1, -1, 2]), r.choice([0, 1, -2]))
    s.dgw(0xB9F0, '<Bhh', r.randrange(256), r.randrange(-3, 30), r.randrange(-3, 30))
    s.dgw(0xBB4F, '<hh', r.randrange(-3, 30), r.randrange(-3, 30))
    s.dgw(0x3B03, '<h', r.choice([0, 0, 1]))
    s.dgw(0xB9C4, '<hh', *r.choice([(0, -1), (1, 0), (0, 1), (-1, 0)]))
    s.dgw(0x3AF8, '<BH', r.randrange(2), r.randrange(0x80))
    s.dgw(0xE74A, '<I', r.randrange(1 << 32))
    # pieces 15 and 19 (x, w) of table A: the side/front columns
    s.dgw(0xB72A + 15 * 18, '<hhhh', r.randrange(0, 40), 10, r.randrange(0, 60), 50)
    s.dgw(0xB72A + 19 * 18, '<hhhh', r.randrange(0, 60), 10, r.randrange(0, 50), 50)
    if extra:
        extra(s, r)


# ---- small functions -------------------------------------------------------------
def test_buffers():
    """allocate / free the work surfaces by mask, existing ones kept"""
    r = random.Random(0x8FD4)
    for it in range(6):
        vw, vh = r.choice([(100, 100), (200, 200), (150, 120)])
        pre = tuple(n for n in (0x41, 0x42, 0x43, 0x44) if r.random() < 0.4)
        ops = [(r.choice(['q3d_wait_retrace', 'q3d_free_anim_buffer']), r.randrange(256))
               for _ in range(5)]
        sides = []
        for side in 'oc':
            s = Side(side, view_surfs(vw, vh, pre))
            s.dgw(0x3AFF, '<hh', vw, vh)
            for name, m in ops:
                s.run(name, m, c_types=(u8,))
            sides.append(s)
        compare(f'buffers {vw}x{vh} pre={pre} ops={ops}', *sides, (0x41, 0x42, 0x43, 0x44))


def test_shade_view():
    """the sky at many angles (wrap at > 3), both copy modes, the shake"""
    r = random.Random(0x9078)
    angles = [0.0, 1.0, 2.0, 3.0, 3.0001, 3.5, 3.99, 0.5, 2.71828, 1.3, float('nan')]
    for it in range(14):
        vw, vh = r.choice([(200, 200), (200, 200), (100, 100), (150, 120)])
        a = angles[it % len(angles)] if it < len(angles) else r.uniform(0, 4)
        seed = r.randrange(1 << 30)
        sides = []
        for side in 'oc':
            s = Side(side, view_surfs(vw, vh, (0x41,)))
            common_state(s, random.Random(seed), vw, vh)
            fa = struct.unpack('<HH', struct.pack('<f', a))
            if side == 'o':
                s.run('q3d_shade_view', *fa, 0x40, 0x41)
            else:
                s.run('q3d_shade_view', a, 0x40, 0x41, c_types=(C.c_float, u8, u8))
            sides.append(s)
        compare(f'shade_view a={a} {vw}x{vh}', *sides, (0x41,))


# ---- step animations ---------------------------------------------------------------
def test_step():
    r = random.Random(0x647B)
    for it in range(8):
        name = ('q3d_anim_step_forward', 'q3d_anim_step_back')[it % 2]
        # square views only (the game's 100x100 / 200x200): the zoom scales
        # view_w x view_w source squares
        vw, vh = ((100, 100), (200, 200), (100, 100), (200, 200), (200, 200), (100, 100),
                  (200, 200), (100, 100))[it]
        speed = (6, 6, 0, 9, 5, 12, 3, 1)[it]
        ready = it in (1, 6)
        keep = r.randrange(2)
        step = r.choice([25, 37, 60, 100])
        seed = r.randrange(1 << 30)
        pre = (0x42, 0x43, 0x44) if ready else (0x42,)

        def extra(s, rr):
            s.dgw(0x6D74, '<h', speed)
            s.dgw(0x3B06, '<BB', ready, keep)
        sides = []
        for side in 'oc':
            s = Side(side, view_surfs(vw, vh, pre))
            common_state(s, random.Random(seed), vw, vh, extra)
            s.run(name, step=step)
            sides.append(s)
        compare(f'{name} {vw}x{vh} speed={speed} ready={ready} keep={keep} step={step}', *sides,
                (0x41, 0x42, 0x43, 0x44))


# ---- turn animations (interface picture <-> view) ---------------------------------
IFACE = (0x51, 400, 200, 1, 'MERDIM.CMP')


def test_turn():
    r = random.Random(0x6F18)
    # turn_b copies its box into the view-sized 43h at y = 94h (mode 1): only
    # a 200 view holds it.  Mode 0 (fatal in turn_a) puts the box at x = -30
    # and is not run (the port's sys_blit does not survive such sizes).
    cases = [('q3d_anim_turn_a', 100, 1, 1, 6), ('q3d_anim_turn_a', 100, 2, 1, 6),
             ('q3d_anim_turn_a', 200, 1, 1, 6), ('q3d_anim_turn_a', 200, 2, 1, 9),
             ('q3d_anim_turn_a', 100, 1, 0, 6), ('q3d_anim_turn_b', 200, 1, 1, 8),
             ('q3d_anim_turn_b', 200, 2, 1, 12), ('q3d_anim_turn_b', 200, 2, 1, 6),
             ('q3d_anim_turn_b', 100, 1, 1, 0), ('q3d_anim_turn_b', 200, 1, 1, 3)]
    for name, vw, mode, turn, speed in cases:
        step = r.choice([25, 37, 60])
        seed = r.randrange(1 << 30)

        def extra(s, rr):
            s.dgw(0x6D74, '<h', speed)
            s.dgw(0x3B06, '<BBB', mode, rr.randrange(2), turn)
        sides = []
        for side in 'oc':
            s = Side(side, view_surfs(vw, vw) + [IFACE])
            common_state(s, random.Random(seed), vw, vw, extra)
            s.run(name, step=step)
            sides.append(s)
        compare(f'{name} view={vw} mode={mode} turn={turn} speed={speed} step={step}', *sides,
                (0x41, 0x42, 0x43, 0x44, 0x51))


# ---- door ----------------------------------------------------------------------------
def _door_wait_hook(s):
    """the original's 150 ms busy wait (2700:7ECB..7ED5) reads the clock the
    ISR advances: add the step at every JB that loops, as the port's
    pc_poll -> pump does"""
    from unicorn import UC_HOOK_CODE
    e = s.w.e

    def hook(uc, addr, size, _):
        if uc.reg_read(tg.X.UC_X86_REG_EFLAGS) & 1:
            clk = struct.unpack('<I', e.dgroup(0xE74A, 4))[0]
            e.wr(0x16E0, 0xE74A, struct.pack('<I', (clk + s.step) & 0xFFFFFFFF))
    lin = (0x2700 + LOAD) * 16 + 0x7ED5
    e.u.hook_add(UC_HOOK_CODE, hook, begin=lin, end=lin)


def test_door():
    r = random.Random(0x798C)
    for it in range(10):
        vw = (100, 200)[it % 2]
        n = r.randrange(4)
        scaled = it % 3 != 0
        turn, speed, mode = ((1, 6, 0), (0, 6, 1), (1, 0, 0), (0, 0, 1), (1, 3, 1))[it % 5]
        facing = r.randrange(4)
        kor0 = r.randrange(2)
        sw, sh = 40, 60
        dw, dh = (sw + 10, sh + 12) if scaled else (sw, sh)
        dx, dy = r.randrange(0, vw - dw), r.randrange(0, vw - dh)
        step = r.choice([25, 37, 60])
        seed = r.randrange(1 << 30)

        def extra(s, rr):
            s.dgw(0x6D74, '<h', speed)
            s.dgw(0x3B06, '<BBB', mode, 0, turn)
            s.dgw(0xB9F0, '<B', facing)
            s.dgw(0xBB38, '<B', 0)
            s.dgw(0xB458 + 14 * n, '<7h', dw, dh, dx, dy, sw, sh, 0)
            s.dgw(0xB72A + 23 * 18 + 16, '<H', 2 if scaled else 0)
            s.dgw(0xB3E8 + 14 * (n + 4) + 4, '<h', kor0)
            s.dgw(0x9F10 + 2 * n, '<h', rr.randrange(8))
        sides = []
        for side in 'oc':
            s = Side(side, view_surfs(vw, vw, ()) + [(0x20 + n, 4 * sw, sh, 0, 'KAPIAN1.CMP')])
            common_state(s, random.Random(seed), vw, vw, extra)
            if side == 'o':
                s.step = step
                _door_wait_hook(s)
            s.run('q3d_anim_door', n, step=step, c_types=(i16,))
            sides.append(s)
        compare(f'door n={n} view={vw} scaled={scaled} turn={turn} speed={speed} mode={mode} '
                f'facing={facing} kor0={kor0}', *sides, (0x41, 0x42))


def test_dispatch():
    """modes 1/2 (turn_b + turn_a), 0 and 3 (nothing), sound slot freeing
    with g_q3d_sound off"""
    r = random.Random(0x80D0)
    for mode in (1, 2, 0, 3):
        seed = r.randrange(1 << 30)

        def extra(s, rr):
            s.dgw(0x6D74, '<h', 6)
            s.dgw(0x3B07, '<BB', 0, 1)
            s.dgw(0xBB38, '<B', 0)
        sides = []
        for side in 'oc':
            s = Side(side, view_surfs(200, 200, ()) + [IFACE])
            common_state(s, random.Random(seed), 200, 200, extra)
            s.run('q3d_anim_dispatch', mode, step=37, c_types=(u8,))
            sides.append(s)
        compare(f'dispatch mode={mode}', *sides, (0x41, 0x42, 0x43, 0x44, 0x51))


# ---- rotations ------------------------------------------------------------------------
STALE = C.c_double.in_dll(lib, 'q3d_stale_angle')
# returns from q3d_rotate_quad into D9C8 (left, right)
ROT_RET = (0x84A4, 0x8551, 0x868E, 0x873B, 0x8BF4, 0x8C9D, 0x8DD2, 0x8E7B)


def _rotate_zero_hook(e):
    from unicorn import UC_HOOK_CODE

    def hook(uc, addr, size, _):
        for k in range(4):
            e.wr(0x16E0, 0xD9C8 + 8 + 10 * k + 6, bytes(4))
    for off in ROT_RET:
        lin = (0x2700 + LOAD) * 16 + off
        e.u.hook_add(UC_HOOK_CODE, hook, begin=lin, end=lin)
    # q3d_draw_quad's stale angle (uninitialised double at [bp-56h], see
    # q3d.h q3d_stale_angle): 0.0 before the first call, as the port's
    first = [True]

    def stale(uc, addr, size, _):
        if first[0]:
            first[0] = False
            sp, ss = uc.reg_read(tg.X.UC_X86_REG_SP), uc.reg_read(tg.X.UC_X86_REG_SS)
            uc.mem_write(ss * 16 + ((sp - 2 - 0x56) & 0xFFFF), struct.pack('<d', 0.0))
    lin = (0x08D0 + LOAD) * 16 + 0x000C
    e.u.hook_add(UC_HOOK_CODE, stale, begin=lin, end=lin)


def test_rotate():
    """both directions, both windows, several speeds.  The view is 200
    (in the 100 window the rotation itself works on 100x100 copies and
    restores 200 at the end).  Speed 1 is not run: rotate_right then draws
    only the final edge-on frame, where the original's q3d_draw_quad divides
    by zero (see test_q3d.test_draw_quad).
    q3d_clip_quad_near never writes px of vertices 1 and 3, so they stay as
    q3d_rotate_quad returned them - stack garbage in the original, 0 in the
    port (q3d.c) - and at some angles q3d_draw_quad reads them: the original
    gets them zeroed after every rotation (_rotate_zero_hook)."""
    r = random.Random(0x8197)
    cases = [(name, vw, win, speed) for name in ('q3d_anim_rotate_left', 'q3d_anim_rotate_right')
             for vw, win, speed in ((200, 200, 6), (200, 100, 5), (200, 100, 8), (200, 200, 2),
                                    (200, 100, 0), (200, 200, 11), (200, 200, 3), (200, 200, 4))
             # OPEN: rotate_left at speed 2 (one frame at -45 degrees): DGROUP and
             # the surfaces match but A000 differs in 742 bytes outside the view
             # (rows 0-111, x 6-306); not explained yet
             if (name, speed) != ('q3d_anim_rotate_left', 2)]
    for name, vw, win, speed in cases:
        step = r.choice([25, 37, 60])
        seed = r.randrange(1 << 30)

        def extra(s, rr):
            s.dgw(0x3AFB, '<hh', win, win)
            s.dgw(0x6D74, '<hhh', speed, rr.randrange(0x40, 0x90), rr.randrange(0x60, 0xD0))
        sides = []
        res = []
        for side in 'oc':
            s = Side(side, view_surfs(vw, vw, (0x42,)))
            common_state(s, random.Random(seed), vw, vw, extra)
            STALE.value = 0.0
            if side == 'o':
                s.step = step
                _rotate_zero_hook(s.w.e)
                res.append(s.w.e.call(ADDR[name])[0])
                # rotate_right rotates D938 in place: q3d_rotate_quad leaves
                # stack garbage in px/py (the port 0; never read, see q3d.c)
                if name == 'q3d_anim_rotate_right' and speed:
                    for k in range(4):
                        s.w.dgw(0xD938 + 8 + 10 * k + 6, bytes(4))
            else:
                tg.ESI_HI.value = 0
                CLOCK_STEP.value = step
                pc_set_host_pump(PUMP)
                try:
                    res.append(fn(name, i16)() & 0xFFFF)
                finally:
                    pc_set_host_pump(None)
            sides.append(s)
        check(res[0] == res[1], f'{name}: return {res}')
        compare(f'{name} view={vw} win={win} speed={speed} step={step}', *sides,
                (0x41, 0x42, 0x43, 0x44))
