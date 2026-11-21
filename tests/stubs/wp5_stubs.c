/*
 * wp5_stubs.c - TEST ONLY helpers for tests/py/test_amp.py (WP5: CDI, AMP,
 * AMF loader).  Not part of the game build.
 *
 *  - a recording CDI device: every call appended to wp5_log[] as
 *    {slot, a, b, c}; sample refs encoded by wp5_ref_id().
 *  - fake DDS exports MCPSampleRealAddress / EMSAlloc / EMSCopyTo / EMSFree.
 *  - wp5_dump_module(): the loaded module serialised in a canonical form the
 *    Python side also builds from the original's memory.
 */
#include <stdlib.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/cdi.h"
#include "dsmi/dds.h"

/* ---- recording CDI device ------------------------------------------------------ */
typedef struct { int32_t slot, a, b, c; } Wp5Call;
#define WP5_LOG_MAX 400000
Wp5Call wp5_log[WP5_LOG_MAX];
int32_t wp5_log_n;
int32_t wp5_download_result;            /* returned by download */
static AmpModule *wp5_module;           /* for sample identities */

void wp5_set_module(AmpModule *m) { wp5_module = m; }

/* sample identity: 0 = null, 0x10000|handle = EMS, else 1 + first instrument
 * whose sample has this data pointer, -1 = unknown */
static int32_t wp5_ref_id(CdiSampleRef r)
{
    if (cdi_ref_is_null(r))
        return 0;
    if (r.in_ems)
        return 0x10000 | r.ems_handle;
    if (wp5_module)
        for (int i = 0; i < wp5_module->instrumentCount; i++)
            if (wp5_module->instruments[i].sample.ptr == r.ptr)
                return i + 1;
    return -1;
}

static void wp5_rec(int32_t slot, int32_t a, int32_t b, int32_t c)
{
    if (wp5_log_n < WP5_LOG_MAX)
        wp5_log[wp5_log_n++] = (Wp5Call){ slot, a, b, c };
}

static int32_t r_setSample(int32_t ch, CdiSampleInfo *si)
{
    wp5_rec(0x00, ch, wp5_ref_id(si->sample), (int32_t)si->length);
    wp5_rec(0x100, (int32_t)si->loopstart, (int32_t)si->loopend, si->mode | si->sampleID << 8);
    wp5_rec(0x101, si->rate, (int32_t)si->freq, si->volume);
    return 0;
}
static int32_t r_playSample(int32_t ch, uint32_t f, uint32_t v) { wp5_rec(0x04, ch, (int32_t)f, (int32_t)v); return 0; }
static int32_t r_setVolume(int32_t ch, uint32_t v)   { wp5_rec(0x08, ch, (int32_t)v, 0); return 0; }
static int32_t r_setFrequency(int32_t ch, uint32_t f) { wp5_rec(0x0C, ch, (int32_t)f, 0); return 0; }
static int32_t r_setLinearRate(int32_t ch, uint32_t r) { wp5_rec(0x10, ch, (int32_t)r, 0); return 0; }
static int32_t r_setPosition(int32_t ch, uint32_t p) { wp5_rec(0x14, ch, (int32_t)p, 0); return 0; }
static int32_t r_setPanning(int32_t ch, int32_t p)   { wp5_rec(0x18, ch, p, 0); return 0; }
static int32_t r_setMasterVolume(uint32_t v)         { wp5_rec(0x1C, (int32_t)v, 0, 0); return 0; }
static int32_t r_mute(int32_t ch)                    { wp5_rec(0x20, ch, 0, 0); return 0; }
static int32_t r_unmute(int32_t ch)                  { wp5_rec(0x24, ch, 0, 0); return 0; }
static int32_t r_stop(int32_t ch)                    { wp5_rec(0x28, ch, 0, 0); return 0; }
static int32_t r_muteAll(void)                       { wp5_rec(0x2C, 0, 0, 0); return 0; }
static int32_t r_unmuteAll(void)                     { wp5_rec(0x30, 0, 0, 0); return 0; }
static int32_t r_download(uint8_t *s, CdiSampleRef t, uint32_t len)
{
    /* sample data checksum instead of the pointer */
    uint32_t sum = 0;
    for (uint32_t i = 0; s && i < len; i++)
        sum = sum * 31 + s[i];
    wp5_rec(0x34, (int32_t)sum, wp5_ref_id(t), (int32_t)len);
    return wp5_download_result;
}
static int32_t r_unload(CdiSampleRef t)              { wp5_rec(0x38, wp5_ref_id(t), 0, 0); return 0; }
static int32_t r_unloadAll(void)                     { wp5_rec(0x3C, 0, 0, 0); return 0; }
static int32_t r_get(int32_t ch)                     { wp5_rec(0x40, ch, 0, 0); return 0; }
static CdiSampleRef r_getSample(int32_t ch)          { CdiSampleRef z = { 0 }; wp5_rec(0x50, ch, 0, 0); return z; }
static int32_t r_setup(int32_t n)                    { wp5_rec(0x54, n, 0, 0); return 0; }
static void    r_poll(void)                          { wp5_rec(0x5C, 0, 0, 0); }
static int32_t r_setEffect(int32_t a, int32_t b)     { wp5_rec(0x60, a, b, 0); return 0; }
static int32_t r_getStatus(int32_t a)                { wp5_rec(0x64, a, 0, 0); return 0; }

static const CdiDevice wp5_dev = {
    r_setSample, r_playSample, r_setVolume, r_setFrequency, r_setLinearRate,
    r_setPosition, r_setPanning, r_setMasterVolume, r_mute, r_unmute, r_stop,
    r_muteAll, r_unmuteAll, r_download, r_unload, r_unloadAll, r_get, r_get,
    r_get, r_get, r_getSample, r_setup, r_get, r_poll, r_setEffect, r_getStatus,
};

int16_t wp5_register_device(void) { return cdiRegister(&wp5_dev); }

/* ---- fake DDS exports ------------------------------------------------------------ */
static uint8_t wp5_ems[64][0x10000];
static int16_t wp5_ems_next = 1;

static uint8_t *f_real_address(uint32_t h, uint32_t pos)
{
    wp5_rec(0x200, (int32_t)h, (int32_t)pos, 0);
    return wp5_ems[h & 63] + pos;
}
static int16_t f_ems_alloc(int32_t size)
{
    wp5_rec(0x201, size, wp5_ems_next, 0);
    return wp5_ems_next++;
}
static int16_t f_ems_copyto(int16_t h, const void *src, int32_t off, int32_t len)
{
    memcpy(wp5_ems[h & 63] + off, src, (size_t)len);
    wp5_rec(0x202, h, off, len);
    return 0;
}
static void f_ems_free(int16_t h) { wp5_rec(0x203, h, 0, 0); }

/* register the fakes with the real DDS; ems = also the EMS functions */
void wp5_register_dds(int ems)
{
    wp5_ems_next = 1;
    DDSRegisterFunction("MCPSampleRealAddress", (DdsFn)f_real_address);
    if (ems) {
        DDSRegisterFunction("EMSAlloc", (DdsFn)f_ems_alloc);
        DDSRegisterFunction("EMSCopyTo", (DdsFn)f_ems_copyto);
        DDSRegisterFunction("EMSFree", (DdsFn)f_ems_free);
    }
}

/* ---- canonical module dump ----------------------------------------------------
 * header: type size filesize name[32] ch pans[32] ins ord trackCount tempo speed
 * orders: rows, then per channel the track index (0 = NULL, n = tracks[n],
 *         0xFFFF = not in the table)
 * instruments: type name filename length rate volume loopstart loopend,
 *         sample id (wp5_ref_id) and the sample bytes (EMS: from the fake EMS)
 * tracks: for i in 1..trackCount: index of the first equal pointer, then the
 *         data (count*3+3 bytes + 3) when first seen */
static uint8_t *wp5_out;
static int32_t wp5_out_n, wp5_out_max;
static void put(const void *p, int32_t n)
{
    if (wp5_out_n + n <= wp5_out_max)
        memcpy(wp5_out + wp5_out_n, p, (size_t)n);
    wp5_out_n += n;
}
static void put8(uint8_t v)  { put(&v, 1); }
static void put16(uint16_t v) { put(&v, 2); }
static void put32(uint32_t v) { put(&v, 4); }

int32_t wp5_dump_module(AmpModule *m, uint8_t *out, int32_t max)
{
    wp5_out = out; wp5_out_n = 0; wp5_out_max = max;
    wp5_module = m;
    put8(m->type); put32(m->size); put32(m->filesize); put(m->name, 32);
    put8(m->channelCount); put(m->channelPanning, 32); put8(m->instrumentCount);
    put8(m->patternCount); put16(m->trackCount); put8(m->tempo); put8(m->speed);
    for (int i = 0; i < m->patternCount; i++) {
        AmpPattern *o = &m->patterns[i];
        put16(o->rows);
        for (int c = 0; c < m->channelCount; c++) {
            uint16_t idx = 0xFFFF;
            if (!o->track[c])
                idx = 0;
            else
                for (int k = 1; k <= m->trackCount; k++)
                    if (m->tracks[k] == o->track[c]) { idx = (uint16_t)k; break; }
            put16(idx);
        }
    }
    for (int i = 0; i < m->instrumentCount; i++) {
        AmpInstrument *ins = &m->instruments[i];
        int32_t id = wp5_ref_id(ins->sample);
        put8(ins->type); put(ins->name, 32); put(ins->filename, 13);
        put32(ins->length); put16(ins->rate); put8(ins->volume);
        put32(ins->loopstart); put32(ins->loopend);
        put32((uint32_t)(ins->sample.in_ems ? id : !cdi_ref_is_null(ins->sample)));
        if (ins->sample.in_ems)
            put(wp5_ems[ins->sample.ems_handle & 63], (uint16_t)ins->length);
        else if (ins->sample.ptr)
            put(ins->sample.ptr, (uint16_t)ins->length);
    }
    for (int i = 1; i <= m->trackCount; i++) {
        uint8_t *t = m->tracks[i];
        uint16_t first = (uint16_t)i;
        for (int k = 1; k < i; k++)
            if (m->tracks[k] == t) { first = (uint16_t)k; break; }
        put16(t ? first : 0);
        if (t && first == i) {
            uint16_t count = (uint16_t)(t[0] | t[1] << 8);
            put(t, (uint16_t)(count * 3 + 3));
        }
    }
    return wp5_out_n;
}

/* SAMPLEINFO of channel ch serialised as the original 26 bytes (the sample
 * far pointer replaced by wp5_ref_id) */
void wp5_sampleinfo(int ch, uint8_t *out)
{
    CdiSampleInfo *si = &amp_sampleinfo[ch];
    int32_t id = wp5_ref_id(si->sample);
    memcpy(out + 0x00, &id, 4);
    memcpy(out + 0x04, &si->length, 4);
    memcpy(out + 0x08, &si->loopstart, 4);
    memcpy(out + 0x0C, &si->loopend, 4);
    out[0x10] = si->mode;
    memcpy(out + 0x11, &si->sampleID, 2);
    memcpy(out + 0x13, &si->rate, 2);
    memcpy(out + 0x15, &si->freq, 4);
    out[0x19] = si->volume;
}

/* forget the native player state (the original's lives in DGROUP, which
 * dg_reset clears; SAMPLEINFO is not cleared by ampInit) */
void wp5_reset(void)
{
    memset(amp_sampleinfo, 0, sizeof amp_sampleinfo);
    memset(amp_track_data, 0, sizeof amp_track_data);
}
