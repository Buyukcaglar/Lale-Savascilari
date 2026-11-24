/*
 * flac.c - see flac.h.  Reference: RFC 9639 (the FLAC format), sections 9
 * (frame structure) and 9.2 (subframes).
 * Verified: tests/test_codecs.c (round trips; a frame with every subframe
 * type); the CHD the port writes is read back whole (chd_verify).
 */
#include "flac.h"

#include <stdlib.h>
#include <string.h>

static uint8_t  crc8_tab[256];
static uint16_t crc16_tab[256];

static void crc_init(void)
{
    if (crc8_tab[1])
        return;
    for (int i = 0; i < 256; i++) {
        uint8_t c = (uint8_t)i;
        uint16_t d = (uint16_t)(i << 8);
        for (int k = 0; k < 8; k++) {
            c = (uint8_t)((c << 1) ^ (c & 0x80 ? 0x07 : 0));
            d = (uint16_t)((d << 1) ^ (d & 0x8000 ? 0x8005 : 0));
        }
        crc8_tab[i] = c;
        crc16_tab[i] = d;
    }
}

static uint8_t crc8(const uint8_t *p, size_t n)
{
    uint8_t c = 0;
    while (n--)
        c = crc8_tab[c ^ *p++];
    return c;
}

static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0;
    while (n--)
        c = (uint16_t)((c << 8) ^ crc16_tab[(c >> 8) ^ *p++]);
    return c;
}

/* ======================================================================== */
/* encoder                                                                  */
/* ======================================================================== */

typedef struct BitW {
    uint8_t *out;
    size_t   cap, len;
    uint64_t acc;
    int      n;
    int      full;
} BitW;

static void bw(BitW *b, uint32_t v, int bits)
{
    if (bits == 0)
        return;
    if (bits < 32)
        v &= (1u << bits) - 1;
    b->acc = b->acc << bits | v;
    b->n += bits;
    while (b->n >= 8) {
        b->n -= 8;
        if (b->len < b->cap)
            b->out[b->len++] = (uint8_t)(b->acc >> b->n);
        else
            b->full = 1;
    }
}

static void bw_unary(BitW *b, uint32_t q)
{
    while (q >= 24) {
        bw(b, 0, 24);
        q -= 24;
    }
    bw(b, 1, (int)q + 1);
}

static void bw_align(BitW *b)
{
    if (b->n)
        bw(b, 0, 8 - b->n);
}

#define MAX_BLOCK 4608
#define MAX_PORDER 8

typedef struct Sub {
    int      type;                      /* 0 constant, 1 verbatim, 2 fixed */
    int      order;
    int      porder;
    uint8_t  param[1 << MAX_PORDER];
    uint64_t bits;
} Sub;

static uint32_t zigzag(int32_t r) { return ((uint32_t)r << 1) ^ (uint32_t)(r >> 31); }

/* Bits of the Rice partition with parameter k. */
static uint64_t rice_bits(const int32_t *r, uint32_t n, int k)
{
    uint64_t bits = (uint64_t)n * (uint64_t)(k + 1);
    for (uint32_t i = 0; i < n; i++)
        bits += zigzag(r[i]) >> k;
    return bits;
}

static void fixed_residual(const int32_t *s, uint32_t n, int order, int32_t *r)
{
    for (uint32_t i = (uint32_t)order; i < n; i++) {
        switch (order) {
        case 0: r[i] = s[i]; break;
        case 1: r[i] = s[i] - s[i - 1]; break;
        case 2: r[i] = s[i] - 2 * s[i - 1] + s[i - 2]; break;
        case 3: r[i] = s[i] - 3 * s[i - 1] + 3 * s[i - 2] - s[i - 3]; break;
        default: r[i] = s[i] - 4 * s[i - 1] + 6 * s[i - 2] - 4 * s[i - 3] + s[i - 4]; break;
        }
    }
}

/* The cheapest way to code one channel of a block; res gets the residual of
   the chosen fixed predictor. */
static void plan_subframe(const int32_t *s, uint32_t n, int bps, int32_t *res, Sub *sub)
{
    uint32_t i;
    for (i = 1; i < n && s[i] == s[0]; i++)
        ;
    if (i == n) {
        sub->type = 0;
        sub->bits = 8 + (uint64_t)bps;
        return;
    }
    /* the fixed predictor with the smallest residual */
    int best_order = 0;
    uint64_t best_sum = UINT64_MAX;
    int maxorder = n > 4 ? 4 : (int)n - 1;
    for (int order = 0; order <= maxorder; order++) {
        uint64_t sum = 0;
        fixed_residual(s, n, order, res);
        for (i = (uint32_t)order; i < n; i++)
            sum += (uint64_t)(res[i] < 0 ? -(int64_t)res[i] : res[i]);
        if (sum < best_sum) {
            best_sum = sum;
            best_order = order;
        }
    }
    fixed_residual(s, n, best_order, res);
    /* the best partition order and Rice parameters for it */
    uint64_t best_bits = UINT64_MAX;
    sub->porder = 0;
    for (int po = 0; po <= MAX_PORDER; po++) {
        uint32_t parts = 1u << po, plen = n >> po;
        uint8_t param[1 << MAX_PORDER];
        uint64_t bits = 0;
        if (n % parts || plen <= (uint32_t)best_order)
            break;
        for (uint32_t p = 0; p < parts; p++) {
            uint32_t start = p * plen, cnt = plen;
            uint64_t sum = 0, b0, b1;
            int k = 0;
            if (p == 0) {
                start = (uint32_t)best_order;
                cnt -= (uint32_t)best_order;
            }
            for (i = 0; i < cnt; i++)
                sum += zigzag(res[start + i]);
            while (k < 14 && ((uint64_t)cnt << (k + 1)) <= sum)
                k++;
            b0 = rice_bits(res + start, cnt, k);
            if (k < 14 && (b1 = rice_bits(res + start, cnt, k + 1)) < b0) {
                b0 = b1;
                k++;
            } else if (k > 0 && (b1 = rice_bits(res + start, cnt, k - 1)) < b0) {
                b0 = b1;
                k--;
            }
            param[p] = (uint8_t)k;
            bits += 4 + b0;
        }
        if (bits < best_bits) {
            best_bits = bits;
            sub->porder = po;
            memcpy(sub->param, param, parts);
        }
    }
    sub->type = 2;
    sub->order = best_order;
    sub->bits = 8 + (uint64_t)best_order * (uint64_t)bps + 6 + best_bits;
    if (sub->bits >= 8 + (uint64_t)n * (uint64_t)bps) {
        sub->type = 1;
        sub->bits = 8 + (uint64_t)n * (uint64_t)bps;
    }
}

static void write_subframe(BitW *b, const int32_t *s, uint32_t n, int bps, const int32_t *res, const Sub *sub)
{
    if (sub->type == 0) {
        bw(b, 0x00, 8);
        bw(b, (uint32_t)s[0], bps);
    } else if (sub->type == 1) {
        bw(b, 0x02, 8);
        for (uint32_t i = 0; i < n; i++)
            bw(b, (uint32_t)s[i], bps);
    } else {
        uint32_t parts = 1u << sub->porder, plen = n >> sub->porder;
        bw(b, (uint32_t)(0x10 | (8 + sub->order) << 1) & 0x7E, 8);
        for (int i = 0; i < sub->order; i++)
            bw(b, (uint32_t)s[i], bps);
        bw(b, 0, 2);                            /* Rice method 0: 4-bit parameters */
        bw(b, (uint32_t)sub->porder, 4);
        for (uint32_t p = 0; p < parts; p++) {
            uint32_t start = p ? p * plen : (uint32_t)sub->order;
            uint32_t end = (p + 1) * plen;
            int k = sub->param[p];
            bw(b, (uint32_t)k, 4);
            for (uint32_t i = start; i < end; i++) {
                uint32_t u = zigzag(res[i]);
                bw_unary(b, u >> k);
                bw(b, u, k);
            }
        }
    }
}

size_t flac_encode(const int16_t *pcm, uint32_t nsamples, uint32_t blocksize, uint8_t *out, size_t cap)
{
    BitW b = { out, cap, 0, 0, 0, 0 };
    int32_t *ch = malloc(sizeof *ch * 8 * MAX_BLOCK);
    uint32_t frame = 0;
    if (!ch || blocksize < 16 || blocksize > MAX_BLOCK) {
        free(ch);
        return 0;
    }
    crc_init();
    int32_t *L = ch, *R = L + MAX_BLOCK, *M = R + MAX_BLOCK, *S = M + MAX_BLOCK;
    int32_t *res[4] = { S + MAX_BLOCK, S + 2 * MAX_BLOCK, S + 3 * MAX_BLOCK, S + 4 * MAX_BLOCK };
    for (uint32_t done = 0; done < nsamples; done += blocksize, frame++) {
        uint32_t n = nsamples - done < blocksize ? nsamples - done : blocksize;
        size_t start = b.len;
        Sub sub[4];
        for (uint32_t i = 0; i < n; i++) {
            L[i] = pcm[2 * (done + i)];
            R[i] = pcm[2 * (done + i) + 1];
            M[i] = (L[i] + R[i]) >> 1;
            S[i] = L[i] - R[i];
        }
        plan_subframe(L, n, 16, res[0], &sub[0]);
        plan_subframe(R, n, 16, res[1], &sub[1]);
        plan_subframe(M, n, 16, res[2], &sub[2]);
        plan_subframe(S, n, 17, res[3], &sub[3]);
        /* channel assignment: 1 = left right, 8 = left side, 9 = side right, 10 = mid side */
        static const int layout[4][3] = { { 1, 0, 1 }, { 8, 0, 3 }, { 9, 3, 1 }, { 10, 2, 3 } };
        int best = 0;
        for (int i = 1; i < 4; i++)
            if (sub[layout[i][1]].bits + sub[layout[i][2]].bits
                < sub[layout[best][1]].bits + sub[layout[best][2]].bits)
                best = i;
        /* frame header */
        bw(&b, 0xFFF8, 16);
        bw(&b, 0x7, 4);                         /* block size - 1 follows as 16 bits */
        bw(&b, 0x9, 4);                         /* 44.1 kHz */
        bw(&b, (uint32_t)layout[best][0], 4);
        bw(&b, 0x4, 3);                         /* 16 bits per sample */
        bw(&b, 0, 1);
        if (frame < 0x80) {
            bw(&b, frame, 8);
        } else if (frame < 0x800) {
            bw(&b, 0xC0 | frame >> 6, 8);
            bw(&b, 0x80 | (frame & 0x3F), 8);
        } else if (frame < 0x10000) {
            bw(&b, 0xE0 | frame >> 12, 8);
            bw(&b, 0x80 | (frame >> 6 & 0x3F), 8);
            bw(&b, 0x80 | (frame & 0x3F), 8);
        } else {
            free(ch);
            return 0;
        }
        bw(&b, n - 1, 16);
        if (b.full)
            break;
        bw(&b, crc8(out + start, b.len - start), 8);
        int c0 = layout[best][1], c1 = layout[best][2];
        const int32_t *chan[4] = { L, R, M, S };
        write_subframe(&b, chan[c0], n, c0 == 3 ? 17 : 16, res[c0], &sub[c0]);
        write_subframe(&b, chan[c1], n, c1 == 3 ? 17 : 16, res[c1], &sub[c1]);
        bw_align(&b);
        if (b.full)
            break;
        bw(&b, crc16(out + start, b.len - start), 16);
    }
    free(ch);
    return b.full ? 0 : b.len;
}

/* ======================================================================== */
/* decoder                                                                  */
/* ======================================================================== */

typedef struct BitR {
    const uint8_t *in;
    size_t         n, pos;
    uint64_t       acc;
    int            nbits;
    int            bad;
} BitR;

static uint32_t br(BitR *b, int bits)
{
    if (bits == 0)
        return 0;
    while (b->nbits < bits) {
        if (b->pos < b->n) {
            b->acc = b->acc << 8 | b->in[b->pos++];
        } else {
            b->acc <<= 8;
            b->bad = 1;
        }
        b->nbits += 8;
    }
    b->nbits -= bits;
    return (uint32_t)(b->acc >> b->nbits) & (bits < 32 ? (1u << bits) - 1 : 0xFFFFFFFFu);
}

static int32_t br_signed(BitR *b, int bits)
{
    uint32_t v = br(b, bits);
    if (bits < 32 && (v & (1u << (bits - 1))))
        v |= ~((1u << bits) - 1);
    return (int32_t)v;
}

static uint32_t br_unary(BitR *b)
{
    uint32_t q = 0;
    while (!b->bad && br(b, 1) == 0)
        q++;
    return q;
}

static int read_residual(BitR *b, int32_t *s, uint32_t n, int order)
{
    int method = (int)br(b, 2), porder = (int)br(b, 4);
    int pbits = method == 0 ? 4 : 5;
    uint32_t parts = 1u << porder, plen = n >> porder, i = (uint32_t)order;
    if (method > 1 || (porder && n % parts) || plen < (uint32_t)order)
        return -1;
    for (uint32_t p = 0; p < parts; p++) {
        uint32_t end = (p + 1) * plen;
        int k = (int)br(b, pbits);
        if (k == (1 << pbits) - 1) {            /* escape: raw samples */
            int raw = (int)br(b, 5);
            for (; i < end; i++)
                s[i] = raw ? br_signed(b, raw) : 0;
        } else {
            for (; i < end; i++) {
                uint32_t q = br_unary(b);
                uint32_t u = q << k | br(b, k);
                s[i] = (int32_t)(u >> 1) ^ -(int32_t)(u & 1);
            }
        }
        if (b->bad)
            return -1;
    }
    return 0;
}

static int read_subframe(BitR *b, int32_t *s, uint32_t n, int bps)
{
    int head = (int)br(b, 8), type = head >> 1 & 0x3F, wasted = 0;
    if (head & 0x80)
        return -1;
    if (head & 1) {
        wasted = (int)br_unary(b) + 1;
        bps -= wasted;
        if (bps < 1)
            return -1;
    }
    if (type == 0) {
        int32_t v = br_signed(b, bps);
        for (uint32_t i = 0; i < n; i++)
            s[i] = v;
    } else if (type == 1) {
        for (uint32_t i = 0; i < n; i++)
            s[i] = br_signed(b, bps);
    } else if (type >= 8 && type <= 12) {
        int order = type - 8;
        if ((uint32_t)order > n)
            return -1;
        for (int i = 0; i < order; i++)
            s[i] = br_signed(b, bps);
        if (read_residual(b, s, n, order))
            return -1;
        for (uint32_t i = (uint32_t)order; i < n; i++) {
            switch (order) {
            case 1: s[i] += s[i - 1]; break;
            case 2: s[i] += 2 * s[i - 1] - s[i - 2]; break;
            case 3: s[i] += 3 * s[i - 1] - 3 * s[i - 2] + s[i - 3]; break;
            case 4: s[i] += 4 * s[i - 1] - 6 * s[i - 2] + 4 * s[i - 3] - s[i - 4]; break;
            default: break;
            }
        }
    } else if (type >= 32) {
        int order = (type & 31) + 1, prec, shift;
        int32_t coef[32];
        if ((uint32_t)order > n)
            return -1;
        for (int i = 0; i < order; i++)
            s[i] = br_signed(b, bps);
        prec = (int)br(b, 4) + 1;
        shift = br_signed(b, 5);
        if (prec == 16 || shift < 0)
            return -1;
        for (int i = 0; i < order; i++)
            coef[i] = br_signed(b, prec);
        if (read_residual(b, s, n, order))
            return -1;
        for (uint32_t i = (uint32_t)order; i < n; i++) {
            int64_t pred = 0;
            for (int j = 0; j < order; j++)
                pred += (int64_t)coef[j] * s[i - 1 - (uint32_t)j];
            s[i] += (int32_t)(pred >> shift);
        }
    } else {
        return -1;
    }
    if (wasted)
        for (uint32_t i = 0; i < n; i++)
            s[i] = (int32_t)((uint32_t)s[i] << wasted);
    return b->bad ? -1 : 0;
}

size_t flac_decode(const uint8_t *in, size_t n, int16_t *pcm, uint32_t nsamples)
{
    static const uint16_t bs_tab[16] = { 0, 192, 576, 1152, 2304, 4608, 0, 0,
                                         256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };
    int32_t *c0 = malloc(sizeof *c0 * 2 * 65536), *c1;
    size_t pos = 0;
    uint32_t done = 0;
    if (!c0)
        return 0;
    c1 = c0 + 65536;
    crc_init();
    while (done < nsamples) {
        BitR b = { in + pos, n - pos, 0, 0, 0, 0 };
        uint32_t sync = br(&b, 16), bscode = br(&b, 4), srcode = br(&b, 4);
        uint32_t chan = br(&b, 4), sscode = br(&b, 3), lead, bsize;
        if ((sync & 0xFFFE) != 0xFFF8 || br(&b, 1) || (chan > 1 && chan < 8) || chan > 10
            || (sscode != 0 && sscode != 4) || srcode == 15)
            goto bad;
        lead = br(&b, 8);                       /* coded frame / sample number */
        for (uint32_t m = 0x80; (lead & m) && m > 1; m >>= 1)
            if (m != 0x80 && (br(&b, 8) & 0xC0) != 0x80)
                goto bad;
        if (bscode == 6)
            bsize = br(&b, 8) + 1;
        else if (bscode == 7)
            bsize = br(&b, 16) + 1;
        else
            bsize = bs_tab[bscode];
        if (srcode == 12)
            br(&b, 8);
        else if (srcode == 13 || srcode == 14)
            br(&b, 16);
        if (b.bad || bsize == 0 || bsize > nsamples - done)
            goto bad;
        size_t hlen = b.pos;
        if (br(&b, 8) != crc8(in + pos, hlen))
            goto bad;
        if (chan == 0) {                        /* mono: both channels the same */
            if (read_subframe(&b, c0, bsize, 16))
                goto bad;
            memcpy(c1, c0, sizeof *c0 * bsize);
        } else if (read_subframe(&b, c0, bsize, chan == 9 ? 17 : 16)
                   || read_subframe(&b, c1, bsize, chan == 8 || chan == 10 ? 17 : 16)) {
            goto bad;
        }
        b.nbits -= b.nbits & 7;                 /* padding to the byte boundary */
        size_t body = b.pos - (size_t)(b.nbits / 8);
        uint32_t crc = br(&b, 16);
        if (b.bad || crc != crc16(in + pos, body))
            goto bad;
        for (uint32_t i = 0; i < bsize; i++) {
            int32_t l = c0[i], r = c1[i];
            if (chan == 8) {
                r = l - r;
            } else if (chan == 9) {
                l = l + r;
            } else if (chan == 10) {
                int32_t mid = (int32_t)((uint32_t)l << 1) | (r & 1);
                l = (mid + r) >> 1;
                r = (mid - r) >> 1;
            }
            pcm[2 * (done + i)] = (int16_t)l;
            pcm[2 * (done + i) + 1] = (int16_t)r;
        }
        done += bsize;
        pos += body + 2;
    }
    free(c0);
    return pos;
bad:
    free(c0);
    return 0;
}
