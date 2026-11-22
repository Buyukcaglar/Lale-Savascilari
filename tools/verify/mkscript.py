#!/usr/bin/env python3
"""
mkscript.py - write an input script (sdl3_main.c "input scripts") from a
compact Python description, so play-throughs stay readable:

    python tools/verify/mkscript.py playthrough out/verify/scripts/play.txt

Each step is (gap_ms, action...) with time accumulated; text() types a
string with one key per `typing` ms.
"""
import sys

SC = {'a': 0x1E, 'b': 0x30, 'c': 0x2E, 'd': 0x20, 'e': 0x12, 'f': 0x21, 'g': 0x22, 'h': 0x23,
      'i': 0x17, 'j': 0x24, 'k': 0x25, 'l': 0x26, 'm': 0x32, 'n': 0x31, 'o': 0x18, 'p': 0x19,
      'q': 0x10, 'r': 0x13, 's': 0x1F, 't': 0x14, 'u': 0x16, 'v': 0x2F, 'w': 0x11, 'x': 0x2D,
      'y': 0x15, 'z': 0x2C, '1': 0x02, '2': 0x03, '3': 0x04, '4': 0x05, '5': 0x06, '6': 0x07,
      '7': 0x08, '8': 0x09, '9': 0x0A, '0': 0x0B}
K = dict(ENTER=0x1C0D, ESC=0x011B, SPACE=0x3920, TAB=0x0F09, UP=0x4800, DOWN=0x5000,
         LEFT=0x4B00, RIGHT=0x4D00, PGUP=0x4900, PGDN=0x5100, HOME=0x4700, END=0x4F00,
         F1=0x3B00, F2=0x3C00, F3=0x3D00, F5=0x3F00, F10=0x4400)


def key_of(ch):
    if ch.isupper():
        return (SC[ch.lower()] << 8) | ord(ch)
    return (SC[ch] << 8) | ord(ch)


class Script:
    """events are timed on the game clock (at(), the default) or, after
    reads(), relative to the previous event in counted input reads (+n), or
    after after(), in ms of the game clock after the previous event (@n)"""
    def __init__(self, seed=1234, t=0):
        self.lines = [f'seed {seed}']
        self.t = t
        self.rel = False
        self.pend = 0

    def at(self, t):
        self.rel = False
        self.t = t
        return self

    def reads(self, n=0):
        """switch to read-count timing; the next event n reads after the last one"""
        self.rel = True
        self.pend = n
        return self

    def after(self, n=0):
        """switch to clock-relative timing; the next event n ms after the last one"""
        self.rel = '@'
        self.pend = n
        return self

    def emit(self, text):
        if self.rel:
            self.lines.append(f'{"@" if self.rel == "@" else "+"}{self.pend} {text}')
            self.pend = 0
        else:
            self.lines.append(f'{self.t} {text}')
        return self

    def wait(self, n):
        if self.rel:
            self.pend += n
        else:
            self.t += n
        return self

    def key(self, k, gap=400):
        w = K[k] if isinstance(k, str) else k
        return self.emit(f'key {w:04X}').wait(gap)

    def keys(self, *ks, gap=400):
        for k in ks:
            self.key(k, gap)
        return self

    def text(self, s, gap=250):
        for ch in s:
            self.key(key_of(ch), gap)
        return self

    def mouse(self, x, y, b=0, gap=300):
        return self.emit(f'mouse {x} {y} {b}').wait(gap)

    def click(self, x, y, gap=600):
        self.mouse(x, y, 0, 200).mouse(x, y, 1, 250).mouse(x, y, 0, gap)
        return self

    def dump(self, name):
        return self.emit(f'dump {name}')

    def every(self, until, step, prefix):
        self.lines.append(f'dumpevery {self.t} {until} {step} {prefix}')
        return self

    def sync(self, addr, val):
        """anchor: due when also DGROUP word addr == val (the +n after it count from here)"""
        return self.emit(f'sync {addr:04X} {val:04X}')

    def srand(self, v):
        """set the RTL random state (re-aligns rand() after clock-timed calls)"""
        return self.emit(f'srand {v:08X}')

    def poke(self, addr, data):
        """write bytes to DGROUP:addr on both sides (neutralise clock-timed state)"""
        return self.emit(f'poke {addr:04X} ' + ' '.join(f'{b:02X}' for b in data))

    def poke32(self, addr, v):
        return self.poke(addr, (v & 0xFFFFFFFF).to_bytes(4, 'little'))

    def poke16(self, addr, v):
        return self.poke(addr, (v & 0xFFFF).to_bytes(2, 'little'))

    def syncne(self, addr, val):
        return self.emit(f'syncne {addr:04X} {val:04X}')

    def quit(self):
        return self.emit('quit')

    def write(self, path):
        open(path, 'w').write('\n'.join(self.lines) + '\n')


def playthrough(path, stop=None):
    s = Script(seed=1234)
    s.at(50000).dump('c00_menu').key('ENTER', 6000)            # menu: new game
    s.dump('c01_chargen')
    for i, name in enumerate(('Ali', 'Ayse', 'Rifki', 'Zeki')):
        s.key('TAB', 600).text(name).key('ENTER', 800).dump(f'c02_member{i + 1}').key('SPACE', 2500)
    s.dump('c03_after_chargen').wait(3000)
    for i in range(4):                                       # story videos: Esc
        s.key('ESC', 3000)
    s.at(150000).dump('c04_district')
    moves = ['UP', 'UP', 'RIGHT', 'UP', 'UP', 'LEFT', 'LEFT', 'UP', 'DOWN', 'RIGHT']
    for i, m in enumerate(moves):
        s.key(m, 1500).dump(f'c05_move{i:02d}')
    s.wait(1500)
    s.key(0x1F73, 2500).dump('c06_savemenu')                 # S: save menu
    s.key('DOWN', 1000).key('ENTER', 1500).text('ab').key('ENTER', 1500).key('ENTER', 4000)
    s.dump('c07_saved').wait(1000).quit()
    s.write(path)


def walk(path):
    """exploration probe: long walk after the district start (port only)"""
    s = Script(seed=1234)
    s.at(50000).key('ENTER', 6000)
    for name in ('Ali', 'Ayse', 'Rifki', 'Zeki'):
        s.key('TAB', 600).text(name).key('ENTER', 800).key('SPACE', 2500)
    s.wait(3000)
    for i in range(4):
        s.key('ESC', 3000)
    s.at(150000)
    t0 = s.t
    s.key('UP', 3000).key('ENTER', 3000)
    s.at(t0).every(t0 + 120000, 3000, 'w')
    s.at(t0 + 120000).quit()
    s.write(path)


def chargen_reads(s, gap=3000, typing=1500, reseed=False):
    """menu -> new game -> four members, timed in input reads (the chargen
    loop calls random(1000) once per pass: equal passes = equal rolls)"""
    s.at(50000).dump('c00_menu').key('ENTER').reads(gap).dump('c01_chargen')
    for i, name in enumerate(('Ali', 'Ayse', 'Rifki', 'Zeki')):
        s.key('TAB', typing).text(name, typing).key('ENTER', gap).dump(f'c02_member{i + 1}')
        if reseed:
            s.srand(0x1000 + i)                     # same read as the Space: the next roll
        s.key('SPACE', gap)
    s.dump('c03_after_chargen')
    return s


def story_skip(s):
    """the story videos after chargen: Esc four times (game clock)"""
    s.at(70000)
    for i in range(4):
        s.key('ESC', 3000)
    return s


def play2(path):
    """the verification play-through: chargen (read-count sync), story,
    district 0, save, the door event (yes, treasure screen), the dog battle
    (read-count sync from the battle window on)"""
    s = Script(seed=1234)
    chargen_reads(s, reseed=True)
    story_skip(s)
    s.at(150000).dump('c04_district')
    s.key('RIGHT', 2500).dump('c05_turn1').key('RIGHT', 2500).dump('c05_turn2')
    s.key(0x1F73, 2500).dump('c06_savemenu')                # S: save / load
    s.key('DOWN', 1000).key('ENTER', 1500).text('ab', 400).key('ENTER', 1500).key('ENTER', 4000)
    s.dump('c07_saved')
    s.key('UP', 2500).dump('c08_door')                      # event 0, trigger 1: the question
    s.key(0x1265, 8000).dump('c09_treasure')                # 'e' (evet): treasure screen
    s.srand(0x2000).key('ESC', 500)                         # leave it: trigger 2, the battle
    s.syncne(0xD5DE, 0)                                     # battle: the first actor is up
    s.reads(0).dump('c10_battle')
    for i, k in enumerate(['UP', 'UP', 'ENTER', 'TAB'] * 3):
        s.wait(30000).key(k, 10000).dump(f'c11_b{i:02d}_{k}')
    s.wait(1000).quit()
    s.write(path)


CG_WALK, CG_BLINK = 0x48E8, 0x48EA      # chargen walking frame, blink state (chargen.h)
GFL_LAST_FRAME = 0xD618                 # last frame processed by gfl_show_frame (gfl.h)


def cg_settle(s, name, seed):
    """chargen checkpoint independent of the clock: walking frame := 3 and
    blink := 2 (no new blink can start, the next 150 ms tick draws frame 0
    and the normal face on both sides), wait for that tick (48E8 == 0),
    re-seed (the passes before the tick differ in number) and dump"""
    s.poke(CG_WALK, [3, 0, 2, 0]).sync(CG_WALK, 0).srand(seed)
    return s.dump(name)


def chargen_exact(s, gap=3000, typing=1500):
    """menu -> new game -> four members; every roll after a re-seed with the
    animation state cleared (the blink's random(3) is clock-timed)"""
    s.at(50000).dump('c00_menu').srand(0x0FFF).poke(CG_WALK, [0, 0, 0, 0]).key('ENTER')
    s.reads(gap)
    cg_settle(s, 'c01_chargen', 0x0F00).wait(gap)
    for i, name in enumerate(('Ali', 'Ayse', 'Rifki', 'Zeki')):
        s.key('TAB', typing).text(name, typing).key('ENTER', gap)
        cg_settle(s, f'c02_member{i + 1}', 0x0F10 + i)
        s.srand(0x1000 + i).poke(CG_WALK, [0, 0, 0, 0])   # the Space's pass rolls next
        s.key('SPACE', gap)
    # the story video seq1.GFL follows; its frame is due by the clock, so
    # dump when the player has processed frame 5 (D618, the read after it:
    # both sides show every frame at 10 fps, no drops)
    s.sync(GFL_LAST_FRAME, 5).dump('c03_after_chargen')
    return s


def quiet_seed(base, n=8, below=1639):
    """the first seed >= base whose next n rand() values are all >= below:
    with below = 1639 none of them makes random(20) == 0, so the 3DSYS
    portrait blink (a clock-timed random(20) once a second, its timer a
    local whose phase depends on load times) cannot start on either side,
    whatever the interleaving of those calls with the step's own rolls"""
    v = base
    while True:
        st, ok = v, True
        for _ in range(n):
            st = (st * 0x015A4E35 + 1) & 0xFFFFFFFF
            if ((st >> 16) & 0x7FFF) < below:
                ok = False
                break
        if ok:
            return v
        v += 1


def xkey(s, k, gap, seed):
    """exploration key with a quiet re-seed in the same read"""
    return s.srand(quiet_seed(seed)).key(k, gap)


def play3(path, battle_keys=12):
    """play2 with exact checkpoints: chargen (chargen_exact), story, district
    0 (quiet re-seed at every key), save, the door event, the dog battle
    (re-seeded once the first actor is up)"""
    s = Script(seed=1234)
    chargen_exact(s)
    story_skip(s)
    for i, t in enumerate(range(84000, 150000, 2000)):       # no portrait blink can start
        s.at(t).srand(quiet_seed(0x3000 + 0x10 * i))
    s.at(150000).srand(quiet_seed(0x3800)).dump('c04_district')
    xkey(s, 'RIGHT', 2500, 0x3100).srand(quiet_seed(0x3180)).dump('c05_turn1')
    xkey(s, 'RIGHT', 2500, 0x3200).srand(quiet_seed(0x3280)).dump('c05_turn2')
    xkey(s, 0x1F73, 2500, 0x3300).dump('c06_savemenu')        # S: save / load
    s.key('DOWN', 1000).key('ENTER', 1500).text('ab', 400).key('ENTER', 1500).key('ENTER', 4000)
    s.dump('c07_saved')
    xkey(s, 'UP', 2500, 0x3400).dump('c08_door')              # event 0, trigger 1: the question
    xkey(s, 0x1265, 8000, 0x3500).dump('c09_treasure')        # 'e' (evet): treasure screen
    s.srand(0x2000).key('ESC', 500)                           # leave it: trigger 2, the battle
    s.syncne(0xD5DE, 0)                                       # battle: the first actor is up
    s.reads(0).srand(0x2100).dump('c10_battle')
    for i, k in enumerate((['UP', 'UP', 'ENTER', 'TAB'] * 8)[:battle_keys]):
        # 30000 reads before a key: the talking face (random(2) while a voice
        # plays) is over; after TAB 100000 (the enemy's turn: clock-timed
        # animations; DOSBox-X makes ~3x more reads per ms than the port)
        s.wait(30000).srand(0x2200 + i).key(k, 100000 if k == 'TAB' else 10000)
        s.dump(f'c11_b{i:02d}_{k}')
    s.wait(1000).quit()
    s.write(path)


def play3_prefix(s):
    """play3 up to the dog battle's first actor (c10_battle)"""
    chargen_exact(s)
    story_skip(s)
    for i, t in enumerate(range(84000, 150000, 2000)):       # no portrait blink can start
        s.at(t).srand(quiet_seed(0x3000 + 0x10 * i))
    s.at(150000).srand(quiet_seed(0x3800)).dump('c04_district')
    xkey(s, 'RIGHT', 2500, 0x3100).srand(quiet_seed(0x3180)).dump('c05_turn1')
    xkey(s, 'RIGHT', 2500, 0x3200).srand(quiet_seed(0x3280)).dump('c05_turn2')
    xkey(s, 0x1F73, 2500, 0x3300).dump('c06_savemenu')        # S: save / load
    s.key('DOWN', 1000).key('ENTER', 1500).text('ab', 400).key('ENTER', 1500).key('ENTER', 4000)
    s.dump('c07_saved')
    xkey(s, 'UP', 2500, 0x3400).dump('c08_door')              # event 0, trigger 1: the question
    xkey(s, 0x1265, 8000, 0x3500).dump('c09_treasure')        # 'e' (evet): treasure screen
    s.srand(0x2000).key('ESC', 500)                           # leave it: trigger 2, the battle
    s.syncne(0xD5DE, 0)                                       # battle: the first actor is up
    s.reads(0).srand(0x2100).dump('c10_battle')
    return s


def battle_groups(s, groups, prefix, n0=0, last_dump=True):
    """battle actions: each group is a list of keys (one actor's action),
    30000 reads before every key (the talking face is over), a re-seed in
    the key's read, 10000 reads after it (100000 after Tab: the enemy turn;
    12 s of clock after a bump or a cast); one dump per group"""
    n = n0
    for gi, g in enumerate(groups):
        for ki, k in enumerate(g):
            kk = key_of(k) if isinstance(k, str) and len(k) == 1 else k
            s.wait(30000).srand(0x2200 + n)
            if isinstance(k, int) or (k == 'ENTER' and ki and g[ki - 1] == 'o'):
                # a bump or a cast: its animation, voice and texts are
                # clock-timed (DOSBox-X makes ~2.5x more reads per ms):
                # 12 s of clock, then back to read counts
                s.key(kk, 0).after(12000).srand(0x2300 + n).reads(0)
            else:
                s.key(kk, 100000 if k == 'TAB' else 10000)
            n += 1
        if last_dump or gi < len(groups) - 1:     # the last one ends the battle (clock-timed)
            s.dump(f'{prefix}{gi:02d}')
    return n


# the dog battle (out/verify/h/plan2.py, planned on the port): everybody
# waits (Tab) but Rifki (unit 27), who casts vecize 1 ("pis") once the dog
# is next to him and then bumps into it (the party has no weapons: Enter
# attacks need one); the dog's AI bites the party to death: lost
DOG_BATTLE = [['TAB'], ['TAB'], ['TAB'], ['TAB'], ['TAB'], ['v', 'ENTER', 'o', 'ENTER'], ['TAB'], ['TAB'],
              ['TAB'], [0x5000], ['TAB'], ['TAB'], ['TAB'], ['TAB'], [0x5000], ['TAB'],
              [0x5000], ['TAB'], ['TAB'], ['TAB'], ['TAB'], [0x5000], ['TAB'], ['TAB'],
              ['TAB'], ['TAB'], ['TAB'], ['TAB'], ['TAB'], ['TAB'], ['TAB'], ['TAB'],
              ['TAB'], ['TAB'], ['TAB'], ['TAB'], ['TAB'], ['TAB'], ['TAB'], ['TAB'],
              ['TAB'], ['TAB']]

# the same battle after loading: every member tries vecize 1 (the dog is out
# of its range: "desem de duyamaz ki!") and an attack (no weapon: ignored),
# then Tab; the dog's AI runs away on its first turn
FLEE_BATTLE = [['v', 'ENTER', 'o', 'ENTER', 'ENTER', 'o', 'ENTER', 'TAB']] * 4


def bprobe(path, groups=None):
    """battle probe (port): the dog battle, a dump after each group"""
    s = Script(seed=1234)
    play3_prefix(s)
    battle_groups(s, groups or DOG_BATTLE, 'b')
    s.wait(1000).quit()
    s.write(path)


# exploration after a battle: the two clocks differ (the battle ran in
# read-count timing), so the 3D keys are timed in ms of the game clock after
# the previous event (@n), each with a quiet re-seed in its read.  The FRPOS
# waits (ui_wait_click_or_key, the message box) poll the mouse in a tight
# loop (reads at CPU speed), so read counts cannot time them; on the clock
# they end at the same time on both sides (up to the few ms that loading and
# drawing take in DOSBox-X), keys are sent after them.
XGAP = 2500


def quiet_wait(s, ms, seed, step=1000):
    """wait ms (clock-relative) with a quiet re-seed every step ms: the 3D
    view's portrait blink (random(20) once a second) cannot start while
    nothing else draws random numbers"""
    for i in range(ms // step):     # 64 quiet draws: the 3D view draws more than 8 a second
        s.wait(step).srand(quiet_seed(seed + 0x10 * i, 64))
    return s.wait(ms % step)


def rkey(s, k, seed, gap=XGAP):
    """3D key (clock-relative) with a quiet re-seed in its read"""
    return s.srand(quiet_seed(seed)).key(k, gap)


def rwalk(s, keys, seed, name=None, gap=XGAP, extra=None, syncs=None):
    """3D keys; extra = {step: ms} waits after the moves that start a
    dialog, syncs = {step: (addr, word)} anchors on the flag word a dialog
    sets at its end (then XGAP more); dumps <name>_NN after each when name
    is given"""
    for i, k in enumerate(keys):
        rkey(s, k, seed + 0x10 * i, gap + (extra or {}).get(i, 0))
        if syncs and i in syncs:
            s.sync(*syncs[i]).srand(quiet_seed(seed + 0x10 * i + 8))
            quiet_wait(s, XGAP, seed + 0x10 * i + 9)
        if name:
            s.dump(f'{name}_{i:02d}')
    return s


# district walks (out/verify/h/path.py: BFS over the map cells' walls)
# district 0 (PC1), after the dog battle at (2,0) facing north -> the
# unconscious man (8,1); step 4 enters (1,2) (event 3: a remark), step 39
# (8,1) (event 1: "search him?")
D0_TO_MAN = ['LEFT', 'UP', 'LEFT', 'UP', 'UP', 'LEFT', 'UP', 'RIGHT', 'UP', 'UP', 'LEFT', 'UP',
             'UP', 'LEFT', 'UP', 'UP', 'UP', 'UP', 'RIGHT', 'UP', 'RIGHT', 'UP', 'UP', 'UP', 'UP',
             'LEFT', 'UP', 'UP', 'LEFT', 'UP', 'UP', 'UP', 'UP', 'RIGHT', 'UP', 'UP', 'RIGHT', 'UP',
             'RIGHT', 'UP']
# (8,1) facing west -> the car park exit (0,5); step 25 enters (4,5) (event
# 4: "really leave?"), step 45 (0,5) (event 2: the exit, teleport to
# district 1 (PC2) at (3,22) facing west)
D0_TO_EXIT = ['LEFT', 'LEFT', 'UP', 'RIGHT', 'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'RIGHT', 'UP',
              'LEFT', 'UP', 'UP', 'RIGHT', 'UP', 'UP', 'UP', 'RIGHT', 'UP', 'UP', 'UP', 'UP', 'LEFT',
              'UP', 'UP', 'UP', 'LEFT', 'UP', 'UP', 'UP', 'UP', 'RIGHT', 'UP', 'UP', 'RIGHT', 'UP',
              'UP', 'RIGHT', 'UP', 'LEFT', 'UP', 'UP', 'LEFT', 'UP']
# district 1 (PC2) from (3,22) facing west: the goods shop (3,7) through
# events 1 (3,20: the Phantom), 30 (1,15: the party talks), 28 (1,13), 28 (1,7)
D1_TO_GOODS = ['RIGHT', 'UP', 'UP', 'UP', 'LEFT', 'UP', 'UP', 'RIGHT', 'UP', 'UP', 'UP', 'UP',
               'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'RIGHT', 'UP', 'UP']
# leaving a shop puts the party back on its previous cell: (2,7) facing
# east -> the weapon shop (11,10), through 28 (1,7: key 2), 28 (1,13: key 9)
D1_TO_WEAPONS = ['LEFT', 'LEFT', 'UP', 'LEFT', 'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'LEFT',
                 'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'UP', 'LEFT', 'UP', 'UP',
                 'UP', 'UP']
# (11,11) facing north -> the food stall (13,16), through 19 (11,19: key 14)
D1_TO_FOOD = ['LEFT', 'LEFT', 'UP', 'UP', 'UP', 'RIGHT', 'UP', 'LEFT', 'UP', 'UP', 'UP', 'UP',
              'UP', 'LEFT', 'UP', 'LEFT', 'UP', 'UP', 'UP', 'RIGHT', 'UP', 'UP']
FR_FLAGS = 0xB9CA                       # FRPOS event flags (flag n: byte n/8, bit n%8)
KEY_E = 0x1265                          # 'e': evet / eminim (first answer)


def play4(path, stop=None):
    """play3's prefix, the dog battle to its end (vecize + attacks, the dog's
    AI), then on the clock relative to the battle's end: district 0 to the
    unconscious man (item exchange: take his item), out of the car park (two
    questions, teleport to district 1), the goods shop, the weapon shop
    (buy and sell by keys and by mouse), the food stall, camp, save"""
    s = Script(seed=1234)
    play3_prefix(s)
    battle_groups(s, DOG_BATTLE, 'c12_b', last_dump=False)
    s.sync(0x632E, 2)                                       # lost (ISOSYS result): game over
    # "continue an old game?" (the music tempo fade of s3d_apply_sound_options,
    # 100 ms steps in the idle callback, ends ~10 s after the result in DOSBox-X)
    s.after(0).srand(quiet_seed(0x3DF0))
    quiet_wait(s, 14000, 0x3E00).dump('c13_game_over')
    s.key(KEY_E, 3000).dump('c13_loadmenu')                 # evet: the load menu
    s.key('ENTER', 0)                                       # SAKLA001.SAV
    quiet_wait(s, 8000, 0x3E10).dump('c13_loaded')
    rkey(s, 'UP', 0x3F00, 2500).dump('c14_door')            # the door again
    rkey(s, KEY_E, 0x3F10, 8000).dump('c14_treasure')
    s.poke16(0xD5DE, 0)                                     # the first battle's last actor
    s.srand(0x2000).key('ESC', 500)                         # the dog battle again
    s.syncne(0xD5DE, 0)
    s.reads(0).srand(0x2100).dump('c14_battle2')
    battle_groups(s, FLEE_BATTLE, 'c14_b', last_dump=False)  # the dog runs away: won
    s.sync(FR_FLAGS, 0x0002)                                # the remark after it, flag 1
    s.after(0).srand(quiet_seed(0x4000))
    quiet_wait(s, 6000, 0x4010).dump('c14_after_battle')
    rwalk(s, D0_TO_MAN, 0x4100, extra={4: 8000})            # step 4: a remark (event 3)
    s.srand(quiet_seed(0x4600)).dump('c15_man')
    rkey(s, KEY_E, 0x4610, 1500).dump('c15_exchange')       # search him: the exchange screen
    for i, k in enumerate(['i', 'ENTER', 'ENTER']):         # his item -> slot 1 of member 1
        kk = key_of(k) if len(k) == 1 else k
        s.srand(quiet_seed(0x4620 + 0x10 * i)).key(kk, 1000).dump(f'c15_x{i}_{k}')
    rkey(s, 'ESC', 0x4660, 0).sync(FR_FLAGS, 0x0006).srand(quiet_seed(0x4670))  # "a key!", flag 2
    quiet_wait(s, XGAP, 0x4680).dump('c16_key')
    if stop == 'man':
        return s.quit().write(path)
    rwalk(s, D0_TO_EXIT[:26], 0x4700)
    s.dump('c17_leave_q')                                   # (4,5): "really leave?"
    rkey(s, KEY_E, 0x4A00, 0).sync(FR_FLAGS + 10, 0x0002).srand(quiet_seed(0x4A10))  # .. flag 81
    quiet_wait(s, XGAP, 0x4A20).dump('c17_left')
    rwalk(s, D0_TO_EXIT[26:], 0x4B00)
    s.dump('c18_exit_q')                                    # (0,5): leave the car park?
    rkey(s, KEY_E, 0x4E00, 0)
    quiet_wait(s, 12000, 0x4E10).dump('c19_district1')     # district 1 loaded, music
    if stop == 'd1':
        return s.quit().write(path)
    # the Phantom (step 2, flag 82), the party talks (step 11, flag 181),
    # two signs (steps 13, 19)
    rwalk(s, D1_TO_GOODS, 0x5000, syncs={2: (FR_FLAGS + 10, 0x0006), 11: (FR_FLAGS + 22, 0x0020)},
          extra={13: 12000, 19: 12000})
    # "gerekli seyler" (a 0.8 s ui_wait_click_or_key: 40 rounds that take
    # > 2.5 s in DOSBox-X), then the stock is rolled (random(): no re-seed
    # may fall between the move's re-seed and the roll) and the panel slides in
    s.wait(8000).srand(quiet_seed(0x5200)).dump('c20_goods')
    shop(s, 'c21_goods', 0x5300)
    rwalk(s, D1_TO_WEAPONS, 0x5400, extra={2: 12000, 9: 12000})
    s.wait(8000).srand(quiet_seed(0x5600)).dump('c22_weapons')
    shop(s, 'c23_weapons', 0x5700)
    rwalk(s, D1_TO_FOOD, 0x5800, extra={14: 12000})
    s.wait(8000).srand(quiet_seed(0x5A00)).dump('c24_food')  # the stall: price random(5)+2, "evet?"
    rkey(s, KEY_E, 0x5A10, 0)                               # evet: hunger cleared
    quiet_wait(s, 12000, 0x5A20).dump('c25_fed')
    if stop == 'food':
        return s.quit().write(path)
    rkey(s, key_of('k'), 0x5B00, 2000).dump('c26_camp')     # camp: rest until healed
    s.wait(28000).srand(quiet_seed(0x5B10)).dump('c27_rested')
    rkey(s, 0x1F73, 0x5C00).dump('c28_savemenu')            # S: [bosch] is entry 2
    s.key('DOWN', 1000).key('ENTER', 1500).text('cd', 400).key('ENTER', 1500).key('ENTER', 4000)
    s.srand(quiet_seed(0x5C10)).dump('c29_saved')
    s.wait(1000).quit().write(path)


def play4load(path):
    """after play4 (pair.py --keep): menu, load the game saved last (entry 2:
    SAKLA002.SAV), dumps on arrival and after one turn"""
    s = Script(seed=1234)
    s.at(50000).dump('l00_menu').key('DOWN', 1000).key('ENTER', 3000).dump('l01_loadmenu')
    s.key('DOWN', 1000).key('ENTER', 1000)
    for i, t in enumerate(range(s.t, s.t + 20000, 2000)):    # no portrait blink can start
        s.at(t).srand(quiet_seed(0x6000 + 0x10 * i))
    s.at(s.t + 20000).srand(quiet_seed(0x6100)).dump('l02_loaded')
    xkey(s, 'RIGHT', 2500, 0x6200).srand(quiet_seed(0x6280)).dump('l03_turn')
    s.wait(1000).quit().write(path)


# ---- play5: district 4 (PC4) from a prepared save (tools/verify/mksave.py) ----
# (11,12) facing south -> the fortune-teller (10,13), event 18
D4_TO_GYPSY = ['UP', 'RIGHT', 'UP']
# (10,13) facing west -> the doctor (8,11) through the sign of (9,10) (event 16)
D4_TO_DOCTOR = ['LEFT', 'LEFT', 'UP', 'LEFT', 'UP', 'UP', 'UP', 'UP', 'LEFT', 'UP', 'UP', 'LEFT', 'UP',
                'UP', 'RIGHT', 'UP']
# the doctor's cell again: every way out of the event but "too poor" puts
# the party back on (9,11), still facing west
D4_DOCTOR_AGAIN = ['UP']
# (8,11) facing west -> the hotel room (12,9) (key 31313131) through the signs
# of (9,10) and (14,9) (event 13)
D4_TO_HOTEL = ['LEFT', 'LEFT', 'UP', 'LEFT', 'UP', 'UP', 'UP', 'UP', 'RIGHT', 'UP', 'UP', 'UP', 'UP', 'UP',
               'RIGHT', 'UP', 'UP', 'RIGHT', 'UP', 'UP']
# (12,9) facing west -> the hotel keeper (13,8), event 11
D4_TO_KEEPER = ['LEFT', 'LEFT', 'UP', 'LEFT', 'UP']
D4_DOCTOR, D4_SIGN, D4_HOTEL_SIGN = (8, 11), (9, 10), (14, 9)
HP3 = 0x99A6 + 0x5C * 3 + 0x3A          # rifki's health word
KEY_H = 0x2368                          # 'h': hayIr (second answer)


def step_of(start, keys, cell):
    """index of the key that enters `cell` (start: x, y, facing 0 north .. 3 west)"""
    x, y, f = start
    for i, k in enumerate(keys):
        if k == 'LEFT':
            f = (f + 3) % 4
        elif k == 'RIGHT':
            f = (f + 1) % 4
        else:
            x, y = x + (0, 1, 0, -1)[f], y + (-1, 0, 1, 0)[f]
            if (x, y) == cell:
                return i
    raise ValueError(cell)


def portrait_click(s, p, seed, gap=1200):
    """left click on member p's portrait (3D view zones 1..4: x 197..229,
    y 2 + 38 (p - 1) .. +32; the point is inside with and without the
    cursor's hot point offset), driver coordinates"""
    x, y = 2 * 205, 10 + 38 * (p - 1)
    s.srand(quiet_seed(seed)).mouse(x, y, 0, gap // 4).mouse(x, y, 1, gap // 4)
    return s.mouse(x, y, 0, gap // 2)


SIGN_WAIT = 14000       # a one-line sign: 240 rounds of 20 ms (far longer in DOSBox-X)
SHOP_WAIT = 8000        # a shop's greeting: 40 rounds


def play5(path, stop=None):
    """from the prepared save play5.sav (installed as SAKLA001.SAV by
    `pair.py --install`): main menu, load, then district 4 (PC4):
    the fortune-teller (event 18: question, pay 5, four texts, two music
    changes, findminmax + say), the doctor (key 31313127) four times -
    refuse; accept and pick members that are not dead (F1, a click on
    portrait 2, F4), Esc; accept and pick the dead one (click on portrait
    3): revived, fee paid; accept with too little money - the hotel room
    (key 31313131: no encounters while camping) with a camp, the hotel
    keeper (event 11), and a save to a new slot"""
    s = Script(seed=1234)
    s.at(50000).dump('d00_menu').key('DOWN', 1000).key('ENTER', 3000).dump('d01_loadmenu')
    s.key('ENTER', 1000)                                    # the only entry: SAKLA001.SAV
    for i, t in enumerate(range(s.t, s.t + 20000, 2000)):   # no portrait blink can start
        s.at(t).srand(quiet_seed(0x6000 + 0x10 * i))
    s.at(s.t + 20000).srand(quiet_seed(0x6100)).dump('d02_loaded')
    s.after(XGAP)
    # the fortune-teller: the first text is followed by the question at once
    rwalk(s, D4_TO_GYPSY, 0x6200)
    s.wait(3000).srand(quiet_seed(0x6300)).dump('d03_gypsy_q')
    rkey(s, KEY_E, 0x6310, 5000).dump('d04_gypsy_text')     # pay 5; the first of four texts
    s.sync(FR_FLAGS + 12, 0x0010).srand(quiet_seed(0x6320))  # .. flag 100, then music 1 (fades)
    quiet_wait(s, 12000, 0x6330).dump('d05_gypsy_done')
    if stop == 'gypsy':
        return s.quit().write(path)
    # the doctor, first visit: refuse
    i = step_of((10, 13, 3), D4_TO_DOCTOR, D4_SIGN)
    rwalk(s, D4_TO_DOCTOR, 0x6400, extra={i: SIGN_WAIT})
    s.wait(SHOP_WAIT).srand(quiet_seed(0x6600)).dump('d06_doctor_q')
    rkey(s, KEY_H, 0x6610, 4000).dump('d07_refused')
    # second visit: accept, pick members who are not dead, cancel
    rwalk(s, D4_DOCTOR_AGAIN, 0x6700)
    s.wait(SHOP_WAIT).srand(quiet_seed(0x6800)).dump('d08_doctor_q2')
    rkey(s, KEY_E, 0x6810, 3000).dump('d09_who')
    rkey(s, K['F1'], 0x6820, 3000).dump('d10_not_dead_f1')
    portrait_click(s, 2, 0x6830).wait(1800).dump('d11_not_dead_click2')
    rkey(s, 0x3E00, 0x6840, 3000).dump('d12_not_dead_f4')
    rkey(s, 'ESC', 0x6850, 4000).dump('d13_cancelled')
    if stop == 'doctor2':
        return s.quit().write(path)
    # third visit: the dead member, by a click on his portrait
    rwalk(s, D4_DOCTOR_AGAIN, 0x6900)
    s.wait(SHOP_WAIT).srand(quiet_seed(0x6A00)).dump('d14_doctor_q3')
    rkey(s, KEY_E, 0x6A10, 3000).dump('d15_who')
    portrait_click(s, 3, 0x6A20, gap=1200)
    s.sync(HP3, 1).srand(quiet_seed(0x6A30))                # revived (two more texts follow)
    quiet_wait(s, 12000, 0x6A40).dump('d16_revived')
    # fourth visit: accept, too poor
    rwalk(s, D4_DOCTOR_AGAIN, 0x6B00)
    s.wait(SHOP_WAIT).srand(quiet_seed(0x6C00)).dump('d17_doctor_q4')
    rkey(s, KEY_E, 0x6C10, 0)
    quiet_wait(s, 8000, 0x6C20).dump('d18_too_poor')
    if stop == 'doctor':
        return s.quit().write(path)
    # the hotel: two signs on the way, the room, a camp in it
    start = (8, 11, 3)
    rwalk(s, D4_TO_HOTEL, 0x6D00, extra={step_of(start, D4_TO_HOTEL, D4_SIGN): SIGN_WAIT,
                                         step_of(start, D4_TO_HOTEL, D4_HOTEL_SIGN): SIGN_WAIT})
    s.srand(quiet_seed(0x6F00)).dump('d19_hotel_room')
    # the camp advances one minute per 50 ms of clock plus its drawing (more
    # emulated time in DOSBox-X): the checkpoint inside it is anchored on the
    # calendar minute (3D64: 9:56 + 24), not on the clock
    rkey(s, key_of('k'), 0x6F10, 0).sync(0x3D64, 20).dump('d20_camp')
    s.wait(40000).srand(quiet_seed(0x6F20)).dump('d21_rested')
    rwalk(s, D4_TO_KEEPER, 0x7000, extra={len(D4_TO_KEEPER) - 1: 20000})
    s.srand(quiet_seed(0x7100)).dump('d22_keeper')
    rkey(s, 0x1F73, 0x7200).dump('d23_savemenu')            # S: the new slot is entry 2
    s.key('DOWN', 1000).key('ENTER', 1500).text('ef', 400).key('ENTER', 1500).key('ENTER', 4000)
    s.srand(quiet_seed(0x7210)).dump('d24_saved')
    if stop == 'save':
        return s.wait(1000).quit().write(path)
    # 3DSYS keys no other play-through uses (from (13,8) facing north)
    s.wait(XGAP)
    rkey(s, 'DOWN', 0x7300).dump('d25_back')                # a step back to (13,9)
    rkey(s, key_of('4'), 0x7310).dump('d26_sidestep_l')     # sidesteps: (12,9), the hotel room,
    rkey(s, key_of('6'), 0x7320).dump('d27_sidestep_r')     # and back to (13,9)
    rkey(s, 'PGDN', 0x7330, 4000).dump('d28_bigview')       # the 200x200 view
    rkey(s, 'RIGHT', 0x7340).dump('d29_bigview_turn')
    rkey(s, 'PGUP', 0x7350, 4000).dump('d30_smallview')
    rkey(s, K['F1'], 0x7360, 4000).dump('d31_charscr1')     # the character screen
    rkey(s, 0x3D00, 0x7370, 3000).dump('d32_charscr3')      # F3: the revived member
    rkey(s, 'ESC', 0x7380, 5000).dump('d33_view')
    rkey(s, key_of('t'), 0x7390, 3000).dump('d34_options')  # the options list
    rkey(s, 'ESC', 0x73A0, 3000).dump('d35_view')
    # N: where are we?  With flag 82 the city map (har4.cmp) first; both
    # waits (2000 and 2400 rounds) are ended by a key
    rkey(s, key_of('n'), 0x73B0, 0)
    quiet_wait(s, 8000, 0x73C0).dump('d36_citymap')
    s.srand(quiet_seed(0x73D0)).key('SPACE', 4000).dump('d37_location')
    s.srand(quiet_seed(0x73E0)).key('SPACE', 0)
    quiet_wait(s, 10000, 0x73F0).dump('d38_view')
    s.wait(1000).quit().write(path)


def play5save(path):
    play5(path, 'save')


def startup(path):
    """start-up check: dumps at the first input reads of the intro (250 ms,
    1 s, 5 s of the game clock), no input - for
    `vcompare.py --dg-detail --clock` (docs/VERIFY.md, "Start-up")"""
    s = Script(seed=1234)
    for t in (250, 1000, 5000):
        s.at(t).dump(f's_{t:05d}')
    s.at(6000).quit().write(path)


def play5gypsy(path):
    play5(path, 'gypsy')


def play5doctor2(path):
    play5(path, 'doctor2')


def play5doctor(path):
    play5(path, 'doctor')


# the item exchange screen's zones (fr_item_exchange, read from a dump:
# 16E0:E5F5 + 8 z): inventory slot s = 1..10 at x 7 + 17 (s - 1), y 44..59;
# shop slot s at y 27..42; 26 exit (302..310, 3..13), 28 sell (270..282)
def zone_xy(z):
    """a point of zone z that is inside it with and without the cursor's
    hot point offset (+7 for the item shapes), in driver coordinates"""
    if 1 <= z <= 20:
        x, y = 7 + 17 * ((z - 1) % 10) + 3, (44 if z <= 10 else 27) + 4
    else:
        x, y = {26: (302, 3), 28: (270, 3)}[z]
        x, y = x + 1, y + 2
    return 2 * x, y


def mclick(s, z, seed, gap=1200):
    """left click on exchange zone z (move, press, release), re-seeded"""
    x, y = zone_xy(z)
    s.srand(quiet_seed(seed)).mouse(x, y, 0, gap // 4).mouse(x, y, 1, gap // 4)
    return s.mouse(x, y, 0, gap // 2)


def shop(s, name, seed):
    """in a shop (exchange screen mode 1/2): change the currencies (B),
    buy by keys (I: shop slot 1, Enter: inventory row, Right Right: slot 3,
    Enter: buy), sell by keys (I, Enter: inventory slot 1, S), buy by mouse
    (shop slot 3, inventory slot 4), sell by mouse (inventory slot 4, the
    sell zone), leave (Esc: the exit zone's click would reach the 3D view's
    arrows after the panel is gone); a dump after each step"""
    steps = [('b',), ('i',), ('ENTER',), ('RIGHT',), ('RIGHT',), ('ENTER',),
             ('i',), ('ENTER',), ('s',),
             ('m', 13), ('m', 4), ('m', 4), ('m', 28), ('ESC',)]
    for i, st in enumerate(steps):
        if st[0] == 'm':
            mclick(s, st[1], seed + 0x10 * i)
            tag = f'z{st[1]}'
        else:
            k = st[0]
            s.srand(quiet_seed(seed + 0x10 * i)).key(key_of(k) if len(k) == 1 else k,
                                                     0 if k == 'ESC' else 1000)
            if k == 'ESC':      # the palette/music fade back to the 3D view (100 ms steps)
                quiet_wait(s, 6000, seed + 0x10 * i + 1)
            tag = k
        s.dump(f'{name}_{i:02d}_{tag}')
    return s.wait(XGAP)


def probe4(path):
    """district entry: portraits every 2 s (port + original)"""
    s = Script(seed=1234)
    chargen_exact(s)
    story_skip(s)
    s.at(80000).every(150000, 2000, 'z')
    s.at(150001).quit()
    s.write(path)


def probe2(path):
    s = Script(seed=1234)
    chargen_reads(s)
    s.at(52000).every(140000, 2000, 'x')
    s.at(140000).quit()
    s.write(path)


def probe3(path):
    s = Script(seed=1234)
    chargen_reads(s, reseed=True)
    story_skip(s)
    s.at(150000).dump('c04_district')
    for i, k in enumerate(['RIGHT', 'RIGHT', 'UP']):
        s.key(k, 2500).dump(f'm{i}_{k}')
    s.key(0x1265, 8000).key('ESC', 1000)
    s.every(s.t + 40000, 2000, 'y')
    s.wait(40000).quit()
    s.write(path)


def rate(path):
    """probe: reads per ms in chargen"""
    s = Script(seed=1234)
    s.at(50000).key('ENTER').reads(0)
    for i in range(8):
        s.wait(500).dump(f'r{i}')
    s.wait(500).quit()
    s.write(path)



def bprobe2(path):
    """probe 2's sequence, one key per group"""
    bprobe(path, [['TAB']] * 5 + [['v', 'ENTER', 'o', 'ENTER']]
           + [['v', 'ENTER', 'o', 'ENTER', 'ENTER', 'o', 'ENTER', 'TAB']] * 24)


def gprobe(path):
    """after the lost dog battle: the game over (port timeline)"""
    s = Script(seed=1234)
    play3_prefix(s)
    battle_groups(s, DOG_BATTLE, 'c12_b')
    s.after(0)
    for i in range(30):
        s.wait(3000).dump(f'g{i:02d}')
        if i == 12:
            s.key(KEY_E, 0)
        if i == 16:
            s.key('ENTER', 0)
    s.quit().write(path)


def play4man(path):
    play4(path, 'man')


def play4d1(path):
    play4(path, 'd1')


def play4food(path):
    play4(path, 'food')


if __name__ == '__main__':
    globals()[sys.argv[1]](sys.argv[2])
