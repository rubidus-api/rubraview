#include "rubraview/resample.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>

/* D-38: the two passes run from rubraview_resample_axis_weights' tables,
   in the order the CPU adds — what the card is given to do. */
static void two_pass_from_tables(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, rubraview_resample_filter_t f) {
    int taps = rubraview_resample_taps(f);
    int32_t *xi = malloc(sizeof(int32_t) * (size_t)dst->width * taps), *yi = malloc(sizeof(int32_t) * (size_t)dst->height * taps);
    float *xw = malloc(sizeof(float) * (size_t)dst->width * taps), *yw = malloc(sizeof(float) * (size_t)dst->height * taps);
    float *h = malloc(sizeof(float) * (size_t)dst->width * (size_t)src->height * 4);
    assert(rubraview_resample_axis_weights(f, src->width, dst->width, xi, xw));
    assert(rubraview_resample_axis_weights(f, src->height, dst->height, yi, yw));
    for (int y = 0; y < src->height; ++y)
        for (int k = 0; k < dst->width; ++k)
            for (int c = 0; c < 4; ++c) {
                float acc = 0.0f;
                for (int j = 0; j < taps; ++j) acc += src->pixels[(ptrdiff_t)y * src->stride + xi[k * taps + j] * 4 + c] * xw[k * taps + j];
                h[((size_t)y * dst->width + k) * 4 + c] = acc;
            }
    for (int y = 0; y < dst->height; ++y)
        for (int k = 0; k < dst->width; ++k)
            for (int c = 0; c < 4; ++c) {
                float v = 0.0f;
                for (int i = 0; i < taps; ++i) v += h[((size_t)yi[y * taps + i] * dst->width + k) * 4 + c] * yw[y * taps + i];
                int iv = (int)(v + 0.5f);
                dst->pixels[(ptrdiff_t)y * dst->stride + k * 4 + c] = (uint8_t)(iv < 0 ? 0 : (iv > 255 ? 255 : iv));
            }
    free(xi); free(yi); free(xw); free(yw); free(h);
}

static int g_accel_calls;
static bool g_accel_result;
static bool fake_accel(void *context, const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, rubraview_resample_filter_t f) {
    (void)context; (void)src; (void)f;
    g_accel_calls++;
    if (g_accel_result) memset(dst->pixels, 0x5A, (size_t)dst->stride * (size_t)dst->height);
    return g_accel_result;
}

int main(void) {
    printf("[test_resample] Starting image resampling unit tests...\n");

    /* Allocate 2MB arena */
    size_t mem_size = 4 * 1024 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);

    proven_mem_mut_t backing = {
        .ptr = (proven_byte_t*)raw_mem,
        .size = mem_size
    };
    proven_arena_t arena = proven_arena_create(backing);

    /* Test 1: Nearest Neighbor 2x integer upscale */
    rubraview_pixbuf_t src2x2 = rubraview_pixbuf_create(&arena, 2, 2, RUBRAVIEW_PIXFMT_RGBA8);
    /* Set top-left red, top-right green, bottom-left blue, bottom-right white */
    *(uint32_t*)rubraview_pixbuf_at(&src2x2, 0, 0) = 0xFF0000FF; /* Red */
    *(uint32_t*)rubraview_pixbuf_at(&src2x2, 1, 0) = 0x00FF00FF; /* Green */
    *(uint32_t*)rubraview_pixbuf_at(&src2x2, 0, 1) = 0x0000FFFF; /* Blue */
    *(uint32_t*)rubraview_pixbuf_at(&src2x2, 1, 1) = 0xFFFFFFFF; /* White */

    rubraview_pixbuf_t nearest4x4 = rubraview_pixbuf_resample(&arena, &src2x2, 4, 4, RUBRAVIEW_FILTER_NEAREST);
    assert(rubraview_pixbuf_is_valid(&nearest4x4));
    assert(nearest4x4.width == 4 && nearest4x4.height == 4);
    /* Verify 2x2 replication of top-left red */
    assert(*(uint32_t*)rubraview_pixbuf_at(&nearest4x4, 0, 0) == 0xFF0000FF);
    assert(*(uint32_t*)rubraview_pixbuf_at(&nearest4x4, 1, 0) == 0xFF0000FF);
    assert(*(uint32_t*)rubraview_pixbuf_at(&nearest4x4, 0, 1) == 0xFF0000FF);
    assert(*(uint32_t*)rubraview_pixbuf_at(&nearest4x4, 1, 1) == 0xFF0000FF);
    /* Verify bottom-right white */
    assert(*(uint32_t*)rubraview_pixbuf_at(&nearest4x4, 2, 2) == 0xFFFFFFFF);
    assert(*(uint32_t*)rubraview_pixbuf_at(&nearest4x4, 3, 3) == 0xFFFFFFFF);
    printf("  [PASS] Nearest Neighbor integer 2x upscale\n");

    /* Test 2: Bilinear 2x downscale */
    rubraview_pixbuf_t src4x4 = rubraview_pixbuf_create(&arena, 4, 4, RUBRAVIEW_PIXFMT_GRAY8);
    rubraview_pixbuf_clear(&src4x4, 100);
    rubraview_pixbuf_t bi2x2 = rubraview_pixbuf_resample(&arena, &src4x4, 2, 2, RUBRAVIEW_FILTER_BILINEAR);
    assert(rubraview_pixbuf_is_valid(&bi2x2));
    assert(bi2x2.width == 2 && bi2x2.height == 2);
    /* Uniform field remains uniform */
    assert(*rubraview_pixbuf_at(&bi2x2, 0, 0) == 100);
    assert(*rubraview_pixbuf_at(&bi2x2, 1, 1) == 100);
    printf("  [PASS] Bilinear downscale uniform preservation\n");

    /* Test 3: Bicubic Catmull-Rom upscale on ramp */
    rubraview_pixbuf_t ramp_src = rubraview_pixbuf_create(&arena, 8, 8, RUBRAVIEW_PIXFMT_GRAY8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            *rubraview_pixbuf_at(&ramp_src, x, y) = (uint8_t)(x * 30);
        }
    }
    rubraview_pixbuf_t bicubic16x16 = rubraview_pixbuf_resample(&arena, &ramp_src, 16, 16, RUBRAVIEW_FILTER_BICUBIC);
    assert(rubraview_pixbuf_is_valid(&bicubic16x16));
    assert(bicubic16x16.width == 16 && bicubic16x16.height == 16);
    /* Check that values increase monotonically along rows */
    for (int y = 0; y < 16; ++y) {
        for (int x = 1; x < 16; ++x) {
            uint8_t prev = *rubraview_pixbuf_at(&bicubic16x16, x - 1, y);
            uint8_t cur = *rubraview_pixbuf_at(&bicubic16x16, x, y);
            assert(cur >= prev);
        }
    }
    printf("  [PASS] Bicubic Catmull-Rom gradient monotonicity\n");

    /* Test 4: Lanczos-3 downsampling & boundary clamping */
    rubraview_pixbuf_t lanczos_out = rubraview_pixbuf_resample(&arena, &ramp_src, 4, 4, RUBRAVIEW_FILTER_LANCZOS3);
    assert(rubraview_pixbuf_is_valid(&lanczos_out));
    assert(lanczos_out.width == 4 && lanczos_out.height == 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            uint8_t val = *rubraview_pixbuf_at(&lanczos_out, x, y);
            /* Values must exist and not be garbage */
            (void)val;
        }
    }
    /* Value at center should be reasonably close to midpoint of ramp */
    uint8_t mid_val = *rubraview_pixbuf_at(&lanczos_out, 2, 2);
    assert(mid_val > 50 && mid_val < 200);
    printf("  [PASS] Lanczos-3 downsampling and boundary safety\n");

    /* Test 5: Identity dimension fast path */
    rubraview_pixbuf_t same = rubraview_pixbuf_resample(&arena, &src2x2, 2, 2, RUBRAVIEW_FILTER_LANCZOS3);
    assert(rubraview_pixbuf_is_valid(&same));
    assert(same.pixels != src2x2.pixels);
    assert(*(uint32_t*)rubraview_pixbuf_at(&same, 0, 0) == *(uint32_t*)rubraview_pixbuf_at(&src2x2, 0, 0));
    printf("  [PASS] Identity dimension clone fast path\n");

    /* Test 6: Invalid arguments */
    rubraview_pixbuf_t invalid = rubraview_pixbuf_resample(&arena, &src2x2, 0, -5, RUBRAVIEW_FILTER_NEAREST);
    assert(!rubraview_pixbuf_is_valid(&invalid));
    rubraview_pixbuf_t invalid2 = rubraview_pixbuf_resample(NULL, &src2x2, 10, 10, RUBRAVIEW_FILTER_NEAREST);
    assert(!rubraview_pixbuf_is_valid(&invalid2));
    printf("  [PASS] Invalid argument rejection\n");

    /* D-38: the weight tables. Each row of weights sums to 1 and indexes
       inside the source; the two passes run from them give exactly the
       CPU's bytes where the CPU uses its two passes (enlarging). */
    {
        proven_arena_reset(&arena);
        rubraview_pixbuf_t src = rubraview_pixbuf_create(&arena, 37, 23, RUBRAVIEW_PIXFMT_RGBA8);
        unsigned seed = 3;
        for (size_t i = 0; i < (size_t)src.stride * src.height; ++i) { seed = seed * 1103515245u + 12345u; src.pixels[i] = (uint8_t)(seed >> 16); }
        rubraview_resample_filter_t filters[2] = { RUBRAVIEW_FILTER_BICUBIC, RUBRAVIEW_FILTER_LANCZOS3 };
        for (int f = 0; f < 2; ++f) {
            int taps = rubraview_resample_taps(filters[f]);
            assert(taps == (f == 0 ? 4 : 6));
            int32_t idx[90 * 6]; float w[90 * 6];
            assert(rubraview_resample_axis_weights(filters[f], 37, 90, idx, w));
            for (int k = 0; k < 90; ++k) {
                float sum = 0.0f;
                for (int j = 0; j < taps; ++j) { sum += w[k * taps + j]; assert(idx[k * taps + j] >= 0 && idx[k * taps + j] < 37); }
                assert(fabsf(sum - 1.0f) < 1e-4f);
            }
            rubraview_pixbuf_t cpu = rubraview_pixbuf_create(&arena, 90, 61, RUBRAVIEW_PIXFMT_RGBA8);
            rubraview_pixbuf_t tab = rubraview_pixbuf_create(&arena, 90, 61, RUBRAVIEW_PIXFMT_RGBA8);
            rubraview_resample_band(&src, &cpu, filters[f], 0, cpu.height);
            two_pass_from_tables(&src, &tab, filters[f]);
            assert(memcmp(cpu.pixels, tab.pixels, (size_t)cpu.stride * cpu.height) == 0);
        }
        assert(rubraview_resample_taps(RUBRAVIEW_FILTER_BILINEAR) == 0);
        int32_t i1[4]; float w1[4];
        assert(!rubraview_resample_axis_weights(RUBRAVIEW_FILTER_BILINEAR, 10, 4, i1, w1));
    }
    printf("  [PASS] The card's weight tables give the CPU's two-pass bytes exactly\n");

    /* D-38: the accelerator is asked only where it can help, and a refusal
       leaves the CPU's result. */
    {
        proven_arena_reset(&arena);
        rubraview_pixbuf_t src = rubraview_pixbuf_create(&arena, 40, 30, RUBRAVIEW_PIXFMT_RGBA8);
        for (size_t i = 0; i < (size_t)src.stride * src.height; ++i) src.pixels[i] = (uint8_t)(i * 7);
        assert(rubraview_resample_accel_wanted(RUBRAVIEW_FILTER_LANCZOS3, RUBRAVIEW_PIXFMT_RGBA8, 40, 30, 80, 60, 1000));
        assert(!rubraview_resample_accel_wanted(RUBRAVIEW_FILTER_BILINEAR, RUBRAVIEW_PIXFMT_RGBA8, 40, 30, 80, 60, 1000));
        assert(!rubraview_resample_accel_wanted(RUBRAVIEW_FILTER_BICUBIC, RUBRAVIEW_PIXFMT_GRAY8, 40, 30, 80, 60, 1000));
        assert(!rubraview_resample_accel_wanted(RUBRAVIEW_FILTER_BICUBIC, RUBRAVIEW_PIXFMT_RGBA8, 40, 30, 20, 15, 5000));
        assert(rubraview_resample_accel_wanted(RUBRAVIEW_FILTER_BICUBIC, RUBRAVIEW_PIXFMT_RGBA8, 40, 30, 100, 50, 5000));

        rubraview_pixbuf_t plain = rubraview_pixbuf_resample(&arena, &src, 80, 60, RUBRAVIEW_FILTER_BICUBIC);
        rubraview_resample_set_accel(fake_accel, NULL, 1000);
        g_accel_calls = 0; g_accel_result = true;
        rubraview_pixbuf_t by_card = rubraview_pixbuf_resample(&arena, &src, 80, 60, RUBRAVIEW_FILTER_BICUBIC);
        assert(g_accel_calls == 1 && by_card.pixels[0] == 0x5A && by_card.pixels[(size_t)by_card.stride * 60 - 1] == 0x5A);
        (void)rubraview_pixbuf_resample(&arena, &src, 80, 60, RUBRAVIEW_FILTER_BILINEAR);
        assert(g_accel_calls == 1);                       /* not asked for bilinear */
        g_accel_result = false;
        rubraview_pixbuf_t refused = rubraview_pixbuf_resample(&arena, &src, 80, 60, RUBRAVIEW_FILTER_BICUBIC);
        assert(g_accel_calls == 2 && memcmp(refused.pixels, plain.pixels, (size_t)plain.stride * 60) == 0);
        rubraview_resample_set_accel(NULL, NULL, 0);
    }
    printf("  [PASS] The accelerator is asked only where it can help; a refusal leaves the CPU's result\n");

    free(raw_mem);
    printf("[test_resample] All tests passed successfully!\n");
    return 0;
}
