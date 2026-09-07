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

    free(raw_mem);
    printf("[test_pixbuf] All tests passed successfully!\n");
    return 0;
}
