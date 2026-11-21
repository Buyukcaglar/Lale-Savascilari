/*
 * fatal.h - ovl559A: fatal error exit, the Turkish text-mode font, beep,
 * table clearing.  Original: IST.EXE ovl559A (far cdecl).
 * Implementation: src/engine/fatal.c.
 *
 * Text output.  The original prints with printf/puts to DOS stdout after
 * switching to text mode 3.  The port has no text-mode console yet: the
 * text goes to fatal_console (default: host stderr, raw cp857 bytes); the
 * font o559A_load_trdos_font loads is kept in o559A_text_font for such a
 * console.
 *
 * Exit.  exit(0) becomes bc_exit_streams() + the host exit().  Tests set
 * o559A_test_mode (see below) so that fatal returns instead.
 */
#ifndef LALE_ENGINE_FATAL_H
#define LALE_ENGINE_FATAL_H

#include <stdint.h>

#include "engine/dgroup.h"

/* ---- DGROUP ------------------------------------------------------------- */
/* 16E0:8A34 char far *[]: message per error code (NULL = none), DGROUP
   strings; set for 3Fh-42h and 5Bh.  The table runs into the strings at
   8C0C, so codes >= 76h read text bytes as pointers (no caller does). */
#define O559A_MSG_TABLE     0x8A34
#define g_o559A_msg_off(c)  DG16(O559A_MSG_TABLE + 4 * (uint16_t)(c))
#define g_o559A_msg_seg(c)  DG16(O559A_MSG_TABLE + 4 * (uint16_t)(c) + 2)
/* 16E0:8DD6 void (far *)(void): called before the sound driver is unloaded.
   Nothing in the image assigns it (0 at main()): native o559A_sound_hook. */
/* 16E0:B384 int[8]: archive slots mounted by ist_main (unmounted here) */
#define g_archive_slot(i)   DGS16(0xB384 + 2 * (i))
/* 16E0:E85C u32: DDS handle of the loaded sound driver (DDSLoadDriver) */
#define g_sound_driver      DG32(0xE85C)
/* 16E0:DE34: 49 sample slots of 17h bytes; +0 far MmHandle * (see memman.h
   mm_ref) */
#define SAMPLE_SLOTS        0x31
#define SAMPLE_SLOT(i)      (0xDE34 + 0x17 * (i))

/* g_sys_flags (750Ah, memman.h): bit 1 = mm_shutdown running (fatal sets it
   around the call), bit 2 = fatal in progress (a second fatal only prints). */

extern void (*o559A_sound_hook)(void);   /* 16E0:8DD6 far fn ptr */

/* ---- port: text console, font, tests ------------------------------------ */
/* Where printf/puts of this module go (host side).  Default: stderr. */
extern void (*fatal_console)(const char *text, uint16_t len);
/* printf()/puts() to that console, for the diagnostics other modules print
   right before calling o559A_fatal (formats %d %u %s %lu - pass unsigned
   long for %lu). */
void fatal_printf(const char *fmt, ...);
void fatal_puts(const char *s);

/* The 8x16 font INT 10h AX=1100h loaded (BH=16 bytes/char, BL=block 0,
   CX=256 chars from DX=0): TRDOS.DAT, 4096 bytes. */
extern uint8_t o559A_text_font[256 * 16];
extern int16_t o559A_text_font_loaded;

/* o559A_test_mode: 0 = the game (fatal exits); 1 = record the call and
   return at once (stand-in for tests of other modules, like the old
   tests/stubs/wp1_stubs.c); 2 = run the whole shutdown, record, return
   instead of exit(0).  Records: */
extern int16_t o559A_test_mode;
extern int16_t o559A_fatal_count, o559A_fatal_code, o559A_fatal_line;

/* ---- functions (original address order) ---------------------------------- */
void o559A_fatal(const char *file, int16_t line, const char *msg, int16_t code); /* 559A:0000 */
void o559A_load_trdos_font(void);                                                /* 559A:01F3 */
void o559A_beep(uint16_t freq, uint16_t ms);                                     /* 559A:0291 */
void o559A_clear_tables(void);                                                   /* 559A:02AD */

#endif
