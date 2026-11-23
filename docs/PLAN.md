# İstanbul Efsaneleri – Lale Savaşçıları: decompilation and port plan

The goal is a fully annotated, human-readable, portable C reconstruction of the DOS game
*İstanbul Efsaneleri – Bölüm I: Lale Savaşçıları* (v0.12.96, 1995–2005). It builds
for **Windows** (developed there) and **macOS** (Apple Silicon; the port of 2026-11-23,
[`MACOS.md`](MACOS.md)).

**1:1 fidelity is the top priority.** Every game-visible behaviour must match the
original DOS executable: logic, RNG sequences, timing, rendering down to the pixel,
palette, audio mixing and save-file bytes. Readability comes second, and portability
third.

---

## 1. Inputs

`_references/Istanbul-Efsaneleri-Lale-Savascilari_DOS_TR.zip` is extracted to `orig/`
(git-ignored).

| File | Role |
|---|---|
| `IST.EXE` | The game. 16-bit real mode, Borland C++ 3.x, large model, VROOMM overlays, 8087 emulator, needs EMS |
| `KUR.EXE` | Sound setup ("kur" = install). Writes `SES.CFG` |
| `*.CKD` | "FLiB" archives (see `docs/FORMATS.md`) |
| `SDI*.DSD` | DSMI sound drivers (GUS, PAS, SB16, WSS) |
| `SES.CFG` | Sound configuration (driver name and parameters) |
| `TRDOS.DAT` | 8×16 bitmap font, 256 glyphs (Turkish code page) |
| `GRP.DAT`, `GRPHAZIR.DAT` | Party / character data (`GRPHAZIR` = "ready-made party") |
| `SAKLA001.SAV` | A save game ("sakla" = save) |
| `SONPAL.PAL`, `BOSCH.KOR` | Palette, misc |

Archive contents (extracted to `extracted/<archive>/` by `tools/flib.py`):

| Archive | Content |
|---|---|
| `0GFL` | 35 `.GFL` + 35 `.GHD` full-motion video (district intros/outros, ending) – 218 MB |
| `3D` | First-person engine: wall/door/stair textures `.CMP`, `.PAL`, `.KOR` coordinate files, `.RES` |
| `FRPOS` | `PCn`/`PCSn` event/shop scripts (FRPOS.L interpreter) |
| `IKON` | 94 icon/portrait `.CMP` + `.PAL` |
| `IVIR` | Misc. screens `.CMP`/`.PAL`, `.RIX` fonts, THANX.DOC |
| `MUZIK` | 15 `.AMF` DSMI modules |
| `SAVAS` | Battle ("savaş") graphics `.CMP`, `.KOR` |
| `SOUNDS` | 196 `.PUR` raw samples + 10 `.SND` |

## 2. Binary facts (IST.EXE)

* MZ image 0x26010 bytes, 2063 relocations, entry `0000:0000` (Borland `c0`), DGROUP = **16E0**.
* Borland `__SEGTABLE__` is at file offset 0x18800 with 61 entries `{seg, maxoff, flags, minoff}`.
  TLINK packs segments at byte alignment, so contents live at `[seg:minoff, seg:maxoff)`.
* FBOV overlay block at file offset 0x28410: 15 overlays (one more stub is empty) and 280 entry thunks.
  The stub header is decoded in `tools/exeload.py`. Overlay fixups hold *segtable index × 8*.
* About 1,700 floating-point ops are 8087-emulator interrupts (`INT 34h`–`3Dh`).
  `tools/ghidra/FixFPU.java` turns them back into x87 opcodes, but only at true
  instruction boundaries.
* `asm2599` is hand-written 186/386 assembly in a data-class segment. The game needs a 386.

### Module map (from the inventory in `analysis/names/*.md`)

| Segment(s) | Module | Origin | Port treatment |
|---|---|---|---|
| seg0000 | Borland C++ 3.x RTL `_TEXT` (c0, stdio, malloc, printf, math…) | RTL | replaced; exact `rand`/`time`/`itoa`/printf behaviour in `src/rtl` |
| seg1351, seg15C9 | 8087 emulator / real-x87 fix-up support | RTL | dropped (native FP, checked) |
| seg122B | Borland VROOMM overlay manager (`_OvrInitEms`, INT 3Fh) | RTL | dropped |
| seg0546 | `CODE\ISTANBUL.C` – `main`, start-up checks | game | decompile |
| seg05DC | `ALTDUZEY\INTRFACE.L` – UI, sample voices | engine | decompile |
| seg09D4 | `ALTDUZEY\MEMMAN.L` – EMS/conventional memory | engine | decompile over a heap backend |
| seg0AC6 | `ALTDUZEY\SYSTEMS.L` – clock, input, blits | engine | decompile |
| seg0C7F | `ALTDUZEY\NGRAPHS.L` – graphics | engine | decompile |
| seg0D42 | `ALTDUZEY\NSES.L` – sound | engine | decompile |
| seg0DB7 | `ALTDUZEY\NKONTROL.L` – keyboard/mouse, 100 Hz clock ISR | engine | decompile; ISR becomes a platform tick |
| seg08D0 | 3D quad build/rotate/clip/rasterise (used by 3DQUICK) | game | decompile |
| asm2599 | hand-written 386: scaled/mirrored blits, palette blend, 2× blending upscaler | game | decompile (bit-exact) |
| seg0DF2 | DDS – DSMI dynamic driver/resource registry, loads `.DSD` | DSMI | replaced |
| seg0DDF, 0E9E, 1134, 1145, 1154 | DSMI helpers, MemAlloc, EMS heap, time base | DSMI | replaced / partly decompiled |
| seg0EAE | DSMI CDI (channel distributor, dispatch into driver) | DSMI | decompile the interface |
| seg0EF6, 105B, 111B | DSMI AMP 2.00 player, AMF loader, module free | DSMI | **decompile** (tempo/effects fidelity) |
| seg10EF | DSMI timer service (INT 08h, PIT ≈100 Hz, divisor 11931) | DSMI | platform timer at an identical rate |
| `SDISB16.DSD` (file) | **MCP software mixer** + SB16 driver | DSMI | **decompile the mixer from the driver file** |
| ovl2700 | `3DQUICK.L` – first-person renderer, FP start-up tables | game | decompile |
| ovl303F | `3DSYS.L` – exploration loop (303F:8524), camp, save/load, options, cheats | game | decompile |
| ovl39E5 | `CHARGEN.L` – character generation, GRP.DAT | game | decompile |
| ovl3C2D | `DEMOLAR.L` – intro, city map, district travel, ending, credits (mode 12h) | game | decompile |
| ovl3FD1 | `FRPOS.L` – shop/event script interpreter (33 opcodes) | game | decompile |
| ovl4595 | `ISOSYS.L` – isometric battle (`iso_battle` 4595:B40A) | game | decompile |
| ovl5313 | `GFL.C` – GFL/GHD video player (RLE/delta codec) | game | decompile |
| ovl5447 | `ALTDUZEY\FILEMAN.L` – RLE/CMP decoders, picture/file loading | engine | decompile |
| ovl5511 | `OGRAPHS.L` – mode set, palettes, screen clears | engine | decompile |
| ovl559A | fatal error / misc | engine | decompile |
| ovl55CC | `OSES.L` – sound helpers | engine | decompile |
| ovl5646, ovl569A | virtual file layer over the FLiB archives + its FILE table | engine | decompile |
| ovl5737 | MSCDEX CD-audio control (device requests) | engine | platform CD-audio replacement |
| ovl5784 | `GRTEXT.L` – text drawing, Turkish case helpers | engine | decompile |

Key hardware facts:

* **Video**: BIOS mode 13h, chained. A 320×204 surface is composed off-screen and copied to
  A000 after vertical retrace; the start address is always 0. **Mode 12h** (640×480×16,
  planar) is used for the credit screens. The DAC is written with `rep outsb` after retrace.
* **Timer**: DSMI hooks INT 08h. The engine's 100 Hz routine adds 10 ms to a clock at 16E0:E74A.
  The AMP player tick is ⌊BPM·2/5⌋ Hz, quantised to 10 ms. GFL video paces itself on that clock.
* **Input**: BIOS `kbhit`/`getch` (INT 09h only traps Ctrl+Alt+Del). Mouse uses INT 33h
  functions 0/3/4/7/8, with a software cursor.
* **INT 00h**: a division by zero yields 0xFFFF and execution continues. This must be reproduced.
* **RNG**: Borland `rand` (seed 16E0:94C4), `random(n)` = inline `(long)rand()*n/0x8000`, and
  `randomize()` in `main`. Borland's `max()` macro evaluates its arguments twice; ISOSYS
  initiative relies on this, so the RNG stream depends on it.

## 3. Strategy

### 3.1 Decompile directly to readable C, then prove equivalence

For each function the work goes:

1. Read the Ghidra draft (`analysis/ghidra_ist/*.c`) **and** the annotated listing
   (`analysis/ghidra_ist/asm/*.asm`). The listing is authoritative; the draft is a hint.
2. Write the readable C in `src/` with names, types and comments. Every function carries
   a header comment giving its original address (`/* IST.EXE ovl4595:1234 */`).
3. Verify it (see §4) before marking the function done in `docs/PROGRESS.md`.

### 3.2 Fidelity rules for the C code

* **Integer widths are explicit.** Borland `int` is 16 bits and `long` is 32 bits.
  Code uses `int16_t`/`uint16_t`/`int32_t` and keeps 16-bit wrap-around, sign
  extension, and the `/`/`%` truncation semantics where the original relies on them.
  `char` is signed in BC++.
* **Floating point.** The original's results come from the 8087 emulator or an x87, with
  80-bit intermediates. Every FP expression is checked for bit-exact results. Where `double`
  differs, the code uses explicit rounding or precomputed tables taken from the original.
* **RNG.** Borland `rand()`/`random()`/`randomize()` are reimplemented exactly
  (LCG `seed = seed*0x015A4E35 + 1`, `(seed>>16)&0x7FFF`).
* **Data tables** in DGROUP are extracted byte-exact by tools into generated C, never hand-typed.
* **Save files and data files** are read and written byte-for-byte with explicit
  little-endian (de)serialisation. Struct layouts are never `memcpy`'d.
* **Timing.** PIT reprogramming and IRQ0 handlers are reproduced by the platform layer
  at the exact original tick rate. Game logic that ran in an ISR runs on the
  same tick schedule.

### 3.3 What gets replaced instead of decompiled

| Original | Replacement |
|---|---|
| Borland RTL (stdio, string, malloc, …) | ISO C library, plus `src/rtl/` for behaviour-sensitive pieces (rand, printf quirks, `itoa`, …) |
| 8087 emulator | native FP, with exactness checks |
| Overlay manager (VROOMM) | nothing – overlays become normal code |
| EMS (`INT 67h`) / XMS | plain heap memory behind the same MEMMAN API |
| VGA hardware (ports, A000h) | `src/platform/` framebuffer + 6-bit DAC palette → SDL3 texture |
| Keyboard/mouse ISR, `INT 33h` | SDL3 events → original scan codes / mouse state |
| DSMI hardware drivers (`.DSD`) | SDL3 audio stream |
| DSMI mixer + AMF player logic | **decompiled** (audio fidelity), output to the SDL3 stream |
| CD-ROM checks | emulated "CD present", data read from the game folder |

## 4. Verification

1. **Function-level differential tests** (`tests/`, Python + Unicorn). The original
   function runs from `build/ist_flat.bin` (with x87 restored) on a synthetic or captured
   memory state; the C function runs on the same inputs through a small shared-library
   harness; outputs and memory side effects are compared. This suits decoders, math,
   rules and RNG.
2. **Whole-game comparison through a scripted DOSBox-X debugger.** DOSBox-X 2026.10's
   `mcp_server` option connects the emulator's debugger to a local listener
   (`tools/dosbox/dbxdebug.py`, a line protocol: `REQ id EXEC <cmd>`). That gives
   breakpoints, register reads/writes, `MEMDUMPBIN` (raw memory to file), EMS handle
   maps, DAC state, port I/O and run-to-retrace on the *real* game. With
   `tools/dosbox/ist.conf` the image loads at paragraph **0x0824**, so a flat address
   `S:O` is runtime `S+0x824:O` for resident code and DGROUP; live DGROUP was confirmed
   to match the static image. Ground truth captured this way includes tables built at
   start-up, game state after scripted input, frames (VGA planes + DAC) and function
   inputs/outputs at breakpoints. The port gets a matching deterministic input-replay
   mode so that state and frames can be diffed at checkpoints.
3. **Data round-trips.** Saves written by the port must be byte-identical to the
   original's for the same game state.

## 5. Repository layout

```
docs/        PLAN.md (module map in §2), FORMATS.md, PROGRESS.md, VERIFY.md, DECOMPILING.md, …
tools/       RE tooling (Python, Ghidra scripts) – see tools/README.md
analysis/    curated analysis outputs (generated drafts under analysis/ghidra_*/ are ignored)
src/game/    decompiled game modules (istanbul.c, s3d_*.c, iso_*.c, frpos*.c, …)
src/engine/  decompiled ALTDUZEY library (fileman, memman, ngraphs, nses, nkontrol, …)
src/dsmi/    decompiled DSMI mixer/player (portable)
src/rtl/     Borland-compatible runtime pieces
src/platform/ SDL3 backend (video, audio, input, timer, files)
tests/       differential tests
```

## 6. Toolchain

| Purpose | Tool |
|---|---|
| Analysis | Python 3.14 + capstone/unicorn, Ghidra 12.2 (JDK 21), `tools/*` |
| Reference runtime | DOSBox-X (`C:\DOSBox-X\dosbox-x.exe`) |
| Windows build | CMake + Ninja + llvm-mingw (clang 23, UCRT) in `.tools/`, SDL3 3.4.18 |
| macOS build | CMake + Ninja + Apple clang, SDL3 from Homebrew – same sources; the app to give away: preset `macos-release` (universal, static SDL3), `tools/package_macos.sh` |
| macOS analysis / oracle | `.tools/venv` (Python), Ghidra in `.tools/`, Homebrew `openjdk@21` and `dosbox-x` – `tools/hostenv.sh` |

## 7. Roadmap

1. **Foundation** – unpacker, segment model, Ghidra pipeline, FPU fix, listings ✔
2. **Inventory** – identify every RTL/DSMI function, name the ALTDUZEY API and the game
   modules; module map in §2, names in `analysis/names.tsv` and `analysis/names/*.md` ✔
   (2026-11-20/21).
3. **Formats** – CMP images, PAL, KOR, RES, RIX blend tables, GFL/GHD video, AMF, PUR/SND,
   FRPOS scripts, SAV/GRP.DAT, with Python viewers/checkers to prove understanding ✔
   (2026-11-23, [`FORMATS.md`](FORMATS.md); `tools/fmtcheck.py` for KOR/RIX/AMF).
4. **Platform + engine** – SDL3 backend, then FILEMAN, MEMMAN, NGRAPHS, NKONTROL, SYSTEMS,
   INTRFACE, NSES. First milestone: intro screens render identically ✔ (2026-11-21).
5. **Game modules** – ISTANBUL → DEMOLAR → 3DQUICK/3DSYS → CHARGEN → FRPOS → ISOSYS → GFL ✔
   (2026-11-21: every function of IST.EXE decompiled).
6. **Audio** – DSMI MCP mixer + AMP player decompiled, compared against DOSBox-X captures ✔
   (bit-exact mixer, identical CDI call streams; sound on in every play-through).
7. **Full-game verification**, then the macOS build ✔ (2026-11-23, [`VERIFY.md`](VERIFY.md),
   [`MACOS.md`](MACOS.md)); function status: `python tools/funcstatus.py`.
