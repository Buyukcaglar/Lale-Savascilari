/*
 * dsmitime.c - DSMI time base (IST.EXE seg1145, DSMI library).
 *
 * A 32-bit counter advanced by a Timer Service routine, with a nesting
 * "lock" that freezes the value GetDSMITime returns (the AMP player locks
 * it while it processes a tick, so all its reads see the same time).
 */
#include "dsmi/dsmitime.h"

#include "dsmi/dds.h"

/*
 * dsmiTimeInit - reset the time and publish the accessors through DDS.
 *
 * Registers "GetDSMITime", "LockDSMITime", "UnlockDSMITime" (DGROUP strings
 * 16E0:86C4/86D0/86DD).  The original never sets AX after the last call, so
 * the result is that of the third DDSRegisterFunction.
 *
 * Original: IST.EXE 1145:0004, far pascal (long step).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t dsmiTimeInit(int32_t step)
{
    dsmi_time_step = (uint32_t)step;
    dsmi_time = 0;
    dsmi_time_lock = 0;
    DDSRegisterFunction(DGSTR(0x86C4), (DdsFn)GetDSMITime);
    DDSRegisterFunction(DGSTR(0x86D0), (DdsFn)LockDSMITime);
    return DDSRegisterFunction(DGSTR(0x86DD), (DdsFn)UnlockDSMITime);
}

/*
 * GetDSMITime - the time, or the snapshot while locked.
 *
 * Original: IST.EXE 1145:005E, far; DX:AX.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
uint32_t GetDSMITime(void)
{
    if (dsmi_time_lock != 0)
        return dsmi_time_snapshot;
    return dsmi_time;
}

/*
 * dsmiTimeAdvance - time += step (a far-RET twin of dsmiTimeISR).
 *
 * Original: IST.EXE 1145:007F, far - unreferenced.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
void dsmiTimeAdvance(void)
{
    dsmi_time += dsmi_time_step;
}

/*
 * dsmiTimeISR - Timer Service routine: time += step.
 *
 * Original: IST.EXE 1145:0096, IRET routine called by tsTimerISR.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
void dsmiTimeISR(void)
{
    dsmi_time += dsmi_time_step;
}

/*
 * LockDSMITime - take the snapshot on the outermost lock; count nesting.
 *
 * Original: IST.EXE 1145:00BD, far.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
void LockDSMITime(void)
{
    if (dsmi_time_lock == 0)
        dsmi_time_snapshot = dsmi_time;
    dsmi_time_lock++;
}

/*
 * UnlockDSMITime - undo one lock (never below 0).
 *
 * Original: IST.EXE 1145:00DE, far.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
void UnlockDSMITime(void)
{
    if (dsmi_time_lock != 0)
        dsmi_time_lock--;
}
