#!/usr/bin/env python3
"""
mksave.py - build a prepared SAKLAnnn.SAV for a verification script
(docs/VERIFY.md, play5; format: docs/FORMATS.md "SAKLAnnn.SAV").

    python tools/verify/mksave.py play5 out/verify/scripts/play5.sav

The base is a save the original IST.EXE wrote itself in DOSBox-X:
tools/verify/saves/play4_SAKLA002.SAV (the save of play4: district 1,
(12,16), 107 kIvrIk, party ali/ayse/rifki/zeki).  Only the fields listed in
the function are changed; pair.py --install puts the same file into both
game folders.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.join(HERE, 'saves', 'play4_SAKLA002.SAV')

DESC, FLAGS, STATS, OPTS = 0, 62, 94, 1464
POS, DISTRICT, MUSIC, BB6D, TIME = 1473, 1478, 1480, 1482, 1490
STOCK_W, STOCK_G, MARK, FEE = 1496, 1507, 1518, 1522
ST_HP = 0x3A                     # health word of a 5Ch stat record (word 1Dh)


def patch(d, x, y, facing, district, money, hp, desc):
    """position, district (0-based: district n is PC<n+1>), money (BB73),
    health of members 1..4 (None = unchanged), description.  The fields that
    s3d_load_district(first = 1) sets on a real arrival are set the same
    way: the shop stocks BBB7/BBAC cleared (rolled on the first visit) and
    the city-map marker BB8A/BB88 from the table 16E0:4A86 (district - 1)."""
    marks = {1: (105, 95), 2: (125, 185), 3: (107, 226), 4: (156, 210)}   # (BB8A, BB88)
    d = bytearray(d)
    d[DESC:DESC + 60] = desc.encode('ascii').ljust(60, b'\0')
    struct.pack_into('<hhB', d, POS, x, y, facing)
    struct.pack_into('<h', d, DISTRICT, district)
    struct.pack_into('<h', d, BB6D + 6, money)
    for i, v in enumerate(hp, 1):
        if v is not None:
            struct.pack_into('<h', d, STATS + 0x5C * i + ST_HP, v)
    d[STOCK_W:STOCK_W + 11] = bytes(11)
    d[STOCK_G:STOCK_G + 11] = bytes(11)
    struct.pack_into('<hh', d, MARK, *marks[district])
    assert len(d) == 1524
    return bytes(d)


def play5(base):
    """district 3 (PC4, Eminonu) at (11,12) facing south: one step from the
    fortune-teller's door (10,13) and 10 cells from the doctor (8,11).
    206 kIvrIk: 5 for the fortune-teller, then 201 - one doctor's fee
    (101..200) pays, a second one never does.  Health: ali 4 (full), ayse 1
    (wounded), rifki -10 (dead: the only state the doctor treats, <= -5),
    zeki -2 (down but not dead)."""
    return patch(base, 11, 12, 2, 3, 206, (None, 1, -10, -2), 'doktor')


def main():
    kind, out = sys.argv[1], sys.argv[2]
    data = globals()[kind](open(BASE, 'rb').read())
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'wb').write(data)
    print(f'{out}: {len(data)} bytes')


if __name__ == '__main__':
    main()
