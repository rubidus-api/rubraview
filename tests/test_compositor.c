#include "rubraview/compositor.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

static bool approx(double a, double b) { return fabs(a - b) < 1e-9; }

/* Where does the top-left corner of a command's source rectangle land on
   screen, and how wide is it after the transform? */
static void mapped_bounds(const rubraview_draw_command_t *cmd, double *out_x, double *out_y, double *out_w, double *out_h) {
    double x0, y0, x1, y1;
    double src_w = cmd->src_right - cmd->src_left;
    double src_h = cmd->src_bottom - cmd->src_top;
    rubraview_mat3x2_apply(cmd->transform, 0.0, 0.0, &x0, &y0);
    rubraview_mat3x2_apply(cmd->transform, src_w, src_h, &x1, &y1);
    *out_x = x0;
    *out_y = y0;
    *out_w = x1 - x0;
    *out_h = y1 - y0;
}

int main(void) {
    printf("[test_compositor] Starting multi-page compositor unit tests...\n");

    const rubraview_page_size_t page = { .width = 800, .height = 1200 };

    /* Test 1: A single page fits the window and is centred. */
    {
        rubraview_spread_t spread = { .left_index = 0, .right_index = -1, .left_half = RUBRAVIEW_SPREAD_WHOLE };
        rubraview_composition_t c = rubraview_compose_spread(&spread, &page, NULL, 1200, 1200,
                                                             RUBRAVIEW_FIT_WINDOW, 8.0, 1.0, 0.0, 0.0);
        assert(c.count == 1);
        assert(c.commands[0].page_index == 0);
        assert(approx(c.content_width, 800) && approx(c.content_height, 1200));
        assert(approx(c.scale, 1.0)); /* height-limited: 1200/1200 */

        double x, y, w, h;
        mapped_bounds(&c.commands[0], &x, &y, &w, &h);
        assert(approx(w, 800) && approx(h, 1200));
        assert(approx(x, 200.0)); /* (1200 - 800) / 2, pillarboxed */
        assert(approx(y, 0.0));
    }
    printf("  [PASS] Single page is fitted and centred in the window\n");

    /* Test 2: A dual spread places the two pages side by side with the
       gutter between them, and fits the pair as one unit. */
    {
        rubraview_spread_t spread = { .left_index = 3, .right_index = 4, .left_half = RUBRAVIEW_SPREAD_WHOLE };
        double gutter = 16.0;
        rubraview_composition_t c = rubraview_compose_spread(&spread, &page, &page, 1616, 1200,
                                                             RUBRAVIEW_FIT_WINDOW, gutter, 1.0, 0.0, 0.0);
        assert(c.count == 2);
        assert(c.commands[0].page_index == 3);
        assert(c.commands[1].page_index == 4);
        assert(approx(c.content_width, 800 + 16 + 800));
        assert(approx(c.scale, 1.0)); /* 1616 wide window exactly fits 1616 of content */

        double lx, ly, lw, lh, rx, ry, rw, rh;
        mapped_bounds(&c.commands[0], &lx, &ly, &lw, &lh);
        mapped_bounds(&c.commands[1], &rx, &ry, &rw, &rh);

        assert(approx(lx, 0.0));
        assert(approx(lw, 800.0));
        assert(approx(rx, 816.0));           /* left width + gutter */
        assert(approx(rx - (lx + lw), gutter)); /* the gap is exactly the gutter */
        assert(approx(ly, 0.0) && approx(ry, 0.0));
    }
    printf("  [PASS] Dual spread places pages side by side separated by the gutter\n");

    /* Test 3: Pages of unequal height are each centred vertically within
       the spread. */
    {
        rubraview_page_size_t tall = { .width = 800, .height = 1200 };
        rubraview_page_size_t short_page = { .width = 800, .height = 600 };
        rubraview_spread_t spread = { .left_index = 0, .right_index = 1, .left_half = RUBRAVIEW_SPREAD_WHOLE };

        rubraview_composition_t c = rubraview_compose_spread(&spread, &tall, &short_page, 1600, 1200,
                                                             RUBRAVIEW_FIT_WINDOW, 0.0, 1.0, 0.0, 0.0);
        assert(c.count == 2);
        assert(approx(c.content_height, 1200)); /* the taller page sets the height */

        double lx, ly, lw, lh, rx, ry, rw, rh;
        mapped_bounds(&c.commands[0], &lx, &ly, &lw, &lh);
        mapped_bounds(&c.commands[1], &rx, &ry, &rw, &rh);
        assert(approx(ly, 0.0));    /* tall page fills the height */
        assert(approx(ry, 300.0));  /* short page centred: (1200 - 600) / 2 */
    }
    printf("  [PASS] Unequal page heights are each centred within the spread\n");

    /* Test 4: A split spread half draws only that half of the source. */
    {
        rubraview_page_size_t wide = { .width = 1600, .height = 1200 };
        rubraview_spread_t left_half = { .left_index = 7, .right_index = -1, .left_half = RUBRAVIEW_SPREAD_LEFT_HALF };
        rubraview_composition_t cl = rubraview_compose_spread(&left_half, &wide, NULL, 800, 1200,
                                                              RUBRAVIEW_FIT_WINDOW, 0.0, 1.0, 0.0, 0.0);
        assert(cl.count == 1);
        assert(approx(cl.commands[0].src_left, 0.0));
        assert(approx(cl.commands[0].src_right, 800.0));
        assert(approx(cl.content_width, 800.0)); /* only half the page is composed */

        rubraview_spread_t right_half = { .left_index = 7, .right_index = -1, .left_half = RUBRAVIEW_SPREAD_RIGHT_HALF };
        rubraview_composition_t cr = rubraview_compose_spread(&right_half, &wide, NULL, 800, 1200,
                                                              RUBRAVIEW_FIT_WINDOW, 0.0, 1.0, 0.0, 0.0);
        assert(cr.count == 1);
        assert(approx(cr.commands[0].src_left, 800.0));
        assert(approx(cr.commands[0].src_right, 1600.0));
    }
    printf("  [PASS] Split spread halves select the correct source sub-rectangle\n");

    /* Test 5: Zoom multiplies the fit scale and keeps the content centred. */
    {
        rubraview_spread_t spread = { .left_index = 0, .right_index = -1, .left_half = RUBRAVIEW_SPREAD_WHOLE };
        rubraview_composition_t c = rubraview_compose_spread(&spread, &page, NULL, 1200, 1200,
                                                             RUBRAVIEW_FIT_WINDOW, 0.0, 2.0, 0.0, 0.0);
        assert(approx(c.scale, 2.0));

        double x, y, w, h;
        mapped_bounds(&c.commands[0], &x, &y, &w, &h);
        assert(approx(w, 1600.0) && approx(h, 2400.0));
        /* Centred: the overflow is split evenly, so the origin goes negative. */
        assert(approx(x, (1200.0 - 1600.0) / 2.0));
        assert(approx(y, (1200.0 - 2400.0) / 2.0));
    }
    printf("  [PASS] Zoom multiplies the fit scale and stays centred\n");

    /* Test 6: Pan shifts the composed result by exactly the given offset. */
    {
        rubraview_spread_t spread = { .left_index = 0, .right_index = -1, .left_half = RUBRAVIEW_SPREAD_WHOLE };
        rubraview_composition_t base = rubraview_compose_spread(&spread, &page, NULL, 1200, 1200,
                                                                RUBRAVIEW_FIT_WINDOW, 0.0, 1.0, 0.0, 0.0);
        rubraview_composition_t panned = rubraview_compose_spread(&spread, &page, NULL, 1200, 1200,
                                                                  RUBRAVIEW_FIT_WINDOW, 0.0, 1.0, 45.0, -30.0);
        double bx, by, bw, bh, px, py, pw, ph;
        mapped_bounds(&base.commands[0], &bx, &by, &bw, &bh);
        mapped_bounds(&panned.commands[0], &px, &py, &pw, &ph);
        assert(approx(px - bx, 45.0));
        assert(approx(py - by, -30.0));
        assert(approx(pw, bw) && approx(ph, bh)); /* pan must not change size */
    }
    printf("  [PASS] Pan shifts the composition without changing its size\n");

    /* Test 7: A spread whose page has not loaded yet composes nothing
       rather than dividing by zero. */
    {
        rubraview_spread_t spread = { .left_index = 0, .right_index = -1, .left_half = RUBRAVIEW_SPREAD_WHOLE };
        rubraview_composition_t c = rubraview_compose_spread(&spread, NULL, NULL, 1200, 1200,
                                                             RUBRAVIEW_FIT_WINDOW, 0.0, 1.0, 0.0, 0.0);
        assert(c.count == 0);

        rubraview_composition_t degenerate = rubraview_compose_spread(&spread, &page, NULL, 0, 0,
                                                                      RUBRAVIEW_FIT_WINDOW, 0.0, 1.0, 0.0, 0.0);
        assert(degenerate.count == 0);
    }
    printf("  [PASS] Unloaded pages and degenerate windows compose nothing, no divide by zero\n");

    /* Test 8: When only the right page of a pair has loaded, it is drawn
       alone with no phantom gutter. */
    {
        rubraview_spread_t spread = { .left_index = 2, .right_index = 3, .left_half = RUBRAVIEW_SPREAD_WHOLE };
        rubraview_composition_t c = rubraview_compose_spread(&spread, NULL, &page, 1200, 1200,
                                                             RUBRAVIEW_FIT_WINDOW, 16.0, 1.0, 0.0, 0.0);
        assert(c.count == 1);
        assert(c.commands[0].page_index == 3);
        assert(approx(c.content_width, 800.0)); /* no gutter is added for a missing side */
    }
    printf("  [PASS] A half-loaded pair draws the available page with no phantom gutter\n");

    printf("[test_compositor] All tests passed successfully!\n");
    return 0;
}
