# The CD (redump.org 91232) and the simulated image

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
* **The IST.EXE we have (dated 2005-02-08) never enables CD audio.** `o5737_cd_init`
  (5737:0198, which sets the active flag 16E0:8F4E) has no callers. Every play request
  therefore returns immediately, even with the disc mounted; this was checked in DOSBox-X
  with the simulated image as D:.
* **The videos are local.** The GFL videos load from `0GFL.CKD` (218 MB) through the archive
  layer (ovl569A). Before playing one, the FRPOS video opcode (3FD1:1540) checks the flag
  16E0:3D5C:
  * if it is 1, the opcode calls `o5737_cd_vtoc_ok` (INT 2Fh 1505h) up to 3 times;
  * if there is no disc, it shows "Biz size CD'yi sürücüden çıkarmayın…", and on refusal
    "CD'yi takmayacaksanız size animasyon falan yok!".
* **The CD build is patched off.** DGROUP initialises 3D5C to **1**, and `main` overwrites it
  with 0 at 0546:02DD, a 12-byte `mov es:[3D5C],0` right after `ctl_timer_install`.
  * Hypothesis: the 2005 re-release patched out the CD requirement there, and probably the
    `cd_init` call with it.
  * Confirm against the 1997 disc's IST.EXE once the real dump is available.

## CD-music option (port)

| Setting | |
|---|---|
| `lale.ini` | `cd_music = 1`, `cd_image = path/to/disc.cue` |
| command line | `--cd-music --cd-image <cue>` |
| default | **off**, i.e. the original 2005 behaviour |

* **When on,** the port opens the image with the virtual MSCDEX drive (`src/platform/cdrom.c`,
  drive D:) and calls the decompiled `o5737_cd_init()`. From then on the unchanged game code
  plays track 2 in the intro and menu, plays track 3 in the story slideshow, and loops through
  `ui_cd_music`.
* **3D5C is left at 0,** so the videos still play from `0GFL.CKD` without a disc check.
* **Test mode:** `lale --cd-music --cd-image <cue> --cd-play 2 [--seconds 15]` plays a track
  through the module and its loop logic.
* **The module is decompiled 1:1** in `src/game/cd5737.c`: it builds the same request blocks
  in a DGROUP mirror and uses the same length maths. It is tested in `tests/test_cdaudio.c`.

## Simulated image (`tools/cdsim.py`)

`python tools/cdsim.py build` writes `build/cd/… (Simulated).cue` plus 3 `.bin` files with the
exact sector counts and track types above. `tools/dosbox/ist_cd.conf` mounts it as D:.

* **Track 1** is ISO 9660 with the files from `orig/` (except the user files GRP.DAT and
  SAKLA001.SAV). It uses 115,833 sectors, and the rest is zero-padded to 116,170. Sectors are raw
  Mode 1: sync, BCD MSF header, EDC and ECC P/Q, checked by `cdsim.py selftest` against a scalar
  reference implementation. The PVD dates are unset, as on the real disc. The volume label
  (`ISTANBUL`) and the file layout are **guesses**.
* **Tracks 2 and 3** hold original music synthesised by the script: a drone, an oud-like pluck in
  the Hicaz makam, and frame-drum usul patterns. There are no samples or recordings, the output is
  deterministic, and it is dedicated to the public domain under **CC0 1.0**.

When the real dump arrives, `python tools/cdsim.py verify <cue>` checks it against the hashes
above.
