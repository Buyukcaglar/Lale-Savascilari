/*
 * systems.c - ALTDUZEY\SYSTEMS.L graphics part: blitters between surface
 * slots, fills, line/rectangle drawing, planar row copy, the 3D strip
 * callbacks, palette searches and the .RIX blend table.
 * Original: IST.EXE seg0AC6 (Borland C, far cdecl).  See systems.h.
 *
 * Offsets.  The original adds 16-bit products to the offset half of far
 * pointers (IMUL then ADD to the offset); the port adds the same uint16_t
 * value to the native pointer.  That is exact as long as the pointer's
 * offset plus the value does not pass 64 KB, which the mapping lengths
 * (mm_map windows) guarantee for every caller.
 */
#include "engine/systems.h"

#include <stdio.h>
#include <string.h>

#include "dsmi/emsheap.h"
#include "engine/blit386.h"
#include "engine/fileman.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/ographs.h"
#include "platform/pc.h"
#include "platform/vga.h"

#define SYS_FILE DGSTR(0x7134)          /* "T:ALTDUZEY\SYSTEMS.L" (one copy per call site) */

/* record of surface slot n (NULL when the far pointer is 0000:0000) */
static MmHandle *slot_rec(uint16_t n)
{
    return mm_ref((uint16_t)(0xE29F + (n << 3)));
}
static int slot_null(uint16_t n)
{
    uint16_t o = (uint16_t)(0xE29F + (n << 3));
    return (DG16(o) | DG16(o + 2)) == 0;
}

/* far pointer offset of a page-frame pointer (EMSLock result) */
static uint16_t frame_off(const uint8_t *p)
{
    return (uint16_t)(p - pc_ems_frame);
}

/* far pointer + 16-bit offset: the offset wraps inside the segment.  For
   pointers into the EMS page frame (E000:xxxx) and the screen (A000:xxxx)
   the port wraps the same way; other (conventional, mm_map'ed) pointers
   have a far offset of 0..15, so the add is plain. */
static uint8_t *seg_add(const uint8_t *p, uint16_t off)
{
    if (p >= pc_ems_frame && p < pc_ems_frame + 0x10000)
        return pc_ems_frame + (uint16_t)(frame_off(p) + off);
    if (p >= pc_a000 && p < pc_a000 + 0x10000)
        return pc_a000 + (uint16_t)((uint16_t)(p - pc_a000) + off);
    return (uint8_t *)p + off;
}

static uint32_t color32(uint8_t c)
{
    return ((uint32_t)c << 24) | ((uint32_t)c << 16) | ((uint32_t)c << 8) | c;
}

/* A000 accesses of the planar copier go through the VGA (mode 12h). */
static int in_a000(const uint8_t *p)
{
    return p >= pc_a000 && p < pc_a000 + sizeof pc_a000;
}
static uint8_t vrd(const uint8_t *p)
{
    return in_a000(p) ? vga_mem_read8(&pc_vga, 0xA0000u + (uint32_t)(p - pc_a000)) : *p;
}
static void vwr(uint8_t *p, uint8_t v)
{
    if (in_a000(p))
        vga_mem_write8(&pc_vga, 0xA0000u + (uint32_t)(p - pc_a000), v);
    else
        *p = v;
}

/* adapters, see systems.h */
int16_t sys_rect_copy(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t h,
                      uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                      uint16_t dpitch, uint8_t key)
{
    (void)key;
    return asm_copy_rect(src, sx, sy, w, h, spitch, dst, dx, dy, dpitch);
}

void sys_span_memcpy(const uint8_t *src, uint8_t *dst, uint16_t n, uint8_t key)
{
    (void)key;
    mm_memcpy(src, dst, n);
}

/*
 * sys_rowcopy_planar - row routine for mode 12h (640x480x16): copies `rows`
 * rows of a planar source (each row = 4 planes of spitch/8 bytes) to the
 * screen plane by plane, setting the map mask for each plane and merging
 * partial bytes at both ends.  Returns 0 (DX:AX) when the source and
 * destination bit phases differ, else 1.
 *
 * Original: IST.EXE 0AC6:000A (SYSTEMS.L), far cdecl (13 words, see
 * sys_blit_cb).  Quirks kept: the destination offset adds dx itself (not
 * dx/8); the right mask shifts by the low byte of sx (not sx & 7) plus
 * w & 7, with the 386 count masking (& 31); the edge merges read the
 * destination through the VGA read path (graphics controller read map,
 * which this routine never sets).
 * Verified: tests/py/test_graphics.py (test_rowcopy_planar).
 */
int32_t sys_rowcopy_planar(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w,
                           uint16_t rows, uint16_t spitch, uint8_t *dst, uint16_t dx,
                           uint16_t dy, uint16_t dpitch, uint8_t key)
{
    uint8_t lmask = 0, rmask = 0, plane;
    uint16_t r, p, n;
    (void)key;

    if ((sx & 7) != (dx & 7))
        return 0;
    src += (uint16_t)((uint16_t)(int16_t)((int16_t)(spitch >> 1) * (int16_t)sy) + (sx >> 3));
    dst += (uint16_t)(dx + (uint16_t)(int16_t)((int16_t)(dpitch >> 3) * (int16_t)dy));
    lmask = (uint8_t)(0xFF << ((8 - (sx & 7)) & 31));            /* SHL AL,CL: 8 -> 0 */
    rmask = (uint8_t)(0xFFu >> ((uint8_t)((uint8_t)sx + (w & 7)) & 31));   /* SAR AX,CL */
    if (lmask != 0 && rmask != 0) {
        w--;
        dst++;
        src++;
    }
    for (r = 0; r < rows; r++) {
        plane = 0x11;
        for (p = 0; p < 4; p++) {
            ogr_set_map_mask(plane & 0x0F);
            plane = (uint8_t)((plane << 1) | (plane >> 7));      /* ROL AL,1 */
            if (lmask != 0)
                vwr(dst - 1, (uint8_t)((vrd(dst - 1) & lmask) | (src[-1] & (uint8_t)~lmask)));
            /* mm_memcpy(src, dst, w >> 3): byte stores into VGA memory */
            if (in_a000(dst)) {
                for (n = 0; n < (w >> 3); n++)
                    vwr(dst + n, src[n]);
            } else {
                mm_memcpy(src, dst, (uint16_t)(w >> 3));
            }
            if ((uint8_t)~rmask != 0) {
                n = (uint16_t)(w >> 3);
                vwr(dst + n, (uint8_t)((vrd(dst + n) & rmask) | (src[n] & (uint8_t)~rmask)));
            }
            src += spitch >> 3;
        }
        dst += dpitch >> 3;
    }
    return 1;
}

/*
 * sys_putpixel - buf[y * pitch + x] = color (16-bit offset arithmetic).
 *
 * Original: IST.EXE 0AC6:01AA (SYSTEMS.L), far cdecl (x, y, color, buf, pitch).
 * Verified: tests/py/test_graphics.py (test_putpixel_and_colors).
 */
void sys_putpixel(uint16_t x, uint16_t y, uint8_t color, uint8_t *buf, uint16_t pitch)
{
    buf[(uint16_t)(pitch * y + x)] = color;
}

/*
 * sys_blit_cb - copy a w x h rectangle from a 4-bit planar source slot
 * (rows of srcw/2 bytes) to a destination slot (dstw/8 bytes per row, the
 * mode-12h screen) through the row routine fn (sys_rowcopy_planar).
 *
 * Original: IST.EXE 0AC6:01C5 (SYSTEMS.L), far cdecl (sx, sy, w, h, dx, dy,
 * src_slot, dst_slot, fn).  fn gets (src, sx, 0, w, rows, 640, dst, dx, 0,
 * dstw, FEh); a zero result is fatal ("_4bitp_copyshell").
 * Division by zero (srcw < 2 or dstw < 8): DIV BX is 2 bytes, the INT 00h
 * handler would resume mid-instruction - not modelled, no caller does it.
 * Verified: tests/py/test_graphics.py (test_blit_cb).
 */
void sys_blit_cb(uint16_t sx, uint16_t sy, uint16_t w, uint16_t h, uint16_t dx,
                 uint16_t dy, uint8_t src_slot, uint8_t dst_slot, SysRectFn32 fn)
{
    uint16_t slot = 0, srclen, dstlen, rows, a, b;
    uint16_t srcw = (uint16_t)g_surf_w(src_slot), dstw = (uint16_t)g_surf_w(dst_slot);
    MmHandle *srec = slot_rec(src_slot), *drec = slot_rec(dst_slot);
    uint8_t *srcp, *dstp;

    if (w == 0 || h == 0)
        return;
    mm_ems_lock(g_blit_lock, 0x8F, SYS_FILE);
    do {
        slot = 0;
        srclen = 0xFFFF;
        dstlen = 0xFFFF;
        mm_map(srec, &srcp, &srclen, &slot, ((uint32_t)sy * srcw) >> 1);
        mm_map(drec, &dstp, &dstlen, &slot, ((uint32_t)dy * dstw) >> 3);
        a = nk_div_u16((uint16_t)(srclen - (w >> 1)), (uint16_t)(srcw >> 1), NULL);
        b = nk_div_u16((uint16_t)(dstlen - (w >> 3)), (uint16_t)(dstw >> 3), NULL);
        rows = (uint16_t)((a < b ? a : b) + 1);
        if (rows >= h)
            rows = h;
        if (fn(srcp, sx, 0, w, rows, 0x280, dstp, dx, 0, dstw, 0xFE) == 0)
            o559A_fatal(DGSTR(0x7149), 0x9B, DGSTR(0x715E), -1);
        sy += rows;
        dy += rows;
        h -= rows;
    } while (h != 0);
    mm_ems_unlock(g_blit_lock);
}

/*
 * Blits whose destination starts outside a conventional block.
 *
 * dy is negative as a 16-bit value (the big capital of a battle text box:
 * grt_draw_text draws its 19x19 cell at y - 5 = -1 / -2 on the screen), so
 * (u32)dy * dstw + dx is far past the block's end.  The original's huge-
 * pointer add in mm_map wraps at 1 MB: the piece starts at
 * block + (offset mod 100000h) and its rows follow each other from there.
 * With dstw = 320 that is 320 bytes below the block per negative row (the
 * screen: 9FExxh/9FFxxh, the top of conventional memory) and the next rows
 * are the block's rows 0, 1, ...; with other widths (a 100-wide surface:
 * +3FF9Ch) the whole piece misses the block.
 * The port does not model the memory around a block: out_of_block() says
 * whether a piece starts outside, row_in_block() gives the offset of row i
 * if all of it lies inside - those rows are drawn where the original draws
 * them, the others dropped.
 * Verified: tests/py/test_iso_d.py:test_textbox_capital (sys_blit on the
 * screen), tests/py/test_q3dview_gap.py:test_render_100 (100-wide surface).
 */
static int out_of_block(const MmHandle *rec, uint16_t dy, uint16_t dstw, uint16_t dx)
{
    /* only offsets that wrap at 1 MB: a start merely past the block's end is
       the next heap block in the port's heap model as in the original */
    return rec->type == 0 && (uint32_t)dy * dstw + dx >= 0x100000;
}

static int row_in_block(const MmHandle *rec, uint16_t dy, uint16_t dstw, uint16_t dx,
                        uint16_t i, uint16_t w, uint32_t *rel)
{
    *rel = ((uint32_t)dy * dstw + dx + (uint32_t)i * dstw) & 0xFFFFF;
    return *rel < rec->size && *rel + w <= rec->size;
}

/*
 * sys_blit - copy a w x h rectangle from (sx,sy) of src_slot to (dx,dy) of
 * dst_slot with the row routine fn (asm_copy_rect family, key FEh), in
 * pieces that fit both mapping windows.
 *
 * Original: IST.EXE 0AC6:0430 (SYSTEMS.L), far cdecl (sx, sy, w, h, dx, dy,
 * src_slot, dst_slot, fn).  fn's result is ignored.
 * Verified: tests/py/test_graphics.py (test_blit_*).
 */
void sys_blit(uint16_t sx, uint16_t sy, uint16_t w, uint16_t h, uint16_t dx, uint16_t dy,
              uint8_t src_slot, uint8_t dst_slot, SysRectFn fn)
{
    uint16_t slot = 0, srclen, dstlen, rows, a, b;
    uint16_t srcw = (uint16_t)g_surf_w(src_slot), dstw = (uint16_t)g_surf_w(dst_slot);
    MmHandle *srec = slot_rec(src_slot), *drec = slot_rec(dst_slot);
    uint8_t *srcp, *dstp;

    if (w == 0 || h == 0)
        return;
    mm_ems_lock(g_blit_lock, 0xBE, DGSTR(0x7192));
    do {
        slot = 0;
        srclen = 0xFFFF;
        dstlen = 0xFFFF;
        mm_map(srec, &srcp, &srclen, &slot, (uint32_t)sy * srcw + sx);
        mm_map(drec, &dstp, &dstlen, &slot, (uint32_t)dy * dstw + dx);
        a = nk_div_u16((uint16_t)(srclen - w), srcw, NULL);
        b = nk_div_u16((uint16_t)(dstlen - w), dstw, NULL);
        rows = (uint16_t)((a < b ? a : b) + 1);
        if (rows >= h)
            rows = h;
        if (!out_of_block(drec, dy, dstw, dx)) {
            fn(srcp, 0, 0, w, rows, srcw, dstp, 0, 0, dstw, 0xFE);
        } else {                        /* runs of rows that lie in the block */
            uint8_t *base = dstp - ((uint32_t)dy * dstw + dx);
            uint32_t rel, rel0 = 0;
            uint16_t i, run = 0, first = 0;
            for (i = 0; i <= rows; i++) {
                int in = i < rows && row_in_block(drec, dy, dstw, dx, i, w, &rel);
                if (in && run != 0 && rel == rel0 + (uint32_t)run * dstw) {
                    run++;
                    continue;
                }
                if (run != 0)
                    fn(srcp + (uint32_t)first * srcw, 0, 0, w, run, srcw, base + rel0,
                       0, 0, dstw, 0xFE);
                run = 0;
                if (in) {
                    first = i;
                    rel0 = rel;
                    run = 1;
                }
            }
        }
        sy += rows;
        dy += rows;
        h -= rows;
    } while (h != 0);
    mm_ems_unlock(g_blit_lock);
}

/*
 * sys_blit_0615 - run the span routine fn over two whole slots in parallel
 * (fn(src, dst, n, key) per mapped piece) until `total` bytes are done.
 *
 * Original: IST.EXE 0AC6:0615 (SYSTEMS.L), far cdecl (src_slot, dst_slot,
 * total, key, fn).  Both maps share one length variable.  The remaining
 * count is not clamped: a piece longer than the rest makes it wrap and
 * the loop go on (callers pass the block size, so it never happens).
 * Verified: tests/py/test_graphics.py (test_blit_0615).
 */
void sys_blit_0615(uint8_t src_slot, uint8_t dst_slot, uint16_t total, uint8_t key,
                   SysSpanFn fn)
{
    MmHandle *srec = slot_rec(src_slot), *drec = slot_rec(dst_slot);
    uint16_t len, slot = 0;
    uint32_t off = 0;
    uint8_t *srcp, *dstp;

    mm_ems_lock(g_blit_lock, 0xE3, DGSTR(0x71A7));
    do {
        len = 0xFFFF;
        slot = 0;
        mm_map(srec, &srcp, &len, &slot, off);
        mm_map(drec, &dstp, &len, &slot, off);
        fn(srcp, dstp, len, key);
        off += len;
        total -= len;
    } while (total != 0);
    mm_ems_unlock(g_blit_lock);
}

/*
 * sys_blit_recolor - like sys_blit, every row through mm_copy_recolor
 * (pixels == match become color, the rest copied).  Used by the text
 * renderer.
 *
 * Original: IST.EXE 0AC6:070B (SYSTEMS.L), far cdecl (sx, sy, w, h, dx, dy,
 * src_slot, dst_slot, match, color).
 * Verified: tests/py/test_graphics.py (test_blit_recolor).
 */
void sys_blit_recolor(uint16_t sx, uint16_t sy, uint16_t w, uint16_t h, uint16_t dx,
                      uint16_t dy, uint8_t src_slot, uint8_t dst_slot,
                      uint8_t match, uint8_t color)
{
    uint16_t slot = 0, srclen, dstlen, rows, a, b, i;
    uint16_t srcw = (uint16_t)g_surf_w(src_slot), dstw = (uint16_t)g_surf_w(dst_slot);
    MmHandle *srec = slot_rec(src_slot), *drec = slot_rec(dst_slot);
    uint8_t *srcp, *dstp;

    if (w == 0 || h == 0)
        return;
    mm_ems_lock(g_blit_lock, 0x20E, DGSTR(0x71BC));
    do {
        slot = 0;
        srclen = 0xFFFF;
        dstlen = 0xFFFF;
        mm_map(srec, &srcp, &srclen, &slot, (uint32_t)sy * srcw + sx);
        mm_map(drec, &dstp, &dstlen, &slot, (uint32_t)dy * dstw + dx);
        a = nk_div_u16((uint16_t)(srclen - w), srcw, NULL);
        b = nk_div_u16((uint16_t)(dstlen - w), dstw, NULL);
        rows = (uint16_t)((a < b ? a : b) + 1);
        if (rows >= h)
            rows = h;
        if (!out_of_block(drec, dy, dstw, dx)) {
            for (i = 0; i < rows; i++)
                mm_copy_recolor(srcp + (uint16_t)(i * srcw), dstp + (uint16_t)(i * dstw),
                                w, match, color);
        } else {
            uint8_t *base = dstp - ((uint32_t)dy * dstw + dx);
            uint32_t rel;
            for (i = 0; i < rows; i++)
                if (row_in_block(drec, dy, dstw, dx, i, w, &rel))
                    mm_copy_recolor(srcp + (uint16_t)(i * srcw), base + rel, w, match, color);
        }
        sy += rows;
        dy += rows;
        h -= rows;
    } while (h != 0);
    mm_ems_unlock(g_blit_lock);
}

/*
 * sys_fill_rect - fill a w x h rectangle of a slot with color
 * (asm_memset32 per row).  EMS slots are locked directly through EMSLock
 * in 64 KB windows, re-locked when a row would start past 0C000h.
 *
 * Original: IST.EXE 0AC6:0908 (SYSTEMS.L), far cdecl (x, y, w, h, slot, color).
 * y*w + x is computed in 16 bits (IMUL, low word) in both paths.
 * Verified: tests/py/test_graphics.py (test_fill_rect).
 */
void sys_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t slot,
                   uint8_t color)
{
    uint16_t width, r, inpage;
    uint32_t off, base = 0;
    MmHandle *rec;
    uint8_t *p;

    if (w == 0 || h == 0)
        return;
    if (slot_null(slot))
        o559A_fatal(DGSTR(0x71D1), 0x2AE, NULL, 0x0C);
    rec = slot_rec(slot);
    if (rec->type == 1) {
        mm_ems_lock(g_blit_lock, 0x2BE, DGSTR(0x71E6));
        width = (uint16_t)g_surf_w(slot);
        p = EMSLock((int16_t)rec->ems, 0, 0xFFFF);               /* 0C000h + 3FFFh */
        inpage = frame_off(p) & 0x3FFF;
        off = (uint16_t)((int16_t)y * (int16_t)width + x);
        for (r = 0; r < h; r++) {
            if ((uint32_t)(off - base + inpage) > 0xC000) {
                p = EMSLock((int16_t)rec->ems, (int32_t)off, 0xFFFF);
                inpage = frame_off(p) & 0x3FFF;
                base = off;
            }
            asm_memset32(p + (off - base), color32(color), w);
            off += width;
        }
        mm_ems_unlock(g_blit_lock);
    } else {
        for (r = 0; r < h; r++)
            asm_memset32(mm_conv_ptr(rec)
                         + (uint16_t)((int16_t)(y + r) * g_surf_w(slot)) + x,
                         color32(color), w);
    }
}

/*
 * sys_fill_rect_checked - outline of a rectangle (four sys_fill_slot_rect
 * lines: top, right, bottom, left), corners at (x,y) and (x+w,y+h).
 *
 * Original: IST.EXE 0AC6:0B99 (SYSTEMS.L), far cdecl (x, y, w, h, slot, color).
 * Verified: tests/py/test_graphics.py (test_lines).
 */
void sys_fill_rect_checked(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t slot,
                           uint8_t color)
{
    if (slot_null(slot))
        o559A_fatal(DGSTR(0x71FB), 0x2DA, NULL, 0x0D);
    sys_fill_slot_rect(x, y, (int16_t)(x + w), y, color, (uint8_t)slot);
    sys_fill_slot_rect((int16_t)(x + w), y, (int16_t)(x + w), (int16_t)(y + h), color,
                       (uint8_t)slot);
    sys_fill_slot_rect(x, (int16_t)(y + h), (int16_t)(x + w), (int16_t)(y + h), color,
                       (uint8_t)slot);
    sys_fill_slot_rect(x, y, x, (int16_t)(y + h), color, (uint8_t)slot);
}

/*
 * sys_blit_scaled - scale a sw x sh rectangle of src_slot to dw x dh at
 * (dx,dy) of dst_slot with asm_blit_scaled_key_flip (key FEh, flags bit 0
 * = mirror), in pieces whose source and destination rows both fit the
 * mapping windows.
 *
 * Original: IST.EXE 0AC6:0C5A (SYSTEMS.L), far cdecl (sx, sy, sw, sh, dx,
 * dy, dw, dh, src_slot, dst_slot, flags).  The row products are 32-bit
 * (LXMUL) but only their low word is divided (XOR DX,DX before DIV).
 * Verified: tests/py/test_graphics.py (test_blit_scaled).
 */
void sys_blit_scaled(uint16_t sx, uint16_t sy, uint16_t sw, uint16_t sh, uint16_t dx,
                     uint16_t dy, uint16_t dw, uint16_t dh, uint8_t src_slot,
                     uint8_t dst_slot, uint8_t flags)
{
    uint16_t srcw = (uint16_t)g_surf_w(src_slot), dstw = (uint16_t)g_surf_w(dst_slot);
    uint16_t srch = (uint16_t)g_surf_h(src_slot), dsth = (uint16_t)g_surf_h(dst_slot);
    uint16_t slot, srclen, dstlen, rs, rd, t, c;
    MmHandle *srec = slot_rec(src_slot), *drec = slot_rec(dst_slot);
    uint8_t *srcp, *dstp;

    if (slot_null(src_slot) || slot_null(dst_slot))
        o559A_fatal(DGSTR(0x7210), 0x328, NULL, 0x0E);
    mm_ems_lock(g_blit_lock, 0x33A, DGSTR(0x7225));
    do {
        slot = 0;
        srclen = 0xFFFF;
        dstlen = 0xFFFF;
        mm_map(srec, &srcp, &srclen, &slot, (uint32_t)sy * srcw + sx);
        mm_map(drec, &dstp, &dstlen, &slot, (uint32_t)dy * dstw + dx);
        t = (uint16_t)(nk_div_u16((uint16_t)(srclen - sw), srcw, NULL) + 1);
        rs = t < sh ? t : sh;
        t = nk_div_u16((uint16_t)((uint32_t)rs * dh), sh, NULL);
        c = (uint16_t)(nk_div_u16((uint16_t)(dstlen - dw), dstw, NULL) + 1);
        if (c >= dh)
            c = dh;
        rd = t > c ? c : t;
        t = nk_div_u16((uint16_t)((uint32_t)rd * sh), dh, NULL);
        rs = t > rs ? rs : t;
        asm_blit_scaled_key_flip(srcp, 0, 0, sw, rs, srcw, srch,
                                 dstp, 0, 0, dw, rd, dstw, dsth, 0xFE, flags);
        sh -= rs;
        dh -= rd;
        sy += rs;
        dy += rd;
    } while (sh != 0);
    mm_ems_unlock(g_blit_lock);
}

/*
 * sys_blit_0F21 - run a strip routine (sys_line_*) over the rows of two
 * slots: fn(src, sx, 0, w, row0, rows, srcw, dst, dx, 0, limit, dstw,
 * slope, FEh) per mapped piece, row0 counting the rows done.
 *
 * Original: IST.EXE 0AC6:0F21 (SYSTEMS.L), far cdecl (sx, sy, w, h, dx, dy,
 * limit, slope, src_slot, dst_slot, fn).  Only whole rows are mapped (the
 * x offsets are left to fn).
 * Verified: tests/py/test_graphics.py (test_blit_0F21).
 */
void sys_blit_0F21(uint16_t sx, uint16_t sy, uint16_t w, uint16_t h, uint16_t dx,
                   uint16_t dy, uint16_t limit, uint8_t slope, uint8_t src_slot,
                   uint8_t dst_slot, SysStripFn fn)
{
    uint16_t slot = 0, srclen, dstlen, rows, a, b, row0 = 0;
    uint16_t srcw = (uint16_t)g_surf_w(src_slot), dstw = (uint16_t)g_surf_w(dst_slot);
    MmHandle *srec = slot_rec(src_slot), *drec = slot_rec(dst_slot);
    uint8_t *srcp, *dstp;

    if (w == 0 || h == 0)
        return;
    mm_ems_lock(g_blit_lock, 0x438, DGSTR(0x723A));
    do {
        slot = 0;
        srclen = 0xFFFF;
        dstlen = 0xFFFF;
        mm_map(srec, &srcp, &srclen, &slot, (uint32_t)sy * srcw);
        mm_map(drec, &dstp, &dstlen, &slot, (uint32_t)dy * dstw);
        a = nk_div_u16(srclen, srcw, NULL);
        b = nk_div_u16(dstlen, dstw, NULL);
        rows = a < b ? a : b;
        if (rows >= h)
            rows = h;
        fn(srcp, sx, 0, w, row0, rows, srcw, dstp, dx, 0, limit, dstw, slope, 0xFE);
        sy += rows;
        dy += rows;
        row0 += rows;
        h -= rows;
    } while (h != 0);
    mm_ems_unlock(g_blit_lock);
}

/*
 * sys_line_10F1 / 1185 / 1261 / 1307 - strip routines for sys_blit_0F21:
 * per row i in [row0, row0+rows) copy a span of w + 2i bytes with colour
 * key (mm_copy_transparent), the destination column moving by `slope`
 * (10F1/1261: dx + slope*i, right) or -(slope+2) (1185/1307: left) per
 * row and clipped at `limit`; 10F1/1185 walk rows downward, 1261/1307
 * upward (row index sy + row0 + rows - i).
 * A span entirely beyond the limit ends the routine (not just the row).
 * Rows are indexed from the start of the whole blit while sys_blit_0F21
 * passes the pointer of the current mapped piece, so later pieces address
 * past the piece: the 16-bit offset then wraps inside the segment (the
 * EMS frame), modelled with seg_add.
 *
 * Original: IST.EXE 0AC6:10F1, 1185, 1261, 1307 (SYSTEMS.L), far cdecl,
 * all 16-bit offset arithmetic; comparisons unsigned (10F1/1261) and
 * signed (1185/1307) as in the listing.
 * Verified: tests/py/test_graphics.py (test_blit_0F21).
 */
void sys_line_10F1(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t row0,
                   uint16_t rows, uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                   uint16_t limit, uint16_t dpitch, uint8_t slope, uint8_t key)
{
    uint16_t i, d0, len;
    for (i = row0; (uint16_t)(row0 + rows) > i; i++) {
        d0 = (uint16_t)(slope * i + dx);
        len = (uint16_t)(w + 2 * i);
        if ((uint16_t)(d0 + len) > limit) {
            if (d0 >= limit)
                return;
            len = (uint16_t)(limit - d0);
        }
        mm_copy_transparent(seg_add(src, (uint16_t)((sy + i) * spitch) - i + sx),
                            seg_add(dst, (uint16_t)((dy + i) * dpitch) + d0), len, key);
    }
}

void sys_line_1185(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t row0,
                   uint16_t rows, uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                   uint16_t limit, uint16_t dpitch, uint8_t slope, uint8_t key)
{
    uint16_t i, s0, d0, len;
    for (i = row0; (uint16_t)(row0 + rows) > i; i++) {
        s0 = (uint16_t)(sx - i);
        len = (uint16_t)(w + 2 * i);
        d0 = (uint16_t)(dx - (slope + 2) * i);
        if ((int16_t)d0 < (int16_t)limit) {
            if ((int16_t)(d0 + len) <= (int16_t)limit)
                return;
            s0 = (uint16_t)(slope * i + sx + i + limit - dx);
            d0 = limit;
            len = (uint16_t)((w + 2 * i) - (limit - (uint16_t)(dx - (slope + 2) * i)));
        }
        mm_copy_transparent(seg_add(src, (uint16_t)((sy + i) * spitch) + s0),
                            seg_add(dst, (uint16_t)((dy + i) * dpitch) + d0), len, key);
    }
}

void sys_line_1261(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t row0,
                   uint16_t rows, uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                   uint16_t limit, uint16_t dpitch, uint8_t slope, uint8_t key)
{
    uint16_t i, d0, len;
    sy += (uint16_t)(rows + row0);
    dy += (uint16_t)(rows + row0);
    for (i = row0; (uint16_t)(row0 + rows) > i; i++) {
        d0 = (uint16_t)(slope * i + dx);
        len = (uint16_t)(w + 2 * i);
        if ((uint16_t)(d0 + len) > limit) {
            if (d0 >= limit)
                return;
            len = (uint16_t)(limit - d0);
        }
        mm_copy_transparent(seg_add(src, (uint16_t)((sy - i) * spitch) + sx - i),
                            seg_add(dst, (uint16_t)((dy - i) * dpitch) + d0), len, key);
    }
}

void sys_line_1307(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t row0,
                   uint16_t rows, uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                   uint16_t limit, uint16_t dpitch, uint8_t slope, uint8_t key)
{
    uint16_t i, s0, d0, len;
    sy += (uint16_t)(rows + row0);
    dy += (uint16_t)(rows + row0);
    for (i = row0; (uint16_t)(row0 + rows) > i; i++) {
        s0 = (uint16_t)(sx - i);
        len = (uint16_t)(w + 2 * i);
        d0 = (uint16_t)(dx - (slope + 2) * i);
        if ((int16_t)d0 < (int16_t)limit) {
            if ((int16_t)(d0 + len) <= (int16_t)limit)
                return;
            s0 = (uint16_t)(slope * i + sx + i + limit - dx);
            d0 = limit;
            len = (uint16_t)((w + 2 * i) - (limit - (uint16_t)(dx - (slope + 2) * i)));
        }
        mm_copy_transparent(seg_add(src, (uint16_t)((sy - i) * spitch) + s0),
                            seg_add(dst, (uint16_t)((dy - i) * dpitch) + d0), len, key);
    }
}

/*
 * sys_fill_slot_rect - draw a line from (x1,y1) to (x2,y2) in color on a
 * 320-pixel-wide slot (the name is historical: it is the line drawer used
 * by sys_fill_rect_checked and the automap).  DDA in 1/100 pixel units.
 *
 * Original: IST.EXE 0AC6:13F5 (SYSTEMS.L), far cdecl (x1, y1, x2, y2,
 * color, slot).  Quirks kept: the pitch is always 320; a mostly-horizontal
 * line with x2 < x1 (or mostly-vertical with y2 < y1) draws nothing,
 * because the counter starts negative; dx = dy = 0 divides by zero
 * (handler: step = -1, one pixel drawn); EMS slots are locked once for
 * w*h (16-bit) bytes from offset 0.
 * Verified: tests/py/test_graphics.py (test_lines).
 */
void sys_fill_slot_rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color,
                        uint8_t slot)
{
    int16_t dx = (int16_t)(x2 - x1), dy = (int16_t)(y2 - y1);
    int16_t step, sgn, i, acc, adx, ady;
    uint8_t ems, *base = NULL;
    MmHandle *rec;

    mm_ems_lock(g_blit_lock, 0x544, DGSTR(0x724F));
    rec = slot_rec(slot);
    /* (a NULL slot reads 0000:0008 in the original; the fatal below exits) */
    if (rec != NULL && rec->type == 1) {
        base = EMSLock((int16_t)rec->ems, 0,
                       (uint16_t)(g_surf_w(slot) * g_surf_h(slot)));
        ems = 1;
    } else {
        ems = 0;
    }
    if (slot_null(slot))
        o559A_fatal(DGSTR(0x7264), 0x553, NULL, 0x17);
    adx = dx < 0 ? (int16_t)-dx : dx;
    ady = dy < 0 ? (int16_t)-dy : dy;
    if (adx >= ady) {
        step = nk_idiv_s16((int16_t)(dy * 100), dx, NULL);
        sgn = dx > 0 ? 1 : -1;
        for (i = dx, acc = (int16_t)(dy * 100); i >= 0;
             i = (int16_t)(i - sgn), acc = (int16_t)(acc - step * sgn)) {
            uint16_t o = (uint16_t)(x1 + i + (int16_t)((int16_t)(acc / 100 + y1) * 320));
            (ems ? base : mm_conv_ptr(slot_rec(slot)))[o] = color;
        }
    } else {
        step = nk_idiv_s16((int16_t)(dx * 100), dy, NULL);
        sgn = dy > 0 ? 1 : -1;
        for (acc = (int16_t)(dx * 100), i = dy; i >= 0;
             acc = (int16_t)(acc - step * sgn), i = (int16_t)(i - sgn)) {
            uint16_t o = (uint16_t)(x1 + acc / 100 + (int16_t)((int16_t)(y1 + i) * 320));
            (ems ? base : mm_conv_ptr(slot_rec(slot)))[o] = color;
        }
    }
    mm_ems_unlock(g_blit_lock);
}

/*
 * sys_fill_slot - fill the first len bytes of a slot (clamped to the block
 * size) with color.
 *
 * Original: IST.EXE 0AC6:16DC (SYSTEMS.L), far cdecl (slot, len, color).
 * EMS: one EMSLock from offset 0; fatal 9 when the run would pass the
 * 64 KB frame, fatal 19h for other block types.
 * Verified: tests/py/test_graphics.py (test_fill_slot).
 */
void sys_fill_slot(uint16_t slot, uint16_t len, uint8_t color)
{
    MmHandle *rec;
    uint8_t *p;

    mm_ems_lock(g_blit_lock, 0x57B, DGSTR(0x7279));
    if (slot_null(slot))
        o559A_fatal(DGSTR(0x728E), 0x57E, NULL, 0x18);
    rec = slot_rec(slot);
    if ((uint32_t)len > rec->size)
        len = (uint16_t)rec->size;
    if (rec->type == 0) {
        asm_memset32(mm_conv_ptr(rec), color32(color), len);
    } else if (rec->type == 1) {
        p = EMSLock((int16_t)rec->ems, 0, len);
        if ((uint32_t)(frame_off(p) & 0x3FFF) + len > 0xFFFF)
            o559A_fatal(DGSTR(0x72A3), 0x586, NULL, 9);
        asm_memset32(p, color32(color), len);
    } else {
        o559A_fatal(DGSTR(0x72B8), 0x588, NULL, 0x19);
    }
    mm_ems_unlock(g_blit_lock);
}

/*
 * sys_find_glyph - nearest colour: the entry of pal2 (256 RGB triples)
 * with the smallest sum of absolute component differences to pal[idx]
 * (8-bit sums, first minimum wins, only sums < C0h count).  Returns
 * (sum << 8) | index, or FFFFh after printing "bulamadim!!!".
 *
 * Original: IST.EXE 0AC6:18AF (SYSTEMS.L), far cdecl (pal, idx, pal2), asm.
 * Unreferenced.
 * Verified: tests/py/test_graphics.py (test_putpixel_and_colors).
 */
uint16_t sys_find_glyph(const uint8_t *pal, uint16_t idx, const uint8_t *pal2)
{
    uint8_t ref[3], best = 0xC0, at = 0, sum, d;
    int i, k;
    const uint8_t *p = pal + (uint16_t)(idx * 3);

    memcpy(ref, p, 3);
    for (i = 0; i < 256; i++) {
        sum = 0;
        for (k = 0; k < 3; k++) {
            d = (uint8_t)(pal2[i * 3 + k] - ref[k]);
            if (pal2[i * 3 + k] < ref[k])
                d = (uint8_t)-d;
            sum = (uint8_t)(sum + d);
        }
        if (sum < best) {
            best = sum;
            at = (uint8_t)i;
        }
    }
    if (best == 0xC0) {
        printf("%s", DGSTR(0x72CD));
        return 0xFFFF;
    }
    return (uint16_t)((best << 8) | at);
}

/*
 * sys_find_color - index of the palette entry nearest to the average of
 * entries a and b (component (x + y) >> 1 in 8 bits; sum of absolute
 * differences; a sum <= the best so far (start C0h) replaces it, so the
 * LAST minimum wins).  FFFFh and "bulamadim!!!" if the best is still C0h.
 *
 * Original: IST.EXE 0AC6:195E (SYSTEMS.L), far cdecl (pal, a, b), asm.
 * Verified: tests/py/test_graphics.py (test_putpixel_and_colors).
 */
uint16_t sys_find_color(const uint8_t *pal, uint16_t a, uint16_t b)
{
    uint8_t ref[3], best = 0xC0, at = 0, sum, d;
    int i, k;
    const uint8_t *pb = pal + (uint16_t)(b * 3), *pa = pal + (uint16_t)(a * 3);

    for (k = 0; k < 3; k++)
        ref[k] = (uint8_t)((uint8_t)(pa[k] + pb[k]) >> 1);
    for (i = 0; i < 256; i++) {
        sum = 0;
        for (k = 0; k < 3; k++) {
            d = (uint8_t)(pal[i * 3 + k] - ref[k]);
            if (pal[i * 3 + k] < ref[k])
                d = (uint8_t)-d;
            sum = (uint8_t)(sum + d);
        }
        if (sum <= best) {
            best = sum;
            at = (uint8_t)i;
        }
    }
    /* the original tests DH != C0h, so a best sum of exactly C0h also
       reports "not found" */
    if (best == 0xC0) {
        printf("%s", DGSTR(0x72DA));
        return 0xFFFF;
    }
    return at;
}

/*
 * sys_build_aa_table - fill row n of the triangular blend table: for every
 * colour i the nearest colour to the average of i and n in the target
 * palette (E5EF).  Entries i < n go to n-1 + sum(0xFF - k, k=1..i), the
 * entries i > n follow consecutively.
 *
 * Original: IST.EXE 0AC6:1A13 (SYSTEMS.L), far cdecl (table, n).
 * Verified: tests/py/test_graphics.py (test_build_aa_table).
 */
void sys_build_aa_table(uint8_t *table, int16_t n)
{
    uint16_t k = (uint16_t)(n - 1);
    int16_t i;

    for (i = 0; i < n; ) {
        table[k] = (uint8_t)sys_find_color(gr_target_pal, (uint16_t)i, (uint16_t)n);
        i++;
        k = (uint16_t)(k + (0xFF - i));
    }
    for (i = (int16_t)(n + 1); i < 0x100; i++) {
        k++;
        table[k] = (uint8_t)sys_find_color(gr_target_pal, (uint16_t)i, (uint16_t)n);
    }
}

/*
 * sys_load_rix - load a .RIX blend table into the EMS block E52F unless
 * the same name is already loaded.  Returns 1.
 *
 * Original: IST.EXE 0AC6:1AA0 (SYSTEMS.L), far cdecl (name).  The name is
 * copied into E570 (12 bytes) without a bound check, before loading.
 * Verified: tests/py/test_graphics.py (test_rix, real XD*.RIX files).
 */
uint8_t sys_load_rix(const char *name)
{
    if (mm_ref(G_RIX_TABLE_REF) == NULL)
        memset(g_rix_name, 0, 0x0C);
    if (strcmp(name, g_rix_name) == 0)
        return 1;
    strcpy(g_rix_name, name);
    if (mm_ref(G_RIX_TABLE_REF) != NULL)
        ctl_free_checked(G_RIX_TABLE_REF, 0x69E, DGSTR(0x72E7));
    mm_ref_set(G_RIX_TABLE_REF, mm_alloc(0xC001, 1));
    fm_load_file(1, name, mm_ref(G_RIX_TABLE_REF));
    return 1;
}

/*
 * sys_free_rix - free the .RIX table (if any), clear E56C/E56E and the name.
 *
 * Original: IST.EXE 0AC6:1B58 (SYSTEMS.L), far cdecl, unreferenced (bytes
 * after sys_load_rix that the listing left undisassembled).
 * Verified: tests/py/test_graphics.py (test_rix).
 */
void sys_free_rix(void)
{
    if (mm_ref(G_RIX_TABLE_REF) == NULL)
        return;
    ctl_free_checked(G_RIX_TABLE_REF, 0x6A9, DGSTR(0x72FC));
    g_rix_e56e = 0;
    g_rix_e56c = 0;
    strcpy(g_rix_name, DGSTR(0x7311));                /* "" */
}
