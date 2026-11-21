/*
 * grtext.c - GRTEXT.L: bitmap text, cp857 case helpers, text input field.
 * Original: IST.EXE ovl5784 (Borland C, far cdecl).  See grtext.h.
 */
#include "engine/grtext.h"

#include <string.h>

#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/systems.h"
#include "platform/pc.h"

/* both fonts of grt_input_line: shadow at (sx, sy), text at (tx, ty) */
static void draw_pair(int16_t font, uint8_t dst, const char *s, uint8_t fg, uint8_t shadow,
                      int16_t sx, int16_t sy, int16_t tx, int16_t ty)
{
    if (font == 1) {
        grt_draw_text(dst, sx, sy, s, shadow, 5);
        grt_draw_text(dst, tx, ty, s, fg, 5);
    } else if (font == 2) {
        grt_draw_text_alt(dst, sx, sy, s, shadow);
        grt_draw_text_alt(dst, tx, ty, s, fg);
    }
}

/* the whole text: font 1 shadow (x, y+1), font 2 shadow (x-1, y+1); text (x-1, y) */
static void draw_initial(int16_t font, uint8_t dst, const char *s, uint8_t fg, uint8_t shadow,
                         int16_t x, int16_t y)
{
    draw_pair(font, dst, s, fg, shadow, font == 1 ? x : (int16_t)(x - 1), (int16_t)(y + 1),
              (int16_t)(x - 1), y);
}

/*
 * grt_input_line - edit s in place on screen (Turkish letters typed as
 * their capital, see grt_decode_turkish_lower); 1 = Enter, 0 = Esc
 * (s restored).
 *
 * Original: IST.EXE 5784:0000 (GRTEXT.L), far cdecl (char far *s, int
 * font, u8 fg, u8 shadow, int maxw, int x, int y, u8 dst, int bgx, int bgy,
 * u8 bgslot).  The field's background is copied back from (bgx, bgy) of
 * bgslot, 12 rows high.  A "-" is appended as the cursor while editing
 * (and removed on Enter).  Font 1 draws the shadow at (+1, +1) of the
 * text, font 2 at (0, +1) (also when the whole text is drawn, at x-1;
 * font 1's whole-text shadow is at x); the first draw puts the text at x-1, later
 * characters at x + width.  The cursor (E5E3 shape) is updated while
 * waiting; extended keys are read and dropped.
 * Kept: the backup copy is mm_farmalloc(strlen(s)) - one byte short for
 * the terminator, which _fstrcpy writes past the block - and never freed.
 * A typed key is rejected when the text without cursor is already maxw
 * wide (signed); if the new character makes it wider than maxw it is
 * taken back without redrawing.  Backspace needs at least one character
 * besides the cursor.
 * Empty s: farmalloc(0) returns NULL, so the original copies the
 * terminator to 0000:0000 - the low byte of the INT 00h (divide error)
 * vector - and on Esc copies the "" it finds there back into s.  The port
 * uses a 1-byte stand-in for that address (the vector itself is not
 * emulated: divide errors go through the nk_div_* helpers), which gives the
 * same s; the clobbered vector byte is not modelled (it would only matter
 * for a later divide error reaching the game's INT 00h handler).
 * Port: the wait loop polls through pc_kbhit().
 * Verified: tests/py/test_grtext.py (test_input_line).
 */
static char g_far_null[1];       /* what 0000:0000 holds after the copy */

uint8_t grt_input_line(char *s, int16_t font, uint8_t fg, uint8_t shadow, int16_t maxw,
                       int16_t x, int16_t y, uint8_t dst, int16_t bgx, int16_t bgy,
                       uint8_t bgslot)
{
    char tmp[2];
    char *backup;
    uint8_t cw, key = 0;
    int16_t w1, w2;

    memcpy(tmp, &g_grt_key_init, 2);
    backup = mm_farmalloc(strlen(s));
    if (backup == NULL)              /* strlen(s) == 0: far NULL, see above */
        backup = g_far_null;
    strcpy(backup, s);
    cw = (uint8_t)grt_text_width(s, (uint8_t)font);
    strcat(s, DGSTR(0x8F5A));
    cw = (uint8_t)(grt_text_width(s, (uint8_t)font) - cw);
    gr_cursor_hide();
    draw_initial(font, dst, s, fg, shadow, x, y);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);

    while (key != 0x0D) {
        gr_cursor_update((uint8_t)g_cursor_shape);
        if (!pc_kbhit())
            continue;
        key = (uint8_t)pc_getch();
        switch (key) {
        case 0x00:
            pc_getch();
            break;
        case 0x08:
            if (strlen(s) <= 1)
                break;
            w1 = grt_text_width(s, (uint8_t)font);
            s[strlen(s) - 2] = 0;
            w2 = grt_text_width(s, (uint8_t)font);
            gr_cursor_hide();
            sys_blit((uint16_t)(bgx + w2), (uint16_t)bgy, (uint16_t)(w1 - w2), 12,
                     (uint16_t)(x + w2), (uint16_t)y, bgslot, dst, sys_rect_copy);
            strcat(s, DGSTR(0x8F5C));
            if (font == 1)
                draw_pair(1, dst, s + strlen(s) - 1, fg, shadow, x + w2 + 1, y + 1, x + w2, y);
            else if (font == 2)
                draw_pair(2, dst, s + strlen(s) - 1, fg, shadow, x + w2, y + 1, x + w2, y);
            gr_cursor_set_shape((uint8_t)g_cursor_shape);
            break;
        case 0x0D:
            key = 0x0D;
            break;
        case 0x1B:
            w1 = grt_text_width(s, (uint8_t)font);
            strcpy(s, backup);
            gr_cursor_hide();
            sys_blit((uint16_t)bgx, (uint16_t)bgy, (uint16_t)w1, 12, (uint16_t)x, (uint16_t)y,
                     bgslot, dst, sys_rect_copy);
            draw_initial(font, dst, s, fg, shadow, x, y);
            gr_cursor_set_shape((uint8_t)g_cursor_shape);
            return 0;
        default:
            w1 = (int16_t)(grt_text_width(s, (uint8_t)font) - cw);
            if (w1 >= maxw)
                break;
            tmp[0] = (char)key;
            grt_decode_turkish_lower(tmp);
            s[strlen(s)] = 0;
            s[strlen(s) - 1] = tmp[0];
            if (grt_text_width(s, (uint8_t)font) > maxw) {
                s[strlen(s) - 1] = 0;
                strcat(s, DGSTR(0x8F5F));
                break;
            }
            strcat(s, DGSTR(0x8F62));
            gr_cursor_hide();
            sys_blit((uint16_t)(bgx + w1), (uint16_t)bgy, cw, 12, (uint16_t)(x + w1),
                     (uint16_t)y, bgslot, dst, sys_rect_copy);
            if (font == 1)
                draw_pair(1, dst, s + strlen(s) - 2, fg, shadow, x + w1 + 1, y + 1, x + w1, y);
            else if (font == 2)
                draw_pair(2, dst, s + strlen(s) - 2, fg, shadow, x + w1, y + 1, x + w1, y);
            gr_cursor_set_shape((uint8_t)g_cursor_shape);
            break;
        }
    }
    w1 = (int16_t)(grt_text_width(s, (uint8_t)font) - cw);
    gr_cursor_hide();
    sys_blit((uint16_t)(bgx + w1), (uint16_t)bgy, cw, 12, (uint16_t)(x + w1), (uint16_t)y,
             bgslot, dst, sys_rect_copy);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
    s[strlen(s) - 1] = 0;
    return 1;
}

/*
 * grt_text_width - width of s in pixels, minus 1.
 *
 * Original: IST.EXE 5784:06B4 (GRTEXT.L), far cdecl (char far *s, u8 font).
 * Font 1: glyph cells (see grt_draw_text) are width byte + 2 each, big
 * capitals 20, anything else 3; font 2: 4 per character; other fonts 0
 * (so -1).  Unlike grt_draw_text the ranges are tested with SIGNED chars
 * and '*' counts as a glyph (grt_draw_text draws it as a blank).  Each
 * glyph width is read through mm_map under the blit EMS lock.
 * Verified: tests/py/test_grtext.py (test_text_width).
 */
int16_t grt_text_width(const char *s, uint8_t font)
{
    int16_t width = 0, g = 0;
    uint16_t i, len = 1, slot = 0;
    uint8_t *p;

    if (font == 1) {
        for (i = 0; i < strlen(s); i++) {
            int8_t c = (int8_t)s[i];
            int kind = 1;
            if (c >= 0x21 && c <= 0x40)
                g = c - 0x21;
            else if (c >= 0x61 && c <= 0x7A)
                g = c - 0x41;
            else if (c >= 0x5B && c <= 0x60)
                g = c - 0x21;
            else if (c >= 0x7B && (uint8_t)c <= 0x7F)
                g = c - 0x3B;
            else if (c >= 0x41 && c <= 0x5B) {
                g = c - 0x41;
                kind = 2;
            } else {
                switch ((uint8_t)c) {
                case 0x80: case 0x98: case 0x99: case 0x9A: case 0x9E: kind = 2; break;
                case 0x81: g = 0x4B; break;
                case 0x87: g = 0x46; break;
                case 0x8D: g = 0x48; break;
                case 0x94: g = 0x49; break;
                case 0x9F: g = 0x4A; break;
                case 0xA6: g = 0x45; break;
                case 0xA7: g = 0x47; break;
                default:
                    width += 3;
                    kind = 0;
                    break;
                }
            }
            if (kind == 1) {
                mm_ems_lock(g_blit_lock, 0xA9, DGSTR(0x8F65));
                len = 1;
                slot = 0;
                mm_map(mm_surface(3), &p, &len, &slot, (uint32_t)(uint16_t)(g * 0x8F));
                width += (int8_t)*p + 2;
                mm_ems_unlock(g_blit_lock);
            } else if (kind == 2) {
                width += 0x14;
            }
        }
    } else if (font == 2) {
        width = (int16_t)(strlen(s) << 2);
    }
    return (int16_t)(width - 1);
}

/*
 * grt_is_upper - 1 for 'A'..'[' and the cp857 capitals 80h 98h 99h 9Ah
 * 9Eh A6h, else 0.
 *
 * Original: IST.EXE 5784:08E7 (GRTEXT.L), far cdecl (u8 c).
 * Verified: tests/py/test_grtext.py (test_case_helpers, all 256 bytes).
 */
int16_t grt_is_upper(uint8_t c)
{
    if (c >= 0x41 && c <= 0x5B)
        return 1;
    return c == 0x80 || c == 0x98 || c == 0x99 || c == 0x9E || c == 0x9A || c == 0xA6;
}

/*
 * grt_toupper_inplace - Turkish upper case of *c: 'a'..'z' except 'i'
 * -20h; i -> 98h (I-dot), 81h -> 9Ah, 87h -> 80h, 8Dh (dotless i) -> 'I',
 * 94h -> 99h, 9Fh -> 9Eh, A7h -> A6h.
 *
 * Original: IST.EXE 5784:0923 (GRTEXT.L), far cdecl (char far *c).
 * Verified: tests/py/test_grtext.py (test_case_helpers, all 256 bytes).
 */
void grt_toupper_inplace(char *c)
{
    uint8_t v = (uint8_t)*c;
    if (v >= 0x61 && v <= 0x7A && v != 0x69) {
        *c = (char)(v - 0x20);
        return;
    }
    switch (v) {
    case 0x69: *c = (char)0x98; break;
    case 0x81: *c = (char)0x9A; break;
    case 0x87: *c = (char)0x80; break;
    case 0x8D: *c = (char)0x49; break;
    case 0x94: *c = (char)0x99; break;
    case 0x9F: *c = (char)0x9E; break;
    case 0xA7: *c = (char)0xA6; break;
    }
}

/*
 * grt_tolower_inplace - Turkish lower case of *c: 'A'..'[' except 'I'
 * +20h ('[' becomes '{'); I -> 8Dh (dotless i), 80h -> 87h, 98h -> 'i',
 * 99h -> 94h, 9Ah -> 81h, 9Eh -> 9Fh, A6h -> A7h.
 *
 * Original: IST.EXE 5784:09D1 (GRTEXT.L), far cdecl (char far *c).
 * Verified: tests/py/test_grtext.py (test_case_helpers, all 256 bytes).
 */
void grt_tolower_inplace(char *c)
{
    uint8_t v = (uint8_t)*c;
    if (v >= 0x41 && v <= 0x5B && v != 0x49) {
        *c = (char)(v + 0x20);
        return;
    }
    switch (v) {
    case 0x49: *c = (char)0x8D; break;
    case 0x80: *c = (char)0x87; break;
    case 0x98: *c = (char)0x69; break;
    case 0x99: *c = (char)0x94; break;
    case 0x9A: *c = (char)0x81; break;
    case 0x9E: *c = (char)0x9F; break;
    case 0xA6: *c = (char)0xA7; break;
    }
}

/*
 * grt_strip_accent_inplace - the cp857 Turkish letters to plain ASCII
 * (80h -> C, 81h -> u, 87h -> c, 8Dh -> i, 94h -> o, 98h -> I, 99h -> O,
 * 9Ah -> U, 9Eh -> S, 9Fh -> s, A6h -> G, A7h -> g).
 *
 * Original: IST.EXE 5784:0A7F (GRTEXT.L), far cdecl (char far *c).
 * Verified: tests/py/test_grtext.py (test_case_helpers, all 256 bytes).
 */
void grt_strip_accent_inplace(char *c)
{
    switch ((uint8_t)*c) {
    case 0x80: *c = 'C'; break;
    case 0x81: *c = 'u'; break;
    case 0x87: *c = 'c'; break;
    case 0x8D: *c = 'i'; break;
    case 0x94: *c = 'o'; break;
    case 0x98: *c = 'I'; break;
    case 0x99: *c = 'O'; break;
    case 0x9A: *c = 'U'; break;
    case 0x9E: *c = 'S'; break;
    case 0x9F: *c = 's'; break;
    case 0xA6: *c = 'G'; break;
    case 0xA7: *c = 'g'; break;
    }
}

/*
 * grt_draw_text - draw s with font 1 into surface dst at (x, y).
 *
 * Original: IST.EXE 5784:0B43 (GRTEXT.L), far cdecl (u8 dst, int x, int y,
 * char far *s, u8 color, u8 bigslot).  UNSIGNED character ranges:
 *   21h-40h except '*'  glyph c-21h      61h-7Ah  glyph c-41h
 *   5Bh-60h             glyph c-21h      7Bh-7Fh  glyph c-3Bh
 *   cp857 lower case    glyphs 45h-4Bh (81h 4B, 87h 46, 8Dh 48, 94h 49,
 *                       9Fh 4A, A6h 45, A7h 47 - G-breve has no capital)
 *   41h-5Bh             big capital c-41h; 80h 1A, 98h 1B, 99h 1C,
 *                       9Ah 1E, 9Eh 1D
 *   0Ah, B6h            line break: y += 10, x = start
 *   anything else       3 px blank
 * A glyph (width byte w from slot 3 through mm_map under the blit EMS
 * lock) is drawn (w+1) x 11 from (0, g*11) of slot 3 with sys_blit_recolor
 * (FFh -> color), advance w+2; a big capital is the 19x19 cell (1, g*20+1)
 * of bigslot copied opaque (2599:02D0) to (x, y-5), advance 20.
 * Kept: mm_map's length variable is set to 1 once, not per glyph.
 * Verified: tests/py/test_grtext.py (test_draw_text: all characters, both
 * big slots, on the real fonts).
 */
void grt_draw_text(uint8_t dst, int16_t x, int16_t y, const char *s, uint8_t color,
                   uint8_t bigslot)
{
    int16_t x0 = x, g = 0, w = 0;
    uint16_t i, len = 1, slot;
    uint8_t *p;

    for (i = 0; i < strlen(s); i++) {
        uint8_t c = (uint8_t)s[i];
        int kind = 1;
        if (c >= 0x21 && c <= 0x40 && c != 0x2A)
            g = (int16_t)(c - 0x21);
        else if (c >= 0x61 && c <= 0x7A)
            g = (int16_t)(c - 0x41);
        else if (c >= 0x5B && c <= 0x60)
            g = (int16_t)(c - 0x21);
        else if (c >= 0x7B && c <= 0x7F)
            g = (int16_t)(c - 0x3B);
        else if (c >= 0x41 && c <= 0x5B) {
            g = (int16_t)(c - 0x41);
            kind = 2;
        } else {
            switch (c) {
            case 0x0A:
            case 0xB6:
                y = (int16_t)(y + 10);
                x = x0;
                continue;
            case 0x80: g = 0x1A; kind = 2; break;
            case 0x98: g = 0x1B; kind = 2; break;
            case 0x99: g = 0x1C; kind = 2; break;
            case 0x9A: g = 0x1E; kind = 2; break;
            case 0x9E: g = 0x1D; kind = 2; break;
            case 0x81: g = 0x4B; break;
            case 0x87: g = 0x46; break;
            case 0x8D: g = 0x48; break;
            case 0x94: g = 0x49; break;
            case 0x9F: g = 0x4A; break;
            case 0xA6: g = 0x45; break;
            case 0xA7: g = 0x47; break;
            default:
                w = 2;
                kind = 0;
                break;
            }
        }
        if (kind == 1) {
            mm_ems_lock(g_blit_lock, 0x121, DGSTR(0x8F79));
            slot = 0;
            mm_map(mm_surface(3), &p, &len, &slot, (uint32_t)(uint16_t)(g * 0x8F));
            w = (int16_t)((int8_t)*p + 1);
            mm_ems_unlock(g_blit_lock);
            sys_blit_recolor(0, (uint16_t)(g * 11), (uint16_t)w, 11, (uint16_t)x, (uint16_t)y,
                             3, dst, 0xFF, color);
        } else if (kind == 2) {
            w = 0x13;
            sys_blit(1, (uint16_t)(g * 0x14 + 1), 0x13, 0x13, (uint16_t)x, (uint16_t)(y - 5),
                     bigslot, dst, sys_rect_copy);
        }
        x = (int16_t)(x + w + 1);
    }
}

/*
 * grt_draw_text_alt - draw s with font 2 (3x5, slot 4) into dst at (x, y).
 *
 * Original: IST.EXE 5784:0DEA (GRTEXT.L), far cdecl (u8 dst, int x, int y,
 * char far *s, u8 color).  Upper and lower case share the cells A-Z
 * (0-25), the Turkish letters are 1Ah-1Fh (C/c-cedilla 1A, G-breve 1B,
 * I-dot/i 1C, O-umlaut 1D, S-cedilla 1E, U-umlaut 1F; dotless i = 'I');
 * 21h-40h are cells c-1.  0Ah breaks the line (y += 7, back to the start
 * x); a glyph that would reach x+4 >= 320 (signed) wraps first.  Glyphs
 * are 3x5 from (0, g*5) of slot 4 with recolor FFh -> color; every
 * character but a line break advances 4 (unknown ones are blanks).
 * Verified: tests/py/test_grtext.py (test_draw_text).
 */
void grt_draw_text_alt(uint8_t dst, int16_t x, int16_t y, const char *s, uint8_t color)
{
    int16_t x0 = x, g;
    uint16_t i;

    for (i = 0; i < strlen(s); i++) {
        uint8_t c = (uint8_t)s[i];
        if (c >= 0x61 && c <= 0x7A && c != 0x69)
            g = (int16_t)(c - 0x61);
        else if (c >= 0x41 && c <= 0x5A)
            g = (int16_t)(c - 0x41);
        else if (c >= 0x21 && c <= 0x40)
            g = (int16_t)(c - 1);
        else {
            switch (c) {
            case 0x0A:
                y = (int16_t)(y + 7);
                x = x0;
                continue;
            case 0x80: case 0x87: g = 0x1A; break;
            case 0xA6: case 0xA7: g = 0x1B; break;
            case 0x69: case 0x98: g = 0x1C; break;
            case 0x8D: g = 0x08; break;
            case 0x94: case 0x99: g = 0x1D; break;
            case 0x9E: case 0x9F: g = 0x1E; break;
            case 0x81: case 0x9A: g = 0x1F; break;
            default:
                x = (int16_t)(x + 4);
                continue;
            }
        }
        if ((int16_t)(x + 4) >= 0x140) {
            y = (int16_t)(y + 7);
            x = x0;
        }
        sys_blit_recolor(0, (uint16_t)(g * 5), 3, 5, (uint16_t)x, (uint16_t)y, 4, dst, 0xFF,
                         color);
        x = (int16_t)(x + 4);
    }
}

/*
 * grt_decode_turkish_lower - the input field's Turkish keys: capitals
 * C G I O S U become the cp857 lower-case letters (87h A7h 8Dh 94h 9Fh
 * 81h), then every character goes through grt_tolower_inplace.  Returns s.
 *
 * Original: IST.EXE 5784:0F84 (GRTEXT.L), far cdecl (char far *s).
 * strlen is re-evaluated per character.
 * Verified: tests/py/test_grtext.py (test_case_helpers).
 */
char *grt_decode_turkish_lower(char *s)
{
    uint16_t i;
    for (i = 0; i < strlen(s); i++) {
        switch (s[i]) {
        case 'C': s[i] = (char)0x87; break;
        case 'G': s[i] = (char)0xA7; break;
        case 'I': s[i] = (char)0x8D; break;
        case 'O': s[i] = (char)0x94; break;
        case 'S': s[i] = (char)0x9F; break;
        case 'U': s[i] = (char)0x81; break;
        }
        grt_tolower_inplace(&s[i]);
    }
    return s;
}

/*
 * grt_char_to_ascii - the cp857 lower-case Turkish letters as their plain
 * ASCII letter (81h u, 87h c, 8Dh i, 94h o, 9Fh s, A7h g), else c.
 *
 * Original: IST.EXE 5784:1048 (GRTEXT.L), far cdecl (u8 c).
 * Verified: tests/py/test_grtext.py (test_case_helpers, all 256 bytes).
 */
uint8_t grt_char_to_ascii(uint8_t c)
{
    switch (c) {
    case 0x81: return 'u';
    case 0x87: return 'c';
    case 0x8D: return 'i';
    case 0x94: return 'o';
    case 0x9F: return 's';
    case 0xA7: return 'g';
    }
    return c;
}
