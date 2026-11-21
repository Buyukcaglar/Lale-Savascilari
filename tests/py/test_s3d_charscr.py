"""3DSYS character screen (303F:4F3A s3d_character_screen,
src/game/s3d_charscr.c) against the original, in the uiworld world.

* Party: orig/GRP.DAT loaded into the name/stat records on both sides (the
  layout of cg_load_party, gameA.md 2.4) plus test inventories.
* Surfaces: 9 / 0Ah (cursor cache and save-under, allocated at start-up
  in the game), 4Ch (the screen's background, KARAKTER.CMP), 47h-4Ah (party
  portraits), 4Bh (item icons, ITEMBANK.CMP, from uiworld).
* The ISOSYS routines are stubbed on both sides (s3d_common.install_ext);
  the hook logs every call with its strings, the DGROUP selection words at
  iso_refresh_desc and a checksum of all EMS memory (surface 6, the
  composed screen, is an EMS block) at iso_show_picture (the only time the
  screen would reach the display), and implements iso_clamp_add.
* The window record's cursor-shape callback (16E0:9650) points at
  INTRFACE's ui_get_9671, the idle callback (964C) at ui_nop.
* Input: keys / mouse moves from a uiworld.Script; the 100 Hz clock
  advances 5 ms per poll (gr_fade, ui_wait_frames).
"""
import glob
import hashlib
import os
import struct

from lale import ROOT, check, lib
import helpers
import uiworld
from uiworld import run, fit, pic, ORIG_POLL_POINTS
from s3d_common import install_ext, uninstall_ext, Ptr

# pc_poll points of the port's busy loops (as in test_intrface.py): gr_fade's
# clock loop, ui_wait_frames, ui_wait_voice, and the character screen's
# button-release waits (after their gr_cursor_update)
for _pt in ((0x0C7F, 0x0854), (0x05DC, 0x1E81), (0x05DC, 0x1D87), (0x303F, 0x5E2F),
            (0x303F, 0x60E5), (0x303F, 0x73A6)):
    if _pt not in ORIG_POLL_POINTS:
        ORIG_POLL_POINTS.append(_pt)

RIX = open(glob.glob(os.path.join(ROOT, 'extracted', '*', 'XDINTRO.RIX'))[0], 'rb').read()
GRP = open(os.path.join(ROOT, 'orig', 'GRP.DAT'), 'rb').read()

ESC, F1, F2, F5, ENTER = 0x011B, 0x3B00, 0x3C00, 0x3F00, 0x1C0D
LEFT, RIGHT, SPACE, KEY_I, KEY_Q, KEY_V = 0x4B00, 0x4D00, 0x3920, 0x1769, 0x1071, 0x2F76

SURFS = uiworld.SURFS + [(9, 0x5A, 0x0F, 0, 'ITEMBANK.CMP'), (0x0A, 0x0F, 0x0F, 0, 'ITEMBANK.CMP'),
                         (0x4C, 320, 200, 1, 'KARAKTER.CMP')] + \
    [(0x46 + i, 32, 32, 1, f'IKON{i}.CMP') for i in range(1, 5)]


def setup_party(s, items=None, hands=None):
    """GRP.DAT into the records; items[p] = 10 inventory words, hands[p] =
    3 hand words."""
    for i in range(1, 5):
        rec = 0x9C26 + 0x94 * i
        s.dgw(rec, GRP[74 * (i - 1):74 * (i - 1) + 37])
        s.dgw(rec + 0x6F, GRP[74 * (i - 1) + 37:74 * i])
        st = bytearray(GRP[296 + 92 * (i - 1):296 + 92 * i])
        if items and i in items:
            st[0x46:0x5A] = struct.pack('<10h', *items[i])
        if hands and i in hands:
            st[0x3C:0x42] = struct.pack('<3h', *hands[i])
        s.dgw(0x99A6 + 0x5C * i, bytes(st))
        race = struct.unpack('<h', st[2:4])[0]
        s.dgw(rec + 0x4A, s.dg(0x2638 + 11 * race, 11))
    s.dgw(0x2898, struct.pack('<h', 1))
    # shape callback = ui_get_9671 (05DC:00D9)
    s.dgw(0x9650, struct.pack('<HH', 0x00D9, 0x05DC + 0x824))
    # idle callback (ui_poll_input) = ui_nop (05DC:00CD)
    s.dgw(0x964C, struct.pack('<HH', 0x00CD, 0x05DC + 0x824))


def ems_digest():
    """md5 of all EMS memory (the pc layer's; both sides' EMS heaps place
    blocks identically).  Surface 6 is an EMS block; reading it through the
    original's EMSLock would need a nested emulator call inside a stub."""
    h = hashlib.md5()
    for n in range(1, 16):
        h.update(helpers.ems_mem(n))
    return h.hexdigest()


class Hook:
    def __init__(self, vecize=0):
        self.log = []
        self.vecize = vecize

    def __call__(self, name, args, s):
        shown = []
        for a in args:
            if isinstance(a, Ptr):
                shown.append(a.i16() if name == 'iso_clamp_add' else a.cstr())
            else:
                shown.append(a)
        if name == 'iso_clamp_add':
            v = (shown[0] + args[1]) & 0xFFFF
            v = v - 0x10000 if v & 0x8000 else v
            v = max(args[2], min(args[3], v))
            args[0].set_i16(v)
        elif name == 'iso_show_picture':
            shown.append(ems_digest())
        elif name == 'iso_refresh_desc':
            shown.append(s.dg(0xD5E6, 8).hex())
        elif name == 'iso_cmd_vecize':
            s.dgw(0xD5E2, struct.pack('<h', self.vecize))
        elif name == 'iso_cast_vecize':
            s.dgw(0x631A, struct.pack('<h', 3))
            s.dgw(0xA510, b'bir vecize sonucu uzun bir metin olarak burada yazar\0')
        self.log.append((name, tuple(shown)))
        if name == 'iso_calc_stat_c':
            return 17
        if name == 'iso_calc_stat_b':
            return -4
        if name == 'iso_calc_ap_by_load':
            return 7
        return 0


def charscr(label, keys=(), moves=(), items=None, hands=None, cur=1, vecize=0, every=40,
            pre=None):
    logs = []
    keys = list(keys) + [ESC] * 3 if keys else keys

    def setup(s):
        # surface 52h = the .RIX blend table of gr_draw_strip (16E0:E52F)
        s.surf(0x52, 0x100, 0xC0, 1)
        s.write(0x52, (RIX + bytes(0xC000))[:0xC000])
        if s.side == 'o':
            s.c('ogr_mouse_init')
        else:
            lib.ogr_mouse_init()
        setup_party(s, items, hands)
        s.dgw(0x2898, struct.pack('<h', cur))
        if pre:
            pre(s)
        h = Hook(vecize)
        logs.append(h.log)
        install_ext(s, h)

    def op(s):
        if os.environ.get('S3D_VERBOSE'):
            print('   run', label, s.side, flush=True)
        try:
            if s.side == 'o':
                s.c('s3d_character_screen')
            else:
                lib.s3d_character_screen()
        finally:
            if s.side == 'c':
                uninstall_ext()

    # mouse x in the driver's 640-pixel range
    moves = [(t, 2 * x, y, b) for t, x, y, b in moves]
    run(label, op, keys=keys, moves=moves, setup=setup, surfs=SURFS, clock_step=5,
        every=every)
    lo, lc = logs
    if os.environ.get('S3D_VERBOSE'):
        print('   ', label, len(lo), sorted(set(n[4:] for n, _ in lo) - {'calc_stat_a', 'calc_stat_b', 'calc_stat_c', 'calc_weapon', 'calc_ap_by_load', 'draw_text_shadow', 'show_picture'}), [a for n, a in lo if n == 'iso_draw_textbox'], flush=True)
    if lo != lc:
        i = next((k for k in range(min(len(lo), len(lc))) if lo[k] != lc[k]), min(len(lo), len(lc)))
        check(False, f'{label}: ext calls differ at {i}: {lo[i:i + 2]} vs {lc[i:i + 2]} '
                     f'({len(lo)}/{len(lc)})')
    check(any(n == 'iso_show_picture' for n, _ in lo), f'{label}: screen never shown')
    return lo


ITEMS = {1: [101, 205, 0x1B, 0x22, 0x23, 0, 0x1A, 0x2B, 12, 0],
         2: [3, 0, 0, 0, 0, 0, 0, 0, 0, 0],
         3: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]}


def test_browse():
    """open, F2/F3 and portrait clicks between the members, Esc / exit button"""
    charscr('esc', keys=[ESC])
    charscr('F keys', keys=[F2, 0x3D00, F1, 0x3E00, ESC], items=ITEMS)
    charscr('member 3 dead', keys=[F2, 0x3D00, ESC], items=ITEMS,
            pre=lambda s: s.dgw(0x99A6 + 0x5C * 3 + 0x3A, struct.pack('<h', -6)))
    # mouse: portrait 2 (x 0BFh..0DFh), then the exit zone (8Ah..98h, 57h..66h)
    charscr('mouse', items=ITEMS, moves=[(60, 200, 30, 0), (70, 200, 30, 1), (80, 200, 30, 0),
                                         (200, 0x90, 0x5E, 0), (210, 0x90, 0x5E, 1),
                                         (220, 0x90, 0x5E, 0)])
    charscr('both buttons', items=ITEMS, moves=[(60, 10, 190, 3), (90, 10, 190, 0)])


def test_items_keyboard():
    """I picks up the last item (mouse off); arrows move it; Enter + arrows
    choose a hand; F2 gives it away; F5 throws it; Space uses it"""
    charscr('pick esc', keys=[KEY_I, ESC, ESC], items=ITEMS)
    charscr('pick move', keys=[KEY_I, LEFT, LEFT, RIGHT, ESC, ESC], items=ITEMS)
    charscr('equip hand 1', keys=[KEY_I, LEFT, LEFT, LEFT, ENTER, ENTER, ESC], items=ITEMS)
    charscr('equip hand 2', keys=[KEY_I, LEFT, ENTER, RIGHT, ENTER, ESC], items=ITEMS)
    charscr('equip hand 3', keys=[KEY_I, LEFT, LEFT, LEFT, LEFT, LEFT, LEFT, ENTER, RIGHT,
                                  RIGHT, ENTER, ESC], items=ITEMS)
    charscr('give', keys=[KEY_I, F2, KEY_I, F2, F1, ESC], items=ITEMS, every=60)
    charscr('give full', keys=[KEY_I, 0x3D00, ESC], items=ITEMS, every=60)
    charscr('bin yes', keys=[KEY_I, F5, LEFT, ESC], items=ITEMS, every=60)
    charscr('bin no', keys=[KEY_I, F5, RIGHT, ESC], items=ITEMS, every=60)
    charscr('use', keys=[KEY_I, LEFT, LEFT, LEFT, LEFT, LEFT, SPACE, ESC], items=ITEMS)
    charscr('in use', keys=[KEY_I, F2, ESC], items=ITEMS, hands={1: [0x2B, 0, 0]}, every=60)
    charscr('unequip', keys=[KEY_Q, 0x1177, 0x1265, ESC], items=ITEMS,
            hands={1: [0x23, 0x24, 0x25]})


def test_items_mouse():
    """left click on an item, then on a hand slot / portrait / bin / another
    slot; right click uses an item"""
    def click(x, y, t0, b=1):
        return [(t0, x, y, 0), (t0 + 10, x, y, b), (t0 + 20, x, y, 0)]
    slot = lambda n: (n - 1) * 16 + 8                       # noqa: E731
    ex = (0x90, 0x5E)
    charscr('hand 1', items=ITEMS, moves=click(slot(1), 8, 60) + click(0x30, 0x2D, 120)
            + click(*ex, 200))
    charscr('hand 2', items=ITEMS, moves=click(slot(7), 8, 60) + click(0x41, 0x2D, 120)
            + click(*ex, 200))
    charscr('portrait', items=ITEMS, moves=click(slot(2), 8, 60) + click(0xC8, 0x20, 120)
            + click(*ex, 200))
    charscr('other slot', items=ITEMS, moves=click(slot(2), 8, 60) + click(slot(9), 8, 120)
            + click(10, 190, 180, 2) + click(*ex, 260))
    charscr('bin', items=ITEMS, moves=click(slot(3), 8, 60) + click(0x130, 0x20, 120)
            + click(10, 190, 200) + click(*ex, 280))
    charscr('right use', items=ITEMS, moves=click(slot(5), 8, 60, 2) + click(*ex, 160))


def test_vecize():
    """V opens iso_cmd_vecize: none chosen, a battle vecize, one on the
    caster, one asking for a target (F3)"""
    charscr('vecize none', keys=[KEY_V, ESC], items=ITEMS)
    # mouse: the vecize button (zone 20) confines the mouse, a click in zone 14
    charscr('vecize mouse', items=ITEMS, moves=[(60, 0x90, 0x70, 0), (70, 0x90, 0x70, 1),
                                                (80, 0x90, 0x70, 0), (120, 0x50, 0xA0, 1),
                                                (130, 0x50, 0xA0, 0), (200, 0x90, 0x5E, 0),
                                                (210, 0x90, 0x5E, 1), (220, 0x90, 0x5E, 0)])
    from lale import DG
    seen = set()
    for n in range(1, 34):
        kind = struct.unpack('<h', bytes(DG[0x11FD + 2 * n:0x11FF + 2 * n]))[0]
        tgt = struct.unpack('<h', bytes(DG[0x1241 + 2 * n:0x1243 + 2 * n]))[0]
        if (kind, tgt) in seen:
            continue
        seen.add((kind, tgt))
        charscr(f'vecize {n} kind {kind}/{tgt}', keys=[KEY_V, 0x3D00, ESC], items=ITEMS,
                vecize=n, every=60)
