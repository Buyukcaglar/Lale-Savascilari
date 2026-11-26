#!/bin/sh
# package_windows.sh - the Windows program to give away.
#
#   tools/package_windows.sh
#
# Builds preset windows (the configuration the tests run; only the program,
# so that a test run holding the test library does not block it), takes Lale64.exe
# without its debug information, checks that it needs nothing but Windows and
# SDL3.dll, and writes
#   dist/Lale64-<version>-Windows.zip    Lale64.exe, SDL3.dll, the READ ME
#                                        (Turkish, then English), LICENSES.txt,
#                                        lale.ini.example, recomplauncher.json
#                                        (for RecompLauncher: docs/PROGRESS.md)
# The game's own files are not part of it (see the READ ME:
# tools/package/OKUBENI-README.txt).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
. "$ROOT/tools/hostenv.sh"
[ -f src/gen/lale.ico ] || { echo "src/gen is missing: run tools/bootstrap.sh first"; exit 1; }
BIN=$(ls -d .tools/llvm-mingw/*/bin 2>/dev/null | head -1)
[ -n "$BIN" ] || { echo "llvm-mingw is missing: run tools/bootstrap.sh --tools"; exit 1; }

cmake --preset windows >/dev/null
cmake --build --preset windows --target lale
VER=$(sed -n 's/^#define LALE_VERSION "\(.*\)".*/\1/p' src/platform/about.c)
[ -n "$VER" ] || { echo "no LALE_VERSION in src/platform/about.c"; exit 1; }

STAGE=out/windows/stage/Lale64
rm -rf out/windows/stage
mkdir -p "$STAGE" dist
"$BIN/llvm-strip" -o "$STAGE/Lale64.exe" out/windows/Lale64.exe
cp out/windows/SDL3.dll lale.ini.example recomplauncher.json "$STAGE/"
cp docs/LICENSES.txt "$STAGE/LICENSES.txt"

# nothing but Windows itself (system libraries, the universal C runtime) and SDL3.dll
OTHER=$("$BIN/llvm-objdump" -p "$STAGE/Lale64.exe" | sed -n 's/.*DLL Name: //p' | tr 'A-Z' 'a-z' | sort -u |
        grep -v -E '^(advapi32|gdi32|kernel32|shell32|user32|uxtheme|dwmapi|ole32|sdl3|api-ms-win-crt-[a-z0-9-]*)\.dll$' || true)
if [ -n "$OTHER" ]; then echo "the program depends on other libraries: $OTHER"; exit 1; fi

# the READ ME for Notepad: UTF-8 with a byte-order mark, CR LF; then the archive
"$PY" - "$STAGE" "$VER" "dist/Lale64-$VER-Windows.zip" <<'EOF'
import os, sys, zipfile
stage, ver, out = sys.argv[1:4]
text = open('tools/package/OKUBENI-README.txt', encoding='utf-8').read().replace('@VER@', ver)
with open(os.path.join(stage, 'OKU BENI - READ ME.txt'), 'w', encoding='utf-8-sig', newline='\r\n') as f:
    f.write(text)
if os.path.exists(out):
    os.remove(out)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name in sorted(os.listdir(stage)):
        z.write(os.path.join(stage, name), 'Lale64/' + name)
print('package_windows: %s (%d bytes): %s' % (out, os.path.getsize(out), ', '.join(sorted(os.listdir(stage)))))
EOF
