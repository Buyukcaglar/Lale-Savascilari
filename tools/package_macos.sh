#!/bin/sh
# package_macos.sh - the macOS application to give away.
#
#   tools/package_macos.sh
#
# Builds preset macos-release (universal arm64 + x86_64, macOS 11 or later,
# SDL3 built from source and linked statically: the app needs nothing that is
# not part of macOS), checks the result and writes
#   dist/Lale64-<version>-macOS.zip      Lale64.app, the READ ME (Turkish, then
#                                        English), LICENSES.txt, lale.ini.example
# The app is signed ad hoc, not notarised: on another Mac the first start is
# by right-click > Open (or: xattr -dr com.apple.quarantine Lale64.app).
# The game's own files are not part of it (see the READ ME:
# tools/package/OKUBENI-README-macOS.txt).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
[ "$(uname -s)" = Darwin ] || { echo "package_macos.sh runs on macOS"; exit 1; }
[ -f src/gen/lale.icns ] || { echo "src/gen is missing: run tools/bootstrap.sh first"; exit 1; }

cmake --preset macos-release >/dev/null
cmake --build --preset macos-release
APP=out/macos-release/Lale64.app
BIN=$APP/Contents/MacOS/Lale64
VER=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$APP/Contents/Info.plist")

# both architectures, an intact signature, no library outside the system
ARCHS=$(lipo -archs "$BIN")
case "$ARCHS" in *arm64*x86_64*|*x86_64*arm64*) ;; *) echo "not universal: $ARCHS"; exit 1;; esac
codesign --verify --deep --strict "$APP"
if otool -L "$BIN" | grep -v ':$' | grep -v -E '^[[:space:]]*(/System/Library/|/usr/lib/)'; then
  echo "the program depends on libraries outside macOS (above)"; exit 1
fi

STAGE=out/macos-release/stage/Lale64
rm -rf out/macos-release/stage
mkdir -p "$STAGE" dist
ditto "$APP" "$STAGE/Lale64.app"
cp lale.ini.example "$STAGE/"
cp docs/LICENSES.txt "$STAGE/LICENSES.txt"
# the READ ME (Turkish, then English): tools/package/OKUBENI-README-macOS.txt
sed "s/@VER@/$VER/g" tools/package/OKUBENI-README-macOS.txt > "$STAGE/OKU BENI - READ ME.txt"
ZIP=dist/Lale64-$VER-macOS.zip
rm -f "$ZIP"
ditto -c -k --norsrc --noextattr --keepParent "$STAGE" "$ZIP"
echo "package_macos: $ZIP ($ARCHS, $(du -h "$ZIP" | cut -f1))"
