/*
 * vfile.h - ovl5646: integer file handles over Borland stdio FILE pointers.
 *
 * The game's file layer (vfs.h, ovl569A) does real-file I/O through this
 * small table: a handle is an index into a calloc'ed array of FILE pointers.
 * All functions were far pascal in the original; the C functions keep the
 * logical (declaration) parameter order.  Counts are 32-bit and reads/writes
 * go in 0xFFF0-byte pieces through huge pointers.
 */
#ifndef LALE_ENGINE_VFILE_H
#define LALE_ENGINE_VFILE_H

#include <stdint.h>

#include "engine/dgroup.h"
#include "rtl/bc_stdio.h"

/* 16E0:E860 int  number of slots in vfile_files (o5646_init: n, 0 -> 20) */
#define g_vfile_count   DGS16(0xE860)
/* 16E0:8ED8 char far *[16]  fopen modes by (mode & 15): "rb","rt","rb","rt",
 * "wb","wt","r+b","r+t","ab","at","ab","at","a+b","a+t","a+b","a+t".  The far
 * pointers all point into DGROUP; the port uses their offset word. */
#define VFILE_MODE_TABLE 0x8ED8

/* 16E0:E862 far ptr -> calloc'ed FILE far *[g_vfile_count] (native here).
 * main() (seg0546:03C2) also indexes it directly for its setvbuf call. */
extern BcFile **vfile_files;

int16_t vfile_init(int16_t count);                               /* o5646_init  5646:0000 */
int16_t vfile_open(const char *name, int16_t mode);              /* o5646_open  5646:003A */
int16_t vfile_close(int16_t h);                                  /* o5646_close 5646:00BF */
int32_t vfile_read(int16_t h, void *buf, int32_t count);         /* o5646_read  5646:0140 */
int32_t vfile_write(int16_t h, const void *buf, int32_t count);  /* o5646_write 5646:0248 */
int16_t vfile_seek(int16_t h, int32_t offset, int16_t whence);   /* o5646_seek  5646:0350 */
int32_t vfile_tell(int16_t h);                                   /* o5646_tell  5646:03A6 */
int16_t vfile_flush(int16_t h);                                  /* o5646_flush 5646:03F6 */
int16_t vfile_eof(int16_t h);                                    /* o5646_eof   5646:0443 */

/* Tests: forget the table (after dg_reset / bc_stdio_reset). */
void vfile_reset(void);

#endif
