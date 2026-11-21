/*
 * dsmistr.h - DSMI string/memory helpers.  Original: IST.EXE seg0DDF
 * (Pascal convention; logical parameter order kept).
 * Implementation: src/dsmi/dsmistr.c.
 *
 * They are NOT drop-in libc replacements - each keeps the original's quirks
 * (16-bit counts, dsmiStrLen = strlen + 1, dsmiMemCmp's JECXZ, StrCmp's
 * unusual non-zero values).  Use them where the original did.
 */
#ifndef LALE_DSMI_DSMISTR_H
#define LALE_DSMI_DSMISTR_H

#include <stdint.h>

void    dsmiStrCpy(char *dst, const char *src);                          /* 0DDF:000A */
void    dsmiStrNCpy(char *dst, const char *src, int32_t n);              /* 0DDF:0025 */
int16_t dsmiStrCmp(const char *s1, const char *s2);                      /* 0DDF:0044 */
int16_t dsmiStrLen(const char *s);                                       /* 0DDF:007D */
void    dsmiMemCpy(void *dst, const void *src, int32_t n);               /* 0DDF:0099 */
void    dsmiMemCpy2(void *dst, const void *src, int32_t n);              /* 0DDF:00BB */
int16_t dsmiMemCmp(const void *s1, const void *s2, int32_t n);           /* 0DDF:00DD */
void    dsmiMemSet(void *dst, uint8_t c, int32_t n);                     /* 0DDF:0105 */
int16_t dsmiAbs(int16_t v);                                              /* 0DDF:011C */

#endif
