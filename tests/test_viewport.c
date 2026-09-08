#include "rubraview/viewport.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

static bool approx(double a, double b) {
    return fabs(a - b) < 1e-9;
}

int main(void) {
    printf("[test_viewport] Starting fit-mode and viewport matrix unit tests...\n");

    /* Test 1: FIT_WINDOW letterboxes on the constraining axis and centers. */
    {
        rubraview_viewport_transform_t t = rubraview_fit_compute(RUBRAVIEW_FIT_WINDOW, 800, 600, 400, 400);
        assert(approx(t.scale_x, 0.5) && approx(t.scale_y, 0.5));
        assert(approx(t.offset_x, 0.0));  /* 400 - 800*0.5 = 0, no pillarbox */
        assert(approx(t.offset_y, 50.0)); /* (400 - 600*0.5)/2 = 50, letterboxed */
    }
    printf("  [PASS] FIT_WINDOW scales to the constraining axis and letterboxes\n");

    /* Test 2: FIT_WIDTH matches width; overflow height yields a negative
       centering offset (content extends beyond the window, scrollable). */
    {
        rubraview_viewport_transform_t t = rubraview_fit_compute(RUBRAVIEW_FIT_WIDTH, 800, 3000, 400, 600);
        assert(approx(t.scale_x, 0.5) && approx(t.scale_y, 0.5));
        assert(approx(t.offset_x, 0.0));
        assert(approx(t.offset_y, -450.0)); /* (600 - 3000*0.5)/2 = -450 */
    }
    printf("  [PASS] FIT_WIDTH overflow produces a negative (scrollable) offset\n");

    /* Test 3: FIT_HEIGHT matches height. */
    {
        rubraview_viewport_transform_t t = rubraview_fit_compute(RUBRAVIEW_FIT_HEIGHT, 1600, 900, 800, 1000);
        double expected_scale = 1000.0 / 900.0;
        assert(approx(t.scale_x, expected_scale) && approx(t.scale_y, expected_scale));
        double expected_w = 1600 * expected_scale;
        assert(approx(t.offset_x, (800.0 - expected_w) / 2.0));
        assert(approx(t.offset_y, 0.0));
    }
    printf("  [PASS] FIT_HEIGHT matches window height\n");

    /* Test 4: FIT_STRETCH fills exactly, ignoring aspect ratio, no offset. */
    {
        rubraview_viewport_transform_t t = rubraview_fit_compute(RUBRAVIEW_FIT_STRETCH, 800, 600, 400, 1000);
        assert(approx(t.scale_x, 0.5));
        assert(approx(t.scale_y, 1000.0 / 600.0));
        assert(approx(t.offset_x, 0.0) && approx(t.offset_y, 0.0));
    }
    printf("  [PASS] FIT_STRETCH fills exactly with independent axis scales\n");

    /* Test 5: FIT_ACTUAL_SIZE is always scale 1.0, centered. */
    {
        rubraview_viewport_transform_t t = rubraview_fit_compute(RUBRAVIEW_FIT_ACTUAL_SIZE, 200, 100, 400, 400);
        assert(approx(t.scale_x, 1.0) && approx(t.scale_y, 1.0));
        assert(approx(t.offset_x, 100.0)); /* (400-200)/2 */
        assert(approx(t.offset_y, 150.0)); /* (400-100)/2 */
    }
    printf("  [PASS] FIT_ACTUAL_SIZE is always 1:1, centered\n");

    /* Test 6: FIT_SMART downscales when larger than the window... */
    {
        rubraview_viewport_transform_t t = rubraview_fit_compute(RUBRAVIEW_FIT_SMART, 800, 600, 400, 400);
        assert(approx(t.scale_x, 0.5) && approx(t.scale_y, 0.5));
    }
    /* ...and stays at 1:1 (no upscaling) when smaller than the window. */
    {
        rubraview_viewport_transform_t t = rubraview_fit_compute(RUBRAVIEW_FIT_SMART, 100, 80, 400, 400);
        assert(approx(t.scale_x, 1.0) && approx(t.scale_y, 1.0));
        assert(approx(t.offset_x, 150.0)); /* (400-100)/2 */
    }
    printf("  [PASS] FIT_SMART downscales only when the image exceeds the window\n");

    /* Test 7: Degenerate input returns identity, never divides by zero. */
    {
        rubraview_viewport_transform_t t = rubraview_fit_compute(RUBRAVIEW_FIT_WINDOW, 0, 600, 400, 400);
        assert(approx(t.scale_x, 1.0) && approx(t.scale_y, 1.0));
        assert(approx(t.offset_x, 0.0) && approx(t.offset_y, 0.0));
    }
    printf("  [PASS] Degenerate (zero/negative) dimensions return identity, no crash\n");

    /* Test 8: Matrix identity, translate, and scale apply correctly. */
    {
        double ox, oy;
        rubraview_mat3x2_apply(rubraview_mat3x2_identity(), 3.0, 4.0, &ox, &oy);
        assert(approx(ox, 3.0) && approx(oy, 4.0));

        rubraview_mat3x2_apply(rubraview_mat3x2_translate(10.0, -5.0), 3.0, 4.0, &ox, &oy);
        assert(approx(ox, 13.0) && approx(oy, -1.0));

        rubraview_mat3x2_apply(rubraview_mat3x2_scale(2.0, 0.5), 3.0, 4.0, &ox, &oy);
        assert(approx(ox, 6.0) && approx(oy, 2.0));
    }
    printf("  [PASS] Identity, translate, and scale matrices apply correctly\n");

    /* Test 9: Composition order matters — translate-then-scale differs from
       scale-then-translate for the same two component transforms. */
    {
        rubraview_mat3x2_t t = rubraview_mat3x2_translate(10.0, 0.0);
        rubraview_mat3x2_t s = rubraview_mat3x2_scale(2.0, 2.0);

        double x1, y1, x2, y2;
        rubraview_mat3x2_apply(rubraview_mat3x2_multiply(t, s), 0.0, 0.0, &x1, &y1); /* translate then scale: (10,0)*2 = (20,0) */
        rubraview_mat3x2_apply(rubraview_mat3x2_multiply(s, t), 0.0, 0.0, &x2, &y2); /* scale then translate: (0,0)*2 + (10,0) = (10,0) */
        assert(approx(x1, 20.0));
        assert(approx(x2, 10.0));
        assert(!approx(x1, x2));
    }
    printf("  [PASS] Matrix composition order is respected (non-commutative)\n");

    /* Test 10: viewport_matrix (M = T(dx,dy).S(scale).T(-cx,-cy)) is the
       classic "zoom centered on (cx,cy), panned to (dx,dy)" transform: the
       pivot point always lands exactly on the pan target regardless of
       scale, and a point offset from the pivot scales proportionally
       around it. */
    {
        double cx = 50.0, cy = 30.0, scale = 3.0, dx = 200.0, dy = 100.0;
        rubraview_mat3x2_t m = rubraview_viewport_matrix(cx, cy, scale, dx, dy);

        double px, py;
        rubraview_mat3x2_apply(m, cx, cy, &px, &py);
        assert(approx(px, dx) && approx(py, dy));

        double qx, qy;
        rubraview_mat3x2_apply(m, cx + 4.0, cy - 2.0, &qx, &qy);
        assert(approx(qx, dx + scale * 4.0));
        assert(approx(qy, dy + scale * (-2.0)));
    }
    printf("  [PASS] viewport_matrix centers zoom on the pivot and pans it to the target\n");

    printf("[test_viewport] All tests passed successfully!\n");
    return 0;
}
