#!/usr/bin/env python3
"""
mksave.py - build a prepared SAKLAnnn.SAV for a verification script
(docs/VERIFY.md, play5 to play10; format: docs/FORMATS.md "SAKLAnnn.SAV").

    python tools/verify/mksave.py play5 out/verify/scripts/play5.sav
    python tools/verify/mksave.py play6 out/verify/scripts/play6.sav

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
ST_HUNGER = 0x22                 # hunger word (word 11h): + (5 - endurance/100, min 1) per 10 minutes


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
    if district - 1 < 12:          # s3d_load_district sets the marker only for these
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


def set_flags(d, flags):
    """set FRPOS event flags (flag n: byte n/8, bit n%8 of the block at 62)"""
    d = bytearray(d)
    for n in flags:
        d[FLAGS + n // 8] |= 1 << (n % 8)
    return bytes(d)


# The story of district 4 (PCS4) up to the point where its shops open.  The
# doors of the food stall (16,9), the weapon shop (8,16) and the goods shop
# (3,17) carry event 12, whose trigger 1 says "kapalI" and cancels the move
# while flag 99 is clear.  Flag 99 is set only by event 10 (cell (14,0), the
# landing of the stairs out of the cellar of Yeni Camii, trigger 2): with
# flag 31 set it shows Seyh cehalet flying away, plays music 1 and sets 99.
# The chain that leads there, each flag set by the event named:
#   29  event 1  (0,19)  tabIk vesile takes the party to the resistance
#                        (without it Yeni Camii is "closed for repairs")
#   97  event 2  (8..10,5) the battle with the birds at the mosque's doors
#   30  event 3  (9,1)   the bird that tells of the cellars (opens event 4)
#   98  event 4  (7,0)   the battle at the hidden door
#   31  event 5  (3,3)   Seyh cehalet in the cellar (battle, the purse);
#                        event 6 (3,2) then teleports to (14,0)
#   99  event 10 (14,0)  "Cok geC kalDIk..." - music 1, the shops open
PC4_STORY_FLAGS = (29, 97, 30, 98, 31, 99)


def play6(base):
    """district 3 (PC4) after Seyh cehalet's escape: the flags of
    PC4_STORY_FLAGS and music track 1 (event 10's `music 1` comes right
    before its `setflag 99`).  (10,7) facing north, in the open street in
    front of Yeni Camii (free cells on all four sides: the sidesteps).
    Money 126: the doctor's fee is 106 (kept from the base save), so the
    "who" prompt is reached, and after one revival 20 are left for the
    food stall (random(5)+2 per living member: 6..18 for three) - one meal,
    then not enough.  Health as in play5 (ali 4, ayse 1, rifki -10 = dead,
    zeki -2); hunger words set to what a party that walked that far has
    (the base save was written right after a meal: all 0), so that the
    meal's effect shows."""
    d = bytearray(patch(base, 10, 7, 0, 3, 126, (None, 1, -10, -2), 'lokanta'))
    for i, v in enumerate((40, 36, 30, 44), 1):
        struct.pack_into('<h', d, STATS + 0x5C * i + ST_HUNGER, v)
    struct.pack_into('<h', d, MUSIC, 1)
    return set_flags(d, PC4_STORY_FLAGS)


def play7(base):
    """district 3 (PC4) one step before the end of its story: flags 29, 97,
    30, 98, 31 (PC4_STORY_FLAGS without 99), so the doors of event 12 are
    still "kapalI" and event 10 at (14,0) - Seyh cehalet flying away - is
    due.  (10,7) facing north as play6.  300 kIvrIk for two shops."""
    d = patch(base, 10, 7, 0, 3, 300, (), 'kapali')
    return set_flags(d, PC4_STORY_FLAGS[:-1])


def play8(base):
    """district 1 (PC2) at (18,19) facing east, one step west of (19,19),
    a cell of event 2: silikon baba and the story slideshow (FRPOS op 27,
    demo_story_scene).  Flag 4 (set at the end of event 2) is clear in the
    base save; nothing else changes but the position."""
    return patch(base, 18, 19, 1, 1, 107, (), 'hikaye')


def play9(base):
    """district 17 (PC18) at (3,2) facing south, the cell next to event 5
    (3,1, the last battle) - with flag 156 (9Ch) set, which event 5 sets
    after the battle: the next pass of s3d_explore_loop that handles a key
    calls demo_ending (the SON video, soon/devam, the save menu in mode 1,
    the credits).  The city-map marker stays as in the base save
    (s3d_load_district sets it only for districts 1..12)."""
    d = patch(base, 3, 2, 2, 17, 107, (), 'son')
    return set_flags(d, (156,))


def main():
    kind, out = sys.argv[1], sys.argv[2]
    data = globals()[kind](open(BASE, 'rb').read())
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'wb').write(data)
    print(f'{out}: {len(data)} bytes')


if __name__ == '__main__':
    main()
