#include "rubraview/thumb.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t *px(rubraview_pixbuf_t *pb, int x, int y) { return pb->pixels + (ptrdiff_t)y * pb->stride + x * 4; }

int main(void) {
    printf("[test_thumb] Starting picker thumbnail tests...\n");
    size_t size = 8u << 20;
    void *raw = malloc(size);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = size });

    /* A wide picture: left third red, middle green, right third blue,
       alpha 0 everywhere as a shell bitmap often gives it. */
    rubraview_pixbuf_t src = rubraview_pixbuf_create(&arena, 300, 100, RUBRAVIEW_PIXFMT_BGRA8);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 300; ++x) {
            uint8_t *p = px(&src, x, y);
            p[0] = x >= 200 ? 200 : 0; p[1] = (x >= 100 && x < 200) ? 200 : 0; p[2] = x < 100 ? 200 : 0; p[3] = 0;
        }

    /* A square tile: only the green middle is kept, small, opaque, darker. */
    rubraview_pixbuf_t t = rubraview_thumb_make(&arena, &src, 1.0, 50, 0, 0.75);
    assert(rubraview_pixbuf_is_valid(&t) && t.width == 50 && t.height == 50 && t.format == RUBRAVIEW_PIXFMT_BGRA8);
    uint8_t *c = px(&t, 25, 25);
    assert(c[0] == 0 && c[2] == 0 && c[1] == 150 && c[3] == 255);
    printf("  [PASS] The middle is cut to the tile's shape, made small, darker and opaque\n");

    /* A tall tile from the same picture keeps the middle's full height. */
    rubraview_pixbuf_t tall = rubraview_thumb_make(&arena, &src, 0.5, 40, 0, 1.0);
    assert(tall.width == 40 && tall.height == 80);
    printf("  [PASS] The cut follows the tile's aspect\n");

    /* Blurred: a hard edge becomes a ramp. */
    rubraview_pixbuf_t edge = rubraview_pixbuf_create(&arena, 40, 40, RUBRAVIEW_PIXFMT_RGBA8);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) { uint8_t *p = px(&edge, x, y); p[0] = p[1] = p[2] = x < 20 ? 0 : 240; p[3] = 255; }
    rubraview_pixbuf_t sharp = rubraview_thumb_make(&arena, &edge, 1.0, 40, 0, 1.0);
    rubraview_pixbuf_t soft = rubraview_thumb_make(&arena, &edge, 1.0, 40, 2, 1.0);
    assert(px(&sharp, 19, 20)[0] == 0 && px(&sharp, 20, 20)[0] == 240);
    assert(px(&soft, 19, 20)[0] > 0 && px(&soft, 20, 20)[0] < 240);
    assert(px(&soft, 2, 20)[0] == 0 && px(&soft, 37, 20)[0] == 240);   /* far from the edge, unchanged */
    printf("  [PASS] A blur radius softens edges and leaves flat areas alone\n");

    /* A small source is not blown up; nonsense is refused. */
    rubraview_pixbuf_t tiny = rubraview_pixbuf_create(&arena, 8, 8, RUBRAVIEW_PIXFMT_RGBA8);
    rubraview_pixbuf_t t2 = rubraview_thumb_make(&arena, &tiny, 1.0, 96, 1, 0.8);
    assert(t2.width == 8 && t2.height == 8);
    rubraview_pixbuf_t gray = rubraview_pixbuf_create(&arena, 8, 8, RUBRAVIEW_PIXFMT_GRAY8);
    assert(rubraview_thumb_make(&arena, &gray, 1.0, 96, 1, 0.8).pixels == NULL);
    assert(rubraview_thumb_make(&arena, &tiny, 0.0, 96, 1, 0.8).pixels == NULL);
    assert(rubraview_thumb_make(&arena, NULL, 1.0, 96, 1, 0.8).pixels == NULL);
    printf("  [PASS] A small picture is not blown up; a wrong format or shape is refused\n");

    free(raw);
    printf("[test_thumb] All tests passed successfully!\n");
    return 0;
}
