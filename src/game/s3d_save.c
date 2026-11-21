/*
 * s3d_save.c - 3DSYS.L part 5 (IST.EXE ovl303F:3DD0-4352): writing and
 * reading a saved game (saklaNNN.sav, 1524 bytes; layout in
 * docs/FORMATS.md "Saved game").  See s3d.h.
 */
#include "game/s3d.h"

#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/memalloc.h"
#include "engine/fatal.h"
#include "engine/fileman.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/vfs.h"
#include "game/cd5737.h"
#include "game/s3d_ext.h"
#include "rtl/bc_rand.h"

/* the blocks after the description and the size word, in file order */
static const struct { uint16_t off, len; } save_blocks[] = {
    { 0x99A6, 0x1CC },  /* 5 x 5Ch party records */
    { 0x9C26, 0x2E4 },  /* 5 x 94h names / character data */
    { 0x98FC, 0xAA },   /* 5 x 22h */
    { 0xBB34, 7 },      /* options */
    { 0x3D5E, 2 },      /* state bits (skipped when reading) */
    { 0xB9F3, 2 },      /* x */
    { 0xB9F1, 2 },      /* y */
    { 0xB9F0, 1 },      /* facing */
    { 0xBB6B, 2 },      /* district */
    { 0xBB4D, 2 },      /* music track */
    { 0xBB6D, 8 },      /* BB73 = money */
    { 0x3D64, 2 },      /* minute */
    { 0x3D66, 2 },      /* hour */
    { 0x3D68, 2 },      /* day */
    { 0xBBB7, 0xB },
    { 0xBBAC, 0xB },
    { 0xBB8A, 2 },      /* city map marker x */
    { 0xBB88, 2 },      /* y */
    { 0xBBAB, 2 },      /* doctor's fee: a byte, written as a word (+ BBAC[0]) */
};
#define N_BLOCKS ((int)(sizeof save_blocks / sizeof save_blocks[0]))

/*
 * s3d_write_save - write the saved game `name`: 3Ch bytes of `desc` (all
 * of them, also what follows its NUL), the word 20h (length of the next
 * block), the 32 event flag bytes and the blocks above.
 *
 * Original: IST.EXE 303F:3DD0 (3DSYS.L), far cdecl (char far *name, char
 * far *desc).  An open failure is fatal (3DSYS.L line 1749).
 * Verified: tests/py/test_s3d_save.py (test_write_save).
 */
void s3d_write_save(const char *name, const char *desc)
{
    int16_t size = 0x20;
    int16_t fd = vfs_open(name, 4);
    int i;

    if (fd == -1)
        o559A_fatal(DGSTR(0x42C0), 0x6D5, DGSTR(0x42CF), -1);  /* "Saklama dosyası açılma hatası ..." */
    vfs_write(fd, desc, 0x3C);
    vfs_write(fd, &size, 2);
    vfs_write(fd, DGP(0xB9CA), 0x20);
    for (i = 0; i < N_BLOCKS; i++)
        vfs_write(fd, DGP(save_blocks[i].off), save_blocks[i].len);
    vfs_close(fd);
}

/*
 * s3d_read_save - load the saved game `name` and enter it: fade out, stop
 * the music and the CD, restore the screen under the menu, read the file
 * (the description is skipped, the flag block has the length stored in
 * the file, the state bits are read and dropped), roll a new doctor's fee
 * only if the last read came up short (vfs_read returns 0 on success, so
 * a complete save keeps its fee), load the four portraits ("s" + portrait name, pictures 47h-4Ah), reset
 * the voices, load the district, set the view size and animation speed
 * from the options and enter the view.
 *
 * Original: IST.EXE 303F:3F95 (3DSYS.L), far cdecl (char far *name).  An
 * open failure is fatal (line 1796).  The portrait loop counter is the
 * global D5EC.  The local name buffer is first initialised from "s" at
 * 3EDD (13 bytes) and then overwritten by strcpy - no visible effect.
 * Verified: tests/py/test_s3d_save.py (test_read_save: orig/SAKLA001.SAV and a
 * port-written save, district + view entered).
 */
void s3d_read_save(const char *name)
{
    int16_t size = 0x20, state, fd;
    int32_t got = 0;
    char buf[0x14];
    int i;

    gr_fade(500, (int16_t)-g_pal_level, (int16_t)-g_music_level);
    if (g_sound_on) {
        ampStopModule();
        if (g_music_module != NULL) {
            ampFreeModule(g_music_module);
            MemFree(g_music_module);
            g_music_module = NULL;
        }
    }
    o5737_cd_stop();
    ui_restore_background();
    fd = vfs_open(name, 2);
    if (fd == -1)
        o559A_fatal(DGSTR(0x4300), 0x704, DGSTR(0x430F), -1);  /* "Saklama dosyası yükleme hatası ..." */
    vfs_seek(fd, 0x3C, 0);
    vfs_read(fd, &size, 2);
    vfs_read(fd, DGP(0xB9CA), size);
    for (i = 0; i < N_BLOCKS; i++)
        got = vfs_read(fd, save_blocks[i].off == 0x3D5E ? (void *)&state : (void *)DGP(save_blocks[i].off),
                       save_blocks[i].len);
    if (got != 0)
        DG8(0xBBAB) = (uint8_t)((int32_t)bc_rand() * 100 / 0x8000 + 0x65);
    vfs_close(fd);
    for (DGS16(0xD5EC) = 0; DGS16(0xD5EC) < 4; DGS16(0xD5EC)++) {
        strcpy(buf, DGSTR(0x4342));                             /* "s" */
        strcat(buf, DGSTR(0x269B + 0xD * DGS16(0x9A44 + 0x5C * DGS16(0xD5EC))));
        fm_load_cmp(1, buf, (int16_t)(DGS16(0xD5EC) + 0x47));
    }
    DGS16(0x632E) = 0;                                          /* g_iso_result */
    ui_init_voices();
    s3d_load_district(g_s3d_district, 0);
    gr_cursor_hide();
    if (g_s3d_opt(6) != 0) {                                    /* big view */
        g_q3d_win_w = 200;
        g_q3d_win_h = 200;
        g_q3d_anim_speed = 0;
        g_q3d_anim_turn = (uint8_t)(g_s3d_opt(0) != 0);
    } else {
        g_q3d_win_w = 100;
        g_q3d_win_h = 100;
        g_q3d_anim_speed = g_s3d_opt(0) != 0 ? 9 : 0;
    }
    g_s3d_prev_x = g_s3d_x;
    g_s3d_prev_y = g_s3d_y;
    g_q3d_shake_set = 0;
    s3d_enter_view();
    (void)state;
}
