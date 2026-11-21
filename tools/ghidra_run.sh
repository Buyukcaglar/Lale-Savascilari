#!/bin/sh
# Re-create the Ghidra project for an executable from build/ and export drafts.
#   usage: tools/ghidra_run.sh ist|kur
# Pipeline: raw import of build/<exe>_flat.bin (x86 16-bit real mode); for IST the
#   image is first FPU-restored by tools/fpupatch.py (linear sweep) -> build/ist_fpu_lin.bin
#   -> SetupIST      segments, CS/DS context, far entry points, overlay thunks
#   -> auto analysis
#   -> FixNear       re-disassemble code decoded with the wrong segment context
#   -> FixFPU        Borland 8087 emulator INT 34h-3Dh -> real x87 opcodes (KUR only;
#                    for IST it mis-patched operands reached by wrong flows)
#   -> FixSwitch     switch jump tables -> case targets + decompiler overrides
#   -> FixFPU        again, for code reached only through switches
#   -> PruneFunctions  drop unreferenced auto functions left by wrong flows
#   -> ApplyNames    analysis/names.tsv
#   -> ExportDecomp  analysis/ghidra_<exe>/*.c + functions.tsv
#   -> ExportListing analysis/ghidra_<exe>/asm/*.asm + <exe>_fpu.bin
set -e
EXE=${1:-ist}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
GH=$(ls -d "$ROOT"/.tools/ghidra/ghidra_*_PUBLIC | head -1)
export JAVA_HOME=$(ls -d "/c/Program Files/Eclipse Adoptium/jdk-21"* | head -1)
OUT="$ROOT/analysis/ghidra_$EXE"
rm -rf "$OUT"
mkdir -p "$ROOT/build/ghidra" "$OUT"
IMG="$ROOT/build/${EXE}_flat.bin"
FPU="-postScript FixFPU.java"
if [ "$EXE" = ist ]; then
  python "$ROOT/tools/fpupatch.py" "$ROOT/build/ist_fpu_lin.bin"
  IMG="$ROOT/build/ist_fpu_lin.bin"
  FPU=""
fi
w() { cygpath -w "$1"; }
"$GH/support/analyzeHeadless.bat" "$(w "$ROOT/build/ghidra")" "Lale_$EXE" \
  -import "$(w "$IMG")" -overwrite \
  -loader BinaryLoader -loader-baseAddr 0000:0000 \
  -processor "x86:LE:16:Real Mode" -cspec default \
  -scriptPath "$(w "$ROOT/tools/ghidra")" \
  -preScript SetupIST.java "$(w "$ROOT/build/${EXE}_segments.json")" \
  -postScript FixNear.java \
  $FPU \
  -postScript FixSwitch.java "$(w "$ROOT/build/${EXE}_switches.tsv")" \
  $FPU \
  -postScript PruneFunctions.java \
  -postScript ApplyNames.java "$(w "$ROOT/analysis/names.tsv")" \
  -postScript ExportDecomp.java "$(w "$OUT")" \
  -postScript ExportListing.java "$(w "$OUT")" "$EXE" \
  -max-cpu 8 2>&1 | grep -E "SetupIST|FixNear: re|PruneFunctions: rem|FixSwitch|FixFPU: patched|ApplyNames|ExportDecomp:|ExportListing|ERROR|Exception|error:|skipping" | head -40
# every relative branch must resolve like the CPU would (catches language/context regressions)
python "$ROOT/tools/branchcheck.py" "$OUT"
