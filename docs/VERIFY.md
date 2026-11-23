# Whole-program verification: port vs. original (DOSBox-X)

One input script drives both programs; both write checkpoint dumps in the same
format; Python tools compare them.

## Input scripts

Text file, one event per line (`#` comments), times in ms of the **game's own
clock** DGROUP:E74A (NKONTROL, +10 per 100 Hz tick):

```
seed 1234                    srand() seed at start-up (time() override)
50000 key 1C0D               BIOS key word scan<<8 | ASCII
52000 mouse 320 100 1        driver coordinates (x 0..639 in 320-pixel modes), buttons
52300 buttons 0
60000 dump c04_district      checkpoint dump
dumpevery 250 50000 250 i    dump i_<ms> every 250 ms
70000 quit
```

**Delivery rule (both sides):** at every *input read* of the game - `kbhit`
(INT 21h 0Bh), `getch` (INT 21h 01h/07h/08h, and every wake-up while it
blocks), `bioskey` 0/1/10h/11h (INT 16h, idem) and the mouse poll (INT 33h
AX=3) - all events whose time is <= the clock are applied, in file order.

Why: host time and frame counts differ between the two programs (DOSBox-X
spends emulated time decoding/loading, the port's virtual time does not), but
the game's clock and its input reads are game-visible on both.  When the game
waits for input, both see an event at the first read after the same tick.
Breakpoints per input read are not an option on the DOSBox-X side: every
debugger command costs 50 ms (the emulator polls the link at 20 Hz).

**Read-count sync** (`+n` instead of a time): a *counted read* is the entry
of one of the functions above (not the wake-ups of a blocking read; on the
DOSBox-X side the INT 16h calls DOS makes inside the game's INT 21h call are
not counted).  `+n <event>` is due n counted reads after the read that
applied the previous event - or at once at a wake-up of a blocking read.
Loops that poll once per pass then make the same number of passes between
two events on both sides; chargen calls `random(1000)` once per pass, so its
rolls only match this way.  Anchors re-align the count where a clock-bounded
loop (videos, fades with input polls, timed texts) came before:

```
150000 sync D5DE 0019        nothing; due only when also DGROUP word D5DE == 19h
150000 syncne D5DE 0000      ... when D5DE != 0 (battle: the first actor is up)
+30000 key 4800              30000 counted reads after the anchor
+0 srand 00001000            RTL random state DGROUP:94C4 := 1000h (re-aligns rand())
```

**Clock-relative timing** (`@n`): due when the clock is >= n ms past its
value at the read that applied the previous event (hook.S type bit 40h,
`sc_rel == 2` in sdl3_main.c).  Needed wherever the game waits on the clock
in a tight polling loop: `ui_wait_click_or_key` (n x 20 ms), the message
box (mode 0: (lines*6+20) x 150 ms), spell and bump animations, palette and
music fades.  There the reads run at CPU speed - about 14-20 reads/ms in
the port, 37-43 in DOSBox-X - so a read count is a different time on each
side, and keys that arrive inside such a wait are flushed.  `mkscript`'s
`Script.after(n)` switches to `@`, `reads(n)` back to `+`; the mode
sticks, so a clock gap followed by a read gap needs an event in between
(a `srand`).

Checkpoint events that neutralise clock-timed state (both sides):

```
+0 srand 00001000            RTL random state DGROUP:94C4 := 1000h
+0 poke 48E8 03 00 02 00     write bytes (hex) to DGROUP:48E8.. (split into
                             byte/word events, hook.S type 8)
```

### What is clock-timed and how play3 neutralises it

| State | Why it differs | play3 |
|---|---|---|
| chargen walking frame / blink 48E8/48EA, the blink's `random(3)` (150 ms tick) | ticks fall on different passes | at each checkpoint `poke 48E8 = 3, 48EA = 2` (no new blink; the next tick draws frame 0 and the normal face), `sync 48E8 0000` (that tick), `srand`, dump; at Space `srand` + `poke 48E8 = 0 0 0 0` - the roll and the next member's screen are then identical |
| 3DSYS portrait blink: `random(20)` once a second, timer and state are locals whose phase depends on load times (DOSBox-X spends emulated clock loading, the port's virtual time does not) | blinks start on different calls; a blink cut by leaving the loop leaves its face on screen | `srand` with a *quiet seed* (`mkscript.quiet_seed`: the next 8 `rand()` values are all >= 1639, so no `random(20)` is 0) every 2 s from the story on and at every exploration key / dump |
| battle talking face `random(2)` every 150 ms while a voice plays (BBC4) | as above | keys 30000 reads after the previous checkpoint (voice over on both sides), `srand` at every key, dumps 10000 reads after a key, 100000 after TAB (the enemy turn's clock-timed animations; the port makes ~3x fewer reads per ms than DOSBox-X) |
| AMP/DSMI timer state, DSMI time, GFL frame/start times, s3d fade time, voice end time 73D1, EMS mapping registers that follow GFL streaming | music/videos started at load-time-dependent clocks | not neutralised: `vcompare.py` lists them by name only with `--clock` (`CLOCK_FIELDS`) |

`vcompare.py` also ignores *host fields* (`HOST_FIELDS`: Borland RTL data
the port replaces - _8087, atexit count, exit hooks, conio _video, delay
calibration/brk/environ, overlay-manager table, harderr vectors - and the
CD request block, kept by cd5737.c in a native mirror; none is read by game
code) and far pointers whose segment is 0 in the port (`farptr_ranges`,
`farptr_tail_filter`: records that keep only the offset).

### Result (2026-11-21, `play3`)

`python tools/verify/mkscript.py play3 out/verify/scripts/play3.txt;
python -u tools/verify/pair.py out/verify/scripts/play3.txt play3`
(pair.py resets the game folders, runs both sides, compares the dumps and
the files the game wrote):

* menu, chargen start + 4 members: identical (screen, DAC, registers,
  DGROUP); GRP.DAT identical.  `c03_after_chargen` was dumped at a fixed
  read count inside the story video (seq1.GFL, 10 fps, frames due by the
  clock): the port showed frame 3, the original frame 1.  It is now
  anchored on the video itself - `sync D618 0005` (D618 = the last frame
  processed) - and is identical (play4 runs of 2026-11-21, which share
  play3's prefix).
* district 0: arrival, 2 turns, save menu, save to slot 2, the door
  question, the treasure screen: identical; SAKLA001.SAV identical.
* the dog battle: start (first actor up) and 12 battle keys (UP, UP,
  ENTER, TAB x3, both sides' turns): identical at all 13 checkpoints.
* So every checkpoint of play3 but `c03_after_chargen` is identical in
  registers, DAC, screen, video memory and DGROUP (minus host fields; the
  clock fields are listed with `--clock`), and both written files match.
  (Run of 2026-11-21 after the mouse latch; earlier runs differed only in
  the 3DSYS cursor shape E5E1/E5E3 = host pointer, and in battle dumps
  taken mid enemy turn before the 100000-read gap.)
* Shop, item exchange, spells, load: see play4 below.
* Differential suite after these changes: 219/219 (out/base).

Port fixes found on the way: the DSMI driver load opens SDISB16.DSD through
the game's file layer (its 512-byte stdio buffer is the paragraph that made
6E06/DE30 differ, `src/dsmi/dds.c`); the save description buffer is an
uninitialised stack buffer written in full (60 bytes) - modelled as a
zeroed static (`src/game/s3d_menu.c`).

Mouse: hook.S reports at AX=3 only the position the game (AX=0/4/7/8) or
the script set, as the driver clamped it - with `autolock = false`
DOSBox-X's mouse integration otherwise moves the DOS mouse whenever the
host pointer crosses its window (seen as a different 3DSYS cursor shape
E5E3 in one run).

DOSBox-X configuration: the SB16 must be on IRQ 7 (tools/dosbox/ist.conf),
as SES.CFG says.  With IRQ 5 the DSMI driver unmasks IRQ 7, gets no
interrupt, the DMA stalls, every voice's position stays 0 and
ui_wait_voice (05DC:1D1B: position + 20h >= sample end, or Esc) never ends
and eats the script's keys; on hardware (and in the port) a 300h-byte
voice at 3676 Hz ends after ~0.21 s.  Dumps made before this change ran
without sound and are not comparable.

### play4: battle with spells, game over + load, item exchange, shops, camp, save

`python tools/verify/mkscript.py play4 out/verify/scripts/play4.txt;
python -u tools/verify/pair.py out/verify/scripts/play4.txt play4`
(about 45 min; then `play4load` with `pair.py --keep`, see below).

Flow (play3's prefix up to the dog battle, then):

* **Battle with a spell and the enemy AI, to a loss** (`c12_b00..40`, 42
  action groups planned on the port, `out/verify/h/plan2.py`): Rifki (unit
  27) casts vecize 1 "pis" (`v`, Enter, `o` next enemy, Enter) on the dog
  once it is adjacent, then bumps into it (keypad moves into the enemy);
  the others pass (Tab).  The party has no weapons, so Enter attacks are
  ignored (the attack needs one) and vecize 1 does no damage; the dog's AI
  bites the party to death: ISOSYS result 632E = 2.
* **Game over and load**: "eski bir oyuna devam edecek misiniz?" -> `e` ->
  load menu -> Enter loads SAKLA001 (`c13_*`).  Load is only reachable
  from the main menu and from this game-over loop.
* **Second battle, won**: the door/treasure event again, then four groups
  in which every member casts vecize 1 (out of range: "desem de duyamaz
  ki!") and tries an attack; the dog flees on its turn, result 1
  (`c14_*`).
* **Item exchange** with the man at (8,1) of district 0 (`c15_*`, `i`,
  Enter, Enter), the key event (flag 2), leaving district 0 (flags 81) and
  arriving in district 1 (`c16..c19`).
* **Shops** (district 1: goods at (3,7), weapons at (11,10), food at
  (13,16)): in goods and weapons, each step dumped (`c21_goods_*`,
  `c23_weapons_*`): `b` converts the currencies, buy by keys (`i` shop
  row, Enter, Right Right, Enter), sell by keys (`i`, Enter, `s`), buy by
  mouse (shop slot 3 then inventory slot 4), sell by mouse (inventory slot
  4 then the sell zone), Esc.  Money: 11 -> 216 (convert) -> 78 (key buy)
  -> 87 (key sell) -> 11 (mouse buy) -> 50 (mouse sell).  Food stall:
  "evet", 12 paid, hunger cleared (`c24/c25`).
* **Camp** (`k`, rest, `c26/c27`) and **save** to a new slot (SAKLA002,
  description "cd", `c28/c29`).
* **Doctor**: not in play4 - the only doctor event (key 31313127) is in
  district 4 (PC4 at (8,11)), out of reach of a short script; play5 below
  gets there from a prepared save.

Result (2026-11-21): **all 112 checkpoints of play4 are identical** -
registers, DAC, screen, video memory and DGROUP (minus host fields; clock
fields listed with `--clock`), sound on in both programs (SB16 IRQ 7);
GRP.DAT and SAKLA001.SAV identical.  SAKLA002.SAV differs in 48 bytes,
0x0B-0x3A: the tail of the 60-byte description after its NUL, which
`s3d_save_load_menu` writes from an uninitialised stack buffer
([bp-3CAh]).  In the original that tail is a stale DOS find_t record
("SAKLA001.SAV", attributes, time, size) left by the load menu that ran
from the game-over loop at a different stack depth; the port's static
buffer holds the text of its previous use.  Reproducing it would need a
model of the original stack's history; no game code reads those bytes.
`play4load` (`pair.py ... play4load --keep`: main menu, load SAKLA002 in
a fresh session, one turn) - 4 checkpoints identical except the load
menu's list entry A6AB-A6DB, which holds that same description tail; the
loaded game (district 1, (12,16), 107 money) is identical.
No port bug was found by play4: every divergence on the way was script
timing (fixed in the script, see below).  Differential suite after
these changes: 219/219 (out/base).

Timing lessons (all in `mkscript.py`):

* Clock-timed waits (sign texts, shop greetings, the cast and bump
  animations, fades) use `@n`; keys inside 3D walks are re-seeded with a
  quiet seed in their own read (`rkey`, `rwalk`), long waits re-seed every
  second (`quiet_wait`); dialogs are anchored on the flags they set at the
  end (`sync B9CA+n`).
* `quiet_wait` re-seeds with seeds whose next 64 draws are quiet (the 3D
  view draws more than 8 between two re-seeds a second apart; with 8 a
  portrait blink started on one side only, c25).
* Dumps after a shop's Esc or the game-over result wait for the
  100 ms-step fades of `s3d_apply_sound_options` to end (DAC, 3D6E, 3D70,
  7318, 731C differed mid-fade).
* The shops roll their stock with `random()` right after the greeting:
  no re-seed may fall between the move's re-seed and that roll.
* After a cast or a bump, 12 s of clock (`battle_groups`): with read gaps
  DOSBox-X was still in the spell animation when the next Tab came.
* The second battle's anchor needs `poke D5DE 0000` first (D5DE keeps the
  first battle's last actor); game over is anchored on `sync 632E 0002`.
* `vcompare.CLOCK_FIELDS` gained BBC4 (ISOSYS talking-face timer) and D534
  (the AI decision's clock word): clock stamps only.

### play5: district 4 from a prepared save - doctor, fortune-teller, hotel, 3DSYS keys

```
python tools/verify/mksave.py play5 out/verify/scripts/play5.sav
python tools/verify/mkscript.py play5 out/verify/scripts/play5.txt
python -u tools/verify/pair.py out/verify/scripts/play5.txt play5 --install out/verify/scripts/play5.sav=SAKLA001.SAV
```
(port 25 s, DOSBox-X about 10 min.)

**The save.**  `mksave.py` derives it from a save the original wrote itself
(`tools/verify/saves/play4_SAKLA002.SAV`, play4's SAKLA002: district 1,
party ali/ayse/rifki/zeki) and changes only: the description ("doktor" +
NULs), position (11,12) facing south, district 3 (PC4), money 206, the
health words of members 2-4 (1, -10, -2), and the three things
`s3d_load_district(first)` sets on a real arrival (shop stocks BBB7/BBAC
cleared, city-map marker BB8A/BB88 = 107/226).  Flags, time, music track,
options and the fee byte (106) stay.  `pair.py --install FILE=NAME` copies
it into both game folders after the reset.  The crafted save loads
identically: `d02_loaded` is identical in DGROUP, screen and DAC.

Flow (39 checkpoints `d00`..`d38`):

* **Load** from the main menu (`d00_menu`, `d01_loadmenu`, `d02_loaded`).
* **Fortune-teller** (10,13), event 18 - ops not used by play3/4: a
  question (`d03_gypsy_q`), `pay 5` (op 26: 206 -> 201), four long texts
  (`d04_gypsy_text` in the first), music 7 and back to 1 (op 25, with
  fades), `findminmax` + `say 5` (ops 23, 17 with WHO), flag 100
  (`d05_gypsy_done`).
* **Doctor** (8,11), through the sign of (9,10) (event 16), four visits:
  1. the fee question "visite : 106" (`d06_doctor_q`), `h`: refused
     (`d07_refused`);
  2. `e`, "kim servis istiyor ?" (`d09_who`), then members that are not
     dead: F1 (health 4, `d10`), a click on portrait 2 (health 1, `d11`), F4
     (health -2, `d12`) - "bu adam OlU deGil. baSkasInI seCin" each time -
     and Esc (`d13_cancelled`, no money taken);
  3. `e`, a click on portrait 3 (health -10): the four texts, health 1,
     201 -> 95 (`d16_revived`);
  4. `e` with 95 < 106: "bu para yetmez!" (`d18_too_poor`).

  The event only distinguishes health <= -5 ("dead") from the rest.
* **Hotel**: the sign of (14,9) (event 13), the room (12,9) with the fixed
  key 31313131 (3D5E bit 2: no encounters while camping, `d19_hotel_room`),
  a camp in it (`d20_camp` inside the rest, `d21_rested`: health 4/4/7/3),
  the keeper (13,8), event 11 (`d22_keeper`).
* **Save** to a new slot, SAKLA002 "ef" (`d23_savemenu`, `d24_saved`).
* **3DSYS keys** no other play-through uses: a step back (`d25`), the
  sidesteps `4`/`6` against doors (refused, `d26`/`d27`), PgDn/PgUp view
  size with a turn in the 200x200 view (`d28`..`d30`), the character
  screen F1 and F3 inside it (`d31`, `d32`, back `d33`), the options list
  `t` (`d34`, Esc `d35`), `n`: the city map har4.cmp (flag 82, `d36`), the
  location text (`d37`) and back through `s3d_enter_view` (`d38`).

Result (2026-11-22): **all 39 checkpoints identical** - registers, DAC,
screen, video memory and DGROUP (minus host fields; clock fields listed
with `--clock`; the clocks themselves differ by up to 1 s at the same
checkpoint, DOSBox-X's emulated loading and drawing time).  GRP.DAT and
SAKLA001.SAV identical.  SAKLA002.SAV differs in 30 bytes, 0x0C-0x2F: again
the tail of the description after its NUL, the original's uninitialised
stack buffer ([bp-3CAh] of `s3d_save_load_menu`) - here it holds what the
`s3d_read_save` of the main menu's load left at that stack depth
("sinsan1.cmp", the last portrait name, return addresses); the port's
static buffer is zero there.  The other 1494 bytes are equal.  No port bug
was found.

Two statements of the documentation were wrong and are corrected from the
original's behaviour: a loaded save keeps its doctor's fee (FORMATS.md said
it is re-rolled), and the doctor treats members at -5 or lower (not "above
-5").

Timing lessons:

* Every way out of the doctor but "too poor" ends in `fr_redraw`, which
  puts the party back on (9,11) still facing west: one Up re-enters.  "Too
  poor" leaves it in the doctor's cell.
* The checkpoint inside the camp was first taken 2 s after `k`: the port
  was at 10:17, the original at 10:11 (one calendar minute per 50 ms of
  clock *plus* the drawing, which costs emulated time in DOSBox-X).  It is
  now anchored on the calendar minute (`sync 3D64 0014`): the first input
  read of that minute's pass is the same point on both sides.
* Long dialogs are anchored on what they set at their end: flag 100
  (`sync B9D6 0010`) for the fortune-teller - its texts take 53 s in the
  port and longer in DOSBox-X - and the revived member's health word
  (`sync 9AF4 0001`) for the doctor; one-line signs get 14 s of clock.
* The two waits of `n` (2000 and 2400 rounds) are ended by keys sent on
  the clock (8 s and 4 s after the previous event).

### play6: district 4 after flag 99 - sidesteps, options, right button, food stall

```
python tools/verify/mksave.py play6 out/verify/scripts/play6.sav
python tools/verify/mkscript.py play6 out/verify/scripts/play6.txt
python -u tools/verify/pair.py out/verify/scripts/play6.txt play6 --install out/verify/scripts/play6.sav=SAKLA001.SAV
```
(port 15 s, DOSBox-X about 10 min; `play6side`, `play6opt1`, `play6opt`,
`play6doctor`, `play6food` stop after a part.)

**The save.**  As play5's (same base, same `patch`), plus what the story of
district 4 leaves behind once its shops are open.  The doors of the food
stall (16,9), the weapon shop (8,16) and the goods shop (3,17) carry event
12, whose trigger 1 says "kapalI" and cancels the move while **flag 99** is
clear.  Flag 99 is set in one place only (all 18 PCS files and the code
searched): event 10 of PCS4, trigger 2 of the cell (16,0) - the landing of
the stairs out of the cellar of Yeni Camii.  With flag 31 set it shows Seyh
cehalet flying away ("Cok geC kalDIk..."), plays music 1 and sets 99.  The
chain that leads there, each flag set by the event named: 29 (event 1,
(0,19): tabIk vesile takes the party to the resistance; without it the
mosque is "closed for repairs"), 97 (event 2, (8..10,5): the battle with the
birds at the mosque's doors), 30 (event 3, (9,1): the bird that tells of the
cellars), 98 (event 4, (7,0): the battle at the hidden door), 31 (event 5,
(3,3): Seyh cehalet in the cellar; event 6 then teleports to (14,0)), 99.
`mksave.py play6` sets these six flags and music track 1 (event 10's
`music 1` comes right before its `setflag 99`), position (10,7) facing
north, money 126, health 4 / 1 / -10 / -2 as in play5, and hunger words
40 / 36 / 30 / 44 (the base save was written right after a meal: all 0).
Not reproduced: the optional loot of events 1 and 5 (three weapons, a
purse) and the flags of the optional battles (109, 110).

Flow (46 checkpoints `e00`..`e30`):

* **Load** (`e00_menu`, `e01_loadmenu`, `e02_loaded`).
* **Sidesteps** in the open street in front of the mosque: facing north
  `4` -> (9,7) (`e03_side_l`), `6` -> (10,7) (`e04_side_r`); a turn to the
  east (`e05_east`), `4` -> (10,6) (`e06_side_l_east`), `6` -> (10,7)
  (`e07_side_r_east`).  All four succeed.
* **Options list** (`t`, `e08_options`).  It holds seven entries: walking /
  door animation, battle speed (0..9), game music, battle music, sound
  effects, difficulty, view size.  Each is changed once by a left click on
  it (`e09_opt1_anim` .. `e09_opt7_view`: animation off, speed 5 -> 6, game
  music off, battle music off, effects off, difficulty 0 -> 1, view 200 ->
  100) and then back by keys with the pointer moved off the list
  (`e10_opt7_view` .. `e10_opt1_anim`, Down/Up + Enter; speed 6 -> 7).
  Entry 7 switching to the big view sets the animation byte, so the Enter on
  entry 1 switched it off again and one more Enter restores it
  (`e10_opt1_anim_on`).  Esc closes (`e11_view`); opened again
  (`e12_options`) and closed with the right button (`e13_view`).  Final
  option bytes 1 7 1 1 1 0 1.
* **Doctor** (8,11), three visits: `e` and at "kim servis istiyor ?"
  (`e15_who`) the **right mouse button**: cancelled, no money taken
  (`e16_rclick_cancelled`); `e`, F2 (health 1: "bu adam OlU deGil",
  `e18_not_dead_f2`) and the right button at that second prompt
  (`e19_rclick_cancelled2`); `e`, F3 (health -10): revived, 126 -> 20
  (`e21_revived`).
* **Food stall**: the signs of (9,10) and (16,8) (event 15), the door cell
  (16,9) - event 12 lets the party in (`e22_stall_door`) - and the stall
  (15,9), key 31313128, three visits: the price question "adam baSI 3"
  (`e23_food_q`), `h`: refused (`e24_food_refused`); price 4, `e`: the meal
  picture (`e26_eating1`), 3 living members x 4 = 12 paid, 20 -> 8, the four
  hunger words 55/51/40/44 -> 0 (`e26_fed1`); price 4 again, `e` with
  8 < 12: "not enough money" (`e28_food_too_poor`), nothing changed
  (`e28_food_left`).  These are all three branches of `shop_food`.
* **Save** to a new slot, SAKLA002 "gh" (`e29_savemenu`, `e30_saved`).

Result (2026-11-23): **all 46 checkpoints identical** - registers, DAC,
screen, video memory and DGROUP (minus host fields; clock fields listed
with `--clock`).  GRP.DAT and SAKLA001.SAV identical.  SAKLA002.SAV differs
in 30 bytes, 0x0C-0x2F: the description tail after its NUL (the original's
uninitialised stack buffer, the same bytes as in play5: "sinsan1.cmp" and
return addresses; zeros in the port).  The other 1494 bytes are equal,
among them the changed option bytes and the flags.  No port bug was found;
no run showed a mouse-cursor-only difference.

`s3d_apply_sound_options` and the options list: the list's own changes use
blocking `gr_fade` calls (game music: 300 ms down, module stopped or
started, 300 ms up; view size back to 100: 100 ms down and up), they leave
the counters 3D6E / 3D70 / 3D72 at 0, so the idle callback has nothing to
step after any entry - every options checkpoint, 3 s after the choice (8 s
for entries 3 and 7), is identical without waiting for a fade.  The
callback does step something later in play6: entering the roofed cell
(16,9) makes `s3d_apply_daylight` set the brightness and tempo counters,
which then move one step per 100 ms of clock.

Timing lessons (both first-run differences were script timing):

* `e22_stall_door` was first dumped 2.5 s after the step into (16,9): mid
  fade (port 3D6E/3D70 = 11/-1, original 13/-3, 106 DAC entries, 7318,
  731C).  Now 8 s later, after the fade.
* `e26_eating1` was first dumped 1.2 s after `e`.  The meal picture has two
  waits, 50 rounds inside `fr_show_icon` and 120 after it: 1 s + 2.4 s in
  the port, about three times that in DOSBox-X, so the port was in the
  second wait and the original in the first (same screen; EMS use 6E0A,
  8702, DC00 and the handle of surface 42h differed).  Now anchored:
  `fr_show_icon` frees surface 42h before loading and allocates it again
  after its wait, and `sync E4AF 0000` is the first read of that wait.
* A step's random encounter roll (`random(300) + 1 <=` the map's rate, 2 in
  PC4) comes more than 8 draws after the key's re-seed: with the 8-draw
  quiet seeds one step of the walk to the stall started a battle.  play6's
  moves use 64 quiet draws (`rkey(..., n=64)`).
* The options list's hotspots overlap ((24, 133 + 7i)-(162, 149 + 7i)) and
  the first match wins, with no hot-point offset for the list's cursor:
  entry i is clicked at y = 146 + 7i.  For the keys the pointer must be off
  the list (an entry under it takes the selection back), and the list opens
  on entry min(last choice, 3).

### Start-up (DSMI) - port vs original, 2026-11-22

`python tools/verify/mkscript.py startup out/verify/scripts/startup.txt;
python -u tools/verify/pair.py out/verify/scripts/startup.txt startup`, then
`vcompare.py out/verify/p_startup out/verify/d_startup --dg-detail --clock`
(dumps at 250 ms, 1 s and 5 s of the game clock), and
`tests/py/test_istanbul.py` (`test_startup_oracle`: the far heap and DGROUP
at main()'s call of the intro against a DOSBox-X memory capture).

* **Driver-load heap block: gone.**  The far-heap chain at the end of
  start-up is identical - 107 blocks, same first/last/rover, including the
  mixer's DMA buffer (1F8h paragraphs) and tables (404h) - against the
  capture of 2026-11-21 and a new one made with today's configuration (SB16
  IRQ 7).  The test now checks the whole chain strictly.
* **6E06 (conventional low-water) and DE30 (EMS low-water): identical** at
  `s_01000`, `s_05000`, the menu checkpoints and in the test (the
  `KNOWN` exemption of 6E06 is removed).
* **7B70-7BE7** (the test called 7BC8.. "SDI device record"): it is AMP's
  private interrupt stack (`ampInterrupt` switches SS:SP to DGROUP:7BE4);
  the original leaves the frames of the last timer tick there (return
  addresses 171A:0F46/0F79 = seg0EF6, DS, locals), the port has no 8086
  stack and leaves zeros.  Host field, never read as data.
* **DSMI time E798/E7A0, AMP last-tick time 85A2**: clock fields.  At game
  clock 1000 the original's DSMI time is 10100 (0.1 ms units), the port's
  10000: the DSMI timer starts before NKONTROL's clock and the start-up
  in between costs emulated time in DOSBox-X, none in the port's virtual
  time.  The timer-service slots 85CE.. differ only in their routine far
  pointers (F000:FEA5 the old INT 8, 1969:0096, 1566:0120 ..., native
  pointers in the port).
* `s_00250` is not comparable: the original's first input read is at clock
  440, still in text mode; `s_01000` and `s_05000` are identical in DGROUP
  and differ on screen only by the intro video's frame (D618: frame 9 vs
  4 at 1 s - the video starts later on the original's clock; the intro is
  compared frame by frame with `frames_match.py`).

No open DSMI start-up difference remains.

`tools/verify/mkscript.py` builds the scripts (`play3` above; `play2` is
the older version without the checkpoint neutralisation);
`tools/verify/mksave.py` builds prepared saves.

## Port side

```
out/windows/Lale64.exe --game-dir <copy of orig> --script s.txt --out <dir>
                     [--seed n] [--step-us 100] [--frame-log <dir>]
```

`src/platform/sdl3_main.c` ("input scripts"): virtual time
(`pc_set_virtual_time`), window input ignored, events applied through
`pc_set_input_hook` (`src/platform/pc.c`: kbhit/getch/bioskey/INT 33h AX=3).
`--frame-log` writes every distinct video state (one check per 10 ms tick).

## DOSBox-X side

```
python -u tools/verify/dbx_replay.py s.txt --out <dir> [--game-dir out/verify/game_dbx]
```

Breaks at main() after `srand` (0546:0292), overwrites the seed, writes
`tools/verify/hook.S` (assembled by `build_hook.py` with llvm-mingw) into the
free UMB at D800h (MCB CC00h marked as DOS-owned) and hooks INT 16h/21h/33h.
The hook implements the delivery rule inside the emulated PC (keys are
stuffed into the BDA ring, mouse moves go through the driver's AX=4, buttons
replace BX of AX=3).  Only `dump`/`quit` markers stop at a breakpoint
(`hook+marker_bp`); the hook has already captured VGA registers + DAC, Python
dumps A000 (chain-4) or the 4 planes and DGROUP.

## Dumps and comparison

`<name>.vga` (misc, seq 0-4, gc 0-8, crtc 0-24, attr 0-20, DAC), `.vram`
(chain-4: the CPU view of A000, else 4 planes), `.dg` (DGROUP), `.txt` (clock).

* `tools/verify/pair.py <script> <tag> [--keep] [--install FILE=NAME]` - both sides +
  vcompare + written files; `--keep` does not reset the game folders (a
  script that loads what the previous one saved: `play4load` after
  `play4`); `--install` copies a prepared file (a save of `mksave.py`)
  into both game folders after the reset (`play5`).
* `tools/verify/vcompare.py <port> <orig> [--png dir] [--dg-detail] [--clock]` - same-name
  checkpoints: registers, DAC, rendered picture, video memory, DGROUP.  DGROUP
  ignores: far pointers (heap/EMS/VGA, native in the port), relocated segment
  words (16E0 vs 1F04), RTL vectors/PSP, AMP's private ISR stack (7BA0-7BE7),
  the INT 33h flags image (E764), past DGROUP end (EA80+: stack).
* `tools/verify/frames_match.py <port frame log> <orig dumps>` - timing-
  independent: every sample of the original is looked up among the port's
  distinct states.
* `tools/verify/montage.py <dir> out.png [cols] [filter]` - contact sheet.

Game folders: work on copies (`out/verify/game_port`, `out/verify/game_dbx`),
the game writes its saves there.
