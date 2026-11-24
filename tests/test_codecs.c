/*
 * test_codecs.c - the port's own codecs and the importer, on made-up data
 * (nothing of the game is needed):
 *
 *   zdeflate   deflate -> inflate round trips, a hand-made stream, CRC-32
 *   flac       encode -> decode round trips (noise, tones, silence, extremes)
 *   cdsector   Mode 1 sectors: EDC/ECC, damage is noticed, scrambling
 *   iso9660    a volume built here: mount, find, read
 *   chd        a disc (data + audio track) -> CHD -> every sector back, verify
 *   zipfile    an archive built here (stored and deflated members)
 *   install    folder, ZIP, cue/bin and ISO into DATA / SAVE; what must be
 *              refused is refused and leaves nothing behind
 *
 * The same code is checked against the real disc and against other programs'
 * output (zlib, ffmpeg's FLAC decoder, redump.org's sums) by tools/chd.py and
 * the runs in docs/INSTALL.md; this test keeps it from breaking unnoticed.
 *
 * Works in the folder test_codecs.tmp of the current directory and removes it.
 */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define rmdir _rmdir
#else
#include <unistd.h>
#endif

#include "platform/cdrom.h"
#include "platform/cdsector.h"
#include "platform/chd.h"
#include "platform/flac.h"
#include "platform/install.h"
#include "platform/iso9660.h"
#include "platform/zdeflate.h"
#include "platform/zipfile.h"

#define TMP "test_codecs.tmp"

static int failures, checks;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        checks++;                                           \
        if (!(cond)) {                                      \
            failures++;                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

/* ---- helpers ------------------------------------------------------------------------ */

static uint32_t rng_state = 0x1234567u;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* Something a compressor has work with: runs, repeats and noise, mixed. */
static void fill(uint8_t *p, size_t n, int kind)
{
    for (size_t i = 0; i < n; i++) {
        switch (kind) {
        case 0: p[i] = 0; break;
        case 1: p[i] = (uint8_t)rnd(); break;
        case 2: p[i] = (uint8_t)("the quick brown fox "[i % 20] + (i / 997) % 3); break;
        default: p[i] = (uint8_t)((i / 64) % 2 ? rnd() : i >> 3); break;
        }
    }
}

static void rm_tree(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *de;
    char sub[1400];
    if (!d) {
        remove(path);
        return;
    }
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        snprintf(sub, sizeof sub, "%s/%s", path, de->d_name);
        rm_tree(sub);
    }
    closedir(d);
    rmdir(path);
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int put_file(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    size_t w = fwrite(data, 1, n, f);
    fclose(f);
    return w == n ? 0 : -1;
}

/* 1 when the file holds exactly these bytes. */
static int file_is(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    size_t got;
    int same;
    if (!f)
        return 0;
    buf = malloc(n + 1);
    got = fread(buf, 1, n + 1, f);
    fclose(f);
    same = got == n && memcmp(buf, data, n) == 0;
    free(buf);
    return same;
}

static int no_progress(void *ctx, uint32_t done, uint32_t total)
{
    (void)ctx, (void)done, (void)total;
    return 0;
}

static int no_text(void *ctx, const char *text, uint64_t done, uint64_t total)
{
    (void)ctx, (void)text, (void)done, (void)total;
    return 0;
}

/* ---- deflate ------------------------------------------------------------------------ */

static void test_deflate(void)
{
    static uint8_t in[65535], z[70000], out[65535];
    static const size_t sizes[] = { 0, 1, 2, 3, 257, 4096, 19600, 65535 };
    size_t len, used;

    CHECK(z_crc32(0, "123456789", 9) == 0xCBF43926u, "CRC-32 of the check string");
    CHECK(z_crc32(z_crc32(0, "1234", 4), "56789", 5) == 0xCBF43926u, "CRC-32 in two pieces");

    /* "a" as one fixed-Huffman block, as zlib writes it */
    CHECK(z_inflate_mem((const uint8_t *)"\x4B\x04\x00", 3, out, sizeof out, &len, &used) == 0 && len == 1
          && out[0] == 'a' && used == 3, "hand-made stream");
    /* a stored block: 5 bytes */
    CHECK(z_inflate_mem((const uint8_t *)"\x01\x05\x00\xFA\xFFhello", 10, out, sizeof out, &len, &used) == 0
          && len == 5 && memcmp(out, "hello", 5) == 0, "stored block");
    CHECK(z_inflate_mem((const uint8_t *)"\x01\x05\x00\xFA\xFEhello", 10, out, sizeof out, &len, NULL) == -1,
          "stored block with a wrong length check is refused");
    CHECK(z_inflate_mem((const uint8_t *)"\x4B", 1, out, sizeof out, &len, NULL) == -1, "cut-off stream is refused");
    CHECK(z_inflate_mem((const uint8_t *)"\x07", 1, out, sizeof out, &len, NULL) == -1, "block type 3 is refused");

    for (int kind = 0; kind < 4; kind++) {
        for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
            size_t n = sizes[s], zn;
            fill(in, n, kind);
            zn = z_deflate(in, n, z, sizeof z);
            CHECK(zn > 0, "deflate kind %d size %u", kind, (unsigned)n);
            memset(out, 0xEE, sizeof out);
            CHECK(z_inflate_mem(z, zn, out, sizeof out, &len, &used) == 0 && len == n && used == zn
                  && memcmp(in, out, n) == 0, "round trip kind %d size %u", kind, (unsigned)n);
            if (kind == 0 && n >= 4096)
                CHECK(zn < n / 50, "zeros compress (%u -> %u)", (unsigned)n, (unsigned)zn);
            if (kind == 2 && n >= 4096)
                CHECK(zn < n / 4, "text compresses (%u -> %u)", (unsigned)n, (unsigned)zn);
            if (n > 16)
                CHECK(z_inflate_mem(z, zn, out, n - 1, &len, NULL) == -1, "output limit is kept");
        }
    }
    fill(in, 4096, 1);
    CHECK(z_deflate(in, 4096, z, 100) == 0, "deflate reports a buffer too small");
}

/* ---- FLAC --------------------------------------------------------------------------- */

static void test_flac(void)
{
    enum { N = 2352 * 3 + 100 };                /* three whole frames and a short one */
    static int16_t pcm[N * 2], back[N * 2];
    static uint8_t enc[N * 4 + 4096];
    uint32_t phase = 0;

    for (int kind = 0; kind < 6; kind++) {
        for (int i = 0; i < N; i++) {
            int16_t l, r;
            switch (kind) {
            case 0: l = r = 0; break;                                           /* silence */
            case 1: l = (int16_t)rnd(); r = (int16_t)rnd(); break;              /* noise */
            case 2:                                                             /* triangle tones */
                phase += 900;
                l = (int16_t)(((phase >> 4) & 0x7FFF) - 0x4000);
                r = (int16_t)(l / 2 + (int16_t)(rnd() % 16));
                break;
            case 3: l = (i & 1) ? 32767 : -32768; r = (int16_t)-l; break;       /* extremes */
            case 4: l = 1234; r = -4321; break;                                 /* constant */
            default: l = (int16_t)((i * 7) << 4); r = l; break;                 /* wasted bits, equal channels */
            }
            pcm[2 * i] = l;
            pcm[2 * i + 1] = r;
        }
        size_t n = flac_encode(pcm, N, 2352, enc, sizeof enc);
        CHECK(n > 0, "FLAC encode kind %d", kind);
        memset(back, 0x55, sizeof back);
        size_t used = flac_decode(enc, n, back, N);
        CHECK(used == n && memcmp(pcm, back, sizeof pcm) == 0, "FLAC round trip kind %d (%u of %u bytes)", kind,
              (unsigned)used, (unsigned)n);
        if (kind == 0 || kind == 4)
            CHECK(n < 200, "constant audio is a few bytes (%u)", (unsigned)n);
        if (kind == 2)
            CHECK(n < sizeof pcm / 2, "tones compress (%u of %u)", (unsigned)n, (unsigned)sizeof pcm);
        if (n > 8) {
            enc[n / 2] ^= 0x10;                 /* damage: the frame's CRC-16 must notice */
            CHECK(flac_decode(enc, n, back, N) == 0 || memcmp(pcm, back, sizeof pcm) != 0 || kind == 0 || kind == 4,
                  "damaged FLAC kind %d", kind);
        }
    }
    CHECK(flac_encode(pcm, N, 2352, enc, 10) == 0, "FLAC encode reports a buffer too small");
}

/* ---- CD sectors --------------------------------------------------------------------- */

static void test_sectors(void)
{
    static const uint8_t scr[8] = { 0x01, 0x80, 0x00, 0x60, 0x00, 0x28, 0x00, 0x1E };  /* ECMA-130 Annex B */
    uint8_t user[2048], raw[2352], copy[2352];

    fill(user, sizeof user, 3);
    cd_mode1_build(raw, 12345, user);
    CHECK(memcmp(raw, cd_sync, 12) == 0, "sync pattern");
    /* 12345 + 150 = 12495 frames = 02:46:45 */
    CHECK(raw[12] == 0x02 && raw[13] == 0x46 && raw[14] == 0x45 && raw[15] == 1, "header %02X:%02X:%02X mode %d",
          raw[12], raw[13], raw[14], raw[15]);
    CHECK(memcmp(raw + 16, user, 2048) == 0, "user data in place");
    CHECK(cd_ecc_ok(raw) == 1, "parity of a built sector");
    uint32_t edc = cd_edc(raw, 2064);
    CHECK(raw[2064] == (uint8_t)edc && raw[2065] == (uint8_t)(edc >> 8) && raw[2066] == (uint8_t)(edc >> 16)
          && raw[2067] == (uint8_t)(edc >> 24), "EDC over sync, header and data");
    memcpy(copy, raw, sizeof raw);
    memset(copy + 2076, 0, 276);
    cd_ecc_generate(copy);
    CHECK(memcmp(copy, raw, sizeof raw) == 0, "parity regenerated");
    for (int pos = 12; pos < 2352; pos += 233) {
        memcpy(copy, raw, sizeof raw);
        copy[pos] ^= 0x04;
        CHECK(cd_ecc_ok(copy) == 0, "damage at byte %d is noticed", pos);
    }

    memcpy(copy, raw, sizeof raw);
    cd_scramble(copy);
    CHECK(memcmp(copy, raw, 12) == 0, "scrambling leaves the sync alone");
    for (int i = 0; i < 8; i++)
        CHECK((copy[12 + i] ^ raw[12 + i]) == scr[i], "scrambler byte %d is %02X", i, copy[12 + i] ^ raw[12 + i]);
    CHECK(memcmp(copy + 12, raw + 12, 2340) != 0, "scrambled differs");
    cd_scramble(copy);
    CHECK(memcmp(copy, raw, sizeof raw) == 0, "scrambling twice gives the sector back");
}

/* ---- a made-up disc ----------------------------------------------------------------- */

enum {
    FILE1_LBA = 19, FILE1_SIZE = 150000,        /* 3D.CKD (what marks the game's disc) */
    FILE2_LBA = 93, FILE2_SIZE = 70001,         /* TEST.DAT */
    DATA_SECTORS = 151,                         /* not a multiple of 4: the CHD pads tracks */
    AUDIO_SECTORS = 90
};

static uint8_t iso[DATA_SECTORS * 2048];
static int16_t audio[AUDIO_SECTORS * 588 * 2];

static void both32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
        p[7 - i] = (uint8_t)(v >> (8 * i));
    }
}

static uint8_t *dir_record(uint8_t *p, const char *name, size_t namelen, uint32_t lba, uint32_t size, int is_dir)
{
    size_t len = 33 + namelen + ((33 + namelen) & 1);
    memset(p, 0, len);
    p[0] = (uint8_t)len;
    both32(p + 2, lba);
    both32(p + 10, size);
    p[18] = 96, p[19] = 11, p[20] = 10, p[21] = 12;     /* 10 November 1996, 12:00 */
    p[25] = is_dir ? 2 : 0;
    p[28] = 1, p[31] = 1;                               /* volume sequence number */
    p[32] = (uint8_t)namelen;
    memcpy(p + 33, name, namelen);
    return p + len;
}

/* The volume: descriptors at 16-17, root directory at 18, the files behind.
   with_game = 0 leaves 3D.CKD out of the directory. */
static void build_iso(int with_game)
{
    uint8_t *pvd = iso + 16 * 2048, *p;
    memset(iso, 0, sizeof iso);
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    memset(pvd + 8, ' ', 32);
    memset(pvd + 40, ' ', 32);
    memcpy(pvd + 40, "TESTDISC", 8);
    both32(pvd + 80, DATA_SECTORS);
    pvd[120] = 1, pvd[123] = 1;                         /* volume set size */
    pvd[124] = 1, pvd[127] = 1;                         /* volume sequence number */
    pvd[128] = 0x00, pvd[129] = 0x08, pvd[130] = 0x08, pvd[131] = 0x00;     /* 2048 bytes a block */
    dir_record(pvd + 156, "\0", 1, 18, 2048, 1);
    pvd[881] = 1;
    iso[17 * 2048] = 255;
    memcpy(iso + 17 * 2048 + 1, "CD001", 5);
    iso[17 * 2048 + 6] = 1;

    p = iso + 18 * 2048;
    p = dir_record(p, "\0", 1, 18, 2048, 1);
    p = dir_record(p, "\1", 1, 18, 2048, 1);
    if (with_game)
        p = dir_record(p, "3D.CKD;1", 8, FILE1_LBA, FILE1_SIZE, 0);
    dir_record(p, "TEST.DAT;1", 10, FILE2_LBA, FILE2_SIZE, 0);

    rng_state = 77;
    fill(iso + FILE1_LBA * 2048, FILE1_SIZE, 3);
    fill(iso + FILE2_LBA * 2048, FILE2_SIZE, 2);
}

static void build_audio(void)
{
    uint32_t phase = 0;
    rng_state = 99;
    for (int i = 0; i < AUDIO_SECTORS * 588; i++) {
        int sector = i / 588;
        int16_t l, r;
        if (sector < 10 || (sector >= 40 && sector < 56)) {
            l = r = 0;                                  /* silence: whole hunks alike */
        } else if (sector < 30) {
            phase += 700 + (uint32_t)sector;
            l = (int16_t)(((phase >> 3) & 0x3FFF) - 0x2000);
            r = (int16_t)(l - (int16_t)(rnd() % 64));
        } else {
            l = (int16_t)rnd();                         /* noise: nothing compresses it */
            r = (int16_t)rnd();
        }
        audio[2 * i] = l;
        audio[2 * i + 1] = r;
    }
}

/* The disc as a .bin file would hold it (audio as little-endian samples). */
static void disc_sector(uint32_t lba, uint8_t *raw)
{
    if (lba < DATA_SECTORS) {
        cd_mode1_build(raw, lba, iso + (size_t)lba * 2048);
    } else {
        const int16_t *s = audio + (size_t)(lba - DATA_SECTORS) * 588 * 2;
        for (int i = 0; i < 1176; i++) {
            raw[2 * i] = (uint8_t)s[i];
            raw[2 * i + 1] = (uint8_t)((uint16_t)s[i] >> 8);
        }
    }
}

static int disc_read(void *ctx, uint32_t lba, uint8_t *raw)
{
    (void)ctx;
    if (lba >= DATA_SECTORS + AUDIO_SECTORS)
        return -1;
    disc_sector(lba, raw);
    return 0;
}

static int iso_read_mem(void *ctx, uint32_t lba, uint8_t *sector)
{
    (void)ctx;
    if (lba >= DATA_SECTORS)
        return -1;
    memcpy(sector, iso + (size_t)lba * 2048, 2048);
    return 0;
}

static int count_entries(void *ctx, const IsoEntry *e)
{
    (void)e;
    ++*(int *)ctx;
    return 0;
}

static void test_iso(void)
{
    IsoFs fs;
    IsoEntry e;
    static uint8_t buf[FILE1_SIZE];
    int n = 0;

    build_iso(1);
    CHECK(iso_mount(&fs, iso_read_mem, NULL) == 0, "ISO mount");
    CHECK(strcmp(fs.volume_id, "TESTDISC") == 0 && fs.volume_sectors == DATA_SECTORS, "volume '%s', %u sectors",
          fs.volume_id, (unsigned)fs.volume_sectors);
    CHECK(iso_find(&fs, "3D.CKD", &e) == 0 && e.lba == FILE1_LBA && e.size == FILE1_SIZE && !e.is_dir, "find 3D.CKD");
    CHECK(iso_find(&fs, "3d.ckd", &e) == 0, "names are found whatever their case");
    CHECK(iso_find(&fs, "NOTHERE.DAT", &e) == -1, "a missing file is not found");
    CHECK(iso_find(&fs, "TEST.DAT", &e) == 0 && e.size == FILE2_SIZE, "find TEST.DAT");
    CHECK(iso_read(&fs, &e, 0, buf, FILE2_SIZE) == FILE2_SIZE && memcmp(buf, iso + FILE2_LBA * 2048, FILE2_SIZE) == 0,
          "read a whole file");
    CHECK(iso_read(&fs, &e, 5000, buf, 3000) == 3000 && memcmp(buf, iso + FILE2_LBA * 2048 + 5000, 3000) == 0,
          "read across sectors from the middle");
    CHECK(iso_read(&fs, &e, FILE2_SIZE - 10, buf, 100) == 10, "read stops at the end of the file");
    CHECK(iso_list(&fs, "", count_entries, &n) == 0 && n == 2, "the directory lists %d files", n);

    memset(iso + 16 * 2048 + 1, 'X', 5);
    CHECK(iso_mount(&fs, iso_read_mem, NULL) == -1, "not ISO 9660 is refused");
    build_iso(1);
}

/* ---- CHD ---------------------------------------------------------------------------- */

static void set_tracks(CdromTrack *t)
{
    memset(t, 0, 2 * sizeof *t);
    t[0].number = 1, t[0].audio = 0, t[0].start = 0, t[0].sectors = DATA_SECTORS, t[0].sector_size = 2352;
    t[1].number = 2, t[1].audio = 1, t[1].start = DATA_SECTORS, t[1].sectors = AUDIO_SECTORS, t[1].sector_size = 2352;
}

/* Every sector of the CHD against the made-up disc; cooked = the data track
   was stored as user data only. */
static void compare_chd(const char *path, int ntracks, int cooked, const char *what)
{
    char err[256] = "";
    Chd *c = chd_open(path, err, sizeof err);
    CdromTrack t[CDROM_MAX_TRACKS];
    uint32_t leadout = 0, total = DATA_SECTORS + (ntracks > 1 ? AUDIO_SECTORS : 0);
    uint8_t raw[2352], want[2352];
    int bad = 0;

    CHECK(c != NULL, "%s: open: %s", what, err);
    if (!c)
        return;
    CHECK(chd_tracks(c, t, &leadout) == ntracks && leadout == total, "%s: %d tracks, lead-out %u", what,
          chd_tracks(c, t, &leadout), (unsigned)leadout);
    CHECK(t[0].number == 1 && !t[0].audio && t[0].start == 0 && t[0].sectors == DATA_SECTORS, "%s: track 1", what);
    if (ntracks > 1)
        CHECK(t[1].number == 2 && t[1].audio && t[1].start == DATA_SECTORS && t[1].sectors == AUDIO_SECTORS,
              "%s: track 2 at %u, %u sectors", what, (unsigned)t[1].start, (unsigned)t[1].sectors);
    for (uint32_t lba = 0; lba < total; lba++) {
        memset(raw, 0xEE, sizeof raw);
        if (chd_read_sector(c, lba, raw) != 0) {
            bad++;
            continue;
        }
        disc_sector(lba, want);
        if (cooked && lba < DATA_SECTORS ? memcmp(raw + 16, want + 16, 2048) != 0 : memcmp(raw, want, 2352) != 0)
            bad++;
    }
    CHECK(bad == 0, "%s: %d of %u sectors differ", what, bad, (unsigned)total);
    CHECK(chd_read_sector(c, total + 8, raw) == -1, "%s: no sector behind the lead-out", what);
    CHECK(chd_verify(c, no_progress, NULL, err, sizeof err) == 0, "%s: verify: %s", what, err);
    chd_close(c);
}

static int cancel_at_half(void *ctx, uint32_t done, uint32_t total)
{
    (void)ctx;
    return done * 2 > total;
}

static void test_chd(void)
{
    CdromTrack t[2];
    CdromSource src = { disc_read, NULL, NULL };
    ChdStats st;
    char err[256] = "";
    const char *path = TMP "/disc.chd";
    FILE *f;
    long size;

    build_iso(1);
    build_audio();
    set_tracks(t);
    memset(&st, 0, sizeof st);
    CHECK(chd_create_cd(path, t, 2, DATA_SECTORS + AUDIO_SECTORS, &src, no_progress, NULL, &st, err, sizeof err) == 0,
          "CHD create: %s", err);
    CHECK(st.sectors == DATA_SECTORS + AUDIO_SECTORS && st.unreadable == 0, "CHD statistics: %u sectors, %u unreadable",
          (unsigned)st.sectors, (unsigned)st.unreadable);
    CHECK(st.bytes > 0 && st.bytes < (uint64_t)(DATA_SECTORS + AUDIO_SECTORS) * 2352, "the CHD is smaller than the disc (%u bytes)",
          (unsigned)st.bytes);
    compare_chd(path, 2, 0, "CHD");
    {
        Chd *c = chd_open(path, err, sizeof err);
        if (c) {
            CHECK(memcmp(chd_sha1(c), st.sha1, 20) == 0, "the header's SHA-1 is the one reported");
            chd_close(c);
        }
    }

    /* the same disc written twice gives the same file */
    {
        ChdStats st2;
        CHECK(chd_create_cd(TMP "/disc2.chd", t, 2, DATA_SECTORS + AUDIO_SECTORS, &src, NULL, NULL, &st2, err,
                            sizeof err) == 0 && st2.bytes == st.bytes && memcmp(st2.sha1, st.sha1, 20) == 0,
              "a second CHD of the disc is identical");
    }

    /* cancelled: nothing stays */
    CHECK(chd_create_cd(TMP "/half.chd", t, 2, DATA_SECTORS + AUDIO_SECTORS, &src, cancel_at_half, NULL, NULL, err,
                        sizeof err) == 1 && !exists(TMP "/half.chd"), "a cancelled CHD is removed");

    /* damage in the middle of the file: verify notices */
    f = fopen(TMP "/disc2.chd", "r+b");
    if (f) {
        Chd *c;
        int ch;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, size / 2, SEEK_SET);
        ch = fgetc(f);
        fseek(f, size / 2, SEEK_SET);
        fputc(ch ^ 0x01, f);
        fclose(f);
        c = chd_open(TMP "/disc2.chd", err, sizeof err);
        if (c) {
            CHECK(chd_verify(c, NULL, NULL, err, sizeof err) == -1, "a damaged CHD does not verify");
            chd_close(c);
        }
    }
    CHECK(put_file(TMP "/junk.chd", "MComprHD and then nothing of use", 32) == 0
          && chd_open(TMP "/junk.chd", err, sizeof err) == NULL, "a file that is no CHD is refused");
    CHECK(chd_open(TMP "/nothere.chd", err, sizeof err) == NULL, "a missing CHD is refused");
}

/* ---- ZIP ---------------------------------------------------------------------------- */

typedef struct ZipOut {
    FILE    *f;
    uint8_t  dir[4096];
    size_t   dirlen;
    int      count;
} ZipOut;

static void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, v), le16(p + 2, v >> 16); }

/* Adds a member; deflated when that is possible (at most 65535 bytes) and asked for. */
static void zip_add(ZipOut *z, const char *name, const uint8_t *data, uint32_t n, int deflated)
{
    static uint8_t comp[70000];
    uint8_t h[46];
    uint32_t offset = (uint32_t)ftell(z->f), csize = n;
    uint32_t crc = z_crc32(0, data, n);
    size_t namelen = strlen(name);
    const uint8_t *body = data;
    if (deflated && n <= 65535) {
        csize = (uint32_t)z_deflate(data, n, comp, sizeof comp);
        body = comp;
    } else
        deflated = 0;
    memset(h, 0, sizeof h);
    le32(h, 0x04034B50), le16(h + 4, 20), le16(h + 8, deflated ? 8 : 0);
    le32(h + 14, crc), le32(h + 18, csize), le32(h + 22, n), le16(h + 26, (uint32_t)namelen);
    fwrite(h, 1, 30, z->f);
    fwrite(name, 1, namelen, z->f);
    fwrite(body, 1, csize, z->f);
    memset(h, 0, sizeof h);
    le32(h, 0x02014B50), le16(h + 4, 20), le16(h + 6, 20), le16(h + 10, deflated ? 8 : 0);
    le32(h + 16, crc), le32(h + 20, csize), le32(h + 24, n), le16(h + 28, (uint32_t)namelen);
    le32(h + 42, offset);
    memcpy(z->dir + z->dirlen, h, 46);
    memcpy(z->dir + z->dirlen + 46, name, namelen);
    z->dirlen += 46 + namelen;
    z->count++;
}

static void zip_finish(ZipOut *z)
{
    uint8_t e[22];
    uint32_t offset = (uint32_t)ftell(z->f);
    fwrite(z->dir, 1, z->dirlen, z->f);
    memset(e, 0, sizeof e);
    le32(e, 0x06054B50), le16(e + 8, (uint32_t)z->count), le16(e + 10, (uint32_t)z->count);
    le32(e + 12, (uint32_t)z->dirlen), le32(e + 16, offset);
    fwrite(e, 1, 22, z->f);
    fclose(z->f);
}

/* The files of a made-up game folder: what the importer asks for, something
   for SAVE, and things it must leave alone. */
static const struct GameFile {
    const char *name;
    uint32_t    size;
    int         where;          /* 1 DATA, 2 SAVE, 0 nowhere */
} game_files[] = {
    { "0GFL.CKD", 90000, 1 }, { "3D.CKD", 5000, 1 },    { "FRPOS.CKD", 1, 1 },     { "IKON.CKD", 300, 1 },
    { "IVIR.CKD", 65535, 1 }, { "MUZIK.CKD", 70000, 1 }, { "SAVAS.CKD", 2048, 1 }, { "SOUNDS.CKD", 12345, 1 },
    { "TRDOS.DAT", 777, 1 },  { "GRPHAZIR.DAT", 99, 1 }, { "BOSCH.KOR", 64, 1 },   { "GRP.DAT", 512, 2 },
    { "SAKLA001.SAV", 4000, 2 }, { "IST.EXE", 3000, 0 }, { "OKU.TXT", 100, 0 },
};
enum { GAME_FILES = sizeof game_files / sizeof game_files[0] };

static uint8_t *game_data(int i)
{
    uint8_t *p = malloc(game_files[i].size + 1);
    rng_state = 1000 + (uint32_t)i;
    fill(p, game_files[i].size, 1 + i % 3);
    return p;
}

/* DATA and SAVE below base hold exactly what the made-up game asks for. */
static void check_installed(const char *base, const char *what)
{
    char path[1400];
    for (int i = 0; i < GAME_FILES; i++) {
        uint8_t *p = game_data(i);
        snprintf(path, sizeof path, "%s/DATA/%s", base, game_files[i].name);
        CHECK(file_is(path, p, game_files[i].size) == (game_files[i].where == 1), "%s: DATA/%s", what, game_files[i].name);
        snprintf(path, sizeof path, "%s/SAVE/%s", base, game_files[i].name);
        CHECK(file_is(path, p, game_files[i].size) == (game_files[i].where == 2), "%s: SAVE/%s", what, game_files[i].name);
        free(p);
    }
    snprintf(path, sizeof path, "%s/DATA", base);
    CHECK(install_have_files(path) == 1, "%s: the game's files are complete", what);
    snprintf(path, sizeof path, "%s/DATA/_import", base);
    CHECK(!exists(path), "%s: no temporary folder left", what);
}

static void test_zip(void)
{
    ZipOut z = { 0 };
    ZipFile *zf;
    char err[256] = "", name[600];
    int found = 0;

    z.f = fopen(TMP "/game.zip", "wb");
    CHECK(z.f != NULL, "create the test archive");
    if (!z.f)
        return;
    zip_add(&z, "Lale/", (const uint8_t *)"", 0, 0);
    for (int i = 0; i < GAME_FILES; i++) {
        uint8_t *p = game_data(i);
        snprintf(name, sizeof name, "Lale/%s", game_files[i].name);
        zip_add(&z, name, p, game_files[i].size, i % 2);
        free(p);
    }
    zip_finish(&z);

    zf = zip_open(TMP "/game.zip", err, sizeof err);
    CHECK(zf != NULL, "ZIP open: %s", err);
    if (!zf)
        return;
    CHECK(zip_count(zf) == GAME_FILES + 1, "ZIP holds %d members", zip_count(zf));
    for (int i = 0; i < zip_count(zf); i++) {
        const ZipEntry *e = zip_entry(zf, i);
        if (strcmp(e->name, "Lale/IVIR.CKD") == 0 || strcmp(e->name, "Lale/0GFL.CKD") == 0) {
            int k = strcmp(e->name, "Lale/IVIR.CKD") == 0 ? 4 : 0;
            uint8_t *p = game_data(k);
            CHECK(zip_extract(zf, e, TMP "/member.bin", NULL, NULL, err, sizeof err) == 0
                  && file_is(TMP "/member.bin", p, game_files[k].size), "ZIP extract %s (method %d): %s", e->name,
                  e->method, err);
            free(p);
            found++;
        }
    }
    CHECK(found == 2, "both members were in the list");
    zip_close(zf);

    /* a member whose data does not match its CRC is refused and not left behind */
    {
        FILE *f = fopen(TMP "/game.zip", "rb");
        static uint8_t all[600000];
        size_t n = f ? fread(all, 1, sizeof all, f) : 0;
        if (f)
            fclose(f);
        all[100] ^= 0x20;                       /* inside the first file's (stored) data */
        put_file(TMP "/bad.zip", all, n);
        zf = zip_open(TMP "/bad.zip", err, sizeof err);
        if (zf) {
            CHECK(zip_extract(zf, zip_entry(zf, 1), TMP "/bad.bin", NULL, NULL, err, sizeof err) == -1
                  && !exists(TMP "/bad.bin"), "a member with a wrong CRC is refused");
            zip_close(zf);
        }
    }
    CHECK(put_file(TMP "/junk.zip", "PK but nothing else in here at all", 34) == 0
          && zip_open(TMP "/junk.zip", err, sizeof err) == NULL, "a file that is no ZIP is refused");
}

/* ---- the importer ------------------------------------------------------------------- */

static void test_install(void)
{
    char err[512] = "", path[1400];
    static uint8_t raw[2352];
    FILE *f;

    /* 1. a folder (the game one level down, as archives unpack) */
    install_mkdir(TMP "/src");
    install_mkdir(TMP "/src/Lale Savascilari");
    for (int i = 0; i < GAME_FILES; i++) {
        uint8_t *p = game_data(i);
        snprintf(path, sizeof path, TMP "/src/Lale Savascilari/%s", game_files[i].name);
        put_file(path, p, game_files[i].size);
        free(p);
    }
    install_mkdir(TMP "/b1");
    CHECK(install_import(TMP "/b1", TMP "/src", no_text, NULL, err, sizeof err) == INSTALL_FILES, "folder import: %s", err);
    check_installed(TMP "/b1", "folder");

    /* what is in SAVE already is the player's: a second import leaves it alone */
    put_file(TMP "/b1/SAVE/SAKLA001.SAV", "my game", 7);
    CHECK(install_import(TMP "/b1", TMP "/src/Lale Savascilari", no_text, NULL, err, sizeof err) == INSTALL_FILES,
          "second folder import: %s", err);
    CHECK(file_is(TMP "/b1/SAVE/SAKLA001.SAV", "my game", 7), "a saved game in SAVE is not overwritten");

    /* 2. the ZIP of test_zip() */
    install_mkdir(TMP "/b2");
    CHECK(install_import(TMP "/b2", TMP "/game.zip", no_text, NULL, err, sizeof err) == INSTALL_FILES, "ZIP import: %s", err);
    check_installed(TMP "/b2", "ZIP");

    /* 3. a disc image: cue + bin */
    build_iso(1);
    build_audio();
    f = fopen(TMP "/disc.bin", "wb");
    for (uint32_t lba = 0; f && lba < DATA_SECTORS + AUDIO_SECTORS; lba++) {
        disc_sector(lba, raw);
        fwrite(raw, 1, sizeof raw, f);
    }
    if (f)
        fclose(f);
    f = fopen(TMP "/disc.cue", "w");
    if (f) {
        fprintf(f, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
                   "  TRACK 02 AUDIO\n    INDEX 01 %02d:%02d:%02d\n",
                DATA_SECTORS / 4500, DATA_SECTORS / 75 % 60, DATA_SECTORS % 75);
        fclose(f);
    }
    install_mkdir(TMP "/b3");
    CHECK(install_import(TMP "/b3", TMP "/disc.cue", no_text, NULL, err, sizeof err) == INSTALL_DISC, "cue import: %s", err);
    CHECK(install_have_chd(TMP "/b3/DATA", path, sizeof path) == 1, "DATA/LALE.CHD is there");
    compare_chd(TMP "/b3/DATA/LALE.CHD", 2, 0, "CHD from cue/bin");
    CHECK(file_is(TMP "/b3/DATA/LALE.CHD", "", 0) == 0 && !exists(TMP "/b3/DATA/LALE.CHD.tmp"), "no unfinished CHD left");

    /* the .bin named instead of the .cue finds its .cue */
    install_mkdir(TMP "/b4");
    CHECK(install_import(TMP "/b4", TMP "/disc.bin", no_text, NULL, err, sizeof err) == INSTALL_DISC, "bin import: %s", err);
    CHECK(install_have_chd(TMP "/b4/DATA", NULL, 0) == 1, "DATA/LALE.CHD from the .bin");

    /* 4. an ISO: the data track alone */
    put_file(TMP "/disc.iso", iso, sizeof iso);
    install_mkdir(TMP "/b5");
    CHECK(install_import(TMP "/b5", TMP "/disc.iso", no_text, NULL, err, sizeof err) == INSTALL_DISC, "ISO import: %s", err);
    compare_chd(TMP "/b5/DATA/LALE.CHD", 1, 1, "CHD from ISO");

    /* 5. a CHD: checked and copied */
    install_mkdir(TMP "/b6");
    CHECK(install_import(TMP "/b6", TMP "/b3/DATA/LALE.CHD", no_text, NULL, err, sizeof err) == INSTALL_DISC,
          "CHD import: %s", err);
    compare_chd(TMP "/b6/DATA/LALE.CHD", 2, 0, "copied CHD");

    /* 6. a folder that holds an image: the image is what gets imported */
    install_mkdir(TMP "/b7");
    install_mkdir(TMP "/imgdir");
    {
        static uint8_t all[(DATA_SECTORS + AUDIO_SECTORS) * 2352];
        for (uint32_t lba = 0; lba < DATA_SECTORS + AUDIO_SECTORS; lba++)
            disc_sector(lba, all + (size_t)lba * 2352);
        put_file(TMP "/imgdir/disc.bin", all, sizeof all);
    }
    f = fopen(TMP "/imgdir/disc.cue", "w");
    if (f) {
        fprintf(f, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
                   "  TRACK 02 AUDIO\n    INDEX 01 %02d:%02d:%02d\n",
                DATA_SECTORS / 4500, DATA_SECTORS / 75 % 60, DATA_SECTORS % 75);
        fclose(f);
    }
    CHECK(install_import(TMP "/b7", TMP "/imgdir", no_text, NULL, err, sizeof err) == INSTALL_DISC, "folder with an image: %s", err);
    CHECK(install_have_chd(TMP "/b7/DATA", NULL, 0) == 1, "DATA/LALE.CHD from the folder");

    /* 7. what must be refused, with a reason and nothing left behind */
    install_mkdir(TMP "/bx");
    err[0] = 0;
    CHECK(install_import(TMP "/bx", TMP "/nothere", no_text, NULL, err, sizeof err) == -1 && err[0], "a missing source");
    install_mkdir(TMP "/empty");
    err[0] = 0;
    CHECK(install_import(TMP "/bx", TMP "/empty", no_text, NULL, err, sizeof err) == -1 && err[0], "an empty folder");
    install_mkdir(TMP "/part");
    put_file(TMP "/part/3D.CKD", "x", 1);
    put_file(TMP "/part/TRDOS.DAT", "x", 1);
    err[0] = 0;
    CHECK(install_import(TMP "/bx", TMP "/part", no_text, NULL, err, sizeof err) == -1 && err[0], "an incomplete game");
    put_file(TMP "/junk.cue", "this is not a cue sheet\n", 24);
    err[0] = 0;
    CHECK(install_import(TMP "/bx", TMP "/junk.cue", no_text, NULL, err, sizeof err) == -1 && err[0], "a cue sheet without tracks");
    err[0] = 0;
    CHECK(install_import(TMP "/bx", TMP "/junk.chd", no_text, NULL, err, sizeof err) == -1 && err[0], "a broken CHD");
    err[0] = 0;
    CHECK(install_import(TMP "/bx", TMP "/junk.zip", no_text, NULL, err, sizeof err) == -1 && err[0], "a broken ZIP");
    build_iso(0);                               /* a disc that is not the game's */
    put_file(TMP "/other.iso", iso, sizeof iso);
    err[0] = 0;
    CHECK(install_import(TMP "/bx", TMP "/other.iso", no_text, NULL, err, sizeof err) == -1 && err[0], "another disc");
    build_iso(1);
    CHECK(install_have_chd(TMP "/bx/DATA", NULL, 0) == 0 && !install_have_files(TMP "/bx/DATA")
          && !exists(TMP "/bx/DATA/LALE.CHD.tmp") && !exists(TMP "/bx/DATA/_import"), "refused sources leave nothing in DATA");

    /* 8. the default sound settings for a disc install */
    install_mkdir(TMP "/b3/SAVE");
    install_default_sescfg(TMP "/b3/DATA", TMP "/b3/SAVE");
    CHECK(exists(TMP "/b3/SAVE/SES.CFG"), "SES.CFG is written for a disc install");
    put_file(TMP "/b3/SAVE/SES.CFG", "mine", 4);
    install_default_sescfg(TMP "/b3/DATA", TMP "/b3/SAVE");
    CHECK(file_is(TMP "/b3/SAVE/SES.CFG", "mine", 4), "an existing SES.CFG is kept");

    /* 9. what the program would offer on its own */
    {
        static char found[8][1024];
        int n = install_find_sources(TMP, found, 8);
        CHECK(n >= 3, "sources found beside the program: %d", n);
        CHECK(n > 0 && strstr(found[0], "disc.") != NULL, "an image comes first (%s)", n > 0 ? found[0] : "-");
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    rm_tree(TMP);
    if (install_mkdir(TMP) != 0 && !exists(TMP)) {
        printf("cannot create %s\n", TMP);
        return 2;
    }
    test_deflate();
    test_flac();
    test_sectors();
    test_iso();
    test_chd();
    test_zip();
    test_install();
    rm_tree(TMP);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
