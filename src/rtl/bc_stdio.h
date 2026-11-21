/*
 * bc_stdio.h - Borland C++ 3.x stdio / io.h behaviour, bit-exact.
 *
 * The game opens its files through Borland's stdio (fopen/fread/fwrite/fseek/
 * ftell/fflush/fclose/setvbuf, see ovl5646 and ovl559A).  The port never uses
 * the host stdio for game files: Borland's FILE state machine (level/flags/
 * curp, the text-mode CR-LF and ^Z handling of read()/write(), ftell's newline
 * correction, the 512-byte default buffer) is reproduced here on top of a
 * small emulated DOS file-handle layer (INT 21h 3Ch-44h semantics) that sits on
 * pc_fopen() binary host streams.  So text mode behaves identically on every
 * host, including the original's quirks.
 *
 * Runtime model
 *   - The FILE table _streams[20] lives in dg[] at its original address
 *     (16E0:929E, 20 bytes each) so its level/flags/fd/bsize/token bytes are
 *     the original's.  Its two far pointers (buffer, curp) cannot hold native
 *     pointers; they live in native side arrays inside bc_stdio.c and the
 *     8 bytes at FILE+08..+0F in dg[] stay unused.
 *   - _openfd[] (16E0:9430), _nfile, errno, _doserrno, _fmode are in dg[].
 *   - DOS handles 0-4 are the standard devices (stdout/stderr go to the host
 *     stdout/stderr); files get the lowest free handle >= 5, max 20 (the
 *     default PSP handle table).
 *
 * Use these instead of host stdio for everything the original did with
 * Borland stdio: bc_fopen/bc_fread/bc_fwrite/bc_fseek/bc_ftell/bc_fflush/
 * bc_fclose/bc_flushall/bc_setvbuf/bc_fgetc/bc_fputc, and bc_exit_streams()
 * from the exit path.  printf/puts/sprintf are not covered here.
 */
#ifndef LALE_RTL_BC_STDIO_H
#define LALE_RTL_BC_STDIO_H

#include <stddef.h>
#include <stdint.h>

#include "engine/dgroup.h"

/* ---- FILE (Borland <stdio.h>), overlaid on dg[] ---------------------------- */
#pragma pack(push, 1)
typedef struct BcFile {
    int16_t  level;        /* +00 >0: chars left in read buffer; <0: -(free+1) in write buffer */
    uint16_t flags;        /* +02 BC_F_* */
    int8_t   fd;           /* +04 DOS handle, -1 = free slot */
    uint8_t  hold;         /* +05 1-byte buffer of unbuffered streams */
    int16_t  bsize;        /* +06 buffer size (0 = unbuffered) */
    uint32_t buffer_far;   /* +08 far ptr - unused in the port (native side table) */
    uint32_t curp_far;     /* +0C far ptr - unused in the port (native side table) */
    uint16_t istemp;       /* +10 tmpfile number (always 0 in this game) */
    int16_t  token;        /* +12 near ptr to itself: validity check */
} BcFile;
#pragma pack(pop)
_Static_assert(sizeof(BcFile) == 20, "Borland FILE is 20 bytes");
_Static_assert(offsetof(BcFile, token) == 0x12, "FILE.token at +12h");

#define BC_STREAMS_OFF   0x929E                       /* 16E0:929E _streams[] */
#define bc_streams       ((BcFile *)DGP(BC_STREAMS_OFF))
#define bc_stdin         (&bc_streams[0])
#define bc_stdout        (&bc_streams[1])
#define bc_stderr        (&bc_streams[2])

/* FILE.flags */
#define BC_F_RDWR   0x0003
#define BC_F_READ   0x0001
#define BC_F_WRIT   0x0002
#define BC_F_BUF    0x0004   /* buffer was malloc'ed by setvbuf */
#define BC_F_LBUF   0x0008   /* line buffered */
#define BC_F_ERR    0x0010
#define BC_F_EOF    0x0020
#define BC_F_BIN    0x0040
#define BC_F_IN     0x0080   /* last operation was input */
#define BC_F_OUT    0x0100   /* last operation was output */
#define BC_F_TERM   0x0200   /* stream is a terminal */

/* open() oflag / _openfd[] bits */
#define BC_O_RDONLY  0x0001
#define BC_O_WRONLY  0x0002
#define BC_O_RDWR    0x0004
#define BC_O_CREAT   0x0100  /* in _openfd[]: "file may be written" */
#define BC_O_TRUNC   0x0200
#define BC_O_EOF     0x0200  /* in _openfd[]: ^Z seen by text read() */
#define BC_O_EXCL    0x0400
#define BC_O_APPEND  0x0800
#define BC_O_CHANGED 0x1000
#define BC_O_DEVICE  0x2000
#define BC_O_TEXT    0x4000
#define BC_O_BINARY  0x8000

#define BC_IOFBF 0
#define BC_IOLBF 1
#define BC_IONBF 2

/* ---- RTL globals in DGROUP ------------------------------------------------- */
#define bc_errno            DGS16(0x007F)   /* errno */
#define bc_nfile            DG16(0x942E)    /* _nfile = 20 */
#define bc_openfd(fd)       DG16(0x9430 + 2 * (uint16_t)(fd))   /* _openfd[] (unchecked index, like the original) */
#define bc_fmode            DG16(0x9458)    /* _fmode, default O_TEXT */
#define bc_notumask         DG16(0x945A)    /* _notUmask */
#define bc_doserrno         DGS16(0x945E)   /* _doserrno */
#define BC_DOSERR_TO_SV     0x9460          /* _dosErrorToSV[0x59] (int8) */
#define bc_stdin_buffered   DG16(0x959C)    /* setvbuf: stdin already set up */
#define bc_stdout_buffered  DG16(0x959E)    /* setvbuf: stdout already set up */
#define BC_CR_STRING        0x959A          /* "\r" written before '\n' by unbuffered text fputc */
#define bc_fgetc_char       DG8(0xEA70)     /* static 1-byte buffer of unbuffered fgetc */
#define bc_fputc_char       DG8(0xEA72)     /* static copy of fputc's character */

/* ---- stdio (the functions the game calls, plus what they need) ------------- */
BcFile  *bc_fopen(const char *name, const char *mode);               /* 0000:3968 */
int16_t  bc_fclose(BcFile *fp);                                       /* 0000:35D9 */
int16_t  bc_fflush(BcFile *fp);                                       /* 0000:3684 */
int16_t  bc_flushall(void);                                           /* 0000:3751 */
uint16_t bc_fread(void *ptr, uint16_t size, uint16_t n, BcFile *fp);  /* 0000:3AAD */
uint16_t bc_fwrite(const void *ptr, uint16_t size, uint16_t n, BcFile *fp); /* 0000:3C95 */
int16_t  bc_fseek(BcFile *fp, int32_t offset, int16_t whence);        /* 0000:3BBD */
int32_t  bc_ftell(BcFile *fp);                                        /* 0000:3C3D */
int16_t  bc_setvbuf(BcFile *fp, void *buf, int16_t type, uint16_t size); /* 0000:4759 */
int16_t  bc_fgetc(BcFile *fp);                                        /* 0000:3DEE */
int16_t  bc_fputc(int16_t c, BcFile *fp);                             /* 0000:428A */

/* ---- io.h level ------------------------------------------------------------ */
int16_t  bc_open(const char *path, uint16_t oflag, uint16_t pmode);  /* 0000:4095 */
int16_t  bc_close(int16_t fd);                                        /* 0000:351C */
int16_t  bc_read(int16_t fd, void *buf, uint16_t len);               /* 0000:4688 text-mode aware */
int16_t  bc_write(int16_t fd, const void *buf, uint16_t len);        /* 0000:5081 text-mode aware */
int32_t  bc_lseek(int16_t fd, int32_t offset, int16_t whence);       /* 0000:1621 */
int32_t  bc_tell(int16_t fd);                                         /* 0000:1966 */
int16_t  bc_eof(int16_t fd);                                          /* 0000:3562 */
int16_t  bc_isatty(int16_t fd);                                       /* 0000:156E */

/* ---- start-up / exit ------------------------------------------------------- */
void     bc_setupio(void);       /* 0000:1818 #pragma startup: streams 5.._nfile-1, stdin/stdout buffers */
void     bc_exit_streams(void);  /* what __exit runs: (*_exitbuf)() = _xfflush, (*_exitfopen)() = _xfclose */
void     bc_xfflush(void);       /* 0000:5249 */
void     bc_xfclose(void);       /* 0000:520C */

/* Tests: close every host file, forget buffers, DOS handles back to 0-4. */
void     bc_stdio_reset(void);
/* Tests: native buffer/curp of a stream (offset of curp inside the buffer). */
uint8_t *bc_file_buffer(BcFile *fp);
uint8_t *bc_file_curp(BcFile *fp);

#endif
