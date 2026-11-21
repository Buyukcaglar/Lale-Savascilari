/*
 * amp.h - DSMI AMP 2.00 module player and its AMF loader.
 *
 * Original: IST.EXE seg0EF6 (player, "AMP 2.00 - (C) 1992,1994 Otto Chrons"),
 * seg105B (AMF loader), seg111B (ampFreeModule).  Implementation:
 * src/dsmi/amp.c (player), src/dsmi/amfload.c (loader + free).
 * All public functions are far Pascal in the original; the logical parameter
 * order is kept, 32-bit arguments stay 32-bit.
 *
 * Output goes through CDI (src/dsmi/cdi.h).  Timing: ampInterrupt() is the
 * routine the NSES 100 Hz timer calls; ampPoll() runs ampTick() for every
 * tick length (10000 / tickHz in 0.1 ms DSMI time units) that has elapsed.
 */
#ifndef LALE_DSMI_AMP_H
#define LALE_DSMI_AMP_H

#include <stddef.h>
#include <stdint.h>

#include "dsmi/cdi.h"
#include "engine/dgroup.h"

/* ---- module structures (native mirrors of the DSMI layout) -------------------
 * Far pointers are native pointers, so these structs are NOT byte images of
 * the original; the original offsets are given per field. */

/* INSTRUMENT, 0x41 bytes (the AMF v10+ on-disk record). */
typedef struct AmpInstrument {
    uint8_t      type;          /* +00                                                 */
    char         name[32];      /* +01                                                 */
    char         filename[13];  /* +21                                                 */
    CdiSampleRef sample;        /* +2E far ptr to the data, FFFF:handle in EMS, 0 none */
    uint32_t     length;        /* +32 bytes                                           */
    uint16_t     rate;          /* +36 c2spd (Hz at note 60)                           */
    uint8_t      volume;        /* +38 0..64                                           */
    uint32_t     loopstart;     /* +39                                                 */
    uint32_t     loopend;       /* +3D 0 = no loop                                     */
    uint32_t     sample_index;  /* native: the +2E dword as read from the file (the    */
                                /* original overwrites it with the pointer)            */
} AmpInstrument;

/* ORDER ("pattern"), 0x82 bytes: rows + one track per channel. */
typedef struct AmpPattern {
    uint16_t  rows;             /* +00                                                 */
    uint8_t  *track[32];        /* +02 far ptrs (track numbers until ampLinkTracks)    */
    uint16_t  track_ref[32];    /* native: the track numbers read from the file        */
} AmpPattern;

/* Track data (one malloc block per stored track, count*3+6 bytes):
 *   +0 u16 count, +2 u8 0, then count entries {u8 row, u8 cmd, u8 arg}.
 *   cmd < 0x80: note (0 = stop, 0x7F = keep note), arg = volume (0xFF = none)
 *   cmd 0x80..0x97: row command, see amp.c; row 0xFF ends the track. */
#define AMP_TRACK_HEADER 3

/* MODULE, 0x5C bytes. */
typedef struct AmpModule {
    uint8_t        type;            /* +00 3 = AMF, 0 = not loaded                  */
    uint32_t       size;            /* +01 bytes of memory used                     */
    uint32_t       filesize;        /* +05                                          */
    char           name[32];        /* +09                                          */
    uint8_t        channelCount;    /* +29                                          */
    int8_t         channelPanning[32]; /* +2A -63..63, 100 = surround               */
    uint8_t        instrumentCount; /* +4A                                          */
    AmpInstrument *instruments;     /* +4B                                          */
    uint8_t        patternCount;    /* +4F (orders)                                 */
    AmpPattern    *patterns;        /* +50                                          */
    uint16_t       trackCount;      /* +54                                          */
    uint8_t      **tracks;          /* +56 [trackCount+1], [0] = NULL               */
    uint8_t        tempo;           /* +5A BPM, default 125                         */
    uint8_t        speed;           /* +5B ticks per row, default 6                 */
} AmpModule;

/* ---- per-track player state: 32 x 45 bytes at DGROUP 7C91 -------------------- */
#pragma pack(push, 1)
typedef struct AmpTrack {
    uint8_t  data_far[4];   /* +00 far ptr to the track data -> amp_track_data[] (unused) */
    uint16_t pos;           /* +04 byte offset of the next entry, 0xFFFF = track ended */
    uint16_t flags;         /* +06 bit1 = paused                                       */
    uint8_t  note;          /* +08                                                     */
    uint8_t  instrument;    /* +09                                                     */
    uint8_t  volume;        /* +0A                                                     */
    uint16_t ticks;         /* +0B ticks since note on (saturating)                    */
    uint16_t period;        /* +0D                                                     */
    uint8_t  cmd;           /* +0F active tick effect (0 = none)                       */
    uint8_t  param;         /* +10                                                     */
    uint16_t target;        /* +11 tone porta target period                            */
    int8_t   tp_speed;      /* +13 tone porta speed (signed)                           */
    uint8_t  tp_mem;        /* +14 tone porta memory                                   */
    int8_t   porta_mem;     /* +15 portamento memory                                   */
    uint32_t arp_freq[3];   /* +16 arpeggio frequencies                                */
    uint8_t  pad22;         /* +22                                                     */
    uint8_t  vib_pos;       /* +23                                                     */
    uint8_t  vib_mem;       /* +24 speed<<4 | depth                                    */
    uint8_t  trem_mem;      /* +25                                                     */
    uint8_t  trem_speed;    /* +26                                                     */
    uint8_t  trem_depth;    /* +27                                                     */
    uint8_t  trem_pos;      /* +28                                                     */
    uint8_t  offset_mem;    /* +29                                                     */
    uint8_t  pad2a[2];      /* +2A                                                     */
    int8_t   pan;           /* +2C                                                     */
} AmpTrack;

/* TRACKDATA returned by ampGetTrackData (10 bytes at DGROUP 7C44). */
typedef struct AmpTrackData {
    uint16_t flags;         /* 7C44 */
    uint8_t  note;          /* 7C46 */
    uint8_t  instrument;    /* 7C47 */
    uint8_t  volume;        /* 7C48 */
    uint16_t ticks;         /* 7C49 */
    uint8_t  cmd;           /* 7C4B  cmd | 0x80 */
    uint8_t  param;         /* 7C4C */
    uint8_t  pan;           /* 7C4D */
} AmpTrackData;
#pragma pack(pop)
_Static_assert(sizeof(AmpTrack) == 45, "AmpTrack is 45 bytes");
_Static_assert(offsetof(AmpTrack, cmd) == 0x0F, "AmpTrack.cmd");
_Static_assert(offsetof(AmpTrack, arp_freq) == 0x16, "AmpTrack.arp_freq");
_Static_assert(offsetof(AmpTrack, pan) == 0x2C, "AmpTrack.pan");
_Static_assert(sizeof(AmpTrackData) == 10, "AmpTrackData is 10 bytes");

/* ---- DGROUP (16E0) ---------------------------------------------------------- */
/* 7AE4..7BE3: AMP's private ISR stack (ampInterrupt), 7BE4/7BE6 saved SP/SS: not used by the port */
#define g_amp_mod_copy      DGP(0x7BE8)     /* 0x5C bytes: copy of the playing MODULE      */
#define g_amp_mod_type      DG8(0x7BE8)     /*   +00                                      */
#define g_amp_mod_channels  DG8(0x7C11)     /*   +29                                      */
#define g_amp_mod_pans      ((int8_t *)DGP(0x7C12)) /* +2A [32]                           */
#define g_amp_mod_inscount  DG8(0x7C32)     /*   +4A                                      */
#define g_amp_mod_ordcount  DG8(0x7C37)     /*   +4F                                      */
#define g_amp_mod_tempo     DG8(0x7C42)     /*   +5A                                      */
#define g_amp_mod_speed     DG8(0x7C43)     /*   +5B                                      */
#define g_amp_trackdata     ((AmpTrackData *)DGP(0x7C44))
#define g_amp_init_options  DG16(0x7C4E)    /* ampInit options                             */
#define g_amp_status        DG8(0x7C50)     /* bit0 playing, bit1 paused                   */
#define g_amp_play_options  DG16(0x7C51)    /* bit0 = loop                                 */
#define g_amp_start_order   DG8(0x7C53)
#define g_amp_end_order     DG8(0x7C54)
#define g_amp_cur_order     DG8(0x7C55)
#define g_amp_cur_track     DG8(0x7C56)     /* track being processed (= CDI channel)       */
#define g_amp_row           DGS16(0x7C57)   /* next row to read                            */
#define g_amp_last_row      DG16(0x7C59)    /* row last read (ampGetRow)                   */
#define g_amp_tick          DG8(0x7C5B)
/* 7C5C far ORDER* -> amp_orders, 7C60 far INSTRUMENT* -> amp_instruments */
#define g_amp_speed         DG8(0x7C64)     /* ticks per row                               */
#define g_amp_tempo         DG8(0x7C65)     /* BPM                                         */
#define g_amp_7c66          DG8(0x7C66)     /* set to 0x40 by ampPlayRange, never read     */
#define g_amp_sync          DG8(0x7C67)     /* effect 0x8E value, read by ampGetSync       */
#define g_amp_break         DG8(0x7C68)     /* pattern break / position jump pending       */
#define g_amp_7c6b          DG16(0x7C6B)    /* cleared by ampPlayRange, never read         */
#define g_amp_channels      DG16(0x7C6D)
#define g_amp_pans          ((int8_t *)DGP(0x7C6F)) /* [32] initial pans                  */
#define g_amp_rows          DGS16(0x7C8F)   /* rows in the current order                   */
#define g_amp_tracks_base   0x7C91          /* AmpTrack[32]                                */
/* 8231 + ch*26: SAMPLEINFO per channel -> amp_sampleinfo[] (contains a far ptr) */
#define g_amp_track_volume  DGP(0x8571)     /* u8[32] per-track volume 0..64 (0x40)        */
#define g_amp_initialized   DG8(0x8591)     /* bit0                                        */
#define g_amp_poll_lock     DG8(0x859E)
#define g_amp_int_lock      DG8(0x859F)
#define g_amp_tick_hz       DG16(0x85A0)    /* BPM*50/125                                  */
#define g_amp_last_tick     DG32(0x85A2)    /* DSMI time of the last tick (0.1 ms)         */
/* 85A6 far ptr MCPSampleRealAddress -> amp_sample_real_address */
#define g_amp_default_pans  ((int8_t *)DGP(0x85AA)) /* -63,63,63,-63 x4 (old AMF files) */
#define g_amp_name_emsalloc  DGSTR(0x85BA)  /* "EMSAlloc"  */
#define g_amp_name_emscopyto DGSTR(0x85C3)  /* "EMSCopyTo" */
#define g_amp_name_emsfree   DGSTR(0x86B4)  /* "EMSFree"   */
#define g_amf_ems_ok        DG16(0xE786)    /* EMSAlloc and EMSCopyTo found                */
#define g_amf_options       DG16(0xE788)    /* ampLoadModule options                       */
/* E78A far EMSCopyTo, E78E far EMSAlloc, E792 far EMSFree -> native pointers in amfload.c */

/* ---- functions imported through DDS (native signatures) ----------------------
 * Whoever registers these names must use exactly these types. */
/* "MCPSampleRealAddress" (registered by NSES 0D42:0005): real address of byte
 * pos of the EMS sample with this handle (maps it into the page frame). */
typedef uint8_t *(*McpSampleRealAddressFn)(uint32_t handle, uint32_t pos);   /* = mcp.h McpRealAddressFn */
/* DSMI EMS heap (seg1154, src/dsmi/emsheap.h EMSAlloc/EMSCopyTo/EMSFree). */
typedef int16_t (*EmsAllocFn)(int32_t size);                 /* -> handle (> 0 ok) */
typedef int16_t (*EmsCopyToFn)(int16_t handle, const void *src, int32_t offset, int32_t length);
typedef void    (*EmsFreeFn)(int16_t handle);

/* ---- native state ------------------------------------------------------------ */
extern uint8_t       *amp_track_data[32];      /* 7C91 + t*45 +0: far track pointers   */
extern CdiSampleInfo  amp_sampleinfo[32];      /* 8231 + ch*26                         */
extern AmpPattern    *amp_orders;              /* 7C5C                                 */
extern AmpInstrument *amp_instruments;         /* 7C60                                 */
extern AmpInstrument *amp_mod_instruments;     /* 7C33 (module copy +4B)               */
extern AmpPattern    *amp_mod_patterns;        /* 7C38 (module copy +50)               */
extern uint8_t      **amp_mod_tracks;          /* 7C3E (module copy +56)               */
extern McpSampleRealAddressFn amp_sample_real_address; /* 85A6                         */

/* per-track state record: original offset from the CS:0045 table (t*45) */
AmpTrack *amp_track(uint16_t t);

/* ---- player (seg0EF6) ---------------------------------------------------------- */
uint32_t ampScaleVolume(int32_t ch, uint32_t vol);                       /* 0EF6:039B */
void     ampTick(void);                                                  /* 0EF6:0E56 */
int16_t  ampPoll(void);                                                  /* 0EF6:0ED2 */
void     ampInterrupt(void);                                             /* 0EF6:0F4C */
int16_t  ampInit(int32_t options);                                       /* 0EF6:0F8C */
int16_t  ampClose(void);                                                 /* 0EF6:1014 */
int16_t  ampPlayRange(AmpModule *m, int32_t start, int32_t end, int32_t options); /* 0EF6:1032 */
int16_t  ampPlayModule(AmpModule *m, int32_t options);                   /* 0EF6:121F */
int16_t  ampPlayPattern(AmpModule *m, int32_t order, int32_t options);   /* 0EF6:125A */
int16_t  ampStopModule(void);                                            /* 0EF6:1296 */
int16_t  ampPauseModule(void);                                           /* 0EF6:12FE */
int16_t  ampResumeModule(void);                                          /* 0EF6:1338 */
int16_t  ampGetModuleStatus(void);                                       /* 0EF6:1372 */
int16_t  ampGetTrackStatus(int32_t track);                               /* 0EF6:138A */
int16_t  ampPauseTrack(int32_t track);                                   /* 0EF6:13B9 */
int16_t  ampResumeTrack(int32_t track);                                  /* 0EF6:1407 */
int16_t  ampGetPattern(void);                                            /* 0EF6:1458 */
int16_t  ampGetRow(void);                                                /* 0EF6:146F */
int16_t  ampGetSync(void);                                               /* 0EF6:1484 */
uint16_t ampGetTempo(void);                                              /* 0EF6:14A0 */
int16_t  ampSetTempo(int32_t speed_bpm);                                 /* 0EF6:14B9 */
AmpTrackData *ampGetTrackData(int32_t track);                            /* 0EF6:14F6 */
int16_t  ampBreakPattern(int32_t dir);                                   /* 0EF6:1577 */
int32_t  ampGetTickLength(void);                                         /* 0EF6:15B9 */
int16_t  ampSetPanning(int32_t track, int32_t pan);                      /* 0EF6:15E5 */
int16_t  ampSetTrackVolume(int32_t track, int32_t vol);                  /* 0EF6:1625 */

/* ---- loader (seg105B, seg111B) ------------------------------------------------- */
void     ampLinkTracks(AmpModule *m);                                    /* 105B:0009 */
int16_t  ampLoadAMF(int16_t handle, AmpModule *m, int32_t options);      /* 105B:008A */
int16_t  ampLoadModule(const char *filename, int32_t options, AmpModule **out); /* 105B:07F3 */
void     ampFreeModule(AmpModule *m);                                    /* 111B:0003 */

#endif
