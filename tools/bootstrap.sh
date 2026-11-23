#!/bin/sh
# bootstrap.sh - recreate every git-ignored input from the reference zip and
# (optionally) download the analysis/build tooling.
#
#   tools/bootstrap.sh            # orig/, extracted/, build/ analysis inputs
#   tools/bootstrap.sh --tools    # + Ghidra (and on Windows llvm-mingw, SDL3) into .tools/
#   tools/bootstrap.sh --ghidra   # + run the Ghidra pipeline (needs --tools first)
#   tools/bootstrap.sh --win-cross  # macOS: + llvm-mingw for this host and SDL3's mingw package
#
# System packages installed separately: DOSBox-X, JDK 21, CMake, Ninja
# (Windows: winget, Temurin JDK; macOS: brew install cmake ninja sdl3
# openjdk@21 dosbox-x).  Python needs: capstone, unicorn, numpy, pillow -
# on macOS, where the system Python takes no packages, they go into a
# virtual environment that every script here picks up by itself:
#   python3 -m venv .tools/venv && .tools/venv/bin/pip install capstone unicorn numpy pillow
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
. "$ROOT/tools/hostenv.sh"

ZIP=$(ls _references/*.zip | head -1)
mkdir -p orig
unzip -o -q "$ZIP" -d orig
for f in orig/*.CKD; do
    "$PY" tools/flib.py extract "$f" "extracted/$(basename "$f" .CKD)" >/dev/null
done
"$PY" tools/exeload.py orig/IST.EXE build
"$PY" tools/exeload.py orig/KUR.EXE build
"$PY" tools/segmodel.py ist
"$PY" tools/gendgroup.py          # src/gen/dgroup_init.c: initial DGROUP for the port
"$PY" tools/genicon.py            # src/gen/lale.ico + app_icon.c: program icon

for a in "$@"; do
  case "$a" in
  --tools)
    mkdir -p .tools && cd .tools
    dl() { [ -d "$2" ] || { curl -sSL -o "$2.zip" "$1" && mkdir -p "$2" && unzip -q -o "$2.zip" -d "$2" && rm "$2.zip"; }; }
    dl https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.2_build/ghidra_12.2_PUBLIC_20261016.zip ghidra
    if [ "$HOST_OS" = windows ]; then
      dl https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/llvm-mingw-20261104-ucrt-x86_64.zip llvm-mingw
      dl https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-mingw.zip sdl3
    fi
    cd "$ROOT" ;;
  --win-cross)
    # macOS: the tool chain that builds the Windows program from here
    # (cmake --preset windows-cross) - to check that shared code still
    # compiles for Windows; the result is not run or tested on the Mac.
    mkdir -p .tools/llvm-mingw .tools/sdl3
    if ! ls -d .tools/llvm-mingw/llvm-mingw-*-ucrt-macos-universal >/dev/null 2>&1; then
      curl -sSL -o .tools/llvm-mingw/lm.tar.xz https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/llvm-mingw-20261104-ucrt-macos-universal.tar.xz
      tar xf .tools/llvm-mingw/lm.tar.xz -C .tools/llvm-mingw && rm .tools/llvm-mingw/lm.tar.xz
    fi
    if ! ls -d .tools/sdl3/SDL3-*/x86_64-w64-mingw32 >/dev/null 2>&1; then
      curl -sSL -o .tools/sdl3/s.zip https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-mingw.zip
      unzip -q -o .tools/sdl3/s.zip -d .tools/sdl3 && rm .tools/sdl3/s.zip
    fi ;;
  --ghidra)
    sh tools/ghidra_patch.sh
    sh tools/ghidra_run.sh ist
    PYTHONIOENCODING=utf-8 "$PY" tools/strrefs.py ist ;;
  esac
done
echo "bootstrap done"
