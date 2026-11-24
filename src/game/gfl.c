/*
 * gfl.c - the GFL/GHD video player, decompiled from IST.EXE ovl5313
 * (5313:0000-1320; sources GFLALT.H, GFLSTR.H, GFLMAIN.H, GFL.C).
 * See gfl.h for the overview and docs/FORMATS.md for the file format.
 *
 * Ports of the hardware/driver accesses: kbhit/getch -> pc_kbhit/pc_getch,
 * in 3DAh -> pc_in_3da.  The 100 Hz clock ISR that advances g_clock_ms
 * while the original streams data runs from pc_poll() in the port: the
 * streaming loop of gfl_stream_fill polls once per pass (see there).
 *
 * Verified: tests/py/test_gfl.py (unicorn differential), 2026-11-20.
 */
#include "game/gfl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/cdi.h"
#include "dsmi/emsheap.h"
#include "engine/blit386.h"
#include "engine/fileman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/vfs.h"
#include "platform/pc.h"
#include "rtl/bc_farheap.h"

#define GFL_SCREEN_REF    0xE29F        /* surface slot 0 record (memman.h g_surf_mm_off(0)) */


uint8_t *gfl_index;                     /* 16E0:D61C */

/* Frame index entry k (little-endian bytes of the GHD).
 *
 * Out of range: gfl_stream_fill reads entry nframes when the last frame is
 * due (k = frame + 1).  The original then reads 8 bytes past the
 * farmalloc'd index: the block's slack and the header of the next far-heap
 * block (normally the frame buffer: size in paragraphs 0FA1h, then the
 * previous block's segment), i.e. offset >= 0FA100h with bit 7 of flags
 * clear, so it takes the "seek forward" branch, finds no later key frame
 * and returns.  The index is a block of the port's Borland far heap
 * (src/rtl/bc_farheap.h), so the same bytes are read here. */
static GflFrame gfl_entry(uint16_t k)
{
    GflFrame f;
    const uint8_t *e = gfl_index + 8u * k;
    f.type = e[0];
    f.vsize = (uint16_t)(e[1] | e[2] << 8);
    f.offset = (uint32_t)e[3] | (uint32_t)e[4] << 8 | (uint32_t)e[5] << 16 | (uint32_t)e[6] << 24;
    f.flags = e[7];
    return f;
}

/*
 * gfl_ring_read_far - copy len bytes at stream position pos out of the EMS
 * ring buffer (size ring_size) to dst + dstoff, in two pieces when the
 * range wraps at the end of the ring.
 *
 * Original: IST.EXE 5313:0000 (GFLALT.H), far cdecl (MmHandle far *ring,
 * void far *dst, long pos, unsigned dstoff, long len, long ring_size).
 * First piece = min(ring_size - pos % ring_size, len) (signed compare).
 * EMSLock failure is fatal (GFLALT.H lines 15, 19).  The pieces are 16-bit
 * (asm_memcpy32 count, far-pointer offset arithmetic).
 * Verified: tests/py/test_gfl.py (unicorn differential), 2026-11-20.
 */
void gfl_ring_read_far(MmHandle *ring, uint8_t *dst, uint32_t pos, uint16_t dstoff,
                       uint32_t len, uint32_t ring_size)
{
    uint32_t n1;
    uint8_t *p;

    if ((int32_t)(ring_size - pos % ring_size) < (int32_t)len)
        n1 = ring_size - pos % ring_size;
    else
        n1 = len;
    if (n1 > 0) {
        p = EMSLock((int16_t)ring->ems, (int32_t)(pos % ring_size), (uint16_t)n1);
        if (p == NULL)
            o559A_fatal(DGSTR(0x68CC), 0x0F, NULL, 0x10);
        asm_memcpy32(dst + dstoff, p, (uint16_t)n1);
    }
    if (n1 < len) {
        p = EMSLock((int16_t)ring->ems, (int32_t)((pos + n1) % ring_size), (uint16_t)(len - n1));
        if (p == NULL)
            o559A_fatal(DGSTR(0x68E5), 0x13, NULL, 0x10);
        /* far pointer offset dst + dstoff + n1 (16-bit) */
        asm_memcpy32(dst + (uint16_t)(dstoff + (uint16_t)n1), p, (uint16_t)(len - n1));
    }
}

/*
 * gfl_ring_copy_to_ems - as gfl_ring_read_far, but into another MEMMAN
 * block (EMS-to-EMS through mm_copy_range) at the 32-bit offset dstoff.
 * Used for the frame staging block and the audio double buffer.
 *
 * Original: IST.EXE 5313:0145 (GFLALT.H), far cdecl (MmHandle far *ring,
 * MmHandle far *dst, long pos, long dstoff, long len, long ring_size).
 * Verified: tests/py/test_gfl.py (unicorn differential), 2026-11-20.
 */
void gfl_ring_copy_to_ems(MmHandle *ring, MmHandle *dst, uint32_t pos, uint32_t dstoff,
                          uint32_t len, uint32_t ring_size)
{
    uint32_t n1;

    if ((int32_t)(ring_size - pos % ring_size) < (int32_t)len)
        n1 = ring_size - pos % ring_size;
    else
        n1 = len;
    if (n1 > 0)
        mm_copy_range(ring, dst, pos % ring_size, dstoff, n1);
    if (n1 < len)
        mm_copy_range(ring, dst, (pos + n1) % ring_size, dstoff + n1, len - n1);
}

/*
 * gfl_alloc_sample_slot - give NSES sample slot `slot` an EMS block of
 * len * count bytes and fill its SAMPLEINFO (nses.h) as a looping EMS
 * sample (loop 0..size, mode 0).
 *
 * Original: IST.EXE 5313:0241 (GFLALT.H), far cdecl (int slot, unsigned
 * len, int count).  len > C000h is fatal (line 2Dh), a slot that already
 * has a block too (line 31h, code 25h).  The entry bytes +4..+16h are
 * cleared first; the block pointer at +0 is not.
 * Verified: tests/py/test_gfl.py (unicorn differential), 2026-11-20.
 */
void gfl_alloc_sample_slot(int16_t slot, uint16_t len, int16_t count)
{
    uint32_t size = (uint32_t)len * (uint32_t)(int32_t)count;   /* LXMUL, count sign-extended */
    MmHandle *h;

    if (len > 0xC000)
        o559A_fatal(DGSTR(0x68FE), 0x2D, NULL, 0x10);
    /* memset(&slot.sampleinfo, 0, 13h) (the port's helper also clears the
       native data pointer, which an EMS sample does not use) */
    snd_slot_clear(slot);
    if (mm_ref(SND_SLOT_MM_REF(slot)) != NULL)
        o559A_fatal(DGSTR(0x6917), 0x31, NULL, 0x25);
    h = mm_alloc(size, 1);
    mm_ref_set(SND_SLOT_MM_REF(slot), h);
    /* EMS sample: far pointer FFFF:handle, sampleID = handle (the original
       dereferences h without a NULL check; mm_alloc is fatal on failure) */
    g_snd_si_ptr_seg(slot) = 0xFFFF;
    g_snd_si_ptr_off(slot) = h->ems;
    g_snd_si_id(slot) = h->ems;
    g_snd_si_length(slot) = size;
    g_snd_si_loopstart(slot) = 0;
    g_snd_si_loopend(slot) = size;
    g_snd_si_mode(slot) = 0;
}

/*
 * gfl_decode_frame - apply one frame's video data (len bytes at src) to the
 * 320x200 buffer dst, which holds the previous frame.  Code byte b:
 *   1nnnnnnn  copy (b & 7Fh) + 1 literal bytes
 *   01ffnnnn  skip L + 1 pixels
 *   00ffnnnn  fill L + 3 pixels with the next byte
 * with L = n, 2n + 10h, 4n + 30h, 8n + 70h for ff = 00, 01, 10, 11.
 * The test after each code: continue while src < src + len and dst <
 * dst + 64000 (a run can overshoot; the shipped files end exactly).
 *
 * Original: IST.EXE 5313:0390 (GFLSTR.H area), far cdecl (u8 far *dst,
 * u8 far *src, unsigned len).  The original compares 16-bit far-pointer
 * offsets; its buffers (frame buffer at seg:0004, EMS staging window at
 * E000:0000) never wrap, so index compares are equivalent.  Do-while: one
 * code is decoded even for len == 0.
 * Port: the last code of a stream can run past pixel 64000 (up to 234
 * bytes).  The shipped frames never do, but a frame decoded from stale
 * ring data does (see gfl_stream_fill); the original then writes into the
 * 12 bytes of Borland block slack and the next far-heap block.  That
 * memory is not reproducible, so the port drops the bytes past the
 * 64000-byte buffer (gfl_clip); everything inside the buffer is identical.
 * Verified: tests/py/test_gfl.py (unicorn differential on frames of every
 * video; gflview.py on all frames), 2026-11-20.
 */
/* bytes of a run of n at pixel d that land inside the 64000-byte buffer
   (d < 64000 always holds when a code starts) */
static uint16_t gfl_clip(uint16_t d, uint8_t n)
{
    return (uint16_t)(d + n > GFL_FRAME_BYTES ? GFL_FRAME_BYTES - d : n);
}

void gfl_decode_frame(uint8_t *dst, const uint8_t *src, uint16_t len)
{
    uint16_t d = 0, s = 0;
    uint8_t b, n;

    do {
        b = src[s];
        if (b & 0x80) {
            n = (uint8_t)((b & 0x7F) + 1);
            s++;
            asm_memcpy32(dst + d, src + s, gfl_clip(d, n));
            d = (uint16_t)(d + n);
            s = (uint16_t)(s + n);
            continue;
        }
        if (b & 0x10) {
            if (b & 0x20)
                n = (uint8_t)(((b & 0x0F) << 3) + 0x70);
            else
                n = (uint8_t)(((b & 0x0F) << 1) + 0x10);
        } else if (b & 0x20) {
            n = (uint8_t)(((b & 0x0F) << 2) + 0x30);
        } else {
            n = (uint8_t)(b & 0x0F);
        }
        if (b & 0x40) {                      /* skip */
            n++;
        } else {                             /* fill */
            n = (uint8_t)(n + 3);
            s++;
            memset(dst + d, src[s], gfl_clip(d, n));
        }
        d = (uint16_t)(d + n);
        s++;
    } while (s < len && d < GFL_FRAME_BYTES);
}

/*
 * gfl_load_ghd - open the GHD, read the 310h-byte header to D620, check the
 * version, read the frame index into farmalloc'd memory, and (sound on and
 * audio present) give sample slot 30h a 2 x abytes EMS block holding the
 * first second of audio in its first half.  The header palette is copied
 * to the target palette (not set on the DAC).
 *
 * Original: IST.EXE 5313:04D0 (GFLSTR.H), far cdecl (char far *name).
 * Fatal: open (line 8), read (0Bh, 22h, 26h; only result -1 counts),
 * version (0Fh: the three signed signature bytes must sum to D9h, "GFL"),
 * index memory (13h), EMSLock (20h).  Without sound the audio is skipped
 * with a relative seek.
 * Verified: tests/py/test_gfl.py (unicorn differential), 2026-11-20.
 */
void gfl_load_ghd(const char *name)
{
    int16_t h;
    uint8_t *p;

    h = vfs_open(name, 2);
    if (h == -1)
        o559A_fatal(DGSTR(0x6930), 0x08, DGSTR(0x6949), -1);
    if (vfs_read(h, g_gfl_hdr, 0x310) == -1)
        o559A_fatal(DGSTR(0x6960), 0x0B, DGSTR(0x6979), -1);
    if ((int8_t)DG8(0xD620) + (int8_t)DG8(0xD621) + (int8_t)DG8(0xD622) != 0xD9)
        o559A_fatal(DGSTR(0x6990), 0x0F, DGSTR(0x69A9), -1);
    gfl_index = mm_farmalloc((uint16_t)(g_gfl_nframes << 3));
    if (gfl_index == NULL)
        o559A_fatal(DGSTR(0x69BE), 0x13, DGSTR(0x69D7), -1);
    if (g_sound_on != 0 && g_gfl_abytes > 0)
        gfl_alloc_sample_slot(GFL_SND_SLOT, g_gfl_abytes, 2);
    memcpy(gr_target_pal, g_gfl_palette, 0x300);
    if (g_sound_on != 0 && g_gfl_abytes > 0) {
        p = EMSLock((int16_t)mm_ref(SND_SLOT_MM_REF(GFL_SND_SLOT))->ems, 0, g_gfl_abytes);
        if (p == NULL)
            o559A_fatal(DGSTR(0x69EE), 0x20, NULL, 0x10);
        if (vfs_read(h, p, g_gfl_abytes) == -1)
            o559A_fatal(DGSTR(0x6A07), 0x22, DGSTR(0x6A20), -1);
    } else {
        vfs_seek(h, (int32_t)g_gfl_abytes, 1);
    }
    if (vfs_read(h, gfl_index, (uint16_t)(g_gfl_nframes << 3)) == -1)
        o559A_fatal(DGSTR(0x6A37), 0x26, DGSTR(0x6A50), -1);
    vfs_close(h);
}

/*
 * gfl_open_gfl - open the GFL and set up streaming: the 64 KB EMS staging
 * block, the EMS ring buffer, the 64000-byte frame buffer, and preload the
 * ring in C000h-byte reads.
 *
 * Ring size = EMSHeapAvail(), at most the file length, then for memory
 * mode (8DDA) 0 at most 2/3 and for modes 1/2 at most 1/2 of the length
 * (signed long division, signed compare); other modes no further limit.
 *
 * Original: IST.EXE 5313:06AA (GFLSTR.H), far cdecl (char far *name).
 * Quirk kept: the "not enough memory" check compares the long DX:AX =
 * 0:fps with the halves swapped, i.e. it is fatal when (fps << 16) >
 * EMSHeapAvail() (line 4Dh).  A preload read that does not return 0 is
 * fatal (line 68h), as is a failed EMSLock (66h).
 * Verified: tests/py/test_gfl.py (unicorn differential), 2026-11-20.
 */
void gfl_open_gfl(const char *name)
{
    uint32_t n;
    uint8_t *p;
    int32_t t;

    g_gfl_handle = vfs_open(name, 2);
    if (g_gfl_handle == -1)
        o559A_fatal(DGSTR(0x6A67), 0x40, DGSTR(0x6A80), -1);
    mm_ref_set(GFL_STAGE_REF, mm_alloc(0xFFFF, 1));
    g_gfl_file_len = (uint32_t)fm_filelength(g_gfl_handle);
    g_gfl_ring_size = EMSHeapAvail();
    if (((uint32_t)g_gfl_fps << 16) > g_gfl_ring_size)     /* 5313:070B, see above */
        o559A_fatal(DGSTR(0x6A97), 0x4D, DGSTR(0x6AB0), -1);
    if (g_gfl_file_len <= g_gfl_ring_size)
        g_gfl_ring_size = g_gfl_file_len;
    switch (g_ses_cfg_tail) {
    case 0:
        t = (int32_t)(g_gfl_file_len << 1) / 3;
        if (t < (int32_t)g_gfl_ring_size)
            g_gfl_ring_size = (uint32_t)t;
        break;
    case 1:
    case 2:
        t = (int32_t)g_gfl_file_len / 2;
        if (t < (int32_t)g_gfl_ring_size)
            g_gfl_ring_size = (uint32_t)t;
        break;
    }
    mm_ref_set(GFL_RING_REF, mm_alloc(g_gfl_ring_size, 1));
    mm_ref_set(GFL_FRAMEBUF_REF, mm_alloc(GFL_FRAME_BYTES, 0));
    g_gfl_file_pos = 0;
    while (g_gfl_file_pos < g_gfl_ring_size) {
        n = g_gfl_ring_size - g_gfl_file_pos;
        if (n > 0xC000)
            n = 0xC000;
        p = EMSLock((int16_t)mm_ref(GFL_RING_REF)->ems, (int32_t)g_gfl_file_pos, (uint16_t)n);
        if (p == NULL)
            o559A_fatal(DGSTR(0x6AC7), 0x66, NULL, 0x10);
        if (vfs_read(g_gfl_handle, p, (uint16_t)n) != 0)
            o559A_fatal(DGSTR(0x6AE0), 0x68, DGSTR(0x6AF9), -1);
        g_gfl_file_pos += n;
    }
}

/*
 * gfl_close - free the index, the sample slot block (if allocated), close
 * the GFL, free ring, staging and frame buffer (ctl_free_checked: NULL is
 * fatal, the variable is cleared).
 *
 * Original: IST.EXE 5313:08E2 (GFLSTR.H), far cdecl.
 * Verified: tests/py/test_gfl.py (unicorn differential), 2026-11-20.
 */
void gfl_close(void)
{
    bc_farfree(gfl_index);                              /* 5313:08ED farfree */
    gfl_index = NULL;           /* port: the far pointer at D61C is left dangling */
    if (g_sound_on != 0 && g_gfl_abytes > 0)
        ctl_free_checked(SND_SLOT_MM_REF(GFL_SND_SLOT), 0x73, DGSTR(0x6B10));
    vfs_close(g_gfl_handle);
    ctl_free_checked(GFL_RING_REF, 0x77, DGSTR(0x6B29));
    ctl_free_checked(GFL_STAGE_REF, 0x78, DGSTR(0x6B42));
    ctl_free_checked(GFL_FRAMEBUF_REF, 0x79, DGSTR(0x6B5B));
}

/*
 * gfl_update_frame_clock - frame = (g_clock_ms - start) * fps / 1000.
 *
 * Original: IST.EXE 5313:095E (GFLMAIN.H), far cdecl.  32-bit product
 * (LXMUL, low 32 bits), unsigned division (LUDIV), low word stored.
 * Verified: tests/py/test_gfl.py (unicorn differential), 2026-11-20.
 */
void gfl_update_frame_clock(void)
{
    uint32_t t = (g_clock_ms - g_gfl_start_ms) * (uint32_t)g_gfl_fps;
    g_gfl_frame = (int16_t)(uint16_t)(t / 1000u);
}

/*
 * gfl_show_frame - process the frame the clock asks for (g_gfl_frame), if
 * it was not processed yet:
 *  1. (sound on, audio) music sync: s = ((flags & 7Fh) >> 1), negated if
 *     flags bit 0; ampBreakPattern(s), s -+ 1, ... down to 0 inclusive -
 *     so every shipped frame (flags 00h/80h) calls ampBreakPattern(0) once.
 *  2. a delta frame after skipped frames is dropped (nothing shown).
 *  3. palette: flags bit 7 and the chunk already streamed -> read it from
 *     the ring into the target palette and set it; no chunk -> restore the
 *     header palette if a frame palette was set.
 *  4. type 1: nothing more (counts as shown).  type 2: the second of audio
 *     goes to half ((frame / fps + 1) & 1) of the sample block (when
 *     streamed and sound on).  Then, if the video bytes are in the ring:
 *     copy them to the staging block, decode, wait for the end of a
 *     vertical retrace, copy the frame buffer to surface 0.  A frame whose
 *     data has not been streamed yet is not shown (and not retried).
 *  5. resync (sound on, audio): expected sample position
 *     ((now - start) * abytes / 1000) mod (2 * abytes) (signed long maths);
 *     if its half differs from the half of the play position of channel
 *     (channels - 1), both channels are set to it.
 *
 * Original: IST.EXE 5313:0996 (GFLMAIN.H), far cdecl (char resync).
 * Verified: tests/py/test_gfl.py (unicorn differential, stepwise with a
 * controlled clock), 2026-11-20.
 */
void gfl_show_frame(uint8_t resync)
{
    uint16_t audio = 0;
    int8_t s, old;
    uint8_t *p;
    GflFrame f;

    if (g_gfl_frame <= g_gfl_last_frame)
        goto done;
    f = gfl_entry((uint16_t)g_gfl_frame);

    if (g_sound_on != 0 && g_gfl_abytes > 0) {
        int16_t fl = (int8_t)f.flags;                          /* cbw */
        s = (int8_t)(((fl & 0x7F) >> 1) * -((fl & 1) * 2 - 1));
        do {
            ampBreakPattern((int32_t)s);
            if (s >= 0)
                old = s--;
            else
                old = s++;
        } while (old != 0);
    }

    if ((int16_t)(g_gfl_last_shown + 1) < g_gfl_frame && f.type == 0)
        goto done;                                             /* dropped */

    if (f.flags & 0x80) {
        if (f.offset <= g_gfl_file_pos) {
            gfl_ring_read_far(mm_ref(GFL_RING_REF), gr_target_pal, f.offset - 0x300, 0, 0x300,
                              g_gfl_ring_size);
            gr_set_palette(gr_target_pal);
            g_gfl_pal_changed = 1;
        }
    } else if (g_gfl_pal_changed != 0) {
        gr_set_palette(g_gfl_palette);
        g_gfl_pal_changed = 0;
    }

    if (f.type == 1)
        goto shown;
    if (f.type == 2) {
        if (f.offset + g_gfl_abytes <= g_gfl_file_pos && g_sound_on != 0 && g_gfl_abytes > 0) {
            /* (frame / fps + 1) % 2, 16-bit IDIVs (fps 0: INT 0 handler gives -1) */
            int16_t half = (int16_t)((int16_t)(nk_idiv_s16(g_gfl_frame, g_gfl_fps, NULL) + 1) % 2);
            gfl_ring_copy_to_ems(mm_ref(GFL_RING_REF), mm_ref(SND_SLOT_MM_REF(GFL_SND_SLOT)),
                                 f.offset, (uint16_t)(half * (int16_t)g_gfl_abytes),
                                 g_gfl_abytes, g_gfl_ring_size);
        }
        audio = g_gfl_abytes;
    }
    if (f.offset + audio + f.vsize <= g_gfl_file_pos) {
        gfl_ring_copy_to_ems(mm_ref(GFL_RING_REF), mm_ref(GFL_STAGE_REF), f.offset + audio, 0,
                             f.vsize, g_gfl_ring_size);
        p = EMSLock((int16_t)mm_ref(GFL_STAGE_REF)->ems, 0, f.vsize);
        if (p == NULL)
            o559A_fatal(DGSTR(0x6B74), 0x4D, NULL, 0x10);
        gfl_decode_frame(mm_conv_ptr(mm_ref(GFL_FRAMEBUF_REF)), p, f.vsize);
        while ((pc_in_3da() & 8) != 8)
            ;
        while (pc_in_3da() & 8)
            ;
        mm_copy_block(mm_ref(GFL_FRAMEBUF_REF), mm_ref(GFL_SCREEN_REF));
shown:
        g_gfl_last_shown = g_gfl_frame;
    }

    if (resync != 0 && g_sound_on != 0 && g_gfl_abytes > 0) {
        int32_t prod = (int32_t)((g_clock_ms - g_gfl_start_ms) * (uint32_t)g_gfl_abytes);
        int32_t pos = prod / 1000 % (int32_t)((uint32_t)g_gfl_abytes * 2);   /* LDIV, LMOD */
        int8_t want = (int8_t)(pos / (int32_t)g_gfl_abytes);                 /* LDIV */
        int8_t have = (int8_t)((uint32_t)cdiGetPosition((int16_t)g_snd_channels - 1)
                               / (uint32_t)g_gfl_abytes);                    /* LUDIV */
        if (have != want) {
            cdiSetPosition((int16_t)g_snd_channels - 1, (uint32_t)pos);
            cdiSetPosition((int16_t)g_snd_channels - 2, (uint32_t)pos);
        }
    }
done:
    g_gfl_last_frame = g_gfl_frame;
}

/*
 * gfl_stream_fill - top up the ring buffer from the GFL while the current
 * frame period has more than 10 ms left.
 *
 * k = the next frame (frame + 1).  If the current frame is a dropped delta
 * frame, k moves on to the next frame with type != 0 (none left: return).
 * If k's data (with its palette chunk) starts after what was streamed so
 * far, the file skips forward: k moves to a frame with type != 0 and the
 * file is seeked to its start.  The amount to read is the minimum of the
 * ring size, the rest of the file, the free ring space before k's data
 * would be overwritten and the space to the end of the ring, at most
 * C000h (nothing: return); it is read in 800h-byte pieces while
 * 1000 / fps - (now - frame start) > 10 (16-bit arithmetic).
 *
 * Original: IST.EXE 5313:0DE8 (GFLMAIN.H), far cdecl.
 * Port: the loop calls pc_poll() once per pass, where the original's
 * timer ISR advances g_clock_ms during the reads.  Quirks kept: once set,
 * the palette size (300h) is not reset for the frame found by the skip
 * loop; a failed EMSLock is fatal but the loop still runs (line A8h); the
 * index entry after the last frame is read (see gfl_entry).
 * Verified: tests/py/test_gfl.py (unicorn differential), 2026-11-20.
 */
void gfl_stream_fill(void)
{
    int32_t n;
    uint8_t *p;
    uint16_t pal = 0;
    uint16_t k = (uint16_t)(g_gfl_frame + 1);
    int16_t chunk;
    uint32_t start, room;

    if ((int16_t)(g_gfl_last_shown + 1) < g_gfl_frame && gfl_entry((uint16_t)g_gfl_frame).type == 0) {
        while (k < g_gfl_nframes && gfl_entry(k).type == 0)
            k++;
        if (k >= g_gfl_nframes)
            return;
    }
    if (gfl_entry(k).flags & 0x80)
        pal = 0x300;
    if (gfl_entry(k).offset - pal > g_gfl_file_pos) {
        while (k < g_gfl_nframes && gfl_entry(k).type == 0)
            k++;
        if (k >= g_gfl_nframes)
            return;
        if (gfl_entry(k).flags & 0x80)
            pal = 0x300;
        g_gfl_file_pos = gfl_entry(k).offset - pal;
        vfs_seek(g_gfl_handle, (int32_t)(g_gfl_file_pos - (uint32_t)vfs_tell(g_gfl_handle)), 1);
    }

    n = (int32_t)g_gfl_ring_size;
    if ((int32_t)(g_gfl_file_len - g_gfl_file_pos) < n)
        n = (int32_t)(g_gfl_file_len - g_gfl_file_pos);
    start = gfl_entry(k).offset - pal;
    room = g_gfl_ring_size - (g_gfl_file_pos - start);
    if (room < (uint32_t)n)
        n = (int32_t)room;
    if ((int32_t)(g_gfl_ring_size - g_gfl_file_pos % g_gfl_ring_size) < n)
        n = (int32_t)(g_gfl_ring_size - g_gfl_file_pos % g_gfl_ring_size);
    if (n > 0xC000)
        n = 0xC000;
    else if (n <= 0)
        return;

    p = EMSLock((int16_t)mm_ref(GFL_RING_REF)->ems, (int32_t)(g_gfl_file_pos % g_gfl_ring_size),
                (uint16_t)n);
    if (p == NULL)
        o559A_fatal(DGSTR(0x6B8E), 0xA8, NULL, 0x10);
    for (;;) {
        pc_poll();                       /* port: the 100 Hz ISR of the original */
        if (!((int16_t)(nk_idiv_s16(1000, g_gfl_fps, NULL)
                        - (int16_t)((uint16_t)g_clock_ms - (uint16_t)g_gfl_frame_ms)) > 10))
            break;
        chunk = n < 0x800 ? (int16_t)n : 0x800;
        if (chunk <= 0)
            return;
        if (vfs_read(g_gfl_handle, p, chunk) != 0)
            o559A_fatal(DGSTR(0x6BA8), 0xB2, DGSTR(0x6BC2), -1);
        n -= chunk;
        p += chunk;                      /* far pointer offset, inside the locked window */
        g_gfl_file_pos += (uint32_t)(int32_t)chunk;
    }
}

/*
 * gfl_play_loop - clear surface 0, set the header palette, show frame 0,
 * start the clock and (sound on, audio) the sample on channels
 * (channels - 1) and (channels - 2); then until Esc or the clock passes
 * the last frame: kbhit/getch, take the frame start time, update the
 * frame number, show it, stream.  Stops the two channels at the end.
 * Returns the last key read (0 if none); every key is consumed.
 *
 * Original: IST.EXE 5313:1126 (GFL.C), far cdecl, returns char.
 * Verified: tests/py/test_gfl.py (unicorn differential with scripted
 * clock and keys), 2026-11-20.
 */
char gfl_play_loop(void)
{
    char key = 0;

    asm_memset32(mm_conv_ptr(mm_ref(GFL_SCREEN_REF)), 0, GFL_FRAME_BYTES);
    gr_set_palette(g_gfl_palette);
    g_gfl_pal_changed = 0;
    g_gfl_frame = 0;
    g_gfl_last_shown = -1;
    g_gfl_last_frame = -1;
    gfl_show_frame(0);
    g_gfl_start_ms = g_clock_ms;
    pc_video_event(PC_VIDEO_START, NULL);       /* port: the host may show a better copy (hdvideo.h) */
    if (g_sound_on != 0 && g_gfl_abytes > 0) {
        snd_play_sample(GFL_SND_SLOT, g_gfl_abytes, 0x40, 0, (uint8_t)(g_snd_channels - 1));
        snd_play_sample(GFL_SND_SLOT, g_gfl_abytes, 0x40, 0, (uint8_t)(g_snd_channels - 2));
    }
    for (;;) {
        if (pc_kbhit()) {
            key = (char)pc_getch();
            if (key == 0x1B)
                break;
        }
        g_gfl_frame_ms = g_clock_ms;
        gfl_update_frame_clock();
        if ((uint16_t)g_gfl_frame >= g_gfl_nframes)
            break;
        gfl_show_frame(1);
        gfl_stream_fill();
    }
    if (g_sound_on != 0 && g_gfl_abytes > 0) {
        cdiStopChannel((int16_t)g_snd_channels - 1);
        cdiStopChannel((int16_t)g_snd_channels - 2);
    }
    return key;
}

/*
 * gfl_play - play <name>.GHD/.GFL; returns the key that ended playback
 * (1Bh = Esc, 0 = played to the end without a key).
 *
 * Original: IST.EXE 5313:1270 (GFL.C), far cdecl (char far *name),
 * returns char.  Names longer than 8 characters are fatal (line 16h).
 * The file names are sprintf'd into the DGROUP string literal
 * "01234567.012" (6BD9, 13 bytes).  The whole playback runs inside
 * mm_ems_lock(DE26) / mm_ems_unlock.
 * Port: the three pc_video_event() calls (here and in gfl_play_loop) tell
 * the host when a video is asked for, starts and ends; they do nothing to
 * the game (platform/pc.h, platform/hdvideo.h).
 * Verified: tests/py/test_gfl.py (unicorn differential, whole playback
 * of INTRO), 2026-11-20.
 */
char gfl_play(const char *name)
{
    char key;

    if (strlen(name) > 8)
        o559A_fatal(DGSTR(0x6BE7), 0x16, DGSTR(0x6BFA), -1);
    pc_video_event(PC_VIDEO_OPEN, name);        /* port: hdvideo.h */
    mm_ems_lock(g_blit_lock, 0x19, DGSTR(0x6C16));
    sprintf(g_gfl_name_buf, DGSTR(0x6C29), name);       /* "%s.GHD" */
    gfl_load_ghd(g_gfl_name_buf);
    sprintf(g_gfl_name_buf, DGSTR(0x6C30), name);       /* "%s.GFL" */
    gfl_open_gfl(g_gfl_name_buf);
    key = gfl_play_loop();
    pc_video_event(PC_VIDEO_STOP, name);        /* port: hdvideo.h */
    gfl_close();
    mm_ems_unlock(g_blit_lock);
    return key;
}
