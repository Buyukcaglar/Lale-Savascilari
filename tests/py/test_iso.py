"""ISOSYS.L (ovl4595, WP17) against the original, in the world of
uiworld.py.  Battle state (units A08A, templates 9F18, grids C2CE/C8EE,
party records 99A6) is written into DGROUP the same way on both sides;
the whole DGROUP (rand seed included), the screen and the results are
compared.  Routines not decompiled yet are stubbed on both sides
(tests/stubs/wp17_stubs.c; e.stub on the original)."""
import ctypes
import random
import struct

from lale import check, fn, DG, patch, unpatch
from uiworld import run

C = ctypes
i16, u16, vp = C.c_int16, C.c_uint16, C.c_void_p
DGA = C.addressof(DG)
DGSEG = 0x1F04          # runtime DGROUP segment of the original

c = {
    'iso_clamp_add': fn('iso_clamp_add', None, vp, i16, i16, i16),
    'iso_sign': fn('iso_sign', i16, i16),
    'iso_bit_test': fn('iso_bit_test', i16, u16, u16),
    'iso_step_sfx': fn('iso_step_sfx', i16, i16),
    'iso_add_status': fn('iso_add_status', None, i16, i16, i16),
    'iso_has_status': fn('iso_has_status', i16, i16, i16),
    'iso_roll_initiative': fn('iso_roll_initiative', None),
    'iso_give_xp': fn('iso_give_xp', None, i16, i16),
    'iso_xp_progress': fn('iso_xp_progress', None, i16),
    'iso_attack': fn('iso_attack', None),
    'iso_move_step': fn('iso_move_step', None),
    'iso_set_cell': fn('iso_set_cell', None, i16, i16, i16),
}

# not decompiled yet / other modules: stubbed on the original (far, no-op)
ORIG_STUBS = {
    'iso_calc_weapon': '4595:6828', 'iso_cast_vecize': '4595:84E9',
    'iso_draw_char_info': '4595:7574', 'iso_draw_name': '4595:7470',
}


def setup_stubs(s):
    """the routines of ORIG_STUBS do nothing on both sides (the port's are
    decompiled now: patched to no-ops, see run_stubbed)"""
    if s.side == 'o':
        for addr in ORIG_STUBS.values():
            s.e.stub(addr, lambda e, a: (0, 0))
    else:
        for name in ORIG_STUBS:
            patch(name, lambda: None, None)


def run_stubbed(label, op, **k):
    try:
        return run(label, op, setup=setup_stubs, **k)
    finally:
        unpatch()


def call(s, name, *args):
    """args: ints, or ('dg', off) for a far pointer into DGROUP"""
    if s.side == 'o':
        words = []
        for a in args:
            if isinstance(a, tuple):
                words += [a[1], DGSEG]
            else:
                words.append(a & 0xFFFF)
        r = s.c(name, *words)[0]
    else:
        r = c[name](*[(DGA + a[1]) if isinstance(a, tuple) else a for a in args])
    return (r or 0) & 0xFFFF


def w(s, off, *vals):
    s.dgw(off, struct.pack(f'<{len(vals)}h', *vals))


def unit(s, n, **f):
    """write fields of battle unit n (A08A + 28h*n)"""
    names = ['hp', 'spell', 'facing', 'tmpl', 'x', 'y', 'target', 'side', 'f10', 'to_hit',
             'dodge']
    base = 0xA08A + 0x28 * n
    for k, v in f.items():
        if k == 'stun':
            w(s, base + 0x22, v)
        elif k == 'on_hit':
            w(s, base + 0x20, v)
        else:
            w(s, base + 2 * names.index(k), v)


# ======================================================================================
def test_pure():
    def op(s):
        r = []
        for v in (-32768, -5, -1, 0, 1, 7, 32767):
            r.append(call(s, 'iso_sign', v))
        for n in (0, 1, 3, 15, 16, 0x80):   # FFFFh hangs the original too
            for m in (0, 1, 2, 0x80, 0x8000):
                r.append(call(s, 'iso_bit_test', n, m))
        for start, d, lo, hi in ((5, 1, 1, 6), (6, 1, 1, 6), (1, -1, 1, 6), (3, 10, 1, 2),
                                 (2, 1, 1, 2), (-4, 0, -3, 3), (0, 0, 0, 0)):
            w(s, 0xD5E6, start)
            call(s, 'iso_clamp_add', ('dg', 0xD5E6), d, lo, hi)
            r.append(struct.unpack('<h', s.dg(0xD5E6, 2))[0])
        return r
    run('iso pure', op)


def test_step_sfx():
    def op(s):
        r = []
        for inwin in (0, 1):
            s.dgw(0x9672, bytes([3]))
            s.dgw(0x9792, bytes([3 if inwin else 4]))
            for actor in (5, 24, 25):
                w(s, 0xD5DE, actor)
                for grp in (0, 14, 15, 16, 17, 18, 19, 30, 31, 32):
                    unit(s, actor, tmpl=6)
                    w(s, 0x5BC2 + 6 * 6, grp)
                    for t in range(-2, 13):
                        r.append(call(s, 'iso_step_sfx', t))
        return r
    run('iso_step_sfx', op)


def test_status():
    def op(s):
        rng = random.Random(5)
        r = []
        for n in range(1, 6):
            for k in range(7):
                call(s, 'iso_add_status', n, rng.randrange(1, 20), rng.randrange(1, 9))
            for k in range(1, 21):
                r.append(call(s, 'iso_has_status', n, k))
        return r
    run('iso status', op)


def battle(s, rng):
    """a random small battle: party 25..28 (side 1), monsters 1..8 (side 2)"""
    s.dgw(0xC2CE, bytes(0x36 * 0x1C * 2))
    for n in list(range(1, 9)) + list(range(25, 29)):
        party = n >= 25
        x, y = rng.randrange(2, 26), rng.randrange(2, 26)
        unit(s, n, hp=rng.randrange(1, 40), facing=rng.randrange(1, 5),
             tmpl=(n - 24) if party else 5 + (n - 1) // 8,
             x=x, y=y, side=1 if party else 2, to_hit=rng.randrange(0, 12),
             dodge=rng.randrange(0, 10), stun=rng.choice((0, 0, 0, 2)),
             on_hit=0)
        call(s, 'iso_set_cell', x, y, n)
    for g in range(1, 8):
        w(s, 0x9F18 + 0x2E * g, *[rng.randrange(-3, 400) for _ in range(23)])
    for p in range(1, 5):
        w(s, 0x99A6 + 0x5C * p, *[rng.randrange(0, 300) for _ in range(0x2E)])
        w(s, 0x9B7C + 0x24 * p, *[rng.randrange(-3, 4) for _ in range(0x12)])
        w(s, 0x9902 + 0x22 * p, *[rng.randrange(0, 200) for _ in range(0x11)])


def test_xp():
    def op(s):
        rng = random.Random(11)
        battle(s, rng)
        for k in range(40):
            call(s, 'iso_give_xp', rng.choice((1, 5, 25, 26, 27, 28)), rng.randrange(0, 400))
        for p in range(1, 5):
            call(s, 'iso_xp_progress', p)
        return None
    run('iso xp', op)


def test_initiative():
    def op(s):
        battle(s, random.Random(3))
        call(s, 'iso_roll_initiative')
        call(s, 'iso_roll_initiative')
        return None
    run('iso initiative', op)


def test_attack():
    hits = []
    for sd in range(12):
        def op(s, sd=sd):
            r = []
            rng = random.Random(100 + sd)
            battle(s, rng)
            s.dgw(0x9672, b'\x01')
            s.dgw(0x9792, b'\x02')            # not in the battle window: no drawing
            for k in range(6):
                a = rng.choice((1, 2, 3, 25, 26, 27, 28))
                d = rng.choice((4, 5, 6, 25, 26, 27, 28))
                w(s, 0xD5DE, a)
                w(s, 0xD56E, a)
                w(s, 0xD56C, d)
                w(s, 0xD54C, rng.randrange(0, 12))
                w(s, 0xD5A4, rng.choice((0, 0, 1)))
                w(s, 0xD55A, rng.choice((0, 1)))
                call(s, 'iso_attack')
                r.append(struct.unpack('<5h', s.dg(0xD56C, 2) + s.dg(0xD570, 2) + s.dg(0xD59C, 2)
                                       + s.dg(0xA08A + 0x28 * d, 2) + s.dg(0xD566, 2)))
            return r
        res = run_stubbed(f'iso_attack {sd}', op, clock_step=1)
        hits.extend(res[0])
    check(any(x[1] > 0 for x in hits), 'iso_attack: some hit')
    check(any(x[3] < 1 for x in hits), 'iso_attack: some kill')
    check(any(x[4] == 5 for x in hits), 'iso_attack: some miss')


def test_move_step():
    for sd in range(8):
        def op(s, sd=sd):
            r = []
            rng = random.Random(300 + sd)
            battle(s, rng)
            s.dgw(0x9672, b'\x01')
            s.dgw(0x9792, b'\x02')
            s.dgw(0xC8EE, bytes(rng.choice((0, 1, 3, 6, 8, 12)) if rng.random() < .3 else 0
                                for _ in range(0x36 * 0x1C)))
            for k in range(10):
                w(s, 0xD5DE, rng.choice((1, 2, 25, 26)))
                w(s, 0xD5D6, rng.randrange(0x46, 0x53))
                w(s, 0xD57C, rng.choice((0, 0, 0, 1)))
                w(s, 0xD54C, rng.randrange(0, 10))
                call(s, 'iso_move_step')
                a = struct.unpack('<h', s.dg(0xD5DE, 2))[0]
                r.append(struct.unpack('<3h', s.dg(0xA08A + 0x28 * a, 2)
                                       + s.dg(0xA092 + 0x28 * a, 4)))
            return r
        run_stubbed(f'iso_move_step {sd}', op, clock_step=1)
