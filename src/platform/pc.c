/*
 * pc.c - the emulated PC services the decompiled code calls (see pc.h).
 */
#include "platform/pc.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

void pc_poll(void)
{
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
    else {
        uint64_t before = pit_next_irq;
        advance_to(target_us * PC_PIT_HZ / 1000000u);
        /* The original spins in busy loops; the port yields the CPU once a
           loop has polled many times without any timer interrupt.  The
           sleep is far shorter than one timer period (10 ms at 100 Hz). */
        if (pit_next_irq != before)
            idle_polls = 0;
        else if (++idle_polls > 64 && host_sleep)
            host_sleep(500);
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
    else
        pc_poll();
    uint8_t v = vga_in8(&pc_vga, 0x3DA);
    if (v & 8)
        present_if_new_frame();
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
static int  (*int09_handler)(uint8_t scan);
static int    dos_break_pending;

void pc_set_int23(void (*handler)(void))        { int23_handler = handler; }
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
    if (dos_break_pending) {          /* the look-ahead ^C of Ctrl-Break */
        dos_break_pending = 0;
        if (int23_handler)
            int23_handler();
        return 0;
    }
    if (pending_scan >= 0 || khead == ktail)
        return 0;
    c = (uint8_t)kbuf[khead];
    if (c == 0x03) {
        khead = (khead + 1) & 15;
        if (int23_handler)
            int23_handler();
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
        else if (c == 0x03 && int23_handler)
            int23_handler();
        return 1;
    }
    return 0;
}

void pc_ctrl_break(void)
{
    khead = ktail = 0;
    pending_scan = -1;
    kbuf[ktail++] = 0x0000;
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

uint8_t pc_ems_frame[0x10000];

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
uint8_t  pc_a000[0x10000];
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

static char game_dir[1024] = ".";

void        pc_set_game_dir(const char *dir) { snprintf(game_dir, sizeof game_dir, "%s", dir); }
const char *pc_game_dir(void)                { return game_dir; }

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
    return *a == *b;
}

char *pc_resolve(const char *dos_name, char *buf, size_t size)
{
    char tmp[1024];
    snprintf(buf, size, "%s", game_dir);
    snprintf(tmp, sizeof tmp, "%s", dos_name);
    char *save = NULL;
    for (char *part = strtok_r(tmp, "\\/", &save); part; part = strtok_r(NULL, "\\/", &save)) {
        if (part[0] && part[1] == ':' && !part[2])
            continue;                  /* drive letter */
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

FILE *pc_fopen(const char *dos_name, const char *mode)
{
    char path[1024];
    return fopen(pc_resolve(dos_name, path, sizeof path), mode);
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
