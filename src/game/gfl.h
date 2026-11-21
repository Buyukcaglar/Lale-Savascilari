/*
 * gfl.h - the GFL/GHD video player.  Original: IST.EXE ovl5313, sources
 * CODE\GFLCODE\GFL.C, GFLMAIN.H, GFLSTR.H, GFLALT.H (from the assert
 * strings), Borland C, far cdecl.  Implementation: src/game/gfl.c.
 * Format: docs/FORMATS.md "GFL/GHD video", tools/gflview.py.
 *
 * Only gfl_play() is called from outside (DEMOLAR.L, 7 call sites).  It
 * loads <name>.GHD (header, palette, first second of audio, frame index),
 * opens <name>.GFL and streams it through an EMS ring buffer, decodes each
 * frame into a 64000-byte conventional buffer and copies it to surface 0
 * after vertical retrace.  Frames are paced on the 100 Hz ms clock
 * (g_clock_ms); a late player drops delta frames and skips forward to the
 * next key frame.  Audio is a looping 2 x 1 s EMS sample (NSES slot 30h)
 * played on the last two mixer channels and re-synchronised to the clock.
 *
 * Far pointers: the MmHandle far pointers (D5F4/D5F8/D5FC, sample slot
 * E284) stay in dg[] (mm_ref/mm_ref_set).  The farmalloc'd frame index
 * (D61C) is the native gfl_index; its dg[] bytes stay unused.
 */
#ifndef LALE_GAME_GFL_H
#define LALE_GAME_GFL_H

#include <stdint.h>

#include "engine/dgroup.h"
#include "engine/memman.h"
#include "engine/nses.h"
#include "engine/oses.h"

/* ---- GHD header, read verbatim to D620 ------------------------------------ */
#pragma pack(push, 1)
typedef struct GflHeader {
    char     sig[3];          /* D620 "GFL" (only sum of the signed bytes == D9h is checked) */
    uint16_t nframes;         /* D623 */
    uint8_t  fps;             /* D625 (10 in every file) */
    uint16_t abytes;          /* D626 audio bytes per 1 s chunk (22050), 0 = silent */
    uint32_t magic;           /* D628 100000 in every file, never read */
    uint8_t  pad[4];          /* D62C 0 */
    uint8_t  palette[0x300];  /* D630 header palette (6-bit DAC values) */
} GflHeader;

/* One frame index entry (8 bytes in the GHD and in memory). */
typedef struct GflFrame {
    uint8_t  type;            /* 0 delta, 2 with 1 s of audio, 1 palette only (no video) */
    uint16_t vsize;           /* video bytes */
    uint32_t offset;          /* GFL offset of the payload (palette sits at offset-300h) */
    uint8_t  flags;           /* bit 7 palette chunk; bits 0-6 music sync (see gfl_show_frame) */
} GflFrame;
#pragma pack(pop)
_Static_assert(sizeof(GflHeader) == 0x310, "GflHeader is 0x310 bytes");
_Static_assert(sizeof(GflFrame) == 8, "GflFrame is 8 bytes");

#define GFL_FRAME_BYTES   0xFA00u   /* 320 x 200 */

/* ---- DGROUP ----------------------------------------------------------------- */
#define g_gfl_frame       DGS16(0xD5F2)  /* i16  frame due by the clock */
#define GFL_RING_REF      0xD5F4         /* far MmHandle *: EMS ring buffer (mm_ref) */
#define GFL_STAGE_REF     0xD5F8         /* far MmHandle *: 64 KB EMS staging block */
#define GFL_FRAMEBUF_REF  0xD5FC         /* far MmHandle *: 64000-byte frame buffer (conv.) */
#define g_gfl_ring_size   DG32(0xD600)   /* u32  ring buffer size */
#define g_gfl_file_len    DG32(0xD604)   /* u32  GFL length (signed in some compares) */
#define g_gfl_file_pos    DG32(0xD608)   /* u32  bytes of the GFL streamed into the ring */
#define g_gfl_handle      DGS16(0xD60C)  /* i16  vfs handle of the GFL */
#define g_gfl_start_ms    DG32(0xD60E)   /* u32  g_clock_ms when frame 0 was shown */
#define g_gfl_frame_ms    DG32(0xD612)   /* u32  g_clock_ms at the start of this loop pass */
#define g_gfl_last_shown  DGS16(0xD616)  /* i16  last frame decoded (or palette-only) */
#define g_gfl_last_frame  DGS16(0xD618)  /* i16  last frame processed by gfl_show_frame */
#define g_gfl_pal_changed DG16(0xD61A)   /* u16  1 = a frame palette is on the DAC */
/* D61C: far ptr to the frame index -> gfl_index (native) */
#define g_gfl_hdr         ((GflHeader *)DGP(0xD620))
#define g_gfl_nframes     DG16(0xD623)   /* u16 */
#define g_gfl_fps         DG8(0xD625)    /* u8  */
#define g_gfl_abytes      DG16(0xD626)   /* u16 */
#define g_gfl_palette     DGP(0xD630)    /* u8[0x300] header palette */
#define g_gfl_name_buf    DGSTR(0x6BD9)  /* the string literal "01234567.012", used as
                                            the sprintf buffer for "<name>.GHD/.GFL" */

/* Other modules' DGROUP the player uses: g_sound_on (memman.h), g_clock_ms
   (nkontrol.h), g_snd_channels and the sample slot table (nses.h),
   g_ses_cfg_tail = memory mode 8DDA (oses.h). */
#define GFL_SND_SLOT      0x30           /* the player's sample slot (ref E284) */

/* 16E0:D61C far ptr -> farmalloc'd GflFrame[nframes] (raw little-endian bytes) */
extern uint8_t *gfl_index;

/* ---- functions (original address order) ------------------------------------ */
void gfl_ring_read_far(MmHandle *ring, uint8_t *dst, uint32_t pos, uint16_t dstoff,
                       uint32_t len, uint32_t ring_size);                  /* 5313:0000 */
void gfl_ring_copy_to_ems(MmHandle *ring, MmHandle *dst, uint32_t pos, uint32_t dstoff,
                          uint32_t len, uint32_t ring_size);               /* 5313:0145 */
void gfl_alloc_sample_slot(int16_t slot, uint16_t len, int16_t count);     /* 5313:0241 */
void gfl_decode_frame(uint8_t *dst, const uint8_t *src, uint16_t len);     /* 5313:0390 */
void gfl_load_ghd(const char *name);                                       /* 5313:04D0 */
void gfl_open_gfl(const char *name);                                       /* 5313:06AA */
void gfl_close(void);                                                      /* 5313:08E2 */
void gfl_update_frame_clock(void);                                         /* 5313:095E */
void gfl_show_frame(uint8_t resync);                                       /* 5313:0996 */
void gfl_stream_fill(void);                                                /* 5313:0DE8 */
char gfl_play_loop(void);                                                  /* 5313:1126 */
char gfl_play(const char *name);                                           /* 5313:1270 */

#endif
