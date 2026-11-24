/*
 * zdeflate.c - see zdeflate.h.  Reference: RFC 1951.
 *
 * Inflate: table decoding as in Sean Barrett's stb_image (9-bit direct table,
 * canonical search for longer codes).  Deflate: hash chains with lazy
 * matching, one block per call - dynamic, fixed or stored, whichever is
 * smallest - with length-limited Huffman codes (the count adjustment of
 * zlib's/miniz's "enforce max code size").
 * Verified: tests/test_codecs.c (round trips, hand-made streams) and
 * tools/chd.py verify (Python's zlib decodes what z_deflate wrote).
 */
#include "zdeflate.h"

#include <stdlib.h>
#include <string.h>

/* ======================================================================== */
/* CRC-32                                                                   */
/* ======================================================================== */

uint32_t z_crc32(uint32_t crc, const void *data, size_t n)
{
    static uint32_t table[256];
    const uint8_t *p = data;
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c >> 1) ^ (c & 1 ? 0xEDB88320u : 0);
            table[i] = c;
        }
    }
    crc = ~crc;
    while (n--)
        crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* ======================================================================== */
/* inflate                                                                  */
/* ======================================================================== */

#define FAST_BITS 9

typedef struct ZHuff {
    uint16_t fast[1 << FAST_BITS];      /* length << 9 | symbol, 0 = longer code */
    uint16_t firstcode[17], firstsymbol[17];
    int32_t  maxcode[18];
    uint8_t  size[288];
    uint16_t value[288];
} ZHuff;

static int bit_reverse(int v, int bits)
{
    int r = 0;
    while (bits--) {
        r = r << 1 | (v & 1);
        v >>= 1;
    }
    return r;
}

static int huff_build(ZHuff *z, const uint8_t *sizes, int num)
{
    int sizes_n[17] = {0}, next_code[17], code = 0, k = 0;
    memset(z->fast, 0, sizeof z->fast);
    for (int i = 0; i < num; i++)
        sizes_n[sizes[i]]++;
    sizes_n[0] = 0;
    for (int i = 1; i < 16; i++) {
        next_code[i] = code;
        z->firstcode[i] = (uint16_t)code;
        z->firstsymbol[i] = (uint16_t)k;
        code += sizes_n[i];
        if (sizes_n[i] && code - 1 >= (1 << i))
            return -1;                          /* over-subscribed */
        z->maxcode[i] = code << (16 - i);
        code <<= 1;
        k += sizes_n[i];
    }
    z->maxcode[16] = 0x10000;
    for (int i = 0; i < num; i++) {
        int s = sizes[i];
        if (!s)
            continue;
        int c = next_code[s] - z->firstcode[s] + z->firstsymbol[s];
        z->size[c] = (uint8_t)s;
        z->value[c] = (uint16_t)i;
        if (s <= FAST_BITS) {
            for (int j = bit_reverse(next_code[s], s); j < (1 << FAST_BITS); j += 1 << s)
                z->fast[j] = (uint16_t)(s << 9 | i);
        }
        next_code[s]++;
    }
    return 0;
}

typedef struct ZIn {
    ZReadFn  rd;
    void    *rctx;
    ZWriteFn wr;
    void    *wctx;
    uint8_t  in[16384];
    size_t   in_pos, in_len;
    uint64_t in_total;                  /* bytes handed out of in[] */
    int      eof;
    int      over;                      /* bits read past the end of input */
    uint32_t bits;
    int      nbits;
    uint8_t  win[32768];
    uint32_t wpos;                      /* bytes written in all, modulo 2^32 */
} ZIn;

static int in_byte(ZIn *z)
{
    if (z->in_pos == z->in_len) {
        if (!z->eof) {
            z->in_len = z->rd(z->rctx, z->in, sizeof z->in);
            z->in_pos = 0;
            if (z->in_len == 0)
                z->eof = 1;
        }
        if (z->eof) {
            z->over += 8;               /* zero bits; an error once they are used */
            return 0;
        }
    }
    z->in_total++;
    return z->in[z->in_pos++];
}

static void fill(ZIn *z)
{
    while (z->nbits <= 24) {
        z->bits |= (uint32_t)in_byte(z) << z->nbits;
        z->nbits += 8;
    }
}

static uint32_t getbits(ZIn *z, int n)
{
    uint32_t v;
    if (z->nbits < n)
        fill(z);
    v = z->bits & ((1u << n) - 1);
    z->bits >>= n;
    z->nbits -= n;
    return v;
}

static int decode(ZIn *z, const ZHuff *h)
{
    int b, s, k;
    if (z->nbits < 16)
        fill(z);
    b = h->fast[z->bits & ((1 << FAST_BITS) - 1)];
    if (b) {
        s = b >> 9;
        z->bits >>= s;
        z->nbits -= s;
        return b & 511;
    }
    k = bit_reverse((int)(z->bits & 0xFFFF), 16);
    for (s = FAST_BITS + 1; s < 16; s++)
        if (k < h->maxcode[s])
            break;
    if (s >= 16)
        return -1;
    b = (k >> (16 - s)) - h->firstcode[s] + h->firstsymbol[s];
    if (b < 0 || b >= 288 || h->size[b] != s)
        return -1;
    z->bits >>= s;
    z->nbits -= s;
    return h->value[b];
}

static int out_flush(ZIn *z, uint32_t n)
{
    return n ? z->wr(z->wctx, z->win, n) : 0;
}

static int out_byte(ZIn *z, uint8_t c)
{
    z->win[z->wpos++ & 32767] = c;
    if ((z->wpos & 32767) == 0)
        return out_flush(z, 32768);
    return 0;
}

static const uint16_t len_base[31] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0 };
static const uint8_t len_extra[31] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                       3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 0, 0 };
static const uint16_t dist_base[32] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
                                        513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385,
                                        24577, 0, 0 };
static const uint8_t dist_extra[32] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
                                        9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 0, 0 };
static const uint8_t cl_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

static int inflate_block(ZIn *z, const ZHuff *lit, const ZHuff *dst)
{
    for (;;) {
        int s = decode(z, lit);
        if (s < 0 || z->over > z->nbits)
            return -1;
        if (s < 256) {
            if (out_byte(z, (uint8_t)s))
                return -1;
            continue;
        }
        if (s == 256)
            return 0;
        s -= 257;
        if (s >= 29)
            return -1;
        uint32_t len = len_base[s] + getbits(z, len_extra[s]);
        int d = decode(z, dst);
        if (d < 0 || d >= 30)
            return -1;
        uint32_t dist = dist_base[d] + getbits(z, dist_extra[d]);
        if (dist > z->wpos && z->wpos < 32768)
            return -1;                          /* before the start of the output */
        while (len--)
            if (out_byte(z, z->win[(z->wpos - dist) & 32767]))
                return -1;
    }
}

int z_inflate(ZReadFn rd, void *rctx, ZWriteFn wr, void *wctx, uint64_t *in_used)
{
    ZIn *z = calloc(1, sizeof *z);
    ZHuff *lit = malloc(sizeof *lit), *dst = malloc(sizeof *dst);
    int rc = -1, final;
    if (!z || !lit || !dst)
        goto done;
    z->rd = rd; z->rctx = rctx; z->wr = wr; z->wctx = wctx;
    do {
        final = (int)getbits(z, 1);
        int type = (int)getbits(z, 2);
        if (type == 0) {                        /* stored */
            z->bits >>= z->nbits & 7;
            z->nbits -= z->nbits & 7;
            uint32_t len = getbits(z, 16), nlen = getbits(z, 16);
            if ((len ^ nlen) != 0xFFFF || z->over > z->nbits)
                goto done;
            while (len--) {
                int c = z->nbits ? (int)getbits(z, 8) : in_byte(z);
                if (z->over > z->nbits || out_byte(z, (uint8_t)c))
                    goto done;
            }
        } else if (type == 1) {                 /* fixed codes */
            uint8_t sizes[288 + 32];
            memset(sizes, 8, 144);
            memset(sizes + 144, 9, 112);
            memset(sizes + 256, 7, 24);
            memset(sizes + 280, 8, 8);
            memset(sizes + 288, 5, 32);
            if (huff_build(lit, sizes, 288) || huff_build(dst, sizes + 288, 32)
                || inflate_block(z, lit, dst))
                goto done;
        } else if (type == 2) {                 /* dynamic codes */
            uint8_t cl[19] = {0}, sizes[286 + 32 + 137];
            int hlit = (int)getbits(z, 5) + 257, hdist = (int)getbits(z, 5) + 1;
            int hclen = (int)getbits(z, 4) + 4, n = 0;
            for (int i = 0; i < hclen; i++)
                cl[cl_order[i]] = (uint8_t)getbits(z, 3);
            if (huff_build(lit, cl, 19))
                goto done;
            while (n < hlit + hdist) {
                int c = decode(z, lit), rep;
                uint8_t fillv = 0;
                if (c < 0 || c > 18 || z->over > z->nbits)
                    goto done;
                if (c < 16) {
                    sizes[n++] = (uint8_t)c;
                    continue;
                }
                if (c == 16) {
                    if (n == 0)
                        goto done;
                    rep = 3 + (int)getbits(z, 2);
                    fillv = sizes[n - 1];
                } else if (c == 17) {
                    rep = 3 + (int)getbits(z, 3);
                } else {
                    rep = 11 + (int)getbits(z, 7);
                }
                if (n + rep > hlit + hdist)
                    goto done;
                memset(sizes + n, fillv, (size_t)rep);
                n += rep;
            }
            if (hlit > 286 || huff_build(lit, sizes, hlit) || huff_build(dst, sizes + hlit, hdist)
                || inflate_block(z, lit, dst))
                goto done;
        } else {
            goto done;
        }
    } while (!final);
    if (z->over > z->nbits || out_flush(z, z->wpos & 32767))
        goto done;
    if (in_used)                                /* whole bytes still buffered were not used */
        *in_used = z->in_total - (uint64_t)((z->nbits - z->over) / 8);
    rc = 0;
done:
    free(z);
    free(lit);
    free(dst);
    return rc;
}

typedef struct Mem {
    const uint8_t *in;
    size_t         in_len;
    uint8_t       *out;
    size_t         out_cap, out_len;
} Mem;

static size_t mem_read(void *ctx, uint8_t *buf, size_t cap)
{
    Mem *m = ctx;
    size_t n = m->in_len < cap ? m->in_len : cap;
    memcpy(buf, m->in, n);
    m->in += n;
    m->in_len -= n;
    return n;
}

static int mem_write(void *ctx, const uint8_t *data, size_t n)
{
    Mem *m = ctx;
    if (n > m->out_cap - m->out_len)
        return -1;
    memcpy(m->out + m->out_len, data, n);
    m->out_len += n;
    return 0;
}

int z_inflate_mem(const uint8_t *in, size_t n, uint8_t *out, size_t outcap, size_t *outlen, size_t *in_used)
{
    Mem m = { in, n, out, outcap, 0 };
    uint64_t used = 0;
    if (z_inflate(mem_read, &m, mem_write, &m, &used) != 0)
        return -1;
    if (outlen)
        *outlen = m.out_len;
    if (in_used)
        *in_used = (size_t)used;
    return 0;
}

/* ======================================================================== */
/* deflate                                                                  */
/* ======================================================================== */

#define HASH_BITS 15
#define MAX_CHAIN 128
#define MAX_IN    65535

typedef struct BitOut {
    uint8_t *out;
    size_t   cap, len;
    uint32_t acc;
    int      n;
    int      full;
} BitOut;

static void put(BitOut *b, uint32_t v, int bits)
{
    b->acc |= v << b->n;
    b->n += bits;
    while (b->n >= 8) {
        if (b->len < b->cap)
            b->out[b->len++] = (uint8_t)b->acc;
        else
            b->full = 1;
        b->acc >>= 8;
        b->n -= 8;
    }
}

static void put_flush(BitOut *b)
{
    if (b->n)
        put(b, 0, 8 - b->n);
}

typedef struct Sym {
    uint32_t freq;
    uint16_t sym;
} Sym;

static int sym_cmp(const void *a, const void *b)
{
    const Sym *x = a, *y = b;
    if (x->freq != y->freq)
        return x->freq < y->freq ? -1 : 1;
    return (int)x->sym - (int)y->sym;
}

/* Code lengths (at most maxlen) of an optimal prefix code for freq[0..n-1];
   unused symbols get 0.  A lone symbol gets length 1. */
static void huff_lengths(const uint32_t *freq, int n, int maxlen, uint8_t *len)
{
    Sym leaf[288];
    uint32_t weight[2 * 288];
    uint16_t parent[2 * 288];
    int count[33] = {0};
    int used = 0;
    memset(len, 0, (size_t)n);
    for (int i = 0; i < n; i++)
        if (freq[i]) {
            leaf[used].freq = freq[i];
            leaf[used].sym = (uint16_t)i;
            used++;
        }
    if (used == 0)
        return;
    if (used == 1) {
        len[leaf[0].sym] = 1;
        return;
    }
    qsort(leaf, (size_t)used, sizeof leaf[0], sym_cmp);
    /* two queues: the sorted leaves and the internal nodes, made in rising order */
    for (int i = 0; i < used; i++)
        weight[i] = leaf[i].freq;
    int li = 0, ni = used, nn = used;           /* next leaf, next internal, node count */
    while (used - li + nn - ni > 1) {
        int pick[2];
        for (int k = 0; k < 2; k++) {
            if (li < used && (ni >= nn || weight[li] <= weight[ni]))
                pick[k] = li++;
            else
                pick[k] = ni++;
        }
        weight[nn] = weight[pick[0]] + weight[pick[1]];
        parent[pick[0]] = parent[pick[1]] = (uint16_t)nn;
        nn++;
    }
    /* depth of every leaf; nodes were created parents-last */
    uint8_t depth[2 * 288];
    depth[nn - 1] = 0;
    for (int i = nn - 2; i >= 0; i--)
        depth[i] = (uint8_t)(depth[parent[i]] + 1);
    for (int i = 0; i < used; i++)
        count[depth[i] > 32 ? 32 : depth[i]]++;
    /* fold the too long codes into maxlen and restore the Kraft sum */
    for (int i = maxlen + 1; i <= 32; i++) {
        count[maxlen] += count[i];
        count[i] = 0;
    }
    uint32_t total = 0;
    for (int i = maxlen; i > 0; i--)
        total += (uint32_t)count[i] << (maxlen - i);
    while (total != 1u << maxlen) {
        count[maxlen]--;
        for (int i = maxlen - 1; i > 0; i--)
            if (count[i]) {
                count[i]--;
                count[i + 1] += 2;
                break;
            }
        total--;
    }
    /* the rarest symbols get the longest codes */
    int j = 0;
    for (int l = maxlen; l > 0; l--)
        for (int c = count[l]; c > 0; c--)
            len[leaf[j++].sym] = (uint8_t)l;
}

/* canonical codes, bit-reversed for the LSB-first stream */
static void huff_codes(const uint8_t *len, int n, uint16_t *code)
{
    int count[16] = {0}, next[16], c = 0;
    for (int i = 0; i < n; i++)
        count[len[i]]++;
    count[0] = 0;
    for (int i = 1; i < 16; i++) {
        c = (c + count[i - 1]) << 1;
        next[i] = c;
    }
    for (int i = 0; i < n; i++)
        code[i] = len[i] ? (uint16_t)bit_reverse(next[len[i]]++, len[i]) : 0;
}

static int len_symbol(int len, int *extra, int *ebits)
{
    int s = 28;
    if (len < 258) {
        s = 0;
        while (len_base[s + 1] <= len)
            s++;
    }
    *extra = len - len_base[s];
    *ebits = len_extra[s];
    return 257 + s;
}

static int dist_symbol(int dist, int *extra, int *ebits)
{
    int s = 0;
    while (s < 29 && dist_base[s + 1] <= dist)
        s++;
    *extra = dist - dist_base[s];
    *ebits = dist_extra[s];
    return s;
}

typedef struct Token {
    uint16_t len;       /* 0: literal in dist */
    uint16_t dist;
} Token;

static uint32_t hash3(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]) * 0x9E3779B1u >> (32 - HASH_BITS);
}

static int match_at(const uint8_t *in, size_t n, size_t pos, const int32_t *head, const int32_t *prev,
                    int *dist_out)
{
    int best = 2, chain = MAX_CHAIN;
    size_t maxlen = n - pos < 258 ? n - pos : 258;
    if (maxlen < 3)
        return 0;
    for (int32_t c = head[hash3(in + pos)]; c >= 0 && chain--; c = prev[c]) {
        size_t d = pos - (size_t)c;
        if (d > 32768)
            break;
        if (in[c + best] != in[pos + best] || in[c] != in[pos])
            continue;
        size_t l = 0;
        while (l < maxlen && in[c + l] == in[pos + l])
            l++;
        if ((int)l > best) {
            best = (int)l;
            *dist_out = (int)d;
            if (l == maxlen)
                break;
        }
    }
    return best >= 3 ? best : 0;
}

static void emit_tokens(BitOut *b, const Token *tok, size_t ntok, const uint16_t *lc, const uint8_t *ll,
                        const uint16_t *dc, const uint8_t *dl)
{
    for (size_t i = 0; i < ntok; i++) {
        int extra, ebits, s;
        if (tok[i].len == 0) {
            put(b, lc[tok[i].dist], ll[tok[i].dist]);
            continue;
        }
        s = len_symbol(tok[i].len, &extra, &ebits);
        put(b, lc[s], ll[s]);
        if (ebits)
            put(b, (uint32_t)extra, ebits);
        s = dist_symbol(tok[i].dist, &extra, &ebits);
        put(b, dc[s], dl[s]);
        if (ebits)
            put(b, (uint32_t)extra, ebits);
    }
    put(b, lc[256], ll[256]);
}

size_t z_deflate(const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    if (n > MAX_IN)
        return 0;
    int32_t *head = malloc(sizeof *head * (1u << HASH_BITS));
    int32_t *prev = malloc(sizeof *prev * (n + 1));
    Token *tok = malloc(sizeof *tok * (n + 1));
    size_t ntok = 0, result = 0;
    if (!head || !prev || !tok)
        goto done;
    memset(head, 0xFF, sizeof *head * (1u << HASH_BITS));

    /* ---- LZ77 with one step of lazy evaluation ---- */
    uint32_t lf[288] = {0}, df[32] = {0};
    size_t pos = 0;
#define INSERT(p) do { if ((p) + 3 <= n) { uint32_t h_ = hash3(in + (p)); prev[p] = head[h_]; head[h_] = (int32_t)(p); } } while (0)
    while (pos < n) {
        int dist = 0, len = match_at(in, n, pos, head, prev, &dist);
        INSERT(pos);
        if (len && len < 258 && pos + 1 < n) {
            int dist2 = 0, len2 = match_at(in, n, pos + 1, head, prev, &dist2);
            if (len2 > len) {                   /* a literal now, the longer match next */
                tok[ntok].len = 0;
                tok[ntok++].dist = in[pos];
                lf[in[pos]]++;
                pos++;
                continue;
            }
        }
        if (len) {
            int extra, ebits;
            tok[ntok].len = (uint16_t)len;
            tok[ntok++].dist = (uint16_t)dist;
            lf[len_symbol(len, &extra, &ebits)]++;
            df[dist_symbol(dist, &extra, &ebits)]++;
            for (int k = 1; k < len; k++)
                INSERT(pos + (size_t)k);
            pos += (size_t)len;
        } else {
            tok[ntok].len = 0;
            tok[ntok++].dist = in[pos];
            lf[in[pos]]++;
            pos++;
        }
    }
#undef INSERT
    lf[256] = 1;

    /* ---- dynamic codes ---- */
    uint8_t ll[288], dl[32], cl[19];
    uint16_t lc[288], dc[32], cc[19];
    huff_lengths(lf, 286, 15, ll);
    huff_lengths(df, 30, 15, dl);
    int hlit = 286, hdist = 30;
    while (hlit > 257 && !ll[hlit - 1])
        hlit--;
    while (hdist > 1 && !dl[hdist - 1])
        hdist--;
    /* run-length code the lengths (symbols 16, 17, 18) */
    uint8_t seq[286 + 30], rl_sym[286 + 30], rl_ext[286 + 30];
    uint32_t cf[19] = {0};
    int nseq = hlit + hdist, nrl = 0;
    memcpy(seq, ll, (size_t)hlit);
    memcpy(seq + hlit, dl, (size_t)hdist);
    for (int i = 0; i < nseq;) {
        int run = 1;
        while (i + run < nseq && seq[i + run] == seq[i])
            run++;
        if (seq[i] == 0 && run >= 3) {
            int r = run > 138 ? 138 : run;
            rl_sym[nrl] = r <= 10 ? 17 : 18;
            rl_ext[nrl++] = (uint8_t)(r <= 10 ? r - 3 : r - 11);
            i += r;
        } else if (run >= 4) {
            int r = run - 1 > 6 ? 6 : run - 1;
            rl_sym[nrl] = seq[i];
            rl_ext[nrl++] = 0;
            rl_sym[nrl] = 16;
            rl_ext[nrl++] = (uint8_t)(r - 3);
            i += 1 + r;
        } else {
            rl_sym[nrl] = seq[i];
            rl_ext[nrl++] = 0;
            i++;
        }
    }
    for (int i = 0; i < nrl; i++)
        cf[rl_sym[i]]++;
    huff_lengths(cf, 19, 7, cl);
    {   /* the code length code must be complete even with one symbol */
        int used = 0;
        for (int i = 0; i < 19; i++)
            used += cl[i] != 0;
        if (used == 1)
            cl[cl[0] ? 1 : 0] = 1;
    }
    int hclen = 19;
    while (hclen > 4 && !cl[cl_order[hclen - 1]])
        hclen--;
    huff_codes(ll, 286, lc);
    huff_codes(dl, 30, dc);
    huff_codes(cl, 19, cc);

    /* ---- sizes of the three block types, in bits ---- */
    uint8_t fl[288], fd[32];
    uint16_t flc[288], fdc[32];
    memset(fl, 8, 144);
    memset(fl + 144, 9, 112);
    memset(fl + 256, 7, 24);
    memset(fl + 280, 8, 8);
    memset(fd, 5, 32);
    uint64_t dyn = 3 + 5 + 5 + 4 + 3 * (uint64_t)hclen, fix = 3, extra_bits = 0;
    for (int i = 0; i < nrl; i++)
        dyn += cl[rl_sym[i]] + (rl_sym[i] == 16 ? 2 : rl_sym[i] == 17 ? 3 : rl_sym[i] == 18 ? 7 : 0);
    for (int i = 0; i < 286; i++) {
        dyn += (uint64_t)lf[i] * ll[i];
        fix += (uint64_t)lf[i] * fl[i];
        if (i >= 257)
            extra_bits += (uint64_t)lf[i] * len_extra[i - 257];
    }
    for (int i = 0; i < 30; i++) {
        dyn += (uint64_t)df[i] * dl[i];
        fix += (uint64_t)df[i] * 5;
        extra_bits += (uint64_t)df[i] * dist_extra[i];
    }
    dyn += extra_bits;
    fix += extra_bits;
    uint64_t stored = 8 * (uint64_t)(n + 5);

    BitOut b = { out, cap, 0, 0, 0, 0 };
    if (stored <= dyn && stored <= fix) {
        put(&b, 1, 1);
        put(&b, 0, 2);
        put_flush(&b);
        put(&b, (uint32_t)n, 16);
        put(&b, (uint32_t)n ^ 0xFFFF, 16);
        for (size_t i = 0; i < n; i++)
            put(&b, in[i], 8);
    } else if (fix <= dyn) {
        huff_codes(fl, 288, flc);
        huff_codes(fd, 32, fdc);
        put(&b, 1, 1);
        put(&b, 1, 2);
        emit_tokens(&b, tok, ntok, flc, fl, fdc, fd);
    } else {
        put(&b, 1, 1);
        put(&b, 2, 2);
        put(&b, (uint32_t)(hlit - 257), 5);
        put(&b, (uint32_t)(hdist - 1), 5);
        put(&b, (uint32_t)(hclen - 4), 4);
        for (int i = 0; i < hclen; i++)
            put(&b, cl[cl_order[i]], 3);
        for (int i = 0; i < nrl; i++) {
            put(&b, cc[rl_sym[i]], cl[rl_sym[i]]);
            if (rl_sym[i] == 16)
                put(&b, rl_ext[i], 2);
            else if (rl_sym[i] == 17)
                put(&b, rl_ext[i], 3);
            else if (rl_sym[i] == 18)
                put(&b, rl_ext[i], 7);
        }
        emit_tokens(&b, tok, ntok, lc, ll, dc, dl);
    }
    put_flush(&b);
    result = b.full ? 0 : b.len;
done:
    free(head);
    free(prev);
    free(tok);
    return result;
}
