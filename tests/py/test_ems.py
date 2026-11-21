"""DSMI EMS (seg1134 raw calls, seg1154 EMS heap) against the original.

Both sides use the port's emulated EMS (lib pc_int67/pc_ems_move): the
original through tools/emu86.py attach_pc, the C directly; environment from
helpers.make_emu / reset_c.  Between the
original run and the C run the PC is reset, so handles/pages coincide and
EMS memory can be compared byte for byte (pc_ems_handle_mem).
"""
import ctypes
import random
import struct

from lale import NAMES, check, fn, dg_from_emu, DG
from helpers import (make_emu, reset_c, ems_mem, far_off, c_ptr_far, s16, pc_reset,  # noqa: F401
                     dg_reset, FRAME, EMS_DG)

C = ctypes
u8, u16, i16, i32, u32, vp = C.c_uint8, C.c_uint16, C.c_int16, C.c_int32, C.c_uint32, C.c_void_p

pc_ems_configure = fn('pc_ems_configure', None, u16)
pc_ems_set_present = fn('pc_ems_set_present', None, C.c_int)

c_emsInit = fn('emsInit', i16)
c_emsAllocPages = fn('emsAllocPages', i16, u16)
c_emsMapPage = fn('emsMapPage', i16, u16, u16)
c_emsSavePageMap = fn('emsSavePageMap', u16)
c_emsRestorePageMap = fn('emsRestorePageMap', u16)
c_emsGetFreePages = fn('emsGetFreePages', u16)
c_emsFreeHandle = fn('emsFreeHandle', None)
c_emsGetPageFrame = fn('emsGetPageFrame', u16)
c_emsHeapInit = fn('emsHeapInit', i16, u16, u16)
c_emsHeapClose = fn('emsHeapClose', None)
c_EMSAlloc = fn('EMSAlloc', i16, i32)
c_EMSFree = fn('EMSFree', None, i16)
c_EMSLock = fn('EMSLock', vp, i16, i32, u16)
c_EMSLockRegion = fn('EMSLockRegion', vp, i16, i32, u16, u16)
c_EMSCopyTo = fn('EMSCopyTo', i16, i16, vp, i32, i32)
c_EMSCopyFrom = fn('EMSCopyFrom', i16, vp, i16, i32, i32)
c_EMSCopy = fn('EMSCopy', i16, i16, i16, i32, i32, i32)
c_EMSSaveState = fn('EMSSaveState', i16)
c_EMSRestoreState = fn('EMSRestoreState', i16)
c_EMSGetFreeBytes = fn('EMSGetFreeBytes', i32)
c_EMSHeapAvail = fn('EMSHeapAvail', u32)

A = {n: NAMES[n] for n in ('emsInit', 'emsAllocPages', 'emsMapPage', 'emsSavePageMap',
                           'emsRestorePageMap', 'emsGetFreePages', 'emsFreeHandle',
                           'emsGetPageFrame', 'emsHeapInit', 'emsHeapClose', 'EMSAlloc',
                           'EMSFree', 'EMSLock', 'EMSLockRegion', 'EMSCopyTo', 'EMSCopyFrom',
                           'EMSCopy', 'EMSSaveState', 'EMSRestoreState', 'EMSHeapAvail')}
A['EMSGetFreeBytes'] = '1154:0C2B'

# ---- seg1134 ------------------------------------------------------------------
def test_raw_ems_calls():
    for pages in (2048, 0):
        e = make_emu()
        pc_reset()
        pc_ems_configure(pages)
        dg_reset()
        dg_from_emu(e)
        seq = [('emsMapPage', (0, 1)), ('emsAllocPages', (4,)), ('emsInit', ()),
               ('emsGetPageFrame', ()), ('emsGetFreePages', ()), ('emsAllocPages', (0,)),
               ('emsAllocPages', (3000,)), ('emsAllocPages', (16,)), ('emsGetFreePages', ()),
               ('emsMapPage', (3, 2)), ('emsMapPage', (16, 0)), ('emsMapPage', (15, 7)),
               ('emsSavePageMap', ()), ('emsSavePageMap', ()), ('emsMapPage', (1, 2)),
               ('emsRestorePageMap', ()), ('emsRestorePageMap', ()), ('emsFreeHandle', ()),
               ('emsMapPage', (0, 0))]
        orig = []
        for name, args in seq:                 # pascal: first argument pushed first
            orig.append(e.call(A[name], *reversed(args))[0])
        dg_o = e.dgroup(0x86BC, 8)
        pc_reset()
        pc_ems_configure(pages)
        port = []
        for name, args in seq:
            f = globals()['c_' + name]
            r = f(*args)
            port.append(0 if r is None else r & 0xFFFF)
        for (name, args), o, p in zip(seq, orig, port):
            if name != 'emsFreeHandle':
                check(o == p, f'pages={pages} {name}{args}: orig {o:04X} port {p:04X}')
        check(dg_o == bytes(DG[0x86BC:0x86C4]), f'pages={pages} DGROUP 86BC: {dg_o.hex()} vs '
              f'{bytes(DG[0x86BC:0x86C4]).hex()}')
    pc_ems_configure(2048)


# ---- seg1154 ------------------------------------------------------------------
class Orig:
    """seg1154 calls on the original (pascal: arguments pushed in order)."""
    def __init__(self, e):
        self.e = e

    def c(self, name, *args, ret='int'):
        words = []
        for kind, v in args:                    # pascal order -> reverse for e.call
            if kind == 'w':
                words.append([v & 0xFFFF])
            elif kind == 'l':
                words.append([v & 0xFFFF, (v >> 16) & 0xFFFF])
            else:                               # far pointer (seg, off)
                words.append([v[1], v[0]])
        flat = [w for grp in reversed(words) for w in grp]
        return self.e.call(A[name], *flat, ret=ret)


def heap_ops(rnd, n):
    ops = []
    for _ in range(n):
        k = rnd.random()
        if k < 0.35:
            ops.append(('alloc', rnd.choice([1, 15, 16, 17, 100, 4000, 0x3FFF, 0x4000, 0xBFF0,
                                              0xBFF1, 0xC000, 0xFF00, 0x12345, 0x40000,
                                              rnd.randrange(1, 0x30000)])))
        elif k < 0.5:
            ops.append(('free', rnd.randrange(0, 12)))
        elif k < 0.65:
            ops.append(('lock', rnd.randrange(0, 12), rnd.choice([0, 5, 0x3FF0, 0x4000, 0x10000,
                                                               rnd.randrange(0, 0x20000)]),
                        rnd.choice([1, 0x100, 0x4000, 0x8000, 0xFFFF]), rnd.choice([None, 0, 1, 2, 3])))
        elif k < 0.8:
            ops.append(('copyto', rnd.randrange(0, 12), rnd.randrange(0, 0x8000), rnd.randrange(1, 3000)))
        elif k < 0.88:
            ops.append(('copyfrom', rnd.randrange(0, 12), rnd.randrange(0, 0x8000), rnd.randrange(1, 3000)))
        elif k < 0.95:
            ops.append(('copy', rnd.randrange(0, 12), rnd.randrange(0, 12), rnd.randrange(0, 0x4000),
                        rnd.randrange(0, 0x4000), rnd.randrange(1, 0x5000)))
        else:
            ops.append(('state',))
    return ops


def run_heap(ops, init=(0x900, 0x1100), side='orig', e=None, seed=0):
    """Apply ops; returns (results, EMS memory, dg bytes)."""
    rnd = random.Random(seed)
    res = []
    handles = []
    if side == 'orig':
        o = Orig(e)
        buf = e.alloc(0x10000)
        res.append(s16(o.c('emsHeapInit', ('w', init[0]), ('w', init[1]))[0]))
    else:
        reset_c()
        cbuf = (C.c_uint8 * 0x10000)()
        res.append(c_emsHeapInit(*init))
    for op in ops:
        data = bytes(rnd.randrange(256) for _ in range(64)) * 64
        if op[0] == 'alloc':
            h = s16(o.c('EMSAlloc', ('l', op[1]))[0]) if side == 'orig' else c_EMSAlloc(op[1])
            res.append(('alloc', h))
            if h > 0:
                handles.append(h)
        elif op[0] == 'free':
            h = handles.pop(op[1] % len(handles)) if handles else op[1]
            o.c('EMSFree', ('w', h)) if side == 'orig' else c_EMSFree(h)
        elif op[0] == 'lock':
            h = handles[op[1] % len(handles)] if handles else op[1]
            if op[4] is None:
                r = (far_off(*o.c('EMSLock', ('w', h), ('l', op[2]), ('w', op[3]))) if side == 'orig'
                     else c_ptr_far(c_EMSLock(h, op[2], op[3])))
            else:
                r = (far_off(*o.c('EMSLockRegion', ('w', h), ('l', op[2]), ('w', op[3]), ('w', op[4])))
                     if side == 'orig' else c_ptr_far(c_EMSLockRegion(h, op[2], op[3], op[4])))
            res.append(('lock', r))
            if r is not None:                  # write through the frame (stale-data check)
                off = r & 0xFFFF
                n = min(256, 0x10000 - off)
                if side == 'orig':
                    e.wr(0xE000, off, data[:n], runtime=True)
                else:
                    C.memmove(FRAME + off, data[:n], n)
        elif op[0] in ('copyto', 'copyfrom'):
            h = handles[op[1] % len(handles)] if handles else op[1]
            n = op[3]
            if side == 'orig':
                e.wr(buf, 0, data[:n], runtime=True)
                if op[0] == 'copyto':
                    r = o.c('EMSCopyTo', ('w', h), ('p', (buf, 0)), ('l', op[2]), ('l', n))[0]
                else:
                    r = o.c('EMSCopyFrom', ('p', (buf, 0)), ('w', h), ('l', op[2]), ('l', n))[0]
                got = e.rd(buf, 0, n, runtime=True)
            else:
                C.memmove(cbuf, data[:n], n)
                if op[0] == 'copyto':
                    r = c_EMSCopyTo(h, C.addressof(cbuf), op[2], n)
                else:
                    r = c_EMSCopyFrom(C.addressof(cbuf), h, op[2], n)
                got = bytes(cbuf[:n])
            res.append((op[0], s16(r & 0xFFFF), got))
        elif op[0] == 'copy':
            hd = handles[op[1] % len(handles)] if handles else op[1]
            hs = handles[op[2] % len(handles)] if handles else op[2]
            r = (o.c('EMSCopy', ('w', hd), ('w', hs), ('l', op[3]), ('l', op[4]), ('l', op[5]))[0]
                 if side == 'orig' else c_EMSCopy(hd, hs, op[3], op[4], op[5]))
            res.append(('copy', s16(r & 0xFFFF)))
        else:
            if side == 'orig':
                r = [o.c('EMSSaveState')[0], o.c('EMSRestoreState')[0]]
            else:
                r = [c_EMSSaveState() & 0xFFFF, c_EMSRestoreState() & 0xFFFF]
            res.append(('state', r))
        avail = (o.c('EMSHeapAvail', ret='long') if side == 'orig' else c_EMSHeapAvail())
        res.append(('avail', avail))
    fb = (o.c('EMSGetFreeBytes', ret='long') if side == 'orig' else c_EMSGetFreeBytes() & 0xFFFFFFFF)
    res.append(('freebytes', fb))
    mem = ems_mem(1)
    dgb = e.dgroup(0, 0x10000) if side == 'orig' else bytes(DG)
    return res, mem, dgb


def compare_heap(ops, init=(0x900, 0x1100), seed=0, label=''):
    e = make_emu()
    pc_reset()
    dg_from_emu(e)
    ro, mo, dgo = run_heap(ops, init, 'orig', e, seed)
    # C side starts from the same DGROUP as the original did (after DDSInit)
    rc, mc, dgc = run_heap(ops, init, 'c', None, seed)
    for i, (a, b) in enumerate(zip(ro, rc)):
        if not check(a == b, f'{label} step {i}: orig {str(a)[:120]} port {str(b)[:120]}'):
            break
    check(len(ro) == len(rc), f'{label}: result count')
    check(mo == mc, f'{label}: EMS memory differs ({len(mo)} vs {len(mc)} bytes, first diff '
          f'{next((i for i in range(min(len(mo), len(mc))) if mo[i] != mc[i]), None)})')
    for lo, hi in EMS_DG:
        check(dgo[lo:hi] == dgc[lo:hi], f'{label}: DGROUP {lo:04X}-{hi:04X} {dgo[lo:hi].hex()} vs {dgc[lo:hi].hex()}')


def test_heap_init_variants():
    for pages, init in ((2048, (0x900, 0x1100)), (2048, (0x900, 0x7FF0)), (100, (0x900, 0x1100)),
                        (300, (0x900, 0x1100)), (300, (0x10, 0x8))):
        e = make_emu()
        pc_reset()
        pc_ems_configure(pages)
        o = Orig(e)
        r1 = s16(o.c('emsHeapInit', ('w', init[0]), ('w', init[1]))[0])
        r2 = s16(o.c('emsHeapInit', ('w', init[0]), ('w', init[1]))[0])
        av = o.c('EMSHeapAvail', ret='long')
        dgo = e.dgroup(0, 0x10000)
        reset_c()
        pc_ems_configure(pages)
        c1 = c_emsHeapInit(*init)
        c2 = c_emsHeapInit(*init)
        avc = c_EMSHeapAvail()
        check((r1, r2, av) == (c1, c2, avc), f'pages {pages} init {init}: orig {(r1, r2, av)} port {(c1, c2, avc)}')
        for lo, hi in EMS_DG:
            check(dgo[lo:hi] == bytes(DG[lo:hi]), f'pages {pages} {init}: DGROUP {lo:04X}')
        c_emsHeapClose()
    pc_ems_configure(2048)
    # no EMM driver
    reset_c()
    pc_ems_set_present(0)
    check(c_emsHeapInit(0x900, 0x1100) == -1, 'no EMMXXXX0 -> -1')
    pc_ems_set_present(1)


def test_heap_sequences():
    for seed in range(6):
        rnd = random.Random(1154 + seed)
        compare_heap(heap_ops(rnd, 60), seed=seed, label=f'seed {seed}')


def test_heap_not_initialised():
    ops = [('alloc', 100), ('lock', 0, 0, 10, None), ('copyto', 0, 0, 10), ('copy', 0, 0, 0, 0, 5),
           ('state',)]
    e = make_emu()
    pc_reset()
    dg_from_emu(e)
    o = Orig(e)
    ro = [s16(o.c('EMSAlloc', ('l', 100))[0]), far_off(*o.c('EMSLock', ('w', 1), ('l', 0), ('w', 10))),
          s16(o.c('EMSCopy', ('w', 1), ('w', 1), ('l', 0), ('l', 0), ('l', 5))[0]),
          o.c('EMSSaveState')[0] & 0xFF, o.c('EMSHeapAvail', ret='long'),
          o.c('EMSGetFreeBytes', ret='long')]
    reset_c()
    rc = [c_EMSAlloc(100), c_ptr_far(c_EMSLock(1, 0, 10)), c_EMSCopy(1, 1, 0, 0, 5),
          c_EMSSaveState() & 0xFF, c_EMSHeapAvail(), c_EMSGetFreeBytes()]
    check(ro == rc, f'not initialised: orig {ro} port {rc}')
    del ops
