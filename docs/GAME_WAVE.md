# Game-module wave (2026-11-21) – shared rules

Five packages run in parallel, one per original module:

| WP | Blocks | Files | Owner of |
|---|---|---|---|
| WP13 | seg0546 (ISTANBUL.C main) + ovl3C2D (DEMOLAR.L) | src/game/istanbul.c/.h, src/game/demolar*.c/.h | `ist_main()` – the game entry the SDL host calls |
| WP14 | ovl39E5 (CHARGEN.L) | src/game/chargen*.c/.h | character generation, GRP.DAT |
| WP15 | ovl3FD1 (FRPOS.L) | src/game/frpos*.c/.h | shop/event script interpreter |
| WP16 | ovl303F (3DSYS.L) | src/game/s3d*.c/.h | exploration loop, camp, save/load, options; also merges the 3DQUICK tests (tests/py/test_q3dview*.py) into the shared build |
| WP17 | ovl4595 (ISOSYS.L) | src/game/iso*.c/.h | isometric battle |

Rules on top of docs/DECOMPILING.md:

1. **Publish your interface first.** Within your first steps, create your module header
   (`src/game/<module>.h`) with prototypes of every function other modules call
   (callers outside your block: see the listings' `called from:` lines), named after
   analysis/names.tsv. Keep those names/signatures stable afterwards; if you must change
   one, say so in your report.
2. **Calling another game module:** include its header if it exists; otherwise declare
   the prototype in your own `.c` under `/* provided by <module> (WPnn) */`. For your
   private test DLL, stub missing callees in `tests/stubs/<wp>_stubs.c` (and stub the
   same routines on the emulator side with `e.stub`).
3. **Game state in DGROUP** (party, characters, map position, flags…): before defining a
   macro, `grep -rn "0x<OFFSET>" src/`. If several game modules share a structure
   (e.g. the party/character records), the lowest-numbered WP that needs it defines it
   in its header; others include that header (and if it does not exist yet, use DG*()
   accessors with a `/* TODO: <name> from <header> */` comment).
4. **Far code pointers** stored in UI window records go through
   `ui_register_far_fn(flat_seg, off, fn)` (src/engine/intrface.h); an unknown address is
   fatal. Register the ones that resolve to your functions (overlay thunk stubs 1670…16DA:
   build/ist_map.json "thunks" maps stub → overlay target).
5. **Keyboard:** `ui_bioskey`, `ui_kbhit`, `ui_getch`, `ui_flush_keys` (intrface.h).
   Random numbers: `bc_random(n)` / `bc_rand()` (src/rtl/bc_rand.h) – mind Borland's
   double-evaluating `max()`/`min()` macros (PLAN §2).
6. **Floating point:** x87 reference; reuse the 80-bit model in src/game/x87ext.h.
7. Big blocks: work in address order, split into several .c files by area, verify as
   you go, keep going until done. If you run low on context: finish the current
   function, make everything compile and pass, and report exactly what is done/left.
