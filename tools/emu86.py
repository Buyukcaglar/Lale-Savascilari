#!/usr/bin/env python3
"""
emu86.py - run single IST.EXE routines in a CPU emulator (Unicorn, x86 16-bit).

This is the "original" side of the function-level differential tests
(docs/PLAN.md section 4.1).  The C side is the same function from
lale_testlib.dll, called through ctypes (tests/py/lale.py).

Memory model
------------
The FPU-restored flat image (analysis/ghidra_ist/ist_fpu.bin, every 8087
emulator interrupt turned back into a real x87 opcode, overlays linked in and
their stub thunks rewritten as JMP FAR) is relocated to the same load segment
DOSBox-X uses with tools/dosbox/ist.conf, LOAD = 0824h.  So:

    flat address  S:O   ->  runtime  (S+0824h):O     (identical to the oracle)

All public helpers take FLAT segment:offset pairs ('16E0:94C4' or (0x16E0,
0x94C4)) and add LOAD themselves; `lin()` gives the runtime linear address.

DGROUP can be seeded from an oracle memory snapshot (build/oracle/MAIN_MEM.BIN,
captured at main() by tools/dosbox/capture_boot.py) so that start-up-built
state is the real one.

Calling
-------
    e = Emu()                      # image only, DGROUP as in the file
    e = Emu(snapshot='main')       # + DGROUP/BSS/heap from the oracle at main()
    ax, dx = e.call('0C7F:0123', 5, e.far(buf_seg, buf_off))
    e.call(..., ret='long')        # DX:AX
Arguments are 16-bit words; `e.far(seg, off)` makes the two words of a far
pointer (offset pushed last so it ends up lowest - Borland large model).
Borland cdecl: caller pops, arguments pushed right to left.

Scratch memory for buffers: `e.alloc(nbytes)` returns a runtime segment
(paragraph-aligned, above the image).  `e.wr/rd(seg, off, ...)` with
`runtime=True` address runtime segments directly.

Interrupts: INT 21h file services are served from orig/ (open/read/seek/
close/size), anything else unhandled raises EmuError so a test never silently
diverges.  Far routines can be replaced by Python with `e.stub(addr, fn)`.
"""
import json
import os
import struct

import unicorn as U
import unicorn.x86_const as X

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOAD = 0x0824
DGROUP = 0x16E0
MEMSIZE = 0x110000
SENTINEL_SEG = 0xF800           # return address for call(): F800:0000 holds HLT
HEAP_SEG = 0x6200               # scratch allocations (above the relocated image)
HEAP_END = 0x9FF0
STACK_SEG, STACK_TOP = 0x0100, 0x7000   # call() stack: linear 1000h-8000h
PC_EMS_FRAME = 0xE000           # EMS page frame segment (as DOSBox-X / pc.h)


class EmuError(Exception):
    pass


def parse(addr):
    if isinstance(addr, str):
        s, o = addr.split(':')
        return int(s, 16), int(o, 16)
    return addr


class Emu:
    def __init__(self, snapshot=None, image=None):
        img = bytearray(open(image or os.path.join(ROOT, 'analysis', 'ghidra_ist', 'ist_fpu.bin'),
                             'rb').read())
        info = json.load(open(os.path.join(ROOT, 'build', 'ist_map.json')))
        self._relocate(img, info)
        self.u = U.Uc(U.UC_ARCH_X86, U.UC_MODE_16)
        self.u.mem_map(0, MEMSIZE)
        self.u.mem_write(LOAD * 16, bytes(img))
        if snapshot:
            self._load_snapshot(snapshot)
        self.u.mem_write(SENTINEL_SEG * 16, b'\xf4')          # HLT
        self._heap = HEAP_SEG
        self._stubs = {}
        self._files = {}
        self._next_handle = 5
        self.trace_ints = []
        self.u.hook_add(U.UC_HOOK_INTR, self._on_int)
        self.u.hook_add(U.UC_HOOK_MEM_INVALID, self._on_bad_mem)
        self.file_root = os.path.join(ROOT, 'orig')

    # ---- image ---------------------------------------------------------------
    @staticmethod
    def _relocate(img, info):
        def add(lin):
            v = struct.unpack_from('<H', img, lin)[0]
            struct.pack_into('<H', img, lin, (v + LOAD) & 0xFFFF)
        for r in info['relocations']:                     # MZ relocations
            s, o = parse(r)
            add(s * 16 + o)
        for ov in info['overlays']:                       # overlay segment fixups
            if ov.get('empty'):
                continue
            for f in ov['fixups']:
                if f not in ov['bad_fixups']:
                    add(ov['seg'] * 16 + f)
        for stub in info['thunks']:                       # JMP FAR seg of each thunk
            s, o = parse(stub)
            add(s * 16 + o + 3)

    def _load_snapshot(self, name):
        path = name if os.path.isfile(name) else os.path.join(ROOT, 'build', 'oracle',
                                                              f'{name.upper()}_MEM.BIN')
        mem = open(path, 'rb').read()
        # DGROUP (64 KB) and everything above the resident image (heap) come
        # from the snapshot; code stays from the image (overlays are not
        # resident at main(), and their area would otherwise be overwritten).
        dg = (DGROUP + LOAD) * 16
        self.u.mem_write(dg, mem[dg:dg + 0x10000])
        self.u.mem_write(0, mem[:0x500])                  # IVT + BDA

    # ---- addressing ----------------------------------------------------------
    @staticmethod
    def lin(seg, off=None, runtime=False):
        if off is None:
            seg, off = parse(seg)
        return ((seg if runtime else seg + LOAD) & 0xFFFF) * 16 + (off & 0xFFFF)

    def rd(self, seg, off, n, runtime=False):
        return bytes(self.u.mem_read(self.lin(seg, off, runtime), n))

    def wr(self, seg, off, data, runtime=False):
        self.u.mem_write(self.lin(seg, off, runtime), bytes(data))

    def rw(self, seg, off, runtime=False):
        return struct.unpack('<H', self.rd(seg, off, 2, runtime))[0]

    def ww(self, seg, off, v, runtime=False):
        self.wr(seg, off, struct.pack('<H', v & 0xFFFF), runtime)

    def dgroup(self, off, n):
        return self.rd(DGROUP, off, n)

    def alloc(self, nbytes, fill=0):
        """Scratch block; returns its RUNTIME segment (offset 0)."""
        seg = self._heap
        self._heap += (nbytes + 15) // 16 + 1
        if self._heap > HEAP_END:
            raise EmuError('emu heap exhausted')
        self.u.mem_write(seg * 16, bytes([fill]) * nbytes)
        return seg

    @staticmethod
    def far(seg, off):
        """Two argument words of a far pointer (runtime segment)."""
        return [off & 0xFFFF, seg & 0xFFFF]

    # ---- execution -----------------------------------------------------------
    def stub(self, addr, fn):
        """Replace the far routine at flat addr: fn(emu, args_reader) -> (ax, dx)."""
        s, o = parse(addr)
        lin = self.lin(s, o)
        self._stubs[lin] = fn
        self.u.hook_add(U.UC_HOOK_CODE, self._on_stub, begin=lin, end=lin)

    def _on_stub(self, uc, address, size, _):
        fn = self._stubs.get(address)
        if fn is None:
            return
        sp = uc.reg_read(X.UC_X86_REG_SP)
        ss = uc.reg_read(X.UC_X86_REG_SS)

        def arg(i):
            return struct.unpack('<H', uc.mem_read(ss * 16 + ((sp + 4 + 2 * i) & 0xFFFF), 2))[0]
        r = fn(self, arg) or (0, 0)
        ip, cs = struct.unpack('<HH', uc.mem_read(ss * 16 + sp, 4))
        uc.reg_write(X.UC_X86_REG_AX, r[0] & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_DX, r[1] & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_SP, (sp + 4) & 0xFFFF)
        uc.reg_write(X.UC_X86_REG_CS, cs)
        uc.reg_write(X.UC_X86_REG_IP, ip)

    def call(self, addr, *args, ds=DGROUP + LOAD, ret='int', regs=None, max_insns=200_000_000):
        """Far-call the routine at flat addr with Borland cdecl word args."""
        s, o = parse(addr)
        words = []
        for a in args:
            words.extend(a if isinstance(a, (list, tuple)) else [a])
        ss, sp = STACK_SEG, STACK_TOP     # linear 1000h-8000h: below the image, clear of heap and A000
        frame = struct.pack('<HH', 0, SENTINEL_SEG) + b''.join(
            struct.pack('<H', w & 0xFFFF) for w in words)
        sp -= len(frame)
        self.u.mem_write(ss * 16 + sp, frame)
        u = self.u
        for r, v in (('SS', ss), ('SP', sp), ('DS', ds), ('ES', ds), ('BP', 0),
                     ('SI', 0), ('DI', 0), ('AX', 0), ('BX', 0), ('CX', 0), ('DX', 0)):
            u.reg_write(getattr(X, f'UC_X86_REG_{r}'), v)
        for r, v in (regs or {}).items():
            u.reg_write(getattr(X, f'UC_X86_REG_{r.upper()}'), v)
        u.reg_write(X.UC_X86_REG_EFLAGS, 0x0202)
        # Unicorn starts with FPU control word 0 (24-bit precision) and FNINIT
        # does not fix it: set the power-on/FNINIT value (64-bit precision)
        if not getattr(self, '_fpu_ready', False):
            u.reg_write(X.UC_X86_REG_FPCW, 0x037F)
            self._fpu_ready = True
        start = self.lin(s, o)
        u.reg_write(X.UC_X86_REG_CS, (s + LOAD) & 0xFFFF)
        self._error = None
        # Unicorn's 16-bit begin address is CS:IP linear; stop at the sentinel HLT
        u.reg_write(X.UC_X86_REG_IP, o)
        u.emu_start(start, SENTINEL_SEG * 16, count=max_insns)
        if self._error:
            raise self._error
        cs, ip = u.reg_read(X.UC_X86_REG_CS), u.reg_read(X.UC_X86_REG_IP)
        if cs * 16 + ip != SENTINEL_SEG * 16:
            raise EmuError(f'stopped at {cs:04X}:{ip:04X} (runtime), not at the return')
        ax, dx = u.reg_read(X.UC_X86_REG_AX), u.reg_read(X.UC_X86_REG_DX)
        if ret == 'long':
            return (dx << 16) | ax
        if ret == 'regs':
            return {r: u.reg_read(getattr(X, f'UC_X86_REG_{r}'))
                    for r in ('AX', 'BX', 'CX', 'DX', 'SI', 'DI', 'BP', 'DS', 'ES')}
        return ax, dx

    def reg(self, name):
        return self.u.reg_read(getattr(X, f'UC_X86_REG_{name.upper()}'))

    # ---- RTL stand-ins ---------------------------------------------------------
    def stub_heap(self):
        """Serve Borland farmalloc/farcalloc/farfree/malloc/free from the scratch
        heap (the real allocator needs the DOS arena, which a bare image lacks).
        Returned pointers are runtime seg:0004 (Borland's far-heap block
        header is 4 bytes, so user pointers have offset 4 - kept here)."""
        def alloc(n, zero=False):
            seg = self.alloc(n + 4)
            return 4, seg
        self.stub('0000:25D6', lambda e, a: alloc((a(1) << 16) | a(0)))        # farmalloc(long)
        self.stub('0000:28F7', lambda e, a: alloc(((a(1) << 16) | a(0)) * ((a(3) << 16) | a(2))))  # farcalloc
        self.stub('0000:25CC', lambda e, a: alloc(a(0)))                        # malloc(unsigned)
        self.stub('0000:1F64', lambda e, a: alloc(a(0) * a(1)))                # calloc
        self.stub('0000:24C2', lambda e, a: (0, 0))                             # farfree
        self.stub('0000:2987', lambda e, a: (0xFFF0, 0x0003))                   # farcoreleft

    def attach_pc(self, lib):
        """Route the original's hardware/driver access to the port's emulated PC
        (src/platform/pc.c in lale_testlib): INT 67h (EMS, with the E000 page
        frame kept in sync with pc_ems_frame), INT 33h (mouse), INT 10h AH=00h,
        and VGA port I/O (3C0h-3DFh) on pc_vga.  Run the original, record the
        state, call lib.pc_reset(), run the C, compare."""
        import ctypes
        self.pc = lib
        self._frame = (ctypes.c_uint8 * 0x10000).in_dll(lib, 'pc_ems_frame')
        self._pcvga = ctypes.c_void_p.in_dll(lib, 'pc_vga')     # address only
        self._vga_addr = ctypes.addressof(ctypes.c_uint8.in_dll(lib, 'pc_vga'))
        lib.vga_out8.argtypes = (ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint8)
        lib.vga_in8.argtypes = (ctypes.c_void_p, ctypes.c_uint16)
        lib.vga_in8.restype = ctypes.c_uint8
        lib.vga_bios_set_mode.argtypes = (ctypes.c_void_p, ctypes.c_uint8)
        lib.pc_in_3da.restype = ctypes.c_uint8
        self.u.hook_add(U.UC_HOOK_INSN, self._on_out, None, 1, 0, X.UC_X86_INS_OUT)
        self.u.hook_add(U.UC_HOOK_INSN, self._on_in, None, 1, 0, X.UC_X86_INS_IN)

    def _on_out(self, uc, port, size, value, _):
        if 0x3C0 <= port <= 0x3DF:
            self.pc.vga_out8(self._vga_addr, port, value & 0xFF)
            if size == 2:
                self.pc.vga_out8(self._vga_addr, port + 1, (value >> 8) & 0xFF)

    def _on_in(self, uc, port, size, _):
        if port == 0x3DA:
            return self.pc.pc_in_3da()
        if 0x3C0 <= port <= 0x3DF:
            return self.pc.vga_in8(self._vga_addr, port)
        return 0xFF

    def _pc_int(self, uc, intno):
        import ctypes
        R = lambda r: uc.reg_read(getattr(X, f'UC_X86_REG_{r}'))  # noqa: E731
        W = lambda r, v: uc.reg_write(getattr(X, f'UC_X86_REG_{r}'), v & 0xFFFF)  # noqa: E731

        class Regs(ctypes.Structure):
            _fields_ = [('ax', ctypes.c_uint16), ('bx', ctypes.c_uint16),
                        ('cx', ctypes.c_uint16), ('dx', ctypes.c_uint16)]
        r = Regs(R('AX'), R('BX'), R('CX'), R('DX'))
        if intno == 0x67:
            fr = PC_EMS_FRAME * 16
            ctypes.memmove(ctypes.addressof(self._frame), bytes(uc.mem_read(fr, 0x10000)), 0x10000)
            if (r.ax >> 8) == 0x57:
                st = self._ems_move(uc, R('DS'), R('SI'))
                r.ax = (st << 8) | (r.ax & 0xFF)
            else:
                self.pc.pc_int67(ctypes.byref(r))
            uc.mem_write(fr, bytes(self._frame))
        elif intno == 0x33:
            self.pc.pc_int33(ctypes.byref(r))
        elif intno == 0x10 and (r.ax >> 8) == 0:
            self.pc.vga_bios_set_mode(self._vga_addr, r.ax & 0x7F | (r.ax & 0x80))
        else:
            return False
        W('AX', r.ax); W('BX', r.bx); W('CX', r.cx); W('DX', r.dx)
        return True

    def _ems_move(self, uc, ds, si):
        """INT 67h AX=5700h: LIM move descriptor at DS:SI -> lib.pc_ems_move.
        Conventional sides are copied into a native buffer (or, inside the
        E000 page frame, pointed at the lib's pc_ems_frame) and copied back
        when they are the destination."""
        import ctypes

        class Move(ctypes.Structure):
            _fields_ = [('length', ctypes.c_uint32),
                        ('src_type', ctypes.c_uint8), ('src_handle', ctypes.c_uint16),
                        ('src_off', ctypes.c_uint16), ('src_page', ctypes.c_uint16),
                        ('src_ptr', ctypes.c_void_p),
                        ('dst_type', ctypes.c_uint8), ('dst_handle', ctypes.c_uint16),
                        ('dst_off', ctypes.c_uint16), ('dst_page', ctypes.c_uint16),
                        ('dst_ptr', ctypes.c_void_p)]
        d = bytes(uc.mem_read(ds * 16 + si, 18))
        length = struct.unpack_from('<I', d, 0)[0]
        m = Move(length=length)
        keep, back = [], None
        for side, o in (('src', 4), ('dst', 11)):
            typ, h, off, sp = d[o], *struct.unpack_from('<HHH', d, o + 1)
            setattr(m, side + '_type', typ)
            setattr(m, side + '_handle', h)
            setattr(m, side + '_off', off)
            setattr(m, side + '_page', sp)
            if typ == 0:
                lin = sp * 16 + off
                fr = PC_EMS_FRAME * 16
                if fr <= lin and lin + length <= fr + 0x10000:
                    setattr(m, side + '_ptr', ctypes.addressof(self._frame) + lin - fr)
                else:
                    n = min(length, MEMSIZE - lin)
                    buf = ctypes.create_string_buffer(bytes(uc.mem_read(lin, n)), max(n, 1))
                    keep.append(buf)
                    setattr(m, side + '_ptr', ctypes.addressof(buf))
                    if side == 'dst':
                        back = (lin, buf, n)
        self.pc.pc_ems_move.restype = ctypes.c_uint8
        st = self.pc.pc_ems_move(ctypes.byref(m))
        if back and st in (0, 0x92):
            uc.mem_write(back[0], back[1].raw[:back[2]])
        return st

    # ---- interrupts ----------------------------------------------------------
    def _on_bad_mem(self, uc, access, address, size, value, _):
        self._error = EmuError(f'invalid memory access at {address:X}')
        return False

    def _on_int(self, uc, intno, _):
        ah = (uc.reg_read(X.UC_X86_REG_AX) >> 8) & 0xFF
        self.trace_ints.append((intno, ah))
        hook = getattr(self, 'int_hooks', {}).get((intno, ah))   # {(int, ah): fn(emu, uc) -> bool}
        if hook and hook(self, uc):
            return
        if intno == 0x21 and self._int21(uc, ah):
            return
        if getattr(self, 'pc', None) and self._pc_int(uc, intno):
            return
        if intno == 0:          # the game's INT 0 handler: result FFFFh, continue
            raise EmuError('divide error (the original installs a handler: emulate it explicitly)')
        self._error = EmuError(f'unhandled INT {intno:02X}h AH={ah:02X}h at '
                               f'{uc.reg_read(X.UC_X86_REG_CS):04X}:{uc.reg_read(X.UC_X86_REG_IP):04X}')
        uc.emu_stop()

    def _cf(self, uc, err=None):
        fl = uc.reg_read(X.UC_X86_REG_EFLAGS)
        if err is None:
            uc.reg_write(X.UC_X86_REG_EFLAGS, fl & ~1)
        else:
            uc.reg_write(X.UC_X86_REG_EFLAGS, fl | 1)
            uc.reg_write(X.UC_X86_REG_AX, err)

    def _asciiz(self, seg, off):
        s = bytearray()
        while True:
            b = self.rd(seg, off + len(s), 1, runtime=True)[0]
            if not b:
                return s.decode('cp857')
            s.append(b)

    def _int21(self, uc, ah):
        R = lambda r: uc.reg_read(getattr(X, f'UC_X86_REG_{r}'))  # noqa: E731
        W = lambda r, v: uc.reg_write(getattr(X, f'UC_X86_REG_{r}'), v & 0xFFFF)  # noqa: E731
        if ah == 0x3D:                                    # open
            name = self._asciiz(R('DS'), R('DX'))
            p = os.path.join(self.file_root, name.replace('\\', os.sep).lstrip(os.sep))
            if not os.path.isfile(p):
                self._cf(uc, 2)
                return True
            h = self._next_handle
            self._next_handle += 1
            self._files[h] = open(p, 'rb')
            W('AX', h)
            self._cf(uc)
            return True
        if ah == 0x3E:                                    # close
            f = self._files.pop(R('BX'), None)
            if f:
                f.close()
            self._cf(uc, None if f else 6)
            return True
        if ah == 0x3F:                                    # read
            f = self._files.get(R('BX'))
            if not f:
                self._cf(uc, 6)
                return True
            data = f.read(R('CX'))
            self.wr(R('DS'), R('DX'), data, runtime=True)
            W('AX', len(data))
            self._cf(uc)
            return True
        if ah == 0x42:                                    # lseek
            f = self._files.get(R('BX'))
            if not f:
                self._cf(uc, 6)
                return True
            pos = (R('CX') << 16) | R('DX')
            if pos & 0x80000000:
                pos -= 1 << 32
            f.seek(pos, R('AX') & 0xFF)
            p = f.tell()
            W('AX', p & 0xFFFF)
            W('DX', p >> 16)
            self._cf(uc)
            return True
        return False
