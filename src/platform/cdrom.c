/*
 * cdrom.c - virtual CD-ROM drive and MSCDEX device requests.  The disc is a
 * cue/bin image read here, or any other sector source (cdrom_open_source: the
 * CHD of src/platform/chd.c).  See cdrom.h.  References: Microsoft MSCDEX 2.21 extensions spec (device
 * request layouts, IOCTL control blocks), DOSBox-X src/dos/dos_mscdex.cpp
 * (values the spec leaves open, marked "DOSBox-X").
 */
#include "cdrom.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 99

static struct {
    int        open;
    int        ntracks;
    CdromTrack tracks[CDROM_MAX_TRACKS];
    uint32_t   leadout;
    FILE      *files[MAX_FILES];      /* cue/bin image                        */
    int        nfiles;
    CdromSource src;                  /* where sectors come from              */
    uint32_t   cache_lba;             /* the sector in `cache` (valid: cache_ok) */
    int        cache_ok;
    uint8_t    cache[2352];
    char       error[256];
    int        mscdex;

    /* audio state, in absolute CD frames (LBA * 588 + sample frame) */
    enum { ST_STOPPED, ST_PLAYING, ST_PAUSED } state;
    uint64_t   pos;          /* current head position                */
    uint64_t   end;          /* end of the current play request      */
    uint64_t   play_start;   /* start of the current play request    */
} g;

const char *cdrom_error(void) { return g.error; }
int  cdrom_is_open(void)       { return g.open; }
int  cdrom_track_count(void)   { return g.ntracks; }
uint32_t cdrom_leadout(void)   { return g.leadout; }
void mscdex_set_present(int p) { g.mscdex = p; }
int  mscdex_present(void)      { return g.mscdex; }
uint32_t cdrom_position(void)  { return (uint32_t)(g.pos / CDROM_FRAMES_PER_SECTOR); }
int  cdrom_playing(void)       { return g.state == ST_PLAYING; }

const CdromTrack *cdrom_track(int number)
{
    for (int i = 0; i < g.ntracks; i++)
        if (g.tracks[i].number == number)
            return &g.tracks[i];
    return NULL;
}

static int fail(const char *msg, const char *arg)
{
    snprintf(g.error, sizeof g.error, "%s%s%s", msg, arg ? ": " : "", arg ? arg : "");
    cdrom_close();
    return -1;
}

void cdrom_close(void)
{
    if (g.src.close)
        g.src.close(g.src.ctx);
    for (int i = 0; i < g.nfiles; i++)
        if (g.files[i])
            fclose(g.files[i]);
    int mscdex = g.mscdex;
    char err[sizeof g.error];
    memcpy(err, g.error, sizeof err);
    memset(&g, 0, sizeof g);
    g.mscdex = mscdex;
    memcpy(g.error, err, sizeof err);
}

/* ---- cue sheet --------------------------------------------------------- */

static int64_t file_size(FILE *f)
{
    if (fseek(f, 0, SEEK_END) != 0)
        return -1;
#if defined(_WIN32)
    return _ftelli64(f);
#else
    return (int64_t)ftello(f);
#endif
}

static int seek64(FILE *f, uint64_t off)
{
#if defined(_WIN32)
    return _fseeki64(f, (long long)off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}

static uint32_t msf_text(const char *s)      /* "mm:ss:ff" -> frames */
{
    unsigned m = 0, sec = 0, f = 0;
    sscanf(s, "%u:%u:%u", &m, &sec, &f);
    return (m * 60 + sec) * 75 + f;
}

/* The cue/bin image as a sector source.  2048-byte tracks come back as Mode 1
   sectors without sync pattern, EDC and ECC (nothing here looks at them). */
static int cue_read_sector(void *ctx, uint32_t lba, uint8_t *raw)
{
    (void)ctx;
    for (int i = g.ntracks - 1; i >= 0; i--) {
        const CdromTrack *t = &g.tracks[i];
        if (lba < t->start)
            continue;
        if (lba >= t->start + t->sectors)
            return -1;                      /* a gap no file holds */
        FILE *f = g.files[t->file];
        uint64_t off = t->file_offset + (uint64_t)(lba - t->start) * t->sector_size;
        if (seek64(f, off) != 0)
            return -1;
        if (t->sector_size == 2048) {
            memset(raw, 0, 2352);
            raw[15] = 1;
            return fread(raw + 16, 1, 2048, f) == 2048 ? 0 : -1;
        }
        return fread(raw, 1, 2352, f) == 2352 ? 0 : -1;
    }
    return -1;
}

int cdrom_open(const char *cue_path)
{
    cdrom_close();
    g.error[0] = 0;
    FILE *cue = fopen(cue_path, "rb");
    if (!cue)
        return fail("cannot open cue sheet", cue_path);

    /* directory of the cue sheet: FILE names are relative to it */
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", cue_path);
    char *slash = NULL;
    for (char *p = dir; *p; p++)
        if (*p == '/' || *p == '\\')
            slash = p;
    if (slash) slash[1] = 0; else dir[0] = 0;

    char line[1024];
    uint32_t file_base = 0;       /* LBA where the current FILE starts    */
    uint32_t file_secs = 0;       /* its length in sectors                */
    uint32_t pregap_total = 0;    /* PREGAP sectors (not in any file)     */
    CdromTrack *cur = NULL;
    while (fgets(line, sizeof line, cue)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (!strncmp(p, "FILE", 4)) {
            char *q1 = strchr(p, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            if (!q1 || !q2) { fclose(cue); return fail("bad FILE line", p); }
            if (!strstr(q2, "BINARY")) { fclose(cue); return fail("only BINARY files supported", p); }
            if (g.nfiles == MAX_FILES) { fclose(cue); return fail("too many files", NULL); }
            *q2 = 0;
            char path[2048];
            snprintf(path, sizeof path, "%s%s", dir, q1 + 1);
            FILE *f = fopen(path, "rb");
            if (!f) { fclose(cue); return fail("cannot open track file", path); }
            if (file_size(f) < 0) { fclose(f); fclose(cue); return fail("cannot size track file", path); }
            file_base += file_secs;      /* previous file's length          */
            g.files[g.nfiles++] = f;
            file_secs = 0;               /* known once TRACK gives the sector size */
            cur = NULL;
        } else if (!strncmp(p, "TRACK", 5)) {
            unsigned n = 0;
            char type[32] = {0};
            if (sscanf(p + 5, "%u %31s", &n, type) != 2 || !g.nfiles || n < 1 || n > 99
                || g.ntracks == CDROM_MAX_TRACKS) {
                fclose(cue); return fail("bad TRACK line", p);
            }
            cur = &g.tracks[g.ntracks++];
            memset(cur, 0, sizeof *cur);
            cur->number = (uint8_t)n;
            cur->file = g.nfiles - 1;
            if (!strcmp(type, "AUDIO"))           { cur->audio = 1; cur->sector_size = 2352; }
            else if (!strcmp(type, "MODE1/2352")) { cur->sector_size = 2352; }
            else if (!strcmp(type, "MODE2/2352")) { cur->sector_size = 2352; }
            else if (!strcmp(type, "MODE1/2048")) { cur->sector_size = 2048; }
            else { fclose(cue); return fail("unsupported track type", type); }
            /* the file's length in sectors, in this track's sector size */
            int64_t sz = file_size(g.files[cur->file]);
            file_secs = (uint32_t)(sz / cur->sector_size);
        } else if (!strncmp(p, "PREGAP", 6) && cur) {
            pregap_total += msf_text(p + 6);
        } else if (!strncmp(p, "INDEX", 5) && cur) {
            unsigned idx = 0;
            char t[32] = {0};
            if (sscanf(p + 5, "%u %31s", &idx, t) == 2 && idx == 1) {
                uint32_t rel = msf_text(t);
                cur->start = file_base + pregap_total + rel;
                cur->file_offset = (uint64_t)rel * cur->sector_size;
            }
        }
    }
    fclose(cue);
    if (!g.ntracks)
        return fail("no tracks in cue sheet", cue_path);
    g.leadout = file_base + pregap_total + file_secs;
    for (int i = 0; i < g.ntracks; i++) {
        uint32_t next = (i + 1 < g.ntracks) ? g.tracks[i + 1].start : g.leadout;
        /* a track ends where the next starts, but never past its own file */
        if (i + 1 < g.ntracks && g.tracks[i + 1].file != g.tracks[i].file) {
            int64_t sz = file_size(g.files[g.tracks[i].file]);
            uint32_t in_file = (uint32_t)((sz - (int64_t)g.tracks[i].file_offset)
                                          / g.tracks[i].sector_size);
            if (g.tracks[i].start + in_file < next)
                next = g.tracks[i].start + in_file;
        }
        g.tracks[i].sectors = next - g.tracks[i].start;
    }
    g.src.read_sector = cue_read_sector;
    g.open = 1;
    g.state = ST_STOPPED;
    g.pos = 0;
    return 0;
}

int cdrom_open_source(const CdromTrack *tracks, int ntracks, uint32_t leadout, const CdromSource *src)
{
    cdrom_close();
    g.error[0] = 0;
    if (ntracks < 1 || ntracks > CDROM_MAX_TRACKS || !src || !src->read_sector) {
        if (src && src->close)
            src->close(src->ctx);
        return fail("bad disc layout", NULL);
    }
    memcpy(g.tracks, tracks, (size_t)ntracks * sizeof *tracks);
    g.ntracks = ntracks;
    g.leadout = leadout;
    g.src = *src;
    g.open = 1;
    g.state = ST_STOPPED;
    g.pos = 0;
    return 0;
}

/* ---- sectors -------------------------------------------------------------- */

static const CdromTrack *track_at(uint32_t lba)
{
    const CdromTrack *t = g.ntracks ? &g.tracks[0] : NULL;
    for (int i = 0; i < g.ntracks; i++)
        if (g.tracks[i].start <= lba)
            t = &g.tracks[i];
    return t;
}

int cdrom_read_raw(uint32_t lba, uint8_t *raw)
{
    if (!g.open || lba >= g.leadout)
        return -1;
    if (!g.cache_ok || g.cache_lba != lba) {
        g.cache_ok = 0;
        if (g.src.read_sector(g.src.ctx, lba, g.cache) != 0)
            return -1;
        g.cache_lba = lba;
        g.cache_ok = 1;
    }
    memcpy(raw, g.cache, 2352);
    return 0;
}

int cdrom_read_data(uint32_t lba, uint8_t *user)
{
    uint8_t raw[2352];
    const CdromTrack *t = track_at(lba);
    if (!g.open || !t || t->audio || lba < t->start || lba >= t->start + t->sectors
        || cdrom_read_raw(lba, raw) != 0)
        return -1;
    /* Mode 1: user data after sync + header; Mode 2 (XA form 1): after the subheader too */
    memcpy(user, raw + (raw[15] == 2 ? 24 : 16), 2048);
    return 0;
}

/* ---- MSCDEX --------------------------------------------------------------- */

enum {
    ST_DONE = 0x0100, ST_BUSY = 0x0200, ST_ERROR = 0x8000,
    ERR_UNKNOWN_UNIT = 0x01, ERR_NOT_READY = 0x02, ERR_UNKNOWN_CMD = 0x03,
    ERR_SECTOR_NOT_FOUND = 0x08,
};

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Red Book address as MSCDEX returns it: frame, second, minute, 0 */
static void wr_redbook(uint8_t *p, uint32_t lba)
{
    uint32_t a = lba + CDROM_LEADIN;
    p[0] = (uint8_t)(a % 75);
    p[1] = (uint8_t)(a / 75 % 60);
    p[2] = (uint8_t)(a / 4500);
    p[3] = 0;
}

static void set_status(uint8_t *req, uint16_t st)
{
    if (g.state == ST_PLAYING)
        st |= ST_BUSY;                  /* DOSBox-X: busy while audio plays */
    req[3] = (uint8_t)st;
    req[4] = (uint8_t)(st >> 8);
}

void mscdex_install_check(uint16_t *bx, uint16_t *cx)
{
    if (!g.mscdex)
        return;                         /* no TSR: registers unchanged */
    *bx = 1;
    *cx = CDROM_DRIVE_INDEX;
}

static uint16_t ioctl_input(uint8_t *x)
{
    switch (x[0]) {
    case 0x08: {                        /* return volume size */
        /* DOSBox-X: lead-out as an absolute frame count (LBA + 150).  The game
         * relies on this: ovl5737:03FE subtracts 150 again. */
        wr32(x + 1, g.leadout + CDROM_LEADIN);
        return ST_DONE;
    }
    case 0x0A:                          /* audio disk info */
        x[1] = g.tracks[0].number;
        x[2] = g.tracks[g.ntracks - 1].number;
        wr_redbook(x + 3, g.leadout);
        return ST_DONE;
    case 0x0B: {                        /* audio track info */
        const CdromTrack *t = cdrom_track(x[1]);
        if (!t)
            return ST_ERROR | ERR_SECTOR_NOT_FOUND;
        wr_redbook(x + 2, t->start);
        x[6] = t->audio ? 0x00 : 0x40;  /* control: 0x40 = data track */
        return ST_DONE;
    }
    case 0x0C: {                        /* audio Q-channel info */
        uint32_t lba = cdrom_position();
        const CdromTrack *t = track_at(lba);
        uint32_t rel = lba - t->start;
        x[1] = t->audio ? 0x01 : 0x41;  /* control/ADR (ADR 1 = position) */
        x[2] = t->number;               /* binary, as DOSBox-X returns it */
        x[3] = 1;                       /* index */
        x[4] = (uint8_t)(rel / 4500);
        x[5] = (uint8_t)(rel / 75 % 60);
        x[6] = (uint8_t)(rel % 75);
        x[7] = 0;
        uint32_t a = lba + CDROM_LEADIN;
        x[8] = (uint8_t)(a / 4500);
        x[9] = (uint8_t)(a / 75 % 60);
        x[10] = (uint8_t)(a % 75);
        return ST_DONE;
    }
    default:
        return ST_ERROR | ERR_UNKNOWN_CMD;
    }
}

void mscdex_send_request(uint8_t drive, uint8_t *req, uint8_t *xfer)
{
    if (!g.mscdex)
        return;                         /* INT 2Fh falls through, nothing written */
    if (drive != CDROM_DRIVE_INDEX)
        return;                         /* MSCDEX: CF set, AX = 000Fh (invalid
                                           drive); the driver is not called and
                                           the request header stays untouched
                                           (RBIL INT 2F/AX=1510h; DOSBox-X
                                           SendDriverRequest).  The game's
                                           intr() ignores CF/AX. */
    req[1] = 0;                         /* MSCDEX fills in the subunit */
    if (!g.open) {
        set_status(req, ST_DONE | ST_ERROR | ERR_NOT_READY);
        return;
    }
    uint16_t st;
    switch (req[2]) {
    case 0x03:                          /* IOCTL input */
        st = xfer ? ioctl_input(xfer) : (ST_ERROR | ERR_UNKNOWN_CMD);
        break;
    case 0x0C:                          /* IOCTL output */
        if (xfer && xfer[0] == 0x02) {  /* reset drive: stops audio */
            g.state = ST_STOPPED;
            st = ST_DONE;
        } else {
            st = ST_ERROR | ERR_UNKNOWN_CMD;
        }
        break;
    case 0x84: {                        /* play audio */
        uint32_t start = rd32(req + 14), count = rd32(req + 18);
        if (req[13] == 1)               /* Red Book address: frame, sec, min */
            start = (uint32_t)req[16] * 4500 + req[15] * 75u + req[14] - CDROM_LEADIN;
        if (start >= g.leadout) {
            st = ST_ERROR | ERR_SECTOR_NOT_FOUND;
            break;
        }
        if (count) {
            uint64_t end = (uint64_t)start + count;
            if (end > g.leadout)
                end = g.leadout;
            g.play_start = (uint64_t)start * CDROM_FRAMES_PER_SECTOR;
            g.pos = g.play_start;
            g.end = end * CDROM_FRAMES_PER_SECTOR;
            g.state = ST_PLAYING;
        }
        st = ST_DONE;
        break;
    }
    case 0x85:                          /* stop audio: pause, or stop when paused */
        if (g.state == ST_PLAYING)
            g.state = ST_PAUSED;
        else
            g.state = ST_STOPPED;
        st = ST_DONE;
        break;
    case 0x88:                          /* resume audio */
        if (g.state == ST_PAUSED) {
            g.state = ST_PLAYING;
            st = ST_DONE;
        } else {
            st = ST_ERROR | 0x0C;       /* general failure */
        }
        break;
    default:
        st = ST_ERROR | ERR_UNKNOWN_CMD;
        break;
    }
    set_status(req, (uint16_t)(st | ST_DONE));
}

/* ---- audio ---------------------------------------------------------------- */

void cdrom_render(int16_t *out, size_t frames)
{
    uint8_t buf[2352];
    while (frames) {
        if (g.state != ST_PLAYING || g.pos >= g.end) {
            if (g.state == ST_PLAYING)
                g.state = ST_STOPPED;   /* end of request: head stays there */
            memset(out, 0, frames * 4);
            return;
        }
        uint32_t lba = (uint32_t)(g.pos / CDROM_FRAMES_PER_SECTOR);
        uint32_t in_sec = (uint32_t)(g.pos % CDROM_FRAMES_PER_SECTOR);
        size_t n = CDROM_FRAMES_PER_SECTOR - in_sec;
        if (n > frames) n = frames;
        if (g.pos + n > g.end) n = (size_t)(g.end - g.pos);
        const CdromTrack *t = track_at(lba);
        int ok = t && t->audio && lba < t->start + t->sectors && cdrom_read_raw(lba, buf) == 0;
        const uint8_t *b = buf + in_sec * 4;
        for (size_t i = 0; i < n; i++) {
            if (ok) {               /* Red Book PCM is little-endian */
                out[2 * i]     = (int16_t)(b[4 * i]     | b[4 * i + 1] << 8);
                out[2 * i + 1] = (int16_t)(b[4 * i + 2] | b[4 * i + 3] << 8);
            } else {                /* data sectors / read errors play as silence */
                out[2 * i] = out[2 * i + 1] = 0;
            }
        }
        out += 2 * n;
        frames -= n;
        g.pos += n;
        if (g.pos >= g.end)
            g.state = ST_STOPPED;       /* end of request: head stays there */
    }
}
