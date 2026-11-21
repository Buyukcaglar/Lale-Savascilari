/*
 * ngraphs.c - ALTDUZEY\NGRAPHS.L: DAC upload, software mouse cursor,
 * hotspot zones, palette/music fade, EMS blend helpers.
 * Original: IST.EXE seg0C7F (Borland C + inline asm, far cdecl).
 * See ngraphs.h.
 */
#include "engine/ngraphs.h"

#include <stdio.h>
#include <string.h>

#include "dsmi/amp.h"
#include "engine/blit386.h"
#include "engine/memman.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "platform/pc.h"
#include "platform/vga.h"

uint8_t *gr_work_pal;            /* 16E0:E5EB */
uint8_t *gr_target_pal;          /* 16E0:E5EF */

/*
 * gr_int33 - Borland int86x(0x33, &in, &out, &sregs) with the REGS at E766
 * (in), E756 (out), SREGS at E74E.  int86x loads AX BX CX DX SI DI from
 * `in` and DS/ES from sregs, calls the driver, then stores AX BX CX DX SI
 * DI, cflag = FLAGS & 1 and FLAGS into `out` and ES/DS back into sregs.
 * The driver returns with the caller's flags (IRET); at the INT those are
 * the ones of int86x's "CMP AL,26h" (AL = 33h): AF, IF and bit 1 = 0212h,
 * carry clear (so __IOerror is never called).  SI, DI and the segment
 * registers are not changed by the driver functions the game uses, so
 * out.si/out.di = in.si/in.di and sregs keep their value.
 * Original: IST.EXE 0000:2C51 (int86x, Borland RTL) at the call sites of
 * 0C7F:04AF and ovl5511.
 * Verified: through gr_mouse_read / ogr_mouse_* (test_graphics.py).
 */
void gr_int33(void)
{
    PcMouseRegs r;
    r.ax = DG16(0xE766);
    r.bx = DG16(0xE768);
    r.cx = DG16(0xE76A);
    r.dx = DG16(0xE76C);
    pc_int33(&r);
    DG16(0xE756) = r.ax;
    DG16(0xE758) = r.bx;
    DG16(0xE75A) = r.cx;
    DG16(0xE75C) = r.dx;
    DG16(0xE75E) = DG16(0xE76E);      /* si */
    DG16(0xE760) = DG16(0xE770);      /* di */
    DG16(0xE762) = 0;                 /* cflag */
    DG16(0xE764) = 0x0212;            /* flags */
}

/*
 * gr_set_dac - load `count` DAC entries from `start` in `chunks` parts,
 * each written right after a vertical retrace has ended (wait for bit 3
 * of 3DAh set, then clear, then REP OUTSB to 3C9h).  PEL mask is set to FFh.
 *
 * Original: IST.EXE 0C7F:000C (NGRAPHS.L), far cdecl (start, count, pal,
 * chunks), inline asm.  Part size = remaining / chunks_left (DIV of the
 * 3-byte form, so the INT 00h handler semantics apply: chunks 0 -> FFFFh).
 * Verified: tests/py/test_graphics.py (test_set_dac).
 */
void gr_set_dac(uint8_t start, uint16_t count, const uint8_t *pal, uint16_t chunks)
{
    uint16_t n, cx;
    vga_out8(&pc_vga, 0x3C6, 0xFF);
    vga_out8(&pc_vga, 0x3C8, start);
    do {
        n = nk_div_u16(count, chunks, NULL);
        count -= n;
        cx = (uint16_t)(n * 3);
        while (!(pc_in_3da() & 8))
            ;
        while (pc_in_3da() & 8)
            ;
        while (cx--)
            vga_out8(&pc_vga, 0x3C9, *pal++);
    } while (--chunks != 0);
}

/*
 * gr_set_palette - all 256 DAC entries in one part.
 *
 * Original: IST.EXE 0C7F:0050 (NGRAPHS.L), far cdecl (pal).
 * Verified: tests/py/test_graphics.py (test_set_dac).
 */
void gr_set_palette(const uint8_t *pal)
{
    gr_set_dac(0, 0x100, pal, 1);
}

/* the min(.., 15) of the cursor blits (signed compares) */
static uint16_t clip15(int16_t lim, int16_t pos)
{
    int16_t v = (int16_t)(lim - pos);
    return (uint16_t)(v > 15 ? 15 : v);
}

/* cache six shapes from slot 4Bh into slot 9, starting at shape `base` */
static void load_strip(uint8_t base)
{
    int16_t b15 = (int16_t)(base * 15);
    int16_t w = (int16_t)(g_surf_w(0x4B) - b15);
    sys_blit((uint16_t)b15, 0, (uint16_t)(w > 0x5A ? 0x5A : w), 0x0F, 0, 0, 0x4B, 9,
             sys_rect_copy);
    g_cursor_strip = base;
}

/*
 * gr_cursor_set_shape - select cursor shape n and draw it at the current
 * mouse position (or, if the cursor is visible with the same shape, just
 * update it).
 *
 * Original: IST.EXE 0C7F:0069 (NGRAPHS.L), far cdecl (shape).  The strip
 * base here is max(shape - 3, 0) (gr_cursor_update also caps it at the
 * strip end).
 * Verified: tests/py/test_graphics.py (test_cursor).
 */
void gr_cursor_set_shape(uint8_t shape)
{
    uint8_t base = (int16_t)(shape - 3) > 0 ? (uint8_t)(shape - 3) : 0;

    if (!g_mouse_present)
        return;
    if (g_cursor_visible != 0) {
        if (shape != g_cursor_shape) {
            gr_cursor_hide();
        } else {
            gr_cursor_update(shape);
            return;
        }
    }
    if (shape < g_cursor_strip || shape >= (int16_t)(g_cursor_strip + 6))
        load_strip(base);
    {   /* gr_mouse_read(&E5E9, &E5E7, &E5E5): through aligned temporaries */
        int16_t x, y, b;
        gr_mouse_read(&x, &y, &b);
        g_cursor_x = x;
        g_cursor_y = y;
        g_mouse_buttons = b;
    }
    sys_blit((uint16_t)g_cursor_x, (uint16_t)g_cursor_y,
             clip15(g_mouse_lim_x, g_cursor_x), clip15(g_mouse_lim_y, g_cursor_y),
             0, 0, 0, 10, sys_rect_copy);
    sys_blit((uint16_t)((shape - g_cursor_strip) * 15), 0,
             clip15(g_mouse_lim_x, g_cursor_x), clip15(g_mouse_lim_y, g_cursor_y),
             (uint16_t)g_cursor_x, (uint16_t)g_cursor_y, 9, 0, asm_copy_rect_key);
    g_cursor_visible = 1;
    g_cursor_shape = shape;
}

/*
 * gr_cursor_hide - restore the save-under at the cursor position.
 *
 * Original: IST.EXE 0C7F:0204 (NGRAPHS.L), far cdecl.
 * Verified: tests/py/test_graphics.py (test_cursor).
 */
void gr_cursor_hide(void)
{
    if (!g_mouse_present || g_cursor_visible == 0)
        return;
    sys_blit(0, 0, clip15(g_mouse_lim_x, g_cursor_x), clip15(g_mouse_lim_y, g_cursor_y),
             (uint16_t)g_cursor_x, (uint16_t)g_cursor_y, 10, 0, sys_rect_copy);
    g_cursor_visible = 0;
}

/*
 * gr_cursor_update - poll the mouse; if it moved or the shape changed,
 * restore the old save-under, save the new one and draw the shape.
 *
 * Original: IST.EXE 0C7F:026B (NGRAPHS.L), far cdecl (shape).  The strip
 * base is min(max(shape - 3, 0), slot4Bh_w / 15 - 6).
 * Verified: tests/py/test_graphics.py (test_cursor).
 */
void gr_cursor_update(uint8_t shape)
{
    int16_t x, y, b, lim = (int16_t)(g_surf_w(0x4B) / 15 - 6);
    int16_t s3 = (int16_t)(shape - 3) > 0 ? (int16_t)(shape - 3) : 0;
    uint8_t base = s3 < lim ? (uint8_t)s3 : (uint8_t)lim;

    if (!g_mouse_present)
        return;
    gr_mouse_read(&x, &y, &b);
    if (g_cursor_visible == 0) {
        gr_cursor_set_shape(shape);
        return;
    }
    g_mouse_buttons = b;
    if (x == g_cursor_x && y == g_cursor_y && shape == g_cursor_shape)
        return;
    if (shape < g_cursor_strip || shape >= (int16_t)(g_cursor_strip + 6))
        load_strip(base);
    sys_blit(0, 0, clip15(g_mouse_lim_x, g_cursor_x), clip15(g_mouse_lim_y, g_cursor_y),
             (uint16_t)g_cursor_x, (uint16_t)g_cursor_y, 10, 0, sys_rect_copy);
    sys_blit((uint16_t)x, (uint16_t)y, clip15(g_mouse_lim_x, x), clip15(g_mouse_lim_y, y),
             0, 0, 0, 10, sys_rect_copy);
    sys_blit((uint16_t)((shape - g_cursor_strip) * 15), 0,
             clip15(g_mouse_lim_x, x), clip15(g_mouse_lim_y, y),
             (uint16_t)x, (uint16_t)y, 9, 0, asm_copy_rect_key);
    g_cursor_x = x;
    g_cursor_y = y;
    g_cursor_shape = shape;
}

/*
 * gr_call_then_cursor - call fn and update the cursor with the low byte
 * of its result as the shape.
 *
 * Original: IST.EXE 0C7F:04A1 (NGRAPHS.L), far cdecl (far fn).
 * Verified: trivially (test_cursor calls it).
 */
void gr_call_then_cursor(int16_t (*fn)(void))
{
    gr_cursor_update((uint8_t)fn());
}

/*
 * gr_mouse_read - INT 33h AX=3: *x = CX >> shift (arithmetic), *y = DX,
 * *buttons = BX; returns *buttons (re-read, so with x, y, buttons all
 * pointing to one variable the result is BX).
 *
 * Original: IST.EXE 0C7F:04AF (NGRAPHS.L), far cdecl (x, y, buttons).
 * Verified: tests/py/test_graphics.py (test_mouse).
 */
int16_t gr_mouse_read(int16_t *x, int16_t *y, int16_t *buttons)
{
    g_int33_in_ax = 3;
    gr_int33();
    *x = (int16_t)((int16_t)g_int33_out_cx >> (g_mouse_shift & 31));
    *y = (int16_t)g_int33_out_dx;
    *buttons = (int16_t)g_int33_out_bx;
    return *buttons;
}

/* hotspot offset: 7 for the arrow-like shapes (< 2Ch except 21h) */
static int16_t hot_offset(void)
{
    return (g_cursor_shape < 0x2C && g_cursor_shape != 0x21) ? 7 : 0;
}

/*
 * gr_zone_under_cursor - first hotspot rectangle 1..40 containing the
 * cursor's hot point, 0 if none.
 *
 * Original: IST.EXE 0C7F:0515 (NGRAPHS.L), far cdecl.  The cursor x/y in
 * DGROUP are temporarily offset by the hot point (and restored).
 * Verified: tests/py/test_graphics.py (test_zones).
 */
int16_t gr_zone_under_cursor(void)
{
    int16_t off = hot_offset(), i;
    g_cursor_x = (int16_t)(g_cursor_x + off);
    g_cursor_y = (int16_t)(g_cursor_y + off);
    for (i = 1; i <= 40; i++)
        if (g_zone_x1(i) <= g_cursor_x && g_zone_x2(i) >= g_cursor_x &&
            g_zone_y1(i) <= g_cursor_y && g_zone_y2(i) >= g_cursor_y)
            break;
    g_cursor_x = (int16_t)(g_cursor_x - off);
    g_cursor_y = (int16_t)(g_cursor_y - off);
    return i > 40 ? 0 : i;
}

/*
 * gr_cursor_in_zone - does the 15x15 cursor box (moved by the hot point
 * offset) overlap hotspot rectangle n?
 *
 * Original: IST.EXE 0C7F:05C7 (NGRAPHS.L), far cdecl (n).
 * Verified: tests/py/test_graphics.py (test_zones).
 */
uint8_t gr_cursor_in_zone(uint8_t n)
{
    int16_t off = hot_offset();
    uint8_t hit = 0;
    g_cursor_x = (int16_t)(g_cursor_x + off);
    g_cursor_y = (int16_t)(g_cursor_y + off);
    if (g_zone_x1(n) <= (int16_t)(g_cursor_x + 15) && g_zone_x2(n) >= g_cursor_x &&
        g_zone_y1(n) <= (int16_t)(g_cursor_y + 15) && g_zone_y2(n) >= g_cursor_y)
        hit = 1;
    g_cursor_x = (int16_t)(g_cursor_x - off);
    g_cursor_y = (int16_t)(g_cursor_y - off);
    return hit;
}

/*
 * gr_cursor_in_rect - does the 15x15 cursor box overlap (x, y, w, h)
 * (inclusive edges, no hot point offset)?
 *
 * Original: IST.EXE 0C7F:067D (NGRAPHS.L), far cdecl (x, y, w, h).
 * Verified: tests/py/test_graphics.py (test_zones).
 */
uint8_t gr_cursor_in_rect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    if ((int16_t)(g_cursor_x + 15) >= x && (int16_t)(x + w) >= g_cursor_x &&
        (int16_t)(g_cursor_y + 15) >= y && (int16_t)(y + h) >= g_cursor_y)
        return 1;
    return 0;
}

/*
 * gr_zones_clear - set all 41 hotspot rectangles to -1.
 *
 * Original: IST.EXE 0C7F:06B6 (NGRAPHS.L), far cdecl.
 * Verified: tests/py/test_graphics.py (test_zones).
 */
void gr_zones_clear(void)
{
    int16_t i;
    for (i = 0; i <= 40; i++) {
        g_zone_x1(i) = -1;
        g_zone_y1(i) = -1;
        g_zone_x2(i) = -1;
        g_zone_y2(i) = -1;
    }
}

/*
 * gr_wait_buttons_up - keep the cursor moving until no mouse button is down.
 *
 * Original: IST.EXE 0C7F:0714 (NGRAPHS.L), far cdecl.  The port calls
 * pc_poll() once per iteration so host mouse input (and time) arrive.
 * Verified: tests/py/test_graphics.py (test_mouse, buttons already up).
 */
void gr_wait_buttons_up(void)
{
    int16_t t;
    while (gr_mouse_read(&t, &t, &t) != 0) {
        gr_cursor_update((uint8_t)g_cursor_shape);
        pc_poll();
    }
}

/*
 * gr_fade - change palette brightness by d_bright and music volume by
 * d_volume over `ms` milliseconds of the 100 Hz clock, then set the end
 * values exactly.  Both deltas are clamped to [0, max].
 *
 * Original: IST.EXE 0C7F:073F (NGRAPHS.L), far cdecl (ms, d_bright,
 * d_volume).  Arithmetic kept: the elapsed time t is sign-extended (CWD)
 * into the 32-bit products and divided by ms as an unsigned long; inside
 * the loop each component is target * level in 16 bits (IMUL low word,
 * CWD) divided by g_pal_level_max; the final values use 32-bit products.
 * The music volume goes to ampSetTrackVolume(-1, ...) only if sound is on.
 * The port calls pc_poll() once per loop iteration (the original spins on
 * the clock, which only its timer interrupt advances).
 * IDIV [731E] is 4 bytes long: with g_pal_level_max = 0 the INT 00h
 * handler would resume mid-instruction - not modelled (never 0).
 * Verified: tests/py/test_graphics.py (test_fade): ms = 0 only - end
 * values, clamps, the original's volume calls.  The in-loop interpolation
 * is NOT differentially tested (the emulated original has no running
 * clock).
 */
void gr_fade(uint16_t ms, int16_t d_bright, int16_t d_volume)
{
    uint16_t t0, t, i;
    int16_t lvl;
    int32_t q;

    if ((int16_t)(g_pal_level + d_bright) > g_pal_level_max)
        d_bright = (int16_t)(g_pal_level_max - g_pal_level);
    if ((int32_t)g_pal_level + d_bright < 0)           /* JGE after ADD: true sign */
        d_bright = (int16_t)-g_pal_level;
    if ((int16_t)(g_music_level + d_volume) > g_music_level_max)
        d_volume = (int16_t)(g_music_level_max - g_music_level);
    if ((int32_t)g_music_level + d_volume < 0)
        d_volume = (int16_t)-g_music_level;

    t0 = (uint16_t)g_clock_ms;
    for (t = 0; t < ms; t = (uint16_t)((uint16_t)g_clock_ms - t0)) {
        if (d_bright != 0) {
            q = ((int32_t)(int16_t)t * d_bright) / (int32_t)ms;
            lvl = (int16_t)(g_pal_level + (int16_t)q);
            for (i = 0; i < 0x300; i++)
                gr_work_pal[i] = (uint8_t)nk_idiv_s16((int16_t)(gr_target_pal[i] * lvl),
                                                      g_pal_level_max, NULL);
            gr_set_palette(gr_work_pal);
        }
        if (d_volume != 0 && g_sound_on != 0) {
            q = ((int32_t)d_volume * (int16_t)t) / (int32_t)ms;
            ampSetTrackVolume(-1, (int32_t)g_music_level + q);
        }
        pc_poll();
    }
    if (d_bright != 0) {
        lvl = (int16_t)(g_pal_level + d_bright);
        for (i = 0; i < 0x300; i++)
            gr_work_pal[i] = (uint8_t)(((int32_t)lvl * gr_target_pal[i]) /
                                       (int32_t)g_pal_level_max);
        gr_set_palette(gr_work_pal);
    }
    if (d_volume != 0 && g_sound_on != 0)
        ampSetTrackVolume(-1, (int32_t)(int16_t)(g_music_level + d_volume));
    g_pal_level = (int16_t)(g_pal_level + d_bright);
    g_music_level = (int16_t)(g_music_level + d_volume);
}

/*
 * gr_blend3 - dst[i] = table-blend(src1[i], src2[i]) over the size of
 * src1, in pieces that fit all three mappings (asm_blend_span).
 *
 * Original: IST.EXE 0C7F:08FC (NGRAPHS.L), far cdecl (src1, src2, dst,
 * table).  Fatal 28h if all three blocks are EMS (only two EMS windows) or
 * src1 is larger than dst.  The loop runs while the remaining count is
 * non-zero (a wrap would keep it going; mm_map's clamp to src1 prevents it).
 * NOT modelled: when neither src1 nor src2 is EMS and dst is, the first
 * piece is 0FFFFh bytes, but conventional blocks start at offset 4 of
 * their segment, so the original's source offsets wrap after FFFBh bytes
 * (bytes FFFCh-FFFEh are blended from the 4 bytes before the block; in the
 * emulator they came out unwritten).  The port reads on linearly.  Only
 * conventional sources larger than FFFBh bytes are affected.
 * Verified: tests/py/test_graphics.py (test_blend3).
 */
void gr_blend3(MmHandle *src1, MmHandle *src2, MmHandle *dst, const uint8_t *table)
{
    uint32_t rem = src1->size, off = 0;
    uint16_t len, slot;
    uint8_t *p1, *p2, *p3;

    if ((src1->type != 0 && src2->type != 0 && dst->type != 0) || src1->size > dst->size)
        o559A_fatal(DGSTR(0x7326), 0x227, NULL, 0x28);
    mm_ems_lock(g_blit_lock, 0x229, DGSTR(0x733B));
    do {
        len = 0xFFFF;
        slot = 0;
        mm_map(src1, &p1, &len, &slot, off);
        mm_map(src2, &p2, &len, &slot, off);
        mm_map(dst, &p3, &len, &slot, off);
        asm_blend_span(p1, p2, p3, table, len);
        off += len;
        rem -= len;
    } while (rem != 0);
    mm_ems_unlock(g_blit_lock);
}

/*
 * gr_draw_strip - tint a w x h rectangle of a slot towards `color` through
 * the .RIX blend table (asm_tint_rect: pixels equal to color stay), in
 * mapped pieces.
 *
 * Original: IST.EXE 0C7F:0A51 (NGRAPHS.L), far cdecl (x, y, w, h, slot,
 * color) - "RenkFiltre" (colour filter) in the error text.  Out-of-bounds
 * rectangles are fatal (message built with sprintf, "ML(?!)" appended
 * when it is 68h characters or longer); a table mapping shorter than
 * 7F80h is fatal 70h.
 * Verified: tests/py/test_graphics.py (test_draw_strip).
 */
void gr_draw_strip(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t slot,
                   uint8_t color)
{
    char msg[0x82];
    uint16_t sw = (uint16_t)g_surf_w(slot), plen, tlen, s, rows;
    MmHandle *rec = mm_ref((uint16_t)(0xE29F + 8 * slot));
    MmHandle *tab = mm_ref(G_RIX_TABLE_REF);
    uint8_t *p, *t;

    if (w == 0 || h == 0)
        return;
    if ((uint16_t)(x + w) > sw || (uint16_t)g_surf_h(slot) < (uint16_t)(y + h)) {
        if (sprintf(msg, DGSTR(0x7350), (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h,
                    (int)slot) >= 0x68)
            strcat(msg, DGSTR(0x7386));
        o559A_fatal(DGSTR(0x738D), 0x268, msg, -1);
    }
    mm_ems_lock(g_blit_lock, 0x26B, DGSTR(0x73A2));
    do {
        s = 0;
        plen = 0xFFFF;
        tlen = 0xFFFF;
        mm_map(rec, &p, &plen, &s, (uint32_t)y * sw + x);
        mm_map(tab, &t, &tlen, &s, 0);
        if (tlen < 0x7F80)
            o559A_fatal(DGSTR(0x73B7), 0x271, NULL, 0x70);
        rows = (uint16_t)(nk_div_u16((uint16_t)(plen - w), sw, NULL) + 1);
        if (rows >= h)
            rows = h;
        asm_tint_rect(p, t, 0, 0, w, rows, sw, color);
        y += rows;
        h -= rows;
    } while (h != 0);
    mm_ems_unlock(g_blit_lock);
}
