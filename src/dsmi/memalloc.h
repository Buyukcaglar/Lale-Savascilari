/*
 * memalloc.h - DSMI memory allocation entry points (registered with DDS as
 * "MemAlloc", "MemCalloc", "MemRealloc", "MemFree" and called directly by
 * DDS, the AMP loader and the game).  Original: IST.EXE seg0E9E (Borland C,
 * Pascal convention; logical parameter order kept).
 * Implementation: src/dsmi/memalloc.c, on the Borland far heap (rtl/bc_farheap.h).
 *
 * Return codes: 0 ok, 2 out of memory, 3 NULL pointer (MemFree).
 */
#ifndef LALE_DSMI_MEMALLOC_H
#define LALE_DSMI_MEMALLOC_H

#include <stddef.h>
#include <stdint.h>

int16_t MemAlloc(int32_t size, void **p);                     /* 0E9E:0000 */
int16_t MemCalloc(int32_t nunits, int32_t unitsz, void **p);  /* 0E9E:003F */
int16_t MemRealloc(int32_t size, void **p);                   /* 0E9E:0082 */
int16_t MemFree(void *p);                                     /* 0E9E:00DA */

/* port: the same for a native structure whose original is orig_size bytes
   (orig_unit per unit): the far-heap block has the original's size, the
   structure is host memory (bc_farmalloc_native).  MemFree frees both. */
int16_t MemAllocNative(int32_t orig_size, size_t native_size, void **p);
int16_t MemCallocNative(int32_t nunits, int32_t orig_unit, size_t native_unit, void **p);

#endif
