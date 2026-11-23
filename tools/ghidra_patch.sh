#!/bin/sh
# Apply the project's fixes to the downloaded Ghidra and rebuild the x86 language.
#   ia_rel16.patch - real-mode near branches (rel8/rel16) wrapped at LINEAR 64 KB
#                    boundaries.  Adds the context field rmseg (code segment,
#                    set by SetupIST) and wraps within the segment like the CPU.
# Idempotent.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
GH=$(ls -d "$ROOT"/.tools/ghidra/ghidra_*_PUBLIC | head -1)
L="$GH/Ghidra/Processors/x86/data/languages"
. "$ROOT/tools/hostenv.sh"
find_java
if grep -q "Lale: real-mode near branches" "$L/ia.sinc"; then
  echo "ghidra_patch: ia.sinc already patched"
else
  (cd "$L" && patch -p1 < "$ROOT/tools/ghidra/ia_rel16.patch")
fi
# relative path: SleighCompile rejects absolute paths whose case differs from the
# file system's (C:\projects vs C:\Projects); </dev/null answers the .bat's pause
cd "$ROOT"
REL=${L#"$ROOT"/}
if "$GH/support/sleigh$BAT" "$(native "$REL/x86.slaspec")" </dev/null 2>&1 | grep -E "ERROR|error" ; then
  echo "ghidra_patch: sleigh compile FAILED"; exit 1
fi
echo "ghidra_patch: x86.sla rebuilt"
