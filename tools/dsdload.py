#!/usr/bin/env python3
"""
dsdload.py - parse, relocate and run DSMI ".DSD" driver images (SDISB16.DSD & co).

Format (docs/FORMATS.md, "DSMI driver (.DSD)"): an MZ-style executable whose
signature is "DS" instead of "MZ".  IST.EXE's DDSLoadDriver (0DF2:0611) loads
the load module at a paragraph-aligned block, adds the load segment at every
relocation and far-calls the entry (CS:IP from the header) as
    pascal long entry(long cmd, void far *DDSAccessFunction, long 0)
cmd 1 = load (returns AX = 0 ok, DX = device id), cmd 2 = unload.
The driver then pulls every import by name through DDSAccessFunction and
registers its own functions (SDIInitDevice ...) and resources.

Command line
------------
    python tools/dsdload.py orig/SDISB16.DSD            # header, relocations, names,
                                                         # imports/exports (runs dsd_main)
    python tools/dsdload.py orig/SDISB16.DSD --flat out.bin [--base 1000]
    python tools/dsdload.py orig/SDISB16.DSD --dis 1675 1700   # capstone listing (image offsets)

Library
-------
    d = Dsd('orig/SDISB16.DSD'); d.header, d.relocs, d.image, d.names, d.relocated(seg)
    e = DsdEmu('orig/SDISB16.DSD')   # Unicorn: driver loaded at LOAD_SEG, DDS imports
                                     # stubbed in Python, SB DSP ports emulated
    e.load()                         # entry(1, ...) -> e.exports, e.resources
    e.call_export('SDIInitDevice', e.far(seg, off))
    e.cdi_table                      # {slot_name: (seg, off)} after cdiRegister
    e.call_far((seg, off), *pascal_dword_args)

All arguments to call_far() are 32-bit values pushed in order (DSMI pascal
convention: first argument pushed first).  Far pointers are passed as
(seg << 16) | off.
"""
import argparse
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HEADER_FIELDS = (            # offset, name (MZ meaning / DS meaning)
    (0x00, 'signature'),     # 'DS'
    (0x02, 'last_page_bytes'),
    (0x04, 'pages'),
    (0x06, 'reloc_count'),
    (0x08, 'header_paras'),
    (0x0A, 'min_extra_paras'),
    (0x0C, 'device_id'),     # MZ max_extra: SB16 0x64, WSS16 0x65, PAS16 0x66, GUS16 0xC8
    (0x0E, 'ss'),
    (0x10, 'sp'),
    (0x12, 'version'),       # MZ checksum: 0x0100
    (0x14, 'ip'),
    (0x16, 'cs'),
    (0x18, 'reloc_offset'),
    (0x1A, 'flags'),         # MZ overlay number: bit0 must be 1 ("loadable driver")
)

CDI_SLOTS = ('setSample', 'playSample', 'setVolume', 'setFrequency', 'setLinearRate',
             'setPosition', 'setPanning', 'setMasterVolume', 'mute', 'unmute', 'stop',
             'muteAll', 'unmuteAll', 'downloadSample', 'unloadSample', 'unloadAll',
             'getVolume', 'getFrequency', 'getPosition', 'getPanning', 'getSample',
             'setupChannels', 'getChannelStatus', 'poll', 'setEffect', 'getStatus')


class Dsd:
    def __init__(self, path):
        self.path = path
        data = open(path, 'rb').read()
        self.file = data
        h = struct.unpack_from('<14H', data, 0)
        self.header = {name: h[i] for i, (_, name) in enumerate(HEADER_FIELDS)}
        if self.header['signature'] != 0x5344:
            raise ValueError(f'{path}: not a DS driver')
        hd = self.header
        # DDSLoadDriver: 16-bit arithmetic, +0x20 slack (reads 0x20 bytes past EOF)
        self.load_size = ((hd['pages'] - 1) * 512 + hd['last_page_bytes']
                          - hd['header_paras'] * 16 + 0x20) & 0xFFFF
        self.alloc_size = self.load_size + hd['min_extra_paras'] * 16 + 0x20
        start = hd['header_paras'] * 16
        img = bytearray(data[start:start + self.load_size])
        img += bytes(self.alloc_size - len(img))           # short read + BSS: zero here
        self.image = img
        self.relocs = [struct.unpack_from('<HH', data, hd['reloc_offset'] + 4 * i)[::-1]
                       for i in range(hd['reloc_count'])]  # (seg, off)
        self.entry = (hd['cs'], hd['ip'])
        self.names = self._find_names()

    def _find_names(self):
        """Procedure names the driver's assembler embedded before each routine:
        the ASCII name followed by a length byte; code starts after it."""
        out = []
        for m in re.finditer(rb'[A-Za-z_][A-Za-z0-9_]{3,}', bytes(self.image)):
            e = m.end()
            if e < len(self.image) and self.image[e] == e - m.start():
                out.append((e + 1, m.group().decode()))
        return out

    def segments(self):
        return sorted({struct.unpack_from('<H', self.image, s * 16 + o)[0]
                       for s, o in self.relocs})

    def relocated(self, base_seg):
        img = bytearray(self.image)
        for s, o in self.relocs:
            lin = s * 16 + o
            v = struct.unpack_from('<H', img, lin)[0]
            struct.pack_into('<H', img, lin, (v + base_seg) & 0xFFFF)
        return img


# ---------------------------------------------------------------------------
# Unicorn harness
# ---------------------------------------------------------------------------
LOAD_SEG = 0x1000          # driver image (runtime segment)
STUB_SEG = 0xF000          # import stubs: STUB_SEG:4*n holds RETF n
SENTINEL = 0xF800          # return address of call_far: HLT
HEAP_SEG = 0x3000          # MemAlloc & scratch
HEAP_END = 0x9F00
STACK = (0x9F00, 0xFFF0)


class EmuError(Exception):
    pass


class DsdEmu:
    """Run a .DSD driver under Unicorn with the DDS environment stubbed."""

    def __init__(self, path=None, load_seg=LOAD_SEG):
        import unicorn as U
        import unicorn.x86_const as X
        self.U, self.X = U, X
        self.dsd = Dsd(path or os.path.join(ROOT, 'orig', 'SDISB16.DSD'))
        self.load_seg = load_seg
        self.u = U.Uc(U.UC_ARCH_X86, U.UC_MODE_16)
        self.u.mem_map(0, 0x110000)
        self.u.mem_write(load_seg * 16, bytes(self.dsd.relocated(load_seg)))
        self.u.mem_write(SENTINEL * 16, b'\xf4')
        self.heap = HEAP_SEG
        self.time = 0                  # GetDSMITime
        self.dma_pos = 0               # SDIGetBufferPosition
        self.env = {}                  # GetEnvString
        self.exports = {}              # DDSRegisterFunction: name -> (seg, off)
        self.resources = {}            # DDSRegisterResource: name -> ((seg, off), size)
        self.accessed = []             # names the driver asked for
        self.cdi_table = None
        self.cdi_addr = None
        self.vectors = {}
        self.real_address = None       # MCPSampleRealAddress(handle, pos) -> (seg, off)
        self.virtual_calls = []        # MCPEnable/DisableVirtualSamples trace
        self.port_log = []
        self._stub_fns = {}
        self._next_stub = 0
        self._error = None
        self._dsp_out = []
        self._dsp_reset = 0
        self.imports = {}
        self._make_imports()
        self.u.hook_add(U.UC_HOOK_INSN, self._on_out, None, 1, 0, X.UC_X86_INS_OUT)
        self.u.hook_add(U.UC_HOOK_INSN, self._on_in, None, 1, 0, X.UC_X86_INS_IN)
        self.u.hook_add(U.UC_HOOK_INTR, self._on_int)

    # ---- memory --------------------------------------------------------------
    def lin(self, seg, off):
        return (seg & 0xFFFF) * 16 + (off & 0xFFFF)

    def rd(self, seg, off, n):
        return bytes(self.u.mem_read(self.lin(seg, off), n))

    def wr(self, seg, off, data):
        self.u.mem_write(self.lin(seg, off), bytes(data))

    def rw(self, seg, off):
        return struct.unpack('<H', self.rd(seg, off, 2))[0]

    def rdw(self, seg, off):
        return struct.unpack('<I', self.rd(seg, off, 4))[0]

    def alloc(self, nbytes, fill=0):
        seg = self.heap
        self.heap += (nbytes + 15) // 16 + 1
        if self.heap > HEAP_END:
            raise EmuError('heap exhausted')
        self.u.mem_write(seg * 16, bytes([fill]) * nbytes)
        return seg

    @staticmethod
    def far(seg, off):
        return ((seg & 0xFFFF) << 16) | (off & 0xFFFF)

    def asciiz(self, farptr):
        seg, off = farptr >> 16, farptr & 0xFFFF
        s = bytearray()
        while True:
            b = self.rd(seg, off + len(s), 1)[0]
            if not b:
                return s.decode('latin-1')
            s.append(b)

    @property
    def data_seg(self):
        return self.load_seg + 0x3D3 if 'SDISB16' in self.dsd.path.upper() else None

    # ---- import stubs --------------------------------------------------------
    def stub(self, fn, argbytes):
        """Allocate STUB_SEG:off holding RETF argbytes; fn(args_dwords_reader) -> (ax, dx)."""
        off = self._next_stub
        self._next_stub += 4
        code = b'\xcb' if argbytes == 0 else b'\xca' + struct.pack('<H', argbytes)
        self.u.mem_write(STUB_SEG * 16 + off, code)
        lin = STUB_SEG * 16 + off
        self._stub_fns[lin] = fn
        self.u.hook_add(self.U.UC_HOOK_CODE, self._on_stub, begin=lin, end=lin)
        return (STUB_SEG << 16) | off

    def _on_stub(self, uc, address, size, _):
        X = self.X
        fn = self._stub_fns[address]
        sp = uc.reg_read(X.UC_X86_REG_SP)
        ss = uc.reg_read(X.UC_X86_REG_SS)

        def arg(i):          # i-th dword from the top of the argument area (last pushed = 0)
            return struct.unpack('<I', uc.mem_read(ss * 16 + ((sp + 4 + 4 * i) & 0xFFFF), 4))[0]
        r = fn(arg)
        if r is not None:
            ax, dx = r
            uc.reg_write(X.UC_X86_REG_AX, ax & 0xFFFF)
            uc.reg_write(X.UC_X86_REG_DX, dx & 0xFFFF)

    def _make_imports(self):
        def wr_far(pp, value):
            self.wr(pp >> 16, pp & 0xFFFF, struct.pack('<I', value))

        def access(a):                       # DDSAccessFunction(name, out) pascal
            name = self.asciiz(a(1))
            self.accessed.append(name)
            fn = self.imports.get(name) or self.exports.get(name)
            if fn is None:
                return 9, 0
            if isinstance(fn, tuple):
                fn = (fn[0] << 16) | fn[1]
            wr_far(a(0), fn)
            return 0, 0

        def access_res(a):
            name = self.asciiz(a(2))
            r = self.resources.get(name)
            if r is None:
                return 10, 0
            wr_far(a(1), (r[0][0] << 16) | r[0][1])
            wr_far(a(0), r[1])
            return 0, 0

        def reg_fn(a):
            self.exports[self.asciiz(a(1))] = (a(0) >> 16, a(0) & 0xFFFF)
            return 0, 0

        def reg_res(a):
            self.resources[self.asciiz(a(2))] = ((a(1) >> 16, a(1) & 0xFFFF), a(0))
            return 0, 0

        def remove_fn(a):
            self.exports.pop(self.asciiz(a(0)), None)
            return 0, 0

        def remove_res(a):
            self.resources.pop(self.asciiz(a(0)), None)
            return 0, 0

        def mem_alloc(a):                    # MemAlloc(long size, void far **out)
            seg = self.alloc(a(1))
            wr_far(a(0), seg << 16)
            return 0, 0

        def mem_calloc(a):                   # MemCalloc(long n, long size, out)
            seg = self.alloc(a(2) * a(1))
            wr_far(a(0), seg << 16)
            return 0, 0

        def cdi_register(a):
            p = a(0)
            self.cdi_addr = (p >> 16, p & 0xFFFF)
            raw = self.rd(p >> 16, p & 0xFFFF, 0x68)
            self.cdi_table = {}
            for i, name in enumerate(CDI_SLOTS):
                off, seg = struct.unpack_from('<HH', raw, 4 * i)
                self.cdi_table[name] = (seg, off)
            return 0, 0

        def set_vector(a):                   # SetVector(long intno, handler) -> old
            old = self.vectors.get(a(1) & 0xFF, 0)
            self.vectors[a(1) & 0xFF] = a(0)
            return old & 0xFFFF, old >> 16

        def get_env(a):
            v = self.env.get(self.asciiz(a(0)))
            if v is None:
                return 0, 0
            seg = self.alloc(len(v) + 1)
            self.wr(seg, 0, v.encode() + b'\0')
            return 0, seg

        def real_address(a):                 # MCPSampleRealAddress(long handle, long pos)
            if self.real_address is None:
                return 0, 0
            seg, off = self.real_address(a(1), a(0))
            return off, seg

        def dsmi_time(a):
            return self.time & 0xFFFF, self.time >> 16

        def buf_pos(a):
            return self.dma_pos & 0xFFFF, 0

        def virt(tag):
            def f(a):
                self.virtual_calls.append(tag)
                return 0, 0
            return f

        I = self.imports
        I['DDSAccessFunction'] = self.stub(access, 8)
        I['DDSAccessResource'] = self.stub(access_res, 12)
        I['DDSRegisterFunction'] = self.stub(reg_fn, 8)
        I['DDSRegisterResource'] = self.stub(reg_res, 12)
        I['DDSRemoveFunction'] = self.stub(remove_fn, 4)
        I['DDSRemoveResource'] = self.stub(remove_res, 4)
        I['MemAlloc'] = self.stub(mem_alloc, 8)
        I['MemCalloc'] = self.stub(mem_calloc, 12)
        I['MemRealloc'] = self.stub(lambda a: (12, 0), 8)
        I['MemFree'] = self.stub(lambda a: (0, 0), 4)
        I['cdiRegister'] = self.stub(cdi_register, 4)
        I['SetVector'] = self.stub(set_vector, 8)
        I['GetEnvString'] = self.stub(get_env, 4)
        I['GetDSMITime'] = self.stub(dsmi_time, 0)
        I['MCPSampleRealAddress'] = self.stub(real_address, 8)
        I['MCPEnableVirtualSamples'] = self.stub(virt('enable'), 0)
        I['MCPDisableVirtualSamples'] = self.stub(virt('disable'), 0)
        # the driver registers its own SDIGetBufferPosition (DMA position read
        # from the 8237); the harness serves it so a test controls the position
        I['SDIGetBufferPosition'] = self.stub(buf_pos, 0)

    # ---- ports: minimal Sound Blaster DSP ------------------------------------
    def _on_out(self, uc, port, size, value, _):
        self.port_log.append(('out', port, value))
        base = port & ~0xF
        if port - base == 6:                       # DSP reset
            if value & 1:
                self._dsp_reset = 1
            elif self._dsp_reset:
                self._dsp_reset = 0
                self._dsp_out = [0xAA]
        elif port - base == 0xC:                   # DSP command/data
            if value == 0xE1:                      # version 4.13 (SB16)
                self._dsp_out += [4, 13]

    def _on_in(self, uc, port, size, _):
        base = port & ~0xF
        r = 0
        if 0x200 <= port < 0x300:
            if port - base == 0xE:                 # read-buffer status
                r = 0xFF if self._dsp_out else 0x7F
            elif port - base == 0xA:
                r = self._dsp_out.pop(0) if self._dsp_out else 0xFF
            elif port - base == 0xC:               # write status: ready
                r = 0x7F
            elif port - base == 0x5:               # mixer data
                r = 0
        self.port_log.append(('in', port, r))
        return r

    def _on_int(self, uc, intno, _):
        X = self.X
        self._error = EmuError(f'unhandled INT {intno:02X}h at '
                               f'{uc.reg_read(X.UC_X86_REG_CS):04X}:{uc.reg_read(X.UC_X86_REG_IP):04X}')
        uc.emu_stop()

    # ---- calls ---------------------------------------------------------------
    def call_far(self, target, *args, max_insns=50_000_000, ret='long'):
        """Far-call seg:off (tuple or dword) with 32-bit pascal arguments."""
        X = self.X
        if isinstance(target, int):
            target = (target >> 16, target & 0xFFFF)
        seg, off = target
        ss, sp = STACK
        frame = struct.pack('<HH', 0, SENTINEL)
        for a in reversed(args):          # first argument ends up highest
            frame += struct.pack('<I', a & 0xFFFFFFFF)
        sp -= len(frame)
        self.u.mem_write(ss * 16 + sp, frame)
        u = self.u
        for r, v in (('SS', ss), ('SP', sp), ('DS', 0), ('ES', 0), ('EBP', 0),
                     ('ESI', 0), ('EDI', 0), ('EAX', 0), ('EBX', 0), ('ECX', 0), ('EDX', 0)):
            u.reg_write(getattr(X, f'UC_X86_REG_{r}'), v)
        u.reg_write(X.UC_X86_REG_EFLAGS, 0x0202)
        u.reg_write(X.UC_X86_REG_CS, seg)
        u.reg_write(X.UC_X86_REG_IP, off)
        self._error = None
        u.emu_start(self.lin(seg, off), SENTINEL * 16, count=max_insns)
        if self._error:
            raise self._error
        cs, ip = u.reg_read(X.UC_X86_REG_CS), u.reg_read(X.UC_X86_REG_IP)
        if cs * 16 + ip != SENTINEL * 16:
            raise EmuError(f'stopped at {cs:04X}:{ip:04X}, not at the return')
        ax, dx = u.reg_read(X.UC_X86_REG_AX), u.reg_read(X.UC_X86_REG_DX)
        if ret == 'eax':
            return u.reg_read(X.UC_X86_REG_EAX)
        return (dx << 16) | ax

    def load(self):
        """Run entry(1, DDSAccessFunction, 0) like DDSLoadDriver."""
        cs, ip = self.dsd.entry
        r = self.call_far(((cs + self.load_seg) & 0xFFFF, ip), 1, self.imports['DDSAccessFunction'], 0)
        if r & 0xFFFF:
            raise EmuError(f'driver init failed: {r & 0xFFFF}')
        self.device_id = r >> 16
        return self.device_id

    def call_export(self, name, *args, **kw):
        return self.call_far(self.exports[name], *args, **kw)

    def cdi(self, slot, *args, **kw):
        return self.call_far(self.cdi_table[slot], *args, **kw)


# ---------------------------------------------------------------------------
def disassemble(dsd, start, end):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    img = dsd.image
    rel = {s * 16 + o for s, o in dsd.relocs}
    marks = {p - len(n) - 1: (n, p) for p, n in dsd.names}
    bounds = sorted({start, end} | set(marks) | {p for _, p in marks.values()})
    pc = start
    while pc < end:
        if pc in marks:
            print(f'\n; ===== {marks[pc][0]}')
            pc = marks[pc][1]
            continue
        nxt = min([b for b in bounds if b > pc] + [end])
        ins = next(md.disasm(bytes(img[pc:nxt]), pc), None)
        if ins is None:
            print(f'{pc:04x}: db {img[pc]:02x}')
            pc += 1
            continue
        r = '   ; reloc' if any(a in rel for a in range(pc, pc + ins.size)) else ''
        print(f'{pc:04x}: {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}{r}')
        pc += ins.size


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('file')
    ap.add_argument('--base', default='1000', help='load segment (hex) for --flat / emulation')
    ap.add_argument('--flat', help='write the relocated image here')
    ap.add_argument('--dis', nargs=2, metavar=('START', 'END'), help='disassemble image range (hex)')
    ap.add_argument('--no-run', action='store_true', help='do not run dsd_main in Unicorn')
    a = ap.parse_args()
    d = Dsd(a.file)
    base = int(a.base, 16)
    if a.dis:
        disassemble(d, int(a.dis[0], 16), int(a.dis[1], 16))
        return
    if a.flat:
        open(a.flat, 'wb').write(d.relocated(base))
        print(f'wrote {a.flat} ({d.alloc_size} bytes, base {base:04X})')
        return
    print(f'{a.file}')
    for off, name in HEADER_FIELDS:
        print(f'  +{off:02X} {name:<16} {d.header[name]:#06x}')
    print(f'  load module {d.load_size:#x} bytes (+min alloc -> {d.alloc_size:#x}), '
          f'entry {d.entry[0]:04X}:{d.entry[1]:04X}')
    segs = d.segments()
    print(f'  {len(d.relocs)} relocations; segment values referenced: '
          + ', '.join(f'{s:04X} (image +{s * 16:#x})' for s in segs))
    print('  embedded procedure names (image offset of code):')
    for p, n in d.names:
        print(f'    {p:04x}  {n}')
    if a.no_run:
        return
    e = DsdEmu(a.file, base)
    dev = e.load()
    print(f'  dsd_main(1): device id {dev:#x}')
    print('  imports (DDSAccessFunction): ' + ', '.join(e.accessed))
    print('  exports (DDSRegisterFunction):')
    for n, (s, o) in e.exports.items():
        print(f'    {n:<24} image {((s - base) * 16 + o):#06x}')
    for n, ((s, o), size) in e.resources.items():
        print(f'  resource {n} at image {((s - base) * 16 + o):#06x}, size {size:#x}')


if __name__ == '__main__':
    sys.exit(main())
