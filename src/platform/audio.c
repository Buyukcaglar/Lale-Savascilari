/*
 * audio.c - emulated SB16 DMA over the MCP ring + host output mix.
 * See audio.h for the model and why the game audio stays bit-exact.
 */
#include "platform/audio.h"

#include <string.h>

#include "dsmi/mcp.h"
#include "platform/cdrom.h"
#include "platform/pc.h"

/* ---- DMA ------------------------------------------------------------------ */
static int      dma_on;
static uint64_t dma_t0;            /* PIT clock at audio_dma_start */
static uint16_t dma_rate;
static uint8_t  dma_bpf;           /* bytes per frame */
static uint8_t  dma_ss, dma_stereo;
static uint16_t dma_ring;
static uint64_t dma_bytes;         /* bytes played (= captured) since start */

/* ---- captured game bytes -------------------------------------------------- */
static uint8_t  fifo[AUDIO_FIFO_BYTES];
static size_t   fifo_head, fifo_len;    /* ring buffer: head = oldest */
static uint32_t fifo_dropped;
/* format of the bytes in the FIFO (they outlive a restart with another format
   only in theory: the game starts the DMA once) */
static uint8_t  fmt_ss = 2, fmt_stereo = 1, fmt_bpf = 4;
static uint16_t fmt_rate = 22050;

/* ---- output ------------------------------------------------------------- */
static int      out_started;
static uint64_t out_t0;            /* PIT clock of output frame 0 */
static uint64_t out_frames;
static int      cd_on;
static int16_t  rs_prev[2], rs_cur[2];
static uint64_t rs_frac;           /* 32.32 position between prev and cur */

void audio_reset(void)
{
    dma_on = 0;
    dma_t0 = dma_bytes = 0;
    dma_rate = 0;
    dma_bpf = dma_ss = dma_stereo = 0;
    dma_ring = 0;
    fifo_head = fifo_len = 0;
    fifo_dropped = 0;
    fmt_ss = 2; fmt_stereo = 1; fmt_bpf = 4; fmt_rate = 22050;
    out_started = 0;
    out_t0 = out_frames = 0;
    rs_prev[0] = rs_prev[1] = rs_cur[0] = rs_cur[1] = 0;
    rs_frac = 0;
}

static void fifo_put(const uint8_t *p, size_t n)
{
    while (n) {
        if (fifo_len == AUDIO_FIFO_BYTES) {      /* full: drop the oldest */
            size_t d = n < AUDIO_FIFO_BYTES ? n : AUDIO_FIFO_BYTES;
            fifo_head = (fifo_head + d) % AUDIO_FIFO_BYTES;
            fifo_len -= d;
            fifo_dropped += (uint32_t)d;
        }
        size_t tail = (fifo_head + fifo_len) % AUDIO_FIFO_BYTES;
        size_t k = AUDIO_FIFO_BYTES - tail;
        if (k > n) k = n;
        if (k > AUDIO_FIFO_BYTES - fifo_len) k = AUDIO_FIFO_BYTES - fifo_len;
        memcpy(fifo + tail, p, k);
        fifo_len += k;
        p += k;
        n -= k;
    }
}

void audio_dma_start(uint16_t rate, uint8_t sample_size, uint8_t stereo, uint16_t ring)
{
    dma_on = rate && ring;
    dma_t0 = pc_pit_ticks();
    dma_rate = rate;
    dma_ss = sample_size;
    dma_stereo = stereo;
    dma_bpf = (uint8_t)(sample_size << (stereo ? 1 : 0));
    dma_ring = ring;
    dma_bytes = 0;
    fmt_ss = sample_size; fmt_stereo = stereo; fmt_bpf = dma_bpf; fmt_rate = rate;
    mcp_dma_position = 0;
}

void audio_dma_stop(void)
{
    audio_dma_sync();
    dma_on = 0;
}

int      audio_dma_running(void)      { return dma_on; }
uint64_t audio_dma_bytes_played(void) { return dma_bytes; }

void audio_dma_sync(void)
{
    uint64_t frames, target;
    if (!dma_on)
        return;
    /* whole frames played at the nominal rate since the start */
    frames = (pc_pit_ticks() - dma_t0) * dma_rate / PC_PIT_HZ;
    target = frames * dma_bpf;
    if (target > dma_bytes) {
        uint64_t n = target - dma_bytes;
        /* more than the FIFO holds: only the newest part can be kept (the
           ring is unchanged between polls, so this is still exact) */
        if (n > AUDIO_FIFO_BYTES) {
            uint64_t skip = n - AUDIO_FIFO_BYTES;
            fifo_dropped += (uint32_t)skip;
            dma_bytes += skip;
            n = AUDIO_FIFO_BYTES;
        }
        while (n) {
            uint16_t pos = (uint16_t)(dma_bytes % dma_ring);
            uint64_t k = (uint64_t)dma_ring - pos;
            if (k > n) k = n;
            fifo_put(mcp_dma_buffer + pos, (size_t)k);
            dma_bytes += k;
            n -= k;
        }
    }
    mcp_dma_position = (uint16_t)(dma_bytes % dma_ring);
}

size_t audio_game_available(void) { return fifo_len; }
uint32_t audio_game_dropped(void) { return fifo_dropped; }

size_t audio_game_take(uint8_t *out, size_t max)
{
    size_t n = max < fifo_len ? max : fifo_len, done = 0;
    while (done < n) {
        size_t k = AUDIO_FIFO_BYTES - fifo_head;
        if (k > n - done) k = n - done;
        if (out)
            memcpy(out + done, fifo + fifo_head, k);
        fifo_head = (fifo_head + k) % AUDIO_FIFO_BYTES;
        fifo_len -= k;
        done += k;
    }
    return n;
}

void audio_set_cd(int on) { cd_on = on; }
uint64_t audio_frames_rendered(void) { return out_frames; }

/* next game frame as s16 stereo; silence when the FIFO is empty */
static void next_game_frame(int16_t f[2])
{
    uint8_t b[4];
    if (fifo_len < fmt_bpf) {
        f[0] = f[1] = 0;
        return;
    }
    audio_game_take(b, fmt_bpf);
    if (fmt_ss == 2) {
        f[0] = (int16_t)(b[0] | b[1] << 8);
        f[1] = fmt_stereo ? (int16_t)(b[2] | b[3] << 8) : f[0];
    } else {
        f[0] = (int16_t)((b[0] - 128) * 256);
        f[1] = fmt_stereo ? (int16_t)((b[1] - 128) * 256) : f[0];
    }
}

size_t audio_render(int16_t *out, size_t max_frames)
{
    uint64_t now, target, n, step;
    int16_t cd[2 * 1024];

    audio_dma_sync();
    now = pc_pit_ticks();
    if (!out_started) {
        out_started = 1;
        out_t0 = now;
    }
    target = (now - out_t0) * AUDIO_OUT_RATE / PC_PIT_HZ;
    target = target > AUDIO_LAG_FRAMES ? target - AUDIO_LAG_FRAMES : 0;
    if (target <= out_frames)
        return 0;
    n = target - out_frames;
    if (n > max_frames)
        n = max_frames;

    /* game stream: linear interpolation from fmt_rate to 44100 Hz */
    step = ((uint64_t)fmt_rate << 32) / AUDIO_OUT_RATE;
    for (uint64_t i = 0; i < n; i++) {
        int64_t t = (int64_t)(rs_frac >> 16);              /* 0..65535 */
        for (int c = 0; c < 2; c++)
            out[2 * i + c] = (int16_t)(rs_prev[c] + (((int64_t)rs_cur[c] - rs_prev[c]) * t >> 16));
        rs_frac += step;
        while (rs_frac >= (1ull << 32)) {
            rs_frac -= 1ull << 32;
            rs_prev[0] = rs_cur[0];
            rs_prev[1] = rs_cur[1];
            if (dma_on || fifo_len)
                next_game_frame(rs_cur);
            else
                rs_cur[0] = rs_cur[1] = 0;
        }
    }

    /* CD audio, 44.1 kHz already: add and clip */
    if (cd_on) {
        for (uint64_t i = 0; i < n; ) {
            size_t k = (size_t)(n - i > 1024 ? 1024 : n - i);
            cdrom_render(cd, k);
            for (size_t j = 0; j < 2 * k; j++) {
                int32_t s = out[2 * i + j] + cd[j];
                out[2 * i + j] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
            }
            i += k;
        }
    }
    out_frames += n;
    return (size_t)n;
}
