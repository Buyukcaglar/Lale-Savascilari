/*
 * memman.h - ALTDUZEY\MEMMAN.L: block allocator over conventional memory
 * (farmalloc) and the DSMI EMS heap, page-frame mapping, surface slots.
 * Original: IST.EXE seg09D4 (Borland C, far cdecl).
 * Implementation: src/engine/memman.c.
 *
 * Handles.  mm_alloc returns a pointer to a 9-byte record in the DGROUP
 * table at DA48 (110 entries):  {u16 ptr_off | ems_handle; u16 ptr_seg;
 * u32 size; u8 type}, type 0 = conventional, 1 = EMS, FFh = free slot.
 * For type 0 the first 4 bytes are a far pointer in the original.  In the
 * port they stay unused in dg[] and the native pointer is kept beside the
 * table: read it with mm_conv_ptr(h).  Code that builds a type-0 record
 * itself (ogr_init's screen record at E5D8 = A000:0000) or copies a record
 * must register the pointer with mm_set_conv_ptr().
 *
 * Mapping.  mm_map gives a pointer for (record, offset): conventional
 * blocks directly, EMS blocks through the page frame (pc_ems_frame), two
 * 32 KB windows (physical pages 0-1 and 2-3).  mm_ems_lock/unlock save and
 * restore the page mapping around such use.
 */
#ifndef LALE_ENGINE_MEMMAN_H
#define LALE_ENGINE_MEMMAN_H

#include <stdint.h>

#include "engine/dgroup.h"

#pragma pack(push, 1)
typedef struct MmHandle {
    uint16_t ems;      /* +0 EMS heap handle (type 1) / far ptr offset (type 0, unused) */
    uint16_t seg;      /* +2 far ptr segment (type 0, unused in the port) */
    uint32_t size;     /* +4 block size in bytes */
    uint8_t  type;     /* +8 0 conventional, 1 EMS, FFh free */
} MmHandle;
_Static_assert(sizeof(MmHandle) == 9, "MmHandle is 9 bytes");

/* Page-map save area of mm_ems_lock (callers own it). */
typedef struct MmEmsLock {
    uint16_t map[4];   /* copy of 86F8: logical page in physical page 0..3 */
    uint16_t busy;     /* 1 while locked */
} MmEmsLock;
_Static_assert(sizeof(MmEmsLock) == 10, "MmEmsLock is 10 bytes");

/* 16E0:DE26: the lock context shared by the loaders (FILEMAN), every
   blitter (SYSTEMS, q3d) and the GFL player */
#define g_blit_lock         ((MmEmsLock *)DGP(0xDE26))
#pragma pack(pop)

/* ---- DGROUP ------------------------------------------------------------- */
#define MM_HANDLES          110
#define g_mm_ems_status     DGS32(0x6E02)  /* i32  0 = no EMS heap, 1 = EMS heap up
                                              (emsHeapInit result, 0 -> 1) */
#define g_mm_conv_lowwater  DG32(0x6E06)   /* u32  lowest farcoreleft() seen by mm_farmalloc */
#define g_mm_ems_used       DG32(0x6E0A)   /* u32  bytes in EMS blocks */
#define g_sound_on          DG8(0x73D0)    /* u8   sound enabled (set by main) */
#define g_sys_flags         DG8(0x750A)    /* u8   bit0 MEMMAN up, bit2 in fatal,
                                              bit4 NKONTROL vectors installed */
#define g_mm_lock_line      DG16(0xD9F8)   /* u16  line of the active mm_ems_lock */
#define g_mm_lock_file      DGSTR(0xD9FA)  /* char[0x32] file of the active mm_ems_lock */
/* DA2C: 4 x 7-byte mapping slots {far u16 *len (native, below); u16 in-page
   offset; u8 physical page} written by mm_map. */
#define g_mm_slot_inpage(i) DG16(0xDA30 + 7 * (i))
#define g_mm_slot_phys(i)   DG8(0xDA32 + 7 * (i))
#define g_mm_handles        ((MmHandle *)DGP(0xDA48))   /* MM_HANDLES records */
#define g_mm_free_lowwater  DG32(0xDE30)   /* u32  EMS low-water mark (see mm_alloc) */
/* E29B: 90 surface slots {u16 w; u16 h; far MmHandle *} */
#define g_surf_w(n)         DGS16(0xE29B + 8 * (n))
#define g_surf_h(n)         DGS16(0xE29D + 8 * (n))
#define g_surf_mm_off(n)    DG16(0xE29F + 8 * (n))      /* record offset in DGROUP */
#define g_surf_mm_seg(n)    DG16(0xE2A1 + 8 * (n))      /* MM_DGROUP_SEG */

/* Records are in DGROUP, so their far pointers are real: offset + the
   runtime DGROUP segment (IST.EXE loaded at 0824h as in DOSBox-X), which
   is what the original stores.  The port only uses the offset. */
#define MM_DGROUP_SEG       0x1F04u
#define mm_surface(n)       ((MmHandle *)DGP(g_surf_mm_off(n)))

/* "MmHandle far *" variables kept in DGROUP (surface table, sample slots,
   ...) hold the record's real far pointer; ref = DGROUP offset of such a
   variable.  NULL is 0000:0000. */
static inline MmHandle *mm_ref(uint16_t ref)
{
    return (DG16(ref) | DG16(ref + 2)) ? (MmHandle *)DGP(DG16(ref)) : (MmHandle *)0;
}
static inline void mm_ref_set(uint16_t ref, const MmHandle *h)
{
    DG16(ref) = h ? (uint16_t)((const uint8_t *)h - dg) : 0;
    DG16(ref + 2) = h ? MM_DGROUP_SEG : 0;
}

/* native pointer of a type-0 record; para_off = low 4 bits of the
   original far pointer's linear address (4 for farmalloc blocks, 0 for
   A000:0000) - mm_map's window length depends on it. */
uint8_t *mm_conv_ptr(const MmHandle *h);
void     mm_set_conv_ptr(const MmHandle *h, uint8_t *p, uint8_t para_off);

/* ovl559A:0000 fatal error, implemented by the ovl559A module. */
void o559A_fatal(const char *file, int16_t line, const char *msg, int16_t code);

void      mm_memcpy(const void *src, void *dst, uint16_t n);                     /* 09D4:0006 */
void      mm_copy_recolor(const uint8_t *src, uint8_t *dst, uint16_t n,
                          uint8_t match, uint8_t color);                         /* 09D4:0022 */
void      mm_copy_transparent(const uint8_t *src, uint8_t *dst, uint16_t n,
                              uint8_t key);                                      /* 09D4:004C */
int16_t   mm_init(uint32_t ems_min, uint32_t ems_want);                          /* 09D4:0071 */
MmHandle *mm_alloc(uint32_t size, uint8_t type);                                 /* 09D4:0164 */
uint8_t   mm_free(MmHandle *h, int16_t line, const char *file);                  /* 09D4:0360 */
uint32_t  mm_coreleft(uint8_t which);                                            /* 09D4:044C */
void      mm_check_type(uint8_t type);                                           /* 09D4:0485 */
MmHandle *mm_duplicate(MmHandle *h, uint8_t type);                               /* 09D4:04AB */
uint8_t   mm_change_type(MmHandle *h, uint8_t type);                             /* 09D4:05F4 */
void      mm_copy_block(MmHandle *src, MmHandle *dst);                           /* 09D4:068A */
void      mm_copy_range(MmHandle *src, MmHandle *dst, uint32_t src_off,
                        uint32_t dst_off, uint32_t len);                         /* 09D4:07C9 */
void      mm_shutdown(void);                                                     /* 09D4:095A */
void      mm_rle_09A8(const uint8_t *src, uint8_t *dst, uint16_t n, uint8_t key); /* 09D4:09A8 */
void      mm_copy_09CC(const uint8_t *src, uint8_t *dst, uint16_t n,
                       uint8_t key, uint8_t color);                              /* 09D4:09CC */
void      mm_rle_09F6(const uint8_t *src, uint8_t *dst, uint16_t n,
                      uint8_t key, uint8_t color);                               /* 09D4:09F6 */
void     *mm_farmalloc(uint32_t size);                                           /* 09D4:0A1D */
void      mm_ems_lock(MmEmsLock *ctx, int16_t line, const char *file);           /* 09D4:0A81 */
void      mm_ems_unlock(MmEmsLock *ctx);                                         /* 09D4:0B22 */
void      mm_map(MmHandle *h, uint8_t **out, uint16_t *len, uint16_t *slot,
                 uint32_t off);                                                  /* 09D4:0BB4 */
MmHandle *mm_surface_alloc(uint8_t n, int16_t w, int16_t h, uint8_t type);       /* 09D4:0E4A */

#endif
