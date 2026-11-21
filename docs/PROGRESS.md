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
| 5 | Engine (ALTDUZEY) decompiled | all engine layers below the UI done and integrated (2026-11-21): 103/103 differential tests in one DLL, exact Borland far-heap layout; GRTEXT (10/10) + INTRFACE (34/34) done, 17 tests, in base (120/123 green; the 3 are the 3DQUICK tests, merged by WP16). Game-module wave WP13–17 done and integrated (2026-11-21): every function of IST.EXE is decompiled; shared DLL 218/219 before the last battle test landed; whole-game verification (docs/VERIFY.md): the intro matches the original in DOSBox-X from start-up to the menu (200 samples, 0–50 s; EMS double-mapping fix); scripted play-through play3 (read-count sync + poke/srand anchors) matches the original exactly at every checkpoint – menu, chargen x4, district, save, door/treasure event, battle start + 12 battle keys (registers, DAC, screen, VRAM, DGROUP; GRP.DAT and SAKLA001.SAV byte-identical) – except one checkpoint inside the clock-timed fade into the story. Fixed on the way: save description buffer, DDS driver load via vfs (lowwater 6E06/DE30 now match), DOSBox-X SB16 IRQ 7 (voice wait). Next: shop + spell battle. Lale64.exe (was lale.exe) runs the intro (start-up matches the DOSBox-X oracle in EMS/DAC/VGA/screen; DSMI driver-load heap block and 3 DSMI values differ – open). Earlier: infrastructure done (2026-11-20): DGROUP model `dg[]`, emulated PC `pc.h`, Unicorn differential harness, SDL3 host loop; wave 1 (EMS/MEMMAN/NKONTROL, DSMI DDS/timer, 386 blitters + 3D quads, file layer + Borland stdio, AMP player, MCP mixer) in progress |
| 6 | Game modules decompiled | – |
| 7 | DSMI audio decompiled | done: player, loader, CDI, MCP mixer (verified); audio-backend wiring pending (NSES/OSES wave) |
| 8 | Full-game verification against DOSBox-X | – |
| 9 | macOS build | – |

## Function status

| Module | Functions | draft | verified | replaced |
|---|---|---|---|---|
| (filled in once the inventory is merged into analysis/names.tsv) | | | | |

## Decompiled modules

| Module | Functions | State |
|---|---|---|
| ovl5737 CD audio (`src/game/cd5737.c`) | 15/15 | draft, unit-tested (`tests/test_cdaudio.c`); oracle check pending |
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
| ISOSYS battles ovl4595 (`src/game/iso*.c`) | 77/77 | verified (32 tests: rules, AI 96 seeded turns, every vecize, battlefield of all 18 maps, divide-by-zero via the INT 0 handler semantics, 8 whole scripted battles with real SAVAS.CKD graphics); open: Ctrl-Break, sound-on paths, door wall without bar picture (original blits garbage, port would crash on NULL – check whether game data contains it) |
| FRPOS shop/event scripts ovl3FD1 (`src/game/frpos*.c`) | 33/33 | verified (5 tests: every event/trigger of all 18 PCSn files, 33/33 opcodes; tools/frposdis.py) – in its own DLL, joins base after the wave |
| 3DSYS exploration ovl303F (`src/game/s3d*.c`) | 32/32 | verified (charscr, loop, menus, panel, camp, move, keys, save write + read incl. the original's SAKLA001.SAV, district load/enter/location, all wall kinds); untested: s3d_debug_buffers, s3d_zones, s3d_save_map |
| 3DQUICK first-person renderer ovl2700 (`src/game/q3dview*.c`) | 36/36 | verified (15 tests: start-up tables over ~340 view sizes, all 18 maps x 4 facings, automap, animations); open: 100x100 views, rotate_left speed 2 (742 bytes outside view) |
| 3D quads seg08D0 (`src/game/q3d.c`) | 7/7 | verified (differential, bit-exact x87 model); EMS lock wrapper by inspection |

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
