/*
 * pc.h - the emulated PC the decompiled code runs on.
 *
 * The original talks to DOS, the BIOS, the mouse driver, EMS and the VGA
 * directly.  Decompiled code keeps those calls - with the same arguments and
 * in the same order - but they land here instead of in INT xxh.  Nothing in
 * this file knows about SDL; src/platform/sdl3_main.c feeds it input and
 * takes frames and audio out of it, and the tests drive it directly.
 *
 *   time      PIT channel 0 + the INT 08h handler the game installs (DSMI
 *             timer service).  pc_poll() runs the handler once per elapsed
 *             PIT period.  Busy-wait loops in the original become loops that
 *             call pc_poll() - that is where "interrupts" happen in the port.
 *   keyboard  BIOS buffer: INT 16h AH=00h/01h/02h (+10h-12h) via pc_bioskey,
 *             DOS kbhit/getch via pc_kbhit/pc_getch - both on the same buffer.
 *   mouse     INT 33h functions 0, 3, 4, 7, 8 (the only ones used).
 *   EMS       LIM 4.0 subset used by the DSMI EMS library (seg1134):
 *             40h 41h 42h 43h 44h 45h 46h 47h 48h 57h; one 64 KB page frame.
 *   VGA       pc_vga (src/platform/vga.h) + the A000 window, see pc_a000.
 *   files     DOS 8.3 names resolved case-insensitively in the game folder.
 */
#ifndef LALE_PLATFORM_PC_H
#define LALE_PLATFORM_PC_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "platform/vga.h"

/* ---- time ---------------------------------------------------------------- */

#define PC_PIT_HZ 1193182u          /* PIT input clock: 14.31818 MHz / 12 */

typedef void (*PcIsr)(void);

/* PIT channel 0 divisor (0 means 65536, the BIOS default 18.2 Hz). */
void     pc_pit_set_divisor(uint16_t divisor);
uint16_t pc_pit_divisor(void);
/* INT 08h vector: the handler the game installed (NULL = BIOS: BDA tick only). */
void     pc_set_int08(PcIsr isr);
PcIsr    pc_get_int08(void);

/* Let time pass: runs the INT 08h handler for every PIT period that elapsed
   since the last call, and pumps host events (pc_set_host_pump).
   - real-time mode (default in the game): elapsed = host clock
   - virtual mode (tests, input replay): each call advances `pc_virtual_step_us`
     and no host clock is read, so runs are deterministic. */
void     pc_poll(void);
void     pc_set_virtual_time(int on, uint32_t step_us);
uint64_t pc_pit_ticks(void);        /* PIT input clocks since start (1.19 MHz) */
uint32_t pc_bios_ticks(void);       /* BIOS tick count 0040:006C (when the BIOS INT 08h runs) */
/* The BIOS INT 08h handler itself (BDA tick).  A handler the game installs
   that chains to the "old vector" calls this when that vector was the BIOS
   one (pc_get_int08() returned NULL). */
void     pc_bios_int08(void);

/* Wait for the next vertical retrace (70 Hz in modes 12h/13h).  Presents a
   frame to the host and keeps polling the timer while waiting. */
void     pc_wait_retrace(void);
/* Status bits of port 3DAh as the original reads them: bit 3 = vertical
   retrace, bit 0 = display disabled.  Calling it advances emulated time a
   little, so `while (!(inp(0x3DA) & 8));` loops terminate. */
uint8_t  pc_in_3da(void);

/* Real-time mode: forget the host time that passed since the last poll, so
   the emulated clock does not catch up on it - the host was paused (a modal
   dialog such as the About box).  No effect in virtual-time mode. */
void     pc_resync_clock(void);

/* Host hooks (set by sdl3_main.c): event pump, frame presentation. */
void     pc_set_host_pump(void (*pump)(void));
void     pc_set_host_present(void (*present)(void));

/* The video player (src/game/gfl.c) tells the host what it is doing - a port
   addition, so that the host can show a better copy of the video in place of
   the original picture (hdvideo.h).  OPEN: gfl_play() was called for `name`,
   nothing is loaded yet; START: frame 0 is on the screen and the video's
   clock runs from this moment (g_gfl_start_ms is set); STOP: playback is
   over.  Without a hook (the tests) the calls do nothing. */
enum { PC_VIDEO_OPEN, PC_VIDEO_START, PC_VIDEO_STOP };
void     pc_set_host_video(void (*hook)(int event, const char *name));
void     pc_video_event(int event, const char *name);
/* Optional: called at every input read of the game (kbhit, getch and each
   wake-up while it blocks, bioskey 0/1 likewise, INT 33h AX=3) before the
   read is served.  Used by the input-script mode of sdl3_main.c. */
void     pc_set_input_hook(void (*hook)(int wake));
/* Optional: sleep the host thread for about `us` microseconds (busy loops). */
void     pc_set_host_sleep(void (*sleep_us)(uint32_t us));

/* ---- keyboard (BIOS) ---------------------------------------------------------- */

/* Host side: push one BIOS key word (scan code << 8 | ASCII).  The 16-entry
   BIOS buffer drops keys when full, like the original. */
void     pc_key_push(uint16_t key);
/* Borland kbhit()/getch() over INT 16h: getch returns ASCII, or 0 followed by
   the scan code on the next call for extended keys. */
int16_t  pc_kbhit(void);
int16_t  pc_getch(void);
/* INT 21h AX=0C00h: empty the console's type-ahead (the BIOS buffer and the
   pending second byte of an extended key). */
void     pc_dos_flush_keys(void);
/* Borland bioskey(cmd) = INT 16h AH=cmd on the same buffer:
     0 / 10h  wait for a key, remove it, return its word (scan << 8 | ASCII)
     1 / 11h  peek: 0 if the buffer is empty, else the word without removing
              it (Borland returns -1 for a 0000h word, i.e. Ctrl+Break)
     2 / 12h  0200h | shift flags (the BIOS leaves AH alone and bioskey
              returns AX); 12h: extended flags in the high byte, kept 0
   The non-enhanced functions (0/1) translate gray-key words xxE0h to xx00h,
   like the BIOS.  Peeking polls (lets time pass) like pc_kbhit. */
int16_t  pc_bioskey(int16_t cmd);
/* Shift-state byte (BDA 0040:0017). */
uint8_t  pc_shift_flags(void);
/* Break handling (see pc.c "Ctrl-C / Ctrl-Break"): setvect(23h) / setvect(09h)
   of the game's handlers; pc_int09 = the host offers a make code to the
   game's INT 09h hook first (1 = swallowed); pc_ctrl_break = the BIOS saw
   Ctrl-Break (buffer emptied, word 0000h, INT 1Bh); pc_int1b = INT 1Bh
   alone (DOS's handler: a ^C for the console's next character, so the next
   kbhit() issues INT 23h) - what the game's INT 09h hook raises for a
   trapped Ctrl+Alt+Del.  pc_set_int23_observer: called right before INT 23h
   is issued (script mode: the `int23` checkpoint). */
void     pc_set_int23(void (*handler)(void));
void     pc_set_int23_observer(void (*observer)(void));
void     pc_set_int09(int (*handler)(uint8_t scan));
int      pc_int09(uint8_t scan);
void     pc_ctrl_break(void);
void     pc_int1b(void);
void     pc_set_shift_flags(uint8_t f);

/* ---- mouse (INT 33h) ------------------------------------------------------- */

typedef struct PcMouseRegs { uint16_t ax, bx, cx, dx; } PcMouseRegs;
/* Executes INT 33h with r as input, returns the output registers in r. */
void     pc_int33(PcMouseRegs *r);
/* Host side: mouse position in virtual-screen pixels (0..639, 0..199 in mode
   13h, as the driver reports them) and button bits (1 left, 2 right). */
void     pc_mouse_host(int16_t x, int16_t y, uint16_t buttons);

/* ---- EMS (INT 67h) --------------------------------------------------------- */

#define PC_EMS_PAGE      0x4000u
#define PC_EMS_FRAME_SEG 0xE000u    /* as in DOSBox-X with tools/dosbox/ist.conf */

/* The 64 KB page frame the game addresses (E000:0000-FFFF). */
extern uint8_t pc_ems_frame[0x10000];

/* Configure the amount of EMS (pages) before the game starts; default 2048 (32 MB). */
void     pc_ems_configure(uint16_t total_pages);
/* INT 67h, AH = function.  Returns AH (status, 0 = ok) and outputs like the
   driver.  Pointers in function 57h (move) are passed resolved: */
typedef struct PcEmsRegs { uint16_t ax, bx, cx, dx; } PcEmsRegs;
void     pc_int67(PcEmsRegs *r);
/* AH=57h AL=00h move memory region.  For a conventional side, `ptr` is the
   native address; for an EMS side, handle + logical page + offset. */
typedef struct PcEmsMove {
    uint32_t length;
    uint8_t  src_type;   uint16_t src_handle; uint16_t src_off; uint16_t src_page; uint8_t *src_ptr;
    uint8_t  dst_type;   uint16_t dst_handle; uint16_t dst_off; uint16_t dst_page; uint8_t *dst_ptr;
} PcEmsMove;
uint8_t  pc_ems_move(const PcEmsMove *m);   /* returns AH */
/* Direct view of a handle's logical memory (tests / debugging only). */
uint8_t *pc_ems_handle_mem(uint16_t handle, uint32_t *size);
/* EMM driver presence, i.e. whether DOS can open the character device
   "EMMXXXX0" and IOCTL 4400h on it succeeds (the DSMI EMS heap's detection,
   1154:0124).  Default: present.  pc_ems_set_present(0) = no EMS driver. */
void     pc_ems_set_present(int on);
int      pc_ems_detect(void);
/* INT 67h AH=4Eh (only the unreferenced DSMI stub 1154:0D52 uses it).
   AL=00h get the page map into buf, AL=01h set it from buf, AL=02h both,
   AL=03h return the map size (AL out).  The map is 4 x {u16 handle, u16
   page} little-endian (16 bytes; handle FFFFh = unmapped) - the format is
   driver-specific in reality.  Returns AH. */
#define PC_EMS_MAP_SIZE 16
uint8_t  pc_ems_page_map(uint8_t al, uint8_t *get_buf, const uint8_t *set_buf, uint8_t *size_out);

/* ---- VGA ------------------------------------------------------------------- */

extern VgaState pc_vga;

/* The A000:0000 window as the CPU sees it.  In chained mode 13h the game
   writes plain bytes to A000 (memcpy into "slot 0"); the port keeps those
   writes in this buffer and pc_vga_sync() pushes them through the VGA write
   path before scan-out.  Planar code (mode 12h) calls vga_mem_write8 directly
   and must not use this buffer.

   PC_A000_SLACK: the original's blits address the screen with a huge
   pointer and sometimes run past the 64 KB window - a text drawn at x = -1
   (ui_format_text, right-aligned and outlined, wider than its place) goes
   to offset 65535 + y*320, i.e. into B000, where nothing shows it.  In the
   port that write would leave the array and hit whatever the host's linker
   put behind it (macOS: the EMS page frame - a corrupted font, found by
   test_intrface.test_format_text; Windows: by luck nothing visible).  The
   64 KB behind the window take those writes, like B000 does; they are
   never shown. */
#define PC_A000_SIZE  0x10000u
#define PC_A000_SLACK 0x10000u
extern uint8_t pc_a000[PC_A000_SIZE + PC_A000_SLACK];
void     pc_vga_sync(void);

/* ---- files ----------------------------------------------------------------- */

/*
 * The game's files live in two folders and, for the CD edition, on the
 * virtual CD (src/platform/cdrom.c):
 *   DATA  the read-only originals (2005 edition; the CD edition has only
 *         LALE.CHD there, whose files are read through the virtual drive);
 *   SAVE  everything the game writes: saved games, GRP.DAT, SES.CFG.
 * A DOS name is looked up in SAVE, then DATA, then on the disc; files are
 * created in SAVE, and one of the originals opened for writing is copied
 * there first.  A name with the CD drive's letter is a file of the disc.
 * Names match without regard to case; '\\' separates folders.
 *
 * pc_set_game_dir(dir) makes both folders the same one: the DOS game's
 * working folder as it is (tests, --game-dir).
 */
void        pc_set_dirs(const char *data_dir, const char *save_dir);
void        pc_set_game_dir(const char *dir);
const char *pc_game_dir(void);              /* = the SAVE folder */
const char *pc_data_dir(void);
const char *pc_save_dir(void);
/* Take the files of the open virtual CD into the lookup (on != 0) or leave
   them out.  0, or -1 when the disc has no ISO 9660 volume. */
int         pc_use_disc_files(int on);
const char *pc_disc_volume_id(void);        /* "" without disc files */

typedef struct PcFile PcFile;
enum { PC_OPEN_READ, PC_OPEN_RDWR, PC_OPEN_CREATE };
PcFile  *pc_file_open(const char *dos_name, int mode);     /* NULL: not found / refused */
int      pc_file_exists(const char *dos_name);
void     pc_file_close(PcFile *f);
uint32_t pc_file_size(PcFile *f);
size_t   pc_file_read(PcFile *f, uint32_t pos, void *buf, size_t n);
size_t   pc_file_write(PcFile *f, uint32_t pos, const void *buf, size_t n);
int      pc_file_truncate(PcFile *f, uint32_t size);       /* 0 = ok */
int      pc_file_drive(const PcFile *f);    /* drive index: 2 = C:, the CD drive's for its files */
/* Upper-cased 8.3 names with the extension ext ("SAV") of all three places,
   each name once; returns their number (at most max). */
int      pc_list_files(const char *ext, char names[][13], int max);

/* Host stream / host path of a DOS name in the SAVE folder (host-side tools). */
FILE       *pc_fopen(const char *dos_name, const char *mode);
char       *pc_resolve(const char *dos_name, char *buf, size_t size);

/* ---- reset (tests) ----------------------------------------------------------- */
void pc_reset(void);

#endif
