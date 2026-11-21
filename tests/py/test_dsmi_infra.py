"""DSMI infrastructure against the original: string helpers (seg0DDF), DDS
registry and driver loading (seg0DF2), Timer Service (seg10EF), DSMI time
(seg1145)."""
import ctypes
import os
import random
import struct

import unicorn as U
import unicorn.x86_const as X

from lale import Emu, NAMES, ROOT, check, fn, dg_from_emu, dg_diff, dg_read, lib

C = ctypes
CP = C.c_char_p


def pcall(e, name, *params, ret='int'):
    """Far PASCAL call: the first declared parameter is pushed first (ends up
    highest), so the word list is the parameters in reverse order."""
    words = []
    for p in reversed(params):
        words.extend(p if isinstance(p, (list, tuple)) else [p])
    return e.call(NAMES[name] if ':' not in name else name, *words, ret=ret)


def L(v):
    """long parameter -> two words"""
    v &= 0xFFFFFFFF
    return [v & 0xFFFF, v >> 16]


def emu_buf(e, data, extra=0, fill=0):
    seg = e.alloc(len(data) + extra, fill=fill)
    e.wr(seg, 0, data, runtime=True)
    return seg


# ===========================================================================
# seg0DDF string helpers
# ===========================================================================

c_strcpy = fn('dsmiStrCpy', None, C.c_void_p, C.c_void_p)
c_strncpy = fn('dsmiStrNCpy', None, C.c_void_p, C.c_void_p, C.c_int32)
c_strcmp = fn('dsmiStrCmp', C.c_int16, C.c_void_p, C.c_void_p)
c_strlen = fn('dsmiStrLen', C.c_int16, C.c_void_p)
c_memcpy = fn('dsmiMemCpy', None, C.c_void_p, C.c_void_p, C.c_int32)
c_memcpy2 = fn('dsmiMemCpy2', None, C.c_void_p, C.c_void_p, C.c_int32)
c_memcmp = fn('dsmiMemCmp', C.c_int16, C.c_void_p, C.c_void_p, C.c_int32)
c_memset = fn('dsmiMemSet', None, C.c_void_p, C.c_uint8, C.c_int32)
c_abs = fn('dsmiAbs', C.c_int16, C.c_int16)


def _rand_str(rnd, maxlen=12, alpha=b'abAB\x80\xff'):
    return bytes(rnd.choice(alpha) for _ in range(rnd.randrange(maxlen)))


def test_dsmistr_strings():
    e = Emu()
    rnd = random.Random(0xDDF)
    for k in range(300):
        e._heap = 0x6200
        a = _rand_str(rnd) + b'\0' + bytes(rnd.randrange(256) for _ in range(4))
        b = (a[:rnd.randrange(len(a))] if rnd.random() < .4 else _rand_str(rnd)) + b'\0'
        # StrCmp(s1, s2)
        sa, sb = emu_buf(e, a), emu_buf(e, b)
        ro = pcall(e, 'dsmiStrCmp', e.far(sa, 0), e.far(sb, 0))[0]
        rc = c_strcmp(a, b) & 0xFFFF
        check(ro == rc, f'StrCmp({a!r},{b!r}) orig {ro:#x} C {rc:#x}')
        # StrLen
        ro = pcall(e, 'dsmiStrLen', e.far(sa, 0))[0]
        rc = c_strlen(a) & 0xFFFF
        check(ro == rc, f'StrLen({a!r}) orig {ro} C {rc}')
        # StrCpy
        dst = e.alloc(64, fill=0xCC)
        pcall(e, 'dsmiStrCpy', e.far(dst, 0), e.far(sa, 0))
        cb = C.create_string_buffer(b'\xcc' * 64, 64)
        c_strcpy(cb, a)
        check(e.rd(dst, 0, 64, runtime=True) == cb.raw, f'StrCpy {a!r}')
        # StrNCpy with small / zero-low-word counts
        for n in (rnd.randrange(0, 20), 0x10000 + rnd.randrange(5)):
            dst = e.alloc(64, fill=0xCC)
            pcall(e, 'dsmiStrNCpy', e.far(dst, 0), e.far(sa, 0), L(n))
            cb = C.create_string_buffer(b'\xcc' * 64, 64)
            c_strncpy(cb, a, n)
            check(e.rd(dst, 0, 64, runtime=True) == cb.raw, f'StrNCpy {a!r} n={n:#x}')
    check(c_strlen(b'') == 1 and c_strlen(b'abc') == 4, 'StrLen is strlen+1')


def test_dsmistr_memory():
    e = Emu()
    rnd = random.Random(0xDDF2)
    for k in range(300):
        e._heap = 0x6200
        n = rnd.choice([0, 1, 2, 3, 7, 8, 31, rnd.randrange(64)])
        a = bytes(rnd.choice((0, 1, 0x7F, 0x80, 0xFF, rnd.randrange(256))) for _ in range(64))
        b = bytearray(a)
        if rnd.random() < .7:
            i = rnd.randrange(max(n, 1) + 1)
            if i < 64:
                b[i] = rnd.randrange(256)
        b = bytes(b)
        hi = rnd.choice([0, 0, 0, 1, 0x7FFF])          # high word of the long count
        cnt = (hi << 16) | n
        sa, sb = emu_buf(e, a), emu_buf(e, b)
        ro = pcall(e, 'dsmiMemCmp', e.far(sa, 0), e.far(sb, 0), L(cnt))[0]
        rc = c_memcmp(a, b, cnt) & 0xFFFF
        check(ro == rc, f'MemCmp n={cnt:#x} diff@{[i for i in range(64) if a[i] != b[i]]}: '
                        f'orig {ro:#x} C {rc:#x}')
        # MemCpy / MemCpy2 incl. overlapping forward copy (dst = src + 1)
        for name, cf in (('dsmiMemCpy', c_memcpy), ('dsmiMemCpy2', c_memcpy2)):
            for ov in (0, 1, 3):
                buf = e.alloc(128)
                e.wr(buf, 0, a + a, runtime=True)
                pcall(e, name, e.far(buf, ov), e.far(buf, 0), L(cnt))
                cb = (C.c_uint8 * 128).from_buffer_copy(a + a)
                cf(C.byref(cb, ov), C.byref(cb, 0), cnt)
                check(e.rd(buf, 0, 128, runtime=True) == bytes(cb), f'{name} n={cnt:#x} ov={ov}')
        # MemSet (count high word must stay 0 here: it would fill 64 KB)
        buf = emu_buf(e, a)
        c = rnd.randrange(256)
        pcall(e, 'dsmiMemSet', e.far(buf, 0), c, L(n))
        cb = (C.c_uint8 * 64).from_buffer_copy(a)
        c_memset(cb, c, n)
        check(e.rd(buf, 0, 64, runtime=True) == bytes(cb), f'MemSet n={n}')
    # the JECXZ quirk: a difference in the last byte reads as "equal"
    check(c_memcmp(b'abc', b'abd', 3) == 0 and c_memcmp(b'abc', b'abd', 4) != 0, 'MemCmp quirk')
    for v in (0, 1, -1, 5, -5, 32767, -32767, -32768):
        ro = pcall(e, 'dsmiAbs', v & 0xFFFF)[0]
        check(ro == c_abs(v) & 0xFFFF, f'Abs({v})')


# ===========================================================================
# Timer Service (seg10EF) and DSMI time (seg1145)
# ===========================================================================

TS_LO, TS_HI = 0x85CE, 0x86B4
TS_PTR_BYTES = {0x85CE + 14 * i + k for i in range(16) for k in range(4)}   # native in the port

c_tsInit = fn('tsInit', C.c_int16)
c_tsClose = fn('tsClose', None)
c_tsChange = fn('tsChangeRoutine', C.c_int16, C.c_int16, C.c_uint16)
c_tsAdd = fn('tsAddRoutine', C.c_int16, C.c_void_p, C.c_uint16)
c_tsRemove = fn('tsRemoveRoutine', C.c_int16, C.c_int16)
c_tsSetRate = fn('tsSetTimerRate', C.c_int16, C.c_uint16)
c_tsGetRate = fn('tsGetTimerRate', C.c_uint16)
c_tsISR = fn('tsTimerISR', None)
c_pc_reset = fn('pc_reset', None)
c_pc_set_int08 = fn('pc_set_int08', None, C.c_void_p)
c_pc_get_int08 = fn('pc_get_int08', C.c_void_p)
c_pc_divisor = fn('pc_pit_divisor', C.c_uint16)
c_pc_bios_ticks = fn('pc_bios_ticks', C.c_uint32)
c_pc_poll = fn('pc_poll', None)
c_pc_virtual = fn('pc_set_virtual_time', None, C.c_int, C.c_uint32)
c_dg_reset = fn('dg_reset', None)

ISR_T = C.CFUNCTYPE(None)
PROBE_PORT = 0xE0


class TimerEmu:
    """The original with INT 21h 35h/25h on a real IVT, PIT/probe port writes
    recorded, and tiny IRET routines that report their id on port E0h."""

    def __init__(self):
        e = self.e = Emu()
        e.stub_heap()
        self.pit = []
        self.calls = []
        orig = e._int21

        def int21(uc, ah):
            al = uc.reg_read(X.UC_X86_REG_AX) & 0xFF
            if ah == 0x35:
                off, seg = struct.unpack('<HH', uc.mem_read(al * 4, 4))
                uc.reg_write(X.UC_X86_REG_BX, off)
                uc.reg_write(X.UC_X86_REG_ES, seg)
                return True
            if ah == 0x25:
                uc.mem_write(al * 4, struct.pack('<HH', uc.reg_read(X.UC_X86_REG_DX),
                                                 uc.reg_read(X.UC_X86_REG_DS)))
                return True
            return orig(uc, ah)
        e._int21 = int21
        e.u.hook_add(U.UC_HOOK_INSN, self._out, None, 1, 0, X.UC_X86_INS_OUT)
        # routine k at rseg:k*16: push ax / mov al,k / out E0h,al / pop ax / iret
        self.rseg = e.alloc(17 * 16)
        for k in range(17):
            e.wr(self.rseg, k * 16, bytes([0x50, 0xB0, k, 0xE6, PROBE_PORT, 0x58, 0xCF]),
                 runtime=True)
        # "BIOS" INT 08h = routine 16
        e.u.mem_write(8 * 4, struct.pack('<HH', 16 * 16, self.rseg))

    def _out(self, uc, port, size, value, _):
        if port in (0x40, 0x43):
            self.pit.append((port, value & 0xFF))
        elif port == PROBE_PORT:
            self.calls.append(value & 0xFF)

    def routine(self, k):
        return self.e.far(self.rseg, k * 16)

    def isr(self):
        # an interrupt frame: IP, CS (call() pushes them) + FLAGS as an argument
        self.e.call('10EF:0048', 0x0202)

    def ivt8(self):
        return struct.unpack('<HH', self.e.u.mem_read(8 * 4, 4))


class TimerC:
    def __init__(self):
        c_pc_reset()
        self.calls = []
        self.cbs = [ISR_T(lambda k=k: self.calls.append(k)) for k in range(17)]
        c_pc_set_int08(C.cast(self.cbs[16], C.c_void_p))    # "BIOS" = routine 16

    def routine(self, k):
        return C.cast(self.cbs[k], C.c_void_p)


def _pit_from_writes(w):
    """Divisors programmed through 43h=36h, 40h lo, 40h hi."""
    out = []
    for i in range(0, len(w) - 2):
        if w[i] == (0x43, 0x36) and w[i + 1][0] == 0x40 and w[i + 2][0] == 0x40:
            out.append(w[i + 1][1] | w[i + 2][1] << 8)
    return out


def _timer_scenario(rnd):
    """A random sequence of Timer Service operations."""
    ops = [('init',)]
    for _ in range(rnd.randrange(3, 12)):
        r = rnd.random()
        if r < .45:
            ops.append(('add', rnd.randrange(16), rnd.choice([11931, 11931, 0x7485, 0x1234, 600,
                                                               0xFFFF, 0x2000, rnd.randrange(1, 0x10000)])))
        elif r < .55:
            ops.append(('remove', rnd.randrange(-1, 17)))
        elif r < .7:
            ops.append(('change', rnd.randrange(0, 16), rnd.randrange(1, 0x10000)))
        elif r < .75:
            ops.append(('setrate', rnd.choice([11931, 0x8000, 1000])))
        else:
            ops.append(('irq', rnd.randrange(1, 60)))
    ops.append(('irq', 200))
    if rnd.random() < .5:
        ops.append(('close',))
    return ops


def _run_orig(ops):
    t = TimerEmu()
    e = t.e
    rets = []
    for op in ops:
        t.calls.append(('op', op[0]))
        if op[0] == 'init':
            rets.append(e.call('10EF:007D')[0])
        elif op[0] == 'close':
            e.call('10EF:00DF')
        elif op[0] == 'add':
            rets.append(pcall(e, '10EF:0178', t.routine(op[1]), L(op[2]))[0])
        elif op[0] == 'remove':
            rets.append(pcall(e, '10EF:0206', L(op[1]))[0])
        elif op[0] == 'change':
            rets.append(pcall(e, '10EF:0110', L(op[1]), L(op[2]))[0])
        elif op[0] == 'setrate':
            rets.append(pcall(e, '10EF:0263', L(op[1]))[0])
        elif op[0] == 'irq':
            for _ in range(op[1]):
                t.isr()
        rets.append(e.call('10EF:02B5')[0])
    return t, rets


def _run_c(ops):
    t = TimerC()
    rets = []
    for op in ops:
        t.calls.append(('op', op[0]))
        if op[0] == 'init':
            rets.append(c_tsInit() & 0xFFFF)
        elif op[0] == 'close':
            c_tsClose()
        elif op[0] == 'add':
            rets.append(c_tsAdd(t.routine(op[1]), op[2]) & 0xFFFF)
        elif op[0] == 'remove':
            rets.append(c_tsRemove(op[1]) & 0xFFFF)
        elif op[0] == 'change':
            rets.append(c_tsChange(op[1], op[2]) & 0xFFFF)
        elif op[0] == 'setrate':
            rets.append(c_tsSetRate(op[1]) & 0xFFFF)
        elif op[0] == 'irq':
            for _ in range(op[1]):
                c_tsISR()
        rets.append(c_tsGetRate())
    return t, rets


def test_timer_service_sequences():
    rnd = random.Random(0x10EF)
    for k in range(40):
        ops = _timer_scenario(rnd)
        to, ro = _run_orig(ops)
        c_dg_reset()
        tc, rc = _run_c(ops)
        check(ro == rc, f'#{k} {ops}: returns orig {ro} C {rc}')
        check(to.calls == tc.calls, f'#{k} {ops}: routine call order differs\n'
                                    f'orig {to.calls[:80]}\nC    {tc.calls[:80]}')
        d = dg_diff(to.e, [(TS_LO, TS_HI)], ignore=TS_PTR_BYTES)
        check(not d, f'#{k} {ops}: DGROUP differs {[(hex(a), b, c) for a, b, c in d[:8]]}')
        pit = _pit_from_writes(to.pit)
        check(len(pit) == len([w for w in to.pit if w == (0x43, 0x36)]), 'PIT write pattern')
        if pit:
            check(c_pc_divisor() == pit[-1], f'#{k}: PIT divisor orig {pit[-1]:#x} C {c_pc_divisor():#x}')
        # INT 08h vector: hooked while installed, restored by tsClose
        closed = ops[-1][0] == 'close'
        off, seg = to.ivt8()
        cur = c_pc_get_int08()
        if closed:
            check((seg, off) == (to.rseg, 16 * 16), 'orig vector not restored')
            check(cur == tc.routine(16).value, 'C vector not restored')
        else:
            check(seg == 0x10EF + 0x0824 and off == 0x48, 'orig vector not hooked')
            check(cur == ctypes.cast(fn('tsTimerISR', None), C.c_void_p).value, 'C vector not tsTimerISR')
        c_tsClose()


def test_timer_game_setup_pc_poll():
    """The game's set-up (3 routines at 100 Hz) driven by pc_poll in virtual
    time: each runs 100 times per emulated second; the BIOS handler (NULL
    vector) keeps the BDA tick at ~18.2 Hz."""
    c_dg_reset()
    c_pc_reset()
    n = [0, 0, 0]
    cbs = [ISR_T(lambda i=i: n.__setitem__(i, n[i] + 1)) for i in range(3)]
    check(c_tsInit() == 0, 'tsInit')
    for cb in cbs:
        c_tsAdd(C.cast(cb, C.c_void_p), 11931)
    check(c_pc_divisor() == 11931, 'PIT at 100 Hz')
    c_pc_virtual(1, 1000)
    for _ in range(10000):          # 10 s
        c_pc_poll()
    check(all(abs(x - 1000) <= 1 for x in n), f'100 Hz routines ran {n}')
    bt = c_pc_bios_ticks()
    check(abs(bt - 182) <= 2, f'BIOS ticks {bt} (expected ~182)')
    c_tsClose()
    check(c_pc_get_int08() is None and c_pc_divisor() == 0xFFFF, 'tsClose restores BIOS vector / PIT FFFFh')
    c_pc_virtual(0, 0)


c_timeInit = fn('dsmiTimeInit', C.c_int16, C.c_int32)
c_getTime = fn('GetDSMITime', C.c_uint32)
c_timeISR = fn('dsmiTimeISR', None)
c_timeAdvance = fn('dsmiTimeAdvance', None)
c_lock = fn('LockDSMITime', None)
c_unlock = fn('UnlockDSMITime', None)
c_ddsInit = fn('DDSInit', C.c_int16)


def test_dsmi_time():
    rnd = random.Random(0x1145)
    e = Emu()
    e.stub_heap()
    step = 0x7FFFFF80                                  # carries out of the low word quickly
    check(e.call('0DF2:0000')[0] == 0, 'orig DDSInit')
    ro = pcall(e, '1145:0004', L(step))[0]
    c_dg_reset()
    c_ddsInit()
    rc = c_timeInit(step) & 0xFFFF
    check(ro == rc, f'dsmiTimeInit ret orig {ro} C {rc}')
    ops = [rnd.choice(['isr', 'isr', 'adv', 'lock', 'unlock', 'get']) for _ in range(400)]
    for i, op in enumerate(ops):
        if op == 'isr':
            e.call('1145:0096', 0x0202)                # IRET routine: give it a FLAGS word
            c_timeISR()
        elif op == 'adv':
            e.call('1145:007F')
            c_timeAdvance()
        elif op == 'lock':
            e.call('1145:00BD')
            c_lock()
        elif op == 'unlock':
            e.call('1145:00DE')
            c_unlock()
        a = e.call('1145:005E', ret='long')
        b = c_getTime()
        if not check(a == b, f'step {i} {op}: GetDSMITime orig {a:#x} C {b:#x}'):
            break
    d = dg_diff(e, [(0xE796, 0xE7A4)])
    check(not d, f'time DGROUP differs {d[:6]}')


# ===========================================================================
# DDS (seg0DF2)
# ===========================================================================

class FuncNode(C.Structure):
    pass


FuncNode._fields_ = [('name', C.c_char_p), ('fn', C.c_void_p), ('next', C.POINTER(FuncNode))]


class ResNode(C.Structure):
    pass


ResNode._fields_ = [('name', C.c_char_p), ('ptr', C.c_void_p), ('size', C.c_uint32),
                    ('next', C.POINTER(ResNode))]


class DrvNode(C.Structure):
    pass


DrvNode._fields_ = [('loaded', C.c_uint16), ('id', C.c_int16), ('handle', C.c_uint32),
                    ('entry', C.c_void_p), ('image', C.c_void_p), ('block', C.c_void_p),
                    ('next', C.POINTER(DrvNode))]

ENTRY_T = C.CFUNCTYPE(C.c_uint32, C.c_int32, C.c_void_p, C.c_int32)

c_regF = fn('DDSRegisterFunction', C.c_int16, CP, C.c_void_p)
c_regR = fn('DDSRegisterResource', C.c_int16, CP, C.c_void_p, C.c_uint32)
c_accF = fn('DDSAccessFunction', C.c_int16, CP, C.POINTER(C.c_void_p))
c_accR = fn('DDSAccessResource', C.c_int16, CP, C.POINTER(C.c_void_p), C.POINTER(C.c_uint32))
c_remF = fn('DDSRemoveFunction', C.c_int16, CP)
c_remR = fn('DDSRemoveResource', C.c_int16, CP)
c_info = fn('DDSGetDriverInfo', C.c_int16, CP, C.POINTER(C.c_uint16 * 3))
c_load = fn('DDSLoadDriver', C.c_int16, CP, C.POINTER(C.c_uint32))
c_regD = fn('DDSRegisterDriver', C.c_int16, C.c_void_p, C.POINTER(C.c_uint32))
c_unload = fn('DDSUnloadDriver', C.c_int16, C.c_uint32)
c_native = fn('dds_register_native_driver', None, CP, C.c_void_p)
c_set_game_dir = fn('pc_set_game_dir', None, CP)


def _c_list(head_name, node_t, link='next'):
    head = C.POINTER(node_t).in_dll(lib, head_name)
    out = []
    p = head.contents.next
    while C.addressof(p.contents) != C.addressof(head.contents):
        out.append(p.contents)
        p = p.contents.next
    return out


def _emu_far(e, seg, off):
    o, s = struct.unpack('<HH', e.rd(seg, off, 4, runtime=True))
    return s, o


def _emu_list(e, head_off, next_off):
    hs, ho = struct.unpack('<HH', e.dgroup(head_off, 4))[::-1]
    out = []
    s, o = _emu_far(e, hs, ho + next_off)
    while (s, o) != (hs, ho):
        out.append((s, o))
        s, o = _emu_far(e, s, o + next_off)
    return out


def _emu_str(e, seg, off):
    s = bytearray()
    while True:
        b = e.rd(seg, off + len(s), 1, runtime=True)[0]
        if not b:
            return bytes(s)
        s.append(b)


def _emu_funcs(e):
    out = []
    for s, o in _emu_list(e, 0xE77E, 8):
        ns, no = _emu_far(e, s, o)
        fs, fo = _emu_far(e, s, o + 4)
        out.append((_emu_str(e, ns, no), (fs << 16) | fo))
    return out


def test_dds_registry():
    rnd = random.Random(0xDF2)
    names = [b'A', b'B', b'AB', b'Ab', b'GetDSMITime', b'MemFree', b'x' * 20, b'\x80']
    for k in range(25):
        e = Emu()
        e.stub_heap()
        c_dg_reset()
        nbuf = {}
        rets_o, rets_c = [], []
        out = e.alloc(16)
        # some calls before DDSInit (error 2)
        pre = rnd.random() < .3
        script = []
        if pre:
            script.append(('regF', b'A', 1))
        script.append(('init',))
        for _ in range(rnd.randrange(5, 40)):
            op = rnd.choice(['regF', 'regF', 'regF', 'accF', 'remF', 'regR', 'regR', 'accR', 'remR'])
            nm = rnd.choice(names + [None] * 1)
            script.append((op, nm, rnd.choice([0, 1, 0x12345678, 0xDEAD0000])))
        cbuf = {}
        for st in script:
            if st[0] == 'init':
                rets_o.append(e.call('0DF2:0000')[0])
                rets_c.append(c_ddsInit())
                continue
            op, nm, val = st
            if nm is not None and nm not in nbuf:
                nbuf[nm] = emu_buf(e, nm + b'\0')
                cbuf[nm] = C.create_string_buffer(nm)
            ename = e.far(nbuf[nm], 0) if nm is not None else [0, 0]
            cname = C.cast(cbuf[nm], CP) if nm is not None else None
            if op == 'regF':
                rets_o.append(pcall(e, '0DF2:011B', ename, L(val))[0])
                rets_c.append(c_regF(cname, val or None))
            elif op == 'regR':
                rets_o.append(pcall(e, '0DF2:0214', ename, L(val), L(val ^ 0x55))[0])
                rets_c.append(c_regR(cname, val or None, (val ^ 0x55) & 0xFFFFFFFF))
            elif op == 'accF':
                e.wr(out, 0, b'\xee' * 8, runtime=True)
                rets_o.append(pcall(e, '0DF2:0316', ename, e.far(out, 0))[0])
                v = C.c_void_p(0xEEEEEEEE)
                rets_c.append(c_accF(cname, C.byref(v)))
                # DDSInit's own entries (DDS*, Mem*) point to different code on each side
                user = (1, 0x12345678, 0xDEAD0000, 0xEEEEEEEE)
                vo = struct.unpack('<I', e.rd(out, 0, 4, runtime=True))[0]
                vc = (v.value or 0) & 0xFFFFFFFF if rets_c[-1] == 0 else 0xEEEEEEEE
                rets_o.append(vo if vo in user else ('builtin', nm))
                rets_c.append(vc if vc in user else ('builtin', nm))
            elif op == 'accR':
                e.wr(out, 0, b'\xee' * 8, runtime=True)
                rets_o.append(pcall(e, '0DF2:03A5', ename, e.far(out, 0), e.far(out, 4))[0])
                v, sz = C.c_void_p(0xEEEEEEEE), C.c_uint32(0xEEEEEEEE)
                r = c_accR(cname, C.byref(v), C.byref(sz))
                rets_c.append(r)
                rets_o.append(e.rd(out, 0, 8, runtime=True))
                rets_c.append(struct.pack('<II', (v.value or 0) & 0xFFFFFFFF if r == 0 else 0xEEEEEEEE,
                                          sz.value))
            elif op == 'remF':
                rets_o.append(pcall(e, '0DF2:044A', ename)[0])
                rets_c.append(c_remF(cname))
            elif op == 'remR':
                rets_o.append(pcall(e, '0DF2:04F8', ename)[0])
                rets_c.append(c_remR(cname))
        rets_c = [r & 0xFFFF if isinstance(r, int) and r < 0x10000 and r >= -0x8000 else r for r in rets_c]
        check(rets_o == rets_c, f'#{k} {script}\n orig {rets_o}\n C    {rets_c}')
        # final function list: names in order (pointers of DDSInit's own entries differ)
        lo = [n for n, _ in _emu_funcs(e)]
        lc = [n.name for n in _c_list('dds_functions', FuncNode)]
        check(lo == lc, f'#{k} function list orig {lo} C {lc}')
        d = dg_diff(e, [(0xE776, 0xE77A)])
        check(not d, f'#{k} DDS DGROUP {d}')
    # the documented quirk: a duplicate right after its twin is accepted
    c_ddsInit()
    check(c_regF(b'Q', 1) == 0 and c_regF(b'Q', 2) == 0 and c_regF(b'Q', 3) == 7, 'dup quirk')


def _c_files():
    """Port side: Borland stdio + the virtual file layer (vfs, o569A) over
    orig/ - DDS opens the drivers through it, as the original does."""
    from helpers import c_mount
    c_mount()


def _dsd(name):
    return os.path.join(ROOT, 'orig', name)


def test_dds_driver_info():
    e = Emu()
    _stub_file_layer(e)
    c_set_game_dir(os.path.join(ROOT, 'orig').encode())
    _c_files()
    for f in ('SDISB16.DSD', 'SDIGUS16.DSD', 'SDIPAS16.DSD', 'SDIWSS16.DSD', 'IST.EXE', 'NOFILE.DSD'):
        e._heap = 0x6200
        nb = emu_buf(e, f.encode() + b'\0')
        out = e.alloc(8, fill=0xEE)
        ro = pcall(e, '0DF2:05A6', e.far(nb, 0), e.far(out, 0))[0]
        info = (C.c_uint16 * 3)(0xEEEE, 0xEEEE, 0xEEEE)
        rc = c_info(f.encode(), C.byref(info))
        check(ro == rc, f'{f}: GetDriverInfo orig {ro} C {rc}')
        oi = struct.unpack('<3H', e.rd(out, 0, 6, runtime=True))
        check(oi == tuple(info), f'{f}: info orig {oi} C {tuple(info)}')


def _pascal_stub(e, lin, nwords, fn):
    """Replace the far pascal routine at linear address lin: fn(words) gets
    the argument words from the lowest (last pushed) up and returns DX:AX;
    the stub pops the arguments like RETF n."""
    def hook(uc, address, size, _):
        ss, sp = uc.reg_read(X.UC_X86_REG_SS), uc.reg_read(X.UC_X86_REG_SP)
        w = struct.unpack(f'<{2 + nwords}H', uc.mem_read(ss * 16 + sp, 4 + 2 * nwords))
        r = fn(w[2:]) & 0xFFFFFFFF
        uc.reg_write(X.UC_X86_REG_AX, r & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_DX, r >> 16)
        uc.reg_write(X.UC_X86_REG_SP, (sp + 4 + 2 * nwords) & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_CS, w[1])
        uc.reg_write(X.UC_X86_REG_IP, w[0])
    e.u.hook_add(U.UC_HOOK_CODE, hook, begin=lin, end=lin)


def _pascal_entry_stub(e, seg, off, fn):
    """A driver entry at runtime seg:off: far pascal (long cmd, ptr, long),
    returns DX:AX = fn(cmd)."""
    _pascal_stub(e, seg * 16 + off, 6, lambda w: fn(w[4] | w[5] << 16))


def _stub_file_layer(e):
    """The virtual file layer o569A_open/close/read/seek (thunks 16CD:xxxx)
    served from orig/ - DDS only needs the real files; the port reads them
    through its own vfs layer (_c_files)."""
    files = {}

    def op(w):                                     # (name far, int mode)
        name = _emu_str(e, w[2], w[1]).decode('cp857')
        p = os.path.join(ROOT, 'orig', name)
        if not os.path.isfile(p):
            return 0xFFFF
        h = len(files) + 1
        files[h] = open(p, 'rb')
        return h

    def cl(w):
        files.pop(w[0]).close()
        return 0

    def rd(w):                                     # (h, buf far, long n)
        n = w[0] | w[1] << 16
        data = files[w[4]].read(n)
        e.wr(w[3], w[2], data, runtime=True)
        return 0 if len(data) == n else 1

    def sk(w):                                     # (h, long off, int whence)
        files[w[3]].seek(w[1] | w[2] << 16, w[0])
        return 0
    _pascal_stub(e, e.lin('16CD:0025'), 3, op)
    _pascal_stub(e, e.lin('16CD:002A'), 1, cl)
    _pascal_stub(e, e.lin('16CD:002F'), 5, rd)
    _pascal_stub(e, e.lin('16CD:0039'), 4, sk)
    return files


def test_dds_drivers():
    """DDSLoadDriver on the real files: the original runs the real driver's
    init; the port runs a native stand-in that returns the same DX:AX."""
    c_set_game_dir(os.path.join(ROOT, 'orig').encode())
    files = ['SDISB16.DSD', 'SDIGUS16.DSD', 'SDIPAS16.DSD', 'SDIWSS16.DSD']
    e = Emu()
    e.stub_heap()
    _stub_file_layer(e)
    e.call('0DF2:0000')
    # DX:AX of each real entry(1) call, right after CALLF [BP-28h]
    init_results = []
    e.u.hook_add(U.UC_HOOK_CODE, lambda uc, ad, sz, _: init_results.append(
        uc.reg_read(X.UC_X86_REG_DX) << 16 | uc.reg_read(X.UC_X86_REG_AX)),
        begin=e.lin('0DF2:0806'), end=e.lin('0DF2:0806'))
    seq_o = []
    hbuf = e.alloc(8)
    for f in files + ['SDISB16.DSD', 'NOFILE.DSD', 'IST.EXE']:
        nb = emu_buf(e, f.encode() + b'\0')
        e.wr(hbuf, 0, b'\xee' * 4, runtime=True)
        try:
            r = pcall(e, '0DF2:0611', e.far(nb, 0), e.far(hbuf, 0))[0]
        except Exception as ex:                       # noqa: BLE001
            check(False, f'{f}: original driver init did not run in the emulator: {ex}')
            return
        seq_o.append((f, r, struct.unpack('<I', e.rd(hbuf, 0, 4, runtime=True))[0]))
    nodes_o = []
    for s, o in _emu_list(e, 0xE782, 0x14):
        loaded, did, handle = struct.unpack('<HhI', e.rd(s, o, 8, runtime=True))
        nodes_o.append((loaded, did, handle))
    # port side
    c_dg_reset()
    _c_files()
    c_ddsInit()
    unload_calls = []

    queue = list(init_results)

    def mk(f):
        def entry(cmd, arg, arg2):
            if cmd == 1:
                # stand-in: what the real driver's init returned in the original
                # (in the bare emulator only the GUS driver initialises - the
                # others find no card or miss services and fail)
                return queue.pop(0)
            unload_calls.append(f)
            return 0
        return ENTRY_T(entry)
    entries = {f: mk(f) for f in files}
    for f in files:
        c_native(f.lower().encode(), C.cast(entries[f], C.c_void_p))
    seq_c = []
    for f in files + ['SDISB16.DSD', 'NOFILE.DSD', 'IST.EXE']:
        h = C.c_uint32(0xEEEEEEEE)
        r = c_load(f.encode(), C.byref(h))
        seq_c.append((f, r, h.value))
    check(seq_o == seq_c, f'LoadDriver orig {seq_o}\n C {seq_c}')
    nodes_c = [(n.loaded, n.id, n.handle) for n in _c_list('dds_drivers', DrvNode)]
    check(nodes_o == nodes_c, f'driver list orig {nodes_o} C {nodes_c}')
    # image loaded paragraph-aligned with the file's load module
    for n in _c_list('dds_drivers', DrvNode):
        check(n.image % 16 == 0, 'image alignment')
    img = _c_list('dds_drivers', DrvNode)[0].image
    raw = open(_dsd([f for f, r, h in seq_c if r == 0][0]), 'rb').read()
    hp = struct.unpack_from('<H', raw, 8)[0] * 16
    check(C.string_at(img, 256) == raw[hp:hp + 256], 'image contents')
    # unload: unknown handle, then one by one (refusal -> 11 but removed)
    check(c_unload(99) == 6, 'unload unknown')
    first = nodes_c[0][2]
    check(c_unload(first) == 0 and c_unload(first) == 6, 'unload twice')

    # RegisterDriver / UnloadDriver protocol against the original, with
    # Python driver entries on both sides
    e = Emu()
    e.stub_heap()
    e.call('0DF2:0000')
    c_dg_reset()
    c_ddsInit()
    script = [(0x10 << 16, 0), (0x11 << 16, 0), (0x11 << 16, 0), (0x10 << 16, 0), (3, 0),
              (0x8000 << 16, 0), (0x8000 << 16, 0), (0, 1), (0x20 << 16, 7)]
    calls_o, calls_c = [], []
    sseg = e.alloc(len(script) * 16)
    hbuf = e.alloc(8)
    for i, (ret1, ret2) in enumerate(script):
        def stub(cmd, i=i, ret1=ret1, ret2=ret2):
            calls_o.append((i, cmd))
            return ret1 if cmd == 1 else ret2
        _pascal_entry_stub(e, sseg, i * 16, stub)
    centries = []
    for i, (ret1, ret2) in enumerate(script):
        def ce(cmd, arg, arg2, i=i, ret1=ret1, ret2=ret2):
            calls_c.append((i, cmd))
            return ret1 if cmd == 1 else ret2
        centries.append(ENTRY_T(ce))
    ro, rc = [], []
    for i in range(len(script)):
        e.wr(hbuf, 0, bytes([0xEE] * 4), runtime=True)
        r = pcall(e, '0DF2:08EF', e.far(sseg, i * 16), e.far(hbuf, 0))[0]
        ro.append((r, struct.unpack('<I', e.rd(hbuf, 0, 4, runtime=True))[0]))
        h = C.c_uint32(0xEEEEEEEE)
        rc.append((c_regD(C.cast(centries[i], C.c_void_p), C.byref(h)), h.value))
    for h in (3, 1, 2, 9, 5, 4, 6):
        ro.append(pcall(e, '0DF2:09EE', L(h))[0])
        rc.append(c_unload(h))
    check(ro == rc, f'RegisterDriver/Unload orig {ro}\n C {rc}')
    check(calls_o == calls_c, f'entry calls orig {calls_o}\n C {calls_c}')
    d = dg_diff(e, [(0xE776, 0xE77A)])
    check(not d, f'DDS DGROUP {d}')
