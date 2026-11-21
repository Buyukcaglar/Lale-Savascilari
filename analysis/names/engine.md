# Engine group: ALTDUZEY library + ISTANBUL.C main

Scope: seg0546, seg05DC, seg09D4, seg0AC6, seg0C7F, seg0D42, seg0DB7, ovl5447, ovl5511, ovl559A,
ovl55CC, ovl5737. Names are in `engine.tsv`. The asm2599 routines, ovl569A/ovl5646 (archive / DOS
file layer) and seg10EF (DSMI timer service) are outside this group. They are described here only
where the engine depends on them.

## 0. Overlay addresses in the Ghidra listings are wrong (read this first)

`tools/exeload.py` places overlays incorrectly. `mem[oseg*16 : ...] = code` runs on a bytearray
that is still shorter than `oseg*16`, so Python appends each overlay at the current end of the
buffer and not at its segment. Each overlay therefore sits contiguously after the previous one.
`build/ist_flat.bin`, the Ghidra project and every `analysis/ghidra_ist/asm/ovlXXXX.asm` listing
except ovl2700 show overlay code at **listing offset = true offset + delta**:

| ovl | 2700 | 303F | 39E5 | 3C2D | 3FD1 | 4595 | 5313 | 5447 | 5511 | 559A | 55CC | 5646 | 569A | 5737 | 5784 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| delta | 0 | −26 | −49 | −72 | −95 | −121 | −141 | −172 | −194 | −213 | −240 | −265 | −281 | −312 | −330 |

The stub-thunk targets (`F_5447_052A` and so on, which are the "-> callee" annotations) are the
**true** offsets. They land mid-function in the listings. For example, fm_load_cmp has true
address 5447:052A but listing address 5447:047E (`FUN_5447_047e`). The first |delta| bytes of each
overlay show up at the end of the previous overlay's listing. The VGA routines that the task
brief places at "ovl5447 0BDE.." are really **ovl5511:0000..00FD**, and "ovl5447:0000" is the
tail of fm_load_cmp. Every overlay-entry prologue (all 14 checked overlays) lines up once the
delta is applied. `engine.tsv` uses true addresses and gives the listing address in the
description where it differs. Fix: in exeload, pad `mem` to `oseg*16+len(code)` before the slice
assignment. Then regenerate the flat image and the Ghidra project. Other inventory groups that
work on overlays need the same correction.

Related finding: **seg122B is not 3DSYS.** 122B:08EF is Borland's `_OvrInitEms(handle=0, first=0,
pages=0x12)`, which main calls before printing "Overlay sistemi tamam". So seg122B (at least that
part) is the VROOMM overlay manager.

## 1. Video

**Mode.** The game uses plain BIOS **mode 13h** (320×200, 256 colours, chain-4, linear at
A000:0000). It is never unchained: no Mode X, no Sequencer reg 4 or CRTC 14h/17h writes anywhere
in the program, and no line doubling changes.
- ogr_init (5511:06F0) does the following:
  - Checks for VGA with INT 10h AX=1A00h; on failure it fails with "VGA kartı bulunamadı!".
  - Sets surface slot 0: w=320 (E29B), h=**204** (E29D). Its mm descriptor E5D8 is
    `{far A000:0000, u32 size 0xFF00, type 0}`.
  - Allocates two 768-byte palettes: E5EF (master) and E5EB (working/DAC copy).
  - Calls ogr_int10(0x0013), then sets 7318 = 731E (full brightness).
- ogr_int10 (5511:0000) is `mov ax,[arg]; int 10h`. It is called with 13h (game), **12h**
  (640×480×16, only for the DEMOLAR "comp4a/comp4b" credit screens, 3C2D true ~074C/0921, where
  E29B=640, E29D=240, size 0x25800), and **03h** (exit, from o559A_fatal).
- Mode 12h drawing goes through sys_blit_cb (0AC6:01C5) with the row callback
  sys_rowcopy_planar (0AC6:000A). For each of the 4 planes the callback writes the map mask with
  `OUT 3C4h, (mask<<8)|02h` (ogr_set_map_mask 5511:000A; mask rotates 1,2,4,8) and copies
  width/8 bytes, with edge bit-masks.

**No page flipping.** The only CRTC start-address writer is ogr_set_start_addr (5511:0090), and
DEMOLAR calls it once, with 0. It waits on 3DAh for bit3 to be set and then clear, then does
`outport(3D4h,(hi<<8)|0Ch)` and `outport(3D4h,(lo<<8)|0Dh)` with `start = u32 arg >> 8 / & 0xFF`.

**Split screen exists but is unused.** ogr_set_split_line (5511:0018) has no callers. With
`v = 2*n` it does the following:
- CR18 = **v mod 255** (`div bx` with bx=0FFh, an author bug: it should be `& 0xFF`).
- CR11 bit7 is cleared.
- CR07 bit4 = v bit8 (`(v>>4)&10h`).
- CR11 bit7 is set again.
- CR09 bit6 = v bit9 (`(v>>3)&40h`).

All of this runs inside cli/sti.

**Attribute controller.**
- ogr_screen_on (5511:00E1): `in 3DAh; in 3BAh; out 3C0h,20h`, which sets PAS and turns the
  display on.
- ogr_screen_off (5511:00FD): the same, with `out 3C0h,00h`, which blanks the screen.

**Frame presentation.** Frames are composed in off-screen mm blocks (often in EMS) and copied
whole into slot 0, which is VRAM. The DEMOLAR animation loop at 3C2D true 0369 shows the pattern:
1. Wait `in 3DAh` until bit3 is set, then until it clears (end of vertical retrace).
2. Copy with mm_copy_range (09D4:07C9) into slot 0. For an EMS source this is an INT 67h
   AH=57h move straight into A000 through seg1154.
3. Pace the frame with the ms clock E74A.

The mouse cursor is software and is drawn directly into VRAM (section 2).

**Palette / DAC.** gr_set_dac (0C7F:000C) does `out 3C6h,FFh` (PEL mask), then `out 3C8h,start`.
For each chunk it waits for 3DAh bit3 to be set, then clear, then does
`rep outsb 3C9h` of `count/chunks*3` bytes. gr_set_palette (0C7F:0050) loads all 256 colours in
one chunk, so the write starts just after retrace ends. Palettes are 6-bit DAC values
(768 bytes). Fades work as follows:
- gr_fade (0C7F:073F) has the signature `(duration_ms, dBright, dVolume)`.
- Each iteration it computes `level = 7318 + dBright*t/duration` from E74A and sets
  `E5EB[i] = E5EF[i]*level/731E` (integer division, 768 entries).
- It reloads the DAC each iteration.
- It also ramps music volume through 0EF6:1625 (base 731A, max 7320) when sound (73D0) is on.
- ogr_palette_step_fade (5511:03DF) is a step-based fade (±n per step, with a tick delay).

**Surfaces.** The table at DGROUP E29B holds 90 slots of 8 bytes:
`{u16 w; u16 h; far ptr to mm handle}`. Slot 0 is the screen. Slot 9 is the cursor composite and
slot 10 (0Ah) is the cursor save-under. Slot 0x4B holds the cursor shapes: a 90×15 strip of six
15×15 shapes. Slots 3, 4, 5 and 0x54 hold the font images deneyx/gocuk/irem6/irem62.cmp. The blit
core is sys_blit (0AC6:0430):
`blit(sx,sy,w,h,dx,dy,srcSlot,dstSlot,rowfn far[,key])`. It maps both slots through
mm_ems_lock/mm_map and calls asm row routines:
- 2599:02D0: opaque copy.
- 2599:0341: colour-key copy (the key byte is the extra argument; the cursor uses 0).
- 2599:03C4: mirrored colour-key copy.
- 2599:028F: memset32.
- 2599:02AF: memcpy32.

Note that 2599:0341's error path does `pop si; pop ds` after pushing only ds, which corrupts the
stack if it is ever taken.

## 2. Timer and input

**Timer.** The engine uses the DSMI timer service in seg10EF; that code is not in this group.
- tsInit (10EF:007D) hooks INT 08h with `INT 21h AX=3508h/2508h`.
- 10EF:0036 programs PIT channel 0 (`out 40h` low byte then high byte).
- The ISR at 10EF:0048 sends EOI first (`out 20h,20h`), does sti, then walks up to 16 routines of
  14 bytes each at 85CE. Each routine uses a 16-bit fractional accumulator
  (`step = 0xFFFF:? / rate`) and is called as pushf/lcall, so each routine ends with iret.
  Slot 0 is the old BIOS vector at rate 0xFFFF (18.2 Hz). The PIT runs at the smallest requested
  divisor.
- Registered routines:
  - ctl_timer_tick (0DB7:01F4), divisor **0x2E9B = 11931**, about 100.007 Hz. It does
    `dword E74A += 10`, a millisecond clock that all game timing reads.
  - snd_timer_100hz (0D42:0120, 0x2E9B), which calls the AMP player poll (0EF6:0F4C).
  - 1145:0096 (0x2E9B).
  - snd_timer_40hz (0D42:0093), divisor **0x7485** (≈40 Hz), conditional on SDI flags & 0x80.
    It saves/restores the EMS map around 0EAE:0414.

**Keyboard.** ctl_install (0DB7:0026) sets these vectors (with RTL getvect/setvect, 0000:12BC and
0000:12CB):
- **INT 09h → ctl_int09_isr.** It only traps Ctrl+Alt+Del: `in al,60h == 53h` with BIOS 40:17
  &0Ch. Then it acks the keyboard through port 61h, sends EOI, and does INT 1Bh. Every other key
  chains to the BIOS.
- **INT 23h → ctl_int23_isr**, which exits through the fatal handler.
- **INT 00h → ctl_div0_isr.** On division by zero it **returns AX=EAX=FFFFh and skips 3 bytes**
  (it assumes a 3-byte `div` instruction). This is fidelity-critical: game code that divides by
  zero gets 0xFFFF.
- ctrlbrk(ctl_ctrlbrk_handler), where the handler allows 3 breaks.

There is no scancode buffer. The game reads keys through BIOS/RTL `kbhit()` and `getch()`
(0000:2E45 and 0000:29D7). ctl_flush_keys drains them.

**Mouse.** All calls go through INT 33h with `int86x(0x33, &E766, &E756, &E74E)`:
- Function 0, reset: presence goes to 7324.
- Function 3, read x, y, buttons: x is shifted right by 7325. 7325=1 when x after the reset is 320,
  which means a 640-wide driver.
- Function 4, set position.
- Functions 7 and 8, set the horizontal and vertical ranges (0..319 and 0..199).

The cursor is software only. gr_cursor_update (0C7F:026B) restores slot 10 at the old position,
saves a 15×15 block of the screen into slot 10, and draws slot 9 with key 0. The cursor is
clipped against limits 7314/7316. Up to 41 hotspot rectangles of 8 bytes `{x1,y1,x2,y2}` live at
E5F5 (gr_zone_under_cursor / gr_cursor_in_zone).

## 3. Files

**CKD archives.** This layer is ovl569A, which is Pascal-convention code (callee pops). It is not
in this group, but its behaviour is confirmed here.
- `ckd_open(name)` (569A:0047) uses up to 8 archive slots of 12 bytes at E86C:
  `{u16 dos handle, u16 count, far name, far entries}`.
- It reads the 12-byte header and checks "FLiB".
- It then **seeks to header+8, which is the offset of the trailing directory copy**. The game reads
  the trailer and never the inline headers. This is verified on all CKDs: hdr[8] equals
  filesize − count×0x41.
- It reads `count` 0x41-byte "FiLE" records into 0x38-byte in-memory entries:
  - +0: u16 handle
  - +2: char[32] name
  - +22h: u32 data offset (rec+35h)
  - +26h: u32 size (rec+39h)
  - +2Ah: u32 original size (rec+3Dh)
  - +2Eh: u32 position (0)
  - +32h: u32 from rec+31h. This is the last 4 bytes of the 16-byte 8.3 name field and is always
    0 in the files seen.
  - +36h: u16 in-archive flag (1)
- `fs_init(n)` (569A:02CB) creates n open-file slots of 0x38 bytes at E868. main uses n=40.
- `fs_open(mode, name)` (569A:0342) **linearly searches every archive in load order**. It compares
  32 chars case-insensitively (a-z upcased) and the first match wins. It copies the entry into a
  free slot. If nothing matches it falls back to a real DOS open (5646:003A, slot flag +36h = 0).
- Read (053C) seeks the shared archive handle to `offset+pos`, clamps to size, and advances pos.
  On clamp it sets `pos = size-1` (sic).
- Seek is 074E, tell 08B2, close 04CA. Close does not close the shared archive handle.

main opens, in this order: 0gfl, muzik, sounds, frpos, ikon, ivir, savas, 3d.ckd, then one more
archive named in a stack buffer.

**Resource API (FILEMAN.L, ovl5447).**
- fm_load_cmp(memtype, name, slot) at 5447:052A.
- fm_load_pal(name, first, count) at 5447:08B9: reads count×3 bytes from file offset 0 into
  E5EF+first×3. It does **not** set the DAC; the caller does that with gr_set_palette or gr_fade.
- fm_load_file(memtype, name, mmblock) at 5447:093A: loads a raw file into a block, chunked
  through EMS. The RIX table is loaded this way.
- fm_filelength at 5447:0873.
- fm_change_ext at 5447:0B5F.

All of these hold mm_ems_lock(DE26) while they run and fail through "Resim Dosyası Bulunamadı!"
or "Dosya açma hatası!".

**CMP format (verified from code).**
```
u16 width, u16 height,
RLE stream to EOF (length = filesize-4):
  c = byte
  c >  0x80 : run   — count = c-0x80 (1..127), next byte = value  (written with memset32)
  c <= 0x80 : literal — copy next c bytes (0 = no-op, 0x80 = 128 bytes)
decoded size = width*height, 8-bit palette indices, row-major
```
Two decoders exist:
- fm_cmp_decode (5447:043B): 16-bit source index, returns the byte count. Used when w×h < 64K.
- fm_cmp_decode_huge (5447:0000): 32-bit indices. Into EMS it remaps a 64K window
  (1154:0688) whenever the in-window offset exceeds 0xC000; into conventional memory it uses huge
  pointers.

In both, the whole compressed file is first read into a farmalloc buffer. The surface block for
the slot is allocated with memtype 0 (conventional) or 1 (EMS), or converted if a block of the
other type already exists.

**EMS caching.** Resources are cached simply by being decoded straight into EMS-backed surface
slots or mm blocks: "EMS statik bloklar" (303F:394A), voice samples (ui_init_voices), and DSMI
virtual samples through the snd_ems_* hooks (MCPSampleRealAddress/Enable/DisableVirtualSamples).
main allocates EMS with `mm_init((ems_free + 0x20.0000 + 0x10.0000)/1024 KB)` and requires a
minimum.

## 4. MEMMAN (seg09D4) API

**Handle table.** DGROUP DA48 holds 110 entries of 9 bytes:
`{u16 off_or_emsHandle; u16 seg; u32 size; u8 type}`, where type is 0 = conventional (farmalloc),
1 = EMS, FFh = free. mm_alloc returns a near pointer into this table. Totals: 6E0A is the EMS bytes
allocated; DE30/DE32 and 6E06/6E08 are low-water marks of free memory. 6E02/6E04 is the EMS lib
state; nonzero means EMS is active.

**EMS backend.** The EMS library is seg1154/seg1134 (not in this group):
- 1154:010D init
- 0410 alloc
- 061B free
- 0688 map a 64K window at a u32 offset and return a far ptr
- 0963/0A0D/0AB7 INT 67h AH=57h moves: conv→EMS, EMS→conv, EMS→EMS
- 0BAB/0BF4 save/restore the page map
- 0C4D free bytes

**Lock discipline.** The current 4-page mapping is kept at 86F8 (4×u16).
- mm_ems_lock(ctx, file, line) copies it into ctx and sets ctx.busy; a nested lock is fatal
  ("Once line: %d, file: %s'dan simdi de line ...").
- mm_ems_unlock remaps from ctx.
- The global ctx DE26 is used by every blitter and loader.

mm_init oddly issues `INT 21h AH=3Ah AL=1 BX=1` first. This is RMDIR with garbage DS:DX and is
probably a debugger or memory-manager probe; it is harmless but must not be emulated as a real
rmdir.

## 5. Sound glue

**NSES.L / OSES.L.** oses_init (55CC:0000) reads **SES.CFG**: char[13] driver .DSD file name,
then 0x5C bytes of SDI device config (E7EC), then u16 at 8DDA. main prints 8DDA as
"Animasyon kalitesi". oses_init then:
- loads the driver through DDS (0DF2);
- resolves the imports MCPSampleRealAddress, MCPEnableVirtualSamples, MCPDisableVirtualSamples,
  GetEnvString and SetVector to the NSES hooks at 0D42:0005/0041/005E/00CF/00EB;
- resolves SDIInitDevice, SDIStartOutput and SDIStopOutput (8DD6, which is called at exit);
- registers the timer routines listed in section 2.

Samples go into a table of 49 entries of 0x17 bytes at DE34/DE38.

## 6. Other modules identified

- **ovl5511 = `ALTDUZEY\OGRAPHS.L`** (string "T:ALTDUZEY\OGRAPHS.L"): VGA ports, mode set, mouse
  INT 33h, palette fade.
- **ovl5447 = `ALTDUZEY\FILEMAN.L`**: the loader part of FILEMAN. Resident FILEMAN code is in
  seg0000, per PLAN.
- **ovl559A = error/exit module (name unknown).** o559A_fatal(file,line,msg,code):
  - prints "(%d) numaralı iç hata" with the error-string table at 8A34 (far ptrs) indexed by code;
  - guards against recursion with flag 750A&4 ("Çift hata");
  - stops AMP and SDI, frees DDS, closes the archive handles at B384, runs tsClose, sets mode 3,
    stops CD audio, loads the TRDOS.DAT font (INT 10h AX=1100h), runs mm_shutdown and
    ctl_uninstall, clears the BIOS shift flags, and calls exit(0).
- **ovl55CC = `ALTDUZEY\OSES.L`**: sound init and sample loading.
- **ovl5737 = CD-audio module (MSCDEX)**: INT 2Fh AX=1500h/1501h/1510h, IOCTL 0Ah/0Bh/0Ch,
  device requests 84h PLAY and 85h STOP. Its "port I/O" is MSCDEX device requests only. The
  0x3D4 hit in the summaries is not a port.
- seg0AC6 = SYSTEMS.L (blitters), seg0C7F = NGRAPHS.L (DAC, cursor, hotspots, fade),
  seg0D42 = NSES.L, seg0DB7 = NKONTROL.L, seg05DC = `CODE\INTRFACE.L` (the string says CODE\,
  not ALTDUZEY\): UI loop, dialogs, voice playback.

## 7. main() sequence (0546:0001)

1. Load the TRDOS font.
2. fs_init(40), then "Dosya sistemi hazır".
3. _OvrInitEms(0,0,18), which fails with the EMS message if it returns −1.
4. DDS and timer init.
5. oses_init(1). If SES.CFG is missing it prints "KUR.EXE'yi çalıştırınız".
6. Ask Evet/Hayır.
7. mm_init(EMS) with a minimum check, then a conventional memory check.
8. Clear the tables, then ctl_install, ctl_timer_install and oses_init(0).
9. ui_init_tables.
10. Open the archives.
11. Load the fonts (CMP slots 3/4/5/0x54) and xdintro.rix.
12. Run the FRPOS/3D/EMS-static init.
13. ogr_mouse_init, then ogr_init (mode 13h).
14. Intro (3C2D:054D).
15. Load grphazir/grp.dat, then the main loop through the 303F/3C2D modules.
16. "...SON..." and the ending.

## Open questions

- The exact meaning of SES.CFG's trailing u16 (8DDA) and when it is read twice.
- 0AC6 blit variants (0615/0C5A/0F21/10F1..1307) and 0C7F:08FC/0A51 need a closer look. Names are
  `[low]`.
- 5511:0018 (split screen) and the planar path are dead or used only by the credits. An emulator
  needs mode 12h only for the comp4 screens.
- 2599:0018/04AD/0500 (asm, 386): purpose not established (scaled/AA blits?).
