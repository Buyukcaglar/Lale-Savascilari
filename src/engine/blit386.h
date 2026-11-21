/*
 * blit386.h - hand-written 386 assembly helpers of IST.EXE (segment 2599 in the
 * flat image: a data-class segment, no C source, no listing).  Scaled and
 * mirrored colour-key blits, 32-bit memset/memcpy, rectangle copies, the
 * triangular blend table and the 2x blending upscaler of the 3D view.
 *
 * Pointer model: every far pointer becomes a native pointer, and the 16-bit
 * offset registers (SI/DI/BX/BP) are reproduced as uint16_t offsets RELATIVE
 * to that pointer.  This is bit-exact with the original whenever the far
 * pointer's offset part is 0 (paragraph-aligned buffers, A000:0000, ...);
 * with another offset only the point where 64 KB wrap-around happens (and the
 * "offset add carried" error checks) would differ, which no caller reaches.
 */
#ifndef LALE_ENGINE_BLIT386_H
#define LALE_ENGINE_BLIT386_H

#include <stdint.h>

#include "engine/dgroup.h"

/* Upper 16 bits of the 386 ESI register as left by the last 2599 routine.
   The scaled blits keep the 16.16 source x position in ESI with the integer
   part in SI and the fraction in the upper half, which the routines never
   initialise (asm_blit_scaled_key_flip, asm_blit_scaled_key) - so the first
   column's fraction is whatever the CPU held.  In the game nothing else of
   ours touches it (DSMI's AMP player does MOVZX ESI, i.e. zeroes it), so it is
   modelled as this global, initially 0.  Tests set it like the emulator's ESI. */
extern uint16_t asm_esi_hi;

/* Work variables of the scaled blits, CS:0000-0017 of segment 2599.  At run
   time that code segment (2DBDh) lies inside DGROUP's 64 KB (1F04h), at
   DS:EB90-EBA7, so the stores land in dg[] (other code sees these bytes). */
#define g_blit_xstep     DG32(0xEB90)   /* CS:0000 u32 x step (16.16) */
#define g_blit_ystep     DG32(0xEB94)   /* CS:0004 u32 y step (16.16) */
#define g_blit_rows      DG16(0xEB9A)   /* CS:000A u16 rows left */
#define g_blit_yacc      DG32(0xEB9C)   /* CS:000C u32 y accumulator */
#define g_blit_xdir      DG16(0xEBA4)   /* CS:0014 u16 +1 / -1 (0018 only) */
#define g_blit_row_fix   DG16(0xEBA6)   /* CS:0016 u16 row end correction (0018 only) */

/* 2599:0018 - 16.16 scaled colour-key blit, flags bit 0 = horizontal mirror. */
void asm_blit_scaled_key_flip(const uint8_t *src, uint16_t sx, uint16_t sy,
                              uint16_t sw, uint16_t sh, uint16_t spitch, uint16_t unused14,
                              uint8_t *dst, uint16_t dx, uint16_t dy,
                              uint16_t dw, uint16_t dh, uint16_t dpitch, uint16_t unused24,
                              uint8_t key, uint8_t flags);
/* 2599:0144 - scaled colour-key blit without mirror (unreferenced). */
void asm_blit_scaled_key(const uint8_t *src, uint16_t sx, uint16_t sy,
                         uint16_t sw, uint16_t sh, uint16_t spitch, uint16_t unused14,
                         uint8_t *dst, uint16_t dx, uint16_t dy,
                         uint16_t dw, uint16_t dh, uint16_t dpitch, uint16_t unused24,
                         uint16_t unused26, uint8_t key);
/* 2599:0234 - scale one span sw -> dw bytes with colour key (unreferenced). */
void asm_span_scaled_key(const uint8_t *src, uint8_t *dst, uint16_t sw, uint16_t dw,
                         uint8_t key);
/* 2599:028F - REP STOSD of value, then (count & 3) bytes of its low byte. */
void asm_memset32(uint8_t *dst, uint32_t value, uint16_t count);
/* 2599:02AF - REP MOVSD then REP MOVSB, forward (4-byte units on overlap). */
void asm_memcpy32(uint8_t *dst, const uint8_t *src, uint16_t count);
/* 2599:02D0 / 0341 / 03C4 - rectangle copies; return 0FFFFh (-1) on success,
   0 when sy*spitch or dy*dpitch does not fit 16 bits. */
int16_t asm_copy_rect(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t h,
                      uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy, uint16_t dpitch);
int16_t asm_copy_rect_key(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t h,
                          uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                          uint16_t dpitch, uint8_t key);
int16_t asm_copy_rect_key_mirror(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w,
                                 uint16_t h, uint16_t spitch, uint8_t *dst, uint16_t dx,
                                 uint16_t dy, uint16_t dpitch, uint8_t key);
/* 2599:0440 - blend every pixel != colour of a rectangle with colour (unreferenced). */
void asm_tint_rect(uint8_t *buf, const uint8_t *table, uint16_t x, uint16_t y,
                   uint16_t w, uint16_t h, uint16_t pitch, uint8_t colour);
/* 2599:04AD - dst[i] = blend(src1[i], src2[i]). */
void asm_blend_span(const uint8_t *src1, const uint8_t *src2, uint8_t *dst,
                    const uint8_t *table, uint16_t count);
/* 2599:0500 - 2x upscale of the 100-byte-pitch 3D view into A000 (pc_a000). */
void asm_upscale_view2x(const uint8_t *view, const uint8_t *table);

#endif
