/*
 * ngraphs.h - ALTDUZEY\NGRAPHS.L: DAC/palette upload, the software mouse
 * cursor (15x15 shapes, save-under), mouse hotspot zones, palette + music
 * fades and two EMS-mapped blend helpers.
 * Original: IST.EXE seg0C7F (Borland C + inline asm, far cdecl).
 * Implementation: src/engine/ngraphs.c.
 *
 * Cursor.  Shapes are 15x15 cells of the strip in surface slot 4Bh; six
 * consecutive shapes are cached in slot 9 (90x15, first shape number in
 * g_cursor_strip), the screen under the cursor is saved in slot 10 (15x15).
 * The cursor is drawn into slot 0 (the screen) with colour key FEh.
 *
 * Mouse.  The driver is called through Borland's int86x(33h, &in, &out,
 * &sregs) with the REGS unions in DGROUP (E766 in, E756 out, E74E sregs);
 * gr_int33() is the port of that call (pc_int33, same DGROUP writes).
 */
#ifndef LALE_ENGINE_NGRAPHS_H
#define LALE_ENGINE_NGRAPHS_H

#include <stdint.h>

#include "engine/dgroup.h"
#include "engine/memman.h"

/* ---- DGROUP ------------------------------------------------------------- */
#define g_mouse_lim_x      DGS16(0x7314)  /* i16  x + w + 1 of the mouse window */
#define g_mouse_lim_y      DGS16(0x7316)  /* i16  y + h + 1 */
#define g_pal_level        DGS16(0x7318)  /* i16  current palette brightness (0..g_pal_level_max) */
#define g_music_level      DGS16(0x731A)  /* i16  current music volume */
#define g_pal_level_max    DGS16(0x731E)  /* i16  full brightness */
#define g_music_level_max  DGS16(0x7320)  /* i16  full volume */
#define g_mouse_present    DG8(0x7324)    /* u8   1 = mouse driver found */
#define g_mouse_shift      DG8(0x7325)    /* u8   driver x >> shift = pixel x (1: 640 range) */
#define g_cursor_strip     DGS16(0xE5E1)  /* i16  first shape cached in slot 9 (8001h = none) */
#define g_cursor_shape     DGS16(0xE5E3)  /* i16  current cursor shape */
#define g_mouse_buttons    DGS16(0xE5E5)  /* i16  buttons at the last cursor update */
#define g_cursor_y         DGS16(0xE5E7)  /* i16  cursor position (pixels) */
#define g_cursor_x         DGS16(0xE5E9)
#define g_cursor_visible   DGS16(0xE5F3)  /* i16  cursor drawn (save-under valid) */
/* E5F5: 41 hotspot rectangles {i16 x1, y1, x2, y2} (index 0 unused by
   gr_zone_under_cursor, which scans 1..40) */
#define GR_ZONES           41
#define g_zone_x1(i)       DGS16(0xE5F5 + 8 * (i))
#define g_zone_y1(i)       DGS16(0xE5F7 + 8 * (i))
#define g_zone_x2(i)       DGS16(0xE5F9 + 8 * (i))
#define g_zone_y2(i)       DGS16(0xE5FB + 8 * (i))
/* Borland union REGS / struct SREGS of the mouse calls */
#define g_int33_sregs      0xE74E         /* es, cs, ss, ds */
#define g_int33_out_ax     DG16(0xE756)   /* out: ax bx cx dx si di cflag flags */
#define g_int33_out_bx     DG16(0xE758)
#define g_int33_out_cx     DG16(0xE75A)
#define g_int33_out_dx     DG16(0xE75C)
#define g_int33_in_ax      DG16(0xE766)   /* in:  ax bx cx dx si di cflag flags */
#define g_int33_in_cx      DG16(0xE76A)
#define g_int33_in_dx      DG16(0xE76C)

/* E5EB / E5EF: far pointers to the two 768-byte palettes (ogr_init
   allocates them).  Native pointers; the dg[] bytes stay unused. */
extern uint8_t *gr_work_pal;     /* 16E0:E5EB  palette sent to the DAC (faded copy) */
extern uint8_t *gr_target_pal;   /* 16E0:E5EF  palette at full brightness */

/* port of int86x(0x33, E766, E756, E74E) */
void gr_int33(void);

/* ---- functions (original address order) ------------------------------------ */
void     gr_set_dac(uint8_t start, uint16_t count, const uint8_t *pal, uint16_t chunks); /* 0C7F:000C */
void     gr_set_palette(const uint8_t *pal);                                 /* 0C7F:0050 */
void     gr_cursor_set_shape(uint8_t shape);                                 /* 0C7F:0069 */
void     gr_cursor_hide(void);                                               /* 0C7F:0204 */
void     gr_cursor_update(uint8_t shape);                                    /* 0C7F:026B */
void     gr_call_then_cursor(int16_t (*fn)(void));                           /* 0C7F:04A1 */
int16_t  gr_mouse_read(int16_t *x, int16_t *y, int16_t *buttons);            /* 0C7F:04AF */
int16_t  gr_zone_under_cursor(void);                                         /* 0C7F:0515 */
uint8_t  gr_cursor_in_zone(uint8_t n);                                       /* 0C7F:05C7 */
uint8_t  gr_cursor_in_rect(int16_t x, int16_t y, int16_t w, int16_t h);      /* 0C7F:067D */
void     gr_zones_clear(void);                                               /* 0C7F:06B6 */
void     gr_wait_buttons_up(void);                                           /* 0C7F:0714 */
void     gr_fade(uint16_t ms, int16_t d_bright, int16_t d_volume);           /* 0C7F:073F */
void     gr_blend3(MmHandle *src1, MmHandle *src2, MmHandle *dst,
                   const uint8_t *table);                                    /* 0C7F:08FC */
void     gr_draw_strip(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t slot,
                       uint8_t color);                                       /* 0C7F:0A51 */

#endif
