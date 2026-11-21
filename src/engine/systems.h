/*
 * systems.h - ALTDUZEY\SYSTEMS.L: surface-slot blitters (EMS aware), fills,
 * the line/rectangle drawer, the mode-12h planar row copier, the strip
 * callbacks of the 3D view, palette searches and the .RIX blend table.
 * Original: IST.EXE seg0AC6 (Borland C, far cdecl).
 * Implementation: src/engine/systems.c.
 *
 * Surfaces.  A "slot" n is the 8-byte entry at E29B + 8n (memman.h:
 * g_surf_w/g_surf_h/record).  The blitters map source and destination
 * through mm_map in pieces (EMS windows), call a row routine per piece and
 * loop, all under the shared MEMMAN lock context at DE26 (g_blit_lock).
 *
 * Row routines.  The original passes far code pointers (2599:02D0,
 * 09D4:004C, 0AC6:000A, ...); the port passes native function pointers of
 * the types below.  Where the original passes a routine with fewer
 * parameters than the caller supplies (cdecl tolerates it), the port uses
 * an adapter with the full parameter list: sys_rect_copy (2599:02D0
 * asm_copy_rect, no key) and sys_span_memcpy (09D4:0006 mm_memcpy).
 *
 * Pointers into slot 0 / A000.  In chained mode 13h the screen is
 * pc_a000.  sys_rowcopy_planar (mode 12h) sends every access that lands in
 * pc_a000 through vga_mem_read8/vga_mem_write8 instead, so the sequencer
 * map mask and the graphics controller read map act as on the hardware.
 */
#ifndef LALE_ENGINE_SYSTEMS_H
#define LALE_ENGINE_SYSTEMS_H

#include <stdint.h>

#include "engine/dgroup.h"
#include "engine/memman.h"

/* ---- DGROUP ------------------------------------------------------------- */
/* DE26: g_blit_lock (memman.h), the MmEmsLock shared by every blitter */
/* E52F: far MmHandle * of the .RIX blend table (EMS, 0xC001 bytes); use
   mm_ref(G_RIX_TABLE_REF) / mm_ref_set */
#define G_RIX_TABLE_REF   0xE52F
#define g_rix_e56c        DG16(0xE56C)    /* u16  cleared by sys_free_rix (with E56E) */
#define g_rix_e56e        DG16(0xE56E)    /* u16  "                                   */
#define g_rix_name        DGSTR(0xE570)   /* char[0x0C] name of the loaded .RIX
                                             (_fstrcpy without bound in sys_load_rix) */

/* ---- row routine types ----------------------------------------------------- */
/* sys_blit: asm_copy_rect family (13 argument words) */
typedef int16_t (*SysRectFn)(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w,
                             uint16_t h, uint16_t spitch, uint8_t *dst, uint16_t dx,
                             uint16_t dy, uint16_t dpitch, uint8_t key);
/* sys_blit_cb: same words, result DX:AX checked (0 = fatal) */
typedef int32_t (*SysRectFn32)(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w,
                               uint16_t h, uint16_t spitch, uint8_t *dst, uint16_t dx,
                               uint16_t dy, uint16_t dpitch, uint8_t key);
/* sys_blit_0615: byte span (mm_copy_transparent, mm_memcpy via adapter) */
typedef void (*SysSpanFn)(const uint8_t *src, uint8_t *dst, uint16_t n, uint8_t key);
/* sys_blit_0F21: strip routine (sys_strip_* below) */
typedef void (*SysStripFn)(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w,
                           uint16_t row0, uint16_t rows, uint16_t spitch, uint8_t *dst,
                           uint16_t dx, uint16_t dy, uint16_t limit, uint16_t dpitch,
                           uint8_t slope, uint8_t key);

/* adapters for routines the original passes with fewer parameters */
int16_t sys_rect_copy(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t h,
                      uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                      uint16_t dpitch, uint8_t key);                 /* = 2599:02D0 */
void    sys_span_memcpy(const uint8_t *src, uint8_t *dst, uint16_t n, uint8_t key); /* = 09D4:0006 */

/* ---- functions (original address order) ------------------------------------ */
int32_t  sys_rowcopy_planar(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w,
                            uint16_t rows, uint16_t spitch, uint8_t *dst, uint16_t dx,
                            uint16_t dy, uint16_t dpitch, uint8_t key);          /* 0AC6:000A */
void     sys_putpixel(uint16_t x, uint16_t y, uint8_t color, uint8_t *buf,
                      uint16_t pitch);                                           /* 0AC6:01AA */
void     sys_blit_cb(uint16_t sx, uint16_t sy, uint16_t w, uint16_t h, uint16_t dx,
                     uint16_t dy, uint8_t src_slot, uint8_t dst_slot, SysRectFn32 fn); /* 0AC6:01C5 */
void     sys_blit(uint16_t sx, uint16_t sy, uint16_t w, uint16_t h, uint16_t dx, uint16_t dy,
                  uint8_t src_slot, uint8_t dst_slot, SysRectFn fn);           /* 0AC6:0430 */
void     sys_blit_0615(uint8_t src_slot, uint8_t dst_slot, uint16_t total, uint8_t key,
                       SysSpanFn fn);                                            /* 0AC6:0615 */
void     sys_blit_recolor(uint16_t sx, uint16_t sy, uint16_t w, uint16_t h, uint16_t dx,
                          uint16_t dy, uint8_t src_slot, uint8_t dst_slot,
                          uint8_t match, uint8_t color);                         /* 0AC6:070B */
void     sys_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t slot,
                       uint8_t color);                                           /* 0AC6:0908 */
void     sys_fill_rect_checked(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t slot,
                               uint8_t color);                                   /* 0AC6:0B99 */
void     sys_blit_scaled(uint16_t sx, uint16_t sy, uint16_t sw, uint16_t sh, uint16_t dx,
                         uint16_t dy, uint16_t dw, uint16_t dh, uint8_t src_slot,
                         uint8_t dst_slot, uint8_t flags);                       /* 0AC6:0C5A */
void     sys_blit_0F21(uint16_t sx, uint16_t sy, uint16_t w, uint16_t h, uint16_t dx,
                       uint16_t dy, uint16_t limit, uint8_t slope, uint8_t src_slot,
                       uint8_t dst_slot, SysStripFn fn);                         /* 0AC6:0F21 */
void     sys_line_10F1(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t row0,
                       uint16_t rows, uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                       uint16_t limit, uint16_t dpitch, uint8_t slope, uint8_t key); /* 0AC6:10F1 */
void     sys_line_1185(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t row0,
                       uint16_t rows, uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                       uint16_t limit, uint16_t dpitch, uint8_t slope, uint8_t key); /* 0AC6:1185 */
void     sys_line_1261(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t row0,
                       uint16_t rows, uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                       uint16_t limit, uint16_t dpitch, uint8_t slope, uint8_t key); /* 0AC6:1261 */
void     sys_line_1307(const uint8_t *src, uint16_t sx, uint16_t sy, uint16_t w, uint16_t row0,
                       uint16_t rows, uint16_t spitch, uint8_t *dst, uint16_t dx, uint16_t dy,
                       uint16_t limit, uint16_t dpitch, uint8_t slope, uint8_t key); /* 0AC6:1307 */
void     sys_fill_slot_rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color,
                            uint8_t slot);                                       /* 0AC6:13F5 (a line) */
void     sys_fill_slot(uint16_t slot, uint16_t len, uint8_t color);             /* 0AC6:16DC */
uint16_t sys_find_glyph(const uint8_t *pal, uint16_t idx, const uint8_t *pal2);  /* 0AC6:18AF */
uint16_t sys_find_color(const uint8_t *pal, uint16_t a, uint16_t b);             /* 0AC6:195E */
void     sys_build_aa_table(uint8_t *table, int16_t n);                         /* 0AC6:1A13 */
uint8_t  sys_load_rix(const char *name);                                         /* 0AC6:1AA0 */
void     sys_free_rix(void);                                                     /* 0AC6:1B58 */

#endif
