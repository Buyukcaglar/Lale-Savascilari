# Lale Savaşçıları – decompilation and portable C port

This is a reconstruction of the 1995–2005 DOS game **İstanbul Efsaneleri – Bölüm I: Lale
Savaşçıları** (the Turkish title means "Istanbul Legends – Part I: Tulip Warriors") as
annotated, portable C. The aim is 1:1 fidelity with the original `IST.EXE`.

* Strategy, module map and roadmap: [`docs/PLAN.md`](docs/PLAN.md)
* File formats: [`docs/FORMATS.md`](docs/FORMATS.md)
* Progress: [`docs/PROGRESS.md`](docs/PROGRESS.md)
* Analysis tooling: [`tools/README.md`](tools/README.md)
* The macOS port: [`docs/MACOS.md`](docs/MACOS.md)
* Playing: where the game's data comes from (CD, disc image, the 2005 release) and the folders
  `DATA`, `SAVE`, `VIDEO`: [`docs/INSTALL.md`](docs/INSTALL.md); the two editions:
  [`docs/EDITIONS.md`](docs/EDITIONS.md)

## 4K videos / 4K videolar

**English.** All 34 videos of the game, upscaled from 320x200 to 4K (H.264 MP4, 1.08 GB):
**[Lale Savascilari 4K Video Pack on ModDB](https://www.moddb.com/mods/lale-savascilari-4k-video-pack/downloads/lale-savascilari-4k-video-pack)**
(MD5 `2ad684edf009f77449def40685f1bce3`).

1. Download and unzip `Lale-Savascilari-Video-4k.zip`.
2. Put the `VIDEO` folder next to the program (the folder that holds the executable).
3. Start the game. The videos play in 4K. Press **Ctrl+V** at any time to switch between 4K
   and the original video. The game's own sound is kept; only the picture changes.

**Türkçe.** Oyunun 34 videosunun tamamı, 320x200'den 4K'ya büyütülmüş halde (H.264 MP4,
1,08 GB): **[ModDB'de Lale Savaşçıları 4K Video Paketi](https://www.moddb.com/mods/lale-savascilari-4k-video-pack/downloads/lale-savascilari-4k-video-pack)**
(MD5 `2ad684edf009f77449def40685f1bce3`).

1. `Lale-Savascilari-Video-4k.zip` dosyasını indirip açın.
2. `VIDEO` klasörünü programın yanına (çalıştırılabilir dosyanın bulunduğu klasöre) koyun.
3. Oyunu başlatın. Videolar 4K oynar. İstediğiniz anda **Ctrl+V** ile 4K ve orijinal video
   arasında geçiş yapabilirsiniz. Oyunun kendi sesi korunur; yalnızca görüntü değişir.

## Getting started (Windows)

```sh
tools/bootstrap.sh --tools          # extract the reference zip, download Ghidra/llvm-mingw/SDL3
tools/bootstrap.sh --ghidra         # optional: regenerate the analysis (about 3 minutes)
cmake --preset windows && cmake --build --preset windows
ctest --test-dir out/windows
```

The original game data is not tracked in git and not part of the program. At its first start
`Lale64` imports it from what you have - the game CD, a disc image (`.cue`/`.bin`, `.iso`,
`.chd`) or the 2005 release (ZIP, 7Z, RAR or a folder) - into `DATA/` beside the executable;
saved games go to `SAVE/`. Both editions of the game (the 1996 CD and the 2005 re-release) are
supported: [`docs/INSTALL.md`](docs/INSTALL.md). For development, `--game-dir orig` runs the
game straight from a folder of the original's files.

## macOS (Apple Silicon)

```sh
brew install cmake ninja sdl3                 # + openjdk@21 dosbox-x for the analysis / oracle
python3 -m venv .tools/venv && .tools/venv/bin/pip install capstone unicorn numpy pillow
tools/bootstrap.sh                            # extract the reference zip, generate src/gen
cmake --preset macos && cmake --build --preset macos
out/macos/Lale64 --game-dir orig              # the game, straight from the original's files
open out/macos/Lale64.app                     # the same program as an application
tools/package_macos.sh                        # dist/Lale64-<version>-macOS.zip to give away
```

`tools/package_macos.sh` builds the preset `macos-release`: a universal app (arm64 +
x86_64, macOS 11 or later) with SDL3 linked statically, signed ad hoc.

The tests need the analysis image and the DOSBox-X captures, made on the Mac the same
way as on Windows:

```sh
tools/bootstrap.sh --tools --ghidra           # Ghidra into .tools/, FPU-restored image
.tools/venv/bin/python tools/dosbox/capture_boot.py
.tools/venv/bin/python tools/dosbox/capture_startup.py
ctest --test-dir out/macos
```

What is particular to the Mac (keys, where the app looks for the game files, the About
box): [`docs/MACOS.md`](docs/MACOS.md).
