/*
 * memalloc.c - DSMI MemAlloc/MemCalloc/MemRealloc/MemFree (IST.EXE seg0E9E).
 *
 * Thin wrappers over the Borland far heap (src/rtl/bc_farheap.h), as in the
 * original: farmalloc 0000:25D6, farcalloc 28F7, farrealloc 273A, farfree
 * 24C2.  Callers that keep native structures (larger than the original's,
 * far pointers being 8 bytes) use MemAllocNative / MemCallocNative, which
 * take the original's size for the far-heap block.
 */
#include "dsmi/memalloc.h"

#include "rtl/bc_farheap.h"

/*
 * MemAlloc - *p = farmalloc(size).
 *
 * Original: IST.EXE 0E9E:0000 (DSMI), far pascal (long size, void far **p).
 * Returns 0, or 2 (and *p untouched) when the allocation fails.
 * Verified: tests/py/test_memman.py (return codes), test_heap_layout.py.
 */
int16_t MemAlloc(int32_t size, void **p)
{
    void *q = bc_farmalloc((uint32_t)size);
    if (q == NULL)
        return 2;
    *p = q;
    return 0;
}

/*
 * MemCalloc - *p = farcalloc(nunits, unitsz).
 *
 * Original: IST.EXE 0E9E:003F (DSMI), far pascal (long nunits, long unitsz,
 * void far **p).  Returns 0 or 2.
 * Verified: tests/py/test_memman.py (return codes), test_heap_layout.py.
 */
int16_t MemCalloc(int32_t nunits, int32_t unitsz, void **p)
{
    void *q = bc_farcalloc((uint32_t)nunits, (uint32_t)unitsz);
    if (q == NULL)
        return 2;
    *p = q;
    return 0;
}

/*
 * MemRealloc - *p = farrealloc(*p, size).
 *
 * Original: IST.EXE 0E9E:0082 (DSMI), far pascal (long size, void far **p).
 * Returns 0, or 2 (and *p untouched) when the reallocation fails
 * (farrealloc(p, 0) frees p and returns NULL).
 * Verified: no (never called in IST; farrealloc itself: test_farheap.py).
 */
int16_t MemRealloc(int32_t size, void **p)
{
    void *q = bc_farrealloc(*p, (uint32_t)size);
    if (q == NULL)
        return 2;
    if (*p != q)
        *p = q;
    return 0;
}

/*
 * MemFree - farfree(p).
 *
 * Original: IST.EXE 0E9E:00DA (DSMI), far pascal (void far *p).
 * Returns 0, or 3 when p is NULL.
 * Verified: tests/py/test_memman.py (return codes).
 */
int16_t MemFree(void *p)
{
    if (p == NULL)
        return 3;
    bc_farfree(p);
    return 0;
}

/* port: MemAlloc / MemCalloc of a native structure (bc_farheap.h) */
int16_t MemAllocNative(int32_t orig_size, size_t native_size, void **p)
{
    void *q = bc_farmalloc_native((uint32_t)orig_size, native_size);
    if (q == NULL)
        return 2;
    *p = q;
    return 0;
}

int16_t MemCallocNative(int32_t nunits, int32_t orig_unit, size_t native_unit, void **p)
{
    void *q = bc_farcalloc_native((uint32_t)nunits, (uint32_t)orig_unit, native_unit);
    if (q == NULL)
        return 2;
    *p = q;
    return 0;
}
