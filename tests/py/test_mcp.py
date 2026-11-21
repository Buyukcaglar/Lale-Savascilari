"""MCP software mixer (src/dsmi/mcp.c) against the original SDISB16.DSD.

The driver runs in Unicorn (tools/dsdload.py DsdEmu: relocated image, DDS
imports stubbed, SB DSP ports emulated).  Both sides get the same SDI config,
the same CDI command stream with the same GetDSMITime stamps, and polls with
the same DMA positions; after every poll the whole DMA ring, the write
position and the raw channel records must be identical.
"""
import ctypes
import glob
import os
import random
import struct

from lale import ROOT, check, fn, lib
from dsdload import DsdEmu

SDISB16 = os.path.join(ROOT, 'orig', 'SDISB16.DSD')
CFG = open(os.path.join(ROOT, 'orig', 'SES.CFG'), 'rb').read()[13:13 + 0x5C]


class SampleRef(ctypes.Structure):
    _fields_ = [('ptr', ctypes.c_void_p), ('ems_handle', ctypes.c_uint16), ('in_ems', ctypes.c_uint8)]


class SampleInfo(ctypes.Structure):
    _fields_ = [('sample', SampleRef), ('length', ctypes.c_uint32), ('loopstart', ctypes.c_uint32),
                ('loopend', ctypes.c_uint32), ('mode', ctypes.c_uint8), ('sampleID', ctypes.c_uint16),
                ('rate', ctypes.c_uint16), ('freq', ctypes.c_uint32), ('volume', ctypes.c_uint8)]


class McpConfig(ctypes.Structure):
    _fields_ = [('options', ctypes.c_uint16), ('stereo', ctypes.c_uint8), ('sample_size', ctypes.c_uint8),
                ('rate', ctypes.c_uint16), ('buffer_size', ctypes.c_uint16)]


TIMEFN = ctypes.CFUNCTYPE(ctypes.c_uint32)
REALFN = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32)
VOIDFN = ctypes.CFUNCTYPE(None)
POSFN = ctypes.CFUNCTYPE(ctypes.c_uint16)
OFFFN = ctypes.CFUNCTYPE(ctypes.c_uint16, ctypes.c_void_p)

c_reset = fn('mcp_reset', None)
c_imports = fn('mcp_set_imports', None, TIMEFN, REALFN, VOIDFN, VOIDFN, POSFN)
c_cfg = fn('mcp_config_from_sdi', ctypes.c_int16, ctypes.c_char_p, ctypes.POINTER(McpConfig))
c_init_dev = fn('SDIInitDevice', ctypes.c_int16, ctypes.c_char_p)
c_start = fn('SDIStartOutput', ctypes.c_int16)
c_state = fn('mcp_channel_state', None, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32))
c_mixpos = fn('mcp_mix_position', ctypes.c_uint16)
c_bufsize = fn('mcp_buffer_size', ctypes.c_uint16)
C = {
    'setSample': fn('mcpSetSample', ctypes.c_int32, ctypes.c_int32, ctypes.POINTER(SampleInfo)),
    'playSample': fn('mcpPlaySample', ctypes.c_int32, ctypes.c_int32, ctypes.c_uint32, ctypes.c_uint32),
    'setVolume': fn('mcpSetVolume', ctypes.c_int32, ctypes.c_int32, ctypes.c_uint32),
    'setFrequency': fn('mcpSetFrequency', ctypes.c_int32, ctypes.c_int32, ctypes.c_uint32),
    'setPosition': fn('mcpSetPosition', ctypes.c_int32, ctypes.c_int32, ctypes.c_uint32),
    'setPanning': fn('mcpSetPanning', ctypes.c_int32, ctypes.c_int32, ctypes.c_int32),
    'setMasterVolume': fn('mcpSetMasterVolume', ctypes.c_int32, ctypes.c_uint32),
    'mute': fn('mcpMuteChannel', ctypes.c_int32, ctypes.c_int32),
    'unmute': fn('mcpUnmuteChannel', ctypes.c_int32, ctypes.c_int32),
    'stop': fn('mcpStopChannel', ctypes.c_int32, ctypes.c_int32),
    'muteAll': fn('mcpMuteAll', ctypes.c_int32),
    'unmuteAll': fn('mcpUnmuteAll', ctypes.c_int32),
    'getVolume': fn('mcpGetVolume', ctypes.c_int32, ctypes.c_int32),
    'getFrequency': fn('mcpGetFrequency', ctypes.c_int32, ctypes.c_int32),
    'getPosition': fn('mcpGetPosition', ctypes.c_int32, ctypes.c_int32),
    'getPanning': fn('mcpGetPanning', ctypes.c_int32, ctypes.c_int32),
    'getChannelStatus': fn('mcpGetChannelStatus', ctypes.c_int32, ctypes.c_int32),
    'setupChannels': fn('mcpSetupChannels', ctypes.c_int32, ctypes.c_int32),
    'getStatus': fn('mcpGetStatus', ctypes.c_int32, ctypes.c_int32),
    'poll': fn('mcpPoll', None),
}
DMA = (ctypes.c_uint8 * 0x10000).in_dll(lib, 'mcp_dma_buffer')
OFFHOOK = ctypes.c_void_p.in_dll(lib, 'mcp_far_offset_hook')
_default_offhook = OFFHOOK.value

GETTERS = ('getVolume', 'getFrequency', 'getPosition', 'getPanning', 'getChannelStatus')
IMMEDIATE = ('mute', 'unmute', 'stop')


def pur_samples():
    files = sorted(glob.glob(os.path.join(ROOT, 'extracted', 'SOUNDS', '*.PUR')))
    check(len(files) > 50, 'SOUNDS/*.PUR missing (run tools/bootstrap.sh)')
    return files


class Pair:
    """Original (Unicorn) and port side by side."""

    def __init__(self, cfg=CFG, far_off=0):
        self.e = DsdEmu(SDISB16)
        self.e.load()
        seg = self.e.alloc(0x60)
        self.e.wr(seg, 0, cfg)
        r = self.e.call_export('SDIInitDevice', self.e.far(seg, 0))
        assert r & 0xFFFF == 0, hex(r)
        self.D = self.e.data_seg
        self.time = 0
        self.dma = 0
        self.real = {}                       # handle -> (emu seg, off, c buffer)
        self._keep = []
        self.cb = (TIMEFN(lambda: self.time & 0xFFFFFFFF),
                   REALFN(self._c_real), VOIDFN(lambda: None), VOIDFN(lambda: None),
                   POSFN(lambda: self.dma & 0xFFFF))
        c_reset()
        c_imports(*self.cb)
        self.far_off = far_off
        self._offcb = OFFFN(lambda p: far_off)
        OFFHOOK.value = ctypes.cast(self._offcb, ctypes.c_void_p).value if far_off else _default_offhook
        # SDIInitDevice allocates the DMA ring and the table segment from the
        # far heap like the original (never freed): start each pair afresh
        lib.bc_farheap_reset()
        r = c_init_dev(cfg)
        assert r == 0
        self.e.real_address = lambda h, pos: self.real[h][:2]
        self.e.time = self.time
        self.e.call_export('SDIStartOutput')
        c_start()
        self.bufsize = self.e.rw(self.D, 0x8F7)
        self.ems = {}

    def _c_real(self, h, pos):
        return ctypes.addressof(self.real[h][2])

    def sample(self, data, loopstart=0, loopend=0, mode=None, pad=64, handle=0):
        """Place a sample on both sides; returns (emu SAMPLEINFO seg, c SampleInfo)."""
        rnd = random.Random(len(data))
        padb = bytes(rnd.randrange(256) for _ in range(pad))
        blob = bytes(data) + padb
        # memory around the sample reads as 0x80 on both sides (the port cannot
        # know what the original had in front of a far pointer with offset > 0)
        seg = self.e.alloc(len(blob) + 16 + self.far_off, fill=0x80)
        self.e.wr(seg, self.far_off, blob)
        cbuf = (ctypes.c_uint8 * len(blob)).from_buffer_copy(blob)
        self._keep.append(cbuf)
        if mode is None:
            mode = 1 if loopend else 0
        si_seg = self.e.alloc(0x20)
        ptr = 0 if handle else self.e.far(seg, self.far_off)
        self.e.wr(si_seg, 0, struct.pack('<IIIIBH', ptr if not handle else 0xFFFF0000 | handle,
                                         len(data), loopstart, loopend, mode, handle))
        si = SampleInfo()
        if handle:
            si.sample.ptr = None
            si.sample.ems_handle = handle
            si.sample.in_ems = 1
            self.real[handle] = (seg, self.far_off, cbuf)
        else:
            si.sample.ptr = ctypes.addressof(cbuf)
        si.length, si.loopstart, si.loopend, si.mode, si.sampleID = len(data), loopstart, loopend, mode, handle
        self._keep.append(si)
        return (si_seg, si)

    def cdi(self, slot, *args):
        """Same call on both sides; returns (orig DX:AX, port int32)."""
        self.e.time = self.time
        self.e.dma_pos = self.dma
        eargs = []
        cargs = []
        for a in args:
            if isinstance(a, tuple):             # sample
                eargs.append(self.e.far(a[0], 0))
                cargs.append(ctypes.byref(a[1]))
            else:
                eargs.append(a & 0xFFFFFFFF)
                cargs.append(a)
        ro = self.e.cdi(slot, *eargs) if slot != 'getStatus' else self.e.cdi(slot, *eargs)
        rc = C[slot](*cargs)
        return ro, rc

    def channel(self, ch):
        b = self.e.rd(self.D, 0xA65 + ch * 0x28, 0x28)
        flags, = struct.unpack_from('<H', b, 0)
        step, pos = struct.unpack_from('<II', b, 0x0A)
        si, sf, frac = struct.unpack_from('<HHH', b, 0x1F)
        return [flags, pos, frac, step, si, sf, b[0x1A], b[0x27]]

    def compare(self, tag, nch):
        ok = True
        o = self.e.rd(self.e.rw(self.D, 0xA04), self.e.rdw(self.D, 0xA06), self.bufsize)
        c = bytes(DMA[:self.bufsize])
        if o != c:
            i = next(k for k in range(len(o)) if o[k] != c[k])
            ok = check(False, f'{tag}: DMA ring differs at byte {i} '
                              f'(orig {o[i:i + 8].hex()} port {c[i:i + 8].hex()})')
        mo, mc = self.e.rw(self.D, 0xA23), c_mixpos()
        ok &= check(mo == mc, f'{tag}: mix position {mo} vs {mc}')
        for ch in range(nch):
            st = (ctypes.c_uint32 * 8)()
            c_state(ch, st)
            so, sc = self.channel(ch), list(st)
            if so != sc:
                ok = check(False, f'{tag}: channel {ch} orig {so} port {sc}')
                break
        return ok


def run_scenario(seed, cfg=CFG, nch=None, steps=60, far_off=0, long_samples=False, ems=False):
    rnd = random.Random(seed)
    p = Pair(cfg, far_off)
    files = pur_samples()
    nch = nch or rnd.randrange(1, 33)
    ro, rc = p.cdi('setupChannels', nch)
    samples = []
    for f in rnd.sample(files, 6):
        d = open(f, 'rb').read()[:rnd.choice((300, 4000) if long_samples else (300, 4000, 20000, 70000))]
        if rnd.random() < 0.4 and len(d) > 64:
            le = rnd.randrange(32, len(d) + 1)
            ls = rnd.randrange(0, le - 16)
            samples.append(p.sample(d, ls, le))
        else:
            samples.append(p.sample(d, mode=rnd.choice((0, 1))))
    if long_samples:                              # > 64 KB: 16-bit offset wrap
        d = bytes(rnd.randrange(256) for _ in range(90000))
        samples.append(p.sample(d))
        samples.append(p.sample(d[:80000], 1000, 79000))
    if ems:
        d = open(files[3], 'rb').read()[:30000]
        samples.append(p.sample(d, handle=7))
        samples.append(p.sample(d, 100, 20000, handle=9))
    rate = p.e.rw(p.D, 0xA19)
    bpf = p.e.rw(p.D, 0xA21)
    played = 0                                     # bytes the "DMA" has played
    for step in range(steps):
        # commands between two polls, at DSMI ticks (100 units per 10 ms)
        for _ in range(rnd.randrange(0, 12)):
            if rnd.random() < 0.3:
                p.time += 100
            ch = rnd.randrange(nch + (1 if rnd.random() < 0.05 else 0))
            k = rnd.random()
            if k < 0.25:
                p.cdi('setSample', ch, rnd.choice(samples))
                p.cdi('playSample', ch, rnd.choice((8363, 11025, 16000, 22050, 44100, 5000,
                                                    rnd.randrange(100, 90000))),
                      rnd.choice((64, 32, 0, 1, 63, rnd.randrange(256))))
            elif k < 0.35:
                p.cdi('setVolume', ch, rnd.choice((0, 64, 31, rnd.randrange(256))))
            elif k < 0.45:
                p.cdi('setFrequency', ch, rnd.randrange(1000, 60000))
            elif k < 0.52:
                p.cdi('setPosition', ch, rnd.randrange(0, 30000))
            elif k < 0.65:
                p.cdi('setPanning', ch, rnd.choice((0, -63, 63, 100, -100, 2, -2, 3, -3, 70,
                                                    rnd.randrange(-63, 64))))
            elif k < 0.75:
                slot = rnd.choice(IMMEDIATE)
                ro, rc = p.cdi(slot, ch)
                check(ro & 0xFFFF == rc & 0xFFFF, f'seed {seed} {slot}({ch}): {ro:#x} vs {rc:#x}')
            elif k < 0.78:
                p.cdi('setMasterVolume', rnd.choice((64, 32, 0, 65, rnd.randrange(65))))
            elif k < 0.80:
                p.cdi(rnd.choice(('muteAll', 'unmuteAll')))
            elif k < 0.82:
                p.cdi('setSample', ch, rnd.choice(samples))
            else:
                for g in GETTERS:
                    ro, rc = p.cdi(g, ch)
                    if g == 'getPanning':          # MOVSX EAX: DX is not set
                        ro, rc = ro & 0xFFFF, rc & 0xFFFF
                    if ch < nch:
                        check(ro == rc & 0xFFFFFFFF, f'seed {seed} {g}({ch}): {ro:#x} vs {rc & 0xFFFFFFFF:#x}')
        # time to the next poll: the 40 Hz routine on a 100 Hz clock -> 200..300,
        # sometimes late, sometimes too early (< 120: no mixing)
        dt = rnd.choice((200, 300, 200, 300, 100, 0, 500, 1000))
        p.time += dt
        played += rate * dt // 10000 * bpf + rnd.randrange(-8, 9)
        p.dma = max(played, 0) % p.bufsize
        if bpf > 1:
            p.dma &= ~1
        p.cdi('poll')
        if not p.compare(f'seed {seed} step {step}', nch):
            return False
    return True


def test_config_from_sdi():
    """initDevice's configuration (rate, stereo, size, options, ring size) for
    every card type / option combination the setup program offers."""
    bad = 0
    for typ in (1, 2, 6):
        for amp in range(5):
            for o44 in range(2 if typ != 1 else 3):
                for o48 in range(4 if typ == 6 else 3):
                    cfg = bytearray(CFG)
                    struct.pack_into('<I', cfg, 0, typ)
                    struct.pack_into('<III', cfg, 0x40, amp, o44, o48)
                    e = DsdEmu(SDISB16)
                    e.load()
                    seg = e.alloc(0x60)
                    e.wr(seg, 0, cfg)
                    e.call_export('SDIInitDevice', e.far(seg, 0))
                    D = e.data_seg
                    orig = (e.rw(D, 0x870), e.rd(D, 0x87A, 1)[0], e.rd(D, 0x87B, 1)[0],
                            e.rw(D, 0xA19), e.rw(D, 0x8F7), e.rw(D, 0xA00), e.rw(D, 0xA02))
                    c = McpConfig()
                    c_cfg(bytes(cfg), ctypes.byref(c))
                    c_reset()
                    port = (c.options, c.stereo, c.sample_size, c.rate, c.buffer_size)
                    if not check(orig[:5] == port, f'type {typ} amp {amp} {o44} {o48}: {orig} vs {port}'):
                        bad += 1
                        if bad > 5:
                            return


def test_ses_cfg_mixing():
    """The shipped SES.CFG: SB16 22050 Hz stereo 16-bit, no amplification."""
    for seed in range(6):
        run_scenario(seed)


def test_few_channels_loud():
    for seed in range(100, 104):
        run_scenario(seed, nch=rnd_n(seed, 1, 4))


def rnd_n(seed, a, b):
    return random.Random(seed).randrange(a, b + 1)


def _cfg(typ, amp, o44, o48):
    cfg = bytearray(CFG)
    struct.pack_into('<I', cfg, 0, typ)
    struct.pack_into('<III', cfg, 0x40, amp, o44, o48)
    return bytes(cfg)


def test_other_outputs():
    """SB16 mono / 44.1 kHz, amplified 16-bit accumulation, 8-bit SB Pro / SB."""
    cases = [(6, 0, 1, 2), (6, 3, 0, 0), (6, 1, 0, 3), (6, 4, 1, 1), (6, 2, 0, 2),
             (2, 0, 0, 0), (2, 1, 1, 2), (2, 4, 0, 1), (1, 3, 0, 1), (1, 0, 2, 0)]
    for i, c in enumerate(cases):
        run_scenario(200 + i, _cfg(*c), steps=30)


def test_long_samples_offset_wrap():
    """Samples over 64 KB: the mixer's 16-bit source offset wraps inside a block."""
    run_scenario(300, steps=80, long_samples=True, nch=2)
    run_scenario(301, steps=80, long_samples=True, far_off=4, nch=3)


def test_virtual_samples():
    """sampleID != 0: MCPSampleRealAddress is asked for the data every block."""
    run_scenario(400, steps=40, ems=True, nch=4)


def directed(cfg, nch, setup, polls, poll_dt=300, far_off=0, tag='directed'):
    """Fixed command list, then `polls` regular polls with the DMA in step."""
    p = Pair(cfg, far_off)
    p.cdi('setupChannels', nch)
    setup(p)
    rate, bpf = p.e.rw(p.D, 0xA19), p.e.rw(p.D, 0xA21)
    played = 0
    for i in range(polls):
        p.time += poll_dt
        played += rate * poll_dt // 10000 * bpf
        p.dma = played % p.bufsize & ~1
        p.cdi('poll')
        if not p.compare(f'{tag} poll {i}', nch):
            return False
    return True


def test_directed_long_sample_crossing_64k():
    """A 100 KB sample played through: the source pointer is normalised once
    its offset passes FFFFh, and the block that crosses it wraps inside the
    segment first (it re-reads 64 KB earlier data, as the original)."""
    data = bytes(random.Random(7).randrange(256) for _ in range(100000))
    for off, freq in ((0, 44100), (4, 37000), (0, 22050)):
        def setup(p, freq=freq):
            s = p.sample(data)
            p.cdi('setSample', 0, s)
            p.cdi('playSample', 0, freq, 64)
            p.cdi('setPanning', 0, 20)
        directed(CFG, 1, setup, 90, far_off=off, tag=f'64k off {off} freq {freq}')


def test_directed_accumulate_modes():
    """16-bit accumulation mono/stereo (clipping) incl. surround, all pans."""
    d = open(pur_samples()[10], 'rb').read()[:6000]
    pans = (0, -63, 63, 100, 2, -40, 40)
    for c in ((6, 4, 1, 0), (6, 4, 0, 0), (6, 1, 0, 1), (2, 3, 0, 0), (1, 4, 0, 0)):
        def setup(p):
            s = p.sample(d, 500, 5900)
            for ch, pan in enumerate(pans):
                p.cdi('setSample', ch, s)
                p.cdi('playSample', ch, 9000 + 3000 * ch, 64 - 3 * ch)
                p.cdi('setPanning', ch, pan)
                p.time += 37
        directed(_cfg(*c), len(pans), setup, 40, tag=f'acc {c}')


def test_native_driver_entry():
    """sdisb16_entry speaks the DDS driver protocol: imports, SDI* exports."""
    fn('DDSInit', ctypes.c_int16)()
    fn('dsmiTimeInit', ctypes.c_int16, ctypes.c_int32)(100)
    noop = VOIDFN(lambda: None)
    reg = fn('DDSRegisterFunction', ctypes.c_int16, ctypes.c_char_p, ctypes.c_void_p)
    acc = fn('DDSAccessFunction', ctypes.c_int16, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p))
    entry = fn('sdisb16_entry', ctypes.c_uint32, ctypes.c_int32, ctypes.c_void_p, ctypes.c_int32)
    access_ptr = ctypes.cast(lib.DDSAccessFunction, ctypes.c_void_p).value
    c_reset()
    check(entry(1, access_ptr, 0) == 5, 'entry(1) without MCPDisableVirtualSamples must fail with 5')
    reg(b'MCPDisableVirtualSamples', ctypes.cast(noop, ctypes.c_void_p).value)
    r = entry(1, access_ptr, 0)
    check(r == 0x640000, f'entry(1) -> {r:#x}')
    out = ctypes.c_void_p()
    for n in (b'SDIInitDevice', b'SDIStartOutput', b'SDIGetBufferPosition', b'SDIStopOutput',
              b'SDICloseDevice'):
        check(acc(n, ctypes.byref(out)) == 0, f'{n} not registered')
    check(entry(2, None, 0) == 0, 'entry(2)')
    check(acc(b'SDIInitDevice', ctypes.byref(out)) == 9, 'SDIInitDevice still registered after unload')
    c_reset()
