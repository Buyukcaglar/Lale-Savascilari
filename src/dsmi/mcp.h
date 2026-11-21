/*
 * mcp.h - DSMI MCP v3.5 software mixer ("MCP v3.5 - (C) 1992,1995 Otto
 * Chrons"), decompiled from the driver file SDISB16.DSD (image offsets
 * 1675..36EF; addresses below are SDISB16.DSD image offsets, code segment
 * 0000, data segment 03D3 = image +3D30).  The SB16 hardware part of the
 * driver (DSP/DMA/IRQ programming) is not decompiled: the port's audio
 * backend plays mcp_dma_buffer like the DMA controller would.
 * Analysis: docs/DSMI_MCP.md.  Implementation: src/dsmi/mcp.c.
 *
 * How the original produces sound (the port must drive it the same way):
 *   - The CDI calls setSample/playSample/setVolume/setFrequency/setPosition/
 *     setPanning are NOT executed at once: they are queued with the current
 *     GetDSMITime() (0.1 ms units).  stop/mute/unmute/setMasterVolume and
 *     setupChannels act immediately.
 *   - poll (CDI slot 5C, IST.EXE calls it from a 40 Hz timer routine) reads
 *     the DMA play position (SDIGetBufferPosition, bytes into the ring) and
 *     mixes from its own write position up to it, applying the queued
 *     commands at the sample frame that corresponds to their time stamp.
 *     The mixed data is one DMA ring (8016 bytes = 2004 frames at 22050 Hz
 *     16-bit stereo, ~91 ms) ahead of what is being played.
 *   - Identical output needs: the same commands with the same time stamps,
 *     polls at the same DSMI times seeing the same DMA positions.  Then
 *     mcp_dma_buffer holds bit-identical data.
 *
 * Output of the shipped SES.CFG: SB16, 22050 Hz, stereo, signed 16-bit,
 * amplification "Yok" (none) -> options 0: channels are added directly into
 * the DMA ring with 16-bit wrap-around (no clipping).
 */
#ifndef LALE_DSMI_MCP_H
#define LALE_DSMI_MCP_H

#include <stdint.h>

#include "dsmi/cdi.h"
#include "dsmi/dds.h"

/* ---- configuration ------------------------------------------------------ */

/* MCP options word (driver 0870 / 0A00) */
#define MCP_OPT_ACCUMULATE  0x0001  /* mix into a 16/32-bit accumulator, then clip/convert
                                       (forced for 8-bit output) */
#define MCP_OPT_AMP_MASK    0x0030  /* amplification 100/200/350/500 % (table 1725) */
#define MCP_OPT_AUTO_AMP    0x0040  /* amplification from channel count (table 172D) */

typedef struct McpConfig {      /* the 12-byte MCP init record (driver 31BC -> 0870) */
    uint16_t options;           /* +0 MCP_OPT_* */
    uint8_t  stereo;            /* +A 1 = stereo */
    uint8_t  sample_size;       /* +B bytes per output sample: 1 (unsigned 8-bit) or 2 (signed 16) */
    uint16_t rate;              /* MCPOutputBuffer +0E (driver 088A): mixing rate, Hz */
    uint16_t buffer_size;       /* MCPOutputBuffer +0A (driver 0886): DMA ring bytes */
} McpConfig;

/* Derive the MCP configuration from the 0x5C-byte SDI config record (SES.CFG
 * bytes 13..104, DGROUP 16E0:E7EC) like SDISB16 initDevice (08F6) does for
 * card types 1 (SB 2.x, DSP >= 2.01 assumed), 2 (SB Pro) and 6 (SB16).
 * Returns 0, or -1 for an unknown card type (the original returns -1 too). */
int16_t mcp_config_from_sdi(const uint8_t *sdi_cfg, McpConfig *out);

/* ---- the mixer ------------------------------------------------------------ */

int16_t mcpInit(const McpConfig *cfg);                 /* 2A2E */
int16_t mcpClose(void);                                /* 2B9A */
int16_t mcpStartVoice(void);                           /* 2DEA  (start output) */
int16_t mcpStopVoice(void);                            /* 2E3E */
uint16_t mcpGetMixRate(void);                          /* 34F4 (not in the CDI table) */
int16_t mcpGetChannelCount(void);                      /* 2F76 (not in the CDI table) */

/* CDI slots (cdi.h CdiDevice signatures) */
int32_t mcpSetSample(int32_t ch, CdiSampleInfo *si);              /* 00: 3189 */
int32_t mcpPlaySample(int32_t ch, uint32_t freq, uint32_t volume);/* 04: 327A */
int32_t mcpSetVolume(int32_t ch, uint32_t volume);                /* 08: 334B */
int32_t mcpSetFrequency(int32_t ch, uint32_t freq);               /* 0C: 33A2 */
int32_t mcpSetLinearRate(int32_t ch, uint32_t rate);              /* 10: 169D (no-op) */
int32_t mcpSetPosition(int32_t ch, uint32_t pos);                 /* 14: 3431 */
int32_t mcpSetPanning(int32_t ch, int32_t pan);                   /* 18: 34A0 */
int32_t mcpSetMasterVolume(uint32_t volume);                      /* 1C: 34FF */
int32_t mcpMuteChannel(int32_t ch);                               /* 20: 2E63 */
int32_t mcpUnmuteChannel(int32_t ch);                             /* 24: 2EA0 */
int32_t mcpStopChannel(int32_t ch);                               /* 28: 2EDD */
int32_t mcpMuteAll(void);                                         /* 2C: 2F1A */
int32_t mcpUnmuteAll(void);                                       /* 30: 2F48 */
int32_t mcpDownloadSample(uint8_t *sample, CdiSampleRef tag, uint32_t length); /* 34: 3677 */
int32_t mcpUnloadSample(CdiSampleRef tag);                        /* 38: 3682 */
int32_t mcpUnloadAll(void);                                       /* 3C: 3681 */
int32_t mcpGetVolume(int32_t ch);                                 /* 40: 3526 */
int32_t mcpGetFrequency(int32_t ch);                              /* 44: 35D5 */
int32_t mcpGetPosition(int32_t ch);                               /* 48: 3559 */
int32_t mcpGetPanning(int32_t ch);                                /* 4C: 363A */
CdiSampleRef mcpGetSample(int32_t ch);                            /* 50: 358C */
int32_t mcpSetupChannels(int32_t count);                          /* 54: 2D2C */
int32_t mcpGetChannelStatus(int32_t ch);                          /* 58: 2F8B */
void    mcpPoll(void);                                            /* 5C: 3082 */
int32_t mcpSetEffect(int32_t a, int32_t b);                       /* 60: 368A */
int32_t mcpGetStatus(int32_t arg);                                /* 64: 36B1 */

/* Fill a CDI device table with the 26 slots above (the driver's table at
 * data 088F). */
void mcp_fill_cdi_device(CdiDevice *dev);

/* ---- imports (the original pulls them by name through DDS, mcpOpenDDS 001C) */
/* Native signatures the MCP expects behind these DDS names:              */
typedef uint32_t (*McpTimeFn)(void);                        /* "GetDSMITime"            */
typedef uint8_t *(*McpRealAddressFn)(uint32_t handle, uint32_t pos);
                                       /* "MCPSampleRealAddress": pointer to the START of
                                          the virtual (EMS) sample, NULL = not available.
                                          IST.EXE ignores pos and returns EMSLock(handle). */
typedef void     (*McpVoidFn)(void);                        /* "MCPEnable/DisableVirtualSamples" */
typedef uint16_t (*McpBufPosFn)(void);                      /* "SDIGetBufferPosition"   */

/* Resolve the imports with DDSAccessFunction (as mcpOpenDDS).  0 = ok, 5 = a
 * required one is missing (GetDSMITime, MCPDisableVirtualSamples or
 * SDIGetBufferPosition - the original only checks those three). */
int16_t mcpOpenDDS(DdsAccessFn access);
void    mcp_set_imports(McpTimeFn time, McpRealAddressFn real, McpVoidFn enable,
                        McpVoidFn disable, McpBufPosFn bufpos);   /* tests / direct use */

/* Original far-pointer offset of a sample pointer, used to reproduce the
 * 16-bit offset wrap of the mixer's source pointer (DS.md "segment wrap").
 * Default: offset inside pc_ems_frame for EMS-mapped samples, else 0. */
extern uint16_t (*mcp_far_offset_hook)(const uint8_t *p);

/* Optional post-processing hook (setEffect in the original: a far function
 * called after each mixed block with (buffer, frames, format) where format =
 * (sample_size >> 1) | (stereo << 1)). */
typedef void (*McpEffectFn)(uint8_t *buf, uint32_t frames, uint16_t format);
void mcp_set_effect_fn(McpEffectFn fn);

void mcp_reset(void);                                   /* port: load-time state */
void mcp_channel_state(int ch, uint32_t out[8]);       /* port/tests: raw channel fields */

/* ---- output --------------------------------------------------------------- */

/* The DMA ring (MCPOutputBuffer data), buffer_size bytes are used; the
 * array is 64 KB like the segment the original's 16-bit offsets live in. */
extern uint8_t mcp_dma_buffer[0x10000 + 16];
uint16_t mcp_buffer_size(void);         /* 08F7 */
uint16_t mcp_mix_position(void);        /* 0A23: next byte the mixer writes */

/* ---- the SDISB16.DSD driver shell (native DDS driver) --------------------- */

/* DMA play position in bytes (0 .. buffer_size-1); the audio backend that
 * plays mcp_dma_buffer keeps it current.  SDIGetBufferPosition returns it. */
extern volatile uint16_t mcp_dma_position;

/* DDS driver entry for "SDISB16.DSD": register with
 * dds_register_native_driver("SDISB16.DSD", sdisb16_entry).  cmd 1 imports
 * the DDS functions, registers SDIDetectDevices .. SDICloseDevice and the
 * MCPOutputBuffer resource, returns device id 0x64. */
uint32_t sdisb16_entry(int32_t cmd, DdsAccessFn access, int32_t arg2);

int16_t SDIInitDevice(const uint8_t *sdi_cfg);   /* 08F6 (mixer part only) */
int16_t SDIStartOutput(void);                    /* 105C */
int16_t SDIStopOutput(void);                     /* 13A0 */
int16_t SDICloseDevice(void);                    /* 140A */
uint16_t SDIGetBufferPosition(void);             /* 1382 */

#endif
