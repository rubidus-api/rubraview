#include "rubraview/transform.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

static bool approx(double a, double b) { return fabs(a - b) < 1e-9; }

static void map(rubraview_orientation_t o, double w, double h, double x, double y, double *ox, double *oy) {
    rubraview_mat3x2_apply(rubraview_orientation_matrix(o, w, h), x, y, ox, oy);
}

/* Every corner of the source must land on a corner of the oriented
   rectangle, and the four must stay distinct — that is what makes a
   transform a rigid re-placement rather than a squash. */
static void assert_corners_map_onto_oriented_rect(rubraview_orientation_t o, double w, double h) {
    double ow = 0.0, oh = 0.0;
    rubraview_orientation_apply_size(o, w, h, &ow, &oh);

    const double sx[4] = { 0, w, 0, w };
    const double sy[4] = { 0, 0, h, h };
    int seen = 0;

    for (int i = 0; i < 4; ++i) {
        double x, y;
        map(o, w, h, sx[i], sy[i], &x, &y);
        bool at_corner = (approx(x, 0.0) || approx(x, ow)) && (approx(y, 0.0) || approx(y, oh));
        assert(at_corner);

        int bit = (approx(x, ow) ? 1 : 0) | (approx(y, oh) ? 2 : 0);
        assert((seen & (1 << bit)) == 0); /* no two corners collapse together */
        seen |= (1 << bit);
    }
    assert(seen == 0xF);
}

int main(void) {
    printf("[test_transform] Starting non-destructive orientation unit tests...\n");

    const double W = 800.0, H = 1200.0;

    /* Test 1: The identity leaves points and size untouched. */
    {
        rubraview_orientation_t o = rubraview_orientation_identity();
        double x, y, ow, oh;
        map(o, W, H, 123.0, 456.0, &x, &y);
        assert(approx(x, 123.0) && approx(y, 456.0));
        rubraview_orientation_apply_size(o, W, H, &ow, &oh);
        assert(approx(ow, W) && approx(oh, H));
    }
    printf("  [PASS] Identity orientation is a no-op\n");

    /* Test 2: A quarter turn swaps the presented extent; a half turn does not. */
    {
        rubraview_orientation_t r90 = rubraview_orientation_rotate_cw(rubraview_orientation_identity());
        assert(rubraview_orientation_swaps_axes(r90));
        double ow, oh;
        rubraview_orientation_apply_size(r90, W, H, &ow, &oh);
        assert(approx(ow, H) && approx(oh, W));

        rubraview_orientation_t r180 = rubraview_orientation_rotate_cw(r90);
        assert(!rubraview_orientation_swaps_axes(r180));
        rubraview_orientation_apply_size(r180, W, H, &ow, &oh);
        assert(approx(ow, W) && approx(oh, H));
    }
    printf("  [PASS] Quarter turns swap the presented width and height\n");

    /* Test 3: 90 degrees clockwise puts the source's top-left at the
       oriented top-right, which is what "clockwise" means. */
    {
        rubraview_orientation_t r90 = rubraview_orientation_rotate_cw(rubraview_orientation_identity());
        double x, y;
        map(r90, W, H, 0.0, 0.0, &x, &y);
        assert(approx(x, H) && approx(y, 0.0));   /* top-left -> top-right */
        map(r90, W, H, 0.0, H, &x, &y);
        assert(approx(x, 0.0) && approx(y, 0.0)); /* bottom-left -> top-left */
    }
    printf("  [PASS] 90 degrees clockwise moves the top-left corner to the top-right\n");

    /* Test 4: Every rotation maps corners onto corners without collapsing. */
    {
        rubraview_orientation_t o = rubraview_orientation_identity();
        for (int i = 0; i < 4; ++i) {
            assert_corners_map_onto_oriented_rect(o, W, H);
            o = rubraview_orientation_rotate_cw(o);
        }
        assert(o.rotation == RUBRAVIEW_ROTATE_0); /* four turns return to the start */
    }
    printf("  [PASS] All four rotations map corners onto corners and cycle back\n");

    /* Test 5: Clockwise and counter-clockwise are inverses. */
    {
        rubraview_orientation_t o = rubraview_orientation_identity();
        o = rubraview_orientation_rotate_cw(o);
        o = rubraview_orientation_rotate_ccw(o);
        assert(o.rotation == RUBRAVIEW_ROTATE_0);

        o = rubraview_orientation_rotate_ccw(o);
        assert(o.rotation == RUBRAVIEW_ROTATE_270); /* wraps below zero */
    }
    printf("  [PASS] Clockwise and counter-clockwise are inverses and wrap correctly\n");

    /* Test 6: A horizontal flip mirrors across the vertical centre. */
    {
        rubraview_orientation_t o = rubraview_orientation_flip_h(rubraview_orientation_identity());
        double x, y;
        map(o, W, H, 0.0, 0.0, &x, &y);
        assert(approx(x, W) && approx(y, 0.0));
        map(o, W, H, W, H, &x, &y);
        assert(approx(x, 0.0) && approx(y, H));
        assert_corners_map_onto_oriented_rect(o, W, H);
    }
    printf("  [PASS] Horizontal flip mirrors across the vertical centre\n");

    /* Test 7: A vertical flip mirrors across the horizontal centre, and
       applying it twice returns to the original. */
    {
        rubraview_orientation_t o = rubraview_orientation_flip_v(rubraview_orientation_identity());
        double x, y;
        map(o, W, H, 0.0, 0.0, &x, &y);
        assert(approx(x, 0.0) && approx(y, H));

        o = rubraview_orientation_flip_v(o);
        assert(!o.flip_vertical);
        map(o, W, H, 0.0, 0.0, &x, &y);
        assert(approx(x, 0.0) && approx(y, 0.0));
    }
    printf("  [PASS] Vertical flip mirrors and is its own inverse\n");

    /* Test 8: Rotating a mirrored image still looks clockwise on screen.
       "Mirror, then turn clockwise" must equal "turn clockwise (as the
       state records it), then mirror" — the reason rotate_cw steps the
       other way while exactly one flip is active. */
    {
        rubraview_orientation_t mirrored = rubraview_orientation_flip_h(rubraview_orientation_identity());
        rubraview_orientation_t turned = rubraview_orientation_rotate_cw(mirrored);

        /* Expected: mirror the source, then rotate that clockwise. */
        for (int i = 0; i < 4; ++i) {
            const double sx[4] = { 0, W, 0, W };
            const double sy[4] = { 0, 0, H, H };

            double mx = W - sx[i], my = sy[i];        /* mirror */
            double expect_x = H - my, expect_y = mx;  /* then rotate 90 CW (source is W x H) */

            double got_x, got_y;
            map(turned, W, H, sx[i], sy[i], &got_x, &got_y);
            assert(approx(got_x, expect_x) && approx(got_y, expect_y));
        }
    }
    printf("  [PASS] Rotating a mirrored image still turns clockwise on screen\n");

    /* Test 9: Combined rotation and both flips stays a rigid placement. */
    {
        rubraview_orientation_t o = rubraview_orientation_identity();
        o = rubraview_orientation_rotate_cw(o);
        o = rubraview_orientation_flip_h(o);
        o = rubraview_orientation_flip_v(o);
        assert_corners_map_onto_oriented_rect(o, W, H);
    }
    printf("  [PASS] Rotation combined with both flips remains a rigid placement\n");

    printf("[test_transform] All tests passed successfully!\n");
    return 0;
}
