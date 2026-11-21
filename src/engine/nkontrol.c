/*
 * nkontrol.c - ALTDUZEY\NKONTROL.L (IST.EXE seg0DB7).  See nkontrol.h for
 * how the interrupt hooks map onto the port.
 */
#include "engine/nkontrol.h"

#include "dsmi/dds.h"
#include "dsmi/timer.h"
#include "platform/pc.h"

/*
 * ctl_ctrlbrk_handler - DOS critical-error (harderr) handler: the first 3
 * errors answer 1 (retry), the 4th answers 0 (ignore) and resets the count.
 *
 * Original: IST.EXE 0DB7:0009 (NKONTROL.L), far cdecl, installed with
 * harderr() by ctl_install.  Never called in the port (no INT 24h).
 * Verified: tests/py/test_nkontrol.py (unicorn differential), 2026-11-20.
 */
int16_t ctl_ctrlbrk_handler(void)
{
    if (g_harderr_count < 3) {
        g_harderr_count++;
        return 1;
    }
    g_harderr_count = 0;
    return 0;
}

/*
 * ctl_install - hook INT 23h, 09h and 00h and the critical-error handler.
 *
 * Original: IST.EXE 0DB7:0026 (NKONTROL.L), far.  Installing twice is fatal.
 * Port: getvect/setvect/harderr are no-ops (see nkontrol.h); only the
 * "installed" flag (g_sys_flags bit 4) is kept.
 * Verified: tests/py/test_nkontrol.py (flag only), 2026-11-20.
 */
void ctl_install(void)
{
    if (g_sys_flags & 0x10)
        o559A_fatal(DGSTR(0x7529), 0x11C, NULL, 0x1A);
    /* E73E = getvect(23h); setvect(23h, ctl_int23_isr);
       E746 = getvect(09h); setvect(09h, ctl_int09_isr);
       E742 = getvect(00h); setvect(00h, ctl_div0_isr);
       harderr(ctl_ctrlbrk_handler); */
    g_sys_flags |= 0x10;
}

/*
 * ctl_uninstall - restore the three vectors (if installed).
 *
 * Original: IST.EXE 0DB7:00B3 (NKONTROL.L), far, called by o559A_fatal.
 * Verified: tests/py/test_nkontrol.py (flag only), 2026-11-20.
 */
void ctl_uninstall(void)
{
    /* if installed: setvect(00h, E742); setvect(09h, E746); setvect(23h, E73E) */
    g_sys_flags &= (uint8_t)~0x10;
}

int ctl_installed(void)
{
    return (g_sys_flags & 0x10) != 0;
}

/*
 * ctl_div0_isr - INT 00h handler: AX = EAX = FFFFh, resume at faulting
 * IP + 3 (assumes a 3-byte DIV/IDIV).
 *
 * Original: IST.EXE 0DB7:00F6 (NKONTROL.L), interrupt handler.
 * Port: a CPU fault cannot be resumed; division sites use nk_div_u16 /
 * nk_idiv_s16 / nk_div_u8 (nkontrol.h).  This function only returns the
 * AX value the handler produces.
 * Verified: no (an interrupt handler; semantics documented in nkontrol.h).
 */
uint16_t ctl_div0_isr(void)
{
    return (uint16_t)NK_DIV0_RESULT;
}

/*
 * ctl_int23_isr - Ctrl-C / Ctrl-Break: fatal error with "Hımm, birisi
 * bilgisayarı resetlemeye çalıştı galiba!" after a trapped Ctrl+Alt+Del,
 * else "Tamam abi breyk, breyk, sitop !!!".
 *
 * Original: IST.EXE 0DB7:0135 (NKONTROL.L), INT 23h handler.
 * Verified: no (interrupt handler; one fatal call).
 */
void ctl_int23_isr(void)
{
    o559A_fatal(DGSTR(0x753D), 0x16F, g_reset_attempt ? DGSTR(0x7551) : DGSTR(0x7586), -1);
}

/*
 * ctl_int09_isr - keyboard IRQ hook: Ctrl+Alt+Del (make code 53h with Ctrl
 * and Alt held, BDA 0040:0017 & 0Ch) is swallowed (keyboard acknowledged
 * via port 61h, EOI) and turned into a Ctrl-Break (INT 1Bh) with
 * g_reset_attempt = 1; every other key goes to the BIOS handler.
 *
 * Original: IST.EXE 0DB7:0174 (NKONTROL.L), INT 09h handler.
 * Port: the host calls this with each make code while ctl_installed();
 * 0 = let the BIOS (pc_key_push) have the key, 1 = swallowed.  DOS turns
 * INT 1Bh into INT 23h at its next break check (the game polls kbhit()
 * constantly), so the port goes to ctl_int23_isr directly.
 * Verified: no (interrupt handler).
 */
int ctl_int09_isr(uint8_t scan)
{
    if (scan == 0x53 && (pc_shift_flags() & 0x0C) == 0x0C) {
        /* in al,61h; out 61h,al|80h; out 61h,al; out 20h,20h */
        /* int 1Bh -> DOS break flag -> INT 23h */
        g_reset_attempt = 1;
        ctl_int23_isr();
        return 1;
    }
    return 0;
}

/*
 * ctl_flush_keys - while (kbhit()) getch();
 *
 * Original: IST.EXE 0DB7:01E3 (NKONTROL.L), far.
 * Verified: tests/py/test_nkontrol.py (C only; uses the BIOS keyboard), 2026-11-20.
 */
void ctl_flush_keys(void)
{
    while (pc_kbhit())
        pc_getch();
}

/*
 * ctl_timer_tick - the 100 Hz timer routine: g_clock_ms += 10.
 *
 * Original: IST.EXE 0DB7:01F4 (NKONTROL.L), timer-service routine (IRET).
 * Verified: tests/py/test_nkontrol.py (unicorn differential), 2026-11-20.
 */
void ctl_timer_tick(void)
{
    g_clock_ms += 10;
}

/*
 * ctl_timer_install - tsAddRoutine(ctl_timer_tick, 2E9Bh): 1193182 / 11931
 * = 100.007 Hz.
 *
 * Original: IST.EXE 0DB7:021E (NKONTROL.L), far; returns what
 * tsAddRoutine leaves in AX (callers ignore it).
 * Verified: no (thin call into the DSMI timer service).
 */
int16_t ctl_timer_install(void)
{
    return tsAddRoutine(ctl_timer_tick, 0x2E9B);
}

/*
 * ctl_free_checked - mm_free(*pp, line, file); *pp = NULL.  *pp == NULL is
 * fatal ("İç hata!").
 *
 * Original: IST.EXE 0DB7:022F (NKONTROL.L), far cdecl (MmHandle far **pp,
 * line, file).  All 79 call sites pass a variable in DGROUP, so the port
 * takes its DGROUP offset `ref` (see mm_ref in memman.h).
 * Verified: tests/py/test_nkontrol.py (unicorn differential), 2026-11-20.
 */
void ctl_free_checked(uint16_t ref, int16_t line, const char *file)
{
    if (mm_ref(ref) == NULL)
        o559A_fatal(DGSTR(0x75A8), 0x1E0, DGSTR(0x75BC), -1);
    mm_free(mm_ref(ref), line, file);
    mm_ref_set(ref, NULL);
}

/*
 * ctl_dds_timer_init - DDSInit(); tsInit();
 *
 * Original: IST.EXE 0DB7:027E (NKONTROL.L), far.
 * Verified: no (two calls into DSMI).
 */
void ctl_dds_timer_init(void)
{
    DDSInit();
    tsInit();
}
