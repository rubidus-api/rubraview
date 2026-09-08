#include "rubraview/ui_input.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    printf("[test_ui_input] Starting pointer and wheel semantics unit tests...\n");

    rubraview_pointer_context_t comic = {
        .window_width = 1000.0,
        .reading_direction = RUBRAVIEW_READING_LTR,
        .comic_mode = true,
        .zoomed_in = false,
        .scrollable_vertically = false,
    };

    /* Test 1: The canvas splits 30 / 40 / 30 (§3.7.3). */
    {
        assert(rubraview_click_zone_at(0.0, 1000.0) == RUBRAVIEW_ZONE_LEFT);
        assert(rubraview_click_zone_at(299.0, 1000.0) == RUBRAVIEW_ZONE_LEFT);
        assert(rubraview_click_zone_at(300.0, 1000.0) == RUBRAVIEW_ZONE_CENTER);
        assert(rubraview_click_zone_at(699.0, 1000.0) == RUBRAVIEW_ZONE_CENTER);
        assert(rubraview_click_zone_at(700.0, 1000.0) == RUBRAVIEW_ZONE_RIGHT);
        assert(rubraview_click_zone_at(999.0, 1000.0) == RUBRAVIEW_ZONE_RIGHT);
    }
    printf("  [PASS] Click zones split the canvas 30 / 40 / 30\n");

    /* Test 2: Left-to-right reading — left zone goes back, right zone
       goes forward, centre toggles the overlay. */
    {
        assert(rubraview_pointer_click(&comic, 100.0, false) == RUBRAVIEW_INTENT_PREV_PAGE);
        assert(rubraview_pointer_click(&comic, 900.0, false) == RUBRAVIEW_INTENT_NEXT_PAGE);
        assert(rubraview_pointer_click(&comic, 500.0, false) == RUBRAVIEW_INTENT_TOGGLE_OVERLAY);
    }
    printf("  [PASS] LTR click zones flip pages in reading order\n");

    /* Test 3: Right-to-left (manga) mirrors which side advances. */
    {
        rubraview_pointer_context_t manga = comic;
        manga.reading_direction = RUBRAVIEW_READING_RTL;
        assert(rubraview_pointer_click(&manga, 100.0, false) == RUBRAVIEW_INTENT_NEXT_PAGE);
        assert(rubraview_pointer_click(&manga, 900.0, false) == RUBRAVIEW_INTENT_PREV_PAGE);
        assert(rubraview_pointer_click(&manga, 500.0, false) == RUBRAVIEW_INTENT_TOGGLE_OVERLAY);
    }
    printf("  [PASS] RTL manga order mirrors which side advances\n");

    /* Test 4: While zoomed in, dragging pans instead of flipping pages. */
    {
        rubraview_pointer_context_t zoomed = comic;
        zoomed.zoomed_in = true;
        assert(rubraview_pointer_click(&zoomed, 100.0, false) == RUBRAVIEW_INTENT_PAN);
        assert(rubraview_pointer_click(&zoomed, 900.0, false) == RUBRAVIEW_INTENT_PAN);
    }
    printf("  [PASS] Zoomed in, a drag pans rather than flipping pages\n");

    /* Test 5: Outside comic mode a click just toggles the overlay. */
    {
        rubraview_pointer_context_t photo = comic;
        photo.comic_mode = false;
        assert(rubraview_pointer_click(&photo, 100.0, false) == RUBRAVIEW_INTENT_TOGGLE_OVERLAY);
    }
    printf("  [PASS] Outside comic mode, clicks only toggle the overlay\n");

    /* Test 6: Double click toggles fullscreen from any zone, even zoomed. */
    {
        assert(rubraview_pointer_click(&comic, 100.0, true) == RUBRAVIEW_INTENT_TOGGLE_FULLSCREEN);
        rubraview_pointer_context_t zoomed = comic;
        zoomed.zoomed_in = true;
        assert(rubraview_pointer_click(&zoomed, 500.0, true) == RUBRAVIEW_INTENT_TOGGLE_FULLSCREEN);
    }
    printf("  [PASS] Double click toggles fullscreen from any zone\n");

    /* Test 7: Middle click toggles 1:1, right click opens the context menu. */
    {
        assert(rubraview_pointer_middle_click(&comic) == RUBRAVIEW_INTENT_TOGGLE_ACTUAL_SIZE);
        assert(rubraview_pointer_right_click(&comic) == RUBRAVIEW_INTENT_CONTEXT_MENU);
    }
    printf("  [PASS] Middle click toggles 1:1; right click opens the context menu\n");

    /* Test 8: Wheel semantics — Ctrl zooms, Shift skips, plain flips. */
    {
        assert(rubraview_pointer_wheel(&comic, 1.0, RUBRAVIEW_MOD_CTRL) == RUBRAVIEW_INTENT_ZOOM_IN);
        assert(rubraview_pointer_wheel(&comic, -1.0, RUBRAVIEW_MOD_CTRL) == RUBRAVIEW_INTENT_ZOOM_OUT);
        assert(rubraview_pointer_wheel(&comic, 1.0, RUBRAVIEW_MOD_SHIFT) == RUBRAVIEW_INTENT_SKIP_BACKWARD);
        assert(rubraview_pointer_wheel(&comic, -1.0, RUBRAVIEW_MOD_SHIFT) == RUBRAVIEW_INTENT_SKIP_FORWARD);
        assert(rubraview_pointer_wheel(&comic, 1.0, RUBRAVIEW_MOD_NONE) == RUBRAVIEW_INTENT_PREV_PAGE);
        assert(rubraview_pointer_wheel(&comic, -1.0, RUBRAVIEW_MOD_NONE) == RUBRAVIEW_INTENT_NEXT_PAGE);
        assert(rubraview_pointer_wheel(&comic, 0.0, RUBRAVIEW_MOD_NONE) == RUBRAVIEW_INTENT_NONE);
    }
    printf("  [PASS] Wheel: Ctrl zooms, Shift skips, plain notch flips pages\n");

    /* Test 9: When the image overflows vertically (Fit to Width on a
       webtoon), a plain wheel scrolls instead of flipping. */
    {
        rubraview_pointer_context_t webtoon = comic;
        webtoon.scrollable_vertically = true;
        assert(rubraview_pointer_wheel(&webtoon, -1.0, RUBRAVIEW_MOD_NONE) == RUBRAVIEW_INTENT_SCROLL_VERTICAL);
        /* ...but Ctrl still zooms, scrollable or not. */
        assert(rubraview_pointer_wheel(&webtoon, 1.0, RUBRAVIEW_MOD_CTRL) == RUBRAVIEW_INTENT_ZOOM_IN);
    }
    printf("  [PASS] A vertically scrollable view scrolls on a plain wheel notch\n");

    /* Test 10: Side buttons map to previous/next page. */
    {
        assert(rubraview_pointer_side_button(true) == RUBRAVIEW_INTENT_NEXT_PAGE);
        assert(rubraview_pointer_side_button(false) == RUBRAVIEW_INTENT_PREV_PAGE);
    }
    printf("  [PASS] Side buttons map to next and previous page\n");

    /* Test 11: A swipe must pass the distance threshold, and its
       direction follows the reading order. */
    {
        assert(rubraview_pointer_swipe(&comic, -20.0, 48.0) == RUBRAVIEW_INTENT_NONE); /* too short */
        assert(rubraview_pointer_swipe(&comic, -100.0, 48.0) == RUBRAVIEW_INTENT_NEXT_PAGE);
        assert(rubraview_pointer_swipe(&comic, 100.0, 48.0) == RUBRAVIEW_INTENT_PREV_PAGE);

        rubraview_pointer_context_t manga = comic;
        manga.reading_direction = RUBRAVIEW_READING_RTL;
        assert(rubraview_pointer_swipe(&manga, -100.0, 48.0) == RUBRAVIEW_INTENT_PREV_PAGE);
        assert(rubraview_pointer_swipe(&manga, 100.0, 48.0) == RUBRAVIEW_INTENT_NEXT_PAGE);
    }
    printf("  [PASS] Swipes need a minimum distance and follow the reading order\n");

    printf("[test_ui_input] All tests passed successfully!\n");
    return 0;
}
