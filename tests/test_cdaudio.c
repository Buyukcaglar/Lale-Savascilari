/*
 * test_cdaudio.c - virtual CD drive + decompiled ovl5737 module.
 *
 * Builds a small disc in the working directory:
 *   track 1  MODE1/2352  300 sectors   (file t1.bin)
 *   track 2  AUDIO       200 sectors   (file t2.bin)
 *   track 3  AUDIO       100 sectors   (file t3.bin)
 * Every audio frame holds L = LBA & 0x7FFF, R = frame-in-sector, so the
 * rendered PCM shows exactly which sector the drive is playing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/cd5737.h"
#include "platform/cdrom.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void write_track(const char *name, uint32_t lba0, uint32_t sectors, int audio)
{
    FILE *f = fopen(name, "wb");
    uint8_t s[2352];
    for (uint32_t i = 0; i < sectors; i++) {
        memset(s, audio ? 0 : 0x55, sizeof s);
        if (audio)
            for (int k = 0; k < 588; k++) {
                uint16_t l = (uint16_t)((lba0 + i) & 0x7FFF), r = (uint16_t)k;
                s[4 * k] = (uint8_t)l; s[4 * k + 1] = (uint8_t)(l >> 8);
                s[4 * k + 2] = (uint8_t)r; s[4 * k + 3] = (uint8_t)(r >> 8);
            }
        fwrite(s, 1, sizeof s, f);
    }
    fclose(f);
}

static void make_disc(void)
{
    write_track("t1.bin", 0, 300, 0);
    write_track("t2.bin", 300, 200, 1);
    write_track("t3.bin", 500, 100, 1);
    FILE *c = fopen("test.cue", "w");
    fputs("FILE \"t1.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
          "FILE \"t2.bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n"
          "FILE \"t3.bin\" BINARY\n  TRACK 03 AUDIO\n    INDEX 01 00:00:00\n", c);
    fclose(c);
    /* the same disc as one file with INDEX 00 pregap inside track 2 */
    FILE *one = fopen("one.bin", "wb");
    const char *parts[] = { "t1.bin", "t2.bin", "t3.bin" };
    for (int i = 0; i < 3; i++) {
        FILE *p = fopen(parts[i], "rb");
        int ch;
        while ((ch = fgetc(p)) != EOF) fputc(ch, one);
        fclose(p);
    }
    fclose(one);
    c = fopen("one.cue", "w");
    fputs("FILE \"one.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
          "  TRACK 02 AUDIO\n    INDEX 00 00:04:00\n    INDEX 01 00:04:10\n"
          "  TRACK 03 AUDIO\n    INDEX 01 00:06:50\n", c);
    fclose(c);
}

static void render_sectors(uint32_t n)
{
    static int16_t buf[588 * 2];
    for (uint32_t i = 0; i < n; i++)
        cdrom_render(buf, 588);
}

int main(void)
{
    make_disc();

    /* --- module inactive (the original IST.EXE never calls cd_init) --- */
    CHECK(o5737_cd_play_track(2, -1) == 0);
    CHECK(o5737_cd_stop() == 0);
    CHECK(cd5737_active() == 0);
    CHECK(o5737_msf_to_sectors(0x000200) == 0);            /* 00:02:00 */
    CHECK(o5737_msf_to_sectors(0x010000) == 4350);
    CHECK(o5737_msf_to_sectors(0x000000) == 0xFFFFFF6Au);  /* -150, 32-bit wrap */

    /* --- cue parsing --- */
    CHECK(cdrom_open("one.cue") == 0);
    CHECK(cdrom_track_count() == 3);
    CHECK(cdrom_track(2)->start == 310 && cdrom_track(2)->sectors == 190);
    CHECK(cdrom_track(3)->start == 500 && cdrom_track(3)->sectors == 100);
    CHECK(cdrom_leadout() == 600);

    /* --- option on --- */
    CHECK(cd5737_port_enable("missing.cue") == -1);
    CHECK(cd5737_port_enable("test.cue") == 0);
    CHECK(cd5737_active() == 1);
    CHECK(cdrom_leadout() == 600);

    /* track 2: length = start(3) - start(2) */
    CHECK(o5737_cd_play_track(2, -1) == 1);
    CHECK(cd5737_start() == 300 && cd5737_length() == 200);
    CHECK(cdrom_playing());
    CHECK(o5737_cd_play() == 0);           /* head still at start: pos <= start */
    render_sectors(2);
    CHECK(o5737_cd_play() == 1);
    {
        int16_t b[4];
        cdrom_render(b, 2);                /* first frames of sector 302 */
        CHECK(b[0] == 302 && b[1] == 0 && b[2] == 302 && b[3] == 1);
    }
    render_sectors(193);                   /* 302 + 2 frames + 193 sectors -> 495 */
    CHECK(o5737_cd_play() == 1);           /* 495 < 300 + 200 - 4 */
    render_sectors(1);                     /* 496 = end - 4 */
    CHECK(o5737_cd_play() == 0);

    /* last track: length = volume size - start - 150 */
    CHECK(o5737_cd_play_track(3, -1) == 1);
    CHECK(cd5737_start() == 500 && cd5737_length() == 100);
    render_sectors(120);                   /* runs out: drive stops at the end */
    CHECK(!cdrom_playing());
    CHECK(cdrom_position() == 600);
    CHECK(o5737_cd_play() == 0);

    /* track 0: replay the remembered start/length (menu loop) */
    CHECK(o5737_cd_play_track(0, -1) == 1);
    CHECK(cdrom_playing() && cdrom_position() == 500);

    /* explicit length */
    CHECK(o5737_cd_play_track(2, 10) == 1);
    CHECK(cd5737_length() == 10);
    render_sectors(10);
    CHECK(!cdrom_playing() && cdrom_position() == 310);

    /* stop pauses a playing track */
    CHECK(o5737_cd_play_track(2, -1) == 1);
    render_sectors(1);
    CHECK(o5737_cd_stop() == 1);
    CHECK(!cdrom_playing() && cdrom_position() == 301);

    /* a track number past the disc: the driver reports an error and the
     * original passes it on unchanged as start = -150 (msf 0 -> wraps) */
    CHECK(o5737_cd_play_track(9, 5) == 0);

    cdrom_close();
    if (!failures)
        printf("test_cdaudio: all checks passed\n");
    return failures ? 1 : 0;
}
