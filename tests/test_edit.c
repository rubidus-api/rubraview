#include "rubraview/edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static bool near(float a, float b) { return fabsf(a - b) < 0.0001f; }

int main(void) {
    printf("[test_edit] Starting editing session unit tests...\n");

    size_t mem_size = 8 * 1024 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: a fresh session changes nothing, which is what greys out
       the Apply button (§3.13). */
    {
        rubraview_edit_session_t s = rubraview_edit_begin(800, 600);
        assert(rubraview_edit_is_neutral(&s));
        rubraview_edit_set_slider(&s, RUBRAVIEW_SLIDER_CONTRAST, 20.0f);
        assert(!rubraview_edit_is_neutral(&s));
        rubraview_edit_reset(&s);
        assert(rubraview_edit_is_neutral(&s));
        assert(s.image_width == 800); /* the reset keeps the image, not just the numbers */
    }
    printf("  [PASS] A fresh session is neutral, and a reset returns to it\n");

    /* Test 2: §3.13's slider ranges are enforced by the model, so a
       panel cannot ask the commit layer for something it would refuse. */
    {
        rubraview_edit_session_t s = rubraview_edit_begin(100, 100);
        rubraview_edit_set_slider(&s, RUBRAVIEW_SLIDER_EXPOSURE, 99.0f);
        assert(near(rubraview_edit_get_slider(&s, RUBRAVIEW_SLIDER_EXPOSURE), 3.0f));
        rubraview_edit_set_slider(&s, RUBRAVIEW_SLIDER_EXPOSURE, -99.0f);
        assert(near(rubraview_edit_get_slider(&s, RUBRAVIEW_SLIDER_EXPOSURE), -3.0f));

        rubraview_edit_set_slider(&s, RUBRAVIEW_SLIDER_SHARPEN_AMOUNT, 1000.0f);
        assert(near(rubraview_edit_get_slider(&s, RUBRAVIEW_SLIDER_SHARPEN_AMOUNT), 300.0f));

        /* A blur below the specification's minimum radius means off,
           not "almost off". */
        rubraview_edit_set_slider(&s, RUBRAVIEW_SLIDER_BLUR_SIGMA, 0.1f);
        assert(near(rubraview_edit_get_slider(&s, RUBRAVIEW_SLIDER_BLUR_SIGMA), 0.5f));
        rubraview_edit_set_slider(&s, RUBRAVIEW_SLIDER_BLUR_SIGMA, 0.0f);
        assert(near(rubraview_edit_get_slider(&s, RUBRAVIEW_SLIDER_BLUR_SIGMA), 0.0f));
    }
    printf("  [PASS] Every slider clamps to its specified range\n");

    /* Test 3: levels cannot invert — setting one point pushes the other. */
    {
        rubraview_edit_session_t s = rubraview_edit_begin(100, 100);
        rubraview_edit_set_black_point(&s, 200);
        assert(s.params.black_point == 200 && s.params.white_point == 255);
        /* Now the white point is dragged below the black one. */
        rubraview_edit_set_white_point(&s, 50);
        assert(s.params.white_point == 50 && s.params.black_point == 49);
        rubraview_edit_set_black_point(&s, 300);
        assert(s.params.black_point == 254 && s.params.white_point == 255);
    }
    printf("  [PASS] The black and white points cannot cross\n");

    /* Test 4: the curve stays a function. Points are sorted, endpoints
       are pinned, and an interior point cannot pass its neighbours. */
    {
        rubraview_edit_session_t s = rubraview_edit_begin(100, 100);
        int32_t mid = rubraview_edit_curve_grab(&s, 128.0f, 200.0f, 6.0f);
        assert(mid == 1);
        assert(s.curves[RUBRAVIEW_EDIT_CHANNEL_RGB].point_count == 3);

        int32_t low = rubraview_edit_curve_grab(&s, 64.0f, 40.0f, 6.0f);
        assert(low == 1); /* inserted before the one at 128 */
        assert(s.curves[RUBRAVIEW_EDIT_CHANNEL_RGB].points[1].x == 64.0f);
        assert(s.curves[RUBRAVIEW_EDIT_CHANNEL_RGB].points[2].x == 128.0f);

        /* Clicking near an existing point grabs it instead of stacking
           a second one on the same spot. */
        int32_t again = rubraview_edit_curve_grab(&s, 65.0f, 41.0f, 6.0f);
        assert(again == 1 && s.curves[RUBRAVIEW_EDIT_CHANNEL_RGB].point_count == 4);

        /* Dragging that point past its right-hand neighbour stops at it. */
        rubraview_edit_curve_move(&s, 1, 250.0f, 10.0f);
        assert(s.curves[RUBRAVIEW_EDIT_CHANNEL_RGB].points[1].x == 127.0f);

        /* The endpoints move vertically only. */
        rubraview_edit_curve_move(&s, 0, 100.0f, 30.0f);
        assert(s.curves[RUBRAVIEW_EDIT_CHANNEL_RGB].points[0].x == 0.0f);
        assert(s.curves[RUBRAVIEW_EDIT_CHANNEL_RGB].points[0].y == 30.0f);

        /* And they cannot be removed. */
        assert(!rubraview_edit_curve_remove(&s, 0));
        assert(!rubraview_edit_curve_remove(&s, 3));
        assert(rubraview_edit_curve_remove(&s, 1));
        assert(s.curves[RUBRAVIEW_EDIT_CHANNEL_RGB].point_count == 3);
    }
    printf("  [PASS] The tone curve stays a function under any drag\n");

    /* Test 5: a crop drag is the same rectangle whichever corner it
       started from, and never leaves the image. */
    {
        rubraview_edit_session_t s = rubraview_edit_begin(400, 300);
        rubraview_edit_crop_drag(&s, 300, 200, 100, 50);
        assert(s.crop_active);
        assert(s.crop.x == 100 && s.crop.y == 50 && s.crop.width == 200 && s.crop.height == 150);

        rubraview_edit_crop_drag(&s, -50, -50, 9999, 9999);
        assert(s.crop.x == 0 && s.crop.y == 0);
        assert(s.crop.width == 400 && s.crop.height == 300);
    }
    printf("  [PASS] A crop drag normalises and stays inside the image\n");

    /* Test 6: §3.13's aspect locks reshape the rectangle, and a lock
       that would take it outside the image shrinks it instead. */
    {
        rubraview_edit_session_t s = rubraview_edit_begin(400, 300);
        rubraview_edit_crop_set_ratio(&s, RUBRAVIEW_CROP_1_1);
        rubraview_edit_crop_drag(&s, 0, 0, 300, 100);
        assert(s.crop.width == s.crop.height);

        rubraview_edit_crop_set_ratio(&s, RUBRAVIEW_CROP_16_9);
        assert(fabs((double)s.crop.width / (double)s.crop.height - 16.0 / 9.0) < 0.05);
        assert(s.crop.x + s.crop.width <= 400 && s.crop.y + s.crop.height <= 300);

        /* A 16:9 crop taller than the image would need 533 px of width;
           there are only 400, so it gives up height rather than
           spilling over the edge. */
        rubraview_edit_crop_drag(&s, 0, 0, 400, 300);
        assert(s.crop.width <= 400 && s.crop.height <= 300);
        assert(fabs((double)s.crop.width / (double)s.crop.height - 16.0 / 9.0) < 0.05);

        assert(rubraview_crop_ratio_value(RUBRAVIEW_CROP_FREE, 400, 300) == 0.0);
        assert(fabs(rubraview_crop_ratio_value(RUBRAVIEW_CROP_ORIGINAL, 400, 300) - 4.0 / 3.0) < 0.0001);
    }
    printf("  [PASS] Aspect locks reshape the crop without leaving the image\n");

    /* Test 7: the resize aspect lock, and percentages measured against
       the crop rather than the original when one is set. */
    {
        rubraview_edit_session_t s = rubraview_edit_begin(1000, 500);
        rubraview_edit_set_resize(&s, 400, 0, true);
        assert(s.resize_width == 400 && s.resize_height == 200);

        rubraview_edit_set_resize(&s, 0, 100, false);
        assert(s.resize_width == 200 && s.resize_height == 100);

        int32_t w = 0, h = 0;
        assert(rubraview_edit_resize_percent(&s, 50.0, &w, &h));
        assert(w == 500 && h == 250);

        /* Out-of-range percentages clamp to §3.13's 10%-500%. */
        assert(rubraview_edit_resize_percent(&s, 1000.0, &w, &h));
        assert(w == 5000 && h == 2500);

        rubraview_edit_crop_drag(&s, 0, 0, 200, 100);
        assert(rubraview_edit_resize_percent(&s, 50.0, &w, &h));
        assert(w == 100 && h == 50); /* half of the crop, not half of the original */
    }
    printf("  [PASS] The resize lock follows the aspect, and percentages follow the crop\n");

    /* Test 8: commit runs, does not touch the source, and applies the
       crop and resize it was given. */
    {
        rubraview_pixbuf_t src = rubraview_pixbuf_create(&arena, 64, 64, RUBRAVIEW_PIXFMT_RGBA8);
        assert(rubraview_pixbuf_is_valid(&src));
        for (int32_t y = 0; y < 64; ++y) {
            for (int32_t x = 0; x < 64; ++x) {
                uint8_t *px = src.pixels + (ptrdiff_t)y * src.stride + x * 4;
                px[0] = 100; px[1] = 100; px[2] = 100; px[3] = 255;
            }
        }

        rubraview_edit_session_t s = rubraview_edit_begin(64, 64);
        rubraview_edit_crop_drag(&s, 16, 16, 48, 48);
        rubraview_edit_set_resize(&s, 16, 16, true);
        rubraview_edit_set_slider(&s, RUBRAVIEW_SLIDER_BRIGHTNESS, 50.0f);

        rubraview_pixbuf_t out = rubraview_edit_commit(&arena, &s, &src);
        assert(rubraview_pixbuf_is_valid(&out));
        assert(out.width == 16 && out.height == 16);
        assert(out.pixels != src.pixels);
        assert(src.pixels[0] == 100); /* non-destructive: the source is untouched */
        assert(out.pixels[0] > 100);  /* brightness was applied */
    }
    printf("  [PASS] Commit crops, resizes and brightens without touching the source\n");

    /* Test 9: a neutral session still produces a usable copy rather
       than nothing — Apply with no changes is not an error. */
    {
        rubraview_pixbuf_t src = rubraview_pixbuf_create(&arena, 8, 8, RUBRAVIEW_PIXFMT_RGBA8);
        assert(rubraview_pixbuf_is_valid(&src));
        src.pixels[0] = 77;

        rubraview_edit_session_t s = rubraview_edit_begin(8, 8);
        rubraview_pixbuf_t out = rubraview_edit_commit(&arena, &s, &src);
        assert(rubraview_pixbuf_is_valid(&out));
        assert(out.width == 8 && out.height == 8);
        assert(out.pixels[0] == 77);
    }
    printf("  [PASS] A neutral commit copies the image through unchanged\n");

    /* Test 10: the preview is bounded by the view, never magnified, and
       measured against the crop when there is one. */
    {
        rubraview_edit_session_t s = rubraview_edit_begin(9000, 6000);
        int32_t w = 0, h = 0;

        rubraview_edit_preview_size(&s, 1920, 1080, &w, &h);
        assert(w <= 1920 && h <= 1080);
        assert(w > 0 && h > 0);
        /* The aspect ratio is kept: 3:2 in, 3:2 out. */
        assert(fabs((double)w / (double)h - 1.5) < 0.01);

        /* A small image is not blown up to fill the view. */
        rubraview_edit_session_t small = rubraview_edit_begin(64, 64);
        rubraview_edit_preview_size(&small, 1920, 1080, &w, &h);
        assert(w == 64 && h == 64);

        /* With a crop set, the preview follows the crop. */
        rubraview_edit_crop_drag(&s, 0, 0, 900, 600);
        rubraview_edit_preview_size(&s, 300, 300, &w, &h);
        assert(w == 300 && h == 200);

        /* No view given means full size — the commit path. */
        rubraview_edit_preview_size(&s, 0, 0, &w, &h);
        assert(w == 900 && h == 600);
    }
    printf("  [PASS] The preview is bounded by the view, never magnified, and follows the crop\n");

    /* The curve's channel label is a button: forward on a click, back on
       a right click, round the five and back to RGB on a reset. Each
       channel keeps its own curve. */
    {
        rubraview_edit_session_t s = rubraview_edit_begin(100, 100);
        assert(s.active_channel == RUBRAVIEW_EDIT_CHANNEL_RGB);
        rubraview_edit_cycle_channel(&s, 1);
        assert(s.active_channel == RUBRAVIEW_EDIT_CHANNEL_RED);
        int32_t i = rubraview_edit_curve_grab(&s, 128.0f, 200.0f, 4.0f);
        assert(i == 1 && s.curves[RUBRAVIEW_EDIT_CHANNEL_RED].point_count == 3);
        assert(s.curves[RUBRAVIEW_EDIT_CHANNEL_RGB].point_count == 2);
        for (int k = 0; k < 5; ++k) rubraview_edit_cycle_channel(&s, 1);
        assert(s.active_channel == RUBRAVIEW_EDIT_CHANNEL_RED);   /* five steps: round once */
        rubraview_edit_cycle_channel(&s, -1);
        rubraview_edit_cycle_channel(&s, -1);
        assert(s.active_channel == RUBRAVIEW_EDIT_CHANNEL_LUMA);
        rubraview_edit_reset(&s);
        assert(s.active_channel == RUBRAVIEW_EDIT_CHANNEL_RGB);
        assert(s.curves[RUBRAVIEW_EDIT_CHANNEL_RED].point_count == 2);
    }
    printf("  [PASS] The channel label cycles the five curves, each kept apart\n");

    free(raw);
    printf("[test_edit] All tests passed successfully!\n");
    return 0;
}
