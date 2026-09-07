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
    rv_pixbuf_t pb = rv_pixbuf_create(&arena, 64, 64, RV_PIXFMT_RGBA8);
    assert(rv_pixbuf_is_valid(&pb));
    assert(pb.width == 64);
    assert(pb.height == 64);
    assert(pb.stride == 64 * 4);
    printf("  [PASS] Pixbuf allocation (64x64 RGBA8)\n");

    /* Test 2: Bounds checking on rv_pixbuf_at */
    assert(rv_pixbuf_at(&pb, 0, 0) != NULL);
    assert(rv_pixbuf_at(&pb, 63, 63) != NULL);
    assert(rv_pixbuf_at(&pb, -1, 0) == NULL);
    assert(rv_pixbuf_at(&pb, 64, 0) == NULL);
    assert(rv_pixbuf_at(&pb, 0, 64) == NULL);
    printf("  [PASS] Pixbuf coordinate bounds checks\n");

    /* Test 3: Clear and verify pixel values */
    rv_pixbuf_clear(&pb, 0xFF0000FF); /* Solid red */
    uint32_t *p0 = (uint32_t*)rv_pixbuf_at(&pb, 0, 0);
    uint32_t *pLast = (uint32_t*)rv_pixbuf_at(&pb, 63, 63);
    assert(*p0 == 0xFF0000FF);
    assert(*pLast == 0xFF0000FF);
    printf("  [PASS] Pixbuf fill and pixel color validation\n");

    /* Test 4: Arena reset and reallocation */
    proven_arena_reset(&arena);
    rv_pixbuf_t pb2 = rv_pixbuf_create(&arena, 128, 128, RV_PIXFMT_GRAY8);
    assert(rv_pixbuf_is_valid(&pb2));
    assert(pb2.stride == 128);
    printf("  [PASS] Arena reset and reallocation\n");

    /* Test 5: Pixbuf Clone */
    rv_pixbuf_t src = rv_pixbuf_create(&arena, 32, 32, RV_PIXFMT_RGBA8);
    rv_pixbuf_clear(&src, 0x11223344);
    rv_pixbuf_t cloned = rv_pixbuf_clone(&arena, &src);
    assert(rv_pixbuf_is_valid(&cloned));
    assert(cloned.width == src.width && cloned.height == src.height);
    assert(cloned.pixels != src.pixels);
    assert(*(uint32_t*)rv_pixbuf_at(&cloned, 0, 0) == 0x11223344);
    assert(*(uint32_t*)rv_pixbuf_at(&cloned, 31, 31) == 0x11223344);
    printf("  [PASS] Pixbuf deep clone\n");

    /* Test 6: Pixbuf Subview (non-owning rectangular view) */
    rv_pixbuf_t sub = rv_pixbuf_subview(&src, 10, 10, 10, 10);
    assert(rv_pixbuf_is_valid(&sub));
    assert(sub.width == 10 && sub.height == 10);
    assert(sub.stride == src.stride);
    assert(sub.arena == NULL);
    assert(sub.pixels == rv_pixbuf_at(&src, 10, 10));
    /* Test bounds clamping on subview */
    rv_pixbuf_t sub_clamped = rv_pixbuf_subview(&src, 25, 25, 20, 20);
    assert(rv_pixbuf_is_valid(&sub_clamped));
    assert(sub_clamped.width == 7); /* 32 - 25 */
    assert(sub_clamped.height == 7);
    printf("  [PASS] Pixbuf non-owning subview & bounds clamping\n");

    /* Test 7: Pixbuf Crop */
    rv_pixbuf_t cropped = rv_pixbuf_crop(&arena, &src, 5, 5, 12, 12);
    assert(rv_pixbuf_is_valid(&cropped));
    assert(cropped.width == 12 && cropped.height == 12);
    assert(cropped.stride == 12 * 4);
    assert(*(uint32_t*)rv_pixbuf_at(&cropped, 0, 0) == 0x11223344);
    printf("  [PASS] Pixbuf crop into independent buffer\n");

    /* Test 8: Format Conversions */
    /* Create RGBA test image with known channel values: R=0x10, G=0x20, B=0x30, A=0xFF */
    rv_pixbuf_t rgba_test = rv_pixbuf_create(&arena, 4, 4, RV_PIXFMT_RGBA8);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            uint8_t *px = rv_pixbuf_at(&rgba_test, x, y);
            px[0] = 0x10; /* R */
            px[1] = 0x20; /* G */
            px[2] = 0x30; /* B */
            px[3] = 0xFF; /* A */
        }
    }
    /* RGBA8 -> BGRA8 */
    rv_pixbuf_t bgra_test = rv_pixbuf_convert(&arena, &rgba_test, RV_PIXFMT_BGRA8);
    assert(rv_pixbuf_is_valid(&bgra_test));
    uint8_t *bgra_px = rv_pixbuf_at(&bgra_test, 0, 0);
    assert(bgra_px[0] == 0x30); /* B */
    assert(bgra_px[1] == 0x20); /* G */
    assert(bgra_px[2] == 0x10); /* R */
    assert(bgra_px[3] == 0xFF); /* A */

    /* BGRA8 -> RGBA8 */
    rv_pixbuf_t back_rgba = rv_pixbuf_convert(&arena, &bgra_test, RV_PIXFMT_RGBA8);
    assert(rv_pixbuf_is_valid(&back_rgba));
    uint8_t *back_px = rv_pixbuf_at(&back_rgba, 0, 0);
    assert(back_px[0] == 0x10);
    assert(back_px[1] == 0x20);
    assert(back_px[2] == 0x30);
    assert(back_px[3] == 0xFF);

    /* RGBA8 -> GRAY8 */
    rv_pixbuf_t gray_test = rv_pixbuf_convert(&arena, &rgba_test, RV_PIXFMT_GRAY8);
    assert(rv_pixbuf_is_valid(&gray_test));
    assert(gray_test.stride == 4);
    uint8_t *g_px = rv_pixbuf_at(&gray_test, 0, 0);
    /* 2126*16 + 7152*32 + 722*48 + 5000 = 34016 + 228864 + 34656 + 5000 = 302536 / 10000 = 30 */
    assert(*g_px == 30);

    /* GRAY8 -> RGBA8 */
    rv_pixbuf_t from_gray = rv_pixbuf_convert(&arena, &gray_test, RV_PIXFMT_RGBA8);
    assert(rv_pixbuf_is_valid(&from_gray));
    uint8_t *fg_px = rv_pixbuf_at(&from_gray, 0, 0);
    assert(fg_px[0] == 30 && fg_px[1] == 30 && fg_px[2] == 30 && fg_px[3] == 255);
    printf("  [PASS] Format conversions (RGBA8 <-> BGRA8, RGBA8 <-> GRAY8)\n");

    /* Test 9: bytes per pixel */
    assert(rv_bytes_per_pixel(RV_PIXFMT_RGBA8) == 4);
    assert(rv_bytes_per_pixel(RV_PIXFMT_BGRA8) == 4);
    assert(rv_bytes_per_pixel(RV_PIXFMT_GRAY8) == 1);
    assert(rv_bytes_per_pixel(RV_PIXFMT_RGBA16F) == 8);
    printf("  [PASS] rv_bytes_per_pixel check\n");

    free(raw_mem);
    printf("[test_pixbuf] All tests passed successfully!\n");
    return 0;
}
