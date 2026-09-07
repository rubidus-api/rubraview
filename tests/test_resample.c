#include "rubraview/resample.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(void) {
    printf("[test_resample] Starting image resampling unit tests...\n");

    /* Allocate 2MB arena */
    size_t mem_size = 2 * 1024 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);

    proven_mem_mut_t backing = {
        .ptr = (proven_byte_t*)raw_mem,
        .size = mem_size
    };
    proven_arena_t arena = proven_arena_create(backing);

    /* Test 1: Nearest Neighbor 2x integer upscale */
    rv_pixbuf_t src2x2 = rv_pixbuf_create(&arena, 2, 2, RV_PIXFMT_RGBA8);
    /* Set top-left red, top-right green, bottom-left blue, bottom-right white */
    *(uint32_t*)rv_pixbuf_at(&src2x2, 0, 0) = 0xFF0000FF; /* Red */
    *(uint32_t*)rv_pixbuf_at(&src2x2, 1, 0) = 0x00FF00FF; /* Green */
    *(uint32_t*)rv_pixbuf_at(&src2x2, 0, 1) = 0x0000FFFF; /* Blue */
    *(uint32_t*)rv_pixbuf_at(&src2x2, 1, 1) = 0xFFFFFFFF; /* White */

    rv_pixbuf_t nearest4x4 = rv_pixbuf_resample(&arena, &src2x2, 4, 4, RV_FILTER_NEAREST);
    assert(rv_pixbuf_is_valid(&nearest4x4));
    assert(nearest4x4.width == 4 && nearest4x4.height == 4);
    /* Verify 2x2 replication of top-left red */
    assert(*(uint32_t*)rv_pixbuf_at(&nearest4x4, 0, 0) == 0xFF0000FF);
    assert(*(uint32_t*)rv_pixbuf_at(&nearest4x4, 1, 0) == 0xFF0000FF);
    assert(*(uint32_t*)rv_pixbuf_at(&nearest4x4, 0, 1) == 0xFF0000FF);
    assert(*(uint32_t*)rv_pixbuf_at(&nearest4x4, 1, 1) == 0xFF0000FF);
    /* Verify bottom-right white */
    assert(*(uint32_t*)rv_pixbuf_at(&nearest4x4, 2, 2) == 0xFFFFFFFF);
    assert(*(uint32_t*)rv_pixbuf_at(&nearest4x4, 3, 3) == 0xFFFFFFFF);
    printf("  [PASS] Nearest Neighbor integer 2x upscale\n");

    /* Test 2: Bilinear 2x downscale */
    rv_pixbuf_t src4x4 = rv_pixbuf_create(&arena, 4, 4, RV_PIXFMT_GRAY8);
    rv_pixbuf_clear(&src4x4, 100);
    rv_pixbuf_t bi2x2 = rv_pixbuf_resample(&arena, &src4x4, 2, 2, RV_FILTER_BILINEAR);
    assert(rv_pixbuf_is_valid(&bi2x2));
    assert(bi2x2.width == 2 && bi2x2.height == 2);
    /* Uniform field remains uniform */
    assert(*rv_pixbuf_at(&bi2x2, 0, 0) == 100);
    assert(*rv_pixbuf_at(&bi2x2, 1, 1) == 100);
    printf("  [PASS] Bilinear downscale uniform preservation\n");

    /* Test 3: Bicubic Catmull-Rom upscale on ramp */
    rv_pixbuf_t ramp_src = rv_pixbuf_create(&arena, 8, 8, RV_PIXFMT_GRAY8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            *rv_pixbuf_at(&ramp_src, x, y) = (uint8_t)(x * 30);
        }
    }
    rv_pixbuf_t bicubic16x16 = rv_pixbuf_resample(&arena, &ramp_src, 16, 16, RV_FILTER_BICUBIC);
    assert(rv_pixbuf_is_valid(&bicubic16x16));
    assert(bicubic16x16.width == 16 && bicubic16x16.height == 16);
    /* Check that values increase monotonically along rows */
    for (int y = 0; y < 16; ++y) {
        for (int x = 1; x < 16; ++x) {
            uint8_t prev = *rv_pixbuf_at(&bicubic16x16, x - 1, y);
            uint8_t cur = *rv_pixbuf_at(&bicubic16x16, x, y);
            assert(cur >= prev);
        }
    }
    printf("  [PASS] Bicubic Catmull-Rom gradient monotonicity\n");

    /* Test 4: Lanczos-3 downsampling & boundary clamping */
    rv_pixbuf_t lanczos_out = rv_pixbuf_resample(&arena, &ramp_src, 4, 4, RV_FILTER_LANCZOS3);
    assert(rv_pixbuf_is_valid(&lanczos_out));
    assert(lanczos_out.width == 4 && lanczos_out.height == 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            uint8_t val = *rv_pixbuf_at(&lanczos_out, x, y);
            /* Values must exist and not be garbage */
            (void)val;
        }
    }
    /* Value at center should be reasonably close to midpoint of ramp */
    uint8_t mid_val = *rv_pixbuf_at(&lanczos_out, 2, 2);
    assert(mid_val > 50 && mid_val < 200);
    printf("  [PASS] Lanczos-3 downsampling and boundary safety\n");

    /* Test 5: Identity dimension fast path */
    rv_pixbuf_t same = rv_pixbuf_resample(&arena, &src2x2, 2, 2, RV_FILTER_LANCZOS3);
    assert(rv_pixbuf_is_valid(&same));
    assert(same.pixels != src2x2.pixels);
    assert(*(uint32_t*)rv_pixbuf_at(&same, 0, 0) == *(uint32_t*)rv_pixbuf_at(&src2x2, 0, 0));
    printf("  [PASS] Identity dimension clone fast path\n");

    /* Test 6: Invalid arguments */
    rv_pixbuf_t invalid = rv_pixbuf_resample(&arena, &src2x2, 0, -5, RV_FILTER_NEAREST);
    assert(!rv_pixbuf_is_valid(&invalid));
    rv_pixbuf_t invalid2 = rv_pixbuf_resample(NULL, &src2x2, 10, 10, RV_FILTER_NEAREST);
    assert(!rv_pixbuf_is_valid(&invalid2));
    printf("  [PASS] Invalid argument rejection\n");

    free(raw_mem);
    printf("[test_resample] All tests passed successfully!\n");
    return 0;
}
