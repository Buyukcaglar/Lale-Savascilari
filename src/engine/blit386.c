/*
 * blit386.c - hand-written 386 assembly of IST.EXE segment 2599 (see blit386.h).
 *
 * The segment is data-class (no listing); it was disassembled from the flat
 * image (capstone, 16-bit code with 66h-prefixed 32-bit operations).
 * CS:0000-0017 are work variables of the scaled blits, written through CS:
 *     CS:0000 dword  x step (16.16)          CS:000A word  rows left
 *     CS:0004 dword  y step (16.16)          CS:000C dword y accumulator
 *     CS:0014 word   +1/-1 x direction       CS:0016 word  row end correction
 * The segment (runtime 2DBDh) overlaps DGROUP (1F04h): these are DS:EB90-
 * EBA7, kept in dg[] (blit386.h g_blit_*).  The routines compute in locals
 * and store what the original leaves there (the values at the last store;
 * nothing else runs in between).  There is no self-modifying code.
 *
 * Error paths that crash the original (they are never reached by the game):
 *   - 0018/0144 jump to 2599:0138 when sy*spitch / dy*dpitch overflow 16 bits:
 *     it pops 7 registers but skips LEAVE, so RETF returns through the
 *     uninitialised ENTER frame.  The port just returns.
 *   - 0018/0144/0234 divide by dw and dh (0234: dw): 0 is a #DE.
 *   - 0341's error path does POP SI / POP DS after pushing only DS: it returns
 *     0 with DS loaded from an uninitialised local.
 * Register state the code does not initialise is documented where it is read
 * (upper ESI = asm_esi_hi; upper ECX in 0144 assumed 0).
 */
#include "engine/blit386.h"
#include "platform/pc.h"

#include <string.h>

uint16_t asm_esi_hi = 0;

/*
 * blend_lookup - the triangular blend table lookup shared by 0440, 04AD, 0500.
 * For a != b with lo = min, hi = max:
 *     index = (lo * (509 - lo)) / 2 - 1 + hi        (16-bit arithmetic)
 * i.e. row lo of a packed upper triangle (255 + 254 + ... entries), so the
 * table holds 32640 bytes.  Equal colours are returned unchanged (no lookup).
 */
static uint8_t blend_lookup(const uint8_t *table, uint8_t a, uint8_t b)
{
    if (a == b)
        return a;
    uint8_t lo = a < b ? a : b, hi = a < b ? b : a;      /* jb / xchg cl,al */
    uint16_t ix = (uint16_t)((uint16_t)(lo * (0x1FD - lo)) >> 1);
    ix = (uint16_t)(ix - 1 + hi);                         /* dec ax; add al,cl; adc ah,0 */
    return table[ix];                                     /* FS:[BX+AX], offset wraps */
}

/*
 * asm_blit_scaled_key_flip - scale the sw x sh source rectangle at (sx,sy) to
 * dw x dh at (dx,dy) of dst, skipping source bytes equal to key; flags bit 0
 * mirrors horizontally.
 *
 * Original: IST.EXE 2599:0018 (hand-written 386 asm), far cdecl.  Called by
 * 0AC6:0EE8 with key 0FEh.  The original returns the caller's AX (void).
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 *
 * Stepping: xstep = (sw << 16) / dw, ystep = (sh << 16) / dh (unsigned 32-bit,
 * truncating).  The source x position is ESI rotated: SI = integer part,
 * upper ESI = fraction, and the fraction starts at asm_esi_hi (never
 * initialised) for every row - rows restart from the saved ESI (EDX).  Rows
 * advance by spitch * (yacc >> 16) after each output row (yacc starts at 0,
 * so the first two output rows can read the same source row).
 */
void asm_blit_scaled_key_flip(const uint8_t *src, uint16_t sx, uint16_t sy,
                              uint16_t sw, uint16_t sh, uint16_t spitch, uint16_t unused14,
                              uint8_t *dst, uint16_t dx, uint16_t dy,
                              uint16_t dw, uint16_t dh, uint16_t dpitch, uint16_t unused24,
                              uint8_t key, uint8_t flags)
{
    uint32_t p;
    uint16_t si, di, dir, row_fix, rows;
    uint32_t xstep, ystep, yacc;

    p = (uint32_t)sy * spitch;
    if (p >> 16)
        return;                       /* 2599:0032 -> 0138: crashes in the original */
    si = (uint16_t)(p + sx);
    p = (uint32_t)dy * dpitch;
    if (p >> 16)
        return;                       /* 2599:004B -> 0138 */
    di = (uint16_t)(p + dx);

    if (dw == 0 || dh == 0)
        return;                       /* DIV ECX by 0: #DE in the original */
    xstep = ((uint32_t)sw << 16) / dw;                    /* CS:0000 */
    ystep = ((uint32_t)sh << 16) / dh;                    /* CS:0004 */
    yacc = 0;                                             /* CS:000C */
    rows = dh;                                            /* CS:000A */

    if (!(flags & 1)) {               /* CLD */
        dir = 1;
        row_fix = (uint16_t)-dw;      /* CS:0016 */
    } else {                          /* STD, start at the right end */
        dir = 0xFFFF;
        row_fix = dw;
        di = (uint16_t)(di + dw - 1);
    }
    g_blit_xstep = xstep;
    g_blit_ystep = ystep;
    g_blit_xdir = dir;
    g_blit_row_fix = row_fix;

    do {
        /* ESI rotated: integer part (SI) in the top half, fraction below */
        uint32_t r = ((uint32_t)si << 16) | asm_esi_hi;
        uint16_t n = dw;
        do {
            uint8_t al = src[(uint16_t)(r >> 16)];
            if (al != key)
                dst[di] = al;         /* STOSB (DF gives the direction) */
            di = (uint16_t)(di + dir);
            r += xstep;               /* ror esi,16 / add esi,[0] / rol esi,16 */
        } while (--n);
        /* ESI restored from EDX: SI back at the row start, fraction unchanged */
        yacc += ystep;
        uint16_t skip = (uint16_t)(yacc >> 16);
        if (skip) {
            while (skip--)            /* add si,[spitch] / loop */
                si = (uint16_t)(si + spitch);
            yacc &= 0xFFFF;
        }
        di = (uint16_t)(di + dpitch + row_fix);
    } while (--rows);                 /* dh = 0 cannot reach here (#DE above) */
    g_blit_yacc = yacc;
    g_blit_rows = 0;
}

/*
 * asm_blit_scaled_key - scaled colour-key blit without mirror.
 *
 * Original: IST.EXE 2599:0144 (hand-written 386 asm), far cdecl; unreferenced.
 * Same arguments as 2599:0018 except that the key is the word at [bp+28h]
 * ([bp+26h] is unused) and there are no flags.
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 *
 * Unlike 0018 this version does not save ESI per row, and its row advance is
 * buggy (kept): after a row SI has moved by the sum of the x steps (about sw),
 * then
 *   - if the y accumulator crossed an integer: SI += spitch - sw ONCE (the
 *     instruction is "rep add esi,eax": REP on a non-string op is ignored),
 *     however many source rows the step covers;
 *   - else SI -= sw.
 * The fraction keeps running across rows and is left in asm_esi_hi.  Upper
 * ECX is not cleared before the DIVs; the port assumes 0.
 */
void asm_blit_scaled_key(const uint8_t *src, uint16_t sx, uint16_t sy,
                         uint16_t sw, uint16_t sh, uint16_t spitch, uint16_t unused14,
                         uint8_t *dst, uint16_t dx, uint16_t dy,
                         uint16_t dw, uint16_t dh, uint16_t dpitch, uint16_t unused24,
                         uint16_t unused26, uint8_t key)
{
    uint32_t p, xstep, ystep, yacc, r;
    uint16_t di, rows;

    p = (uint32_t)sy * spitch;
    if (p >> 16)
        return;                       /* 2599:015D -> 0138: crashes in the original */
    r = ((uint32_t)(uint16_t)(p + sx) << 16) | asm_esi_hi;
    p = (uint32_t)dy * dpitch;
    if (p >> 16)
        return;
    di = (uint16_t)(p + dx);
    if (dw == 0 || dh == 0)
        return;                       /* #DE */
    xstep = ((uint32_t)sw << 16) / dw;                    /* CS:0000 */
    ystep = ((uint32_t)sh << 16) / dh;                    /* CS:0004 */
    yacc = 0;                                             /* CS:000C */
    rows = dh;                                            /* CS:000A */
    g_blit_xstep = xstep;
    g_blit_ystep = ystep;

    do {
        uint16_t n = dw;
        do {
            uint8_t al = src[(uint16_t)(r >> 16)];
            if (al != key)
                dst[di] = al;
            di++;
            r += xstep;
        } while (--n);
        yacc += ystep;
        if (yacc >> 16) {
            r += (uint32_t)(uint16_t)(spitch - sw) << 16;   /* 2599:0200 */
            yacc &= 0xFFFF;
        } else {
            r -= (uint32_t)sw << 16;                         /* 2599:0218 */
        }
        di = (uint16_t)(di + dpitch - dw);
    } while (--rows);
    g_blit_yacc = yacc;
    g_blit_rows = 0;
    asm_esi_hi = (uint16_t)r;
}

/*
 * asm_span_scaled_key - copy dw bytes scaled from sw source bytes, skipping
 * key bytes.  Fraction starts at 0 ("and esi,0FFFFh"); xstep = (sw<<16)/dw.
 *
 * Original: IST.EXE 2599:0234 (hand-written 386 asm), far cdecl; unreferenced.
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 */
void asm_span_scaled_key(const uint8_t *src, uint8_t *dst, uint16_t sw, uint16_t dw,
                         uint8_t key)
{
    uint32_t xstep, r = 0;
    uint16_t di = 0, n = dw;

    if (dw == 0)
        return;                       /* #DE */
    xstep = ((uint32_t)sw << 16) / dw;
    do {
        uint8_t al = src[(uint16_t)(r >> 16)];
        if (al != key)
            dst[di] = al;
        di++;
        r += xstep;
    } while (--n);
    asm_esi_hi = (uint16_t)r;         /* the fraction stays in upper ESI */
}

/*
 * asm_memset32 - count/4 dwords of value, then count%4 bytes of its low byte.
 *
 * Original: IST.EXE 2599:028F (hand-written 386 asm), far cdecl.
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 */
void asm_memset32(uint8_t *dst, uint32_t value, uint16_t count)
{
    uint16_t n = count / 4, i;
    uint8_t v[4] = { (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                     (uint8_t)(value >> 24) };
    for (i = 0; i < n; i++)
        memcpy(dst + 4 * i, v, 4);
    memset(dst + 4 * n, v[0], count % 4);
}

/*
 * asm_memcpy32 - REP MOVSD of count/4 dwords, then count%4 bytes, forward.
 * Overlapping copies behave like the 386: each dword is read, then written.
 *
 * Original: IST.EXE 2599:02AF (hand-written 386 asm), far cdecl.
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 */
void asm_memcpy32(uint8_t *dst, const uint8_t *src, uint16_t count)
{
    uint16_t n = count / 4, i;
    uint8_t t[4];
    for (i = 0; i < n; i++) {
        memcpy(t, src + 4 * i, 4);
        memcpy(dst + 4 * i, t, 4);
    }
    for (i = (uint16_t)(4 * n); i < count; i++)
        dst[i] = src[i];
}

/*
 * asm_copy_rect - opaque copy of a w x h rectangle.
 * Per row: (w & 3) single bytes first, then w/4 dwords.  h = 0 copies nothing.
 * Returns 0FFFFh, or 0 if sy*spitch or dy*dpitch overflow 16 bits.
 *
 * Original: IST.EXE 2599:02D0 (hand-written 386 asm), far cdecl; passed as a
 * far function pointer to the blit dispatcher 0AC6:0430.
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 */
int16_t asm_copy_rect(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t h,
                      uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy, uint16_t dpitch)
{
    uint32_t p;
    uint16_t si, di, i;
    uint8_t t[4];

    p = (uint32_t)spitch * sy;
    if (p >> 16)
        return 0;
    si = (uint16_t)(p + sx);
    p = (uint32_t)dpitch * dy;
    if (p >> 16)
        return 0;
    di = (uint16_t)(p + dx);

    for (; h != 0; h--) {
        for (i = 0; i < (w & 3); i++)
            dst[di++] = src[si++];
        for (i = 0; i < (w >> 2); i++) {
            memcpy(t, src + si, 4);
            memcpy(dst + di, t, 4);
            si = (uint16_t)(si + 4);
            di = (uint16_t)(di + 4);
        }
        si = (uint16_t)(si + spitch - w);
        di = (uint16_t)(di + dpitch - w);
    }
    return -1;
}

/*
 * asm_copy_rect_key - like asm_copy_rect but bytes equal to key are skipped.
 * Quirk: w = 0 copies 65536 bytes per row (DEC CX / JNE, no zero test).
 *
 * Original: IST.EXE 2599:0341 (hand-written 386 asm), far cdecl; far fn ptr.
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 */
int16_t asm_copy_rect_key(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t h,
                          uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                          uint16_t dpitch, uint8_t key)
{
    uint32_t p;
    uint16_t si, di, n;

    p = (uint32_t)spitch * sy;
    if (p >> 16)
        return 0;                     /* + corrupted DS in the original */
    si = (uint16_t)(p + sx);
    p = (uint32_t)dpitch * dy;
    if (p >> 16)
        return 0;
    di = (uint16_t)(p + dx);

    for (; h != 0; h--) {
        n = w;
        do {
            uint8_t al = src[si++];
            if (al != key)
                dst[di] = al;
            di++;
        } while (--n);
        si = (uint16_t)(si + spitch - w);
        di = (uint16_t)(di + dpitch - w);
    }
    return -1;
}

/*
 * asm_copy_rect_key_mirror - colour-key copy, horizontally mirrored:
 * dst row x + w-1-i receives src row x + i.  w = 0 or h = 0 copy nothing.
 *
 * Original: IST.EXE 2599:03C4 (hand-written 386 asm), far cdecl; far fn ptr.
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 */
int16_t asm_copy_rect_key_mirror(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w,
                                 uint16_t h, uint16_t spitch, uint8_t *dst, uint16_t dx,
                                 uint16_t dy, uint16_t dpitch, uint8_t key)
{
    uint32_t p;
    uint16_t si, di, n;

    p = (uint32_t)spitch * sy;
    if (p >> 16)
        return 0;
    si = (uint16_t)(p + sx);
    p = (uint32_t)dpitch * dy;
    if (p >> 16)
        return 0;
    di = (uint16_t)(p + dx);
    if (h == 0 || w == 0)
        return -1;

    di--;                             /* 2599:0417, once */
    for (; h != 0; h--) {
        n = w;
        di = (uint16_t)(di + w);
        do {
            uint8_t al = src[si++];
            if (al != key)
                dst[di] = al;
            di--;
        } while (--n);
        si = (uint16_t)(si + spitch - w);
        di = (uint16_t)(di + dpitch);
    }
    return -1;
}

/*
 * asm_tint_rect - replace every pixel p != colour of the rectangle by
 * blend(p, colour) (pixels equal to colour are rewritten unchanged).
 *
 * Original: IST.EXE 2599:0440 (hand-written 386 asm), far cdecl; unreferenced.
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 *
 * Quirks kept: y*pitch is not overflow-checked; the row loop runs while
 * DI < start+w (unsigned 16-bit, tested after each pixel), so w = 0 still
 * processes one pixel and the following rows shift right by one; the row
 * count is the low byte of h, 0 meaning 256.
 */
void asm_tint_rect(uint8_t *buf, const uint8_t *table, uint16_t x, uint16_t y,
                   uint16_t w, uint16_t h, uint16_t pitch, uint8_t colour)
{
    uint16_t di = (uint16_t)(y * pitch + x);
    uint16_t row_skip = (uint16_t)(pitch - w);          /* [bp-4] */
    uint8_t rows = (uint8_t)h;

    do {
        uint16_t si = di;
        uint16_t end = (uint16_t)(di + w);                /* [bp-2] */
        do {
            uint8_t al = buf[si++];
            if (al != colour)
                al = blend_lookup(table, al, colour);
            buf[di++] = al;
        } while (di < end);
        di = (uint16_t)(di + row_skip);
    } while (--rows);
}

/*
 * asm_blend_span - dst[i] = blend(src1[i], src2[i]) for count bytes
 * (at least one: the loop tests DI < start+count after each byte).
 *
 * Original: IST.EXE 2599:04AD (hand-written 386 asm), far cdecl; called by
 * 0C7F:0A0F.
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 */
void asm_blend_span(const uint8_t *src1, const uint8_t *src2, uint8_t *dst,
                    const uint8_t *table, uint16_t count)
{
    uint16_t i = 0;
    do {
        dst[i] = blend_lookup(table, src1[i], src2[i]);
        i++;
    } while (i < count);
}

/*
 * asm_upscale_view2x - draw the 3D view buffer (pitch 100) doubled into the
 * mode 13h screen: source rows 2..98, columns 3..95 become screen x 6..190,
 * rows 4..195; in-between pixels are blends of their neighbours.  Source
 * bytes 0FEh are "transparent": they take the screen pixel already there.
 *
 * Original: IST.EXE 2599:0500 (hand-written 386 asm), far cdecl; called by
 * 3FD1:4D26.  Writes A000:0000 -> pc_a000[].
 * Verified: tests/py/test_blit386.py (unicorn differential), 2026-11-20.
 *
 * Pass 1, even rows 4..194 (di = 0506h + 640k): pixel s0 is written (0FEh ->
 * screen), then for each of 92 further source bytes s: the pixel between is
 * blend(prev, s) and s itself follows.  The 0FEh replacements of both blend
 * inputs read the screen at the in-between position, and the stored right
 * pixel is that replaced value.
 * Pass 2, odd rows 5..195: vertical blends V of source rows r and r+1 (0FEh
 * -> screen pixel at the position being written), then H = blend(Vprev, V)
 * (0FEh again -> screen pixel at H's position) and V.  The 386 code juggles
 * Vprev/V in the bytes of EAX; only the values reach the screen.
 */
void asm_upscale_view2x(const uint8_t *view, const uint8_t *table)
{
    uint8_t *scr = pc_a000;
    uint16_t si, bx, di;
    uint8_t al, cl;
    int n;

    /* ---- pass 1: even screen rows ---- */
    si = 0xCB;                        /* row 2, column 3 */
    di = 0x506;                       /* row 4, x 6 */
    do {
        al = view[si++];
        if (al == 0xFE)
            al = scr[di];
        scr[di++] = al;
        for (n = 0x5C; n; n--) {
            cl = view[si];
            if (al == 0xFE)
                al = scr[di];
            if (cl == 0xFE)
                cl = scr[di];
            scr[di++] = blend_lookup(table, al, cl);
            scr[di++] = cl;
            al = cl;
            si++;
        }
        si += 7;
        di += 0x1C7;
    } while (di < 0xF500);

    /* ---- pass 2: odd screen rows ---- */
    si = 0xCB;
    bx = 0xCB + 100;
    di = 0x646;                       /* row 5, x 6 */
    do {
        uint8_t vprev, v;
        al = view[si++];
        cl = view[bx++];
        if (al == 0xFE)
            al = scr[di];
        if (cl == 0xFE)
            cl = scr[di];
        vprev = blend_lookup(table, al, cl);
        scr[di++] = vprev;
        for (n = 0x5C; n; n--) {
            al = view[si++];
            cl = view[bx++];
            if (al == 0xFE)
                al = scr[di];
            if (cl == 0xFE)
                cl = scr[di];
            v = blend_lookup(table, al, cl);
            al = vprev;
            cl = v;
            if (al == 0xFE)
                al = scr[di];
            if (cl == 0xFE)
                cl = scr[di];
            scr[di++] = blend_lookup(table, al, cl);
            scr[di++] = v;
            vprev = v;
        }
        si += 7;
        bx += 7;
        di += 0x1C7;
    } while (di < 0xF500);
}
