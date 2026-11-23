#!/bin/sh
# testlib.sh - build a private differential-test DLL (macOS: .dylib) without CMake.
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
. "$ROOT/tools/hostenv.sh"
case "$HOST_OS" in
  windows) CC=$(ls -d "$ROOT"/.tools/llvm-mingw/llvm-mingw-*-ucrt-x86_64 | head -1)/bin/x86_64-w64-mingw32-clang.exe
           LIB=lale_testlib.dll ;;
  macos)   CC="${CC:-cc} -fno-common"; LIB=lale_testlib.dylib ;;
  *)       CC=${CC:-cc}; LIB=lale_testlib.so ;;
esac
BASE=$(grep -v '^#' "$ROOT/tests/base_sources.txt" | grep . | sed "s#^#$ROOT/#")
EXTRA=""
for f in "$@"; do
  case " $BASE " in *"$ROOT/$f"*) ;; *) EXTRA="$EXTRA $f";; esac
done
$CC -std=c11 -O1 -g -shared -fPIC -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -fwrapv \
  -ffp-contract=off -falign-functions=16 \
  -I"$ROOT/src" -o "$OUT/$LIB" $BASE $EXTRA
echo "testlib: $OUT/$LIB"
