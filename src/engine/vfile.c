/*
 * vfile.c - ovl5646: integer file handles over Borland stdio FILE pointers.
 *
 * Original: IST.EXE ovl5646 (5646:0000-052F), all far pascal.
 * Verified: tests/py/test_vfs.py (unicorn differential, original ovl5646 +
 * Borland RTL vs this file + rtl/bc_stdio.c), 2026-11-20.
 */
#include "engine/vfile.h"

#include <stdlib.h>

#include "rtl/bc_farheap.h"

BcFile **vfile_files;   /* 16E0:E862 */

/* A valid handle is in range and has a FILE. */
static int vfile_valid(int16_t h)
{
    return h >= 0 && h < g_vfile_count && vfile_files[h] != NULL;
}

/*
 * vfile_init - allocate the handle table: calloc(count, 4), count 0 -> 20.
 * Returns 0, or -1 if calloc failed.
 * Original: IST.EXE 5646:0000 o5646_init (pascal (count)).
 * Borland calloc(0, ...) returns NULL; count is unsigned there (a negative
 * count asks for more than 64 KB and fails too).
 */
int16_t vfile_init(int16_t count)
{
    if (count == 0)
        count = 20;
    g_vfile_count = count;
    /* calloc (0000:1F64) of count far pointers: a native table beside a
       far-heap block of count * 4 bytes (bc_calloc_native) */
    vfile_files = bc_calloc_native((uint16_t)count, 4, sizeof *vfile_files);
    return vfile_files == NULL ? -1 : 0;
}

/*
 * vfile_open - fopen(name, mode_table[mode & 15]) into the first free slot.
 * Returns the handle or -1 (table full or fopen failed).
 * Original: IST.EXE 5646:003A o5646_open (pascal (name, mode)).
 */
int16_t vfile_open(const char *name, int16_t mode)
{
    int16_t h;
    for (h = 0; h < g_vfile_count; h++)
        if (vfile_files[h] == NULL)
            break;
    if (h == g_vfile_count)
        return -1;
    const char *fmode = DGSTR(DG16(VFILE_MODE_TABLE + (mode & 15) * 4));
    BcFile *fp = bc_fopen(name, fmode);
    if (fp == NULL)
        return -1;
    vfile_files[h] = fp;
    return h;
}

/*
 * vfile_close - fflush + fclose, free the slot.  Returns 0 or -1 (bad
 * handle); fclose's result is ignored.
 * Original: IST.EXE 5646:00BF o5646_close (pascal (h)).
 */
int16_t vfile_close(int16_t h)
{
    if (!vfile_valid(h))
        return -1;
    bc_fflush(vfile_files[h]);
    bc_fclose(vfile_files[h]);
    vfile_files[h] = NULL;
    return 0;
}

/*
 * vfile_read - fread count bytes into a huge buffer, 0xFFF0 bytes per call
 * (the far pointer's segment advances by 0FFFh each time).
 * Returns 0 when everything was read, -1 for a bad handle, otherwise
 * 1 + the number of bytes read (the running total starts at 1).
 * Original: IST.EXE 5646:0140 o5646_read (pascal (h, buf, count)).
 * Quirk: a negative count skips the loop and freads (uint16)count bytes;
 * the result can never equal count, so it returns 1 + bytes read.
 */
int32_t vfile_read(int16_t h, void *buf, int32_t count)
{
    uint8_t *p = buf;
    int32_t total = 1;
    if (!vfile_valid(h))
        return -1;
    BcFile *fp = vfile_files[h];
    while (count > 0xFFF0) {
        uint16_t got = bc_fread(p, 1, 0xFFF0, fp);
        if (got != 0xFFF0)
            return total + got;
        count -= 0xFFF0;
        total += 0xFFF0;
        p += 0xFFF0;                            /* seg += 0FFFh */
    }
    if (count != 0) {
        uint16_t got = bc_fread(p, 1, (uint16_t)count, fp);
        if ((int32_t)got != count)
            return total + got;
    }
    return 0;
}

/*
 * vfile_write - fwrite counterpart of vfile_read, same return convention.
 * Original: IST.EXE 5646:0248 o5646_write (pascal (h, buf, count)).
 */
int32_t vfile_write(int16_t h, const void *buf, int32_t count)
{
    const uint8_t *p = buf;
    int32_t total = 1;
    if (!vfile_valid(h))
        return -1;
    BcFile *fp = vfile_files[h];
    while (count > 0xFFF0) {
        uint16_t put = bc_fwrite(p, 1, 0xFFF0, fp);
        if (put != 0xFFF0)
            return total + put;
        count -= 0xFFF0;
        total += 0xFFF0;
        p += 0xFFF0;
    }
    if (count != 0) {
        uint16_t put = bc_fwrite(p, 1, (uint16_t)count, fp);
        if ((int32_t)put != count)
            return total + put;
    }
    return 0;
}

/*
 * vfile_seek - fseek; returns its result (0 / -1), -1 for a bad handle.
 * Original: IST.EXE 5646:0350 o5646_seek (pascal (h, offset, whence)).
 */
int16_t vfile_seek(int16_t h, int32_t offset, int16_t whence)
{
    if (!vfile_valid(h))
        return -1;
    return bc_fseek(vfile_files[h], offset, whence);
}

/*
 * vfile_tell - ftell, -1 for a bad handle.
 * Original: IST.EXE 5646:03A6 o5646_tell (pascal (h)).
 */
int32_t vfile_tell(int16_t h)
{
    if (!vfile_valid(h))
        return -1;
    return bc_ftell(vfile_files[h]);
}

/*
 * vfile_flush - fflush, -1 for a bad handle.
 * Original: IST.EXE 5646:03F6 o5646_flush (pascal (h)).
 */
int16_t vfile_flush(int16_t h)
{
    if (!vfile_valid(h))
        return -1;
    return bc_fflush(vfile_files[h]);
}

/*
 * vfile_eof - end-of-file test.  If the FILE has _F_EOF the result is the
 * flag value itself (20h, not 1); otherwise ftell, fseek to the end, ftell,
 * compare (1 = at the end, 0 = not) and fseek back.  -1 for a bad handle.
 * Original: IST.EXE 5646:0443 o5646_eof (pascal (h)).
 * The probe goes through fseek, so it drops the stream's buffer and clears
 * _F_EOF/_F_IN/_F_OUT as a side effect.
 */
int16_t vfile_eof(int16_t h)
{
    if (!vfile_valid(h))
        return -1;
    BcFile *fp = vfile_files[h];
    int16_t r = (int16_t)(fp->flags & BC_F_EOF);
    if (r != 0)
        return r;
    int32_t pos = bc_ftell(fp);
    bc_fseek(fp, 0, 2);
    r = bc_ftell(fp) == pos ? 1 : 0;
    bc_fseek(fp, pos, 0);
    return r;
}

void vfile_reset(void)
{
    /* forgotten, not freed: the far heap is reset separately (bc_farheap_reset) */
    vfile_files = NULL;
}
