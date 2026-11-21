/*
 * dsmitime.h - DSMI time base advanced by a Timer Service routine.
 * Original: IST.EXE seg1145.  Implementation: src/dsmi/dsmitime.c.
 *
 * The game calls dsmiTimeInit(100) and adds dsmiTimeISR with
 * tsAddRoutine(dsmiTimeISR, 11931) (100 Hz), so the time counts in units
 * of 0.1 ms (+100 per 10 ms tick).
 */
#ifndef LALE_DSMI_DSMITIME_H
#define LALE_DSMI_DSMITIME_H

#include <stdint.h>

#include "engine/dgroup.h"

#define dsmi_time_lock      DG16(0xE796)   /* u16 LockDSMITime nesting count */
#define dsmi_time_snapshot  DG32(0xE798)   /* u32 time at the outermost lock */
#define dsmi_time_step      DG32(0xE79C)   /* u32 added per routine call */
#define dsmi_time           DG32(0xE7A0)   /* u32 the running time */

int16_t  dsmiTimeInit(int32_t step);   /* 1145:0004  returns the last DDSRegisterFunction result */
uint32_t GetDSMITime(void);            /* 1145:005E */
void     dsmiTimeAdvance(void);        /* 1145:007F  unreferenced */
void     dsmiTimeISR(void);            /* 1145:0096  Timer Service routine */
void     LockDSMITime(void);           /* 1145:00BD */
void     UnlockDSMITime(void);         /* 1145:00DE */

#endif
