/*
 * chdtool.c - command-line front end of the port's disc code (src/platform:
 * chd.c, cdrom.c, iso9660.c and the codecs), for looking at DATA/LALE.CHD and
 * for checking that code against other programs.  Built as out/<host>/chdtool.
 *
 *   chdtool create  <disc.cue> <out.chd>     write a CHD from a cue/bin image
 *   chdtool info    <disc.chd|disc.cue>      tracks, volume, SHA-1
 *   chdtool verify  <disc.chd>               read everything, compare the SHA-1 sums
 *   chdtool extract <disc.chd|disc.cue> <outdir>   tracks as .bin files + cue sheet
 *   chdtool ls      <disc.chd|disc.cue>      the files of the data track
 *   chdtool cat     <disc.chd|disc.cue> <NAME> <outfile>   one file of the data track
 *   chdtool import  <base> <source>          what the game does with a source the
 *                                            player gives it: <base>/DATA, <base>/SAVE
 *                                            (src/platform/install.h)
 *
 * Codec checks against zlib and a FLAC decoder (tools/chd.py uses them):
 *   chdtool deflate <in> <out>      in (at most 65535 bytes) as one raw deflate stream
 *   chdtool inflate <in> <out>      a raw deflate stream decoded
 *   chdtool flacenc <in.s16le> <out.flac>    16-bit stereo PCM as a .flac file
 *   chdtool flacdec <in.flac> <out.s16le>    a 16-bit stereo .flac file decoded
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/cdrom.h"
#include "platform/chd.h"
#include "platform/flac.h"
#include "platform/install.h"
#include "platform/iso9660.h"
#include "platform/zdeflate.h"

static int ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    if (n < m)
        return 0;
    for (size_t i = 0; i < m; i++)
        if ((s[n - m + i] | 0x20) != ext[i])
            return 0;
    return 1;
}

static int open_disc(const char *path)
{
    char err[512] = "";
    if (ends_with(path, ".chd")) {
        if (chd_mount(path, err, sizeof err) != 0) {
            fprintf(stderr, "%s\n", err);
            return -1;
        }
        return 0;
    }
    if (cdrom_open(path) != 0) {
        fprintf(stderr, "%s\n", cdrom_error());
        return -1;
    }
    return 0;
}

static int progress(void *ctx, uint32_t done, uint32_t total)
{
    (void)ctx;
    static unsigned last = 999;
    unsigned pct = total ? (unsigned)((uint64_t)done * 100 / total) : 100;
    if (pct != last)
        fprintf(stderr, "\r%3u %%", pct);
    last = pct;
    return 0;
}

static int src_read(void *ctx, uint32_t lba, uint8_t *raw)
{
    (void)ctx;
    return cdrom_read_raw(lba, raw);
}

static void print_sha1(const uint8_t *d)
{
    for (int i = 0; i < 20; i++)
        printf("%02x", d[i]);
}

static int iso_rd(void *ctx, uint32_t lba, uint8_t *sector)
{
    (void)ctx;
    return cdrom_read_data(lba, sector);
}

static int ls_cb(void *ctx, const IsoEntry *e)
{
    (void)ctx;
    printf("%10u  %6u  %04d-%02d-%02d %02d:%02d:%02d  %s%s\n", (unsigned)e->size, (unsigned)e->lba,
           1900 + e->date[0], e->date[1], e->date[2], e->date[3], e->date[4], e->date[5],
           e->name, e->is_dir ? "/" : "");
    return 0;
}

static uint8_t *load(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long size;
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)size + 1);
    *n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    return buf;
}

static int save(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(data, 1, n, f) != n) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    fclose(f);
    return 0;
}

static int cmd_info(const char *path)
{
    if (open_disc(path))
        return 1;
    for (int n = 1; n <= 99; n++) {
        const CdromTrack *t = cdrom_track(n);
        if (t)
            printf("track %2d  %-5s  start %6u  sectors %6u\n", t->number, t->audio ? "audio" : "data",
                   (unsigned)t->start, (unsigned)t->sectors);
    }
    printf("lead-out  %u\n", (unsigned)cdrom_leadout());
    IsoFs fs;
    if (iso_mount(&fs, iso_rd, NULL) == 0)
        printf("volume    %s (%u sectors)\n", fs.volume_id, (unsigned)fs.volume_sectors);
    cdrom_close();
    if (ends_with(path, ".chd")) {
        char err[256];
        Chd *c = chd_open(path, err, sizeof err);
        if (c) {
            printf("sha1      ");
            print_sha1(chd_sha1(c));
            printf("\n");
            chd_close(c);
        }
    }
    return 0;
}

static int cmd_create(const char *cue, const char *out)
{
    CdromTrack tracks[CDROM_MAX_TRACKS];
    char err[512] = "";
    ChdStats st;
    int n = 0;
    if (cdrom_open(cue) != 0) {
        fprintf(stderr, "%s\n", cdrom_error());
        return 1;
    }
    for (int i = 1; i <= 99; i++)
        if (cdrom_track(i))
            tracks[n++] = *cdrom_track(i);
    CdromSource src = { src_read, NULL, NULL };
    int rc = chd_create_cd(out, tracks, n, cdrom_leadout(), &src, progress, NULL, &st, err, sizeof err);
    fprintf(stderr, "\n");
    cdrom_close();
    if (rc) {
        fprintf(stderr, "%s\n", rc == 1 ? "cancelled" : err);
        return 1;
    }
    printf("%s: %u sectors, %llu bytes, %u unreadable\nsha1 ", out, (unsigned)st.sectors,
           (unsigned long long)st.bytes, (unsigned)st.unreadable);
    print_sha1(st.sha1);
    printf("\n");
    return 0;
}

static int cmd_verify(const char *path)
{
    char err[512] = "";
    Chd *c = chd_open(path, err, sizeof err);
    if (!c) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    int rc = chd_verify(c, progress, NULL, err, sizeof err);
    fprintf(stderr, "\n");
    chd_close(c);
    if (rc) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    printf("%s: intact\n", path);
    return 0;
}

static int cmd_extract(const char *path, const char *outdir)
{
    char name[1024];
    uint8_t raw[2352];
    if (open_disc(path))
        return 1;
    snprintf(name, sizeof name, "%s/disc.cue", outdir);
    FILE *cue = fopen(name, "wb");
    if (!cue) {
        fprintf(stderr, "cannot write %s (the folder must exist)\n", name);
        return 1;
    }
    /* each file runs from where the previous track ends to where its own does */
    uint32_t begin = 0;
    for (int n = 1; n <= 99; n++) {
        const CdromTrack *t = cdrom_track(n);
        if (!t)
            continue;
        uint32_t end = t->start + t->sectors, bad = 0;
        snprintf(name, sizeof name, "%s/track%02d.bin", outdir, n);
        FILE *f = fopen(name, "wb");
        if (!f) {
            fprintf(stderr, "cannot write %s\n", name);
            return 1;
        }
        for (uint32_t lba = begin; lba < end; lba++) {
            if (cdrom_read_raw(lba, raw) != 0) {
                memset(raw, 0, sizeof raw);
                bad++;
            }
            fwrite(raw, 1, sizeof raw, f);
        }
        fclose(f);
        fprintf(cue, "FILE \"track%02d.bin\" BINARY\r\n  TRACK %02d %s\r\n", n, n,
                t->audio ? "AUDIO" : raw[15] == 2 ? "MODE2/2352" : "MODE1/2352");
        if (t->start > begin) {
            uint32_t gap = t->start - begin;
            fprintf(cue, "    INDEX 00 00:00:00\r\n    INDEX 01 %02u:%02u:%02u\r\n", gap / 4500, gap / 75 % 60, gap % 75);
        } else {
            fprintf(cue, "    INDEX 01 00:00:00\r\n");
        }
        printf("track%02d.bin  %u sectors%s\n", n, (unsigned)(end - begin), bad ? "  (unreadable ones as zeros)" : "");
        begin = end;
    }
    fclose(cue);
    cdrom_close();
    return 0;
}

static int cmd_ls(const char *path)
{
    IsoFs fs;
    if (open_disc(path))
        return 1;
    if (iso_mount(&fs, iso_rd, NULL) != 0) {
        fprintf(stderr, "no ISO 9660 volume\n");
        return 1;
    }
    iso_list(&fs, "", ls_cb, NULL);
    cdrom_close();
    return 0;
}

static int cmd_cat(const char *path, const char *name, const char *out)
{
    IsoFs fs;
    IsoEntry e;
    uint8_t buf[2048 * 16];
    if (open_disc(path))
        return 1;
    if (iso_mount(&fs, iso_rd, NULL) != 0 || iso_find(&fs, name, &e) != 0 || e.is_dir) {
        fprintf(stderr, "no such file on the disc: %s\n", name);
        return 1;
    }
    FILE *f = fopen(out, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    for (uint32_t pos = 0; pos < e.size;) {
        uint32_t n = iso_read(&fs, &e, pos, buf, sizeof buf);
        if (!n) {
            fprintf(stderr, "read error at byte %u\n", (unsigned)pos);
            return 1;
        }
        fwrite(buf, 1, n, f);
        pos += n;
    }
    fclose(f);
    cdrom_close();
    return 0;
}

static void put32be(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static int cmd_codec(const char *cmd, const char *in, const char *out)
{
    size_t n = 0, m = 0;
    uint8_t *src = load(in, &n), *dst;
    int rc = 1;
    if (!src)
        return 1;
    if (!strcmp(cmd, "deflate")) {
        dst = malloc(n + 64);
        m = z_deflate(src, n, dst, n + 64);
        if (m)
            rc = save(out, dst, m);
        else
            fprintf(stderr, "deflate failed (at most 65535 bytes)\n");
    } else if (!strcmp(cmd, "inflate")) {
        size_t cap = n * 1100 + 65536;
        dst = malloc(cap);
        if (z_inflate_mem(src, n, dst, cap, &m, NULL) == 0)
            rc = save(out, dst, m);
        else
            fprintf(stderr, "inflate failed\n");
    } else if (!strcmp(cmd, "flacenc")) {
        uint32_t samples = (uint32_t)(n / 4);
        size_t cap = n + n / 2 + 4096;
        dst = malloc(cap);
        /* "fLaC", STREAMINFO (last block): block size 2352, 44.1 kHz, stereo, 16 bits */
        static const uint8_t head[8] = { 'f', 'L', 'a', 'C', 0x80, 0, 0, 34 };
        memcpy(dst, head, 8);
        memset(dst + 8, 0, 34);
        dst[8] = dst[10] = 2352 >> 8;
        dst[9] = dst[11] = 2352 & 0xFF;
        dst[18] = 44100 >> 12;
        dst[19] = (44100 >> 4) & 0xFF;
        dst[20] = (uint8_t)((44100 & 15) << 4 | 1 << 1 | 0);      /* 2 channels; 16 bits: high bit of bps-1 */
        dst[21] = (uint8_t)(15 << 4);                             /* bps-1 low 4 bits; samples high 4 bits */
        put32be(dst + 22, samples);
        m = flac_encode((const int16_t *)src, samples, 2352, dst + 42, cap - 42);
        if (m)
            rc = save(out, dst, m + 42);
        else
            fprintf(stderr, "flac encode failed\n");
    } else if (!strcmp(cmd, "flacdec")) {
        size_t pos = 4;
        uint32_t samples = 0;
        int last = 0;
        if (n < 42 || memcmp(src, "fLaC", 4) != 0) {
            fprintf(stderr, "not a .flac file\n");
            return 1;
        }
        while (!last && pos + 4 <= n) {         /* skip the metadata blocks */
            uint32_t len = (uint32_t)src[pos + 1] << 16 | (uint32_t)src[pos + 2] << 8 | src[pos + 3];
            last = src[pos] & 0x80;
            if ((src[pos] & 0x7F) == 0)
                samples = (uint32_t)src[pos + 4 + 14] << 24 | (uint32_t)src[pos + 4 + 15] << 16
                          | (uint32_t)src[pos + 4 + 16] << 8 | src[pos + 4 + 17];
            pos += 4 + len;
        }
        dst = malloc((size_t)samples * 4 + 4);
        m = flac_decode(src + pos, n - pos, (int16_t *)dst, samples);
        if (m)
            rc = save(out, dst, (size_t)samples * 4);
        else
            fprintf(stderr, "flac decode failed\n");
    }
    return rc;
}

static int import_progress(void *ctx, const char *text, uint64_t done, uint64_t total)
{
    static const char *last;
    static unsigned last_pct = 999;
    unsigned pct = total ? (unsigned)(done * 100 / total) : 0;
    (void)ctx;
    if (text != last) {
        fprintf(stderr, "%s%s\n", last ? "\n" : "", text);
        last_pct = 999;
    }
    last = text;
    if (total && pct != last_pct)
        fprintf(stderr, "\r%3u %%", pct);
    last_pct = pct;
    return 0;
}

static int cmd_import(const char *base, const char *source)
{
    char err[1024] = "";
    int rc = install_import(base, source, import_progress, NULL, err, sizeof err);
    fprintf(stderr, "\n");
    if (rc < 0) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    printf("%s\n", rc == INSTALL_DISC ? "disc -> DATA/LALE.CHD" : rc == INSTALL_FILES ? "files -> DATA, SAVE" : "cancelled");
    return 0;
}

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "";
    if (!strcmp(cmd, "create") && argc == 4)  return cmd_create(argv[2], argv[3]);
    if (!strcmp(cmd, "info") && argc == 3)    return cmd_info(argv[2]);
    if (!strcmp(cmd, "verify") && argc == 3)  return cmd_verify(argv[2]);
    if (!strcmp(cmd, "extract") && argc == 4) return cmd_extract(argv[2], argv[3]);
    if (!strcmp(cmd, "ls") && argc == 3)      return cmd_ls(argv[2]);
    if (!strcmp(cmd, "cat") && argc == 5)     return cmd_cat(argv[2], argv[3], argv[4]);
    if (!strcmp(cmd, "import") && argc == 4)  return cmd_import(argv[2], argv[3]);
    if (argc == 4 && (!strcmp(cmd, "deflate") || !strcmp(cmd, "inflate") || !strcmp(cmd, "flacenc")
                      || !strcmp(cmd, "flacdec")))
        return cmd_codec(cmd, argv[2], argv[3]);
    fprintf(stderr,
            "usage: chdtool create <disc.cue> <out.chd>\n"
            "       chdtool info|verify|ls <disc.chd|disc.cue>\n"
            "       chdtool extract <disc> <outdir>\n"
            "       chdtool cat <disc> <NAME> <outfile>\n"
            "       chdtool import <base> <source>\n"
            "       chdtool deflate|inflate|flacenc|flacdec <in> <out>\n");
    return 2;
}
