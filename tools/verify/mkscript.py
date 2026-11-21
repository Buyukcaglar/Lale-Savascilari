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
    reads(), relative to the previous event in counted input reads (+n)"""
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

    def emit(self, text):
        if self.rel:
            self.lines.append(f'+{self.pend} {text}')
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
    s.dump('c03_after_chargen')
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
        # 40000 reads: the enemy's turn after TAB (clock-timed animations, the
        # port makes fewer reads per ms) is over on both sides at the dump
        s.wait(10000).srand(0x2200 + i).key(k, 40000).dump(f'c11_b{i:02d}_{k}')
    s.wait(1000).quit()
    s.write(path)


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


if __name__ == '__main__':
    globals()[sys.argv[1]](sys.argv[2])
