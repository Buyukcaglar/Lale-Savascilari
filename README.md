# Lale Savaşçıları – decompilation and portable C port

This is a reconstruction of the 1995–2005 DOS game **İstanbul Efsaneleri – Bölüm I: Lale
Savaşçıları** (the Turkish title means "Istanbul Legends – Part I: Tulip Warriors") as
annotated, portable C. The aim is 1:1 fidelity with the original `IST.EXE`.

* Strategy, module map and roadmap: [`docs/PLAN.md`](docs/PLAN.md)
* File formats: [`docs/FORMATS.md`](docs/FORMATS.md)
* Progress: [`docs/PROGRESS.md`](docs/PROGRESS.md)
* Analysis tooling: [`tools/README.md`](tools/README.md)
* The macOS port: [`docs/MACOS.md`](docs/MACOS.md)

## Getting started (Windows)

```sh
tools/bootstrap.sh --tools          # extract the reference zip, download Ghidra/llvm-mingw/SDL3
tools/bootstrap.sh --ghidra         # optional: regenerate the analysis (about 3 minutes)
cmake --preset windows && cmake --build --preset windows
ctest --test-dir out/windows
```

The original game data is not tracked in git. The port reads it from the game folder
(the `.CKD` archives plus `TRDOS.DAT`, `GRP.DAT`, …).

## macOS (Apple Silicon)

```sh
brew install cmake ninja sdl3                 # + openjdk@21 dosbox-x for the analysis / oracle
python3 -m venv .tools/venv && .tools/venv/bin/pip install capstone unicorn numpy pillow
tools/bootstrap.sh                            # extract the reference zip, generate src/gen
cmake --preset macos && cmake --build --preset macos
out/macos/Lale64                              # the game, from the repo root (game files: ./orig)
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
