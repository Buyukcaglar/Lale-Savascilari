/*
 * emsheap.c - DSMI EMS heap (IST.EXE seg1154).
 *
 * Far pointers of the original (list links, descriptor table, page-frame
 * pointers) are native pointers here; the page frame E000:0000 is
 * pc_ems_frame.  Everything else - block placement, handle numbering, the
 * page-map bookkeeping at 86F8 - follows the original exactly.
 */
#include "dsmi/emsheap.h"

#include <stdlib.h>
#include <string.h>

#include "dsmi/dds.h"
#include "dsmi/ems.h"
#include "platform/pc.h"
#include "rtl/bc_farheap.h"

EmsBlock *emsh_head;
EmsBlock *emsh_tail;
EmsBlock *emsh_desc;

/* The descriptor table is a native array beside a far-heap block of the
   original's size (calloc(n, 18), bc_calloc_native).  Tables replaced by
   emsNewBlockDesc: the original farfree()s them while list links still
   point into them (they keep working until the heap reuses that memory);
   the port frees the far-heap block but keeps the native array alive, i.e.
   it behaves like the not-yet-reused case.  Unreachable in IST: MEMMAN
   never has more than 110 blocks + 2 sentinels < 128 descriptors. */
static EmsBlock **old_tables;
static int        old_count;

/* Far pointer E7A6:E7A4 + off as a native pointer into the page frame.
   E7A6 is always the EMM's frame segment (pc_ems_frame); the offset wraps
   at 64 KB like a real far pointer. */
static uint8_t *frame_ptr(uint16_t off)
{
    return pc_ems_frame + (uint16_t)(g_emsh_frame_off + off);
}

/*
 * emsFindBlock - descriptor of the block with this handle, or NULL.
 *
 * Original: IST.EXE 1154:0001 (DSMI EMS heap), far cdecl.
 * Handle 0 is never found.  The walk starts at the head sentinel (handle
 * FFFFh, so handle -1 finds the head) and stops before the tail sentinel.
 * Verified: tests/py/test_ems.py (via EMSAlloc/EMSLock/...), 2026-11-20.
 */
EmsBlock *emsFindBlock(int16_t handle)
{
    EmsBlock *b = emsh_head;
    if (handle == 0)
        return NULL;
    for (; b->next != NULL; b = b->next)
        if (b->handle == handle)
            return b;
    return NULL;
}

/*
 * emsNewBlockDesc - first unused descriptor (handle 0), growing the table
 * by 128 entries when full; the new descriptor gets handle FFFFh.
 *
 * Original: IST.EXE 1154:0053 (DSMI EMS heap), far cdecl.
 * Verified: tests/py/test_ems.py (via EMSAlloc), 2026-11-20.
 */
EmsBlock *emsNewBlockDesc(void)
{
    int16_t i;
    for (i = 0; i < (int16_t)g_emsh_desc_count; i++)
        if (emsh_desc[i].handle == 0)
            break;
    if (i == (int16_t)g_emsh_desc_count) {
        EmsBlock *t = bc_calloc_native((uint16_t)(g_emsh_desc_count + 0x80), 18, sizeof *t);
        memcpy(t, emsh_desc, (size_t)g_emsh_desc_count * sizeof *t);
        g_emsh_desc_count += 0x80;
        /* farfree(old table): the block goes back to the far heap, the
           native array is kept alive (see old_tables) */
        old_tables = realloc(old_tables, (size_t)(old_count + 1) * sizeof *old_tables);
        old_tables[old_count++] = emsh_desc;
        bc_native_detach(emsh_desc);
        emsh_desc = t;
    }
    emsh_desc[i].handle = -1;
    return &emsh_desc[i];
}

/*
 * emsClearDesc - mark a descriptor unused.
 *
 * Original: IST.EXE 1154:0100 (DSMI EMS heap), far cdecl.
 * Verified: tests/py/test_ems.py (via EMSFree), 2026-11-20.
 */
void emsClearDesc(EmsBlock *d)
{
    d->handle = 0;
}

/*
 * emsHeapInit - detect the EMM, allocate between min_kb and want_kb KB of
 * EMS, set up the block list and register the EMS* functions with DDS.
 *
 * Original: IST.EXE 1154:010D (DSMI EMS heap), far pascal (min_kb, want_kb).
 * Returns 0; -1 when already initialised, no EMM driver, emsInit fails or
 * a DDS registration fails (the heap then stays initialised); -2 when less
 * than min_kb KB are free or the allocation fails.
 * Free memory is computed in 16 bits: free pages << 4.  The heap size is
 * set to want_kb << 10 (sign-extended) before the size checks.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int16_t emsHeapInit(uint16_t min_kb, uint16_t want_kb)
{
    static int atexit_done;
    uint16_t free_kb;

    if (g_emsh_ready != 0)
        return -1;
    /* intdosx(3D00h "EMMXXXX0"), IOCTL 4400h, close: pc_ems_detect() */
    if (!pc_ems_detect())
        return -1;
    if (emsInit() != 0)
        return -1;
    g_emsh_size = (uint32_t)((int32_t)(int16_t)want_kb << 10);
    free_kb = (uint16_t)(emsGetFreePages() << 4);
    if (free_kb < min_kb)
        return -2;
    if (free_kb > want_kb) {
        g_emsh_ems_handle = (uint16_t)emsAllocPages((uint16_t)((int16_t)(want_kb + 0xF) / 0x10));
        if ((int16_t)g_emsh_ems_handle < 0)
            return -2;
    } else {
        g_emsh_ems_handle = (uint16_t)emsAllocPages(free_kb >> 4);
        if ((int16_t)g_emsh_ems_handle < 0)
            return -2;
        g_emsh_size = (uint32_t)free_kb << 10;
    }
    g_emsh_desc_count = 0x80;
    emsh_desc = bc_calloc_native(0x80, 18, sizeof *emsh_desc);   /* calloc(80h, 18) */
    emsh_head = emsNewBlockDesc();
    emsh_tail = emsNewBlockDesc();
    emsh_head->handle = -1;
    emsh_head->size = 0;
    emsh_head->off = 0;
    emsh_head->next = emsh_tail;
    emsh_head->prev = NULL;
    *emsh_tail = *emsh_head;                 /* memcpy(tail, head, 18) */
    emsh_tail->next = NULL;
    emsh_tail->prev = emsh_head;
    emsh_tail->off = g_emsh_size;
    g_emsh_ready = 1;
    g_emsh_frame_seg = emsGetPageFrame();
    g_emsh_frame_off = 0;
    if (!atexit_done) {                      /* atexit(emsHeapClose); once per process */
        atexit(emsHeapClose);
        atexit_done = 1;
    }
    if (DDSRegisterFunction(DGSTR(0x870D), (DdsFn)EMSAlloc) ||
        DDSRegisterFunction(DGSTR(0x8716), (DdsFn)EMSFree) ||
        DDSRegisterFunction(DGSTR(0x871E), (DdsFn)EMSCopyTo) ||
        DDSRegisterFunction(DGSTR(0x8728), (DdsFn)EMSCopyFrom) ||
        DDSRegisterFunction(DGSTR(0x8734), (DdsFn)EMSCopy) ||
        DDSRegisterFunction(DGSTR(0x873C), (DdsFn)EMSLock) ||
        DDSRegisterFunction(DGSTR(0x8744), (DdsFn)EMSRestoreState) ||
        DDSRegisterFunction(DGSTR(0x8754), (DdsFn)EMSSaveState))
        return -1;
    return 0;
}

/*
 * emsHeapClose - release the EMS handle and unregister the EMS* functions.
 *
 * Original: IST.EXE 1154:03A5 (DSMI EMS heap), far, also run by atexit.
 * Quirk kept: "EMSAlloc" is removed twice and "EMSSaveState" never.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
void emsHeapClose(void)
{
    if (g_emsh_ready != 1)
        return;
    emsFreeHandle();
    g_emsh_ready = 0;
    bc_farfree(emsh_desc);                   /* farfree(E7B4); 86F0/86F4 dangle */
    emsh_desc = NULL;
    for (int i = 0; i < old_count; i++)
        free(old_tables[i]);
    free(old_tables);
    old_tables = NULL;
    old_count = 0;
    DDSRemoveFunction(DGSTR(0x870D));        /* EMSAlloc */
    DDSRemoveFunction(DGSTR(0x8716));        /* EMSFree */
    DDSRemoveFunction(DGSTR(0x871E));        /* EMSCopyTo */
    DDSRemoveFunction(DGSTR(0x8728));        /* EMSCopyFrom */
    DDSRemoveFunction(DGSTR(0x8734));        /* EMSCopy */
    DDSRemoveFunction(DGSTR(0x873C));        /* EMSLock */
    DDSRemoveFunction(DGSTR(0x870D));        /* EMSAlloc again (original bug) */
    DDSRemoveFunction(DGSTR(0x8744));        /* EMSRestoreState */
}

/*
 * EMSAlloc - allocate a block of `size` bytes (rounded up to 16) and return
 * its handle (1, 2, 3, ... never reused).
 *
 * Original: IST.EXE 1154:0410 (DSMI EMS heap), DDS "EMSAlloc", far pascal.
 * Best fit over the gaps between neighbouring blocks: the smallest gap
 * >= size wins, the first one on ties.  For size >= 48 KB the gap is
 * measured from the previous block's end rounded up to 16 KB and the block
 * is placed on that page boundary.  All comparisons are signed 32-bit.
 * Returns -1 when the heap is not initialised, -2 when no gap fits.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int16_t EMSAlloc(int32_t size)
{
    EmsBlock *cur = emsh_head, *best = emsh_head, *d;
    int32_t   best_gap = 0x02000000, gap;
    uint32_t  end;
    int       page_align = 0;

    if (g_emsh_ready != 1)
        return -1;
    size = (int32_t)(((uint32_t)size + 0xF) & 0xFFFFFFF0u);
    if (size >= 0xC000)
        page_align = 1;
    for (; cur->next != NULL; cur = cur->next) {
        end = cur->off + cur->size;
        if (page_align)
            end = (end + 0x3FFF) & 0xFFFFC000u;
        gap = (int32_t)(cur->next->off - end);
        if (gap >= size && gap < best_gap) {
            best_gap = gap;
            best = cur;
        }
    }
    if (best_gap == 0x02000000)
        return -2;
    d = emsNewBlockDesc();
    d->next = best->next;
    best->next = d;
    d->prev = best;
    d->next->prev = d;
    d->off = best->off + best->size;
    if (page_align)
        d->off = (d->off + 0x3FFF) & 0xFFFFC000u;
    d->size = (uint32_t)size;
    g_emsh_last_handle++;
    d->handle = (int16_t)g_emsh_last_handle;
    return (int16_t)g_emsh_last_handle;
}

/*
 * EMSFree - unlink a block and mark its descriptor unused.  Unknown handles
 * are ignored.  The EMS memory keeps its contents.
 *
 * Original: IST.EXE 1154:061B (DSMI EMS heap), DDS "EMSFree", far pascal.
 * (AX on return is garbage; no caller uses it.)
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
void EMSFree(int16_t handle)
{
    EmsBlock *b;
    if (g_emsh_ready != 1)
        return;
    b = emsFindBlock(handle);
    if (b == NULL)
        return;
    b->prev->next = b->next;
    b->next->prev = b->prev;
    emsClearDesc(b);
}

/*
 * EMSLock - map the region [off, off+len) of a block into the page frame
 * starting at physical page 0 and return a pointer to its first byte.
 *
 * Original: IST.EXE 1154:0688 (DSMI EMS heap), DDS "EMSLock", far pascal
 * (handle, long off, len).
 * len is clipped to the block end.  Maps successive logical pages into
 * physical pages 0..3 until len bytes are covered (at most 4 pages; a
 * longer len is silently cut).  Each mapped page is recorded at
 * g_emsh_map(phys); if emsMapPage fails its error code is recorded there
 * and NULL is returned.  NULL also for no heap / unknown handle / off past
 * the block end (signed compare, off == size is allowed).
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
uint8_t *EMSLock(int16_t handle, int32_t off, uint16_t len)
{
    EmsBlock *b;
    int16_t   page, phys = 0, r;
    uint16_t  inpage;
    int32_t   avail;
    uint8_t  *p;

    if (g_emsh_ready != 1)
        return NULL;
    b = emsFindBlock(handle);
    if (b == NULL)
        return NULL;
    if ((int32_t)b->size < off)
        return NULL;
    if ((int32_t)((uint32_t)len + (uint32_t)off) > (int32_t)b->size)
        len = (uint16_t)((uint16_t)b->size - (uint16_t)off);
    page = (int16_t)((int32_t)(b->off + (uint32_t)off) / 0x4000);
    inpage = (uint16_t)((uint16_t)b->off + (uint16_t)off - (uint16_t)(page << 14));
    p = frame_ptr(inpage);
    /* bytes of the region inside the first page (may be <= 0) */
    avail = (int32_t)(0x4000u - b->off - (uint32_t)off + (uint32_t)((int32_t)page << 14));
    if (emsMapPage((uint16_t)page, (uint16_t)phys) != 0)
        return NULL;
    g_emsh_map(phys) = (uint16_t)page;
    while ((int32_t)len > avail) {
        page++;
        phys++;
        if (phys > 3)
            break;
        r = emsMapPage((uint16_t)page, (uint16_t)phys);
        g_emsh_map(phys) = (uint16_t)r;
        if (r != 0)
            return NULL;
        g_emsh_map(phys) = (uint16_t)page;
        avail += 0x4000;
    }
    return p;
}

/*
 * EMSLockRegion - EMSLock starting at physical page `phys` (MEMMAN maps two
 * independent windows: pages 0-1 and 2-3).
 *
 * Original: IST.EXE 1154:07F4 (DSMI EMS heap), far pascal
 * (handle, long off, len, phys).  The pointer is frame + phys * 16 KB +
 * in-page offset (16-bit wrap).
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
uint8_t *EMSLockRegion(int16_t handle, int32_t off, uint16_t len, uint16_t phys)
{
    EmsBlock *b;
    int16_t   page, r;
    uint16_t  inpage;
    int32_t   avail;
    uint8_t  *p;

    if (g_emsh_ready != 1)
        return NULL;
    b = emsFindBlock(handle);
    if (b == NULL)
        return NULL;
    if ((int32_t)b->size < off)
        return NULL;
    if ((int32_t)((uint32_t)len + (uint32_t)off) > (int32_t)b->size)
        len = (uint16_t)((uint16_t)b->size - (uint16_t)off);
    page = (int16_t)((int32_t)(b->off + (uint32_t)off) / 0x4000);
    inpage = (uint16_t)((uint16_t)b->off + (uint16_t)off - (uint16_t)(page << 14));
    p = frame_ptr((uint16_t)(inpage + (uint16_t)(phys << 14)));
    avail = (int32_t)(0x4000u - b->off - (uint32_t)off + (uint32_t)((int32_t)page << 14));
    if (emsMapPage((uint16_t)page, phys) != 0)
        return NULL;
    g_emsh_map(phys) = (uint16_t)page;
    while ((int32_t)len > avail) {
        page++;
        phys++;
        if ((int16_t)phys > 3)
            break;
        r = emsMapPage((uint16_t)page, phys);
        g_emsh_map(phys) = (uint16_t)r;
        if (r != 0)
            return NULL;
        g_emsh_map(phys) = (uint16_t)page;
        avail += 0x4000;
    }
    return p;
}

/* LIM move descriptor side for heap offset `pos` of the library's handle. */
static void ems_side(uint32_t pos, uint8_t *type, uint16_t *handle, uint16_t *off, uint16_t *page)
{
    *type = 1;
    *handle = g_emsh_ems_handle;
    *off = (uint16_t)((int32_t)pos % 0x4000);    /* N_LMOD@ (signed) */
    *page = (uint16_t)((int32_t)pos / 0x4000);   /* N_LDIV@ (signed) */
}

/*
 * EMSCopyTo - copy len bytes from conventional memory into a block at off.
 *
 * Original: IST.EXE 1154:0963 (DSMI EMS heap), DDS "EMSCopyTo", far pascal
 * (handle, far *src, long off, long len).
 * Returns 0 (the EMM status of the move is ignored), -1 no heap, -4 unknown
 * handle.  No bounds check against the block size.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int16_t EMSCopyTo(int16_t handle, const void *src, int32_t off, int32_t len)
{
    EmsMove m = {0};
    EmsBlock *b;
    if (g_emsh_ready != 1)
        return -1;
    b = emsFindBlock(handle);
    if (b == NULL)
        return -4;
    m.length = (uint32_t)len;
    m.src_type = 0;
    m.src_handle = 0;
    m.src_ptr = (uint8_t *)src;
    ems_side(b->off + (uint32_t)off, &m.dst_type, &m.dst_handle, &m.dst_off, &m.dst_page);
    emsMoveMemory(&m);
    return 0;
}

/*
 * EMSCopyFrom - copy len bytes from a block at off into conventional memory.
 *
 * Original: IST.EXE 1154:0A0D (DSMI EMS heap), DDS "EMSCopyFrom", far
 * pascal (far *dst, handle, long off, long len).  Returns as EMSCopyTo.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int16_t EMSCopyFrom(void *dst, int16_t handle, int32_t off, int32_t len)
{
    EmsMove m = {0};
    EmsBlock *b;
    if (g_emsh_ready != 1)
        return -1;
    b = emsFindBlock(handle);
    if (b == NULL)
        return -4;
    m.length = (uint32_t)len;
    m.dst_type = 0;
    m.dst_handle = 0;
    m.dst_ptr = (uint8_t *)dst;
    ems_side(b->off + (uint32_t)off, &m.src_type, &m.src_handle, &m.src_off, &m.src_page);
    emsMoveMemory(&m);
    return 0;
}

/*
 * EMSCopy - copy len bytes between two blocks.
 *
 * Original: IST.EXE 1154:0AB7 (DSMI EMS heap), DDS "EMSCopy", far pascal
 * (dst_handle, src_handle, long dst_off, long src_off, long len).
 * Returns as EMSCopyTo (-4 if either handle is unknown).
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int16_t EMSCopy(int16_t dst_handle, int16_t src_handle, int32_t dst_off,
                int32_t src_off, int32_t len)
{
    EmsMove m = {0};
    EmsBlock *d, *s;
    if (g_emsh_ready != 1)
        return -1;
    d = emsFindBlock(dst_handle);
    if (d == NULL)
        return -4;
    s = emsFindBlock(src_handle);
    if (s == NULL)
        return -4;
    m.length = (uint32_t)len;
    ems_side(s->off + (uint32_t)src_off, &m.src_type, &m.src_handle, &m.src_off, &m.src_page);
    ems_side(d->off + (uint32_t)dst_off, &m.dst_type, &m.dst_handle, &m.dst_off, &m.dst_page);
    emsMoveMemory(&m);
    return 0;
}

/*
 * EMSSaveState - save the page map (INT 67h/47h) and the 86F8 bookkeeping.
 *
 * Original: IST.EXE 1154:0BAB (DSMI EMS heap), DDS "EMSSaveState", far.
 * Returns the EMM status sign-extended from AH (SAR AX,8).  Without a heap
 * it returns with AL = 0 and AH = runtime DGROUP high byte.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int16_t EMSSaveState(void)
{
    if (g_emsh_ready != 1)
        return (int16_t)(EMS_RT_DGROUP & 0xFF00u);
    memcpy(DGP(0xE7AC), DGP(0x86F8), 8);
    /* followed by two redundant dword copies of the same data */
    return (int16_t)((int16_t)emsSavePageMap() >> 8);
}

/*
 * EMSRestoreState - restore the page map saved by EMSSaveState.
 *
 * Original: IST.EXE 1154:0BF4 (DSMI EMS heap), DDS "EMSRestoreState", far.
 * Returns as EMSSaveState.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int16_t EMSRestoreState(void)
{
    if (g_emsh_ready != 1)
        return (int16_t)(EMS_RT_DGROUP & 0xFF00u);
    memcpy(DGP(0x86F8), DGP(0xE7AC), 8);
    return (int16_t)((int16_t)emsRestorePageMap() >> 8);
}

/*
 * EMSGetFreeBytes - unallocated EMS of the whole system in bytes
 * ((long)(int)emsGetFreePages() << 14), 0 without a heap.
 *
 * Original: IST.EXE 1154:0C2B (DSMI EMS heap), far; unreferenced (not
 * disassembled by Ghidra: bytes 1E B8 E0 16 ... CB).
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int32_t EMSGetFreeBytes(void)
{
    if (g_emsh_ready != 1)
        return 0;
    return (int32_t)((uint32_t)(int32_t)(int16_t)emsGetFreePages() << 14);
}

/*
 * EMSHeapAvail - largest free gap, measured from the previous block's end
 * rounded up to 16 KB, rounded down to 16 bytes (0 without a heap).
 *
 * Original: IST.EXE 1154:0C4D (DSMI EMS heap), far.  Signed compares.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
uint32_t EMSHeapAvail(void)
{
    EmsBlock *cur = emsh_head;
    int32_t   best = 0, gap;
    if (g_emsh_ready != 1)
        return 0;
    while (cur != NULL) {
        cur = cur->next;
        if (cur == NULL)
            break;
        gap = (int32_t)((cur->off - ((cur->prev->off + cur->prev->size + 0x3FFF) & 0xFFFFC000u))
                        & 0xFFFFFFF0u);
        if (gap >= best)
            best = gap;
    }
    return (uint32_t)best;
}

/*
 * emsGetPageMapRaw - INT 67h AX=4E00h (get page map into buf).
 *
 * Original: IST.EXE 1154:0D52 (DSMI EMS heap), far pascal (far *buf);
 * unreferenced.  On error it clears its own copy of the pointer argument
 * (no effect).  Returns AX: AH = status, AL = 0.
 * Verified: no (unreferenced; the map format is driver-specific).
 */
uint16_t emsGetPageMapRaw(uint8_t *buf)
{
    return (uint16_t)(pc_ems_page_map(0, buf, NULL, NULL) << 8);
}
