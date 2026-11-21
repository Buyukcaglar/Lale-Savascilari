#!/usr/bin/env python3
"""
frames_match.py - timing-independent picture comparison.

    python tools/verify/frames_match.py <port_frame_log_dir> <dbx_dump_dir> [--png <dir>]

The port's --frame-log holds every distinct video state it had at an input
read (one check per 10 ms tick).  Each sample of the original
(dbx_replay.py dumps) is rendered and looked up in that set:
  match       picture + DAC identical to some port state
  pix-only    picture identical, DAC differs (fade level / palette)
  MISSING     no port state with this picture: a divergence; the nearest port
              state (fewest differing pixels) is reported
Samples are processed in clock order; the first MISSING one is the first
divergence.
"""
import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vcompare as V  # noqa: E402


def frames(d):
    return sorted(f[:-4] for f in os.listdir(d) if f.endswith('.vga'))


def key(w, h, img, dac=b''):
    return hashlib.sha1(bytes(str((w, h)), 'ascii') + img + bytes(dac)).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('port')
    ap.add_argument('orig')
    ap.add_argument('--png')
    ap.add_argument('--max-report', type=int, default=20)
    a = ap.parse_args()
    full, pix = {}, {}
    port = []
    for n in frames(a.port):
        w, h, img, dac = V.render(V.load(a.port, n))
        full.setdefault(key(w, h, img, dac), n)
        pix.setdefault(key(w, h, img), n)
        port.append((n, w, h, img, dac))
    print(f'port: {len(port)} distinct states')
    first = None
    nmiss = 0
    for n in frames(a.orig):
        d = V.load(a.orig, n)
        w, h, img, dac = V.render(d)
        clock = d['txt'].get('clock')
        if key(w, h, img, dac) in full:
            print(f'{n} (clock {clock}): match {full[key(w, h, img, dac)]}')
            continue
        if key(w, h, img) in pix:
            pn = pix[key(w, h, img)]
            pd = V.regs(V.load(a.port, pn)['vga'])['dac']
            bad = [i // 3 for i in range(0, 768, 3) if pd[i:i + 3] != bytes(dac[i:i + 3])]
            print(f'{n} (clock {clock}): pix-only {pn}, DAC differs in {len(bad)} entries {bad[:6]}')
            continue
        nmiss += 1
        best = None
        for pn, pw, ph, pimg, pdac in port:
            if (pw, ph) != (w, h):
                continue
            nd = sum(1 for x, y in zip(img, pimg) if x != y)
            if best is None or nd < best[0]:
                best = (nd, pn, pimg, pdac)
        if best:
            nd, pn, pimg, pdac = best
            px = [k for k in range(len(img)) if img[k] != pimg[k]]
            box = (min(k % w for k in px), min(k // w for k in px), max(k % w for k in px),
                   max(k // w for k in px))
            print(f'{n} (clock {clock}): MISSING; nearest {pn}: {nd} pixels differ, box {box}')
            if a.png and nmiss <= a.max_report:
                os.makedirs(a.png, exist_ok=True)
                V.write_png(os.path.join(a.png, f'{n}_orig.png'), w, h, img, dac)
                V.write_png(os.path.join(a.png, f'{n}_nearest_{pn}.png'), w, h, pimg, pdac)
        else:
            print(f'{n} (clock {clock}): MISSING; no port state of size {w}x{h}')
        if first is None:
            first = n
    print(f'first divergence: {first}; {nmiss} samples without a port state')


if __name__ == '__main__':
    main()
