"""3DSYS exploration loop (303F:8524 s3d_explore_loop, src/game/s3d_loop.c)
against the original, in the world of test_s3d_menu.py.

The loop never returns.  The window record's idle callback (964C), called
once per pass, points at a scratch far address: the port registers
tests/stubs/testhook.c's th_tick there, which calls the Python hook and
leaves the loop (th_run, longjmp) at its n-th call; the emulator stubs the same address
and stops there.  Both sides are compared at that moment.

Scripts: the idle callback applies the next scripted move every few passes; s3d_advance_clock (stand-in) adds the minutes to 3D64;
FRPOS/ISOSYS/DEMOLAR stubs answer per scenario (iso_battle sets the
result word 632E).  Party = orig/GRP.DAT; map = extracted/FRPOS/PCn, its
whole file also in the EMS block of E33F (the encounter table).
"""
import ctypes as C
import os
import struct

from lale import ROOT, check, lib
import uiworld
from s3d_common import s16
import test_s3d_menu as tm
from test_s3d_menu import Hook, mrun, w16, r16

GRP = open(os.path.join(ROOT, 'orig', 'GRP.DAT'), 'rb').read()
IDLE_FLAT = (0x5F00, 0x0010)          # scratch far address of the idle callback
MAP_PTR = C.c_void_p.in_dll(lib, 's3d_map')
TICK_T = C.CFUNCTYPE(None, C.c_uint32)
TICK_HOOK = C.c_void_p.in_dll(lib, 'th_tick_hook')
TICK_LEFT = C.c_int32.in_dll(lib, 'th_tick_left')
_keep = []
# 3DSYS routines replaced by the hook on both sides (test_s3d_menu.STAND):
# the key handler (the moves come from the idle callback), the clock and
# the portrait blink (the original's goes through test_intrface's 'shape'
# stub of its thunk 1670:0084)
LOOP_STAND = {'s3d_handle_key', 's3d_advance_clock', 's3d_draw_portrait_hl'}


def setup_party(s, mods=None):
    for i in range(1, 5):
        rec = 0x9C26 + 0x94 * i
        s.dgw(rec, GRP[74 * (i - 1):74 * (i - 1) + 37])
        s.dgw(rec + 0x6F, GRP[74 * (i - 1) + 37:74 * i])
        st = bytearray(GRP[296 + 92 * (i - 1):296 + 92 * i])
        for (p, off), v in (mods or {}).items():
            if p == i:
                st[off:off + 2] = struct.pack('<h', v)
        s.dgw(0x99A6 + 0x5C * i, bytes(st))


def setup_map(s, name, x, y, facing):
    d = open(os.path.join(ROOT, 'extracted', 'FRPOS', name), 'rb').read()
    w, h, cells = d[20], d[21], d[22:]
    data = struct.pack('<BB', h, w)
    pos = struct.pack('<B', facing) + struct.pack('<hh', y, x)
    if s.side == 'o':
        seg = s.e.alloc(len(cells) + 16)
        s.e.wr(seg, 0, cells, runtime=True)
        s.dgw(0xB9EA, data + struct.pack('<HH', 0, seg) + pos)
        ax, dx = s.c('mm_alloc', len(d), 0, 1)
        s.dgw(0xE33F, struct.pack('<HH', ax, dx))
    else:
        buf = C.create_string_buffer(cells, len(cells) + 16)
        _keep.append(buf)
        MAP_PTR.value = C.addressof(buf)
        s.dgw(0xB9EA, data + b'\0' * 4 + pos)
        p = uiworld.c_mm_alloc(len(d), 1)
        s.dgw(0xE33F, struct.pack('<HH', p - uiworld.DGA, 0x1F04))
    rec = struct.unpack('<H', s.dg(0xE33F, 2))[0]
    s._io(rec, len(d), d)
    s.dgw(0xBB4F, struct.pack('<hh', y, x))         # prev = current
    s.dgw(0xB9C4, struct.pack('<hh', *((-1, 0), (0, 1), (1, 0), (0, -1))[facing & 3]))
    return w, h


class LoopHook(Hook):
    """moves: one scripted step (dx, dy) every `period` passes, applied in
    the idle callback (the loop sees the position differ from the previous
    one and runs its move branch; s3d_handle_key is a no-op stand-in)"""
    def __init__(self, moves=(), ret=None, battle=1, event_once=False, period=3):
        super().__init__(ret=ret)
        self.moves = list(moves)
        self.battle = battle
        self.event_once = event_once
        self.seen_cells = set()
        self.period = period
        self.passes = 0

    def __call__(self, name, args, s):
        if name == 'loop_idle':
            self.passes += 1
            if self.moves and self.passes % self.period == 0:
                dx, dy = self.moves.pop(0)
                w16(s, 0xB9F3, r16(s, 0xB9F3) + dx)
                w16(s, 0xB9F1, r16(s, 0xB9F1) + dy)
        elif name == 's3d_advance_clock':
            w16(s, 0x3D64, r16(s, 0x3D64) + args[0])
        elif name == 'iso_battle':
            w16(s, 0x632E, self.battle)
        elif name == 'fr_run_event' and self.event_once and args[1] == 2:
            r = args[0] not in self.seen_cells
            self.seen_cells.add(args[0])
            super().__call__(name, args, s)
            return int(r)
        return super().__call__(name, args, s)


def loop_op(n):
    def op(s):
        if s.side == 'o':
            e = s.e
            cnt = [0]

            def idle(e, a):
                cnt[0] += 1
                s.hook('loop_idle', [], s)
                if cnt[0] >= n:
                    e.u.emu_stop()
                return 0, 0
            e.stub(f'{IDLE_FLAT[0]:04X}:{IDLE_FLAT[1]:04X}', idle)
            try:
                s.c('s3d_explore_loop')
            except Exception as x:                      # noqa: BLE001
                if cnt[0] < n:
                    raise
                del x
        else:
            def idle(arg):
                s.hook('loop_idle', [], s)
            cb = TICK_T(idle)
            lib.ui_register_far_fn(C.c_uint16(IDLE_FLAT[0]), C.c_uint16(IDLE_FLAT[1]), lib.th_tick)
            TICK_HOOK.value = C.cast(cb, C.c_void_p).value
            TICK_LEFT.value = n
            try:
                lib.th_run(C.cast(lib.s3d_explore_loop, C.c_void_p), 0, 0, 0, 0)
            finally:
                TICK_HOOK.value = None
                TICK_LEFT.value = 0
        s.dgw(0xB9EC, bytes(4))             # the map's far pointer: native in the port
        return (s.dg(0xB9F1, 4), s.dg(0xBB4F, 4), s.dg(0x3D64, 10), s.dg(0x5BD8, 0x30),
                s.dg(0xBA84, 4))
    return op


def lrun(label, n, hook_f, pre, **kw):
    def pre2(s):
        s.dgw(0x964C, struct.pack('<HH', IDLE_FLAT[1], IDLE_FLAT[0] + 0x824))
        pre(s)

    # the idle stub needs the side's hook: remember it on the side object
    orig_install = tm.install_standins

    def install(s, hook):
        s.hook = hook
        orig_install(s, hook)
    tm.install_standins = install
    tm.STAND.update(LOOP_STAND)
    try:
        return mrun(label, loop_op(n), hook_f, pre=pre2, **kw)
    finally:
        tm.install_standins = orig_install
        tm.STAND.clear()


def base_pre(minute=9, rate=0, flags=0x20, mods=None, district=0, mapname='PC1', pos=(5, 5, 1)):
    def pre(s):
        setup_party(s, mods)
        setup_map(s, mapname, *pos)
        w16(s, 0xBB6B, district)
        w16(s, 0x3D64, minute)
        w16(s, 0xBA86, rate)
        w16(s, 0x3D5E, flags)
        s.dgw(0xBB34, bytes([1, 5, 0, 0, 0, 1, 0]))
    return pre


def test_loop_idle():
    """no input: idle passes and portrait blinks only"""
    _, log = lrun('loop idle', 120, LoopHook, base_pre(), clock_step=20)
    check(sum(x[0] == 'loop_idle' for x in log) == 120, f'loop idle: {log[:20]}')


def test_loop_moves():
    """moves (clock, status line, events, hunger at minute 10), no encounters"""
    mods = {(2, 0x22): 999, (2, 0x20): 0, (3, 0x22): 999, (3, 0x20): 40, (4, 0x22): 600}
    moves = [(1, 0), (0, 1), (0, 0), (-1, 0), (0, -1), (1, 0)] * 3
    lrun('loop moves', 80, lambda: LoopHook(moves=moves, event_once=True),
         base_pre(mods=mods), clock_step=10)


def test_loop_encounter():
    """moves with an encounter each step (rate 400): day (kind 1) / night
    (kind 0) tables, battle won (loot: the monster table entries of every
    type made non-zero) / lost (game over)"""
    base = base_pre(rate=400)

    def loot(flags):
        def pre(s):
            base(s)
            w16(s, 0x3D5E, flags)
            for t in range(1, 40):
                s.dgw(0x2894 + 6 * t + 1, bytes([t, 2 * t, 3, 0x31]))
        return pre
    # after the battle the district is reloaded and the view re-entered:
    # stood in (s3d_district's own tests cover them)
    for flags, battle in ((0x20, 1), (0x28, 1), (0x20, 2)):
        _, log = lrun(f'loop encounter flags={flags:X} battle={battle}', 400,
                      lambda battle=battle: LoopHook(moves=[(1, 0), (0, 1), (-1, 0)], battle=battle),
                      loot(flags), clock_step=10, stand={'s3d_load_district', 's3d_enter_view'})
        names = [x[0] for x in log]
        check(names.count('iso_battle') >= 2, f'encounter {flags:X}/{battle}: {names[:40]}')
        want = 'fr_item_exchange' if battle == 1 else 'demo_game_over'
        check(want in names, f'encounter {flags:X}/{battle}: no {want}')


def test_loop_end():
    """the party dead (game over, checked after input / a move);
    fr_flag_test 9Ch set (the ending, also no encounters on this district)"""
    dead = {(p, 0x3A): 0 for p in range(1, 5)}
    _, log = lrun('loop dead', 6, lambda: LoopHook(moves=[(1, 0)]), base_pre(mods=dead))
    check('demo_game_over' in [x[0] for x in log], f'loop dead: {log}')
    _, log = lrun('loop ending', 20,
                  lambda: LoopHook(moves=[(1, 0), (0, 1)], ret={'fr_flag_test': 1}),
                  base_pre(rate=400), clock_step=10)
    names = [x[0] for x in log]
    check('demo_ending' in names and 'iso_battle' not in names, f'loop ending: {names}')
