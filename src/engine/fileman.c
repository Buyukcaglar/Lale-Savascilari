/*
 * fileman.c - ALTDUZEY\FILEMAN.L (IST.EXE ovl5447), decompiled.
 *
 * Verified: tests/py/test_fileman.py (differential against the original in
 * tools/emu86.py).
 */
#include "engine/fileman.h"

#include <string.h>

#include "dsmi/emsheap.h"
#include "engine/fatal.h"
#include "engine/ngraphs.h"
#include "engine/vfs.h"
#include "platform/pc.h"
#include "rtl/bc_farheap.h"

/* offset part of a page-frame pointer (E000:xxxx) */
static uint16_t frame_off(const uint8_t *p)
{
    return p ? (uint16_t)(p - pc_ems_frame) : 0;
}

/* The report fm_cmp_decode_huge prints when EMSLock returns NULL (printf,
   printf, getch), before its fatal.  s_null: the "EMSLock->NULL!" line of
   the remap sites (0 at the first lock). */
static void huge_lock_report(uint16_t s_null, uint16_t s_cnt, uint16_t s_map,
                             uint32_t base, uint32_t out)
{
    if (s_null)
        fatal_printf(DGSTR(s_null));                     /* "EMSLock->NULL!" */
    fatal_printf(DGSTR(s_cnt), (unsigned long)base, (unsigned long)out,
                 (unsigned long)mm_coreleft(1));          /* "compteur:%lu, k:%lu, EMSav:%lu.." */
    fatal_printf(DGSTR(s_map), (int)g_emsh_map(0), (int)g_emsh_map(1),
                 (int)g_emsh_map(2), (int)g_emsh_map(3)); /* "1: %d, %d, %d, %d" */
    pc_getch();
}

/*
 * fm_cmp_decode_huge - CMP RLE decoder for pictures over 64 KB.
 *
 * Original: IST.EXE 5447:0000, far cdecl (u8 huge *src, u32 src_len, 4-byte
 * dst, u8 type); the dst argument is the destination far pointer (type 0)
 * or the record's first word = EMS heap handle (type 1): here dst / ems.
 * 32-bit counters, huge-pointer source.  Type 1 writes through the page
 * frame: EMSLock(ems, base, FFFFh) maps 64 KB from `base`; before every run
 * or literal the window is moved on when out + (mapped offset & 3FFFh) >
 * C000h, i.e. 48 KB into the window (a run adds at most 128 bytes, so the
 * window is never overrun).  Those remap points are kept exactly: the page
 * mapping (g_emsh_map) is visible state.  A NULL EMSLock prints a report,
 * waits for a key and is fatal (3Ch, 3Eh); a type other than 0/1 is fatal
 * 3Dh.  Returns base + out = bytes written.
 * Verified: tests/py/test_fileman.py (type 0 and 1, real pictures > 64 KB).
 */
uint32_t fm_cmp_decode_huge(const uint8_t *src, uint32_t src_len, uint8_t *dst,
                            uint16_t ems, uint8_t type)
{
    uint32_t in = 0;         /* BP-4/-2 */
    uint32_t out = 0;        /* BP-8/-6: position in the current window */
    uint32_t base = 0;       /* BP-C/-A: EMS offset of the window */
    uint16_t page_off = 0;   /* BP-14: offset of the window in its first page */
    uint8_t *win = NULL;     /* BP-12/-10 */
    uint8_t c;

    if (type == 0) {
        win = dst;
    } else if (type == 1) {
        win = EMSLock((int16_t)ems, 0, 0xFFFF);
        if (win == NULL) {
            huge_lock_report(0, 0x8762, 0x8784, base, out);
            o559A_fatal(DGSTR(0x8797), 0x1E, NULL, 0x3C);
        }
        page_off = frame_off(win) & 0x3FFF;
    } else {
        o559A_fatal(DGSTR(0x87AC), 0x25, NULL, 0x3D);
    }

    while (in < src_len) {
        c = src[in++];
        if (c > 0x80) {
            if (type == 1 && out + page_off > 0xC000) {
                base += out;
                win = EMSLock((int16_t)ems, (int32_t)base, 0xFFFF);
                if (win == NULL) {
                    huge_lock_report(0x87C1, 0x87D1, 0x87F3, base, out);
                    o559A_fatal(DGSTR(0x8806), 0x3C, NULL, 0x3E);
                }
                page_off = frame_off(win) & 0x3FFF;
                out = 0;
            }
            c = (uint8_t)(c + 0x80);
            memset(win + out, src[in], c);              /* asm_memset32 */
            out += c;
            in++;
        } else {
            if (type == 1 && out + page_off > 0xC000) {
                base += out;
                win = EMSLock((int16_t)ems, (int32_t)base, 0xFFFF);
                if (win == NULL) {
                    huge_lock_report(0x881B, 0x882B, 0x884D, base, out);
                    o559A_fatal(DGSTR(0x8860), 0x59, NULL, 0x3E);
                }
                page_off = frame_off(win) & 0x3FFF;
                out = 0;
            }
            mm_memcpy(src + in, win + out, c);
            in += c;
            out += c;
        }
    }
    return base + out;
}

/*
 * fm_cmp_decode - decode one CMP run-length chunk.
 *
 * Original: IST.EXE 5447:043B, far cdecl (src far, u16 src_len, dst far).
 * The runs are filled with asm_memset32 (2599:028F) and the literals copied
 * with mm_memcpy (09D4:0006); both are plain byte fills/copies here.
 * Both indices are 16-bit, like the original's offsets: the caller never
 * hands over more than 64 KB, so they cannot wrap in practice.
 * A control byte of 00h copies nothing and is simply skipped; 80h is a
 * literal run of 128 bytes (the test is `c > 80h` for repeats, JBE).
 */
uint16_t fm_cmp_decode(const uint8_t *src, uint16_t src_len, uint8_t *dst)
{
    uint16_t in = 0, out = 0;

    while (in < src_len) {
        uint8_t c = src[in++];
        if (c > 0x80) {
            uint8_t n = (uint8_t)(c + 0x80);        /* c - 80h */
            memset(dst + out, src[in], n);
            out = (uint16_t)(out + n);
            in++;
        } else {
            memcpy(dst + out, src + in, c);
            in = (uint16_t)(in + c);
            out = (uint16_t)(out + c);
        }
    }
    return out;
}

/* What a record's first word holds in the original: the EMS heap handle
   (type 1) or, for a conventional block, its far pointer's offset - 4 for
   farmalloc blocks (memman.h mm_conv_ptr).  fm_load_cmp hands it to
   EMSLock when called with type 2 for a record that ended up conventional. */
static int16_t rec_word0(const MmHandle *h)
{
    return h->type == 0 ? 4 : (int16_t)h->ems;
}

/*
 * fm_load_cmp - load CMP picture `name` into surface slot `slot` (E29B +
 * 8 * slot: w, h, record) with memory type `type` (0 conventional, 1 EMS).
 *
 * Original: IST.EXE 5447:052A, far cdecl (u8 type, char far *name, int slot).
 * Remembers the name's last 49 characters in g_fm_last_name; under the
 * DE26 EMS lock reads w and h into the slot, gets the data length (file
 * length - 4), allocates a w*h record if the slot has none (else converts
 * the record to `type`), reads the packed data into an mm_farmalloc buffer
 * and decodes it: w*h > FFFFh with fm_cmp_decode_huge, else fm_cmp_decode
 * into the conventional block or through EMSLock(handle, 0, size).  Errors
 * are fatal: "Resim Dosyası Bulunamadı!" (-1), read error 3Fh, no memory 40h.
 * Quirks: the decoder choice tests a size variable the original only sets
 * when it allocates the record - for an existing record it is stack
 * garbage; the port uses w*h then (pictures up to 64 KB decode the same
 * with both decoders, so only garbage picking the 16-bit decoder for a big
 * picture is not reproduced - in tools/emu86.py the original did exactly
 * that when a 323x214 picture was reloaded into its slot: the 16-bit
 * offsets wrap and the first E02h bytes get the picture's tail.  Whether
 * the game ever reloads a > 64 KB picture into a used slot: check against
 * DOSBox-X).  The second open's
 * failure jumps back to the first one's fatal (5447:0789 -> 05DA); fatal
 * does not return, so the port just calls it.  The decoders get the type
 * argument, not the record's type.
 * Verified: tests/py/test_fileman.py (unicorn differential: surface table,
 * g_fm_last_name, record, decoded bytes in conventional and EMS memory).
 */
void fm_load_cmp(uint8_t type, const char *name, int16_t slot)
{
    uint16_t ref = (uint16_t)(0xE29F + 8 * slot);       /* far MmHandle * of the slot */
    uint16_t hdr = 0, l, n;
    int16_t fd, skip;
    int32_t len;
    uint32_t size;
    uint8_t *buf;
    MmHandle *rec;

    memset(g_fm_last_name, 0, FM_LAST_NAME_LEN);
    l = (uint16_t)strlen(name);
    n = l > 0x31 ? 0x31 : l;
    skip = (int16_t)(l - 0x32 + 1);
    if (skip < 0)
        skip = 0;
    strncpy(g_fm_last_name, name + skip, n);
    mm_ems_lock(g_blit_lock, 0x96, DGSTR(0x8875));

    fd = vfs_open(name, 2);
    if (fd == -1)
        o559A_fatal(DGSTR(0x888A), 0x9E, DGSTR(0x889F), -1);   /* "Resim Dosyası Bulunamadı!" */
    hdr += 2;
    if (vfs_read(fd, DGP(0xE29B + 8 * slot), 2) == -1)
        o559A_fatal(DGSTR(0x88B9), 0xA2, NULL, 0x3F);
    hdr += 2;
    if (vfs_read(fd, DGP(0xE29D + 8 * slot), 2) == -1)
        o559A_fatal(DGSTR(0x88CE), 0xA4, NULL, 0x3F);
    len = fm_filelength(fd) - hdr;
    vfs_close(fd);

    /* (u32)(long w * long h) / 1: the LUDIV by the constant 1 is the original's */
    size = (uint32_t)((int32_t)g_surf_w(slot) * (int32_t)g_surf_h(slot)) / 1u;
    rec = mm_ref(ref);
    if (rec == NULL) {
        rec = mm_alloc(size, type);
        mm_ref_set(ref, rec);
    } else if (rec->type != type) {
        mm_change_type(rec, type);
    }

    buf = mm_farmalloc((uint32_t)len);
    if (buf == NULL)
        o559A_fatal(DGSTR(0x88E3), 0xAF, NULL, 0x40);
    fd = vfs_open(name, 2);
    if (fd == -1)
        o559A_fatal(DGSTR(0x888A), 0x9E, DGSTR(0x889F), -1);
    vfs_seek(fd, 4, 0);
    vfs_read(fd, buf, len);
    vfs_close(fd);

    rec = mm_ref(ref);
    if ((int32_t)size > 0xFFFF)
        fm_cmp_decode_huge(buf, (uint32_t)len, mm_conv_ptr(rec), (uint16_t)rec_word0(rec), type);
    else if (type == 0)
        fm_cmp_decode(buf, (uint16_t)len, mm_conv_ptr(rec));
    else
        fm_cmp_decode(buf, (uint16_t)len, EMSLock(rec_word0(rec), 0, (uint16_t)rec->size));
    bc_farfree(buf);
    mm_ems_unlock(g_blit_lock);
}

/*
 * fm_filelength - length of an open file: tell, seek to the end, tell,
 * seek back.
 * Original: IST.EXE 5447:0873, far cdecl (int fd).
 * Verified: tests/py/test_fileman.py (unicorn differential).
 */
int32_t fm_filelength(int16_t fd)
{
    int32_t pos = vfs_tell(fd), end;
    vfs_seek(fd, 0, 2);
    end = vfs_tell(fd);
    vfs_seek(fd, pos, 0);
    return end;
}

/*
 * fm_load_pal - read count RGB triples from `name` into the target palette
 * (E5EF) from entry `first`.  first + count > 256 is fatal (6Fh), a missing
 * file too (41h).
 * Original: IST.EXE 5447:08B9, far cdecl (char far *name, int first,
 * int count).  The palette offset first*3 is 16-bit, like the original's.
 * Verified: tests/py/test_fileman.py (unicorn differential).
 */
void fm_load_pal(const char *name, int16_t first, int16_t count)
{
    int16_t fd;
    if ((int16_t)(first + count) > 0x100)
        o559A_fatal(DGSTR(0x88F8), 0xDB, NULL, 0x6F);
    fd = vfs_open(name, 2);
    if (fd == -1)
        o559A_fatal(DGSTR(0x890D), 0xE0, NULL, 0x41);
    vfs_read(fd, gr_target_pal + (int16_t)(first * 3), (int32_t)(int16_t)(count * 3));
    vfs_close(fd);
}

/*
 * fm_load_file - load a whole file into memory block `block` (allocated
 * here with `type` when NULL; else it must be big enough - fatal "Yetersiz
 * Konvansiyonel DOS hafızası" - and is converted to `type`).
 * EMS blocks are filled in pieces: each EMSLock(handle, off, piece) maps up
 * to 64 KB; the piece (at most FFFFh) is cut so that it ends inside the
 * frame (piece + (offset & 3FFFh) <= FFFFh).
 * Original: IST.EXE 5447:093A, far cdecl (u8 type, char far *name,
 * MmHandle far *block), returns the block.  Missing file: fatal "Dosya
 * açma hatası!" (-1); EMSLock NULL: fatal 43h.
 * Verified: tests/py/test_fileman.py (unicorn differential, type 0 and 1).
 */
MmHandle *fm_load_file(uint8_t type, const char *name, MmHandle *block)
{
    uint32_t off = 0;          /* BP-A/-8 */
    uint32_t chunk;            /* BP-14/-12 (set to C001h at entry, never read) */
    uint16_t page_off;         /* BP-C */
    int32_t len;
    uint8_t *p;
    int16_t fd;

    mm_ems_lock(g_blit_lock, 0xEF, DGSTR(0x8922));
    fd = vfs_open(name, 2);
    if (fd == -1)
        o559A_fatal(DGSTR(0x8937), 0xF3, DGSTR(0x894C), -1);    /* "Dosya açma hatası!" */
    len = fm_filelength(fd);
    vfs_close(fd);
    if (block == NULL) {
        block = mm_alloc((uint32_t)len, type);
    } else {
        if (block->size < (uint32_t)len)
            o559A_fatal(DGSTR(0x895F), 0xFB, DGSTR(0x8974), -1);
        if (block->type != type)
            mm_change_type(block, type);
    }
    fd = vfs_open(name, 2);
    if (fd == -1)
        o559A_fatal(DGSTR(0x8998), 0x101, DGSTR(0x89AD), -1);
    do {
        if (type == 1) {
            chunk = len >= 0x10000 ? 0xFFFF : (uint32_t)len;
            p = EMSLock((int16_t)block->ems, (int32_t)off, (uint16_t)chunk);
            if (p == NULL)
                o559A_fatal(DGSTR(0x89C0), 0x108, NULL, 0x43);
            page_off = frame_off(p) & 0x3FFF;
            if (chunk + page_off > 0xFFFF)
                chunk -= page_off;
        } else {
            /* the block's own far pointer, not advanced (one pass: chunk = len) */
            chunk = (uint32_t)len;
            p = mm_conv_ptr(block);
        }
        vfs_read(fd, p, (int32_t)chunk);
        len -= (int32_t)chunk;
        off += chunk;
    } while (len > 0);
    vfs_close(fd);
    mm_ems_unlock(g_blit_lock);
    return block;
}

/*
 * fm_change_ext - farmalloc'd copy of `path` with its extension replaced by
 * `ext` (a "." is put in unless ext contains one).
 * Original: IST.EXE 5447:0B5F, far cdecl (char far *path, char far *ext).
 * The buffer is strlen(path) + 5 bytes: a longer ext overruns it (into the
 * next heap block, as in the original - the port's heap is the same
 * arena).  The extension starts at the first "." not followed by a "\"
 * (so "..\X\NAME.EXT" works); the original's offset comparisons are on one
 * segment and equal to the pointer comparisons here.  When the search runs
 * out the original calls _fstrstr(NULL, "\") (scanning the IVT) with an
 * unused result; the port skips that call.
 * Verified: tests/py/test_fileman.py (unicorn differential incl. the heap).
 */
char *fm_change_ext(const char *path, const char *ext)
{
    char *p = bc_farmalloc((uint32_t)(uint16_t)(strlen(path) + 5));
    char *dot, *q;

    strcpy(p, path);
    q = p;
    do {
        dot = strstr(q, DGSTR(0x89D5));                 /* "." */
        q = dot ? strstr(dot, DGSTR(0x89D7)) : NULL;    /* "\" */
    } while (dot != NULL && q != NULL && q > dot && p + strlen(p) >= q);
    if (dot != NULL && p + strlen(p) >= dot)
        *dot = '\0';
    q = strstr(ext, DGSTR(0x89D9));                     /* "." */
    if (q == NULL || ext + strlen(ext) < q)
        strcat(p, DGSTR(0x89DB));                       /* "." */
    strcat(p, ext);
    return p;
}
