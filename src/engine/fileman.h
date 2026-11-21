/*
 * fileman.h - ALTDUZEY\FILEMAN.L: picture/palette/file loading and the CMP
 * run-length decoders.  Original: IST.EXE ovl5447 (far cdecl).
 * Implementation: src/engine/fileman.c.
 *
 * CMP picture file: u16 width, u16 height, then CMP RLE data:
 *   byte c > 80h -> (c - 80h) copies of the next byte;
 *   byte c <= 80h -> c literal bytes follow.
 */
#ifndef LALE_ENGINE_FILEMAN_H
#define LALE_ENGINE_FILEMAN_H

#include <stdint.h>

#include "engine/dgroup.h"
#include "engine/memman.h"

/* 16E0:E7B8 char[0x32]: name of the last picture fm_load_cmp loaded (its
   last 49 characters, zero padded) */
#define g_fm_last_name      DGSTR(0xE7B8)
#define FM_LAST_NAME_LEN    0x32

/* Huge decoder: dst is a conventional block (type 0) or EMS heap block
   `ems` (type 1, written through the page frame, remapped every 48 KB);
   other types are fatal.  Returns bytes written. */
uint32_t fm_cmp_decode_huge(const uint8_t *src, uint32_t src_len, uint8_t *dst,
                            uint16_t ems, uint8_t type);                      /* 5447:0000 */
uint16_t fm_cmp_decode(const uint8_t *src, uint16_t src_len, uint8_t *dst);   /* 5447:043B */
void     fm_load_cmp(uint8_t type, const char *name, int16_t slot);          /* 5447:052A */
int32_t  fm_filelength(int16_t fd);                                          /* 5447:0873 */
void     fm_load_pal(const char *name, int16_t first, int16_t count);        /* 5447:08B9 */
MmHandle *fm_load_file(uint8_t type, const char *name, MmHandle *block);     /* 5447:093A */
char    *fm_change_ext(const char *path, const char *ext);                   /* 5447:0B5F */

#endif
