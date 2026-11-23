# File formats

All multi-byte values are little-endian. Each section notes the status: **verified**
(round-trips or is fully explained by code), **partial**, or **todo**.

## FLiB archive (`*.CKD`) – verified

```
+0x00 char[4]  "FLiB"
+0x04 u32      entry count
+0x08 u32      payload size (informational)
+0x0C entries, back to back:
      +0x00 char[4]  "FiLE"
      +0x04 char[32] name, NUL padded
      +0x24 char[16] name again (8.3)
      +0x34 u8       flags (always 0 = stored)
      +0x35 u32      absolute offset of the data (= entry offset + 0x41)
      +0x39 u32      stored size
      +0x3D u32      original size (always = stored size)
      +0x41 data
trailer: a verbatim copy of all entry headers (count × 0x41 bytes), i.e. a directory
         the game can read from (file size − count×0x41).
```

Tool: `tools/flib.py list|extract`. The trailer is checked against the inline headers.

**How the game reads it** (ovl569A/ovl5646): it reads only the trailing directory. The
header's `+0x08` field is the directory offset, which holds for every CKD. Lookup is a linear,
case-insensitive 32-character name compare across the archives in open order (0gfl, muzik,
sounds, frpos, ikon, ivir, savas, 3d), falling back to a plain DOS file.

## IST.EXE overlay block (FBOV) – verified

Documented in `tools/exeload.py`.

## CMP image – verified (decoder in `tools/cmpview.py`)

```
u16 width, u16 height
RLE stream up to EOF:
  c >  0x80 : run      - (c - 0x80) copies of the next byte
  c <= 0x80 : literal  - the next c bytes (c = 0: no-op)
```
Pixels are 8-bit palette indices, row-major. Decoders: FILEMAN `fm_cmp_decode` 5447:043B
(< 64 KB) and `fm_cmp_decode_huge` 5447:0000 (EMS window / huge pointers).

**Fidelity note:** 11 of 262 files decode to more than `width*height` bytes (+1 byte, or +320
for IVIR/SCW.CMP). The original writes those bytes past the image, into the slack of the
MEMMAN block. The port must allocate with MEMMAN's rounding and keep the overrun:
3D/GOTY0, GOTY1, VIEWPC; IVIR/ACILISA, ACILISB, IREM62, ISTB, RAKS, RPZ, SCW; SAVAS/IROSP1.

## GFL/GHD video – tool-verified (`tools/gflview.py`, player `src/game/gfl.c`)

35 videos in `0GFL.CKD` (`extracted/0GFL/`), each a pair `<name>.GHD` + `<name>.GFL`.
`gflview.py check extracted/0GFL` verifies every statement below on all 35 pairs; the
codec is checked bit for bit against the original decoder (`tests/py/test_gfl.py`).
All numbers little-endian.

**GHD** = header + first second of audio + frame index (file size = `0x310 + abytes + 8*nframes`):

| off | size | field |
|---|---|---|
| 0x000 | 3 | `"GFL"`. The player only checks that the 3 *signed* bytes sum to 0xD9 ("GFL versiyon hatası.") |
| 0x003 | u16 | nframes |
| 0x005 | u8 | fps – 10 in every file |
| 0x006 | u16 | abytes: audio bytes per 1-second chunk – 22050 in every file (8-bit unsigned mono, 22050 Hz, 0x80 = silence); 0 = no audio |
| 0x008 | u32 | 100000 in every file, never read |
| 0x00C | 4 | 0 |
| 0x010 | 0x300 | header palette, 256 × RGB, 6-bit VGA DAC values |
| 0x310 | abytes | audio of second 0 |
| 0x310+abytes | 8·nframes | frame index |

Index entry (8 bytes, packed): `u8 type, u16 vsize, u32 offset, u8 flags`.

* `type` 0: delta frame. 2: frame that carries the next second of audio – every `fps`-th
  frame (0, 10, 20, …); its video is usually a full picture, but not always (SEQ5 has
  2-type frames of 548 bytes). 1: palette-only frame (no video is decoded; `vsize` says 768
  but no video bytes follow) – only the fade-out at the end of SEQ5 (37 frames). The player
  treats every type ≠ 0 as a resync point ("key frame") when it drops frames.
* `offset`: GFL offset of the frame's payload. If `flags & 0x80` a 0x300-byte palette sits
  right **before** it, at `offset - 0x300`.
* `flags`: bit 7 = palette chunk present. Bits 0–6 = music sync: `n = (flags & 0x7F) >> 1`,
  negated if bit 0; the player calls AMP `ampBreakPattern(n)`, then n∓1 … down to 0
  (so `ampBreakPattern(0)` once for every shown frame when sound is on). Every shipped
  file has 0x00 or 0x80.

**GFL** = the payloads in frame order: `[palette 0x300 if flags & 0x80] [audio abytes if
type == 2] [video vsize bytes, none for type 1]`, contiguous:
`offset[i+1] = offset[i] + (type==2 ? abytes : 0) + (type==1 ? 0 : vsize) + (flags[i+1] & 0x80 ? 0x300 : 0)`,
and the last frame ends at the end of the file (for SEQ5's last, palette-only frame
`offset` = file size). 28 of the 35 videos carry palette chunks (31 each in most district
movies, 20 in SEQ4, 58 in SEQ5, 73 in SON); FUN, INTRO, SEQ1–3, UMR-D-IN and UMR-D-OT have none.

**Video codec** (`gfl_decode_frame` 5313:0390). It updates a persistent 320×200 8-bit
buffer (64000 bytes) that holds the previous frame; nothing ever clears it. For each code
byte b:

| b | action |
|---|---|
| `1nnnnnnn` | literal: copy the next `(b & 0x7F) + 1` bytes |
| `01ffnnnn` | skip `L + 1` pixels |
| `00ffnnnn` | fill `L + 3` pixels with the next byte |

`L` = n, 2n + 0x10, 4n + 0x30, 8n + 0x70 for ff = 00, 01, 10, 11, so skips are 1–16,
17–47 step 2, 49–109 step 4, 113–233 step 8 and fills 3–18, 19–49/2, 51–111/4, 115–235/8.
After each code the decoder continues while `src < start + vsize` and `dst < 64000`
(do-while: one code is decoded even for vsize 0; a run can overshoot the end). In every
frame of every file the stream is consumed exactly and ends exactly at pixel 64000.

**Playback** (details in `src/game/gfl.c` and `analysis/names/gameB.md`): frame due =
`(clock_ms - start) * fps / 1000`; palette chunks are set when the frame is shown, a frame
without one restores the header palette (only if a chunk palette was set); the audio of a
type-2 frame k goes to half `(k / fps + 1) & 1` of a looping 2 × abytes sample. Late frames:
a delta frame due after skipped frames is not shown, and streaming jumps to the next
type ≠ 0 frame.

## Formats from the inventory notes – where they are now

The first inventory (`analysis/names/gameA.md`, `gameB.md`, `lib.md`) listed these; each now has
a section here, built from the decompiled code:

| Format | Section |
|---|---|
| SAKLA###.SAV | "SAKLAnnn.SAV (saved game)" – verified |
| Map banks ("AmBk", 15-byte cells) | "FRPOS event scripts PCn / PCSn" – verified (cells, event keys); wall-set bytes 3..6 of the map: "KOR" below |
| FRPOS scripts PCn/PCSn | "FRPOS event scripts PCn / PCSn" – verified |
| Combatant / template tables | "ISOSYS battle state (DGROUP)" – partly verified |
| AMF modules | "AMF – DSMI module" – verified |
| DSD driver container ("DS") | "DSD – DSMI driver" – verified |

Not file formats, and not documented here: the district table 16E0:2E31 (18 × 1Dh: far
pointers to the map name "pcN", the genitive name, four GFL names day-in/night-in/day-out/night-out
at +08..+14, the horizon picture at +18, then a byte at +1C; see `S3D_DISTRICT_TAB` in
`src/game/s3d.h`), and the fields of three DGROUP tables: the monster types 1287 + 2Eh·t
(23 words; only how the battle copies them into the templates is in the ISOSYS section), the
weapons 0E7F + 0Eh·k (7 words; word 0 = the monster type it gets +1 to hit and +2 damage against, `iso_combat.c`) and the
vecizes 007E + 16h·s (11 parameter words, `VECIZE_PARAM` in `iso_spell.c`). The meaning of most
of their fields is only guessed in `analysis/names/gameB.md` and has not been checked.

## TRDOS.DAT – verified (`o559A_load_trdos_font` 559A:01F3, tests/py/test_fatal.py)

4096 bytes = 256 glyphs × 16 rows, 1 bit per pixel, MSB = leftmost: the 8×16 code page 857
(Turkish DOS) text-mode font (e.g. glyph 98h is İ). A plain DOS file next to IST.EXE, opened with
`fopen("TRDOS.DAT", "rb")` (not through the CKD archives). `o559A_load_trdos_font` reads 1000h
bytes into a malloc'd buffer and loads them into the VGA character generator with INT 10h
AX=1100h (BH=10h bytes per glyph, BL=0 block 0, CX=100h glyphs from DX=0, ES:BP = buffer), then
frees the buffer. Called by `ist_main` at start-up and by `o559A_fatal` after switching back to
text mode 3, so the exit and error messages show Turkish letters. A missing file only prints
"Turkce Dos Fontu bulunamadi."; a short file leaves the buffer's old bytes in the rest of the font.

## PAL – verified (data side)

768 bytes = 256 × (R, G, B), 6-bit VGA DAC values (0–63). Four 48-byte files hold
16-colour partial palettes.

## PUR / SND – verified (loader and players; the DEMOLAR callers against DOSBox-X: para00.snd in the story slideshow, docs/VERIFY.md play8; buun.pur in the new-game intro, play3/playq)

Headerless unsigned 8-bit mono PCM (silence = 0x80; the MCP mixer reads byte 0 as −128).
SOUNDS.CKD holds 196 `.PUR` files (effects, voices) and `PARA00.SND`..`PARA09.SND` (narration,
125–489 KB). Nothing in a file gives its rate: every caller passes one.

**Loading** (`oses_load_sample` 55CC:01F3, tests/py/test_sound.py on real files): the file goes
into sample slot n (0..30h; record DE34 + 17h·n = MEMMAN reference, SAMPLEINFO from +4); the
callers here all load into EMS (type 1), in pieces of at most FF00h bytes. With
flags bit 1 the rest continues in slots n+1, n+2 … (up to 30h), each marked 200h in the sound
table but the last; without it only the first piece is loaded. Flags bit 0 stores each piece
reversed (table flag 100h).

**Sound table** 16E0:22DB + 11h·n, n = 0..32 (from INTRFACE's side; `g_snd_name`/`g_snd_rate`
in `src/engine/intrface.h`): `char name[13]; u16 rate; u16 flags`. `ui_load_voice(n, rate, …)`
(05DC:192D) loads slot n on demand and plays it on the last two channels; `iso_play_sfx`,
3DSYS, the door animation (slot = wall set + 0Dh) and FRPOS op 12 (slot 9) pass the table rate.

| n | name | rate | flags (hex) | n | name | rate | flags (hex) |
|---|---|---|---|---|---|---|---|
| 0 | kufurbuf | 21000 | 03 | 16 | kapi1.pur | 11000 | 15 |
| 1 | adim1.pur | 4000 | 37 | 17 | kapi4.pur | 22000 | 15 |
| 2 | olum.pur | 44100 | 23 | 18 | kapi5.pur | 11000 | 15 |
| 3 | vurus.pur | 8000 | 13 | 19 | kapi6.pur | 11000 | 15 |
| 4 | vurama.pur | 7500 | 13 | 20 | kapi7.pur | 10000 | 15 |
| 5 | iska.pur | 6000 | 13 | 21 | merdalt.pur | 32000 | 04 |
| 6 | adim2.pur | 11000 | 37 | 22 | merdust.pur | 32000 | 04 |
| 7 | adim3.pur | 40000 | 37 | 23 | kanat.pur | 44000 | 33 |
| 8 | uzmen.pur | 20000 | 13 | 24 | surt.pur | 30000 | 33 |
| 9 | bhasar.pur | 34000 | 07 | 25–28 | sw1..sw4.pur | 11000 | 23 |
| 10 | bosch.pur | 21000 | 07 | 29–32 | fw1..fw4.pur | 44100 | 23 |
| 11 | adim5.pur | 40000 | 37 |  |  |  |  |
| 12 | adim8.pur | 44000 | 37 |  |  |  |  |
| 13 | viyk.pur | 8500 | 33 |  |  |  |  |
| 14 | kapi1.pur | 11000 | 15 |  |  |  |  |
| 15 | kapi2.pur | 11000 | 15 |  |  |  |  |

Values read from IST.EXE's DGROUP (records 33..48 are empty). Flags: 1 never freed;
2 / 4 select the slot for the voice modes of `ui_init_voices` (mode 0 loads the slots without 4,
mode 1 those without 2, mode 3 those with both); 8 play the continuation pieces too and wait;
10h random pitch, `rate + (rand()·(rate/5) >> 15) − rate/10` (±10 %; the shifted product is
cut to 16 bits); 20h voiced: in battle
`iso_play_sfx` (ovl4595:214E) scales the rate by the speaker's voice class; 100h/200h set at
run time (above).

**Voice class rate** `iso_sfx_rate(who, base)` (ovl4595:255B, tests/py/test_iso.py):
`r · base / 7500` (u32, capped at FFFFh), r from the byte table 16E0:1E8D + who (monster type):
class 0/2 6000, 1 7500, 3 7000, 4 6500, 5 4500, 6 20000, 7 8500, 9 44000, others 22000; the party
(who > 40) 7500. The vecize voices (`vecizeNN.pur`, `fecizeNN.pur`, `cevapNN.pur`,
`fevapNN.pur`) are loaded into slot 21h and played at `iso_sfx_rate(speaker, 44100)`
(`iso_speak_vecize`, tests/py/test_iso_d.py test_sound_speak); `ui_play_voice` plays the
continuation pieces at the same rate.

**Others** (not covered by tests yet): `PARA*.SND` - `demo_story_scene` (3C2D:1108) loads
`paraNN.snd` for each slide into slot 21h with flags 2 (EMS pieces in slots 21h..) and plays it at
4E52h = 20050 Hz (`ui_load_voice(21h, 20050, 0, 0)`, then `ui_play_voice`); the last digit
(byte 5 of the name) is incremented per slide and never reset. `buun.pur` (new-game intro, `demo_load_sounds`) goes into
slot 2Fh with loop end 46E3h and plays at 2134h = 8500 Hz. The streaming player `snd_load_sound`
(0D42:01F0, 60000-byte ring in slot 30h) has no caller in the listing.

## RES – verified

Plain text, CRLF-separated file names. These are manifests of the resources an archive
preloads ("EMS statik bloklar"), e.g. `3D.RES`, `3DEM.RES`, `SAVDEM.RES`, `IDEM.RES`.

## THANX.DOC / DEBUG.LOG

cp857 text: credits, and a leftover developer heap/debug log from ALTDUZEY.

## DSD – DSMI driver ("DS" file) – verified (`DDSGetDriverInfo`/`DDSLoadDriver`, src/dsmi/dds.c)

`SDISB16.DSD`, `SDIGUS16.DSD`, `SDIPAS16.DSD`, `SDIWSS16.DSD`: a DOS MZ executable with the
signature changed to `DS` and two header fields reused. Loaded by `DDSLoadDriver`
(0DF2:0611) into the far heap and relocated like an EXE; it is not a DOS program.
28-byte header, all fields little-endian u16 unless noted:

| Off | MZ name | Meaning for DDS |
|---|---|---|
| 00 | e_magic | `"DS"` (5344h); anything else: error 4 |
| 02 | e_cblp | bytes in the last 512-byte page |
| 04 | e_cp | pages |
| 06 | e_crlc | relocation count (0 makes the load fail: farcalloc(0) = NULL, error 12) |
| 08 | e_cparhdr | header size in paragraphs (= file offset of the load module / 16) |
| 0A | e_minalloc | extra paragraphs allocated after the image |
| 0C | e_maxalloc | **device id** (SB16 64h, WSS 65h, PAS 66h, GUS C8h); `DDSGetDriverInfo` word 0 |
| 0E, 10 | e_ss, e_sp | 0 (unused) |
| 12 | e_csum | **version**, 0100h in all four; `DDSGetDriverInfo` word 2 |
| 14 | e_ip, e_cs (u32) | entry point CS:IP relative to the load segment (0000:0000 in all four) |
| 18 | e_lfarlc | file offset of the relocation table (20h) |
| 1A | e_ovno | **flags**: bit 0 must be set ("loadable"), else error 4; `DDSGetDriverInfo` word 1 |

Load (16-bit arithmetic as in the original):
`image = ((e_cp - 1) * 512 + e_cblp - e_cparhdr * 16 + 32) & 0xFFFF` bytes are read from
offset `e_cparhdr * 16` (32 more than the load module - the read is just short);
the block is `image + e_minalloc * 16 + 32` bytes and the image starts at its first
paragraph boundary. Relocations are `e_crlc` × {u16 off, u16 seg}; the word at
`image + ((seg * 16 + off) & 0xFFFF)` gets the load segment added.

Entry protocol (far pascal `long entry(long cmd, void far *arg, long arg2)`, result DX:AX):
`cmd 1` with `arg = DDSAccessFunction` - the driver looks up the DSMI services it needs
by name; AX = 0 ok (else DDS error 5), DX = device id (a loaded id again: error 8).
`cmd 2` (unload, `arg = 0`): non-zero = refused (DDS returns 11 but unloads anyway).

| File | Size | e_cp/e_cblp | relocs | hdr paras | minalloc | id |
|---|---|---|---|---|---|---|
| SDISB16.DSD | 29581 | 3Ah/18Dh | 13Ah | 51h | 12h | 64h |
| SDIWSS16.DSD | 25107 | 32h/13h | BCh | 31h | 12h | 65h |
| SDIPAS16.DSD | 21341 | 2Ah/15Dh | 88h | 24h | 11h | 66h |
| SDIGUS16.DSD | 13147 | 1Ah/15Bh | AEh | 2Eh | 2Fh | C8h |

### DSD contents (SDISB16.DSD) – verified (`tools/dsdload.py`, runs `dsd_main` in Unicorn)

Two segments: code at image +0 (`JMP FAR` to `dsd_main` 147Ch at offset 0) and data
segment 03D3h at image +3D30h; all 314 relocations patch one of these two segment
values. The assembler left the **procedure names** in the code: each routine is
preceded by its ASCII name and a length byte (`...mcpOpenDDS` `0A`, code follows);
`tools/dsdload.py` lists them (mcpOpenDDS, scanLong, detectDevices, cmdSB, resetDSP,
SB16IRQHandler, initDevice, startOutput, getBufPos, stopOutput, closeDevice, dsd_main,
DMAInit…DMAStatus). The MCP mixer (image 1675h–36EFh) has no names.

`dsd_main(1)` imports through DDSAccessFunction: DDSAccessResource, DDSRegisterResource,
DDSRegisterFunction, DDSRemoveFunction, DDSRemoveResource, MemAlloc, MemCalloc,
MemRealloc, MemFree, cdiRegister, SetVector, GetEnvString (the result picks a flag at
d:31FE), then via mcpOpenDDS GetDSMITime, MCPSampleRealAddress,
MCPEnable/DisableVirtualSamples and SDIGetBufferPosition. It exports SDIDetectDevices
(01DE), SDIQueryDeviceCount (03F6), SDIQueryDevice (0406), SDIInitDevice (08F6),
SDIStartOutput (105C), SDIGetBufferPosition (1382), SDIStopOutput (13A0), SDICloseDevice
(140A) and the resource `MCPOutputBuffer` (d:087C, 12h bytes: u16 seg, u32 off, u32
linear, u16 size, u16 size, u32 rate, u8 flags). SDIInitDevice calls `cdiRegister` with
the CDI table at d:088F (26 far pointers, see docs/DSMI_MCP.md). `dsd_main(2)` removes
the SDI functions and returns its third argument; any other command returns 1.

### SES.CFG – verified (oses_init 55CC:0000, SDISB16 initDevice)

107 bytes: `char[13]` driver file name (`SDISB16.DSD`), the 5Ch-byte **SDI config** record
copied to DGROUP E7EC and passed to SDIInitDevice, then `u16` at DGROUP 8DDA (2).
SDI config, u32 fields: +00 card type (1 SB, 2 SB Pro, 6 SB16; others: init fails),
+04 flags (bit 7: IST.EXE adds its 40 Hz cdiPoll timer routine), +08 I/O port, +0C DMA
channel, +14 IRQ, +10/+18..+20 stale far pointers of the setup program, +40
amplification (0 none, 1 auto, 2 low, 3 medium, 4 high), +44 stereo/mode option
(0 = stereo for SB Pro/SB16; SB: rate index), +48 rate index (SB16: 44100, 32000, 22050,
16000), +58 far pointer to a caller-supplied output buffer (0). Shipped file: SB16 at
220h, DMA 5, IRQ 7, no amplification, stereo, 22050 Hz → 16-bit stereo 22050 Hz, ring
of 8016 bytes.

## GRP.DAT / GRPHAZIR.DAT (party file) – verified (`src/game/chargen.c`, tests/py/test_chargen.py)

Written by `cg_save_party` (39E5:182E) at the end of character generation, read by
`cg_load_party` (39E5:1932); GRPHAZIR.DAT is the ready-made party of "çabuk başla".
664 bytes, no header, little-endian, members 1..4 (record 0 of DGROUP is not stored):

| file off | size | content |
|---|---|---|
| 74·(i-1) | 37 | name, NUL-terminated (PartyName +00, DGROUP 9C26+94h·i) |
| 74·(i-1)+37 | 37 | aphorism flags '1'/'0' ×36 + NUL (PartyName +6F); written as "1000…0" |
| 296+92·(i-1) | 92 | PartyStats record verbatim (DGROUP 99A6+5Ch·i), layout in `src/game/chargen.h` |

Stats words used at creation: +02 race 1..8, +04 gender 1/2, +0A..+20 the 12 attributes,
+22, +24 height, +26 weight, +28, +2C race special, +38 = +0A and +3A (health) = +0C set
just before writing, +42 portrait 1..16.  On load the race name (+4A of the name record)
is filled from the 11-byte table at 16E0:2638.  Bytes the game never set are written as
they are in DGROUP (zero in a new game).

## KOR – coordinate tables – verified (`tools/fmtcheck.py kor`: all 59 files)

Two unrelated layouts share the extension ("Kordinat dosyası"). The CKD lookup falls back to a
plain DOS file, which is how `orig/BOSCH.KOR` is found.

### 3D/*.KOR – wall-set texture rectangles (`q3d_load_kor` 2700:1E06, tests/py/test_q3dview.py test_load_kor)

On entering a district `s3d_load_district` takes the wall-set number N of each of the four map
bytes 3..6 (kept at 16E0:9F10 + 2i; 0 = none; the name tables go up to 9 = merdust/merdalt) and
loads `duvarN.kor` into KOR slot i and `kapiN.kor` into slot 4 + i (16E0:B3E8 + 0Eh·slot).
Before each call it presets the slot to version 1, n 6, {hdr0, w, h} = {0, 200, 200} for walls
and {0EBh, 200, 200} for doors. A missing file is the normal case: the slot then draws with the
default piece table (16E0:B72A, 37 × 12h bytes).

| off | size | field |
|---|---|---|
| 0 | i16 | version; anything but 1 is fatal (line 1F7h, "Kordinat dosyasi versiyonu desteklenmiyor") |
| 2 | u16 | n = header bytes that follow; read unchecked into the slot from its +4 (n > 6 would overwrite the slot's piece pointer) |
| 4 | n | i16 hdr0, i16 w, i16 h: w × h = the view size the records were made for |
| 4+n | 37 × 14 | one record per wall piece i: i16 tx, ty, tw, th, dx, dy, flags |

The records are read only if `filelength − 4 > n`; then the slot gets a copy of the default
table in which piece i takes the source rectangle (tx, ty, tw, th), the size w × h = tw × th, the
offset (dx, dy) added to its position and `flags` (bit 0 mirrored, bit 1 scaled), clipped to the
view. If the view (100×100 low detail, 200×200) differs from the header's w × h, flag 2 is set on
every piece and size and offset are scaled by the default piece's w/tw and h/th (quirks: see
`q3d_load_kor`). Of the header only hdr0 is used later: wall pieces of a slot with hdr0 = 0 are
drawn with `q3d_draw_strip_a`, otherwise with `q3d_draw_strip_b` (side pieces take their texture
0EBh pixels further right when facing north or west; walls on sides 2/3 facing the viewer borrow
the opposite piece's rectangle, mirrored); the door animation's mirroring for facing 2/3 also
tests hdr0 ≠ 0 (`door_mirror` in `src/game/q3dview_anim.c`).

Shipped files, all with version 1, n 6 and header {0, 200, 200}:

| file | size | content |
|---|---|---|
| DUVAR4, DUVAR8 | 528 | header + 37 records (74 pieces: flags 0 ×41, 1 ×16, 2 ×17) |
| KAPI4, KAPI5, KAPI6, KAPI8 | 10 | header only: the slot keeps the default table, but hdr0 becomes 0 instead of the door preset 0EBh |

### SAVAS/*.KOR, BOSCH.KOR – sprite frame boxes (`iso_draw_sprite` ovl4595:0387, tests/py/test_iso.py, tests/py/test_chargen.py)

72 bytes = 6 records × 6 i16: `x1, y1, x2, y2` (inclusive box of the frame in the sprite sheet
`<name>.CMP`), `ax, ay` (anchor). The sheet pixel (ax, ay) is drawn at (x − 4, y − 8) for the
unit position (x, y); mirrored frames keep that (the box is flipped around it). Unit frame
f = 1..8 (facing 1..4, +4 = attack; the death frame +100 only computes the position) maps to
record `f − 1` for f = 1..3, `f − 2` for f = 5..7, and records 1 / 4 mirrored for f = 4 / 8.
The HP bar goes at the box centre − 13 (0Dh) on both axes.

ISOSYS (`iso_load_battle_gfx` ovl4595:0570) loads the seven sprites of a battle (party slots
0Bh..0Eh from the portrait names, or `karafa` for all four with event flag 8Dh set and 8Fh
clear; monster groups 0Fh..11h from the short names 16E0:2185 + 9·type, `bosch` when that name
is empty) and each one's `.KOR` into a 1F8h-byte block (16E0:E337) at 48h·(slot − 0Bh); CHARGEN (`cg_load_char_graphics` 39E5:011F) loads one into a
48h-byte block. The loader reads whole files: a larger `.KOR` would overrun the next sprite's
record. Shipped: 52 files in SAVAS.CKD and `orig/BOSCH.KOR` (picture `IVIR/BOSCH.CMP`), all 72
bytes, every box inside its CMP. The anchor of record 3 (attack, facing 1) lies outside its box
in 19 of them (BOLONEZ, CEHALET, …). SAVAS pictures without a `.KOR` are not sprites: ANAKES,
DUVARIC1..9/A/B, TABLETUR, YERICON.

## RIX – blend table – verified (`blend_lookup` in src/engine/blit386.c, tests/py/test_blit386.py, tests/py/test_graphics.py test_rix; `tools/fmtcheck.py rix`)

32640 bytes = 256·255/2, no header: for every pair of different colours a ≠ b, the colour of
their 50 % mix. Packed upper triangle, row by row: with lo = min(a, b), hi = max(a, b) the entry
is at `lo·(509 − lo)/2 − 1 + hi` (16-bit), so row lo holds (lo, lo+1) … (lo, 255): (0, 1) at 0,
(0, 255) at 254, (1, 2) at 255, (254, 255) at 32639. blend(a, a) = a without a lookup.

Users (hand-written 386 code, 2599): `asm_tint_rect` 2599:0440 (every pixel p ≠ c of a
rectangle becomes blend(p, c); `gr_draw_strip` 0C7F:0A51 "RenkFiltre", fatal 70h when less than
7F80h bytes of the table are mapped), `asm_blend_span` 2599:04AD (`gr_blend3` 0C7F:08FC, the
cross-fades of `demo_show_picture`) and `asm_upscale_view2x` 2599:0500 (the in-between pixels of
the doubled low-detail 3D view). `sys_load_rix` 0AC6:1AA0 loads the file into an EMS block of
C001h bytes (16E0:E52F; the name is kept at E570 and a second load of the same name does
nothing). After palette changes (callers in 3DSYS, ISOSYS and FRPOS) `sys_build_aa_table`
0AC6:1A13 rewrites row 0 (pairs (0, i)) from the target palette 16E0:E5EF.

| file | loaded by |
|---|---|
| XD.RIX | `s3d_load_district`; `demo_story_scene` when it ends |
| XDINTRO.RIX | `ist_main` at start-up, `demo_intro_menu` (after acilisa.pal) |
| XDEMO.RIX | `demo_story_scene` |
| XDSON.RIX | `demo_ending` |

Content (checked against the palettes, not needed by the code): an entry is the palette colour
nearest to ((Ra + Rb) >> 1, (Ga + Gb) >> 1, (Ba + Bb) >> 1) by the sum of absolute differences,
the first one on a tie. XDSON.RIX follows that rule over SOOON.PAL in all 32640 entries, XD.RIX
over PAL3D.PAL (= orig/SONPAL.PAL) in all but 255, which all pair a colour with 254 or 255.
XDINTRO.RIX and XDEMO.RIX match ACILISA.PAL / DEMO1.PAL in only 15232 / 9923 entries: the palettes
they were made from are not identified. `sys_build_aa_table` uses the same average and distance
(`sys_find_color` 0AC6:195E) but lets the last minimum win.

## AMF – DSMI module – verified (`ampLoadAMF` 105B:008A in src/dsmi/amfload.c, tests/py/test_amp.py; `tools/fmtcheck.py amf`)

The music of MUZIK.CKD: 15 modules, all version 14. `ampLoadModule` (105B:07F3) checks "AMF",
copies 20 bytes of the name, forces 4 channels with the default pans for v1/v8, then
`ampLoadAMF` reads from the start of the file. Layout as the loader reads it (v14 offsets):

| off | size | field |
|---|---|---|
| 00 | 4 | `"AMF"` + version byte: 1, 8, 9, 10..14 accepted, else −3 |
| 04 | 32 | song name |
| 24 | u8 | instrument count |
| 25 | u8 | order count (MODULE "patternCount") |
| 26 | u16 | logical track count |
| 28 | u8 | channels (v ≥ 9) |
| 29 | 32 × i8 | channel pan −63..63, 100 = surround (v ≥ 12; v 9..11: 16 bytes, and below v11 replaced by the default table 16E0:85AA) |
| 49 | u8, u8 | BPM, speed (v ≥ 13; else 125, 6) |
| 4B | | orders: order count × { u16 rows (v14; else 64), channels × u16 track ref } |
| | | instruments: count × 65 bytes (v < 10: 59) |
| | | track map: track count × u16 stored-track number, 1-based |
| | | stored tracks: max(map) × { u16 count, u8 type, count × {u8 row, u8 cmd, u8 arg} } (v1: 3 more bytes each) |
| | | samples: raw 8-bit, by sample number; nothing follows |

A track ref r points at logical track r (1-based, 0 = none); logical track j plays the stored
track the map names (several may share one; count 0 = no data). Instrument (v ≥ 10):

| off | size | field |
|---|---|---|
| 00 | u8 | type |
| 01 | 32 | name |
| 21 | 13 | file name |
| 2E | u32 | sample number, 1-based (0 = none) |
| 32 | u32 | length (the loader uses the low word) |
| 36 | u16 | rate of C (c2spd) |
| 38 | u8 | volume 0..64 |
| 39 | u32, u32 | loop start, loop end (0 = no loop) |

v < 10: u16 length, u16 rate, u8 volume, u16 loop start, u16 loop end (FFFFh: no loop) from +32.
Sample k's length is taken from the instrument that first brings the sample count up to k; each
sample gets a block of length + 16 bytes, and with DDS's "EMSAlloc"/"EMSCopyTo" exports a sample
over 400h bytes moves to EMS.

Track entries are read by `ampReadTrackRow` (0EF6:0955): all entries with row ≤ the current
row; row FFh ends the track. cmd < 80h is a note (0 = stop, 7Fh = keep the note) with arg = volume
(FFh = keep); cmd 80h..97h are commands (80h instrument, 81h speed, 82h volume slide, 83h volume,
84h portamento, 86h tone portamento, 88h arpeggio, 89h vibrato, 8Ch pattern break, 8Dh position
jump, 94h note cut, 95h tempo, …; see `src/dsmi/amp.c`).

Shipped files: header 75 bytes, 31 instruments each, of which 3..13 are used (type 1, sample
numbers 1, 2, 3 … in instrument order, unused ones in between with type 0, number 0, length 0);
rate 8368 (89 instruments) or 8252 (2); 6 channels (ISTANBUL.AMF 8) panned −63, 63, 63, −63, …,
the rest 0; header tempo 125/6 (effect 95h sets 120 or 144 in 11 places); every order 64 rows;
6..108 logical over 6..89 stored tracks, all of type 0, 496 ending in `FF FF FF` and 4 empty.
Commands used: 80h, 81h, 82h, 83h, 84h, 86h, 88h, 89h, 8Ch, 8Dh, 94h, 95h. Each file ends
exactly after its last sample.

## ISOSYS battle state (DGROUP) – partly verified (`src/game/iso*.c`, tests/py/test_iso.py, test_iso_b/c/d.py)

The isometric battle (ovl4595 ISOSYS.L) keeps its whole state in DGROUP; nothing is
read from a battle file of its own (the battlefield is built from the 3D map, the monster
group records and GRP.DAT's party records).  All words are little-endian i16.

| where | what |
|---|---|
| A08A + 28h·c, c = 1..28 | unit: +00 hp, +02 vecize points, +04 facing 1..4 (+4 attack frame, +100 death frame), +06 template g, +08 x, +0A y, +0C last target, +0E side (1 party/allies, 2 enemies), +10 charmer, +12 to-hit, +14 dodge, +16..+1E five status slots (duration·100 + id), +20 on-hit vecize, +22 stun turns, +24 was hit, +26 ?. Units 1..24 monsters (group (c-1)/8), 25..28 the party |
| 9F18 + 2Eh·g | template (23 words). g 1..4 party members, 5..7 monster groups.  w1, w3, w9, w11, w16: kill value; w2 damage (hundreds·100 + base: random(h+1) + base); w5 armour; w7/w8 AP cost of attack 1/2 (−100 when > 100); w9 < 10 and w10 < 0: can be stunned |
| 1287 + 2Eh·t | monster type t (23 words). The battle set-up (`iso_battle.c`) fills template 4 + i of group i (type at 5BDA + 6i; above 100: an allied group, 100 is subtracted) with words 2..19 of its type, word 1 = w1 + difficulty (option BB39)·w0, then adds 1 to word 3; an empty group gets words 1..19 = 0 |
| 5BC2 + 6·g | group type of template g (footstep sounds; slayer weapons) |
| C2CE + 38h·x + 2·y | occupancy grid 28×28: unit number or 0 |
| C8EE + 38h·x + 2·y | terrain grid: 0 open, 1..9 floor tiles, ≥ 10 walls (picture 2616[t], top t+6); 6 and 8 and ≥ 10 block a step |
| CF0E + 36h·x + 2·y | mini-map colours, 27×27 |
| 99A6 + 5Ch·p | party record (GRP.DAT): +06 practice, +08 experience, +0C..+1A stats grown by iso_xp_progress, +3A+2k weapon in hand k |
| 9902 + 22h·p | growth marks: counter values at the last growth of each stat |
| 9B7C + 24h·p | race words: +16 favourite enemy group, +18 hated group, +02..+12 growth modifiers |
| 2632 + 2·side | "side has a living unit"; 632E = 1 when one side is gone |
| D4C0 + 2·i / D4FA + 2·c | initiative order / rolls |
| D54A, D54C | round, AP of the actor; D5DE actor, D56E/D56C attacker/defender, D576/D574 animated unit / kind (1 turn, 2 death, 3/4 hit, 5 talk, 6 miss) |
| D57A/D578/D57C | target cursor x, y, on; D580/D57E view origin (0..20) |
| 6393 + 0Dh·i | ten exclamations shown on a miss ("random(10)+1" → 631A, copied to A510) |

## SAKLAnnn.SAV (saved game) – verified (`src/game/s3d_save.c`, tests/py/test_s3d_save.py; orig/SAKLA001.SAV)

1524 bytes, written by s3d_write_save (303F:3DD0) as 22 raw DGROUP blocks, read back in the same
order by s3d_read_save (303F:3F95). Little-endian words; party records as in GRP.DAT above.

| offset | size | content (DGROUP) |
|---|---|---|
| 0 | 60 | description typed in the save menu, NUL-terminated; the bytes after the NUL are whatever the caller's buffer held (the loader skips all 60) |
| 60 | 2 | length of the next block = 0020h (the loader reads that many bytes) |
| 62 | 32 | B9CA: 256 FRPOS event flags (flag n = byte n/8, bit n%8) |
| 94 | 460 | 99A6: 5 × 5Ch character stat records (record 0 unused) |
| 554 | 740 | 9C26: 5 × 94h name / character records |
| 1294 | 170 | 98FC: 5 × 22h per-character table |
| 1464 | 7 | BB34: options (animations, battle speed, game music, battle music, sound effects, difficulty, big view) |
| 1471 | 2 | 3D5E: state bits (written, skipped by the loader, which recomputes them) |
| 1473 | 2+2+1 | B9F3 x, B9F1 y, B9F0 facing (0 north, 1 east, ...) |
| 1478 | 2 | BB6B: district 0..17 |
| 1480 | 2 | BB4D: music track |
| 1482 | 8 | BB6D: 4 words, BB73 = money |
| 1490 | 2+2+2 | 3D64 minute, 3D66 hour, 3D68 day (0..359) |
| 1496 | 11 | BBB7 |
| 1507 | 11 | BBAC |
| 1518 | 2+2 | BB8A, BB88: city map marker x, y |
| 1522 | 2 | BBAB: the doctor's fee byte written as a word (the second byte repeats BBAC[0]). A complete save keeps its fee on loading (the re-roll random(100)+101 only runs when the last read came up short: play5 loads a save with fee 106 and the original asks 106, docs/VERIFY.md); a new fee is rolled on arriving in a district |

After reading, the loader loads the portraits "s" + name (16E0:269B + 0Dh × record word +42h) into
pictures 47h-4Ah, loads the district and enters the 3D view; the view size comes from the big-view option.

## FRPOS event scripts PCn / PCSn – verified (`src/game/frpos*.c`, `tools/frposdis.py`, tests/py/test_frpos.py)

Every district n (1..18) has a map **PCn** and a script bank **PCSn** in FRPOS.CKD. Both are AMOS
banks: 20-byte header `"AmBk"`, BE16 bank number, BE16 flags, BE32 `80000000h | length`, `"Data    "`.
3DSYS keeps the whole map file in an EMS block (record pointer 16E0:E33F) and a pointer to the
cells, file byte 2Bh, at 16E0:B9EC (s3d_load_district locks byte 14h first to read the map header -
width, height, start ... - and then 2Bh, 303F s3d_district.c); the script file is the EMS block of 16E0:E34F. All offsets below are
file offsets (header included); the interpreter's PC is the word 16E0:BB98.

**Map cells.** Cell (x, y) is 15 bytes at file byte 2Bh + (y·width + x)·15 (byte 14h is the width, 15h the height). Bytes 11..14 are the event
key: each byte (≤ 20h, else fatal 51h) printed `%d` with a leading 0 below 10, giving 8 characters.
"00000000" = no event. Fixed keys (trigger 2 only): "31313127" doctor (for the BBAB fee a member whose health is −5 or lower gets health 1; anybody
else is "not dead, pick another"; too little money: "bu para yetmez!"), "31313128" food (random(5)+2 per living member, clears hunger word 11h),
"31313129" weapon shop (item exchange mode 1, icon 2Bh), "31313130" goods shop (mode 2, icon 2Ch),
"31313131" sets 3D5E bit 2 (any other key clears it). Otherwise the event number is, in 8-bit
arithmetic, `(b13 − m27)·32 − sign(b13 − m27) + b14 − m28` with m27/m28 = map file bytes 27h/28h.
fr_run_event(cell, trigger) is called by 3DSYS with trigger 1 (entering), 3 (leaving), 2 (search/use).

**Script bank.** At 14h: BE16 event count (event ≥ count is fatal 53h/54h), then the events, each
`[BE16 len][body]`. Body: BE16 command budget (→ BB93: commands that may run, decremented per
command and by skips, shared by the sections), BE16 offsets (from the body start) of the trigger 2
section, the trigger 3 section and a trailer; the trigger 1 section starts at body + 8. A section is
`[12 + trigger][flags → BB92]` and its commands (a wrong first byte prints "..baslik:%d Tarz:%d..",
then fatal 56h). The trailer is always `04 00`. Strings are `[len][len bytes]`, each byte stored 10
lower (text 0ACh+10 = 0B6h is the line break of the message font). The interpreter runs commands
while `BB93-- != 0` and no command stopped it; at the end it restores the panel and calls
s3d_redraw_view(B9C8). It returns 1 only after a teleport (op 11). ACC = 16E0:BB9E (also the
yes/no result of ui_dialog_2198), WHO = BB9C, the 256 flags are bits of B9CA (flag n: byte n/8,
bit n%8; saved in the .SAV).

| op | operands | effect |
|---|---|---|
| 1 | t, a, n | t=1: if flag a is clear skip n commands; t=2: if ACC ≠ a skip n; other t fatal 57h |
| 2 | str | clear panel, BBAA=0, message; unless the next opcode is 5, wait lines·100+140 |
| 3 | b | portrait/picture "ikon<b>.cmp" (+ .pal) in the view; B9C8 = 1 |
| 4 | – | clear panel, stop |
| 5 | l1, l2, s1, s2 | yes/no question (ui_dialog_2198) → ACC (0 = first) |
| 6 | n | skip n commands |
| 7 | f | set flag f |
| 8 | x, t1, c1, t2, c2, t3, c3 | battle with enemy groups (type, count) 1..3 at 5BDA/5BDC + 6i; all zero = three random groups (type from map byte 1Ch + random(8), count random(8)+1); after ISOSYS: district reloaded, battle tables A0B0/9F46 cleared; lost (632E = 2) or nobody alive → game over, stop, cancel the move |
| 9 | a, b, c, W1, W2, W3 | treasure: currencies BBA2/4/6 (added to BB6F/BB71/BB73), items in BB57.. (BE16 words), item exchange screen mode 0 |
| 10 | s1, s2 | one of the two messages (random(2) ? s1 : s2); wait as op 2 (lines·100+100) |
| 11 | d, BE16 pos, f | teleport: d < 100: district d−1 (reloaded with music and fades if it changes), y = pos / width, x = pos % width − 1, facing f % 4; d = 100: leave the district dialog; then redraw, B9C8 = 1, cancel the move, stop, return 1 |
| 12 | who, w, p | stat word w := w·p/100 (16-bit) for members 1..4 (who 0) or WHO (who 5, also BA89+11h·WHO = 1); for w = 1Dh (health) computed on health + 5; p < 100 on health plays voice 9 |
| 13, 14, 15 | b | nothing (never used) |
| 16 | – | cancel the move (BBA8 = 1), stop |
| 17 | c, str | member c speaks the text (0 = random(4)+1, 5 = WHO; WHO > 4 fatal 58h) |
| 18 | – | ACC = 3D5E bit 0 ? 0 : 1 |
| 19 | b | film: CD check when 3D5C = 1 (Esc gives up), fade out (b = 2 keeps the music), gfl_play(16E0:2D6E + 13·b), district reloaded |
| 20 | n | debug printf + getch, ACC = random(n)+1 (never used) |
| 21 | 6 × str | menu of six choices (fr_menu: click or first letter) → ACC 1..6 (never used) |
| 22 | w, kind, v | WHO := first member whose word w is < v (kind 0), > v (1), = v (other); none: stop |
| 23 | kind, w | WHO := member with the lowest (kind 0) / highest word w (fr_find_character) |
| 24 | – | the player picks a living member (F1-F4 / portrait) → WHO |
| 25 | m | music m (BB4D) with fades; m = 1 also resets 3D5E to 20h and the daylight fade; respects the music options BB36/BB38 |
| 26 | m | m = FFh: all money 0 first; then if m ≤ BB73: pay m, ACC = 1, else ACC = 0 |
| 27 | 40 × str | story scene 11 (demo_story_scene), frees the map block; the 40 strings are skipped unread |
| 28 | – | teach an aphorism (fr_teach_aphorism) |
| 29 | x, BE16 item | remove item word `item` from every inventory (and the hand slots pointing at it) |
| 30 | l, n, str | password: the player types up to l characters on the panel; unless it equals str (both lower-cased, accents stripped) skip n |
| 31 | n | debug printf + getch, wait n·50 (never used) |
| 32 | – | put the party back on its previous cell (BB4F/BB51), redraw, stop |
| 33 | – | leave-district dialog (s3d_leave_district), stop |

Other opcodes are fatal 5Dh. fr_skip_commands steps over commands with the operand sizes above
(op 30 skips `l + 1` bytes after its count byte: the `n` byte and the string); an unknown opcode there
is fatal 61h/62h. With 16E0:5C0B > 0 (0 in the game) every command prints "Komut no:%d" and waits
for a key; > 1 adds flag/ACC/skip traces. Census of the 18 shipped scripts (266 events): every
opcode except 13, 14, 15, 20, 21 and 31 is used.

**Item exchange screen** (fr_item_exchange, mode 0 treasure / 1 weapons / 2 goods). The shop stock
(10 slots, words BB57..) of mode 1/2 is kept per district in the bytes BBB8../BBAD.., byte BBB7 /
BBAC being its price base (random on the first visit: 4..11 items, ids random(31)+1 resp.
random(10)+21h). Buying: 2·price(id) + base − trade-in, where price is the byte table 16E0:1041 + id
and the trade-in of the inventory item swapped out is 2·price + 5 − random(10) − 1 (at least 1);
selling (S, zone 28) pays 2·price + 5 − random(10) + 100·(item/100) − 1 (key) or + 1 (mouse).
B / zone 27 changes the other currencies: BB6F·10 + random(9) and BB71·5 + random(2) kIvrIk.
