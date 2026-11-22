/*
 * intrface.c - ALTDUZEY\INTRFACE.L: panel, list menus, message boxes,
 * voices, input waits.
 * Original: IST.EXE seg05DC (Borland C, far cdecl).  See intrface.h.
 */
#include "engine/intrface.h"

#include <stdio.h>
#include <string.h>

#include "dsmi/cdi.h"
#include "engine/blit386.h"
#include "engine/grtext.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/nses.h"
#include "engine/ographs.h"
#include "engine/oses.h"
#include "engine/systems.h"
#include "game/cd5737.h"
#include "platform/pc.h"
#include "rtl/bc_rand.h"

#define SURF_REF(n)  ((uint16_t)(0xE29F + 8 * (n)))   /* far MmHandle * of slot n */
#define S17          0x17                              /* panel background save */

/* ======================================================================== */
/* far code pointer registry                                                */
/* ======================================================================== */

typedef struct { uint32_t addr; UiFarFn fn; } UiFarEntry;

static const UiFarEntry builtin[] = {
    { UI_FAR(0x05DC, 0x0004), (UiFarFn)ui_text_measure_0004 },
    { UI_FAR(0x05DC, 0x00CD), (UiFarFn)ui_nop },
    { UI_FAR(0x05DC, 0x00D2), (UiFarFn)ui_return_zero },
    { UI_FAR(0x05DC, 0x00D9), (UiFarFn)ui_get_9671 },
    { UI_FAR(0x05DC, 0x00E9), (UiFarFn)ui_button_press_anim },
    { UI_FAR(0x05DC, 0x0167), (UiFarFn)ui_button_release_anim },
    { UI_FAR(0x05DC, 0x027B), (UiFarFn)ui_draw_panel_027B },
    { UI_FAR(0x05DC, 0x0308), (UiFarFn)ui_draw_panel_0308 },
    { UI_FAR(0x05DC, 0x041C), (UiFarFn)ui_setup_hotspots },
    { UI_FAR(0x05DC, 0x0568), (UiFarFn)ui_rect_op_0568 },
    { UI_FAR(0x05DC, 0x05E4), (UiFarFn)ui_rect_op_05E4 },
    { UI_FAR(0x05DC, 0x066B), (UiFarFn)ui_update_hover },
    { UI_FAR(0x05DC, 0x0831), (UiFarFn)ui_unhighlight },
    { UI_FAR(0x05DC, 0x0881), (UiFarFn)ui_free_panel },
    { UI_FAR(0x05DC, 0x08F6), (UiFarFn)ui_cd_music },
    { UI_FAR(0x05DC, 0x2D9A), (UiFarFn)ui_call_9650 },
};

#define UI_FAR_MAX 64
static UiFarEntry registered[UI_FAR_MAX];
static int n_registered;

/*
 * ui_register_far_fn - make the far code address flat_seg:off (IST.EXE
 * flat segment, as in the listings) callable through ui_far_fn.  A second
 * registration of the same address replaces the first.
 * Port only (the original jumps to the address).
 */
void ui_register_far_fn(uint16_t flat_seg, uint16_t off, UiFarFn fn)
{
    uint32_t a = UI_FAR(flat_seg, off);
    int i;
    for (i = 0; i < n_registered; i++)
        if (registered[i].addr == a) {
            registered[i].fn = fn;
            return;
        }
    if (n_registered < UI_FAR_MAX) {
        registered[n_registered].addr = a;
        registered[n_registered].fn = fn;
        n_registered++;
    }
}

/*
 * ui_far_fn - native function for a far code address as stored in dg[]
 * (runtime segment << 16 | offset).  Unknown addresses are fatal.
 * Port only.
 */
UiFarFn ui_far_fn(uint32_t far_addr)
{
    size_t i;
    for (i = 0; i < (size_t)n_registered; i++)
        if (registered[i].addr == far_addr)
            return registered[i].fn;
    for (i = 0; i < sizeof builtin / sizeof builtin[0]; i++)
        if (builtin[i].addr == far_addr)
            return builtin[i].fn;
    {
        static char msg[64];
        snprintf(msg, sizeof msg, "INTRFACE: no function at %04X:%04X",
                 (unsigned)(far_addr >> 16), (unsigned)(far_addr & 0xFFFF));
        o559A_fatal("intrface.c", 0, msg, -1);
    }
    return (UiFarFn)ui_nop;
}

/* store far code pointer flat_seg:off (relocated) at DGROUP `at` */
void ui_far_set(uint16_t at, uint16_t flat_seg, uint16_t off)
{
    DG16(at) = off;
    DG16(at + 2) = (uint16_t)(flat_seg + UI_LOAD_SEG);
}

static UiFarFn cb(uint16_t at)                 { return ui_far_fn(DG32(at)); }
static UiFarFn ext(uint16_t seg, uint16_t off) { return ui_far_fn(UI_FAR(seg, off)); }

typedef void (*FnVoid)(void);
typedef void (*FnIdle)(uint32_t);
typedef void (*FnOpen)(int16_t *, int16_t);
typedef void (*FnI3)(int16_t, int16_t, int16_t);
typedef void (*FnI4)(int16_t, int16_t, int16_t, int16_t);
typedef void (*FnI6)(int16_t, int16_t, int16_t, int16_t, int16_t, int16_t);
typedef void (*FnItem)(const char *, int16_t, int16_t, uint8_t, int16_t);
typedef void (*FnShadow)(const char *, int16_t, int16_t, int16_t, int16_t);
typedef void (*FnClamp)(int16_t *, int16_t, int16_t, int16_t);

static void idle(void)        { ((FnIdle)cb(UI_CB_IDLE))(g_ui_idle_arg); }
/* iso_clamp_add on a DGROUP word (through a local: dg[] words may be unaligned) */
static void clamp_add(uint16_t at, int16_t d, int16_t lo, int16_t hi)
{
    int16_t v = DGS16(at);
    ((FnClamp)ext(UI_EXT_CLAMP_ADD))(&v, d, lo, hi);
    DGS16(at) = v;
}
static void item(int16_t n, int16_t x, int16_t row, int16_t color, int16_t h)
{
    ((FnItem)cb(UI_CB_ITEM))(DGSTR(UI_LIST_ENTRY(n)), x, row, (uint8_t)color, h);
}

/* ======================================================================== */
/* keyboard                                                                 */
/* ======================================================================== */

/*
 * ui_bioskey - Borland bioskey(cmd) (IST.EXE 0000:1F3F), i.e. INT 16h:
 * 0 waits for a key and returns its word (scan << 8 | ASCII), 1 peeks
 * (0 = none, -1 for a 0000h word), 2 returns the shift flags.  Kept as a
 * named wrapper so game code reads like the original's bioskey() calls.
 */
int16_t ui_bioskey(int16_t cmd) { return pc_bioskey(cmd); }

/* DOS kbhit()/getch() (INT 21h 0Bh/07h) - the same BIOS buffer bioskey reads */
int16_t ui_kbhit(void) { return pc_kbhit(); }
int16_t ui_getch(void) { return pc_getch(); }

/* ctl_flush_keys() (0DB7:01E3: while (kbhit()) getch();) */
void ui_flush_keys(void) { ctl_flush_keys(); }

/* Nothing to reset any more (the keyboard state lives in the pc layer and
   pc_reset clears it); kept for the test harness. */
void ui_bioskey_reset(void) {}

/* ======================================================================== */
/* INTRFACE.L                                                               */
/* ======================================================================== */

/*
 * ui_text_measure_0004 - (despite the name) draw a list item into the
 * panel background (slot 17h): s at (col*8, row*h) with a shadow in
 * colour 9667 one pixel down (font 2, when h = 7) or down-right (font 1).
 *
 * Original: IST.EXE 05DC:0004 (INTRFACE.L), far cdecl (char far *s, int
 * col, int row, u8 color, int h); window record item callback (+18h).
 * Verified: tests/py/test_intrface.py (test_list_items).
 */
void ui_text_measure_0004(const char *s, int16_t col, int16_t row, uint8_t color, int16_t h)
{
    int16_t x = (int16_t)(col << 3), y = (int16_t)(row * h);
    if (h == 7) {
        grt_draw_text_alt(S17, x, (int16_t)(y + 1), s, g_ui_col_shadow);
        grt_draw_text_alt(S17, x, y, s, color);
    } else {
        grt_draw_text(S17, (int16_t)(x + 1), (int16_t)(y + 1), s, g_ui_col_shadow, 5);
        grt_draw_text(S17, x, y, s, color, 5);
    }
}

/*
 * ui_nop - empty window record callback.
 * Original: IST.EXE 05DC:00CD (INTRFACE.L), far cdecl.
 * Verified: trivially.
 */
void ui_nop(void)
{
}

/*
 * ui_return_zero - window record callback returning 0.
 * Original: IST.EXE 05DC:00D2 (INTRFACE.L), far cdecl.  Unreferenced by
 * ui_init_tables (the game may store it).
 * Verified: trivially.
 */
int16_t ui_return_zero(void)
{
    return 0;
}

/*
 * ui_get_9671 - the panel's cursor shape (byte 9671).
 * Original: IST.EXE 05DC:00D9 (INTRFACE.L), far cdecl; cursor shape
 * callback (+24h) of several records.
 * Verified: tests/py/test_intrface.py (test_small).
 */
int16_t ui_get_9671(void)
{
    return g_ui_shape;
}

/*
 * ui_button_press_anim - window open callback: *count = min(*count, n),
 * restore and save the panel background again, mouse to (10, panel y-2),
 * panel cursor shape, slot 6 = copy of the saved background (type 1).
 *
 * Original: IST.EXE 05DC:00E9 (INTRFACE.L), far cdecl (int far *count,
 * int n); record +00.  The slot 6 record it overwrites is not freed.
 * Verified: tests/py/test_intrface.py (test_panel).
 */
void ui_button_press_anim(int16_t *count, int16_t n)
{
    *count = *count > n ? n : *count;
    gr_cursor_hide();
    ui_restore_background();
    ui_save_background();
    ogr_mouse_set_pos(10, (int16_t)(g_ui_panel_y - 2));
    gr_cursor_set_shape(g_ui_shape);
    mm_ref_set(SURF_REF(6), mm_duplicate(mm_ref(SURF_REF(S17)), 1));
}

/* the panel's hotspot rectangle: (0, y) - (w, y + h) */
static void panel_zone(int16_t w)
{
    uint16_t z = g_ui_panel_zone;
    g_zone_x1(z) = 0;
    g_zone_y1(z) = g_ui_panel_y;
    g_zone_x2(z) = (int16_t)(g_zone_x1(z) + w);
    g_zone_y2(z) = (int16_t)(g_zone_y1(z) + g_ui_panel_h);
}

/*
 * ui_button_release_anim - window open callback: like ui_button_press_anim
 * without the restore; also sets the panel hotspot to 320 x panel height.
 *
 * Original: IST.EXE 05DC:0167 (INTRFACE.L), far cdecl (int far *count,
 * int n); record +00.
 * Verified: tests/py/test_intrface.py (test_panel).
 */
void ui_button_release_anim(int16_t *count, int16_t n)
{
    *count = *count > n ? n : *count;
    gr_cursor_hide();
    panel_zone(0x140);
    ui_save_background();
    ogr_mouse_set_pos(10, (int16_t)(g_ui_panel_y - 2));
    mm_ref_set(SURF_REF(6), mm_duplicate(mm_ref(SURF_REF(S17)), 1));
}

/* slot 6 -> slot 17h, then the optional 12x32 button icon */
static void panel_icon(int16_t icon)
{
    if ((uint8_t)icon)
        sys_blit(g_ui_icon_x, 0, 0x0C, 0x20, 0, 0x0A, 0x4E, S17, asm_copy_rect_key);
}

/*
 * ui_draw_panel_027B - window frame callback: panel background (slot 17h)
 * = slot 6, the strip of gr_draw_strip over it, and with icon != 0 the
 * button icon (12x32 from (965E, 0) of slot 4Eh, colour key) at (0, 10).
 *
 * Original: IST.EXE 05DC:027B (INTRFACE.L), far cdecl (int x, int y, int
 * h, u8 icon); record +04.  x, y, h are only assigned to themselves.
 * Verified: tests/py/test_intrface.py (test_panel).
 */
void ui_draw_panel_027B(int16_t x, int16_t y, int16_t h, int16_t icon)
{
    (void)x; (void)y; (void)h;
    mm_copy_block(mm_ref(SURF_REF(6)), mm_ref(SURF_REF(S17)));
    gr_draw_strip(0, 0, (uint16_t)g_surf_w(S17), (uint16_t)g_surf_h(S17), S17, 0);
    panel_icon(icon);
}

/*
 * ui_draw_panel_0308 - window frame callback: panel hotspot 200 wide,
 * slot 17h = slot 6, optional button icon (no strip).
 *
 * Original: IST.EXE 05DC:0308 (INTRFACE.L), far cdecl (int x, int y, int
 * h, u8 icon); record +04.
 * Verified: tests/py/test_intrface.py (test_panel).
 */
void ui_draw_panel_0308(int16_t x, int16_t y, int16_t h, int16_t icon)
{
    (void)x; (void)y; (void)h;
    panel_zone(0xC8);
    mm_copy_block(mm_ref(SURF_REF(6)), mm_ref(SURF_REF(S17)));
    panel_icon(icon);
}

/*
 * ui_setup_hotspots - window frame callback of the character panel: slot
 * 4Dh is copied to the bottom of slot 6 (dst offset w6 * (h6 - h4Dh),
 * signed 16-bit factors), the current member's conditions 1-5 (non-zero
 * words at 99D2 + 5Ch*member) are printed with iso_draw_text_shadow
 * (name 361h + 1Fh*(v % 100), at (20, 19+i), colour 9666, 7), and hotspots
 * 38/39 become the two arrow buttons (5,91h)-(0Fh,A6h), (5,A7h)-(0Fh,BBh).
 *
 * Original: IST.EXE 05DC:041C (INTRFACE.L), far cdecl; record +04.
 * Verified: tests/py/test_intrface.py (test_hotspots, iso_draw_text_shadow
 * stubbed on both sides).
 */
void ui_setup_hotspots(void)
{
    int16_t i;
    MmHandle *src = mm_ref(SURF_REF(0x4D));
    mm_copy_range(src, mm_ref(SURF_REF(6)), 0,
                  (uint32_t)((int32_t)g_surf_w(6) * (int32_t)(int16_t)(g_surf_h(6) - g_surf_h(0x4D))),
                  src->size);
    for (i = 1; i <= 5; i++) {
        int16_t v = g_party_word(g_party_cur, i);
        if (v != 0)
            ((FnShadow)ext(UI_EXT_DRAW_TEXT_SHADOW))(
                DGSTR((uint16_t)(UI_COND_NAMES + 0x1F * (v % 100))), 0x14,
                (int16_t)(i + 0x13), g_ui_col_text, 7);
    }
    g_zone_x1(0x26) = 5;    g_zone_y1(0x26) = 0x91; g_zone_x2(0x26) = 0x0F; g_zone_y2(0x26) = 0xA6;
    g_zone_x1(0x27) = 5;    g_zone_y1(0x27) = 0xA7; g_zone_x2(0x27) = 0x0F; g_zone_y2(0x27) = 0xBB;
}

/*
 * ui_rect_op_0568 - item hotspot n: (col*8, row*h) - (+8Ah, +h+9).
 * Original: IST.EXE 05DC:0568 (INTRFACE.L), far cdecl (n, col, row, h);
 * record +0C.
 * Verified: tests/py/test_intrface.py (test_small).
 */
void ui_rect_op_0568(int16_t n, int16_t col, int16_t row, int16_t h)
{
    g_zone_x1(n) = (int16_t)(col << 3);
    g_zone_y1(n) = (int16_t)(row * h);
    g_zone_x2(n) = (int16_t)(g_zone_x1(n) + 0x8A);
    g_zone_y2(n) = (int16_t)(g_zone_y1(n) + h + 9);
}

/*
 * ui_rect_op_05E4 - item hotspot n on the panel: (col*8, row*h + panel y
 * - 8) - (+8Ah, +h+9).
 * Original: IST.EXE 05DC:05E4 (INTRFACE.L), far cdecl (n, col, row, h);
 * record +0C.
 * Verified: tests/py/test_intrface.py (test_small).
 */
void ui_rect_op_05E4(int16_t n, int16_t col, int16_t row, int16_t h)
{
    g_zone_x1(n) = (int16_t)(col << 3);
    g_zone_y1(n) = (int16_t)(row * h + g_ui_panel_y - 8);
    g_zone_x2(n) = (int16_t)(g_zone_x1(n) + 0x8A);
    g_zone_y2(n) = (int16_t)(g_zone_y1(n) + h + 9);
}

/*
 * ui_update_hover - list-drawn callback: panel hotspot 320 wide, arrow
 * hotspots 38/39 at the panel's left edge ((0, y+10) and (0, y+26), 16x16),
 * then the panel background (slot 17h) is copied to the screen at (0,
 * panel y) with the cursor hidden.
 *
 * Original: IST.EXE 05DC:066B (INTRFACE.L), far cdecl (int x, int y, int
 * h); record +08.  The hide depends on the cursor being over the panel
 * zone; the final set_shape always runs.  Ends with a dead store
 * x = h/46*82 + x + y + panel y (IDIV by 2Eh; h is never 0 there).
 * Verified: tests/py/test_intrface.py (test_panel).
 */
void ui_update_hover(int16_t x, int16_t y, int16_t h)
{
    (void)x; (void)y; (void)h;
    panel_zone(0x140);
    g_zone_x1(0x26) = 0;
    g_zone_y1(0x26) = (int16_t)(g_ui_panel_y + 0x0A);
    g_zone_x2(0x26) = (int16_t)(g_zone_x1(0x26) + 0x10);
    g_zone_y2(0x26) = (int16_t)(g_zone_y1(0x26) + 0x10);
    g_zone_x1(0x27) = 0;
    g_zone_y1(0x27) = (int16_t)(g_ui_panel_y + 0x1A);
    g_zone_x2(0x27) = (int16_t)(g_zone_x1(0x27) + 0x10);
    g_zone_y2(0x27) = (int16_t)(g_zone_y1(0x27) + 0x10);
    if (gr_cursor_in_zone((uint8_t)g_ui_panel_zone))
        gr_cursor_hide();
    sys_blit(0, 0, (uint16_t)g_surf_w(S17), (uint16_t)g_surf_h(S17), 0,
             (uint16_t)g_ui_panel_y, S17, 0, sys_rect_copy);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * ui_unhighlight - selection-moved callback: copy the panel background to
 * the screen (cursor hidden).
 * Original: IST.EXE 05DC:0831 (INTRFACE.L), far cdecl; record +10.
 * Verified: tests/py/test_intrface.py (test_panel).
 */
void ui_unhighlight(void)
{
    gr_cursor_hide();
    sys_blit(0, 0, (uint16_t)g_surf_w(S17), (uint16_t)g_surf_h(S17), 0,
             (uint16_t)g_ui_panel_y, S17, 0, sys_rect_copy);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * ui_free_panel - close callback: slot 17h = slot 6, free slot 6's block
 * (ctl_free_checked, line 81h), restore the screen under the panel.
 * Original: IST.EXE 05DC:0881 (INTRFACE.L), far cdecl; record +14.
 * Verified: tests/py/test_intrface.py (test_panel).
 */
void ui_free_panel(void)
{
    mm_copy_block(mm_ref(SURF_REF(6)), mm_ref(SURF_REF(S17)));
    ctl_free_checked(SURF_REF(6), 0x81, DGSTR(0x6C38));
    gr_cursor_hide();
    ui_restore_background();
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * ui_memcpy - copy one 48h-byte window record.
 * Original: IST.EXE 05DC:08DB (INTRFACE.L), far cdecl (dst, src):
 * asm_memcpy32(dst, src, 48h).
 * Verified: through ui_init_tables (test_init_tables).
 */
void ui_memcpy(uint8_t *dst, const uint8_t *src)
{
    asm_memcpy32(dst, src, UI_WIN_SIZE);
}

/*
 * ui_cd_music - idle callback: keep the CD audio going (o5737_cd_play;
 * 0 = restart track 0 to the end, FFh = fatal 39h "line 91h"), then
 * s3d_apply_sound_options(arg).
 * Original: IST.EXE 05DC:08F6 (INTRFACE.L), far cdecl (void far *arg);
 * record +20.
 * Verified: tests/py/test_intrface.py (test_cd_music, CD stubbed).
 */
void ui_cd_music(uint32_t arg)
{
    uint8_t r = o5737_cd_play();
    if (r == 0)
        o5737_cd_play_track(0, -1L);
    else if (r == 0xFF)
        o559A_fatal(DGSTR(0x6C4A), 0x91, NULL, 0x39);
    ((FnIdle)ext(UI_EXT_SOUND_OPTIONS))(arg);
}

/*
 * ui_init_tables - fill the window records 9674..98FB (code pointers,
 * coordinates, colours, ids 0-8 at +46h).
 *
 * Original: IST.EXE 05DC:0940 (INTRFACE.L), far cdecl.  A straight list of
 * stores, kept in order: 9824 is built first and copied (ui_memcpy) to
 * 96BC, 97DC, 9704, 9674 before the others are patched.  Far code
 * pointers are stored relocated (see intrface.h).
 * Verified: tests/py/test_intrface.py (test_init_tables: all of DGROUP).
 */
void ui_init_tables(void)
{
    uint8_t id = 0;
    DG8(0x986A) = id++;
    ui_far_set(0x9824, 0x05DC, 0x00E9);
    ui_far_set(0x9828, 0x05DC, 0x027B);
    ui_far_set(0x9830, 0x05DC, 0x05E4);
    ui_far_set(0x982C, 0x05DC, 0x066B);
    ui_far_set(0x9834, 0x05DC, 0x0831);
    ui_far_set(0x9838, 0x05DC, 0x0881);
    ui_far_set(0x983C, 0x05DC, 0x0004);
    ui_far_set(0x9848, 0x1670, 0x0084);
    DG16(0x9850) = 0x0094;
    DG16(0x9852) = 0x0034;
    DG16(0x9854) = 0x0025;
    DG8(0x985E) = 0xF5;
    DG8(0x9861) = 0xFA;
    DG8(0x9862) = 0xF0;
    DG8(0x9863) = 0xA0;
    DG8(0x9864) = 0xAA;
    ui_far_set(0x984C, 0x1670, 0x00A2);
    DG8(0x9860) = 0x40;
    ui_far_set(0x9844, 0x1670, 0x009D);
    DG8(0x985D) = 0xFF;
    DG8(0x985C) = 0xDC;
    DG8(0x985F) = 0x00;
    DG16(0x9856) = 0x00C8;
    DG8(0x9869) = 0x2C;
    ui_far_set(0x9840, 0x1689, 0x003E);
    DG8(0x986B) = 0x01;
    ui_memcpy(DGP(0x96BC), DGP(0x9824));
    ui_memcpy(DGP(0x97DC), DGP(0x9824));
    ui_memcpy(DGP(0x9704), DGP(0x9824));
    ui_memcpy(DGP(0x9674), DGP(0x9824));

    DG8(0x9792) = id++;
    ui_far_set(0x974C, 0x05DC, 0x00CD);
    ui_far_set(0x9750, 0x1694, 0x0043);
    ui_far_set(0x97A0, 0x05DC, 0x0568);
    ui_far_set(0x9758, 0x05DC, 0x0568);
    ui_far_set(0x979C, 0x1694, 0x0048);
    ui_far_set(0x9754, 0x1694, 0x0048);
    ui_far_set(0x97A8, 0x1694, 0x004D);
    ui_far_set(0x9760, 0x1694, 0x004D);
    ui_far_set(0x97A4, 0x1694, 0x004D);
    ui_far_set(0x975C, 0x1694, 0x004D);
    ui_far_set(0x97AC, 0x1694, 0x0052);
    ui_far_set(0x9764, 0x1694, 0x0052);
    DG8(0x9789) = 0x2A;
    DG8(0x978A) = 0x88;
    DG8(0x978B) = 0xAA;
    ui_far_set(0x9774, 0x1694, 0x0106);

    DG8(0x97DA) = id++;
    ui_far_set(0x9794, 0x05DC, 0x00CD);
    ui_far_set(0x9798, 0x05DC, 0x041C);
    DG8(0x97CE) = 0xF0;
    DG8(0x97D1) = 0xAF;
    DG8(0x97D2) = 0xB0;
    DG8(0x97D3) = 0xCC;
    DG8(0x97D4) = 0xA0;
    DG8(0x97D5) = 0xD9;
    DG8(0x97CD) = 0xFF;
    DG8(0x9785) = 0xFF;
    DG8(0x9784) = 0xDC;
    DG8(0x97CC) = 0xDC;
    ui_far_set(0x9770, 0x05DC, 0x00D9);
    ui_far_set(0x97B8, 0x05DC, 0x00D9);
    ui_far_set(0x98D8, 0x05DC, 0x00D9);
    ui_far_set(0x9890, 0x05DC, 0x00D9);
    ui_far_set(0x9728, 0x05DC, 0x00D9);
    ui_far_set(0x9894, 0x05DC, 0x00CD);
    ui_far_set(0x98DC, 0x05DC, 0x00CD);
    ui_far_set(0x97BC, 0x05DC, 0x00CD);
    ui_far_set(0x9804, 0x05DC, 0x00CD);
    ui_far_set(0x972C, 0x05DC, 0x00CD);
    DG8(0x9816) = 0xF5;
    DG8(0x9786) = 0xF5;
    DG8(0x9817) = 0x00;
    DG8(0x97CF) = 0x00;
    DG8(0x9787) = 0x00;
    DG16(0x97C6) = 0x00C8;
    DG16(0x977E) = 0x00C8;
    ui_far_set(0x97B4, 0x05DC, 0x00CD);
    ui_far_set(0x976C, 0x05DC, 0x00CD);
    DG8(0x97D9) = 0x2D;
    DG8(0x9791) = 0x2D;
    DG8(0x9821) = 0x2C;
    DG8(0x97D0) = 0x00;
    DG8(0x9788) = 0x00;
    DG8(0x98F0) = 0x00;
    ui_far_set(0x97F8, 0x05DC, 0x00CD);
    ui_far_set(0x97B0, 0x05DC, 0x00CD);
    ui_far_set(0x9888, 0x05DC, 0x00CD);
    ui_far_set(0x98D0, 0x05DC, 0x00CD);
    ui_far_set(0x9768, 0x05DC, 0x00CD);

    DG8(0x98FA) = id++;
    ui_far_set(0x98B4, 0x05DC, 0x0167);
    ui_far_set(0x98B8, 0x05DC, 0x0308);
    ui_far_set(0x98C0, 0x05DC, 0x05E4);
    ui_far_set(0x98BC, 0x05DC, 0x066B);
    ui_far_set(0x98C4, 0x05DC, 0x0831);
    ui_far_set(0x98C8, 0x05DC, 0x0881);
    ui_far_set(0x98CC, 0x05DC, 0x0004);
    DG16(0x98E0) = 0x0087;
    DG16(0x98E2) = 0x003C;
    DG16(0x98E4) = 0x0025;
    DG8(0x98ED) = 0xF8;
    DG8(0x98EC) = 0xFF;
    DG8(0x98EE) = 0x1F;
    DG16(0x98E6) = 0x00BC;
    DG8(0x98F9) = 0x32;
    DG8(0x98EF) = 0xF4;
    ui_far_set(0x98D4, 0x05DC, 0x08F6);

    DG8(0x98B2) = id++;
    ui_far_set(0x986C, 0x05DC, 0x0167);
    ui_far_set(0x9870, 0x05DC, 0x0308);
    ui_far_set(0x9878, 0x05DC, 0x05E4);
    ui_far_set(0x9874, 0x05DC, 0x066B);
    ui_far_set(0x987C, 0x05DC, 0x0831);
    ui_far_set(0x9880, 0x05DC, 0x0881);
    ui_far_set(0x9884, 0x05DC, 0x0004);
    DG16(0x9898) = 0x0087;
    DG16(0x989A) = 0x003C;
    DG16(0x989C) = 0x0025;
    DG8(0x98A5) = 0xFC;
    DG8(0x98A4) = 0xFE;
    DG8(0x98A6) = 0xF5;
    DG8(0x98B1) = 0x33;
    DG8(0x98A8) = 0xFA;
    DG16(0x989E) = 0x009F;
    ui_far_set(0x988C, 0x1670, 0x009D);

    DG8(0x9822) = id++;
    ui_far_set(0x9800, 0x1689, 0x0098);
    DG16(0x9808) = 0x0000;
    DG16(0x980A) = 0x003E;
    DG16(0x980C) = 0x001D;

    DG8(0x974A) = id++;
    DG8(0x9702) = id++;
    ui_far_set(0x96D8, 0x05DC, 0x00CD);
    DG8(0x9703) = 0x02;
    DG8(0x96BA) = id++;
    ui_far_set(0x9694, 0x05DC, 0x08F6);
}

/*
 * ui_event_loop - run a list menu with the callbacks of the current window
 * record (962C).  Entries are the strings UI_LIST_ENTRY(1..n); the result
 * is g_ui_list_sel (0 = cancelled with Esc or the right button).
 *
 * Original: IST.EXE 05DC:1133 (INTRFACE.L), far cdecl (int n, int x, int
 * y, int col_item, int col_sel, int col_done, int count, int style).
 * style 1 uses 11-pixel rows, else 7 (font 2).  x is raised to 2 and the
 * starting selection is max(1, min(sel, x, n)) - x, not count (kept).
 * The open callback may shrink count (visible rows).  Up/Down (scan 48h,
 * 50h, read with bioskey(1) >> 8 without removing the key) or a left
 * click on hotspot 38/39 move the selection, scrolling the list at its
 * ends (iso_clamp_add on sel and top); the mouse over item hotspots 1..34
 * selects (with a key held the mouse is moved to the item instead); Enter,
 * a left click on an item, Esc or the right button end it.  After a key
 * the keyboard buffer is flushed (that is what consumes the peeked key).
 * Port: loops poll through the idle callback / ui_bioskey; mouse button
 * waits call pc_poll().
 * Verified: tests/py/test_intrface.py (test_event_loop: keys and clicks,
 * the game callbacks stubbed on both sides).
 */
void ui_event_loop(int16_t n, int16_t x, int16_t y, int16_t col_item, int16_t col_sel,
                   int16_t col_done, int16_t count, int16_t style)
{
    int16_t h = style == 1 ? 0x0B : 7;
    int16_t last_zone = -3, z, top, i, row, m;

    g_ui_list_top = 1;
    x = x > 2 ? x : 2;
    m = g_ui_list_sel < x ? g_ui_list_sel : x;
    m = m < n ? m : n;
    g_ui_list_sel = g_ui_list_prev = m > 1 ? m : 1;
    ((FnOpen)cb(UI_CB_OPEN))(&count, n);
redraw:
    gr_zones_clear();
    ((FnI4)cb(UI_CB_FRAME))(x, y, h, (int16_t)(n > count));
    top = g_ui_list_top;
    for (i = 1; (int16_t)(g_ui_list_top + count - 1) >= top && i < 0x23; i++, top++) {
        row = (int16_t)(y + i);
        item(top, x, row, col_item, h);
        ((FnI4)cb(UI_CB_ITEM_ZONE))(top, x, row, h);
    }
    item(g_ui_list_sel, x, (int16_t)(g_ui_list_sel - g_ui_list_top + y + 1), col_sel, h);
    ((FnI3)cb(UI_CB_LIST_DONE))(x, y, h);
flush:
    ui_flush_keys();
    for (;;) {
        idle();
        g_ui_list_key = (int16_t)(ui_bioskey(1) >> 8);
        gr_cursor_update((uint8_t)g_cursor_shape);
        z = gr_zone_under_cursor();
        if (g_ui_list_key == 0x48 || (g_mouse_buttons == 1 && gr_cursor_in_zone(0x26))) {
            if (g_ui_list_sel == g_ui_list_top && n != count) {
                clamp_add(0xD5DC, -1, 1, n);
                clamp_add(0xD5D8, -1, 1, (int16_t)(n - count + 1));
                g_ui_list_prev = g_ui_list_sel;
            } else {
                clamp_add(0xD5DC, -1, 1, n);
            }
            if (g_mouse_buttons == 1)
                while (g_mouse_buttons != 0) {
                    gr_cursor_update((uint8_t)g_cursor_shape);
                    pc_poll();
                }
            goto redraw;
        }
        if (g_ui_list_key == 0x50 || (g_mouse_buttons == 1 && gr_cursor_in_zone(0x27))) {
            if (g_ui_list_sel == (int16_t)(g_ui_list_top + count - 1) && n != count) {
                clamp_add(0xD5DC, 1, 1, n);
                clamp_add(0xD5D8, 1, 1, (int16_t)(n - count + 1));
                g_ui_list_prev = g_ui_list_sel;
            } else {
                clamp_add(0xD5DC, 1, 1, n);
            }
            if (g_mouse_buttons == 1)
                while (g_mouse_buttons != 0) {
                    gr_cursor_update((uint8_t)g_cursor_shape);
                    pc_poll();
                }
            goto redraw;
        }
        if (g_ui_list_key == 1 || g_mouse_buttons == 2) {
            g_ui_list_prev = g_ui_list_top = g_ui_list_sel = 0;
            break;
        }
        if (g_ui_list_key == 0x1C)
            break;
        if (g_mouse_buttons == 1 && z < 0x23 && z != 0)
            break;
        if (g_ui_list_sel != z && z < 0x23 && z != 0) {
            if (g_ui_list_key != 0) {
                ogr_mouse_set_pos(g_cursor_x, g_zone_y1(g_ui_list_sel));
                gr_cursor_update((uint8_t)g_cursor_shape);
            } else if (last_zone != z) {
                g_ui_list_sel = gr_zone_under_cursor();
                last_zone = z;
            }
        }
        if (g_ui_list_prev != g_ui_list_sel && g_ui_list_sel < 0x23) {
            item(g_ui_list_prev, x, (int16_t)(y + g_ui_list_prev - g_ui_list_top + 1),
                 col_item, h);
            item(g_ui_list_sel, x, (int16_t)(y + g_ui_list_sel - g_ui_list_top + 1),
                 col_sel, h);
            ((FnVoid)cb(UI_CB_MOVED))();
            g_ui_list_prev = g_ui_list_sel;
        }
        if (g_ui_list_key != 0)
            goto flush;
    }
    gr_wait_buttons_up();
    item(g_ui_list_sel, x, (int16_t)(g_ui_list_sel - g_ui_list_top + y + 1), col_done, h);
    ((FnVoid)cb(UI_CB_CLOSE))();
    ui_flush_keys();
}

/*
 * ui_init_voices - (un)load the voice samples of sound slots 1..30h for
 * the voice mode (9672): a slot whose table flags have bit 0 is freed;
 * mode 0 keeps the slots without flag 4, mode 1 those without flag 2,
 * mode 3 those with flags 2 and 4, other modes none.  Kept slots are loaded
 * (oses_load_sample(1, name, slot, 0, 0, 0)) if not yet loaded, the others
 * freed (ctl_free_checked line 19Dh).  A failed load is reported only when
 * the formatted message is >= 68h characters (fatal line 197h).
 *
 * Original: IST.EXE 05DC:178E (INTRFACE.L), far cdecl.  Nothing without
 * sound.
 * Verified: tests/py/test_intrface.py (test_voices).
 */
void ui_init_voices(void)
{
    int16_t i;
    if (g_sound_on == 0)
        return;
    for (i = 1; i < 0x31; i++) {
        uint16_t f = g_snd_table_flags(i);
        int keep;
        if (f & 1)
            keep = 0;
        else
            switch (g_ui_voice_mode) {
            case 0:  keep = !(f & 4); break;
            case 1:  keep = !(f & 2); break;
            case 3:  keep = (f & 2) && (f & 4); break;
            default: keep = 0; break;
            }
        if (keep) {
            if (mm_ref(SND_SLOT_MM_REF(i)) == NULL) {
                int16_t r = oses_load_sample(1, g_snd_name(i), i, 0, 0, 0);
                if (r != 0 &&
                    sprintf(g_ui_msg_buf, DGSTR(0x6C5C), g_snd_name(i), i,
                            r == 1 ? DGSTR(0x6C8A) : DGSTR(0x6C97)) >= 0x68)
                    o559A_fatal(DGSTR(0x6CA3), 0x197, g_ui_msg_buf, -1);
            }
        } else if (mm_ref(SND_SLOT_MM_REF(i)) != NULL) {
            ctl_free_checked(SND_SLOT_MM_REF(i), 0x19D, DGSTR(0x6CB5));
        }
    }
}

/*
 * ui_load_voice - load (if needed) and start the voice sample `slot` on
 * the last two channels; the voice's wait is len clock units.
 *
 * Original: IST.EXE 05DC:192D (INTRFACE.L), far cdecl (int slot, long
 * freq, int len, int flags).  flags are OR-ed with the slot's table flags:
 * 10h: freq += (int)(((u32)rand() * (freq/5)) >> 15) - freq/10 (unsigned
 * divisions, the shifted product truncated to 16 bits and sign-extended);
 * 8: wait for the voice and play the continuation slots (flag 200h, EMS
 * pieces) one after another with the same frequency; else the next slot's
 * rate is set to freq (when it is a continuation).  A failed load is
 * always fatal (line 1BCh; the length check of the message is dead).
 * 73CC = the sample's length, 73D1 = clock + len, 73D5 = len.
 * Verified: tests/py/test_intrface.py (test_voices).
 */
void ui_load_voice(int16_t slot, uint32_t freq, uint16_t len, uint16_t flags)
{
    uint8_t ch;
    if (g_sound_on == 0)
        return;
    if (mm_ref(SND_SLOT_MM_REF(slot)) == NULL) {
        int16_t r = oses_load_sample(1, g_snd_name(slot), slot, 0, 0, 0);
        if (r != 0) {
            sprintf(g_ui_msg_buf, DGSTR(0x6CC7), g_snd_name(slot), slot,
                    r == 1 ? DGSTR(0x6CE9) : DGSTR(0x6CF6));
            o559A_fatal(DGSTR(0x6D02), 0x1BC, g_ui_msg_buf, -1);
        }
    }
    if ((g_snd_table_flags(slot) | flags) & 0x10) {
        int16_t r = bc_rand();
        uint32_t p = (uint32_t)(int32_t)r * (freq / 5);
        freq = freq + (uint32_t)(int32_t)(int16_t)(uint16_t)(p >> 15) - freq / 10;
    }
    for (;;) {
        ch = g_snd_channels;
        cdiStopChannel((int32_t)(int16_t)(ch - 1));
        cdiStopChannel((int32_t)(int16_t)(ch - 2));
        snd_play_sample(slot, (int32_t)freq, 0x40, 0x64, (uint8_t)(g_snd_channels - 1));
        snd_play_sample(slot, (int32_t)freq, 0x40, 0x64, (uint8_t)(g_snd_channels - 2));
        g_voice_sample_end = g_snd_si_length(slot);
        g_voice_end_time = g_clock_ms + len;
        g_voice_len = len;
        if (!((g_snd_table_flags(slot) | flags) & 8)) {
            if (g_snd_table_flags(slot++) & 0x200)
                g_snd_rate(slot) = (uint16_t)freq;
            return;
        }
        ui_wait_voice();
        if (!(g_snd_table_flags(slot++) & 0x200))
            return;
    }
}

/* free a voice slot unless its flags (or the caller's) have bit 0 */
static void drop_voice(int16_t slot, uint16_t flags, int16_t line, uint16_t file)
{
    if (!((g_snd_table_flags(slot) | flags) & 1) && mm_ref(SND_SLOT_MM_REF(slot)) != NULL) {
        ctl_free_checked(SND_SLOT_MM_REF(slot), line, DGSTR(file));
        g_snd_table_flags(slot) &= 0xFDFF;
    }
}

/*
 * ui_play_voice - finish the voice started on `slot`: wait for it, then
 * play its continuation slots (flag 200h, loaded) one by one at their
 * rate with a 1F4h wait, freeing each played piece unless flag 1.
 * Returns 1 if Esc cut the voice short.
 *
 * Original: IST.EXE 05DC:1B6A (INTRFACE.L), far cdecl (int slot, int
 * flags).  Nothing (0) without sound and no pending wait.  The next
 * piece's rate is copied from the current one first.  After an Esc
 * (r = 1) the remaining pieces are still started and freed, but no longer
 * waited for.
 * Verified: tests/py/test_intrface.py (test_voices).
 */
uint8_t ui_play_voice(int16_t slot, uint16_t flags)
{
    uint8_t r;
    if (g_sound_on == 0 && g_voice_end_time == 0xFFFFFFFFu)
        return 0;
    r = (uint8_t)ui_wait_voice();
    while ((g_snd_table_flags(slot) & 0x200) && mm_ref(SND_SLOT_MM_REF(slot + 1)) != NULL) {
        drop_voice(slot, flags, 0x1DE, 0x6D14);
        slot++;
        if (g_snd_table_flags(slot) & 0x200)
            g_snd_rate(slot + 1) = g_snd_rate(slot);
        ui_load_voice(slot, g_snd_rate(slot), 0x1F4, 0);
        if (r == 0)
            r = (uint8_t)ui_wait_voice();
    }
    drop_voice(slot, flags, 0x1E9, 0x6D26);
    return r;
}

/*
 * ui_wait_voice - wait until the voice's time (73D1) has come (calling
 * the idle callback), clear it, then with sound on wait until the voice
 * channel's position + 20h reaches the sample's length (Esc: return 1)
 * and set the silence SAMPLEINFO (DF1E) on it.  Returns 0.
 *
 * Original: IST.EXE 05DC:1D1B (INTRFACE.L), far cdecl.  The remaining time
 * is a 32-bit difference; the first wait only runs while its high word is
 * 0 and the low word <= 73D5, and it stops after the idle call that saw
 * a difference of 0.  No pending time (FFFFFFFFh): return 0 at once.
 * Port: polls through the idle callback's pc calls and ui_kbhit; the
 * SAMPLEINFO at DF1E is converted like nses.c's snd_sampleinfo.
 * Verified: tests/py/test_intrface.py (test_voices).
 */
int16_t ui_wait_voice(void)
{
    CdiSampleInfo si;
    if (g_voice_end_time == 0xFFFFFFFFu)
        return 0;
    for (;;) {
        uint32_t d = g_voice_end_time - g_clock_ms;
        if ((d >> 16) != 0 || (uint16_t)d > g_voice_len)
            break;
        idle();
        pc_poll();
        if ((uint16_t)d == 0)
            break;
    }
    g_voice_end_time = 0xFFFFFFFFu;
    g_voice_len = 0;
    if (g_sound_on == 0)
        return 0;
    while ((uint32_t)(cdiGetPosition((int32_t)(int16_t)(g_snd_channels - 1)) + 0x20) <
           g_voice_sample_end) {
        if (ui_kbhit() && ui_getch() == 0x1B)
            return 1;
        idle();
    }
    memset(&si, 0, sizeof si);
    if (DG16(UI_VOICE_SILENCE + 2) == 0xFFFF) {
        si.sample.in_ems = 1;
        si.sample.ems_handle = DG16(UI_VOICE_SILENCE);
    }
    si.length = DG32(UI_VOICE_SILENCE + 0x04);
    si.loopstart = DG32(UI_VOICE_SILENCE + 0x08);
    si.loopend = DG32(UI_VOICE_SILENCE + 0x0C);
    si.mode = DG8(UI_VOICE_SILENCE + 0x10);
    si.sampleID = DG16(UI_VOICE_SILENCE + 0x11);
    si.rate = DG16(UI_VOICE_SILENCE + 0x13);
    si.freq = DG32(UI_VOICE_SILENCE + 0x15);
    si.volume = DG8(UI_VOICE_SILENCE + 0x19);
    cdiSetSample((int32_t)(int16_t)(g_snd_channels - 1), &si);
    g_voice_sample_end = 0;
    return 0;
}

/*
 * ui_wait_frames - n times: wait until the low clock word has advanced
 * by (u16)(BB35 * (BB35+1)), updating the cursor.
 * Original: IST.EXE 05DC:1E5E (INTRFACE.L), far cdecl (int n).
 * Port: pc_poll() per iteration.
 * Verified: tests/py/test_intrface.py (test_waits).
 */
void ui_wait_frames(int16_t n)
{
    while (n-- != 0) {
        uint16_t t0 = (uint16_t)g_clock_ms;
        do {
            gr_cursor_update((uint8_t)g_cursor_shape);
            pc_poll();
        } while ((uint16_t)(g_ui_frame_ticks * (g_ui_frame_ticks + 1)) >
                 (uint16_t)((uint16_t)g_clock_ms - t0));
    }
}

/*
 * ui_restore_background - put the saved screen rows under the panel back
 * (slot 17h -> screen at panel y, E355 rows of 320) and free the save;
 * always resets the message line count (BBAA).
 * Original: IST.EXE 05DC:1EB9 (INTRFACE.L), far cdecl.  Offsets are
 * 16-bit products (panel y * 320, rows * 320), zero-extended.
 * Verified: tests/py/test_intrface.py (test_panel).
 */
void ui_restore_background(void)
{
    if (mm_ref(SURF_REF(S17)) != NULL) {
        if (gr_cursor_in_zone((uint8_t)g_ui_panel_zone))
            gr_cursor_hide();
        mm_copy_range(mm_ref(SURF_REF(S17)), mm_surface(0), 0,
                      (uint16_t)(g_ui_panel_y * 0x140), (uint16_t)(g_surf_h(S17) * 0x140));
        ctl_free_checked(SURF_REF(S17), 0x224, DGSTR(0x6D38));
        gr_cursor_set_shape((uint8_t)g_cursor_shape);
    }
    g_ui_msg_lines = 0;
}

/*
 * ui_save_background - save the screen rows under the panel to slot 17h
 * (allocated 320 x panel h, conventional, if there is none) and call the
 * record's fill callback (0, panel y, 320, panel h, 0, 0).
 * Original: IST.EXE 05DC:1F6A (INTRFACE.L), far cdecl.  Size and source
 * offset are unsigned 16x16 products, the length rows * 320 is signed.
 * Verified: tests/py/test_intrface.py (test_panel).
 */
void ui_save_background(void)
{
    if (gr_cursor_in_zone((uint8_t)g_ui_panel_zone))
        gr_cursor_hide();
    if (mm_ref(SURF_REF(S17)) == NULL) {
        g_surf_w(S17) = 0x140;
        g_surf_h(S17) = g_ui_panel_h;
        mm_ref_set(SURF_REF(S17), mm_alloc((uint16_t)(g_surf_w(S17) * g_surf_h(S17)), 0));
    }
    mm_copy_range(mm_surface(0), mm_ref(SURF_REF(S17)), (uint32_t)(uint16_t)g_ui_panel_y * 0x140,
                  0, (uint32_t)((int32_t)g_surf_h(S17) * 0x140));
    ((FnI6)cb(UI_CB_FILL))(0, g_ui_panel_y, 0x140, g_ui_panel_h, 0, 0);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * ui_poll_input - wait for a choice: F1-F4 -> 1-4, a left click on hotspot
 * base..base+3 -> 1-4 (other clicks wait again), Esc / the right button
 * -> 0.
 * Original: IST.EXE 05DC:2083 (INTRFACE.L), far cdecl (u8 base).  Keys via
 * bioskey(1)/bioskey(0); after any key the buffer is flushed.  A click
 * after a key in the same round wins.  Idle callback each round, cursor
 * updated while it is shown.
 * Verified: tests/py/test_intrface.py (test_poll_input).
 */
uint8_t ui_poll_input(uint8_t base)
{
    uint8_t r = 0x64;
    while (r == 0x64) {
        idle();
        if (g_cursor_visible != 0)
            gr_cursor_update((uint8_t)g_cursor_shape);
        if (ui_bioskey(1) != 0) {
            int16_t k = ui_bioskey(0);
            if ((k & 0xFF) == 0x1B)
                r = 0;
            ui_flush_keys();
            k = (int16_t)(k >> 8);
            if (k == 1)
                r = 0;
            else if (k >= 0x3B && k <= 0x3E)
                r = (uint8_t)(k + 0xC6);
        }
        if (g_mouse_buttons == 1) {
            uint8_t z = (uint8_t)gr_zone_under_cursor();
            if (z >= base && (int16_t)z < (int16_t)(base + 4))
                r = (uint8_t)(z - base + 1);
            gr_wait_buttons_up();
        } else if (g_mouse_buttons == 2) {
            r = 0;
        }
    }
    return r;
}

/* shadowed text: (x+1, y+1) colour 0, then (x, y) colour 9666, big slot 5 */
static void shadow_text(int16_t x, int16_t y, const char *s)
{
    grt_draw_text(0, (int16_t)(x + 1), (int16_t)(y + 1), s, 0, 5);
    grt_draw_text(0, x, y, s, g_ui_col_text, 5);
}

/*
 * ui_dialog_2198 - two choices a / b on the panel's next line (centred on
 * x 80 and 240, first letters in colour 9665, hotspots 6 and 7); returns
 * through BB9E: 0 = a (click or its first letter), 1 = b.
 *
 * Original: IST.EXE 05DC:2198 (INTRFACE.L), far cdecl (char far *a, char
 * far *b).  Both strings are Turkish-decoded in place.  Line =
 * min(BBAA+2, 6) (BBAA updated), y = (line-1)*8 + 94h.  x = 50h /
 * F0h - (u16)(strlen*10) >> 1.  Keys: kbhit, then bioskey(0) & FFh
 * lower-cased (grt_tolower_inplace on its low byte) against the
 * grt_char_to_ascii of each first letter.  Ends with
 * s3d_draw_party_panel and ui_restore_background.
 * Verified: tests/py/test_intrface.py (test_dialog).
 */
void ui_dialog_2198(char *a, char *b)
{
    char ca[2], cb_[2];
    int16_t xa, xb, y, k;
    uint8_t line;

    if (gr_cursor_in_zone((uint8_t)g_ui_panel_zone))
        gr_cursor_hide();
    if (mm_ref(SURF_REF(S17)) == NULL)
        ui_save_background();
    grt_decode_turkish_lower(a);
    grt_decode_turkish_lower(b);
    xa = (int16_t)(0x50 - ((uint16_t)(strlen(a) * 10) >> 1));
    xb = (int16_t)(0xF0 - ((uint16_t)(strlen(b) * 10) >> 1));
    line = (uint8_t)(g_ui_msg_lines + 2);
    g_ui_msg_lines = (int16_t)(g_ui_msg_lines + 2) < 6 ? line : 6;
    y = (int16_t)(((int16_t)g_ui_msg_lines - 1) * 8 + 0x94);
    shadow_text(xa, y, a);
    shadow_text(xb, y, b);
    sprintf(ca, DGSTR(0x6D4A), a[0]);      /* "%c" */
    grt_draw_text(0, xa, y, ca, g_ui_col_initial, 5);
    sprintf(cb_, DGSTR(0x6D4D), b[0]);
    grt_draw_text(0, xb, y, cb_, g_ui_col_initial, 5);
    g_zone_x1(6) = (int16_t)(xa - 3);
    g_zone_y1(6) = (int16_t)(y - 3);
    g_zone_x2(6) = (int16_t)(xa + grt_text_width(a, 1) + 3);
    g_zone_y2(6) = (int16_t)(y + 0x0D);
    g_zone_x1(7) = (int16_t)(xb - 3);
    g_zone_y1(7) = (int16_t)(y - 3);
    g_zone_x2(7) = (int16_t)(xb + grt_text_width(b, 1) + 3);
    g_zone_y2(7) = (int16_t)(y + 0x0D);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
    ca[0] = (char)grt_char_to_ascii((uint8_t)ca[0]);
    cb_[0] = (char)grt_char_to_ascii((uint8_t)cb_[0]);
    g_ui_dialog_result = 0x64;
    while (g_ui_dialog_result == 0x64) {
        idle();
        if (g_cursor_visible != 0)
            gr_cursor_update((uint8_t)g_cursor_shape);
        if (g_mouse_buttons == 1) {
            int16_t z = gr_zone_under_cursor();
            if (z == 6)
                g_ui_dialog_result = 0;
            else if (z == 7)
                g_ui_dialog_result = 1;
            gr_wait_buttons_up();
        }
        if (ui_kbhit()) {
            char kc[2];
            k = (int16_t)(ui_bioskey(0) & 0xFF);
            kc[0] = (char)k;
            kc[1] = 0;
            grt_tolower_inplace(kc);
            k = (int16_t)((k & 0xFF00) | (uint8_t)kc[0]);
            if ((uint8_t)ca[0] == k)
                g_ui_dialog_result = 0;
            else if ((uint8_t)cb_[0] == k)
                g_ui_dialog_result = 1;
        }
    }
    ((FnVoid)ext(UI_EXT_PARTY_PANEL))();
    ui_restore_background();
}

/*
 * ui_format_text - draw the first line of s (up to the first B6h or 0Ah,
 * temporarily cut there) at (x, y) of surface dst with font 1; *pos +=
 * the line's length.  Returns 1 if s had no line break (last line).
 *
 * Original: IST.EXE 05DC:25EC (INTRFACE.L), far cdecl (u8 dst, char far
 * *s, int x, int y, u8 color, int far *pos, u8 flags).  flags 1: right
 * aligned at x (x -= width + 1, at least 0); flags 2: outlined (x moved
 * 1 left (flags 1) or right, at least 0; outline colour 9668 at (-1,0),
 * (0,-1), black at (+1,+1), (0,+1), (+1,0), then the text), else a black
 * shadow at (+1,+1).  The breaks are found with _fmemchr over strlen
 * bytes, the earlier one wins.  The font-2 branches of the original are
 * dead (font constant 1).
 * Verified: tests/py/test_intrface.py (test_format_text).
 */
uint8_t ui_format_text(uint8_t dst, char *s, int16_t x, int16_t y, uint8_t color,
                       int16_t *pos, uint8_t flags)
{
    uint8_t last = 1;
    char saved = 0;
    uint32_t idx = 0xFFFFFFFFu;
    size_t len = strlen(s);
    char *p = memchr(s, 0xB6, len), *q;

    if (p)
        idx = (uint32_t)(p - s);
    q = memchr(s, 0x0A, strlen(s));
    if (q && (uint32_t)(q - s) < idx)
        p = q;
    if (p) {
        idx = (uint32_t)(p - s);
        if ((uint32_t)strlen(s) > idx) {
            saved = *p;
            *p = 0;
            last = 0;
        }
    } else {
        idx = (uint32_t)strlen(s);
    }
    *pos = (int16_t)(*pos + (uint16_t)idx);
    if ((flags & 1) == 1) {
        x = (int16_t)(x - (grt_text_width(s, 1) + 1));
        x = x < 0 ? 0 : x;
    }
    if ((flags & 2) == 2) {
        x = (flags & 1) == 1 ? (int16_t)(x - 1) : (int16_t)(x + 1);
        x = x < 0 ? 0 : x;
        grt_draw_text(dst, (int16_t)(x - 1), y, s, g_ui_col_outline, 5);
        grt_draw_text(dst, x, (int16_t)(y - 1), s, g_ui_col_outline, 5);
        grt_draw_text(dst, (int16_t)(x + 1), (int16_t)(y + 1), s, 0, 5);
        grt_draw_text(dst, x, (int16_t)(y + 1), s, 0, 5);
        grt_draw_text(dst, (int16_t)(x + 1), y, s, 0, 5);
    } else {
        grt_draw_text(dst, (int16_t)(x + 1), (int16_t)(y + 1), s, 0, 5);
    }
    grt_draw_text(dst, x, y, s, color, 5);
    if (p)
        *p = saved;
    return last;
}

/*
 * ui_message_box - show s on the panel: its first letter large at (4,
 * panel y + 5), then up to 6 lines (ui_format_text, x 17h for the first
 * two, 4 below, 8 pixels apart, while BBAA < 6).  mode 0 also shows the
 * talking party member's portrait (slot 46h + who, 30x30, random frame)
 * at (8, 6Eh) and waits: 150 clock units per frame, (lines*6 + 14h)
 * frames, ended early (after more than 3 frames) by Esc/Space/Enter or a
 * click on the panel.  Returns the frame counter (0 after a mode-0 wait;
 * else lines + 1).
 *
 * Original: IST.EXE 05DC:2948 (INTRFACE.L), far cdecl (char far *s, int
 * mode, int who).  s is Turkish-decoded in place; its first character is
 * upper-cased for the letter and lower-cased afterwards.  Letter big slot
 * 5 (mode != 0) or 54h; text colour 9665 (mode != 0) or 966C; outlined
 * when 9673 = 2.  Mode 0: the screen under the portrait is saved in slot
 * 58h (32x32, conventional; fatal 60h if it is in use) and put back at the
 * end (freed, line 332h); if all members 1-4 have HP <= 0, demo_game_over
 * runs; a member with HP <= 0 is replaced by a random one (1 + rand*4 /
 * 8000h, long arithmetic).  Each frame clears the portrait box (31x31,
 * colour 9665) and blits the 30x30 frame at x 1, 97 or 129 (rand: half
 * 1, else 97 / 129).
 * Port: the frame waits poll (pc_poll), keys via ui_kbhit/ui_getch.
 * Verified: tests/py/test_intrface.py (test_message_box).
 */
uint8_t ui_message_box(char *s, int16_t mode, int16_t who)
{
    int16_t pos = 0, n = 1, start, i, px = 8, py = 0x6E;
    char letter[2];

    grt_decode_turkish_lower(s);
    grt_toupper_inplace(s);
    ui_save_background();
    if (gr_cursor_in_zone((uint8_t)g_ui_panel_zone))
        gr_cursor_hide();
    letter[0] = s[0];
    letter[1] = 0;
    grt_draw_text(0, 4, (int16_t)(g_ui_panel_y + 5), letter, g_ui_col_initial,
                  mode != 0 ? 5 : 0x54);
    grt_tolower_inplace(s);
    for (i = 0; i < 6 && g_ui_msg_lines < 6; ) {
        int16_t at = pos++;
        if (ui_format_text(0, s + at + 1, i < 2 ? 0x17 : 4, (int16_t)(g_ui_panel_y + (i << 3)),
                           mode != 0 ? g_ui_col_initial : g_ui_col_text2, &pos,
                           g_ui_text_style == 2 ? 2 : 0))
            break;
        i++;
        g_ui_msg_lines++;
        n++;
    }
    gr_cursor_set_shape(g_ui_shape);
    if (mode != 0)
        return (uint8_t)n;

    if (mm_ref(SURF_REF(0x58)) != NULL)
        o559A_fatal(DGSTR(0x6D50), 0x306, NULL, 0x60);
    g_surf_w(0x58) = 0x20;
    g_surf_h(0x58) = 0x20;
    mm_ref_set(SURF_REF(0x58), mm_alloc((uint32_t)(int32_t)(int16_t)(g_surf_w(0x58) *
                                                                       g_surf_h(0x58)), 0));
    if (gr_cursor_in_rect(px, py, 0x20, 0x20))
        gr_cursor_hide();
    sys_blit((uint16_t)px, (uint16_t)py, 0x20, 0x20, 0, 0, 0, 0x58, sys_rect_copy);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
    for (i = 1; i < 5; i++)
        if (g_party_hp(i) > 0)
            break;
    if (i == 5)
        ((FnVoid)ext(UI_EXT_GAME_OVER))();
    while (g_party_hp(who) <= 0)
        who = (int16_t)(((int32_t)bc_rand() << 2) / 0x8000 + 1);
    n = (int16_t)(n * 6 + 0x14);
    start = n;
    g_mouse_buttons = 0;
    for (;;) {
        if (g_mouse_buttons != 0 && gr_cursor_in_zone((uint8_t)g_ui_panel_zone) &&
            (int16_t)(start - n) >= 3)
            break;
        {
            uint16_t t0 = (uint16_t)g_clock_ms, sx;
            while ((uint16_t)((uint16_t)g_clock_ms - t0) < 0x96) {
                gr_cursor_update((uint8_t)g_cursor_shape);
                pc_poll();
            }
            if (gr_cursor_in_rect(px, py, 0x20, 0x20))
                gr_cursor_hide();
            sys_fill_rect_checked(px, py, 0x1F, 0x1F, 0, g_ui_col_initial);
            if ((int32_t)bc_rand() * 2 / 0x8000 != 0)
                sx = 0;
            else
                sx = (uint16_t)(((int32_t)bc_rand() * 2 / 0x8000 + 3) << 5);
            sys_blit((uint16_t)(sx + 1), 1, 0x1E, 0x1E, (uint16_t)(px + 1), (uint16_t)(py + 1),
                     (uint8_t)(who + 0x46), 0, sys_rect_copy);
            gr_cursor_set_shape((uint8_t)g_cursor_shape);
        }
        if (n-- < 0)
            break;
        if (ui_kbhit()) {
            int16_t k = ui_getch();
            ui_flush_keys();
            if ((k == 0x1B || k == 0x20 || k == 0x0D) && (int16_t)(start - n) > 3)
                break;
        }
    }
    gr_wait_buttons_up();
    n = 0;
    if (mm_ref(SURF_REF(0x58)) != NULL) {
        gr_cursor_hide();
        sys_blit(0, 0, 0x20, 0x20, (uint16_t)px, (uint16_t)py, 0x58, 0, sys_rect_copy);
        gr_cursor_set_shape((uint8_t)g_cursor_shape);
        ctl_free_checked(SURF_REF(0x58), 0x332, DGSTR(0x6D62));
    }
    return (uint8_t)n;
}

/*
 * ui_call_9650 - cursor shape: the panel's (9671) over the panel zone,
 * else what the record's shape callback (9650) returns.
 * Original: IST.EXE 05DC:2D9A (INTRFACE.L), far cdecl; passed to
 * gr_call_then_cursor by ui_wait_click_or_key.
 * Verified: tests/py/test_intrface.py (test_waits).
 */
int16_t ui_call_9650(void)
{
    if (gr_cursor_in_zone((uint8_t)g_ui_panel_zone))
        return g_ui_shape;
    return ((int16_t (*)(void))cb(UI_CB_SHAPE))();
}

/*
 * ui_wait_click_or_key - wait n rounds of 20 clock units (idle callback,
 * cursor shape from ui_call_9650); after more than 20 rounds Esc / Space
 * / Enter or a change of the mouse buttons over the panel ends it.
 * Original: IST.EXE 05DC:2DCA (INTRFACE.L), far cdecl (int n).  The key
 * buffer is flushed first and after every key.
 * Port: the 20-unit waits poll (pc_poll), keys via ui_kbhit/ui_getch.
 * Verified: tests/py/test_intrface.py (test_waits).
 */
void ui_wait_click_or_key(int16_t n)
{
    int16_t n0 = n, t, b0;
    b0 = gr_mouse_read(&t, &t, &t);
    ui_flush_keys();
    while (n > 0) {
        uint16_t t0 = (uint16_t)g_clock_ms;
        while ((uint16_t)((uint16_t)g_clock_ms - t0) < 0x14) {
            idle();
            gr_call_then_cursor(ui_call_9650);
            pc_poll();
        }
        if (ui_kbhit()) {
            int16_t k = ui_getch();
            ui_flush_keys();
            if ((k == 0x1B || k == 0x20 || k == 0x0D) && (int16_t)(n0 - n) > 0x14)
                return;
        }
        if (gr_mouse_read(&t, &t, &t) != b0 && gr_cursor_in_zone((uint8_t)g_ui_panel_zone)) {
            gr_wait_buttons_up();
            b0 = gr_mouse_read(&t, &t, &t);
            if ((int16_t)(n0 - n) > 0x14)
                return;
        }
        gr_cursor_update((uint8_t)g_cursor_shape);
        n--;
    }
}

/*
 * ui_ask_evet_hayir - wait for E/e ("evet", returns 1) or H/h ("hayir",
 * returns 0); other keys are ignored.
 * Original: IST.EXE 05DC:2EEC (INTRFACE.L), far cdecl.
 * Port: polls through ui_kbhit.
 * Verified: tests/py/test_intrface.py (test_small).
 */
uint8_t ui_ask_evet_hayir(void)
{
    uint8_t r = 0;
    while (r == 0) {
        if (ui_kbhit()) {
            switch (ui_getch()) {
            case 'E': case 'e': r = 2; break;
            case 'H': case 'h': r = 1; break;
            }
        }
    }
    return (uint8_t)(r - 1);
}
