/*
 * drive.c - see drive.h.
 * Verified: the table of contents and the data path against an ISO image
 * mounted as a virtual drive (docs/INSTALL.md); the audio path
 * (IOCTL_CDROM_RAW_READ) has not been run against a real drive yet.
 */
#include "drive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/cdsector.h"

#ifdef _WIN32

#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>
#include "platform/uitext.h"

#define CHUNK 16                        /* sectors per read */

typedef struct Drive {
    HANDLE     h;
    CdromTrack track[CDROM_MAX_TRACKS];
    int        ntracks;
    uint32_t   leadout;
    uint32_t   cache_lba;               /* first sector in buf */
    int        cache_n, cache_audio;
    uint8_t    buf[CHUNK * 2352];
} Drive;

static void seterr(char *err, size_t n, const char *msg, const char *arg)
{
    if (err && n)
        snprintf(err, n, "%s%s%s", msg, arg ? ": " : "", arg ? arg : "");
}

static int drive_letter(const char *path)
{
    char c = path[0];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) || path[1] != ':')
        return 0;
    if (path[2] && !((path[2] == '\\' || path[2] == '/') && !path[3]))
        return 0;
    return c & ~0x20;
}

int drive_is_optical(const char *path)
{
    char root[4] = "A:\\";
    root[0] = (char)drive_letter(path);
    return root[0] && GetDriveTypeA(root) == DRIVE_CDROM;
}

int drive_find_game(const char *label, char *out, size_t size)
{
    DWORD drives = GetLogicalDrives();
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);   /* no "insert a disc" boxes */
    int found = 0;
    for (int i = 0; i < 26 && !found; i++) {
        char root[4] = { (char)('A' + i), ':', '\\', 0 }, name[64] = "";
        if (!(drives & (1u << i)) || GetDriveTypeA(root) != DRIVE_CDROM)
            continue;
        if (GetVolumeInformationA(root, name, sizeof name, NULL, NULL, NULL, NULL, 0)
            && _stricmp(name, label) == 0) {
            snprintf(out, size, "%c:", root[0]);
            found = 1;
        }
    }
    SetErrorMode(mode);
    return found;
}

static const CdromTrack *track_of(const Drive *d, uint32_t lba)
{
    for (int i = d->ntracks - 1; i >= 0; i--)
        if (lba >= d->track[i].start || i == 0)
            return &d->track[i];
    return NULL;
}

/* n sectors from lba into d->buf: user data (2048 bytes each) or audio (2352) */
static int read_chunk(Drive *d, uint32_t lba, int n, int audio)
{
    DWORD got = 0;
    if (audio) {
        RAW_READ_INFO ri;
        ri.DiskOffset.QuadPart = (LONGLONG)lba * 2048;
        ri.SectorCount = (ULONG)n;
        ri.TrackMode = CDDA;
        return DeviceIoControl(d->h, IOCTL_CDROM_RAW_READ, &ri, sizeof ri, d->buf, (DWORD)n * 2352, &got, NULL)
               && got == (DWORD)n * 2352 ? 0 : -1;
    }
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)lba * 2048;
    return SetFilePointerEx(d->h, pos, NULL, FILE_BEGIN) && ReadFile(d->h, d->buf, (DWORD)n * 2048, &got, NULL)
           && got == (DWORD)n * 2048 ? 0 : -1;
}

static int drive_read(void *ctx, uint32_t lba, uint8_t *raw)
{
    Drive *d = ctx;
    const CdromTrack *t = track_of(d, lba);
    if (!t || lba >= d->leadout)
        return -1;
    uint32_t end = t->start + t->sectors;
    if (!(d->cache_n > 0 && lba >= d->cache_lba && lba < d->cache_lba + (uint32_t)d->cache_n
          && d->cache_audio == t->audio)) {
        int n = end - lba < CHUNK ? (int)(end - lba) : CHUNK;
        d->cache_n = 0;
        if (read_chunk(d, lba, n, t->audio) != 0) {
            n = 1;                              /* a bad spot: sector by sector from here */
            if (read_chunk(d, lba, 1, t->audio) != 0) {
                /* the post-gap before an audio track: empty sectors */
                if (!t->audio && end < d->leadout && end - lba <= 150) {
                    static const uint8_t zero[2048];
                    cd_mode1_build(raw, lba, zero);
                    return 0;
                }
                return -1;
            }
        }
        d->cache_lba = lba;
        d->cache_n = n;
        d->cache_audio = t->audio;
    }
    if (t->audio)
        memcpy(raw, d->buf + (size_t)(lba - d->cache_lba) * 2352, 2352);
    else
        cd_mode1_build(raw, lba, d->buf + (size_t)(lba - d->cache_lba) * 2048);
    return 0;
}

static void drive_close(void *ctx)
{
    Drive *d = ctx;
    CloseHandle(d->h);
    free(d);
}

int drive_open(const char *drive, CdromTrack *tracks, uint32_t *leadout, CdromSource *src,
               char *err, size_t errlen)
{
    char dev[8];
    CDROM_TOC toc;
    DWORD got = 0;
    Drive *d;
    if (!drive_letter(drive)) {
        seterr(err, errlen, T_E_NOT_DRIVE, drive);
        return -1;
    }
    d = calloc(1, sizeof *d);
    if (!d)
        return -1;
    snprintf(dev, sizeof dev, "\\\\.\\%c:", drive_letter(drive));
    d->h = CreateFileA(dev, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (d->h == INVALID_HANDLE_VALUE) {
        seterr(err, errlen, T_E_DRIVE_OPEN, drive);
        free(d);
        return -1;
    }
    memset(&toc, 0, sizeof toc);
    if (!DeviceIoControl(d->h, IOCTL_CDROM_READ_TOC, NULL, 0, &toc, sizeof toc, &got, NULL)
        || toc.LastTrack < toc.FirstTrack || toc.LastTrack > CDROM_MAX_TRACKS) {
        seterr(err, errlen, T_E_DRIVE_NO_DISC, drive);
        drive_close(d);
        return -1;
    }
    int n = toc.LastTrack - toc.FirstTrack + 1;
    for (int i = 0; i <= n; i++) {              /* entry n is the lead-out (track AAh) */
        const TRACK_DATA *td = &toc.TrackData[i];
        uint32_t lba = ((uint32_t)td->Address[1] * 60 + td->Address[2]) * 75 + td->Address[3];
        lba = lba >= CDROM_LEADIN ? lba - CDROM_LEADIN : 0;
        if (i == n) {
            d->leadout = lba;
            break;
        }
        d->track[i].number = td->TrackNumber;
        d->track[i].audio = !(td->Control & 4);
        d->track[i].start = lba;
        d->track[i].sector_size = 2352;
    }
    /* Virtual drives (a mounted .iso) give the last sector as the lead-out:
       a data track that ends the disc is at least as long as its volume
       (primary volume descriptor, sector 16, volume space size). */
    if (!d->track[n - 1].audio && read_chunk(d, d->track[n - 1].start + 16, 1, 0) == 0
        && memcmp(d->buf + 1, "CD001", 5) == 0) {
        uint32_t vol = (uint32_t)d->buf[80] | (uint32_t)d->buf[81] << 8 | (uint32_t)d->buf[82] << 16
                       | (uint32_t)d->buf[83] << 24;
        if (vol < 450000 && d->track[n - 1].start + vol > d->leadout)
            d->leadout = d->track[n - 1].start + vol;
    }
    for (int i = 0; i < n; i++)
        d->track[i].sectors = (i + 1 < n ? d->track[i + 1].start : d->leadout) - d->track[i].start;
    d->ntracks = n;
    memcpy(tracks, d->track, sizeof d->track[0] * (size_t)n);
    *leadout = d->leadout;
    src->read_sector = drive_read;
    src->close = drive_close;
    src->ctx = d;
    return n;
}

#else /* not Windows */

int drive_is_optical(const char *path) { (void)path; return 0; }

int drive_find_game(const char *label, char *out, size_t size)
{
    (void)label; (void)out; (void)size;
    return 0;
}

int drive_open(const char *drive, CdromTrack *tracks, uint32_t *leadout, CdromSource *src,
               char *err, size_t errlen)
{
    (void)tracks; (void)leadout; (void)src;
    if (err && errlen)
        snprintf(err, errlen, T_E_DRIVE_NOT_HERE ": %s", drive);
    return -1;
}

#endif
