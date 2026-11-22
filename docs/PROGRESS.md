# Progress

Status legend for functions: **draft** (written, not yet verified), **verified** (passes a
differential test or was checked against the oracle), **replaced** (platform/RTL
equivalent, no decompilation needed).

## Milestones

| # | Milestone | State |
|---|---|---|
| 1 | Foundation: unpacker, segment model, Ghidra pipeline, FPU fix, listings | done (2026-11-20) |
| 2 | Inventory: all functions named and modules mapped | first pass done: 880 names (`analysis/names.tsv`) |
| 3 | Formats documented, with Python viewers | FLiB, PAL, PUR, RES, CMP verified; GRP/SAV/FRPOS/GFL/AMF described, not yet tool-verified |
| 4 | Platform layer: VGA emulation, SDL3 host, timer, input, audio | VGA: modes 13h/X/12h scan-out, BIOS tables and DACs verified against DOSBox-X; virtual CD drive + CD-music option; next: SDL3 window/timer/input loop |
| 5 | Engine (ALTDUZEY) decompiled | all engine layers below the UI done and integrated (2026-11-21): 103/103 differential tests in one DLL, exact Borland far-heap layout; GRTEXT (10/10) + INTRFACE (34/34) done, 17 tests, in base (120/123 green; the 3 are the 3DQUICK tests, merged by WP16). Game-module wave WP13–17 done and integrated (2026-11-21): every function of IST.EXE is decompiled; shared DLL 218/219 before the last battle test landed; whole-game verification (docs/VERIFY.md): the intro matches the original in DOSBox-X from start-up to the menu (200 samples, 0–50 s; EMS double-mapping fix); scripted play-through play3 (read-count sync + poke/srand anchors) matches the original exactly at every checkpoint – menu, chargen x4, district, save, door/treasure event, battle start + 12 battle keys (registers, DAC, screen, VRAM, DGROUP; GRP.DAT and SAKLA001.SAV byte-identical) – except one checkpoint inside the clock-timed fade into the story. Fixed on the way: save description buffer, DDS driver load via vfs (lowwater 6E06/DE30 now match), DOSBox-X SB16 IRQ 7 (voice wait). play3's chargen-fade checkpoint now anchored on the story video's frame (identical). play4 (2026-11-21, with sound on both sides): spell battle lost to the dog's AI, game over + load, second battle won, item exchange, goods/weapon shops (buy/sell by keys and mouse), food stall, camp, save – all 112 checkpoints identical (DGROUP minus host/clock fields, screen, DAC); SAKLA002.SAV differs only in the description tail, uninitialised stack in the original; no port bugs found, new clock-relative `@n` script timing. Doctor: play5 (2026-11-22, crafted save in district 4): doctor (refuse, not dead x3, revive, too poor), fortune-teller, hotel room + camp + keeper, save, step back/sidestep/view size/character screen/options/city map – 39 checkpoints identical. Lale64.exe (was lale.exe) runs the intro (start-up matches the DOSBox-X oracle in EMS/DAC/VGA/screen; start-up heap chain (107 blocks) and DGROUP identical since the driver load goes through the file layer; what still differs is AMP's private interrupt stack, native routine pointers and the DSMI clock phase – none read by game code; checked 2026-11-22). Earlier: infrastructure done (2026-11-20): DGROUP model `dg[]`, emulated PC `pc.h`, Unicorn differential harness, SDL3 host loop; wave 1 (EMS/MEMMAN/NKONTROL, DSMI DDS/timer, 386 blitters + 3D quads, file layer + Borland stdio, AMP player, MCP mixer) in progress |
| 6 | Game modules decompiled | done (2026-11-21): main, DEMOLAR, CHARGEN, FRPOS, 3DQUICK, 3DSYS, ISOSYS, GFL – see "Decompiled modules" |
| 7 | DSMI audio decompiled | done: player, loader, CDI, MCP mixer (verified); audio-backend wiring pending (NSES/OSES wave) |
| 8 | Full-game verification against DOSBox-X | play3, play4, play4load identical at every checkpoint (docs/VERIFY.md). Decision (user, 2026-11-22): the save description's tail after its NUL stays zeros in the port (uninitialised stack in the original, never read). Gap wave started 2026-11-22: doctor event (district 4), ISOSYS Ctrl-Break / sound-on / door wall without bar picture, 3DQUICK 100x100 + rotate speed 2, three untested 3DSYS functions, CD audio vs the original code |
| 9 | macOS build | – (on the Mac mini, same sources) |

## Function status

| Module | Functions | draft | verified | replaced |
|---|---|---|---|---|
| (filled in once the inventory is merged into analysis/names.tsv) | | | | |

## Decompiled modules

| Module | Functions | State |
|---|---|---|
| ovl5737 CD audio (`src/game/cd5737.c`) | 15/15 | verified (2026-11-22, `tests/py/test_cd5737.py`, 16 tests: every device request, return value and DGROUP write vs the original code over an MSCDEX model; 466/468 instructions run, the 2 others unreachable). Fixed: `vtoc_ok` without MSCDEX / wrong drive / audio-only disc, stack reuse in `play_track`, `read_cfg` (was a stub); `cdrom.c` now leaves the request untouched for a non-CD drive and fills in the subunit, as MSCDEX does. Not compared: `drive_info` without MSCDEX (uninitialised far pointer in the original, no callers) |
| Borland `rand/srand` (`src/rtl/bc_rand.c`) | 2/2 | verified (differential, 4 seeds × 2000 steps) |
| FILEMAN `fm_cmp_decode` (`src/engine/fileman.c`) | 1/7 | verified (differential, game CMP files + random) |
| DSMI DDS registry seg0DF2 (`src/dsmi/dds.c`) | 11/11 | verified (differential); DDSLoadDriver maps .DSD names to native drivers |
| DSMI string helpers seg0DDF (`src/dsmi/dsmistr.c`) | 9/9 | verified (differential, quirks kept) |
| DSMI Timer Service seg10EF (`src/dsmi/timer.c`) | 9/9 | verified (differential: call order, slot table, PIT writes) |
| DSMI time base seg1145 (`src/dsmi/dsmitime.c`) | 6/6 | verified (differential) |
| DSMI raw EMS seg1134 (`src/dsmi/ems.c`) | 9/9 | verified (differential) |
| DSMI EMS heap seg1154 (`src/dsmi/emsheap.c`) | 17/17 | verified except unreferenced emsGetPageMapRaw (block placement byte-exact) |
| DSMI MemAlloc seg0E9E (`src/dsmi/memalloc.c`) | 4/4 | return codes verified |
| MEMMAN seg09D4 (`src/engine/memman.c`) | 22/22 | verified (differential); farcoreleft model pending (WP8) |
| NKONTROL seg0DB7 (`src/engine/nkontrol.c`) | 11/11 | tick/flags verified; ISRs by inspection |
| Borland stdio/io (`src/rtl/bc_stdio.c`) | 40 routines | verified (differential incl. text mode) |
| stdio handle layer ovl5646 (`src/engine/vfile.c`) | 9/9 | verified (differential) |
| FLiB virtual files ovl569A (`src/engine/vfs.c`) | 13/13 | verified (differential, all 8 archives) |
| 386 blitters asm2599 (`src/engine/blit386.c`) | 11/11 | verified (differential, whole destinations) |
| DSMI CDI seg0EAE + AMP 2.00 player seg0EF6 + AMF loader seg105B/111B (`src/dsmi/cdi.c, amp.c, amfload.c`) | 100/100 | verified (all 15 game AMFs: identical CDI call streams over 2,500 ticks) |
| DSMI MCP v3.5 software mixer from SDISB16.DSD (`src/dsmi/mcp.c`) | all mixer + 26 CDI slots + SDI shell | verified bit-exact (DMA ring) against the driver in Unicorn; 22050 Hz stereo s16 |
| NSES seg0D42 + OSES ovl55CC (`src/engine/nses.c, oses.c`) | 13/13 | verified except getenv/setvect hooks; native MCP registered as SDISB16.DSD |
| Audio backend (`src/platform/audio.c, audio_sdl.c`) | – | emulated SB16 DMA clocked by the PIT; game + CD audio in one SDL stream; wired into the host |
| FILEMAN rest ovl5447 (`src/engine/fileman.c`) | 7/7 | verified (real CKD members, >64 KB pictures, EMS remap points) |
| Fatal/misc ovl559A (`src/engine/fatal.c`) | 4/4 | 3 verified; beep unreferenced |
| Borland far heap (`src/rtl/bc_farheap.c`) | 10 routines | verified: seg:off, farcoreleft and arena bytes vs the original from the main() snapshot; MEMMAN switch-over pending |
| GFL/GHD video player ovl5313 (`src/game/gfl.c`) | 12/12 | verified (decoder on all 35 videos vs tools/gflview.py; player runs vs original: DGROUP/EMS/screen/palette/CDI) |
| SYSTEMS seg0AC6 + NGRAPHS seg0C7F + OGRAPHS ovl5511 (`src/engine/systems.c, ngraphs.c, ographs.c`) | 52/52 | verified (23 tests: DGROUP, A000, VGA regs/DAC/planes, EMS); fade interpolation only at ms=0 |
| main seg0546 + DEMOLAR ovl3C2D (`src/game/istanbul.c`, `demolar.c`) | 14/14 | partly verified (6 tests: waits, picture show/fade, zone outline, music off); open: intro/story/ending/credits need video/voice/music + real modules; main() start-up vs DOSBox-X oracle (integration pass) |
| CHARGEN ovl39E5 (`src/game/chargen.c`) | 12/12 | verified (8 tests: stat rolls, draw, load/save GRP.DAT, cancel, full 4-member flow by keys and mouse, animations) |
| ISOSYS battles ovl4595 (`src/game/iso*.c`) | 77/77 | verified (32 tests: rules, AI 96 seeded turns, every vecize, battlefield of all 18 maps, divide-by-zero via the INT 0 handler semantics, 8 whole scripted battles with real SAVAS.CKD graphics); 2026-11-22 (`test_iso_d.py`, 12 tests): Ctrl-Break (the original ends through its INT 23h handler at the next kbhit(); the port continued the battle – fixed), sound-on paths (same CDI/AMP call order and state in single calls and three whole battles), door wall without bar picture (cannot occur in the 18 shipped maps; forced, the original ends with internal error 10h and so does the port – no garbage blit, no crash) |
| FRPOS shop/event scripts ovl3FD1 (`src/game/frpos*.c`) | 33/33 | verified (5 tests: every event/trigger of all 18 PCSn files, 33/33 opcodes; tools/frposdis.py) – in its own DLL, joins base after the wave |
| 3DSYS exploration ovl303F (`src/game/s3d*.c`) | 32/32 | verified (charscr, loop, menus, panel, camp, move, keys, save write + read incl. the original's SAKLA001.SAV, district load/enter/location, all wall kinds); 2026-11-22 (`test_s3d_gap.py`): the portrait/zone routine 303F:27F7 (44 cases), and the two routines no code references – debug buffers 303F:23ED, map writer 303F:189D (written bytes identical) – verified |
| 3DQUICK first-person renderer ovl2700 (`src/game/q3dview*.c`) | 36/36 | verified (15 tests: start-up tables over ~340 view sizes, all 18 maps x 4 facings, automap, animations); 2026-11-22 (`test_q3dview_gap.py`, 5 tests): rotations in the game's configuration (100 window, speed 9) identical in whole A000/surfaces/DGROUP; 100x100 render identical up to the floor/ceiling strips, where the original (and the port) loops forever in the strip blitter – the game never renders at 100x100; the "742 bytes" were the port missing the 64 KB offset wrap in `q3d_draw_quad_mapped` (only after an ignored fatal error) – fixed in `q3d.c` (`far_at`) |
| 3D quads seg08D0 (`src/game/q3d.c`) | 7/7 | verified (differential, bit-exact x87 model); EMS lock wrapper by inspection |

## Gap wave (2026-11-22)

Engine and host fixes found while closing the gaps above:

| Where | Problem | Fix |
|---|---|---|
| `src/engine/intrface.h` | `ui_wait_voice` set the silence sample from DGROUP:DE1E; the original pushes DF1Eh (05DC:1E3C: sound slot 0Ah, bosch.pur) | constant corrected; `test_iso_d.test_sound_silence_sample` |
| `src/engine/systems.c` | `sys_blit` / `sys_blit_recolor` with a destination row above a conventional block (the big capital of a battle text box, y = -1 / -2) crashed; the original's huge pointer wraps to just below the block | the piece is placed as the original's 1 MB wrap places it (for a 320-wide surface: negative rows fall below the block and are dropped, the rest drawn from row 0; for other widths the whole piece misses the block); `test_iso_d.test_textbox_capital`, `test_q3dview_gap.test_render_100` (`sys_blit`; `sys_blit_recolor` by the same reasoning, no test reaches it) |
| `src/game/q3d.c` | missing 64 KB offset wrap in the quad drawer (after an ignored fatal error only) | `far_at` |
| `src/platform/cdrom.c` | request to a non-CD drive got a status written; MSCDEX leaves the header alone; subunit not filled in | as MSCDEX |
| `src/platform/pc.c`, `nkontrol.c`, `sdl3_main.c` | Ctrl-Break could not be produced by the host | Ctrl+Pause / Ctrl+ScrollLock = BIOS Ctrl-Break (buffer emptied, word 0000h, INT 23h at the next kbhit()); the game's INT 09h hook is offered every make code. A typed Ctrl+C stays an ordinary key, as the original in DOSBox-X takes it at the main menu (MS-DOS itself may break there – not verifiable here) |
| `src/platform/sdl3_main.c` | a key release was handled like a press: Esc, Enter, Tab, cursor and function keys were pushed twice in interactive play (scripts bypass the window) | releases only update the shift flags |

Not covered by any test or play-through: right-button cancel at the doctor's prompt, the
food stall of district 4 (needs flag 99), changing a setting in the options list, a
successful sidestep, `o5737_cd_drive_info` without MSCDEX, the Ctrl-Break DOS sequence
in DOSBox-X itself (modelled from documented BIOS/DOS behaviour).

## Analysis pipeline fixes (2026-11-20)

| Problem | Fix | Effect |
|---|---|---|
| Stock Ghidra wraps real-mode near branches at *linear* 64 KB boundaries; segment-offset wrap (used by TLINK for >32 KB backward calls) was also missing | `ia_rel16.patch` (+`rmseg` context), `FixNear` | 183 wrong branch targets → 0 of 10,738 (checked by `branchcheck.py` on every run) |
| Switch jump tables not followed | `FixSwitch` | 45 tables, 304 case targets disassembled (e.g. the 33 FRPOS opcodes) |
| FixFPU patched `CD 3x` bytes inside operands reached by wrong flows and missed real sites (ovl2700, ~650 bytes) | `tools/fpupatch.py` linear sweep (skipping switch tables) before import; FixFPU kept only for KUR.EXE (2026-11-21) | image now identical to the independent sweep; 10,746 branches still 0 wrong |
| Bogus functions from wrong flows | `PruneFunctions` | 10 removed (2 had been named as "fragments") |
| Total | | resolved call sites 5,034 → 5,573; string refs 951 → 1,019; decompile failures 25 → 4 |

Remaining: one RTL table (`0000:0B08`, FP formatting, replaced in the port anyway).
Caller lists written before this date (`analysis/names/*.md`) may miss callers that go
through switches or wrongly-resolved near calls; re-check against the current listings.

## Reference runtime

`C:\DOSBox-X\dosbox-x.exe -conf tools/dosbox/ist.conf` runs the original from `orig/`
(386, VGA, EMS, SB16). It was confirmed booting into the intro on 2026-11-20.
`tools/dosbox/oracle.py` scripts it through the debugger link (breakpoints, DGROUP/VGA/DAC dumps).
