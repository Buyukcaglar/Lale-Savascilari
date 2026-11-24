/*
 * flac.h - FLAC frames for 16-bit stereo CD audio, both ways.
 *
 * The "cdfl" hunks of a CHD (src/platform/chd.c) hold the audio of 8 CD
 * sectors as bare FLAC frames - no stream header - of 2352 samples each,
 * 44.1 kHz, 2 channels, 16 bits (MAME's chd_cd_flac_compressor).  This is the
 * port's own small codec for them:
 *
 *   flac_encode   frames with fixed predictors (order 0-4), partitioned Rice
 *                 coding and the best of the four stereo layouts - what the
 *                 reference encoder does without its LPC stage;
 *   flac_decode   every subframe type of the format (constant, verbatim,
 *                 fixed, LPC; wasted bits; both Rice methods), so that hunks
 *                 written by other programs (chdman / libFLAC) decode too.
 *
 * Samples are interleaved (left, right) int16.
 */
#ifndef LALE_PLATFORM_FLAC_H
#define LALE_PLATFORM_FLAC_H

#include <stddef.h>
#include <stdint.h>

/* Encodes nsamples stereo samples as frames of `blocksize` samples (the last
 * one shorter if need be).  Returns the number of bytes, or 0 when they do not
 * fit into cap. */
size_t flac_encode(const int16_t *pcm, uint32_t nsamples, uint32_t blocksize, uint8_t *out, size_t cap);

/* Decodes frames until nsamples stereo samples are out.  Returns the number
 * of bytes the frames took, or 0 on bad data. */
size_t flac_decode(const uint8_t *in, size_t n, int16_t *pcm, uint32_t nsamples);

#endif
