/*
 * vga.c - register-level VGA emulation (see vga.h for scope).
 *
 * Reference behaviour: IBM VGA Technical Reference / FreeVGA register docs,
 * cross-checked against DOSBox-X (our oracle) where hardware docs are vague:
 *   - chain-4 CPU address A -> plane (A & 3), plane offset (A & ~3);
 *   - split screen starts on scan line LineCompare + 1;
 *   - reading 3DAh resets the attribute controller flip-flop.
 */
#include "vga.h"

#include <math.h>
#include <string.h>

/* ---- standard BIOS register sets ------------------------------------- */

typedef struct VgaModeRegs {
    uint8_t misc;
    uint8_t seq[5];
    uint8_t crtc[25];
    uint8_t gc[9];
    uint8_t attr[21];
} VgaModeRegs;

static const VgaModeRegs mode_03h = {
    0x67,
    { 0x03, 0x00, 0x03, 0x00, 0x02 },
    { 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x4F, 0x0D, 0x0E, 0x00,
      0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B,
      0x3C, 0x3D, 0x3E, 0x3F, 0x0C, 0x00, 0x0F, 0x08, 0x00 },
};

/* Verified against DOSBox-X after INT 10h AX=0013h (2026-11-20). */
static const VgaModeRegs mode_13h = {
    0x63,
    { 0x03, 0x01, 0x0F, 0x00, 0x0E },
    { 0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
      0x0C, 0x0D, 0x0E, 0x0F, 0x49, 0x00, 0x0F, 0x00, 0x00 },  /* 49h: DOSBox-X (IBM: 41h) */
};

/* 640x480x16 planar.  Used by DEMOLAR for the credit screens (comp4a/comp4b).
   Verified register for register against DOSBox-X after INT 10h AX=0012h
   (2026-11-20).  Attribute mode control is 09h there (blink enable set);
   IBM's BIOS table has 01h.  We follow the oracle.  Graphics-mode blinking
   (colours 8-15) is NOT rendered - TODO(verify) against DOSBox-X when the
   credit screens are checked. */
static const VgaModeRegs mode_12h = {
    0xE3,
    { 0x03, 0x01, 0x0F, 0x00, 0x06 },
    { 0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, 0x00, 0x40, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B,
      0x3C, 0x3D, 0x3E, 0x3F, 0x09, 0x00, 0x0F, 0x00, 0x00 },
};

/* EGA 64-colour palette loaded by the BIOS for 16-colour/text modes.
   Verified: identical to the DOSBox-X DAC after INT 10h AX=0012h. */
static void dac_load_ega64(VgaState *v)
{
    for (int i = 0; i < 64; i++) {
        v->dac[i][0] = (uint8_t)(((i >> 2) & 1) * 0x2A + ((i >> 5) & 1) * 0x15);
        v->dac[i][1] = (uint8_t)(((i >> 1) & 1) * 0x2A + ((i >> 4) & 1) * 0x15);
        v->dac[i][2] = (uint8_t)(((i >> 0) & 1) * 0x2A + ((i >> 3) & 1) * 0x15);
    }
}

/* Default 256-colour palette loaded by the VGA BIOS for mode 13h:
   16 EGA colours, 16 greys, 9 rings of 24 hues, 8 blacks.
   Verified: identical to the DOSBox-X DAC after INT 10h AX=0013h (2026-11-20). */
static void dac_load_default256(VgaState *v)
{
    static const uint8_t ega[16][3] = {
        {0x00,0x00,0x00},{0x00,0x00,0x2A},{0x00,0x2A,0x00},{0x00,0x2A,0x2A},
        {0x2A,0x00,0x00},{0x2A,0x00,0x2A},{0x2A,0x15,0x00},{0x2A,0x2A,0x2A},
        {0x15,0x15,0x15},{0x15,0x15,0x3F},{0x15,0x3F,0x15},{0x15,0x3F,0x3F},
        {0x3F,0x15,0x15},{0x3F,0x15,0x3F},{0x3F,0x3F,0x15},{0x3F,0x3F,0x3F} };
    static const uint8_t grey[16] = { 0x00,0x05,0x08,0x0B,0x0E,0x11,0x14,0x18,
                                      0x1C,0x20,0x24,0x28,0x2D,0x32,0x38,0x3F };
    static const uint8_t lv[9][5] = {   /* lo, l1, l2, l3, hi per ring */
        {0x00,0x10,0x1F,0x2F,0x3F},{0x1F,0x27,0x2F,0x37,0x3F},{0x2D,0x31,0x36,0x3A,0x3F},
        {0x00,0x07,0x0E,0x15,0x1C},{0x0E,0x11,0x15,0x18,0x1C},{0x14,0x16,0x18,0x1A,0x1C},
        {0x00,0x04,0x08,0x0C,0x10},{0x08,0x0A,0x0C,0x0E,0x10},{0x0B,0x0C,0x0D,0x0F,0x10} };
    /* hue ring as indices into lv[]: r, g, b */
    static const uint8_t ring[24][3] = {
        {0,0,4},{1,0,4},{2,0,4},{3,0,4},{4,0,4},{4,0,3},{4,0,2},{4,0,1},
        {4,0,0},{4,1,0},{4,2,0},{4,3,0},{4,4,0},{3,4,0},{2,4,0},{1,4,0},
        {0,4,0},{0,4,1},{0,4,2},{0,4,3},{0,4,4},{0,3,4},{0,2,4},{0,1,4} };
    int n = 0;
    for (int i = 0; i < 16; i++, n++)
        memcpy(v->dac[n], ega[i], 3);
    for (int i = 0; i < 16; i++, n++)
        v->dac[n][0] = v->dac[n][1] = v->dac[n][2] = grey[i];
    for (int r = 0; r < 9; r++)
        for (int h = 0; h < 24; h++, n++)
            for (int c = 0; c < 3; c++)
                v->dac[n][c] = lv[r][ring[h][c]];
    for (; n < 256; n++)
        v->dac[n][0] = v->dac[n][1] = v->dac[n][2] = 0;
}

static void load_regs(VgaState *v, const VgaModeRegs *m)
{
    v->misc_output = m->misc;
    memcpy(v->seq, m->seq, sizeof m->seq);
    memcpy(v->crtc, m->crtc, sizeof m->crtc);
    memcpy(v->gc, m->gc, sizeof m->gc);
    memcpy(v->attr, m->attr, sizeof m->attr);
    v->attr_index = 0x20;          /* palette address source = screen on */
    v->attr_flipflop = 0;
    v->pel_mask = 0xFF;
}

void vga_init(VgaState *v, uint64_t (*clock_us)(void))
{
    memset(v, 0, sizeof *v);
    v->clock_us = clock_us;
    vga_bios_set_mode(v, 0x03);
}

int vga_bios_set_mode(VgaState *v, uint8_t mode)
{
    int keep = mode & 0x80;
    switch (mode & 0x7F) {
    case 0x03:
        load_regs(v, &mode_03h);
        dac_load_ega64(v);
        if (!keep) {
            memset(v->plane, 0, sizeof v->plane);
            for (uint32_t i = 0; i < 0x8000; i += 2) {
                v->plane[0][i] = 0x20;   /* space */
                v->plane[1][i] = 0x07;   /* light grey on black */
            }
        }
        return 1;
    case 0x12:
        load_regs(v, &mode_12h);
        dac_load_ega64(v);
        if (!keep)
            memset(v->plane, 0, sizeof v->plane);
        return 1;
    case 0x13:
        load_regs(v, &mode_13h);
        dac_load_default256(v);
        if (!keep)
            memset(v->plane, 0, sizeof v->plane);
        return 1;
    default:
        return 0;
    }
}

/* ---- I/O ports -------------------------------------------------------- */

static double dot_clock_mhz(const VgaState *v)
{
    double mhz = ((v->misc_output >> 2) & 3) == 1 ? 28.322 : 25.175;
    if (v->seq[VGA_SEQ_CLOCKING] & 0x08)
        mhz /= 2.0;
    return mhz;
}

static int v_total(const VgaState *v)
{
    uint8_t o = v->crtc[VGA_CRTC_OVERFLOW];
    return (v->crtc[VGA_CRTC_V_TOTAL] | (o & 0x01) << 8 | (o & 0x20) << 4) + 2;
}

static int v_display_end(const VgaState *v)   /* last displayed line */
{
    uint8_t o = v->crtc[VGA_CRTC_OVERFLOW];
    return v->crtc[VGA_CRTC_V_DISPLAY_END] | (o & 0x02) << 7 | (o & 0x40) << 3;
}

static int v_retrace_start(const VgaState *v)
{
    uint8_t o = v->crtc[VGA_CRTC_OVERFLOW];
    return v->crtc[VGA_CRTC_V_RETRACE_START] | (o & 0x04) << 6 | (o & 0x80) << 2;
}

static double line_us(const VgaState *v)
{
    int dots = (v->seq[VGA_SEQ_CLOCKING] & 0x01) ? 8 : 9;
    return (double)(v->crtc[VGA_CRTC_H_TOTAL] + 5) * dots / dot_clock_mhz(v);
}

double vga_refresh_hz(const VgaState *v)
{
    return 1e6 / (line_us(v) * v_total(v));
}

static uint8_t status1(VgaState *v)
{
    if (!v->clock_us)
        return 0;
    double lu = line_us(v);
    double frame = lu * v_total(v);
    double t = fmod((double)v->clock_us(), frame);      /* both in microseconds */
    int line = (int)(t / lu);
    double hfrac = (t - line * lu) / lu;
    uint8_t s = 0;
    int vrs = v_retrace_start(v);
    int len = ((v->crtc[VGA_CRTC_V_RETRACE_END] & 0x0F) - (vrs & 0x0F)) & 0x0F;
    if (len == 0)
        len = 16;
    if (line >= vrs && line < vrs + len)
        s |= 0x08;
    int hde = v->crtc[VGA_CRTC_H_DISPLAY_END] + 1;
    if (line > v_display_end(v) || hfrac >= (double)hde / (v->crtc[VGA_CRTC_H_TOTAL] + 5))
        s |= 0x01;
    return s;
}

void vga_out8(VgaState *v, uint16_t port, uint8_t value)
{
    switch (port) {
    case VGA_PORT_ATTR:
        if (!v->attr_flipflop)
            v->attr_index = value & 0x3F;
        else
            v->attr[v->attr_index & 0x1F] = value;
        v->attr_flipflop ^= 1;
        break;
    case VGA_PORT_MISC_W:
        v->misc_output = value;
        break;
    case VGA_PORT_SEQ_INDEX:
        v->seq_index = value & 0x07;
        break;
    case VGA_PORT_SEQ_DATA:
        v->seq[v->seq_index] = value;
        break;
    case VGA_PORT_PEL_MASK:
        v->pel_mask = value;
        break;
    case VGA_PORT_DAC_READ:
        v->dac_read_index = value;
        v->dac_read_comp = 0;
        v->dac_state = 0x03;
        break;
    case VGA_PORT_DAC_WRITE:
        v->dac_write_index = value;
        v->dac_write_comp = 0;
        v->dac_state = 0x00;
        break;
    case VGA_PORT_DAC_DATA:
        v->dac_latch[v->dac_write_comp++] = value & 0x3F;
        if (v->dac_write_comp == 3) {
            memcpy(v->dac[v->dac_write_index], v->dac_latch, 3);
            v->dac_write_index++;
            v->dac_write_comp = 0;
        }
        break;
    case VGA_PORT_GC_INDEX:
        v->gc_index = value & 0x0F;
        break;
    case VGA_PORT_GC_DATA:
        v->gc[v->gc_index] = value;
        break;
    case VGA_PORT_CRTC_INDEX:
    case 0x3B4:
        v->crtc_index = value & 0x1F;
        break;
    case VGA_PORT_CRTC_DATA:
    case 0x3B5:
        if ((v->crtc[0x11] & 0x80) && v->crtc_index <= 7) {
            /* write-protected; only the line-compare bit of 07h passes */
            if (v->crtc_index == 7)
                v->crtc[7] = (uint8_t)((v->crtc[7] & ~0x10) | (value & 0x10));
            break;
        }
        v->crtc[v->crtc_index] = value;
        break;
    default:
        break;
    }
}

void vga_out16(VgaState *v, uint16_t port, uint16_t value)
{
    vga_out8(v, port, (uint8_t)value);
    vga_out8(v, (uint16_t)(port + 1), (uint8_t)(value >> 8));
}

uint8_t vga_in8(VgaState *v, uint16_t port)
{
    uint8_t r;
    switch (port) {
    case VGA_PORT_ATTR:       return v->attr_index;
    case VGA_PORT_ATTR_R:     return v->attr[v->attr_index & 0x1F];
    case VGA_PORT_MISC_R:     return v->misc_output;
    case VGA_PORT_SEQ_INDEX:  return v->seq_index;
    case VGA_PORT_SEQ_DATA:   return v->seq[v->seq_index];
    case VGA_PORT_PEL_MASK:   return v->pel_mask;
    case VGA_PORT_DAC_READ:   return v->dac_state;
    case VGA_PORT_DAC_WRITE:  return v->dac_write_index;
    case VGA_PORT_DAC_DATA:
        r = v->dac[v->dac_read_index][v->dac_read_comp++];
        if (v->dac_read_comp == 3) {
            v->dac_read_index++;
            v->dac_read_comp = 0;
        }
        return r;
    case VGA_PORT_GC_INDEX:   return v->gc_index;
    case VGA_PORT_GC_DATA:    return v->gc[v->gc_index];
    case VGA_PORT_CRTC_INDEX: return v->crtc_index;
    case VGA_PORT_CRTC_DATA:  return v->crtc[v->crtc_index];
    case VGA_PORT_STATUS1:
    case 0x3BA:
        v->attr_flipflop = 0;
        return status1(v);
    default:
        return 0xFF;
    }
}

/* ---- CPU memory access ------------------------------------------------- */

/* Map a linear CPU address into the window selected by GC misc bits 2-3.
   Returns the window offset, or -1 when the address is not decoded. */
static int32_t window_offset(const VgaState *v, uint32_t addr)
{
    switch ((v->gc[VGA_GC_MISC] >> 2) & 3) {
    case 0: return (addr >= 0xA0000 && addr < 0xC0000) ? (int32_t)(addr - 0xA0000) : -1;
    case 1: return (addr >= 0xA0000 && addr < 0xB0000) ? (int32_t)(addr - 0xA0000) : -1;
    case 2: return (addr >= 0xB0000 && addr < 0xB8000) ? (int32_t)(addr - 0xB0000) : -1;
    default: return (addr >= 0xB8000 && addr < 0xC0000) ? (int32_t)(addr - 0xB8000) : -1;
    }
}

static uint8_t alu(const VgaState *v, uint8_t d, uint8_t latch)
{
    switch ((v->gc[VGA_GC_DATA_ROTATE] >> 3) & 3) {
    case 1:  return d & latch;
    case 2:  return d | latch;
    case 3:  return d ^ latch;
    default: return d;
    }
}

static void write_planes(VgaState *v, uint32_t off, uint8_t val, uint8_t enable)
{
    int rot = v->gc[VGA_GC_DATA_ROTATE] & 7;
    uint8_t r = (uint8_t)(((unsigned)val >> rot) | ((unsigned)val << (8 - rot)));
    uint8_t mask = v->gc[VGA_GC_BIT_MASK];
    uint8_t sr = v->gc[VGA_GC_SET_RESET], esr = v->gc[VGA_GC_ENABLE_SET_RESET];
    off &= 0xFFFF;
    for (int p = 0; p < 4; p++) {
        if (!(enable & (1 << p)))
            continue;
        uint8_t l = v->latch[p], d;
        switch (v->gc[VGA_GC_MODE] & 3) {
        case 0:
            d = (esr & (1 << p)) ? ((sr & (1 << p)) ? 0xFF : 0x00) : r;
            d = alu(v, d, l);
            d = (uint8_t)((d & mask) | (l & ~mask));
            break;
        case 1:
            d = l;
            break;
        case 2:
            d = (val & (1 << p)) ? 0xFF : 0x00;
            d = alu(v, d, l);
            d = (uint8_t)((d & mask) | (l & ~mask));
            break;
        default: {
            uint8_t m = r & mask;
            d = (sr & (1 << p)) ? 0xFF : 0x00;
            d = alu(v, d, l);
            d = (uint8_t)((d & m) | (l & ~m));
            break;
        }
        }
        v->plane[p][off] = d;
    }
}

void vga_mem_write8(VgaState *v, uint32_t addr, uint8_t value)
{
    int32_t w = window_offset(v, addr);
    if (w < 0)
        return;
    uint32_t a = (uint32_t)w;
    uint8_t mapmask = v->seq[VGA_SEQ_MAP_MASK] & 0x0F;
    if (v->seq[VGA_SEQ_MEMORY_MODE] & 0x08) {              /* chain-4 */
        write_planes(v, a & ~3u, value, (uint8_t)(mapmask & (1 << (a & 3))));
    } else if (!(v->seq[VGA_SEQ_MEMORY_MODE] & 0x04)) {    /* odd/even */
        /* even addresses -> plane 0 (and 2), odd -> plane 1 (and 3) */
        uint8_t sel = (a & 1) ? 0x0A : 0x05;
        write_planes(v, a & ~1u, value, (uint8_t)(mapmask & sel));
    } else {
        write_planes(v, a, value, mapmask);
    }
}

uint8_t vga_mem_read8(VgaState *v, uint32_t addr)
{
    int32_t w = window_offset(v, addr);
    if (w < 0)
        return 0xFF;
    uint32_t a = (uint32_t)w, off;
    int plane;
    if (v->seq[VGA_SEQ_MEMORY_MODE] & 0x08) {
        off = a & ~3u;
        plane = (int)(a & 3);
    } else if (!(v->seq[VGA_SEQ_MEMORY_MODE] & 0x04)) {
        off = a & ~1u;
        plane = (int)(a & 1);
    } else {
        off = a;
        plane = v->gc[VGA_GC_READ_MAP] & 3;
    }
    off &= 0xFFFF;
    for (int p = 0; p < 4; p++)
        v->latch[p] = v->plane[p][off];
    if (!(v->gc[VGA_GC_MODE] & 0x08))
        return v->latch[plane];
    /* read mode 1: colour compare */
    uint8_t cc = v->gc[VGA_GC_COLOR_COMPARE], dc = v->gc[VGA_GC_COLOR_DONT_CARE];
    uint8_t res = 0xFF;
    for (int p = 0; p < 4; p++) {
        if (!(dc & (1 << p)))
            continue;
        uint8_t want = (cc & (1 << p)) ? 0xFF : 0x00;
        res &= (uint8_t)~(v->latch[p] ^ want);
    }
    return res;
}

/* ---- scan-out --------------------------------------------------------- */

static uint32_t scan_address(const VgaState *v, uint32_t ctr)
{
    ctr &= 0xFFFF;
    if (v->crtc[VGA_CRTC_UNDERLINE] & 0x40)              /* doubleword */
        return ((ctr << 2) | ((ctr >> 14) & 3)) & 0xFFFF;
    if (!(v->crtc[VGA_CRTC_MODE] & 0x40)) {              /* word mode */
        int bit = (v->crtc[VGA_CRTC_MODE] & 0x20) ? 15 : 13;
        return ((ctr << 1) | ((ctr >> bit) & 1)) & 0xFFFF;
    }
    return ctr;                                           /* byte mode */
}

/* Attribute controller: 4-bit pixel -> DAC index (16-colour modes). */
static uint8_t attr_lookup(const VgaState *v, uint8_t pix)
{
    uint8_t pal = v->attr[pix & v->attr[0x12] & 0x0F] & 0x3F;
    uint8_t csel = v->attr[0x14];
    if (v->attr[0x10] & 0x80)                    /* P5,P4 from colour select */
        pal = (uint8_t)((pal & 0x0F) | (csel & 0x03) << 4);
    return (uint8_t)(pal | (csel & 0x0C) << 4);  /* P7,P6 from colour select */
}

void vga_render(const VgaState *v, VgaFrame *out)
{
    memcpy(out->dac, v->dac, sizeof out->dac);
    int chars = v->crtc[VGA_CRTC_H_DISPLAY_END] + 1;
    int lines = v_display_end(v) + 1;
    int graphics = v->attr[0x10] & 0x01;
    int is256 = graphics && (v->gc[VGA_GC_MODE] & 0x40) && (v->attr[0x10] & 0x40);
    out->width = chars * (is256 ? 4 : 8);
    out->height = lines;
    int scan_per_row = ((v->crtc[VGA_CRTC_MAX_SCAN] & 0x1F) + 1) *
                       ((v->crtc[VGA_CRTC_MAX_SCAN] & 0x80) ? 2 : 1);
    out->line_repeat = scan_per_row;
    out->blanked = (v->seq[VGA_SEQ_CLOCKING] & 0x20) || !(v->attr_index & 0x20) || !graphics;
    if (out->width * out->height > (int)sizeof out->pixels) {
        out->width = out->height = 0;
        return;
    }
    if (out->blanked) {
        memset(out->pixels, v->attr[0x11] & v->pel_mask, (size_t)out->width * out->height);
        return;
    }
    uint32_t start = (uint32_t)v->crtc[VGA_CRTC_START_HI] << 8 | v->crtc[VGA_CRTC_START_LO];
    uint32_t offset = (uint32_t)v->crtc[VGA_CRTC_OFFSET] * 2;
    int lc = v->crtc[VGA_CRTC_LINE_COMPARE] | (v->crtc[VGA_CRTC_OVERFLOW] & 0x10) << 4 |
             (v->crtc[VGA_CRTC_MAX_SCAN] & 0x40) << 3;
    uint32_t row = start;
    int row_line = 0, split = 0;
    uint8_t buf[8 * 129 + 8];
    for (int y = 0; y < lines; y++) {
        if (y == lc + 1) {
            row = 0;
            row_line = 0;
            split = 1;
        }
        int panreg = (split && (v->attr[0x10] & 0x20)) ? 0 : (v->attr[0x13] & 0x0F);
        int n = 0;
        if (is256) {
            for (int c = 0; c <= chars && c < 128; c++) {
                uint32_t a = scan_address(v, row + (uint32_t)c);
                for (int p = 0; p < 4; p++)
                    buf[n++] = v->plane[p][a];
            }
        } else {
            /* one byte per plane per character clock, 8 pixels, MSB first */
            for (int c = 0; c <= chars && c < 128; c++) {
                uint32_t a = scan_address(v, row + (uint32_t)c);
                uint8_t b0 = v->plane[0][a], b1 = v->plane[1][a];
                uint8_t b2 = v->plane[2][a], b3 = v->plane[3][a];
                for (int bit = 7; bit >= 0; bit--)
                    buf[n++] = attr_lookup(v, (uint8_t)(((b0 >> bit) & 1) | ((b1 >> bit) & 1) << 1 |
                                                        ((b2 >> bit) & 1) << 2 | ((b3 >> bit) & 1) << 3));
            }
        }
        /* 256-colour: pan in pixel pairs (bits 2-1); 16-colour: 0-7 pixels */
        int pan = is256 ? (panreg & 7) >> 1 : (panreg < 8 ? panreg : 0);
        uint8_t *dst = out->pixels + (size_t)y * out->width;
        for (int x = 0; x < out->width; x++)
            dst[x] = buf[x + pan] & v->pel_mask;
        if (++row_line == scan_per_row) {
            row_line = 0;
            row += offset;
        }
    }
}
