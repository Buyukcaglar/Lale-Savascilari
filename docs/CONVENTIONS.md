# C conventions for the decompiled source

These rules exist to keep **1:1 behaviour** while staying readable. If they conflict,
fidelity wins, and the code carries a comment explaining why.

## 1. Every function is traceable

```c
/*
 * iso_apply_damage - subtract damage from a combatant and handle death.
 *
 * Original: IST.EXE ovl4595:1A2C (ISOSYS.L), far cdecl.
 * Verified: tests/iso_damage_test.py (unicorn differential), 2026-11-xx.
 */
int16_t iso_apply_damage(Combatant *c, int16_t dmg)
```

* The header comment gives the original address (`seg:off` in the flat image, see
  `tools/README.md`), the original module, and how the function was verified.
* Non-obvious statements carry a short comment with the original's intent, and the
  address when that helps (`/* ovl4595:1A60 - clamps at 0, original uses jl not jle */`).

## 2. Integer semantics are Borland's

| Borland C++ 3.x | Port |
|---|---|
| `char` (signed) | `int8_t` or `char` for text |
| `unsigned char` | `uint8_t` |
| `int`, `short` | `int16_t` |
| `unsigned` | `uint16_t` |
| `long` | `int32_t` |
| `unsigned long` | `uint32_t` |
| `float` / `double` / `long double` | `float` / `double` / see §5 |

* Never use plain `int` for original data. Use it only for host-side loop counters that
  can't overflow and don't feed back into game state.
* C's integer promotion differs: the original computes `int16_t a*b` in 16 bits and
  truncates, while C promotes to 32-bit `int`. When an expression can overflow 16 bits,
  write the truncation explicitly: `(int16_t)(a * b)`. Signed 16-bit wrap is defined
  in the port: we compile with `-fwrapv`, and narrowing to `int16_t` is two's complement on
  every target compiler.
* Division and modulus of negative numbers truncate toward zero in both, so there is no
  change. Shifts of negative values are arithmetic (`sar`) in the original; the port uses
  `>>` on signed types, which is arithmetic on clang/gcc/MSVC. Logical shifts must
  use unsigned types.
* `long` multiply/divide/shift helpers (`N_LXMUL@`, `N_LDIV@`, …) become plain `int32_t`
  arithmetic; their semantics are standard.

## 3. Memory model

* **Far pointers become native pointers.** Segment arithmetic is spelled out where
  the original depends on it (16-bit offset wrap inside a segment, huge-pointer
  normalisation, `FP_SEG`/`FP_OFF` tricks), with a comment.
* **EMS** is replaced by heap blocks behind the original MEMMAN API. The *page-frame
  window* semantics (only 4 × 16 KB mapped at a time) are kept if the game relies on
  them, for example by writing through a stale mapping.
* **DGROUP data that is game state or a table** goes into structs whose layout is the
  original's byte layout (`#pragma pack(push, 1)`), with `_Static_assert(offsetof(...))`
  checks against the original DGROUP offsets. Out-of-bounds reads of adjacent
  globals, which DOS games often do, then behave the same.
  Far-pointer globals (runtime-only, never saved) live outside these structs.
* **Initial values** of DGROUP tables are generated from `IST.EXE` by a tool
  (`src/generated/dgroup_init.c`), never typed by hand.
* **Uninitialised memory** the original reads (BSS is zeroed by `c0`; heap is not): the
  port reproduces whatever the original would have seen (usually zero, or the value
  observed in the DOSBox-X oracle), with a comment.

## 4. Files and data

* All file formats are parsed with explicit little-endian helpers (`rd16le`,
  `rd32le`, …). Structs are never `fread` directly.
* Paths: the original uses upper-case 8.3 names in the current directory. The port
  resolves them case-insensitively inside the game data folder.
* Text is cp857 (Turkish DOS code page), stored as bytes. Conversion to UTF-8 happens
  only at the host boundary (window title, logs), never in game logic.

## 5. Floating point

* The original's x87/emulator code runs with 80-bit intermediates. Each FP site is
  reviewed. If a `double` evaluation could differ in the last bit and that feeds an
  integer result (`(int)(x*k)`), either
  - use `long double` where the platform makes it 80-bit (not on MSVC/arm64), or
  - restructure to exact integer/fixed-point maths that reproduces the original result,
    verified exhaustively over its input domain, or
  - load the precomputed table from an oracle dump.
* **Reference FPU = x87 hardware.** IST.EXE runs its floating point either on a real
  x87 (486DX and later: the emulator interrupts are patched into real opcodes at run
  time) or, without a coprocessor, in Borland's software emulator, whose transcendental
  functions can differ in the last bit. The port reproduces the **x87** path; the
  DOSBox-X oracle (`tools/dosbox/ist.conf`) therefore runs `cputype=486`, `fpu=true`
  (changed 2026-11-20; with `386` the game's `_8087` was 0, i.e. the emulator path).
  `src/game/q3d.c` carries a small software model of 80-bit x87 arithmetic, verified
  bit-exact against x87 on 2 million random operations; reuse it for other FP sites.
* Borland's `(int)` conversion truncates (the RTL sets the rounding mode for `FIST`),
  which matches C.

## 6. Hardware access

* VGA register and memory access goes through `src/platform/vga.h`
  (`vga_out16(VGA_PORT_SEQ_INDEX, …)`), reproducing the original sequence of writes.
* Busy-wait loops on hardware state (3DAh retrace, PIT counter reads) become calls to
  platform wait primitives that keep the original timing semantics.
* Interrupt handlers (timer, keyboard) become callbacks the platform invokes at the
  original rate and in the original order relative to the main loop.

## 7. Naming

* Function and variable names follow `analysis/names.tsv` (module prefixes such as
  `iso_`, `s3d_`, `gr_`, …). Turkish names stay where the original clearly used them
  (`savas`, `kamp`, `sakla`), and get an English gloss in the comment.
* Types use `PascalCase` (`Combatant`, `GflHeader`), constants and enums use `UPPER_CASE`.
