/*
 * nses.h - ALTDUZEY\NSES.L: the resident part of the game's sound code.
 * Original: IST.EXE seg0D42 (Borland C + inline asm).  Implementation:
 * src/engine/nses.c.  Set-up (SES.CFG, driver, timers) is OSES (oses.h).
 *
 *   - DSMI service hooks that OSES registers through DDS:
 *       "MCPSampleRealAddress"     snd_ems_map_sample   (EMS sample -> address)
 *       "MCPEnableVirtualSamples"  snd_ems_save_map     (EMS map save)
 *       "MCPDisableVirtualSamples" snd_ems_restore_map  (EMS map restore)
 *       "GetEnvString"             snd_getenv_hook
 *       "SetVector"                snd_setvect_hook
 *   - two Timer Service routines (ISRs in the original, run from pc_poll()
 *     in the port):
 *       snd_timer_40hz   divisor 7485h (40.0 Hz): cdiPoll -> the MCP mixer
 *       snd_timer_100hz  divisor 2E9Bh (100 Hz):  ampInterrupt (AMP player)
 *   - the sample slot table and the functions that load/play from it.
 *
 * Sample slot table (DGROUP DE34, SND_SLOTS records of 0x17 bytes):
 *     +00 far MmHandle *  MEMMAN block holding the sample (mm_ref)
 *     +04 SAMPLEINFO      the DSMI SAMPLEINFO (cdi.h CdiSampleInfo) handed
 *                         to cdiSetSample:  +04 sample far ptr (FFFF:handle
 *                         for EMS), +08 length, +0C loopstart, +10 loopend,
 *                         +14 mode, +15 sampleID (EMS handle).
 * SAMPLEINFO is 0x1A bytes but the stride is only 0x17: its rate/freq/
 * volume fields (+13..+19) are the first 7 bytes of the NEXT slot (its
 * MmHandle pointer and the low 3 bytes of its sample pointer).  The loaders
 * clear only 0x13 bytes.  The MCP mixer never reads those fields, so the
 * overlap is harmless; the port hands the same bytes on (snd_sampleinfo).
 *
 * Conventional samples: the original stores the data's far pointer at +04;
 * the port stores the 4 bytes the original copies (the MmHandle record's
 * first dword, unused by MEMMAN in the port) and keeps the native pointer
 * in snd_slot_data[slot].  EMS samples (FFFF:handle) live entirely in dg[].
 */
#ifndef LALE_ENGINE_NSES_H
#define LALE_ENGINE_NSES_H

#include <stdint.h>

#include "dsmi/cdi.h"
#include "engine/dgroup.h"
#include "engine/memman.h"

/* ---- DGROUP ------------------------------------------------------------- */
/* g_sound_on (73D0) is defined in memman.h */
#define g_snd_channels     DG8(0x73D7)    /* u8   channels for cdiSetupChannels (8); the
                                             last one plays snd_load_sound's stream */
#define g_snd_poll_busy    DG8(0x73D8)    /* u8   snd_timer_40hz re-entry guard */
#define g_snd_amp_busy     DG8(0x73DB)    /* u8   snd_timer_100hz re-entry guard */
#define SND_STR_PLAY_ERR   0x73DE         /* "İc hata!\n%d. kanaldan %d. s_membuffer'daki ..." */
#define SND_STR_FILE       0x743A         /* "T:ALTDUZEY\NSES.L" (one copy per call site,
                                             743A 7479 748B 749D 74AF 74C1 74D3 74E5 74F7) */
#define SND_STR_NOTFOUND   0x744C         /* "%s. ses dosyası bulunamadı! (playlongsample)" */

#define SND_SLOTS          0x31           /* slots 0..30h (30h = snd_load_sound's stream) */
#define SND_SLOT_SIZE      0x17
#define SND_SLOT_BASE      0xDE34
#define SND_SLOT_MM_REF(n) ((uint16_t)(SND_SLOT_BASE + (n) * SND_SLOT_SIZE))  /* mm_ref() ref */
#define SND_SI_OFF(n)      ((uint16_t)(0xDE38 + (n) * SND_SLOT_SIZE))         /* SAMPLEINFO */
#define g_snd_si_ptr_off(n)  DG16(SND_SI_OFF(n) + 0x00)   /* sample far ptr offset / EMS handle */
#define g_snd_si_ptr_seg(n)  DG16(SND_SI_OFF(n) + 0x02)   /* segment, FFFFh = EMS sample */
#define g_snd_si_length(n)   DG32(SND_SI_OFF(n) + 0x04)
#define g_snd_si_loopstart(n) DG32(SND_SI_OFF(n) + 0x08)
#define g_snd_si_loopend(n)  DG32(SND_SI_OFF(n) + 0x0C)
#define g_snd_si_mode(n)     DG8(SND_SI_OFF(n) + 0x10)
#define g_snd_si_id(n)       DG16(SND_SI_OFF(n) + 0x11)
#define SND_SI_CLEAR_BYTES   0x13         /* what the loaders memset */

/* Native halves of the conventional-memory sample pointers (+04 when the
   segment is not FFFFh).  Cleared together with the SAMPLEINFO head. */
extern uint8_t *snd_slot_data[SND_SLOTS];

/* Port: the CdiSampleInfo for slot n, built from the dg[] bytes (+ the
   native data pointer).  The pointer stays valid until the next call for
   the same slot. */
CdiSampleInfo *snd_sampleinfo(int16_t slot);

/* Port helper: clear the SAMPLEINFO head of a slot (memset 0x13 bytes) and
   its native data pointer. */
void snd_slot_clear(int16_t slot);


/* ---- functions (original address order) ---------------------------------- */
uint8_t *snd_ems_map_sample(uint32_t handle, uint32_t pos);      /* 0D42:0005 */
void     snd_ems_save_map(void);                                 /* 0D42:0041 */
void     snd_ems_restore_map(void);                              /* 0D42:005E */
uint32_t snd_identity_addr(uint32_t a, uint32_t b);              /* 0D42:007B unreferenced */
void     snd_timer_40hz(void);                                   /* 0D42:0093 */
const char *snd_getenv_hook(const char *name);                   /* 0D42:00CF */
void    *snd_setvect_hook(int32_t intno, void *isr);             /* 0D42:00EB */
void     snd_timer_100hz(void);                                  /* 0D42:0120 */
void     snd_play_sample(int16_t slot, int32_t freq, int32_t volume, int32_t pan,
                         uint8_t ch);                            /* 0D42:014F */
void     snd_load_sound(const char *name, int32_t freq, int32_t volume, int32_t pan,
                        uint8_t ch);                             /* 0D42:01F0 */
int16_t  snd_load_sample_ems(uint8_t type, int16_t slot, int16_t count,
                             int16_t unit);                      /* 0D42:044D */

/* Tests: forget native state (vectors, slot pointers). */
void nses_reset(void);

#endif
