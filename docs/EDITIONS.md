# The two editions

Two builds of `IST.EXE` exist, and the port behaves as either (`g_edition`,
`src/engine/edition.h`):

| | CD edition | 2005 edition |
|---|---|---|
| `IST.EXE` | 1996-11-10, 384,560 bytes, on the game CD (redump.org 91232) | 2005-02-08, 385,328 bytes, the re-release that runs from a folder |
| Needs | MSCDEX and the disc in the drive | nothing but its folder |
| Videos | checks for the disc before each one | no check |
| Music in the intro, menu and story | CD audio tracks 2 and 3 | the module `DEMO.AMF` |
| Data files | the same, byte for byte (`*.CKD`, `TRDOS.DAT`, `GRPHAZIR.DAT`, `*.DSD`, …) | |

The port's code was decompiled from the 2005 build; `tools/exediff.py` compares the two
executables routine by routine. They are the same program except for:

* **`main`** (`src/game/istanbul.c`). The CD build sets the flag 3D5C to 1, calls
  `o5737_cd_init()` - fatal without MSCDEX ("CD sürücü tanındı…" otherwise) - and waits for the
  disc (`o5737_cd_vtoc_ok()` three times, else "insert the CD" until a key; Esc quits). The 2005
  build has `mov es:[3D5C],0` in that place and loads `DEMO.AMF` later on. With 3D5C = 1 the
  FRPOS video opcode asks for the disc before every video.
* **Two routines of DEMOLAR** (`src/game/demolar.c`): the intro/menu and the story scene. The CD
  build has no module to load, fade or free there; CD track 2 or 3 plays instead. The 2005 build
  still makes the same `o5737_cd_play_track()` calls, but its CD module is never initialised and
  they return at once.
* **`ctl_flush_keys`** (`src/engine/nkontrol.c`): INT 21h AX=0C00h in the CD build, a
  `kbhit`/`getch` loop in the 2005 build.
* **What follows from the different source lines**: the line numbers and file names the builds
  pass to their error routine (`ED_LINE`, `ED_FILE`; the CD build was compiled from drive `T:`),
  and a few strings only the CD build has.

The decompiled code asks `ED_CD` at exactly these places; everything else is shared. The data
segment mirror is the 2005 build's for both.

## Which one runs

`edition = auto` (the default) takes the CD edition when the game was imported from a disc with
audio tracks (`DATA/LALE.CHD`), the 2005 edition when it was imported as files. `--edition cd`
or `--edition 2005` (`edition` in `lale.ini`) decides otherwise where that is possible: the 2005
edition runs from the CHD as well - it finds its data files on the disc - while the CD edition
cannot run without a disc that has the audio tracks. See [INSTALL.md](INSTALL.md).

In the CD edition the disc is `DATA/LALE.CHD`, mounted as drive D: of the virtual MSCDEX
(`src/platform/cdrom.c`, `chd.c`); the game's own CD module (`src/game/cd5737.c`, decompiled
1:1) talks to it with the same device requests as on the PC.

Saved games, `GRP.DAT` and `SES.CFG` are kept in `SAVE/` in both editions and are
interchangeable between them.
