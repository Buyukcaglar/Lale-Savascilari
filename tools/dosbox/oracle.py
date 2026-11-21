#!/usr/bin/env python3
"""
oracle.py - run the original IST.EXE in DOSBox-X and capture ground truth.

    from oracle import Oracle
    with Oracle() as o:                 # launches DOSBox-X with the debugger link
        o.wait_for_game()               # until IST.EXE's DGROUP is live
        o.bp_flat(0x0546, 0x0010)       # breakpoint at a flat (analysis) address
        o.run(); o.wait_break()
        o.dump_dgroup('build/capture/dg.bin')
        o.dump_vga('build/capture/frame')   # 4 raw planes + DAC + registers

Address spaces: "flat" = tools/exeload.py image (overlays at 2700+);
runtime = flat + LOAD_SEG for resident code/DGROUP (LOAD_SEG = 0x0824 with
tools/dosbox/ist.conf).  Overlay code moves at run time: resolve it through
the overlay manager's stub (JMP FAR patched in place) with resolve_overlay().
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dbxdebug import Debugger  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOSBOX = r'C:\DOSBox-X\dosbox-x.exe'
DGROUP_FLAT = 0x16E0


class Oracle:
    def __init__(self, port=7777, conf=None, extra_conf=''):
        self.port = port
        self.conf = conf or os.path.join(ROOT, 'tools', 'dosbox', 'ist.conf')
        self.extra = extra_conf
        self.proc = None
        self.dbg = None
        self.load_seg = None

    # ---- lifecycle --------------------------------------------------------
    def __enter__(self):
        base = open(self.conf, encoding='utf-8').read()
        base = base.replace('[dosbox]\n', f'[dosbox]\nmcp_server = {self.port}\n', 1)
        fd, self.tmpconf = tempfile.mkstemp(suffix='.conf', dir=os.path.join(ROOT, 'build'))
        with os.fdopen(fd, 'w', encoding='utf-8') as f:
            f.write(base + '\n' + self.extra)
        self.dbg = Debugger(self.port, accept_timeout=60, io_timeout=60).__enter__()
        self.proc = subprocess.Popen([DOSBOX, '-conf', self.tmpconf, '-nopromptfolder'],
                                     cwd=ROOT)
        self.dbg.wait_connect()
        return self

    def __exit__(self, *exc):
        try:
            self.proc and self.proc.kill()
        finally:
            self.dbg and self.dbg.__exit__(*exc)
            try:
                os.unlink(self.tmpconf)
            except OSError:
                pass

    def x(self, cmd):
        return self.dbg.exec(cmd)

    def run(self):
        self.dbg.run()

    def pause(self):
        self.dbg.brk()

    def regs(self, *names):
        return self.dbg.regs(*names)

    # ---- addresses --------------------------------------------------------
    def wait_for_game(self, timeout=60.0):
        """Pause periodically until DS points at IST.EXE's DGROUP (copyright
        string at DS:0004).  Leaves the emulator paused; sets load_seg."""
        t0 = time.time()
        while time.time() - t0 < timeout:
            time.sleep(1.0)
            self.pause()
            ds = self.regs('DS')['DS']
            path = os.path.join(ROOT, 'build', 'capture', '_probe.bin')
            self.x(f'MEMDUMPBIN {ds:04X}:0004 10 {path}')
            if open(_case(path), 'rb').read().startswith(b'Borland C++'):
                self.load_seg = ds - DGROUP_FLAT
                return self.load_seg
            self.run()
        raise TimeoutError('IST.EXE DGROUP not seen')

    def rt(self, seg, off):
        """flat seg:off -> runtime 'SSSS:OOOO' (resident code / DGROUP only)."""
        return f'{seg + self.load_seg:04X}:{off:04X}'

    def bp_flat(self, seg, off):
        return self.x(f'BP {self.rt(seg, off)}')

    def wait_at(self, cs, ip, timeout=120.0, poll=0.25):
        """Wait until the CPU stops at exactly CS:IP (a breakpoint there)."""
        t0 = time.time()
        while time.time() - t0 < timeout:
            r = self.regs('CS', 'IP')
            if r['CS'] * 16 + r['IP'] == cs * 16 + ip:   # DOSBox-X may report PSP:0100
                return r
            time.sleep(poll)
        raise TimeoutError(f'not stopped at {cs:04X}:{ip:04X}')

    def wait_break(self, timeout=60.0, poll=0.25):
        """Wait until a breakpoint stops the CPU (CS:IP stops changing)."""
        t0 = time.time()
        last = None
        while time.time() - t0 < timeout:
            r = self.regs('CS', 'IP')
            if r == last:
                return r
            last = r
            time.sleep(poll)
        raise TimeoutError('no breakpoint hit')

    # ---- dumps ------------------------------------------------------------
    def dump(self, seg, off, length, path):
        self.x(f'MEMDUMPBIN {seg:04X}:{off:04X} {length:X} {path}')
        return open(_case(path), 'rb').read()

    def dump_dgroup(self, path):
        return self.dump(DGROUP_FLAT + self.load_seg, 0, 0x10000, path)

    def _inp(self, port):
        out = self.x(f'INP {port:X}')          # reply: "Result: 1f" (hex, no padding)
        m = re.search(r'Result:\s*([0-9a-fA-F]+)', out)
        if not m:
            raise RuntimeError(f'INP {port:X}: unexpected reply {out!r}')
        return int(m.group(1), 16)

    def _outp(self, port, val):
        self.x(f'OUTP {port:X} {val:X}')

    def _idx_read(self, iport, index):
        self._outp(iport, index)
        return self._inp(iport + 1)

    def dump_vga(self, prefix):
        """Raw VGA state: 4 planes (64 KB each), DAC, sequencer/GC/CRTC regs.
        Registers touched for the dump are restored afterwards."""
        seq_i, gc_i, crtc_i = self._inp(0x3C4), self._inp(0x3CE), self._inp(0x3D4)
        seq = [self._idx_read(0x3C4, i) for i in range(5)]
        gc = [self._idx_read(0x3CE, i) for i in range(9)]
        crtc = [self._idx_read(0x3D4, i) for i in range(25)]
        misc = self._inp(0x3CC)
        # attribute controller: 3DAh read resets the flip-flop, index written
        # with bit 5 set (screen stays on), data read from 3C1h
        attr_i = self._inp(0x3C0)
        attr = []
        for i in range(21):
            self._inp(0x3DA)
            self._outp(0x3C0, i | 0x20)
            attr.append(self._inp(0x3C1))
        self._inp(0x3DA)
        self._outp(0x3C0, attr_i)              # index restored; flip-flop now 'data'
        self._inp(0x3DA)                       # ... and back to 'index'
        # planar, read mode 0, A000 64K window
        self._outp(0x3C4, 4); self._outp(0x3C5, (seq[4] & ~0x08) | 0x04)
        self._outp(0x3CE, 5); self._outp(0x3CF, gc[5] & ~0x18)
        self._outp(0x3CE, 6); self._outp(0x3CF, (gc[6] & ~0x0E) | 0x04)
        planes = []
        for p in range(4):
            self._outp(0x3CE, 4); self._outp(0x3CF, p)
            planes.append(self.dump(0xA000, 0, 0x10000, f'{prefix}_plane{p}.bin'))
        # restore
        for i, val in ((4, seq[4]),):
            self._outp(0x3C4, i); self._outp(0x3C5, val)
        for i in (4, 5, 6):
            self._outp(0x3CE, i); self._outp(0x3CF, gc[i])
        self._outp(0x3C4, seq_i); self._outp(0x3CE, gc_i); self._outp(0x3D4, crtc_i)
        # DAC (read index is restored to its previous value)
        dac_info = self.x('VGA DAC')
        m = re.search(r'read_index=([0-9a-fA-F]+)h', dac_info)
        self._outp(0x3C7, 0)
        dac = bytes(self._inp(0x3C9) for _ in range(768))
        if m:
            self._outp(0x3C7, int(m.group(1), 16))
        pel = self._inp(0x3C6)
        state = dict(misc=misc, seq=seq, gc=gc, crtc=crtc, attr=attr, pel_mask=pel, dac_info=dac_info)
        open(f'{prefix}_dac.bin', 'wb').write(dac)
        json.dump(state, open(f'{prefix}_regs.json', 'w'), indent=1)
        return planes, dac, state


def _case(path):
    """DOSBox-X upper-cases the file name of MEMDUMPBIN targets."""
    if os.path.exists(path):
        return path
    d, b = os.path.split(path)
    up = os.path.join(d, b.upper())
    return up if os.path.exists(up) else path


if __name__ == '__main__':
    with Oracle() as o:
        seg = o.wait_for_game()
        print('IST.EXE load segment', hex(seg))
        o.dump_dgroup(os.path.join(ROOT, 'build', 'capture', 'dgroup_boot.bin'))
        planes, dac, st = o.dump_vga(os.path.join(ROOT, 'build', 'capture', 'boot'))
        print('crtc', [hex(x) for x in st['crtc']])
        print('seq', [hex(x) for x in st['seq']], 'gc', [hex(x) for x in st['gc']])
