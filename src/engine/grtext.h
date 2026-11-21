/*
 * grtext.h - GRTEXT.L: the game's bitmap text (two fonts), cp857 Turkish
 * case helpers and a one-line text input field.
 * Original: IST.EXE ovl5784 (Borland C, far cdecl).
 * Implementation: src/engine/grtext.c.
 *
 * Fonts (loaded by main with fm_load_cmp(1, ..., slot)):
 *   slot 3     DENEYX.CMP  13 x 1000: font 1, 11-row cells (glyph g at
 *              row g*11, 0x8F bytes per glyph); the first byte of a cell
 *              is the glyph width - 1 (signed).  Read through mm_map.
 *   slot 4     GOCUK.CMP   3 x 320: font 2, 3x5 cells (glyph g at row g*5).
 *   slot 5     IREM6.CMP   20 x 620: big capitals for font 1 (cell g at
 *   slot 0x54  IREM62.CMP  row g*20, 19x19 from (1, g*20+1)); the caller
 *              passes the slot (5 or 54h).
 * Glyphs are drawn with colour key FFh (sys_blit_recolor: FFh pixels get
 * the text colour, 0 is transparent).
 *
 * Text is cp857 as the game writes it: Turkish letters 80h C-cedilla,
 * 81h u-umlaut, 87h c-cedilla, 8Dh dotless i, 94h o-umlaut, 98h I-dot,
 * 99h O-umlaut, 9Ah U-umlaut, 9Eh S-cedilla, 9Fh s-cedilla, A6h G-breve,
 * A7h g-breve.  0Ah (and B6h for font 1) is a line break.
 */
#ifndef LALE_ENGINE_GRTEXT_H
#define LALE_ENGINE_GRTEXT_H

#include <stdint.h>

#include "engine/dgroup.h"

/* ---- DGROUP ------------------------------------------------------------- */
#define g_grt_key_init     DG16(0x8F58)   /* " \0": initial value of the input
                                             field's 2-byte key buffer */
#define g_grt_cursor_str   0x8F5A         /* "-" (also 8F5C, 8F5F, 8F62): the
                                             input field's cursor character */
#define g_grt_file_name    0x8F65         /* "T:ALTDUZEY\GRTEXT.L" (also 8F79),
                                             mm_ems_lock file names */

#define GRT_FONT_MAIN 1   /* slot 3 + big capitals */
#define GRT_FONT_SMALL 2  /* slot 4, 3x5, 4 px per character */

/* ---- functions (original address order) ------------------------------------ */
uint8_t grt_input_line(char *s, int16_t font, uint8_t fg, uint8_t shadow, int16_t maxw,
                       int16_t x, int16_t y, uint8_t dst, int16_t bgx, int16_t bgy,
                       uint8_t bgslot);                                   /* 5784:0000 */
int16_t grt_text_width(const char *s, uint8_t font);                      /* 5784:06B4 */
int16_t grt_is_upper(uint8_t c);                                          /* 5784:08E7 */
void    grt_toupper_inplace(char *c);                                     /* 5784:0923 */
void    grt_tolower_inplace(char *c);                                     /* 5784:09D1 */
void    grt_strip_accent_inplace(char *c);                                /* 5784:0A7F */
void    grt_draw_text(uint8_t dst, int16_t x, int16_t y, const char *s, uint8_t color,
                      uint8_t bigslot);                                   /* 5784:0B43 */
void    grt_draw_text_alt(uint8_t dst, int16_t x, int16_t y, const char *s,
                          uint8_t color);                                 /* 5784:0DEA */
char   *grt_decode_turkish_lower(char *s);                                /* 5784:0F84 */
uint8_t grt_char_to_ascii(uint8_t c);                                     /* 5784:1048 */

#endif
