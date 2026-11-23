"""Borland far heap (src/rtl/bc_farheap.c) against the original allocator.

The original runs from the main() snapshot: DGROUP (brk variables), the heap
arena 2EAD:0000-A000:0000 and the program's MCB come from
build/oracle/MAIN_MEM.BIN, the heap state in the RTL code segment
(CS:2382-238C) too.  INT 21h AH=4Ah is served here by a Python transcription
of DOSBox's DOS_ResizeMemory working on the emulated MCB chain - independent
of the C model's shortcut (bc_farheap.c dos_setblock).
"""
import ctypes
import random

from lale import LOAD, check, fn, DG
import helpers as fh

CS_VARS = fh.CS_VARS
ARENA_LIN, ARENA_END = fh.ARENA_LIN, fh.ARENA_END
c_malloc, c_free, c_coreleft, c_addr, c_reset = fh.c_malloc, fh.c_free, fh.c_coreleft, fh.c_addr, fh.c_reset
c_realloc = fn('bc_farrealloc', ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32)
c_calloc = fn('bc_farcalloc', ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32)
c_nmalloc = fn('bc_malloc', ctypes.c_void_p, ctypes.c_uint16)
c_nrealloc = fn('bc_realloc', ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint16)
c_ncalloc = fn('bc_calloc', ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint16)
_arena, _cvars, _emu_cvars, _setup = fh.arena, fh.cvars, fh.emu_cvars, fh.setup_main_heap


def _brkvars(src):
    return bytes(src[0x7B:0x7D]) + bytes(src[0x87:0x93]) + bytes(src[0x9582:0x9584])


def test_reset_matches_main_snapshot():
    e, mem = _setup()
    fn('dg_reset', None)()
    c_reset()
    dgs = (LOAD + 0x16E0) * 16
    check(_brkvars(DG) == _brkvars(mem[dgs:dgs + 0x10000]), 'brk variables differ from main()')
    check(_cvars() == _emu_cvars(e), f'CS vars {_cvars()} vs {_emu_cvars(e)}')
    a = bytes(_arena())
    snap = mem[ARENA_LIN:ARENA_END]
    for seg in (0x2EAD, 0x4BB3, 0x4BD4):                  # block headers
        o = (seg - fh.ARENA_SEG) * 16
        check(a[o:o + 4] == snap[o:o + 4], f'header {seg:04X}')
    o = (0x4BD7 - fh.ARENA_SEG) * 16                         # everything above the break
    # Paragraph 9FFE (just below the MCB that links to upper memory) holds what
    # DOSBox-X itself left there while booting: 4C E7 1A E7 .. 00 F0 in the
    # Windows build's capture - what bc_farheap_reset() reproduces - and zeros
    # in a capture made with the macOS build of the same DOSBox-X version
    # (2026-11-23).  It is the emulator's stale data, not the game's; a
    # snapshot with zeros there is accepted, every other byte must match.
    p = (0x9FFE - fh.ARENA_SEG) * 16
    if snap[p:p + 16] == bytes(16):
        snap = snap[:p] + a[p:p + 16] + snap[p + 16:]
    check(a[o:] == snap[o:], 'arena above the break differs from the snapshot')
    check(c_coreleft() == e.call('0000:2987', ret='long'), 'farcoreleft at main()')


class Pair:
    def __init__(self, e, rnd, small=False):
        self.e, self.rnd, self.live = e, rnd, []      # [(segoff, cptr, nbytes)]
        self.small = small                            # mostly small blocks: long free lists

    def cmp(self, what, orig, cptr):
        cad = c_addr(cptr) if cptr else 0
        return check(orig == cad, f'{what}: original {orig >> 16:04X}:{orig & 0xFFFF:04X}, '
                                  f'port {cad >> 16:04X}:{cad & 0xFFFF:04X}')

    def fill(self, segoff, cptr, n):
        n = min(n, 0x30000)
        if not segoff or n <= 0:
            return
        data = self.rnd.randbytes(n)
        self.e.u.mem_write((segoff >> 16) * 16 + (segoff & 0xFFFF), data)
        ctypes.memmove(cptr, data, n)

    def size(self):
        r = self.rnd.random()
        if self.small and r < 0.97:
            return self.rnd.randrange(1, 2000)
        if r < 0.5:
            return self.rnd.randrange(1, 300)
        if r < 0.8:
            return self.rnd.randrange(300, 20000)
        if r < 0.95:
            return self.rnd.randrange(20000, 140000)
        return self.rnd.choice([0, 0xFFFF, 0x10000, 0xFFFED, 0xFFFEC, 0x100000, 0xFFFFFFFF])

    def step(self):
        e, rnd = self.e, self.rnd
        op = rnd.random()
        if op < 0.35 or not self.live:
            n = self.size()
            o = e.call('0000:25D6', n & 0xFFFF, n >> 16, ret='long')
            c = c_malloc(n)
            if self.cmp(f'farmalloc({n:#x})', o, c) and o:
                self.fill(o, c, n)
                self.live.append((o, c, n))
        elif op < 0.65:
            o, c, n = self.live.pop(rnd.randrange(len(self.live)))
            e.call('0000:24C2', o & 0xFFFF, o >> 16)
            c_free(c)
        elif op < 0.8:
            i = rnd.randrange(len(self.live))
            o, c, n = self.live[i]
            m = self.size() if rnd.random() < 0.7 else max(1, n + rnd.randrange(-40, 40))
            o2 = e.call('0000:273A', o & 0xFFFF, o >> 16, m & 0xFFFF, m >> 16, ret='long')
            c2 = c_realloc(c, m)
            self.cmp(f'farrealloc({o:#x}, {m:#x})', o2, c2)
            if m == 0 or o2:
                self.live.pop(i)
            if o2:
                self.live.append((o2, c2, m))
                if rnd.random() < 0.5:
                    self.fill(o2, c2, m)
        elif op < 0.85:
            a, b = rnd.randrange(1, 300), rnd.randrange(1, 300)
            o = e.call('0000:28F7', a, 0, b, 0, ret='long')
            c = c_calloc(a, b)
            if self.cmp(f'farcalloc({a}, {b})', o, c) and o:
                self.live.append((o, c, a * b))
        elif op < 0.9:
            n = rnd.randrange(0, 0x10000)
            o = e.call('0000:25CC', n, ret='long')
            c = c_nmalloc(n)
            if self.cmp(f'malloc({n:#x})', o, c) and o:
                self.live.append((o, c, n))
        elif op < 0.95:
            a, b = rnd.randrange(0, 400), rnd.randrange(0, 400)
            o = e.call('0000:1F64', a, b, ret='long')
            c = c_ncalloc(a, b)
            if self.cmp(f'calloc({a}, {b})', o, c) and o:
                self.live.append((o, c, a * b))
        else:
            i = rnd.randrange(len(self.live))
            o, c, n = self.live[i]
            m = rnd.randrange(0, 0x10000)
            o2 = e.call('0000:2733', o & 0xFFFF, o >> 16, m, ret='long')
            c2 = c_nrealloc(c, m)
            self.cmp(f'realloc({o:#x}, {m:#x})', o2, c2)
            if m == 0 or o2:
                self.live.pop(i)
            if o2:
                self.live.append((o2, c2, m))
        left_o = e.call('0000:2987', ret='long')
        return check(left_o == c_coreleft(), f'farcoreleft {left_o:#x} vs {c_coreleft():#x}')

    def full_compare(self, tag):
        e = self.e
        ok = check(_emu_cvars(e) == _cvars(), f'{tag}: CS vars {_emu_cvars(e)} vs {_cvars()}')
        dgs = (LOAD + 0x16E0) * 16
        ok &= check(_brkvars(e.u.mem_read(dgs, 0x10000)) == _brkvars(DG), f'{tag}: brk variables')
        o = bytes(e.u.mem_read(ARENA_LIN, ARENA_END - ARENA_LIN))
        c = bytes(_arena())
        if o != c:
            i = next(k for k in range(len(o)) if o[k] != c[k])
            lin = ARENA_LIN + i
            check(False, f'{tag}: arena differs at {lin >> 4:04X}:{lin & 15:X} '
                         f'({o[i]:02X} vs {c[i]:02X})')
            ok = False
        return ok


def _run(seed, steps, small=False):
    e, mem = _setup()
    ctypes.memmove(_arena(), mem[ARENA_LIN:ARENA_END], ARENA_END - ARENA_LIN)
    p = Pair(e, random.Random(seed), small)
    for k in range(steps):
        if not p.step():
            return
        if k % 25 == 24 and not p.full_compare(f'seed {seed} step {k}'):
            return
    p.full_compare(f'seed {seed} end')
    # free everything (in random order): the heap must shrink back alike
    while p.live:
        o, c, n = p.live.pop(p.rnd.randrange(len(p.live)))
        e.call('0000:24C2', o & 0xFFFF, o >> 16)
        c_free(c)
        if not check(e.call('0000:2987', ret='long') == c_coreleft(), f'seed {seed}: coreleft after free'):
            return
    p.full_compare(f'seed {seed} all freed')


def test_random_sequences():
    for seed in range(6):
        _run(seed, 400)
    for seed in range(100, 104):
        _run(seed, 1500, small=True)


def test_exhaust_memory():
    """Allocate until farmalloc fails, free every other block, refill."""
    e, mem = _setup()
    ctypes.memmove(_arena(), mem[ARENA_LIN:ARENA_END], ARENA_END - ARENA_LIN)
    p = Pair(e, random.Random(99))
    while True:
        n = p.rnd.randrange(1000, 30000)
        o = e.call('0000:25D6', n & 0xFFFF, n >> 16, ret='long')
        c = c_malloc(n)
        if not p.cmp(f'farmalloc({n})', o, c) or not o:
            break
        p.live.append((o, c, n))
    check(e.call('0000:2987', ret='long') == c_coreleft(), 'coreleft when full')
    for o, c, n in p.live[::2]:
        e.call('0000:24C2', o & 0xFFFF, o >> 16)
        c_free(c)
    p.live = p.live[1::2]
    for _ in range(200):
        p.step()
    p.full_compare('exhaust')
