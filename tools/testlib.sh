#!/bin/sh
# testlib.sh - build a private differential-test DLL without CMake.
#
#   tools/testlib.sh OUTDIR [extra.c ...]
#
# Compiles tests/base_sources.txt (finished modules) + the given files into
# OUTDIR/lale_testlib.dll.  Several people can work on different modules at
# once: nobody's half-written file breaks anybody else's build.
# Then:  python tests/py/run.py OUTDIR/lale_testlib.dll [filter]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$1; shift
mkdir -p "$OUT"
CC=$(ls -d "$ROOT"/.tools/llvm-mingw/llvm-mingw-*-ucrt-x86_64 | head -1)/bin/x86_64-w64-mingw32-clang.exe
BASE=$(grep -v '^#' "$ROOT/tests/base_sources.txt" | grep . | sed "s#^#$ROOT/#")
EXTRA=""
for f in "$@"; do
  case " $BASE " in *"$ROOT/$f"*) ;; *) EXTRA="$EXTRA $f";; esac
done
"$CC" -std=c11 -O1 -g -shared -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -fwrapv \
  -I"$ROOT/src" -o "$OUT/lale_testlib.dll" $BASE $EXTRA
echo "testlib: $OUT/lale_testlib.dll"
