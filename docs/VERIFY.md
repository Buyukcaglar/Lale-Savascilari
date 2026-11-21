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

Result (2026-11-21, `play2`): between two events both sides make the same
number of rand() calls per segment except for clock-timed ones (chargen
blink `random(3)`, the talking-face `random(2)`): off by one per few
thousand calls, which is enough to change the next roll.  `srand` before
each roll fixes the roll only if no blink is pending; in the battle the
first voice differs and the original then waits (DOSBox-X) in
ui_wait_voice for a 300h-byte voice whose channel position never reaches
the end (open).

What stays clock-driven and therefore differs under read-count sync: the
game clock E74A itself and everything timed from it (chargen walking sprite
frame and blink state 48E8/48EA, 3DSYS portrait blink with its once-a-second
`random(20)`, the battle's talking-face animation with `random(2)` every
1.5 s while a voice plays, AMP/DSMI timing, fade/gfl timestamps).  Under
clock sync it is the other way round: per-pass `random()` calls drift.

`tools/verify/mkscript.py` builds longer scripts (play-through) from Python:
`python tools/verify/mkscript.py play2 out/verify/scripts/play2.txt` is the
verification play-through (chargen in reads, story skipped, district 0,
save to slot 2, the door event with its treasure screen, the dog battle in
reads from the first actor on).

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

* `tools/verify/vcompare.py <port> <orig> [--png dir] [--dg-detail]` - same-name
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
