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

## Formats documented in the inventory notes (to be verified with tools)

| Format | Where |
|---|---|
| SAKLA###.SAV (1,524 bytes, incl. stack-garbage quirks) | `analysis/names/gameA.md` |
| Map banks ("AmBk", 15-byte cells), district table 16E0:2E31 | `analysis/names/gameA.md` |
| FRPOS scripts PCn/PCSn (container, +10 text encoding, 33 opcodes) | `analysis/names/gameA.md` |
| Monster/weapon/spell/combatant tables in DGROUP | `analysis/names/gameB.md` |
| AMF modules (DSMI v1, 8–14), DSD driver container ("DS") | `analysis/names/lib.md` |

## TRDOS.DAT – partial

4096 bytes = 256 glyphs × 16 rows, 1 bit per pixel, MSB = leftmost (8×16 VGA-style font).

## PAL – verified (data side)

768 bytes = 256 × (R, G, B), 6-bit VGA DAC values (0–63). Four 48-byte files hold
16-colour partial palettes.

## PUR / SND – partial

Headerless unsigned 8-bit mono PCM (silence = 0x80). The `.PUR` files are effects (196);
`PARA*.SND` are long recordings. The sample rate comes from the code (todo).

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

## Todo

KOR, RIX, AMF (DSMI module), PCn/PCSn (FRPOS scripts), GRP.DAT, SAKLA###.SAV.

## ISOSYS battle state (DGROUP) – partly verified (`src/game/iso*.c`, tests/py/test_iso.py)

The isometric battle (ovl4595 ISOSYS.L) keeps its whole state in DGROUP; nothing is
read from a battle file of its own (the battlefield is built from the 3D map, the monster
group records and GRP.DAT's party records).  All words are little-endian i16.

| where | what |
|---|---|
| A08A + 28h·c, c = 1..28 | unit: +00 hp, +02 vecize points, +04 facing 1..4 (+4 attack frame, +100 death frame), +06 template g, +08 x, +0A y, +0C last target, +0E side (1 party/allies, 2 enemies), +10 charmer, +12 to-hit, +14 dodge, +16..+1E five status slots (duration·100 + id), +20 on-hit vecize, +22 stun turns, +24 was hit, +26 ?. Units 1..24 monsters (group (c-1)/8), 25..28 the party |
| 9F18 + 2Eh·g | template (23 words). g 1..4 party members, 5..7 monster groups.  w1, w3, w9, w11, w16: kill value; w2 damage (hundreds·100 + base: random(h+1) + base); w5 armour; w7/w8 AP cost of attack 1/2 (−100 when > 100); w9 < 10 and w10 < 0: can be stunned |
| 5BC2 + 6·g | group type of template g (footstep sounds; slayer weapons) |
| C2CE + 38h·x + 2·y | occupancy grid 28×28: unit number or 0 |
| C8EE + 38h·x + 2·y | terrain grid: 0 open, 1..9 floor tiles, ≥ 10 walls (picture 2616[t], top t+6); 6 and 8 and ≥ 10 block a step |
| CF0E + 36h·… | mini-map colours |
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
3DSYS keeps the whole map file in an EMS block (record pointer 16E0:E33F) and a pointer to its byte
20 (the cells) at 16E0:B9EC; the script file is the EMS block of 16E0:E34F. All offsets below are
file offsets (header included); the interpreter's PC is the word 16E0:BB98.

**Map cells.** Cell (x, y) is 15 bytes at map byte 20 + (y·width + x)·15. Bytes 11..14 are the event
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
