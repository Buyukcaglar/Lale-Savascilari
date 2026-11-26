#!/bin/sh
# package_macos.sh - the macOS application to give away.
#
#   tools/package_macos.sh
#
# Builds preset macos-release (universal arm64 + x86_64, macOS 11 or later,
# SDL3 built from source and linked statically: the app needs nothing that is
# not part of macOS), checks the result and writes
#   dist/Lale64-<version>-macOS.zip      Lale64.app, the READ ME (Turkish, then
#                                        English), LICENSES.txt, lale.ini.example,
#                                        recomplauncher.json (for RecompLauncher)
# The app is signed with the owner's Developer ID (hardened runtime, secure
# timestamp, no entitlements), notarised by Apple and stapled, so it opens on
# another Mac without a warning (since 2026-11-27; RecompLauncher starts it too).
# Environment: LALE_SIGN_ID (the identity; default below), LALE_NOTARY (the
# notarytool keychain profile, default civ64-notary), LALE_NOTARIZE=0 to keep
# the build's ad-hoc signature (a local check only: not for release).
# The game's own files are not part of it (see the READ ME:
# tools/package/OKUBENI-README-macOS.txt).
set -e
SIGN_ID="${LALE_SIGN_ID:-Developer ID Application: Onur Buyukcaglar (SZ4F4WP934)}"
PROFILE="${LALE_NOTARY:-civ64-notary}"
NOTARIZE="${LALE_NOTARIZE:-1}"
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

# the release signature: Developer ID, notarised, stapled (the program's bytes
# are unchanged; only the signature is replaced)
if [ "$NOTARIZE" != 0 ]; then
  codesign --force --options runtime --timestamp --sign "$SIGN_ID" "$APP"
  codesign --verify --deep --strict "$APP"
  rm -f out/macos-release/notarize.zip
  ditto -c -k --norsrc --noextattr --keepParent "$APP" out/macos-release/notarize.zip
  xcrun notarytool submit out/macos-release/notarize.zip --keychain-profile "$PROFILE" --wait \
    --output-format json > out/macos-release/notary.json
  cat out/macos-release/notary.json; echo
  grep -q '"status":"Accepted"' out/macos-release/notary.json || { echo "not notarised"; exit 1; }
  rm -f out/macos-release/notarize.zip
  xcrun stapler staple "$APP"
  spctl --assess --type execute --verbose=2 "$APP"
fi

STAGE=out/macos-release/stage/Lale64
rm -rf out/macos-release/stage
mkdir -p "$STAGE" dist
ditto "$APP" "$STAGE/Lale64.app"
cp lale.ini.example recomplauncher.json "$STAGE/"
cp docs/LICENSES.txt "$STAGE/LICENSES.txt"
# the READ ME (Turkish, then English): tools/package/OKUBENI-README-macOS.txt
sed "s/@VER@/$VER/g" tools/package/OKUBENI-README-macOS.txt > "$STAGE/OKU BENI - READ ME.txt"
ZIP=dist/Lale64-$VER-macOS.zip
rm -f "$ZIP"
ditto -c -k --norsrc --noextattr --keepParent "$STAGE" "$ZIP"
echo "package_macos: $ZIP ($ARCHS, $(du -h "$ZIP" | cut -f1))"
