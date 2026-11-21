/*
 * mcp.c - DSMI MCP v3.5 software mixer, decompiled from SDISB16.DSD.
 *
 * Addresses are SDISB16.DSD image offsets (code segment 0000; the data
 * segment 03D3 starts at image +3D30, data offsets are quoted as "d:XXXX").
 * See mcp.h for the API and docs/DSMI_MCP.md for the algorithm.
 *
 * Memory model.  The original keeps its volume tables, the 8-bit output
 * table and the mixing accumulators in one 64 KB segment (d:0A0A, allocated
 * by initDevice, 0x4030 or 0xA030 bytes) addressed with 16-bit offsets; the
 * port keeps the same layout in mcp_tab[] so that out-of-range lookups land
 * where the original's would (bytes past the original allocation read 0
 * here, unknown memory there).  The DMA ring is mcp_dma_buffer[] at offset 0.
 *
 * Verified: tests/py/test_mcp.py runs SDISB16.DSD in Unicorn (tools/dsdload.py)
 * and compares the DMA ring, the channel state and the CDI results after
 * identical command/poll sequences, 2026-11-20.
 */
#include "dsmi/mcp.h"

#include <string.h>

#include "dsmi/memalloc.h"
#include "platform/pc.h"

/* ------------------------------------------------------------------------ */
/* state (driver data segment)                                               */
/* ------------------------------------------------------------------------ */

typedef struct McpChannel {     /* d:0A65 + ch*0x28 (offsets in the original record) */
    uint16_t flags;             /* +00 bit0 playing, bit1 looped, bit2 muted, bit3 sample set */
    CdiSampleRef sample;        /* +02 far ptr to the data (as set, or the real address) */
    uint8_t *data;              /*     native pointer used for mixing (= sample.ptr / real address) */
    uint32_t length;            /* +06 */
    uint32_t step;              /* +0A rate step, 9.23 fixed point (freq << 23) / mixrate */
    uint32_t pos;               /* +0E integer position (bytes) */
    uint32_t loopstart;         /* +12 */
    uint32_t loopend;           /* +16 0 = no loop */
    uint8_t  volume;            /* +1A 0..32 */
    uint32_t freq;              /* +1B Hz */
    uint16_t step_int;          /* +1F freq / mixrate */
    uint16_t step_frac;         /* +21 ((freq % mixrate) << 16) / mixrate */
    uint16_t pos_frac;          /* +23 */
    uint16_t handle;            /* +25 sampleID: virtual (EMS) sample */
    int8_t   panning;           /* +27 -63..63, 100 = surround (only stored in stereo) */
} McpChannel;

typedef struct McpSlot {        /* d:1D7C + n*0x13: SAMPLEINFO copies for queued setSample */
    CdiSampleRef sample;        /* +00 */
    uint32_t length;            /* +04 */
    uint32_t loopstart;         /* +08 */
    uint32_t loopend;           /* +0C */
    uint8_t  mode;              /* +10 */
    uint16_t sampleID;          /* +11 */
} McpSlot;

typedef struct McpEvent {       /* d:0FB0 + n*10: queued command */
    uint32_t time;              /* +0 GetDSMITime() when issued */
    uint32_t value;             /* +4 */
    uint8_t  vol;               /* +8 playSample volume (stale for other commands) */
    uint8_t  cmdch;             /* +9 command << 5 | channel */
} McpEvent;

#define MCP_QUEUE_LEN 350       /* 0x15E */

enum { CMD_SAMPLE, CMD_PLAY, CMD_VOLUME, CMD_FREQ, CMD_POSITION, CMD_PANNING };

static struct {
    McpConfig cfg;              /* d:0870 (+ MCPOutputBuffer d:087C) */
    uint8_t  flags;             /* d:088E bit1 initialised, bit2 output started */
    uint16_t bufsize;           /* d:08F7 */
    uint16_t options;           /* d:0A00 */
    uint16_t amp;               /* d:0A02 amplification in % */
    uint16_t wrapped;           /* d:0A10 */
    uint8_t  loop_hit;          /* d:0A12 */
    uint8_t  master;            /* d:0A18 0..64 */
    uint16_t rate;              /* d:0A19 */
    uint16_t acc_size;          /* d:0A1B bytes per accumulated sample (1/2: direct, 2/4: accumulate) */
    uint16_t frames;            /* d:0A1D frames to mix now */
    uint16_t remain;            /* d:0A1F */
    uint16_t bpf;               /* d:0A21 bytes per output frame */
    uint16_t mixpos;            /* d:0A23 */
    uint8_t  wide;              /* d:0A25 mixer: 0 = 16-bit adds (181E/18DF), 1 = 32-bit (1C51/1D51) */
    uint16_t nch;               /* d:0A27 */
    uint32_t last_poll;         /* d:0A29 */
    uint16_t busy;              /* d:0A2D */
    uint32_t dmapos;            /* d:0A2F */
    uint8_t  pan_class;         /* d:0A3D 0, 3Fh, C1h or 64h */
    int8_t   pan;               /* d:0A3E */
    uint16_t dma_fill;          /* d:0A39 (low word) DMA ring silence */
    uint16_t chunk_rest;        /* d:0A5F */
    McpEffectFn effect;         /* d:0A61 */
    McpChannel ch[32];          /* d:0A65 */
    uint16_t voltab[32];        /* d:0F6D */
    McpEvent queue[MCP_QUEUE_LEN]; /* d:0FB0 */
    McpEvent pending;           /* d:1D5C */
    McpEvent cur;               /* d:1D66 */
    uint32_t q_read, q_write, q_snap;   /* d:1D70, 1D74, 1D78 */
    McpSlot slots[32];          /* d:1D7C */
    uint16_t slot;              /* d:1FDC */
    int16_t  first_channel;     /* d:31F6 (driver shell) */
} M;

uint8_t mcp_dma_buffer[0x10000 + 16];
static uint8_t mcp_tab[0x10000 + 16];          /* segment d:0A0A */
volatile uint16_t mcp_dma_position;

/* imports (d:1FE0..1FF0) */
static McpTimeFn        imp_time;
static McpRealAddressFn imp_real;
static McpVoidFn        imp_enable, imp_disable;
static McpBufPosFn      imp_bufpos;

static uint16_t default_far_offset(const uint8_t *p)
{
    if (p >= pc_ems_frame && p < pc_ems_frame + 0x10000)
        return (uint16_t)(p - pc_ems_frame);
    return 0;
}
uint16_t (*mcp_far_offset_hook)(const uint8_t *p) = default_far_offset;

/* code-segment tables */
static const uint8_t  vol_steps[32] = {        /* 16E5: 1..31, 31 */
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 31 };
static const uint16_t amp_table[4] = { 100, 200, 350, 500 };          /* 1725 */
static const uint16_t auto_amp[32] = {                                  /* 172D */
    70, 80, 90, 100, 100, 120, 140, 160, 180, 200, 220, 240, 260, 280, 300, 320,
    340, 360, 380, 400, 420, 440, 460, 480, 500, 500, 500, 500, 500, 500, 500, 500 };
static const uint16_t post8_div[33] = {                                 /* 2C5E */
    0, 170, 190, 220, 300, 340, 420, 485, 540, 590, 630, 670, 700, 720, 730, 740,
    750, 760, 765, 770, 775, 775, 780, 780, 780, 785, 785, 785, 785, 785, 785, 785, 785 };

static inline uint16_t rd16(const uint8_t *b, uint16_t off) { return (uint16_t)(b[off] | b[(uint16_t)(off + 1)] << 8); }
static inline void wr16(uint8_t *b, uint16_t off, uint16_t v) { b[off] = (uint8_t)v; b[(uint16_t)(off + 1)] = (uint8_t)(v >> 8); }
static inline uint32_t rd32(const uint8_t *b, uint16_t off)
{ return (uint32_t)rd16(b, off) | (uint32_t)rd16(b, (uint16_t)(off + 2)) << 16; }
static inline void wr32(uint8_t *b, uint16_t off, uint32_t v)
{ wr16(b, off, (uint16_t)v); wr16(b, (uint16_t)(off + 2), (uint16_t)(v >> 16)); }

/* ------------------------------------------------------------------------ */
/* source access: es:si with a 16-bit si                                     */
/* ------------------------------------------------------------------------ */

typedef struct Src {
    uintptr_t base;             /* address of es:0000 */
    uintptr_t lo, hi;           /* readable native range */
} Src;

static inline uint8_t src_rd(const Src *s, uint16_t si)
{
    uintptr_t a = s->base + si;
    /* Outside the sample (only after a 16-bit offset wrap, or one byte past
     * the end): the original reads whatever memory is there. */
    return (a >= s->lo && a < s->hi) ? *(const uint8_t *)a : 0x80;
}

/* ------------------------------------------------------------------------ */
/* inner mixing loops 181E/18DF (16-bit adds) and 1C51/1D51 (32-bit adds)    */
/* ------------------------------------------------------------------------ */

/*
 * mix_loop - one channel, `count` frames.  edi = frac<<16 | di and
 * edx = step_frac<<16 | stride are added as 32-bit values, the carry out of
 * the fraction steps si (ADD EDI,EDX / ADC SI,BP).  The sample byte is read
 * before the step and used unsigned as the table column (0 = -128).
 * Returns the new fraction; *si and *di are advanced.
 *
 * Original: 181E (mono, words), 18DF (stereo, words), 1C51 (mono, dwords),
 * 1D51 (stereo, dwords), near, register interface.  Each is unrolled 8x with
 * a Duff's-device entry (tables 176E..180E); the order of operations per
 * sample is the same in every variant.
 */
static uint16_t mix_loop(const Src *src, uint16_t *si_io, uint16_t *di_io, uint16_t frac,
                         uint16_t step_int, uint16_t step_frac, uint16_t count,
                         uint8_t *gs, uint8_t vl, uint8_t vr)
{
    const int wide = M.wide;
    const uint16_t half = wide ? 4 : 2;                 /* one output sample */
    const int stereo = M.cfg.stereo != 0;
    const uint16_t stride = (uint16_t)(stereo ? 2 * half : half);
    int mode;                                           /* 0 single, 1 both same, 2 panned, 3 surround */
    uint32_t edi, edx = (uint32_t)step_frac << 16 | stride;
    uint16_t si = *si_io;

    if (!stereo || (M.pan_class != 0x64 && M.pan_class != 0))
        mode = 0;                                       /* mono out, or hard left/right */
    else if (M.pan_class == 0x64)
        mode = 3;
    else
        mode = M.pan != 0 ? 2 : 1;

    edi = (uint32_t)frac << 16 | (uint16_t)(*di_io - stride);   /* SUB DI,stride */
    for (uint16_t k = 0; k < count; k++) {
        uint8_t s = src_rd(src, si);
        uint32_t before = edi;
        edi += edx;
        si = (uint16_t)(si + step_int + (edi < before));
        uint16_t di = (uint16_t)edi;
        if (wide) {
            uint32_t a = rd32(mcp_tab, (uint16_t)(((uint16_t)vl << 8 | s) * 4));
            wr32(gs, di, rd32(gs, di) + a);
            if (mode == 1 || mode == 3) {
                uint16_t d2 = (uint16_t)(di + 4);
                wr32(gs, d2, mode == 1 ? rd32(gs, d2) + a : rd32(gs, d2) - a);
            } else if (mode == 2) {
                uint32_t b = rd32(mcp_tab, (uint16_t)(((uint16_t)vr << 8 | s) * 4));
                uint16_t d2 = (uint16_t)(di + 4);
                wr32(gs, d2, rd32(gs, d2) + b);
            }
        } else {
            uint16_t a = rd16(mcp_tab, (uint16_t)(((uint16_t)vl << 8 | s) * 2));
            wr16(gs, di, (uint16_t)(rd16(gs, di) + a));
            if (mode == 1 || mode == 3) {
                uint16_t d2 = (uint16_t)(di + 2);
                wr16(gs, d2, (uint16_t)(mode == 1 ? rd16(gs, d2) + a : rd16(gs, d2) - a));
            } else if (mode == 2) {
                uint16_t b = rd16(mcp_tab, (uint16_t)(((uint16_t)vr << 8 | s) * 2));
                uint16_t d2 = (uint16_t)(di + 2);
                wr16(gs, d2, (uint16_t)(rd16(gs, d2) + b));
            }
        }
    }
    *si_io = si;
    *di_io = (uint16_t)((uint16_t)edi + stride);        /* ADD DI,stride */
    return (uint16_t)(edi >> 16);
}

/* ------------------------------------------------------------------------ */
/* one mixing pass: 2276                                                     */
/* ------------------------------------------------------------------------ */

/*
 * mix_pass - mix M.frames frames of every channel at M.mixpos.
 *
 * Original: 2276, near.  Direct mode (options bit0 clear): the DMA ring
 * region is cleared and the channels are added into it as 16-bit words (no
 * clipping, wrap-around).  Accumulate mode: words (8-bit output) at
 * tab:4000 or dwords (16-bit output) at tab:8000, then converted (2512).
 */
static void mix_pass(void)
{
    uint8_t *gs;
    uint16_t di;
    const int stereo = M.cfg.stereo == 1;

    if (!(M.options & MCP_OPT_ACCUMULATE)) {
        gs = mcp_dma_buffer;
        di = M.mixpos;                                  /* d:0A06 (offset 0) + d:0A23 */
    } else {
        gs = mcp_tab;
        di = M.acc_size == 4 ? 0x8000 : 0x4000;
    }
    {   /* 21D9: clear frames*acc_size (*2 stereo) bytes with d:0A35 (always 0) */
        uint16_t n = (uint16_t)(M.frames * M.acc_size);
        if (stereo)
            n = (uint16_t)(n << 1);
        for (uint16_t i = 0; i < n; i++)
            gs[(uint16_t)(di + i)] = 0;
    }

    for (uint16_t c = 0; c < M.nch; c++) {
        McpChannel *ch = &M.ch[c];
        uint16_t cdi = di;                              /* PUSH DI per channel */
        uint16_t cx;

        if (M.cfg.stereo == 0) {
            M.pan_class = 0;
            M.pan = 0;
        } else {                                        /* 22DB: classify the panning */
            int8_t al = ch->panning;
            uint8_t ah = (uint8_t)al;
            if (al != 100) {
                ah = 0x3F;
                if (al < 0x3F) {
                    ah = 0xC1;
                    if (al > -0x3F) {
                        ah = 0;
                        if (al < 3 && al > -3)
                            al = 0;
                    }
                }
            }
            M.pan_class = ah;
            M.pan = al;
            if (M.pan_class == 0x3F)
                cdi = (uint16_t)(cdi + M.acc_size);     /* right channel only */
        }
        if (!(ch->flags & 1) || (ch->flags & 4))
            continue;
        if (ch->handle != 0) {                          /* virtual sample: MCPSampleRealAddress */
            uint8_t *p = imp_real ? imp_real(ch->handle, ch->pos) : NULL;
            if (!p)
                continue;
            ch->data = p;
            ch->sample.ptr = p;
            ch->sample.in_ems = 0;
            ch->sample.ems_handle = 0;
        }
        cx = M.frames;
        M.remain = cx;
        for (uint32_t guard = 0;; guard++) {            /* 2361 */
            uint16_t off = mcp_far_offset_hook(ch->data);
            uint32_t esi = (uint32_t)off + ch->pos;
            uint32_t need = (uint32_t)(((uint64_t)ch->step * cx) >> 23);
            uint32_t end = ch->loopend ? ch->loopend : ch->length;
            uint32_t rem;

            M.loop_hit = 0;
            rem = end - 1 - ch->pos;
            if ((int32_t)rem <= 0) {
                if (ch->loopend)
                    M.loop_hit = 1;
                cx = 0;
            } else if (!(rem > need)) {
                ch->flags &= ~1;
                if (ch->loopend) {
                    M.loop_hit = 1;
                    ch->flags |= 3;
                }
                cx = (uint16_t)(((uint64_t)rem << 23) / ch->step);
            }
            if (cx != 0) {                              /* 23E5 */
                uint32_t esi_saved = esi;
                uint16_t si;
                Src src;
                uintptr_t segbase = (uintptr_t)ch->data - off;
                uint16_t frac_out;

                if (esi > 0xFFFF) {                     /* normalise es:si */
                    segbase += esi & ~0xFu;
                    si = (uint16_t)(esi & 0xF);
                } else {
                    si = (uint16_t)esi;
                }
                src.base = segbase;
                if (ch->data >= pc_ems_frame && ch->data < pc_ems_frame + 0x10000) {
                    src.lo = (uintptr_t)pc_ems_frame;
                    src.hi = (uintptr_t)pc_ems_frame + 0x10000;
                } else {
                    src.lo = (uintptr_t)ch->data;
                    src.hi = ch->data ? (uintptr_t)ch->data + ch->length + 1 : 0;
                }
                {
                    uint16_t si0 = si;
                    int skip = 0;
                    uint8_t vl = 0, vr = 0;

                    if (M.pan_class == 0 && M.pan != 0) {        /* 242E: panned */
                        uint16_t ax = (uint16_t)((int8_t)ch->volume * (int8_t)M.master);
                        uint8_t al = (uint8_t)((uint8_t)(ax >> 6) - 1);
                        if ((int8_t)al < 0) {
                            skip = 1;
                        } else {
                            vl = vr = al;
                            if (M.pan <= 0)
                                vr = (uint8_t)((int16_t)((int8_t)(M.pan + 0x3F) * (int8_t)vr) >> 6);
                            else
                                vl = (uint8_t)((int16_t)((int8_t)-(M.pan - 0x3F) * (int8_t)vl) >> 6);
                            if (M.master == 0)
                                skip = 1;
                        }
                    } else {                                     /* 2472 */
                        int16_t ax = (int16_t)((int8_t)ch->volume * (int8_t)M.master);
                        vl = (uint8_t)((uint8_t)(ax >> 6) - 1);
                        if ((int8_t)vl < 0 || M.master == 0)
                            skip = 1;
                    }
                    if (!skip) {
                        frac_out = mix_loop(&src, &si, &cdi, ch->pos_frac, ch->step_int,
                                            ch->step_frac, cx, gs, vl, vr);
                    } else {                                     /* 2493: advance only */
                        uint32_t st = (uint32_t)ch->step_int << 16 | ch->step_frac;
                        uint32_t adv = (uint32_t)(st * (uint32_t)cx) >> 16;
                        uint32_t bytes = (uint32_t)M.acc_size * (M.cfg.stereo == 1 ? 2u : 1u) * cx;
                        si = (uint16_t)(si + adv);
                        cdi = (uint16_t)(cdi + bytes);
                        frac_out = (uint16_t)bytes;  /* quirk: AX = the byte count lands in the fraction */
                    }
                    esi = esi_saved + (uint16_t)(si - si0);
                    ch->pos_frac = frac_out;
                }
            }
            if (M.loop_hit == 1)                        /* 24CF */
                esi = (uint32_t)off + ch->loopstart;
            cx = (uint16_t)(M.remain - cx);
            M.remain = cx;
            ch->pos = esi - off;
            if (!((int16_t)cx > 0 && M.loop_hit == 1))
                break;
            /* A loop with loopstart >= loopend-1 never advances: the original
             * hangs here (inside the timer interrupt).  The port gives up. */
            if (guard > 0x10000)
                break;
        }
    }

    if (!(M.options & MCP_OPT_ACCUMULATE))
        return;
    {
        uint16_t n = M.frames;
        uint16_t out = M.mixpos;
        if (M.cfg.stereo == 1)
            n = (uint16_t)(n << 1);
        if (M.cfg.sample_size != 2) {                   /* 2525: words -> bytes via tab:6000 */
            for (uint16_t i = 0; i < n; i++) {
                uint16_t w = rd16(mcp_tab, (uint16_t)(0x4000 + 2 * i));
                mcp_dma_buffer[(uint16_t)(out + i)] = mcp_tab[(uint16_t)(0x6000 + w)];
            }
        } else {                                        /* 25CE: dwords -> clipped words */
            for (uint16_t i = 0; i < n; i++) {
                int32_t v = (int32_t)rd32(mcp_tab, (uint16_t)(0x8000 + 4 * i));
                if (v < -32768)
                    v = -32768;
                else if (v > 32767)
                    v = 32767;
                wr16(mcp_dma_buffer, (uint16_t)(out + 2 * i), (uint16_t)v);
            }
        }
    }
}

/*
 * mix_chunk - mix M.frames at M.mixpos (2210).  Accumulate mode splits the
 * work into blocks of 2000 (mono) / 1000 (stereo) frames, the accumulator
 * size.  Brackets the work with MCPEnable/DisableVirtualSamples.
 */
static void mix_chunk(void)
{
    uint16_t save_frames = M.frames, save_pos = M.mixpos;

    if (imp_enable)
        imp_enable();
    if (M.options & MCP_OPT_ACCUMULATE) {
        uint16_t blk = 2000;
        if (M.cfg.stereo == 1)
            blk >>= 1;
        if ((int16_t)M.frames > (int16_t)blk) {
            M.chunk_rest = (uint16_t)(M.frames - blk);
            do {
                M.frames = blk;
                mix_pass();
                M.mixpos = (uint16_t)(M.mixpos + blk * M.bpf);
                M.chunk_rest = (uint16_t)(M.chunk_rest - blk);
            } while ((int16_t)M.chunk_rest >= 0);
            M.chunk_rest = (uint16_t)(M.chunk_rest + blk);
            M.frames = M.chunk_rest;
        }
    }
    mix_pass();
    M.mixpos = save_pos;
    M.frames = save_frames;
    if (imp_disable)
        imp_disable();
}

static void call_effect(void)
{
    if (M.effect) {
        uint16_t fmt = (uint16_t)((M.cfg.sample_size >> 1) | (M.cfg.stereo << 1));
        M.effect(mcp_dma_buffer + M.mixpos, M.frames, fmt);
    }
}

/*
 * mix_time - mix the frames that correspond to `dt` DSMI time units
 * (0.1 ms) at the write position.
 *
 * Original: 2834, far pascal (long dt).  bytes = rate*dt/10000*bpf; less than
 * 16 bytes mixes nothing (the time is lost: the caller still advances its
 * clock).  The frame count is rounded down to 16 bytes minus one byte, a
 * block wrapping past the ring end is split, and a remainder under 8 frames
 * is not mixed.
 */
static void mix_time(uint32_t dt)
{
    uint32_t bytes;

    if (!(M.flags & 2))
        return;
    /* MUL/DIV: the original raises #DE when the quotient exceeds 32 bits
     * (only for a "negative" dt); the port truncates. */
    bytes = (uint32_t)((uint64_t)M.rate * dt / 10000u);
    bytes *= M.bpf;
    if ((int32_t)bytes < 16)
        return;
    while (bytes >= M.bufsize)
        bytes -= M.bufsize;
    M.wrapped = 0;
    if ((uint32_t)M.mixpos + bytes >= M.bufsize)
        M.wrapped++;
    M.frames = (uint16_t)((uint16_t)(((uint16_t)bytes - 1) & 0xFFF0) / M.bpf);
    if (M.wrapped) {
        uint16_t first = (uint16_t)((uint16_t)(M.bufsize - M.mixpos) / M.bpf);
        uint16_t rest = (uint16_t)(M.frames - first);
        M.frames = first;
        mix_chunk();
        call_effect();
        M.frames = rest;
        M.mixpos = 0;
    }
    if ((int16_t)M.frames < 8)
        return;
    mix_chunk();
    call_effect();
    M.mixpos = (uint16_t)(M.mixpos + M.frames * M.bpf);
}

/* clear_buffers - fill the DMA ring with silence and clear the accumulator
 * head.  Original: 295F, far. */
static void clear_buffers(void)
{
    if (!(M.flags & 2))
        return;
    for (uint16_t i = 0; i < M.bufsize; i++)
        mcp_dma_buffer[i] = (uint8_t)(i & 1 ? M.dma_fill >> 8 : M.dma_fill);
    if (M.options & MCP_OPT_ACCUMULATE) {
        if (M.cfg.sample_size == 2)
            memset(mcp_tab + 0x8000, 0, 0x2000);
        else
            memset(mcp_tab + 0x4000, 0, 0x1000);
    }
}

/* time_avail - DSMI time the DMA ring position is ahead of the write
 * position.  Original: 29B3 (reads SDIGetBufferPosition), 29F8 (reuses it). */
static uint32_t time_from_pos(uint16_t pos)
{
    uint16_t ax = (uint16_t)(pos - M.mixpos);
    uint16_t frames;
    if (!((int16_t)pos > (int16_t)M.mixpos))          /* SUB AX,[0A23] / JG */
        ax = (uint16_t)(ax + M.bufsize);
    frames = (uint16_t)(ax / M.bpf);
    return (uint32_t)((uint32_t)frames * 10000u / M.rate);
}

static uint32_t time_avail(void)
{
    uint16_t pos = imp_bufpos ? imp_bufpos() : 0;
    M.dmapos = pos;
    return time_from_pos(pos);
}

/* ------------------------------------------------------------------------ */
/* tables                                                                    */
/* ------------------------------------------------------------------------ */

/* build_tab_words - direct-mode volume table: words ((s<<8)*vol/nch)>>5.
 * Original: 2BB7, far (BX = channels). */
static void build_tab_words(uint16_t nch)
{
    uint16_t di = 0;
    for (int v = 0; v < 32; v++)
        for (int s = -128; s < 128; s++) {
            int32_t x = (int32_t)(((int64_t)(s * 256) * M.voltab[v]) / nch);
            wr16(mcp_tab, di, (uint16_t)(x >> 5));
            di = (uint16_t)(di + 2);
        }
}

/* build_tab_dwords - 16-bit accumulate table: dwords ((s<<8)*vol/nch)>>5.
 * Original: 2C0A, far (BX = channels). */
static void build_tab_dwords(uint16_t nch)
{
    uint16_t di = 0;
    for (int v = 0; v < 32; v++)
        for (int s = -128; s < 128; s++) {
            int32_t x = (int32_t)(((int64_t)(s * 256) * M.voltab[v]) / nch);
            wr32(mcp_tab, di, (uint32_t)(x >> 5));
            di = (uint16_t)(di + 4);
        }
}

/* build_tab_8bit - 8-bit output: post table tab:5001..6FFF (accumulator
 * word -> unsigned byte, clipped at +-127 with a channel-count divisor) and
 * words (s*vol)>>5.  Original: 2CA0, far (BX = channels). */
static void build_tab_8bit(uint16_t nch)
{
    uint32_t div = post8_div[nch];
    for (uint32_t c = 0; c < 0x1000; c++) {
        uint32_t v = c * 0x7F / div;
        uint8_t al;
        if ((uint16_t)v > 0x7F)
            v = 0x7F;
        al = (uint8_t)(v + 0x80);
        mcp_tab[(uint16_t)(0x6000 + c)] = al;
        mcp_tab[(uint16_t)(0x6000 - c)] = (uint8_t)-al;
    }
    {
        uint16_t di = 0;
        for (int v = 0; v < 32; v++)
            for (int s = -128; s < 128; s++) {
                wr16(mcp_tab, di, (uint16_t)((s * (int32_t)M.voltab[v]) >> 5));
                di = (uint16_t)(di + 2);
            }
    }
}

/* ------------------------------------------------------------------------ */
/* init / start / stop                                                       */
/* ------------------------------------------------------------------------ */

/*
 * mcpInit - set up the mixer for the configured output.
 *
 * Original: 2A2E, far pascal (init record far *) - called by initDevice
 * with d:31BC.  The DMA ring and the table segment come from the caller in
 * the original (MCPOutputBuffer at d:087C, d:31BE/31C2); here they are the
 * static mcp_dma_buffer / mcp_tab.  Returns 0.
 * Verified: tests/py/test_mcp.py (configuration sweep).
 */
int16_t mcpInit(const McpConfig *cfg)
{
    M.cfg = *cfg;
    M.bpf = (uint16_t)(cfg->sample_size << (cfg->stereo & 1));
    M.options = cfg->options;
    if (cfg->sample_size != 2)
        M.options |= MCP_OPT_ACCUMULATE;
    M.amp = amp_table[(M.options >> 4) & 3];
    M.bufsize = cfg->buffer_size;
    M.nch = 0;
    memset(M.ch, 0, sizeof M.ch);
    M.acc_size = cfg->sample_size;
    if (!(M.options & MCP_OPT_ACCUMULATE)) {
        M.dma_fill = 0;
        M.wide = 0;                                     /* 181E / 18DF */
    } else if (cfg->sample_size != 2) {
        M.acc_size = 2;
        M.dma_fill = 0x8080;
        M.wide = 0;
    } else {
        M.acc_size = 4;
        M.dma_fill = 0;
        M.wide = 1;                                     /* 1C51 / 1D51 */
    }
    M.rate = cfg->rate;
    M.flags |= 2;
    clear_buffers();
    M.master = 0x40;
    M.effect = NULL;
    M.busy = 0;
    return 0;
}

/* mcpClose - stop and mark uninitialised.  Original: 2B9A, far (unreferenced). */
int16_t mcpClose(void)
{
    if (!(M.flags & 2))
        return -1;
    mcpStopVoice();
    M.flags &= (uint8_t)~6;
    return 0;
}

/* mcpStartVoice - start mixing: write position 0, clock = now, empty queue.
 * Original: 2DEA, far (called by startOutput after the DMA start). */
int16_t mcpStartVoice(void)
{
    if (!(M.flags & 2) || (M.flags & 4))
        return -1;
    M.mixpos = 0;
    M.last_poll = imp_time ? imp_time() : 0;
    M.q_read = 0;
    M.q_write = 0;
    clear_buffers();
    M.flags |= 4;
    return 0;
}

/* mcpStopVoice - Original: 2E3E, far (stopOutput). */
int16_t mcpStopVoice(void)
{
    if (!(M.flags & 2) || !(M.flags & 4))
        return -1;
    M.flags &= (uint8_t)~4;
    clear_buffers();
    return 0;
}

/* Original: 34F4 / 2F76, far (not in the CDI table). */
uint16_t mcpGetMixRate(void) { return M.rate; }
int16_t mcpGetChannelCount(void) { return (M.flags & 2) ? (int16_t)M.nch : -1; }

uint16_t mcp_buffer_size(void) { return M.bufsize; }
uint16_t mcp_mix_position(void) { return M.mixpos; }

/* ------------------------------------------------------------------------ */
/* command queue                                                             */
/* ------------------------------------------------------------------------ */

/* enqueue - stamp the pending command with GetDSMITime() and append it.
 * No overflow check: the 351st unread command overwrites the oldest.
 * Original: 2FC7, far. */
static void enqueue(void)
{
    uint32_t i;
    M.pending.time = imp_time ? imp_time() : 0;
    i = M.q_write;
    M.q_write++;
    if (M.q_write >= MCP_QUEUE_LEN)
        M.q_write = 0;
    M.queue[i] = M.pending;
}

/* dequeue - next command up to the snapshot taken by poll into M.cur.
 * Original: 3017, far; returns 0 or -1 (empty). */
static int dequeue(void)
{
    uint32_t i = M.q_read;
    if (i == M.q_snap)
        return -1;
    M.q_read++;
    if (M.q_read >= MCP_QUEUE_LEN)
        M.q_read = 0;
    M.cur = M.queue[i];
    return 0;
}

/* step computation shared by play (32C1) and setFrequency (33E3) */
static void set_rate(McpChannel *ch, uint32_t freq)
{
    uint32_t r = M.rate;
    /* DIV: #DE in the original when freq >= rate*512; truncated here */
    ch->step = (uint32_t)(((uint64_t)freq << 23) / r);
    ch->freq = freq;
    ch->step_int = (uint16_t)(freq / r);
    ch->step_frac = (uint16_t)((((uint64_t)(freq % r)) << 16) / r);
}

/* apply - execute M.cur.  Original: 305E, far (dispatch through CS:2FBB). */
static void apply(void)
{
    McpChannel *ch = &M.ch[M.cur.cmdch & 0x1F];
    uint32_t v = M.cur.value;
    uint8_t al = (uint8_t)v;

    switch (M.cur.cmdch >> 5) {
    case CMD_SAMPLE: {                                  /* 31F5 */
        const McpSlot *s = &M.slots[al];
        if (cdi_ref_is_null(s->sample) || s->length == 0)
            break;
        ch->sample = s->sample;
        ch->data = s->sample.ptr;
        ch->length = s->length;
        if (s->mode & 1) {
            if (s->loopend != 0) {
                if (!(ch->pos < s->loopend))
                    ch->pos = s->loopstart;
            } else if (!(ch->pos < ch->length)) {
                ch->pos = 0;
            }
        } else {
            ch->pos = 0;
        }
        ch->loopstart = s->loopstart;
        ch->loopend = s->loopend;
        ch->handle = s->sampleID;
        ch->flags |= 8;
        break;
    }
    case CMD_PLAY: {                                    /* 32C1 */
        uint8_t vol;
        if (!(ch->flags & 8))
            break;
        vol = (uint8_t)((uint8_t)(M.cur.vol + 1) >> 1);
        if (vol > 0x20)
            vol = 0x20;
        ch->volume = vol;
        set_rate(ch, v);
        ch->pos = 0;
        ch->pos_frac = 0;
        ch->flags |= 1;
        ch->flags &= ~2;
        break;
    }
    case CMD_VOLUME: {                                  /* 338C */
        uint8_t vol = (uint8_t)((uint8_t)(al + 1) >> 1);
        if ((int8_t)vol > 0x20)
            vol = 0x20;
        ch->volume = vol;
        break;
    }
    case CMD_FREQ:                                      /* 33E3 */
        set_rate(ch, v);
        break;
    case CMD_POSITION:                                  /* 3472 */
        if (ch->flags & 1) {
            if (v >= ch->length) {
                ch->pos = ch->length;
            } else {
                ch->pos = v;
                ch->pos_frac = 0;
            }
        }
        break;
    case CMD_PANNING:                                   /* 34E1 */
        if (M.cfg.stereo != 0)
            ch->panning = (int8_t)al;
        break;
    default:            /* 6, 7: past the 6-entry table in the original (unreachable) */
        break;
    }
}

/* queue_cmd - common front of the queued CDI slots: initialised and
 * (uint16_t)ch < channels (the original compares the low word only). */
static int queue_cmd(int32_t ch, int cmd, uint32_t value)
{
    if (!(M.flags & 2) || (uint16_t)ch >= M.nch)
        return -1;
    M.pending.cmdch = (uint8_t)((ch & 0x1F) | cmd << 5);
    M.pending.value = value;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* CDI slots                                                                 */
/* ------------------------------------------------------------------------ */
/* Return values: the original leaves several registers undefined (AX after
 * a successful queued call is the low word of the time stamp, DX junk);
 * AMP and the game ignore them.  The port returns 0 for success and -1
 * (AX = FFFFh) for failure where the original sets FFFFh. */

/*
 * mcpSetSample - queue "use this sample": the 0x13-byte SAMPLEINFO head is
 * copied now into a ring of 32 slots (d:1D7C, index d:1FDC counting down
 * 0, 31, 30, ...), so a record changed after the call does not matter, but
 * more than 32 setSample calls between two polls overwrite queued slots.
 * Original: 3189, far pascal (ch, SAMPLEINFO far *).
 */
int32_t mcpSetSample(int32_t ch, CdiSampleInfo *si)
{
    McpSlot *s;
    if (queue_cmd(ch, CMD_SAMPLE, M.slot) < 0)
        return -1;
    s = &M.slots[M.slot];
    s->sample = si->sample;
    s->length = si->length;
    s->loopstart = si->loopstart;
    s->loopend = si->loopend;
    s->mode = si->mode;
    s->sampleID = si->sampleID;
    enqueue();
    if ((int16_t)--M.slot < 0)
        M.slot = 0x1F;
    return 0;
}

/* Original: 327A, far pascal (ch, freq, volume): queued; the handler (32C1)
 * sets volume (v+1)>>1 clamped to 32, the step, position 0, playing. */
int32_t mcpPlaySample(int32_t ch, uint32_t freq, uint32_t volume)
{
    if (queue_cmd(ch, CMD_PLAY, freq) < 0)
        return -1;
    M.pending.vol = (uint8_t)volume;
    enqueue();
    return 0;
}

/* Original: 334B, far pascal (ch, volume): queued (handler 338C). */
int32_t mcpSetVolume(int32_t ch, uint32_t volume)
{
    if (queue_cmd(ch, CMD_VOLUME, volume) < 0)
        return -1;
    enqueue();
    return 0;
}

/* Original: 33A2, far pascal (ch, freq): queued (handler 33E3). */
int32_t mcpSetFrequency(int32_t ch, uint32_t freq)
{
    if (queue_cmd(ch, CMD_FREQ, freq) < 0)
        return -1;
    enqueue();
    return 0;
}

/* Original: 169D, far pascal (ch, rate): ENTER/LEAVE/RETF 8 - does nothing. */
int32_t mcpSetLinearRate(int32_t ch, uint32_t rate)
{
    (void)ch; (void)rate;
    return 0;
}

/* Original: 3431, far pascal (ch, pos): queued (handler 3472: only while
 * playing; pos >= length clamps to length and keeps the fraction). */
int32_t mcpSetPosition(int32_t ch, uint32_t pos)
{
    if (queue_cmd(ch, CMD_POSITION, pos) < 0)
        return -1;
    enqueue();
    return 0;
}

/* Original: 34A0, far pascal (ch, pan): queued (handler 34E1, stereo only). */
int32_t mcpSetPanning(int32_t ch, int32_t pan)
{
    if (queue_cmd(ch, CMD_PANNING, (uint32_t)pan) < 0)
        return -1;
    enqueue();
    return 0;
}

/* Original: 34FF, far pascal (volume): immediate, ignored above 64.
 * Always returns AX = FFFFh. */
int32_t mcpSetMasterVolume(uint32_t volume)
{
    if ((M.flags & 2) && volume <= 0x40)
        M.master = (uint8_t)volume;
    return -1;
}

/* Original: 2E63, far pascal (ch): immediate; -2 if already muted. */
int32_t mcpMuteChannel(int32_t ch)
{
    McpChannel *c;
    if (!(M.flags & 2) || (uint16_t)ch >= M.nch)
        return -1;
    c = &M.ch[(uint16_t)ch];
    if (c->flags & 4)
        return -2;
    c->flags |= 4;
    return 0;
}

/* Original: 2EA0, far pascal (ch): immediate; -2 if not muted. */
int32_t mcpUnmuteChannel(int32_t ch)
{
    McpChannel *c;
    if (!(M.flags & 2) || (uint16_t)ch >= M.nch)
        return -1;
    c = &M.ch[(uint16_t)ch];
    if (!(c->flags & 4))
        return -2;
    c->flags &= ~4;
    return 0;
}

/* Original: 2EDD, far pascal (ch): IMMEDIATE (not queued) - a stop issued
 * after a play in the same poll interval is undone when the queued play
 * runs.  -2 if not playing. */
int32_t mcpStopChannel(int32_t ch)
{
    McpChannel *c;
    if (!(M.flags & 2) || (uint16_t)ch >= M.nch)
        return -1;
    c = &M.ch[(uint16_t)ch];
    if (!(c->flags & 1))
        return -2;
    c->flags &= ~1;
    return 0;
}

/* Original: 2F1A / 2F48, far: mute/unmute 0..n-1; -1 when no channels. */
int32_t mcpMuteAll(void)
{
    if (!(M.flags & 2) || M.nch == 0)
        return -1;
    for (uint16_t i = 0; i < M.nch; i++)
        mcpMuteChannel(i);
    return 0;
}

int32_t mcpUnmuteAll(void)
{
    if (!(M.flags & 2) || M.nch == 0)
        return -1;
    for (uint16_t i = 0; i < M.nch; i++)
        mcpUnmuteChannel(i);
    return 0;
}

/* Original: 3677 (returns 0), 3682 / 3681 (no-ops): samples stay where the
 * caller keeps them. */
int32_t mcpDownloadSample(uint8_t *sample, CdiSampleRef tag, uint32_t length)
{
    (void)sample; (void)tag; (void)length;
    return 0;
}
int32_t mcpUnloadSample(CdiSampleRef tag) { (void)tag; return 0; }
int32_t mcpUnloadAll(void) { return 0; }

/* Original: 3526, far pascal (ch): volume * 2 (0..64). */
int32_t mcpGetVolume(int32_t ch)
{
    if (!(M.flags & 2) || (uint16_t)ch >= M.nch)
        return -1;
    return M.ch[(uint16_t)ch].volume << 1;
}

/* Original: 35D5, far pascal (ch). */
int32_t mcpGetFrequency(int32_t ch)
{
    if (!(M.flags & 2) || (uint16_t)ch >= M.nch)
        return -1;
    return (int32_t)M.ch[(uint16_t)ch].freq;
}

/* Original: 3559, far pascal (ch): the mixer's position (the effect of
 * queued commands shows only after the next poll). */
int32_t mcpGetPosition(int32_t ch)
{
    if (!(M.flags & 2) || (uint16_t)ch >= M.nch)
        return -1;
    return (int32_t)M.ch[(uint16_t)ch].pos;
}

/* Original: 363A, far pascal (ch): 0 in mono. */
int32_t mcpGetPanning(int32_t ch)
{
    if (!(M.flags & 2) || (uint16_t)ch >= M.nch)
        return -1;
    return M.cfg.stereo ? M.ch[(uint16_t)ch].panning : 0;
}

/* Original: 358C, far pascal (ch): the sample pointer, or for a virtual
 * sample MCPSampleRealAddress(handle, 0). */
CdiSampleRef mcpGetSample(int32_t ch)
{
    CdiSampleRef r = { 0 };
    McpChannel *c;
    if (!(M.flags & 2) || (uint16_t)ch >= M.nch) {
        r.ems_handle = 0xFFFF;          /* AX = FFFFh, DX undefined */
        return r;
    }
    c = &M.ch[(uint16_t)ch];
    if (c->handle == 0)
        return c->sample;
    r.ptr = imp_real ? imp_real(c->handle, 0) : NULL;
    return r;
}

/*
 * mcpSetupChannels - set the channel count (1..32), clear the channels that
 * become new, recompute the volume table (the per-channel level is divided
 * by the channel count in 16-bit modes).
 * Original: 2D2C, far pascal (count).
 */
int32_t mcpSetupChannels(int32_t count)
{
    uint16_t old, n;
    if (!(M.flags & 2) || (uint32_t)count > 0x20 || count == 0)
        return -1;
    n = (uint16_t)count;
    if (M.options & MCP_OPT_AUTO_AMP)
        M.amp = auto_amp[n - 1];
    old = M.nch;
    M.nch = n;
    for (uint16_t i = old; (int16_t)i < (int16_t)n; i++)
        memset(&M.ch[i], 0, sizeof M.ch[i]);
    for (int i = 0; i < 32; i++)                        /* 2DA1: IMUL / DIV 100 */
        M.voltab[i] = (uint16_t)((uint32_t)vol_steps[i] * M.amp / 100u);
    if (!(M.options & MCP_OPT_ACCUMULATE))
        build_tab_words(n);
    else if (M.cfg.sample_size != 2)
        build_tab_8bit(n);
    else
        build_tab_dwords(n);
    return 0;
}

/* Original: 2F8B, far pascal (ch): the flags word. */
int32_t mcpGetChannelStatus(int32_t ch)
{
    if (!(M.flags & 2) || (uint16_t)ch >= M.nch)
        return -1;
    return M.ch[(uint16_t)ch].flags;
}

/*
 * mcpPoll - mix up to the DMA position, applying the queued commands at
 * their time stamps.
 *
 * Original: 3082, far (CDI slot 5C; IST.EXE: 40 Hz timer routine
 * 0D42:0093 inside EMSSaveState/EMSRestoreState).
 *   avail = time between write position and DMA position (29B3)
 *   nothing happens unless now - last_poll >= 120 (12 ms; with the 100 Hz
 *   DSMI clock that means two ticks)
 *   for each queued command (up to a snapshot of the write index):
 *       dt = its time - previous time (first: - last_poll); avail -= dt;
 *       if avail < 0: apply this and all remaining commands without mixing,
 *                     apply the last one AGAIN, mix up to the DMA position;
 *       else mix(dt) (skipped when dt == 0), apply
 *   then mix the rest up to the DMA position, last_poll = now.
 */
void mcpPoll(void)
{
    int32_t avail;
    uint32_t now, prev;

    if (!(M.flags & 2))
        return;
    if (M.busy == 1)
        return;
    M.busy = 1;
    avail = (int32_t)time_avail();                      /* CLI ... STI */
    now = imp_time ? imp_time() : 0;
    M.q_snap = M.q_write;
    if ((int32_t)(now - M.last_poll) < 0x78)
        goto out;
    if (dequeue() == -1)
        goto rest;
    {
        uint32_t dt = M.cur.time - M.last_poll;
        int32_t old = avail;
        prev = M.cur.time;
        avail = (int32_t)((uint32_t)old - dt);
        if ((int64_t)old < (int64_t)(int32_t)dt)        /* SUB / JL */
            goto overrun;
        mix_time(dt);
        apply();
    }
    for (;;) {
        uint32_t dt;
        if (dequeue() == -1)
            goto rest;
        dt = M.cur.time - prev;
        if (dt != 0) {
            int32_t old = avail;
            prev = M.cur.time;
            avail = (int32_t)((uint32_t)old - dt);
            if ((int64_t)old < (int64_t)(int32_t)dt)
                goto overrun;
            mix_time(dt);
        }
        apply();
    }
rest:
    if (avail >= 0)
        mix_time(time_from_pos((uint16_t)M.dmapos));
    goto done;
overrun:
    do
        apply();
    while (dequeue() != -1);
    apply();                                            /* 3152: the last one twice */
    mix_time(time_from_pos((uint16_t)M.dmapos));
done:
    M.last_poll = now;
out:
    M.busy = 0;
}

/* Original: 368A, far pascal (a, b): d:0A61 = a ? b : 0 - b is a far
 * function pointer, which an int32 cannot carry natively: the port keeps
 * the effect only through mcp_set_effect_fn(); this slot clears it for a = 0. */
int32_t mcpSetEffect(int32_t a, int32_t b)
{
    (void)b;
    if (a == 0)
        M.effect = NULL;
    return 0;
}

void mcp_set_effect_fn(McpEffectFn fn) { M.effect = fn; }

/* Original: 36B1, far pascal (arg): 1 -> bit0 initialised, bit1 stereo;
 * else the mixing rate. */
int32_t mcpGetStatus(int32_t arg)
{
    if (arg == 1) {
        int32_t r = 0;
        if (M.flags & 2) {
            r |= 1;
            if (M.cfg.stereo == 1)
                r |= 2;
        }
        return r;
    }
    return M.rate;
}

/* mcp_reset - port: back to the state of a freshly loaded driver image
 * (all driver data zero, as in SDISB16.DSD).  Imports are kept. */
void mcp_reset(void)
{
    memset(&M, 0, sizeof M);
    memset(mcp_dma_buffer, 0, sizeof mcp_dma_buffer);
    memset(mcp_tab, 0, sizeof mcp_tab);
    mcp_dma_position = 0;
}

/* mcp_channel_state - port/tests: raw channel fields (d:0A65 record order:
 * flags, pos, pos_frac, step, step_int, step_frac, volume, panning). */
void mcp_channel_state(int ch, uint32_t out[8])
{
    const McpChannel *c = &M.ch[ch & 31];
    out[0] = c->flags; out[1] = c->pos; out[2] = c->pos_frac; out[3] = c->step;
    out[4] = c->step_int; out[5] = c->step_frac; out[6] = c->volume; out[7] = (uint8_t)c->panning;
}

/* The driver's CDIDEVICE (d:088F). */
void mcp_fill_cdi_device(CdiDevice *d)
{
    d->setSample = mcpSetSample;
    d->playSample = mcpPlaySample;
    d->setVolume = mcpSetVolume;
    d->setFrequency = mcpSetFrequency;
    d->setLinearRate = mcpSetLinearRate;
    d->setPosition = mcpSetPosition;
    d->setPanning = mcpSetPanning;
    d->setMasterVolume = mcpSetMasterVolume;
    d->muteChannel = mcpMuteChannel;
    d->unmuteChannel = mcpUnmuteChannel;
    d->stopChannel = mcpStopChannel;
    d->muteAll = mcpMuteAll;
    d->unmuteAll = mcpUnmuteAll;
    d->download = mcpDownloadSample;
    d->unload = mcpUnloadSample;
    d->unloadAll = mcpUnloadAll;
    d->getVolume = mcpGetVolume;
    d->getFrequency = mcpGetFrequency;
    d->getPosition = mcpGetPosition;
    d->getPanning = mcpGetPanning;
    d->getSample = mcpGetSample;
    d->setupChannels = mcpSetupChannels;
    d->getChannelStatus = mcpGetChannelStatus;
    d->poll = mcpPoll;
    d->setEffect = mcpSetEffect;
    d->getStatus = mcpGetStatus;
}

/* ------------------------------------------------------------------------ */
/* imports                                                                   */
/* ------------------------------------------------------------------------ */

void mcp_set_imports(McpTimeFn time, McpRealAddressFn real, McpVoidFn enable,
                     McpVoidFn disable, McpBufPosFn bufpos)
{
    imp_time = time;
    imp_real = real;
    imp_enable = enable;
    imp_disable = disable;
    imp_bufpos = bufpos;
}

/* mcpOpenDDS - Original: 001C, near.  The original also registers the
 * MCPOutputBuffer resource first (done by sdisb16_entry here). */
int16_t mcpOpenDDS(DdsAccessFn access)
{
    DdsFn f;
    if (access("GetDSMITime", &f) != 0)
        return 5;
    imp_time = (McpTimeFn)f;
    if (access("MCPSampleRealAddress", &f) == 0)
        imp_real = (McpRealAddressFn)f;
    if (access("MCPEnableVirtualSamples", &f) == 0)
        imp_enable = (McpVoidFn)f;
    if (access("MCPDisableVirtualSamples", &f) != 0)
        return 5;
    imp_disable = (McpVoidFn)f;
    if (access("SDIGetBufferPosition", &f) != 0)
        return 5;
    imp_bufpos = (McpBufPosFn)f;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* SDI configuration (initDevice 08F6, mixer-relevant part)                  */
/* ------------------------------------------------------------------------ */

static uint32_t rd32le(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/*
 * mcp_config_from_sdi - Original: initDevice 08F6 (type checks, rate,
 * stereo, sample size, amplification options 0E47, buffer size 0ED1).
 * SB 2.x assumes DSP version >= 2.01 (the port has no DSP).  An out-of-range
 * rate index leaves the rate uninitialised in the original (stack garbage);
 * the port uses 22050.
 */
int16_t mcp_config_from_sdi(const uint8_t *sdi, McpConfig *out)
{
    uint32_t type = rd32le(sdi), opt44 = rd32le(sdi + 0x44), opt48 = rd32le(sdi + 0x48);
    uint32_t amp = rd32le(sdi + 0x40);
    uint32_t rate = 22050;
    McpConfig c = { 0 };

    if (type == 1) {                                    /* SB: mono 8-bit */
        static const uint16_t r[3] = { 32258, 21276, 16129 };
        c.options = 1; c.stereo = 0; c.sample_size = 1;
        if (opt44 < 3) rate = r[opt44];
    } else if (type == 2) {                             /* SB Pro */
        static const uint16_t rs[3] = { 21276, 16129, 11111 }, rm[3] = { 32258, 21276, 16129 };
        c.options = 1; c.sample_size = 1;
        c.stereo = opt44 == 0;
        if (opt48 < 3) rate = c.stereo ? rs[opt48] : rm[opt48];
    } else if (type == 6) {                             /* SB16 */
        static const uint16_t r[4] = { 44100, 32000, 22050, 16000 };
        c.options = 0; c.sample_size = 2;
        c.stereo = opt44 == 0;
        if (opt48 < 4) rate = r[opt48];
    } else {
        return -1;
    }
    switch (amp) {                                      /* 0E47 */
    case 1: c.options |= 0x41; break;
    case 2: c.options |= 0x10; break;
    case 3: c.options |= 0x21; break;
    case 4: c.options |= 0x31; break;
    default: break;
    }
    c.rate = (uint16_t)rate;
    {   /* 0ED1: 2000 frames at 22000 Hz, rounded down to 16 bytes */
        int32_t ax = (int16_t)((int16_t)(c.sample_size * 2000) << c.stereo);
        c.buffer_size = (uint16_t)((ax * (int32_t)rate / 22000) & 0xFFF0);
    }
    *out = c;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* SDISB16.DSD driver shell                                                  */
/* ------------------------------------------------------------------------ */

static CdiDevice mcp_device;
static uint8_t mcp_output_resource[0x12];   /* d:087C MCPOutputBuffer (layout only) */
static DdsFn imp_cdi_register;

/* SDIInitDevice - Original: initDevice 08F6, far pascal (SDI config far *).
 * The SB hardware set-up (DSP reset/rate, IRQ vector, DMA buffer) is the
 * platform's job; this keeps the mixer part: configuration, mcpInit,
 * cdiRegister.  Returns 0, -1 for an unsupported card type or when an
 * allocation fails.
 *
 * Heap: before mcpInit the original allocates, through the DDS "MemAlloc"
 * (IST.EXE 0E9E:0000, the game's far heap), the DMA ring + 20h bytes
 * (0F13) and the mixing-table segment, 4030h or A030h bytes with the
 * accumulate option (0F8F); a third block (0FE4) only when the SDI record
 * supplies an output-buffer descriptor with bit 1, which IST.EXE never
 * does (its descriptor pointer d:31B4 is NULL, so the first block is
 * always allocated).  The blocks are never freed.  The port mixes in
 * mcp_dma_buffer/mcp_tab but makes the same two allocations, so that the
 * game's far heap (block addresses, farcoreleft, MEMMAN's low-water marks)
 * is the original's - found by tools/verify (DSMI start-up heap block). */
int16_t SDIInitDevice(const uint8_t *sdi_cfg)
{
    McpConfig c;
    void *blk;
    if (mcp_config_from_sdi(sdi_cfg, &c) != 0)
        return -1;
    if (MemAlloc((int32_t)c.buffer_size + 0x20, &blk) != 0)
        return -1;
    if (MemAlloc((c.options & 1) ? 0xA030 : 0x4030, &blk) != 0)
        return -1;
    mcpInit(&c);
    mcp_fill_cdi_device(&mcp_device);
    if (imp_cdi_register)
        M.first_channel = ((int16_t (*)(const CdiDevice *))imp_cdi_register)(&mcp_device);
    return 0;
}

/* SDIStartOutput - Original: 105C: DSP speaker on, DMA auto-init start
 * (the platform starts playing mcp_dma_buffer from offset 0), then 2DEA. */
int16_t SDIStartOutput(void)
{
    mcp_dma_position = 0;
    mcpStartVoice();
    return 0;
}

/* SDIStopOutput - Original: 13A0: DMA stop, DSP reset, 2E3E. */
int16_t SDIStopOutput(void)
{
    mcpStopVoice();
    return 0;
}

/* SDICloseDevice - Original: 140A: stopOutput, restore the IRQ vector/mask. */
int16_t SDICloseDevice(void)
{
    SDIStopOutput();
    return 0;
}

/* SDIGetBufferPosition - Original: getBufPos 1382 -> DMAPosition 39DA:
 * bytes the DMA has played in the current lap of the ring. */
uint16_t SDIGetBufferPosition(void)
{
    return mcp_dma_position;
}

/* SDIDetectDevices (01DE), SDIQueryDeviceCount (03F6), SDIQueryDevice
 * (0406): registered like the original (the DDS registry and its heap
 * blocks - 12-byte node + name - are part of the game's far heap), but
 * IST.EXE never calls them; not decompiled, they return 0. */
static int16_t SDIDetectDevices(void)    { return 0; }
static int16_t SDIQueryDeviceCount(void) { return 0; }
static int16_t SDIQueryDevice(void)      { return 0; }

/*
 * sdisb16_entry - Original: dsd_main 147C, far pascal (long cmd, access,
 * long).  cmd 1: import the DDS functions, register the 8 SDI* functions
 * (1545-15B8, in this order) and (mcpOpenDDS) the MCPOutputBuffer resource
 * and the MCP imports; returns device 0x64, or error 5 after removing the
 * 8 functions again.  cmd 2: remove the 8 functions; returns arg2 (the
 * original returns its third argument).  Other commands: 1.
 */
uint32_t sdisb16_entry(int32_t cmd, DdsAccessFn access, int32_t arg2)
{
    static const char *const names[] = {
        "SDIDetectDevices", "SDIQueryDeviceCount", "SDIQueryDevice",
        "SDIInitDevice", "SDIStartOutput", "SDIGetBufferPosition", "SDIStopOutput",
        "SDICloseDevice" };
    static DdsFn reg_fn, rm_fn, reg_res;

    if (cmd == 1) {
        DdsFn f;
        if (access("DDSRegisterFunction", &reg_fn) != 0 || access("DDSRemoveFunction", &rm_fn) != 0)
            return 5;
        access("DDSRegisterResource", &reg_res);
        if (access("cdiRegister", &f) == 0)
            imp_cdi_register = f;
        ((int16_t (*)(const char *, DdsFn))reg_fn)(names[0], (DdsFn)SDIDetectDevices);
        ((int16_t (*)(const char *, DdsFn))reg_fn)(names[1], (DdsFn)SDIQueryDeviceCount);
        ((int16_t (*)(const char *, DdsFn))reg_fn)(names[2], (DdsFn)SDIQueryDevice);
        ((int16_t (*)(const char *, DdsFn))reg_fn)(names[3], (DdsFn)SDIInitDevice);
        ((int16_t (*)(const char *, DdsFn))reg_fn)(names[4], (DdsFn)SDIStartOutput);
        ((int16_t (*)(const char *, DdsFn))reg_fn)(names[5], (DdsFn)SDIGetBufferPosition);
        ((int16_t (*)(const char *, DdsFn))reg_fn)(names[6], (DdsFn)SDIStopOutput);
        ((int16_t (*)(const char *, DdsFn))reg_fn)(names[7], (DdsFn)SDICloseDevice);
        if (reg_res)
            ((int16_t (*)(const char *, void *, uint32_t))reg_res)("MCPOutputBuffer",
                                                                   mcp_output_resource, 0x12);
        if (mcpOpenDDS(access) != 0) {
            for (int i = 0; i < 8; i++)
                ((int16_t (*)(const char *))rm_fn)(names[i]);
            return 5;
        }
        return (uint32_t)0x64 << 16;
    }
    if (cmd == 2) {
        if (rm_fn)
            for (int i = 0; i < 8; i++)
                ((int16_t (*)(const char *))rm_fn)(names[i]);
        return (uint32_t)arg2;
    }
    return 1;
}
