#!/bin/sh
# bootstrap.sh - recreate every git-ignored input from the reference zip and
# (optionally) download the analysis/build tooling.
#
#   tools/bootstrap.sh            # orig/, extracted/, build/ analysis inputs
#   tools/bootstrap.sh --tools    # + Ghidra, llvm-mingw, SDL3 into .tools/ (Windows)
#   tools/bootstrap.sh --ghidra   # + run the Ghidra pipeline (needs --tools first)
#
# System packages installed separately (winget): DOSBox-X, Temurin JDK 21,
# CMake, Ninja.  Python needs: capstone, unicorn, numpy, pillow.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

ZIP=$(ls _references/*.zip | head -1)
mkdir -p orig
unzip -o -q "$ZIP" -d orig
for f in orig/*.CKD; do
    python tools/flib.py extract "$f" "extracted/$(basename "$f" .CKD)" >/dev/null
done
python tools/exeload.py orig/IST.EXE build
python tools/exeload.py orig/KUR.EXE build
python tools/segmodel.py ist
python tools/gendgroup.py          # src/gen/dgroup_init.c: initial DGROUP for the port
python tools/genicon.py            # src/gen/lale.ico + app_icon.c: program icon

for a in "$@"; do
  case "$a" in
  --tools)
    mkdir -p .tools && cd .tools
    dl() { [ -d "$2" ] || { curl -sSL -o "$2.zip" "$1" && mkdir -p "$2" && unzip -q -o "$2.zip" -d "$2" && rm "$2.zip"; }; }
    dl https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.2_build/ghidra_12.2_PUBLIC_20261016.zip ghidra
    dl https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/llvm-mingw-20261104-ucrt-x86_64.zip llvm-mingw
    dl https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-mingw.zip sdl3
    cd "$ROOT" ;;
  --ghidra)
    tools/ghidra_patch.sh
    tools/ghidra_run.sh ist
    PYTHONIOENCODING=utf-8 python tools/strrefs.py ist ;;
  esac
done
echo "bootstrap done"
