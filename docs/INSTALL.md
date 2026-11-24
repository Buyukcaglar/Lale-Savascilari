# The game's data: DATA, SAVE and VIDEO

The port ships without any part of the original game. At its first start it takes the game from
whatever the player has and keeps it in folders beside the executable:

| Folder | Holds | Written |
|---|---|---|
| `DATA/` | the game as imported: `LALE.CHD` (a disc) or the game's data files (the 2005 release) | once, by the import |
| `SAVE/` | everything the game itself writes: saved games (`*.SAV`), `GRP.DAT`, `SES.CFG` | by the game, in every edition |
| `VIDEO/` | optional: the videos in a better copy (`INTRO.mp4` …), see [4K videos](#4k-videos) | by the player |

"Beside the executable" is the folder of `Lale64.exe`; on macOS the folder that holds
`Lale64.app`. If that folder cannot be written to, the system's per-user folder for the program
is used instead. `--base-dir <folder>` (`base_dir` in `lale.ini`) names another place.

## What the player can provide

| Source | Becomes | Edition played |
|---|---|---|
| the game CD in a drive (Windows) | `DATA/LALE.CHD` | CD (1996) |
| a disc image: `.cue` + `.bin`, `.iso`, `.chd`, or a folder holding one (a redumper dump folder as it is) | `DATA/LALE.CHD` | CD (1996); an `.iso` has no audio tracks and plays as 2005 |
| the 2005 release: a `.zip`, `.7z` or `.rar`, or the folder it was unpacked to | the data files in `DATA/`, saved games and settings in `SAVE/` | 2005 |

The two releases differ in the program only (see [EDITIONS.md](EDITIONS.md)); their data files
are the same byte for byte, so either source gives the complete game. `--edition cd|2005`
overrides the choice: the 2005 edition also runs from `LALE.CHD` alone; the CD edition needs a
disc with its audio tracks.

How the source is found, in this order:

1. named on the command line (`Lale64 <path>`, or drag it onto the program) or as `source` in
   `lale.ini`;
2. already imported: `DATA/` holds `LALE.CHD` or the files;
3. lying beside the program, in the base folder or in the working directory (images first, then
   folders with the game, then archives);
4. a CD drive whose disc is labelled `ISTANBUL`;
5. the program asks: *Choose File*, *Choose Folder*, *Quit*.

A progress window shows the import; Esc cancels it and leaves nothing behind. A source that is
not the game is refused with the reason (a disc without `3D.CKD`, an incomplete folder, a
damaged archive or CHD).

### A disc or disc image → `DATA/LALE.CHD`

The disc is stored as a CHD (MAME's compressed disc format, version 5, the layout `chdman
createcd` writes): data sectors with their error-correction bytes removed and deflated, audio as
FLAC. The real disc (331 MB) becomes 208 MB. The game then reads everything from that file: the
data track as drive D: through the decompiled MSCDEX client, the audio tracks as CD music.
Reader and writer are the port's own code (`src/platform/chd.c`, `zdeflate.c`, `flac.c`,
`cdsector.c`); `chdman` reads the result, and CHDs written by `chdman` with the cdzl and cdfl
codecs are accepted as a source.

* **cue/bin**: one `.bin` or one per track, `MODE1/2352`, `MODE1/2048` and `AUDIO` tracks. Naming
  the `.bin` finds the `.cue` beside it.
* **redumper dumps of this disc**: the disc's data track was mastered 48 samples early, and
  redumper's split leaves the last 150 sectors of track 1 scrambled and the audio 192 bytes
  early. The importer notices this (the end of the data track has no sync pattern, but has one
  a few bytes on, and descrambles to valid sectors) and reads at the corrected position, so the
  CHD is the same as from an image in redump.org's layout.
* **`.iso`**: one data track, stored as user data.
* **`.chd`**: checked against its own SHA-1 sums and copied.
* **a drive** (Windows): the table of contents, the data track read as user data and rebuilt to
  raw sectors, the audio tracks read as CD-DA. On macOS and elsewhere a disc is read as the
  files of its mounted volume instead (which gives the 2005 edition).

### The 2005 release → `DATA/` and `SAVE/`

Copied to `DATA/`: `*.CKD`, `*.DSD`, `TRDOS.DAT`, `GRPHAZIR.DAT`, `BOSCH.KOR`, `SONPAL.PAL`.
Copied to `SAVE/` unless the file is already there: `GRP.DAT`, `SES.CFG`, `*.SAV`. Everything
else (the DOS programs, their PIFs and texts) is not needed. The game must be complete: the
eight `.CKD` files, `TRDOS.DAT` and `GRPHAZIR.DAT`.

ZIP archives are read by the port itself (stored and deflated members). 7Z and RAR archives -
and ZIPs it cannot read - are unpacked by a program of the system: `tar` (bsdtar, part of
Windows 10/11 and macOS), else 7-Zip, else `unar`.

### `SAVE/`

The game opens its files in `SAVE/` first, then in `DATA/`, then on the disc; whatever it writes
goes to `SAVE/` (a file that so far only exists in `DATA/` or on the disc is copied there first).
This holds for both editions. The CD carries no `SES.CFG` - its installer wrote one - so a disc
install gets the settings the 2005 release ships with (Sound Blaster 16, 220h, IRQ 7, DMA 5,
22050 Hz) unless one is found.

## 4K videos

`tools/export_videos.py` writes each of the game's 35 videos as `<NAME>.mp4` (320×200, 10 frames
a second). Upscaled copies of those, put into `VIDEO/` under the same names, are shown in place
of the original picture:

* The game's own player runs unchanged underneath - it reads and decodes its video, keeps its
  clock, plays its sound, takes its keys. Only the picture in the window differs: frame *n* of
  the game shows the frame at *n*/10 s of the file. The file's sound track is not used.
* **Ctrl+V** (Windows and macOS; on macOS also ⌘V) switches between the two at any moment, also
  in the middle of a video. `4K Video` or `Orijinal Video` appears in the top right corner for
  three seconds at each switch. `video = original` in `lale.ini` (`--video original`) starts
  with the originals.
* Without files in `VIDEO/` the feature is off and Ctrl+V goes to the game. A video without a
  file plays as the original.
* Names: `INTRO.mp4` (also `.m4v`, `.mov`), case ignored; a suffix set off by `_ - . (` or a blank
  is accepted (`INTRO_4k.mp4`). Any size; the picture is drawn over the game's 4:3 area. A frame
  wider than 16:10 is taken to be padded at the sides and its middle is used.
* Decoding is the system's: Media Foundation on Windows, AVFoundation on macOS. H.264 plays
  without anything installed. Opening a file and decoding its first frame takes 0.1-0.2 s
  (measured with 2882×2160 H.264), spent before the game starts the video. A switch to 4K in the
  middle of a video decodes from the key frame before that moment and shows the original until
  then: 0.2-0.4 s with a key frame every few seconds.

## Options

| `lale.ini` | Command line | Default | |
|---|---|---|---|
| `source` | `<path>` or `--source <path>` | | what to import at this start |
| `edition` | `--edition auto\|cd\|2005` | `auto` | CD edition when `DATA/LALE.CHD` has audio tracks |
| `base_dir` | `--base-dir <folder>` | beside the executable | where `DATA`, `SAVE` and `VIDEO` are |
| `video` | `--video 4k\|original` | `4k` | what videos start as when `VIDEO/` has files |

`game_dir` and `cd_image` are development options (the game straight from a folder such as
`orig/`, without `DATA`/`SAVE`; `cd_image` is the disc for `edition = cd` there). `cd_music` is
the old name of `edition = cd`.

## Tools

* `chdtool` (built with the port): `create <cue> <chd>`, `info`, `verify`, `ls`, `cat`,
  `extract`, `import <base> <source>` - the same code the game uses.
* `tools/chd.py`: an independent reader (`info`, `verify`) built on zlib and ffmpeg's FLAC
  decoder, which also compares the tracks with redump.org's sums.

## Verification

`tests/test_codecs.c` (ctest `codecs`) exercises every module on data it makes up: deflate,
FLAC, Mode 1 sectors, ISO 9660, CHD writing and reading, ZIP, and the importer with a folder, a
ZIP, cue/bin, an ISO, a CHD and seven kinds of unusable source.

Runs with the two references (2026-11-24, Windows 11):

| Source | Result |
|---|---|
| redumper dump folder of the CD (`lale.cue`, 3 `.bin`) | `LALE.CHD`, 208,112,607 bytes, SHA-1 `18e0f0f394bcb6948be1c8c279c9d54a39600608`; written in 13 s, verified in 5 s |
| the same disc as an image in redump.org's layout | the identical CHD |
| that CHD read back | all three tracks equal the `.bin` files; `tools/chd.py verify` (zlib, ffmpeg) agrees; tracks 1 and 2 match redump.org 91232 |
| `IST.EXE`, `0GFL.CKD`, `SOUNDS.CKD` read from the CHD | identical to the files |
| 2005 release as ZIP, 7Z, RAR and folder | the same 16 files in `DATA/` and `GRP.DAT`, `SAKLA001.SAV`, `SES.CFG` in `SAVE/`, equal to the originals |
| data track as `.iso`; an existing `.chd` | imported; files read back identical |
| a mounted ISO as a drive (Windows) | imported through the drive path (table of contents, data sectors) |
| both editions started from their imports | 2005 from the ZIP, in a base folder with Turkish letters and a blank in its name; CD from the redumper folder: the CD checks pass, the intro video plays from the CHD |

Not verified:

* **Audio from a physical drive** - no optical drive on the development machine. The data path
  of a drive was run with a mounted ISO.
* **Track 3** of this dump lacks its last 192 bytes (48 samples: the drive could not read into
  the lead-out), so it alone does not match redump.org; they are stored as silence.
* **macOS**: the new start-up, the import and `hdvideo_avf.m` have not been built or run there
  yet.
