# Lale Savaşçıları – decompilation and portable C port

This is a reconstruction of the 1995–2005 DOS game **İstanbul Efsaneleri – Bölüm I: Lale
Savaşçıları** (the Turkish title means "Istanbul Legends – Part I: Tulip Warriors") as
annotated, portable C. The aim is 1:1 fidelity with the original `IST.EXE`.

* Strategy, module map and roadmap: [`docs/PLAN.md`](docs/PLAN.md)
* File formats: [`docs/FORMATS.md`](docs/FORMATS.md)
* Progress: [`docs/PROGRESS.md`](docs/PROGRESS.md)
* Analysis tooling: [`tools/README.md`](tools/README.md)

## Getting started (Windows)

```sh
tools/bootstrap.sh --tools          # extract the reference zip, download Ghidra/llvm-mingw/SDL3
tools/bootstrap.sh --ghidra         # optional: regenerate the analysis (about 3 minutes)
cmake --preset windows && cmake --build --preset windows
ctest --test-dir out/windows
```

The original game data is not tracked in git. The port reads it from the game folder
(the `.CKD` archives plus `TRDOS.DAT`, `GRP.DAT`, …).

## macOS (later, on Apple Silicon)

```sh
brew install cmake ninja sdl3
cmake --preset macos && cmake --build --preset macos
```
