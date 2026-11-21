/*
 * cdi.h - DSMI CDI, the "channel distributor interface".
 *
 * Original: IST.EXE seg0EAE (DSMI, Otto Chrons).  Implementation: src/dsmi/cdi.c.
 *
 * CDI sits between the players (AMP, the game's sample code in NSES) and the
 * sound devices.  A device (the MCP software mixer inside SDISB16.DSD, the GUS
 * driver, ...) registers a table of 26 functions with cdiRegister() and gets 32
 * consecutive channel numbers.  Every cdiXxx() wrapper looks the device up from
 * its channel argument and forwards the call.
 *
 * ---------------------------------------------------------------------------
 * THE DEVICE CONTRACT (for whoever implements a device, e.g. src/dsmi/mcp.c)
 * ---------------------------------------------------------------------------
 * Original: a 0x68-byte table of 26 far Pascal function pointers whose
 * arguments are all 32-bit (DSMI pushes dwords), results in DX:AX.  The
 * native CdiDevice keeps the slot order and the argument lists *as the device
 * receives them*.  Note that several wrappers use the channel only to choose
 * the device and do NOT pass it on (marked "no ch" below) - this is the
 * original behaviour.
 *
 *  slot  member            device receives               used by IST.EXE
 *  0x00  setSample         (ch, CdiSampleInfo *si)       AMP instrument, game samples
 *  0x04  playSample        (ch, freq, volume)            AMP note on, game samples
 *  0x08  setVolume         (ch, volume)                  AMP (every row + effects)
 *  0x0C  setFrequency      (ch, freq)                    AMP slides/vibrato/arpeggio
 *  0x10  setLinearRate     (ch, rate)                    -
 *  0x14  setPosition       (ch, pos)                     AMP 0x90/retrig, GFL video
 *  0x18  setPanning        (ch, pan)                     AMP, game
 *  0x1C  setMasterVolume   (volume)          no ch       -
 *  0x20  muteChannel       (ch)                          ampPauseTrack
 *  0x24  unmuteChannel     (ch)                          ampResumeTrack / ampPlayRange
 *  0x28  stopChannel       (ch)                          AMP note 0 / stop, game
 *  0x2C  muteAll           ()                no ch       -
 *  0x30  unmuteAll         ()                no ch       -
 *  0x34  download          (sample, tag, length) no ch   ampPlayRange, game samples
 *  0x38  unload            (tag)             no ch       ampStopModule / ampPlayRange error
 *  0x3C  unloadAll         ()                no ch       -
 *  0x40  getVolume         (ch)                          -
 *  0x44  getFrequency      (ch)                          -
 *  0x48  getPosition       (ch)              -> bytes    GFL video, game voice waits
 *  0x4C  getPanning        (ch)                          -
 *  0x50  getSample         (ch)              -> tag      -
 *  0x54  setupChannels     (count)           no ch       oses_init
 *  0x58  getChannelStatus  (ch)                          -
 *  0x5C  poll              ()                no ch       NSES 40 Hz timer (no IRQ devices)
 *  0x60  setEffect         (a, b)            no ch       -   (wrapper takes (ch, a, b))
 *  0x64  getStatus         (arg)             no ch       -   (wrapper takes (ch, arg))
 *
 * Units and value ranges, as AMP produces them:
 *   ch      channel number as handed out by cdiRegister (device n: n*32 .. n*32+31).
 *           AMP uses channels 0 .. channelCount-1.
 *   freq    playback rate in Hz (sample frames per second), unsigned 32-bit:
 *           c2spd * 2^(note/12) (note 60 = c2spd), or c2spd * 6848 / period.
 *   volume  0..64 normally (64 = full).  Unsigned 32-bit and NOT clamped by AMP:
 *           effect 0x83 passes any byte value (up to 255), and AMP scales by the
 *           per-track volume (vol * trackVol / 64).  The device must cope.
 *   pan     signed: -63 (left) .. 0 (centre) .. +63 (right), 100 = surround.
 *   pos     sample offset in bytes from the start of the sample.
 *   length  bytes (8-bit samples).
 *   Return  int32 (DX:AX).  AMP ignores every result except download's low
 *           word (non-zero = failure, AMP then unloads and fails with -2).
 *
 * Samples and tags.  The original identifies a sample by a 32-bit far pointer:
 * the data in conventional memory, or FFFF:handle for a sample in the DSMI EMS
 * heap.  Natively that is a CdiSampleRef.  download(sample, tag, length) gives
 * the device the real address of the data (for EMS samples AMP obtained it
 * from MCPSampleRealAddress) together with the tag; setSample later refers to
 * the sample by the same tag in CdiSampleInfo.sample, and unload(tag) frees it.
 *
 * CdiSampleInfo lifetime: AMP passes a pointer to its persistent per-channel
 * record (DGROUP 8231 + ch*26 in the original) and keeps updating the freq and
 * volume fields there; the game passes records from its own sample table.
 * A device may keep the pointer or copy the fields.
 */
#ifndef LALE_DSMI_CDI_H
#define LALE_DSMI_CDI_H

#include <stdint.h>

#include "engine/dgroup.h"

/* The original's 32-bit sample identity (a far pointer).  Equality and
 * "is null" follow the original dword: two refs are the same sample if all
 * three fields are equal; the null ref is all zero. */
typedef struct CdiSampleRef {
    uint8_t  *ptr;          /* conventional-memory data (in_ems == 0), else NULL       */
    uint16_t  ems_handle;   /* DSMI EMS heap handle = original offset (in_ems == 1)    */
    uint8_t   in_ems;       /* 1: original pointer was FFFF:handle                      */
} CdiSampleRef;

static inline int cdi_ref_is_null(CdiSampleRef r) { return !r.ptr && !r.in_ems && !r.ems_handle; }
static inline int cdi_ref_equal(CdiSampleRef a, CdiSampleRef b)
{
    return a.ptr == b.ptr && a.ems_handle == b.ems_handle && a.in_ems == b.in_ems;
}

/* SAMPLEINFO, 26 (0x1A) bytes in the original; offsets given per field. */
typedef struct CdiSampleInfo {
    CdiSampleRef sample;    /* +00 far ptr: the sample (tag as given to download)     */
    uint32_t  length;       /* +04 bytes                                               */
    uint32_t  loopstart;    /* +08 bytes                                               */
    uint32_t  loopend;      /* +0C bytes; 0 (with loopstart 0) = no loop              */
    uint8_t   mode;         /* +10 AMP always writes 1                                 */
    uint16_t  sampleID;     /* +11 EMS handle when sample.in_ems, else 0               */
    uint16_t  rate;         /* +13 c2spd: Hz at note 60                                */
    uint32_t  freq;         /* +15 current playback frequency (Hz), kept by AMP        */
    uint8_t   volume;       /* +19 current volume before track scaling, kept by AMP    */
} CdiSampleInfo;

/* CDIDEVICE: 26 function pointers, slot offsets as in the original table. */
typedef struct CdiDevice {
    int32_t (*setSample)(int32_t ch, CdiSampleInfo *si);                 /* 00 */
    int32_t (*playSample)(int32_t ch, uint32_t freq, uint32_t volume);   /* 04 */
    int32_t (*setVolume)(int32_t ch, uint32_t volume);                   /* 08 */
    int32_t (*setFrequency)(int32_t ch, uint32_t freq);                  /* 0C */
    int32_t (*setLinearRate)(int32_t ch, uint32_t rate);                 /* 10 */
    int32_t (*setPosition)(int32_t ch, uint32_t pos);                    /* 14 */
    int32_t (*setPanning)(int32_t ch, int32_t pan);                      /* 18 */
    int32_t (*setMasterVolume)(uint32_t volume);                         /* 1C */
    int32_t (*muteChannel)(int32_t ch);                                  /* 20 */
    int32_t (*unmuteChannel)(int32_t ch);                                /* 24 */
    int32_t (*stopChannel)(int32_t ch);                                  /* 28 */
    int32_t (*muteAll)(void);                                            /* 2C */
    int32_t (*unmuteAll)(void);                                          /* 30 */
    int32_t (*download)(uint8_t *sample, CdiSampleRef tag, uint32_t length); /* 34 */
    int32_t (*unload)(CdiSampleRef tag);                                 /* 38 */
    int32_t (*unloadAll)(void);                                          /* 3C */
    int32_t (*getVolume)(int32_t ch);                                    /* 40 */
    int32_t (*getFrequency)(int32_t ch);                                 /* 44 */
    int32_t (*getPosition)(int32_t ch);                                  /* 48 */
    int32_t (*getPanning)(int32_t ch);                                   /* 4C */
    CdiSampleRef (*getSample)(int32_t ch);                               /* 50 */
    int32_t (*setupChannels)(int32_t count);                             /* 54 */
    int32_t (*getChannelStatus)(int32_t ch);                             /* 58 */
    void    (*poll)(void);                                               /* 5C */
    int32_t (*setEffect)(int32_t a, int32_t b);                          /* 60 */
    int32_t (*getStatus)(int32_t arg);                                   /* 64 */
} CdiDevice;

#define CDI_MAX_DEVICES 8       /* room in DGROUP 7796..7AD5 (8 x 0x68) */

/* ---- DGROUP (16E0) -------------------------------------------------------- */
#define g_cdi_magic        DG16(0x7694)           /* u16  0xDA once cdiInit ran          */
#define g_cdi_channel_dev  DGP(0x7696)            /* u8[256] channel -> device index    */
#define g_cdi_dev_count    DG8(0x7AD6)            /* u8   registered devices            */
#define g_cdi_name_register DGSTR(0x7AD7)         /* "cdiRegister" (DDS export name)    */
/* 7796 + n*0x68: copies of the device tables (far pointers) -> cdi_devices[] */
extern CdiDevice cdi_devices[CDI_MAX_DEVICES];

void         cdiInit(void);                                               /* 0EAE:0006 */
int16_t      cdiRegister(const CdiDevice *dev);                           /* 0EAE:0036 */
int32_t      cdiSetSample(int32_t ch, CdiSampleInfo *si);                 /* 0EAE:008F */
int32_t      cdiPlaySample(int32_t ch, uint32_t freq, uint32_t volume);   /* 0EAE:00B8 */
int32_t      cdiStopChannel(int32_t ch);                                  /* 0EAE:00E6 */
int32_t      cdiSetVolume(int32_t ch, uint32_t volume);                   /* 0EAE:010C */
int32_t      cdiSetFrequency(int32_t ch, uint32_t freq);                  /* 0EAE:0136 */
int32_t      cdiSetLinearRate(int32_t ch, uint32_t rate);                 /* 0EAE:0160 */
int32_t      cdiSetPosition(int32_t ch, uint32_t pos);                    /* 0EAE:018A */
int32_t      cdiSetPanning(int32_t ch, int32_t pan);                      /* 0EAE:01B4 */
int32_t      cdiSetMasterVolume(int32_t ch, uint32_t volume);             /* 0EAE:01DE */
int32_t      cdiMuteChannel(int32_t ch);                                  /* 0EAE:0204 */
int32_t      cdiUnmuteChannel(int32_t ch);                                /* 0EAE:022A */
int32_t      cdiMuteAll(int32_t ch);                                      /* 0EAE:0250 */
int32_t      cdiUnmuteAll(int32_t ch);                                    /* 0EAE:0272 */
int32_t      cdiDownloadSample(int32_t ch, uint8_t *sample, CdiSampleRef tag,
                               uint32_t length);                          /* 0EAE:0294 */
int32_t      cdiUnloadAll(int32_t ch);                                    /* 0EAE:02C2 */
int32_t      cdiUnloadSample(int32_t ch, CdiSampleRef tag);               /* 0EAE:02E4 */
int32_t      cdiGetVolume(int32_t ch);                                    /* 0EAE:030A */
int32_t      cdiGetFrequency(int32_t ch);                                 /* 0EAE:0330 */
int32_t      cdiGetPosition(int32_t ch);                                  /* 0EAE:0356 */
int32_t      cdiGetPanning(int32_t ch);                                   /* 0EAE:037C */
CdiSampleRef cdiGetSample(int32_t ch);                                    /* 0EAE:03A2 */
int32_t      cdiSetupChannels(int32_t ch, int32_t count);                 /* 0EAE:03C8 */
int32_t      cdiGetChannelStatus(int32_t ch);                             /* 0EAE:03EE */
void         cdiPoll(int32_t ch);                                         /* 0EAE:0414 */
int32_t      cdiGetStatus(int32_t ch, int32_t arg);                       /* 0EAE:043A */
int32_t      cdiSetEffect(int32_t ch, int32_t a, int32_t b);              /* 0EAE:0460 */

#endif
