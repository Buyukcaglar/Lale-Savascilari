# seg0000 – Borland C++ 3.x runtime library (`_TEXT`)

Group **rtl**. Source: `analysis/ghidra_ist/asm/seg0000.asm` (0000:0000–0000:5460).
`rtl.tsv` has 242 entries: the 236 Ghidra functions, plus 6 startup routines that are reached only
through the `#pragma startup` table, so Ghidra has no function for them (marked "no Ghidra function").
By confidence: 164 high, 57 med, 21 low. All of the low entries are internal helpers, fragments
or stubs. No game-facing function is low.

**All of seg0000 is Borland RTL. It holds no `ALTDUZEY\FILEMAN.L` code.**
The two `strrefs_ist.tsv` hits (`FUN_0000_4bb2` → 16E0:88F8 "T:ALTDUZEY\FILEMAN.L") are false
positives. 0x88F8 = 35064 = hours in 4 years, the divisor used by `unixtodos`. The real FILEMAN code
is in the overlay area. See §8.

The copyright string at 16E0:0004 is "Borland C++ - Copyright 1991 Borland Intl." (BC++ 3.0/3.1,
large memory model, far calls, `F_`/`N_` helper prefixes).

## 1. Layout (link order)

Module order is alphabetical within each run (the asm modules first, then the C modules, then the
far-string copies):

| Range | Contents |
|---|---|
| 0000–04F0 | c0l startup/exit (`start`, `_cleanup`, `_checknull`, `_terminate`, `SaveVectors`, `_restorezero`, startup-table walker, `abort`/ErrorExit). Embedded data and code: 0260 = DGROUP@ word, 02FC = 8087-emulator install (startup prio 16, hooks INT 34h–3Eh, 75h, 02h), 04B6 = emulator uninstall (exit prio 16) |
| 04F1–1098 | FP support: `__ldtrunc`, `__realcvt`/`__xcvt` (printf %e/%f/%g), `_scantod`/`_scanrslt`/`_scanpop` (scanf/atof), `_fperror`, `__pow10`/`pow10`, `matherr`/`__matherr`, `sin`, `cos`, `atan2` |
| 1099–1522 | `F_FTOL`, `atexit`, `_dos_*`, exit family, long/huge-pointer helpers (`F_LXMUL`, `F_LDIV`…`F_LUMOD`, shifts, `F_PADD`/`F_PSUB`/`N_PADA`/`N_PSBP`, `F_PCMP`, `F_SCOPY`, `F_SPUSH`), `getdate`/`gettime`, `getvect`/`setvect` |
| 1523–1E6B | `__IOerror`, `isatty`, `__longtoa`, `lseek`, tmpnam helpers, `N_LXMUL`, `N_PCMP`, `outport*`, **`srand`/`rand`**, `_read`, `segread`, `setdate`/`settime`, `_setupio`, `stime`, `time`, `tell`, `unlink`, **`__vprinter`** (printf engine) |
| 1E6C–3468 | conio (`__vptr`, `__vram`, `__cputn`, `_VideoInt`, `_crtinit`, `gettext`/`puttext`/`movetext`, `putch`, `getch`, `kbhit`, `textmode`, `__scroll`), `bioskey`, `calloc`, `delay`/`_initdelay`, far heap (`farmalloc`/`farfree`/`farrealloc`/`farcalloc`/`farcoreleft`, `malloc`/`realloc`, brk/sbrk), `harderr` family, `int86`/`int86x`/`intdos`/`intdosx`/`intr`, `ioctl`, `_setargv`, `setblock`, `_setenvp`, `sound`/`nosound` |
| 3469–5280 | `atol`/`atoi`, `bdos`, `_chmod`, `close`/`_close`, `eof`, stdio (`fclose`, `fflush`, `flushall`, `fopen`, `fprintf`, `fread`, `fseek`, `ftell`, `fwrite`, `fgetc`, `fputc`, `puts`, `printf`, `sprintf`, `vsprintf`, `setvbuf`), `getenv`, `itoa`/`ltoa`/`ultoa`, mem*/str*, `open`/`_open`, `read`/`write`/`_write`, time functions (`dostounix`, `unixtodos`, `tzset`, `__isDST`), `_xfclose`, `_xfflush` |
| 5281–5460 | a second copy of the string/memory routines: `_fmemchr`, `_fmemcpy`, `_fmemset`, `_fstrcat`, `_fstrcmp`, `_fstrcpy`, `_fstrlen`, `_fstrncpy`, `_fstrstr`. The code is byte-identical to the near-named ones (large model). The game calls both sets |

## 2. DGROUP (16E0) globals

| Off | Name | Notes |
|---|---|---|
| 0004 | copyright | "Borland C++ - Copyright 1991 Borland Intl." |
| 005B–0069 | saved INT 0/4/5/6 vectors | SaveVectors / _restorezero |
| 006B–0073 | argc / argv / envp for main | _setargv, _setenvp |
| 0075, 0077, 0079, 007B, 007D | env size, env seg, argv area, _psp, _version | c0 |
| 007F | `errno` | |
| 0081/0083 | BIOS tick count at start | c0 (INT 1Ah) |
| 0087–0091 | `_heapbase`, `__brklvl` (008B/008D), `_heaptop` (008F/0091) | heap |
| 8FA0 | `_8087` (FPU type) | sin/cos use FSIN/FCOS when ≥ 3, otherwise emulator INT 3Eh |
| 9030 | FP error message table (6-byte entries) | _fperror |
| 90D0–9142 | powers of ten | __pow10 |
| 918E / E9D2 | atexit count / table (32 far pointers) | |
| 9190 | `_ctype[257]` (`_ctype+1` = 9191) | atol, tzset |
| 9292 / 9296 / 929A | `_exitbuf`, `_exitfopen`, `_exitopen` (far pointers; dummy 0000:11AF at start, set to `_xfflush` 5249 / `_xfclose` 520C) | |
| 929E | `_streams[20]`, 20-byte FILE: +0 level, +2 flags, +4 fd (char), +5 hold, +6 bsize, +8 buffer (far), +C curp (far), +10 istemp, +12 token (= own offset). stdin 929E, stdout 92B2, stderr 92C6, stdaux 92DA, stdprn 92EE | |
| 942E | `_nfile` = 20 | |
| 9430 | `_openfd[20]`: init 6001, 6002, 6002, A004, A002 | |
| 9458 | `_fmode` = 0x4000 (O_TEXT) | |
| 945A | `_notUmask` = 0xFFFF | |
| 945E / 9460 | `_doserrno` / `_dosErrorToSV[]` | |
| 94BA / 94BE | "TMP" / ".$$$" | tmpnam |
| 94C4 / 94C6 | **rand seed, 32-bit long (low/high word), initial value 1** | srand/rand |
| 94C8 | `_stklen` = 0x1000 | |
| 94CA | "(null)" | printf %s with a NULL argument |
| 9532–9543 | `_video` struct (window 9534–9537, attribute 9538/9539, mode 953A, rows 953B, cols 953C, graphics 953D, snow 953E, display offset 953F, segment 9541) and `directvideo` (9543 = 1) | conio |
| 957E/9580 | delay() counts per ms (default 0x0952; `_initdelay` may set it to 0x04A9) | |
| 9584/9585 | ungetch flag / char | getch, kbhit |
| 9596/9598 | `environ` | |
| 959A | "\r" | fputc text mode |
| 959C/959E | stdin/stdout "setvbuf already called" flags | |
| 95A0 | days-per-month table; 95AC cumulative day table | time functions |
| 95C6–95D2 | `tzname[2]` (→ "EST"/"EDT"), `timezone` = 18000, `daylight` = 1 | |
| 95E0 | `_realcvt` vector = 0000:05AE | |
| 95F0–9620 / 9620–962C | `#pragma startup` / `#pragma exit` tables (see §3) | |
| EA52 | tmpnam static buffer | |
| EA60 | SIGFPE handler pointer (used by _fperror) | |
| EA64–EA6E | harderr state | |
| EA70 / EA72 | fgetc / fputc one-byte scratch | |

## 3. Startup and exit

`start` runs the startup table in ascending priority, calls `main` = **0546:0001** with
(argc, argv, envp), then calls `exit(main())`. Startup table (callType, prio, address):

| Prio | Routine |
|---|---|
| 1 | far 122B:0D2B (outside seg0000, not identified) |
| 2 | 0000:1818 `_setupio` |
| 16 | 0000:02FC emulator install |
| 16 | 0000:21CE `_c0crtinit` |
| 16 | far 0000:2300 `_initdelay` |
| 16 | 0000:3252 `_setargv` |
| 16 | 0000:3393 `_setenvp` |
| 30 | far 0000:4DB6 `tzset` |

Exit table: far 122B:0197 (prio 1) and 0000:04B6 (emulator uninstall, prio 16).

`__exit` (11B0) runs, in order: the atexit functions, then `_cleanup` (the exit table), then
`(*_exitbuf)`, `_restorezero`, `_checknull`, `(*_exitfopen)`, `(*_exitopen)` and `_terminate`.
INT 0 (divide error) is hooked to 0000:0171. That handler prints "Divide error" and calls `_exit(3)`.

## 4. Hardware and OS interfaces

* INT 21h: 25h/35h set/get vector, 2Ah–2Dh date/time, 3Ch–3Fh, 40h, 41h, 42h file I/O, 43h attributes,
  44h ioctl, 4Ah setblock, 4Ch exit, 4Eh/4Fh findfirst/findnext, 07h/0Bh console, 30h version,
  5801h/67h/48h/49h (c0 on DOS ≥ 3.30: raise the handle count when `_nfile` > 20; it is 20 here, so this is skipped).
* INT 10h: `_VideoInt`, through conio. INT 16h: `bioskey`. INT 1Ah: c0 start ticks.
* PIT: `readtimer` (port 43h/40h, channel 0) for `delay`. `sound` programs channel 2 and port 61h.
* `__vram` waits on port 3DAh for CGA snow avoidance when `_video.snow` is set.
* `outport`/`outportb`/`outpw`/`outp`: direct port output.
* FP: x87 code is emitted as emulator interrupts. `sin`/`cos` check `_8087` ≥ 3 (387) before using
  FSIN/FCOS, otherwise they call the emulator transcendental (INT 3Eh). `F_FTOL` truncates.

## 5. RTL functions called from game/engine code (the port must provide them)

Callers outside seg0000, found by scanning every listing for `CALLF 0000:xxxx`. **★ = behaviour-sensitive.**

| Addr | Function | Calls | Caller segments | Notes |
|---|---|---|---|---|
| 1779 | ★ **rand** | 65 | 05DC 2700 303F 39E5 3C2D 3FD1 4595 | LCG `seed = seed*0x015A4E35 + 1` on the 32-bit seed at **16E0:94C4 (low) / 16E0:94C6 (high)**, initial value 1. Returns `(seed >> 16) & 0x7FFF` |
| 1768 | ★ **srand** | 1 | 0546 (main) | sets the low word to the argument and the high word to 0 |
| 190E | ★ time | 1 | 0546 | `main` at 0546:0284 does `srand((unsigned)time(NULL))`, which is the `randomize()` macro inlined |
| – | ★ random(n) | inline | | `random()` is a macro: the game inlines `(int)(((long)rand()*n) / 32768)` through `F_LDIV` (for example 4595:1A02 does `rand()*2/0x8000`). No `random` function exists in the binary |
| 48B4 | ★ **sprintf** | 55 | 0546 05DC 09D4 0C7F 0D42 2700 303F 39E5 3FD1 4595 5313 | engine `__vprinter`. Formats in the DGROUP strings: `%d %s %lu %.2d %u %5s %ld %02d %5d %03u`. No FP formats. Note `%.2d` (precision on an integer = zero-pad to 2) and `%5s` |
| 4253 | ★ printf | 34 | 0546 09D4 0AC6 303F 3FD1 5313 5447 5511 559A | to stdout, which is text mode: "\n" is written as "\r\n" |
| 4630 | puts | 17 | 0546 5511 559A | |
| 3F58 | ★ **itoa** | 44 | 303F 39E5 3FD1 4595 | signed only when radix = 10. Other radixes treat the value as unsigned 16-bit. Lower-case digits |
| 3469 | ★ atol | 2 | 4595 | skips `_ctype` whitespace, takes an optional sign, then decimal digits. No overflow detection (wraps mod 2^32) |
| 49CC / 49A3 / 48F7 / 4973 / 49EB / 4A23 / 4936 / 4A54 | strlen (52 calls), strcpy (27), strcat (26), strcmp (5), strncmp (2), strncpy (5), strchr (4), strrev (1) | | 05DC 09D4 0AC6 0C7F 303F 39E5 3FD1 4595 5313 5447 5737 5784 | `strlen(NULL)` returns 0. `strncpy` zero-pads. `strcmp` returns the byte difference (sign matters, not the magnitude) |
| 537A / 530B / 53C2 / 53A3 / 53F3 / 52EC / 5281 | _fstrcpy (15), _fstrcat (17), _fstrncpy, _fstrlen, _fstrstr, _fmemset, _fmemchr | | 0AC6 303F 3C2D 4595 5447 5737 05DC | same semantics as the near versions |
| 4001 / 4049 / 4025 | memcpy, memset, setmem | | 1154 303F 5313 5737 0D42 569A 08D0 | forward copy only (memcpy is not overlap-safe) |
| 25D6 / 24C2 / 273A / 28F7 / 2987 / 25CC / 1F64 | ★ farmalloc, farfree (14), farrealloc, farcalloc, farcoreleft, malloc, calloc | | 09D4 0E9E 1154 2700 303F 3FD1 4595 5313 5447 559A 55CC | **arena behaviour**: blocks are `(n+19)>>4` paragraphs with a 4-byte header, so the returned pointers are `seg:0004`. First fit from a rover. The heap grows with DOS setblock in 1K steps. `farcoreleft` is used by MEMMAN (09D4:044C) to size its pool and in FILEMAN debug output, so its value depends on the DOS memory layout |
| 29D7 | getch | 25 | many | ungetch buffer, then DOS 07h (no echo, no ^C check) |
| 2E45 | kbhit | 12 | | DOS 0Bh |
| 1F3F | bioskey | 15 | 05DC 303F 39E5 3FD1 4595 | |
| 341E | textmode | 5 | 303F 3FD1 | |
| 2321 / 33EB / 3417 | delay, sound, nosound | 1 each | 559A | beep. delay is calibrated by `_initdelay` |
| 3968 / 35D9 / 3AAD / 3C95 / 3BBD / 3C3D / 3684 / 3751 / 4759 | ★ fopen, fclose, fread, fwrite, fseek, ftell, fflush, flushall, setvbuf | | 559A 55CC 5646 0546 | **text/binary**: a mode without 't'/'b' uses `_fmode` = O_TEXT. Text mode strips CR on read, stops at ^Z, writes LF as CRLF, and `ftell`/`fseek` correct for buffered '\n'. Data files must be opened "rb"/"wb" (the callers in 5646/559A should be checked) |
| 112F | _dos_findfirst | 1 | 303F | save-game directory scan |
| 1099 | ★ F_FTOL | 10 | 08D0 2700 | FP → long with truncation toward zero |
| 0E92 / 0F8B | ★ sin, cos | 5 / 3 | 08D0 | x87/emulator results (80-bit). Bit-exactness needs care, see PLAN §3.2 |
| 1242 / 12DF / 12E6 / 12EE / 12F6 / 138D / 13CE / 1477 / 14A6 / 1259 / 127A / 1502 | ★ F_LXMUL (93), F_LDIV (24, truncates toward zero), F_LUDIV, F_LMOD (remainder has the dividend's sign), F_LUMOD, F_LXLSH, F_LXURSH, F_PADD (111), F_PSUB, F_PCMP, F_SCOPY (27), F_SPUSH (17) | | everywhere | compiler helpers: C `long` operations and huge-pointer arithmetic (normalised `seg:off` with off 0..15) |
| 118F / 119E / 12BC / 12CB | _dos_getvect/_dos_setvect (0D42 sound), getvect/setvect (0DB7 keyboard) | | | ISR installation |
| 2ACC | harderr | 1 | 0DB7 | INT 24h critical-error handler |
| 2C20 / 2C51 / 2CEA / 2D18 / 2D78 | int86, int86x, intdos, intdosx, intr | | 5511 0C7F 1154 559A | |
| 1736 | outport | 1 | 5511 | |
| 10C5 | atexit | 1 | 1154 | |
| 1207 | exit | 1 | 559A (fatal error routine F_559A_0000) | |
| 04ED | _fpu_stub_ret2 | 1 | 1351 (emulator) | |

RTL routines that are present but never called from outside seg0000 (not needed by the port as
game API): `open`/`read`/`write`/`lseek`/`close` (only used inside stdio), `atoi`, `ltoa`, `ultoa`,
`vsprintf`, `fprintf` (used only by FP error reporting), `getenv`, `tzset`, `gettext`/`puttext`/`movetext`/`putch`,
`atan2`, `pow10`, the scanf FP engine, `getdate`/`gettime`/`setdate`/`settime`/`stime`, `unlink`,
`_chmod`, `eof`, `ioctl`, `segread`, `bdos`, `memccpy`, `stpcpy`, `strncmp`-family copies with no callers
(`_fmemcpy`, `_fstrcmp`), `outportb`/`outpw`/`outp`, `realloc`, `isatty`.

## 6. Fidelity-critical notes

* **RNG.** There is exactly one generator: `rand` at 0000:1779, with its seed at 16E0:94C4/94C6.
  `srand` (0000:1768) is called once, by `main`, with `(unsigned)time(NULL)`. So a reproducible port
  must allow the seed to be injected; the original seed comes from wall-clock time. Note how `time()`
  behaves: `tzset` defaults to EST5EDT (timezone = 18000, daylight = 1) when TZ is unset, and
  `dostounix` adds 5 h plus one hour of US DST. Only the low 16 bits reach `srand`.
* `random(n)` is inlined as `((long)rand()*n)/32768` through `F_LDIV`. `randomize()` is inlined in main.
* **printf engine.** Integer formats only. `%.2d` zero-pads to the precision. `%s` with NULL prints
  "(null)". `%u`/`%lu` are unsigned 16/32-bit. `__longtoa` uses lower-case hex for `%x` and in `itoa`.
* **itoa(v, buf, r).** Signed only when r == 10. Otherwise the value is converted as `unsigned int`.
* **Text-mode stdio.** See the fopen/fread row in §5. `_fmode` = O_TEXT.
* **Heap.** `farmalloc` returns `seg:0004` and rounds sizes up to paragraphs + 4. Allocation order and
  first-fit reuse decide which addresses the game gets. This only matters if the game compares
  pointers or relies on adjacency (`F_PCMP` is used by 09D4, 5313 and 5447).
* **conio/BIOS.** `delay()` is a PIT busy-wait calibrated at startup. `kbhit`/`getch` go through
  DOS, not BIOS.
* **FP.** `sin`/`cos` choose FSIN/FCOS or the emulator depending on `_8087`. The two can differ in
  the last ulp. `F_FTOL` truncates.
* The c0 BSS clear covers 16E0:962C–EA7C. Everything there starts zeroed.

## 7. Open questions

* 122B:0D2B / 122B:0197 are a far startup routine (prio 1) and a far exit routine (prio 1) in a
  segment the PLAN attributes to 3DSYS. They are probably the Borland emulator/FP init (`_fpinit`) or
  overlay-manager hooks. Unidentified.
* 0000:04ED (`MOV AX,2; RETF`) is called from the emulator. Its exact Borland name is unknown.
* The conio internal names (`__screenio`, `__scr_*`, `__validxy`, `__fillblank`) are guesses based
  on behaviour.
* `_fperror`: the exact Borland symbol name is not confirmed. The behaviour is clear.
* Ghidra "callers" of 0000:01F6, 0519, 0585 and 2E37 from seg0EF6 (DSMI) are bogus mid-instruction
  cross-references. These are not real entry points.

## 8. ALTDUZEY\FILEMAN.L (not in seg0000) – where it actually is

The FILEMAN assert strings (16E0:8797–89C0) are pushed as **negative immediates** (`PUSH -0x7869` =
0x8797). That is why `strrefs_ist.tsv` misses them. A raw byte scan for `68 xx xx` finds every push
in linear 0x54490–0x54E5F. That region is the end of the block labelled **ovl5313** (from 5313:1294)
plus **ovl5447** (5447:0000–0C89). The function at 5313:1294 continues across the block boundary,
which is why the ovl5447 listing starts mid-instruction at 5447:0000. These functions belong to the
group that covers ovl5313/ovl5447. This is a first pass:

| Address | Suggested name | Description |
|---|---|---|
| 5313:1294 (continues as 5447:0000 / 0036 / 004A, which are mid-function entries) | `fm_rle_unpack_ems` | RLE decoder with an EMS destination. Loops over a packed source (far, walked with `F_PADD`). Control byte `b`: if `b > 0x80`, the next byte is repeated `b-0x80` times (stored as 32-bit pattern `bbbbbbbb` through the 386 fill `F_2599_028F`). Otherwise `b` literal bytes are copied (`F_09D4_0006`). When the destination flag (arg at +12h) is 1 and the output offset plus count passes 0xC000, it re-maps the EMS window (`F_1154_0688`, returns a far pointer; `& 0x3FFF` = page offset). If that returns NULL (checked with `F_PCMP`): printf "EMSLock->NULL!", "compteur:%lu, k:%lu, EMSav:%lu" (with `farcoreleft()`), "1: %d, %d, %d, %d" (16E0:86F8–86FE), `getch()`, then fatal `F_559A_0000("T:ALTDUZEY\FILEMAN.L", line 30/37/60/89, NULL, code 60/61/62)`. Returns the unpacked length (long) |
| 5447:038F (+03AA/03D7/043B) | `fm_rle_unpack` | the same RLE into a plain far buffer, 16-bit counters, no EMS paging |
| 5447:047E | `fm_load_picture` (low) | builds a file name in a 50-byte buffer (16E0:E7B8, `_fmemset`, `strlen` ×4, `_fstrncpy`), opens and loads it through MEMMAN `F_09D4_0A81(dst 16E0:DE26, size, __LINE__=150, __FILE__)`. On failure: "Resim Dosyası Bulunamadı!" (picture file not found), then fatal |
| 5447:07C7 | `fm_?` | calls `F_569A_074E` twice (ovl569A). Unknown |
| 5447:080D | `fm_?` | fatal path, assert line 219 (16E0:88F8) |
| 5447:088E / 08B9 | `fm_open_file` (low) | file open. "Dosya açma hatası!" (file open error) → fatal (FILEMAN line 243). The file is opened through ovl569A (stub 16CD:0025 = FUN_569a_0362), which returns -1 on error |
| 5447:093A | `fm_alloc_conv` (low) | calls through the far pointer 16E0:7468 and MEMMAN `F_09D4_05F4`. "Yetersiz Konvansiyonel DOS hafızası" (insufficient conventional DOS memory) → fatal |
| 5447:0AB3 / 0B5F | `fm_make_path` (low) | path/extension handling: `strlen`, `farmalloc`, `_fstrcpy`, `_fstrstr(".")`, `_fstrstr("\\")`, `_fstrcat` |
| 5447:0BDE–0C6E | – | small INT 10h / helper stubs called from ovl5511. Possibly not FILEMAN |

The fatal-error routine `F_559A_0000` (overlay stub 16C1:002A) takes (`__FILE__` far pointer, `__LINE__`, message far pointer or NULL,
error code or -1) and ends in `exit()`.
