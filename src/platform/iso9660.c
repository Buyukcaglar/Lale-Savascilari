/*
 * iso9660.c - see iso9660.h.  Reference: ECMA-119 (ISO 9660), sections 8.4
 * (primary volume descriptor) and 9.1 (directory record).
 */
#include "iso9660.h"

#include <ctype.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int iso_mount(IsoFs *fs, IsoReadFn read, void *ctx)
{
    uint8_t s[2048];
    memset(fs, 0, sizeof *fs);
    fs->read = read;
    fs->ctx = ctx;
    for (uint32_t lba = 16; lba < 32; lba++) {
        if (read(ctx, lba, s) != 0 || memcmp(s + 1, "CD001", 5) != 0)
            return -1;
        if (s[0] == 255)                    /* volume descriptor set terminator */
            return -1;
        if (s[0] != 1)
            continue;
        /* primary volume descriptor: root directory record at 156 */
        if ((s[128] | s[129] << 8) != 2048)
            return -1;                      /* logical block size other than 2048 */
        fs->volume_sectors = le32(s + 80);
        fs->root_lba = le32(s + 156 + 2) + s[156 + 1];
        fs->root_size = le32(s + 156 + 10);
        memcpy(fs->volume_id, s + 40, 32);
        for (int i = 31; i >= 0 && fs->volume_id[i] == ' '; i--)
            fs->volume_id[i] = 0;
        return 0;
    }
    return -1;
}

/* Walks the records of a directory extent; stops when fn returns non-zero
   (that value is returned), 0 at the end, -1 on a read error. */
static int walk(IsoFs *fs, uint32_t lba, uint32_t size, IsoListFn fn, void *ctx)
{
    uint8_t s[2048];
    for (uint32_t done = 0; done < size; done += 2048, lba++) {
        if (fs->read(fs->ctx, lba, s) != 0)
            return -1;
        for (uint32_t o = 0; o < 2048;) {
            uint32_t len = s[o], nlen;
            if (len == 0)
                break;                      /* records do not cross sectors */
            if (len < 34 || o + len > 2048 || (nlen = s[o + 32]) + 33 > len)
                return -1;
            IsoEntry e;
            memset(&e, 0, sizeof e);
            e.lba = le32(s + o + 2) + s[o + 1];
            e.size = le32(s + o + 10);
            e.is_dir = (s[o + 25] & 2) != 0;
            memcpy(e.date, s + o + 18, 7);
            const uint8_t *nm = s + o + 33;
            o += len;
            if (nlen == 1 && (nm[0] == 0 || nm[0] == 1))
                continue;                   /* "." and ".." */
            if (nlen >= sizeof e.name)
                continue;
            for (uint32_t i = 0; i < nlen && nm[i] != ';'; i++)
                e.name[i] = (char)toupper(nm[i]);
            /* "NAME." (no extension) is the file NAME */
            size_t l = strlen(e.name);
            if (l > 1 && e.name[l - 1] == '.')
                e.name[l - 1] = 0;
            int r = fn(ctx, &e);
            if (r)
                return r;
        }
    }
    return 0;
}

typedef struct Find {
    char     want[40];
    IsoEntry got;
} Find;

static int find_cb(void *ctx, const IsoEntry *e)
{
    Find *f = ctx;
    if (strcmp(e->name, f->want) != 0)
        return 0;
    f->got = *e;
    return 1;
}

int iso_find(IsoFs *fs, const char *path, IsoEntry *out)
{
    IsoEntry cur;
    memset(&cur, 0, sizeof cur);
    cur.lba = fs->root_lba;
    cur.size = fs->root_size;
    cur.is_dir = 1;
    if (!fs->read)
        return -1;
    while (*path) {
        Find f;
        size_t n = strcspn(path, "\\/");
        if (n == 0) {
            path++;
            continue;
        }
        if (!cur.is_dir || n >= sizeof f.want)
            return -1;
        for (size_t i = 0; i < n; i++)
            f.want[i] = (char)toupper((unsigned char)path[i]);
        f.want[n] = 0;
        if (n > 1 && f.want[n - 1] == '.')
            f.want[n - 1] = 0;
        if (walk(fs, cur.lba, cur.size, find_cb, &f) != 1)
            return -1;
        cur = f.got;
        path += n;
    }
    *out = cur;
    return 0;
}

int iso_list(IsoFs *fs, const char *dir, IsoListFn fn, void *ctx)
{
    IsoEntry d;
    if (iso_find(fs, dir, &d) != 0 || !d.is_dir)
        return -1;
    return walk(fs, d.lba, d.size, fn, ctx) < 0 ? -1 : 0;
}

uint32_t iso_read(IsoFs *fs, const IsoEntry *e, uint32_t pos, void *buf, uint32_t n)
{
    uint8_t s[2048];
    uint8_t *out = buf;
    uint32_t got = 0;
    if (pos >= e->size)
        return 0;
    if (n > e->size - pos)
        n = e->size - pos;
    while (got < n) {
        uint32_t in = (pos + got) % 2048, take = 2048 - in;
        if (take > n - got)
            take = n - got;
        if (fs->read(fs->ctx, e->lba + (pos + got) / 2048, s) != 0)
            break;
        memcpy(out + got, s + in, take);
        got += take;
    }
    return got;
}
