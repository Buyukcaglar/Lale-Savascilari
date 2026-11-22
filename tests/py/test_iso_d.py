"""ISOSYS.L (ovl4595) gap tests, in the world of tests/py/test_iso_b.py /
test_iso_c.py (which this file imports - the four ISOSYS files are always
built into one DLL):

 1. a door wall without a door picture (terrain 14): what the original
    does (fatal 10h in MEMMAN's mm_map, or - with 2632h = 1 - wall picture
    7) and the same in the port; the shipped maps cannot produce it
    (test_door14_game_data);
 2. Ctrl-Break in the battle's key loop;
 3. the branches that only run with sound on: battle sounds, voices with
    the talking face, the battle music.
"""
import ctypes
import os
import random
import struct
import subprocess
import sys

import helpers
import uiworld
from lale import ROOT, check, fn, lib, DG, NAMES, patch, unpatch

C = ctypes
i16, u16, vp, cp = C.c_int16, C.c_uint16, C.c_void_p, C.c_char_p


class Stop(Exception):
    """the original reached o559A_fatal (which never returns in the game)"""


def _B():
    import test_iso_b
    return test_iso_b


def emu_str(e, seg, off):
    s = bytearray()
    while True:
        b = e.rd(seg, off + len(s), 1, runtime=True)[0]
        if not b:
            return bytes(s)
        s.append(b)


def freeze(s, slots=()):
    """keep the state of this moment as what uiworld.run compares"""
    snap = s.snap()
    dg = bytearray(snap['dg'])
    for i in range(110):                     # as test_iso_b.run / test_iso_c.battle_run do
        rec = 0xDA48 + 9 * i                 # after op: the stale far pointer of freed
        if dg[rec + 8] in (1, 0xFF):         # records, the segment word of EMS ones
            dg[rec + 2:rec + 4] = bytes(2)
        if dg[rec + 8] == 0xFF:
            dg[rec:rec + 4] = bytes(4)
    snap['dg'] = bytes(dg)
    surf = {n: s.read(n) for n in slots}
    s.snap = lambda: snap
    s.read = lambda n: surf[n]


def catch_fatal(s, got, slots=(), stop=True, after=None):
    """o559A_fatal on both sides: its arguments are noted in `got` and the
    state of that moment frozen for the comparison.  The original stops
    there (stop=True); the port's test build returns from it (the game
    exits), `after` runs before it does."""
    def note(file, line, msg, code):
        got.append((file, line, msg, code))
        if len(got) == 1:
            freeze(s, slots)
        if after and s.side == 'c':
            after(s)
    if s.side == 'o':
        def fatal(e, a):
            note(emu_str(e, a(1), a(0)), a(2),
                 emu_str(e, a(4), a(3)) if a(3) | a(4) else None, helpers.s16(a(5)))
            if stop:
                e._error = Stop()
                e.u.emu_stop()
            return (0, 0)
        s.e._stubs[s.e.lin('16C1:002A')] = fatal     # replaces make_emu's recorder
    else:
        patch('o559A_fatal', lambda f, line, m, code: note(f, line, m, code), None,
              cp, i16, cp, i16)


# ======================================================================================
# 1. door wall without a door picture
def test_door14_game_data():
    """The 18 shipped maps (FRPOS.CKD PC1..PC18).  iso_build_battlefield
    makes a door wall (terrain door + 9) from a wall with kind 2 whose wall
    set (map header bytes 3..6 -> 9F10..) is 8; door = 5 (-> terrain 14)
    when no set is 2 or 3.  No shipped map has both: the four maps without
    a set 2/3 (PC13, 14, 16, 18) have no set 8 either, every wall byte is
    0..4 (0 reads 9F0Eh, which nothing writes; 3DSYS's s3d_scan_cells keeps
    them in 1..4), and the FRPOS scripts cannot change walls or sets."""
    door5 = []
    for n in range(1, 19):
        d = open(os.path.join(ROOT, 'extracted', 'FRPOS', f'PC{n}'), 'rb').read()
        w_, h_, sets, cells = d[20], d[21], d[23:27], d[0x2B:]
        check(len(cells) == w_ * h_ * 15, f'PC{n}: {len(cells)} cell bytes for {w_}x{h_}')
        door = 5
        for i in range(4):
            if sets[i] == 3:
                door = i + 1
                break
            if sets[i] == 2:
                door = i + 1
        walls = {cells[k * 15 + 2 * sd] for k in range(w_ * h_) for sd in range(4)}
        check(walls <= {0, 1, 2, 3, 4}, f'PC{n}: wall bytes {sorted(walls)}')
        doors = sum(1 for k in range(w_ * h_) for sd in range(4)
                    if cells[k * 15 + 2 * sd + 1] == 2 and 1 <= cells[k * 15 + 2 * sd] <= 4
                    and sets[cells[k * 15 + 2 * sd] - 1] == 8)
        if door == 5:
            door5.append(n)
            check(8 not in sets and doors == 0, f'PC{n}: sets {list(sets)}, {doors} door walls '
                  'with door 5 -> terrain 14 occurs in the game data')
    check(door5 == [13, 14, 16, 18], f'maps without a wall set 2/3: {door5}')


def door14_world(s, alive0, cells):
    B = _B()
    rng = random.Random(5)
    B.battle(s, rng, terrain=True)
    B.in_window(s)
    B.w(s, 0xD580, 4)                        # view origin
    B.w(s, 0xD57E, 4)
    for x in range(4, 11):                   # no other wall in view
        for y in range(4, 11):
            if B.rd(s, 0xC8EE + 0x38 * x + 2 * y) > 9:
                B.w(s, 0xC8EE + 0x38 * x + 2 * y, 3)
    for x, y in cells:
        B.w(s, 0xC8EE + 0x38 * x + 2 * y, 14)
    B.w(s, 0x2632, alive0)
    B.w(s, 0xD5DE, 25)


def test_door14_fatal():
    """terrain 14 with 2632h = 0 (its value unless a living unit of side 0
    was seen by iso_next_turn & co.): iso_redraw draws bar picture 0 =
    sys_blit from x = FFCEh of yericon (650x40, EMS): past the block, so
    EMSLockRegion gives NULL and mm_map prints the page map, waits for a
    key and ends the game with internal error 10h (MEMMAN.L line 2ECh).
    Nothing of the cell is drawn.  Same on both sides: the arguments of
    the fatal call, the key consumed, DGROUP, screen and work screen at
    that moment."""
    B = _B()
    res = {}

    def op(s):
        door14_world(s, 0, [(7, 7)])
        got = res[s.side] = []
        text = []
        catch_fatal(s, got, slots=(6,))
        if s.side == 'o':                    # printf: INT 21h AH=40h
            def dos_write(e, uc):
                text.append(e.rd(e.reg('DS'), e.reg('DX'), e.reg('CX'), runtime=True))
                uc.reg_write(helpers.X.UC_X86_REG_AX, e.reg('CX'))
                e._cf(uc)
                return True
            s.e.int_hooks[(0x21, 0x40)] = dos_write
        helpers_keys_before = uiworld.pc_kbhit()
        try:
            B.call(s, 'iso_redraw')
        except Stop:
            pass
        except OSError:
            # port, test build only: o559A_fatal returned and mm_map went on
            # with the NULL mapping
            check(s.side == 'c', 'the original ran into an invalid address')
        if s.side == 'o':
            check(b''.join(text) == b'1: 0, 0, 0, 0\r\n', f'door14: the original printed {text}')
        return got[:1], helpers_keys_before, uiworld.pc_kbhit()
    # EA72: the last character of the RTL's fputc (the page map's newline);
    # MEMMAN's printf is the host's in the port
    r = B.run('iso door14 fatal', op, slots=(6,), clock_step=10, keys=[0x1C0D], ignore=[0xEA72])
    check(r[0][0] == [(b'T:ALTDUZEY\\MEMMAN.L', 0x2EC, None, 0x10)], f'door14: fatal {r[0][0]}')


def test_door14_blit():
    """terrain 14 with 2632h = 1: the floor is bar picture 1, the wall top
    picture t + 6 = 20 -> wall picture 7 of the 6 in slot 18 (300x60): x =
    300, i.e. picture 1 (duvarica) one row down, and for the last row the
    50 bytes behind the block in the EMS heap.  Cells in the top row (cut
    at y < 0), in the middle and at the right edge of the view; the cursor
    behind one of them (picture 15 instead)."""
    B = _B()

    def op(s):
        door14_world(s, 1, [(5, 4), (9, 4), (7, 7), (10, 8), (6, 10)])
        r = []
        for cur in ((0, 0, 0), (1, 8, 6), (1, 10, 8)):
            B.w(s, 0xD57C, cur[0])
            B.w(s, 0xD57A, cur[1])
            B.w(s, 0xD578, cur[2])
            B.call(s, 'iso_redraw')
            r.append(s.read(6))
        # the bytes behind the walls' block (the last row of picture 7)
        h = struct.unpack('<H', s.dg(s.rec(0x12), 2))[0]
        if s.side == 'o':
            ax, dx = s.e.call('1154:0688', 50, *uiworld.L(0x4650), h)
            r.append(s.e.rd(dx, ax, 50, runtime=True))
        else:
            r.append(C.string_at(uiworld.c_EMSLock(h, 0x4650, 50), 50))
        return r
    B.run('iso door14 blit', op, slots=(6,), clock_step=10)


def _door14_child():
    """port only, the game's own o559A_fatal (not the test build's
    returning one): iso_redraw of a terrain-14 cell"""
    B = _B()
    uiworld.mount = lambda: helpers.c_mount(b'3D.CKD', b'IVIR.CKD', b'SAVAS.CKD', b'IKON.CKD',
                                            b'FRPOS.CKD')
    helpers.SEARCH.extend(B.SEARCH)
    s = uiworld.world('c', surfs=B.SURFS_B)
    B.battle_gfx(s)
    door14_world(s, 0, [(7, 7)])
    uiworld.pc_key_push(0x1C0D)
    sys.stdout.flush()
    C.c_int16.in_dll(lib, 'o559A_test_mode').value = 0
    B.call(s, 'iso_redraw')
    print('RETURNED', flush=True)
    os._exit(3)


def test_door14_port_exits():
    """the port as the game runs it (o559A_test_mode 0): the terrain-14
    redraw ends the process through o559A_fatal - exit(0) with the
    internal-error text - instead of crashing"""
    env = dict(os.environ, LALE_TESTLIB=lib._name, PYTHONPATH=os.path.dirname(__file__))
    p = subprocess.run([sys.executable, '-u', os.path.abspath(__file__), 'door14-child'], env=env,
                       capture_output=True, timeout=600)
    check(p.returncode == 0, f'door14 child: exit code {p.returncode}\n{p.stderr[-600:]!r}')
    check(b'RETURNED' not in p.stdout, 'door14 child: iso_redraw returned')
    check(b'1: 0, 0, 0, 0' in p.stdout, f'door14 child: page map line missing: {p.stdout[-200:]!r}')
    check(b'Oyunda 16 numaral' in p.stderr and b'MEMMAN.L->748' in p.stderr,
          f'door14 child: message {p.stderr[-300:]!r}')


# ======================================================================================
# 2. Ctrl-Break
def test_ctrl_break_pieces():
    """the pieces outside ISOSYS the battle's Ctrl-Break handling rests on,
    each against the original's code:
     - Borland's bioskey(1) (0000:1F3F) on the word the BIOS stores for
       Ctrl-Break (0000h, ZF clear): FFFFh - as the port's pc_bioskey;
     - NKONTROL's INT 23h handler (0DB7:0135, installed by ctl_install with
       setvect(23h)): o559A_fatal(NKONTROL.L, 16Fh, text, -1), the text by
       750Bh (1 after a trapped Ctrl+Alt+Del)."""
    X = helpers.X
    e = helpers.make_emu()
    for word, zf in ((0x0000, 0), (0x1C0D, 0), (0x0000, 1), (0xFF00, 0)):
        def int16(e, uc, word=word, zf=zf):
            uc.reg_write(X.UC_X86_REG_AX, word)
            fl = uc.reg_read(X.UC_X86_REG_EFLAGS)
            uc.reg_write(X.UC_X86_REG_EFLAGS, fl | 0x40 if zf else fl & ~0x40)
            return True
        e.int_hooks[(0x16, 1)] = int16
        ro = e.call('0000:1F3F', 1)[0]
        uiworld.pc_reset()
        if not zf:
            uiworld.pc_key_push(word)
        rc = fn('pc_bioskey', i16, i16)(1) & 0xFFFF
        check(ro == rc, f'bioskey(1) on {word:04X} zf={zf}: {ro:04X} vs {rc:04X}')
        check(ro == (0 if zf else word or 0xFFFF), f'bioskey(1) on {word:04X} zf={zf}: {ro:04X}')
    uiworld.pc_reset()
    texts = {0: b'Tamam abi breyk, breyk, sitop !!!',
             1: b'H\x8dmm, birisi bilgisayar\x8d resetlemeye \x87al\x8d\x9ft\x8d galiba!'}
    for reset in (0, 1):
        go, gc = [], []
        e.wr(0x16E0, 0x750B, bytes([reset]))
        e._stubs[e.lin('16C1:002A')] = lambda e, a: go.append(
            (emu_str(e, a(1), a(0)), a(2), emu_str(e, a(4), a(3)), helpers.s16(a(5)))) or (0, 0)
        e.call('0DB7:0135', 0x0202)                      # IRET: a flags word
        DG[0x750B] = reset
        patch('o559A_fatal', lambda f, line, m, code: gc.append((f, line, m, code)), None,
              cp, i16, cp, i16)
        try:
            fn('ctl_int23_isr', None)()
        finally:
            unpatch()
        check(go == gc == [(b'ALTDUZEY\\NKONTROL.L', 0x16F, texts[reset], -1)],
              f'INT 23h handler, 750B={reset}: {go} vs {gc}')


def _Cc():
    import test_iso_c
    return test_iso_c


def battle2(label, op_battle, keys, seed=1, ai_kill=1, idle=20, stub=True, sound=0, setup=None,
            extra=None, ignore=(), cap=150, surfs=None, **world):
    """test_iso_c.battle_run with the battle call itself (op_battle(s, fd))
    and the set-up left to the caller"""
    Cc = _Cc()

    def op(s):
        fd = Cc.FEEDER[0] = Cc.Feeder(keys, idle, cap)
        fd.start(s)
        Cc.battle_world(s, random.Random(seed), **world)
        r = op_battle(s, fd)
        for i in range(110):                 # as battle_run: stale far pointers of freed records
            rec = 0xDA48 + 9 * i
            if s.dg(rec + 8, 1)[0] == 0xFF:
                s.dgw(rec, bytes(4))
            elif s.dg(rec + 8, 1)[0] == 1:   # as test_iso_b.run: an EMS record's stale
                s.dgw(rec + 2, bytes(2))     # segment word (a block made EMS)
        return r

    def setup2(s):
        Cc.setup_stubs(s, ai_kill=ai_kill, stub=stub)
        if setup:
            setup(s)
    old = uiworld.mount
    uiworld.mount = lambda: helpers.c_mount(b'3D.CKD', b'IVIR.CKD', b'SAVAS.CKD', b'SOUNDS.CKD')
    pump = uiworld.Script._pump
    uiworld.Script._pump = lambda sc: (pump(sc), Cc.FEEDER[0].poll())
    dirs = [Cc.SAVAS_DIR, os.path.join(ROOT, 'extracted', 'SOUNDS')]
    helpers.SEARCH[:0] = dirs
    try:
        return Cc.srun(label, op, setup=setup2, surfs=surfs or Cc.PANEL_SURFS, clock_step=1,
                       sound=sound,
                       extra=extra,
                       ignore=list(range(0xB9EC, 0xB9F0)) + list(range(0x9662, 0x9666))
                       + [0xD534, 0xD535] + list(ignore))
    finally:
        uiworld.mount = old
        uiworld.Script._pump = pump
        for d in dirs:
            helpers.SEARCH.remove(d)


def run_battle(s, fd):
    if s.side == 'o':
        s.e.call(NAMES['iso_battle'], max_insns=4_000_000_000)
    else:
        _Cc().call(s, 'iso_battle')


BREAK = 0x0000          # the word the BIOS stores on Ctrl-Break


def test_ctrl_break_battle_dos():
    """Ctrl-Break while the battle waits for input, with DOS's part modelled
    on the original's side: the 0000h word comes with INT 1Bh (break
    pending), and the next kbhit() (INT 21h AH=0Bh) enters the INT 23h
    vector - the original's handler 0DB7:0135, run from the kbhit of
    ctl_flush_keys - which ends the game in o559A_fatal.  The port must
    reach the same fatal call with the same state: in the main key loop, in
    the target cursor, in both inventory loops."""
    Cc = _Cc()
    K, ENTER = Cc.K, Cc.ENTER
    bios1 = fn('pc_bioskey', i16, i16)
    for name, keys in (('main', [BREAK]), ('target', [ENTER, BREAK]),
                       ('inventory', [K['i'], BREAK]), ('slot', [K['i'], ENTER, BREAK])):
        def op(s, fd, name=name):
            got, int23 = [], []
            if s.side == 'o':
                e = s.e
                isr = [(0x0DB7 + helpers.LOAD) & 0xFFFF, 0x0135]

                def kbhit(e, a):
                    # DOS: a pending break -> INT 23h; the handler never
                    # returns (fatal), so it is entered by replacing kbhit's
                    # return address
                    if bios1(1) == -1:
                        ss, sp = e.reg('SS'), e.reg('SP')
                        e.u.mem_write(ss * 16 + sp, struct.pack('<HH', isr[1], isr[0]))
                        int23.append(1)
                        return (0, 0)
                    return (uiworld.pc_kbhit() & 0xFFFF, 0)
                e._stubs[e.lin('0000:2E45')] = kbhit
            # the port goes on after its (test build) fatal: end the battle
            catch_fatal(s, got, after=lambda s: s.dgw(0x632E, bytes((1, 0))))
            try:
                run_battle(s, fd)
            except Stop:
                pass
            return got[:1], int23 == [1] or s.side == 'c'
        r = battle2(f'iso ctrl-break {name}', op, keys, seed=30, groups=Cc.GROUPS3, inv=Cc.INV,
                    terrain=0)
        check(r[0] == ([(b'ALTDUZEY\\NKONTROL.L', 0x16F, b'Tamam abi breyk, breyk, sitop !!!',
                         -1)], True), f'ctrl-break {name}: {r[0]}')


def test_ctrl_break_battle_code():
    """the same key loops as plain code, without DOS's break (the
    original's INT 1Bh at C022 does nothing here, the port's fatal call
    returns): after the 0000h word the main loop flushes, waits for one
    more key (getch) and goes on with scan code FFh; the target cursor and
    the inventory loops flush and go on.  A whole battle, everything
    compared at its end."""
    Cc = _Cc()
    K, ENTER, ESC, TAB, KP = Cc.K, Cc.ENTER, Cc.ESC, Cc.TAB, Cc.KP
    keys = [BREAK, KP['8'], ENTER, BREAK, ESC, K['i'], BREAK, ENTER, BREAK, ESC, KP['8'], TAB,
            BREAK, TAB, K['i'], BREAK, ESC, TAB]
    n = {}

    def op(s, fd):
        got = n[s.side] = []
        if s.side == 'o':
            for ah in range(256):
                s.e.int_hooks[(0x1B, ah)] = lambda e, uc: got.append(0x1B) or True
        else:
            patch('o559A_fatal', lambda f, line, m, code: got.append(line), None,
                  cp, i16, cp, i16)
        run_battle(s, fd)
        return fd.fed <= fd.cap + 2
    battle2('iso ctrl-break code', op, keys, seed=31, groups=Cc.GROUPS3, inv=Cc.INV, terrain=0)
    check(n['o'] == [0x1B] * 2, f'ctrl-break code: INT 1Bh of the original {n["o"]}')
    check(n['c'] == [0x16F] * 6, f'ctrl-break code: fatal calls of the port {n["c"]}')


# ======================================================================================
# 3. sound on
def sound_on(s, step=3000, music=False):
    """Sound on (73D0 by the world), 8 channels, s3d option 4 (BB38).  The
    CDI calls of both sides go to s.snd in one format (test_sound.stub_cdi
    on the original's CDI wrappers, a registered ScriptDevice in the port);
    getPosition is scripted: `step` more with every call since the last
    playSample.  music: demo_play_music (sets the track BB4D and a module
    BB75), ampStopModule, ampFreeModule, MemFree are replaced by recorders
    on both sides, ampSetTrackVolume is recorded."""
    import test_sound as TS
    snd = s.snd = []

    def getpos(ch):
        n = 1
        for ev in reversed(snd):
            if ev[0] == 0x04:
                break
            n += ev[0] == 0x48
        snd.append((0x48, ch, 0, 0))
        return n * step
    s.dgw(0x73D7, b'\x08')
    s.dgw(0xBB38, b'\x01')
    if s.side == 'o':
        s.e.cdi = snd
        s.c('cdiInit')                       # as the port below (oses_init's)
        TS.stub_cdi(s.e, getpos)
    else:
        TS.c_nses_reset()
        TS.c_cdiInit()
        s.dev = TS.ScriptDevice(getpos, snd)
        check(s.dev.register() == 0, 'cdiRegister')
    if not music:
        return

    def play(a, b, c_):
        snd.append(('music', a, b, c_))
        s.dgw(0xBB4D, struct.pack('<h', a))
        if s.side == 'o':
            s.dgw(0xBB75, struct.pack('<HH', 8, 0x7000))
        else:
            s.module = C.create_string_buffer(64)
            vp.in_dll(lib, 'g_music_module').value = C.addressof(s.module)

    class Vol(list):
        def append(self, tv):
            snd.append(('vol', tv[0] & 0xFFFFFFFF, tv[1] & 0xFFFFFFFF))
    if s.side == 'o':
        s.vol = Vol()
        s.e.stub('1682:0043', lambda e, a: (play(*[helpers.s16(a(i)) for i in range(3)]), 0)[1:]
                 + (0,))
        TS.pstub(s.e, '0EF6:1296', 0, lambda w: snd.append(('stop',)))
        TS.pstub(s.e, '111B:0003', 2, lambda w: snd.append(('free', (w[0] | w[1]) != 0)))
        TS.pstub(s.e, '0E9E:00DA', 2, lambda w: snd.append(('memfree', (w[0] | w[1]) != 0)))
    else:
        i32 = C.c_int32
        patch('demo_play_music', lambda a, b, c_: play(a, b, c_), None, i16, i16, i16)
        patch('ampSetTrackVolume', lambda t, v: Vol().append((t, v)) or 0, i16, i32, i32)
        patch('ampStopModule', lambda: snd.append(('stop',)) or 0, i16)
        patch('ampFreeModule', lambda p: snd.append(('free', bool(p))), None, vp)
        patch('MemFree', lambda p: snd.append(('memfree', bool(p))) or 0, i16, vp)


def snd_log(s):
    """the comparable calls: the port's device does not see cdiPoll,
    SetupChannels and DownloadSample (the wrappers' own tests: test_sound)"""
    mod = s.dg(0xBB75, 4) != bytes(4) if s.side == 'o' else \
        bool(vp.in_dll(lib, 'g_music_module').value)
    ev = [e for e in s.snd if e[0] not in (0x34, 0x5C, 0x54)]
    # ui_wait_voice's last call - cdiSetSample(voice channel, the "silence"
    # SAMPLEINFO), a setSample without setPanning / playSample after it -
    # is compared by test_sound_silence_sample alone: the port (engine/
    # intrface.h UI_VOICE_SILENCE, not ISOSYS) passes another one
    out, s.silence, i = [], [], 0
    while i < len(ev):
        if ev[i][0] == 0x00 and (i + 3 >= len(ev) or ev[i + 3][0] != 0x18):
            s.silence.append(tuple(ev[i:i + 3]))
            out.append(('silence', ev[i][1]))
            i += 3
        else:
            out.append(ev[i])
            i += 1
    return out + [('module', mod)]


def run_snd(label, op, **k):
    B = _B()
    snd_dir = os.path.join(ROOT, 'extracted', 'SOUNDS')
    old = uiworld.mount

    def setup(s):
        sound_on(s)
    # B.run sets its own mount; SOUNDS.CKD added through c_mount below
    cm = helpers.c_mount
    helpers.c_mount = lambda *a: cm(*a, b'SOUNDS.CKD')
    helpers.SEARCH.insert(0, snd_dir)
    try:
        # 7AD6: CDI's "a device is registered" (the port's recorder; the
        # original's CDI wrappers are replaced)
        k['ignore'] = list(k.get('ignore', ())) + [0x7AD6]
        return B.run(label, op, setup=setup, sound=1, extra=snd_log, **k)
    finally:
        helpers.c_mount = cm
        uiworld.mount = old
        if snd_dir in helpers.SEARCH:
            helpers.SEARCH.remove(snd_dir)


def test_sound_play_sfx():
    """iso_play_sfx with sound on: every sound of the table (1..20h; flag
    20h: the rate by the speaker's voice class, the defender's for sound 2,
    sound 0Dh instead for monster types 14..17, 30, 31), actors of the
    party and of monster groups of several types, BB38 off, outside the
    battle window; the samples are loaded on demand (ui_load_voice,
    oses_load_sample into EMS), started on channels 7 and 6 (stop, set,
    pan, play) with the random rate of flag 10h, and the voice waited for
    with a scripted position.  Same CDI calls, same state."""
    B = _B()
    types = (1, 5, 14, 16, 17, 30, 31, 9, 22)
    seen = {}

    def op(s):
        rng = random.Random(77)
        B.battle(s, rng)
        r = []
        for rnd in range(3):
            inwin = rnd != 2
            (B.in_window if inwin else B.no_window)(s)
            s.dgw(0xBB38, bytes([rnd != 1]))
            for k, n in enumerate(list(range(0, 0x21)) + [2] * 12):
                actor = rng.choice((1, 9, 17, 25, 26, 27, 28))
                dfn = rng.choice((1, 9, 17, 25, 28))
                for g in (5, 6, 7):                       # the groups' monster types
                    B.w(s, 0x5BC2 + 6 * g, rng.choice(types))
                B.w(s, 0xD5DE, actor)
                B.w(s, 0xD56C, dfn)
                B.w(s, 0xD566, 5)
                B.call(s, 'iso_play_sfx', n)
                if k % 3 == 0:
                    r.append(B.call(s, 'ui_wait_voice'))
                r.append((B.rd(s, 0xD566), s.dg(0x73CC, 4), s.dg(0x73D5, 2)))
        seen[s.side] = list(s.snd)
        return r
    run_snd('iso_play_sfx (sound on)', op, clock_step=7)
    plays = [e for e in seen['c'] if e[0] == 0x04]
    check(len(plays) >= 2 * 44 and len({e[2] for e in plays}) > 20,
          f'iso_play_sfx: {len(plays)} playSample calls')
    check(any(e[0] == 0x48 for e in seen['c']), 'iso_play_sfx: no position was asked for')


def test_sound_silence_sample():
    """what ui_wait_voice leaves on the voice channel when the voice has
    ended: the original calls cdiSetSample(channels - 1, 16E0:DF1Eh) - the
    SAMPLEINFO of sound slot 0Ah, bosch.pur ("empty") - 05DC:1E3C `push
    16E0h / push DF1Eh`.  With the slot not loaded and loaded.
    (INTRFACE's ui_wait_voice, reached from every ISOSYS voice.)"""
    B = _B()
    got = {}

    def op(s):
        B.battle(s, random.Random(3))
        B.in_window(s)
        B.w(s, 0xD5DE, 25)
        r = []
        for n in (3, 0x0A, 5):               # 0Ah: loads bosch.pur itself
            B.call(s, 'iso_play_sfx', n)
            r.append(B.call(s, 'ui_wait_voice'))
            r.append(s.dg(0xDE38 + 0x17 * 0x0A, 0x13))
        snd_log(s)
        got[s.side] = s.silence
        return r
    run_snd('silence sample', op, clock_step=7)
    check(len(got['o']) == 3 and got['o'][0][0][2:] == (0, 0) and got['o'][2][0][3] == 53,
          f'silence sample: the original set {got["o"]}')
    check(got['o'] == got['c'], 'silence sample: ui_wait_voice sets another SAMPLEINFO than the '
          f'original (slot 0Ah, DGROUP DF1Eh): {got["o"]} vs {got["c"]}')


def test_textbox_capital():
    """ENGINE finding (GRTEXT / SYSTEMS / MEMMAN, not ISOSYS), reached
    through iso_show_message outside the battle window with a text that
    begins with a capital letter: iso_draw_textbox draws the text at y = 4
    and y = 3 on the screen (slot 0), grt_draw_text the big capital's 19x19
    cell at y - 5 = -1 and -2.  The original's sys_blit writes the rows
    above the screen below A000:0000 (linear 9FF61h and 9FE21h, the top of
    conventional memory) and the others from row 0; the port has to draw
    the same screen (and no more)."""
    B = _B()

    def op(s):
        B.battle(s, random.Random(41))
        B.no_window(s)
        B.w(s, 0x631A, 0)
        B.w(s, 0xD5BA, 0)
        B.w(s, 0xD5A2, 0)
        buf = s.string(b'Hadi', 0x60)
        if s.side == 'o':
            m0 = bytes(s.e.u.mem_read(0x9FE21, 0x1DF))
            s.c('iso_show_message', *buf[1], 9)
            m1 = bytes(s.e.u.mem_read(0x9FE21, 0x1DF))
            check(m0[:19] != m1[:19] and m0[0x140:0x153] != m1[0x140:0x153]
                  and m0[19:0x140] == m1[19:0x140] and m0[0x153:] == m1[0x153:],
                  'textbox capital: the original did not write the two rows below A000:0')
            return None
        try:
            fn('iso_show_message', None, vp, i16)(buf[1], 9)
        except OSError as ex:
            check(False, f'textbox capital: the port: {ex}')
        return None
    B.run('textbox capital', op, clock_step=7)


def _voice_numbers():
    names = set(os.listdir(os.path.join(ROOT, 'extracted', 'SOUNDS')))
    ok = {}
    for a, b in (('VECIZE', 'FECIZE'), ('CEVAP', 'FEVAP')):
        ok[a] = [n for n in range(1, 60) if f'{a}{n:02d}.PUR' in names
                 and f'{b}{n:02d}.PUR' in names]
    return ok


def test_sound_speak():
    """iso_speak_vecize / iso_show_message with sound on: the saying or
    answer of party members (male / female) and monsters (voice classes)
    is loaded into slot 21h (reversed; flag 1 or 2), started at the
    speaker's rate and played to its end piece by piece (ui_load_voice,
    ui_play_voice, ui_wait_voice with the scripted position), while the
    idle callback iso_face_anim_tick draws the talking face every 150
    clock units (timer BBC4) - in the battle window, outside it (the
    target's face in the party bar), not for voice 1Ah; gum; BB38 off;
    Esc cutting a voice short.  Same CDI calls, same state, same screen."""
    B = _B()
    nums = _voice_numbers()
    check(len(nums['VECIZE']) > 5 and len(nums['CEVAP']) > 5, f'voice files {nums}')
    for name, keys in (('', ()), (' esc', [0x011B] * 40)):
        seen = {}

        def op(s):
            rng = random.Random(41)
            B.battle(s, rng)
            for p in range(1, 5):                          # genders
                B.w(s, 0x99A6 + 0x5C * p + 4, 1 + p % 2)
            r = []
            faces = 0
            buf = s.string(b'', 0x60)
            for k in range(26):
                inwin = k % 3 != 2
                (B.in_window if inwin else B.no_window)(s)
                voice = rng.choice((0, 0, 0x1A, 0x1D, 3, 7, 0x2D))
                party = rng.random() < .6
                c = rng.choice((25, 26, 27, 28) if party else (1, 9, 17))
                for g in (5, 6, 7):
                    B.w(s, 0x5BC2 + 6 * g, rng.choice((1, 2, 3, 6, 9, 12)))
                n = rng.choice(nums['CEVAP'] if voice else nums['VECIZE'])
                B.w(s, 0x631A, voice)
                B.w(s, 0xD5BA, 1 if k % 7 == 5 else 0)     # gum
                B.w(s, 0xD5B0, k % 7)
                B.w(s, 0xD5A2, 0)
                B.w(s, 0xD5E0, rng.choice((25, 26, 27, 28)))
                B.w(s, 0xD5BC, rng.choice((0, 5)))
                B.w(s, 0xD556, 2)
                s.dgw(0xBB38, bytes([k % 11 != 10]))
                t0 = s.dg(0xBBC4, 2)
                if k % 4 == 3:                             # through the message
                    B.w(s, 0x631A, n if voice else 0)
                    text = b'hadi bakal\x8dm %d' % k        # (a capital: test_textbox_capital)
                    if s.side == 'o':
                        s.e.wr(buf[0], 0, text + b'\0', runtime=True)
                        s.c('iso_show_message', *buf[1], c)
                    else:
                        C.memmove(buf[1], text + b'\0', len(text) + 1)
                        fn('iso_show_message', None, vp, i16)(buf[1], c)
                else:
                    who = c + 0x28 if c > 0x18 else B.rd(s, 0x5BC8 + 6 * (B.rd(
                        s, 0xA08A + 0x28 * c + 6) - 1))
                    B.w(s, 0x6320, who)
                    B.call(s, 'iso_speak_vecize', n)
                faces += s.dg(0xBBC4, 2) != t0
                r.append((B.rd(s, 0x6320), B.rd(s, 0x631A), s.dg(0x964C, 4), s.dg(0xBBC4, 2),
                          s.read(6) if k % 5 == 0 else None))
            seen[s.side] = (list(s.snd), faces)
            return r
        run_snd('iso_speak_vecize (sound on)' + name, op, clock_step=11, keys=keys, slots=(6,),
                # the faces: 5 mouth pictures of 32x32 (frames 0, 3, 4 are drawn)
                surfs=[(0x46 + p, 160, 32, 0, 'TRADE.CMP') for p in range(1, 5)])
        log, faces = seen['c']
        check(sum(1 for e in log if e[0] == 0x04) >= 20, f'speak{name}: few voices played')
        check(faces >= 5, f'speak{name}: the face timer BBC4 ran {faces} times')


def test_sound_battle():
    """whole battles with sound and music on (73D0, options BB37 / BB38):
    the battle music at the start (demo_play_music(0Ch or 10h by
    random(2), 1, 0), the fade volume less 1Eh / 19h, ampSetTrackVolume)
    and its end (ampStopModule, ampFreeModule + MemFree of the module, the
    old track back), the fades' volume calls, the turn / hit / death /
    step sounds and the spoken lines with their faces while the battle
    runs: attacks with the real combat routines, vecizes, and a battle
    of only "end turn" with BB38 off.  Same order of CDI / AMP calls,
    same state at the end."""
    Cc = _Cc()
    K, ENTER, ESC, TAB, KP = Cc.K, Cc.ENTER, Cc.ESC, Cc.TAB, Cc.KP
    attack = [ENTER, K['o'], ENTER, TAB, ENTER, K['o'], K['o'], ENTER, KP['8'], ENTER, K['s'],
              ENTER, TAB, ENTER, ESC] * 3
    vecize = [K['v'], ENTER, ENTER, TAB, K['v'], ESC, TAB, K['v'], K['down'], ENTER, K['o'], ENTER,
              TAB] * 2
    faces = [s_ for s_ in Cc.PANEL_SURFS if not 0x47 <= s_[0] <= 0x4A]
    faces += [(0x46 + p, 160, 32, 0, 'TRADE.CMP') for p in range(1, 5)]
    for name, keys, opt4, track, kw in (
            ('attack', attack, 1, 3, dict(seed=20, stub=False)),
            ('vecize', vecize, 1, 0x0C, dict(seed=41, stub=False, vecize=True)),
            ('tab', [], 0, 7, dict(seed=3))):
        seen = {}

        def op(s, fd, opt4=opt4, track=track):
            s.dgw(0xBB37, bytes([1, opt4]))
            s.dgw(0xBB4D, struct.pack('<h', track))
            run_battle(s, fd)
            seen[s.side] = list(s.snd)
            return fd.fed <= fd.cap + 2, Cc.calls(s)
        battle2(f'iso battle sound {name}', op, keys, sound=1, groups=Cc.GROUPS3, surfs=faces,
                setup=lambda s: sound_on(s, music=True), extra=snd_log,
                # 73D1 (the voice's end time) and BBC4 (the face timer) are
                # copies of the clock, which a whole battle's polls advance
                # differently on the two sides (as D534 in test_iso_c);
                # test_sound_speak compares both
                ignore=[0x7AD6] + list(range(0x73D1, 0x73D5)) + [0xBBC4, 0xBBC5], **kw)
        ev = seen['c']
        mus = [e for e in ev if e[0] in ('music', 'stop', 'free', 'memfree')]
        check(len(mus) == 4 and mus[0][0] == 'music' and mus[0][1] in (0x0C, 0x10)
              and mus[0][2:] == (1, 0) and [e[0] for e in mus[1:]] == ['stop', 'free', 'memfree'],
              f'sound battle {name}: music calls {mus}')
        check(any(e[0] == 'vol' and e[1] == 0xFFFFFFFF for e in ev),
              f'sound battle {name}: no ampSetTrackVolume(-1, level)')
        if opt4:
            check(sum(1 for e in ev if e[0] == 0x04) >= 8,
                  f'sound battle {name}: {sum(1 for e in ev if e[0] == 0x04)} samples played')
        else:
            check(not any(e[0] == 0x04 for e in ev), f'sound battle {name}: samples with BB38 off')


if __name__ == '__main__':
    if sys.argv[1:] == ['door14-child']:
        _door14_child()
