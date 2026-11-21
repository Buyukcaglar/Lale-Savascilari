/*
 * ographs.h - OGRAPHS.L: video mode set, VGA register helpers (map mask,
 * split line, start address, screen on/off), black palette, screen clear,
 * mouse set-up, the stepwise palette fade and graphics start-up.
 * Original: IST.EXE ovl5511 (overlay, Borland C + inline asm, far cdecl).
 * Implementation: src/engine/ographs.c.
 */
#ifndef LALE_ENGINE_OGRAPHS_H
#define LALE_ENGINE_OGRAPHS_H

#include <stdint.h>

#include "engine/dgroup.h"

/* ---- DGROUP ------------------------------------------------------------- */
#define g_pal_fade_busy   DG8(0xE7EA)    /* u8   ogr_palette_step_fade: a single-step
                                            fade is in progress (palette already set up) */
/* E5D8: the screen's 9-byte MEMMAN record (slot 0 = A000:0000, size FF00h) */
#define G_SCREEN_REC      0xE5D8

/* ---- functions (original address order) ------------------------------------ */
void    ogr_int10(uint16_t ax);                                          /* 5511:0000 */
void    ogr_set_map_mask(uint8_t mask);                                  /* 5511:000A */
void    ogr_set_split_line(uint16_t line);                               /* 5511:0018 */
void    ogr_set_start_addr(uint32_t start);                              /* 5511:0090 */
void    ogr_screen_on(void);                                             /* 5511:00E1 */
void    ogr_screen_off(void);                                            /* 5511:00FD */
void    ogr_black_palette(void);                                         /* 5511:0119 */
void    ogr_clear_screen(void);                                          /* 5511:015B */
void    ogr_mouse_init(void);                                            /* 5511:017C */
void    ogr_mouse_set_pos(int16_t x, int16_t y);                         /* 5511:02A1 */
void    ogr_mouse_set_window(int16_t x, int16_t y, int16_t w, int16_t h); /* 5511:02F3 */
uint8_t ogr_palette_step_fade(uint8_t flags, const char *pal_name, uint16_t delay); /* 5511:03DF */
void    ogr_init(void);                                                  /* 5511:06F0 */
void    ogr_copy_pal_entries(uint8_t *p);                                /* 5511:0830 */

#endif
