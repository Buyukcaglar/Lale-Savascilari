/*
 * ems.c - DSMI raw EMS calls (IST.EXE seg1134, hand-written assembly).
 *
 * Each function is a thin INT 67h wrapper; here INT 67h is pc_int67() and
 * the move function is pc_ems_move().  Register values that the original
 * leaves in AX by accident are reproduced (see EMS_RT_DGROUP in ems.h).
 */
#include "dsmi/ems.h"

static uint16_t int67(uint16_t ax, uint16_t bx, uint16_t dx, PcEmsRegs *out)
{
    PcEmsRegs r = { ax, bx, 0, dx };
    pc_int67(&r);
    if (out)
        *out = r;
    return r.ax;
}

/*
 * emsInit - check for an EMS 4.0 driver and remember the page frame.
 *
 * Original: IST.EXE 1134:000E (DSMI EMS), far, no arguments.
 * Returns 0, or -2 when a call fails or the version is below 4.0.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int16_t emsInit(void)
{
    PcEmsRegs r;
    if (int67(0x4000, 0, 0, &r) >> 8)                 /* get status */
        return -2;
    if (int67(0x4100, 0, 0, &r) >> 8)                 /* page frame segment */
        return -2;
    g_ems_frame_seg = r.bx;
    int67(0x4600, r.bx, r.dx, &r);                    /* version: AL = BCD */
    if ((uint8_t)r.ax < 0x40)                         /* jc: below 4.0 */
        return -2;
    g_ems_handle = 0xFFFF;
    g_ems_pages = 0;
    g_ems_ready = 1;
    return 0;
}

/*
 * emsAllocPages - allocate the library's EMS handle with `pages` pages.
 *
 * Original: IST.EXE 1134:0049 (DSMI EMS), far pascal (pages).
 * Returns the handle; on failure AX has AL = FFh and AH = the EMM status
 * (or the runtime DGROUP high byte when not initialised / pages == 0).
 * g_ems_pages is set before the call, even when it fails.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int16_t emsAllocPages(uint16_t pages)
{
    PcEmsRegs r;
    if (g_ems_ready != 1 || pages == 0)
        return (int16_t)((EMS_RT_DGROUP & 0xFF00u) | 0xFFu);
    g_ems_pages = pages;
    uint16_t ax = int67(0x4300, pages, 0, &r);
    if (ax >> 8)
        return (int16_t)((ax & 0xFF00u) | 0xFFu);
    g_ems_handle = r.dx;
    return (int16_t)r.dx;
}

/*
 * emsMapPage - map logical page `logical` of the library's handle into
 * physical page `physical` (0..3, masked with 3).
 *
 * Original: IST.EXE 1134:007A (DSMI EMS), far pascal (logical, physical).
 * Returns 0 - even if the EMM reports an error -, -3 when logical >=
 * g_ems_pages, or (DGROUP hi << 8 | FFh) when not initialised.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
int16_t emsMapPage(uint16_t logical, uint16_t physical)
{
    if (g_ems_ready != 1)
        return (int16_t)((EMS_RT_DGROUP & 0xFF00u) | 0xFFu);
    if (logical >= g_ems_pages)                       /* unsigned: jnc */
        return -3;
    int67((uint16_t)(0x4400 | (physical & 3)), logical, g_ems_handle, NULL);
    return 0;                                         /* status ignored (SUB AX,AX) */
}

/*
 * emsSavePageMap - INT 67h/47h for the library's handle.
 *
 * Original: IST.EXE 1134:00AC (DSMI EMS), far.  Returns AX as the driver
 * leaves it: AH = status, AL = low byte of the runtime DGROUP segment.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
uint16_t emsSavePageMap(void)
{
    return int67((uint16_t)(0x4700 | (EMS_RT_DGROUP & 0xFF)), 0, g_ems_handle, NULL);
}

/*
 * emsRestorePageMap - INT 67h/48h for the library's handle.
 *
 * Original: IST.EXE 1134:00BA (DSMI EMS), far.  Returns AX (see above).
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
uint16_t emsRestorePageMap(void)
{
    return int67((uint16_t)(0x4800 | (EMS_RT_DGROUP & 0xFF)), 0, g_ems_handle, NULL);
}

/*
 * emsGetFreePages - number of unallocated EMS pages (INT 67h/42h).
 *
 * Original: IST.EXE 1134:00C8 (DSMI EMS), far.  FFFFh on error.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
uint16_t emsGetFreePages(void)
{
    PcEmsRegs r;
    if (int67(0x4200, 0, 0, &r) >> 8)
        return 0xFFFF;
    return r.bx;
}

/*
 * emsFreeHandle - release the library's handle (INT 67h/45h); the library
 * is uninitialised afterwards whatever the driver says.
 *
 * Original: IST.EXE 1134:00DC (DSMI EMS), far.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
void emsFreeHandle(void)
{
    int67(0x4500, 0, g_ems_handle, NULL);
    g_ems_ready = 0;
    g_ems_handle = 0xFFFF;
}

/*
 * emsGetPageFrame - page-frame segment stored by emsInit.
 *
 * Original: IST.EXE 1134:00F6 (DSMI EMS), far.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
uint16_t emsGetPageFrame(void)
{
    return g_ems_frame_seg;
}

/*
 * emsMoveMemory - INT 67h AX=5700h with the descriptor *m.
 *
 * Original: IST.EXE 1134:00FF (DSMI EMS), far pascal (far *moveStruct).
 * Returns AX: AH = EMM status, AL = 0.
 * Verified: tests/py/test_ems.py (unicorn differential), 2026-11-20.
 */
uint16_t emsMoveMemory(const EmsMove *m)
{
    return (uint16_t)(pc_ems_move(m) << 8);
}
