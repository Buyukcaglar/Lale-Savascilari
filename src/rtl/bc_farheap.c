/*
 * bc_farheap.c - Borland C++ 3.x far heap (IST.EXE seg0000), transcribed.
 *
 * See bc_farheap.h for the model.  The original keeps its state in the code
 * segment (CS:2382 __first, 2384 __last, 2386 __rover, 2388 saved DS,
 * 238A/238C farrealloc size) and in DGROUP (_brklvl, _heaptop, ...); the
 * block headers live in the paragraphs themselves - here in bc_fh_arena.
 * Register-level helpers (near calls with DS = block) become static
 * functions taking the block segment.
 *
 * Verified: tests/py/test_farheap.py - random farmalloc/farfree/farrealloc/
 * farcalloc/malloc/calloc sequences against the original allocator running
 * in tools/emu86.py from the main() snapshot: returned seg:off, farcoreleft(),
 * the DGROUP/CS variables and every byte of the arena, 2026-11-20.
 */
#include "rtl/bc_farheap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtl/bc_stdio.h"

uint8_t  bc_fh_arena[BC_FH_ARENA_SIZE];
uint16_t bc_fh_first, bc_fh_last, bc_fh_rover;
uint16_t bc_fh_dos_block;

/* farrealloc's requested size (CS:238A hi, CS:238C lo), read by
   __realloc_grow. */
static uint32_t fh_realloc_size;

/* ---- memory access ------------------------------------------------------------- */

static uint8_t *fh_mem(uint16_t seg, uint16_t off)
{
    uint32_t lin = (uint32_t)seg * 16u + off;
    if (lin < BC_FH_ARENA_SEG * 16u || lin + 2 > BC_FH_ARENA_SEG * 16u + BC_FH_ARENA_SIZE) {
        /* Only a corrupted heap gets here (the original would scribble over
           whatever memory that is). */
        fprintf(stderr, "bc_farheap: access %04X:%04X outside the heap\n", seg, off);
        abort();
    }
    return bc_fh_arena + (lin - BC_FH_ARENA_SEG * 16u);
}

static uint16_t W(uint16_t seg, uint16_t off)
{
    const uint8_t *p = fh_mem(seg, off);
    return (uint16_t)(p[0] | p[1] << 8);
}

static void W_set(uint16_t seg, uint16_t off, uint16_t v)
{
    uint8_t *p = fh_mem(seg, off);
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* Header words. */
#define B_SIZE  0       /* paragraphs */
#define B_PREV  2       /* previous physical block; 0 = free */
#define B_PFREE 4       /* free list: previous */
#define B_NFREE 6       /* free list: next */
#define B_SAVE  8       /* free block: previous physical block */

static uint32_t far_lin(uint16_t seg, uint16_t off) { return (uint32_t)seg * 16u + off; }

/* NULL / seg:0004 -> native */
static void *user_ptr(uint16_t seg) { return seg ? fh_mem(seg, 4) : NULL; }

static int in_arena(const void *p);
void *bc_native_block(const void *p);

uint32_t bc_far_addr(const void *p)
{
    uint32_t d;
    if (p == NULL)
        return 0;
    if (!in_arena(p) && bc_native_block(p))
        p = bc_native_block(p);             /* port: native object -> its block */
    d = (uint32_t)((const uint8_t *)p - bc_fh_arena) + BC_FH_ARENA_SEG * 16u;
    return (d >> 4) << 16 | (d & 15);
}

void *bc_far_ptr(uint32_t segoff)
{
    if (segoff == 0)
        return NULL;
    return fh_mem((uint16_t)(segoff >> 16), (uint16_t)segoff);
}

/* FP_SEG of a user pointer.  The original only looks at the segment; every
   pointer the game frees is a seg:0004 it got from this heap. */
static uint16_t ptr_seg(const void *p)
{
    return (uint16_t)(bc_far_addr(p) >> 16);
}

static int in_arena(const void *p)
{
    return (const uint8_t *)p >= bc_fh_arena && (const uint8_t *)p < bc_fh_arena + BC_FH_ARENA_SIZE;
}

/* ---- port: native objects (bc_farheap.h) ---------------------------------------- */

typedef struct { void *native; void *block; } NativeObj;
static NativeObj *native_tab;
static size_t native_n, native_cap;

static void *native_add(void *block, size_t native_size)
{
    void *obj;
    if (block == NULL)
        return NULL;
    obj = calloc(1, native_size ? native_size : 1);
    if (obj != NULL && native_n == native_cap) {
        native_cap = native_cap ? native_cap * 2 : 64;
        native_tab = realloc(native_tab, native_cap * sizeof *native_tab);
    }
    if (obj == NULL || native_tab == NULL) {
        fprintf(stderr, "bc_farheap: host out of memory\n");
        abort();
    }
    native_tab[native_n].native = obj;
    native_tab[native_n].block = block;
    native_n++;
    return obj;
}

static NativeObj *native_find(const void *p)
{
    for (size_t i = native_n; i-- > 0;)
        if (native_tab[i].native == p)
            return &native_tab[i];
    return NULL;
}

static void bc_farfree_block(void *block);

/* free a native object and its block; 0 if p is none */
static int native_free(void *p)
{
    NativeObj *o = native_find(p);
    void *block;
    if (o == NULL)
        return 0;
    block = o->block;
    free(o->native);
    *o = native_tab[--native_n];
    bc_farfree_block(block);
    return 1;
}

void bc_native_detach(void *p)
{
    NativeObj *o = native_find(p);
    void *block;
    if (o == NULL)
        return;
    block = o->block;
    *o = native_tab[--native_n];
    bc_farfree_block(block);
}

void *bc_native_block(const void *p)
{
    NativeObj *o = native_find(p);
    return o ? o->block : NULL;
}

/* ---- DOS ------------------------------------------------------------------------- */

/*
 * dos_setblock - INT 21h AH=4Ah on the program block (ES = PSP), as DOSBox
 * (-X) does it (DOS_ResizeMemory) for this memory layout: the program block
 * is followed by one free block reaching BC_FH_DOS_TOP.  Resizing to less
 * than that writes the free block's MCB right after the program block:
 * 'Z', owner 0000h, size (5 bytes; the rest of the 16 stay as they were).
 * Those bytes are later inside heap blocks (stale data), so they are kept.
 * Returns -1 on success, else the largest possible size (setblock(), 0000:3377,
 * which also sets errno/_doserrno via __IOerror(8)).
 */
static int16_t dos_setblock(uint16_t psp, uint16_t paras)
{
    uint16_t total = (uint16_t)(BC_FH_DOS_TOP - psp);      /* block + free MCB + free block */
    if (paras > total) {
        bc_doserrno = 8;                                    /* insufficient memory */
        bc_errno = DGS8(BC_DOSERR_TO_SV + 8);
        return (int16_t)total;
    }
    if (paras != bc_fh_dos_block && paras < total) {
        uint16_t mcb = (uint16_t)(psp + paras);
        uint8_t *m = fh_mem(mcb, 0);
        m[0] = 'Z';
        m[1] = 0;
        m[2] = 0;
        m[3] = (uint8_t)(total - paras - 1);
        m[4] = (uint8_t)((total - paras - 1) >> 8);
    }
    bc_fh_dos_block = paras;
    return -1;
}

/* ---- brk / sbrk ----------------------------------------------------------------- */

/*
 * __brk_setblock - make the DOS block cover the new break (seg:off) in 1 KB
 * steps, not beyond _heaptop.  Returns 1 and sets _brklvl, or 0 after
 * lowering _heaptop to what DOS can give.
 * Original: IST.EXE 0000:27B4, near (far ptr).
 */
static int16_t brk_setblock(uint16_t off, uint16_t seg)
{
    uint16_t kb = (uint16_t)((uint16_t)(seg + 1 - bc_psp + 0x3F) >> 6);
    if (kb != bc_heap_kb) {
        uint16_t paras = (uint16_t)(kb << 6);
        int16_t r;
        if ((uint16_t)(paras + bc_psp) > bc_heaptop_seg)
            paras = (uint16_t)(bc_heaptop_seg - bc_psp);
        r = dos_setblock(bc_psp, paras);
        if (r != -1) {
            bc_heaptop_seg = (uint16_t)(bc_psp + r);
            bc_heaptop_off = 0;
            return 0;
        }
        bc_heap_kb = (uint16_t)(paras >> 6);
    }
    bc_brklvl_seg = seg;
    bc_brklvl_off = off;
    return 1;
}

/*
 * __brk - set the break to seg:off if it lies within [_heapbase, _heaptop].
 * Original: IST.EXE 0000:282A, near (far ptr); 0 ok, -1 fail (ignored by
 * every caller).
 */
static int16_t fh_brk(uint16_t off, uint16_t seg)
{
    uint32_t a = far_lin(seg, off);
    if (a < far_lin(bc_heapbase_seg, bc_heapbase_off))      /* N_PCMP, huge compare */
        return -1;
    if (a > far_lin(bc_heaptop_seg, bc_heaptop_off))
        return -1;
    return brk_setblock(off, seg) ? 0 : -1;
}

/*
 * __sbrk - move the break by incr bytes; returns the old break (far ptr,
 * normalised) or FFFF:FFFF.
 * Original: IST.EXE 0000:2869, near (long incr).
 */
static uint32_t fh_sbrk(int32_t incr)
{
    int32_t lin = (int32_t)(((uint32_t)bc_brklvl_seg << 4) + bc_brklvl_off) + incr;
    uint32_t old;
    uint16_t nseg, noff;
    if (lin > 0xFFFFF)
        return 0xFFFFFFFFu;
    nseg = (uint16_t)((uint32_t)lin >> 4);                 /* N_PADD normalises */
    noff = (uint16_t)(lin & 15);
    if (far_lin(nseg, noff) < far_lin(bc_heapbase_seg, bc_heapbase_off))
        return 0xFFFFFFFFu;
    if (far_lin(nseg, noff) > far_lin(bc_heaptop_seg, bc_heaptop_off))
        return 0xFFFFFFFFu;
    old = (uint32_t)bc_brklvl_seg << 16 | bc_brklvl_off;
    if (!brk_setblock(noff, nseg))
        return 0xFFFFFFFFu;
    return old;
}

/* ---- free list -------------------------------------------------------------------- */

/*
 * __pull_free_block - unlink blk from the free list; the rover moves to
 * its predecessor (0 when the list becomes empty).
 * Original: IST.EXE 0000:2462, near (DS = block).
 */
static void pull_free_block(uint16_t blk)
{
    if (blk == W(blk, B_NFREE)) {
        bc_fh_rover = 0;
    } else {
        uint16_t next = W(blk, B_NFREE), prev = W(blk, B_PFREE);
        W_set(prev, B_NFREE, next);
        W_set(next, B_PFREE, prev);
        bc_fh_rover = prev;
    }
}

/*
 * __insert_free_block - link blk into the free list right after the rover
 * (or make it the only entry and the rover).
 * Original: IST.EXE 0000:248B, near (DS = block).
 */
static void insert_free_block(uint16_t blk)
{
    if (bc_fh_rover == 0) {
        bc_fh_rover = blk;
        W_set(blk, B_PFREE, blk);
        W_set(blk, B_NFREE, blk);
    } else {
        uint16_t r = bc_fh_rover, next = W(r, B_NFREE);
        W_set(r, B_NFREE, blk);
        W_set(blk, B_PFREE, r);
        W_set(next, B_PFREE, blk);
        W_set(blk, B_NFREE, next);
    }
}

/*
 * __free_last - free the block at the break: lower the break to it, or to
 * the previous block if that one is free (taken off the free list); with
 * the first block the heap becomes empty (__first = __last = __rover = 0).
 * Original: IST.EXE 0000:238E, near (DX = block).
 */
static void free_last(uint16_t blk)
{
    if (blk != bc_fh_first) {
        uint16_t prev = W(blk, B_PREV);
        if (W(prev, B_PREV) != 0) {                 /* previous block in use */
            bc_fh_last = prev;
            fh_brk(0, blk);
            return;
        }
        if (prev != bc_fh_first) {                  /* previous free: release it too */
            bc_fh_last = W(prev, B_SAVE);
            pull_free_block(prev);
            fh_brk(0, prev);
            return;
        }
        blk = bc_fh_first;                          /* only a free first block before it */
    }
    bc_fh_first = 0;
    bc_fh_last = 0;
    bc_fh_rover = 0;
    fh_brk(0, blk);
}

/*
 * __free_join - mark blk free, merge it with a free previous block (else
 * put it on the free list) and then with a free next block.
 * Original: IST.EXE 0000:23F1, near (DX = block).
 */
static void free_join(uint16_t blk)
{
    uint16_t prev = W(blk, B_PREV), b, next;
    W_set(blk, B_PREV, 0);
    W_set(blk, B_SAVE, prev);
    if (blk != bc_fh_first && W(prev, B_PREV) == 0) {
        /* previous block is free: grow it over blk */
        uint16_t size = W(blk, B_SIZE), after;
        W_set(prev, B_SIZE, (uint16_t)(W(prev, B_SIZE) + size));
        after = (uint16_t)(blk + size);
        if (W(after, B_PREV) == 0)
            W_set(after, B_SAVE, prev);
        else
            W_set(after, B_PREV, prev);
        b = prev;
    } else {
        insert_free_block(blk);
        b = blk;
    }
    next = (uint16_t)(b + W(b, B_SIZE));
    if (W(next, B_PREV) != 0)
        return;
    /* next block is free too: absorb it (2444) and unlink it (falls into
       __pull_free_block with DS = next) */
    W_set(b, B_SIZE, (uint16_t)(W(b, B_SIZE) + W(next, B_SIZE)));
    W_set((uint16_t)(next + W(next, B_SIZE)), B_PREV, b);
    pull_free_block(next);
}

/* ---- allocation ------------------------------------------------------------------- */

/*
 * __create_heap - first allocation: align the break to a paragraph, take
 * paras paragraphs, block = __first = __last.
 * Original: IST.EXE 0000:24EB, near (AX = paras).
 */
static uint16_t create_heap(uint16_t paras)
{
    uint32_t r = fh_sbrk(0);
    uint16_t seg;
    if (r & 15)
        fh_sbrk((int32_t)(16 - (r & 15)));
    r = fh_sbrk((int32_t)((uint32_t)paras << 4));
    if ((uint16_t)r == 0xFFFF)                      /* only AX is checked */
        return 0;
    seg = (uint16_t)(r >> 16);
    bc_fh_first = seg;
    bc_fh_last = seg;
    W_set(seg, B_SIZE, paras);
    W_set(seg, B_PREV, seg);                        /* the first block points to itself */
    return seg;
}

/*
 * __extend_heap - new block at the break (aligned up to a paragraph,
 * which costs one more sbrk).
 * Original: IST.EXE 0000:254F, near (AX = paras).
 */
static uint16_t extend_heap(uint16_t paras)
{
    uint32_t r = fh_sbrk((int32_t)((uint32_t)paras << 4));
    uint16_t seg, prev;
    if ((uint16_t)r == 0xFFFF)
        return 0;
    seg = (uint16_t)(r >> 16);
    if (r & 15) {
        if ((uint16_t)fh_sbrk((int32_t)(16 - (r & 15))) == 0xFFFF)
            return 0;
        seg++;
    }
    prev = bc_fh_last;
    bc_fh_last = seg;
    W_set(seg, B_SIZE, paras);
    W_set(seg, B_PREV, prev);
    return seg;
}

/*
 * __allocate_split - take paras paragraphs from the END of free block blk
 * (blk stays on the free list, smaller).
 * Original: IST.EXE 0000:25A9, near (DX = block, AX = paras).
 */
static uint16_t allocate_split(uint16_t blk, uint16_t paras)
{
    uint16_t nb;
    W_set(blk, B_SIZE, (uint16_t)(W(blk, B_SIZE) - paras));
    nb = (uint16_t)(blk + W(blk, B_SIZE));
    W_set(nb, B_SIZE, paras);
    W_set(nb, B_PREV, blk);
    W_set((uint16_t)(nb + paras), B_PREV, nb);
    return nb;
}

/* size in bytes -> paragraphs incl. the 4-byte header; 0 = too big
   ((n + 19) must stay below 1 MB). */
static uint16_t size_paras(uint32_t n)
{
    uint32_t t = n + 0x13;
    if (t < n || (t >> 16) & 0xFFF0)
        return 0;
    return (uint16_t)(t >> 4);
}

/*
 * farmalloc - first fit from the rover around the circular free list; an
 * exact fit is unlinked, a larger block split from its end; no fit grows
 * the heap at the break.  The block is NOT cleared.
 * Original: IST.EXE 0000:25D6, far cdecl (unsigned long nbytes).
 * Verified: tests/py/test_farheap.py.
 */
void *bc_farmalloc(uint32_t nbytes)
{
    uint16_t paras, seg;
    if (nbytes == 0)
        return NULL;
    paras = size_paras(nbytes);
    if (paras == 0)
        return NULL;
    if (bc_fh_first == 0)
        return user_ptr(create_heap(paras));
    if (bc_fh_rover != 0) {
        uint16_t start = bc_fh_rover;
        seg = start;
        do {
            uint16_t sz = W(seg, B_SIZE);
            if (sz >= paras) {
                if (sz > paras)
                    return user_ptr(allocate_split(seg, paras));
                pull_free_block(seg);
                W_set(seg, B_PREV, W(seg, B_SAVE));     /* in use again */
                return user_ptr(seg);
            }
            seg = W(seg, B_NFREE);
        } while (seg != start);
    }
    return user_ptr(extend_heap(paras));
}

/*
 * farfree - free a block: at the break it goes back to DOS, else it is
 * merged with free neighbours.  NULL (segment 0) is ignored; the offset is
 * not looked at.
 * Original: IST.EXE 0000:24C2, far cdecl (void far *).  Also free().
 * Verified: tests/py/test_farheap.py.
 */
void bc_farfree(void *block)
{
    if (block == NULL)
        return;
    if (!in_arena(block)) {
        /* port: a native object from bc_farmalloc_native & co. */
        if (!native_free(block)) {
            fprintf(stderr, "bc_farfree: %p is not a far-heap block\n", block);
            abort();
        }
        return;
    }
    bc_farfree_block(block);
}

static void bc_farfree_block(void *block)
{
    uint16_t seg = ptr_seg(block);
    if (seg == bc_fh_last)
        free_last(seg);
    else
        free_join(seg);
}

/*
 * __realloc_grow - farmalloc a new block, copy the old block's whole
 * capacity (12 bytes, then whole paragraphs, 64 KB at a time), free the
 * old one.
 * Original: IST.EXE 0000:2653, near (BX = block).
 */
static void *realloc_grow(uint16_t seg)
{
    uint8_t *np = bc_farmalloc(fh_realloc_size);
    uint16_t nseg;
    if (np == NULL)
        return NULL;
    nseg = ptr_seg(np);
    /* (size - 1) paragraphs + 12 bytes from seg:0004 = size * 16 - 4 bytes;
       old and new block never overlap (the old one is still allocated) */
    memcpy(fh_mem(nseg, 4), fh_mem(seg, 4), (uint32_t)W(seg, B_SIZE) * 16u - 4u);
    bc_farfree(user_ptr(seg));
    return np;
}

/*
 * __realloc_shrink - shrink in place: at the break the break moves down,
 * else the tail becomes a block of its own that is freed (and merged).
 * Original: IST.EXE 0000:26CF, near (BX = block, AX = new, CX = old paras).
 */
static void *realloc_shrink(uint16_t seg, uint16_t paras, uint16_t old)
{
    uint16_t tail, after;
    if (seg == bc_fh_last) {
        W_set(seg, B_SIZE, paras);
        fh_brk(0, (uint16_t)(seg + paras));
        return user_ptr(seg);
    }
    tail = (uint16_t)(seg + paras);
    W_set(tail, B_SIZE, (uint16_t)(old - paras));
    W_set(tail, B_PREV, seg);
    W_set(seg, B_SIZE, paras);
    after = (uint16_t)(seg + old);
    if (W(after, B_PREV) != 0)
        W_set(after, B_PREV, tail);
    else
        W_set(after, B_SAVE, tail);
    bc_farfree(user_ptr(tail));
    return user_ptr(seg);
}

/*
 * farrealloc - NULL block: farmalloc; size 0: farfree and NULL; same
 * paragraph count: unchanged; else shrink in place or move.
 * Original: IST.EXE 0000:273A, far cdecl (void far *, unsigned long).
 * Verified: tests/py/test_farheap.py.
 */
void *bc_farrealloc(void *block, uint32_t nbytes)
{
    uint16_t seg = block ? ptr_seg(block) : 0, paras, old;
    fh_realloc_size = nbytes;
    if (seg == 0)
        return bc_farmalloc(nbytes);
    if (nbytes == 0) {
        bc_farfree(block);
        return NULL;
    }
    paras = size_paras(nbytes);
    if (paras == 0)
        return NULL;
    old = W(seg, B_SIZE);
    if (old < paras)
        return realloc_grow(seg);
    if (old > paras)
        return realloc_shrink(seg, paras, old);
    return user_ptr(seg);
}

/*
 * farcalloc - farmalloc(n * size) (32-bit product, wraps) and clear it
 * (setmem in 0FA00h-byte pieces).
 * Original: IST.EXE 0000:28F7, far cdecl (unsigned long, unsigned long).
 * Verified: tests/py/test_farheap.py.
 */
void *bc_farcalloc(uint32_t nunits, uint32_t unitsz)
{
    uint32_t total = nunits * unitsz;
    uint8_t *p = bc_farmalloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

/*
 * farcoreleft - bytes between the break and _heaptop, minus 16 when more
 * than 16, rounded down to a paragraph.  Free-list blocks do not count.
 * Original: IST.EXE 0000:2987, far cdecl.
 * Verified: tests/py/test_farheap.py.
 */
uint32_t bc_farcoreleft(void)
{
    uint32_t left = far_lin(bc_heaptop_seg, bc_heaptop_off) - far_lin(bc_brklvl_seg, bc_brklvl_off);
    if (left > 0x10)
        left -= 0x10;
    return left & 0xFFFFFFF0u;
}

/* malloc(size_t) = farmalloc(size).  Original: IST.EXE 0000:25CC. */
void *bc_malloc(uint16_t size) { return bc_farmalloc(size); }

/* free() is farfree() (same code, 0000:24C2). */
void bc_free(void *block) { bc_farfree(block); }

/* realloc(void *, size_t) = farrealloc(p, size).  Original: IST.EXE 0000:2733. */
void *bc_realloc(void *block, uint16_t size) { return bc_farrealloc(block, size); }

/*
 * calloc - malloc(n * size) and clear; NULL when the product exceeds
 * FFFFh.
 * Original: IST.EXE 0000:1F64, far cdecl (size_t, size_t).
 * Verified: tests/py/test_farheap.py.
 */
void *bc_calloc(uint16_t nitems, uint16_t size)
{
    uint32_t total = (uint32_t)nitems * size;
    uint8_t *p;
    if (total > 0xFFFF)
        return NULL;
    p = bc_malloc((uint16_t)total);
    if (p)
        memset(p, 0, total);
    return p;
}

/* ---- port: native objects --------------------------------------------------------- */

void *bc_farmalloc_native(uint32_t orig_size, size_t native_size)
{
    return native_add(bc_farmalloc(orig_size), native_size);
}

void *bc_farcalloc_native(uint32_t nunits, uint32_t orig_unit, size_t native_unit)
{
    return native_add(bc_farcalloc(nunits, orig_unit), (size_t)nunits * native_unit);
}

void *bc_calloc_native(uint16_t nitems, uint16_t orig_unit, size_t native_unit)
{
    return native_add(bc_calloc(nitems, orig_unit), (size_t)nitems * native_unit);
}

/* ---- start-up state ------------------------------------------------------------- */

static int stdio_buffer_taken = 1;

void *bc_farheap_take_stdio_buffer(void)
{
    if (stdio_buffer_taken)
        return NULL;
    stdio_buffer_taken = 1;
    return user_ptr(0x4BB3);
}

void bc_farheap_reset(void)
{
    stdio_buffer_taken = 0;
    /* port: forget the native objects (their host memory is leaked: modules
       still holding them are reset separately and never free them) */
    native_n = 0;
    memset(bc_fh_arena, 0, BC_FH_ARENA_SIZE);
    bc_psp = 0x0814;
    bc_heapbase_off = 0;  bc_heapbase_seg = 0x2EAD;
    bc_brklvl_off = 0;    bc_brklvl_seg = 0x4BD7;
    bc_heaptop_off = 0;   bc_heaptop_seg = 0x9FFF;
    bc_heap_kb = 0x0110;
    bc_fh_dos_block = 0x4400;
    bc_fh_first = 0x2EAD;
    bc_fh_last = 0x4BD4;
    bc_fh_rover = 0;
    fh_realloc_size = 0;
    /* blocks allocated before main(): overlay buffer, stdio buffer, argv */
    W_set(0x2EAD, B_SIZE, 0x1D06);  W_set(0x2EAD, B_PREV, 0x2EAD);
    W_set(0x4BB3, B_SIZE, 0x0021);  W_set(0x4BB3, B_PREV, 0x2EAD);
    W_set(0x4BD4, B_SIZE, 0x0003);  W_set(0x4BD4, B_PREV, 0x4BB3);
    /* free MCB after the program block (stale once the heap grows over it) */
    {
        uint8_t *m = fh_mem(0x4C14, 0);
        m[0] = 'Z';
        m[3] = 0xEA;
        m[4] = 0x53;
    }
    /* MCB 9FFFh, DOSBox-X's link to upper memory (never inside a block:
       _heaptop is 9FFF:0000) */
    memcpy(fh_mem(0x9FFF, 0), "\x4D\x08\x00\x00\x2C\x00\x00\x00SC      ", 16);
    /* the paragraph below it is not zero in DOSBox-X either (seen in the
       dump; the last block below _heaptop gets it as stale data) */
    memcpy(fh_mem(0x9FFE, 0), "\x4C\xE7\x1A\xE7\x00\x01\x00\x01\x20\xCA\x00\xF0\x00\x00\x00\x00", 16);
}
