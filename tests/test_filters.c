#include "rubraview/filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

int main(void) {
    printf("[test_filters] Starting convolution and filter unit tests...\n");

    /* Allocate 2MB arena */
    size_t mem_size = 2 * 1024 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);

    proven_mem_mut_t backing = {
        .ptr = (proven_byte_t*)raw_mem,
        .size = mem_size
    };
    proven_arena_t arena = proven_arena_create(backing);

    /* Test 1: Gaussian blur impulse response */
    rubraview_pixbuf_t impulse = rubraview_pixbuf_create(&arena, 9, 9, RUBRAVIEW_PIXFMT_GRAY8);
    *rubraview_pixbuf_at(&impulse, 4, 4) = 255; /* Center impulse */

    rubraview_pixbuf_t blurred = rubraview_filter_gaussian_blur(&arena, &impulse, 1.0f);
    assert(rubraview_pixbuf_is_valid(&blurred));
    /* Center pixel reduced, symmetric neighbors received energy */
    uint8_t center_val = *rubraview_pixbuf_at(&blurred, 4, 4);
    uint8_t left_val = *rubraview_pixbuf_at(&blurred, 3, 4);
    uint8_t right_val = *rubraview_pixbuf_at(&blurred, 5, 4);
    uint8_t top_val = *rubraview_pixbuf_at(&blurred, 4, 3);
    uint8_t bot_val = *rubraview_pixbuf_at(&blurred, 4, 5);

    assert(center_val < 255 && center_val > 20);
    assert(left_val == right_val);
    assert(top_val == bot_val);
    assert(left_val == top_val);
    printf("  [PASS] Gaussian blur 2D symmetric impulse dispersion\n");

    /* Test 2: Box blur uniformity */
    rubraview_pixbuf_t uni = rubraview_pixbuf_create(&arena, 8, 8, RUBRAVIEW_PIXFMT_RGBA8);
    rubraview_pixbuf_clear(&uni, 0xFF505050);
    rubraview_pixbuf_t box = rubraview_filter_box_blur(&arena, &uni, 2);
    assert(rubraview_pixbuf_is_valid(&box));
    uint8_t *bpx = rubraview_pixbuf_at(&box, 4, 4);
    assert(bpx[0] == 0x50 && bpx[1] == 0x50 && bpx[2] == 0x50 && bpx[3] == 0xFF);
    printf("  [PASS] Box blur uniform preservation\n");

    /* Test 3: Unsharp mask sharpening & threshold gating */
    rubraview_pixbuf_t step_img = rubraview_pixbuf_create(&arena, 10, 2, RUBRAVIEW_PIXFMT_GRAY8);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 10; ++x) {
            /* Left side 50, right side 200 with step edge at x=5 */
            *rubraview_pixbuf_at(&step_img, x, y) = (x < 5) ? 50 : 200;
        }
    }
    /* Threshold = 10 -> step edge difference (150) exceeds threshold, should sharpen */
    rubraview_pixbuf_t sharp = rubraview_filter_unsharp_mask(&arena, &step_img, 1.0f, 1.0f, 10);
    assert(rubraview_pixbuf_is_valid(&sharp));
    /* Edge pixel on dark side should overshoot downwards (< 50) */
    /* Edge pixel on bright side should overshoot upwards (> 200) */
    uint8_t dark_edge = *rubraview_pixbuf_at(&sharp, 4, 0);
    uint8_t bright_edge = *rubraview_pixbuf_at(&sharp, 5, 0);
    assert(dark_edge < 50);
    assert(bright_edge > 200);

    /* Now test threshold gating: threshold = 200 (higher than edge diff 150) */
    rubraview_pixbuf_t ungated = rubraview_filter_unsharp_mask(&arena, &step_img, 1.0f, 1.0f, 200);
    assert(rubraview_pixbuf_is_valid(&ungated));
    assert(*rubraview_pixbuf_at(&ungated, 4, 0) == 50);
    assert(*rubraview_pixbuf_at(&ungated, 5, 0) == 200);
    printf("  [PASS] Unsharp mask edge sharpening and threshold noise gating\n");

    /* Test 4: Auto-trim white margins */
    rubraview_pixbuf_t scan = rubraview_pixbuf_create(&arena, 20, 20, RUBRAVIEW_PIXFMT_GRAY8);
    rubraview_pixbuf_clear(&scan, 255); /* White background */
    /* Draw black rectangle from (x=3, y=2) to (x=16, y=17) -> width = 14, height = 16 */
    for (int y = 2; y <= 17; ++y) {
        for (int x = 3; x <= 16; ++x) {
            *rubraview_pixbuf_at(&scan, x, y) = 10; /* Black ink */
        }
    }
    rubraview_pixbuf_t trimmed = rubraview_filter_autotrim(&arena, &scan, 245, true);
    assert(rubraview_pixbuf_is_valid(&trimmed));
    assert(trimmed.width == 14);
    assert(trimmed.height == 16);
    /* Verify all four corners of trimmed result are the ink */
    assert(*rubraview_pixbuf_at(&trimmed, 0, 0) == 10);
    assert(*rubraview_pixbuf_at(&trimmed, 13, 0) == 10);
    assert(*rubraview_pixbuf_at(&trimmed, 0, 15) == 10);
    assert(*rubraview_pixbuf_at(&trimmed, 13, 15) == 10);
    printf("  [PASS] Auto-trim white margins on document/manga scans\n");

    free(raw_mem);
    printf("[test_filters] All tests passed successfully!\n");
    return 0;
}
