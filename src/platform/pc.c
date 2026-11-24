/*
 * pc.c - the emulated PC services the decompiled code calls (see pc.h).
 */
#include "platform/pc.h"
#include "platform/cdrom.h"
#include "platform/iso9660.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>      /* ftruncate (POSIX; mingw-w64 provides it) */
#ifndef _WIN32
#include <sys/stat.h>
#endif

/* ======================================================================== */
/* time                                                                     */
/* ======================================================================== */

static uint16_t pit_divisor;          /* 0 = 65536 */
static PcIsr    int08;
static uint64_t pit_now;              /* PIT input clocks elapsed */
static uint64_t pit_next_irq;         /* clock of the next IRQ 0 */
static int      virtual_time;
static uint32_t virtual_step_us = 100;
static uint64_t host_t0_us;
static uint32_t bios_ticks;           /* 0040:006C, advanced by the BIOS handler */
static void   (*host_pump)(void);
static void   (*host_present)(void);
static void   (*host_sleep)(uint32_t us);
static uint32_t idle_polls;
static int      in_isr;
static void   (*input_hook)(int wake);

/* Input-script mode (sdl3_main.c --script, docs/VERIFY.md): the host hook
   runs at every input read of the game, the same points where the DOSBox-X
   side (tools/verify/hook.S) applies scripted events.  INPUT_READ() is one
   read of the game (counted by the read-count sync), INPUT_WAKE() a wake-up
   while a blocking read waits for a key (not counted). */
void pc_set_input_hook(void (*f)(int wake)) { input_hook = f; }
#define INPUT_READ() do { if (input_hook) input_hook(0); } while (0)
#define INPUT_WAKE() do { if (input_hook) input_hook(1); } while (0)

static uint64_t host_us(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static uint32_t divisor_clocks(void) { return pit_divisor ? pit_divisor : 0x10000u; }

void     pc_pit_set_divisor(uint16_t d) { pit_divisor = d; pit_next_irq = pit_now + divisor_clocks(); }
uint16_t pc_pit_divisor(void)           { return pit_divisor; }
void     pc_set_int08(PcIsr isr)        { int08 = isr; }
PcIsr    pc_get_int08(void)             { return int08; }
uint64_t pc_pit_ticks(void)             { return pit_now; }
uint32_t pc_bios_ticks(void)            { return bios_ticks; }
void     pc_bios_int08(void)            { bios_ticks++; }   /* BIOS INT 08h: 0040:006C */
void     pc_set_host_pump(void (*f)(void))    { host_pump = f; }
void     pc_set_host_present(void (*f)(void)) { host_present = f; }

static void (*host_video)(int event, const char *name);
void     pc_set_host_video(void (*f)(int, const char *)) { host_video = f; }
void     pc_video_event(int event, const char *name)
{
    if (host_video)
        host_video(event, name);
}
void     pc_set_host_sleep(void (*f)(uint32_t)) { host_sleep = f; }

void pc_set_virtual_time(int on, uint32_t step_us)
{
    virtual_time = on;
    virtual_step_us = step_us ? step_us : 100;
}

static uint64_t emu_clock_us(void) { return pit_now * 1000000u / PC_PIT_HZ; }

void pc_resync_clock(void)
{
    if (host_t0_us)
        host_t0_us = host_us() - emu_clock_us();
}

/* Advance emulated time to `target` PIT clocks, raising IRQ 0 on the way.
   The handler runs with "interrupts disabled" (no nesting), as on the PC. */
static void advance_to(uint64_t target)
{
    while (pit_next_irq <= target) {
        pit_now = pit_next_irq;
        pit_next_irq += divisor_clocks();
        if (in_isr)
            continue;
        in_isr = 1;
        if (int08)
            int08();
        else
            pc_bios_int08();
        in_isr = 0;
    }
    if (target > pit_now)
        pit_now = target;
}

static void advance_us(uint64_t us)
{
    advance_to(pit_now + us * PC_PIT_HZ / 1000000u);
}

/* The VGA scans out continuously, whether the program reads 3DAh or not:
   at the first poll / 3DAh read of each new frame period the host gets the
   current picture (programs that never wait for the retrace - the intro's
   picture shows - are still displayed).  Only with a host present hook, so
   tests never see an extra sync. */
static void present_if_new_frame(void)
{
    static uint64_t last_frame;
    if (!host_present)
        return;
    double hz = vga_refresh_hz(&pc_vga);
    uint64_t frame = (uint64_t)(emu_clock_us() * hz / 1e6);
    if (frame != last_frame) {
        last_frame = frame;
        pc_vga_sync();
        host_present();
    }
}

/* Port 3DAh in real time.  The emulated clock follows the host's, so it
   moves in steps: the idle sleep below, the window update, or simply the
   system taking the processor away for a moment.  A program that waits for
   the vertical retrace reads 3DAh in a tight loop and on the PC cannot miss
   the pulse (two scan lines, 64 us in mode 13h; a read takes about 1 us) -
   here a step can carry the clock right over it, and the wait then lasts
   another frame, or several (the video player ran late that way and dropped
   most of its frames).  So between two reads of 3DAh that follow each other
   with nothing else polled in between, the clock is not allowed past the
   start of the retrace: the second read gets the clock 20 us into the pulse
   and sees bit 3, as it would have on the PC; the clock catches up with the
   host's at the next poll.  Reads that do not follow each other see the
   real time. */
static int      in_3da;             /* pc_poll() called from pc_in_3da() */
static int      last_poll_3da;      /* the poll before this one was a 3DAh read ... */
static uint8_t  last_3da;           /* ... which returned this */
static uint64_t poll_limit_us;      /* this poll may not advance beyond (0 = no limit) */

void pc_poll(void)
{
    if (!in_3da)
        last_poll_3da = 0;
    if (host_pump)
        host_pump();
    if (virtual_time) {
        advance_us(virtual_step_us);
        present_if_new_frame();
        return;
    }
    if (!host_t0_us)
        host_t0_us = host_us() - emu_clock_us();
    uint64_t target_us = host_us() - host_t0_us;
    /* never run more than 1 s behind (debugger pauses, window drags) */
    if (target_us > emu_clock_us() + 1000000u)
        host_t0_us = host_us() - emu_clock_us();
    else if (poll_limit_us && target_us > poll_limit_us) {
        advance_to((poll_limit_us * PC_PIT_HZ + 999999u) / 1000000u);
        idle_polls = 0;
    } else {
        uint64_t before = pit_next_irq;
        advance_to(target_us * PC_PIT_HZ / 1000000u);
        /* The original spins in busy loops; the port yields the CPU once a
           loop has polled many times without any timer interrupt.  The
           sleep is far shorter than one timer period (10 ms at 100 Hz). */
        if (pit_next_irq != before)
            idle_polls = 0;
        else if (++idle_polls > 64 && host_sleep) {
            /* ... and never across the vertical retrace: a program waiting
               for bit 3 of 3DAh polls it without a pause, and the pulse is
               much shorter than this sleep - slept through, the wait lasts
               another frame, and another (the video player then runs late
               and drops frames).  The sleep ends 150 us before the pulse
               (the host's sleep is exact to a few microseconds); those and
               the pulse itself are spent polling. */
            double r = vga_us_to_vretrace(&pc_vga);
            if (r < 0.0 || r > 650.0)
                host_sleep(500);
            else if (r > 200.0)
                host_sleep((uint32_t)(r - 150.0));
        }
    }
    present_if_new_frame();
}

void pc_wait_retrace(void)
{
    /* wait for the end of the current retrace, then for the next start */
    while (pc_in_3da() & 8)
        ;
    while (!(pc_in_3da() & 8))
        ;
}

uint8_t pc_in_3da(void)
{
    /* a 3DAh read takes ~1 us on the ISA bus; in virtual time this is what
       lets busy loops on retrace make progress */
    if (virtual_time)
        advance_us(2);
    else {
        double r = last_poll_3da && !(last_3da & 8) ? vga_us_to_vretrace(&pc_vga) : -1.0;
        poll_limit_us = r > 0.0 ? emu_clock_us() + (uint64_t)r + 20 : 0;
        in_3da = 1;
        pc_poll();
        in_3da = 0;
        poll_limit_us = 0;
    }
    uint8_t v = vga_in8(&pc_vga, 0x3DA);
    if (v & 8)
        present_if_new_frame();
    last_poll_3da = 1;
    last_3da = v;
    return v;
}

/* ======================================================================== */
/* keyboard                                                                 */
/* ======================================================================== */

static uint16_t kbuf[16];
static int      khead, ktail;         /* BIOS ring: 15 usable entries */
static int      pending_scan = -1;    /* getch(): second byte of an extended key */
static uint8_t  shift_flags;

/* Ctrl-C / Ctrl-Break / Ctrl-S as MS-DOS handles them at kbhit().
 *
 * Borland's kbhit() is INT 21h AH=0Bh, getch() is AH=07h.  From Microsoft's
 * published MS-DOS source (github.com/microsoft/MS-DOS, v4.0/src/DOS:
 * CPMIO2.ASM $STD_CON_INPUT_STATUS, CTRLC.ASM STATCHK/INCHK/CNTCHAND,
 * CPMIO.ASM $RAW_CON_INPUT; the same logic is in the v2.0 MSDOS.SYS binary):
 *   AH=0Bh calls STATCHK, which peeks the console's next character:
 *     ^C (03h)  is read and INT 23h issued (CNTCHAND);
 *     ^S (13h)  is read, DOS waits until another character is there and
 *               reads that one too (INCHK) - if it is ^C, INT 23h; AH=0Bh
 *               then returns "character available" (the flags of INCHK's
 *               last compare) although it was taken;
 *     ^P / ^N   are read and toggle the printer echo - NOT modelled (the
 *               flags the call returns with were not established);
 *   AH=07h never checks: a ^C comes back as the character 03h.
 * Only the head of the buffer is looked at.
 * Ctrl-Break (v4.0/src/BIOS/MSCON.ASM CBREAK: INT 1Bh stores 03h in the CON
 * driver's look-ahead byte): the BIOS empties the key buffer, stores the
 * word 0000h and issues INT 1Bh; the console's next character is then a ^C,
 * so the next kbhit() issues INT 23h.  The 0000h word stays for the BIOS
 * (Borland's bioskey(1) returns FFFFh for it).
 *
 * The game's INT 23h handler (NKONTROL ctl_int23_isr) ends the program;
 * without a handler nothing happens here (DOS's own would abort the
 * program; no game code runs kbhit() before ctl_install).
 *
 * Difference from the project's reference: DOSBox-X's DOS does not look at
 * the character in AH=0Bh, so the original run there takes a buffered ^C at
 * the main menu as an ordinary key (checked 2026-11-22) and breaks only on
 * Ctrl-Break.  The port follows MS-DOS, the system the game was made for.
 * Verification scripts never send 03h or 13h. */
static void (*int23_handler)(void);
static void (*int23_observer)(void);
static int  (*int09_handler)(uint8_t scan);
static int    dos_break_pending;

void pc_set_int23(void (*handler)(void))        { int23_handler = handler; }
void pc_set_int23_observer(void (*observer)(void)) { int23_observer = observer; }

/* DOS issues INT 23h */
static void issue_int23(void)
{
    if (int23_observer)
        int23_observer();
    if (int23_handler)
        int23_handler();
}
void pc_set_int09(int (*handler)(uint8_t scan)) { int09_handler = handler; }

int pc_int09(uint8_t scan)
{
    return int09_handler ? int09_handler(scan) : 0;
}

/* STATCHK; returns 1 when AH=0Bh has to report a character whatever the
   buffer holds (after ^S) */
static int dos_break_check(void)
{
    uint8_t c;
    if (dos_break_pending) {          /* the look-ahead ^C of INT 1Bh */
        dos_break_pending = 0;
        issue_int23();
        return 0;
    }
    if (pending_scan >= 0 || khead == ktail)
        return 0;
    c = (uint8_t)kbuf[khead];
    if (c == 0x03) {
        khead = (khead + 1) & 15;
        issue_int23();
        return 0;
    }
    if (c == 0x13) {
        uint16_t k;
        khead = (khead + 1) & 15;     /* eat ^S, wait for a character, read it */
        while (khead == ktail) {
            pc_poll();
            INPUT_WAKE();
        }
        k = kbuf[khead];
        khead = (khead + 1) & 15;
        c = (uint8_t)k;
        if (c == 0 || c == 0xE0)      /* extended key: the 00h was read, the
                                         scan code is the console's next byte */
            pending_scan = k >> 8;
        else if (c == 0x03)
            issue_int23();
        return 1;
    }
    return 0;
}

void pc_ctrl_break(void)
{
    khead = ktail = 0;
    pending_scan = -1;
    kbuf[ktail++] = 0x0000;
    pc_int1b();
}

void pc_int1b(void)
{
    dos_break_pending = 1;
}

void pc_key_push(uint16_t key)
{
    int next = (ktail + 1) & 15;
    if (next == khead)
        return;                       /* buffer full: BIOS beeps and drops */
    kbuf[ktail] = key;
    ktail = next;
}

/* MS-DOS: $STD_CON_INPUT_FLUSH sends the CON driver its flush command, which
   reads INT 16h until the buffer is empty and drops its look-ahead byte (the
   same byte INT 1Bh uses for ^C); no break check is made. */
void pc_dos_flush_keys(void)
{
    pc_poll();
    khead = ktail = 0;
    pending_scan = -1;
    dos_break_pending = 0;
}

int16_t pc_kbhit(void)
{
    pc_poll();
    INPUT_READ();
    if (dos_break_check())
        return 1;
    return (int16_t)(pending_scan >= 0 || khead != ktail);
}

int16_t pc_getch(void)
{
    INPUT_READ();
    if (pending_scan >= 0) {
        int16_t s = (int16_t)pending_scan;
        pending_scan = -1;
        return s;
    }
    while (khead == ktail) {
        pc_poll();
        INPUT_WAKE();
    }
    uint16_t k = kbuf[khead];
    khead = (khead + 1) & 15;
    uint8_t ascii = (uint8_t)k;
    /* DOS INT 21h AH=07h: E0h-prefixed keys of the enhanced keyboard come
       through INT 16h AH=00h as ASCII 00h */
    if (ascii == 0 || ascii == 0xE0) {
        pending_scan = k >> 8;
        return 0;
    }
    return ascii;
}

/* INT 16h AH=00h/01h report E0h-prefixed gray keys with ASCII 00h; the
   enhanced AH=10h/11h keep the E0h.  (An E0h character typed with Alt+keypad
   has scan code 0 and is left alone.) */
static uint16_t bios_word(uint16_t k, int enhanced)
{
    if (!enhanced && (k & 0xFF) == 0xE0 && (k >> 8) != 0)
        k &= 0xFF00;
    return k;
}

int16_t pc_bioskey(int16_t cmd)
{
    int enhanced = (cmd & 0x10) != 0;
    uint16_t k;
    switch (cmd & 0xEF) {
    case 0:
        INPUT_READ();
        while (khead == ktail) {
            pc_poll();
            INPUT_WAKE();
        }
        k = kbuf[khead];
        khead = (khead + 1) & 15;
        return (int16_t)bios_word(k, enhanced);
    case 1:
        pc_poll();
        INPUT_READ();
        if (khead == ktail)
            return 0;                 /* ZF set */
        k = bios_word(kbuf[khead], enhanced);
        return k ? (int16_t)k : -1;
    case 2:
        /* AH=02h returns AL only and leaves AH (= 02h) alone, and Borland
           returns all of AX; AH=12h puts the extended flags (kept 0) in AH */
        return (int16_t)((enhanced ? 0 : 0x0200) | shift_flags);
    default:
        return 0;                     /* not used by the game */
    }
}

uint8_t pc_shift_flags(void)          { return shift_flags; }
void    pc_set_shift_flags(uint8_t f) { shift_flags = f; }

/* ======================================================================== */
/* mouse (INT 33h)                                                          */
/* ======================================================================== */

static struct {
    int16_t  x, y, min_x, max_x, min_y, max_y;
    uint16_t buttons;
} mouse;

static int16_t clamp16(int16_t v, int16_t lo, int16_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static void mouse_reset(void)
{
    /* driver defaults for a 320x200 graphics mode: virtual screen 640x200 */
    int graphics = pc_vga.gc[6] & 1;
    int high = graphics && pc_vga.crtc[0x12] > 0xC7 && !(pc_vga.crtc[9] & 1); /* 480-line modes */
    mouse.min_x = 0; mouse.max_x = 639;
    mouse.min_y = 0; mouse.max_y = high ? 479 : 199;
    mouse.x = (int16_t)((mouse.max_x + 1) / 2);
    mouse.y = (int16_t)((mouse.max_y + 1) / 2);
}

void pc_mouse_host(int16_t x, int16_t y, uint16_t buttons)
{
    mouse.x = clamp16(x, mouse.min_x, mouse.max_x);
    mouse.y = clamp16(y, mouse.min_y, mouse.max_y);
    mouse.buttons = buttons;
}

void pc_int33(PcMouseRegs *r)
{
    switch (r->ax) {
    case 0x0000:                       /* reset driver */
        mouse_reset();
        r->ax = 0xFFFF;
        r->bx = 2;
        break;
    case 0x0003:                       /* position and buttons */
        INPUT_READ();
        r->bx = mouse.buttons;
        r->cx = (uint16_t)mouse.x;
        r->dx = (uint16_t)mouse.y;
        break;
    case 0x0004:                       /* set position */
        mouse.x = clamp16((int16_t)r->cx, mouse.min_x, mouse.max_x);
        mouse.y = clamp16((int16_t)r->dx, mouse.min_y, mouse.max_y);
        break;
    case 0x0007:                       /* horizontal range (swapped if min > max) */
    case 0x0008: {
        int16_t lo = (int16_t)r->cx, hi = (int16_t)r->dx;
        if (lo > hi) { int16_t t = lo; lo = hi; hi = t; }
        if (r->ax == 7) { mouse.min_x = lo; mouse.max_x = hi; mouse.x = clamp16(mouse.x, lo, hi); }
        else            { mouse.min_y = lo; mouse.max_y = hi; mouse.y = clamp16(mouse.y, lo, hi); }
        break;
    }
    default:
        break;                         /* not used by the game */
    }
}

/* ======================================================================== */
/* EMS (LIM 4.0 subset)                                                     */
/* ======================================================================== */

#define EMS_HANDLES 255

_Alignas(16) uint8_t pc_ems_frame[0x10000];

static uint16_t ems_total = 2048;
static uint16_t ems_used;
static struct { int used; uint16_t pages; uint8_t *mem; } ems_h[EMS_HANDLES];
static struct { int16_t handle; uint16_t page; } ems_map[4];  /* handle -1 = unmapped */
static struct { int valid; int16_t handle[4]; uint16_t page[4]; } ems_saved[EMS_HANDLES];

/* The frame is a copy of the mapped logical pages (the port cannot alias
   memory like the EMS hardware).  Every operation that looks at logical
   memory first writes the frame back; every map change reloads. */
static void ems_flush(int p)
{
    if (ems_map[p].handle >= 0 && ems_h[ems_map[p].handle].used)
        memcpy(ems_h[ems_map[p].handle].mem + (size_t)ems_map[p].page * PC_EMS_PAGE,
               pc_ems_frame + p * PC_EMS_PAGE, PC_EMS_PAGE);
}

static void ems_load(int p)
{
    if (ems_map[p].handle >= 0 && ems_h[ems_map[p].handle].used)
        memcpy(pc_ems_frame + p * PC_EMS_PAGE,
               ems_h[ems_map[p].handle].mem + (size_t)ems_map[p].page * PC_EMS_PAGE, PC_EMS_PAGE);
}

/* Write the page frame back.  On real EMS hardware a logical page mapped
   into two physical pages is the same memory; the emulation keeps one copy
   per physical page, so for an aliased logical page only the bytes each
   copy changed are written back (flushing the copies whole, in order, lets
   a stale copy undo the writes made through another one - found by
   tools/verify: GFL ring data lost when its EMSLock window overlapped an
   earlier mapping of the same pages, INTRO.GFL frame 46). */
static void ems_flush_all(void)
{
    static uint8_t before[PC_EMS_PAGE];
    for (int p = 0; p < 4; p++) {
        int alias = 0;
        for (int q = 0; q < 4; q++)
            if (q != p && ems_map[q].handle == ems_map[p].handle && ems_map[q].page == ems_map[p].page)
                alias = 1;
        if (!alias) {
            ems_flush(p);
            continue;
        }
        if (ems_map[p].handle < 0 || !ems_h[ems_map[p].handle].used)
            continue;
        int first = 1;                /* only the first slot of the alias group does the merge */
        for (int q = 0; q < p; q++)
            if (ems_map[q].handle == ems_map[p].handle && ems_map[q].page == ems_map[p].page)
                first = 0;
        if (!first)
            continue;
        uint8_t *mem = ems_h[ems_map[p].handle].mem + (size_t)ems_map[p].page * PC_EMS_PAGE;
        memcpy(before, mem, PC_EMS_PAGE);
        for (int q = p; q < 4; q++) {
            if (ems_map[q].handle != ems_map[p].handle || ems_map[q].page != ems_map[p].page)
                continue;
            const uint8_t *fr = pc_ems_frame + q * PC_EMS_PAGE;
            for (uint32_t i = 0; i < PC_EMS_PAGE; i++)
                if (fr[i] != before[i])
                    mem[i] = fr[i];
        }
    }
}
static void ems_load_all(void)  { for (int p = 0; p < 4; p++) ems_load(p); }

void pc_ems_configure(uint16_t total_pages) { ems_total = total_pages; }

uint8_t *pc_ems_handle_mem(uint16_t handle, uint32_t *size)
{
    if (handle >= EMS_HANDLES || !ems_h[handle].used)
        return NULL;
    ems_flush_all();
    if (size)
        *size = (uint32_t)ems_h[handle].pages * PC_EMS_PAGE;
    return ems_h[handle].mem;
}

static void ems_set_map(const int16_t *h, const uint16_t *pg)
{
    ems_flush_all();
    for (int p = 0; p < 4; p++) {
        ems_map[p].handle = h[p];
        ems_map[p].page = pg[p];
    }
    ems_load_all();
}

void pc_int67(PcEmsRegs *r)
{
    uint8_t fn = (uint8_t)(r->ax >> 8), al = (uint8_t)r->ax;
    uint8_t st = 0;
    switch (fn) {
    case 0x40:                         /* status */
        break;
    case 0x41:                         /* page frame segment */
        r->bx = PC_EMS_FRAME_SEG;
        break;
    case 0x42:                         /* unallocated / total pages */
        r->bx = (uint16_t)(ems_total - ems_used);
        r->dx = ems_total;
        break;
    case 0x43: {                       /* allocate BX pages -> DX handle */
        int h;
        if (r->bx == 0) { st = 0x89; break; }
        if (r->bx > ems_total) { st = 0x87; break; }
        if (r->bx > ems_total - ems_used) { st = 0x88; break; }
        for (h = 1; h < EMS_HANDLES && ems_h[h].used; h++)
            ;
        if (h == EMS_HANDLES) { st = 0x85; break; }
        ems_h[h].mem = calloc(r->bx, PC_EMS_PAGE);
        if (!ems_h[h].mem) { st = 0x80; break; }
        ems_h[h].used = 1;
        ems_h[h].pages = r->bx;
        ems_used += r->bx;
        r->dx = (uint16_t)h;
        break;
    }
    case 0x44: {                       /* map: AL physical, BX logical, DX handle */
        uint16_t h = r->dx;
        if (h >= EMS_HANDLES || !ems_h[h].used) { st = 0x83; break; }
        if (al > 3) { st = 0x8B; break; }
        if (r->bx != 0xFFFF && r->bx >= ems_h[h].pages) { st = 0x8A; break; }
        int16_t hh[4]; uint16_t pg[4];
        for (int p = 0; p < 4; p++) { hh[p] = ems_map[p].handle; pg[p] = ems_map[p].page; }
        hh[al] = r->bx == 0xFFFF ? -1 : (int16_t)h;
        pg[al] = r->bx;
        /* The copy-based frame cannot alias: one logical page mapped into two
           physical pages behaves like two copies.  Not seen in the game so
           far; report it so a real occurrence is noticed (WP9, 2026-11-21). */
        for (int p = 0; p < 4; p++) {
            static int warned;
            if (p != al && hh[p] == hh[al] && hh[al] >= 0 && pg[p] == pg[al] && !warned) {
                warned = 1;
                fprintf(stderr, "pc_int67: logical page %u of handle %d mapped twice (aliasing not emulated)\n",
                        (unsigned)pg[al], hh[al]);
            }
        }
        ems_set_map(hh, pg);
        break;
    }
    case 0x45: {                       /* free handle */
        uint16_t h = r->dx;
        if (h >= EMS_HANDLES || !ems_h[h].used) { st = 0x83; break; }
        if (ems_saved[h].valid) { st = 0x86; break; }
        ems_flush_all();
        for (int p = 0; p < 4; p++)
            if (ems_map[p].handle == (int16_t)h)
                ems_map[p].handle = -1;
        ems_used -= ems_h[h].pages;
        free(ems_h[h].mem);
        ems_h[h].mem = NULL;
        ems_h[h].used = 0;
        ems_h[h].pages = 0;
        break;
    }
    case 0x46:                         /* version */
        r->ax = (uint16_t)((r->ax & 0xFF00) | 0x40);
        break;
    case 0x47: {                       /* save page map for handle DX */
        uint16_t h = r->dx;
        if (h >= EMS_HANDLES || !ems_h[h].used) { st = 0x83; break; }
        if (ems_saved[h].valid) { st = 0x8D; break; }
        ems_saved[h].valid = 1;
        for (int p = 0; p < 4; p++) {
            ems_saved[h].handle[p] = ems_map[p].handle;
            ems_saved[h].page[p] = ems_map[p].page;
        }
        break;
    }
    case 0x48: {                       /* restore page map for handle DX */
        uint16_t h = r->dx;
        if (h >= EMS_HANDLES || !ems_h[h].used) { st = 0x83; break; }
        if (!ems_saved[h].valid) { st = 0x8E; break; }
        ems_set_map(ems_saved[h].handle, ems_saved[h].page);
        ems_saved[h].valid = 0;
        break;
    }
    default:
        st = 0x84;                     /* function not defined */
        break;
    }
    r->ax = (uint16_t)((st << 8) | (r->ax & 0xFF));   /* AL: 40h after fn 46h */
}

static uint8_t *move_side(uint8_t type, uint16_t handle, uint16_t page, uint16_t off,
                          uint8_t *ptr, uint32_t len, uint8_t *err)
{
    if (type == 0)
        return ptr;
    if (handle >= EMS_HANDLES || !ems_h[handle].used) { *err = 0x83; return NULL; }
    if (off >= PC_EMS_PAGE) { *err = 0x95; return NULL; }
    uint32_t start = (uint32_t)page * PC_EMS_PAGE + off;
    if (start + len > (uint32_t)ems_h[handle].pages * PC_EMS_PAGE) { *err = 0x93; return NULL; }
    return ems_h[handle].mem + start;
}

static int ems_present = 1;

void pc_ems_set_present(int on) { ems_present = on; }
int  pc_ems_detect(void)        { return ems_present; }

uint8_t pc_ems_page_map(uint8_t al, uint8_t *get_buf, const uint8_t *set_buf, uint8_t *size_out)
{
    if (al > 3)
        return 0x8F;                   /* subfunction not defined */
    if (al == 3) {
        if (size_out)
            *size_out = PC_EMS_MAP_SIZE;
        return 0;
    }
    if (al == 0 || al == 2) {
        for (int p = 0; p < 4; p++) {
            uint16_t h = (uint16_t)ems_map[p].handle, pg = ems_map[p].page;
            get_buf[p * 4 + 0] = (uint8_t)h;  get_buf[p * 4 + 1] = (uint8_t)(h >> 8);
            get_buf[p * 4 + 2] = (uint8_t)pg; get_buf[p * 4 + 3] = (uint8_t)(pg >> 8);
        }
    }
    if (al == 1 || al == 2) {
        int16_t hh[4]; uint16_t pg[4];
        for (int p = 0; p < 4; p++) {
            hh[p] = (int16_t)(set_buf[p * 4] | set_buf[p * 4 + 1] << 8);
            pg[p] = (uint16_t)(set_buf[p * 4 + 2] | set_buf[p * 4 + 3] << 8);
            if (hh[p] >= 0 && (hh[p] >= EMS_HANDLES || !ems_h[hh[p]].used))
                return 0xA3;           /* source array corrupted */
        }
        ems_set_map(hh, pg);
    }
    return 0;
}

uint8_t pc_ems_move(const PcEmsMove *m)
{
    uint8_t err = 0;
    if (m->length > 0x100000u)
        return 0x96;
    ems_flush_all();
    uint8_t *s = move_side(m->src_type, m->src_handle, m->src_page, m->src_off, m->src_ptr, m->length, &err);
    uint8_t *d = move_side(m->dst_type, m->dst_handle, m->dst_page, m->dst_off, m->dst_ptr, m->length, &err);
    if (err)
        return err;
    memmove(d, s, m->length);
    /* A conventional destination inside the page frame (E000:xxxx) is the
       mapped logical memory itself on real hardware: write the frame back
       before reloading it, or the copy would be lost. */
    if (m->dst_type == 0 && d < pc_ems_frame + sizeof pc_ems_frame && d + m->length > pc_ems_frame)
        ems_flush_all();
    ems_load_all();
    return (s < d + m->length && d < s + m->length) ? 0x92 : 0;   /* 92h: overlap, done */
}

/* ======================================================================== */
/* VGA                                                                      */
/* ======================================================================== */

VgaState pc_vga;
_Alignas(16) uint8_t pc_a000[PC_A000_SIZE + PC_A000_SLACK];
static uint8_t a000_synced[0x10000];

void pc_vga_sync(void)
{
    if (!(pc_vga.seq[4] & 0x08))       /* not chain-4: planar code writes directly */
        return;
    for (uint32_t i = 0; i < 0x10000; i += 64) {
        if (memcmp(pc_a000 + i, a000_synced + i, 64) == 0)
            continue;
        for (uint32_t j = i; j < i + 64; j++)
            vga_mem_write8(&pc_vga, 0xA0000u + j, pc_a000[j]);
        memcpy(a000_synced + i, pc_a000 + i, 64);
    }
}

/* ======================================================================== */
/* files                                                                    */
/* ======================================================================== */

/*
 * Where a DOS file name leads (pc.h):
 *   reads:   SAVE folder, DATA folder, then the virtual CD's own files;
 *   writes:  the SAVE folder, always (a file found elsewhere is copied there
 *            first when it is opened for writing);
 *   a name on the CD drive ("D:\0GFL.CKD"): the disc only, read-only.
 * With pc_set_game_dir() both folders are the same one - the working
 * folder of the DOS game - and nothing is ever copied.
 */
static char data_dir[1024] = ".";
static char save_dir[1024] = ".";
static IsoFs disc_fs;
static int   disc_on;

struct PcFile {
    FILE    *f;             /* host file, or NULL: a file of the disc */
    IsoEntry iso;
    int      drive;
};

void pc_set_dirs(const char *data, const char *save)
{
    snprintf(data_dir, sizeof data_dir, "%s", data);
    snprintf(save_dir, sizeof save_dir, "%s", save);
}

void        pc_set_game_dir(const char *dir) { pc_set_dirs(dir, dir); }
const char *pc_game_dir(void)                { return save_dir; }
const char *pc_data_dir(void)                { return data_dir; }
const char *pc_save_dir(void)                { return save_dir; }

static int disc_read(void *ctx, uint32_t lba, uint8_t *sector)
{
    (void)ctx;
    return cdrom_read_data(lba, sector);
}

int pc_use_disc_files(int on)
{
    disc_on = on && iso_mount(&disc_fs, disc_read, NULL) == 0;
    return disc_on ? 0 : -1;
}

const char *pc_disc_volume_id(void) { return disc_on ? disc_fs.volume_id : ""; }

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
    return *a == *b;
}

/* Drive index of "X:..." (0 = A:), -1 without a drive letter. */
static int dos_drive(const char *dos_name)
{
    if (isalpha((unsigned char)dos_name[0]) && dos_name[1] == ':')
        return toupper((unsigned char)dos_name[0]) - 'A';
    return -1;
}

/* Host path of a DOS name under `root`, each part matched without regard to
   case; a part that does not exist is taken as written. */
static char *resolve_in(const char *root, const char *dos_name, char *buf, size_t size)
{
    char tmp[1024];
    snprintf(buf, size, "%s", root);
    snprintf(tmp, sizeof tmp, "%s", dos_name);
    char *save = NULL;
    for (char *part = strtok_r(tmp, "\\/", &save); part; part = strtok_r(NULL, "\\/", &save)) {
        if (part[0] && part[1] == ':' && !part[2])
            continue;                  /* drive letter */
        if (part[0] && part[1] == ':')
            part += 2;                 /* "D:NAME" */
        size_t len = strlen(buf);
        const char *found = part;
        DIR *d = opendir(buf);
        struct dirent *e;
        if (d) {
            while ((e = readdir(d)) != NULL)
                if (ieq(e->d_name, part)) { found = e->d_name; break; }
        }
        snprintf(buf + len, size - len, "/%s", found);
        if (d)
            closedir(d);
    }
    return buf;
}

char *pc_resolve(const char *dos_name, char *buf, size_t size)
{
    return resolve_in(save_dir, dos_name, buf, size);
}

static FILE *host_open(const char *root, const char *dos_name, const char *mode)
{
    char path[1024];
    FILE *f = fopen(resolve_in(root, dos_name, path, sizeof path), mode);
#ifndef _WIN32
    /* DOS does not open a directory as a file (an empty name resolves to the
       game folder itself).  Windows' fopen refuses one too; POSIX hosts open
       it for reading, so it is refused here (found by test_cd5737's
       read_cfg with an empty name on macOS). */
    struct stat st;
    if (f && fstat(fileno(f), &st) == 0 && S_ISDIR(st.st_mode)) {
        fclose(f);
        return NULL;
    }
#endif
    return f;
}

FILE *pc_fopen(const char *dos_name, const char *mode)
{
    return host_open(save_dir, dos_name, mode);
}

/* The name on the disc: the path without its drive letter. */
static int disc_find(const char *dos_name, IsoEntry *e)
{
    if (!disc_on)
        return -1;
    if (dos_drive(dos_name) >= 0)
        dos_name += 2;
    return iso_find(&disc_fs, dos_name, e) == 0 && !e->is_dir ? 0 : -1;
}

static int on_cd_drive(const char *dos_name)
{
    return disc_on && dos_drive(dos_name) == CDROM_DRIVE_INDEX;
}

static int two_dirs(void) { return strcmp(data_dir, save_dir) != 0; }

static PcFile *file_new(FILE *f, const IsoEntry *iso, int drive)
{
    PcFile *p = calloc(1, sizeof *p);
    if (!p) {
        if (f)
            fclose(f);
        return NULL;
    }
    p->f = f;
    if (iso)
        p->iso = *iso;
    p->drive = drive;
    return p;
}

/* Copies a read-only original into the SAVE folder (upper-case DOS name). */
static FILE *copy_up(PcFile *src, const char *dos_name)
{
    FILE *out = host_open(save_dir, dos_name, "w+b");
    uint8_t buf[16384];
    uint32_t pos = 0;
    size_t n;
    if (!out)
        return NULL;
    while ((n = pc_file_read(src, pos, buf, sizeof buf)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(out);
            return NULL;
        }
        pos += (uint32_t)n;
    }
    fflush(out);
    return out;
}

PcFile *pc_file_open(const char *dos_name, int mode)
{
    IsoEntry e;
    FILE *f;
    if (on_cd_drive(dos_name)) {                    /* the CD: read-only */
        if (mode != PC_OPEN_READ || disc_find(dos_name, &e) != 0)
            return NULL;
        return file_new(NULL, &e, CDROM_DRIVE_INDEX);
    }
    if (mode == PC_OPEN_CREATE) {
        f = host_open(save_dir, dos_name, "w+b");
        return f ? file_new(f, NULL, 2) : NULL;
    }
    f = host_open(save_dir, dos_name, mode == PC_OPEN_READ ? "rb" : "r+b");
    if (f)
        return file_new(f, NULL, 2);
    /* the read-only originals: DATA folder, then the disc */
    PcFile *orig = NULL;
    if (two_dirs() && (f = host_open(data_dir, dos_name, "rb")) != NULL)
        orig = file_new(f, NULL, 2);
    else if (disc_find(dos_name, &e) == 0)
        orig = file_new(NULL, &e, 2);
    if (!orig || mode == PC_OPEN_READ)
        return orig;
    f = copy_up(orig, dos_name);
    pc_file_close(orig);
    return f ? file_new(f, NULL, 2) : NULL;
}

int pc_file_exists(const char *dos_name)
{
    PcFile *f = pc_file_open(dos_name, PC_OPEN_READ);
    if (!f)
        return 0;
    pc_file_close(f);
    return 1;
}

void pc_file_close(PcFile *p)
{
    if (!p)
        return;
    if (p->f)
        fclose(p->f);
    free(p);
}

int pc_file_drive(const PcFile *p) { return p->drive; }

uint32_t pc_file_size(PcFile *p)
{
    if (!p->f)
        return p->iso.size;
    fseek(p->f, 0, SEEK_END);
    return (uint32_t)ftell(p->f);
}

size_t pc_file_read(PcFile *p, uint32_t pos, void *buf, size_t n)
{
    if (!p->f)
        return iso_read(&disc_fs, &p->iso, pos, buf, (uint32_t)n);
    if (fseek(p->f, (long)pos, SEEK_SET) != 0)
        return 0;
    return fread(buf, 1, n, p->f);
}

size_t pc_file_write(PcFile *p, uint32_t pos, const void *buf, size_t n)
{
    if (!p->f || fseek(p->f, (long)pos, SEEK_SET) != 0)
        return 0;
    size_t put = fwrite(buf, 1, n, p->f);
    fflush(p->f);
    return put;
}

int pc_file_truncate(PcFile *p, uint32_t size)
{
    if (!p->f)
        return -1;
    fflush(p->f);
    return ftruncate(fileno(p->f), (off_t)size);
}

/* ---- directory listing (the save menu's findfirst/findnext) ----------------- */

static int list_add(char names[][13], int n, int max, const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    size_t base = dot ? (size_t)(dot - name) : strlen(name);
    char up[13];
    size_t k;
    if (n >= max || !dot || base == 0 || base > 8 || strlen(dot) != 4)
        return n;
    for (k = 0; name[k]; k++)
        up[k] = (char)toupper((unsigned char)name[k]);
    up[k] = 0;
    if (strcmp(up + base + 1, ext) != 0)
        return n;
    for (int i = 0; i < n; i++)
        if (strcmp(names[i], up) == 0)
            return n;                   /* SAVE hides the same name in DATA */
    strcpy(names[n], up);
    return n + 1;
}

typedef struct ListCtx {
    char      (*names)[13];
    int         n, max;
    const char *ext;
} ListCtx;

static int list_iso_cb(void *ctx, const IsoEntry *e)
{
    ListCtx *l = ctx;
    if (!e->is_dir)
        l->n = list_add(l->names, l->n, l->max, e->name, l->ext);
    return 0;
}

int pc_list_files(const char *ext, char names[][13], int max)
{
    int n = 0;
    for (int pass = 0; pass < (two_dirs() ? 2 : 1); pass++) {
        DIR *d = opendir(pass == 0 ? save_dir : data_dir);
        struct dirent *de;
        if (!d)
            continue;
        while ((de = readdir(d)) != NULL)
            n = list_add(names, n, max, de->d_name, ext);
        closedir(d);
    }
    if (disc_on) {
        ListCtx l = { names, n, max, ext };
        iso_list(&disc_fs, "", list_iso_cb, &l);
        n = l.n;
    }
    return n;
}

/* ======================================================================== */

void pc_reset(void)
{
    pit_divisor = 0;
    int08 = NULL;
    pit_now = 0;
    pit_next_irq = 0x10000u;
    bios_ticks = 0;
    host_t0_us = 0;
    in_isr = 0;
    khead = ktail = 0;
    pending_scan = -1;
    shift_flags = 0;
    memset(&mouse, 0, sizeof mouse);
    for (int h = 0; h < EMS_HANDLES; h++) {
        free(ems_h[h].mem);
        memset(&ems_h[h], 0, sizeof ems_h[h]);
        ems_saved[h].valid = 0;
    }
    ems_used = 0;
    ems_present = 1;
    for (int p = 0; p < 4; p++) ems_map[p].handle = -1;
    memset(pc_ems_frame, 0, sizeof pc_ems_frame);
    vga_init(&pc_vga, emu_clock_us);
    memset(pc_a000, 0, sizeof pc_a000);
    memset(a000_synced, 0, sizeof a000_synced);
}
