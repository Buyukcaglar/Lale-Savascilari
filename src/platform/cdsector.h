/*
 * cdsector.h - the frame of a CD-ROM Mode 1 sector (ECMA-130, section 14 and
 * annex A): sync pattern, header, EDC and the P/Q parity bytes around the 2048
 * bytes of user data.
 *
 * Used to turn the 2048-byte sectors a drive or an .iso gives into the raw
 * 2352-byte sectors of a disc image (cd_mode1_build), and by the CHD's CD
 * codecs, which leave sync and ECC out of sectors whose ECC is the regular
 * one and put them back when reading (cd_ecc_ok, cd_ecc_generate).
 */
#ifndef LALE_PLATFORM_CDSECTOR_H
#define LALE_PLATFORM_CDSECTOR_H

#include <stdint.h>

extern const uint8_t cd_sync[12];

/* raw[0..2351] from the user data: sync, header (address of lba, mode 1),
 * EDC, 8 zero bytes, P and Q parity. */
void cd_mode1_build(uint8_t *raw, uint32_t lba, const uint8_t *user);

/* 1 if the P/Q parity bytes of the sector are what the header and data give. */
int  cd_ecc_ok(const uint8_t *raw);
/* Writes the P/Q parity bytes (raw + 2076, 276 bytes). */
void cd_ecc_generate(uint8_t *raw);
/* EDC of Mode 1: over raw[0..2063]. */
uint32_t cd_edc(const uint8_t *data, uint32_t n);

/* Scrambles or unscrambles a raw sector in place (the operation is its own
   inverse): bytes 12..2351 XORed with the sequence of ECMA-130 Annex B, a
   15-bit shift register x^15 + x + 1 preset to 1.  Sectors a dumping program
   could not line up with a sync pattern are left in this form. */
void cd_scramble(uint8_t *raw);

#endif
