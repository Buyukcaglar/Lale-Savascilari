/*
 * timer.c - DSMI Timer Service v2.0 ("(C) 1993-1995 Otto Chrons"),
 * IST.EXE seg10EF.
 *
 * One INT 08h handler serves up to 16 slots.  Slot 0 is the previous INT 08h
 * handler (the BIOS), installed at rate 0xFFFF.  The PIT is programmed with
 * the smallest rate (divisor) any slot asked for; every IRQ each active slot
 * adds its step to a 16-bit accumulator and its routine runs on the carry.
 *
 * Port: the PIT and the vector are the emulated PC's (pc_pit_set_divisor,
 * pc_set_int08); tsTimerISR is invoked by pc_poll().  The routine far
 * pointers live in the native ts_routine[]; the rest of the slot table stays
 * in DGROUP (timer.h).
 */
#include "dsmi/timer.h"

#include <stddef.h>

#include "platform/pc.h"

TsRoutine ts_routine[TS_SLOTS];            /* 16E0:85CE + 14*i, far ptrs */

/* byte-offset accessors for the asm's BX = slot * 14 (16-bit wrap kept) */
#define SLOT_RATE(bx)   DG16(0x85D2 + (bx))
#define SLOT_STEP(bx)   DG16(0x85D6 + (bx))
#define SLOT_ACC(bx)    DG16(0x85D8 + (bx))
#define SLOT_ACTIVE(bx) DG8(0x85DA + (bx))

/*
 * tsSetPIT - program PIT channel 0, mode 3, with a divisor.
 *
 * `out 43h,36h / out 40h,lo / out 40h,hi` under PUSHF/CLI.
 *
 * Original: IST.EXE 10EF:0036, near, AX = divisor.
 * Verified: tests/py/test_dsmi_infra.py (port writes recorded), 2026-11-20.
 */
static void tsSetPIT(uint16_t divisor)
{
    pc_pit_set_divisor(divisor);
}

/* Run the routine of slot i (PUSHF + CALL FAR to an IRET routine). */
static void call_slot(uint16_t i)
{
    /* The loop bound comes from ts_max_slot, which only tsAddRoutine and
       tsRemoveRoutine write (0..15); the check only guards the native table. */
    TsRoutine r = i < TS_SLOTS ? ts_routine[i] : NULL;
    if (r)
        r();
    else
        pc_bios_int08();                    /* the BIOS vector (see timer.h) */
}

/*
 * tsTimerISR - the INT 08h handler.
 *
 * EOI first (OUT 20h,20h - nothing to do in the port), then STI, then for
 * slots 0..ts_max_slot in order: if active, acc += step, and on carry run
 * the routine.  The loop count is read once before the loop; active/step
 * are re-read per slot, so a routine that changes a later slot is seen in
 * the same IRQ.
 *
 * The original enables interrupts while routines run, so a slow routine can
 * be interrupted by the next IRQ (nesting).  pc_poll() never nests handlers;
 * the game's routines are far shorter than a 10 ms tick.
 *
 * Original: IST.EXE 10EF:0048, interrupt handler (IRET).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential, call order
 * over many IRQs), 2026-11-20.
 */
void tsTimerISR(void)
{
    uint16_t cx = (uint16_t)(ts_max_slot + 1);
    uint16_t bx = 0;
    uint16_t i = 0;
    do {                                    /* LOOP: CX = 0 would mean 65536 */
        if (SLOT_ACTIVE(bx) == 1) {
            uint32_t sum = (uint32_t)SLOT_ACC(bx) + SLOT_STEP(bx);
            SLOT_ACC(bx) = (uint16_t)sum;
            if (sum > 0xFFFFu)
                call_slot(i);
        }
        bx = (uint16_t)(bx + TS_SLOT_SIZE);
        i++;
    } while (--cx != 0);
}

/*
 * tsInit - install the Timer Service.
 *
 * Clears the 16-slot table, sets master rate and slot 0 to 0xFFFF, programs
 * the PIT with 0xFFFF (not 0 = 65536: the BIOS rate changes very slightly),
 * saves the old INT 08h vector into slot 0, hooks INT 08h.
 * Returns 0, or -1 (AX = 0FFFFh) if already installed.
 *
 * Original: IST.EXE 10EF:007D, far.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t tsInit(void)
{
    if (ts_init_magic == 0xBE)
        return -1;
    ts_num_active = 1;
    ts_max_slot = 0;
    for (uint16_t k = 0; k < TS_SLOTS * TS_SLOT_SIZE; k++)   /* REP STOSB, 0E0h bytes */
        DG8(0x85CE + k) = 0;
    for (int k = 0; k < TS_SLOTS; k++)
        ts_routine[k] = NULL;
    ts_master_rate = 0xFFFF;
    ts_slot_rate(0) = 0xFFFF;
    ts_slot_step(0) = 0xFFFF;
    tsSetPIT(0xFFFF);
    ts_routine[0] = pc_get_int08();         /* INT 21h AX=3508h */
    ts_slot_active(0) = 1;
    pc_set_int08(tsTimerISR);               /* INT 21h AX=2508h */
    ts_init_magic = 0xBE;
    return 0;
}

/*
 * tsClose - restore the PIT and the old INT 08h vector.
 *
 * The PIT gets slot 0's rate (0xFFFF unless someone changed slot 0), written
 * twice as in the original.
 *
 * Original: IST.EXE 10EF:00DF, far.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
void tsClose(void)
{
    if (ts_init_magic != 0xBE)
        return;
    tsSetPIT(ts_slot_rate(0));
    tsSetPIT(ts_slot_rate(0));
    pc_set_int08(ts_routine[0]);            /* INT 21h AX=2508h from slot 0's pointer */
    ts_init_magic = 0;
}

/* Recompute every active slot's step for the current master rate and reset
   its accumulator (shared tail of tsChangeRoutine and tsSetTimerRate). */
static void recompute_steps(void)
{
    uint16_t cx = (uint16_t)(ts_max_slot + 1);
    uint16_t bx = 0;
    do {
        if (SLOT_ACTIVE(bx) == 1) {
            uint16_t master = ts_master_rate;
            uint16_t step = 0xFFFF;
            if (SLOT_RATE(bx) > master)     /* JBE skips the DIV */
                step = (uint16_t)((((uint32_t)master << 16) | 0xFFFFu) / SLOT_RATE(bx));
            SLOT_STEP(bx) = step;
            SLOT_ACC(bx) = 0xFFFF;
        }
        bx = (uint16_t)(bx + TS_SLOT_SIZE);
    } while (--cx != 0);
}

/*
 * tsChangeRoutine - set the rate of an active slot.
 *
 * If the rate is below the master rate, the master rate drops to it and the
 * PIT is reprogrammed (it is never raised again here).  Then all steps are
 * recomputed and all accumulators reset to 0xFFFF (so every slot fires on
 * its next IRQ).  The slot number is not range-checked (any value indexes
 * DGROUP); only an inactive slot is refused.  Returns 0, or -1.
 *
 * Original: IST.EXE 10EF:0110, far pascal (long slot, long rate) - only the
 * low words are used.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t tsChangeRoutine(int16_t slot, uint16_t rate)
{
    if (ts_init_magic != 0xBE)
        return -1;
    uint16_t bx = (uint16_t)(slot * TS_SLOT_SIZE);   /* IMUL EBX,[slot],0Eh; BX used */
    if (SLOT_ACTIVE(bx) != 1)
        return -1;
    SLOT_RATE(bx) = rate;
    if (rate < ts_master_rate) {            /* JNC: unsigned */
        ts_master_rate = rate;
        tsSetPIT(rate);
    }
    recompute_steps();
    return 0;
}

/*
 * tsAddRoutine - add a routine running at `rate` (PIT divisor).
 *
 * Takes the first free slot 1..15, raises ts_max_slot if needed, stores the
 * routine and rate and calls tsChangeRoutine (which resets every slot's
 * accumulator).  Returns the slot, -1 if not installed, -2 if full.
 *
 * Original: IST.EXE 10EF:0178, far pascal (void far *isr, long rate).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t tsAddRoutine(TsRoutine isr, uint16_t rate)
{
    if (ts_init_magic != 0xBE)
        return -1;
    if ((int8_t)ts_num_active >= 0x10)      /* JGE: signed byte */
        return -2;
    int16_t slot;
    for (slot = 1; slot <= 15; slot++)
        if (ts_slot_active(slot) == 0)
            break;
    if (slot > 15)
        return -2;
    if (slot > (int16_t)ts_max_slot)        /* JLE: signed */
        ts_max_slot = (uint16_t)slot;
    ts_routine[slot] = isr;
    ts_slot_active(slot) = 1;
    DG8(0x85DB + slot * TS_SLOT_SIZE) = 0;
    ts_slot_rate(slot) = rate;
    if (tsChangeRoutine(slot, rate) != 0) { /* cannot fail: the slot is active */
        ts_slot_active(slot) = 0;
        return -1;
    }
    ts_num_active++;
    return slot;
}

/*
 * tsRemoveRoutine - deactivate slot 1..15.
 *
 * Recomputes ts_max_slot as the highest active slot.  The master rate and
 * the other slots' steps are NOT recomputed (the PIT keeps running at the
 * removed routine's rate if it was the fastest), and the routine pointer is
 * left in place.  Returns 0, -1 if not installed, -2 for a bad/inactive slot.
 *
 * Original: IST.EXE 10EF:0206, far pascal (long slot) - unreferenced.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t tsRemoveRoutine(int16_t slot)
{
    if (ts_init_magic != 0xBE)
        return -1;
    if (slot < 1 || slot > 15)
        return -2;
    if (ts_slot_active(slot) != 1)
        return -2;
    ts_slot_active(slot) = 0;
    ts_num_active--;
    for (uint16_t i = 0; i < TS_SLOTS; i++)
        if (ts_slot_active(i) == 1)
            ts_max_slot = i;
    return 0;
}

/*
 * tsSetTimerRate - force the master rate (PIT divisor) and recompute steps.
 *
 * Slots whose rate is at or below the new master rate get step 0xFFFF
 * (they run on all but one of 65536 IRQs).  Returns 0, or -1.
 *
 * Original: IST.EXE 10EF:0263, far pascal (long divisor) - unreferenced.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t tsSetTimerRate(uint16_t divisor)
{
    if (ts_init_magic != 0xBE)
        return -1;
    ts_master_rate = divisor;
    tsSetPIT(divisor);
    recompute_steps();
    return 0;
}

/*
 * tsGetTimerRate - the master rate (no installed check).
 *
 * Original: IST.EXE 10EF:02B5, far; returns MOVZX EAX (DX untouched).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
uint16_t tsGetTimerRate(void)
{
    return ts_master_rate;
}
