/*
 * ographs.c - OGRAPHS.L: mode set, VGA register helpers, palette and mouse
 * set-up, stepwise palette fade, graphics start-up.
 * Original: IST.EXE ovl5511 (overlay; Borland C + inline asm, far cdecl).
 * See ographs.h.
 */
#include "engine/ographs.h"

#include <string.h>

#include "engine/blit386.h"
#include "engine/fileman.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "platform/pc.h"
#include "platform/vga.h"
#include "rtl/bc_stdio.h"


/*
 * ogr_int10 - INT 10h with AX = ax.  The game only sets modes (13h, 12h,
 * 03h): AH = 0 -> vga_bios_set_mode.  The BIOS clears video memory unless
 * bit 7 of the mode is set; in chained mode the port also clears the CPU
 * view pc_a000 (the BIOS wrote zeros through A000), so later reads of
 * slot 0 see what the original sees.
 *
 * Original: IST.EXE 5511:0000 (OGRAPHS.L), far cdecl (ax).
 * Verified: tests/py/test_graphics.py (test_ogr_init).
 */
void ogr_int10(uint16_t ax)
{
    if ((ax >> 8) != 0)
        return;                              /* other functions: not used */
    vga_bios_set_mode(&pc_vga, (uint8_t)ax);
    if (!(ax & 0x80))
        memset(pc_a000, 0, sizeof pc_a000);
}

/*
 * ogr_set_map_mask - sequencer map mask: OUT 3C4h, (mask << 8) | 02h.
 *
 * Original: IST.EXE 5511:000A (OGRAPHS.L), far cdecl (mask).
 * Verified: tests/py/test_graphics.py (test_vga_regs).
 */
void ogr_set_map_mask(uint8_t mask)
{
    vga_out16(&pc_vga, 0x3C4, (uint16_t)((mask << 8) | 0x02));
}

/*
 * ogr_set_split_line - CRTC line compare = 2 * line: register 18h gets
 * (2*line) MOD 255 (sic, not the low byte), bit 8 -> CR07 bit 4 and bit 9
 * -> CR09 bit 6, with CR11 bit 7 (write protect) cleared and set around
 * the CR07 write.  Unreferenced.
 *
 * Original: IST.EXE 5511:0018 (OGRAPHS.L), far cdecl (line), asm.
 * Verified: tests/py/test_graphics.py (test_vga_regs).
 */
void ogr_set_split_line(uint16_t line)
{
    uint16_t n = (uint16_t)(line << 1);
    uint8_t v;
    vga_out8(&pc_vga, 0x3D4, 0x18);
    vga_out8(&pc_vga, 0x3D5, (uint8_t)(n % 0xFF));
    vga_out8(&pc_vga, 0x3D4, 0x11);
    v = vga_in8(&pc_vga, 0x3D5);
    vga_out8(&pc_vga, 0x3D5, v & 0x7F);
    vga_out8(&pc_vga, 0x3D4, 0x07);
    v = vga_in8(&pc_vga, 0x3D5);
    vga_out8(&pc_vga, 0x3D5, (uint8_t)((v & 0xEF) | ((n >> 4) & 0x10)));
    vga_out8(&pc_vga, 0x3D4, 0x11);
    v = vga_in8(&pc_vga, 0x3D5);
    vga_out8(&pc_vga, 0x3D5, v | 0x80);
    vga_out8(&pc_vga, 0x3D4, 0x09);
    v = vga_in8(&pc_vga, 0x3D5);
    vga_out8(&pc_vga, 0x3D5, (uint8_t)((v & 0xBF) | ((n >> 3) & 0x40)));
}

/*
 * ogr_set_start_addr - wait for a retrace to start and end, then CRTC
 * 0Ch/0Dh = start bits 15-8 / 7-0 (two outport() word writes).
 *
 * Original: IST.EXE 5511:0090 (OGRAPHS.L), far cdecl (u32 start).
 * Verified: tests/py/test_graphics.py (test_vga_regs).
 */
void ogr_set_start_addr(uint32_t start)
{
    while ((pc_in_3da() & 8) != 8)
        ;
    while (pc_in_3da() & 8)
        ;
    vga_out16(&pc_vga, 0x3D4, (uint16_t)(((uint16_t)(start >> 8) << 8) | 0x0C));
    vga_out16(&pc_vga, 0x3D4, (uint16_t)(((uint16_t)start << 8) | 0x0D));
}

/*
 * ogr_screen_on / ogr_screen_off - reset the attribute flip-flop (read
 * 3DAh, and 3BAh) and write 20h / 00h to 3C0h (palette address source:
 * display on / blanked).
 *
 * Original: IST.EXE 5511:00E1 / 5511:00FD (OGRAPHS.L), far cdecl, asm.
 * The 3BAh read (mono status port) has no effect on a colour VGA.
 * Verified: tests/py/test_graphics.py (test_vga_regs).
 */
void ogr_screen_on(void)
{
    pc_in_3da();
    vga_out8(&pc_vga, 0x3C0, 0x20);
}

void ogr_screen_off(void)
{
    pc_in_3da();
    vga_out8(&pc_vga, 0x3C0, 0x00);
}

/*
 * ogr_black_palette - zero the work palette, load it, brightness 0.
 *
 * Original: IST.EXE 5511:0119 (OGRAPHS.L), far cdecl.
 * Verified: tests/py/test_graphics.py (test_ogr_init).
 */
void ogr_black_palette(void)
{
    asm_memset32(gr_work_pal, 0, 0x300);
    gr_set_palette(gr_work_pal);
    g_pal_level = 0;
}

/*
 * ogr_clear_screen - 64000 zero bytes into slot 0's memory (A000:0000).
 *
 * Original: IST.EXE 5511:015B (OGRAPHS.L), far cdecl.  Uses the far
 * pointer stored in slot 0's record.
 * Verified: tests/py/test_graphics.py (test_ogr_init).
 */
void ogr_clear_screen(void)
{
    asm_memset32(mm_conv_ptr(mm_ref(0xE29F)), 0, 0xFA00);
}

/*
 * ogr_mouse_init - reset the mouse driver (INT 33h 0); without a driver
 * g_mouse_present = 0 and nothing else.  Otherwise read the position
 * (fn 3): x == 320 (centre of a 640-wide range) -> g_mouse_shift = 1;
 * allocate the cursor save-under (slot 10, 15x15) and shape cache (slot 9,
 * 90x15), mark the cache empty and set the window to 0..319 x 0..199.
 *
 * Original: IST.EXE 5511:017C (OGRAPHS.L), far cdecl.
 * Verified: tests/py/test_graphics.py (test_mouse).
 */
void ogr_mouse_init(void)
{
    g_int33_in_ax = 0;
    gr_int33();
    if (g_int33_out_ax == 0) {
        g_mouse_present = 0;
        return;
    }
    g_mouse_present = 1;
    g_int33_in_ax = 3;
    gr_int33();
    g_mouse_shift = g_int33_out_cx == 0x140 ? 1 : 0;
    mm_ref_set(0xE2EF, mm_alloc(0xE1, 0));          /* slot 10: 15 x 15 */
    g_surf_h(10) = 0x0F;
    g_surf_w(10) = 0x0F;
    mm_ref_set(0xE2E7, mm_alloc(0x546, 0));         /* slot 9: 90 x 15 */
    g_surf_w(9) = 0x5A;
    g_surf_h(9) = 0x0F;
    g_cursor_strip = (int16_t)0x8001;
    ogr_mouse_set_window(0, 0, 0x13F, 0xC7);
}

/*
 * ogr_mouse_set_pos - INT 33h fn 4 with (x << shift, y).
 *
 * Original: IST.EXE 5511:02A1 (OGRAPHS.L), far cdecl (x, y).
 * Verified: tests/py/test_graphics.py (test_mouse).
 */
void ogr_mouse_set_pos(int16_t x, int16_t y)
{
    g_int33_in_ax = 4;
    g_int33_in_cx = (uint16_t)(x << (g_mouse_shift & 31));
    g_int33_in_dx = (uint16_t)y;
    gr_int33();
}

/*
 * ogr_mouse_set_window - hide the cursor, set the driver ranges (fn 7:
 * x..x+w shifted, fn 8: y..y+h), remember the exclusive limits for the
 * cursor clipping, and redraw the cursor if it was visible.
 *
 * Original: IST.EXE 5511:02F3 (OGRAPHS.L), far cdecl (x, y, w, h).
 * Verified: tests/py/test_graphics.py (test_mouse).
 */
void ogr_mouse_set_window(int16_t x, int16_t y, int16_t w, int16_t h)
{
    int16_t vis = g_cursor_visible;
    gr_cursor_hide();
    g_int33_in_ax = 7;
    g_int33_in_cx = (uint16_t)(x << (g_mouse_shift & 31));
    g_int33_in_dx = (uint16_t)((uint16_t)(x + w) << (g_mouse_shift & 31));
    gr_int33();
    g_int33_in_ax = 8;
    g_int33_in_cx = (uint16_t)y;
    g_int33_in_dx = (uint16_t)(y + h);
    gr_int33();
    g_mouse_lim_x = (int16_t)(x + w + 1);
    g_mouse_lim_y = (int16_t)(y + h + 1);
    if (vis != 0)
        gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * ogr_palette_step_fade - fade one palette towards the other by `step`
 * units per component per frame (delay ms per frame).
 *
 *   flags bit 0   0: the work palette (E5EB) moves towards the target (E5EF);
 *                 1: the target moves towards the work palette
 *                 (the moving one is sent to the DAC and finally copied
 *                 into the target)
 *   flags bit 1   set the target first: all 0 (bit 2 = 0) or all 3Fh
 *                 (bit 2 = 1) instead of loading pal_name
 *   flags bits 3-4  step - 1
 *   flags bits 5-7  non-zero: do ONE step per call and return 1 while
 *                 something changed (g_pal_fade_busy keeps the set-up)
 *
 * Without bit 1 the target is loaded from pal_name (fm_load_pal) and bit 0
 * is inverted (also on later single-step calls).  At the end the result is
 * copied into the target and brightness = max.  Returns the "changed" flag
 * (0 at the end).
 *
 * Original: IST.EXE 5511:03DF (OGRAPHS.L), far cdecl (flags, name, delay).
 * The port calls pc_poll() in the frame-delay busy loop.
 * Verified: tests/py/test_graphics.py (test_step_fade, delay 0; with
 * and without the HAR2.PAL load).  delay > 0 waits are not tested.
 */
uint8_t ogr_palette_step_fade(uint8_t flags, const char *pal_name, uint16_t delay)
{
    uint8_t changed = 1, step = (uint8_t)(((flags >> 3) & 3) + 1), once = (uint8_t)(flags >> 5);
    uint8_t *work, *from, *to, d;
    uint16_t t0, i;

    if (once == 0)
        g_pal_fade_busy = 0;
    work = gr_work_pal;
    if (g_pal_fade_busy == 0) {
        if (flags & 2) {
            asm_memset32(work, (flags & 4) == 4 ? 0x3F3F3F3Fu : 0, 0x300);
        } else {
            fm_load_pal(pal_name, 0, 0x100);
            flags ^= 1;
        }
    } else if (!(flags & 2)) {
        flags ^= 1;
    }
    if (once != 0)
        g_pal_fade_busy = 1;
    if (flags & 1) {
        to = gr_target_pal;          /* changed in place, sent to the DAC */
        from = work;
    } else {
        to = work;
        from = gr_target_pal;
    }
    while (changed != 0) {
        t0 = (uint16_t)g_clock_ms;
        changed = 0;
        if (step == 1) {
            for (i = 0; i < 0x300; i++) {
                d = (uint8_t)(from[i] - to[i]);
                if (d != 0) {
                    if (d & 0x80)
                        to[i]--;
                    else
                        to[i]++;
                    changed = 1;
                }
            }
        } else {
            for (i = 0; i < 0x300; i++) {
                d = (uint8_t)(from[i] - to[i]);
                if (d != 0) {
                    int16_t ad = (int8_t)d < 0 ? (int16_t)-(int8_t)d : (int8_t)d;
                    if (ad < step)
                        to[i] = (uint8_t)(to[i] + d);
                    else if (d & 0x80)
                        to[i] = (uint8_t)(to[i] - step);
                    else
                        to[i] = (uint8_t)(to[i] + step);
                    changed = 1;
                }
            }
        }
        gr_set_palette(to);
        while ((uint16_t)((uint16_t)g_clock_ms - t0) < delay)
            pc_poll();
        if (changed == 0) {
            g_pal_fade_busy = 0;
            mm_memcpy(to, gr_target_pal, 0x300);
            g_pal_level = g_pal_level_max;
        }
        if (once != 0)
            return changed;
    }
    mm_memcpy(to, gr_target_pal, 0x300);
    g_pal_level = g_pal_level_max;
    return 0;
}

/*
 * ogr_init - require a VGA (INT 10h AX=1A00h must return AL != 0, else
 * fatal "VGA kartı bulunamadı!"), make slot 0 the 320x204 screen at
 * A000:0000 (record E5D8, size FF00h), allocate the two palettes if not
 * yet done, set mode 13h and full brightness.
 *
 * Original: IST.EXE 5511:06F0 (OGRAPHS.L), far cdecl.  The port always
 * emulates a VGA (AL = 1Ah).  The record's far pointer bytes are written
 * as in the original (0000:A000 -> A000:0000) and the native pointer is
 * pc_a000.
 * Verified: tests/py/test_graphics.py (test_ogr_init).
 */
void ogr_init(void)
{
    MmHandle *scr = (MmHandle *)DGP(G_SCREEN_REC);
    uint8_t al = 0x1A;                       /* INT 10h 1A00h: VGA BIOS present */

    /* int86(0x10, ...): Borland's int86x reaches the INT with the carry of
       its "CMP AL,25h" (10h < 25h: CF = 1); the BIOS returns with IRET, so
       cflag = 1 and __IOerror(AX = 001Ah) runs: _doserrno = 1Ah, errno =
       _dosErrorToSV[1Ah].  Harmless, but DGROUP-visible. */
    bc_doserrno = 0x1A;
    bc_errno = DGS8(BC_DOSERR_TO_SV + 0x1A);
    if (al == 0)
        o559A_fatal(DGSTR(0x89DE), 0x262, DGSTR(0x89F3), -1);
    g_surf_w(0) = 0x140;
    g_surf_h(0) = 0xCC;
    mm_ref_set(0xE29F, scr);
    scr->seg = 0xA000;
    scr->ems = 0;
    mm_set_conv_ptr(scr, pc_a000, 0);
    scr->type = 0;
    scr->size = 0xFF00;
    if (gr_target_pal == NULL) {
        gr_target_pal = mm_farmalloc(0x300);
        if (gr_target_pal == NULL)
            o559A_fatal(DGSTR(0x8A09), 0x26B, NULL, 0x29);
    }
    if (gr_work_pal == NULL) {
        gr_work_pal = mm_farmalloc(0x300);
        if (gr_work_pal == NULL)
            o559A_fatal(DGSTR(0x8A1E), 0x26C, NULL, 0x2A);
    }
    ogr_int10(0x13);
    g_pal_level = g_pal_level_max;
}

/*
 * ogr_copy_pal_entries - p[3Ch..3Eh] = p[12h..14h]; 18h bytes from p+18h
 * to p+A8h (a struct helper; the name is historical).
 *
 * Original: IST.EXE 5511:0830 (OGRAPHS.L), far cdecl (far p).
 * Verified: tests/py/test_graphics.py (test_copy_pal_entries).
 */
void ogr_copy_pal_entries(uint8_t *p)
{
    p[0x3C] = p[0x12];
    p[0x3D] = p[0x13];
    p[0x3E] = p[0x14];
    mm_memcpy(p + 0x18, p + 0xA8, 0x18);
}
