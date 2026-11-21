/*
 * bc_stdio.c - Borland C++ 3.x stdio / io.h behaviour, bit-exact.
 *
 * Every RTL function below is a transcription of IST.EXE seg0000 (the linked
 * Borland C++ 3.x large-model library), including its quirks.  See
 * bc_stdio.h for the runtime model.  Below the RTL sits an emulated DOS file
 * handle layer ("INT 21h") over pc_fopen() binary host streams; it is the only
 * place host file I/O happens.
 *
 * Verified: tests/py/test_vfs.py (unicorn differential against the original
 * RTL code with the same DOS semantics served to the emulator), 2026-11-20.
 */
#include "rtl/bc_stdio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      /* ftruncate (POSIX; mingw-w64 provides it) */

#include "platform/pc.h"
#include "rtl/bc_farheap.h"

/* ========================================================================== */
/* Emulated DOS file handles (INT 21h AH=3Ch/3Dh/3Eh/3Fh/40h/42h/43h/44h)       */
/* ========================================================================== */
/*
 * What the RTL sees of DOS, kept to the semantics the original relies on:
 *   - handles 0-4 are character devices (CON, CON, CON, AUX, PRN);
 *   - open/create return the lowest free handle >= 5; 20 handles in all
 *     (default PSP job file table), error 4 when full;
 *   - the file position is a 32-bit unsigned value, seeking past the end or
 *     "before 0" succeeds (the position wraps), reads there return 0 bytes;
 *   - a write of 0 bytes truncates (or extends) the file at the position;
 *   - access mode 0 = read, 1 = write, 2 = read/write; a read on a write-only
 *     handle or a write on a read-only one fails with error 5.
 * Errors are returned as negative DOS error codes.
 */
#define DOS_HANDLES 20

typedef struct DosFile {
    uint8_t  used;
    uint8_t  device;
    uint8_t  access;        /* 0 read, 1 write, 2 read/write */
    uint16_t info;          /* IOCTL 4400h device information word */
    FILE    *f;             /* host binary stream */
    uint32_t pos;
} DosFile;

static DosFile dos_files[DOS_HANDLES];
static int     dos_ready;

static void dos_init(void)
{
    static const uint16_t dev_info[5] = { 0x80D3, 0x80D3, 0x80D3, 0x80C0, 0xA8C0 };
    if (dos_ready)
        return;
    memset(dos_files, 0, sizeof dos_files);
    for (int h = 0; h < 5; h++) {
        dos_files[h].used = 1;
        dos_files[h].device = 1;
        dos_files[h].access = 2;
        dos_files[h].info = dev_info[h];
    }
    dos_ready = 1;
}

static DosFile *dos_handle(int16_t h)
{
    dos_init();
    if ((uint16_t)h >= DOS_HANDLES || !dos_files[h].used)
        return NULL;
    return &dos_files[h];
}

static int dos_alloc(void)
{
    dos_init();
    for (int h = 0; h < DOS_HANDLES; h++)
        if (!dos_files[h].used)
            return h;
    return -1;
}

static int dos_exists(const char *name)
{
    FILE *f = pc_fopen(name, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* AH=3Dh open existing file; al = access mode | sharing flags. */
static int32_t dos_open(const char *name, uint8_t al)
{
    uint8_t access = al & 7;
    if (access > 2)
        return -0x0C;                                   /* invalid access code */
    FILE *f = pc_fopen(name, access == 0 ? "rb" : "r+b");
    if (!f)
        return dos_exists(name) ? -5 : -2;              /* read-only / not found */
    int h = dos_alloc();
    if (h < 0) {
        fclose(f);
        return -4;                                      /* too many open files */
    }
    dos_files[h] = (DosFile){ 1, 0, access, 0x0002, f, 0 };   /* info: drive C:, file */
    return h;
}

/* AH=3Ch create/truncate; the handle is read/write.  The read-only attribute
 * (attr bit 0) cannot be set portably and is ignored: no caller in the game
 * creates read-only files. */
static int32_t dos_creat(const char *name, uint16_t attr)
{
    (void)attr;
    int h = dos_alloc();
    if (h < 0)
        return -4;
    FILE *f = pc_fopen(name, "w+b");
    if (!f)
        return dos_exists(name) ? -5 : -3;              /* access denied / path not found */
    dos_files[h] = (DosFile){ 1, 0, 2, 0x0002, f, 0 };
    return h;
}

static int32_t dos_close(int16_t h)
{
    DosFile *d = dos_handle(h);
    if (!d)
        return -6;
    if (d->f)
        fclose(d->f);
    memset(d, 0, sizeof *d);
    return 0;
}

static uint32_t dos_size(DosFile *d)
{
    fseek(d->f, 0, SEEK_END);
    return (uint32_t)ftell(d->f);
}

static int32_t dos_read(int16_t h, void *buf, uint16_t n)
{
    DosFile *d = dos_handle(h);
    if (!d)
        return -6;
    if (d->access == 1)
        return -5;
    if (d->device)
        return 0;                                       /* no console input in the port */
    if (d->pos >= 0x80000000u || n == 0)
        return 0;
    if (fseek(d->f, (long)d->pos, SEEK_SET) != 0)
        return 0;
    size_t got = fread(buf, 1, n, d->f);
    d->pos += (uint32_t)got;
    return (int32_t)got;
}

static int32_t dos_write(int16_t h, const void *buf, uint16_t n)
{
    DosFile *d = dos_handle(h);
    if (!d)
        return -6;
    if (d->access == 0)
        return -5;
    if (d->device) {
        if (h == 1 || h == 2) {
            fwrite(buf, 1, n, h == 1 ? stdout : stderr);
            fflush(h == 1 ? stdout : stderr);
        }
        return n;
    }
    if (d->pos >= 0x80000000u)
        return 0;                                       /* "disk full" */
    if (n == 0) {                                       /* DOS: truncate/extend here */
        fflush(d->f);
        if (ftruncate(fileno(d->f), (off_t)d->pos) != 0)
            return -5;
        return 0;
    }
    if (fseek(d->f, (long)d->pos, SEEK_SET) != 0)
        return 0;
    size_t put = fwrite(buf, 1, n, d->f);
    fflush(d->f);
    d->pos += (uint32_t)put;
    return (int32_t)put;
}

static int32_t dos_lseek(int16_t h, uint8_t whence, int32_t off, uint32_t *pos)
{
    DosFile *d = dos_handle(h);
    if (!d)
        return -6;
    if (whence > 2)
        return -1;                                      /* invalid function */
    if (d->device) {
        *pos = 0;
        return 0;
    }
    uint32_t base = whence == 0 ? 0 : whence == 1 ? d->pos : dos_size(d);
    d->pos = base + (uint32_t)off;
    *pos = d->pos;
    return 0;
}

/* AX=4300h get attributes: 20h (archive) for an existing file. */
static int32_t dos_getattr(const char *name)
{
    dos_init();
    return dos_exists(name) ? 0x20 : -2;
}

/* AX=4400h get device information. */
static int32_t dos_ioctl_info(int16_t h)
{
    DosFile *d = dos_handle(h);
    return d ? d->info : -6;
}

/* ========================================================================== */
/* RTL internals                                                              */
/* ========================================================================== */

/* Native stand-ins for FILE.buffer / FILE.curp (far pointers in the original).
 * 22 entries: __getfp can run two slots past _streams[19] (see bc_getfp). */
#define NSTREAM_SLOTS 22
static uint8_t *file_buf[NSTREAM_SLOTS];
static uint8_t *file_cur[NSTREAM_SLOTS];

static int fidx(const BcFile *fp)
{
    return (int)(((const uint8_t *)fp - DGP(BC_STREAMS_OFF)) / (int)sizeof(BcFile));
}
#define BUF(fp)  file_buf[fidx(fp)]
#define CURP(fp) file_cur[fidx(fp)]
/* fp->token holds the near address of the FILE itself */
#define FP_OFF(fp) ((int16_t)((const uint8_t *)(fp) - dg))

/* Exit hooks: the original stores far code pointers in DGROUP (_exitbuf
 * 16E0:9292, _exitfopen 16E0:9296); the port keeps flags instead. */
static int exitbuf_is_xfflush;
static int exitfopen_is_xfclose;

/*
 * __IOerror - map a DOS error (or a negated errno) to errno/_doserrno.
 * Original: IST.EXE 0000:1523, near pascal.  Always returns -1.
 */
static int16_t bc_ioerror(int16_t code)
{
    int16_t e = code;
    if (e < 0) {
        e = -e;
        if (e <= 0x23) {                    /* a valid errno value */
            bc_doserrno = -1;
            bc_errno = e;
            return -1;
        }
        e = 0x57;
    } else if (e > 0x58) {
        e = 0x57;                           /* "unknown error" */
    }
    bc_doserrno = e;
    bc_errno = DGS8(BC_DOSERR_TO_SV + e);
    return -1;
}

/*
 * isatty - IOCTL 4400h, bit 7 of the device word.
 * Original: IST.EXE 0000:156E.  (On a DOS error the original returns bit 7 of
 * the caller's DX - never reached: only called with open handles.)
 */
int16_t bc_isatty(int16_t fd)
{
    int32_t info = dos_ioctl_info(fd);
    return info < 0 ? 0 : (int16_t)(info & 0x80);
}

/*
 * lseek - INT 21h AH=42h; clears O_EOF in _openfd[] first (no range check).
 * Original: IST.EXE 0000:1621.
 */
int32_t bc_lseek(int16_t fd, int32_t offset, int16_t whence)
{
    uint32_t pos;
    bc_openfd(fd) &= ~BC_O_EOF;
    int32_t err = dos_lseek(fd, (uint8_t)whence, offset, &pos);
    if (err < 0)
        return bc_ioerror((int16_t)-err);   /* CWD: DX:AX = -1 */
    return (int32_t)pos;
}

/*
 * _read - raw INT 21h AH=3Fh; EACCES (5) if _openfd says write-only.
 * Original: IST.EXE 0000:179F.  Returns the count (AX) or -1.
 */
static int16_t rtl__read(int16_t fd, void *buf, uint16_t len)
{
    if (bc_openfd(fd) & BC_O_WRONLY)
        return bc_ioerror(5);
    int32_t r = dos_read(fd, buf, len);
    if (r < 0)
        return bc_ioerror((int16_t)-r);
    return (int16_t)r;
}

/* tell - lseek(fd, 0L, SEEK_CUR).  Original: IST.EXE 0000:1966. */
int32_t bc_tell(int16_t fd)
{
    return bc_lseek(fd, 0, 1);
}

/*
 * _chmod - INT 21h AH=43h.  func 0 returns the attributes; func 1 (set) is
 * only used by open() for read-only files created with sharing flags, which
 * the game never does - accepted and ignored.
 * Original: IST.EXE 0000:34FF.
 */
static int16_t rtl_chmod(const char *path, int16_t func, uint16_t attr)
{
    (void)attr;
    int32_t r = dos_getattr(path);
    if (r < 0)
        return bc_ioerror((int16_t)-r);
    (void)func;
    return (int16_t)r;
}

/*
 * close - range check against _nfile, clear _openfd[fd], _close.
 * Original: IST.EXE 0000:351C.
 */
static int16_t rtl__close(int16_t fd);
int16_t bc_close(int16_t fd)
{
    if ((uint16_t)fd >= bc_nfile)
        return bc_ioerror(6);
    bc_openfd(fd) = 0;
    return rtl__close(fd);
}

/* _close - INT 21h AH=3Eh.  Original: IST.EXE 0000:3544. */
static int16_t rtl__close(int16_t fd)
{
    int32_t r = dos_close(fd);
    if (r < 0)
        return bc_ioerror((int16_t)-r);
    bc_openfd(fd) = 0;
    return 0;
}

/*
 * eof - 1 if O_EOF is set or the position is at/after the end, 0 for
 * devices or before the end.  Uses raw INT 21h seeks (O_EOF unchanged).
 * Original: IST.EXE 0000:3562.
 */
int16_t bc_eof(int16_t fd)
{
    if ((uint16_t)fd >= bc_nfile)
        return bc_ioerror(6);
    if (bc_openfd(fd) & BC_O_EOF)
        return 1;
    int32_t info = dos_ioctl_info(fd);
    if (info < 0)
        return bc_ioerror((int16_t)-info);
    if (info & 0x80)
        return 0;
    uint32_t cur, end, dummy;
    int32_t r;
    if ((r = dos_lseek(fd, 1, 0, &cur)) < 0 || (r = dos_lseek(fd, 2, 0, &end)) < 0 ||
        (r = dos_lseek(fd, 0, (int32_t)cur, &dummy)) < 0)
        return bc_ioerror((int16_t)-r);
    return cur >= end ? 1 : 0;
}

/* __creat - INT 21h AH=3Ch.  Original: IST.EXE 0000:4068, near pascal. */
static int16_t rtl_creat(uint16_t attr, const char *path)
{
    int32_t r = dos_creat(path, attr);
    if (r < 0)
        return bc_ioerror((int16_t)-r);
    return (int16_t)r;
}

/* __trunc - write 0 bytes (DOS truncates at the position); result unused.
 * Original: IST.EXE 0000:4083. */
static void rtl_trunc(int16_t fd)
{
    dos_write(fd, NULL, 0);
}

/*
 * _open - INT 21h AH=3Dh; access from O_WRONLY/O_RDWR, sharing bits 4-7
 * passed through; _openfd[fd] = (oflag & 0xB8FF) | O_BINARY.
 * Original: IST.EXE 0000:4204.
 */
static int16_t rtl__open(const char *path, uint16_t oflag)
{
    uint8_t al = (oflag & BC_O_WRONLY) ? 1 : (oflag & BC_O_RDWR) ? 2 : 0;
    al |= (uint8_t)(oflag & 0xF0);
    int32_t r = dos_open(path, al);
    if (r < 0)
        return bc_ioerror((int16_t)-r);
    bc_openfd(r) = (uint16_t)((oflag & 0xB8FF) | BC_O_BINARY);
    return (int16_t)r;
}

/*
 * open - Borland open(path, oflag, pmode).
 * Original: IST.EXE 0000:4095.
 */
int16_t bc_open(const char *path, uint16_t oflag, uint16_t pmode)
{
    int16_t fd;
    if (!(oflag & (BC_O_TEXT | BC_O_BINARY)))
        oflag |= bc_fmode & 0xC000;
    int16_t attr = rtl_chmod(path, 0, 0);           /* -1 (and errno) if missing */

    if (oflag & BC_O_CREAT) {
        pmode &= bc_notumask;
        if (!(pmode & 0x0180))
            bc_ioerror(1);                          /* result ignored, goes on */
        if (attr == -1) {
            if (bc_doserrno != 2)
                return bc_ioerror(bc_doserrno);
            attr = (pmode & 0x0080) ? 0 : 1;        /* !S_IWRITE -> read-only */
            if (oflag & 0x00F0) {                   /* sharing: create, close, reopen */
                fd = rtl_creat(0, path);
                if (fd < 0)
                    return fd;
                rtl__close(fd);
                goto do_open;
            }
            fd = rtl_creat((uint16_t)attr, path);
            if (fd < 0)
                return fd;
            goto set_flags;
        }
        if (oflag & BC_O_EXCL)
            return bc_ioerror(0x50);                /* file exists */
    }
do_open:
    fd = rtl__open(path, oflag);
    if (fd >= 0) {
        int32_t info = dos_ioctl_info(fd);          /* ioctl(fd, 0) */
        uint16_t dev = info < 0 ? 0xFFFF : (uint16_t)info;
        if (dev & 0x80) {
            oflag |= BC_O_DEVICE;
            /* binary device: ioctl(fd, 1, (dev & 0xFF) | 0x20) = raw mode;
             * nothing to do for the port's devices */
        } else if (oflag & BC_O_TRUNC) {
            rtl_trunc(fd);
        }
        if ((attr & 1) && (oflag & BC_O_CREAT) && (oflag & 0x00F0))
            rtl_chmod(path, 1, 1);
    }
set_flags:
    if (fd >= 0)
        bc_openfd(fd) = (uint16_t)((oflag & 0xF8FF) |
                                   ((oflag & (BC_O_CREAT | BC_O_TRUNC)) ? BC_O_CHANGED : 0) |
                                   ((attr & 1) ? 0 : BC_O_CREAT));
    return fd;
}

/*
 * read - Borland read(): _read, then in text mode drop CRs and stop at ^Z
 * (seeking back to it and setting O_EOF so later reads return 0).
 * Original: IST.EXE 0000:4688.
 * Quirk kept: a CR that is the last byte of a chunk makes read() fetch one
 * more byte and store it unexamined (CR, ^Z or anything).  At end of file
 * that byte is an uninitialised stack byte in the original; the port stores 0.
 */
int16_t bc_read(int16_t fd, void *buf, uint16_t len)
{
    uint8_t *p = buf;
    if ((uint16_t)fd >= bc_nfile)
        return bc_ioerror(6);
    if ((uint16_t)(len + 1) < 2 || (bc_openfd(fd) & BC_O_EOF))
        return 0;
    for (;;) {
        uint16_t n = (uint16_t)rtl__read(fd, p, len);
        if ((uint16_t)(n + 1) < 2 || !(bc_openfd(fd) & BC_O_TEXT))
            return (int16_t)n;
        uint16_t cx = n;
        const uint8_t *src = p;
        uint8_t *dst = p;
        for (;;) {
            uint8_t c = *src++;
            if (c == 0x1A) {                        /* ^Z: logical end of file */
                bc_lseek(fd, -(int32_t)cx, 1);
                bc_openfd(fd) |= BC_O_EOF;
                return (int16_t)(dst - p);
            }
            if (c == '\r') {
                if (--cx != 0)
                    continue;
                uint8_t extra = 0;                  /* stack garbage in the original */
                rtl__read(fd, &extra, 1);
                *dst++ = extra;
                break;
            }
            *dst++ = c;
            if (--cx == 0)
                break;
        }
        if (dst != p)
            return (int16_t)(dst - p);
        /* nothing stored: read again (unreachable in practice) */
    }
}

/*
 * _write - raw INT 21h AH=40h; EACCES if _openfd says read-only; sets
 * O_CHANGED.  Original: IST.EXE 0000:51D0.
 */
static int16_t rtl__write(int16_t fd, const void *buf, uint16_t len)
{
    if (bc_openfd(fd) & BC_O_RDONLY)
        return bc_ioerror(5);
    int32_t r = dos_write(fd, buf, len);
    if (r < 0)
        return bc_ioerror((int16_t)-r);
    bc_openfd(fd) |= BC_O_CHANGED;
    return (int16_t)r;
}

/*
 * write - Borland write(): O_APPEND seeks to the end; text mode expands '\n'
 * to "\r\n" through a 128-byte local buffer.  Returns len, -1, or a partial
 * count on a short write.  len 0 (and 0xFFFF) returns 0 without calling DOS.
 * Original: IST.EXE 0000:5081.
 */
int16_t bc_write(int16_t fd, const void *buf, uint16_t len)
{
    if ((uint16_t)fd >= bc_nfile)
        return bc_ioerror(6);
    if ((uint16_t)(len + 1) < 2)
        return 0;
    if (bc_openfd(fd) & BC_O_APPEND)
        bc_lseek(fd, 0, 2);
    if (!(bc_openfd(fd) & BC_O_TEXT))
        return rtl__write(fd, buf, len);

    bc_openfd(fd) &= ~BC_O_EOF;
    const uint8_t *src = buf;
    uint16_t rem = len;
    uint8_t lbuf[0x82];                             /* BP-8Eh: 128 bytes + 1 overflow */
    uint8_t *q = lbuf;
    int16_t r;
    uint16_t n;
    while (rem != 0) {
        rem--;
        uint8_t c = *src++;
        if (c == '\n')
            *q++ = '\r';
        *q++ = c;
        if (q - lbuf >= 0x80) {
            n = (uint16_t)(q - lbuf);
            r = rtl__write(fd, lbuf, n);
            if ((uint16_t)r != n) {
                if (r == -1)
                    return -1;
                return (int16_t)(len - rem + r - n);
            }
            q = lbuf;
        }
    }
    n = (uint16_t)(q - lbuf);
    if (n == 0)
        return (int16_t)len;
    r = rtl__write(fd, lbuf, n);
    if ((uint16_t)r == n)
        return (int16_t)len;
    if (r == -1)
        return -1;
    return (int16_t)(len + r - n);
}

/* ---- streams --------------------------------------------------------------- */

/*
 * fflush - write out a write buffer; for read streams only line-buffered or
 * unbuffered ones are reset (level = 0, without adjusting the file position).
 * NULL flushes all streams and returns 0.
 * Original: IST.EXE 0000:3684.
 */
int16_t bc_fflush(BcFile *fp)
{
    if (fp == NULL) {
        bc_flushall();
        return 0;
    }
    if (fp->token != FP_OFF(fp))
        return -1;
    if (fp->level >= 0) {
        if (!(fp->flags & BC_F_LBUF) && CURP(fp) != &fp->hold)
            return 0;
        fp->level = 0;
        if (CURP(fp) == &fp->hold)
            CURP(fp) = BUF(fp);
        return 0;
    }
    uint16_t n = (uint16_t)(fp->bsize + fp->level + 1);   /* bytes in the buffer */
    fp->level -= (int16_t)n;                              /* -> -(bsize+1): empty */
    CURP(fp) = BUF(fp);
    if ((uint16_t)bc_write(fp->fd, BUF(fp), n) != n && !(fp->flags & BC_F_TERM)) {
        fp->flags |= BC_F_ERR;
        return -1;
    }
    return 0;
}

/* flushall - fflush every stream open for reading or writing; returns the
 * count.  Original: IST.EXE 0000:3751. */
int16_t bc_flushall(void)
{
    int16_t count = 0;
    BcFile *fp = bc_streams;
    for (uint16_t i = bc_nfile; i != 0; i--, fp++) {
        if (fp->flags & BC_F_RDWR) {
            bc_fflush(fp);
            count++;
        }
    }
    return count;
}

/*
 * fclose - flush a write buffer, free a malloc'ed buffer, close the handle,
 * clear the FILE (buffer/curp/token stay).  Returns close()'s result, or -1.
 * Original: IST.EXE 0000:35D9.  (The tmpfile branch - istemp != 0 - is never
 * taken: nothing in the game creates temporary files.)
 */
int16_t bc_fclose(BcFile *fp)
{
    int16_t r = -1;
    if (fp->token != FP_OFF(fp))
        return r;
    if (fp->bsize != 0) {
        if (fp->level < 0 && bc_fflush(fp) != 0)
            return r;
        if (fp->flags & BC_F_BUF)
            bc_free(BUF(fp));                       /* 0000:3623 farfree */
    }
    if (fp->fd >= 0)
        r = bc_close(fp->fd);
    fp->flags = 0;
    fp->bsize = 0;
    fp->level = 0;
    fp->fd = -1;
    if (fp->istemp)
        fp->istemp = 0;                     /* would unlink(__mkname(istemp)) */
    return r;
}

/*
 * __getmode - parse an fopen mode: r / w / a, '+', t / b (else _fmode).
 * Returns the FILE flags (0 = bad mode), *oflag and *pmode for open().
 * Original: IST.EXE 0000:3793, near pascal.
 */
static uint16_t rtl_getmode(const char *mode, uint16_t *oflag, uint16_t *pmode)
{
    uint16_t of, pm = 0, fl;
    char c = *mode++;
    if (c == 'r') {
        of = BC_O_RDONLY;
        fl = BC_F_READ;
    } else if (c == 'w') {
        of = BC_O_WRONLY | BC_O_CREAT | BC_O_TRUNC;
        pm = 0x0080;                                    /* S_IWRITE */
        fl = BC_F_WRIT;
    } else if (c == 'a') {
        of = BC_O_WRONLY | BC_O_CREAT | BC_O_APPEND;
        pm = 0x0080;
        fl = BC_F_WRIT;
    } else {
        return 0;
    }
    c = *mode++;
    if (c == '+' || (*mode == '+' && (c == 't' || c == 'b'))) {
        if (c == '+')
            c = *mode;
        of = (uint16_t)((of & ~3u) | BC_O_RDWR);
        pm = 0x0180;                                    /* S_IREAD | S_IWRITE */
        fl = BC_F_RDWR;
    }
    if (c == 't') {
        of |= BC_O_TEXT;
    } else if (c == 'b') {
        of |= BC_O_BINARY;
        fl |= BC_F_BIN;
    } else {
        of |= bc_fmode & 0xC000;
        if (of & BC_O_BINARY)
            fl |= BC_F_BIN;
    }
    exitfopen_is_xfclose = 1;                           /* _exitfopen = _xfclose */
    *oflag = of;
    *pmode = pm;
    return fl;
}

/*
 * __openfp - open name into fp: flags from the mode, open(), _F_TERM for
 * devices, setvbuf(fp, NULL, terminal ? _IOLBF : _IOFBF, 512).
 * Original: IST.EXE 0000:384F, near pascal.
 */
static BcFile *rtl_openfp(BcFile *fp, const char *name, const char *mode, uint16_t share)
{
    uint16_t oflag, pmode;
    fp->flags = rtl_getmode(mode, &oflag, &pmode);
    if (fp->flags == 0)
        goto fail;
    if (fp->fd < 0) {
        fp->fd = (int8_t)bc_open(name, oflag | share, pmode);
        if (fp->fd < 0)
            goto fail;
    }
    if (bc_isatty(fp->fd))
        fp->flags |= BC_F_TERM;
    if (bc_setvbuf(fp, NULL, (fp->flags & BC_F_TERM) ? BC_IOLBF : BC_IOFBF, 512) != 0) {
        bc_fclose(fp);
        return NULL;
    }
    fp->istemp = 0;
    return fp;
fail:
    fp->fd = -1;
    fp->flags = 0;
    return NULL;
}

/*
 * __getfp - first FILE with fd < 0.
 * Original: IST.EXE 0000:391E, near.
 * Quirk: the scan tests _streams[_nfile] (and, if that one looks in use,
 * returns _streams[_nfile+1] when its fd byte is negative) - DGROUP bytes
 * after the table.  With dg[] overlaid this is reproduced as is.
 */
static BcFile *rtl_getfp(void)
{
    BcFile *fp = bc_streams;
    BcFile *end = bc_streams + bc_nfile;
    for (;;) {
        if (fp->fd < 0)
            break;
        BcFile *old = fp++;
        if (!(old < end))
            break;
    }
    return fp->fd < 0 ? fp : NULL;
}

/*
 * fopen.  Original: IST.EXE 0000:3968.
 */
BcFile *bc_fopen(const char *name, const char *mode)
{
    BcFile *fp = rtl_getfp();
    if (fp == NULL)
        return NULL;
    return rtl_openfp(fp, name, mode, 0);
}

/*
 * __fread_n - read n bytes; binary streams with an empty buffer read whole
 * buffer multiples straight from the handle, everything else goes byte by
 * byte through the buffer (getc).  Returns the number of bytes NOT read.
 * Original: IST.EXE 0000:39B9, near pascal.  All counts are 16-bit unsigned.
 */
static int16_t rtl__fgetc(BcFile *fp);
static uint16_t rtl_fread_n(BcFile *fp, uint16_t n, uint8_t *buf)
{
    uint16_t last = 0;      /* DX: last getc() value (0 on entry from fread) */
    while (n != 0) {
        n++;
        uint16_t chunk = ((uint16_t)fp->bsize <= n) ? (uint16_t)fp->bsize : n;
        if ((fp->flags & BC_F_BIN) && fp->bsize != 0 && (uint16_t)fp->bsize < n &&
            fp->level == 0) {
            n--;
            chunk = 0;
            while ((uint16_t)fp->bsize <= n) {
                chunk += (uint16_t)fp->bsize;
                n -= (uint16_t)fp->bsize;
            }
            uint16_t got = (uint16_t)rtl__read(fp->fd, buf, chunk);
            last = got;
            if (got != chunk) {
                n += (uint16_t)(chunk - got);
                fp->flags |= BC_F_EOF;
                return n;
            }
            buf += got;
            continue;
        }
        for (;;) {
            if (--n == 0)
                break;
            if (--chunk == 0)
                break;
            int16_t c;
            if (--fp->level >= 0)
                c = *CURP(fp)++;                /* inline getc() */
            else
                c = rtl__fgetc(fp);
            last = (uint16_t)c;
            if (c == -1)
                break;
            *buf++ = (uint8_t)c;
        }
        if (last == 0xFFFF) {
            fp->flags |= BC_F_EOF;
            return n;
        }
    }
    return n;
}

/*
 * fread - totals below 64 KB go through one __fread_n (result = whole items
 * read); larger requests item by item.
 * Original: IST.EXE 0000:3AAD.
 */
uint16_t bc_fread(void *ptr, uint16_t size, uint16_t n, BcFile *fp)
{
    if (size == 0)
        return 0;
    uint32_t total = (uint32_t)size * n;
    if (total < 0x10000u) {
        uint16_t left = rtl_fread_n(fp, (uint16_t)total, ptr);
        return (uint16_t)((uint16_t)((uint16_t)total - left) / size);
    }
    uint8_t *p = ptr;
    uint16_t i = (uint16_t)(n + 1);
    while (--i != 0) {
        if (rtl_fread_n(fp, size, p) != 0)
            break;
        p += size;                              /* huge pointer add */
    }
    return (uint16_t)(n - i);
}

/*
 * __nlcount - bytes in the buffer (read: level, write: bsize+level+1), plus
 * one per '\n' in them for text streams (each stands for CR LF in the file).
 * Original: IST.EXE 0000:3B3B, near pascal.
 */
static int16_t rtl_nlcount(BcFile *fp)
{
    uint16_t cx, si;
    if (fp->level < 0)
        cx = (uint16_t)(fp->bsize + fp->level + 1);
    else
        cx = (uint16_t)fp->level;               /* abs(level) */
    si = cx;
    if (!(fp->flags & BC_F_BIN)) {
        uint8_t *p = CURP(fp);
        if (fp->level < 0) {
            while (cx-- != 0)
                if (*--p == '\n')
                    si++;
        } else {
            while (cx-- != 0)
                if (*p++ == '\n')
                    si++;
        }
    }
    return (int16_t)si;
}

/*
 * fseek - fflush, correct SEEK_CUR for buffered input, drop the buffer,
 * lseek.  Returns 0 or -1.
 * Original: IST.EXE 0000:3BBD.
 */
int16_t bc_fseek(BcFile *fp, int32_t offset, int16_t whence)
{
    if (bc_fflush(fp) != 0)
        return -1;
    if (whence == 1 && fp->level > 0)
        offset -= rtl_nlcount(fp);
    fp->flags &= (uint16_t)~(BC_F_OUT | BC_F_IN | BC_F_EOF);
    fp->level = 0;
    CURP(fp) = BUF(fp);
    if (bc_lseek(fp->fd, offset, whence) == -1)
        return -1;
    return 0;
}

/*
 * ftell - handle position minus unread input (plus unwritten output), with
 * the text-mode newline correction of __nlcount.
 * Original: IST.EXE 0000:3C3D.
 * Quirk: in text mode every buffered '\n' counts as two file bytes, so a file
 * with bare LFs (or CRs dropped by read()) gives positions that do not match
 * the file.
 */
int32_t bc_ftell(BcFile *fp)
{
    int32_t pos = bc_tell(fp->fd);
    if (pos == -1)
        return -1;
    if (fp->level < 0)
        pos += rtl_nlcount(fp);
    else
        pos -= rtl_nlcount(fp);
    return pos;
}

/*
 * __fputn - write n bytes: line-buffered streams through fputc; binary
 * streams with memcpy into the buffer or straight to the handle when larger
 * than the buffer; text streams byte-wise through the buffer (translation
 * happens in write() at flush time), unbuffered text through write().
 * Returns n, or 0 on error.
 * Original: IST.EXE 0000:4431, near pascal.
 * Quirk kept: the binary path neither checks _F_WRIT nor sets _F_OUT.
 */
static int16_t rtl__fputc(uint8_t c, BcFile *fp);
static uint16_t rtl_fputn(BcFile *fp, uint16_t n, const uint8_t *ptr)
{
    uint16_t want = n;
    if (fp->flags & BC_F_LBUF) {
        while (n-- != 0)
            if (bc_fputc((int8_t)*ptr++, fp) == -1)
                return 0;
        return want;
    }
    if (fp->flags & BC_F_BIN) {
        if (fp->bsize == 0) {
            if (bc_openfd(fp->fd) & BC_O_APPEND)
                bc_lseek(fp->fd, 0, 2);
            if ((uint16_t)rtl__write(fp->fd, ptr, n) < n)
                return 0;
            return want;
        }
        if ((uint16_t)fp->bsize < n) {
            if (fp->level != 0 && bc_fflush(fp) != 0)
                return 0;
            if (bc_openfd(fp->fd) & BC_O_APPEND)
                bc_lseek(fp->fd, 0, 2);
            if ((uint16_t)rtl__write(fp->fd, ptr, n) < n)
                return 0;
            return want;
        }
        if ((int16_t)(fp->level + n) >= 0) {
            if (fp->level == 0)
                fp->level = (int16_t)(-1 - fp->bsize);
            else if (bc_fflush(fp) != 0)
                return 0;
        }
        memcpy(CURP(fp), ptr, n);
        fp->level += (int16_t)n;
        CURP(fp) += n;
        return want;
    }
    if (fp->bsize != 0) {
        while (n-- != 0) {
            int16_t r;
            if (++fp->level < 0) {
                r = *CURP(fp)++ = *ptr++;
            } else {
                r = rtl__fputc(*ptr++, fp);
            }
            if (r == -1)
                return 0;
        }
        return want;
    }
    if ((uint16_t)bc_write(fp->fd, ptr, n) < n)
        return 0;
    return want;
}

/*
 * fwrite - totals below 64 KB through one __fputn (result = whole items);
 * larger requests item by item.  size 0 returns n (Borland).
 * Original: IST.EXE 0000:3C95.
 */
uint16_t bc_fwrite(const void *ptr, uint16_t size, uint16_t n, BcFile *fp)
{
    if (size == 0)
        return n;
    uint32_t total = (uint32_t)size * n;
    if (total < 0x10000u)
        return (uint16_t)(rtl_fputn(fp, (uint16_t)total, ptr) / size);
    const uint8_t *p = ptr;
    for (uint16_t i = 0; i < n; i++) {
        if (rtl_fputn(fp, size, p) != size)
            return i;
        p += size;
    }
    return n;
}

/*
 * __flushterm - before a terminal read, flush the first 20 streams that
 * are terminals with pending output.  Original: IST.EXE 0000:3D20, near.
 */
static void rtl_flushterm(void)
{
    BcFile *fp = bc_streams;
    for (int i = 20; i != 0; i--, fp++)
        if ((fp->flags & (BC_F_TERM | BC_F_OUT)) == (BC_F_TERM | BC_F_OUT))
            bc_fflush(fp);
}

/*
 * __ffill - refill the buffer with read(); 0 on success, -1 on EOF (_F_EOF)
 * or error (_F_ERR).  Original: IST.EXE 0000:3D5E, near pascal.
 */
static int16_t rtl_ffill(BcFile *fp)
{
    if (fp->flags & BC_F_TERM)
        rtl_flushterm();
    CURP(fp) = BUF(fp);
    fp->level = bc_read(fp->fd, BUF(fp), (uint16_t)fp->bsize);
    if (fp->level > 0) {
        fp->flags &= ~BC_F_EOF;
        return 0;
    }
    if (fp->level == 0) {
        fp->flags = (uint16_t)((fp->flags & ~(BC_F_IN | BC_F_OUT)) | BC_F_EOF);
    } else {
        fp->level = 0;
        fp->flags |= BC_F_ERR;
    }
    return -1;
}

/* _fgetc - getc() slow path: undo getc's level-- and call fgetc.
 * Original: IST.EXE 0000:3DD6. */
static int16_t rtl__fgetc(BcFile *fp)
{
    fp->level++;
    return bc_fgetc(fp);
}

/*
 * fgetc.  Original: IST.EXE 0000:3DEE.  Returns the byte (0..255) or -1.
 * The unbuffered path reads one byte with read() and additionally skips '\r'
 * in text mode (read() of a single CR already fetched the next byte, see
 * bc_read - the original does both).  A read() of -1 is not tested for and
 * returns the static byte, like the original.
 */
int16_t bc_fgetc(BcFile *fp)
{
    if (fp == NULL)
        return -1;
    if (fp->level > 0)
        goto take;
    if (fp->level < 0 || (fp->flags & (BC_F_OUT | BC_F_ERR)) || !(fp->flags & BC_F_READ))
        goto error;
    fp->flags |= BC_F_IN;
    if (fp->bsize != 0) {
        if (rtl_ffill(fp) != 0)
            return -1;
        goto take;
    }
    for (;;) {                                          /* unbuffered */
        if (fp->flags & BC_F_TERM)
            rtl_flushterm();
        if (bc_read(fp->fd, &bc_fgetc_char, 1) == 0) {
            if (bc_eof(fp->fd) == 1) {
                fp->flags = (uint16_t)((fp->flags & ~(BC_F_IN | BC_F_OUT)) | BC_F_EOF);
                return -1;
            }
            goto error;
        }
        if (bc_fgetc_char == '\r' && !(fp->flags & BC_F_BIN))
            continue;
        break;
    }
    fp->flags &= ~BC_F_EOF;
    return bc_fgetc_char;
take:
    fp->level--;
    return *CURP(fp)++;
error:
    fp->flags |= BC_F_ERR;
    return -1;
}

/* _fputc - putc() slow path: undo putc's level++ and call fputc.
 * Original: IST.EXE 0000:426E. */
static int16_t rtl__fputc(uint8_t c, BcFile *fp)
{
    fp->level--;
    return bc_fputc((int8_t)c, fp);
}

/*
 * fputc.  Original: IST.EXE 0000:428A.  Returns the byte (0..255) or -1.
 */
int16_t bc_fputc(int16_t c, BcFile *fp)
{
    bc_fputc_char = (uint8_t)c;
    if (fp->level < -1) {                               /* room in the buffer */
        fp->level++;
        *CURP(fp)++ = bc_fputc_char;
        if ((fp->flags & BC_F_LBUF) && (bc_fputc_char == '\n' || bc_fputc_char == '\r'))
            if (bc_fflush(fp) != 0)
                return -1;
        return bc_fputc_char;
    }
    if ((fp->flags & (BC_F_IN | BC_F_ERR)) || !(fp->flags & BC_F_WRIT))
        goto error;
    fp->flags |= BC_F_OUT;
    if (fp->bsize != 0) {
        if (fp->level != 0 && bc_fflush(fp) != 0)
            return -1;
        fp->level = (int16_t)-fp->bsize;
        *CURP(fp)++ = bc_fputc_char;
        if ((fp->flags & BC_F_LBUF) && (bc_fputc_char == '\n' || bc_fputc_char == '\r'))
            if (bc_fflush(fp) != 0)
                return -1;
        return bc_fputc_char;
    }
    /* unbuffered */
    if (bc_openfd(fp->fd) & BC_O_APPEND)
        bc_lseek(fp->fd, 0, 2);
    if (bc_fputc_char == '\n' && !(fp->flags & BC_F_BIN)) {
        if (rtl__write(fp->fd, DGP(BC_CR_STRING), 1) != 1)
            goto check_term;
    }
    if (rtl__write(fp->fd, &bc_fputc_char, 1) == 1)
        return bc_fputc_char;
check_term:
    if (fp->flags & BC_F_TERM)
        return bc_fputc_char;
error:
    fp->flags |= BC_F_ERR;
    return -1;
}

/*
 * setvbuf.  Original: IST.EXE 0000:4759.
 * type: 0 full, 1 line, 2 none.  Pending input is dropped with
 * fseek(fp, 0, SEEK_CUR).  buf NULL: malloc(size) (freed by fclose).
 */
int16_t bc_setvbuf(BcFile *fp, void *buf, int16_t type, uint16_t size)
{
    if (fp->token != FP_OFF(fp) || type > 2 || size > 0x7FFF)
        return -1;
    if (bc_stdout_buffered == 0 && fp == bc_stdout)
        bc_stdout_buffered = 1;
    else if (bc_stdin_buffered == 0 && fp == bc_stdin)
        bc_stdin_buffered = 1;
    if (fp->level != 0)
        bc_fseek(fp, 0, 1);
    if (fp->flags & BC_F_BUF)
        bc_free(BUF(fp));                               /* 0000:47DD farfree */
    fp->flags &= (uint16_t)~(BC_F_BUF | BC_F_LBUF);
    fp->bsize = 0;
    BUF(fp) = CURP(fp) = &fp->hold;
    if (type != BC_IONBF && size > 0) {
        exitbuf_is_xfflush = 1;                         /* _exitbuf = _xfflush */
        if (buf == NULL) {
            buf = bc_malloc(size);                      /* 0000:4829 malloc (far heap) */
            if (buf == NULL)
                return -1;
            fp->flags |= BC_F_BUF;
        }
        CURP(fp) = BUF(fp) = buf;
        fp->bsize = (int16_t)size;
        if (type == BC_IOLBF)
            fp->flags |= BC_F_LBUF;
    }
    return 0;
}

/*
 * _setupio - #pragma startup (priority 2): mark streams 5.._nfile-1 free
 * (fd -1, token = own address), clear their _openfd[]; stdin/stdout get
 * _F_TERM cleared if they are not devices and a 512-byte buffer (stdin line
 * buffered, stdout unbuffered when a terminal).
 * Original: IST.EXE 0000:1818 (near, no Ghidra function).
 */
void bc_setupio(void)
{
    for (uint16_t i = 5; i < bc_nfile; i++) {
        bc_openfd(i) = 0;
        bc_streams[i].fd = -1;
        bc_streams[i].token = (int16_t)(BC_STREAMS_OFF + i * sizeof(BcFile));
    }
    if (!bc_isatty(bc_stdin->fd))
        bc_stdin->flags &= ~BC_F_TERM;
    {
        /* port: _setupio runs in c0, before main(); the far heap's main()
           state (bc_farheap_reset) already holds the buffer it malloc'ed
           (block 4BB3h), which stdin gets here instead of a second one */
        void *sb = bc_farheap_take_stdio_buffer();
        bc_setvbuf(bc_stdin, sb, (bc_stdin->flags & BC_F_TERM) ? BC_IOLBF : BC_IOFBF, 512);
        if (sb)
            bc_stdin->flags |= BC_F_BUF;
    }
    if (!bc_isatty(bc_stdout->fd))
        bc_stdout->flags &= ~BC_F_TERM;
    bc_setvbuf(bc_stdout, NULL, (bc_stdout->flags & BC_F_TERM) ? BC_IONBF : BC_IOFBF, 512);
}

/* _xfflush - exit hook: fflush the first 4 streams in use.
 * Original: IST.EXE 0000:5249. */
void bc_xfflush(void)
{
    BcFile *fp = bc_streams;
    for (int i = 4; i != 0; i--, fp++)
        if (fp->flags & BC_F_RDWR)
            bc_fflush(fp);
}

/* _xfclose - exit hook: fclose every stream in use.
 * Original: IST.EXE 0000:520C. */
void bc_xfclose(void)
{
    BcFile *fp = bc_streams;
    for (uint16_t i = 0; i < bc_nfile; i++, fp++)
        if (fp->flags & BC_F_RDWR)
            bc_fclose(fp);
}

/* __exit (0000:11B0) calls (*_exitbuf)() then, after _restorezero/_checknull,
 * (*_exitfopen)() - each only does something once setvbuf/fopen installed it. */
void bc_exit_streams(void)
{
    if (exitbuf_is_xfflush)
        bc_xfflush();
    if (exitfopen_is_xfclose)
        bc_xfclose();
}

/* ---- tests ------------------------------------------------------------------ */

void bc_stdio_reset(void)
{
    for (int h = 0; h < DOS_HANDLES; h++)
        if (dos_files[h].f)
            fclose(dos_files[h].f);
    dos_ready = 0;
    dos_init();
    /* buffers are not freed: they are far-heap blocks, reset with the heap
     * (bc_farheap_reset) */
    memset(file_buf, 0, sizeof file_buf);
    memset(file_cur, 0, sizeof file_cur);
    exitbuf_is_xfflush = exitfopen_is_xfclose = 0;
}

uint8_t *bc_file_buffer(BcFile *fp) { return BUF(fp); }
uint8_t *bc_file_curp(BcFile *fp)   { return CURP(fp); }
