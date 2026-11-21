#!/usr/bin/env python3
"""
capture_boot.py - ground truth for the port's DOS loader/runtime.

Runs IST.EXE in DOSBox-X (tools/dosbox/ist.conf) and captures, at the EXE
entry point and at main() (0546:0001):
  * all registers, 1 MB + HMA of memory, the IVT/BDA (inside the dump),
  * DOS MCB chain and EMS state (debugger text output).
Output: build/oracle/<stage>_{mem.bin,regs.json,dos.txt}

usage: capture_boot.py
"""
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oracle import Oracle, ROOT, _case  # noqa: E402

LOAD = 0x0824
OUT = os.path.join(ROOT, 'build', 'oracle')
REGS = ['EAX', 'EBX', 'ECX', 'EDX', 'ESI', 'EDI', 'EBP', 'ESP', 'CS', 'DS', 'ES', 'SS', 'FS', 'GS',
        'EIP', 'EFLAGS']


def capture(o, stage):
    r = o.regs(*REGS)
    json.dump({k: f'{v:X}' for k, v in r.items()}, open(os.path.join(OUT, f'{stage}_regs.json'), 'w'),
              indent=1)
    path = os.path.join(OUT, f'{stage}_mem.bin')
    # MEMDUMPBIN addresses seg:off and the OFFSET wraps at 64 KB, so a single
    # 0000:0000 dump of 1 MB repeats the first segment: dump 64 KB per segment
    mem = bytearray()
    for seg in range(0, 0x11000, 0x1000):
        part = os.path.join(OUT, f'{stage}_part.bin')
        mem += o.dump(seg, 0, 0x10000, part)
        if os.path.exists(_case(part)):
            os.remove(_case(part))
    open(path, 'wb').write(mem)
    txt = []
    for cmd in ('DOS MCBS', 'DOS EMS', 'DOS XMS', 'DOS SDA', 'VGA DAC'):
        try:
            txt.append(f'### {cmd}\n' + o.x(cmd))
        except Exception as e:      # noqa: BLE001
            txt.append(f'### {cmd}\nERROR {e}')
    open(os.path.join(OUT, f'{stage}_dos.txt'), 'w', encoding='utf-8').write('\n'.join(txt))
    print(stage, {k: f'{v:X}' for k, v in r.items()})


def main():
    os.makedirs(OUT, exist_ok=True)
    with Oracle() as o:
        o.pause()
        o.x(f'BP {LOAD:04X}:0000')
        o.x(f'BP {0x0546 + LOAD:04X}:0001')
        # the same address is also the entry of DOSBox-X's MOUNT.COM etc. (same
        # PSP); skip stops until CS is the EXE's own load segment
        while True:
            o.run()
            r = o.wait_at(LOAD, 0x0000)
            if r['CS'] == LOAD:
                break
        capture(o, 'entry')
        o.run()
        o.wait_at(0x0546 + LOAD, 0x0001)
        capture(o, 'main')


if __name__ == '__main__':
    main()
