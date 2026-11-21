"""ISOSYS.L (ovl4595) part WP17b against the original, in the world of
uiworld.py (as tests/py/test_iso.py): target cursor, attack / vecize
commands, battlefield, vecize effects, monster AI.  The battle state is
written into DGROUP the same way on both sides; the whole DGROUP (rand
seed included), the screen, the results and the calls of the stubbed
routines (tests/stubs/wp17b_stubs.c; e.stub on the original) are compared."""
import ctypes

import unicorn.x86_const as X86
import os
import random
import struct

import helpers
import uiworld
from lale import ROOT, check, fn, lib, DG, patch, unpatch
from uiworld import ORIG_POLL_POINTS

C = ctypes
i16, u16, vp = C.c_int16, C.c_uint16, C.c_void_p
DGA = C.addressof(DG)
DGSEG = 0x1F04          # runtime DGROUP segment of the original

_fns = {}
RET = {'iso_eval_param': i16}


def cfn(name, n):
    if name not in _fns:
        _fns[name] = fn(name, RET.get(name), *([i16] * n))
    return _fns[name]


# routines a test replaces on both sides (original: e.stub at the address;
# port: lale.patch), their calls logged as (name, actor D5DE, target D5E0)
STUBS = {}


def dgw16(s, off):
    return struct.unpack('<h', s.dg(off, 2))[0]


def setup_stubs(s, stubs=None):
    s.log = []
    for name, addr in (stubs or STUBS).items():
        def f(*_, name=name):
            s.log.append((name, dgw16(s, 0xD5DE), dgw16(s, 0xD5E0)))
            return 0
        if s.side == 'o':
            s.e.stub(addr, lambda e, a, f=f: (f(), 0))
        else:
            patch(name, f, None)


def stub_log(s):
    return s.log


def call(s, name, *args):
    if s.side == 'o':
        r = s.c(name, *[a & 0xFFFF for a in args])[0]
    else:
        r = cfn(name, len(args))(*args)
    return (r or 0) & 0xFFFF


def w(s, off, *vals):
    s.dgw(off, struct.pack(f'<{len(vals)}h', *vals))


def rd(s, off):
    return struct.unpack('<h', s.dg(off, 2))[0]


FIELDS = ['hp', 'spell', 'facing', 'tmpl', 'x', 'y', 'target', 'side', 'f10', 'to_hit',
          'dodge', 's0', 's1', 's2', 's3', 's4', 'on_hit', 'stun', 'hit', 'f26']


def unit(s, n, **f):
    for k, v in f.items():
        w(s, 0xA08A + 0x28 * n + 2 * FIELDS.index(k), v)


def set_cell(s, x, y, v):
    w(s, 0xC2CE + 0x38 * x + 2 * y, v)


def battle(s, rng, monsters=(1, 2, 3, 4, 9, 10, 17, 18), party=range(25, 29), statuses=False,
           terrain=False):
    """a random battle: party (side 1), monsters (side 2), templates, records;
    terrain: random floor (1..9) with some walls (10..13) instead of all 0"""
    s.dgw(0xA08A, bytes(0x28 * 29))
    s.dgw(0xC2CE, bytes(0x38 * 0x1C))
    if terrain:
        for x in range(0x1C):
            w(s, 0xC8EE + 0x38 * x, *[rng.randrange(10, 14) if rng.random() < .04 else
                                      rng.randrange(1, 10) for _ in range(0x1C)])
    used = set()
    for n in list(monsters) + list(party):
        p = n >= 25
        while True:
            x, y = rng.randrange(2, 26), rng.randrange(2, 26)
            if (x, y) not in used:
                break
        used.add((x, y))
        unit(s, n, hp=rng.randrange(1, 40), spell=rng.randrange(0, 30), facing=rng.randrange(1, 5),
             tmpl=(n - 24) if p else 5 + (n - 1) // 8, x=x, y=y, side=1 if p else 2,
             to_hit=rng.randrange(0, 12), dodge=rng.randrange(0, 10),
             target=rng.choice(list(monsters) + list(party)),
             stun=rng.choice((0, 0, 0, 2)))
        if statuses:
            for k in range(5):
                if rng.random() < .3:
                    w(s, 0xA08A + 0x28 * n + 0x16 + 2 * k, rng.randrange(1, 6) * 100 + rng.randrange(1, 30))
        set_cell(s, x, y, n)
    for g in range(1, 8):
        w(s, 0x9F18 + 0x2E * g, *[rng.randrange(-3, 400) for _ in range(23)])
    for p in range(1, 5):
        party_record(s, rng, p)
        w(s, 0x9B7C + 0x24 * p, *[rng.randrange(-3, 4) for _ in range(0x12)])
        w(s, 0x9902 + 0x22 * p, *[rng.randrange(0, 200) for _ in range(0x11)])


def party_record(s, rng, p):
    """a plausible GRP.DAT record (as tests/py/test_iso_c.py party()):
    inventory words +46.. (count*100 + item id < 2Ch), hands +3C/+3E/+40
    pointing at inventory words"""
    rec = [rng.randrange(0, 300) for _ in range(0x2E)]
    rec[0] = 0
    rec[1] = rng.randrange(1, 9)                 # race
    rec[2] = rng.choice((1, 2))                  # gender
    rec[0x2A // 2] = rng.choice((0, 100, 499, 501, 900, 1500))
    rec[0x2C // 2] = rng.randrange(0, 30)
    for k in range(1, 6):
        rec[0x2C // 2 + k] = rng.choice((0, 0, rng.randrange(101, 999)))
    for i in range(1, 11):
        idd = rng.choice((0, 0, rng.randrange(1, 0x2C)))
        rec[0x22 + i] = rng.randrange(0, 4) * 100 + idd if idd else 0
    for off in (0x3C, 0x3E, 0x40):
        rec[off // 2] = rng.choice((0, 0x22 + rng.randrange(1, 11)))
    rec[0x3C // 2] = 0x22 + rng.randrange(1, 11)
    rec[rec[0x3C // 2]] = rng.randrange(0, 3) * 100 + rng.randrange(1, 27)
    rec[0x42 // 2] = rng.randrange(1, 17)        # portrait
    w(s, 0x99A6 + 0x5C * p, *rec)


SEARCH = [os.path.join(ROOT, 'extracted', a) for a in ('SAVAS', 'IKON', 'FRPOS')]


def load_rix(s):
    """main's sys_load_rix("xdintro.rix"): the tint table of the panel"""
    if s.side == 'o':
        h, a = s.string(b'xdintro.rix')
        s.c('sys_load_rix', *a)
    else:
        lib.sys_load_rix.argtypes = [C.c_char_p]
        lib.sys_load_rix(b'xdintro.rix')


# portraits 47h..4Ah (loaded with a saved game); slots 1, 7, 4Bh are loaded
# by battle_gfx
SURFS_B = [(0x46 + p, 32, 32, 0, 'TRADE.CMP') for p in range(1, 5)]


def battle_gfx(s):
    """the battle pictures as iso_battle loads them (SAVAS.CKD): monster
    groups 1..3, walls 1, 4, 6, 8"""
    for i, t in enumerate((1, 2, 3)):
        w(s, 0x5BE0 + 6 * i, t, 8)
    w(s, 0x9F10, 1, 4, 6, 8)
    load_rix(s)
    call(s, 's3d_load_ui_images')            # the panel pictures main loads
    call(s, 'iso_load_battle_gfx')


def div0_handler(e):
    """the game's INT 00h handler (ctl_div0_isr, engine/nkontrol.h): on a
    divide error AX = FFFFh, DX unchanged, execution resumes 3 bytes after
    the faulting instruction.  Unicorn re-runs the translation block when
    an INT hook moves IP after an exception (and then raises a double
    fault), so the handler is applied before the instruction instead: every
    `div/idiv word [bp+d8]` (F7 76/7E xx, 3 bytes) in ISOSYS gets a code
    hook that, when the division would fault, sets AX = FFFFh and skips it.
    (ISOSYS's other divisions divide by constants in BX.)"""
    import unicorn as U
    code = e.rd(0x4595, 0, 0xB050)
    sites = [i for i in range(len(code) - 2) if code[i] == 0xF7 and code[i + 1] in (0x76, 0x7E)]

    def hook(uc, address, size, _):
        r = uc.reg_read
        op = uc.mem_read(address, 3)
        if op[0] != 0xF7 or op[1] not in (0x76, 0x7E):
            return
        disp = op[2] - 256 if op[2] >= 128 else op[2]
        ss, bp = r(X86.UC_X86_REG_SS), r(X86.UC_X86_REG_BP)
        d = struct.unpack('<H', uc.mem_read(ss * 16 + ((bp + disp) & 0xFFFF), 2))[0]
        n = (r(X86.UC_X86_REG_DX) << 16) | r(X86.UC_X86_REG_AX)
        if op[1] == 0x7E:                       # idiv: signed
            n = n - (1 << 32) if n >= 1 << 31 else n
            d = d - 65536 if d >= 32768 else d
            q = None if d == 0 else (abs(n) // abs(d)) * (1 if (n < 0) == (d < 0) else -1)
            bad = q is None or not -32768 <= q <= 32767
        else:
            bad = d == 0 or n // d > 0xFFFF
        if bad:
            uc.reg_write(X86.UC_X86_REG_AX, 0xFFFF)
            uc.reg_write(X86.UC_X86_REG_IP, (r(X86.UC_X86_REG_IP) + 3) & 0xFFFF)
            e.div0_count = getattr(e, 'div0_count', 0) + 1
    e.div0_count = 0
    for i in sites:
        lin = e.lin(0x4595, i)
        e.u.hook_add(U.UC_HOOK_CODE, hook, begin=lin, end=lin)


def run(label, op, setup=None, **k):
    """uiworld.run with SAVAS.CKD mounted and the battle pictures loaded"""
    def setup2(s):
        if s.side == 'o':
            div0_handler(s.e)
        battle_gfx(s)
        setup_stubs(s)
        if setup:
            setup(s)
    old_mount, old_search = uiworld.mount, list(helpers.SEARCH)
    uiworld.mount = lambda: helpers.c_mount(b'3D.CKD', b'IVIR.CKD', b'SAVAS.CKD',
                                                   b'IKON.CKD', b'FRPOS.CKD')
    helpers.SEARCH.extend(SEARCH)
    # conventional blocks made EMS (mm_change_type: sprite frames, walls):
    # the original keeps the stale segment word in the MEMMAN record
    k.setdefault('surfs', SURFS_B)
    # in_window's copy of record 974C: +36h is a far code pointer (each
    # side's own, see iso_register_far_fns)
    k['ignore'] = list(k.get('ignore', ())) + list(range(0x9662, 0x9666))
    # (MEMMAN records 9 bytes at DA48; cleared on both sides after op)
    def op2(s):
        r = op(s)
        for i in range(110):
            rec = 0xDA48 + 9 * i
            if s.dg(rec + 8, 1)[0] in (1, 0xFF):
                s.dgw(rec + 2, bytes(2))
        return r
    try:
        return uiworld.run(label, op2, setup=setup2, **k)
    finally:
        unpatch()
        uiworld.mount = old_mount
        helpers.SEARCH[:] = old_search


def no_window(s):
    s.dgw(0x9672, b'\x01')
    s.dgw(0x9792, b'\x02')


# clock loops the port polls in: ui_wait_frames, ui_wait_voice, iso_cmd_vecize's 2000 ms
for _pt in ((0x05DC, 0x1E81), (0x05DC, 0x1D87), (0x4595, 0x52C3)):
    if _pt not in ORIG_POLL_POINTS:
        ORIG_POLL_POINTS.append(_pt)


# ======================================================================================
KEYS = {'s': 0x1F73, 'o': 0x186F, 'enter': 0x1C0D, 'esc': 0x011B, 'up': 0x4800,
        'left': 0x4B00, 'pgdn': 0x5100, 'x': 0x2D78}


def test_pick_target():
    seqs = [['enter'], ['esc'], ['s', 'enter'], ['o', 'o', 'enter'], ['up', 'left', 'enter'],
            ['x', 's', 's', 's', 'pgdn', 'enter'], ['o', 'esc']]
    for sd, seq in enumerate(seqs * 2):
        def op(s, sd=sd):
            rng = random.Random(500 + sd)
            battle(s, rng)
            no_window(s)
            if sd >= len(seqs):       # last target dead: the cursor starts on the actor
                unit(s, 25, target=3)
                unit(s, 3, hp=0)
            w(s, 0xD5DE, 25)
            call(s, 'iso_pick_target')
            return [rd(s, 0xD5E0), rd(s, 0xD57A), rd(s, 0xD578)]
        run(f'iso_pick_target {sd}', op, keys=[KEYS[k] for k in seq],
            extra=stub_log, clock_step=1)


def test_cmd_attack():
    for sd in range(16):
        def op(s, sd=sd):
            rng = random.Random(700 + sd)
            battle(s, rng)
            no_window(s)
            s.dgw(0xC8EE, bytes(rng.choice((0, 1, 12)) if rng.random() < .2 else 0
                                for _ in range(0x38 * 0x1C)))
            r = []
            for k in range(4):
                a = rng.choice((1, 2, 25, 26))
                d = rng.choice((0, 3, 4, 27, 28))
                g = rd(s, 0xA090 + 0x28 * a)
                w(s, 0x9F18 + 0x2E * g + 14, rng.choice((50, 100, 102, 105)))
                w(s, 0x9F18 + 0x2E * g + 34, rng.randrange(0, 15))
                w(s, 0xD5DE, a)
                w(s, 0xD5E0, d)
                w(s, 0x2898, rng.randrange(1, 5))
                w(s, 0xD57A, rd(s, 0xA092 + 0x28 * d))
                w(s, 0xD578, rd(s, 0xA094 + 0x28 * d))
                w(s, 0xD54C, rng.randrange(0, 8))
                call(s, 'iso_cmd_attack')
                r.append(rd(s, 0xA08A + 0x28 * d))
            return r
        run(f'iso_cmd_attack {sd}', op, keys=[0x1C0D] * 12, extra=stub_log, clock_step=1)


def test_vecize_reaction():
    for sd in range(24):
        def op(s, sd=sd):
            rng = random.Random(900 + sd)
            battle(s, rng)
            no_window(s)
            a, t = rng.choice((1, 2, 25, 26)), rng.choice((3, 4, 27, 28))
            if sd % 3:
                w(s, 0xA08A + 0x28 * t + 0x16 + 2 * rng.randrange(5), 300 + 0x19)
            w(s, 0xD5DE, a)
            w(s, 0xD5E0, t)
            w(s, 0xD570, rng.choice((0, 3, 7)))
            call(s, 'iso_vecize_reaction')
            return [rd(s, 0xD5E0), rd(s, 0xD570)]
        run(f'iso_vecize_reaction {sd}', op, extra=stub_log, clock_step=1)




def in_window(s):
    """the battle's list window (record 974C) is the current UI record"""
    if s.side == 'c':
        fn('iso_register_far_fns', None)()
        fn('ui_init_tables', None)()
    else:
        s.c('ui_init_tables')
    s.dgw(0x962C, s.dg(0x974C, 0x48))


def test_cmd_vecize():
    for sd in range(16):
        def op(s, sd=sd):
            rng = random.Random(1100 + sd)
            battle(s, rng)
            if sd % 4 == 3:                 # the camp: only "vecize bilmiyor!"
                no_window(s)
            else:
                in_window(s)
            p = rng.randrange(1, 5)
            w(s, 0x2898, p)
            a = rng.choice((1, 9, 17, 24 + p)) if sd % 4 != 3 else 24 + p
            w(s, 0xD5DE, a)
            known = ''.join(rng.choice('0001') if sd % 4 < 3 else '0' for _ in range(35))
            s.dgw(0x9C95 + 0x94 * p, known.encode())
            w(s, 0x99A6 + 0x5C * p + 0x38, rng.randrange(0, 12))
            call(s, 'iso_cmd_vecize')
            return [rd(s, 0xD5E2), rd(s, 0xD562), rd(s, 0xD5BC)]
        n = 1 + sd % 3
        run(f'iso_cmd_vecize {sd}', op, keys=[0x5000] * (n - 1) + [0x1C0D] * 2,
            extra=stub_log, clock_step=10)


# ======================================================================================
def district_map(n):
    """FRPOS PCn: width, height and the 15-byte cells (file byte 2Bh)"""
    d = open(os.path.join(ROOT, 'extracted', 'FRPOS', f'PC{n}'), 'rb').read()
    return d[20], d[21], d[0x2B:]


def set_map(s, cells):
    if s.side == 'o':
        seg, _ = s.string(cells)
        w(s, 0xB9EC, 0, seg)
    else:
        b, a = s.string(cells)
        C.c_void_p.in_dll(lib, 's3d_map').value = a


def test_build_battlefield():
    """every district map, the party on every cell, random wall pictures"""
    for n in range(1, 19):
        mw, mh, cells = district_map(n)

        def op(s, n=n):
            rng = random.Random(1300 + n)
            set_map(s, cells)
            s.dgw(0xB9EA, bytes([mh, mw]))
            s.dgw(0xBB3B, s.dg(0x86F8, 8))         # the map's EMS mapping
            r = []
            for y in range(mh):
                for x in range(mw):
                    w(s, 0x9F0E, *[rng.choice((1, 2, 3, 4, 5, 7, 8, 8, 9)) for _ in range(5)])
                    w(s, 0xB9F3, x)
                    w(s, 0xB9F1, y)
                    call(s, 'iso_build_battlefield')
                    r.append(hash(s.dg(0xC8B0, 0x38 * 0x1C + 0x40)))
            return r
        uiworld.run(f'iso_build_battlefield PC{n}', op, ignore=range(0xB9EC, 0xB9F0))


# ======================================================================================
# iso_spell.c
SPELL_VARS = [0xD5E0, 0xD570, 0xD574, 0xD576, 0xD566, 0xD582, 0xD59C, 0x631A, 0x6326]


def test_eval_param():
    vals = list(range(995, 1022)) + [-5, 0, 999, 0x7FFF]
    for sd in range(4):
        def op(s, sd=sd):
            rng = random.Random(1300 + sd)
            battle(s, rng)
            w(s, 0xD5DE, rng.choice((1, 25)))
            w(s, 0xD5E0, rng.choice((2, 26)))
            w(s, 0xD5BE, rng.randrange(0, 9))
            w(s, 0xD55C, rng.randrange(-1, 9))
            return [call(s, 'iso_eval_param', v) for v in vals]
        run(f'iso_eval_param {sd}', op, extra=stub_log)


def spell_setup(s, rng, sp):
    """a vecize in a random battle: caster, target, cursor, powers"""
    battle(s, rng, statuses=True)
    a = rng.choice((1, 9, 25, 26))
    t = rng.choice((0, 2, 3, 10, 18, 27, 28)) if rng.random() < .9 else a
    w(s, 0xD5DE, a)
    w(s, 0xD5E0, t)
    w(s, 0xD5E2, sp)
    w(s, 0xD5BE, rng.randrange(0, 9))
    w(s, 0xD55C, rng.randrange(0, 9))
    w(s, 0xD5BC, rng.randrange(0, 8))
    w(s, 0xD5BA, rng.choice((0, 0, 1)))
    ax, ay = rd(s, 0xA092 + 0x28 * a), rd(s, 0xA094 + 0x28 * a)
    while True:
        cx, cy = rng.randrange(0, 28), rng.randrange(0, 28)
        if (cx, cy) != (ax, ay):        # vecize 29 divides by the distance
            break
    if t and rng.random() < .6:
        cx, cy = rd(s, 0xA092 + 0x28 * t), rd(s, 0xA094 + 0x28 * t)
    w(s, 0xD57A, cx)
    w(s, 0xD578, cy)
    w(s, 0xD580, rng.randrange(0, 20))
    w(s, 0xD57E, rng.randrange(0, 20))
    for c in rng.sample(range(1, 29), 4):       # some deaf / (23) units
        w(s, 0xA08A + 0x28 * c + 0x16 + 2 * rng.randrange(5), rng.choice((0x1F, 0x17)) * 100 + 5)
    return a, t


def test_cast_vecize():
    for sp in range(1, 35):
        for k in range(3):
            def op(s, sp=sp, k=k):
                rng = random.Random(1400 + 10 * sp + k)
                spell_setup(s, rng, sp)
                if k == 2:
                    no_window(s)
                else:
                    in_window(s)
                w(s, 0xD5A2, 1 if k == 1 and rng.random() < .5 else 0)
                call(s, 'iso_cast_vecize')
                return [rd(s, a) for a in SPELL_VARS + list(range(0xD584, 0xD59A, 2))]
            run(f'iso_cast_vecize {sp}/{k}', op, extra=stub_log, clock_step=10)


def test_vecize_effect():
    """random parameter words (every flag combination) on random targets"""
    for sd in range(48):
        def op(s, sd=sd):
            rng = random.Random(1500 + sd)
            sp = rng.choice((16, 17, 17, 17, 30)) if sd % 2 else rng.randrange(0, 36)
            spell_setup(s, rng, sp)
            in_window(s)
            t = rng.choice((2, 3, 10, 18, 27, 28))
            w(s, 0xD5E0, t)
            if sd % 5 == 0:     # template word 10 <= 0
                g = rd(s, 0xA090 + 0x28 * t)
                w(s, 0x9F2C + 0x2E * g, rng.choice((-1, 0)))
            w(s, 0x6326, rng.randrange(2))
            w(s, 0xD598, rng.randrange(0, 0x100))
            w(s, 0xD596, rng.choice((0, 5)), rng.choice((0, 7)), rng.randrange(0, 6), rng.randrange(0, 6),
              rng.randrange(1, 20), rng.randrange(0, 3) * rng.randrange(1, 20), rng.randrange(-5, 50),
              rng.randrange(-5, 50), rng.randrange(0, 3), rng.randrange(1, 5))
            call(s, 'iso_vecize_effect')
            return [rd(s, a) for a in SPELL_VARS]
        run(f'iso_vecize_effect {sd}', op, extra=stub_log, clock_step=10)


def test_after_hit():
    for sd in range(16):
        def op(s, sd=sd):
            rng = random.Random(1600 + sd)
            battle(s, rng)
            in_window(s)
            a, d = rng.sample((1, 9, 25, 26), 2)
            w(s, 0xD5DE, a)
            w(s, 0xD56E, a)
            w(s, 0xD56C, d)
            if sd % 4 == 1:
                unit(s, d, hp=rng.choice((1, 2)))
            if sd % 4 == 2:
                unit(s, a, hp=rng.choice((1, 2)))
            w(s, 0xD56A, rng.choice((0, 1, 1)))
            w(s, 0xD572, rng.choice((0, 1, 1)))
            w(s, 0xD59C, rng.randrange(0, 6))
            w(s, 0xD566, rng.randrange(0, 9))
            w(s, 0xD54C, 5)
            call(s, 'iso_after_hit')
            return [rd(s, a) for a in SPELL_VARS + [0xD56A, 0xD572, 0xD54C]]
        run(f'iso_after_hit {sd}', op, extra=stub_log, clock_step=10)


# ======================================================================================
# iso_ai.c
def test_ai_turn():
    """many seeded monster turns: surrender, targets, vecize, attacks, walks"""
    for sd in range(96):
        def op(s, sd=sd):
            rng = random.Random(1700 + sd)
            mons = rng.choice(((1, 2, 3, 4, 9, 10, 17, 18), (1, 2), (9, 17, 18)))
            battle(s, rng, monsters=mons, statuses=sd % 3 == 0, terrain=sd % 6 != 5)
            in_window(s)
            a = rng.choice(mons) if sd % 8 else rng.choice((25, 26))
            unit(s, a, side=2, hp=rng.randrange(1, 40), hit=rng.choice((0, 0, 1)),
                 spell=rng.choice((0, 5, 30, 120, 400)))
            if sd % 4 == 1:     # next to its target
                t = rng.choice((25, 26, 27, 28))
                tx, ty = rd(s, 0xA092 + 0x28 * t), rd(s, 0xA094 + 0x28 * t)
                ox, oy = rd(s, 0xA092 + 0x28 * a), rd(s, 0xA094 + 0x28 * a)
                nx, ny = tx + rng.choice((-1, 1)), ty
                if not rd(s, 0xC2CE + 0x38 * nx + 2 * ny):
                    set_cell(s, ox, oy, 0)
                    set_cell(s, nx, ny, a)
                    unit(s, a, x=nx, y=ny, target=t)
            if sd % 5 == 2:     # a dead target
                unit(s, rd(s, 0xA096 + 0x28 * a), hp=0)
            for g in range(5, 8):       # group types (set from the encounter)
                w(s, 0x5BC2 + 6 * g, rng.randrange(0, 30))
            if sd % 3 == 2:     # a vecize monster: knowledge string, points, no hit
                g = rd(s, 0x5BC2 + 6 * rd(s, 0xA090 + 0x28 * a))
                known = ''.join(rng.choice('011') for _ in range(0x23))
                if sd % 6 == 2:     # area vecizes only (13, 17, 22, 24, 27)
                    known = ''.join('1' if i in (13, 17, 22, 24, 27) else '0' for i in range(0x23))
                s.dgw(0x195B + 0x23 * g, known.encode())
                unit(s, a, spell=rng.choice((150, 400, 900)), hit=0, to_hit=rng.randrange(0, 3))
                w(s, 0x9F18 + 0x2E * rd(s, 0xA090 + 0x28 * a) + 18, rng.randrange(0, 500))
            if a > 24:          # a charmed party unit: its own knowledge string
                s.dgw(0x8EB4 + 0x94 * a, ''.join(rng.choice('01') for _ in range(0x22)).encode())
            w(s, 0xD5DE, a)
            w(s, 0xD54C, rng.randrange(1, 12))
            w(s, 0xD54E, rng.choice((0, 1)))
            w(s, 0xD550, rng.choice((0, 1)))
            w(s, 0xD558, rng.randrange(1, 12))
            w(s, 0xD580, 0, 0)
            call(s, 'iso_ai_turn')
            return [rd(s, x) for x in (0xD54C, 0xD5E0, 0xD5E2, 0xD56E, 0xD56C, 0x632E)]
        run(f'iso_ai_turn {sd}', op, extra=stub_log, clock_step=10)


def test_div0():
    """the divide-by-zero sites: vecize 29 with the cursor on the caster,
    ai_turn with no living monster (D558 / 0) and 50*to-hit + 30 == 0"""
    def cast(s):
        rng = random.Random(1800)
        a, _ = spell_setup(s, rng, 29)
        in_window(s)
        w(s, 0xD5A2, 0)
        w(s, 0xD578, rd(s, 0xA094 + 0x28 * a))
        w(s, 0xD57A, rd(s, 0xA092 + 0x28 * a))
        call(s, 'iso_cast_vecize')
        if s.side == 'o':
            assert getattr(s.e, 'div0_count', 0) >= 1, 'no division by zero'
        return [rd(s, x) for x in SPELL_VARS]
    run('div0 cast 29', cast, extra=stub_log, clock_step=10)
    for sd in range(6):
        def ai(s, sd=sd):
            rng = random.Random(1810 + sd)
            battle(s, rng, terrain=True)
            in_window(s)
            a = rng.choice((1, 9, 17))
            if sd < 3:      # no living monster: D558 / 0
                for m in (1, 2, 3, 4, 9, 10, 17, 18):
                    unit(s, m, hp=0)
                w(s, 0xD54E, 1)
            else:           # 50 * 6553 + 30 wraps to 0
                unit(s, a, to_hit=6553, hit=0, spell=rng.choice((0, 5, 300)))
                w(s, 0xD54E, 0)
            w(s, 0xD5DE, a)
            w(s, 0xD54C, rng.randrange(1, 8))
            w(s, 0xD558, rng.randrange(1, 12))
            call(s, 'iso_ai_turn')
            if s.side == 'o' and (sd < 3 or rd(s, 0xA08C + 0x28 * a)):
                assert getattr(s.e, 'div0_count', 0) >= 1, 'no division by zero'
            return [rd(s, x) for x in (0xD54C, 0xD5E0, 0xD5E2, 0xD56E, 0xD56C, 0x632E)]
        run(f'div0 ai_turn {sd}', ai, extra=stub_log, clock_step=10)
