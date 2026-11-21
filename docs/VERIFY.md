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
  DGROUP); GRP.DAT identical.  `c03_after_chargen` is inside the
  clock-timed fade into the story (expected to differ).
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
* Not covered yet: a shop (FRPOS shop opcodes), a battle with spells.
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

`tools/verify/mkscript.py` builds the scripts (`play3` above; `play2` is
the older version without the checkpoint neutralisation).

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

* `tools/verify/pair.py <script> <tag>` - both sides + vcompare + written files.
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
