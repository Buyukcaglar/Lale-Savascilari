/*
 * timer.h - DSMI Timer Service 2.0: one INT 08h handler multiplexing up to 15
 * routines at their own PIT rates.  Original: IST.EXE seg10EF (Pascal).
 * Implementation: src/dsmi/timer.c.  Routines are plain C functions run from
 * pc_poll() (see src/platform/pc.h).
 *
 * Rates are PIT divisors (1193182 / Hz; the game uses 11931 = 100 Hz).  The
 * PIT runs at the smallest divisor asked for ("master rate"); each slot adds
 * step = 65536 * master / rate (approx.) to a 16-bit accumulator per IRQ and
 * runs its routine on carry.  Slot 0 is the previous INT 08h handler.
 */
#ifndef LALE_DSMI_TIMER_H
#define LALE_DSMI_TIMER_H

#include <stdint.h>

#include "engine/dgroup.h"

typedef void (*TsRoutine)(void);

/* ---- DGROUP ------------------------------------------------------------ */
/* 16 slots of 14 bytes at 16E0:85CE:
     +0 far ptr routine (port: ts_routine[] below; these 4 bytes stay 0)
     +4 u16 rate (PIT divisor)   +6 u16 unused
     +8 u16 step                 +A u16 accumulator
     +C u8  active (1)           +D u8  cleared by tsAddRoutine, never read */
#define TS_SLOT_SIZE      14
#define TS_SLOTS          16
#define ts_slot_rate(i)   DG16(0x85D2 + (i) * TS_SLOT_SIZE)
#define ts_slot_step(i)   DG16(0x85D6 + (i) * TS_SLOT_SIZE)
#define ts_slot_acc(i)    DG16(0x85D8 + (i) * TS_SLOT_SIZE)
#define ts_slot_active(i) DG8(0x85DA + (i) * TS_SLOT_SIZE)
#define ts_max_slot       DG16(0x86AE)   /* u16 highest active slot index */
#define ts_num_active     DG8(0x86B0)    /* u8  active slots incl. slot 0 */
#define ts_init_magic     DG8(0x86B1)    /* u8  0xBE while installed */
#define ts_master_rate    DG16(0x86B2)   /* u16 PIT divisor programmed */

/* Native part of the slot table (16E0:85CE + 14*i, far ptr).  A NULL entry
   means "the BIOS handler" (pc_bios_int08), as with pc_get_int08(). */
extern TsRoutine ts_routine[TS_SLOTS];

void     tsTimerISR(void);                                 /* 10EF:0048  INT 08h handler */
int16_t  tsInit(void);                                     /* 10EF:007D  0 ok, -1 already */
void     tsClose(void);                                    /* 10EF:00DF */
int16_t  tsChangeRoutine(int16_t slot, uint16_t rate);     /* 10EF:0110  0 ok, -1 */
int16_t  tsAddRoutine(TsRoutine isr, uint16_t rate);       /* 10EF:0178  slot 1..15, -1 or -2 */
int16_t  tsRemoveRoutine(int16_t slot);                    /* 10EF:0206  0 ok, -1 or -2 */
int16_t  tsSetTimerRate(uint16_t divisor);                 /* 10EF:0263  0 ok, -1 */
uint16_t tsGetTimerRate(void);                             /* 10EF:02B5 */

#endif
