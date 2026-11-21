/*
 * dsmistr.c - DSMI string/memory helpers (IST.EXE seg0DDF, DSMI library).
 *
 * All are far Pascal procedures written in 386 assembly.  Common traits kept
 * here:
 *   - counts are passed as 32-bit longs, but the string instructions run with
 *     16-bit addressing, so only CX (the low 16 bits) counts - except in
 *     dsmiMemCmp, whose JECXZ has an address-size prefix and tests all of ECX;
 *   - pointers are far: a run past 64 KB would wrap SI/DI inside the segment.
 *     No caller in the game gets near that; the port uses flat pointers.
 */
#include "dsmi/dsmistr.h"

/*
 * dsmiStrCpy - copy a NUL-terminated string, terminator included.
 *
 * Original: IST.EXE 0DDF:000A, far pascal (dst, src).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
void dsmiStrCpy(char *dst, const char *src)
{
    char c;
    do {
        c = *src++;
        *dst++ = c;
    } while (c != 0);
}

/*
 * dsmiStrNCpy - copy at most n bytes of src, stopping after the NUL.
 *
 * Unlike strncpy it never pads with NULs.  The LOOPNZ runs on CX: only the
 * low 16 bits of n count and a count of 0 means 65536.
 *
 * Original: IST.EXE 0DDF:0025, far pascal (dst, src, long n) - unreferenced.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
void dsmiStrNCpy(char *dst, const char *src, int32_t n)
{
    uint16_t cx = (uint16_t)n;
    char c;
    do {
        c = *src++;
        *dst++ = c;
        cx--;                               /* LOOPNZ: dec CX, loop if CX != 0 && c != 0 */
    } while (cx != 0 && c != 0);
}

/*
 * dsmiStrCmp - compare two strings for the DDS name lookups.
 *
 * Returns 0 if equal.  Otherwise the value is unusual (only "== 0" is ever
 * tested): 1 when s2 ends first, -1 when s1 ends first, else the byte
 * difference s1[i] - s2[i] taken as an UNSIGNED byte (1..255, never negative).
 *
 * Original: IST.EXE 0DDF:0044, far pascal (s1, s2); s1 is ES:DI (BH),
 * s2 DS:SI (BL).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t dsmiStrCmp(const char *s1, const char *s2)
{
    for (;;) {
        uint8_t bl = (uint8_t)*s2++;
        uint8_t bh = (uint8_t)*s1++;
        if (bl == 0)
            return bh == 0 ? 0 : 1;
        if (bh == 0)
            return -1;                      /* bl != 0 here, so never 0 */
        bh = (uint8_t)(bh - bl);
        if (bh != 0)
            return (int16_t)bh;             /* MOVZX AX, BH */
    }
}

/*
 * dsmiStrLen - length of a string PLUS ONE.
 *
 * The routine is `mov cx,-1 / repne scasb / neg cx / add cx,-1`: NEG instead
 * of the usual NOT makes it return strlen(s) + 1.  Callers add another 1 for
 * the terminator, so DDS allocates one spare byte per name.  If no NUL is
 * found within 65535 bytes, CX ends at 0 and the result is 0xFFFF.
 *
 * Original: IST.EXE 0DDF:007D, far pascal (s).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t dsmiStrLen(const char *s)
{
    uint16_t cx = 0xFFFF;
    do {
        cx--;                               /* REPNE SCASB: one byte per step */
    } while (*s++ != 0 && cx != 0);
    cx = (uint16_t)-cx;
    return (int16_t)(uint16_t)(cx - 1);
}

/* REP MOVSW over CX/2 words, then REP MOVSB for the odd byte (CF of SHR). */
static void memcpy_words(uint8_t *d, const uint8_t *s, int32_t n)
{
    uint16_t cx = (uint16_t)n;
    uint16_t words = (uint16_t)(cx >> 1);
    while (words--) {                       /* word at a time: overlap behaves like MOVSW */
        uint8_t lo = s[0], hi = s[1];
        d[0] = lo;
        d[1] = hi;
        d += 2;
        s += 2;
    }
    if (cx & 1)
        *d = *s;
}

/*
 * dsmiMemCpy - forward copy of n bytes (low 16 bits of n), word-wise.
 *
 * Original: IST.EXE 0DDF:0099, far pascal (dst, src, long n).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
void dsmiMemCpy(void *dst, const void *src, int32_t n)
{
    memcpy_words(dst, src, n);
}

/*
 * dsmiMemCpy2 - byte-for-byte twin of dsmiMemCpy (saves DS twice instead of
 * DI, so it clobbers the caller's DI - irrelevant in C).
 *
 * Original: IST.EXE 0DDF:00BB, far pascal (dst, src, long n) - unreferenced.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
void dsmiMemCpy2(void *dst, const void *src, int32_t n)
{
    memcpy_words(dst, src, n);
}

/*
 * dsmiMemCmp - compare n bytes; 0 equal, -1 / 1 otherwise.
 *
 * `sub ax,ax / repe cmpsb / jecxz done / mov ax,-1 / jl done / mov ax,1`.
 * Quirks kept:
 *   - REPE runs on CX (low 16 bits of n) but JECXZ (67h prefix) tests ECX:
 *     if the first difference is the LAST byte, CX is 0 and the blocks are
 *     reported equal; if n >= 65536 ECX never reaches 0 and the result is
 *     never 0 (1 when all compared bytes are equal);
 *   - the sign is a SIGNED byte compare (JL): -1 if (int8)s1[i] < (int8)s2[i].
 *
 * Original: IST.EXE 0DDF:00DD, far pascal (s1, s2, long n) - unreferenced.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t dsmiMemCmp(const void *s1, const void *s2, int32_t n)
{
    const uint8_t *a = s1, *b = s2;
    uint32_t ecx = (uint32_t)n;
    uint16_t cx = (uint16_t)ecx;
    int less = 0;                           /* flags of SUB AX,AX: JL not taken */
    while (cx != 0) {
        int8_t x = (int8_t)*a++, y = (int8_t)*b++;   /* CMPSB: [DS:SI] - [ES:DI] */
        cx--;
        less = x < y;
        if (x != y)
            break;
    }
    ecx = (ecx & 0xFFFF0000u) | cx;
    if (ecx == 0)
        return 0;
    return less ? -1 : 1;
}

/*
 * dsmiMemSet - fill n bytes (low 16 bits of n) with c.
 *
 * Original: IST.EXE 0DDF:0105, far pascal (dst, int c, long n).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
void dsmiMemSet(void *dst, uint8_t c, int32_t n)
{
    uint8_t *d = dst;
    uint16_t cx = (uint16_t)n;
    while (cx--)
        *d++ = c;
}

/*
 * dsmiAbs - absolute value via the sign mask (abs(-32768) = -32768).
 *
 * Original: IST.EXE 0DDF:011C, far pascal (int v) - unreferenced.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t dsmiAbs(int16_t v)
{
    int16_t m = (int16_t)(v >> 15);
    return (int16_t)((v ^ m) - m);
}
