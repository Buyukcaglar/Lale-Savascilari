"""ISOSYS.L (ovl4595) WP17c: iso_char.c (party <-> battle, character values,
panel) and iso_battle.c against the original, in the world of uiworld.py.
State is written into DGROUP the same way on both sides; the whole DGROUP
(rand seed included), the screen and the results are compared.  The
small tests replace the WP17b routines with logging stand-ins on both
sides (tests/stubs/wp17c_stubs.c via lale.patch; e.stub on the original);
the scripted battles (test_battle_*) run iso_battle whole with the real
ones, the real battle graphics (SAVAS.CKD) and input that a Feeder gives
whenever the battle waits for a key."""
import ctypes
import os
import random
import struct

from lale import check, fn, lib, DG, NAMES, patch, unpatch
import helpers
import uiworld
from uiworld import run, SURFS, fit, pic, pc_key_push, pc_mouse_host

C = ctypes
i16, u16, vp = C.c_int16, C.c_uint16, C.c_void_p
DGA = C.addressof(DG)
DGSEG = 0x1F04          # runtime DGROUP segment of the original

c = {}


def cf(name, res=None, *args):
    if name not in c:
        c[name] = fn(name, res, *args)
    return c[name]


LOG = (C.c_int16 * (4 * 1024)).in_dll(lib, 'wp17c_log')
NLOG = C.c_int32.in_dll(lib, 'wp17c_nlog')
AI_KILL = C.c_int16.in_dll(lib, 'wp17c_ai_kill')

# WP17b routines iso_battle calls: redirected on the port to the logging
# stand-ins of wp17c_stubs.c (lale.patch), stubbed at the same address in
# the original.  name: (address, log id)
STUBS = {
    'iso_build_battlefield': ('4595:568C', 2),
    'iso_cast_vecize': ('4595:84E9', 3),
    'iso_eval_param': ('4595:8269', 4),
    'iso_vecize_effect': ('4595:8E4F', 5),
    'iso_after_hit': ('4595:9490', 6),
    'iso_ai_turn': ('4595:9668', 7),
}


def dgw16(s, off):
    return struct.unpack('<h', s.dg(off, 2))[0]


def setup_stubs(s, extra=None, ai_kill=1, stub=True):
    """original: stubs that log like wp17c_stubs.c; port: the patches
    (stub=False: the real WP17b routines on both sides)"""
    if not stub:
        s.log = []
        NLOG.value = 0
    elif s.side == 'o':
        s.log = []

        def mk(i):
            def f(e, a):
                actor = dgw16(s, 0xD5DE)
                if i == 3:
                    s.log.append((3, actor, dgw16(s, 0xD5E0), dgw16(s, 0xD5E2)))
                elif i == 4:
                    s.log.append((4, a(0) - 0x10000 if a(0) & 0x8000 else a(0), 0, 0))
                elif i == 5:
                    s.log.append((5, actor, dgw16(s, 0xD5E0), 0))
                elif i == 6:
                    s.log.append((6, actor, dgw16(s, 0xD56C), 0))
                elif i == 7:
                    u = 0xA08A + 0x28 * actor
                    s.log.append((7, actor, dgw16(s, u), 0))
                    if ai_kill:
                        w(s, u, 0)
                        w(s, 0xC2CE + 0x38 * dgw16(s, u + 8) + 2 * dgw16(s, u + 10), 0)
                else:
                    s.log.append((i, 0, 0, 0))
                return (0, 0)
            return f
        for name, (addr, i) in STUBS.items():
            s.e.stub(addr, mk(i))
    else:
        NLOG.value = 0
        AI_KILL.value = ai_kill
        for name in STUBS:
            patch(name, 'wp17c_' + name[4:])
    if extra:
        extra(s)


def srun(*a, **k):
    """uiworld.run with the port's patches removed afterwards"""
    try:
        return run(*a, **k)
    finally:
        unpatch()


def calls(s):
    if s.side == 'o':
        return s.log
    return [tuple(LOG[4 * i:4 * i + 4]) for i in range(NLOG.value)]


def call(s, name, *args):
    if s.side == 'o':
        r = s.c(name, *[a & 0xFFFF for a in args])[0]
    else:
        f = getattr(lib, name)
        f.restype = i16
        f.argtypes = [i16] * len(args)
        r = f(*args)
    return (r or 0) & 0xFFFF


def w(s, off, *vals):
    s.dgw(off, struct.pack(f'<{len(vals)}h', *vals))


def party(s, rng, hand1_ids=range(0, 27)):
    """random party records 1..4, templates and units 25..28"""
    for p in range(1, 5):
        base = 0x99A6 + 0x5C * p
        rec = [rng.randrange(0, 300) for _ in range(0x2E)]
        rec[0] = 0                                   # 0 in real records (empty hand -> word 0)
        rec[1] = rng.randrange(1, 9)                 # race
        rec[2] = rng.choice((1, 2))                  # gender
        rec[0x2A // 2] = rng.choice((0, 100, 499, 501, 900, 1500))
        rec[0x2C // 2] = rng.randrange(0, 30)       # race special
        for k in range(1, 6):
            rec[0x2C // 2 + k] = rng.choice((0, 0, rng.randrange(101, 999)))
        for i in range(1, 11):                       # inventory
            idd = rng.choice((0, 0, rng.randrange(1, 0x2C)))
            rec[0x22 + i] = rng.randrange(0, 4) * 100 + idd if idd else 0
        for off in (0x3C, 0x3E, 0x40):
            rec[off // 2] = rng.choice((0, 0x22 + rng.randrange(1, 11)))
        if rec[0x3C // 2]:
            rec[rec[0x3C // 2]] = rng.randrange(0, 4) * 100 + rng.choice(list(hand1_ids))
        if rng.random() < .3 and rec[0x3E // 2]:
            rec[rec[0x3E // 2]] = rng.randrange(0, 3) * 100 + 0x1A     # shield
        w(s, base, *rec)
        u = 0xA08A + 0x28 * (24 + p)
        w(s, u, *[rng.randrange(-5, 300) for _ in range(20)])
        w(s, u + 0x16, *[rng.choice((0, rng.randrange(1, 9) * 100 + rng.randrange(1, 30)))
                         for _ in range(5)])
        w(s, 0x9F18 + 0x2E * p, *[rng.randrange(-3, 300) for _ in range(23)])


# ======================================================================================
def test_char_stats():
    for sd in range(10):
        def op(s, sd=sd):
            rng = random.Random(700 + sd)
            party(s, rng)
            r = []
            call(s, 'iso_party_to_battle')
            for p in range(1, 5):
                w(s, 0x2898, p)
                w(s, 0xD5B6, rng.choice((1, 2)))
                call(s, 'iso_calc_weapon')
                r.append(s.dg(0xD5A8, 4) + s.dg(0xD5DA, 2))
                w(s, 0xD5AC, p)
                call(s, 'iso_calc_stat_a')         # AX: garbage (DGROUP segment)
                r.append(call(s, 'iso_calc_stat_b'))
                r.append(call(s, 'iso_calc_stat_c'))
                r.append(call(s, 'iso_calc_ap_by_load'))
            for p in range(25, 29):
                w(s, 0xA08A + 0x28 * p, rng.randrange(-9, 99), rng.randrange(0, 99))
            call(s, 'iso_battle_to_party')
            return r
        srun(f'iso char stats {sd}', op, setup=setup_stubs)


def test_char_stats_hand_table():
    """hand-1 ids 21..26: the F_SCOPY'd table and the local after it"""
    def op(s):
        rng = random.Random(77)
        for k in range(6):
            party(s, rng, hand1_ids=range(21, 27))
            call(s, 'iso_party_to_battle')
        return None
    srun('iso char stats table', op, setup=setup_stubs)


def test_item_effects():
    def op(s):
        rng = random.Random(9)
        party(s, rng)
        for k in range(30):
            w(s, 0x2898, rng.randrange(1, 5))
            w(s, 0xD5DE, rng.randrange(1, 29))
            call(s, rng.choice(('iso_item_effect_a', 'iso_item_effect_b', 'iso_item_effect_maxhp3',
                                'iso_item_effect_c', 'iso_item_effect_d', 'iso_item_effect_e',
                                'iso_item_effect_f', 'iso_item_effect_g', 'iso_item_effect_h')))
        return None
    srun('iso item effects', op, setup=setup_stubs)


def test_use_item_table():
    def op(s):
        rng = random.Random(21)
        party(s, rng)
        for k in range(40):
            p = rng.randrange(1, 5)
            w(s, 0x2898, p)
            w(s, 0xD5DE, 24 + p)
            w(s, 0xD5EA, rng.choice((rng.randrange(0x20, 0x2A), rng.randrange(1, 0x20))))
            call(s, 'iso_use_item')
        return None
    srun('iso use item', op, setup=setup_stubs)


def test_format_stat_line():
    def op(s):
        r = []
        for idd in list(range(0, 0x2C)) + [0x2B]:
            for cnt in (0, 1, 3):
                w(s, 0xD5EA, idd)
                w(s, 0xD5E6, cnt)
                call(s, 'iso_format_stat_line')
                r.append(s.dg(0xA510, 40).split(b'\0')[0])
        return r
    srun('iso format stat line', op, setup=setup_stubs)


# battle panel: work screen 6, item bank 4Bh, portraits 47h..4Ah, background 1
PANEL_SURFS = SURFS + [(6, 320, 200, 1, 'HAR4.CMP')] + \
    [(0x46 + p, 32, 32, 0, 'TRADE.CMP') for p in range(1, 5)] + \
    [(0x4E, 320, 32, 0, 'TRADE.CMP')]       # list windows' button icons (loaded by main)


def load_rix(s):
    """main's sys_load_rix("xdintro.rix"): the tint table gr_draw_strip needs"""
    if s.side == 'o':
        h, a = s.string(b'xdintro.rix')
        s.c('sys_load_rix', *a)
    else:
        lib.sys_load_rix.argtypes = [C.c_char_p]
        lib.sys_load_rix(b'xdintro.rix')


def test_panel():
    for sd in range(4):
        def op(s, sd=sd):
            rng = random.Random(900 + sd)
            party(s, rng)
            for c in range(1, 25):
                w(s, 0xA08A + 0x28 * c + 6, rng.randrange(5, 8))
            for g in range(4, 7):
                w(s, 0x5BC2 + 6 * g, rng.randrange(0, 40))
            for k in range(4):
                # as iso_redraw calls them: the panel for party actors, the
                # name for monsters (a monster actor would make 2898 negative)
                w(s, 0xD5DE, rng.randrange(25, 29))
                w(s, 0xD54C, rng.randrange(0, 12))
                call(s, 'iso_draw_char_info')
                w(s, 0xD5DE, rng.randrange(1, 24))
                call(s, 'iso_draw_name')
                w(s, 0x2898, rng.randrange(1, 5))
                for k2 in range(3):
                    call(s, 'iso_draw_char_frame', rng.choice((0, 0x1F, 9)), rng.randrange(1, 11))
                for k2 in range(5):
                    call(s, 'iso_draw_portrait', rng.randrange(0, 6), rng.randrange(1, 5),
                         rng.randrange(0, 2))
                w(s, 0xD5EA, rng.randrange(0, 0x2C))
                w(s, 0xD5E6, rng.randrange(0, 3))
                w(s, 0xD5E8, rng.randrange(0, 3))
                call(s, 'iso_refresh_desc')
            call(s, 'iso_init_panel_layout')
            return None
        srun(f'iso panel {sd}', op, setup=lambda s: setup_stubs(s, load_rix), surfs=PANEL_SURFS, slots=(6,))


# ======================================================================================
# whole battles: iso_battle with the real battle graphics, scripted keys
TAB, ENTER, ESC, SPACE = 0x0F09, 0x1C0D, 0x011B, 0x3920
KP = {'7': 0x4737, '8': 0x4838, '9': 0x4939, '4': 0x4B34, '6': 0x4D36, '1': 0x4F31,
      '2': 0x5032, '3': 0x5133}
MAPCELL = None      # the original's segment of the party's map cell


def seed_x(rng):
    return rng.randrange(0, 3)


def battle_world(s, rng, groups=((3, 2), (0, 0), (0, 0)), facing=0, cell=(0, 0, 0, 0, 0, 0, 0, 0),
                 level=1, terrain=0.15, inv=None, vecize=False):
    """an encounter as 3DSYS leaves it before iso_battle: party records,
    groups 5BE0.., options, map position, the window records; what
    iso_build_battlefield reads (map, wall pictures)"""
    load_rix(s)
    if s.side == 'c':                         # main's UI tables (window records)
        fn('iso_register_far_fns', None)()
        fn('ui_init_tables', None)()
    else:
        s.c('ui_init_tables')
    party(s, rng)
    for p in range(1, 5):
        base = 0x99A6 + 0x5C * p
        w(s, base + 0x42, rng.randrange(1, 17))               # portrait
        w(s, base + 0x3A, rng.randrange(3, 25))                # hit points: short battles
        w(s, base + 0x44, 0)                                   # no vecize on a hit (a random
                                                               # one has radius ~30000)
        # wall pictures; one of them 2 or 3: without, a door wall becomes
        # terrain 14, whose bar picture 2632h is 0 -> the original blits
        # from before yericon's surface (garbage), the port's mm_map has
        # nothing there (NULL)
        w(s, 0x9F0C + 2 * (p + 1), rng.choice((2, 3)) if p == 1 else rng.randrange(1, 9))
        if inv is not None:                                    # slots 1.., hands empty
            w(s, base + 0x46, *(list(inv) + [0] * (10 - len(inv))))
            w(s, base + 0x3C, 0, 0, 0)
        if vecize:                                             # knows a few vecizes (all 35
            known = rng.sample(range(35), 6)                   # overrun the menu's zones in
            s.dgw(0x9C95 + 0x94 * p, bytes(0x31 if k in known else 0x30   # the original)
                                           for k in range(35)) + bytes(1))
    for c in range(1, 29):                  # as the previous battle left them
        u = 0xA08A + 0x28 * c
        for off in (0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x22, 0x24):
            w(s, u + off, 0)
    for i, (t, n) in enumerate(groups, 1):
        w(s, 0x5BDA + 6 * i, t, n)
    s.dgw(0xBB34, bytes([1, 1, 1, 0, 0, level, 0]))
    w(s, 0x2898, 1)
    # the map: W x H cells of 15 bytes (walls N E S W: picture, kind;
    # byte 9 floor), the party at (px, py) with its cell `cell`
    W, H, px, py = 6, 5, 2 + seed_x(rng), 2
    data = bytearray()
    for k in range(W * H):
        c = bytearray(15)
        for d in range(4):
            if rng.random() < .3:
                c[2 * d], c[2 * d + 1] = rng.randrange(1, 5), rng.randrange(0, 4)
        c[9] = rng.randrange(0, 5)
        data += c
    data[(W * py + px) * 15:(W * py + px) * 15 + 8] = bytes(cell)
    data = bytes(data)
    if s.side == 'o':
        seg, _ = s.string(data)
        w(s, 0xB9EC, 0, seg)
    else:
        b, a = s.string(data)
        C.c_void_p.in_dll(lib, 's3d_map').value = a
    s.dgw(0xB9EA, bytes([H, W]))
    w(s, 0xB9F1, py)
    w(s, 0xB9F3, px)
    s.dgw(0xB9F0, bytes([facing]))
    s.dgw(0xBB3B, s.dg(0x86F8, 8))                             # the map's EMS mapping
    s.dgw(0xC8EE, bytes(0x36 * 0x1C))
    for x in range(28):
        for y in range(28):
            if rng.random() < terrain:
                w(s, 0xC8EE + 0x38 * x + 2 * y, rng.choice((1, 3, 6, 8, 12)))
    w(s, 0xD554, 1)
    w(s, 0xD552, 1)


# clock loops the port polls in: ui_wait_frames, ui_wait_voice, gr_fade,
# iso_cmd_vecize's 2000 ms
for _pt in ((0x05DC, 0x1E81), (0x05DC, 0x1D87), (0x0C7F, 0x0854), (0x4595, 0x52C3)):
    if _pt not in uiworld.ORIG_POLL_POINTS:
        uiworld.ORIG_POLL_POINTS.append(_pt)

SAVAS_DIR = os.path.join(helpers.ROOT, 'extracted', 'SAVAS')


def savas_mount():
    """the battle pictures: SAVAS.CKD mounted too (the original's file stub
    searches extracted/SAVAS)"""
    helpers.c_mount(b'3D.CKD', b'IVIR.CKD', b'SAVAS.CKD')


pc_bioskey = fn('pc_bioskey', i16, i16)


FEEDER = [None]


class Feeder:
    """input that arrives while the battle waits for it: every bioskey(1)
    peek of an empty buffer counts, and after `idle` of them in a row the
    next item is given - a key word, or ('m', x, y, buttons) for the mouse.
    Keys typed during animations would be flushed by the game; this makes
    a script mean the same on both sides.  After the script: Tab, forever
    (the stubbed AI then ends the battle), Esc between them to leave the
    inventory loops; after `cap` items 632E = 1 ends the battle at the
    next key read."""
    def __init__(self, items, idle=3, cap=150):
        self.items = list(items)
        self.cap = cap
        self.s = None
        self.idle = idle
        self.n = 0
        self.fed = 0
        self.polls = 0
        self.busy = False

    def poll(self):
        """host pump: a wait that reads the keyboard without bioskey (getch
        of a message) gets the next item after 3000 polls"""
        if self.busy:
            return
        self.polls += 1
        if self.polls >= 3000:
            self.busy = True
            try:
                if not pc_bioskey(1):
                    self.feed()
            finally:
                self.busy = False

    def bioskey(self, cmd):
        if cmd == 1:
            r = pc_bioskey(1)
            if r:
                return r
            self.n += 1
            if self.n < self.idle:
                return 0
            self.feed()
            return pc_bioskey(1)
        self.n = 0
        return pc_bioskey(cmd)

    def feed(self):
        if True:
            self.n = 0
            self.polls = 0
            self.fed += 1
            if self.fed > self.cap:
                self.s.dgw(0x632E, bytes((1, 0)))
            it = self.items.pop(0) if self.items else (TAB, ESC)[self.fed & 1]
            if isinstance(it, tuple):
                pc_mouse_host(*it[1:])
            else:
                pc_key_push(it)

    def start(self, s):
        self.s = s
        if s.side == 'o':
            s.e._stubs[s.e.lin('0000:1F3F')] = lambda e, a: (self.bioskey(a(0)) & 0xFFFF, 0)
        else:
            patch('ui_bioskey', lambda cmd: self.bioskey(cmd), i16, i16)


def battle_run(label, keys, seed=1, ai_kill=1, idle=20, stub=False, **world):
    def op(s):
        fd = FEEDER[0] = Feeder(keys, idle)
        fd.start(s)
        rng = random.Random(seed)
        battle_world(s, rng, **world)
        if s.side == 'o':                    # a whole battle: many instructions
            try:
                s.e.call(NAMES['iso_battle'], max_insns=4_000_000_000)
            except Exception:
                print('stuck: actor', dgw16(s, 0xD5DE), 'spell', dgw16(s, 0xD5E2), 'radius',
                      dgw16(s, 0xD586), 'fed', fd.fed, flush=True)
                raise
        else:
            call(s, 'iso_battle')
        check(fd.fed <= fd.cap + 2, f'{label}: the battle did not end ({fd.fed} inputs)')
        # freed conventional records (type FFh) keep a stale far pointer:
        # the allocators of the two worlds differ (emulator scratch heap vs
        # the port's), so the residue is cleared on both sides
        for i in range(110):
            r = 0xDA48 + 9 * i
            if s.dg(r + 8, 1)[0] == 0xFF:
                s.dgw(r, bytes(4))
        return calls(s), fd.fed, len(fd.items)
    old = uiworld.mount
    uiworld.mount = savas_mount
    pump = uiworld.Script._pump
    uiworld.Script._pump = lambda sc: (pump(sc), FEEDER[0].poll())
    helpers.SEARCH.insert(0, SAVAS_DIR)
    try:
        srun(label, op, setup=lambda s: setup_stubs(s, ai_kill=ai_kill, stub=stub), surfs=PANEL_SURFS,
             clock_step=1,
             # B9EC: each side's map pointer; 9662: the window record's far
             # code pointer (+36h, each side's own, see iso_register_far_fns)
             # D534: iso_ai_turn's copy of the 100 Hz clock E74A, which
             # uiworld does not compare either (advanced by polls)
             ignore=list(range(0xB9EC, 0xB9F0)) + list(range(0x9662, 0x9666))
             + [0xD534, 0xD535])
    finally:
        uiworld.mount = old
        uiworld.Script._pump = pump
        helpers.SEARCH.remove(SAVAS_DIR)


def test_battle_tab():
    """everybody ends the turn; the stubbed AI kills each monster on its turn"""
    battle_run('iso battle tab', [])


K = dict(o=0x186F, s=0x1F73, v=0x2F76, i=0x1769, q=0x1071, w=0x1177, e=0x1265,
         right=0x4D00, left=0x4B00, up=0x4800, down=0x5000)
GROUPS3 = ((3, 3), (9, 2), (0, 0))


def test_battle_moves():
    """keypad steps of the party members, then the turn ends"""
    keys = [KP[c] for c in '8869'] + [TAB] + [KP[c] for c in '2214'] + [TAB] + [KP['7']] * 6
    for sd in range(2):
        battle_run(f'iso battle moves {sd}', keys, seed=10 + sd, groups=GROUPS3)


def test_battle_attack():
    """Enter: the target cursor, next enemy ('o'), Enter attacks"""
    keys = [ENTER, K['o'], ENTER, TAB, ENTER, K['o'], K['o'], ENTER, KP['8'], ENTER, K['s'],
            ENTER, TAB, ENTER, ESC]
    for sd in range(2):
        battle_run(f'iso battle attack {sd}', keys * 3, seed=20 + sd, groups=GROUPS3)


def test_battle_inventory():
    """'i': the inventory by keyboard (arrows, Enter, the target slot, Enter),
    Q W E put down the hands, Space uses"""
    keys = [K['i'], K['right'], K['right'], ENTER, ENTER, K['q'], K['w'], K['e'],
            K['i'], K['down'], ENTER, K['right'], ENTER, K['i'], K['left'], ENTER,
            K['right'], K['right'], ENTER, K['i'], SPACE, K['i'], ESC, K['i'], ENTER, ESC,
            K['i'], ENTER, K['right'], K['right'], K['right'], ENTER, TAB]
    for sd in range(2):
        battle_run(f'iso battle inventory {sd}', keys * 2, seed=30 + sd, groups=GROUPS3)


def test_battle_vecize():
    """'v': the vecize menu of iso_cmd_vecize: a vecize chosen (Enter), the
    target cursor (Enter, 'o' next enemy), the cast; or left with Esc"""
    keys = [K['v'], ENTER, ENTER, TAB, K['v'], ESC, TAB, K['v'], K['down'], ENTER, K['o'], ENTER,
            TAB]
    for sd in range(2):
        battle_run(f'iso battle vecize {sd}', keys * 2, seed=40 + sd, groups=GROUPS3,
                   vecize=True)


def test_battle_setup():
    """placement: facings, walls of the map cell, an allied group, difficulty"""
    cells = [(0, 0, 0, 0, 0, 0, 0, 0), (9, 0, 9, 0, 9, 0, 9, 0), (0, 1, 0, 3, 0, 1, 0, 3),
             (2, 2, 7, 5, 0, 4, 1, 0)]
    for k in (0, 1, 4, 7):
        battle_run(f'iso battle setup {k}', [], seed=50 + k, facing=k % 4, cell=cells[k // 2],
                   level=k, groups=((3 + k, 2), (101 + k, 2) if k % 2 else (0, 0), (14, 1)))


# slots: sword (3), dagger (1), shield (1Ah), armour (1Ch), potion (21h), axe (0Ah)
INV = (3, 1, 0x1A, 0x1C, 0x21, 0x0A, 0x2B)


def test_battle_equip():
    """keyboard: hand 1, hand 2 (two one-handed, shield), armour, put down
    (Q W E), throw away (zone 14), use (Space)"""
    R, L_ = K['right'], K['left']
    keys = [K['i'], ENTER, ENTER,                              # sword -> hand 1
            K['i'], R, ENTER, R, ENTER,                        # dagger -> hand 2
            K['i'], R, R, ENTER, R, ENTER,                     # shield -> hand 2 (taken)
            K['w'], K['i'], R, R, ENTER, R, ENTER,             # put down, shield -> hand 2
            K['i'], R, R, R, ENTER, R, R, ENTER,               # armour
            K['i'], R, R, R, R, R, ENTER, R, R, R, ENTER,      # axe: throw away
            K['q'], K['i'], R, R, R, R, R, ENTER, ENTER,       # 2-handed? to hand 1
            K['e'], K['i'], R, R, R, R, SPACE, TAB]
    for sd in range(2):
        battle_run(f'iso battle equip {sd}', keys * 2, seed=60 + sd, groups=GROUPS3, inv=INV,
                   terrain=0)


def click(x, y, b=1):
    return [('m', x, y, b), ('m', x, y, 0)]


def test_battle_mouse():
    """the panel by mouse: arrows, slot -> hand / armour / throw away, right
    click uses, the attack / vecize / end-turn buttons"""
    slot = lambda k: (0xEC + 0x11 * ((k - 1) % 5) + 4, 0xA5 + 0x11 * ((k - 1) // 5) + 4)
    hand = lambda k: (0x3B + 0x11 * (k - 1) + 4, 0xA9)
    btn = lambda i: (0x90 + 0x27 * i + 8, 0x98)
    items = (click(0x06, 0x52) + click(0x30, 0x5E) + click(*slot(1)) + click(*hand(1))
             + click(*slot(2)) + click(*hand(2)) + click(*slot(4)) + click(*hand(3))
             + click(*hand(1)) + click(*slot(6)) + click(0x7A, 0xA9) + click(*slot(5), b=2)
             + click(*btn(2)) + [ESC] + click(*btn(1)) + [ESC] + click(*btn(0))
             + click(0x20, 0x5E) + [ENTER])
    for sd in range(2):
        battle_run(f'iso battle mouse {sd}', items * 2, seed=70 + sd, groups=GROUPS3, inv=INV,
                   terrain=0, vecize=True)
