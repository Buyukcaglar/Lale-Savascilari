/*
 * zdeflate.h - DEFLATE (RFC 1951) both ways, and CRC-32.
 *
 * The port's own small implementation, so that the game needs no library to
 * write and read DATA/LALE.CHD (src/platform/chd.c: the "cdzl" hunks and the
 * subcode part of "cdfl" hunks are raw deflate streams) and to read a ZIP
 * archive of the game (src/platform/zipfile.c).
 *
 *   z_inflate       any raw deflate stream, any size, through callbacks
 *   z_inflate_mem   the same from memory to memory
 *   z_deflate       one buffer of at most 64 KB as one stream (a CHD hunk)
 */
#ifndef LALE_PLATFORM_ZDEFLATE_H
#define LALE_PLATFORM_ZDEFLATE_H

#include <stddef.h>
#include <stdint.h>

/* read: fill buf with up to cap bytes, return their number (0 = end of input) */
typedef size_t (*ZReadFn)(void *ctx, uint8_t *buf, size_t cap);
/* write: take n bytes; return 0, or non-zero to stop (z_inflate then fails) */
typedef int (*ZWriteFn)(void *ctx, const uint8_t *data, size_t n);

/* Decodes one raw deflate stream.  0 = the final block ended cleanly, -1 =
 * bad data, input ended early or the writer stopped.  *in_used (may be NULL):
 * bytes of input the stream took. */
int z_inflate(ZReadFn rd, void *rctx, ZWriteFn wr, void *wctx, uint64_t *in_used);

/* 0 and *outlen, or -1 (bad data, or more than outcap bytes of output). */
int z_inflate_mem(const uint8_t *in, size_t n, uint8_t *out, size_t outcap, size_t *outlen, size_t *in_used);

/* Compresses n bytes (n <= 65535) into out; returns the stream's length, or 0
 * when it does not fit into cap bytes. */
size_t z_deflate(const uint8_t *in, size_t n, uint8_t *out, size_t cap);

/* CRC-32 (ZIP, zlib): start with crc = 0. */
uint32_t z_crc32(uint32_t crc, const void *data, size_t n);

#endif
