/*
 * vga.h - register-level emulation of the IBM VGA, limited to what
 *         İstanbul Efsaneleri - Lale Savaşçıları uses.
 *
 * The original program drives the VGA directly: it writes the sequencer map
 * mask, uses the graphics-controller latches and write modes, programs the
 * CRTC (start address, line-compare split screen, scan doubling) and the DAC.
 * To reproduce every frame bit-exactly, the port keeps the same hardware model
 * and lets the decompiled code talk to it the way the original did. The host
 * layer only presents whatever scan-out the emulated registers produce.
 *
 * Covered:
 *   - 4 x 64 KB planes, CPU reads/writes through the graphics controller
 *     (write modes 0-3, rotate, logical op, set/reset, bit mask, latches,
 *     read modes 0-1), chain-4 and odd/even addressing, memory map select;
 *   - sequencer, graphics controller, CRTC, attribute controller (with its
 *     index/data flip-flop), DAC (6-bit, read/write index, PEL mask);
 *   - input status 1 (3DAh) derived from a microsecond clock using the
 *     programmed CRTC timings;
 *   - scan-out of 256-colour modes (chained mode 13h and unchained "mode X"):
 *     start address, offset, byte/word/dword addressing, max scan line,
 *     double scan, line compare, 256-colour pel panning, screen blanking.
 *   - BIOS mode set (INT 10h AH=00h) register tables for modes 03h, 12h and 13h.
 * Also: 16-colour planar scan-out (mode 12h, used by the credit screens).
 * Not covered (not used by the game): text-mode
 * scan-out (text output is handled by the console layer), CGA/Hercules modes.
 */
#ifndef LALE_PLATFORM_VGA_H
#define LALE_PLATFORM_VGA_H

#include <stdint.h>

#define VGA_PLANE_SIZE 0x10000u

/* I/O ports */
enum {
    VGA_PORT_ATTR        = 0x3C0, /* attribute index/data (write) */
    VGA_PORT_ATTR_R      = 0x3C1, /* attribute data (read) */
    VGA_PORT_MISC_W      = 0x3C2,
    VGA_PORT_SEQ_INDEX   = 0x3C4,
    VGA_PORT_SEQ_DATA    = 0x3C5,
    VGA_PORT_PEL_MASK    = 0x3C6,
    VGA_PORT_DAC_READ    = 0x3C7, /* write: read index; read: DAC state */
    VGA_PORT_DAC_WRITE   = 0x3C8,
    VGA_PORT_DAC_DATA    = 0x3C9,
    VGA_PORT_MISC_R      = 0x3CC,
    VGA_PORT_GC_INDEX    = 0x3CE,
    VGA_PORT_GC_DATA     = 0x3CF,
    VGA_PORT_CRTC_INDEX  = 0x3D4,
    VGA_PORT_CRTC_DATA   = 0x3D5,
    VGA_PORT_STATUS1     = 0x3DA
};

/* Sequencer registers */
enum { VGA_SEQ_RESET = 0, VGA_SEQ_CLOCKING = 1, VGA_SEQ_MAP_MASK = 2,
       VGA_SEQ_CHARMAP = 3, VGA_SEQ_MEMORY_MODE = 4 };

/* Graphics controller registers */
enum { VGA_GC_SET_RESET = 0, VGA_GC_ENABLE_SET_RESET = 1, VGA_GC_COLOR_COMPARE = 2,
       VGA_GC_DATA_ROTATE = 3, VGA_GC_READ_MAP = 4, VGA_GC_MODE = 5, VGA_GC_MISC = 6,
       VGA_GC_COLOR_DONT_CARE = 7, VGA_GC_BIT_MASK = 8 };

/* CRTC registers (those the scan-out uses) */
enum { VGA_CRTC_H_TOTAL = 0x00, VGA_CRTC_H_DISPLAY_END = 0x01, VGA_CRTC_V_TOTAL = 0x06,
       VGA_CRTC_OVERFLOW = 0x07, VGA_CRTC_MAX_SCAN = 0x09, VGA_CRTC_START_HI = 0x0C,
       VGA_CRTC_START_LO = 0x0D, VGA_CRTC_V_RETRACE_START = 0x10,
       VGA_CRTC_V_RETRACE_END = 0x11, VGA_CRTC_V_DISPLAY_END = 0x12, VGA_CRTC_OFFSET = 0x13,
       VGA_CRTC_UNDERLINE = 0x14, VGA_CRTC_MODE = 0x17, VGA_CRTC_LINE_COMPARE = 0x18 };

typedef struct VgaState {
    uint8_t plane[4][VGA_PLANE_SIZE];
    uint8_t latch[4];

    uint8_t misc_output;
    uint8_t seq_index, seq[8];
    uint8_t gc_index, gc[16];
    uint8_t crtc_index, crtc[32];
    uint8_t attr_index;           /* bit 5 = palette address source (screen on) */
    uint8_t attr[32];
    uint8_t attr_flipflop;        /* 0: next 3C0h write is an index */

    uint8_t dac[256][3];          /* 6-bit components */
    uint8_t dac_write_index, dac_read_index;
    uint8_t dac_write_comp, dac_read_comp;
    uint8_t dac_latch[3];
    uint8_t dac_state;            /* value read back from 3C7h */
    uint8_t pel_mask;

    uint64_t (*clock_us)(void);   /* host clock for 3DAh timing */
} VgaState;

/* A finished frame: one DAC index per pixel (PEL mask applied) and a copy
   of the 6-bit DAC at the time of the snapshot. */
typedef struct VgaFrame {
    int     width, height;        /* height = displayed scan lines */
    int     line_repeat;          /* scan lines per source row (2 for 13h) */
    uint8_t blanked;              /* screen disabled: all overscan colour */
    uint8_t pixels[800 * 600];
    uint8_t dac[256][3];
} VgaFrame;

void    vga_init(VgaState *v, uint64_t (*clock_us)(void));

/* BIOS INT 10h AH=00h: load the standard register set for a mode.
   Bit 7 of mode set = do not clear video memory (as the BIOS). */
int     vga_bios_set_mode(VgaState *v, uint8_t mode);

void    vga_out8(VgaState *v, uint16_t port, uint8_t value);
void    vga_out16(VgaState *v, uint16_t port, uint16_t value);  /* index, data */
uint8_t vga_in8(VgaState *v, uint16_t port);

/* CPU memory access; addr is a linear address in A0000h..BFFFFh. */
void    vga_mem_write8(VgaState *v, uint32_t addr, uint8_t value);
uint8_t vga_mem_read8(VgaState *v, uint32_t addr);

/* Frame timing derived from the programmed CRTC + dot clock. */
double  vga_refresh_hz(const VgaState *v);

void    vga_render(const VgaState *v, VgaFrame *out);

#endif
