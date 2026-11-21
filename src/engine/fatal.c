/*
 * fatal.c - ovl559A (IST.EXE 559A:0000-0304): fatal error exit, TRDOS.DAT
 * text font, beep, table clearing.  See fatal.h.
 *
 * Verified: tests/py/test_fatal.py (o559A_clear_tables, o559A_load_trdos_font
 * and the text/flags of o559A_fatal against the original in tools/emu86.py).
 */
#include "engine/fatal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/dds.h"
#include "dsmi/timer.h"
#include "engine/memman.h"
#include "engine/nkontrol.h"
#include "engine/vfs.h"
#include "game/cd5737.h"
#include "platform/pc.h"
#include "platform/speaker.h"
#include "rtl/bc_farheap.h"
#include "rtl/bc_stdio.h"

void (*o559A_sound_hook)(void);
uint8_t o559A_text_font[256 * 16];
int16_t o559A_text_font_loaded;
int16_t o559A_test_mode;
int16_t o559A_fatal_count, o559A_fatal_code, o559A_fatal_line;

static void console_stderr(const char *text, uint16_t len)
{
    fwrite(text, 1, len, stderr);
    fflush(stderr);
}
void (*fatal_console)(const char *text, uint16_t len) = console_stderr;

/* printf() for this module's formats (%d %u %s with DGROUP format strings:
   the host formats them alike). */
void fatal_printf(const char *fmt, ...)
{
    char buf[1024];
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 1)
        n = (int)sizeof buf - 1;
    if (n > 0)
        fatal_console(buf, (uint16_t)n);
}

/* puts(): the string and a newline. */
void fatal_puts(const char *s)
{
    fatal_console(s, (uint16_t)strlen(s));
    fatal_console("\n", 1);
}

/* Message of an error code: the table's far pointers point into DGROUP. */
static const char *code_message(int16_t code)
{
    if ((g_o559A_msg_off(code) | g_o559A_msg_seg(code)) == 0)
        return NULL;
    return DGSTR(g_o559A_msg_off(code));
}

/* "(%d) ... (%s->%u)" + message line + empty line: the code >= 0 report. */
static void print_code_report(uint16_t fmt, uint16_t msg_fmt, uint16_t tail,
                              const char *file, int16_t line, int16_t code)
{
    const char *m;
    fatal_printf(DGSTR(fmt), (int)code, file, (unsigned)(uint16_t)line);
    m = code_message(code);
    if (m)
        fatal_printf(DGSTR(msg_fmt), m);
    fatal_puts(DGSTR(tail));
}

/*
 * o559A_fatal - report an internal error and leave the game: stop the
 * music and unload the sound driver (if sound is on), unmount the 8
 * archives, stop the DSMI timer, text mode 3, stop CD audio, load the
 * Turkish font, print "Oyunda %d numaralı iç hata çıktı. (Çıkış: %s->%u)"
 * (+ the code's message) or, for code < 0, msg; then the support line,
 * free all memory (mm_shutdown), restore the keyboard/mouse vectors, clear
 * the BIOS shift-lock bits 0-1 at 0040:0017 and exit(0).
 * A fatal error during this (g_sys_flags bit 2 set) only prints
 * "Çift hata: ..." and RETURNS to its caller.
 *
 * Original: IST.EXE 559A:0000, far cdecl (char far *file, int line,
 * char far *msg, int code).  Called from 152 sites.
 * Port: ogr_int10(3) (ovl5511:0000, a bare INT 10h AX=0003h) is done
 * directly; exit(0) = bc_exit_streams() + exit(0) (Borland exit flushes and
 * closes the streams; atexit/#pragma exit functions of the game: none).
 * Verified: tests/py/test_fatal.py (printed text and flags, both paths, with
 * the shutdown calls stubbed); the shutdown order by review.
 */
void o559A_fatal(const char *file, int16_t line, const char *msg, int16_t code)
{
    int16_t i;

    if (o559A_test_mode) {
        o559A_fatal_count++;
        o559A_fatal_code = code;
        o559A_fatal_line = line;
        if (o559A_test_mode == 1)
            return;
    }

    if (g_sys_flags & 0x04) {                          /* fatal inside fatal */
        fatal_printf(DGSTR(0x8CDD));                     /* "Çift hata: " */
        if (code >= 0)
            print_code_report(0x8CE9, 0x8D0A, 0x8D0E, file, line, code);
        else if (msg)
            fatal_printf(DGSTR(0x8D0F), msg);            /* "\n%s\n" */
        return;
    }
    g_sys_flags |= 0x04;

    if (g_sound_on) {
        ampClose();
        if (o559A_sound_hook)
            o559A_sound_hook();
        DDSUnloadDriver(g_sound_driver);
    }
    for (i = 0; i < 8; i++)
        vfs_unmount_archive(g_archive_slot(i));
    tsClose();
    vga_bios_set_mode(&pc_vga, 3);                     /* ogr_int10(3) */
    if (cd5737_active()) {
        o5737_cd_stop();
        o5737_nop();
    }
    o559A_load_trdos_font();

    if (code >= 0)
        print_code_report(0x8D14, 0x8D47, 0x8D4B, file, line, code);
    else if (msg)
        fatal_printf(DGSTR(0x8D4C), msg);                /* "%s\n\n" */
    fatal_puts(DGSTR(0x8D51));                           /* RAKS NEWMEDIA support line */

    if (!(g_sys_flags & 0x02)) {
        g_sys_flags |= 0x02;
        mm_shutdown();
        g_sys_flags &= (uint8_t)~0x02;
    }
    ctl_uninstall();
    pc_set_shift_flags(pc_shift_flags() & 0xFC);       /* 0040:0017 &= FCh */

    if (o559A_test_mode)
        return;
    bc_exit_streams();                                 /* exit(0) */
    exit(0);
}

/*
 * o559A_load_trdos_font - load the 8x16 Turkish (cp857) font from
 * TRDOS.DAT into the VGA's character generator: fopen "rb", malloc(1000h),
 * fread(buf, 1000h, 1), fclose, INT 10h AX=1100h BX=1000h CX=0100h DX=0
 * ES:BP=buf, farfree(buf).  Without the file: puts("Turkce Dos Fontu
 * bulunamadi.").
 * Original: IST.EXE 559A:01F3, far cdecl.  Also called by ist_main.
 * Port: the font goes to o559A_text_font (4096 bytes copied from the
 * buffer: a short file leaves the malloc'd block's old bytes, as in the
 * original).  malloc failing is not handled by the original (fread into
 * NULL); the port skips the read then.
 * Verified: tests/py/test_fatal.py (font bytes, heap state, missing file).
 */
void o559A_load_trdos_font(void)
{
    BcFile *fp = bc_fopen(DGSTR(0x8DAA), DGSTR(0x8DB4));   /* "TRDOS.DAT", "rb" */
    uint8_t *buf;
    if (fp == NULL) {
        fatal_puts(DGSTR(0x8DB7));                       /* "Turkce Dos Fontu bulunamadi." */
        return;
    }
    buf = bc_malloc(0x1000);
    if (buf)
        bc_fread(buf, 0x1000, 1, fp);
    bc_fclose(fp);
    if (buf) {
        memcpy(o559A_text_font, buf, sizeof o559A_text_font);   /* INT 10h AX=1100h */
        o559A_text_font_loaded = 1;
    }
    bc_farfree(buf);
}

/*
 * o559A_beep - sound(freq); delay(ms); nosound().
 * Original: IST.EXE 559A:0291, far cdecl (unsigned freq, unsigned ms);
 * unreferenced.
 * Verified: no (unreferenced; the speaker is silent in the port, delay()
 * keeps the timing, see src/platform/speaker.c).
 */
void o559A_beep(uint16_t freq, uint16_t ms)
{
    bc_sound(freq);
    bc_delay(ms);
    bc_nosound();
}

/*
 * o559A_clear_tables - NULL the MmHandle pointers of the 90 surface slots
 * (E29F/E2A1 + 8n) and of the 49 sample slots (DE34/DE36 + 17h n).  Sizes
 * stay.
 * Original: IST.EXE 559A:02AD, far cdecl; called once by ist_main.
 * Verified: tests/py/test_fatal.py (unicorn differential, whole DGROUP).
 */
void o559A_clear_tables(void)
{
    int16_t i;
    for (i = 0; i < 0x5A; i++) {
        g_surf_mm_seg(i) = 0;
        g_surf_mm_off(i) = 0;
    }
    for (i = 0; i < SAMPLE_SLOTS; i++) {
        DG16(SAMPLE_SLOT(i) + 2) = 0;
        DG16(SAMPLE_SLOT(i)) = 0;
    }
}
