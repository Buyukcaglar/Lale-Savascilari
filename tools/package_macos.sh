#!/bin/sh
# package_macos.sh - the macOS application to give away.
#
#   tools/package_macos.sh
#
# Builds preset macos-release (universal arm64 + x86_64, macOS 11 or later,
# SDL3 built from source and linked statically: the app needs nothing that is
# not part of macOS), checks the result and writes
#   dist/Lale64-<version>-macOS.zip      Lale64.app + READ ME + lale.ini.example
# The app is signed ad hoc, not notarised: on another Mac the first start is
# by right-click > Open (or: xattr -dr com.apple.quarantine Lale64.app).
# The game's own files are not part of it (see the READ ME it writes).
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
cat > "$STAGE/READ ME.txt" <<EOF
Istanbul Efsaneleri: Lale Savascilari 64bit $VER - macOS
(macOS 11 or later, Apple Silicon and Intel)

This is a port of the game's program only.  It needs the original game:
a disc image of the game CD (.cue + .bin, .iso or .chd), or the 2005
release as a .zip / .7z / .rar or a folder.  Put it beside Lale64.app, or
start the app and choose it when it asks.  The game is imported once into
a folder DATA beside the app; saved games are written to SAVE there.
Upscaled videos in a folder VIDEO beside the app are shown in place of the
originals (Ctrl+V switches).

First start: the app is not notarised by Apple.  Right-click Lale64.app and
choose Open (once), or run:  xattr -dr com.apple.quarantine Lale64.app

Keys: the PC's Alt is the LEFT Option key.  Cmd+Return or Cmd+F: full screen.
Cmd+Q: quit.  Cmd+. is the PC's Ctrl-Break.  The Lale64 menu has About.
Options (CD music from a cue/bin image of the game CD ...): lale.ini.example.
EOF
ZIP=dist/Lale64-$VER-macOS.zip
rm -f "$ZIP"
ditto -c -k --norsrc --noextattr --keepParent "$STAGE" "$ZIP"
echo "package_macos: $ZIP ($ARCHS, $(du -h "$ZIP" | cut -f1))"
