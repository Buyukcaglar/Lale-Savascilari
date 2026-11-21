/*
 * chargen.c - CHARGEN.L, character generation, decompiled from IST.EXE
 * ovl39E5 (39E5:0000-2468).  See chargen.h for the data, docs/FORMATS.md
 * "GRP.DAT" for the party file.
 *
 * The karakter screen.  Surface 1 holds the clean background
 * (karakter.cmp), surface 6 is a 320x200 work screen that every drawing
 * routine updates and then copies to the display with iso_show_picture(6).
 * Eight mouse zones (set up in cg_main) select the portrait (zones 1/2,
 * left/right button = next/previous, keys Right/Left), the race (zone 3,
 * Up/Down, which also re-rolls and picks the race's first portrait), the
 * gender (zone 4, "C"), re-roll (zone 6, Enter), name entry (zone 5, Tab),
 * accept (zone 8, Space: next member, after the 4th GRP.DAT is written) and
 * back (zone 7, Esc: previous member, from the 1st the screen is left with
 * 0).  While waiting, every 150 ms the walking sprite takes a step and the
 * face blinks now and then (random(1000) == 0 per loop pass).
 *
 * Random numbers: Borland's random(n) is inlined at every site; bc_random()
 * reproduces it and the calls are made in the original's order (one per
 * statement where the original adds several).
 *
 * Port notes: keys through ui_bioskey/ui_flush_keys (engine/intrface.h),
 * the mouse-button release wait calls pc_poll().  Far code pointers: the
 * idle callback of the current UI window record (16E0:964C) goes through
 * ui_far_fn.
 */
#include "game/chargen.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "engine/fileman.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/ographs.h"
#include "engine/systems.h"
#include "engine/vfs.h"
#include "game/iso.h"   /* iso_clamp_add, iso_draw_sprite, iso_draw_text_shadow,
                              iso_show_picture (ISOSYS, WP17; thunks 1694:xxxx) */
#include "platform/pc.h"
#include "rtl/bc_farheap.h"
#include "rtl/bc_rand.h"


/* DGROUP offsets of the "MmHandle far *" variables of the surface slots
   (memman.h g_surf_mm_off), as ctl_free_checked/mm_ref take them */
#define REF_BACK        0xE2A7          /* slot 1 */
#define REF_WORK        0xE2CF          /* slot 6 */
#define REF_SPRITE      0xE2F7          /* slot 0Bh */
#define REF_SPRITE_KOR  0xE337          /* slot 13h */
#define REF_ICONS       0xE4C7          /* slot 45h */
#define REF_FACE        0xE4D7          /* slot 47h */

#define CHARGEN_FILE(o) DGSTR(o)        /* "T:\CODE\CHARGEN.L" (one copy per call site) */

typedef void (*CgIdleFn)(uint32_t);

static PartyStats *cur_stats(void)
{
    return (PartyStats *)DGP((uint16_t)(PARTY_STATS_OFF + 0x5C * g_party_cur));
}

static CgMods *cur_mods(void)
{
    return (CgMods *)DGP((uint16_t)(CG_MODS_OFF + 0x24 * g_party_cur));
}

static PartyName *cur_name(void)
{
    return (PartyName *)DGP((uint16_t)(PARTY_NAMES_OFF + 0x94 * g_party_cur));
}

/* sys_blit with the plain rectangle copy (2599:02D0), as every call here */
static void blit(int16_t sx, int16_t sy, int16_t w, int16_t h, int16_t dx, int16_t dy,
                 uint8_t src, uint8_t dst)
{
    sys_blit((uint16_t)sx, (uint16_t)sy, (uint16_t)w, (uint16_t)h, (uint16_t)dx, (uint16_t)dy,
             src, dst, sys_rect_copy);
}

/* DGROUP offset of a word field of the current member's stat record */
static uint16_t cur_field(uint16_t off)
{
    return (uint16_t)(PARTY_STATS_OFF + 0x5C * g_party_cur + off);
}

/* iso_clamp_add on a DGROUP word (through a local: dg[] words may be
   unaligned) */
static void clamp_add(uint16_t at, int16_t d, int16_t lo, int16_t hi)
{
    int16_t v = DGS16(at);
    iso_clamp_add(&v, d, lo, hi);
    DGS16(at) = v;
}

/* Borland itoa(v, buf, 10) */
static char *itoa10(int16_t v, char *buf)
{
    sprintf(buf, "%d", (int)v);
    return buf;
}

/*
 * cg_random_gender - load the current member's race modifiers: mod[1..15]
 * from the race table, the race name (16E0:BB7C), the gender word for the
 * header (16E0:9F0A), then mod[15] = one of the race's five specials at
 * random.  (The inventory name stuck; it does not pick a gender.)
 *
 * Original: IST.EXE ovl39E5:0000 (CHARGEN.L), far cdecl.
 * Verified: tests/py/test_chargen.py (test_roll_stats: all races/genders,
 * unicorn differential), 2026-11-21.
 */
void cg_random_gender(void)
{
    int16_t i;

    for (i = 1; i <= 15; i++)
        cur_mods()->mod[i] = g_race_mods(cur_stats()->race, i);
    strcpy(g_cg_race_text, g_race_name(cur_stats()->race));
    strcpy(g_cg_gender_text, cur_stats()->gender == 1 ? DGSTR(0x48F9) : DGSTR(0x4900));
    i = bc_random(5);
    cur_mods()->mod[15] = g_race_special(cur_stats()->race, i);
}

/*
 * cg_load_char_graphics - (re)load the pictures of the current portrait:
 * the face pictures "s<portrait>" into slot 47h, the walking sprite sheet
 * into slot 0Bh and its .KOR frame table into a 48h-byte block (slot 13h);
 * draw the face, derive the gender from the portrait (6, 13, 14, 16 are
 * female), take one sprite step and show the work screen.
 *
 * Original: IST.EXE ovl39E5:011F (CHARGEN.L), far cdecl.  The old pictures
 * are freed only when slot 0Bh is an EMS block (type 1); a conventional one
 * is kept and nothing is reloaded (but the .KOR block is freed) - with EMS,
 * as the game needs, fm_load_cmp always gives type 1.  With slot 0Bh empty
 * (the first call) the original reads the type byte through a null far
 * pointer: 0000:0008, the low byte of the NMI vector (E0h in DOSBox-X at
 * main()), i.e. not 1; the port treats NULL as "not EMS".
 * Verified: tests/py/test_chargen.py (unicorn differential), 2026-11-21.
 */
void cg_load_char_graphics(void)
{
    char name[0x12];
    MmHandle *h;
    char *kor;

    memcpy(name, DGP(CG_FACE_PREFIX), 13);          /* F_SCOPY 13 bytes */
    gr_cursor_hide();
    h = mm_ref(REF_SPRITE);
    if (h != NULL && h->type == 1) {
        ctl_free_checked(REF_SPRITE, 0x32, CHARGEN_FILE(0x4906));
        ctl_free_checked(REF_FACE, 0x32, CHARGEN_FILE(0x4918));
    }
    if (mm_ref(REF_SPRITE_KOR) != NULL)
        ctl_free_checked(REF_SPRITE_KOR, 0x33, CHARGEN_FILE(0x492A));
    if (mm_ref(REF_SPRITE) == NULL) {
        blit(0x11, 0x35, 0x30, 0x48, 0x11, 0x35, CG_SLOT_BACK, CG_SLOT_WORK);
        fm_load_cmp(1, strcat(name, g_portrait_name(cur_stats()->portrait)), CG_SLOT_FACE);
        fm_load_cmp(1, g_portrait_name(cur_stats()->portrait), CG_SLOT_SPRITE);
        mm_ref_set(REF_SPRITE_KOR, mm_alloc(0x48, 0));
        kor = fm_change_ext(g_portrait_name(cur_stats()->portrait), DGSTR(0x493C));  /* "kor" */
        fm_load_file(0, kor, mm_ref(REF_SPRITE_KOR));
        bc_farfree(kor);
    }
    blit(0, 0, 0x20, 0x20, 0x44, 0x35, CG_SLOT_FACE, CG_SLOT_WORK);
    g_cg_walk_frame = 0;
    switch ((uint16_t)(cur_stats()->portrait - 6)) {   /* jump table 39E5:0347 */
    case 0: case 7: case 8: case 10:                    /* portraits 6, 13, 14, 16 */
        cur_stats()->gender = 2;
        break;
    default:
        cur_stats()->gender = 1;
        break;
    }
    cg_roll_misc();
    iso_show_picture(CG_SLOT_WORK);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * cg_roll_misc - one animation step on the work screen: restore the
 * sprite area from the background, draw the walking sprite (frame 1..4 at
 * 2Ah,78h), and advance the blink: state 1 draws one of the faces 0..2 at
 * random, state 2 the normal face (state 3 is reset to 0 by cg_main).
 *
 * Original: IST.EXE ovl39E5:035D (CHARGEN.L), far cdecl.
 * Verified: tests/py/test_chargen.py (unicorn differential), 2026-11-21.
 */
void cg_roll_misc(void)
{
    int16_t x, y;

    blit(0x11, 0x35, 0x30, 0x48, 0x11, 0x35, CG_SLOT_BACK, CG_SLOT_WORK);
    x = 0x2A;
    y = 0x78;
    g_cg_walk_frame++;
    iso_draw_sprite(&x, &y, CG_SLOT_SPRITE, g_cg_walk_frame);
    g_cg_walk_frame = (int16_t)(g_cg_walk_frame % 4);
    if (g_cg_blink != 0) {
        if (g_cg_blink == 1) {
            blit((int16_t)(bc_random(3) << 5), 0, 0x20, 0x20, 0x44, 0x35, CG_SLOT_FACE,
                 CG_SLOT_WORK);
            g_cg_blink = 2;
        } else if (g_cg_blink == 2) {
            blit(0, 0, 0x20, 0x20, 0x44, 0x35, CG_SLOT_FACE, CG_SLOT_WORK);
            g_cg_blink = 3;
        }
    }
}

/*
 * cg_roll_stats - roll the current member's attributes (26 random calls),
 * load the race modifiers first (cg_random_gender, 1 more), clamp and show
 * them.  m[i] = race modifier i, g = gender (1/2).  Every sum is a 16-bit
 * int; the divisions are IDIV of the 16-bit sums.
 *
 * Original: IST.EXE ovl39E5:0437 (CHARGEN.L), far cdecl.
 * Verified: tests/py/test_chargen.py (test_roll_stats, unicorn differential
 * over all races/genders/members and many seeds), 2026-11-21.
 */
void cg_roll_stats(void)
{
    PartyStats *s;
    const CgMods *m;
    int16_t r, i;

    cg_random_gender();
    s = cur_stats();
    m = cur_mods();

    r = bc_random(25);
    r = (int16_t)(r + bc_random(25));
    s->height = (int16_t)(r + m->mod[1] * 10 + 150);

    r = bc_random(5);
    r = (int16_t)(r + bc_random(5));
    s->weight = (int16_t)(r + s->height + m->mod[2] * 10 - 110 - (s->gender - 1) * 10);

    r = bc_random(150);
    r = (int16_t)(r + bc_random(150));
    s->intelligence = (int16_t)(r + (s->gender - 1) * 25
                                - (int16_t)(s->height + s->weight) / 5 + m->mod[3] * 10);

    r = bc_random(50);
    r = (int16_t)(r + bc_random(50));
    s->hit_power = (int16_t)(r - s->gender * 25
                             + (int16_t)(s->height + s->weight) / 3 + m->mod[4] * 10);

    r = bc_random(100);
    r = (int16_t)(r + bc_random(100));
    s->vocabulary = (int16_t)(r + s->intelligence / 4 - s->hit_power / 4 + m->mod[5] * 10);

    r = bc_random(6);
    s->strength = (int16_t)(r + s->hit_power / 40 - s->vocabulary / 30 + s->weight / 20
                            + m->mod[6] * 2);

    r = bc_random(70);
    r = (int16_t)(r + bc_random(80));
    r = (int16_t)(r + bc_random(100));
    s->agility = (int16_t)(r + 150 - (int16_t)(s->height + s->weight) + m->mod[7] * 10);

    r = bc_random(200);
    r = (int16_t)(r + bc_random(200));
    s->endurance = (int16_t)(r - (int16_t)(s->weight + s->height) - s->vocabulary / 4
                             + s->hit_power / 4 + m->mod[8] * 10);

    r = bc_random(150);
    r = (int16_t)(r + bc_random(150));
    s->superstition = (int16_t)(r - s->intelligence / 2 + m->mod[9] * 10);

    r = bc_random(150);
    r = (int16_t)(r + bc_random(150));
    s->cheek = (int16_t)(r - s->gender * 50 - s->superstition / 4 + m->mod[10] * 10);

    r = bc_random(151);
    s->istanbulness = (int16_t)(r + s->intelligence / 4 + m->mod[11] * 10);

    r = bc_random(201);
    s->courage = (int16_t)(r + s->hit_power / 4 + s->endurance / 4 - s->superstition / 4
                           - s->intelligence / 4 + m->mod[12] * 10);

    r = bc_random(201);
    s->charisma = (int16_t)(r + (s->gender - 1) * 50 - s->weight * 2 + s->height / 4
                            + m->mod[13] * 20);

    r = bc_random(251);
    s->faith = (int16_t)(r - s->superstition / 4 - s->intelligence / 4 + m->mod[14] * 10);

    s->u22 = (int16_t)(bc_random(151) + 50);
    s->u28 = (int16_t)(bc_random(6) + 15);
    s->special[0] = m->mod[15];

    /* words 3..16 (+06..+20): at least 50, strength (+0C) at least 3 */
    for (i = 3; i <= 16; i++) {
        int16_t v = DGS16(cur_field((uint16_t)(2 * i)));
        if (i == 6)
            v = v < 3 ? 3 : v;
        else
            v = v < 50 ? 50 : v;
        DGS16(cur_field((uint16_t)(2 * i))) = v;
    }
    cg_draw_stats();
}

/* field offset in PartyStats of each attribute row (1..14) of the screen */
static const uint8_t cg_stat_rows[14] = {
    0x24, 0x26, 0x0E, 0x10, 0x0A, 0x0C, 0x14, 0x12, 0x1A, 0x16, 0x18, 0x1C, 0x1E, 0x20
};

/*
 * cg_draw_stats - redraw the attribute column: the 14 values at text
 * column 23h, rows 1..14; from row 3 on a value above its table maximum is
 * drawn in colour 48h, one below its minimum in DEh, else in the text
 * colour.  Then show the work screen.
 *
 * Original: IST.EXE ovl39E5:0F4E (CHARGEN.L), far cdecl, 14 unrolled
 * blocks.  itoa writes into a 6-byte stack buffer (a value below -9999
 * would overrun it; the rolls never get there).
 * Verified: tests/py/test_chargen.py (unicorn differential), 2026-11-21.
 */
void cg_draw_stats(void)
{
    char buf[8];
    int16_t row, v, color;
    uint8_t off;

    blit(0x78, 0x13, 0xC8, 0x8D, 0x78, 0x13, CG_SLOT_BACK, CG_SLOT_WORK);
    for (row = 1; row <= 14; row++) {
        off = cg_stat_rows[row - 1];
        v = DGS16(cur_field(off));
        if (row <= 2) {
            color = g_ui_col_text;
        } else {
            int16_t k = (int16_t)((off - 0x0A) / 2);
            if (v > g_cg_stat_max(k))
                color = 0x48;
            else if (v < g_cg_stat_min(k))
                color = 0xDE;
            else
                color = g_ui_col_text;
        }
        iso_draw_text_shadow(itoa10(v, buf), 0x23, row, color, 10);
    }
    iso_show_picture(CG_SLOT_WORK);
}

/*
 * cg_draw_header - "karakter  #n", the gender word and the race name at
 * the top left, and the gender icon.
 *
 * Original: IST.EXE ovl39E5:164A (CHARGEN.L), far cdecl.
 * Verified: tests/py/test_chargen.py (unicorn differential), 2026-11-21.
 */
void cg_draw_header(void)
{
    char buf[0x10];

    blit(4, 0x13, 0x80, 0x1F, 4, 0x13, CG_SLOT_BACK, CG_SLOT_WORK);
    sprintf(buf, DGSTR(0x4940), (int)g_party_cur);            /* "karakter  #%d" */
    iso_draw_text_shadow(buf, 1, 1, 2, 10);
    strcpy(g_cg_gender_text, cur_stats()->gender == 1 ? DGSTR(0x494E) : DGSTR(0x4955));
    iso_draw_text_shadow(g_cg_gender_text, 1, 2, g_ui_col_text, 10);
    iso_draw_text_shadow(g_cg_race_text, 1, 3, g_ui_col_text, 10);
    blit((int16_t)((cur_stats()->gender + 7) * 0x14), 0, 0x14, 0x14, 0x44, 0x6F,
         CG_SLOT_ICONS, CG_SLOT_WORK);
    iso_show_picture(CG_SLOT_WORK);
}

/*
 * cg_draw_field_a - the race icon and the gender icon.
 *
 * Original: IST.EXE ovl39E5:1753 (CHARGEN.L), far cdecl.
 * Verified: tests/py/test_chargen.py (unicorn differential), 2026-11-21.
 */
void cg_draw_field_a(void)
{
    blit((int16_t)((cur_stats()->race - 1) * 0x14), 0, 0x14, 0x14, 0x44, 0x58,
         CG_SLOT_ICONS, CG_SLOT_WORK);
    blit((int16_t)((cur_stats()->gender + 7) * 0x14), 0, 0x14, 0x14, 0x44, 0x6F,
         CG_SLOT_ICONS, CG_SLOT_WORK);
    iso_show_picture(CG_SLOT_WORK);
}

/*
 * cg_draw_field_b - the name field.
 *
 * Original: IST.EXE ovl39E5:17D2 (CHARGEN.L), far cdecl.
 * Verified: tests/py/test_chargen.py (unicorn differential), 2026-11-21.
 */
void cg_draw_field_b(void)
{
    blit(0x10, 0xA9, 0x86, 0x0E, 0x10, 0xA9, CG_SLOT_BACK, CG_SLOT_WORK);
    iso_draw_text_shadow(cur_name()->name, 2, 0x10, g_ui_col_text, 10);
    iso_show_picture(CG_SLOT_WORK);
}

/*
 * cg_save_party - write GRP.DAT: for members 1..4 the name and the
 * aphorism flags (reset to "1000...0") as 37-byte strings, then their
 * 92-byte stat records.  Before writing, vocabulary_cur (+38) and health
 * (+3A) are set from vocabulary and strength.
 *
 * Original: IST.EXE ovl39E5:182E (CHARGEN.L), far cdecl.  Open failure:
 * fatal "Karakter bilgisi GRP.DAT açılamıyor." (line DEh); if that returns
 * (port test mode) the writes go to handle -1 as in the original.
 * Verified: tests/py/test_chargen.py (file bytes + DGROUP, unicorn
 * differential), 2026-11-21.
 */
void cg_save_party(void)
{
    int16_t fd, i;

    fd = vfs_open(DGSTR(0x495B), 4);                           /* "grp.dat", write */
    if (fd == -1)
        o559A_fatal(CHARGEN_FILE(0x4963), 0xDE, DGSTR(0x4975), -1);
    for (i = 1; i <= PARTY_MEMBERS; i++) {
        g_party_stats[i].vocabulary_cur = g_party_stats[i].vocabulary;
        g_party_stats[i].health = g_party_stats[i].strength;
        strcpy(g_party_names[i].sayings, DGSTR(0x499A));       /* "1000...0" */
        vfs_write(fd, g_party_names[i].name, 0x25);
        vfs_write(fd, g_party_names[i].sayings, 0x25);
    }
    for (i = 1; i <= PARTY_MEMBERS; i++)
        vfs_write(fd, &g_party_stats[i], 0x5C);
    vfs_close(fd);
}

/*
 * cg_load_party - read a party file (GRP.DAT or GRPHAZIR.DAT, the
 * ready-made party) written by cg_save_party, then fill each member's
 * race name.
 *
 * Original: IST.EXE ovl39E5:1932 (CHARGEN.L), far cdecl (char far *name).
 * Open failure: fatal "Karakter bilgisi GRP.DAT açılamıyor." (line 116h).
 * Verified: tests/py/test_chargen.py (GRP.DAT, GRPHAZIR.DAT and a written
 * file, unicorn differential), 2026-11-21.
 */
void cg_load_party(const char *name)
{
    int16_t fd, i;

    fd = vfs_open(name, 2);                                    /* read */
    if (fd == -1)
        o559A_fatal(CHARGEN_FILE(0x49BF), 0x116, DGSTR(0x49D1), -1);
    for (i = 1; i <= PARTY_MEMBERS; i++) {
        vfs_read(fd, g_party_names[i].name, 0x25);
        vfs_read(fd, g_party_names[i].sayings, 0x25);
    }
    for (i = 1; i <= PARTY_MEMBERS; i++)
        vfs_read(fd, &g_party_stats[i], 0x5C);
    vfs_close(fd);
    for (i = 1; i <= PARTY_MEMBERS; i++)
        strcpy(g_party_names[i].race_name, g_race_name(g_party_stats[i].race));
}

/*
 * cg_free_graphics - free the screen's six blocks (sprite .KOR, sprite
 * sheet, faces, work screen, background, icons) and hide the cursor.
 *
 * Original: IST.EXE ovl39E5:1A1A (CHARGEN.L), far cdecl.
 * Verified: tests/py/test_chargen.py (unicorn differential), 2026-11-21.
 */
void cg_free_graphics(void)
{
    ctl_free_checked(REF_SPRITE_KOR, 0x125, CHARGEN_FILE(0x49F6));
    ctl_free_checked(REF_SPRITE, 0x126, CHARGEN_FILE(0x4A08));
    ctl_free_checked(REF_FACE, 0x127, CHARGEN_FILE(0x4A1A));
    ctl_free_checked(REF_WORK, 0x128, CHARGEN_FILE(0x4A2C));
    ctl_free_checked(REF_BACK, 0x129, CHARGEN_FILE(0x4A3E));
    ctl_free_checked(REF_ICONS, 0x12A, CHARGEN_FILE(0x4A50));
    gr_cursor_hide();
}

/* (key == k || (zone == z && buttons == b)), the shape of most commands */
#define CMD(k, z, b)  (key == (k) || (zone == (z) && buttons == (b)))

/*
 * cg_main - the character generation screen; creates members
 * g_party_cur..4 (main starts it at 1) and writes GRP.DAT.  Returns 1 when
 * the party is complete, 0 when the player backs out of member 1.
 *
 * Original: IST.EXE ovl39E5:1AA2 (CHARGEN.L), far cdecl.  The command
 * tests run one after another on the same key/zone/buttons, so one input
 * can trigger several (e.g. the right button in zone 1 or 2 changes the
 * portrait, zone 4 reacts to either button).  A finished command returns
 * to the clock reset at 1E4B; member changes fade out and rebuild the
 * screen from the top (1B53).  The 150 ms animation timer compares the
 * low clock word unsigned.
 * Port: the button-release wait polls (pc_poll).
 * Verified: tests/py/test_chargen.py (scripted keys and mouse through the
 * whole flow, unicorn differential: DGROUP, screen, palette, GRP.DAT),
 * 2026-11-21.
 */
int16_t cg_main(void)
{
    int16_t redraw_cursor = 0;          /* [bp-8] */
    int16_t result = 1;                 /* [bp-0Ah] */
    uint16_t tick = 150;                /* [bp-0Eh] */
    uint16_t t0;                        /* [bp-0Ch] */
    int16_t key, zone, buttons;
    MmHandle *h;

    ogr_clear_screen();
    ogr_black_palette();
    fm_load_pal(DGSTR(0x4A62), 0, 0x100);                     /* "palbat.pal" */
    fm_load_cmp(1, DGSTR(0x4A6D), CG_SLOT_ICONS);             /* "sematik.cmp" */
    fm_load_cmp(1, DGSTR(0x4A79), CG_SLOT_BACK);              /* "karakter.cmp" */
    h = mm_alloc(64000, 0);
    mm_ref_set(REF_WORK, h);
    g_surf_w(CG_SLOT_WORK) = 0x140;
    g_surf_h(CG_SLOT_WORK) = 200;
    blit(0, 0, 0x140, 200, 0, 0, CG_SLOT_BACK, CG_SLOT_WORK);
    ogr_mouse_set_pos(0, 0);

    for (;;) {                                                  /* 1B53: (re)build */
        PartyStats *s = cur_stats();
        s->race = s->race > 1 ? s->race : 1;
        s->gender = s->gender > 1 ? s->gender : 1;
        s->portrait = s->portrait > 1 ? s->portrait : 1;
        ogr_mouse_set_window(0, 0, 0x13F, 0xC7);
        g_cursor_shape = 0x2D;
        cg_random_gender();
        cg_load_char_graphics();
        if (strlen(cur_name()->name) == 0)
            cg_roll_stats();
        else
            cg_draw_stats();
        cg_draw_field_b();
        cg_draw_header();
        cg_roll_misc();
        cg_draw_field_a();
        /* mouse zones 1..8 (x1, y1, x2, y2) */
        g_zone_x1(1) = 0x11; g_zone_y1(1) = 0x35; g_zone_x2(1) = 0x41;  g_zone_y2(1) = 0x83;
        g_zone_x1(2) = 0x44; g_zone_y1(2) = 0x35; g_zone_x2(2) = 0x64;  g_zone_y2(2) = 0x55;
        g_zone_x1(3) = 0x5A; g_zone_y1(3) = 0x57; g_zone_x2(3) = 0x63;  g_zone_y2(3) = 0x6A;
        g_zone_x1(4) = 0x5A; g_zone_y1(4) = 0x6E; g_zone_x2(4) = 0x63;  g_zone_y2(4) = 0x81;
        g_zone_x1(5) = 0x45; g_zone_y1(5) = 0x88; g_zone_x2(5) = 0x6B;  g_zone_y2(5) = 0x9C;
        g_zone_x1(6) = 0x0F; g_zone_y1(6) = 0x88; g_zone_x2(6) = 0x35;  g_zone_y2(6) = 0x9C;
        g_zone_x1(7) = 0;    g_zone_y1(7) = 0;    g_zone_x2(7) = 0x14;  g_zone_y2(7) = 0x14;
        g_zone_x1(8) = 0x9A; g_zone_y1(8) = 0xA5; g_zone_x2(8) = 0x136; g_zone_y2(8) = 0xBC;
        gr_fade(800, (int16_t)(g_pal_level_max - g_pal_level), 0);
        gr_cursor_set_shape((uint8_t)g_cursor_shape);

        for (;;) {                                              /* 1E4B */
            t0 = (uint16_t)g_clock_ms;
            do {                                                /* 1E57 */
                if (bc_random(1000) == 0 && g_cg_blink == 0)
                    g_cg_blink = 1;
                if ((uint16_t)((uint16_t)g_clock_ms - t0) >= tick) {
                    t0 = (uint16_t)g_clock_ms;
                    cg_roll_misc();
                    if (gr_cursor_in_zone(1) || (g_cg_blink != 0 && gr_cursor_in_zone(2))) {
                        gr_cursor_hide();
                        redraw_cursor = 1;
                    }
                    blit(g_zone_x1(1), g_zone_y1(1), (int16_t)(g_zone_x2(1) - g_zone_x1(1)),
                         (int16_t)(g_zone_y2(1) - g_zone_y1(1)), g_zone_x1(1), g_zone_y1(1),
                         CG_SLOT_WORK, 0);
                    if (g_cg_blink != 0) {
                        blit(g_zone_x1(2), g_zone_y1(2), (int16_t)(g_zone_x2(2) - g_zone_x1(2)),
                             (int16_t)(g_zone_y2(2) - g_zone_y1(2)), g_zone_x1(2), g_zone_y1(2),
                             CG_SLOT_WORK, 0);
                        g_cg_blink = (int16_t)(g_cg_blink % 3);
                    }
                    if (redraw_cursor != 0) {
                        gr_cursor_set_shape((uint8_t)g_cursor_shape);
                        redraw_cursor = 0;
                    }
                }
                ((CgIdleFn)ui_far_fn(DG32(UI_CB_IDLE)))(g_ui_idle_arg);
                g_ui_list_key = (int16_t)(ui_bioskey(1) >> 8);
                gr_cursor_update((uint8_t)g_cursor_shape);
                key = g_ui_list_key;
                zone = gr_zone_under_cursor();
                buttons = g_mouse_buttons;
            } while (key == 0 && buttons == 0);
            if (buttons != 0)
                do {
                    gr_cursor_update((uint8_t)g_cursor_shape);
                    pc_poll();
                } while (g_mouse_buttons != 0);
            ui_flush_keys();

            if (key == 0x4D || ((zone == 2 || zone == 1) && buttons == 1)) {   /* Right */
                clamp_add(cur_field(offsetof(PartyStats, portrait)), 1, 1, 16);
                cg_load_char_graphics();
                cg_draw_header();
            }
            if (key == 0x4B || ((zone == 2 || zone == 1) && buttons == 2)) {   /* Left */
                clamp_add(cur_field(offsetof(PartyStats, portrait)), -1, 1, 16);
                cg_load_char_graphics();
                cg_draw_header();
            }
            if (CMD(0x48, 3, 1)) {                                              /* Up */
                clamp_add(cur_field(offsetof(PartyStats, race)), 1, 1, 8);
                blit((int16_t)((cur_stats()->race - 1) * 0x14), 0, 0x14, 0x14, 0x44, 0x58,
                     CG_SLOT_ICONS, CG_SLOT_WORK);
                cg_roll_stats();
                cur_stats()->portrait = (int16_t)((cur_stats()->race - 1) * 2 + 1);
                cg_load_char_graphics();
                cg_draw_header();
            }
            if (CMD(0x50, 3, 2)) {                                              /* Down */
                clamp_add(cur_field(offsetof(PartyStats, race)), -1, 1, 8);
                blit((int16_t)((cur_stats()->race - 1) * 0x14), 0, 0x14, 0x14, 0x44, 0x58,
                     CG_SLOT_ICONS, CG_SLOT_WORK);
                cg_roll_stats();
                cur_stats()->portrait = (int16_t)((cur_stats()->race - 1) * 2 + 1);
                cg_load_char_graphics();
                cg_draw_header();
            }
            if (CMD(0x1C, 6, 1)) {                                              /* Enter */
                gr_cursor_hide();
                cg_roll_stats();
                gr_cursor_set_shape((uint8_t)g_cursor_shape);
            }
            if (key == 0x2E || (zone == 4 && buttons != 0)) {                   /* C */
                gr_cursor_hide();
                clamp_add(cur_field(offsetof(PartyStats, gender)), 1, 1, 2);
                cg_roll_stats();
                cg_draw_header();
                gr_cursor_set_shape((uint8_t)g_cursor_shape);
            }
            if (CMD(0x0F, 5, 1)) {                                              /* Tab */
                iso_show_picture(CG_SLOT_WORK);
                grt_input_line(cur_name()->name, 1, g_ui_col_text, 0, 0x73, 0x11, 0xA8, 0,
                               0x11, 0xA8, CG_SLOT_BACK);
                cg_draw_field_b();
            }
            if (CMD(0x39, 8, 1) && strlen(cur_name()->name) != 0 && g_party_cur < 4) {
                g_party_cur++;                                                  /* Space */
                break;
            }
            if (CMD(0x39, 8, 1) && strlen(cur_name()->name) != 0 && g_party_cur == 4) {
                g_party_cur++;
                gr_fade(0xC80, (int16_t)-g_pal_level, 0);
                cg_save_party();
                cg_free_graphics();
                goto done;
            }
            if (CMD(0x01, 7, 1) && g_party_cur > 1) {                           /* Esc */
                g_party_cur--;
                break;
            }
            if (CMD(0x01, 7, 1) && g_party_cur == 1) {
                cg_free_graphics();
                gr_fade(800, (int16_t)-g_pal_level, 0);
                result = 0;
                goto done;
            }
        }
        gr_fade(800, (int16_t)-g_pal_level, 0);                /* 23F4 */
    }
done:
    gr_zones_clear();
    return result;
}
