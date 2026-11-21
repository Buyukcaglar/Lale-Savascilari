/*
 * emsheap.h - DSMI EMS heap: sub-allocates the library's single EMS handle
 * into 16-byte-granular blocks addressed by small integer handles.
 * Original: IST.EXE seg1154 (Borland C, Pascal convention for the DDS
 * entry points; logical parameter order kept).
 * Implementation: src/dsmi/emsheap.c.
 *
 * Blocks live in a doubly linked list sorted by EMS offset, between a head
 * sentinel (offset 0, size 0) and a tail sentinel (offset = heap size).
 * EMSAlloc is best-fit; blocks of 48 KB and more start on a 16 KB page
 * boundary.  The exact offsets are reproduced: later code reads EMS memory
 * it did not write (stale data of freed blocks).
 */
#ifndef LALE_DSMI_EMSHEAP_H
#define LALE_DSMI_EMSHEAP_H

#include <stdint.h>

#include "engine/dgroup.h"

/* ---- DGROUP ------------------------------------------------------------- */
#define g_emsh_size        DG32(0x86EC)   /* u32  heap size in bytes (KB << 10) */
/* 86F0 far ptr head sentinel, 86F4 far ptr tail sentinel: native, below */
#define g_emsh_map(i)      DG16(0x86F8 + 2 * (i))  /* u16[4] logical page mapped
                                                 at physical page i (EMSLock*),
                                                 or the error EMSLock got */
#define g_emsh_ready       DG16(0x8700)   /* u16  1 = heap initialised */
#define g_emsh_last_handle DG16(0x8702)   /* u16  last block handle given out */
#define EMSH_STR_EMM       0x8704         /* "EMMXXXX0" */
#define g_emsh_frame_off   DG16(0xE7A4)   /* u16  page-frame offset (0) */
#define g_emsh_frame_seg   DG16(0xE7A6)   /* u16  page-frame segment */
#define g_emsh_desc_count  DG16(0xE7A8)   /* u16  capacity of the descriptor table */
#define g_emsh_ems_handle  DG16(0xE7AA)   /* u16  EMS handle (= g_ems_handle) */
#define g_emsh_saved_map(i) DG16(0xE7AC + 2 * (i)) /* u16[4] EMSSaveState copy */
/* E7B4 far ptr descriptor table: native, below */

/* One heap block descriptor (18 bytes in a calloc'd table in the original:
   +0 handle, +2 EMS offset, +6 size, +A next, +E prev).  handle 0 = unused
   descriptor, FFFFh = sentinel. */
typedef struct EmsBlock {
    int16_t          handle;
    uint32_t         off;
    uint32_t         size;
    struct EmsBlock *next;
    struct EmsBlock *prev;
} EmsBlock;

extern EmsBlock *emsh_head;    /* 16E0:86F0 far ptr */
extern EmsBlock *emsh_tail;    /* 16E0:86F4 far ptr */
extern EmsBlock *emsh_desc;    /* 16E0:E7B4 far ptr, g_emsh_desc_count entries */

EmsBlock *emsFindBlock(int16_t handle);                          /* 1154:0001 */
EmsBlock *emsNewBlockDesc(void);                                 /* 1154:0053 */
void      emsClearDesc(EmsBlock *d);                             /* 1154:0100 */
int16_t   emsHeapInit(uint16_t min_kb, uint16_t want_kb);        /* 1154:010D */
void      emsHeapClose(void);                                    /* 1154:03A5 */
int16_t   EMSAlloc(int32_t size);                                /* 1154:0410 */
void      EMSFree(int16_t handle);                               /* 1154:061B */
uint8_t  *EMSLock(int16_t handle, int32_t off, uint16_t len);    /* 1154:0688 */
uint8_t  *EMSLockRegion(int16_t handle, int32_t off, uint16_t len,
                        uint16_t phys);                          /* 1154:07F4 */
int16_t   EMSCopyTo(int16_t handle, const void *src, int32_t off,
                    int32_t len);                                /* 1154:0963 */
int16_t   EMSCopyFrom(void *dst, int16_t handle, int32_t off,
                      int32_t len);                              /* 1154:0A0D */
int16_t   EMSCopy(int16_t dst_handle, int16_t src_handle, int32_t dst_off,
                  int32_t src_off, int32_t len);                 /* 1154:0AB7 */
int16_t   EMSSaveState(void);                                    /* 1154:0BAB */
int16_t   EMSRestoreState(void);                                 /* 1154:0BF4 */
int32_t   EMSGetFreeBytes(void);                                 /* 1154:0C2B unreferenced */
uint32_t  EMSHeapAvail(void);                                   /* 1154:0C4D */
uint16_t  emsGetPageMapRaw(uint8_t *buf);                        /* 1154:0D52 unreferenced */

#endif
