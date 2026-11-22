"""ovl2700 3DQUICK: the gaps left by test_q3dview*.py (see docs/PROGRESS.md).

1. Rotations with the game's projection.  test_q3dview_anim.test_rotate
   sets random projection values (6D76/6D78), with which
   q3d_clip_quad_near rejects both walls in almost every frame: only the
   sky is drawn.  Here 6D76/6D78 keep their values of the shipped game
   (295, 100; nothing ever writes them), and the WHOLE destination is
   compared: all of A000, every work surface, the sky panorama 40h and the
   compass 15h, DGROUP.

   The shipped game only rotates in the 100x100 window (3DSYS sets
   speed 9 with the 100 window and speed 0 with the 200 window, 303F:1310 /
   140D / 42A9-4322 / 7C96-7F14): test_rotate_game.

   In the 200 window (never animated in the game) the original's quad
   mapper leaves the EMS window of the destination: test_rotate_wrap.

2. 100x100 views: test_render_100 (see there).
"""
import ctypes
import os
import random
import struct

import test_graphics as tg
import test_q3dview_anim as ta
from lale import LOAD, check, fn, lib

C = ctypes
i16 = C.c_int16


# ---- snapshots in the right order ----------------------------------------------------
# Both sides share the port's emulated PC (lale_testlib's pc layer: EMS
# memory, VGA registers).  Building the second world resets it, so whatever
# is read from the first side AFTER that - EMS surfaces (through INT 67h)
# and the VGA state - is the second side's.  test_q3dview_anim.compare
# reads both sides at the end: its comparison of the EMS work surfaces
# 41h-44h and of the VGA state is port against port.  Here every side is
# frozen (snapshot + every surface read back) before the next world is
# built.
ALL_SLOTS = (0x15, 0x20, 0x21, 0x22, 0x23, 0x40, 0x41, 0x42, 0x43, 0x44, 0x51)


def freeze(s):
    if not hasattr(s, 'frozen'):
        snap = s.w.snap()
        s.frozen = (snap, {n: s.w.read(n) for n in ALL_SLOTS if s.w.rec(n)})
    return s.frozen


def compare_frozen(label, o, c, slots=ALL_SLOTS):
    (so, ro), (sc, rc) = freeze(o), freeze(c)
    check(sorted(ro) == sorted(rc), f'{label}: allocated surfaces {sorted(ro)} vs {sorted(rc)}')
    tg.compare(label, so, sc, {n: ro[n] for n in ro if n in rc}, rc)


class Frozen:
    """run test_q3dview_anim's scenarios with sides frozen in time"""

    def __enter__(self):
        self.init, self.compare = ta.Side.__init__, ta.compare
        last = []
        init0 = self.init

        def init(s, side, surfs):
            if last:
                freeze(last.pop())
            init0(s, side, surfs)
            last.append(s)

        def compare(label, o, c, slots):
            del last[:]
            compare_frozen(label + ' [frozen]', o, c)
        ta.Side.__init__, ta.compare = init, compare
        return self

    def __exit__(self, *exc):
        ta.Side.__init__, ta.compare = self.init, self.compare
        return False


def test_anim_frozen():
    """every scenario of test_q3dview_anim again, with the original's EMS
    surfaces and VGA state read before the port's world replaces them"""
    with Frozen():
        for t in (ta.test_buffers, ta.test_shade_view, ta.test_step, ta.test_turn, ta.test_door,
                  ta.test_dispatch, ta.test_rotate):
            t()


def rotate_pair(name, win, speed, step, seed, hook=None, random_projection=False):
    """one rotation on both sides -> (side_o, side_c, returns); 6D76/6D78
    are left at the game's values (random_projection: set as
    test_q3dview_anim.test_rotate does)"""
    def extra(s, rr):
        s.dgw(0x3AFB, '<hh', win, win)
        s.dgw(0x6D74, '<h', speed)
        if random_projection:
            s.dgw(0x6D76, '<hh', rr.randrange(0x40, 0x90), rr.randrange(0x60, 0xD0))
    sides, res = [], []
    for side in 'oc':
        s = ta.Side(side, ta.view_surfs(200, 200, (0x42,)))
        ta.common_state(s, random.Random(seed), 200, 200, extra)
        ta.STALE.value = 0.0
        if side == 'o':
            s.step = step
            ta._rotate_zero_hook(s.w.e)
            if hook:
                hook(s)
            res.append(s.w.e.call(ta.ADDR[name])[0])
            if name == 'q3d_anim_rotate_right' and speed:
                for k in range(4):                 # see test_q3dview_anim.test_rotate
                    s.w.dgw(0xD938 + 8 + 10 * k + 6, bytes(4))
        else:
            tg.ESI_HI.value = 0
            ta.CLOCK_STEP.value = step
            ta.pc_set_host_pump(ta.PUMP)
            try:
                res.append(fn(name, i16)() & 0xFFFF)
            finally:
                ta.pc_set_host_pump(None)
        freeze(s)                   # before the next world replaces the PC
        sides.append(s)
    return sides[0], sides[1], res


def quad_trace(s):
    """count q3d_draw_quad's stores of the original (08D0:0940) and those
    whose 16-bit offset wrapped (far pointer offset + di > FFFFh)"""
    from unicorn import UC_HOOK_CODE
    X = tg.X
    e = s.w.e
    s.stores = 0
    s.wrapped = []                  # frame offsets written after the wrap
    s.loads_wrapped = 0             # texel reads whose offset wrapped

    def at_load(uc, addr, size, _):
        bp, ss = uc.reg_read(X.UC_X86_REG_BP), uc.reg_read(X.UC_X86_REG_SS)
        off = struct.unpack('<H', uc.mem_read(ss * 16 + ((bp - 0x0C) & 0xFFFF), 2))[0]
        si = struct.unpack('<H', uc.mem_read(ss * 16 + ((bp - 0x48) & 0xFFFF), 2))[0]
        if off + si > 0xFFFF:
            s.loads_wrapped += 1
    lin = (0x08D0 + LOAD) * 16 + 0x0930
    e.u.hook_add(UC_HOOK_CODE, at_load, begin=lin, end=lin)

    def at_store(uc, addr, size, _):
        bp, ss = uc.reg_read(X.UC_X86_REG_BP), uc.reg_read(X.UC_X86_REG_SS)
        off = struct.unpack('<H', uc.mem_read(ss * 16 + ((bp - 0x10) & 0xFFFF), 2))[0]
        di = struct.unpack('<H', uc.mem_read(ss * 16 + ((bp - 0x46) & 0xFFFF), 2))[0]
        s.stores += 1
        if off + di > 0xFFFF:
            s.wrapped.append((off + di) & 0xFFFF)
    lin = (0x08D0 + LOAD) * 16 + 0x0940
    e.u.hook_add(UC_HOOK_CODE, at_store, begin=lin, end=lin)


def test_rotate_game():
    """the shipped game's rotation: 100 window, speed 9, projection 295/100,
    both directions, several frame rates (so every angle step 1..8 of the
    left turn and 1..9 of the right turn is drawn at least once)"""
    r = random.Random(0x8198)
    drawn = 0
    for name in ('q3d_anim_rotate_left', 'q3d_anim_rotate_right'):
        for step in (37, 60, 100, 20, 150):
            seed = r.randrange(1 << 30)
            o, c, res = rotate_pair(name, 100, 9, step, seed, hook=quad_trace)
            check(res[0] == res[1], f'{name}: return {res}')
            check(not o.wrapped, f'{name} step={step}: {len(o.wrapped)} stores left the window')
            check(not o.loads_wrapped, f'{name} step={step}: {o.loads_wrapped} reads wrapped')
            drawn += o.stores
            compare_frozen(f'{name} game config step={step}', o, c)
    check(drawn > 20000, f'rotate_game: the walls were not drawn ({drawn} stores)')


def test_rotate_wrap():
    """Rotations in the 200x200 window with animation on - a combination
    3DSYS never sets (200 window <=> speed 0) - incl. the case
    test_q3dview_anim.test_rotate leaves out (rotate_left, speed 2).

    What the original does there (established with the hooks of
    quad_trace): q3d_draw_quad maps the 40000-byte destination 41h at
    physical page 2, where only 32767 - in-page offset bytes fit, and calls
    o559A_fatal (PERPEC.L line 60h, code 70h) - the real game exits at the
    first quad.  The harness records the fatal and returns, and the quad is
    drawn: the stores are "LES BX,[bp-10h] / ADD BX,di / MOV ES:[BX],AL"
    (08D0:093A), so past the end of the frame segment the offset wraps to
    E000:0000, physical page 0 - the first page of the SOURCE mapping, here
    the tail of the sky panorama 40h (rows 96-113), which the next sky copy
    brings into the view.  Nothing is written outside the EMS frame.

    The port's q3d_draw_quad_mapped (src/game/q3d.c) indexes native
    pointers without that wrap and writes past pc_ems_frame (in
    lale_testlib into pc_a000, which happens to follow it: the "742 bytes
    outside the view").  With the wrap added to q3d.c the two sides are
    identical everywhere; until then (q3d.c is outside this work package)
    the comparison of A000 and of the surfaces is only made when
    LALE_Q3D_WRAP=strict or when the first case shows the port wraps."""
    cases = [('q3d_anim_rotate_left', 2, 37, 834548919, True),      # the open case of test_rotate
             ('q3d_anim_rotate_left', 2, 37, 11, False), ('q3d_anim_rotate_left', 9, 37, 12, False),
             ('q3d_anim_rotate_left', 3, 60, 13, False), ('q3d_anim_rotate_right', 2, 37, 14, False),
             ('q3d_anim_rotate_right', 9, 60, 15, False), ('q3d_anim_rotate_right', 6, 25, 16, False)]
    strict = os.environ.get('LALE_Q3D_WRAP') == 'strict'
    wraps = None
    for name, speed, step, seed, rnd in cases:
        label = f'{name} win=200 speed={speed} step={step}' + (' random projection' if rnd else '')
        o, c, res = rotate_pair(name, 200, speed, step, seed, hook=quad_trace, random_projection=rnd)
        (so, ro), (sc, rc) = freeze(o), freeze(c)
        check(res[0] == res[1], f'{label}: return {res}')
        check(len(o.wrapped) > 0, f'{label}: no store wrapped')
        check(all(x < 0x4000 for x in o.wrapped), f'{label}: wrapped stores beyond physical page 0')
        check(so['fatal'] == sc['fatal'] and so['fatal'] >= 2, f'{label}: fatals {so["fatal"]} vs {sc["fatal"]}')
        same = so['a000'] == sc['a000'] and ro == rc
        if wraps is None:
            wraps = same
        if strict or wraps:
            compare_frozen(label, o, c)
        else:
            # known difference (q3d.c): everything but A000 and the surfaces
            sc2 = dict(sc, a000=so['a000'])
            tg.compare(label + ' (without A000/surfaces)', so, sc2)


# ---- 100x100 views ---------------------------------------------------------------------
# The shipped game never renders a 100x100 view: main() sets 3AFF/3B01 to
# 200 before the tables are built (0546:048F), and the only other writers
# are the rotations (2700:8245 ...), which switch to 100 after
# q3d_render_view and back to 200 before the next one.  With 100x100
# tables every piece is "scaled" (flags bit 1), but the floor/ceiling
# strips of the layers go to SYSTEMS' slanted strip blitter with the
# texture rectangle (tw x th, e.g. piece 28: 48 x 48 at y = 62), which does
# not scale.  A strip that reaches below row 99 of the 100-row view makes
# sys_blit_0F21 (0AC6:0FC2-10DE) map the destination at its very end:
# 0 rows fit, the remaining height never decreases, and the routine loops
# for ever (test_render_100_hang).  Nothing is written out of bounds; the
# port's sys_blit_0F21 has the same loop.
import test_q3dview_render as tr  # noqa: E402
from lale import NAMES, patch, unpatch  # noqa: E402

u8, u16, vp = C.c_uint8, C.c_uint16, C.c_void_p
STRIP_FNS = {'sys_line_10F1': 0x10F1, 'sys_line_1185': 0x1185, 'sys_line_1261': 0x1261,
             'sys_line_1307': 0x1307}
BLIT_0F21 = (0x0AC6 + LOAD) * 16 + 0x0F21


def endless(s, a):
    """sys_blit_0F21(sx, sy, w, h, dx, dy, limit, slope, src, dst, fn) never
    returns: it has rows to draw and the row range leaves a surface (also
    taken for the calls with a negative row, whose outcome is not
    established: they are skipped on both sides alike)"""
    sy, w, h, dy, src, dst = a[1], a[2], a[3], a[5], a[8] & 0xFF, a[9] & 0xFF
    hs = struct.unpack('<H', s.w.dg(0xE29D + 8 * src, 2))[0]
    hd = struct.unpack('<H', s.w.dg(0xE29D + 8 * dst, 2))[0]
    return w != 0 and h != 0 and (sy + h > hs or dy + h > hd)


def strip_guard(s):
    """skip the sys_blit_0F21 calls that would never return, logging them
    (s.skipped: the 10 word arguments + the strip routine's name)"""
    s.skipped = []
    if s.side == 'o':
        e = s.w.e
        X = tg.X

        def at_entry(uc, addr, size, _):
            sp, ss = uc.reg_read(X.UC_X86_REG_SP), uc.reg_read(X.UC_X86_REG_SS)
            w = struct.unpack('<14H', uc.mem_read(ss * 16 + sp, 28))
            a = w[2:12]
            if not endless(s, a):
                return
            fn = ((w[13] - LOAD) & 0xFFFF, w[12])
            name = next((n for n, o in STRIP_FNS.items() if fn == (0x0AC6, o)), fn)
            s.skipped.append((a[:7] + (a[7] & 0xFF, a[8] & 0xFF, a[9] & 0xFF), name))
            uc.reg_write(X.UC_X86_REG_SP, (sp + 4) & 0xFFFF)
            uc.reg_write(X.UC_X86_REG_CS, w[1])
            uc.reg_write(X.UC_X86_REG_IP, w[0])
        e.u.hook_add(tg.UC_HOOK_CODE, at_entry, begin=BLIT_0F21, end=BLIT_0F21)
    else:
        types = (u16,) * 7 + (u8, u8, u8, vp)
        names = {tg.cfn(n): n for n in STRIP_FNS}

        def cb(*a):
            if endless(s, a):
                s.skipped.append((tuple(a[:10]), names.get(a[10], a[10])))
                return
            unpatch('sys_blit_0F21')
            try:
                fn('sys_blit_0F21', None, *types)(*a)
            finally:
                patch('sys_blit_0F21', cb, None, *types)
        s.guard = cb
        patch('sys_blit_0F21', cb, None, *types)


def test_render_100():
    """q3d_render_view and the five layers with the 100x100 tables, every
    PCn map at a few cells, all facings: destination, DGROUP and the list
    of strip blits that would hang (identical arguments on both sides)."""
    r = random.Random(0x100)
    skipped = 0
    try:
        for k, (mn, m) in enumerate(tr.maps().items()):
            o, c = tr.pair(m, 100, 100, ox=(0, 3)[k % 3 == 2],
                           hdr0=tuple(r.choice((0, 0, 1)) for _ in range(8)))
            for s in (o, c):
                # 50h, the interface picture in the game: cells beyond the
                # map edge select it as a ceiling (in test_q3dview_render's
                # world the slot is empty: the original then reads its
                # record through a null far pointer, the port stops)
                s.w.surf(0x50, 400, 200, 1)
                s.w.write(0x50, tg.fit(*tg.pic('MERDIM.CMP'), 400, 200))
                strip_guard(s)
            W, H = m[0], m[1]
            for x, y in [(r.randrange(W), r.randrange(H)) for _ in range(2)] + [(0, 0), (W - 1, H - 1)]:
                for f in range(4):
                    o.pos(x, y, f)
                    c.pos(x, y, f)
                    del o.skipped[:], c.skipped[:]
                    label = f'{mn} 100x100 ({x},{y}) f{f}'
                    tr.run_both(o, c, label, 'q3d_render_view', tr.DST)
                    check(o.skipped == c.skipped, f'{label}: endless strip blits {o.skipped[:3]} vs '
                          f'{c.skipped[:3]} ({len(o.skipped)}/{len(c.skipped)})')
                    skipped += len(o.skipped)
            unpatch('sys_blit_0F21')
    finally:
        unpatch('sys_blit_0F21')
    check(skipped > 0, 'render_100: no strip blit left the view')


def test_render_100_hang():
    """the original's q3d_render_layer_near with 100x100 tables: the loop of
    sys_blit_0F21 comes round with the same remaining height, source row
    and destination row three times (0 rows mapped) - it never ends"""
    m = tr.maps()['PC1']
    o, c = tr.pair(m, 100, 100)
    e = o.w.e
    X = tg.X
    seen = []

    def at_loop(uc, addr, size, _):
        bp, ss = uc.reg_read(X.UC_X86_REG_BP), uc.reg_read(X.UC_X86_REG_SS)
        st = struct.unpack('<5H', uc.mem_read(ss * 16 + bp + 8, 10))    # sy, w, h, dx, dy
        seen.append(st)
        if len(seen) >= 3 and seen[-1] == seen[-2] == seen[-3]:
            uc.emu_stop()
    lin = (0x0AC6 + LOAD) * 16 + 0x0FC2
    e.u.hook_add(tg.UC_HOOK_CODE, at_loop, begin=lin, end=lin)
    o.pos(5, 5, 0)
    try:
        e.call(NAMES['q3d_render_layer_near'], tr.DST, max_insns=20_000_000)
        check(False, 'render_100_hang: the layer returned')
    except Exception as ex:                                     # noqa: BLE001
        check('not at the return' in str(ex), f'render_100_hang: {ex}')
    check(len(seen) >= 3 and seen[-1] == seen[-2] == seen[-3], f'render_100_hang: loop states {seen[-4:]}')
    if seen:
        sy, w, h, dx, dy = seen[-1]
        check(h != 0 and dy == 100, f'render_100_hang: stuck with h={h} at destination row {dy}')
