/*
 * amfload.c - DSMI AMP module loader (AMF) and ampFreeModule.
 * Original: IST.EXE seg105B (ampLinkTracks, ampLoadAMF, ampLoadModule) and
 * seg111B (ampFreeModule).  Structures: amp.h.
 *
 * Files are read through the game's virtual file layer (ovl569A, vfs.h) and
 * memory comes from the DSMI MemAlloc/MemCalloc/MemFree wrappers
 * (memalloc.h), as in the original.  Samples bigger than 1 KB go to the DSMI
 * EMS heap when DDS exports "EMSAlloc" and "EMSCopyTo".
 *
 * AMF layout: docs/FORMATS.md / analysis/names/lib.md section 5.
 *
 * Verified: tests/py/test_amp.py - every AMF in extracted/MUZIK loaded by the
 * original (Unicorn) and by the port, module structures compared (header,
 * orders, instruments, tracks, sample bytes, size), also with fake EMS.
 */
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/dds.h"
#include "dsmi/dsmistr.h"
#include "dsmi/emsheap.h"
#include "dsmi/memalloc.h"
#include "engine/vfs.h"

/* DGROUP E78A/E78E/E792 far function pointers (native) */
static EmsCopyToFn amf_ems_copyto;      /* E78A */
static EmsAllocFn  amf_ems_alloc;       /* E78E */
static EmsFreeFn   amf_ems_free;        /* E792 */

/* the DDS exports the loader looks up are the EMS heap's (emsheap.c) */
_Static_assert(_Generic(&EMSAlloc, EmsAllocFn: 1, default: 0), "EMSAlloc type");
_Static_assert(_Generic(&EMSCopyTo, EmsCopyToFn: 1, default: 0), "EMSCopyTo type");
_Static_assert(_Generic(&EMSFree, EmsFreeFn: 1, default: 0), "EMSFree type");

#define AMF_MAGIC(v) (0x00464D41u | (uint32_t)(v) << 24)   /* "AMF" + version */

/*
 * ampLinkTracks - replace the track numbers in every order by the track
 * pointers (module->tracks[number]).
 *
 * Original: IST.EXE 105B:0009 (AMP loader), far cdecl (MODULE far*).  A track
 * number past trackCount reads past the table in the original (the table has
 * 3 spare zeroed entries); the port yields NULL there.
 * Verified: tests/py/test_amp.py.
 */
void ampLinkTracks(AmpModule *m)
{
    for (int16_t i = 0; m->patternCount > i; i++) {
        AmpPattern *o = &m->patterns[i];
        for (int16_t c = 0; m->channelCount > c; c++) {
            uint16_t ref = o->track_ref[c];
            o->track[c] = ref <= (uint32_t)m->trackCount + 3 ? m->tracks[ref] : NULL;
        }
    }
}

/*
 * ampLoadAMF - read an AMF (versions 1, 8, 9, 10..14) from an open handle
 * into a zeroed MODULE.  Returns 0, -1 (out of memory), -2 (sample read
 * failed), -3 (unknown version).
 *
 * Original: IST.EXE 105B:008A (AMP loader), far pascal (int handle, MODULE
 * far*, long options).  Kept: 16-bit sizes (sample length low word only,
 * track size count*3+6, "size" sums), samples assigned by the order of
 * increasing sample numbers (lengths recorded only when a number exceeds the
 * count so far), version < 11 pans replaced by the default table, the v1
 * three extra track bytes.  The lengths table is MemCalloc'ed without a
 * result check.  Not kept: a sample number with a non-zero high word stays
 * a garbage far pointer in the original - the port leaves NULL.
 * Verified: tests/py/test_amp.py.
 */
int16_t ampLoadAMF(int16_t h, AmpModule *m, int32_t options)
{
    uint32_t magic = 0;
    uint16_t npans;
    uint16_t extra = 0;             /* [BP-8]  v1: 3 extra bytes per track  */
    int      old_ins = 1;           /* [BP-0A] instrument records of 59 bytes */
    uint16_t nsamples = 0;          /* [BP-6]  */
    uint16_t maxsample = 0;         /* [BP-16] */
    uint16_t maxstored = 0;         /* [BP-14] */
    uint16_t *lengths = NULL;       /* [BP-0E] */
    uint16_t *trackmap = NULL;      /* [BP-12] */
    DdsFn fn;

    fn = 0;
    g_amf_ems_ok = DDSAccessFunction(g_amp_name_emsalloc, &fn) == 0;
    amf_ems_alloc = (EmsAllocFn)fn;
    if (g_amf_ems_ok) {
        fn = 0;
        g_amf_ems_ok = DDSAccessFunction(g_amp_name_emscopyto, &fn) == 0;
        amf_ems_copyto = (EmsCopyToFn)fn;
    }
    g_amf_options = (uint16_t)options;
    m->tempo = 125;
    m->speed = 6;
    vfs_seek(h, 0, 0);
    vfs_read(h, &magic, 4);
    vfs_read(h, m->name, 32);
    npans = (int32_t)magic >= (int32_t)AMF_MAGIC(12) ? 32 : 16;
    if (magic == AMF_MAGIC(1))
        extra = 3;
    else if ((int32_t)magic >= (int32_t)AMF_MAGIC(10))
        old_ins = 0;
    else if (magic != AMF_MAGIC(8) && magic != AMF_MAGIC(9))
        return -3;

    vfs_read(h, &m->instrumentCount, 1);
    vfs_read(h, &m->patternCount, 1);
    vfs_read(h, &m->trackCount, 2);
    if ((int32_t)magic >= (int32_t)AMF_MAGIC(9)) {
        vfs_read(h, &m->channelCount, 1);
        vfs_read(h, m->channelPanning, npans);
        if ((int32_t)magic < (int32_t)AMF_MAGIC(11))
            dsmiMemCpy(m->channelPanning, g_amp_default_pans, 16);
    }
    if ((int32_t)magic >= (int32_t)AMF_MAGIC(13)) {
        vfs_read(h, &m->tempo, 1);
        vfs_read(h, &m->speed, 1);
    }

    if (MemCallocNative(m->patternCount, 0x82, sizeof(AmpPattern), (void **)&m->patterns))
        return -1;
    if (MemCallocNative(m->instrumentCount, 0x41, sizeof(AmpInstrument), (void **)&m->instruments))
        return -1;
    if (MemCallocNative((uint16_t)(m->trackCount + 4), 4, sizeof(uint8_t *), (void **)&m->tracks))
        return -1;
    /* 16-bit sum, zero-extended */
    m->size += (uint16_t)(m->patternCount * 0x82 + m->instrumentCount * 0x41
                          + m->trackCount * 4 + 0x10);

    /* orders */
    for (uint16_t i = 0; m->patternCount > i; i++) {
        AmpPattern *o = &m->patterns[i];
        if ((int32_t)magic >= (int32_t)AMF_MAGIC(14))
            vfs_read(h, &o->rows, 2);
        else
            o->rows = 0x40;
        for (uint16_t c = 0; m->channelCount > c; c++)
            vfs_read(h, &o->track_ref[c], 2);
    }

    /* instruments */
    MemCalloc(m->instrumentCount, 2, (void **)&lengths);
    for (uint16_t i = 0; m->instrumentCount > i; i++) {
        AmpInstrument *ins = &m->instruments[i];
        uint8_t rec[0x41];
        if (old_ins) {
            vfs_read(h, rec, 0x3B);
            ins->length    = (uint16_t)(rec[0x32] | rec[0x33] << 8);
            ins->rate      = (uint16_t)(rec[0x34] | rec[0x35] << 8);
            ins->volume    = rec[0x36];
            ins->loopstart = (uint16_t)(rec[0x37] | rec[0x38] << 8);
            ins->loopend   = (uint16_t)(rec[0x39] | rec[0x3A] << 8);
            if (ins->loopend == 0xFFFF) {
                ins->loopstart = 0;
                ins->loopend = 0;
            }
        } else {
            vfs_read(h, rec, 0x41);
            memcpy(&ins->length, rec + 0x32, 4);
            memcpy(&ins->rate, rec + 0x36, 2);
            ins->volume = rec[0x38];
            memcpy(&ins->loopstart, rec + 0x39, 4);
            memcpy(&ins->loopend, rec + 0x3D, 4);
        }
        ins->type = rec[0];
        memcpy(ins->name, rec + 0x01, 32);
        memcpy(ins->filename, rec + 0x21, 13);
        memcpy(&ins->sample_index, rec + 0x2E, 4);
        if ((uint16_t)ins->sample_index > maxsample)
            maxsample = (uint16_t)ins->sample_index;
        if ((uint16_t)ins->sample_index > nsamples) {
            if (lengths)
                lengths[nsamples] = (uint16_t)ins->length;
            nsamples++;
        }
    }
    nsamples = maxsample;

    /* track map: logical track -> stored track number */
    MemCalloc(m->trackCount, 2, (void **)&trackmap);
    for (uint16_t i = 0; m->trackCount > i; i++) {
        uint16_t v = 0;
        vfs_read(h, &v, 2);
        if (trackmap)
            trackmap[i] = v;
        if (v > maxstored)
            maxstored = v;
    }
    for (uint16_t i = 1; (uint16_t)(m->trackCount + 1) > i; i++)
        m->tracks[i] = NULL;

    /* stored tracks */
    for (uint16_t s = 0; s < maxstored; s++) {
        uint16_t count = 0;
        uint8_t type;
        uint8_t *t = NULL;
        vfs_read(h, &count, 2);
        vfs_read(h, &type, 1);
        if (count) {
            if (MemAlloc((uint16_t)(count * 3 + 6), (void **)&t))
                return -1;
            m->size += (uint16_t)(count * 3 + 3);
            t[2] = 0;
            t[0] = (uint8_t)count;
            t[1] = (uint8_t)(count >> 8);
            vfs_read(h, t + 3, (uint16_t)(count * 3 + extra));
        }
        for (uint16_t j = 0; m->trackCount > j; j++)
            if (trackmap && trackmap[j] == (uint16_t)(s + 1))
                m->tracks[j + 1] = t;
    }

    /* samples, in sample-number order */
    for (uint16_t s = 0; s < nsamples; s++) {
        /* past the instrumentCount entries the original reads heap garbage */
        uint16_t len = lengths && s < m->instrumentCount ? lengths[s] : 0;
        CdiSampleRef ref = { 0 };
        uint8_t *p = NULL;
        if (len == 0)
            continue;
        if (MemAlloc((uint16_t)(len + 0x10), (void **)&p))
            return -1;
        /* not cleared: farmalloc'd, the block keeps stale heap bytes where
           the read comes up short (the 16 spare bytes are never read) */
        m->size += (uint16_t)(len + 0x10);
        if (vfs_read(h, p, len))
            return -2;
        ref.ptr = p;
        if (g_amf_ems_ok && len > 0x400) {
            int16_t eh = amf_ems_alloc(len);
            if (eh > 0) {
                amf_ems_copyto(eh, p, 0, len);
                MemFree(p);
                ref.ptr = NULL;             /* FFFF:handle */
                ref.ems_handle = (uint16_t)eh;
                ref.in_ems = 1;
            }
        }
        for (uint16_t j = 0; m->instrumentCount > j; j++) {
            AmpInstrument *ins = &m->instruments[j];
            /* word +2E == s+1 and high word 0 (= not yet a pointer) */
            if ((uint16_t)ins->sample_index == (uint16_t)(s + 1) && !(ins->sample_index >> 16)
                && cdi_ref_is_null(ins->sample))
                ins->sample = ref;
        }
    }
    /* numbers that got no sample become NULL (already NULL natively) */

    ampLinkTracks(m);
    MemFree(lengths);
    MemFree(trackmap);
    return 0;
}

/*
 * ampLoadModule - allocate a MODULE, open the file, check "AMF", load it.
 * Returns 0 and *out, -1 (no memory), -2 (cannot open), -3 (not an AMF /
 * unknown version), or ampLoadAMF's error (the module is then freed).
 *
 * Original: IST.EXE 105B:07F3 (AMP loader), far pascal (char far *name,
 * long options, MODULE far **out).  Kept: v1/v8 files get 4 channels and the
 * default pans; on -2/-3 from here the MODULE is leaked and (for -3) the file
 * left open; the file size is taken with vfs_tell AFTER vfs_close (so it is
 * whatever the file layer returns for a closed handle).
 * Verified: tests/py/test_amp.py.
 */
int16_t ampLoadModule(const char *filename, int32_t options, AmpModule **out)
{
    AmpModule *m = NULL;
    uint32_t magic = 0;
    int16_t h, r;

    g_amf_options = (uint16_t)options;
    if (MemAllocNative(0x5C, sizeof(AmpModule), (void **)&m))
        return -1;
    memset(m, 0, sizeof *m);                    /* dsmiMemSet(m, 0, 0x5C) */
    h = vfs_open(filename, 2);
    if (h == -1)
        return -2;
    m->type = 0;
    vfs_seek(h, 0, 0);
    vfs_read(h, &magic, 4);
    if ((magic & 0xFFFFFF) != 0x464D41)
        return -3;
    m->type = 3;
    vfs_read(h, m->name, 0x14);
    m->name[0x14] = 0;
    if (magic == AMF_MAGIC(8) || magic == AMF_MAGIC(1)) {
        m->channelCount = 4;
        dsmiMemCpy(m->channelPanning, g_amp_default_pans, 4);
    }
    r = ampLoadAMF(h, m, options);
    if (r == 0) {
        vfs_seek(h, 0, 2);
        vfs_close(h);
        m->filesize = (uint32_t)vfs_tell(h);
        *out = m;
        return 0;
    }
    ampFreeModule(m);
    vfs_close(h);
    MemFree(m);
    return r;
}

/*
 * ampFreeModule - free orders, samples (EMS ones with EMSFree), instruments,
 * tracks; shared samples/tracks are freed once.  The MODULE itself is not
 * freed and its fields are left as they are.
 *
 * Original: IST.EXE 111B:0003 (AMP), far pascal (MODULE far*).  An EMS sample
 * without an "EMSFree" export would be MemFree'd as FFFF:handle in the
 * original; the port skips it (cannot happen: EMS samples need the EMS heap).
 * Verified: tests/py/test_amp.py (call sequence of EMSFree, no crash).
 */
void ampFreeModule(AmpModule *m)
{
    DdsFn fn = 0;
    int have_free = DDSAccessFunction(g_amp_name_emsfree, &fn) == 0;

    amf_ems_free = (EmsFreeFn)fn;
    if (!m || m->type == 0)
        return;
    if (m->patterns)
        MemFree(m->patterns);
    if (m->instruments) {
        for (int16_t i = 0; m->instrumentCount > i; i++) {
            CdiSampleRef s = m->instruments[i].sample;
            if (cdi_ref_is_null(s))
                continue;
            for (int16_t j = 0; j < i; j++)
                if (cdi_ref_equal(m->instruments[j].sample, s)) {
                    s = (CdiSampleRef){ 0 };
                    break;
                }
            if (have_free && s.in_ems)
                amf_ems_free((int16_t)s.ems_handle);
            else if (s.ptr)
                MemFree(s.ptr);
        }
        MemFree(m->instruments);
    }
    if (m->tracks) {
        for (uint16_t i = 1; (uint16_t)(m->trackCount + 1) > i; i++) {
            uint8_t *t = m->tracks[i];
            for (uint16_t j = 1; j < i; j++)
                if (m->tracks[j] == t) {
                    t = NULL;
                    break;
                }
            if (t)
                MemFree(t);
        }
        MemFree(m->tracks);
    }
}
