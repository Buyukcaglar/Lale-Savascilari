/*
 * cdsector.c - see cdsector.h.  The parity computation follows the widely
 * used formulation of Neill Corlett's ECM (two GF(2^8) look-up tables).
 * Verified: tools/chd.py / tests/test_codecs.c - all 116170 sectors of the
 * game CD's data track are rebuilt from their user data byte for byte.
 */
#include "cdsector.h"

#include <string.h>

const uint8_t cd_sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };

static uint8_t  ecc_f[256], ecc_b[256];
static uint32_t edc_tab[256];

static void init(void)
{
    if (edc_tab[1])
        return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t j = (i << 1) ^ (i & 0x80 ? 0x11D : 0), edc = i;
        ecc_f[i] = (uint8_t)j;
        ecc_b[i ^ j] = (uint8_t)i;
        for (int k = 0; k < 8; k++)
            edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001u : 0);
        edc_tab[i] = edc;
    }
}

uint32_t cd_edc(const uint8_t *data, uint32_t n)
{
    uint32_t edc = 0;
    init();
    while (n--)
        edc = (edc >> 8) ^ edc_tab[(edc ^ *data++) & 0xFF];
    return edc;
}

/* One parity block: major_count codewords of minor_count bytes each, taken
   from src (the sector from its header on) in steps of minor_inc. */
static void ecc_block(const uint8_t *src, uint32_t major_count, uint32_t minor_count,
                      uint32_t major_mult, uint32_t minor_inc, uint8_t *dest)
{
    uint32_t size = major_count * minor_count;
    for (uint32_t major = 0; major < major_count; major++) {
        uint32_t index = (major >> 1) * major_mult + (major & 1);
        uint8_t a = 0, b = 0;
        for (uint32_t minor = 0; minor < minor_count; minor++) {
            uint8_t t = src[index];
            index += minor_inc;
            if (index >= size)
                index -= size;
            a ^= t;
            b ^= t;
            a = ecc_f[a];
        }
        a = ecc_b[ecc_f[a] ^ b];
        dest[major] = a;
        dest[major + major_count] = a ^ b;
    }
}

static void ecc_compute(const uint8_t *raw, uint8_t *p, uint8_t *q)
{
    uint8_t tmp[2340];                          /* header .. P parity */
    init();
    memcpy(tmp, raw + 12, 2064);
    ecc_block(tmp, 86, 24, 2, 86, p);
    memcpy(tmp + 2064, p, 172);
    ecc_block(tmp, 52, 43, 86, 88, q);
}

void cd_ecc_generate(uint8_t *raw)
{
    ecc_compute(raw, raw + 2076, raw + 2248);
}

int cd_ecc_ok(const uint8_t *raw)
{
    uint8_t p[172], q[104];
    ecc_compute(raw, p, q);
    return memcmp(p, raw + 2076, 172) == 0 && memcmp(q, raw + 2248, 104) == 0;
}

void cd_mode1_build(uint8_t *raw, uint32_t lba, const uint8_t *user)
{
    uint32_t a = lba + 150, edc;
    memcpy(raw, cd_sync, 12);
    raw[12] = (uint8_t)((a / 4500 / 10) << 4 | (a / 4500 % 10));
    raw[13] = (uint8_t)((a / 75 % 60 / 10) << 4 | (a / 75 % 60 % 10));
    raw[14] = (uint8_t)((a % 75 / 10) << 4 | (a % 75 % 10));
    raw[15] = 1;
    memmove(raw + 16, user, 2048);
    edc = cd_edc(raw, 2064);
    raw[2064] = (uint8_t)edc;
    raw[2065] = (uint8_t)(edc >> 8);
    raw[2066] = (uint8_t)(edc >> 16);
    raw[2067] = (uint8_t)(edc >> 24);
    memset(raw + 2068, 0, 8);
    cd_ecc_generate(raw);
}

void cd_scramble(uint8_t *raw)
{
    static uint8_t table[2352];
    if (!table[12]) {                           /* the first byte of the sequence is 01h */
        uint32_t reg = 1;
        for (int i = 12; i < 2352; i++) {
            uint8_t v = 0;
            for (int b = 0; b < 8; b++) {
                v |= (uint8_t)((reg & 1) << b);
                reg = reg >> 1 | ((reg ^ reg >> 1) & 1) << 14;
            }
            table[i] = v;
        }
    }
    for (int i = 12; i < 2352; i++)
        raw[i] ^= table[i];
}
