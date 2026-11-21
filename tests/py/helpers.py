"""
helpers.py - environments shared by several differential tests.

Kept out of the test_*.py files so that a test file never imports another
one (run.py imports only the files a filter selects).

  * the Borland far heap of the original (FarHeap): the real allocator code
    in a second emulator, seeded from the port's heap state;
  * make_emu / reset_c: MEMMAN's world on both sides - the port's EMS
    through attach_pc, DDS initialised, fatal recorded, the same far heap;
  * the FILEMAN file-layer stand-ins (stub_files / pascal_stub);
  * the VgaState layout of src/platform/vga.h.
"""
import ctypes
import os
import struct

import unicorn as U
import unicorn.x86_const as X

from lale import DG, Emu, LOAD, NAMES, ROOT, check, fn, lib  # noqa: F401

C = ctypes
u8, u16, i16, i32, u32, vp, cp = (C.c_uint8, C.c_uint16, C.c_int16, C.c_int32, C.c_uint32,
                                  C.c_void_p, C.c_char_p)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    return v - 0x100000000 if v & 0x80000000 else v


def L(v):
    """the two argument words of a 32-bit value"""
    return [v & 0xFFFF, (v >> 16) & 0xFFFF]


# ============================================================================
# Borland far heap
# ============================================================================
ARENA_SEG = 0x2EAD
ARENA_LIN = ARENA_SEG * 16
ARENA_END = 0xA0000
CS_VARS = (0x0000 + LOAD) * 16 + 0x2382          # __first __last __rover ...
BRK_RANGES = ((0x7B, 0x7D), (0x87, 0x93), (0x9582, 0x9584))

c_malloc = fn('bc_farmalloc', vp, u32)
c_free = fn('bc_farfree', None, vp)
c_coreleft = fn('bc_farcoreleft', u32)
c_addr = fn('bc_far_addr', u32, vp)
c_reset = fn('bc_farheap_reset', None)
ARENA = C.c_uint8 * (ARENA_END - ARENA_LIN)


def arena():
    return ARENA.in_dll(lib, 'bc_fh_arena')


def cvars():
    return tuple(u16.in_dll(lib, n).value for n in ('bc_fh_first', 'bc_fh_last', 'bc_fh_rover'))


def emu_cvars(e):
    return struct.unpack('<HHH', bytes(e.u.mem_read(CS_VARS, 6)))


# ---- DOSBox DOS_ResizeMemory on the emulated MCB chain ---------------------------
def _mcb(e, seg):
    b = bytes(e.u.mem_read(seg * 16, 5))
    return b[0], struct.unpack_from('<H', b, 1)[0], struct.unpack_from('<H', b, 3)[0]


def _mcb_set(e, seg, typ=None, owner=None, size=None):
    if typ is not None:
        e.u.mem_write(seg * 16, bytes([typ]))
    if owner is not None:
        e.u.mem_write(seg * 16 + 1, struct.pack('<H', owner))
    if size is not None:
        e.u.mem_write(seg * 16 + 3, struct.pack('<H', size))


def _compress(e, first=0x0813):
    seg = first
    while True:
        t, o, s = _mcb(e, seg)
        if t == 0x5A:
            return
        nt, no, ns = _mcb(e, seg + s + 1)
        if o == 0 and no == 0:
            _mcb_set(e, seg, typ=nt, size=s + ns + 1)
            continue
        seg = seg + s + 1


def dos_resize(e, uc):
    """INT 21h AH=4Ah, a transcription of DOSBox's DOS_ResizeMemory working on
    the emulated MCB chain (independent of bc_farheap.c dos_setblock)."""
    seg, want = uc.reg_read(X.UC_X86_REG_ES), uc.reg_read(X.UC_X86_REG_BX)
    mcb = seg - 1
    t, o, total = _mcb(e, mcb)
    if want <= total:
        if want < total:
            _mcb_set(e, seg + want, typ=t, owner=0, size=total - want - 1)
            if t == 0x5A:
                _mcb_set(e, mcb, typ=0x4D)
            _mcb_set(e, mcb, size=want)
            _compress(e)
        e._cf(uc)
        return True
    _compress(e)
    t, o, total = _mcb(e, mcb)
    nt, no, ns = _mcb(e, seg + total)
    if t != 0x5A and no == 0:
        total += ns + 1
    if want < total:
        if t != 0x5A:
            _mcb_set(e, mcb, typ=nt)
        _mcb_set(e, mcb, size=want)
        _mcb_set(e, seg + want, typ=_mcb(e, mcb)[0], owner=0, size=total - want - 1)
        _mcb_set(e, mcb, typ=0x4D)
        e._cf(uc)
        return True
    if want == total:
        if t != 0x5A:
            _mcb_set(e, mcb, typ=nt)
        _mcb_set(e, mcb, size=want)
        e._cf(uc)
        return True
    uc.reg_write(X.UC_X86_REG_BX, total)
    e._cf(uc, 8)
    return True


def main_mem():
    return open(os.path.join(ROOT, 'build', 'oracle', 'MAIN_MEM.BIN'), 'rb').read()


def setup_main_heap():
    """The original from the main() snapshot with its heap (arena, program
    MCB, CS variables) and INT 21h AH=4Ah; the port reset to main() too.
    Note: the arena covers the overlay code of the image (overlays at runtime
    2F24h and up) - only RTL code may run in this emulator."""
    mem = main_mem()
    e = Emu(snapshot='main')
    e.u.mem_write(ARENA_LIN, mem[ARENA_LIN:ARENA_END])
    e.u.mem_write(0x8130, mem[0x8130:0x8140])            # program MCB (0813h)
    e.u.mem_write(CS_VARS, mem[CS_VARS:CS_VARS + 12])
    e.int_hooks = {(0x21, 0x4A): dos_resize}
    from lale import dg_from_emu
    dg_from_emu(e)
    c_reset()
    return e, mem


# ---- the port's heap as a seed for the original's --------------------------------
class HeapState:
    """Snapshot of the port's far heap: arena bytes, CS variables, the DOS
    block size and the DGROUP brk variables."""
    def __init__(self):
        self.arena = bytes(arena())
        self.cvars = cvars()
        self.dos_block = u16.in_dll(lib, 'bc_fh_dos_block').value
        self.brk = [(lo, bytes(DG[lo:hi])) for lo, hi in BRK_RANGES]
        self.coreleft = c_coreleft()


def c_heap_reset(heap_free=None):
    """bc_farheap_reset(), then (heap_free given) one block right above the
    initial ones so that farcoreleft() = heap_free rounded down to a
    paragraph (the test's stand-in for a program that has used memory)."""
    c_reset()
    if heap_free is None:
        return
    left = c_coreleft()
    want = min(heap_free & ~15, left)
    paras = (left - want) // 16
    if paras:
        check(c_malloc(paras * 16 - 4), 'filler block')
    check(c_coreleft() == want, f'farcoreleft {c_coreleft():#x}, wanted {want:#x}')


class FarHeap:
    """The original's far heap for an emulated original under test: the real
    Borland allocator (farmalloc 0000:25D6, farfree 24C2, farrealloc 273A,
    farcalloc 28F7, malloc 25CC, calloc 1F64, realloc 2733, farcoreleft
    2987) runs in a second emulator whose arena, MCB chain, CS and DGROUP
    variables are seeded from the port's heap (HeapState).  So the original
    sees the same farcoreleft() values, the same failures and - in the blocks
    it gets - the same bytes (stale data included) as the port.

    The emulator under test cannot hold the arena at its real address (the
    overlay code of the image is there), so each live block is mirrored into
    scratch memory (B000h-DFFFh, then e.alloc): the user bytes, the slack up
    to the paragraph end and the header paragraph that follows (refreshed
    after every heap call: code that reads past its block sees the next
    block's header).  Before a block is freed or reallocated its bytes go
    back into the heap emulator.  Far pointers therefore differ from the
    port's (bc_far_addr) - compare contents, not addresses."""

    POOL = (0xB000, 0xE000)

    def __init__(self, state=None):
        state = state or HeapState()
        h = Emu()
        h.u.mem_write(ARENA_LIN, state.arena)
        mcb = main_mem()[0x8130:0x8140]
        typ = 0x5A if state.dos_block == 0x9FFF - 0x0814 else 0x4D
        h.u.mem_write(0x8130, bytes([typ]) + mcb[1:3] + struct.pack('<H', state.dos_block) + mcb[5:])
        h.u.mem_write(CS_VARS, struct.pack('<HHH', *state.cvars))
        for lo, b in state.brk:
            h.wr(0x16E0, lo, b)
        h.int_hooks = {(0x21, 0x4A): dos_resize}
        self.h = h
        self.blocks = {}                 # e seg -> (heap seg, paras)
        self.free = [list(self.POOL)]    # [start, end) runs of e paragraphs
        self.e = None

    # ---- heap emulator ----------------------------------------------------------
    def _paras(self, seg):
        return struct.unpack('<H', self.h.u.mem_read(seg * 16, 2))[0]

    def coreleft(self):
        return self.h.call('0000:2987', ret='long')

    def would_fit(self, nbytes):
        """farmalloc(nbytes) would succeed without growing past the top:
        a free block that is big enough, or room above the break."""
        paras = (nbytes + 4 + 15) // 16
        first, last, rover = struct.unpack('<HHH', bytes(self.h.u.mem_read(CS_VARS, 6)))
        seg = rover
        while seg:
            if self._paras(seg) >= paras:
                return True
            seg = struct.unpack('<H', self.h.u.mem_read(seg * 16 + 6, 2))[0]
            if seg == rover:
                break
        return paras * 16 <= self.coreleft()

    # ---- scratch regions in the emulator under test --------------------------
    def _take(self, n):
        for r in self.free:
            if r[1] - r[0] >= n:
                s = r[0]
                r[0] += n
                if r[0] == r[1]:
                    self.free.remove(r)
                return s
        s = self.e.alloc(n * 16)
        return s

    def _give(self, s, n):
        self.free.append([s, s + n])
        self.free.sort()
        out = []
        for r in self.free:
            if out and out[-1][1] == r[0]:
                out[-1][1] = r[1]
            else:
                out.append(r)
        self.free = out

    def _map(self, far):
        """heap block at far (seg:0004) -> its mirror in e; returns e's far"""
        if not far:
            return 0
        hs = far >> 16
        paras = self._paras(hs)
        es = self._take(paras + 1)
        n = min(paras + 1, 0xA000 - hs) * 16
        self.e.u.mem_write(es * 16, bytes(self.h.u.mem_read(hs * 16, n)))
        self.blocks[es] = (hs, paras)
        return (es << 16) | 4

    def _unmap(self, eseg):
        """bytes of e's mirror back into the heap emulator; its heap seg"""
        hs, paras = self.blocks.pop(eseg)
        self.h.u.mem_write(hs * 16 + 4, bytes(self.e.u.mem_read(eseg * 16 + 4, paras * 16 - 4)))
        self._give(eseg, paras + 1)
        return hs

    def _refresh(self):
        for es, (hs, paras) in self.blocks.items():
            nxt = hs + paras
            if nxt < 0xA000:
                self.e.u.mem_write((es + paras) * 16, bytes(self.h.u.mem_read(nxt * 16, 16)))
        # the brk variables of the original's DGROUP follow the real ones
        for lo, hi in BRK_RANGES:
            self.e.wr(0x16E0, lo, self.h.dgroup(lo, hi - lo))

    def _ret(self, far):
        self._refresh()
        return far & 0xFFFF, far >> 16

    # ---- the RTL entry points ------------------------------------------------------
    def farmalloc(self, n):
        return self._map(self.h.call('0000:25D6', *L(n), ret='long'))

    def attach(self, e):
        """Serve e's heap calls from this heap (replaces stub_heap's)."""
        self.e = e
        e.heap = self

        def stub(addr, f):
            lin = e.lin(addr)
            if lin in e._stubs:
                e._stubs[lin] = f
            else:
                e.stub(addr, f)

        def free(e_, a):
            if a(1) in self.blocks:
                hs = self._unmap(a(1))
                self.h.call('0000:24C2', 4, hs)
            elif a(0) | a(1):
                raise AssertionError(f'farfree of {a(1):04X}:{a(0):04X}: not a heap block')
            self._refresh()
            return 0, 0

        def realloc(e_, seg, off, n, entry):
            hs = 0
            if seg in self.blocks:              # its bytes back into the heap first
                hs, paras = self.blocks[seg]
                self.h.u.mem_write(hs * 16 + 4, bytes(self.e.u.mem_read(seg * 16 + 4, paras * 16 - 4)))
            r = self.h.call(entry, 4 if hs else 0, hs, *n, ret='long')
            if r == 0 and hs and any(n):        # failed: the old block stays where it is
                return self._ret(0)
            if hs:
                self._unmap(seg)
            return self._ret(self._map(r))

        stub('0000:25D6', lambda e_, a: self._ret(self._map(
            self.h.call('0000:25D6', a(0), a(1), ret='long'))))
        stub('0000:28F7', lambda e_, a: self._ret(self._map(
            self.h.call('0000:28F7', a(0), a(1), a(2), a(3), ret='long'))))
        stub('0000:25CC', lambda e_, a: self._ret(self._map(self.h.call('0000:25CC', a(0), ret='long'))))
        stub('0000:1F64', lambda e_, a: self._ret(self._map(
            self.h.call('0000:1F64', a(0), a(1), ret='long'))))
        stub('0000:24C2', free)
        stub('0000:273A', lambda e_, a: realloc(e_, a(1), a(0), [a(2), a(3)], '0000:273A'))
        stub('0000:2733', lambda e_, a: realloc(e_, a(1), a(0), [a(2)], '0000:2733'))
        stub('0000:2987', lambda e_, a: L(self.coreleft()))
        self._refresh()
        return self


def c_would_fit(nbytes):
    """FarHeap.would_fit on the port's heap"""
    paras = (nbytes + 4 + 15) // 16
    first, last, rover = cvars()
    a = arena()

    def w(seg, off):
        i = (seg - ARENA_SEG) * 16 + off
        return a[i] | a[i + 1] << 8
    seg = rover
    while seg:
        if w(seg, 0) >= paras:
            return True
        seg = w(seg, 6)
        if seg == rover:
            break
    return paras * 16 <= c_coreleft()


# ============================================================================
# MEMMAN's world: EMS, DDS, fatal, far heap
# ============================================================================
pc_reset = fn('pc_reset', None)
dg_reset = fn('dg_reset', None)
pc_ems_handle_mem = fn('pc_ems_handle_mem', C.POINTER(u8), u16, C.POINTER(u32))
DDSInit = fn('DDSInit', i16)
FRAME = C.addressof((C.c_uint8 * 0x10000).in_dll(lib, 'pc_ems_frame'))
FATALS = i16.in_dll(lib, 'o559A_fatal_count')
FATAL_CODE = i16.in_dll(lib, 'o559A_fatal_code')

# DGROUP bytes the EMS code owns (far pointers 86F0-86F7, E7B4-E7B7 excluded)
EMS_DG = [(0x86BC, 0x86C4), (0x86EC, 0x86F0), (0x86F8, 0x8704), (0xE7A4, 0xE7B4)]


def c_heap_seed(heap_free=None, c_setup=None):
    """The far-heap state both sides start from: DGROUP and heap reset,
    filler (heap_free), then c_setup() - port-side set-up whose
    allocations the original's environment does not repeat (stdio, vfs
    mounts: the original's file layer is a Python stand-in)."""
    dg_reset()
    c_heap_reset(heap_free)
    if c_setup:
        c_setup()
    return HeapState()


def make_emu(heap_free=None, c_setup=None, emu=None, dds=True):
    """Original with the port's PC (EMS, mouse, VGA), EMMXXXX0 device, the
    real far heap (FarHeap, seeded with c_heap_seed(heap_free, c_setup)),
    DDS initialised (dds) and o559A_fatal recorded (e.fatals).  emu: the
    emulator to set up (default a new Emu).  The port side:
    reset_c(heap_free, c_setup)."""
    seed = c_heap_seed(heap_free, c_setup)
    e = emu or Emu()
    e.stub_heap()
    e.attach_pc(lib)
    e.fatals = []
    FarHeap(seed).attach(e)
    e.stub('0000:10C5', lambda e, a: (0, 0))                       # atexit
    e.stub('16C1:002A', lambda e, a: e.fatals.append((a(2), a(5))) or (0, 0))

    def dos_open(e, uc):
        name = e._asciiz(e.reg('DS'), e.reg('DX'))
        if name.upper() != 'EMMXXXX0':
            return False
        uc.reg_write(X.UC_X86_REG_AX, 0x7E)
        e._cf(uc)
        return True

    def dos_ioctl(e, uc):
        if e.reg('BX') != 0x7E:
            return False
        uc.reg_write(X.UC_X86_REG_DX, 0x80)
        e._cf(uc)
        return True

    def dos_close(e, uc):
        if e.reg('BX') != 0x7E:
            return False
        e._cf(uc)
        return True

    def dos_rmdir(e, uc):                  # mm_init's INT 21h AX=3A01h: ignored result
        e._cf(uc, 3)
        return True
    e.int_hooks = {(0x21, 0x3D): dos_open, (0x21, 0x44): dos_ioctl, (0x21, 0x3E): dos_close,
                   (0x21, 0x3A): dos_rmdir}
    if dds:
        e.call(NAMES['DDSInit'])
    return e


def reset_c(heap_free=None, c_setup=None):
    """The port in the state make_emu gives the original."""
    pc_reset()
    c_heap_seed(heap_free, c_setup)
    DDSInit()
    FATALS.value = 0


def ems_mem(h):
    n = u32()
    p = pc_ems_handle_mem(h, C.byref(n))
    return C.string_at(p, n.value) if p else b''


def far_off(ax, dx):
    return None if (ax | dx) == 0 else ((dx << 16) | ax)


def c_ptr_far(p):
    """native page-frame pointer -> (E000 << 16 | off), NULL -> None"""
    return None if not p else (0xE000 << 16) | (p - FRAME)


# ============================================================================
# FILEMAN's file layer (o569A, far pascal) for the original
# ============================================================================
SEARCH = [os.path.join(ROOT, 'extracted', a) for a in ('3D', 'IVIR')] + [os.path.join(ROOT, 'orig')]

c_stdio_reset = fn('bc_stdio_reset', None)
c_setupio = fn('bc_setupio', None)
c_vfile_reset = fn('vfile_reset', None)
c_vfs_reset = fn('vfs_reset', None)
c_vfs_init = fn('vfs_init', i16, i16)
c_vfs_mount = fn('vfs_mount_archive', i16, cp, vp)
c_set_dir = fn('pc_set_game_dir', None, cp)


def c_mount(*archives, count=20):
    """Port side: fresh stdio + vfs over orig/ with the archives mounted."""
    c_stdio_reset()
    c_vfile_reset()
    c_vfs_reset()
    c_set_dir(os.path.join(ROOT, 'orig').encode())
    c_setupio()
    c_vfs_init(count)
    for a in archives:
        check(c_vfs_mount(a, None) >= 0, f'mount {a}')


def find_file(name):
    name = name.replace('\\', '/').split('/')[-1]
    for d in SEARCH:
        for f in os.listdir(d):
            if f.upper() == name.upper():
                return os.path.join(d, f)
    return None


def pascal_stub(e, addr, nwords, f):
    """Replace a far pascal routine: f(words) -> DX:AX (words in stack order,
    last argument first)."""
    lin = e.lin(addr)

    def hook(uc, address, size, _):
        ss, sp = uc.reg_read(X.UC_X86_REG_SS), uc.reg_read(X.UC_X86_REG_SP)
        w = struct.unpack(f'<{2 + nwords}H', uc.mem_read(ss * 16 + sp, 4 + 2 * nwords))
        r = f(w[2:]) & 0xFFFFFFFF
        uc.reg_write(X.UC_X86_REG_AX, r & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_DX, r >> 16)
        uc.reg_write(X.UC_X86_REG_SP, (sp + 4 + 2 * nwords) & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_CS, w[1])
        uc.reg_write(X.UC_X86_REG_IP, w[0])
    e.u.hook_add(U.UC_HOOK_CODE, hook, begin=lin, end=lin)


def stub_files(e):
    """o569A open/close/read/seek/tell over extracted/3D, extracted/IVIR, orig/."""
    files = {}

    def name_at(seg, off):
        s = bytearray()
        while True:
            b = e.rd(seg, off + len(s), 1, runtime=True)[0]
            if not b:
                return s.decode('cp857')
            s.append(b)

    def op(w):                                      # (name far, int mode)
        p = find_file(name_at(w[2], w[1]))
        if not p:
            return 0xFFFF
        h = max(files, default=0) + 1
        files[h] = [open(p, 'rb').read(), 0]
        return h

    def cl(w):
        files.pop(w[0], None)
        return 0

    def rd(w):                                      # (h, buf far, long n)
        f = files.get(w[4])
        if f is None:
            return 0xFFFFFFFF
        n = w[0] | w[1] << 16
        data = f[0][f[1]:f[1] + n]
        f[1] += len(data)
        e.u.mem_write(w[3] * 16 + w[2], data)
        return len(data)

    def sk(w):                                      # (h, long off, int whence)
        f = files[w[3]]
        off = w[1] | w[2] << 16
        off -= (1 << 32) if off & 0x80000000 else 0
        f[1] = [0, f[1], len(f[0])][w[0]] + off
        return 0

    def tl(w):
        return files[w[0]][1]
    pascal_stub(e, '16CD:0025', 3, op)
    pascal_stub(e, '16CD:002A', 1, cl)
    pascal_stub(e, '16CD:002F', 5, rd)
    pascal_stub(e, '16CD:0039', 4, sk)
    pascal_stub(e, '16CD:003E', 1, tl)


# ============================================================================
# a DOS file service for the original (Borland stdio, vfile, vfs)
# ============================================================================
class DosEmu(Emu):
    """Emu with a writable DOS file service rooted at `root`, same semantics as
    the port's dos_* layer (src/rtl/bc_stdio.c)."""

    DEV_INFO = [0x80D3, 0x80D3, 0x80D3, 0x80C0, 0xA8C0]

    def __init__(self, root, snapshot=None):
        super().__init__(snapshot=snapshot)
        self.root = root
        self.dos = {h: {'dev': True, 'info': self.DEV_INFO[h], 'acc': 2} for h in range(5)}

    def resolve(self, name):
        path = self.root
        for part in name.replace('\\', '/').split('/'):
            if not part or (len(part) == 2 and part[1] == ':'):
                continue
            hit = part
            if os.path.isdir(path):
                for n in os.listdir(path):
                    if n.lower() == part.lower():
                        hit = n
                        break
            path = os.path.join(path, hit)
        return path

    def _free(self):
        for h in range(20):
            if h not in self.dos:
                return h
        return None

    def _int21(self, uc, ah):
        R = lambda r: uc.reg_read(getattr(X, f'UC_X86_REG_{r}'))  # noqa: E731
        W = lambda r, v: uc.reg_write(getattr(X, f'UC_X86_REG_{r}'), v & 0xFFFF)  # noqa: E731
        al = R('AX') & 0xFF
        err = lambda c: self._cf(uc, c) or True  # noqa: E731
        if ah == 0x3D:
            acc = al & 7
            if acc > 2:
                return err(0x0C)
            p = self.resolve(self._asciiz(R('DS'), R('DX')))
            if not os.path.isfile(p):
                return err(2)
            try:
                f = open(p, 'rb' if acc == 0 else 'r+b')
            except OSError:
                return err(5)
            h = self._free()
            if h is None:
                f.close()
                return err(4)
            self.dos[h] = {'dev': False, 'info': 2, 'acc': acc, 'f': f, 'pos': 0}
            W('AX', h)
            self._cf(uc)
            return True
        if ah == 0x3C:
            h = self._free()
            if h is None:
                return err(4)
            p = self.resolve(self._asciiz(R('DS'), R('DX')))
            try:
                f = open(p, 'w+b')
            except OSError:
                return err(5 if os.path.exists(p) else 3)
            self.dos[h] = {'dev': False, 'info': 2, 'acc': 2, 'f': f, 'pos': 0}
            W('AX', h)
            self._cf(uc)
            return True
        if ah in (0x3E, 0x3F, 0x40, 0x42) or (ah == 0x44 and al == 0):
            d = self.dos.get(R('BX'))
            if d is None:
                return err(6)
            if ah == 0x3E:
                if 'f' in d:
                    d['f'].close()
                del self.dos[R('BX')]
            elif ah == 0x3F:
                if d['acc'] == 1:
                    return err(5)
                n = 0
                if not d['dev'] and d['pos'] < 0x80000000 and R('CX'):
                    d['f'].seek(d['pos'])
                    data = d['f'].read(R('CX'))
                    self.wr(R('DS'), R('DX'), data, runtime=True)
                    n = len(data)
                    d['pos'] += n
                W('AX', n)
            elif ah == 0x40:
                if d['acc'] == 0:
                    return err(5)
                n = R('CX')
                if not d['dev']:
                    if d['pos'] >= 0x80000000:
                        n = 0
                    elif n == 0:
                        d['f'].flush()
                        d['f'].truncate(d['pos'])
                    else:
                        d['f'].seek(d['pos'])
                        d['f'].write(self.rd(R('DS'), R('DX'), n, runtime=True))
                        d['f'].flush()
                        d['pos'] += n
                W('AX', n)
            elif ah == 0x42:
                if al > 2:
                    return err(1)
                pos = 0
                if not d['dev']:
                    off = (R('CX') << 16) | R('DX')
                    base = 0 if al == 0 else d['pos'] if al == 1 else os.fstat(d['f'].fileno()).st_size
                    pos = d['pos'] = (base + off) & 0xFFFFFFFF
                W('AX', pos & 0xFFFF)
                W('DX', pos >> 16)
            else:
                W('DX', d['info'])
            self._cf(uc)
            return True
        if ah == 0x44 and al == 1:
            self._cf(uc)
            return True
        if ah == 0x43:
            p = self.resolve(self._asciiz(R('DS'), R('DX')))
            if not os.path.isfile(p):
                return err(2)
            if al == 0:
                W('CX', 0x20)
            self._cf(uc)
            return True
        return False

    def close_all(self):
        for d in self.dos.values():
            if 'f' in d:
                d['f'].close()


def run_setupio(e):
    """_setupio (0000:1818) is a near routine: call it with a near return to
    0000:0000 and stop there."""
    u = e.u
    ss, sp = 0x9F00, 0xFFEE
    u.mem_write(ss * 16 + sp, struct.pack('<H', 0))
    for r, v in (('SS', ss), ('SP', sp), ('DS', 0x16E0 + LOAD), ('ES', 0x16E0 + LOAD), ('CS', LOAD),
                 ('IP', 0x1818), ('BP', 0)):
        u.reg_write(getattr(X, f'UC_X86_REG_{r}'), v)
    e._error = None
    u.emu_start(e.lin(0, 0x1818), e.lin(0, 0), count=1_000_000)
    if e._error:
        raise e._error


# ============================================================================
# VGA (src/platform/vga.h VgaState, up to the clock pointer)
# ============================================================================
class VgaState(C.Structure):
    _fields_ = [('plane', (u8 * 0x10000) * 4), ('latch', u8 * 4), ('misc_output', u8),
                ('seq_index', u8), ('seq', u8 * 8), ('gc_index', u8), ('gc', u8 * 16),
                ('crtc_index', u8), ('crtc', u8 * 32), ('attr_index', u8), ('attr', u8 * 32),
                ('attr_flipflop', u8), ('dac', u8 * 768), ('dac_write_index', u8),
                ('dac_read_index', u8), ('dac_write_comp', u8), ('dac_read_comp', u8),
                ('dac_latch', u8 * 3), ('dac_state', u8), ('pel_mask', u8)]


VGA = C.addressof(C.c_uint8.in_dll(lib, 'pc_vga'))


def vga_dac():
    """the 768 bytes of the emulated DAC"""
    return C.string_at(VGA + VgaState.dac.offset, 768)
