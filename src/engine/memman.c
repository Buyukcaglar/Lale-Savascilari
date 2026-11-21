/*
 * memman.c - ALTDUZEY\MEMMAN.L (IST.EXE seg09D4).
 *
 * See memman.h for the data model.  Native pointers replace the far
 * pointers of type-0 records (mm_ptr_tab) and of the mapping slots
 * (mm_slot_len); EMS blocks are reached through the DSMI EMS heap and the
 * page frame pc_ems_frame exactly as in the original.
 */
#include "engine/memman.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsmi/ems.h"
#include "dsmi/emsheap.h"
#include "platform/pc.h"
#include "rtl/bc_farheap.h"

/* ---- native side of the far pointers ------------------------------------- */

typedef struct { uint8_t *p; uint8_t para_off; } ConvPtr;

static ConvPtr mm_ptr_tab[MM_HANDLES];           /* DA48 + 9*i, bytes +0..+3 */
static struct { const MmHandle *rec; ConvPtr v; } mm_ptr_extra[16];
/* DA2C + 7*i: far u16 *len of the last mm_map into physical page i.  Slots
   0..3 are the real ones; mm_map only ever uses 0 and 2. */
static uint16_t *mm_slot_len[8];

static int handle_index(const MmHandle *h)
{
    ptrdiff_t d = (const uint8_t *)h - DGP(0xDA48);
    if (d >= 0 && d < MM_HANDLES * 9 && d % 9 == 0)
        return (int)(d / 9);
    return -1;
}

static ConvPtr *conv_slot(const MmHandle *h, int create)
{
    int i = handle_index(h);
    if (i >= 0)
        return &mm_ptr_tab[i];
    for (i = 0; i < 16; i++)
        if (mm_ptr_extra[i].rec == h)
            return &mm_ptr_extra[i].v;
    if (!create)
        return NULL;
    for (i = 0; i < 16; i++)
        if (mm_ptr_extra[i].rec == NULL) {
            mm_ptr_extra[i].rec = h;
            return &mm_ptr_extra[i].v;
        }
    return NULL;
}

uint8_t *mm_conv_ptr(const MmHandle *h)
{
    ConvPtr *c = conv_slot(h, 0);
    return c ? c->p : NULL;
}

void mm_set_conv_ptr(const MmHandle *h, uint8_t *p, uint8_t para_off)
{
    ConvPtr *c = conv_slot(h, 1);
    if (c) {
        c->p = p;
        c->para_off = para_off & 0xF;
    }
}

static uint8_t conv_para_off(const MmHandle *h)
{
    ConvPtr *c = conv_slot(h, 0);
    return c ? c->para_off : 4;
}

/* Borland farmalloc (0000:25D6) / farfree (0000:24C2) on the far heap
   (src/rtl/bc_farheap.h): blocks are seg:0004, fresh memory and reused
   blocks hold what the original's would. */
static void *far_malloc(uint32_t size)
{
    return bc_farmalloc(size);
}

static void far_free(void *p)
{
    bc_farfree(p);
}

/* Offset part of a far pointer the port holds natively (page-frame
   pointers from EMSLockRegion); 0 for NULL. */
static uint16_t frame_off(const uint8_t *p)
{
    return p ? (uint16_t)(p - pc_ems_frame) : 0;
}

/* ---- functions ----------------------------------------------------------- */

/*
 * mm_memcpy - copy n bytes from src to dst (asm_memcpy32: n/4 dwords with
 * rep movsd, then n%4 bytes, forwards).
 *
 * Original: IST.EXE 09D4:0006 (MEMMAN.L), far cdecl (src, dst, n) ->
 * 2599:02AF (dst, src, n).  Overlapping copies behave like rep movsd/movsb
 * (dword-wise forwards), not like memmove.  The 16-bit offsets of the
 * original wrap at 64 KB; native pointers do not (no caller copies across
 * a segment end).
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_memcpy(const void *src, void *dst, uint16_t n)
{
    const uint8_t *s = src;
    uint8_t *d = dst;
    uint16_t dw = n / 4, i;
    uint8_t t[4];
    for (i = 0; i < dw; i++, s += 4, d += 4) {        /* rep movsd */
        memcpy(t, s, 4);
        memcpy(d, t, 4);
    }
    for (i = 0; i < n % 4; i++)                       /* rep movsb */
        *d++ = *s++;
}

/*
 * mm_copy_recolor - for n bytes: where src == match write color, else
 * leave dst alone.
 *
 * Original: IST.EXE 09D4:0022 (MEMMAN.L), far cdecl (src, dst, n, match,
 * color).  n == 0 means 65536 (LOOP).
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_copy_recolor(const uint8_t *src, uint8_t *dst, uint16_t n, uint8_t match, uint8_t color)
{
    do {
        if (*src++ == match)
            *dst = color;
        dst++;
    } while (--n);
}

/*
 * mm_copy_transparent - copy n bytes, skipping source bytes equal to key.
 *
 * Original: IST.EXE 09D4:004C (MEMMAN.L), far cdecl (src, dst, n, key).
 * n == 0 means 65536.
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_copy_transparent(const uint8_t *src, uint8_t *dst, uint16_t n, uint8_t key)
{
    do {
        uint8_t c = *src++;
        if (c != key)
            *dst = c;
        dst++;
    } while (--n);
}

/*
 * mm_init - set up the EMS heap (when ems_min != 0), clear the handle table
 * and check that enough conventional memory is left.
 *
 * Original: IST.EXE 09D4:0071 (MEMMAN.L), far cdecl (u32 ems_min, u32
 * ems_want), both in bytes (main: 2.25 MB, 2.25 MB + 1 or 2 MB).
 * Returns 1; -1 / -2 when emsHeapInit fails (before the table is cleared);
 * -3 when farcoreleft() < 250000 (sound on) or 190000 (sound off).
 * The start is an INT 21h AX=3A01h BX=1 call (AH=3Ah is RMDIR, with DS:DX
 * whatever the caller left in DX); its result is ignored - the port skips
 * it (no directory is ever named by that pointer in practice: DGROUP:DX).
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
int16_t mm_init(uint32_t ems_min, uint32_t ems_want)
{
    uint8_t i;
    uint32_t need;
    if (ems_min != 0) {
        g_mm_ems_status = emsHeapInit((uint16_t)(ems_min >> 10), (uint16_t)(ems_want >> 10));
        if (g_mm_ems_status == -1)
            return -1;
        if (g_mm_ems_status == -2)
            return -2;
        if (g_mm_ems_status == 0)
            g_mm_ems_status = 1;
        g_mm_free_lowwater = ems_min;
    }
    for (i = 0; i < MM_HANDLES; i++)
        g_mm_handles[i].type = 0xFF;
    g_sys_flags |= 0x01;
    need = 250000u - (g_sound_on ? 0u : 60000u);
    if (mm_coreleft(0) < need)
        return -3;
    return 1;
}

/*
 * mm_alloc - allocate a block in conventional memory (type 0) or EMS
 * (type 1) and return its handle record.
 *
 * Original: IST.EXE 09D4:0164 (MEMMAN.L), far cdecl (u32 size, u8 type).
 * size 0, a full table (record index FFh, past the table) or a bad type
 * (handled as EMS) are fatal.  EMSAlloc's -1 (no EMS heap) is stored as the
 * handle without complaint; -2 (no room) is fatal.  The "y:%s,l:%lu"
 * messages are formatted but never shown (not passed to the fatal call).
 * Afterwards g_mm_free_lowwater = min(itself, EMS heap avail).
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
MmHandle *mm_alloc(uint32_t size, uint8_t type)
{
    char msg[0x2C + 0x40];
    uint8_t slot = 0xFF, i;
    MmHandle *h;

    if (size == 0)
        o559A_fatal(DGSTR(0x6E0E), 0xC4, NULL, 0x2B);
    for (i = 0; i < MM_HANDLES; i++)
        if (g_mm_handles[i].type == 0xFF) {
            slot = i;
            break;
        }
    if (slot == 0xFF)
        o559A_fatal(DGSTR(0x6E22), 0xC8, NULL, 0x2C);
    h = (MmHandle *)DGP(0xDA48 + slot * 9);
    if (type == 0) {
        h->type = type;
        h->size = size;
        uint8_t *p = mm_farmalloc(size);
        mm_set_conv_ptr(h, p, 4);       /* farmalloc blocks are seg:0004 */
        if (p == NULL) {
            sprintf(msg, DGSTR(0x6E72), type ? DGSTR(0x6E7D) : DGSTR(0x6E81), (unsigned long)size);
            o559A_fatal(DGSTR(0x6E86), 0xEC, DGSTR(0x6E9A), -1);
        }
    } else {
        if (type != 1)
            o559A_fatal(DGSTR(0x6E36), 0xCD, NULL, 0x2D);
        int16_t e = EMSAlloc((int32_t)size);
        if (e == -2) {
            sprintf(msg, DGSTR(0x6E4A), type ? DGSTR(0x6E55) : DGSTR(0x6E59), (unsigned long)size);
            o559A_fatal(DGSTR(0x6E5E), 0xD9, NULL, 0x2E);
        }
        h->ems = (uint16_t)e;
        h->type = type;
        h->size = size;
        g_mm_ems_used += size;
    }
    if (mm_coreleft(1) < g_mm_free_lowwater)
        g_mm_free_lowwater = mm_coreleft(1);
    return h;
}

/*
 * mm_free - release a block; returns the number of free table slots.
 *
 * Original: IST.EXE 09D4:0360 (MEMMAN.L), far cdecl (h, line, file).
 * A bad type is fatal (message formatted but unused), then handled as EMS.
 * The record keeps its pointer/handle and size; only type becomes FFh.
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
uint8_t mm_free(MmHandle *h, int16_t line, const char *file)
{
    char msg[0x68 + 0x80];
    uint8_t n = 0, i;
    if (h == NULL) {
        o559A_fatal(DGSTR(0x6ED9), 0x12F, NULL, 0x30);
        return 0;                       /* the original goes on reading 0000:0008 */
    }
    if (h->type == 0) {
        h->type = 0xFF;
        far_free(mm_conv_ptr(h));
    } else {
        if (h->type != 1) {
            if (snprintf(msg, sizeof msg, DGSTR(0x6EED), file, line) >= 0x68)
                strcat(msg, DGSTR(0x6F08));
            o559A_fatal(DGSTR(0x6F1D), 0x138, NULL, 0x31);
        }
        h->type = 0xFF;
        EMSFree((int16_t)h->ems);
        g_mm_ems_used -= h->size;
    }
    for (i = 0; i < MM_HANDLES; i++)
        if (g_mm_handles[i].type == 0xFF)
            n++;
    return n;
}

/*
 * mm_coreleft - free memory: 0 conventional (farcoreleft), 1 EMS heap
 * (largest gap, EMSHeapAvail), anything else the sum.
 *
 * Original: IST.EXE 09D4:044C (MEMMAN.L), far cdecl (u8 which).
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
uint32_t mm_coreleft(uint8_t which)
{
    if (which == 0)
        return bc_farcoreleft();
    if (which == 1)
        return EMSHeapAvail();
    return bc_farcoreleft() + EMSHeapAvail();
}

/*
 * mm_check_type - fatal unless type is 0 or 1.
 *
 * Original: IST.EXE 09D4:0485 (MEMMAN.L), far cdecl (u8 type).
 * Verified: tests/py/test_memman.py (via mm_duplicate), 2026-11-20.
 */
void mm_check_type(uint8_t type)
{
    if (type != 0 && type != 1)
        o559A_fatal(DGSTR(0x6F31), 0x171, NULL, 0x32);
}

/*
 * mm_duplicate - allocate a block of the given type and copy h into it.
 *
 * Original: IST.EXE 09D4:04AB (MEMMAN.L), far cdecl (h, u8 type).
 * conv -> conv copies at most 64 KB - 1 (larger is fatal, then copies the
 * low 16 bits of the size); the other combinations use EMS moves.
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
MmHandle *mm_duplicate(MmHandle *h, uint8_t type)
{
    MmHandle *n;
    uint8_t nt, ot;
    mm_check_type(type);
    n = mm_alloc(h->size, type);
    nt = n->type;
    ot = h->type;
    if (nt == 0 && ot == 0) {
        if (h->size > 0xFFFF)
            o559A_fatal(DGSTR(0x6F45), 0x1A4, NULL, 0x33);
        mm_memcpy(mm_conv_ptr(h), mm_conv_ptr(n), (uint16_t)h->size);
    } else if (nt == 1 && ot == 0) {
        EMSCopyTo((int16_t)n->ems, mm_conv_ptr(h), 0, (int32_t)h->size);
    } else if (nt == 0 && ot == 1) {
        EMSCopyFrom(mm_conv_ptr(n), (int16_t)h->ems, 0, (int32_t)h->size);
    } else if (nt == 1 && ot == 1) {
        EMSCopy((int16_t)n->ems, (int16_t)h->ems, 0, 0, (int32_t)h->size);
    } else {
        o559A_fatal(DGSTR(0x6F59), 0x1AD, NULL, 0x34);
    }
    return n;
}

/*
 * mm_change_type - move a block to the other memory type, keeping its
 * record: duplicate, free the old block, copy the new record over the old
 * one and free the new record's slot.  Returns 0.
 *
 * Original: IST.EXE 09D4:05F4 (MEMMAN.L), far cdecl (h, u8 type).
 * Same type is fatal; the NULL check comes after h->type was read.
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
uint8_t mm_change_type(MmHandle *h, uint8_t type)
{
    MmHandle *n;
    mm_check_type(type);
    if (h->type == type)
        o559A_fatal(DGSTR(0x6F6D), 0x1C4, NULL, 0x35);
    if (h == NULL)
        o559A_fatal(DGSTR(0x6F81), 0x1C5, NULL, 0x36);
    n = mm_duplicate(h, type);
    mm_free(h, 0x1CA, DGSTR(0x6F95));
    *h = *n;                                          /* N_SCOPY@ 9 bytes */
    mm_set_conv_ptr(h, mm_conv_ptr(n), conv_para_off(n));
    n->type = 0xFF;
    return 0;
}

/*
 * mm_copy_block - copy min(src size, dst size) bytes from src to dst.
 *
 * Original: IST.EXE 09D4:068A (MEMMAN.L), far cdecl (src, dst).
 * conv -> conv above 64 KB - 1 is fatal.
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_copy_block(MmHandle *src, MmHandle *dst)
{
    uint32_t len = dst->size;
    uint8_t dt, st;
    if (src->size < len)
        len = src->size;
    dt = dst->type;
    st = src->type;
    if (dt == 0 && st == 0) {
        if (len > 0xFFFF)
            o559A_fatal(DGSTR(0x6FA9), 0x1E4, NULL, 0x37);
        mm_memcpy(mm_conv_ptr(src), mm_conv_ptr(dst), (uint16_t)len);
    } else if (dt == 1 && st == 0) {
        EMSCopyTo((int16_t)dst->ems, mm_conv_ptr(src), 0, (int32_t)len);
    } else if (dt == 0 && st == 1) {
        EMSCopyFrom(mm_conv_ptr(dst), (int16_t)src->ems, 0, (int32_t)len);
    } else if (dt == 1 && st == 1) {
        EMSCopy((int16_t)dst->ems, (int16_t)src->ems, 0, 0, (int32_t)len);
    } else {
        o559A_fatal(DGSTR(0x6FBD), 0x1F1, NULL, 0x38);
    }
}

/*
 * mm_copy_range - copy len bytes from src+src_off to dst+dst_off.
 *
 * Original: IST.EXE 09D4:07C9 (MEMMAN.L), far cdecl (src, dst, u32
 * src_off, u32 dst_off, u32 len).  Ranges past either block end are fatal
 * (32-bit unsigned, sums may wrap); conv -> conv above 64 KB - 1 is fatal.
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_copy_range(MmHandle *src, MmHandle *dst, uint32_t src_off, uint32_t dst_off, uint32_t len)
{
    uint8_t dt, st;
    if (src->size < src_off + len || dst->size < dst_off + len)
        o559A_fatal(DGSTR(0x6FD1), 0x1FD, NULL, 0x39);
    dt = dst->type;
    st = src->type;
    if (dt == 0 && st == 0) {
        if (len > 0xFFFF)
            o559A_fatal(DGSTR(0x6FE5), 0x202, NULL, 0x3A);
        /* N_PADD@ huge-pointer adds: the same bytes natively */
        mm_memcpy(mm_conv_ptr(src) + src_off, mm_conv_ptr(dst) + dst_off, (uint16_t)len);
    } else if (dt == 1 && st == 0) {
        EMSCopyTo((int16_t)dst->ems, mm_conv_ptr(src) + src_off, (int32_t)dst_off, (int32_t)len);
    } else if (dt == 0 && st == 1) {
        EMSCopyFrom(mm_conv_ptr(dst) + dst_off, (int16_t)src->ems, (int32_t)src_off, (int32_t)len);
    } else if (dt == 1 && st == 1) {
        EMSCopy((int16_t)dst->ems, (int16_t)src->ems, (int32_t)dst_off, (int32_t)src_off, (int32_t)len);
    } else {
        o559A_fatal(DGSTR(0x6FF9), 0x20F, NULL, 0x3B);
    }
}

/*
 * mm_shutdown - free all conventional blocks (last slot first) and close
 * the EMS heap.  EMS blocks are not freed one by one.
 *
 * Original: IST.EXE 09D4:095A (MEMMAN.L), far, called by o559A_fatal.
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_shutdown(void)
{
    int16_t i;
    for (i = MM_HANDLES - 1; i >= 0; i--)
        if (g_mm_handles[i].type == 0)
            mm_free(&g_mm_handles[i], 0x22E, DGSTR(0x700D));
    if (g_sys_flags & 0x01)
        emsHeapClose();
}

/*
 * mm_rle_09A8 - mirrored transparent copy: src[i] (i = 0..n-1) goes to
 * dst[n-1-i] unless it equals key.
 *
 * Original: IST.EXE 09D4:09A8 (MEMMAN.L), far cdecl (src, dst, n, key);
 * unreferenced.  n == 0 means 65536 (dst offset wraps in the original).
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_rle_09A8(const uint8_t *src, uint8_t *dst, uint16_t n, uint8_t key)
{
    uint8_t *d = dst + n;
    do {
        uint8_t c = *src++;
        --d;
        if (c != key)
            *d = c;
    } while (--n);
}

/*
 * mm_copy_09CC - silhouette copy: where src != key write color, else skip.
 *
 * Original: IST.EXE 09D4:09CC (MEMMAN.L), far cdecl (src, dst, n, key,
 * color); unreferenced.  n == 0 means 65536.
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_copy_09CC(const uint8_t *src, uint8_t *dst, uint16_t n, uint8_t key, uint8_t color)
{
    do {
        if (*src++ != key)
            *dst = color;
        dst++;
    } while (--n);
}

/*
 * mm_rle_09F6 - mirrored silhouette copy (dst[n-1-i] = color where
 * src[i] != key).
 *
 * Original: IST.EXE 09D4:09F6 (MEMMAN.L), far cdecl (src, dst, n, key,
 * color); unreferenced.
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_rle_09F6(const uint8_t *src, uint8_t *dst, uint16_t n, uint8_t key, uint8_t color)
{
    uint8_t *d = dst + n;
    do {
        --d;
        if (*src++ != key)
            *d = color;
    } while (--n);
}

/*
 * mm_farmalloc - farmalloc that tracks the lowest farcoreleft() seen
 * (g_mm_conv_lowwater).  On failure it stores farcoreleft() into
 * g_mm_free_lowwater instead (sic).
 *
 * Original: IST.EXE 09D4:0A1D (MEMMAN.L), far cdecl (u32 size).
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void *mm_farmalloc(uint32_t size)
{
    void *p = far_malloc(size);
    uint32_t left = mm_coreleft(0);
    if (p == NULL)
        g_mm_free_lowwater = left;
    else if (g_mm_conv_lowwater > left)
        g_mm_conv_lowwater = left;
    return p;
}

/*
 * mm_ems_lock - remember the current page mapping (86F8) in ctx.
 *
 * Original: IST.EXE 09D4:0A81 (MEMMAN.L), far cdecl (ctx, line, file).
 * No-op without EMS.  Locking a busy ctx is fatal ("Once line: %d, file:
 * %s'dan simdi de line: %d, file: %s", formatted but not shown; the
 * original's 104-byte buffer can overflow with long names).  Records
 * line/file of this lock (file: strncpy 50 bytes, not terminated when
 * longer).
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_ems_lock(MmEmsLock *ctx, int16_t line, const char *file)
{
    char msg[0x68 + 0x100];
    if (g_mm_ems_status == 0)
        return;
    if (ctx->busy != 0) {
        if (snprintf(msg, sizeof msg, DGSTR(0x7021), g_mm_lock_line, g_mm_lock_file,
                     line, file) >= 0x68)
            strcat(msg, DGSTR(0x7059));
        o559A_fatal(DGSTR(0x7070), 0x2CB, NULL, 0x72);
    }
    mm_memcpy(DGP(0x86F8), ctx, 8);
    ctx->busy = 1;
    g_mm_lock_line = (uint16_t)line;
    strncpy(g_mm_lock_file, file, 0x32);
}

/*
 * mm_ems_unlock - map ctx's saved pages back into physical pages 0..3 and
 * restore 86F8.
 *
 * Original: IST.EXE 09D4:0B22 (MEMMAN.L), far cdecl (ctx).
 * A mapping error prints "_emsMapPages=%d, while mapping page:%d, ",
 * waits for a key and is fatal.
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_ems_unlock(MmEmsLock *ctx)
{
    int16_t i, r;
    if (g_mm_ems_status == 0)
        return;
    for (i = 0; i < 4; i++) {
        r = emsMapPage(ctx->map[i], (uint16_t)i);
        if (r != 0) {
            printf(DGSTR(0x7084), r, ctx->map[i]);
            pc_getch();
            o559A_fatal(DGSTR(0x70AD), 0x2DB, NULL, 0x71);
        }
    }
    mm_memcpy(ctx, DGP(0x86F8), 8);
    ctx->busy = 0;
}

/*
 * mm_map - pointer to byte `off` of block **hp and the number of bytes
 * usable from there (*len, also the caller's maximum on input).
 *
 * Original: IST.EXE 09D4:0BB4 (MEMMAN.L) + its tail 09D4:0D8D, far cdecl
 * (MmHandle far **hp, u8 far **out, u16 *len, u16 *slot, u32 off).
 * The original only ever reads *hp; the port takes the record itself
 * (callers: mm_ref(E29F + 8*n) or their local pointer).
 *
 * EMS: maps the region at physical page *slot (EMSLockRegion), *slot += 2.
 *   avail = bytes from the pointer to the end of the frame minus 1
 *   ((4 - slot) * 16 KB - 1 - in-page offset), or the rest of the block
 *   if that is smaller.  The slot record (DA2C + 7*slot) remembers len,
 *   the in-page offset and the slot; *len is set to avail at once.  When
 *   mapping a later slot, the length of the slot-0 window (through its
 *   remembered len pointer - a variable of an earlier call!) is cut so it
 *   does not reach into the new window.  The caller's *len is then
 *   min(*len, avail) - i.e. avail (or the cut value if it is the same
 *   variable).  A NULL mapping prints the page map, waits for a key and
 *   is fatal; slot >= 4 is fatal.
 * Conventional: pointer = block + off (huge add: the far offset is then
 *   the linear address & 15), avail = 0xFFFF - that offset, or the rest
 *   of the block if smaller; *len = min(*len, avail).
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
void mm_map(MmHandle *h, uint8_t **out, uint16_t *len, uint16_t *slot, uint32_t off)
{
    uint16_t inpage;
    uint32_t avail = 0;                 /* bad type: uninitialised in the original */

    if (h->type == 1) {
        uint32_t rest = h->size - off;
        uint16_t want = (rest >> 16) == 0 && (uint16_t)rest != 0xFFFF
                        ? (uint16_t)((uint16_t)h->size - (uint16_t)off) : 0xFFFF;
        *out = EMSLockRegion((int16_t)h->ems, (int32_t)off, want, *slot);
        if (*out == NULL) {
            printf(DGSTR(0x70C1), g_emsh_map(0), g_emsh_map(1), g_emsh_map(2), g_emsh_map(3));
            pc_getch();
            o559A_fatal(DGSTR(0x70D4), 0x2EC, NULL, 0x10);
        }
        inpage = frame_off(*out) & 0x3FFF;
        avail = (uint16_t)(((4 - *slot) << 14) - 1);
        if (rest + inpage > avail)
            avail -= inpage;            /* 32-bit: may borrow into the high word */
        else
            avail = rest;
        mm_slot_len[*slot & 7] = len;   /* far ptr at DA2C + 7*slot */
        *len = (uint16_t)avail;
        g_mm_slot_inpage(*slot) = inpage;
        g_mm_slot_phys(*slot) = (uint8_t)*slot;
        if (*slot != 0) {
            uint16_t lim = (uint16_t)((*slot - g_mm_slot_phys(0)) << 14);
            /* slot 0 never mapped: the original's pointer is 0000:0000
               (writes into the IVT); the port skips it */
            if (mm_slot_len[0] != NULL &&
                lim < (uint16_t)(*mm_slot_len[0] + g_mm_slot_inpage(0)))
                *mm_slot_len[0] = (uint16_t)((*slot << 14) - g_mm_slot_inpage(0));
        }
        if (*slot < 4)
            *slot += 2;
        else
            o559A_fatal(DGSTR(0x70E8), 0x300, NULL, 0x28);
    } else if (h->type == 0) {
        *out = mm_conv_ptr(h) + off;
        inpage = (uint16_t)((conv_para_off(h) + off) & 0xF);   /* normalised offset */
        if ((uint32_t)(0xFFFFu - inpage) > h->size - off)
            avail = (uint16_t)((uint16_t)h->size - (uint16_t)off);
        else
            avail = (uint16_t)(0xFFFFu - inpage);
    } else {
        o559A_fatal(DGSTR(0x70FC), 0x305, NULL, 0x32);
    }
    if (!((uint32_t)*len < avail))
        *len = (uint16_t)avail;
}

/*
 * mm_surface_alloc - set surface slot n to w x h and allocate its block;
 * type 2 = conventional if farcoreleft() allows, else EMS if the EMS heap
 * has room, else fatal ("Yetersiz hafıza").
 *
 * Original: IST.EXE 09D4:0E4A (MEMMAN.L), far cdecl (u8 n, int w, int h,
 * u8 type).  size = (long)w * (long)h (signed).
 * Verified: tests/py/test_memman.py (unicorn differential), 2026-11-20.
 */
MmHandle *mm_surface_alloc(uint8_t n, int16_t w, int16_t h, uint8_t type)
{
    MmHandle *r;
    int32_t size;
    g_surf_w(n) = w;
    g_surf_h(n) = h;
    size = (int32_t)g_surf_w(n) * (int32_t)g_surf_h(n);
    if (type == 2) {
        if (mm_coreleft(0) >= (uint32_t)size)
            type = 0;
        else if (mm_coreleft(1) >= (uint32_t)size)
            type = 1;
        else
            o559A_fatal(DGSTR(0x7110), 0x312, DGSTR(0x7124), -1);
    }
    r = mm_alloc((uint32_t)size, type);
    mm_ref_set((uint16_t)(0xE29F + 8 * n), r);
    return r;
}
