/* test_vga.c - unit tests for the VGA emulation (src/platform/vga.c). */
#include "platform/vga.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static VgaState v;
static VgaFrame f;

static void test_mode13_chained(void)
{
    vga_init(&v, NULL);
    CHECK(vga_bios_set_mode(&v, 0x13));
    vga_mem_write8(&v, 0xA0000 + 0, 11);
    vga_mem_write8(&v, 0xA0000 + 1, 22);
    vga_mem_write8(&v, 0xA0000 + 319, 33);
    vga_mem_write8(&v, 0xA0000 + 320 * 199 + 5, 44);
    /* hardware layout: plane (A & 3), offset (A & ~3) */
    CHECK(v.plane[0][0] == 11 && v.plane[1][0] == 22);
    CHECK(v.plane[3][316] == 33);
    CHECK(vga_mem_read8(&v, 0xA0000 + 1) == 22);
    vga_render(&v, &f);
    CHECK(f.width == 320 && f.height == 400 && f.line_repeat == 2 && !f.blanked);
    CHECK(f.pixels[0] == 11 && f.pixels[1] == 22 && f.pixels[319] == 33);
    CHECK(f.pixels[320 + 1] == 22);                      /* line doubled */
    CHECK(f.pixels[(199 * 2) * 320 + 5] == 44 && f.pixels[(199 * 2 + 1) * 320 + 5] == 44);
    CHECK(vga_refresh_hz(&v) > 70.0 && vga_refresh_hz(&v) < 70.2);
}

static void unchain(void)
{
    vga_bios_set_mode(&v, 0x13);
    vga_out16(&v, VGA_PORT_SEQ_INDEX, 0x0604);   /* chain-4 off */
    vga_out16(&v, VGA_PORT_CRTC_INDEX, 0x0014);  /* doubleword off */
    vga_out16(&v, VGA_PORT_CRTC_INDEX, 0xE317);  /* byte mode */
    vga_out16(&v, VGA_PORT_SEQ_INDEX, 0x0F02);
    for (uint32_t i = 0; i < 0x10000; i++)
        vga_mem_write8(&v, 0xA0000 + i, 0);      /* clear all planes */
}

static void test_modex_planar_and_latches(void)
{
    unchain();
    /* pixel x lives in plane x&3 at offset y*80 + x/4 */
    vga_out16(&v, VGA_PORT_SEQ_INDEX, 0x0402);   /* map mask: plane 2 */
    vga_mem_write8(&v, 0xA0000 + 80 * 10 + 3, 77);  /* x = 3*4+2 = 14, y = 10 */
    vga_render(&v, &f);
    CHECK(f.pixels[(10 * 2) * 320 + 14] == 77);
    CHECK(f.pixels[(10 * 2) * 320 + 13] == 0);

    /* write mode 1 latch copy: copies all 4 planes of a byte at once */
    vga_out16(&v, VGA_PORT_SEQ_INDEX, 0x0F02);
    vga_out16(&v, VGA_PORT_GC_INDEX, 0x4105);    /* write mode 1 */
    (void)vga_mem_read8(&v, 0xA0000 + 80 * 10 + 3);
    vga_mem_write8(&v, 0xA0000 + 80 * 20 + 3, 0xAA);
    vga_out16(&v, VGA_PORT_GC_INDEX, 0x4005);
    vga_render(&v, &f);
    CHECK(f.pixels[(20 * 2) * 320 + 14] == 77);

    /* bit mask + set/reset in write mode 0 */
    vga_out16(&v, VGA_PORT_GC_INDEX, 0x0F01);    /* enable set/reset all planes */
    vga_out16(&v, VGA_PORT_GC_INDEX, 0x0500);    /* set/reset value 0101 */
    vga_mem_write8(&v, 0xA0000 + 5, 0);
    CHECK(v.plane[0][5] == 0xFF && v.plane[1][5] == 0x00 && v.plane[2][5] == 0xFF);
    vga_out16(&v, VGA_PORT_GC_INDEX, 0x0001);
}

static void test_split_screen_and_start(void)
{
    unchain();
    vga_out16(&v, VGA_PORT_SEQ_INDEX, 0x0F02);
    vga_mem_write8(&v, 0xA0000 + 0, 9);          /* address 0: shown after split */
    vga_mem_write8(&v, 0xA0000 + 0x4000, 5);     /* page 2 */
    vga_out16(&v, VGA_PORT_CRTC_INDEX, 0x400C);  /* start = 0x4000 */
    vga_out16(&v, VGA_PORT_CRTC_INDEX, 0x000D);
    /* line compare = 199 -> split begins on scan line 200. Mode 13h leaves
       LC = 3FFh (bits 8/9 in overflow and max-scan), so clear those first. */
    vga_out16(&v, VGA_PORT_CRTC_INDEX, 0xC718);
    vga_out8(&v, VGA_PORT_CRTC_INDEX, 0x07);
    vga_out8(&v, VGA_PORT_CRTC_DATA, (uint8_t)(v.crtc[7] & ~0x10));
    vga_out8(&v, VGA_PORT_CRTC_INDEX, 0x09);
    vga_out8(&v, VGA_PORT_CRTC_DATA, (uint8_t)(v.crtc[9] & ~0x40));
    vga_render(&v, &f);
    CHECK(f.pixels[0] == 5 && f.pixels[320] == 5);   /* page 2, row 0, doubled */
    CHECK(f.pixels[199 * 320] == 0);                 /* page 2, row 99 */
    CHECK(f.pixels[200 * 320] == 9 && f.pixels[201 * 320] == 9);  /* split: address 0 */
    CHECK(f.pixels[202 * 320] == 0);
}

static void test_dac_and_attr(void)
{
    vga_init(&v, NULL);
    vga_bios_set_mode(&v, 0x13);
    vga_out8(&v, VGA_PORT_DAC_WRITE, 7);
    vga_out8(&v, VGA_PORT_DAC_DATA, 63);
    vga_out8(&v, VGA_PORT_DAC_DATA, 0x41);       /* masked to 6 bits */
    vga_out8(&v, VGA_PORT_DAC_DATA, 1);
    CHECK(v.dac[7][0] == 63 && v.dac[7][1] == 1 && v.dac[7][2] == 1);
    CHECK(v.dac_write_index == 8);
    vga_out8(&v, VGA_PORT_DAC_READ, 7);
    CHECK(vga_in8(&v, VGA_PORT_DAC_DATA) == 63);
    /* default 256-colour palette spot checks */
    CHECK(v.dac[15][0] == 63 && v.dac[31][0] == 63 && v.dac[32][2] == 63 && v.dac[255][0] == 0);
    /* attribute controller: index write with PAS=0 blanks the screen */
    (void)vga_in8(&v, VGA_PORT_STATUS1);
    vga_out8(&v, VGA_PORT_ATTR, 0x00);
    vga_render(&v, &f);
    CHECK(f.blanked);
    (void)vga_in8(&v, VGA_PORT_STATUS1);
    vga_out8(&v, VGA_PORT_ATTR, 0x20);
    vga_render(&v, &f);
    CHECK(!f.blanked);
}

static void attr_write(uint8_t index, uint8_t value)
{
    (void)vga_in8(&v, 0x3DA);                    /* reset the flip-flop */
    vga_out8(&v, 0x3C0, (uint8_t)(index | 0x20)); /* keep the screen on */
    vga_out8(&v, 0x3C0, value);
}

static void test_mode12_planar16(void)
{
    vga_init(&v, NULL);
    CHECK(vga_bios_set_mode(&v, 0x12));
    vga_render(&v, &f);
    CHECK(f.width == 640 && f.height == 480 && f.line_repeat == 1 && !f.blanked);
    CHECK(f.pixels[0] == 0 && f.pixels[639 + 479 * 640] == 0);
    CHECK(f.dac[7][0] == 0x2A && f.dac[0x14][1] == 0x15);   /* EGA palette: brown */

    /* planes 0 and 2, leftmost pixel -> colour 5 -> attr[5] = 05h */
    vga_out16(&v, VGA_PORT_SEQ_INDEX, 0x0502);
    vga_mem_write8(&v, 0xA0000, 0x80);
    /* write mode 2: colour 6 into pixel 7 of row 10 only (bit mask 01h) */
    vga_out16(&v, VGA_PORT_SEQ_INDEX, 0x0F02);
    vga_out16(&v, VGA_PORT_GC_INDEX, 0x0205);
    vga_out16(&v, VGA_PORT_GC_INDEX, 0x0108);
    (void)vga_mem_read8(&v, 0xA0000 + 80 * 10);  /* load latches */
    vga_mem_write8(&v, 0xA0000 + 80 * 10, 6);
    vga_out16(&v, VGA_PORT_GC_INDEX, 0xFF08);
    vga_out16(&v, VGA_PORT_GC_INDEX, 0x0005);
    vga_render(&v, &f);
    CHECK(f.pixels[0] == 0x05 && f.pixels[1] == 0);
    CHECK(f.pixels[10 * 640 + 7] == 0x14);       /* attr[6] = 14h (brown) */
    CHECK(f.pixels[10 * 640 + 6] == 0 && f.pixels[10 * 640 + 8] == 0);

    /* colour plane enable: only plane 0 -> colour 5 becomes 1 */
    attr_write(0x12, 0x01);
    vga_render(&v, &f);
    CHECK(f.pixels[0] == 0x01);
    attr_write(0x12, 0x0F);

    /* P5/P4 from colour select (mode control bit 7), P7/P6 always */
    attr_write(0x10, 0x81);
    attr_write(0x14, 0x0E);                      /* P7P6 = 11, P5P4 = 10 */
    vga_render(&v, &f);
    CHECK(f.pixels[0] == (0xC0 | 0x20 | 0x05));
    CHECK(f.pixels[10 * 640 + 7] == (0xC0 | 0x20 | 0x04));
    attr_write(0x10, 0x01);
    attr_write(0x14, 0x00);

    /* horizontal pel panning: 3 pixels */
    attr_write(0x13, 0x03);
    vga_render(&v, &f);
    CHECK(f.pixels[10 * 640 + 4] == 0x14);
}

int main(void)
{
    test_mode13_chained();
    test_mode12_planar16();
    test_modex_planar_and_latches();
    test_split_screen_and_start();
    test_dac_and_attr();
    if (failures)
        fprintf(stderr, "test_vga: %d failure(s)\n", failures);
    else
        printf("test_vga: all passed\n");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
