/*
 * oses.h - ALTDUZEY\OSES.L: sound set-up and sample loading (overlay).
 * Original: IST.EXE ovl55CC (Borland C, far cdecl).  Implementation:
 * src/engine/oses.c.  The resident half is NSES (nses.h).
 *
 * oses_init reads SES.CFG (docs/FORMATS.md), starts the DSMI time base,
 * registers NSES's hooks with DDS, loads the configured .DSD driver with
 * DDSLoadDriver and calls its SDIInitDevice/SDIStartOutput, starts AMP and
 * adds the NSES timer routines.
 *
 * Port: the .DSD's real-mode code cannot run; oses_init registers the
 * native driver for SDISB16.DSD (src/dsmi/mcp.h sdisb16_entry, the MCP
 * software mixer) with dds_register_native_driver() before the load, so
 * DDSLoadDriver still reads and checks the real file and the MCP registers
 * itself as the CDI device inside SDIInitDevice - where the original
 * driver did.  The other drivers (GUS/PAS/WSS) have no native version:
 * DDSLoadDriver fails with error 5 and oses_init returns 3 (no sound), as
 * the original does when a driver's init fails.  After SDIStartOutput the
 * port starts the emulated DMA (platform/audio.h audio_dma_start).
 */
#ifndef LALE_ENGINE_OSES_H
#define LALE_ENGINE_OSES_H

#include <stdint.h>

#include "engine/dgroup.h"

/* ---- DGROUP ------------------------------------------------------------- */
/* 22EA + n*11h: a sound table of 17-byte records {u16 flags; char name[13];
   u16 rate} ("adim1.pur", 4000 Hz, ...).  oses_load_sample sets flag 100h
   (loaded reversed) and 200h (EMS, continued in the next slot; cleared
   again on the last slot it fills). */
#define g_snd_table_flags(n) DG16((uint16_t)(0x22EA + (n) * 0x11))
#define OSES_STR_CFG        0x8DDC         /* "Ses.CFG" */
#define g_ses_cfg_tail      DG16(0x8DDA)   /* u16  SES.CFG bytes 105-106 (default 1, file: 2),
                                              read by main and the GFL player */
/* 8DD6: far ptr SDIStopOutput (o559A_fatal calls it) -> oses_sdi_stop_output */
#define g_sdi_config        DGP(0xE7EC)    /* u8[5Ch] SDI config record from SES.CFG */
#define g_sdi_flags         DG16(0xE7F0)   /* u16  SDI config +04 low word: bit 7 = poll timer */
/* E848: far ptr SDIInitDevice, E84C: far ptr SDIStartOutput -> native below */
#define g_oses_driver       DG32(0xE85C)   /* u32  DDS handle of the loaded driver */

typedef int16_t (*OsesSdiInitFn)(const uint8_t *sdi_cfg);
typedef int16_t (*OsesSdiVoidFn)(void);
extern OsesSdiInitFn oses_sdi_init_device;   /* 16E0:E848 far ptr */
extern OsesSdiVoidFn oses_sdi_start_output;  /* 16E0:E84C far ptr */
extern OsesSdiVoidFn oses_sdi_stop_output;   /* 16E0:8DD6 far ptr */

/* probe != 0: only read SES.CFG (returns EEh when it holds a driver).
   Returns FFh (set up, or no sound configured), 1 (no SES.CFG), 3 (a DSMI
   step failed). */
int16_t oses_init(int16_t probe);                                     /* 55CC:0000 */
/* Load a sample file into slot (and, with flags bit 1, the following slots
   in FF00h-byte EMS pieces).  type 0 conventional / 1 EMS; flags bit 0:
   store it reversed.  Returns 0, 1 (cannot open), 2 (out of memory),
   3 (read error). */
int16_t oses_load_sample(uint8_t type, const char *name, int16_t slot, uint8_t flags,
                         int32_t loopstart, int32_t loopend);         /* 55CC:01F3 */

#endif
