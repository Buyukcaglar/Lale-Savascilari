/*
 * chd.c - see chd.h.
 *
 * File layout written here (all numbers big-endian):
 *   0      header, 124 bytes: "MComprHD", 124, 5, four codec tags, logical
 *          size, map offset, metadata offset, hunk size, unit size, SHA-1 of
 *          the data, SHA-1 of data + metadata, SHA-1 of the parent (0)
 *   124    the hunks, each compressed on its own, in order
 *   ...    metadata: one 'CHT2' entry per track
 *   ...    the map: 16-byte header, then a bit stream - a Huffman tree, the
 *          run-length coded compression type of every hunk, then per hunk
 *          its length and CRC-16 (or the hunk it repeats)
 * Verified: tests/test_codecs.c (synthetic discs, written and read back);
 * tools/chd.py (an independent reader in Python) on DATA/LALE.CHD.
 */
#include "chd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/cdsector.h"
#include "platform/flac.h"
#include "platform/zdeflate.h"

#define FRAME        2448u              /* sector + subcode */
#define SECTOR       2352u
#define HUNK_FRAMES  8u
#define HUNK         (FRAME * HUNK_FRAMES)
#define PAD_FRAMES   4u                 /* every track is padded to this */
#define FLAC_BLOCK   2352u              /* chd_cd_flac_compressor::blocksize */

#define TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define TAG_CDZL TAG('c', 'd', 'z', 'l')
#define TAG_CDFL TAG('c', 'd', 'f', 'l')
#define TAG_CHT2 TAG('C', 'H', 'T', '2')
#define TAG_CHTR TAG('C', 'H', 'T', 'R')

enum { COMP_TYPE_0, COMP_TYPE_1, COMP_TYPE_2, COMP_TYPE_3, COMP_NONE, COMP_SELF, COMP_PARENT,
       COMP_RLE_SMALL, COMP_RLE_LARGE, COMP_SELF_0, COMP_SELF_1, COMP_PARENT_SELF, COMP_PARENT_0,
       COMP_PARENT_1 };

/* ---- helpers ----------------------------------------------------------------- */

static void seterr(char *err, size_t n, const char *msg, const char *arg)
{
    if (err && n)
        snprintf(err, n, "%s%s%s", msg, arg ? ": " : "", arg ? arg : "");
}

static uint64_t get_be(const uint8_t *p, int n)
{
    uint64_t v = 0;
    while (n--)
        v = v << 8 | *p++;
    return v;
}

static void put_be(uint8_t *p, uint64_t v, int n)
{
    while (n--)
        *p++ = (uint8_t)(v >> (8 * n));
}

static int seek64(FILE *f, uint64_t off)
{
#if defined(_WIN32)
    return _fseeki64(f, (long long)off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}

/* CRC-16/CCITT-FALSE (MAME crc16_creator): polynomial 1021h, start FFFFh */
static uint16_t crc16(const uint8_t *p, size_t n)
{
    static uint16_t tab[256];
    uint16_t c = 0xFFFF;
    if (!tab[1])
        for (int i = 0; i < 256; i++) {
            uint16_t v = (uint16_t)(i << 8);
            for (int k = 0; k < 8; k++)
                v = (uint16_t)((v << 1) ^ (v & 0x8000 ? 0x1021 : 0));
            tab[i] = v;
        }
    while (n--)
        c = (uint16_t)((c << 8) ^ tab[(c >> 8) ^ *p++]);
    return c;
}

/* ---- SHA-1 (FIPS 180-4) ---------------------------------------------------------- */

typedef struct Sha1 {
    uint32_t h[5];
    uint64_t len;
    uint8_t  buf[64];
} Sha1;

static void sha1_init(Sha1 *s)
{
    s->h[0] = 0x67452301; s->h[1] = 0xEFCDAB89; s->h[2] = 0x98BADCFE;
    s->h[3] = 0x10325476; s->h[4] = 0xC3D2E1F0;
    s->len = 0;
}

static void sha1_block(Sha1 *s, const uint8_t *p)
{
    uint32_t w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)get_be(p + 4 * i, 4);
    for (int i = 16; i < 80; i++) {
        uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = x << 1 | x >> 31;
    }
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
        uint32_t t = (a << 5 | a >> 27) + f + e + k + w[i];
        e = d; d = c; c = b << 30 | b >> 2; b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

static void sha1_add(Sha1 *s, const void *data, size_t n)
{
    const uint8_t *p = data;
    size_t fill = (size_t)(s->len & 63);
    s->len += n;
    if (fill) {
        size_t take = 64 - fill < n ? 64 - fill : n;
        memcpy(s->buf + fill, p, take);
        p += take;
        n -= take;
        if (fill + take < 64)
            return;
        sha1_block(s, s->buf);
    }
    for (; n >= 64; p += 64, n -= 64)
        sha1_block(s, p);
    memcpy(s->buf, p, n);
}

static void sha1_done(Sha1 *s, uint8_t *out)
{
    uint64_t bits = s->len * 8;
    uint8_t pad[72] = { 0x80 };
    size_t fill = (size_t)(s->len & 63), n = (fill < 56 ? 56 : 120) - fill;
    put_be(pad + n, bits, 8);
    sha1_add(s, pad, n + 8);
    for (int i = 0; i < 5; i++)
        put_be(out + 4 * i, s->h[i], 4);
}

/* ---- bit streams, most significant bit first --------------------------------------- */

typedef struct BitOut {
    uint8_t *buf;
    size_t   cap, len;
    uint32_t acc;
    int      n;
} BitOut;

static void bo_write(BitOut *b, uint32_t v, int bits)
{
    while (bits > 0) {
        int take = bits > 16 ? 16 : bits;
        bits -= take;
        b->acc = b->acc << take | ((v >> bits) & ((1u << take) - 1));
        b->n += take;
        while (b->n >= 8) {
            b->n -= 8;
            if (b->len < b->cap)
                b->buf[b->len] = (uint8_t)(b->acc >> b->n);
            b->len++;
        }
    }
}

static size_t bo_flush(BitOut *b)
{
    if (b->n)
        bo_write(b, 0, 8 - b->n);
    return b->len;
}

typedef struct BitIn {
    const uint8_t *buf;
    size_t         len, pos;
    uint32_t       acc;
    int            n;
    int            over;
} BitIn;

static uint32_t bi_peek(BitIn *b, int bits)
{
    while (b->n < bits) {
        uint8_t c = 0;
        if (b->pos < b->len)
            c = b->buf[b->pos];
        else
            b->over = 1;
        b->pos++;
        b->acc = b->acc << 8 | c;
        b->n += 8;
    }
    return (b->acc >> (b->n - bits)) & ((1u << bits) - 1);
}

static uint32_t bi_read(BitIn *b, int bits)
{
    uint32_t v = 0;
    while (bits > 0) {
        int take = bits > 16 ? 16 : bits;
        v = v << take | bi_peek(b, take);
        b->n -= take;
        bits -= take;
    }
    return v;
}

/* ---- the map's Huffman code: 16 symbols, at most 8 bits (MAME huffman.cpp) ---------- */

#define HSYMS 16
#define HMAX  8

/* canonical codes from the lengths: the longest codes get the lowest values */
static int huff_assign(const uint8_t *len, uint16_t *code)
{
    uint32_t histo[33] = {0}, start = 0;
    for (int i = 0; i < HSYMS; i++) {
        if (len[i] > HMAX)
            return -1;
        histo[len[i]]++;
    }
    for (int l = 32; l > 0; l--) {
        uint32_t next = (start + histo[l]) >> 1;
        if (l != 1 && next * 2 != start + histo[l])
            return -1;
        histo[l] = start;
        start = next;
    }
    for (int i = 0; i < HSYMS; i++)
        if (len[i])
            code[i] = (uint16_t)histo[len[i]]++;
    return 0;
}

static int huff_decode(BitIn *b, const uint8_t *len, const uint16_t *code)
{
    uint32_t bits = bi_peek(b, HMAX);
    for (int i = 0; i < HSYMS; i++)
        if (len[i] && (bits >> (HMAX - len[i])) == code[i]) {
            b->n -= len[i];
            return i;
        }
    return -1;
}

/* import_tree_rle: 4 bits per length, 1 = escape (1,1 = a single 1; 1,v,n = v repeated n+3 times) */
static int huff_import(BitIn *b, uint8_t *len, uint16_t *code)
{
    int cur = 0;
    while (cur < HSYMS) {
        int v = (int)bi_read(b, 4);
        if (v != 1) {
            len[cur++] = (uint8_t)v;
            continue;
        }
        v = (int)bi_read(b, 4);
        if (v == 1) {
            len[cur++] = 1;
        } else {
            int rep = (int)bi_read(b, 4) + 3;
            if (cur + rep > HSYMS)
                return -1;
            while (rep--)
                len[cur++] = (uint8_t)v;
        }
    }
    return huff_assign(len, code);
}

static void huff_export(BitOut *b, const uint8_t *len)
{
    for (int i = 0; i < HSYMS;) {
        int v = len[i], rep = 1;
        while (i + rep < HSYMS && len[i + rep] == v)
            rep++;
        i += rep;
        while (rep > 0) {
            if (v == 1) {
                bo_write(b, 1, 4);
                bo_write(b, 1, 4);
                rep--;
            } else if (rep <= 2) {
                bo_write(b, (uint32_t)v, 4);
                rep--;
            } else {
                int cur = rep - 3 > 15 ? 15 : rep - 3;
                bo_write(b, 1, 4);
                bo_write(b, (uint32_t)v, 4);
                bo_write(b, (uint32_t)cur, 4);
                rep -= cur + 3;
            }
        }
    }
}

/* Lengths of a complete prefix code of at most HMAX bits for the symbols used. */
static void huff_build(const uint32_t *histo, uint8_t *len)
{
    uint32_t w[HSYMS];
    int used = 0;
    for (int i = 0; i < HSYMS; i++) {
        w[i] = histo[i];
        used += histo[i] != 0;
    }
    for (int i = 0; used < 2 && i < HSYMS; i++)     /* a code needs two symbols */
        if (!w[i]) {
            w[i] = 1;
            used++;
        }
    for (;;) {
        /* plain Huffman by repeated merging of the two lightest groups */
        uint32_t gw[HSYMS];
        int group[HSYMS], ngroups = 0, maxlen = 0;
        memset(len, 0, HSYMS);
        for (int i = 0; i < HSYMS; i++) {
            group[i] = -1;
            if (w[i]) {
                group[i] = ngroups;
                gw[ngroups++] = w[i];
            }
        }
        for (int left = ngroups; left > 1; left--) {
            int a = -1, b = -1;
            for (int g = 0; g < ngroups; g++) {
                if (gw[g] == 0)
                    continue;
                if (a < 0 || gw[g] < gw[a]) {
                    b = a;
                    a = g;
                } else if (b < 0 || gw[g] < gw[b]) {
                    b = g;
                }
            }
            for (int i = 0; i < HSYMS; i++)
                if (group[i] == a || group[i] == b) {
                    group[i] = a;
                    len[i]++;
                }
            gw[a] += gw[b];
            gw[b] = 0;
        }
        for (int i = 0; i < HSYMS; i++)
            if (len[i] > maxlen)
                maxlen = len[i];
        if (maxlen <= HMAX)
            return;
        for (int i = 0; i < HSYMS; i++)             /* flatten the weights and try again */
            if (w[i])
                w[i] = (w[i] + 1) / 2;
    }
}

/* ======================================================================== */
/* the two CD codecs                                                        */
/* ======================================================================== */

static void split_hunk(const uint8_t *hunk, uint8_t *base, uint8_t *sub)
{
    for (uint32_t f = 0; f < HUNK_FRAMES; f++) {
        memcpy(base + f * SECTOR, hunk + f * FRAME, SECTOR);
        memcpy(sub + f * 96, hunk + f * FRAME + SECTOR, 96);
    }
}

static void join_hunk(uint8_t *hunk, const uint8_t *base, const uint8_t *sub)
{
    for (uint32_t f = 0; f < HUNK_FRAMES; f++) {
        memcpy(hunk + f * FRAME, base + f * SECTOR, SECTOR);
        memcpy(hunk + f * FRAME + SECTOR, sub + f * 96, 96);
    }
}

/* cdzl: ECC bitmap (1 byte), length of the sector stream (2 bytes), the
   sectors deflated, the subcode deflated */
static size_t cdzl_compress(const uint8_t *hunk, uint8_t *out, size_t cap)
{
    uint8_t base[HUNK_FRAMES * SECTOR], sub[HUNK_FRAMES * 96];
    size_t blen, slen;
    if (cap < 4)
        return 0;
    split_hunk(hunk, base, sub);
    out[0] = 0;
    for (uint32_t f = 0; f < HUNK_FRAMES; f++) {
        uint8_t *s = base + f * SECTOR;
        if (memcmp(s, cd_sync, 12) == 0 && cd_ecc_ok(s)) {
            out[0] |= (uint8_t)(1 << f);
            memset(s, 0, 12);
            memset(s + 2076, 0, 276);
        }
    }
    blen = z_deflate(base, sizeof base, out + 3, cap - 3);
    if (!blen)
        return 0;
    out[1] = (uint8_t)(blen >> 8);
    out[2] = (uint8_t)blen;
    slen = z_deflate(sub, sizeof sub, out + 3 + blen, cap - 3 - blen);
    return slen ? 3 + blen + slen : 0;
}

static int cdzl_decompress(const uint8_t *in, size_t n, uint8_t *hunk)
{
    uint8_t base[HUNK_FRAMES * SECTOR], sub[HUNK_FRAMES * 96];
    size_t blen, got;
    if (n < 4 || (blen = (size_t)in[1] << 8 | in[2]) > n - 3)
        return -1;
    if (z_inflate_mem(in + 3, blen, base, sizeof base, &got, NULL) || got != sizeof base)
        return -1;
    if (z_inflate_mem(in + 3 + blen, n - 3 - blen, sub, sizeof sub, &got, NULL) || got != sizeof sub)
        return -1;
    for (uint32_t f = 0; f < HUNK_FRAMES; f++)
        if (in[0] & (1 << f)) {
            memcpy(base + f * SECTOR, cd_sync, 12);
            cd_ecc_generate(base + f * SECTOR);
        }
    join_hunk(hunk, base, sub);
    return 0;
}

/* cdfl: the sectors as FLAC frames (samples big-endian in the hunk), the
   subcode deflated */
static size_t cdfl_compress(const uint8_t *hunk, uint8_t *out, size_t cap)
{
    uint8_t base[HUNK_FRAMES * SECTOR], sub[HUNK_FRAMES * 96];
    static int16_t pcm[HUNK_FRAMES * SECTOR / 2];
    size_t flen, slen;
    split_hunk(hunk, base, sub);
    for (size_t i = 0; i < sizeof base / 2; i++)
        pcm[i] = (int16_t)(base[2 * i] << 8 | base[2 * i + 1]);
    flen = flac_encode(pcm, sizeof base / 4, FLAC_BLOCK, out, cap);
    if (!flen)
        return 0;
    slen = z_deflate(sub, sizeof sub, out + flen, cap - flen);
    return slen ? flen + slen : 0;
}

static int cdfl_decompress(const uint8_t *in, size_t n, uint8_t *hunk)
{
    uint8_t base[HUNK_FRAMES * SECTOR], sub[HUNK_FRAMES * 96];
    static int16_t pcm[HUNK_FRAMES * SECTOR / 2];
    size_t flen = flac_decode(in, n, pcm, sizeof base / 4), got;
    if (!flen)
        return -1;
    if (z_inflate_mem(in + flen, n - flen, sub, sizeof sub, &got, NULL) || got != sizeof sub)
        return -1;
    for (size_t i = 0; i < sizeof base / 2; i++) {
        base[2 * i] = (uint8_t)((uint16_t)pcm[i] >> 8);
        base[2 * i + 1] = (uint8_t)pcm[i];
    }
    join_hunk(hunk, base, sub);
    return 0;
}

/* ======================================================================== */
/* the disc as the CHD holds it                                             */
/* ======================================================================== */

typedef struct Seg {
    uint32_t lba, frames;       /* disc sectors lba .. lba+frames-1 ...          */
    uint32_t phys;              /* ... are the CHD's frames phys ..              */
    uint8_t  audio;
    uint8_t  cooked;            /* "MODE1": 2048 bytes of user data per frame    */
} Seg;

struct Chd {
    FILE    *f;
    uint32_t hunkcount;
    uint64_t logical;
    uint32_t comp[4];
    uint8_t *map;               /* 12 bytes per hunk: type, length 24, offset 48, crc 16 */
    uint8_t  rawsha1[20], sha1[20];
    uint64_t metaoffset;
    Seg      seg[CDROM_MAX_TRACKS];
    CdromTrack track[CDROM_MAX_TRACKS];
    int      ntracks;
    uint32_t leadout;
    /* a few decoded hunks */
    struct { uint32_t hunk; int valid; uint8_t data[HUNK]; } cache[8];
    uint8_t  cbuf[HUNK];
};

static const char *type_name(const CdromTrack *t, int mode)
{
    if (t->audio)
        return "AUDIO";
    if (t->sector_size == 2048)
        return "MODE1";
    return mode == 2 ? "MODE2_RAW" : "MODE1_RAW";
}

/* ======================================================================== */
/* writing                                                                  */
/* ======================================================================== */

typedef struct Writer {
    FILE    *f;
    uint64_t pos;
    uint8_t *map;
    uint8_t *digest;            /* SHA-1 of every hunk: finds repeated hunks */
    uint32_t hunkcount;
    int32_t *bucket;            /* hash table over digest */
    uint32_t nbucket;
} Writer;

static int wr(Writer *w, const void *p, size_t n)
{
    if (fwrite(p, 1, n, w->f) != n)
        return -1;
    w->pos += n;
    return 0;
}

static int write_map(Writer *w, uint64_t *mapoffset)
{
    uint8_t *rle = malloc((size_t)w->hunkcount + 8);
    size_t cap = (size_t)w->hunkcount * 8 + 64;
    uint8_t *out = malloc(cap);
    uint32_t histo[HSYMS] = {0}, max_self = 0, max_len = 0;
    uint8_t len[HSYMS];
    uint16_t code[HSYMS] = {0};
    size_t nrle = 0;
    int rc = -1;
    if (!rle || !out)
        goto done;
    /* run-length code the types */
    uint8_t last = 0;
    uint32_t count = 0;
    for (uint32_t h = 0; h < w->hunkcount; h++) {
        const uint8_t *m = w->map + 12 * h;
        uint8_t cur = m[0];
        if (cur == COMP_SELF) {
            if (get_be(m + 4, 6) > max_self)
                max_self = (uint32_t)get_be(m + 4, 6);
        } else if (get_be(m + 1, 3) > max_len) {
            max_len = (uint32_t)get_be(m + 1, 3);
        }
        if (cur == last)
            count++;
        if (cur != last || h == w->hunkcount - 1) {
            while (count) {
                if (count < 3) {
                    rle[nrle++] = last;
                    count--;
                } else if (count <= 3 + 15) {
                    rle[nrle++] = COMP_RLE_SMALL;
                    rle[nrle++] = (uint8_t)(count - 3);
                    count = 0;
                } else {
                    uint32_t c = count < 3 + 16 + 255 ? count : 3 + 16 + 255;
                    rle[nrle++] = COMP_RLE_LARGE;
                    rle[nrle++] = (uint8_t)((c - 3 - 16) >> 4);
                    rle[nrle++] = (uint8_t)((c - 3 - 16) & 15);
                    count -= c;
                }
            }
            if (cur != last)
                rle[nrle++] = last = cur;
        }
    }
    for (size_t i = 0; i < nrle; i++)
        histo[rle[i]]++;
    huff_build(histo, len);
    if (huff_assign(len, code))
        goto done;

    BitOut b = { out + 16, cap - 16, 0, 0, 0 };
    huff_export(&b, len);
    for (size_t i = 0; i < nrle; i++)
        bo_write(&b, code[rle[i]], len[rle[i]]);
    int lengthbits = 0, selfbits = 0;
    for (uint32_t v = max_len; v; v >>= 1)
        lengthbits++;
    for (uint32_t v = max_self; v; v >>= 1)
        selfbits++;
    uint64_t first = 0;
    const uint8_t *src = rle;
    last = 0;
    count = 0;
    for (uint32_t h = 0; h < w->hunkcount; h++) {
        const uint8_t *m = w->map + 12 * h;
        if (count == 0) {
            uint8_t v = *src++;
            if (v == COMP_RLE_SMALL) {
                count = 2 + *src++;
            } else if (v == COMP_RLE_LARGE) {
                count = 2 + 16 + ((uint32_t)src[0] << 4) + src[1];
                src += 2;
            } else {
                last = v;
            }
        } else {
            count--;
        }
        if (last <= COMP_TYPE_3) {
            bo_write(&b, (uint32_t)get_be(m + 1, 3), lengthbits);
            bo_write(&b, (uint32_t)get_be(m + 10, 2), 16);
        } else if (last == COMP_NONE) {
            bo_write(&b, (uint32_t)get_be(m + 10, 2), 16);
        } else if (last == COMP_SELF) {
            bo_write(&b, (uint32_t)get_be(m + 4, 6), selfbits);
        }
        if (last <= COMP_NONE && first == 0)
            first = get_be(m + 4, 6);
    }
    size_t complen = bo_flush(&b);
    if (complen > cap - 16)
        goto done;
    put_be(out, complen, 4);
    put_be(out + 4, first, 6);
    put_be(out + 10, crc16(w->map, (size_t)w->hunkcount * 12), 2);
    out[12] = (uint8_t)lengthbits;
    out[13] = (uint8_t)selfbits;
    out[14] = 0;
    out[15] = 0;
    *mapoffset = w->pos;
    rc = wr(w, out, complen + 16);
done:
    free(rle);
    free(out);
    return rc;
}

/* An earlier hunk with the same contents, or -1; registers hunk h. */
static int32_t find_same(Writer *w, uint32_t h)
{
    const uint8_t *d = w->digest + 20 * (size_t)h;
    uint32_t i = (uint32_t)get_be(d, 4) % w->nbucket;
    for (;; i = (i + 1) % w->nbucket) {
        if (w->bucket[i] < 0) {
            w->bucket[i] = (int32_t)h;
            return -1;
        }
        if (memcmp(w->digest + 20 * (size_t)w->bucket[i], d, 20) == 0)
            return w->bucket[i];
    }
}

int chd_create_cd(const char *path, const CdromTrack *tracks, int ntracks, uint32_t leadout,
                  const CdromSource *src, ChdProgressFn progress, void *pctx,
                  ChdStats *stats, char *err, size_t errlen)
{
    Writer w;
    Seg seg[CDROM_MAX_TRACKS];
    char meta[CDROM_MAX_TRACKS][256];
    uint8_t header[124] = {0}, hunk[HUNK], raw[SECTOR], comp[HUNK], comp2[HUNK];
    uint8_t *metabuf = NULL;
    uint32_t phys = 0, unreadable = 0;
    Sha1 rawsha;
    int rc = -1;

    memset(&w, 0, sizeof w);
    if (ntracks < 1 || ntracks > CDROM_MAX_TRACKS || leadout == 0) {
        seterr(err, errlen, "bad disc layout", NULL);
        return -1;
    }
    /* the tracks as the CHD stores them: one after the other, each from where
       the previous one ends (a gap between two tracks goes to the first) */
    for (int i = 0; i < ntracks; i++) {
        uint32_t start = i ? tracks[i].start : 0;
        uint32_t end = i + 1 < ntracks ? tracks[i + 1].start : leadout;
        int mode = 1;
        if (end <= start) {
            seterr(err, errlen, "bad disc layout", NULL);
            return -1;
        }
        if (!tracks[i].audio && tracks[i].sector_size != 2048
            && src->read_sector(src->ctx, tracks[i].start, raw) == 0)
            mode = raw[15];
        seg[i].lba = start;
        seg[i].frames = end - start;
        seg[i].phys = phys;
        seg[i].audio = tracks[i].audio;
        seg[i].cooked = !tracks[i].audio && tracks[i].sector_size == 2048;
        snprintf(meta[i], sizeof meta[i],
                 "TRACK:%d TYPE:%s SUBTYPE:NONE FRAMES:%u PREGAP:0 PGTYPE:MODE1 PGSUB:RW POSTGAP:0",
                 tracks[i].number, type_name(&tracks[i], mode), (unsigned)seg[i].frames);
        phys += (seg[i].frames + PAD_FRAMES - 1) / PAD_FRAMES * PAD_FRAMES;
    }
    uint64_t logical = (uint64_t)phys * FRAME;
    w.hunkcount = (uint32_t)((logical + HUNK - 1) / HUNK);
    w.nbucket = w.hunkcount * 2 + 1;
    w.map = calloc(w.hunkcount, 12);
    w.digest = malloc((size_t)w.hunkcount * 20);
    w.bucket = malloc(sizeof *w.bucket * w.nbucket);
    w.f = fopen(path, "wb");
    if (!w.map || !w.digest || !w.bucket || !w.f) {
        seterr(err, errlen, "cannot create", path);
        goto done;
    }
    memset(w.bucket, 0xFF, sizeof *w.bucket * w.nbucket);
    if (wr(&w, header, sizeof header))
        goto io;

    sha1_init(&rawsha);
    int s = 0;                                  /* segment of the current frame */
    for (uint32_t h = 0; h < w.hunkcount; h++) {
        int any_audio = 0;
        memset(hunk, 0, sizeof hunk);
        for (uint32_t k = 0; k < HUNK_FRAMES; k++) {
            uint32_t pf = h * HUNK_FRAMES + k;
            uint8_t *dst = hunk + k * FRAME;
            while (s + 1 < ntracks && pf >= seg[s + 1].phys)
                s++;
            if (pf - seg[s].phys >= seg[s].frames)
                continue;                       /* padding: zeros */
            uint32_t lba = seg[s].lba + (pf - seg[s].phys);
            if (src->read_sector(src->ctx, lba, raw) != 0) {
                unreadable++;
                continue;
            }
            if (seg[s].audio) {                 /* big-endian samples */
                any_audio = 1;
                for (uint32_t i = 0; i < SECTOR; i += 2) {
                    dst[i] = raw[i + 1];
                    dst[i + 1] = raw[i];
                }
            } else if (seg[s].cooked) {
                memcpy(dst, raw + 16, 2048);
            } else {
                memcpy(dst, raw, SECTOR);
            }
        }
        uint64_t left = logical - (uint64_t)h * HUNK;
        sha1_add(&rawsha, hunk, left < HUNK ? (size_t)left : HUNK);
        Sha1 hs;
        sha1_init(&hs);
        sha1_add(&hs, hunk, HUNK);
        sha1_done(&hs, w.digest + 20 * (size_t)h);

        uint8_t *m = w.map + 12 * (size_t)h;
        int32_t same = find_same(&w, h);
        if (same >= 0) {
            m[0] = COMP_SELF;
            put_be(m + 4, (uint64_t)same, 6);
        } else {
            size_t n = cdzl_compress(hunk, comp, sizeof comp - 1);
            const uint8_t *best = comp;
            int type = COMP_TYPE_0;
            if (any_audio) {
                size_t n2 = cdfl_compress(hunk, comp2, sizeof comp2 - 1);
                if (n2 && (!n || n2 < n)) {
                    n = n2;
                    best = comp2;
                    type = COMP_TYPE_1;
                }
            }
            if (!n) {                           /* nothing gained: stored */
                n = HUNK;
                best = hunk;
                type = COMP_NONE;
            }
            m[0] = (uint8_t)type;
            put_be(m + 1, n, 3);
            put_be(m + 4, w.pos, 6);
            put_be(m + 10, crc16(hunk, HUNK), 2);
            if (wr(&w, best, n))
                goto io;
        }
        if (progress && (h % 16 == 0 || h == w.hunkcount - 1)) {
            uint32_t done = (h + 1) * HUNK_FRAMES;
            if (progress(pctx, done < leadout ? done : leadout, leadout)) {
                rc = 1;
                goto done;
            }
        }
    }

    /* metadata, hashed for the overall SHA-1 in the order of (tag, SHA-1) */
    uint64_t metaoffset = w.pos;
    metabuf = malloc((size_t)ntracks * 24);
    if (!metabuf)
        goto done;
    for (int i = 0; i < ntracks; i++) {
        uint8_t mh[16];
        size_t len = strlen(meta[i]) + 1;
        Sha1 ms;
        put_be(mh, TAG_CHT2, 4);
        mh[4] = 1;                              /* CHD_MDFLAGS_CHECKSUM */
        put_be(mh + 5, len, 3);
        put_be(mh + 8, i + 1 < ntracks ? w.pos + 16 + len : 0, 8);
        if (wr(&w, mh, 16) || wr(&w, meta[i], len))
            goto io;
        put_be(metabuf + 24 * i, TAG_CHT2, 4);
        sha1_init(&ms);
        sha1_add(&ms, meta[i], len);
        sha1_done(&ms, metabuf + 24 * i + 4);
    }
    for (int i = 1; i < ntracks; i++)           /* insertion sort */
        for (int j = i; j > 0 && memcmp(metabuf + 24 * (j - 1), metabuf + 24 * j, 24) > 0; j--) {
            uint8_t t[24];
            memcpy(t, metabuf + 24 * j, 24);
            memcpy(metabuf + 24 * j, metabuf + 24 * (j - 1), 24);
            memcpy(metabuf + 24 * (j - 1), t, 24);
        }
    uint64_t mapoffset = 0;
    if (write_map(&w, &mapoffset))
        goto io;

    memcpy(header, "MComprHD", 8);
    put_be(header + 8, 124, 4);
    put_be(header + 12, 5, 4);
    put_be(header + 16, TAG_CDZL, 4);
    put_be(header + 20, TAG_CDFL, 4);
    put_be(header + 32, logical, 8);
    put_be(header + 40, mapoffset, 8);
    put_be(header + 48, metaoffset, 8);
    put_be(header + 56, HUNK, 4);
    put_be(header + 60, FRAME, 4);
    sha1_done(&rawsha, header + 64);
    Sha1 all;
    sha1_init(&all);
    sha1_add(&all, header + 64, 20);
    sha1_add(&all, metabuf, (size_t)ntracks * 24);
    sha1_done(&all, header + 84);
    if (seek64(w.f, 0) || fwrite(header, 1, sizeof header, w.f) != sizeof header || fflush(w.f))
        goto io;
    if (stats) {
        stats->sectors = leadout;
        stats->unreadable = unreadable;
        stats->bytes = w.pos;
        memcpy(stats->sha1, header + 84, 20);
    }
    rc = 0;
    goto done;
io:
    seterr(err, errlen, "cannot write", path);
done:
    if (w.f)
        fclose(w.f);
    if (rc != 0)
        remove(path);
    free(w.map);
    free(w.digest);
    free(w.bucket);
    free(metabuf);
    return rc;
}

/* ======================================================================== */
/* reading                                                                  */
/* ======================================================================== */

static int rd(FILE *f, uint64_t off, void *buf, size_t n)
{
    return seek64(f, off) == 0 && fread(buf, 1, n, f) == n ? 0 : -1;
}

static int read_map(Chd *c, uint64_t mapoffset)
{
    uint8_t mh[16], len[HSYMS] = {0};
    uint16_t code[HSYMS] = {0};
    uint8_t *comp = NULL;
    int rc = -1;
    if (c->comp[0] == 0) {                      /* uncompressed CHD: one block index per hunk */
        uint8_t e[4];
        for (uint32_t h = 0; h < c->hunkcount; h++) {
            uint8_t *m = c->map + 12 * (size_t)h;
            if (rd(c->f, mapoffset + 4 * (uint64_t)h, e, 4))
                return -1;
            m[0] = COMP_NONE;
            put_be(m + 1, HUNK, 3);
            put_be(m + 4, get_be(e, 4) * HUNK, 6);
        }
        return 0;
    }
    if (rd(c->f, mapoffset, mh, 16))
        return -1;
    uint32_t mapbytes = (uint32_t)get_be(mh, 4);
    uint64_t cur = get_be(mh + 4, 6);
    int lengthbits = mh[12], selfbits = mh[13], parentbits = mh[14];
    if (mapbytes > (uint64_t)c->hunkcount * 16 + 1024 || lengthbits > 24 || selfbits > 32
        || parentbits > 32 || !(comp = malloc((size_t)mapbytes + 1))
        || rd(c->f, mapoffset + 16, comp, mapbytes))
        goto done;
    BitIn b = { comp, mapbytes, 0, 0, 0, 0 };
    if (huff_import(&b, len, code))
        goto done;
    uint8_t last = 0;
    int rep = 0;
    for (uint32_t h = 0; h < c->hunkcount; h++) {
        uint8_t *m = c->map + 12 * (size_t)h;
        if (rep > 0) {
            m[0] = last;
            rep--;
            continue;
        }
        int v = huff_decode(&b, len, code);
        if (v < 0)
            goto done;
        if (v == COMP_RLE_SMALL) {
            int n = huff_decode(&b, len, code);
            if (n < 0)
                goto done;
            m[0] = last;
            rep = 2 + n;
        } else if (v == COMP_RLE_LARGE) {
            int hi = huff_decode(&b, len, code), lo = huff_decode(&b, len, code);
            if (hi < 0 || lo < 0)
                goto done;
            m[0] = last;
            rep = 2 + 16 + (hi << 4) + lo;
        } else {
            m[0] = last = (uint8_t)v;
        }
    }
    uint32_t last_self = 0;
    for (uint32_t h = 0; h < c->hunkcount; h++) {
        uint8_t *m = c->map + 12 * (size_t)h;
        uint64_t offset = cur;
        uint32_t length = 0, crc = 0;
        switch (m[0]) {
        case COMP_TYPE_0: case COMP_TYPE_1: case COMP_TYPE_2: case COMP_TYPE_3:
            length = bi_read(&b, lengthbits);
            cur += length;
            crc = bi_read(&b, 16);
            break;
        case COMP_NONE:
            length = HUNK;
            cur += length;
            crc = bi_read(&b, 16);
            break;
        case COMP_SELF:
            last_self = bi_read(&b, selfbits);
            offset = last_self;
            break;
        case COMP_SELF_1:
            last_self++;
            /* fall through */
        case COMP_SELF_0:
            m[0] = COMP_SELF;
            offset = last_self;
            break;
        default:                                /* a parent CHD: not for a game disc */
            goto done;
        }
        put_be(m + 1, length, 3);
        put_be(m + 4, offset, 6);
        put_be(m + 10, crc, 2);
    }
    if (b.pos * 8 - (size_t)b.n > b.len * 8      /* read past the end of the map */
        || crc16(c->map, (size_t)c->hunkcount * 12) != get_be(mh + 10, 2))
        goto done;
    rc = 0;
done:
    free(comp);
    return rc;
}

static int read_metadata(Chd *c, char *err, size_t errlen)
{
    uint64_t off = c->metaoffset;
    uint32_t phys = 0, lba = 0;
    c->ntracks = 0;
    for (int guard = 0; off && guard < 1000; guard++) {
        uint8_t mh[16];
        char text[256] = {0};
        if (rd(c->f, off, mh, 16))
            break;
        uint32_t tag = (uint32_t)get_be(mh, 4), len = (uint32_t)get_be(mh + 5, 3);
        uint64_t next = get_be(mh + 8, 8);
        if ((tag == TAG_CHT2 || tag == TAG_CHTR) && len < sizeof text && c->ntracks < CDROM_MAX_TRACKS
            && rd(c->f, off + 16, text, len) == 0) {
            int number = 0, frames = 0, pregap = 0, postgap = 0;
            char type[32] = "", subtype[32] = "", pgtype[32] = "", pgsub[32] = "";
            int n = sscanf(text, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d",
                           &number, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap);
            if (n < 4 || frames <= 0 || number < 1 || number > 99) {
                seterr(err, errlen, "bad track metadata", text);
                return -1;
            }
            Seg *s = &c->seg[c->ntracks];
            CdromTrack *t = &c->track[c->ntracks];
            int stored = pgtype[0] == 'V';      /* the pregap's sectors are in the file */
            memset(t, 0, sizeof *t);
            s->audio = strcmp(type, "AUDIO") == 0;
            s->cooked = strcmp(type, "MODE1") == 0 || strcmp(type, "MODE2_FORM1") == 0;
            if (!s->audio && !s->cooked && strcmp(type, "MODE1_RAW") != 0 && strcmp(type, "MODE2_RAW") != 0) {
                seterr(err, errlen, "unsupported track type", type);
                return -1;
            }
            if (!stored)
                lba += (uint32_t)pregap;
            s->lba = lba;
            s->frames = (uint32_t)frames;
            s->phys = phys;
            t->number = (uint8_t)number;
            t->audio = s->audio;
            t->start = lba + (stored ? (uint32_t)pregap : 0);
            t->sector_size = s->cooked ? 2048 : 2352;
            lba += (uint32_t)frames + (uint32_t)postgap;
            phys += ((uint32_t)frames + PAD_FRAMES - 1) / PAD_FRAMES * PAD_FRAMES;
            c->ntracks++;
        }
        off = next;
    }
    if (!c->ntracks) {
        seterr(err, errlen, "not a CD image (no track metadata)", NULL);
        return -1;
    }
    c->leadout = lba;
    for (int i = 0; i < c->ntracks; i++)
        c->track[i].sectors = (i + 1 < c->ntracks ? c->track[i + 1].start : c->leadout) - c->track[i].start;
    return 0;
}

Chd *chd_open(const char *path, char *err, size_t errlen)
{
    uint8_t h[124];
    Chd *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->f = fopen(path, "rb");
    if (!c->f) {
        seterr(err, errlen, "cannot open", path);
        goto fail;
    }
    if (fread(h, 1, sizeof h, c->f) != sizeof h || memcmp(h, "MComprHD", 8) != 0) {
        seterr(err, errlen, "not a CHD file", path);
        goto fail;
    }
    if (get_be(h + 8, 4) != 124 || get_be(h + 12, 4) != 5) {
        seterr(err, errlen, "only CHD version 5 is supported", path);
        goto fail;
    }
    for (int i = 0; i < 4; i++) {
        c->comp[i] = (uint32_t)get_be(h + 16 + 4 * i, 4);
        if (c->comp[i] && c->comp[i] != TAG_CDZL && c->comp[i] != TAG_CDFL) {
            char tag[80];
            snprintf(tag, sizeof tag, "%c%c%c%c (this program reads cdzl and cdfl: "
                     "chdman createcd -c cdzl,cdfl)", h[16 + 4 * i], h[17 + 4 * i], h[18 + 4 * i], h[19 + 4 * i]);
            seterr(err, errlen, "CHD compression not supported", tag);
            goto fail;
        }
    }
    c->logical = get_be(h + 32, 8);
    c->metaoffset = get_be(h + 48, 8);
    for (int i = 0; i < 20; i++)
        if (h[104 + i]) {
            seterr(err, errlen, "CHD with a parent is not supported", path);
            goto fail;
        }
    if (get_be(h + 56, 4) != HUNK || get_be(h + 60, 4) != FRAME || c->logical == 0
        || c->logical > (uint64_t)450000 * FRAME) {
        seterr(err, errlen, "not a CD image (hunk or unit size)", path);
        goto fail;
    }
    memcpy(c->rawsha1, h + 64, 20);
    memcpy(c->sha1, h + 84, 20);
    c->hunkcount = (uint32_t)((c->logical + HUNK - 1) / HUNK);
    c->map = calloc(c->hunkcount, 12);
    if (!c->map || read_map(c, get_be(h + 40, 8))) {
        seterr(err, errlen, "damaged CHD (hunk map)", path);
        goto fail;
    }
    if (read_metadata(c, err, errlen))
        goto fail;
    return c;
fail:
    chd_close(c);
    return NULL;
}

void chd_close(Chd *c)
{
    if (!c)
        return;
    if (c->f)
        fclose(c->f);
    free(c->map);
    free(c);
}

int chd_tracks(const Chd *c, CdromTrack *tracks, uint32_t *leadout)
{
    memcpy(tracks, c->track, sizeof c->track[0] * (size_t)c->ntracks);
    *leadout = c->leadout;
    return c->ntracks;
}

const uint8_t *chd_sha1(const Chd *c) { return c->sha1; }

static int read_hunk(Chd *c, uint32_t h, uint8_t *out, int depth)
{
    const uint8_t *m;
    if (h >= c->hunkcount || depth > 4)
        return -1;
    m = c->map + 12 * (size_t)h;
    uint32_t len = (uint32_t)get_be(m + 1, 3);
    uint64_t off = get_be(m + 4, 6);
    if (m[0] == COMP_SELF)
        return read_hunk(c, (uint32_t)off, out, depth + 1);
    if (m[0] == COMP_NONE) {
        if (off == 0 && c->comp[0] == 0) {      /* never written */
            memset(out, 0, HUNK);
            return 0;
        }
        return rd(c->f, off, out, HUNK);
    }
    if (m[0] > COMP_TYPE_3 || len > HUNK || rd(c->f, off, c->cbuf, len))
        return -1;
    int rc = -1;
    if (c->comp[m[0]] == TAG_CDZL)
        rc = cdzl_decompress(c->cbuf, len, out);
    else if (c->comp[m[0]] == TAG_CDFL)
        rc = cdfl_decompress(c->cbuf, len, out);
    if (rc == 0 && crc16(out, HUNK) != get_be(m + 10, 2))
        rc = -1;
    return rc;
}

static const uint8_t *cached_hunk(Chd *c, uint32_t h)
{
    unsigned slot = h % (sizeof c->cache / sizeof c->cache[0]);
    if (!c->cache[slot].valid || c->cache[slot].hunk != h) {
        c->cache[slot].valid = 0;
        if (read_hunk(c, h, c->cache[slot].data, 0))
            return NULL;
        c->cache[slot].hunk = h;
        c->cache[slot].valid = 1;
    }
    return c->cache[slot].data;
}

int chd_read_sector(Chd *c, uint32_t lba, uint8_t *raw)
{
    for (int i = 0; i < c->ntracks; i++) {
        const Seg *s = &c->seg[i];
        if (lba < s->lba || lba - s->lba >= s->frames)
            continue;
        uint32_t pf = s->phys + (lba - s->lba);
        const uint8_t *hunk = cached_hunk(c, pf / HUNK_FRAMES);
        if (!hunk)
            return -1;
        const uint8_t *src = hunk + (pf % HUNK_FRAMES) * FRAME;
        if (s->audio) {
            for (uint32_t k = 0; k < SECTOR; k += 2) {
                raw[k] = src[k + 1];
                raw[k + 1] = src[k];
            }
        } else if (s->cooked) {
            memset(raw, 0, SECTOR);
            raw[15] = 1;
            memcpy(raw + 16, src, 2048);
        } else {
            memcpy(raw, src, SECTOR);
        }
        return 0;
    }
    return -1;                                  /* a gap the file does not hold */
}

int chd_verify(Chd *c, ChdProgressFn progress, void *pctx, char *err, size_t errlen)
{
    uint8_t *hunk = malloc(HUNK), digest[20], metabuf[CDROM_MAX_TRACKS][24];
    Sha1 raw, all;
    int nmeta = 0, rc = -1;
    if (!hunk)
        return -1;
    sha1_init(&raw);
    for (uint32_t h = 0; h < c->hunkcount; h++) {
        uint64_t left = c->logical - (uint64_t)h * HUNK;
        if (read_hunk(c, h, hunk, 0)) {
            char n[32];
            snprintf(n, sizeof n, "%u", (unsigned)h);
            seterr(err, errlen, "damaged CHD: hunk", n);
            goto done;
        }
        sha1_add(&raw, hunk, left < HUNK ? (size_t)left : HUNK);
        if (progress && h % 64 == 0 && progress(pctx, h, c->hunkcount)) {
            rc = 1;
            goto done;
        }
    }
    sha1_done(&raw, digest);
    if (memcmp(digest, c->rawsha1, 20) != 0) {
        seterr(err, errlen, "damaged CHD: the data's SHA-1 differs from the header", NULL);
        goto done;
    }
    /* the overall SHA-1: the data's, then (tag, SHA-1) of the checksummed metadata, sorted */
    for (uint64_t off = c->metaoffset; off && nmeta < CDROM_MAX_TRACKS;) {
        uint8_t mh[16], text[512];
        Sha1 ms;
        if (rd(c->f, off, mh, 16))
            break;
        uint32_t len = (uint32_t)get_be(mh + 5, 3);
        if ((mh[4] & 1) && len <= sizeof text && rd(c->f, off + 16, text, len) == 0) {
            memcpy(metabuf[nmeta], mh, 4);
            sha1_init(&ms);
            sha1_add(&ms, text, len);
            sha1_done(&ms, metabuf[nmeta] + 4);
            nmeta++;
        }
        off = get_be(mh + 8, 8);
    }
    for (int i = 1; i < nmeta; i++)
        for (int j = i; j > 0 && memcmp(metabuf[j - 1], metabuf[j], 24) > 0; j--) {
            uint8_t t[24];
            memcpy(t, metabuf[j], 24);
            memcpy(metabuf[j], metabuf[j - 1], 24);
            memcpy(metabuf[j - 1], t, 24);
        }
    sha1_init(&all);
    sha1_add(&all, digest, 20);
    sha1_add(&all, metabuf, (size_t)nmeta * 24);
    sha1_done(&all, digest);
    if (memcmp(digest, c->sha1, 20) != 0) {
        seterr(err, errlen, "damaged CHD: the overall SHA-1 differs from the header", NULL);
        goto done;
    }
    rc = 0;
done:
    free(hunk);
    return rc;
}

/* ---- the virtual drive's sector source ------------------------------------------- */

static int src_read(void *ctx, uint32_t lba, uint8_t *raw) { return chd_read_sector(ctx, lba, raw); }
static void src_close(void *ctx) { chd_close(ctx); }

int chd_mount(const char *path, char *err, size_t errlen)
{
    CdromTrack tracks[CDROM_MAX_TRACKS];
    uint32_t leadout;
    Chd *c = chd_open(path, err, errlen);
    if (!c)
        return -1;
    int n = chd_tracks(c, tracks, &leadout);
    CdromSource src = { src_read, src_close, c };
    if (cdrom_open_source(tracks, n, leadout, &src) != 0) {
        seterr(err, errlen, cdrom_error(), NULL);
        return -1;
    }
    return 0;
}
