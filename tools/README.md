# Tools

| Script | Purpose |
|---|---|
| `bootstrap.sh` | Recreate `orig/`, `extracted/` and `build/` from the reference zip; `--tools` downloads Ghidra, llvm-mingw and SDL3; `--ghidra` runs the analysis pipeline |
| `flib.py` | List/extract FLiB `.CKD` archives (format in `docs/FORMATS.md`) |
| `exeload.py` | Unpack an MZ + Borland FBOV executable into a flat, linked image (`build/<exe>_flat.bin`) and a map (`build/<exe>_map.json`). Overlays are mapped at segment 0x2700 and up; stub thunks become `JMP FAR` |
| `segmodel.py` | Segment classification + exact far entry points from relocations → `build/<exe>_segments.json` |
| `ghidra_patch.sh` | Apply `ghidra/ia_rel16.patch` to the downloaded Ghidra and rebuild the x86 language. **Required** before `ghidra_run.sh`: stock Ghidra wraps real-mode near branches at linear 64 KB boundaries |
| `ghidra_run.sh` | Headless Ghidra: import → `SetupIST` → analysis → `FixNear` → `FixFPU` → `FixSwitch` → `FixFPU` → `PruneFunctions` → `ApplyNames` → `ExportDecomp` → `ExportListing` → `branchcheck.py` |
| `ghidra/ia_rel16.patch` | Adds the context field `rmseg` (code segment) and makes `rel8`/`rel16` wrap at the segment offset like the CPU |
| `ghidra/SetupIST.java` | Memory blocks per segment, CS/DS/`rmseg` context, entry functions, overlay thunks |
| `ghidra/FixNear.java` | Re-disassemble blocks whose instructions were decoded with a foreign `rmseg` (Ghidra flows context across far calls) |
| `ghidra/FixSwitch.java` | Resolve Borland switch tables (dense index and value-search forms) → case targets, decompiler jump-table overrides, `build/<exe>_switches.tsv` |
| `ghidra/PruneFunctions.java` | Remove functions in DGROUP and unreferenced auto functions whose entry is mid-body (fall-through); keeps pointer-reached routines |
| `ghidra/FixFPU.java` | Borland 8087-emulator `INT 34h–3Dh` → real x87 opcodes |
| `ghidra/ApplyNames.java` | Apply `analysis/names.tsv` (names, comments) |
| `ghidra/ExportDecomp.java` | Draft C per block + `functions.tsv` |
| `ghidra/ExportListing.java` | Annotated `.asm` per block + FPU-restored image `ist_fpu.bin` |
| `strrefs.py` | Match immediates against DGROUP strings → `analysis/strrefs_<exe>.tsv` |
| `strings.py` | Dump cp857 strings from a range of the flat image |
| `branchcheck.py` | Verify every relative branch in the listings against CPU semantics (pipeline gate) |
| `names_merge.py` | Merge `analysis/names/*.tsv` into `analysis/names.tsv` with validation |
| `cmpview.py` | Decode CMP images, verify all, render PNG |
| `cdsim.py` | Build/verify the CD image (redump 91232 layout; simulated or real) — see `docs/DISC.md` |
| `dosbox/oracle.py` | Scripted DOSBox-X debugger oracle (breakpoints, DGROUP/VGA/DAC dumps) |

Addresses are always written `SSSS:OOOO` in the flat image's segment space. That space
equals the original load-relative segments; overlays sit at 2700 and above.
