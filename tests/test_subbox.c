#include "rubraview/subbox.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool near(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void) {
    printf("[test_subbox] Starting subtitle box unit tests...\n");
    const double W = 1000.0, H = 600.0;
    rubraview_rect_t def = { 50.0, 480.0, 900.0, 60.0 };   /* bottom edge at 540 */

    /* Unplaced: the default place, with the height the text needs. */
    rubraview_subbox_t box = {0};
    rubraview_rect_t r = rubraview_subbox_rect(&box, W, H, 80.0, def);
    assert(near(r.x, 50.0) && near(r.width, 900.0) && near(r.y + r.height, 540.0) && near(r.height, 80.0));
    printf("  [PASS] An unplaced box sits in the default place, as tall as its text\n");

    /* Placed boxes are fractions: a bigger window keeps the place. */
    box = (rubraview_subbox_t){ .placed = true, .left = 0.1, .bottom = 0.5, .width = 0.4 };
    r = rubraview_subbox_rect(&box, W, H, 60.0, def);
    assert(near(r.x, 100.0) && near(r.y, 240.0) && near(r.width, 400.0));
    r = rubraview_subbox_rect(&box, 2000.0, 1200.0, 60.0, def);
    assert(near(r.x, 200.0) && near(r.y, 540.0) && near(r.width, 800.0));
    /* Never outside the window, whatever was saved. */
    box = (rubraview_subbox_t){ .placed = true, .left = 0.9, .bottom = 0.01, .width = 0.5 };
    r = rubraview_subbox_rect(&box, W, H, 60.0, def);
    assert(near(r.x, 500.0) && near(r.y, 0.0));
    printf("  [PASS] A placed box keeps its place as fractions, and stays inside the window\n");

    /* Buttons: S M R X above the top-right corner, inside when no room. */
    rubraview_rect_t b[RUBRAVIEW_SUBBOX_BUTTONS];
    rubraview_subbox_buttons((rubraview_rect_t){ 100.0, 300.0, 400.0, 60.0 }, 20.0, 4.0, b);
    assert(near(b[3].x + b[3].width, 500.0) && near(b[0].y, 276.0) && b[0].x < b[1].x && b[2].x < b[3].x);
    rubraview_subbox_buttons((rubraview_rect_t){ 100.0, 10.0, 400.0, 60.0 }, 20.0, 4.0, b);
    assert(near(b[0].y, 14.0));
    printf("  [PASS] The four buttons sit above the top-right corner, or inside it\n");

    /* Hit tests: an empty unselected box takes nothing; buttons only when selected. */
    box = (rubraview_subbox_t){ .placed = true, .left = 0.1, .bottom = 0.6, .width = 0.4 };
    r = rubraview_subbox_rect(&box, W, H, 60.0, def);            /* 100,300 400x60 */
    assert(rubraview_subbox_hit(&box, r, 20.0, 4.0, false, 200.0, 330.0) == RUBRAVIEW_SUBBOX_NONE);
    assert(rubraview_subbox_hit(&box, r, 20.0, 4.0, true, 200.0, 330.0) == RUBRAVIEW_SUBBOX_BODY);
    rubraview_subbox_buttons(r, 20.0, 4.0, b);
    assert(rubraview_subbox_hit(&box, r, 20.0, 4.0, true, b[1].x + 5, b[1].y + 5) == RUBRAVIEW_SUBBOX_NONE);
    box.selected = true;
    assert(rubraview_subbox_hit(&box, r, 20.0, 4.0, false, 200.0, 330.0) == RUBRAVIEW_SUBBOX_BODY);
    assert(rubraview_subbox_hit(&box, r, 20.0, 4.0, false, b[0].x + 5, b[0].y + 5) == RUBRAVIEW_SUBBOX_SETTINGS);
    assert(rubraview_subbox_hit(&box, r, 20.0, 4.0, false, b[1].x + 5, b[1].y + 5) == RUBRAVIEW_SUBBOX_MOVE);
    assert(rubraview_subbox_hit(&box, r, 20.0, 4.0, false, b[2].x + 5, b[2].y + 5) == RUBRAVIEW_SUBBOX_RESIZE);
    assert(rubraview_subbox_hit(&box, r, 20.0, 4.0, false, b[3].x + 5, b[3].y + 5) == RUBRAVIEW_SUBBOX_CLOSE);
    printf("  [PASS] An empty box takes no taps; its buttons answer only while it is selected\n");

    /* Move: the box follows the pointer and stays inside. */
    assert(!rubraview_subbox_begin_drag(&box, RUBRAVIEW_SUBBOX_BODY, r, 0, 0));
    assert(rubraview_subbox_begin_drag(&box, RUBRAVIEW_SUBBOX_MOVE, r, b[1].x + 5, b[1].y + 5));
    rubraview_subbox_drag(&box, r, b[1].x + 5 - 50.0, b[1].y + 5 - 100.0, W, H, 120, 24, 400, NULL);
    r = rubraview_subbox_rect(&box, W, H, 60.0, def);
    assert(near(r.x, 50.0) && near(r.y, 200.0));
    rubraview_subbox_drag(&box, r, -5000.0, -5000.0, W, H, 120, 24, 400, NULL);
    r = rubraview_subbox_rect(&box, W, H, 60.0, def);
    assert(near(r.x, 0.0) && near(r.y, 0.0));
    rubraview_subbox_end_drag(&box);
    printf("  [PASS] M moves the box with the pointer, never out of the window\n");

    /* Resize: bottom-left held, top-right follows, limits kept. */
    box = (rubraview_subbox_t){ .placed = true, .left = 0.1, .bottom = 0.6, .width = 0.4, .selected = true };
    r = rubraview_subbox_rect(&box, W, H, 60.0, def);            /* 100,300 400x60, top-right (500,300) */
    rubraview_subbox_buttons(r, 20.0, 4.0, b);
    double gx = b[2].x + 5, gy = b[2].y + 5, h = 0.0;
    assert(rubraview_subbox_begin_drag(&box, RUBRAVIEW_SUBBOX_RESIZE, r, gx, gy));
    rubraview_subbox_drag(&box, r, gx + 100.0, gy - 40.0, W, H, 120, 24, 400, &h);
    assert(near(h, 100.0));
    r = rubraview_subbox_rect(&box, W, H, h, def);
    assert(near(r.x, 100.0) && near(r.width, 500.0) && near(r.y + r.height, 360.0) && near(r.y, 260.0));
    rubraview_subbox_drag(&box, r, gx - 1000.0, gy + 1000.0, W, H, 120, 24, 400, &h);
    assert(near(h, 24.0) && near(box.width * W, 120.0));
    rubraview_subbox_drag(&box, r, 5000.0, -5000.0, W, H, 120, 24, 400, &h);
    assert(near(h, 360.0) && near(box.width * W, 900.0));   /* the room above the bottom edge; the window's right edge */
    printf("  [PASS] R resizes from the top-right corner within the limits\n");

    /* An unplaced box becomes placed where it was, on the first drag. */
    box = (rubraview_subbox_t){ .selected = true };
    r = rubraview_subbox_rect(&box, W, H, 60.0, def);
    rubraview_subbox_buttons(r, 20.0, 4.0, b);
    assert(rubraview_subbox_begin_drag(&box, RUBRAVIEW_SUBBOX_MOVE, r, b[1].x, b[1].y));
    rubraview_subbox_drag(&box, r, b[1].x, b[1].y, W, H, 120, 24, 400, NULL);
    assert(box.placed && near(box.left * W, 50.0) && near(box.width * W, 900.0) && near(box.bottom * H, 540.0));
    printf("  [PASS] The first drag of an unplaced box starts from where it was drawn\n");

    /* The Sub tile's taps. */
    rubraview_tap_t t = {0};
    assert(rubraview_tap_press(&t, 10.0) == RUBRAVIEW_TAP_NOTHING);
    assert(rubraview_tap_release(&t, 10.1) == RUBRAVIEW_TAP_NOTHING);
    assert(rubraview_tap_waiting(&t));
    assert(rubraview_tap_tick(&t, 10.3) == RUBRAVIEW_TAP_NOTHING);   /* still inside the double-tap window */
    assert(rubraview_tap_tick(&t, 10.5) == RUBRAVIEW_TAP_SINGLE);
    assert(!rubraview_tap_waiting(&t));
    printf("  [PASS] A tap is a single tap once the double-tap window has passed\n");

    t = (rubraview_tap_t){0};
    (void)rubraview_tap_press(&t, 20.0);
    (void)rubraview_tap_release(&t, 20.08);
    assert(rubraview_tap_press(&t, 20.3) == RUBRAVIEW_TAP_CHOOSE);
    assert(rubraview_tap_release(&t, 20.35) == RUBRAVIEW_TAP_NOTHING);
    assert(rubraview_tap_tick(&t, 21.0) == RUBRAVIEW_TAP_NOTHING);  /* no stray single tap afterwards */
    printf("  [PASS] A double tap chooses, and leaves no single tap behind\n");

    t = (rubraview_tap_t){0};
    (void)rubraview_tap_press(&t, 30.0);
    assert(rubraview_tap_tick(&t, 30.4) == RUBRAVIEW_TAP_NOTHING);
    assert(rubraview_tap_tick(&t, 30.5) == RUBRAVIEW_TAP_CHOOSE);
    assert(rubraview_tap_tick(&t, 30.9) == RUBRAVIEW_TAP_NOTHING);
    assert(rubraview_tap_release(&t, 31.0) == RUBRAVIEW_TAP_NOTHING);
    assert(rubraview_tap_tick(&t, 32.0) == RUBRAVIEW_TAP_NOTHING);
    /* Held and let go before any tick looked: still a choose. */
    t = (rubraview_tap_t){0};
    (void)rubraview_tap_press(&t, 40.0);
    assert(rubraview_tap_release(&t, 40.7) == RUBRAVIEW_TAP_CHOOSE);
    /* Two taps far apart are two single taps. */
    t = (rubraview_tap_t){0};
    (void)rubraview_tap_press(&t, 50.0); (void)rubraview_tap_release(&t, 50.1);
    assert(rubraview_tap_tick(&t, 50.6) == RUBRAVIEW_TAP_SINGLE);
    assert(rubraview_tap_press(&t, 51.0) == RUBRAVIEW_TAP_NOTHING);
    printf("  [PASS] A held press chooses once; taps far apart are single taps\n");

    /* The list of choices: above the tile, inside the window. */
    rubraview_rect_t list = rubraview_choice_list_rect(W, H, 3, 40.0, 300.0, 500.0, 500.0);
    assert(near(list.x, 350.0) && near(list.y, 340.0) && near(list.height, 160.0));
    assert(rubraview_choice_list_row_at(list, 40.0, 3, 400.0, 350.0) == -1);   /* the title */
    assert(rubraview_choice_list_row_at(list, 40.0, 3, 400.0, 385.0) == 0);
    assert(rubraview_choice_list_row_at(list, 40.0, 3, 400.0, 499.0) == 2);
    assert(rubraview_choice_list_row_at(list, 40.0, 3, 10.0, 400.0) == -1);
    list = rubraview_choice_list_rect(W, H, 3, 40.0, 300.0, 990.0, 50.0);
    assert(near(list.x, 700.0) && near(list.y, 0.0));
    printf("  [PASS] The list of choices sits above its tile, inside the window, a row a choice\n");

    printf("[test_subbox] All tests passed successfully!\n");
    return 0;
}
