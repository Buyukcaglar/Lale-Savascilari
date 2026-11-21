#!/usr/bin/env python3
"""
capture_startup.py - ground truth for ist_main's start-up.

Runs IST.EXE (no arguments) in DOSBox-X (tools/dosbox/ist.conf) and stops at
the first call from main() into the intro/menu module: the CALLF 1682:002A
(demo_intro_menu) at 0546:057B, i.e. after the whole start-up (file system,
overlay EMS, DSMI timer, sound, MEMMAN, archives, fonts, FRPOS flags, 3D
tables, UI pictures, mouse, ogr_init).  Captures there:
  * registers, 1 MB + HMA of memory (DGROUP, far heap, IVT inside),
  * DOS MCB chain, EMS handles/pages (debugger text),
  * VGA: planes, DAC, registers (oracle.dump_vga),
  * the EMS pages of every handle are not readable through the debugger;
    the handle table text is what tests/py/test_istanbul.py compares.
Output: build/oracle/menu_{mem.bin,regs.json,dos.txt,vga_*}

usage: python -u tools/dosbox/capture_startup.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oracle import Oracle, ROOT  # noqa: E402
from capture_boot import capture, LOAD, OUT  # noqa: E402

CALL_MENU = (0x0546, 0x057B)          # CALLF 1682:002A -> demo_intro_menu


def main():
    os.makedirs(OUT, exist_ok=True)
    with Oracle() as o:
        o.pause()
        # main() first: the same runtime address may be reached by
        # DOSBox-X's own programs before IST.EXE is loaded
        o.x(f'BP {0x0546 + LOAD:04X}:0001')
        while True:
            o.run()
            r = o.wait_at(0x0546 + LOAD, 0x0001, timeout=180)
            if r['CS'] == 0x0546 + LOAD:
                break
        o.x(f'BPDEL *')
        o.x(f'BP {CALL_MENU[0] + LOAD:04X}:{CALL_MENU[1]:04X}')
        o.run()
        o.wait_at(CALL_MENU[0] + LOAD, CALL_MENU[1], timeout=600)
        capture(o, 'menu')
        planes, dac, st = o.dump_vga(os.path.join(OUT, 'menu_vga'))
        json.dump({'load_seg': LOAD}, open(os.path.join(OUT, 'menu_info.json'), 'w'))
        print('captured at', CALL_MENU, 'DAC[0:6]', dac[:6].hex())


if __name__ == '__main__':
    main()
