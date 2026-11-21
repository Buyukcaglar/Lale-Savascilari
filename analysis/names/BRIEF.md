# Inventory brief (shared by all inventory analysts)

Project: decompiling the DOS game "İstanbul Efsaneleri – Lale Savaşçıları" (IST.EXE) to portable C.
1:1 fidelity is the goal. Read `docs/PLAN.md` first (module map, binary facts).

## Inputs (all under C:\projects\Lale-Savascilari)
- `analysis/ghidra_ist/asm/<block>.asm` – annotated listing per segment. AUTHORITATIVE.
  `; FUNCTION <name>` headers, callers, `; -> callee` on calls, DGROUP data labels.
  x87 instructions are already restored from Borland's emulator interrupts.
- `analysis/ghidra_ist/<block>.c` – Ghidra draft decompilation (hint only; often wrong on segments).
- `analysis/ghidra_ist/functions.tsv` – all functions (address, block, size, callers, callees).
- `analysis/strrefs_ist.tsv` – which function pushes which DGROUP string (strings are passed
  as PUSH imm16 so Ghidra has no xrefs). `analysis/strings_dgroup.txt` – all DGROUP strings.
- `build/ist_flat.bin` – linear memory image (address = seg*16+off); DGROUP = 16E0.
- Python 3.14 with capstone is available if you need to inspect bytes.

Addressing: `SSSS:OOOO`. Ghidra names like `FUN_4595_0b68` / `F_0AC6_0430` encode the address.
Borland large model: far calls (CALLF), cdecl, args at [BP+6].., DS = DGROUP 16E0. Strings are
cp857 Turkish. Interrupts: 21h DOS, 10h video, 33h mouse, 67h EMS, 2Fh multiplex/XMS/CD.

## Output (write ONLY these two files; do not touch anything else)
1. `analysis/names/<group>.tsv` – one line per function you can say something about:
   `SSSS:OOOO<TAB>name<TAB>[confidence] one-line description`
   - SSSS:OOOO upper-case hex, the function's entry address.
   - confidence is `high`, `med` or `low`.
   - names are valid C identifiers, unique across the program.
2. `analysis/names/<group>.md` – the module summary:
   - what each segment/module does, its main data structures and globals (DGROUP offsets),
     hardware/OS interfaces it touches, file formats it parses (with field layouts you derived),
   - anything fidelity-critical you noticed (self-modifying code, timing loops, RNG use,
     FP use, ISR code, port I/O, undefined behaviour the game relies on),
   - open questions.

## Naming conventions
- Borland C++ 3.x RTL: the real RTL names without the leading underscore (fopen, strcpy,
  farmalloc, _fmemcpy stays _fmemcpy, `__IOerror`, helpers like `N_LXMUL`, `LDIV`, `F_SCOPY`).
- DSMI (Otto Chrons): the real DSMI 2.x API names where recognisable (mcpInit, ampPlayModule,
  cdiSetVolume …); DDS = their dynamic driver system.
- Author's engine library ALTDUZEY and game code: snake_case with a module prefix:
  fm_ FILEMAN.L, mm_ MEMMAN.L, sys_ SYSTEMS.L, gr_ NGRAPHS.L, snd_ NSES.L, ctl_ NKONTROL.L,
  ui_ INTRFACE.L, ist_ ISTANBUL.C, q3d_ 3DQUICK.L, s3d_ 3DSYS.L, cg_ CHARGEN.L,
  demo_ DEMOLAR.L, fr_ FRPOS.L, iso_ ISOSYS.L, gfl_ GFL.C. Unknown module: o<SEG>_ (e.g. o5646_).
  Use Turkish words when the original clearly does (strings are Turkish), otherwise English.
- Don't guess wildly: a correct `[low]` name beats a confident wrong one.

Be efficient: skim the listings, use grep, focus on what each function does and how the
modules relate. Do not write any C source; this is inventory only.
