/*
 * nkontrol.h - ALTDUZEY\NKONTROL.L: interrupt hooks (Ctrl-Break, keyboard,
 * divide error, DOS critical error), the 100 Hz millisecond clock and a
 * few small helpers.  Original: IST.EXE seg0DB7 (Borland C + inline asm).
 * Implementation: src/engine/nkontrol.c.
 *
 * Vector hooking does not exist in the port:
 *   INT 23h (Ctrl-C/Break)  -> the host calls ctl_int23_isr() on Ctrl+Break
 *   INT 09h (keyboard)      -> the host calls ctl_int09_isr(scan) for every
 *                              make code while ctl_installed(); it returns 1
 *                              when the key was swallowed (Ctrl+Alt+Del)
 *   INT 00h (divide error)  -> no handler; division sites use the
 *                              nk_div* helpers below
 *   harderr (INT 24h)       -> no DOS critical errors in the port
 *   timer                   -> ctl_timer_tick runs from the DSMI timer
 *                              service (pc_poll), see ctl_timer_install
 *
 * Division by zero.  The game installs ctl_div0_isr as INT 00h: on a
 * divide error (divisor 0 OR quotient overflow) it sets AX (and EAX) to
 * FFFFh, leaves DX alone and resumes 3 bytes after the faulting
 * instruction.  So for the usual 3-byte `div/idiv word [bp+xx]`:
 *   quotient  AX = FFFFh (-1 as int)
 *   remainder DX = unchanged = high word of the dividend (cwd: 0 or FFFFh
 *                  for 16-bit division; for 8-bit div AH = FFh)
 * Other instruction lengths resume mid-instruction - every division site
 * that can fault must be checked against the listing.  Borland's long
 * division helpers (N_LDIV@ ...) divide with their own code; check those
 * sites separately.  Use the helpers at the sites the listing confirms.
 */
#ifndef LALE_ENGINE_NKONTROL_H
#define LALE_ENGINE_NKONTROL_H

#include <stdint.h>

#include "engine/dgroup.h"
#include "engine/memman.h"

/* ---- DGROUP ------------------------------------------------------------- */
#define g_reset_attempt   DG8(0x750B)    /* u8   1 after Ctrl+Alt+Del was trapped */
#define g_harderr_count   DGS16(0x7510)  /* i16  critical-error retries (0..3) */
/* E73E/E742/E746: saved INT 23h/00h/09h vectors (far, not in the port) */
#define g_clock_ms        DG32(0xE74A)   /* u32  ms clock, +10 per 100 Hz tick */

/* ---- division-by-zero semantics (see above) ----------------------------- */
#define NK_DIV0_RESULT ((int16_t)-1)
static inline int16_t nk_div0_result(void) { return NK_DIV0_RESULT; }
/* 16-bit DIV of DX:AX = n by d: quotient, or FFFFh if d == 0 or the
   quotient does not fit; *rem = remainder, or DX (n >> 16) on a fault. */
static inline uint16_t nk_div_u16(uint32_t n, uint16_t d, uint16_t *rem)
{
    if (d == 0 || n / d > 0xFFFFu) {
        if (rem) *rem = (uint16_t)(n >> 16);
        return 0xFFFF;
    }
    if (rem) *rem = (uint16_t)(n % d);
    return (uint16_t)(n / d);
}
/* 16-bit IDIV of DX:AX = n by d (386: quotient -32768..32767 allowed). */
static inline int16_t nk_idiv_s16(int32_t n, int16_t d, int16_t *rem)
{
    if (d == 0 || (n == INT32_MIN && d == -1) ||
        n / d > 32767 || n / d < -32768) {
        if (rem) *rem = (int16_t)((uint32_t)n >> 16);
        return -1;
    }
    if (rem) *rem = (int16_t)(n % d);
    return (int16_t)(n / d);
}
/* 8-bit DIV of AX by d: returns the new AX (AH remainder, AL quotient),
   FFFFh on a fault. */
static inline uint16_t nk_div_u8(uint16_t ax, uint8_t d)
{
    if (d == 0 || ax / d > 0xFF)
        return 0xFFFF;
    return (uint16_t)(((ax % d) << 8) | (ax / d));
}

int16_t  ctl_ctrlbrk_handler(void);          /* 0DB7:0009  harderr handler */
void     ctl_install(void);                  /* 0DB7:0026 */
void     ctl_uninstall(void);                /* 0DB7:00B3 */
uint16_t ctl_div0_isr(void);                 /* 0DB7:00F6  (documentation; returns AX) */
void     ctl_int23_isr(void);                /* 0DB7:0135 */
int      ctl_int09_isr(uint8_t scan);        /* 0DB7:0174  1 = swallowed */
void     ctl_flush_keys(void);               /* 0DB7:01E3 */
void     ctl_timer_tick(void);               /* 0DB7:01F4 */
int16_t  ctl_timer_install(void);            /* 0DB7:021E */
void     ctl_free_checked(uint16_t ref, int16_t line, const char *file);   /* 0DB7:022F */
void     ctl_dds_timer_init(void);           /* 0DB7:027E */
int      ctl_installed(void);                /* port: vectors "installed" */

#endif
