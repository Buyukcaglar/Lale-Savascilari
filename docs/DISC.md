# The CD (redump.org 91232), its dump and the simulated image

Reference: <http://redump.org/disc/91232/> (cue, MD5, SHA-1 and SFV files fetched 2026-11-20).

| Field | Value |
|---|---|
| Title | İstanbul Efsaneleri: Lale Savaşçıları: Birinci Bölüm (Turkey) |
| Edition | Aktüel Haftalık Kent Rehberi Time Out (magazine cover disc) |
| Ring / mastering | `9701056 RAKS` (1997), write offset 0, 0 errors |
| PVD dates | all unset (`'0'` × 16, GMT 0) |

| # | Type | Pregap | Length | Sectors | Bytes | CRC-32 | MD5 |
|---|---|---|---|---|---|---|---|
| 1 | Data, MODE1/2352 | 00:00:00 | 25:48:70 | 116170 | 273231840 | 514b4fe9 | 77363cfffa75e795b1877dcfa6d63129 |
| 2 | Audio | 00:00:00 | 02:02:18 | 9168 | 21563136 | 92e65f9b | 42a49f4e7d507533a61d7fcb22f19dbf |
| 3 | Audio | 00:00:00 | 03:26:16 | 15466 | 36376032 | 51036dff | a3c9d7ac93443cff1b7e6bbddc297644 |
| | Total | | 31:17:29 | 140804 | 331171008 | c9767886 | |

SHA-1: track 1 `ee8c6070624f36b6ff40f3bf109fe08f72202a70`, track 2
`e2923e2c9c2a08185e7f4fdabedaa020cd0a18dc`, track 3 `f70cdd125e3f52231a83ec2e401057550ed193f9`.

## The dump (`_references/cd`)

Made with redumper from the user's disc; `tools/cddump.py` analyses it, the port imports it
([INSTALL.md](INSTALL.md)).

* **Track 1** is an ISO 9660 volume `ISTANBUL` of 116,020 sectors followed by a 150-sector
  post-gap (116,170 in all). 23 files dated 1995-05-02 to 1996-11-10: the eight `.CKD` archives,
  `TRDOS.DAT`, `GRPHAZIR.DAT`, the four sound drivers `SDI*.DSD`, `BOSCH.KOR`, `SONPAL.PAL`,
  `IST.EXE` (384,560 bytes), the installer `KUR.EXE`, `NASIL.EXE`, three `.PIF` files and
  `ISTANBUL.TXT`. No `SES.CFG`, `GRP.DAT` or saved game: the installer and the game write those.
  All data files equal those of the 2005 release.
* **Mastering quirk**: the 116,020 data sectors sit 48 samples (192 bytes) early on the disc; the
  post-gap and the audio are at offset 0. redumper's track split therefore leaves the post-gap
  scrambled and the audio tracks 192 bytes early. Read at the corrected positions, **tracks 1
  and 2 match redump.org's sums** above.
* **Track 3** ends 192 bytes (48 samples) short - the drive did not read into the lead-out - so
  its sums differ from redump.org's. A drive that can overread the lead-out would complete it.

## How the game uses the CD

* **ovl5737** is an MSCDEX client:
  * INT 2Fh AX=1500h detects MSCDEX, and IOCTL 0Ah reads the audio disc info.
  * IOCTL 0Bh reads track start positions.
  * Device request 84h plays (start in Red Book frames), and 85h stops.
* **Track use** (`o5737_cd_play_track(track, len_lo, len_hi)`, with length −1 meaning "to the end
  of the track"):
  * **track 2** plays in the intro and main menu (`demo_intro_menu` 3C2D:09DB / 0A48);
  * **track 3** plays in the story slideshow (`demo_story_scene` 3C2D:130F);
  * **track 0** resumes or replays from `ui_cd_music` 05DC:08F6.
* **For the last track it plays, the length is `status − start − 150`**: the end of the disc minus the
  2 s lead-in.
* **The 2005 `IST.EXE` never enables CD audio.** `o5737_cd_init` (5737:0198, which sets the
  active flag 16E0:8F4E) has no callers there. Every play request therefore returns immediately,
  even with the disc mounted (checked in DOSBox-X). The 1996 `IST.EXE` on the disc calls it from
  `main` and plays the tracks.
* **The videos are local.** The GFL videos load from `0GFL.CKD` (218 MB) through the archive
  layer (ovl569A). Before playing one, the FRPOS video opcode (3FD1:1540) checks the flag
  16E0:3D5C:
  * if it is 1, the opcode calls `o5737_cd_vtoc_ok` (INT 2Fh 1505h) up to 3 times;
  * if there is no disc, it shows "Biz size CD'yi sürücüden çıkarmayın…", and on refusal
    "CD'yi takmayacaksanız size animasyon falan yok!".
* **The disc check.** DGROUP initialises 3D5C to **1**. In the 2005 build `main` overwrites it
  with 0 at 0546:02DD, a 12-byte `mov es:[3D5C],0` right after `ctl_timer_install`. The 1996
  build has its CD start-up in that place: `o5737_cd_init()`, then the wait for the disc. This
  confirms what the 2005 build suggested before the disc was at hand; [EDITIONS.md](EDITIONS.md)
  lists every difference between the two.

## The disc in the port

The port runs either edition ([EDITIONS.md](EDITIONS.md)). The CD edition gets its disc as
`DATA/LALE.CHD`, mounted as drive D: of the virtual MSCDEX (`src/platform/cdrom.c`, `chd.c`);
the decompiled CD module (`src/game/cd5737.c`, tested in `tests/test_cdaudio.c`) builds the
same request blocks and uses the same length maths as the original.

Development options: `--game-dir <folder> --cd-image <cue|chd> --edition cd` runs the CD
edition from a folder of files plus an image (`--cd-music` is the old name of `--edition cd`;
before the real disc was at hand it switched the CD module on inside the 2005 program);
`--cd-music --cd-image <cue> --cd-play 2 [--seconds 15]` plays one track through the module.

## Simulated image (`tools/cdsim.py`)

`python tools/cdsim.py build` writes `build/cd/… (Simulated).cue` plus 3 `.bin` files with the
exact sector counts and track types above. `tools/dosbox/ist_cd.conf` mounts it as D:.

* **Track 1** is ISO 9660 with the files from `orig/` (except the user files GRP.DAT and
  SAKLA001.SAV). It uses 115,833 sectors, and the rest is zero-padded to 116,170. Sectors are raw
  Mode 1: sync, BCD MSF header, EDC and ECC P/Q, checked by `cdsim.py selftest` against a scalar
  reference implementation. The PVD dates are unset, as on the real disc. The volume label
  (`ISTANBUL`) turned out right; the order of the files on the real disc is different
  (`0GFL.CKD` comes first, at sector 21).
* **Tracks 2 and 3** hold original music synthesised by the script: a drone, an oud-like pluck in
  the Hicaz makam, and frame-drum usul patterns. There are no samples or recordings, the output is
  deterministic, and it is dedicated to the public domain under **CC0 1.0**.

`python tools/cdsim.py verify <cue>` compares an image in redump.org's layout with the hashes
above; `python tools/chd.py verify <chd>` does the same for the tracks inside a CHD.
