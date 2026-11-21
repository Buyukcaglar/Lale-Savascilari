"""MEMMAN (seg09D4), DSMI MemAlloc (seg0E9E) and NKONTROL (seg0DB7) against
the original.  Environment (EMS, the real far heap on both sides, fatal
recorded) from helpers.make_emu / reset_c; heap_free = farcoreleft() at the
start (a filler block on both sides)."""
import ctypes
import random

from lale import NAMES, check, fn, lib, dg_from_emu, DG
from helpers import (make_emu, reset_c, ems_mem, FRAME, FATALS, EMS_DG, s16, pc_reset, c_would_fit,
                     c_coreleft)

C = ctypes
u8, u16, i16, u32, vp, cp = C.c_uint8, C.c_uint16, C.c_int16, C.c_uint32, C.c_void_p, C.c_char_p
RT_DG = 0x1F04                      # runtime DGROUP segment (load 0824h)
DGA = C.addressof(DG)

c_mm_init = fn('mm_init', i16, u32, u32)
c_mm_alloc = fn('mm_alloc', vp, u32, u8)
c_mm_free = fn('mm_free', u8, vp, i16, cp)
c_mm_coreleft = fn('mm_coreleft', u32, u8)
c_mm_duplicate = fn('mm_duplicate', vp, vp, u8)
c_mm_change_type = fn('mm_change_type', u8, vp, u8)
c_mm_copy_block = fn('mm_copy_block', None, vp, vp)
c_mm_copy_range = fn('mm_copy_range', None, vp, vp, u32, u32, u32)
c_mm_shutdown = fn('mm_shutdown', None)
c_mm_ems_lock = fn('mm_ems_lock', None, vp, i16, cp)
c_mm_ems_unlock = fn('mm_ems_unlock', None, vp)
c_mm_map = fn('mm_map', None, vp, C.POINTER(vp), C.POINTER(u16), C.POINTER(u16), u32)
c_mm_surface_alloc = fn('mm_surface_alloc', vp, u8, i16, i16, u8)
c_mm_conv_ptr = fn('mm_conv_ptr', vp, vp)
c_mm_farmalloc = fn('mm_farmalloc', vp, u32)
c_ctl_free_checked = fn('ctl_free_checked', None, u16, i16, cp)
c_ctl_timer_tick = fn('ctl_timer_tick', None)
c_ctl_ctrlbrk = fn('ctl_ctrlbrk_handler', i16)
c_ctl_install = fn('ctl_install', None)
c_ctl_uninstall = fn('ctl_uninstall', None)

ROW = {n: fn(n, None, vp, vp, u16, u8, u8) for n in ('mm_copy_recolor', 'mm_copy_09CC', 'mm_rle_09F6')}
ROW4 = {n: fn(n, None, vp, vp, u16, u8) for n in ('mm_copy_transparent', 'mm_rle_09A8')}
c_mm_memcpy = fn('mm_memcpy', None, vp, vp, u16)

MM_DG = [(0x6E02, 0x6E0E), (0x750A, 0x750B), (0xD9F8, 0xDA2C), (0xDE30, 0xDE34), (0xE29B, 0xE29B + 90 * 8)]
MM_DG += [(0xDA30 + 7 * i, 0xDA33 + 7 * i) for i in range(4)]
FILE = b'T:TEST\\FILE.L'


def L(v):
    return [v & 0xFFFF, (v >> 16) & 0xFFFF]


class OrigMM:
    def __init__(self, e):
        self.e = e
        self.scr = e.alloc(0x400)            # hp, out, len0, len1, slot, ctx, file
        e.wr(self.scr, 0x100, FILE + b'\0', runtime=True)
        self.buf = e.alloc(0x10000)

    def call(self, name, *a, ret='int'):
        return self.e.call(NAMES[name], *a, ret=ret)

    def rec(self, off):
        return [off, RT_DG]


def rec_c(off):
    return DGA + off


def records_equal(eo, label):
    """Compare the handle table: size and type always, word 0 for EMS."""
    a = eo.dgroup(0xDA48, 990)
    b = bytes(DG[0xDA48:0xDA48 + 990])
    for i in range(110):
        x, y = a[9 * i:9 * i + 9], b[9 * i:9 * i + 9]
        if x[4:9] != y[4:9] or (x[8] == 1 and x[0:2] != y[0:2]):
            check(False, f'{label}: record {i} {x.hex()} vs {y.hex()}')
            return


def conv_bytes_orig(e, off, n):
    o, s = e.rw(0x16E0, off), e.rw(0x16E0, off + 2)
    return e.rd(s, o, n, runtime=True)


def conv_bytes_c(off, n):
    p = c_mm_conv_ptr(rec_c(off))
    return C.string_at(p, n) if p else b''


def mm_ops(rnd, n):
    ops = []
    for _ in range(n):
        k = rnd.random()
        if k < 0.18:
            ops.append(('alloc', rnd.choice([1, 16, 100, 3000, 0x4000, 0xC000, 0xFF00, 0x12000,
                                              rnd.randrange(1, 0x20000)]), rnd.choice([0, 1, 1])))
        elif k < 0.26:
            ops.append(('surf', rnd.randrange(1, 90), rnd.choice([16, 100, 320, 640]),
                        rnd.choice([15, 100, 200, 204]), rnd.choice([0, 1, 2, 2])))
        elif k < 0.34:
            ops.append(('free', rnd.randrange(64)))
        elif k < 0.40:
            ops.append(('dup', rnd.randrange(64), rnd.choice([0, 1])))
        elif k < 0.46:
            ops.append(('chg', rnd.randrange(64)))
        elif k < 0.54:
            ops.append(('cblock', rnd.randrange(64), rnd.randrange(64)))
        elif k < 0.64:
            ops.append(('crange', rnd.randrange(64), rnd.randrange(64), rnd.random(), rnd.random(),
                        rnd.choice([1, 7, 100, 3000, 0x5000])))
        else:
            ops.append(('map', rnd.randrange(64), rnd.random(), rnd.randrange(64), rnd.random(),
                        rnd.choice([0, 0x100, 0xFFFF]), rnd.random() < 0.5))
    return ops


def run_mm(ops, side, e=None, heap_free=None, seed=0):
    rnd = random.Random(seed)
    res, live = [], []
    if side == 'o':
        o = OrigMM(e)
        res.append(s16(o.call('mm_init', *L(0x240000), *L(0x440000))[0]))
    else:
        reset_c(heap_free)
        res.append(c_mm_init(0x240000, 0x440000))
        ctx = (C.c_uint8 * 10)()
        lens = [u16(), u16()]
        slot = u16()
        outp = vp()

    def size_of(off):
        return int.from_bytes(bytes(DG[off + 4:off + 8]) if side == 'c' else e.dgroup(off + 4, 4), 'little')

    def type_of(off):
        return e.dgroup(off + 8, 1)[0] if side == 'o' else DG[off + 8]

    def conv_fails(size):                      # farmalloc would fail: fatal, which
        return not (e.heap.would_fit(size) if side == 'o' else c_would_fit(size))   # does not return

    for op in ops:
        data = bytes(rnd.randrange(256) for _ in range(512))
        kind = op[0]
        if (len(e.fatals) if side == 'o' else FATALS.value):
            break
        if kind == 'alloc' and op[2] == 0 and conv_fails(op[1]):
            continue
        if kind == 'surf' and op[4] == 0 and conv_fails(op[2] * op[3]):
            continue
        if kind in ('dup', 'chg') and live:
            h = live[op[1] % len(live)]
            to_conv = (op[2] == 0) if kind == 'dup' else (type_of(h) == 1)
            if to_conv and conv_fails(size_of(h)):
                continue
        if kind in ('alloc', 'surf'):
            if len(live) >= 40:
                continue
            if kind == 'alloc':
                r = (o.call('mm_alloc', *L(op[1]), op[2])[0] if side == 'o'
                     else c_mm_alloc(op[1], op[2]) - DGA)
            else:
                r = (o.call('mm_surface_alloc', op[1], op[2], op[3], op[4])[0] if side == 'o'
                     else c_mm_surface_alloc(*op[1:]) - DGA)
            res.append((kind, r))
            live.append(r)
        elif not live:
            continue
        elif kind == 'free':
            r = live.pop(op[1] % len(live))
            res.append(('free', o.call('mm_free', *o.rec(r), 0x123, 0x100, o.scr)[0] & 0xFF
                        if side == 'o' else c_mm_free(rec_c(r), 0x123, FILE)))
        elif kind == 'dup':
            if len(live) >= 40:
                continue
            h = live[op[1] % len(live)]
            r = (o.call('mm_duplicate', *o.rec(h), op[2])[0] if side == 'o'
                 else c_mm_duplicate(rec_c(h), op[2]) - DGA)
            res.append(('dup', r))
            live.append(r)
        elif kind == 'chg':
            h = live[op[1] % len(live)]
            t = (e.dgroup(h + 8, 1)[0] if side == 'o' else DG[h + 8]) ^ 1
            r = (o.call('mm_change_type', *o.rec(h), t)[0] & 0xFF if side == 'o'
                 else c_mm_change_type(rec_c(h), t))
            res.append(('chg', r))
        elif kind == 'cblock':
            a, b = live[op[1] % len(live)], live[op[2] % len(live)]
            if side == 'o':
                o.call('mm_copy_block', *o.rec(a), *o.rec(b))
            else:
                c_mm_copy_block(rec_c(a), rec_c(b))
        elif kind == 'crange':
            a, b = live[op[1] % len(live)], live[op[2] % len(live)]
            n = min(op[5], size_of(a), size_of(b))
            ao, bo = int(op[3] * (size_of(a) - n)), int(op[4] * (size_of(b) - n))
            if side == 'o':
                o.call('mm_copy_range', *o.rec(a), *o.rec(b), *L(ao), *L(bo), *L(n))
            else:
                c_mm_copy_range(rec_c(a), rec_c(b), ao, bo, n)
        else:                                  # lock, map two windows, write, unlock
            a, b = live[op[1] % len(live)], live[op[3] % len(live)]
            offs = [int(op[2] * size_of(a)), int(op[4] * size_of(b))]
            got = []
            if side == 'o':
                ee, s = e, o.scr
                if op[6]:
                    o.call('mm_ems_lock', 0x50, s, 0x77, 0x100, s)
                ee.ww(s, 0x10, 0, runtime=True)                       # slot
                for i, (h, off) in enumerate(((a, offs[0]), (b, offs[1]))):
                    ee.ww(s, 0x0, h, runtime=True)
                    ee.ww(s, 0x2, RT_DG, runtime=True)
                    ee.ww(s, 0x8 + 2 * i, op[5], runtime=True)
                    o.call('mm_map', 0, s, 4, s, 8 + 2 * i, s, 0x10, s, *L(off))
                    po, ps = ee.rw(s, 4, runtime=True), ee.rw(s, 6, runtime=True)
                    got.append((ps, po))
                for i, (ps, po) in enumerate(got):
                    n = min(ee.rw(s, 8 + 2 * i, runtime=True), 300)
                    ee.wr(ps, po, data[:n], runtime=True)
                lens_v = [ee.rw(s, 8, runtime=True), ee.rw(s, 0xA, runtime=True), ee.rw(s, 0x10, runtime=True)]
                rel = []
                for (ps, po), h in zip(got, (a, b)):
                    if ps == 0xE000:
                        rel.append(('ems', po))
                    else:
                        bo, bs = ee.rw(0x16E0, h), ee.rw(0x16E0, h + 2)
                        rel.append(('conv', ps * 16 + po - (bs * 16 + bo)))
                if op[6]:
                    o.call('mm_ems_unlock', 0x50, s)
            else:
                if op[6]:
                    c_mm_ems_lock(C.addressof(ctx), 0x77, FILE)
                slot.value = 0
                ptrs = []
                for i, (h, off) in enumerate(((a, offs[0]), (b, offs[1]))):
                    lens[i].value = op[5]
                    c_mm_map(rec_c(h), C.byref(outp), C.byref(lens[i]), C.byref(slot), off)
                    ptrs.append(outp.value or 0)
                for i, p in enumerate(ptrs):
                    n = min(lens[i].value, 300)
                    if p:
                        C.memmove(p, data[:n], n)
                lens_v = [lens[0].value, lens[1].value, slot.value]
                rel = []
                for p, h in zip(ptrs, (a, b)):
                    if FRAME <= p < FRAME + 0x10000:
                        rel.append(('ems', p - FRAME))
                    else:
                        rel.append(('conv', p - (c_mm_conv_ptr(rec_c(h)) or 0)))
                if op[6]:
                    c_mm_ems_unlock(C.addressof(ctx))
            res.append(('map', lens_v, rel))
        res.append(('core', o.call('mm_coreleft', 2, ret='long') if side == 'o' else c_mm_coreleft(2)))
    # block contents
    for h in sorted(set(live)):
        t = e.dgroup(h + 8, 1)[0] if side == 'o' else DG[h + 8]
        if t == 0:
            n = min(size_of(h), 0x2000)
            res.append(('conv', h, conv_bytes_orig(e, h, n) if side == 'o' else conv_bytes_c(h, n)))
    res.append(('fatals', len(e.fatals) if side == 'o' else FATALS.value))
    return res


def compare_mm(ops, seed, heap_free=None, shutdown=False):
    e = make_emu(heap_free)
    pc_reset()
    ro = run_mm(ops, 'o', e, heap_free, seed)
    if shutdown:
        e.call(NAMES['mm_shutdown'])
    memo = ems_mem(1)
    dgo = e.dgroup(0, 0x10000)
    rc = run_mm(ops, 'c', None, heap_free, seed)
    if shutdown:
        c_mm_shutdown()
    memc = ems_mem(1)
    label = f'seed {seed}'
    for i, (a, b) in enumerate(zip(ro, rc)):
        if not check(a == b, f'{label} step {i}: orig {str(a)[:160]} port {str(b)[:160]}'):
            break
    check(len(ro) == len(rc), f'{label}: result count {len(ro)} vs {len(rc)}')
    check(memo == memc, f'{label}: EMS memory differs')
    for lo, hi in MM_DG + EMS_DG:
        check(dgo[lo:hi] == bytes(DG[lo:hi]), f'{label}: DGROUP {lo:04X}-{hi:04X} '
              f'{dgo[lo:hi][:24].hex()} vs {bytes(DG[lo:hi])[:24].hex()}')

    class _E:
        @staticmethod
        def dgroup(o, n):
            return dgo[o:o + n]
    records_equal(_E, label)


def test_main_like_sequence():
    """mm_init as main() does, then the screen-sized surfaces and buffers."""
    ops = [('surf', 1, 320, 204, 2), ('surf', 2, 320, 204, 2), ('surf', 9, 15, 15, 0),
           ('surf', 10, 15, 15, 0), ('surf', 0x4B, 90, 15, 2), ('surf', 3, 320, 200, 2),
           ('surf', 4, 320, 200, 2), ('surf', 5, 320, 200, 2), ('alloc', 0x10000, 1),
           ('map', 0, 0.0, 1, 0.5, 0xFFFF, True), ('map', 5, 0.3, 6, 0.99, 0x100, False),
           ('crange', 0, 7, 0.1, 0.2, 3000), ('cblock', 7, 3), ('chg', 3), ('free', 2)]
    compare_mm(ops, 0, heap_free=0x40000)


def test_random_sequences():
    for seed in range(8):
        rnd = random.Random(0x9D4 + seed)
        compare_mm(mm_ops(rnd, 70), seed, heap_free=rnd.choice([0x20000, 0x30000, 0x3E000]),
                   shutdown=seed % 2 == 1)


def test_mm_init_results():
    # farcoreleft() at the check = heap_free - what DDSInit and mm_init's
    # emsHeapInit (descriptor table) allocate: put it just below / at the limits
    reset_c(0x40000)
    fn('emsHeapInit', i16, u16, u16)(0x900, 0x1100)
    cost = 0x40000 - c_coreleft()
    results = set()
    for lim, sound in ((250000, 1), (190000, 0)):
        for hf in ((lim & ~15) - 16 + cost, ((lim + 15) & ~15) + cost):
            e = make_emu(hf)
            pc_reset()
            e.wr(0x16E0, 0x73D0, bytes([sound]))
            o = OrigMM(e)
            r1 = s16(o.call('mm_init', *L(0x240000), *L(0x440000))[0])
            reset_c(hf)
            DG[0x73D0] = sound
            r2 = c_mm_init(0x240000, 0x440000)
            check(r1 == r2, f'mm_init free={hf:X} sound={sound}: {r1} vs {r2}')
            results.add(r1)
    check(results == {1, -3}, f'mm_init limits not reached: {results}')
    for pages in (100, 2048):              # EMS too small: -2
        e = make_emu()
        pc_reset()
        fn('pc_ems_configure', None, u16)(pages)
        r1 = s16(OrigMM(e).call('mm_init', *L(0x240000), *L(0x440000))[0])
        reset_c()
        fn('pc_ems_configure', None, u16)(pages)
        r2 = c_mm_init(0x240000, 0x440000)
        check(r1 == r2, f'mm_init pages={pages}: {r1} vs {r2}')
    fn('pc_ems_configure', None, u16)(2048)


def test_row_copies():
    rnd = random.Random(0x9D40)
    e = make_emu()
    src, dst = e.alloc(0x400), e.alloc(0x400)
    for _ in range(60):
        n = rnd.randrange(1, 0x300)
        s = bytes(rnd.choice([0, 0, 5, rnd.randrange(256)]) for _ in range(0x400))
        d = bytes(rnd.randrange(256) for _ in range(0x400))
        key, col = rnd.choice([0, 5]), rnd.randrange(256)
        for name in list(ROW) + list(ROW4) + ['mm_memcpy']:
            e.wr(src, 0, s, runtime=True)
            e.wr(dst, 0, d, runtime=True)
            cs = C.create_string_buffer(s, 0x400)
            cd = C.create_string_buffer(d, 0x400)
            if name in ROW:
                e.call(NAMES[name], e.far(src, 0), e.far(dst, 0), n, key, col)
                ROW[name](C.addressof(cs), C.addressof(cd), n, key, col)
            elif name in ROW4:
                e.call(NAMES[name], e.far(src, 0), e.far(dst, 0), n, key)
                ROW4[name](C.addressof(cs), C.addressof(cd), n, key)
            else:
                e.call(NAMES[name], e.far(src, 0), e.far(dst, 0), n)
                c_mm_memcpy(C.addressof(cs), C.addressof(cd), n)
            check(e.rd(dst, 0, 0x400, runtime=True) == cd.raw, f'{name} n={n}')
    # overlapping mm_memcpy (rep movsd semantics)
    for shift in (1, 2, 3, 5):
        buf = bytes(range(256)) * 2
        e.wr(src, 0, buf, runtime=True)
        e.call(NAMES['mm_memcpy'], e.far(src, 0), e.far(src, shift), 200)
        cb = C.create_string_buffer(buf, 512)
        c_mm_memcpy(C.addressof(cb), C.addressof(cb) + shift, 200)
        check(e.rd(src, 0, 512, runtime=True) == cb.raw, f'mm_memcpy overlap {shift}')


def test_memalloc_codes():
    p = vp()
    check(fn('MemAlloc', i16, C.c_int32, C.POINTER(vp))(16, C.byref(p)) == 0 and p.value, 'MemAlloc ok')
    check(fn('MemFree', i16, vp)(p) == 0, 'MemFree ok')
    check(fn('MemFree', i16, vp)(None) == 3, 'MemFree NULL -> 3')
    q = vp(0x1234)
    check(fn('MemAlloc', i16, C.c_int32, C.POINTER(vp))(0, C.byref(q)) == 2 and q.value == 0x1234,
          'MemAlloc(0) -> 2, *p untouched')
    check(fn('MemCalloc', i16, C.c_int32, C.c_int32, C.POINTER(vp))(3, 8, C.byref(p)) == 0
          and C.string_at(p, 24) == bytes(24), 'MemCalloc zeroed')
    fn('MemFree', i16, vp)(p)


def test_nkontrol():
    e = make_emu()
    reset_c()
    dg_from_emu(e)
    seq_o, seq_c = [], []
    for _ in range(9):
        seq_o.append(e.call(NAMES['ctl_ctrlbrk_handler'])[0])
        seq_c.append(c_ctl_ctrlbrk())
    check(seq_o == seq_c, f'ctrlbrk handler {seq_o} vs {seq_c}')
    check(e.dgroup(0x7510, 2) == bytes(DG[0x7510:0x7512]), 'DGROUP 7510')
    # timer tick: an IRET routine - call it through a far-call frame + flags
    e.wr(0x16E0, 0xE74A, (0xFFFFFFF9).to_bytes(4, 'little'))
    DG[0xE74A:0xE74E] = (0xFFFFFFF9).to_bytes(4, 'little')
    for _ in range(3):
        e.call(NAMES['ctl_timer_tick'], 0x0202)
        c_ctl_timer_tick()
    check(e.dgroup(0xE74A, 4) == bytes(DG[0xE74A:0xE74E]), 'ms clock')
    # install/uninstall flag
    c_ctl_install()
    check(DG[0x750A] & 0x10, 'installed flag')
    c_ctl_install()
    check(FATALS.value == 1, 'second install is fatal')
    c_ctl_uninstall()
    check(not DG[0x750A] & 0x10, 'uninstalled')
    # ctl_free_checked on a surface slot
    e2 = make_emu()
    pc_reset()
    o = OrigMM(e2)
    o.call('mm_init', *L(0x240000), *L(0x440000))
    o.call('mm_surface_alloc', 7, 100, 50, 1)
    o.call('mm_surface_alloc', 8, 100, 50, 0)
    for slot in (7, 8, 8):
        o.call('ctl_free_checked', 0xE29F + 8 * slot, RT_DG, 0x99, 0x100, o.scr)
    reset_c()
    c_mm_init(0x240000, 0x440000)
    c_mm_surface_alloc(7, 100, 50, 1)
    c_mm_surface_alloc(8, 100, 50, 0)
    for slot in (7, 8, 8):
        c_ctl_free_checked(0xE29F + 8 * slot, 0x99, FILE)
    check(e2.dgroup(0xE29B, 0x60) == bytes(DG[0xE29B:0xE29B + 0x60]), 'surface table after free')
    check(len(e2.fatals) == FATALS.value, f'fatals {len(e2.fatals)} vs {FATALS.value}')
    records_equal(e2, 'ctl_free_checked')
