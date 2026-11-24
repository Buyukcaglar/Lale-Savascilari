/*
 * zipfile.c - see zipfile.h.
 * Verified: tests/test_codecs.c (an archive the test writes itself, stored
 * and deflated members, a wrong CRC, a file that is no ZIP); the 2005
 * release's own .zip end to end (docs/INSTALL.md), 2026-11-24.
 */
#include "zipfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/zdeflate.h"
#include "platform/uitext.h"

struct ZipFile {
    FILE     *f;
    ZipEntry *e;
    int       n;
};

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static void seterr(char *err, size_t n, const char *msg, const char *arg)
{
    if (err && n)
        snprintf(err, n, "%s%s%s", msg, arg ? ": " : "", arg ? arg : "");
}

ZipFile *zip_open(const char *path, char *err, size_t errlen)
{
    uint8_t tail[65536 + 22], *cd = NULL;
    ZipFile *z = calloc(1, sizeof *z);
    long size, pos, at = -1;
    size_t got;
    if (!z)
        return NULL;
    z->f = fopen(path, "rb");
    if (!z->f) {
        seterr(err, errlen, T_E_OPEN, path);
        goto fail;
    }
    /* the end-of-central-directory record: the last 22 bytes, or before a comment */
    fseek(z->f, 0, SEEK_END);
    size = ftell(z->f);
    pos = size > (long)sizeof tail ? size - (long)sizeof tail : 0;
    fseek(z->f, pos, SEEK_SET);
    got = fread(tail, 1, sizeof tail, z->f);
    for (long i = (long)got - 22; i >= 0; i--)
        if (le32(tail + i) == 0x06054B50 && i + 22 + (long)le16(tail + i + 20) <= (long)got) {
            at = i;
            break;
        }
    if (at < 0) {
        seterr(err, errlen, T_E_ZIP_NOT_ZIP, path);
        goto fail;
    }
    uint32_t count = le16(tail + at + 10), cdsize = le32(tail + at + 12), cdoff = le32(tail + at + 16);
    if (count == 0xFFFF || cdoff == 0xFFFFFFFF || cdsize > 64u * 1024 * 1024) {
        seterr(err, errlen, T_E_ZIP_ZIP64, path);
        goto fail;
    }
    cd = malloc(cdsize + 1);
    z->e = calloc(count + 1, sizeof *z->e);
    if (!cd || !z->e || fseek(z->f, (long)cdoff, SEEK_SET) || fread(cd, 1, cdsize, z->f) != cdsize) {
        seterr(err, errlen, T_E_ZIP_DAMAGED, path);
        goto fail;
    }
    for (uint32_t p = 0; z->n < (int)count && p + 46 <= cdsize && le32(cd + p) == 0x02014B50;) {
        ZipEntry *e = &z->e[z->n];
        uint32_t nlen = le16(cd + p + 28), xlen = le16(cd + p + 30), clen = le16(cd + p + 32);
        if (p + 46 + nlen > cdsize)
            break;
        e->flags = (uint16_t)le16(cd + p + 8);
        e->method = (uint16_t)le16(cd + p + 10);
        e->crc = le32(cd + p + 16);
        e->csize = le32(cd + p + 20);
        e->usize = le32(cd + p + 24);
        e->offset = le32(cd + p + 42);
        if (nlen < sizeof e->name) {            /* longer names: skipped */
            memcpy(e->name, cd + p + 46, nlen);
            for (char *c = e->name; *c; c++)
                if (*c == '\\')
                    *c = '/';
            z->n++;
        }
        p += 46 + nlen + xlen + clen;
    }
    free(cd);
    return z;
fail:
    free(cd);
    zip_close(z);
    return NULL;
}

void zip_close(ZipFile *z)
{
    if (!z)
        return;
    if (z->f)
        fclose(z->f);
    free(z->e);
    free(z);
}

int zip_count(const ZipFile *z) { return z->n; }
const ZipEntry *zip_entry(const ZipFile *z, int i) { return i >= 0 && i < z->n ? &z->e[i] : NULL; }

typedef struct Xfer {
    FILE    *in, *out;
    uint32_t left;              /* compressed bytes not yet read */
    uint32_t written, crc;
    int    (*tick)(void *ctx, uint32_t bytes);
    void    *ctx;
    int      stopped, failed;
    uint32_t last_tick;
} Xfer;

static size_t xfer_read(void *ctx, uint8_t *buf, size_t cap)
{
    Xfer *x = ctx;
    size_t n = cap < x->left ? cap : x->left;
    n = fread(buf, 1, n, x->in);
    x->left -= (uint32_t)n;
    return n;
}

static int xfer_write(void *ctx, const uint8_t *data, size_t n)
{
    Xfer *x = ctx;
    if (fwrite(data, 1, n, x->out) != n) {
        x->failed = 1;
        return -1;
    }
    x->crc = z_crc32(x->crc, data, n);
    x->written += (uint32_t)n;
    if (x->tick && x->written - x->last_tick >= 1u << 20) {
        x->last_tick = x->written;
        if (x->tick(x->ctx, x->written)) {
            x->stopped = 1;
            return -1;
        }
    }
    return 0;
}

int zip_extract(ZipFile *z, const ZipEntry *e, const char *dest,
                int (*tick)(void *ctx, uint32_t bytes), void *ctx, char *err, size_t errlen)
{
    uint8_t lh[30];
    Xfer x = { z->f, NULL, e->csize, 0, 0, tick, ctx, 0, 0, 0 };
    int rc = -1;
    if ((e->flags & 1) || (e->method != 0 && e->method != 8) || e->usize == 0xFFFFFFFF) {
        seterr(err, errlen, T_E_ZIP_METHOD, e->name);
        return -1;
    }
    if (fseek(z->f, (long)e->offset, SEEK_SET) || fread(lh, 1, 30, z->f) != 30 || le32(lh) != 0x04034B50
        || fseek(z->f, (long)(le16(lh + 26) + le16(lh + 28)), SEEK_CUR)) {
        seterr(err, errlen, T_E_ZIP_DAMAGED, e->name);
        return -1;
    }
    x.out = fopen(dest, "wb");
    if (!x.out) {
        seterr(err, errlen, T_E_CREATE, dest);
        return -1;
    }
    if (e->method == 0) {
        static uint8_t buf[65536];
        size_t n;
        while (x.left && (n = xfer_read(&x, buf, sizeof buf)) > 0)
            if (xfer_write(&x, buf, n))
                break;
    } else if (z_inflate(xfer_read, &x, xfer_write, &x, NULL) != 0 && !x.stopped && !x.failed) {
        seterr(err, errlen, T_E_ZIP_DEFLATE, e->name);
        goto done;
    }
    if (x.stopped) {
        rc = 1;
        goto done;
    }
    if (x.failed || fflush(x.out)) {
        seterr(err, errlen, T_E_WRITE_FULL, dest);
        goto done;
    }
    if (x.written != e->usize || x.crc != e->crc) {
        seterr(err, errlen, T_E_ZIP_CRC, e->name);
        goto done;
    }
    rc = 0;
done:
    fclose(x.out);
    if (rc != 0)
        remove(dest);
    return rc;
}
