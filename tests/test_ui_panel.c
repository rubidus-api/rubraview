#include "rubraview/ui_panel.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }

enum { ID_EXPOSURE = 1, ID_CONTRAST, ID_CLEAN, ID_FORMAT, ID_APPLY };

static rubraview_panel_t build(void) {
    rubraview_panel_t p = rubraview_panel_create(lit("Adjust"), 1.0);
    rubraview_panel_add_slider(&p, ID_EXPOSURE, lit("Exposure"), 0.0, -3.0, 3.0, 0.0);
    rubraview_panel_add_slider(&p, ID_CONTRAST, lit("Contrast"), 0.0, -100.0, 100.0, 0.0);
    rubraview_panel_add_separator(&p);
    rubraview_panel_add_toggle(&p, ID_CLEAN, lit("Privacy clean"), false);
    rubraview_panel_add_choice(&p, ID_FORMAT, lit("Format"), 0, 3);
    rubraview_panel_add_button(&p, ID_APPLY, lit("Apply"));
    p.open = true;
    rubraview_panel_layout(&p, 1600.0, 900.0);
    return p;
}

int main(void) {
    printf("[test_ui_panel] Starting panel model unit tests...\n");

    /* Test 1: the panel sits inside the window, against the right edge. */
    {
        rubraview_panel_t p = build();
        assert(p.bounds.x + p.bounds.width <= 1600.0);
        assert(p.bounds.y >= 0.0);
        assert(p.bounds.y + p.bounds.height <= 900.0);
        assert(p.bounds.x > 800.0);  /* the right half, not over the reading area */
    }
    printf("  [PASS] The panel is placed inside the window, against the right edge\n");

    /* Test 2: a panel with more rows than the window is tall is clamped
       rather than running off the bottom. */
    {
        rubraview_panel_t p = rubraview_panel_create(lit("Long"), 1.0);
        for (int i = 0; i < RUBRAVIEW_PANEL_MAX_ROWS; ++i) {
            rubraview_panel_add_slider(&p, i, lit("Row"), 0.0, 0.0, 1.0, 0.0);
        }
        p.open = true;
        rubraview_panel_layout(&p, 800.0, 200.0);
        assert(p.bounds.y >= 0.0);
        assert(p.bounds.height <= 200.0);
    }
    printf("  [PASS] A panel taller than the window is clamped to it\n");

    /* Test 3: rows do not overlap, and separators take less room than
       rows do. */
    {
        rubraview_panel_t p = build();
        rubraview_rect_t previous = {0};
        for (size_t i = 0; i < p.row_count; ++i) {
            rubraview_rect_t r = rubraview_panel_row_rect(&p, i);
            if (i > 0) assert(r.y >= previous.y + previous.height - 0.001);
            previous = r;
        }
        rubraview_rect_t sep = rubraview_panel_row_rect(&p, 2);
        rubraview_rect_t slider = rubraview_panel_row_rect(&p, 0);
        assert(sep.height < slider.height);
    }
    printf("  [PASS] Rows are stacked without overlap; a separator is thinner\n");

    /* Test 4: hit testing finds the right row, ignores separators, and
       returns nothing outside the panel. */
    {
        rubraview_panel_t p = build();
        rubraview_rect_t contrast = rubraview_panel_row_rect(&p, 1);
        int32_t hit = rubraview_panel_row_at(&p, contrast.x + 5.0, contrast.y + contrast.height / 2.0);
        assert(hit == 1);

        rubraview_rect_t sep = rubraview_panel_row_rect(&p, 2);
        assert(rubraview_panel_row_at(&p, sep.x + 5.0, sep.y + sep.height / 2.0) == -1);

        assert(rubraview_panel_row_at(&p, 10.0, 10.0) == -1);

        /* A closed panel swallows nothing. */
        p.open = false;
        assert(rubraview_panel_row_at(&p, contrast.x + 5.0, contrast.y + 2.0) == -1);
    }
    printf("  [PASS] Hit testing finds rows, skips separators and ignores a closed panel\n");

    /* Test 5: clicking a track jumps there and starts a drag. Hunting
       for the handle is not something a reader should have to do. */
    {
        rubraview_panel_t p = build();
        rubraview_rect_t control = rubraview_panel_control_rect(&p, 0);
        int32_t row = -1;

        /* One pixel inside the right edge: a rectangle's own right edge
           is not inside it. */
        rubraview_panel_event_t e = rubraview_panel_press(&p, control.x + control.width - 1.0,
                                                          control.y + control.height / 2.0, &row);
        assert(e == RUBRAVIEW_PANEL_VALUE_CHANGED && row == 0);
        assert(p.rows[0].value > 2.9);   /* the far right end of the track */
        assert(p.active_row == 0);

        /* Dragging back to the far left reaches the minimum, and the
           vertical position is ignored — dragging off the row must not
           drop the slider. */
        e = rubraview_panel_drag(&p, control.x - 500.0, control.y + 400.0, &row);
        assert(e == RUBRAVIEW_PANEL_VALUE_CHANGED);
        assert(fabs(p.rows[0].value + 3.0) < 0.0001);

        rubraview_panel_release(&p);
        assert(p.active_row == -1);
        e = rubraview_panel_drag(&p, control.x, control.y, &row);
        assert(e == RUBRAVIEW_PANEL_NONE);
    }
    printf("  [PASS] A track click jumps and drags; releasing ends it\n");

    /* Test 6: the middle of a track is the middle of its range. */
    {
        rubraview_panel_t p = build();
        rubraview_rect_t control = rubraview_panel_control_rect(&p, 1);
        int32_t row = -1;
        rubraview_panel_press(&p, control.x + control.width / 2.0, control.y + 1.0, &row);
        assert(fabs(p.rows[1].value) < 1.0);   /* -100..100 → about 0 */
    }
    printf("  [PASS] The middle of a track is the middle of its range\n");

    /* Test 7: toggles flip, choices cycle and wrap, buttons report. */
    {
        rubraview_panel_t p = build();
        int32_t row = -1;

        rubraview_rect_t toggle = rubraview_panel_row_rect(&p, 3);
        rubraview_panel_press(&p, toggle.x + 5.0, toggle.y + 2.0, &row);
        assert(p.rows[3].value == 1.0);
        rubraview_panel_press(&p, toggle.x + 5.0, toggle.y + 2.0, &row);
        assert(p.rows[3].value == 0.0);

        rubraview_rect_t choice = rubraview_panel_row_rect(&p, 4);
        for (int i = 0; i < 3; ++i) rubraview_panel_press(&p, choice.x + 5.0, choice.y + 2.0, &row);
        assert(p.rows[4].value == 0.0);  /* three presses over three choices wraps home */

        rubraview_rect_t button = rubraview_panel_row_rect(&p, 5);
        rubraview_panel_event_t e = rubraview_panel_press(&p, button.x + 5.0, button.y + 2.0, &row);
        assert(e == RUBRAVIEW_PANEL_BUTTON_PRESSED && row == 5);
    }
    printf("  [PASS] Toggles flip, choices wrap and buttons report themselves\n");

    /* Test 8: a stepped slider cannot rest between legal values. */
    {
        rubraview_panel_t p = rubraview_panel_create(lit("Quality"), 1.0);
        rubraview_panel_add_slider(&p, 1, lit("PNG level"), 6.0, 0.0, 9.0, 1.0);
        p.open = true;
        rubraview_panel_layout(&p, 1000.0, 800.0);

        rubraview_rect_t control = rubraview_panel_control_rect(&p, 0);
        int32_t row = -1;
        rubraview_panel_press(&p, control.x + control.width * 0.51, control.y + 1.0, &row);
        double value = p.rows[0].value;
        assert(fabs(value - floor(value + 0.5)) < 0.0001);
        assert(value >= 0.0 && value <= 9.0);
    }
    printf("  [PASS] A stepped slider only comes to rest on a legal value\n");

    /* Test 9: values can be set from outside, found by id, and are
       clamped the same way a drag would clamp them. */
    {
        rubraview_panel_t p = build();
        assert(rubraview_panel_find(&p, ID_CONTRAST) == 1);
        assert(rubraview_panel_find(&p, 999) == -1);

        rubraview_panel_set_value(&p, ID_EXPOSURE, 99.0);
        assert(fabs(p.rows[0].value - 3.0) < 0.0001);
        assert(fabs(rubraview_panel_fill_fraction(&p, 0) - 1.0) < 0.0001);

        rubraview_panel_set_value(&p, ID_EXPOSURE, 0.0);
        assert(fabs(rubraview_panel_fill_fraction(&p, 0) - 0.5) < 0.0001);
    }
    printf("  [PASS] Values set from outside are clamped, and the fill matches\n");

    /* Test 10: a full panel refuses more rows instead of overrunning. */
    {
        rubraview_panel_t p = rubraview_panel_create(lit("Full"), 1.0);
        for (int i = 0; i < RUBRAVIEW_PANEL_MAX_ROWS; ++i) {
            assert(rubraview_panel_add_toggle(&p, i, lit("x"), false));
        }
        assert(!rubraview_panel_add_toggle(&p, 99, lit("one too many"), false));
        assert(p.row_count == RUBRAVIEW_PANEL_MAX_ROWS);
    }
    printf("  [PASS] A full panel refuses further rows\n");

    printf("[test_ui_panel] All tests passed successfully!\n");
    return 0;
}
