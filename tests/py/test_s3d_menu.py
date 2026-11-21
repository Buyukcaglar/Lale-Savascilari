"""3DSYS menus (303F:765C-8523, src/game/s3d_menu.c) against the original,
in the uiworld world with INTRFACE's window tables (test_intrface.setup_tables,
which also stubs the FRPOS/ISOSYS window callbacks on both sides).

* Other game modules: stubbed on both sides (s3d_common.install_ext); the
  hook logs every call.
* s3d_* routines of the other 3DSYS files run for real on both sides,
  except those a test puts in STAND: they are redirected to the hook on
  both sides (lale.patch / e.stub, table STANDINS).
* Window record: 9824 (test_intrface), with the cursor-shape callback
  (9650) = ui_get_9671 and the idle / 9654 callbacks = ui_nop.
* _dos_findfirst/_dos_findnext of the original are stubbed with the
  listing the port uses (sorted upper-case *.SAV of the game folder).
"""
import ctypes as C
import os
import shutil
import struct

from lale import ROOT, check, lib, patch, unpatch
import helpers
import uiworld
from uiworld import run, ORIG_POLL_POINTS
from s3d_common import install_ext, uninstall_ext, Ptr, s16
import test_intrface as ti

for _pt in ((0x0C7F, 0x0854), (0x05DC, 0x1E81), (0x05DC, 0x1D87)):
    if _pt not in ORIG_POLL_POINTS:
        ORIG_POLL_POINTS.append(_pt)

ESC, ENTER, DOWN, UP = 0x011B, 0x1C0D, 0x5000, 0x4800

# name, original address, argument kinds ('i' int, 'b' byte, 'p' far char *)
STANDINS = [
    ('s3d_redraw_view', '303F:07B6', 'i'),
    ('s3d_handle_key', '303F:0FFC', ''),
    ('s3d_load_district', '303F:1927', 'ib'),
    ('s3d_enter_view', '303F:2521', ''),
    ('s3d_redraw_screen', '303F:276C', ''),
    ('s3d_draw_party_panel', '303F:2C69', ''),
    ('s3d_advance_clock', '303F:39FD', 'i'),
    ('s3d_apply_daylight', '303F:3CCE', ''),
    ('s3d_draw_portrait_hl', '303F:75EF', ''),
    ('s3d_write_save', '303F:3DD0', 'pp'),
    ('s3d_read_save', '303F:3F95', 'p'),
    ('s3d_draw_status_line', '303F:4D22', 'ii'),
    ('s3d_make_random_phrase', '303F:4A27', 'i'),
]
SI_HOOK_T = C.CFUNCTYPE(C.c_int16, C.c_int16, C.c_int64, C.c_int64)
_keep = []
PHRASE = b'kar\x8d\x9f\x8dk bir g\x81r\x81lt\x81 duyuldu'


# the STANDINS a test replaces by the hook on both sides (default none: the
# real 3DSYS routines run on both sides)
STAND = set()


# always stood in: test_intrface stubs the thunk of the portrait blink
# (1670:0084, logged as 'shape') on the original side, so the real routine
# would only run on the port's
ALWAYS_STAND = {'s3d_draw_portrait_hl'}


def standin(i):
    return STANDINS[i][0] in STAND


class Hook:
    """the hook of both stub layers: log [(name, args)], answers from
    ret[name] (value or callable(args, s)), effects eff[name](args, s)"""
    def __init__(self, ret=None, eff=None):
        self.log = []
        self.ret = ret or {}
        self.eff = eff or {}

    # byte parameters (frpos.h) that s3d_common.S3D_EXT passes as words: the
    # original pushes them with whatever AH held
    BYTE_ARGS = {'fr_show_icon': (0, 1), 'fr_item_exchange': (0,), 'fr_run_event': (1,)}

    def __call__(self, name, args, s):
        for i in self.BYTE_ARGS.get(name, ()):
            if i < len(args) and not isinstance(args[i], Ptr):
                args[i] &= 0xFF
        shown = tuple(a.cstr() if isinstance(a, Ptr) else a for a in args)
        self.log.append((name,) + shown)
        if name in self.eff:
            self.eff[name](args, s)
        r = self.ret.get(name, 0)
        return r(args, s) if callable(r) else r


def install_standins(s, hook):
    """stub the s3d_* routines whose port stand-in is linked"""
    if s.side == 'c':
        for i, (name, _, kinds) in enumerate(STANDINS):
            if not standin(i):
                continue

            def cb(*raw, name=name, kinds=kinds):
                args = [Ptr('c', addr=raw[k] or 0) if c == 'p' else
                        (raw[k] & 0xFF if c == 'b' else s16(raw[k]))
                        for k, c in enumerate(kinds)]
                if name == 's3d_make_random_phrase':
                    hook(name, args, s)
                    return C.addressof(_phrase)
                return hook(name, args, s) or 0
            rt = C.c_void_p if name == 's3d_make_random_phrase' else C.c_int16
            patch(name, cb, rt, *[C.c_void_p if c == 'p' else
                                  (C.c_uint8 if c == 'b' else C.c_int16) for c in kinds])
        return
    e = s.e
    # 3DSYS calls demo_game_over through its overlay thunk 1682:002F, which
    # test_intrface stubs as 'over': route it to the hook like S3D_EXT's
    stub_once(e, '1682:002F', lambda e, a: ((hook('demo_game_over', [], s) or 0) & 0xFFFF, 0))
    phrase_seg = e.alloc(0x80)
    e.wr(phrase_seg, 0, PHRASE + b'\0', runtime=True)
    for i, (name, addr, kinds) in enumerate(STANDINS):
        if not standin(i):
            continue

        def fn(e, a, name=name, kinds=kinds):
            args, k = [], 0
            for c in kinds:
                if c == 'p':
                    args.append(Ptr('o', e, a(k + 1), a(k)))
                    k += 2
                else:
                    args.append(a(k) & 0xFF if c == 'b' else s16(a(k)))
                    k += 1
            r = hook(name, args, s) or 0
            if name == 's3d_make_random_phrase':
                return 0, phrase_seg
            return r & 0xFFFF, 0
        stub_once(e, addr, fn)
        if name == 's3d_draw_portrait_hl':
            stub_once(e, '1670:0084', fn)    # its thunk (gr_call_then_cursor)


def stub_once(e, addr, fn):
    """e.stub, but an address stubbed before (test_intrface's callback
    stubs) only gets its routine replaced: a second code hook at the same
    address would run the stub twice"""
    from emu86 import parse
    sg, of = parse(addr)
    lin = e.lin(sg, of)
    if lin in e._stubs:
        e._stubs[lin] = fn
    else:
        e.stub(addr, fn)


def uninstall():
    uninstall_ext()
    for name, _, _ in STANDINS:
        unpatch(name)


def w16(s, off, v):
    s.dgw(off, struct.pack('<h', s16(v)))


def r16(s, off):
    return struct.unpack('<h', s.dg(off, 2))[0]


def far(s, seg, off):
    return struct.pack('<HH', off, seg + 0x824)


SURFS = ti.PANEL_SURFS + [(0x50, 320, 200, 1, 'HAR4.CMP')]


def world_setup(s, hook, pre=None):
    ti.setup_tables(s, 0x9824)
    s.dgw(0x9650, far(s, 0x05DC, 0x00D9))            # shape: ui_get_9671
    s.dgw(0x964C, far(s, 0x05DC, 0x00CD))            # idle: ui_nop
    s.dgw(0x9654, far(s, 0x05DC, 0x00CD))
    install_ext(s, hook)
    install_standins(s, hook)
    if pre:
        pre(s)


def mrun(label, op, hook_f, pre=None, stand=(), **kw):
    """run op on both sides; `stand`: STANDINS names replaced by the hook"""
    hooks = []
    added = (set(stand) | ALWAYS_STAND) - STAND
    STAND.update(added)

    def setup(s):
        h = hook_f()
        hooks.append(h)
        world_setup(s, h, pre)

    try:
        res = run(label, op, setup=setup, surfs=kw.pop('surfs', SURFS), every=kw.pop('every', 30),
                  clock_step=kw.pop('clock_step', 100), extra=lambda s: list(ti.LOG), **kw)
    finally:
        uninstall()
        ti.LOG.clear()
        STAND.difference_update(added)
    lo, lc = hooks[0].log, hooks[1].log
    if lo != lc:
        i = next((k for k, (a, b) in enumerate(zip(lo, lc)) if a != b), min(len(lo), len(lc)))
        check(False, f'{label}: calls differ at {i}: {lo[max(0, i - 3):i + 6]} vs {lc[max(0, i - 3):i + 6]}')
    return res, lo


def cfn(s, name, *args, restype=C.c_int16, argtypes=()):
    if s.side == 'o':
        return s.c(name, *args)[0]
    f = getattr(lib, name)
    f.restype = restype
    f.argtypes = argtypes
    return f(*args)


# ---- s3d_apply_sound_options ---------------------------------------------------
def test_apply_sound_options():
    """random fade counters / steps / clock, music 1 and others, sound off"""
    import random

    def op(s):
        r = random.Random(0x837A)
        out = []
        for n in range(300):
            w16(s, 0x3D6E, r.choice([0, 0, r.randrange(-40, 40)]))
            w16(s, 0x3D70, r.choice([0, r.randrange(-40, 40)]))
            w16(s, 0x3D72, r.choice([0, r.randrange(-40, 40)]))
            s.dgw(0x3D74, bytes([r.randrange(256), r.randrange(256), r.randrange(256)]))
            w16(s, 0xBA82, r.randrange(0x10000))
            w16(s, 0xBB4D, r.choice([1, 1, 2]))
            w16(s, 0x731C, r.randrange(-100, 100))
            s.dgw(0x7318, struct.pack('<hh', r.randrange(0, 64), r.randrange(0, 64)))
            s.dgw(0xE74A, struct.pack('<I', r.randrange(0, 0x30000)))
            if s.side == 'o':
                s.c('s3d_apply_sound_options', 0, 0)
            else:
                lib.s3d_apply_sound_options()
            out.append(s.dg(0x3D6E, 10) + s.dg(0x7318, 6) + s.dg(0xBA82, 2))
        return out
    mrun('apply_sound_options', op, Hook)


# ---- s3d_options_menu -----------------------------------------------------------
def options_op(s):
    cfn(s, 's3d_options_menu', 0, argtypes=(C.c_uint8,))
    return s.dg(0xBB33, 8), r16(s, 0x6D74), s.dg(0x3B08, 1), r16(s, 0x3AFB), r16(s, 0x3AFD)


def test_options_menu():
    """every entry once (and the battle speed around), small and big view"""
    for big in (0, 1):
        def pre(s, big=big):
            s.dgw(0xBB34, bytes([1, 8, 1, 0, 1, 1, big]))
            w16(s, 0xD5DC, 1)
            w16(s, 0x3AFB, 200 if big else 100)
            w16(s, 0x3AFD, 200 if big else 100)
            w16(s, 0xBB4D, 1)
        keys = [ENTER, DOWN, ENTER, ENTER, ENTER, DOWN, ENTER, DOWN, ENTER, DOWN, ENTER,
                DOWN, ENTER, ESC]
        mrun(f'options entries big={big}', options_op, Hook, pre=pre, keys=keys)
        mrun(f'options view size big={big}', options_op, Hook, pre=pre,
             keys=[DOWN] * 6 + [ENTER, ENTER, ESC])


# ---- s3d_save_load_menu ---------------------------------------------------------
SAVE_DIR = os.path.join(ROOT, 'out', 'wp16_savetest')
c_set_dir = helpers.c_set_dir


def make_save_dir(files):
    shutil.rmtree(SAVE_DIR, ignore_errors=True)
    os.makedirs(SAVE_DIR)
    base = open(os.path.join(ROOT, 'orig', 'SAKLA001.SAV'), 'rb').read()
    for name, desc in files:
        d = (desc + b'\0' * 60)[:60]
        open(os.path.join(SAVE_DIR, name), 'wb').write(d + base[60:])
    open(os.path.join(SAVE_DIR, 'notes.txt'), 'wb').write(b'x')


def listing():
    out = []
    for n in os.listdir(SAVE_DIR):
        u = n.upper()
        b, _, ext = u.rpartition('.')
        if b and len(b) <= 8 and ext == 'SAV':
            out.append(u)
    return sorted(out)


def save_pre(s):
    if s.side == 'c':
        c_set_dir(SAVE_DIR.encode())
        return
    e = s.e
    state = {'names': [], 'pos': 0}

    def fill(seg, off):
        if state['pos'] >= len(state['names']):
            return 0x12
        blk = bytearray(0x2B)
        blk[0x15] = 0x20
        nm = state['names'][state['pos']].encode()
        blk[0x1E:0x1E + len(nm)] = nm
        state['pos'] += 1
        e.wr(seg, off, bytes(blk), runtime=True)
        return 0

    def ff(e, a):
        state['names'], state['pos'] = listing(), 0
        return fill(a(4), a(3)), 0
    e.stub('0000:112F', ff)
    e.stub('0000:1162', lambda e, a: (fill(a(1), a(0)), 0))


def save_op(mode):
    def op(s):
        old = list(helpers.SEARCH)
        helpers.SEARCH.insert(0, SAVE_DIR)
        try:
            r = cfn(s, 's3d_save_load_menu', mode, restype=C.c_uint16, argtypes=(C.c_int16,))
        finally:
            helpers.SEARCH[:] = old
        # freed records (type FFh) keep the old block's handle/offset words
        # in the original; the port's EMS handle numbers differ (memman's
        # business, not the menu's): cleared on both sides
        for i in range(110):
            rec = 0xDA48 + 9 * i
            if s.dg(rec + 8, 1)[0] == 0xFF:
                s.dgw(rec, bytes(4))
        return r & 0xFF, r16(s, 0x3F12), r16(s, 0xD5DC), s.dg(0xA63C, 0x64 * 4)
    return op


# the menu's own work; reading/writing the save file is test_s3d_save's
SAVE_STAND = {'s3d_read_save', 's3d_write_save'}


def test_save_load_menu():
    files = [('SAKLA001.SAV', b'c1'), ('sakla003.sav', b'kale kap\x8ds\x8d'), ('ZZ.SAV', b'son')]
    make_save_dir(files)
    try:
        r, log = mrun('load first', save_op(0), Hook, pre=save_pre, stand=SAVE_STAND, keys=[ENTER])
        check(('s3d_read_save', b'SAKLA001.SAV') in log, f'load first: {log}')
        mrun('load third', save_op(0), Hook, pre=save_pre, stand=SAVE_STAND, keys=[DOWN, DOWN, ENTER])
        mrun('load cancel', save_op(0), Hook, pre=save_pre, stand=SAVE_STAND, keys=[DOWN, ESC])
        # save into the free slot: name sakla004.sav, typed description
        r, log = mrun('save new', save_op(1), Hook, pre=save_pre, stand=SAVE_STAND,
                      keys=[DOWN, DOWN, DOWN, ENTER, 0x1E61, 0x3062, ENTER, ENTER])
        check(any(x[0] == 's3d_write_save' and x[1] == b'sakla004.sav' for x in log), f'save new: {log}')
        mrun('save over', save_op(1), Hook, pre=save_pre, stand=SAVE_STAND, keys=[DOWN, ENTER, 0x1E61, ENTER, ENTER])
        mrun('save typed cancel', save_op(1), Hook, pre=save_pre, stand=SAVE_STAND, keys=[ENTER, ESC, ENTER])
        make_save_dir([])
        mrun('load none', save_op(0), Hook, pre=save_pre, stand=SAVE_STAND, keys=[ENTER])
        mrun('save none', save_op(1), Hook, pre=save_pre, stand=SAVE_STAND, keys=[ENTER, 0x1E61, ENTER, ENTER])
    finally:
        c_set_dir(os.path.join(ROOT, 'orig').encode())


# ---- s3d_leave_district ------------------------------------------------------------
def leave_pre(district, music):
    def pre(s):
        w16(s, 0xBB6B, district)
        w16(s, 0x731A, music)
        w16(s, 0x3D68, 349)
        # no district videos (the random draws still happen)
        for d in range(18):
            s.dgw(0x2E31 + 0x1D * d + 8, bytes(16))
        # the map's block (E33F), freed by ctl_free_checked
        if s.side == 'o':
            ax, dx = s.c('mm_alloc', 0x100, 0, 1)
            s.dgw(0xE33F, struct.pack('<HH', ax, dx))
        else:
            p = uiworld.c_mm_alloc(0x100, 1)
            s.dgw(0xE33F, struct.pack('<HH', p - uiworld.DGA, 0x1F04))
    return pre


def leave_op(s):
    cfn(s, 's3d_leave_district', restype=None)
    return r16(s, 0xBB6B), r16(s, 0xBBA8), r16(s, 0xB9C8), r16(s, 0xBB4D)


# loading / entering the new district is s3d_district's (test_s3d_district)
LEAVE_STAND = {'s3d_load_district', 's3d_enter_view'}


def test_leave_district():
    """no; yes -> city map returns a district / 0 (stay); music on/off"""
    _, log = mrun('leave no', leave_op, Hook, pre=leave_pre(3, 0), stand=LEAVE_STAND, keys=[0x2368])
    check(not any(x[0] == 'demo_city_map_travel' for x in log), f'leave no: {log}')
    for ret, music in ((5, 0), (0, 0), (9, 40)):
        _, log = mrun(f'leave yes ->{ret} music={music}', leave_op,
                      lambda ret=ret: Hook(ret={'demo_city_map_travel': ret}),
                      pre=leave_pre(3, music), stand=LEAVE_STAND, keys=[0x1265])
        # a new district is entered as first visit (1), staying reloads with 0
        check(('s3d_load_district', ret - 1 if ret else 3, 1 if ret else 0) in log,
              f'leave yes {ret}: {log}')
