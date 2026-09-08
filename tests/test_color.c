#include "rubraview/color.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

int main(void) {
    printf("[test_color] Starting color grading and tone curve unit tests...\n");

    /* Allocate 1MB arena for test buffers */
    size_t mem_size = 1024 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);

    proven_mem_mut_t backing = {
        .ptr = (proven_byte_t*)raw_mem,
        .size = mem_size
    };
    proven_arena_t arena = proven_arena_create(backing);

    /* Test 1: sRGB <-> Linear RGB conversions */
    rubraview_color_lut_init();
    assert(fabsf(rubraview_srgb_to_linear(0) - 0.0f) < 1e-5f);
    assert(fabsf(rubraview_srgb_to_linear(255) - 1.0f) < 1e-5f);
    assert(rubraview_linear_to_srgb(0.0f) == 0);
    assert(rubraview_linear_to_srgb(1.0f) == 255);

    /* Round-trip error within 1 LSB for all 256 levels */
    for (int i = 0; i < 256; ++i) {
        float lin = rubraview_srgb_to_linear((uint8_t)i);
        uint8_t roundtrip = rubraview_linear_to_srgb(lin);
        int diff = abs((int)roundtrip - i);
        assert(diff <= 1);
    }
    printf("  [PASS] sRGB <-> Linear RGB LUT round-trip\n");

    /* Test 2: Histogram computation */
    rubraview_pixbuf_t pb = rubraview_pixbuf_create(&arena, 10, 10, RUBRAVIEW_PIXFMT_RGBA8);
    /* Fill with 50 pure Red (255, 0, 0, 255) and 50 pure Green (0, 255, 0, 255) */
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            uint8_t *px = rubraview_pixbuf_at(&pb, x, y);
            if (y < 5) {
                px[0] = 255; px[1] = 0; px[2] = 0; px[3] = 255;
            } else {
                px[0] = 0; px[1] = 255; px[2] = 0; px[3] = 255;
            }
        }
    }
    uint32_t hr[256], hg[256], hb[256], hlum[256];
    rubraview_histogram_compute(&pb, hr, hg, hb, hlum);
    assert(hr[255] == 50);
    assert(hr[0] == 50);
    assert(hg[255] == 50);
    assert(hg[0] == 50);
    assert(hb[0] == 100);
    printf("  [PASS] Multi-channel histogram computation\n");

    /* Test 3: Levels LUT */
    uint8_t levels_lut[256];
    rubraview_levels_build_lut(levels_lut, 20, 235, 1.0f);
    assert(levels_lut[0] == 0);
    assert(levels_lut[20] == 0);
    assert(levels_lut[235] == 255);
    assert(levels_lut[255] == 255);
    assert(levels_lut[127] > 0 && levels_lut[127] < 255);
    /* Monotonicity check */
    for (int i = 1; i < 256; ++i) {
        assert(levels_lut[i] >= levels_lut[i - 1]);
    }
    printf("  [PASS] Levels LUT generation with monotonicity\n");

    /* Test 4: Monotone Cubic Spline (Fritsch-Carlson) */
    rubraview_curve_point_t pts[] = {
        { 0.0f, 0.0f },
        { 64.0f, 100.0f },
        { 192.0f, 150.0f },
        { 255.0f, 255.0f }
    };
    uint8_t curve_lut[256];
    rubraview_curve_build_lut(curve_lut, pts, 4);
    assert(curve_lut[0] == 0);
    assert(abs((int)curve_lut[64] - 100) <= 1);
    assert(abs((int)curve_lut[192] - 150) <= 1);
    assert(curve_lut[255] == 255);
    /* Strictly monotonic non-decreasing guarantee */
    for (int i = 1; i < 256; ++i) {
        assert(curve_lut[i] >= curve_lut[i - 1]);
    }
    printf("  [PASS] Fritsch-Carlson monotone cubic spline curve LUT\n");

    /* Test 5: Invert LUT via rubraview_lut_apply */
    uint8_t invert_lut[256];
    for (int i = 0; i < 256; ++i) invert_lut[i] = (uint8_t)(255 - i);
    rubraview_lut_apply(&pb, invert_lut, invert_lut, invert_lut);
    uint8_t *inv_p0 = rubraview_pixbuf_at(&pb, 0, 0);
    assert(inv_p0[0] == 0);   /* was 255 -> now 0 */
    assert(inv_p0[1] == 255); /* was 0 -> now 255 */
    assert(inv_p0[2] == 255); /* was 0 -> now 255 */
    assert(inv_p0[3] == 255); /* alpha intact */
    printf("  [PASS] LUT application across channels\n");

    /* Test 6: Color adjust saturation = 0.0f (grayscale) */
    rubraview_pixbuf_t col_pb = rubraview_pixbuf_create(&arena, 4, 4, RUBRAVIEW_PIXFMT_RGBA8);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            uint8_t *px = rubraview_pixbuf_at(&col_pb, x, y);
            px[0] = 200; px[1] = 100; px[2] = 50; px[3] = 255;
        }
    }
    rubraview_color_adjust_params_t sat_params = {
        .exposure_ev = 0.0f,
        .contrast = 0.0f,
        .saturation = 0.0f,
        .gamma = 1.0f
    };
    rubraview_color_adjust(&col_pb, &sat_params);
    uint8_t *mono_px = rubraview_pixbuf_at(&col_pb, 0, 0);
    assert(mono_px[0] == mono_px[1] && mono_px[1] == mono_px[2]);
    printf("  [PASS] Color adjust desaturation (R == G == B)\n");

    free(raw_mem);
    printf("[test_color] All tests passed successfully!\n");
    return 0;
}
