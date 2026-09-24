#include <stdint.h>
#include "rubraview/core.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(void) {
    printf("[test_pixbuf] Starting core pixel buffer unit tests...\n");

    /* Allocate 1MB backing slice for arena */
    size_t mem_size = 1024 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);

    proven_mem_mut_t backing = {
        .ptr = (proven_byte_t*)raw_mem,
        .size = mem_size
    };
    proven_arena_t arena = proven_arena_create(backing);

    /* Test 1: Create 64x64 RGBA8 buffer */
    rubraview_pixbuf_t pb = rubraview_pixbuf_create(&arena, 64, 64, RUBRAVIEW_PIXFMT_RGBA8);
    assert(rubraview_pixbuf_is_valid(&pb));
    assert(pb.width == 64);
    assert(pb.height == 64);
    assert(pb.stride == 64 * 4);
    printf("  [PASS] Pixbuf allocation (64x64 RGBA8)\n");

    /* Test 2: Bounds checking on rubraview_pixbuf_at */
    assert(rubraview_pixbuf_at(&pb, 0, 0) != NULL);
    assert(rubraview_pixbuf_at(&pb, 63, 63) != NULL);
    assert(rubraview_pixbuf_at(&pb, -1, 0) == NULL);
    assert(rubraview_pixbuf_at(&pb, 64, 0) == NULL);
    assert(rubraview_pixbuf_at(&pb, 0, 64) == NULL);
    printf("  [PASS] Pixbuf coordinate bounds checks\n");

    /* Test 3: Clear and verify pixel values */
    rubraview_pixbuf_clear(&pb, 0xFF0000FF); /* Solid red */
    uint32_t *p0 = (uint32_t*)rubraview_pixbuf_at(&pb, 0, 0);
    uint32_t *pLast = (uint32_t*)rubraview_pixbuf_at(&pb, 63, 63);
    assert(*p0 == 0xFF0000FF);
    assert(*pLast == 0xFF0000FF);
    printf("  [PASS] Pixbuf fill and pixel color validation\n");

    /* Test 4: Arena reset and reallocation */
    proven_arena_reset(&arena);
    rubraview_pixbuf_t pb2 = rubraview_pixbuf_create(&arena, 128, 128, RUBRAVIEW_PIXFMT_GRAY8);
    assert(rubraview_pixbuf_is_valid(&pb2));
    assert(pb2.stride == 128);
    printf("  [PASS] Arena reset and reallocation\n");

    /* Test 5: Pixbuf Clone */
    rubraview_pixbuf_t src = rubraview_pixbuf_create(&arena, 32, 32, RUBRAVIEW_PIXFMT_RGBA8);
    rubraview_pixbuf_clear(&src, 0x11223344);
    rubraview_pixbuf_t cloned = rubraview_pixbuf_clone(&arena, &src);
    assert(rubraview_pixbuf_is_valid(&cloned));
    assert(cloned.width == src.width && cloned.height == src.height);
    assert(cloned.pixels != src.pixels);
    assert(*(uint32_t*)rubraview_pixbuf_at(&cloned, 0, 0) == 0x11223344);
    assert(*(uint32_t*)rubraview_pixbuf_at(&cloned, 31, 31) == 0x11223344);
    printf("  [PASS] Pixbuf deep clone\n");

    /* Test 6: Pixbuf Subview (non-owning rectangular view) */
    rubraview_pixbuf_t sub = rubraview_pixbuf_subview(&src, 10, 10, 10, 10);
    assert(rubraview_pixbuf_is_valid(&sub));
    assert(sub.width == 10 && sub.height == 10);
    assert(sub.stride == src.stride);
    assert(sub.arena == NULL);
    assert(sub.pixels == rubraview_pixbuf_at(&src, 10, 10));
    /* Test bounds clamping on subview */
    rubraview_pixbuf_t sub_clamped = rubraview_pixbuf_subview(&src, 25, 25, 20, 20);
    assert(rubraview_pixbuf_is_valid(&sub_clamped));
    assert(sub_clamped.width == 7); /* 32 - 25 */
    assert(sub_clamped.height == 7);
    printf("  [PASS] Pixbuf non-owning subview & bounds clamping\n");

    /* Test 7: Pixbuf Crop */
    rubraview_pixbuf_t cropped = rubraview_pixbuf_crop(&arena, &src, 5, 5, 12, 12);
    assert(rubraview_pixbuf_is_valid(&cropped));
    assert(cropped.width == 12 && cropped.height == 12);
    assert(cropped.stride == 12 * 4);
    assert(*(uint32_t*)rubraview_pixbuf_at(&cropped, 0, 0) == 0x11223344);
    printf("  [PASS] Pixbuf crop into independent buffer\n");

    /* Test 8: Format Conversions */
    /* Create RGBA test image with known channel values: R=0x10, G=0x20, B=0x30, A=0xFF */
    rubraview_pixbuf_t rgba_test = rubraview_pixbuf_create(&arena, 4, 4, RUBRAVIEW_PIXFMT_RGBA8);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            uint8_t *px = rubraview_pixbuf_at(&rgba_test, x, y);
            px[0] = 0x10; /* R */
            px[1] = 0x20; /* G */
            px[2] = 0x30; /* B */
            px[3] = 0xFF; /* A */
        }
    }
    /* RGBA8 -> BGRA8 */
    rubraview_pixbuf_t bgra_test = rubraview_pixbuf_convert(&arena, &rgba_test, RUBRAVIEW_PIXFMT_BGRA8);
    assert(rubraview_pixbuf_is_valid(&bgra_test));
    uint8_t *bgra_px = rubraview_pixbuf_at(&bgra_test, 0, 0);
    assert(bgra_px[0] == 0x30); /* B */
    assert(bgra_px[1] == 0x20); /* G */
    assert(bgra_px[2] == 0x10); /* R */
    assert(bgra_px[3] == 0xFF); /* A */

    /* BGRA8 -> RGBA8 */
    rubraview_pixbuf_t back_rgba = rubraview_pixbuf_convert(&arena, &bgra_test, RUBRAVIEW_PIXFMT_RGBA8);
    assert(rubraview_pixbuf_is_valid(&back_rgba));
    uint8_t *back_px = rubraview_pixbuf_at(&back_rgba, 0, 0);
    assert(back_px[0] == 0x10);
    assert(back_px[1] == 0x20);
    assert(back_px[2] == 0x30);
    assert(back_px[3] == 0xFF);

    /* RGBA8 -> GRAY8 */
    rubraview_pixbuf_t gray_test = rubraview_pixbuf_convert(&arena, &rgba_test, RUBRAVIEW_PIXFMT_GRAY8);
    assert(rubraview_pixbuf_is_valid(&gray_test));
    assert(gray_test.stride == 4);
    uint8_t *g_px = rubraview_pixbuf_at(&gray_test, 0, 0);
    /* 2126*16 + 7152*32 + 722*48 + 5000 = 34016 + 228864 + 34656 + 5000 = 302536 / 10000 = 30 */
    assert(*g_px == 30);

    /* GRAY8 -> RGBA8 */
    rubraview_pixbuf_t from_gray = rubraview_pixbuf_convert(&arena, &gray_test, RUBRAVIEW_PIXFMT_RGBA8);
    assert(rubraview_pixbuf_is_valid(&from_gray));
    uint8_t *fg_px = rubraview_pixbuf_at(&from_gray, 0, 0);
    assert(fg_px[0] == 30 && fg_px[1] == 30 && fg_px[2] == 30 && fg_px[3] == 255);
    printf("  [PASS] Format conversions (RGBA8 <-> BGRA8, RGBA8 <-> GRAY8)\n");

    /* Test 9: bytes per pixel */
    assert(rubraview_bytes_per_pixel(RUBRAVIEW_PIXFMT_RGBA8) == 4);
    assert(rubraview_bytes_per_pixel(RUBRAVIEW_PIXFMT_BGRA8) == 4);
    assert(rubraview_bytes_per_pixel(RUBRAVIEW_PIXFMT_GRAY8) == 1);
    assert(rubraview_bytes_per_pixel(RUBRAVIEW_PIXFMT_RGBA16F) == 8);
    printf("  [PASS] rubraview_bytes_per_pixel check\n");

    /* Sizes are multiplied with a check (proven's PROVEN_CKD_MUL), never
       trusted: a width a file claims can be as large as INT32_MAX, and
       width * 8 bytes in int32_t is undefined behaviour, not just wrong. */
    {
        rubraview_pixbuf_t huge = rubraview_pixbuf_create(&arena, INT32_MAX, 1, RUBRAVIEW_PIXFMT_RGBA16F);
        assert(!rubraview_pixbuf_is_valid(&huge));

        proven_result_mem_mut_t over = rubraview_arena_alloc_array(&arena, SIZE_MAX / 2, 4);
        assert(over.err == PROVEN_ERR_OVERFLOW);
        proven_result_mem_mut_t fine = rubraview_arena_alloc_array(&arena, 16, sizeof(uint32_t));
        assert(proven_is_ok(fine.err) && fine.value.size >= 64);
    }
    printf("  [PASS] Sizes are multiplied with a check, and an overflow is refused\n");

    free(raw_mem);
    printf("[test_pixbuf] All tests passed successfully!\n");
    return 0;
}
