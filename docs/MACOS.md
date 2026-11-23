# The macOS port

The game code is the same on every host: `src/game`, `src/engine`, `src/dsmi`,
`src/rtl` and the emulated PC (`src/platform/pc.c`, `vga.c`, `audio.c`, `cdrom.c`)
have no host-specific line.  What is particular to the Mac is in the SDL3 host
(`src/platform/sdl3_main.c`, under `__APPLE__` / `HOST_APPLE`), the About box
(`src/platform/about_macos.m`), the build (`CMakeLists.txt`, `cmake/Info.plist.in`)
and the tooling (`tools/hostenv.sh`).  Build commands: [`README.md`](../README.md).

## What is built

| Output | What |
|---|---|
| `out/macos/Lale64` | the program, for the terminal, the tests and the verification scripts (preset `macos`: arm64, Homebrew SDL3) |
| `out/macos/Lale64.app` | the same sources as an application (target `lale_app`): icon `src/gen/lale.icns`, `Info.plist`, ad-hoc signature |
| `out/macos/lale_testlib.dylib` | the differential tests' library |
| `dist/Lale64-<version>-macOS.zip` | `tools/package_macos.sh`: preset `macos-release` - universal (arm64 + x86_64), macOS 11+, SDL3 3.4.18 built from source and linked statically (`LALE_VENDOR_SDL`), so the app depends on nothing outside macOS |

The app is signed ad hoc and not notarised: on another Mac the first start is
right-click > Open (or `xattr -dr com.apple.quarantine Lale64.app`).  A Developer ID
signature and notarisation need the owner's Apple account and are not part of the
build.

## The Windows build from the Mac

`tools/bootstrap.sh --win-cross` downloads llvm-mingw for macOS (the release the
Windows build uses, 20261104) and SDL3's mingw package into `.tools/`;
`cmake --preset windows-cross && cmake --build --preset windows-cross` then builds
`out/windows-cross/Lale64.exe` from this tree.  It is a compile check for changes
to shared code: the program is not run or tested on the Mac.

## The game folder

The original's files are not in the repository or in the app.  `Lale64` takes
`--game-dir` / `game_dir`, else the working directory if it has `3D.CKD`, else
`./orig`.  An app started from the Finder has no useful working directory, so on
macOS the search goes on (`mac_find_game_dir`):

1. the folder that holds `Lale64.app` (or the bare program), then `orig` in it;
2. `~/Library/Application Support/Lale64`;
3. a dialog asks for the folder; the answer is written as `game_dir = ...` to
   `~/Library/Application Support/Lale64/lale.ini`.

That `lale.ini` is the app's options file; it is read before `./lale.ini`, and the
command line overrides both.  Scripted and counted runs (`--script`, `--frames`)
never put up the dialog.  Saved games (`SAKLA*.SAV`) and `GRP.DAT` are written
into the game folder, as the original does.

## Keyboard, mouse, window

| On the Mac | Meaning |
|---|---|
| left Option | the PC's Alt (Alt+key words, the BIOS shift flag).  The right Option key keeps typing characters |
| Cmd+Return, Cmd+F, the green button | full screen; the picture stays 4:3, with black bars on a wider display |
| Cmd+. | the PC's Ctrl-Break (also Ctrl+F15 / Ctrl+F14, the Pause / ScrollLock keys of an extended Apple keyboard) |
| Cmd+Q, Cmd+H, Cmd+M ... | SDL's menu bar; Command combinations never reach the game |
| Lale64 > About Lale64 | the About box; the game is paused while it is open |

F1-F10 are the function keys (with Fn on a laptop, unless "use F1, F2 as standard
function keys" is on).  The window is a Retina surface (`SDL_WINDOW_HIGH_PIXEL_DENSITY`),
scaled with nearest-neighbour as on Windows.

The 4:3 letterboxing (`present_rect`) replaced SDL's stretch presentation on every
host: windowed it changes nothing (the window is held at 4:3), in full screen it is
what keeps the picture's shape.

## Found while porting

| Where | Problem | Fix |
|---|---|---|
| `src/game/iso_map.c` | `NULL` without `<stddef.h>` (the mingw headers happened to provide it) | include added |
| `src/platform/pc.c` `pc_fopen` | POSIX `fopen` opens a directory for reading; DOS and Windows refuse.  `o5737` `read_cfg` with an empty name got a stream and a handle the original does not (`test_cd5737.test_read_cfg`) | directories are refused on POSIX hosts |
| `src/platform/pc.h` `pc_a000`, `src/rtl/bc_farheap.c` `bc_fh_arena` | a blit that runs past the 64 KB screen window - a text drawn at x = -1 by `ui_format_text` (right-aligned, outlined, wider than its place) goes to offset 65535 + y*320 - left the array and overwrote whatever the linker put behind it: on macOS the EMS page frame (a corrupted font cell, `test_intrface.test_format_text`), on Windows by luck nothing visible.  The original writes into B000 (or, with the screen surface on the heap, into video memory) | both arrays have slack behind them that takes those writes (`PC_A000_SLACK`, `BC_FH_ARENA_SLACK`): defined, and the same on every host |
| `src/engine/systems.c` `sys_blit`, `sys_blit_recolor` | `test_q3dview_gap.test_render_100` failed on the Mac: the first fix for a destination row above a conventional block assumed a width that is a multiple of 16 | the same bug was found on Windows and fixed upstream in `cfcacce` (the original's 1 MB wrap); the port uses that fix |
| `src/rtl/bc_farheap.c` `bc_fh_arena` (also `pc_a000`, `pc_ems_frame`) | a paragraph boundary of the original was a 16-byte boundary natively only by the linker's choice; with `-fno-common` on macOS the arena started at an odd address (`test_dsmi_infra.test_dds_drivers`) | `_Alignas(16)` |
| `tests/stubs/testhook.c`, `tests/py/lale.py` | `test_iso_d.test_door14_fatal` expects the port's test build to run into a NULL mapping and relied on Windows ctypes turning the access violation into `OSError`; on macOS the signal ended the whole run | `th_guard` / `lale.guarded()`: the call runs under a SIGSEGV/SIGBUS handler and the fault is raised as `OSError` on every host |
| `CMakeLists.txt` | clang fuses `a*b+c` into one instruction on arm64 by default | `-ffp-contract=off` for every target (the x87 model is integer code, but host floating point must round alike everywhere) |
| `tests/py/lale.py`, `test_cd5737.py` | the tests redirect C functions by writing an x86-64 jump over their entry (`VirtualProtect`) | arm64 jump (`ldr x16, .+8; br x16; .quad`), code pages made writable through `mach_vm_protect(VM_PROT_COPY)`; 16-byte function alignment in the test library |
| `tests/py/test_farheap.py` | paragraph 9FFE of the `main()` snapshot is DOSBox-X's own stale data and differs between its Windows and macOS builds (zeros on macOS) | a snapshot with zeros there is accepted; `bc_farheap_reset()` keeps the Windows capture's bytes |
| `tools/dosbox/oracle.py` | the macOS DOSBox-X debugger takes no absolute path in `MEMDUMPBIN` | paths relative to the emulator's working directory |
| `tools/verify/build_hook.py` | needed llvm-mingw's `ld.lld` | system clang + a flat link in Python (`link_flat`: one `.text`, `R_386_16` only) |
| `tools/*.sh` | Windows paths (JDK, `.bat` launchers, `cygpath`, `python`) | `tools/hostenv.sh` |

Not available on the Mac: Ghidra's decompiler (the 12.2 release has no `mac_arm_64`
natives; `ExportDecomp` writes no draft C).  The listings, `ist_fpu.bin` and the
branch check are produced and identical to what the tests need.

## Verification on macOS

See [`PROGRESS.md`](PROGRESS.md) ("macOS port") for the numbers.

* `ctest --test-dir out/macos`: `vga`, `cdaudio` and the differential suite - the
  original x86 code in Unicorn against the arm64 library.
* Whole game: `tools/verify/pair.py` with the scripts of [`VERIFY.md`](VERIFY.md)
  (`play3`, `play4`, `play4load`, `play5`) - `out/macos/Lale64` against the original
  in the Homebrew DOSBox-X 2026.10.01.
* Both slices of the release app: the same script through `arch -arm64` and
  `arch -x86_64` (Rosetta), dumps and written files compared byte by byte.
* The About box was rendered off screen in both appearances; the app was started
  through LaunchServices (`open`) with the game files beside it.

Not checked by a person at the keyboard: sound by ear, the full-screen switch, the
folder dialog, the About box on screen.  They are exercised by code paths that ran
(audio device opened, frames presented), but nobody looked or listened.
