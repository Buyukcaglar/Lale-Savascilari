#!/usr/bin/env python3
"""
dbx_replay.py - run an input script (same file as `lale --script`) on the
original IST.EXE in DOSBox-X and write the same checkpoint dumps.

    python -u tools/verify/dbx_replay.py <script.txt> --out <dir> [--game-dir <dir>]
                                         [--timeout <s>] [--ist-args n]

How (docs/VERIFY.md): at main() right after srand() (0546:0292) the seed is
overwritten with the script's seed, and tools/verify/hook.S is written into
a free UMB (MCB CC00h, marked as owned by DOS) and hooked to INT 16h/21h/33h.
The hook applies due events at every input read of the game - the same rule
as the port - so the debugger link (50 ms per command) is only used at
dump/quit markers.
"""
import argparse
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'dosbox'))
sys.path.insert(0, HERE)
from oracle import Oracle, _case  # noqa: E402
import build_hook  # noqa: E402

LOAD = 0x0824
DGSEG = 0x16E0 + LOAD
MAIN_SEG = 0x0546 + LOAD
SRAND_RET = 0x0292                 # 0546:028D CALLF srand; next instruction
HOOK_SEG = 0xD800
# The 1996 CD build (--cd): its DGROUP starts 3 paragraphs later (16E3) and
# the variables the hook uses sit 0C0h further in (tools/exediff.py --data:
# RTL random state 94C4 -> 9584, NKONTROL's ms clock E74A -> E80A).  The hook
# addresses them through its own segment value, so for that build it gets
# DGROUP + 0Ch: the same offsets then reach the 1996 addresses.
DGSEG_CD = 0x16E3 + LOAD
DG_HOOK_CD = DGSEG_CD + 0x0C
UMB_MCB = 0xCC00
EV_BASE = 0x1000


def parse_script(path):
    """-> (events [(t, type, a, b, c, name)], seed) - mirrors script_load() in sdl3_main.c."""
    evs, seed = [], -1
    for ln, line in enumerate(open(path, encoding='utf-8'), 1):
        line = line.split('#', 1)[0]
        w = line.split()
        if not w:
            continue
        if w[0] == 'seed':
            seed = int(w[1], 0)
            continue
        if w[0] == 'dumpevery':
            a, b, st, pre = int(w[1], 0), int(w[2], 0), int(w[3], 0), w[4]
            t = a
            while st and t <= b:
                evs.append((t, 3, 0, 0, 0, f'{pre}_{t:07d}'))
                t += st
            continue
        # +n: n counted reads after the last event; @n: n ms of the game clock after it
        rel = 0x80 if w[0].startswith('+') else 0x40 if w[0].startswith('@') else 0
        t = int(w[0].lstrip('+@'), 0)
        k = w[1]
        if k == 'key':
            evs.append((t, 1 | rel, int(w[2], 16), 0, 0, ''))
        elif k == 'mouse':
            evs.append((t, 2 | rel, int(w[2]), int(w[3]), int(w[4]), ''))
        elif k == 'buttons':
            evs.append((t, 4 | rel, int(w[2]), 0, 0, ''))
        elif k == 'dump':
            evs.append((t, 3 | rel, 0, 0, 0, w[2]))
        elif k == 'quit':
            evs.append((t, 3 | rel, 1, 0, 0, 'quit'))
        elif k == 'sync':                               # anchor: also DGROUP word a == b
            evs.append((t, 5 | rel, int(w[2], 16), int(w[3], 16), 0, ''))
        elif k == 'srand':                              # random state DGROUP:94C4 := v
            v = int(w[2], 16)
            evs.append((t, 7 | rel, v & 0xFFFF, v >> 16, 0, ''))
        elif k == 'poke':                               # DGROUP bytes: split into words
            addr, data = int(w[2], 16), bytes(int(x, 16) for x in w[3:])
            for i in range(0, len(data), 2):
                part = data[i:i + 2]
                v = part[0] | (part[1] << 8 if len(part) > 1 else 0)
                evs.append((t if i == 0 else 0, 8 | rel, addr + i, v, len(part), ''))
        elif k == 'syncne':                             # anchor: DGROUP word a != b
            evs.append((t, 6 | rel, int(w[2], 16), int(w[3], 16), 0, ''))
        elif k == 'kbd':                                # scan-code bytes through the 8042
            for i, x in enumerate(w[2:]):
                evs.append((t if i == 0 else 0, 9 | rel, int(x, 16), 0, 0, ''))
        else:
            raise ValueError(f'{path}:{ln}: cannot parse')
    return evs, seed


class Replay:
    def __init__(self, o, out):
        self.o, self.out = o, out
        self.tmp = os.path.join(out, '_tmp.bin')

    def sm(self, seg, off, data, chunk=48):
        for i in range(0, len(data), chunk):
            part = data[i:i + chunk]
            # leading 0: the debugger reads tokens like CF/DF/AF as flag names
            self.o.x(f'SM {seg:04X}:{off + i:04X} ' + ' '.join(f'0{b:02X}' for b in part))

    def rd(self, seg, off, n):
        return self.o.dump(seg, off, n, self.tmp)

    def install(self, evs, seed, dg_hook=DGSEG):
        o = self.o
        if seed >= 0:
            self.sm(dg_hook, 0x94C4, struct.pack('<HH', seed & 0xFFFF, 0))
        mcb = self.rd(UMB_MCB, 0, 16)
        size = struct.unpack_from('<H', mcb, 3)[0] * 16
        owner = struct.unpack_from('<H', mcb, 1)[0]
        if mcb[0] not in (0x4D, 0x5A) or owner != 0 or UMB_MCB + 1 + size // 16 < HOOK_SEG + 0x800:
            raise RuntimeError(f'UMB MCB {UMB_MCB:04X} not free/large enough: {mcb.hex()}')
        self.sm(UMB_MCB, 1, struct.pack('<H', 8))            # owned by DOS: nobody allocates it
        img = bytearray(open(build_hook.build(), 'rb').read())
        ivt = self.rd(0, 0, 0x100)
        o16, o21, o33 = (struct.unpack_from('<I', ivt, n * 4)[0] for n in (0x16, 0x21, 0x33))
        struct.pack_into('<III', img, 0x08, o16, o21, o33)
        struct.pack_into('<HHH', img, 0x14, 0, len(evs), dg_hook)
        e16, e21, e33, self.marker = struct.unpack_from('<4H', img, 0x30)
        table = b''.join(struct.pack('<IHHHH', t, ty, a & 0xFFFF, b & 0xFFFF, c & 0xFFFF)
                         for t, ty, a, b, c, _ in evs)
        if EV_BASE + len(table) > 0x8000:
            raise RuntimeError('too many events')
        # only the non-zero part of the image (code + header) is written
        end = max(i for i in range(EV_BASE) if img[i]) + 1
        self.sm(HOOK_SEG, 0, bytes(img[:end]))
        self.sm(HOOK_SEG, EV_BASE, table)
        chk = self.rd(HOOK_SEG, 0, end)
        if chk != bytes(img[:end]):
            bad = [i for i in range(end) if chk[i] != img[i]]
            print('readback differs at', len(bad), [hex(i) for i in bad[:20]], chk[:64].hex())
            raise RuntimeError('hook image readback differs')
        for n, off in ((0x16, e16), (0x21, e21), (0x33, e33)):
            self.sm(0, n * 4, struct.pack('<HH', off, HOOK_SEG))
        o.x(f'BP {HOOK_SEG:04X}:{self.marker:04X}')

    def dump_video(self, name, vga):
        seq4 = vga[1 + 4]
        p = os.path.join(self.out, name)
        if seq4 & 0x08:
            vram = self.rd(0xA000, 0, 0x10000)
        else:
            o = self.o
            gc = vga[6:15]
            si, gi = o._inp(0x3C4), o._inp(0x3CE)
            o._outp(0x3C4, 4); o._outp(0x3C5, (seq4 & ~0x08) | 0x04)
            o._outp(0x3CE, 5); o._outp(0x3CF, gc[5] & ~0x18)
            o._outp(0x3CE, 6); o._outp(0x3CF, (gc[6] & ~0x0E) | 0x04)
            vram = b''
            for pl in range(4):
                o._outp(0x3CE, 4); o._outp(0x3CF, pl)
                vram += self.rd(0xA000, 0, 0x10000)
            o._outp(0x3C4, 4); o._outp(0x3C5, seq4)
            for i in (4, 5, 6):
                o._outp(0x3CE, i); o._outp(0x3CF, gc[i])
            o._outp(0x3C4, si); o._outp(0x3CE, gi)
        open(p + '.vram', 'wb').write(vram)

    def marker_hit(self, evs):
        hdr = self.rd(HOOK_SEG, 0, 0x100 + 829)
        idx = struct.unpack_from('<H', hdr, 0x22)[0]
        clock = struct.unpack_from('<I', hdr, 0x24)[0]
        reads = struct.unpack_from('<I', hdr, 0x2C)[0]
        if idx == 0xFFFE:                               # INT 23h issued by DOS (hook int23)
            t, ty, a, b, c, name = 0, 3, 0, 0, 0, 'int23'
        else:
            t, ty, a, b, c, name = evs[idx]
        if a == 1:
            print(f'quit at clock {clock}', flush=True)
            return False
        vga = hdr[0x100:0x100 + 829]
        p = os.path.join(self.out, name)
        open(p + '.vga', 'wb').write(vga)
        self.dump_video(name, vga)
        open(p + '.dg', 'wb').write(self.rd(getattr(self, 'dgseg', DGSEG), 0, 0x10000))
        open(p + '.txt', 'w').write(f'clock {clock}\nevent {idx}\nscript_time {t}\nreads {reads}\n')
        print(f'{name}: clock {clock} reads {reads}', flush=True)
        return name != 'int23'                          # the program ends in its handler


def make_conf(game_dir, out, cd=None):
    base = open(os.path.join(ROOT, 'tools', 'dosbox', 'ist.conf'), encoding='utf-8').read()
    mount = f'mount c "{os.path.abspath(game_dir)}"'
    if cd:                       # the CD edition: the disc as D:, DOSBox-X's own MSCDEX
        mount += f'\nimgmount d "{os.path.abspath(cd)}" -t iso'
    base = base.replace('mount c "orig"', mount)
    path = os.path.join(out, '_ist.conf')
    open(path, 'w', encoding='utf-8').write(base)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('script')
    ap.add_argument('--out', required=True)
    ap.add_argument('--game-dir', default=os.path.join(ROOT, 'out', 'verify', 'game_dbx'))
    ap.add_argument('--timeout', type=float, default=1800)
    ap.add_argument('--seed', type=int)
    ap.add_argument('--port', type=int, default=7777)
    ap.add_argument('--cd', metavar='CUE', help='mount this disc image as D: (the 1996 CD edition)')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    evs, seed = parse_script(args.script)
    if args.seed is not None:
        seed = args.seed
    conf = make_conf(args.game_dir, args.out, args.cd)
    t0 = time.time()
    with Oracle(port=args.port, conf=conf) as o:
        o.load_seg = LOAD
        o.pause()
        o.x(f'BP {MAIN_SEG:04X}:{SRAND_RET:04X}')
        o.run()
        o.wait_at(MAIN_SEG, SRAND_RET, timeout=120)
        o.x('BPDEL *')
        r = Replay(o, args.out)
        r.dgseg = DGSEG_CD if args.cd else DGSEG
        r.install(evs, seed, DG_HOOK_CD if args.cd else DGSEG)
        print(f'hook installed, {len(evs)} events, seed {seed}', flush=True)
        while time.time() - t0 < args.timeout:
            o.run()
            o.wait_at(HOOK_SEG, r.marker, timeout=args.timeout - (time.time() - t0), poll=0.2)
            if not r.marker_hit(evs):
                break
        for f in ('_tmp.bin',):
            p = _case(os.path.join(args.out, f))
            if os.path.exists(p):
                os.remove(p)


if __name__ == '__main__':
    main()
