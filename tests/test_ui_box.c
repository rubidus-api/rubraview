#include "rubraview/ui_box.h"
#include "rubraview/ui_menu.h"
#include "rubraview/ui_actions.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static bool approx(double a, double b) { return fabs(a - b) < 1e-9; }

static bool str_eq(u8str_t s, const char *l) {
    size_t n = strlen(l);
    return s.len == n && (n == 0 || memcmp(s.ptr, l, n) == 0);
}

/* The two halves of the anchor, and the corner a box goes home to. */
static void test_two_part_anchor(void) {
    rubraview_tile_metrics_t m = rubraview_tile_metrics_default(1.0);

    /* Test: the bar is two squares, side by side, and each half knows
       itself. */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_MENU, 100.0, 200.0, 6);

        rubraview_rect_t bar = rubraview_box_anchor_rect(&box, &m);
        assert(approx(bar.width, m.anchor_size * 2.0) && approx(bar.height, m.anchor_size));

        rubraview_rect_t left = rubraview_box_anchor_half_rect(&box, &m, RUBRAVIEW_ANCHOR_CLICK);
        rubraview_rect_t right = rubraview_box_anchor_half_rect(&box, &m, RUBRAVIEW_ANCHOR_HOVER);
        assert(approx(left.width, m.anchor_size) && approx(right.width, m.anchor_size));
        assert(approx(left.x, 100.0) && approx(right.x, 100.0 + m.anchor_size));
        assert(approx(left.y, right.y));   /* side by side, not stacked */

        assert(rubraview_box_anchor_half_at(&box, &m, 105.0, 205.0) == RUBRAVIEW_ANCHOR_CLICK);
        assert(rubraview_box_anchor_half_at(&box, &m, 100.0 + m.anchor_size + 5.0, 205.0) == RUBRAVIEW_ANCHOR_HOVER);
        assert(rubraview_box_anchor_half_at(&box, &m, 50.0, 205.0) == RUBRAVIEW_ANCHOR_NONE);
    }
    printf("  [PASS] The anchor is two squares side by side, each knowing itself\n");

    /* Test: hovering the right half opens; hovering the left half does
       not. That difference is the whole point of splitting them. */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_MENU, 0.0, 0.0, 6);

        rubraview_box_pointer(&box, &m, m.anchor_size * 0.5, m.anchor_size * 0.5);
        assert(box.state == RUBRAVIEW_BOX_COLLAPSED);   /* the click half: nothing */

        rubraview_box_pointer(&box, &m, m.anchor_size * 1.5, m.anchor_size * 0.5);
        assert(box.state == RUBRAVIEW_BOX_EXPANDED);    /* the hover half: opens */

        /* Moving away starts the collapse timer rather than collapsing
           at once — §3.6.3's grace period. */
        rubraview_box_pointer(&box, &m, 900.0, 900.0);
        assert(box.state == RUBRAVIEW_BOX_EXPANDED);
        assert(rubraview_box_tick(&box, 1.0, 0.5));
        assert(box.state == RUBRAVIEW_BOX_COLLAPSED);
    }
    printf("  [PASS] Only the hover half opens the box\n");

    /* Test: a pointer resting on the box holds it open, and moving about
       elsewhere does not keep an abandoned box open. Pointer events stop
       arriving when the mouse stops, so the caller re-states where the
       pointer is on every pass. */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_MENU, 0.0, 0.0, 6);
        double hover_x = m.anchor_size * 1.5, hover_y = m.anchor_size * 0.5;

        rubraview_box_pointer(&box, &m, hover_x, hover_y);
        assert(box.state == RUBRAVIEW_BOX_EXPANDED);

        /* A whole second of resting on it, fed a pass at a time. */
        for (int i = 0; i < 10; ++i) {
            rubraview_box_pointer(&box, &m, hover_x, hover_y);
            assert(!rubraview_box_tick(&box, 0.1, 0.5));
        }
        assert(box.state == RUBRAVIEW_BOX_EXPANDED);

        /* Away from it, the grace runs out even while the mouse keeps
           moving somewhere else. */
        bool collapsed = false;
        for (int i = 0; i < 10 && !collapsed; ++i) {
            rubraview_box_pointer(&box, &m, 900.0 + i, 900.0);
            collapsed = rubraview_box_tick(&box, 0.1, 0.5);
        }
        assert(collapsed && box.state == RUBRAVIEW_BOX_COLLAPSED);
    }
    printf("  [PASS] Resting on the box holds it open; leaving it collapses on time\n");

    /* Test: the click half opens and closes, and what it opens stays
       open when the pointer leaves. */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 0.0, 0.0, 6);

        assert(rubraview_box_click(&box, &m, m.anchor_size * 0.5, m.anchor_size * 0.5));
        assert(box.state == RUBRAVIEW_BOX_LOCKED_OPEN);

        rubraview_box_pointer(&box, &m, 900.0, 900.0);
        assert(!rubraview_box_tick(&box, 5.0, 0.5));
        assert(box.state == RUBRAVIEW_BOX_LOCKED_OPEN);   /* a click means "stay" */

        assert(rubraview_box_click(&box, &m, m.anchor_size * 0.5, m.anchor_size * 0.5));
        assert(box.state == RUBRAVIEW_BOX_COLLAPSED);

        /* A click nowhere near the anchor is not this box's business. */
        assert(!rubraview_box_click(&box, &m, 500.0, 500.0));
    }
    printf("  [PASS] The click half toggles, and what it opens stays open\n");

    /* Test: clicking the hover half keeps what hovering opened. */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_MENU, 0.0, 0.0, 6);
        rubraview_box_pointer(&box, &m, m.anchor_size * 1.5, m.anchor_size * 0.5);
        assert(box.state == RUBRAVIEW_BOX_EXPANDED);

        assert(rubraview_box_click(&box, &m, m.anchor_size * 1.5, m.anchor_size * 0.5));
        assert(box.state == RUBRAVIEW_BOX_EXPANDED);   /* taken, but changes nothing */

        rubraview_box_pointer(&box, &m, 900.0, 900.0);
        assert(rubraview_box_tick(&box, 5.0, 0.5));
        assert(box.state == RUBRAVIEW_BOX_COLLAPSED);  /* leaving still folds it */
        assert(rubraview_box_click(&box, &m, m.anchor_size * 1.5, m.anchor_size * 0.5));
        assert(box.state == RUBRAVIEW_BOX_COLLAPSED);  /* and a click there does not open it */
    }
    printf("  [PASS] The hover half only hovers: a click there opens and pins nothing\n");

    /* Test: putting the boxes back. This is the answer to a floating
       box dragged somewhere the reader cannot reach. */
    {
        double win_w = 1280.0, win_h = 800.0;

        rubraview_box_t menu = rubraview_box_create(RUBRAVIEW_BOX_MENU, 0.0, 0.0, 6);
        rubraview_box_t tools = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 0.0, 0.0, 6);
        assert(menu.home == RUBRAVIEW_BOX_HOME_TOP_LEFT);
        assert(tools.home == RUBRAVIEW_BOX_HOME_BOTTOM_RIGHT);

        /* Drag both somewhere useless, including off the window. */
        menu.anchor_x = -5000.0; menu.anchor_y = 9000.0;
        tools.anchor_x = 99999.0; tools.anchor_y = -400.0;
        tools.state = RUBRAVIEW_BOX_DETACHED;

        rubraview_box_snap_home(&menu, &m, win_w, win_h);
        rubraview_box_snap_home(&tools, &m, win_w, win_h);

        rubraview_rect_t menu_box = rubraview_box_bounds(&menu, &m);
        rubraview_rect_t tools_box = rubraview_box_bounds(&tools, &m);

        /* Both are wholly inside the window again. */
        assert(menu_box.x >= 0.0 && menu_box.y >= 0.0);
        assert(menu_box.x + menu_box.width <= win_w && menu_box.y + menu_box.height <= win_h);
        assert(tools_box.x >= 0.0 && tools_box.y >= 0.0);
        assert(tools_box.x + tools_box.width <= win_w && tools_box.y + tools_box.height <= win_h);

        /* And each is in its own corner. */
        assert(menu_box.x < win_w * 0.5 && menu_box.y < win_h * 0.5);
        assert(tools_box.x + tools_box.width > win_w * 0.5);
        assert(tools_box.y + tools_box.height > win_h * 0.5);

        /* A detached toolbox comes back in — leaving it out would
           defeat the button. */
        assert(tools.state != RUBRAVIEW_BOX_DETACHED);
    }
    printf("  [PASS] Both boxes come back to their own corner, inside the window\n");

    /* Test: an open box still fits after snapping, and a window smaller
       than the box still shows the corner with the buttons on it. */
    {
        rubraview_box_t tools = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 0.0, 0.0, 8);
        rubraview_box_hover_enter(&tools);
        rubraview_box_snap_home(&tools, &m, 1280.0, 800.0);

        rubraview_rect_t open_box = rubraview_box_bounds(&tools, &m);
        assert(open_box.x >= 0.0 && open_box.y >= 0.0);
        assert(open_box.x + open_box.width <= 1280.0);
        assert(open_box.y + open_box.height <= 800.0);

        rubraview_box_t tiny = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 0.0, 0.0, 8);
        rubraview_box_hover_enter(&tiny);
        rubraview_box_snap_home(&tiny, &m, 50.0, 40.0);
        rubraview_rect_t tiny_box = rubraview_box_bounds(&tiny, &m);
        assert(tiny_box.x >= 0.0 && tiny_box.y >= 0.0);
    }
    printf("  [PASS] Snapping keeps an open box inside, and survives a tiny window\n");
}

static void test_boxes(void) {
    rubraview_tile_metrics_t m = rubraview_tile_metrics_default(1.0);
    assert(approx(m.tile_size, 64.0) && approx(m.gutter, 8.0));

    /* Test 1: A collapsed box is just the compact anchor; expanding it
       grows to the tile grid. */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_MENU, 100.0, 100.0, 7);
        /* Collapsed, the box is the two-button bar: two squares wide,
           one tall (owner, 2026-09-10). */
        rubraview_rect_t collapsed = rubraview_box_bounds(&box, &m);
        assert(approx(collapsed.width, m.anchor_size * 2.0) && approx(collapsed.height, m.anchor_size));

        rubraview_box_hover_enter(&box);
        assert(box.state == RUBRAVIEW_BOX_EXPANDED);

        /* 7 tiles at 4 columns = 2 rows. */
        rubraview_rect_t expanded = rubraview_box_bounds(&box, &m);
        assert(approx(expanded.width, 8.0 * 2 + 64.0 * 4 + 8.0 * 3));
        /* ... under a header row that holds the pin (owner, 2026-09-22). */
        assert(approx(expanded.height, 8.0 * 2 + m.header_height + m.gutter / 2.0 + 64.0 * 2 + 8.0 * 1));
    }
    printf("  [PASS] Collapsed anchor expands into a correctly sized tile grid\n");

    /* Test 2: Tiles are laid out row by row, and hit testing finds them. */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 0.0, 0.0, 6);
        rubraview_box_hover_enter(&box);

        rubraview_rect_t t0 = rubraview_box_tile_rect(&box, &m, 0);
        rubraview_rect_t t1 = rubraview_box_tile_rect(&box, &m, 1);
        rubraview_rect_t t4 = rubraview_box_tile_rect(&box, &m, 4); /* second row, first column */

        /* The toolbox's buttons are half a menu tile wide, a quarter of its
           area, eight to a row (owner, 2026-09-22): tile 4 is still on the
           first row. */
        assert(approx(t0.width, m.tile_size / 2.0) && approx(t0.height, m.tile_size / 2.0));
        assert(approx(t1.x - t0.x, m.button_size + m.gutter / 2.0));
        assert(approx(t1.y, t0.y));
        assert(approx(t4.y, t0.y));
        assert(approx(t4.x - t0.x, 4.0 * (m.button_size + m.gutter / 2.0)));

        assert(rubraview_box_tile_at(&box, &m, t4.x + 1.0, t4.y + 1.0) == 4);
        assert(rubraview_box_tile_at(&box, &m, t0.x - 5.0, t0.y - 5.0) == -1); /* in the padding */
        assert(rubraview_box_tile_at(&box, &m, 5000.0, 5000.0) == -1);
    }
    printf("  [PASS] Tiles lay out row by row and hit testing resolves them\n");

    /* Test 2b: RFC-0002 §4 — with the view known, an open grid clears its
       anchor and stays inside: below it with room, above it by the bottom
       edge, and shifted left by the right edge. */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 100.0, 100.0, 10);
        box.view_width = 1280.0;
        box.view_height = 752.0;
        rubraview_box_hover_enter(&box);
        rubraview_rect_t anchor = rubraview_box_anchor_rect(&box, &m);
        rubraview_rect_t body = rubraview_box_bounds(&box, &m);
        assert(approx(body.x, 100.0) && approx(body.y, anchor.y + anchor.height + m.gutter));   /* below */

        box.anchor_x = 1060.0;
        box.anchor_y = 592.0;
        body = rubraview_box_bounds(&box, &m);
        assert(approx(body.y + body.height, box.anchor_y - m.gutter));                         /* above */
        assert(approx(body.x + body.width, 1280.0));                                            /* pulled in */
        rubraview_rect_t last = rubraview_box_tile_rect(&box, &m, 9);
        assert(last.x >= body.x && last.x + last.width <= 1280.0 && last.y + last.height <= box.anchor_y);
        assert(!rubraview_rect_contains(body, box.anchor_x + 1.0, box.anchor_y + 1.0));        /* the anchor stays clear */
        assert(rubraview_box_tile_at(&box, &m, last.x + 2.0, last.y + 2.0) == 9);
    }
    printf("  [PASS] An open grid clears its anchor and stays inside the view\n");

    /* Test 3: A collapsed box has no tiles to hit. */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 0.0, 0.0, 6);
        assert(rubraview_box_tile_at(&box, &m, 10.0, 10.0) == -1);
    }
    printf("  [PASS] A collapsed box exposes no tiles\n");

    /* Test 4: §3.6.2 — the Menu Box is clamped so its whole body stays
       inside the client area, however far it is dragged. */
    {
        rubraview_box_t menu = rubraview_box_create(RUBRAVIEW_BOX_MENU, 0.0, 0.0, 8);
        rubraview_box_hover_enter(&menu);
        rubraview_rect_t body = rubraview_box_bounds(&menu, &m);

        rubraview_box_drag_to(&menu, &m, 5000.0, 5000.0, 1000.0, 800.0);
        assert(approx(menu.anchor_x, 1000.0 - body.width));
        assert(approx(menu.anchor_y, 800.0 - body.height));

        rubraview_box_drag_to(&menu, &m, -500.0, -500.0, 1000.0, 800.0);
        assert(approx(menu.anchor_x, 0.0) && approx(menu.anchor_y, 0.0));
    }
    printf("  [PASS] Menu Box drag is clamped inside the client area on every side\n");

    /* Test 5: The Toolbox is deliberately not clamped, and detaches once
       dragged clear of the window (§3.6.1). */
    {
        rubraview_box_t toolbox = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 500.0, 400.0, 8);
        rubraview_box_hover_enter(&toolbox);

        rubraview_box_drag_to(&toolbox, &m, 1400.0, 400.0, 1000.0, 800.0);
        assert(approx(toolbox.anchor_x, 1400.0)); /* not clamped */
        assert(rubraview_box_update_detach(&toolbox, &m, 1000.0, 800.0, 20.0));
        assert(toolbox.state == RUBRAVIEW_BOX_DETACHED);

        /* Dragging it back over the canvas docks it again, still open. */
        assert(rubraview_box_dock(&toolbox));
        assert(toolbox.state == RUBRAVIEW_BOX_LOCKED_OPEN);
    }
    printf("  [PASS] Toolbox detaches when dragged clear and docks back on return\n");

    /* Test 6: The Menu Box never detaches, however far it is dragged. */
    {
        rubraview_box_t menu = rubraview_box_create(RUBRAVIEW_BOX_MENU, 0.0, 0.0, 8);
        rubraview_box_hover_enter(&menu);
        rubraview_box_drag_to(&menu, &m, 9000.0, 9000.0, 1000.0, 800.0);
        assert(!rubraview_box_update_detach(&menu, &m, 1000.0, 800.0, 20.0));
        assert(menu.state != RUBRAVIEW_BOX_DETACHED);
    }
    printf("  [PASS] Menu Box never detaches into its own window\n");

    /* Test 7: An unpinned hover-expanded box collapses after the grace
       period; a pinned one never does (§3.6.1, §3.6.3). */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 0.0, 0.0, 4);
        rubraview_box_hover_enter(&box);
        rubraview_box_hover_leave(&box);

        assert(!rubraview_box_tick(&box, 0.3, 0.5)); /* still within the grace period */
        assert(box.state == RUBRAVIEW_BOX_EXPANDED);
        assert(rubraview_box_tick(&box, 0.3, 0.5));  /* 0.6 s total: collapses */
        assert(box.state == RUBRAVIEW_BOX_COLLAPSED);

        rubraview_box_t pinned = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 0.0, 0.0, 4);
        rubraview_box_hover_enter(&pinned);
        pinned.pinned = true;
        rubraview_box_hover_leave(&pinned);
        assert(!rubraview_box_tick(&pinned, 10.0, 0.5));
        assert(pinned.state == RUBRAVIEW_BOX_EXPANDED);
    }
    printf("  [PASS] Idle collapse honours the grace period and the pin\n");

    /* Test 8: Click-to-lock holds the box open regardless of the timer,
       and Esc dismisses it (§3.6.3). */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_MENU, 0.0, 0.0, 4);
        rubraview_box_click_anchor(&box);
        assert(box.state == RUBRAVIEW_BOX_LOCKED_OPEN);
        assert(!rubraview_box_tick(&box, 100.0, 0.5));
        assert(box.state == RUBRAVIEW_BOX_LOCKED_OPEN);

        rubraview_box_click_anchor(&box); /* clicking again releases it */
        assert(box.state == RUBRAVIEW_BOX_COLLAPSED);

        rubraview_box_click_anchor(&box);
        rubraview_box_dismiss(&box);
        assert(box.state == RUBRAVIEW_BOX_COLLAPSED);
    }
    printf("  [PASS] Click-to-lock survives the idle timer; dismiss closes it\n");

    /* Test 9: Tile metrics scale with DPI, staying above the 48 px touch
       minimum (§3.6.4, §4.2). */
    {
        rubraview_tile_metrics_t hidpi = rubraview_tile_metrics_default(2.0);
        assert(approx(hidpi.tile_size, 128.0));
        assert(hidpi.tile_size >= 48.0);
        assert(approx(hidpi.gutter, 16.0));
    }
    printf("  [PASS] Tile metrics scale with DPI and stay touch-sized\n");
}

static void test_menu(void) {
    /* A small tree mirroring §3.6.2: two roots, one with children. */
    /* Automatic storage: U8() is a compound literal, not a constant
       initializer, so these cannot be static. They outlive every use
       within this function. */
    const rubraview_menu_item_t ITEMS[] = {
        /* 0 */ { .label = U8("Layout"),  .action = U8(""),             .first_child = 2, .child_count = 3 },
        /* 1 */ { .label = U8("Fit"),     .action = U8("fit_window"),   .first_child = -1, .child_count = 0 },
        /* 2 */ { .label = U8("Single"),  .action = U8("layout_single"),.first_child = -1, .child_count = 0 },
        /* 3 */ { .label = U8("Dual"),    .action = U8("layout_dual"),  .first_child = -1, .child_count = 0 },
        /* 4 */ { .label = U8("Book"),    .action = U8(""),             .first_child = 5, .child_count = 2 },
        /* 5 */ { .label = U8("LTR"),     .action = U8("book_ltr"),     .first_child = -1, .child_count = 0 },
        /* 6 */ { .label = U8("RTL"),     .action = U8("book_rtl"),     .first_child = -1, .child_count = 0 },
    };
    const rubraview_menu_tree_t TREE = {
        .items = ITEMS, .item_count = sizeof(ITEMS) / sizeof(ITEMS[0]),
        .root_first = 0, .root_count = 2,
    };

    /* Test 10: At the root there is no Back tile. */
    {
        rubraview_menu_state_t menu = rubraview_menu_create(&TREE);
        assert(!rubraview_menu_has_back_tile(&menu));
        assert(rubraview_menu_visible_count(&menu) == 2);
        assert(str_eq(rubraview_menu_item_at(&menu, 0)->label, "Layout"));
        assert(str_eq(rubraview_menu_item_at(&menu, 1)->label, "Fit"));
    }
    printf("  [PASS] The root level shows no Back tile\n");

    /* Test 11: Tapping a leaf reports its action; tapping a submenu
       descends and inserts the Back tile at position 0 (§3.6.2). */
    {
        rubraview_menu_state_t menu = rubraview_menu_create(&TREE);
        u8str_t action;

        assert(rubraview_menu_activate(&menu, 1, &action) == RUBRAVIEW_MENU_ACTIVATED);
        assert(str_eq(action, "fit_window"));
        assert(menu.depth == 0); /* a leaf does not change level */

        assert(rubraview_menu_activate(&menu, 0, &action) == RUBRAVIEW_MENU_DESCENDED);
        assert(menu.depth == 1);
        assert(rubraview_menu_has_back_tile(&menu));
        assert(rubraview_menu_visible_count(&menu) == 4); /* Back + 3 children */
        assert(rubraview_menu_item_at(&menu, 0) == NULL); /* tile 0 is Back */
        assert(str_eq(rubraview_menu_item_at(&menu, 1)->label, "Single"));
        assert(str_eq(rubraview_menu_item_at(&menu, 3)->label, "Book"));
    }
    printf("  [PASS] Leaves report actions; submenus descend and add a Back tile\n");

    /* Test 12: The Back tile returns one level; two levels deep works. */
    {
        rubraview_menu_state_t menu = rubraview_menu_create(&TREE);
        u8str_t action;
        rubraview_menu_activate(&menu, 0, &action); /* -> Layout */
        assert(rubraview_menu_activate(&menu, 3, &action) == RUBRAVIEW_MENU_DESCENDED); /* -> Book */
        assert(menu.depth == 2);
        assert(rubraview_menu_visible_count(&menu) == 3); /* Back + LTR + RTL */
        assert(str_eq(rubraview_menu_item_at(&menu, 2)->label, "RTL"));

        assert(rubraview_menu_activate(&menu, 2, &action) == RUBRAVIEW_MENU_ACTIVATED);
        assert(str_eq(action, "book_rtl"));

        assert(rubraview_menu_activate(&menu, 0, &action) == RUBRAVIEW_MENU_WENT_BACK);
        assert(menu.depth == 1);
        assert(rubraview_menu_activate(&menu, 0, &action) == RUBRAVIEW_MENU_WENT_BACK);
        assert(menu.depth == 0);
        assert(!rubraview_menu_back(&menu)); /* already at the root */
    }
    printf("  [PASS] Back tile walks up level by level and stops at the root\n");

    /* Test 13: The breadcrumb names the path (§3.6.2). */
    {
        rubraview_menu_state_t menu = rubraview_menu_create(&TREE);
        char buffer[128];
        u8str_t crumb = rubraview_menu_breadcrumb(&menu, buffer, sizeof(buffer));
        assert(str_eq(crumb, "Menu"));

        u8str_t action;
        rubraview_menu_activate(&menu, 0, &action); /* Layout */
        rubraview_menu_activate(&menu, 3, &action); /* Book */
        crumb = rubraview_menu_breadcrumb(&menu, buffer, sizeof(buffer));
        assert(str_eq(crumb, "Menu > Layout > Book"));
        assert(crumb.ptr[crumb.len] == '\0');
    }
    printf("  [PASS] Breadcrumb names the current path\n");

    /* Test 14: The breadcrumb never overruns a small buffer. */
    {
        rubraview_menu_state_t menu = rubraview_menu_create(&TREE);
        u8str_t action;
        rubraview_menu_activate(&menu, 0, &action);
        rubraview_menu_activate(&menu, 3, &action);

        char tiny[8];
        u8str_t crumb = rubraview_menu_breadcrumb(&menu, tiny, sizeof(tiny));
        assert(crumb.len < sizeof(tiny));
        assert(tiny[crumb.len] == '\0');
    }
    printf("  [PASS] Breadcrumb truncates safely into a small buffer\n");

    /* Test 15: Reset returns to the root, so reopening starts fresh. */
    {
        rubraview_menu_state_t menu = rubraview_menu_create(&TREE);
        u8str_t action;
        rubraview_menu_activate(&menu, 0, &action);
        rubraview_menu_reset(&menu);
        assert(menu.depth == 0);
        assert(!rubraview_menu_has_back_tile(&menu));
    }
    printf("  [PASS] Reset returns the menu to its root\n");
}

/* §3.6.1 pin and detach, as RFC-0002 Q6 builds them. */
static void test_pin_and_detach(void) {
    rubraview_tile_metrics_t m = rubraview_tile_metrics_default(1.0);
    rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 100.0, 100.0, 8);
    rubraview_box_set_pinned(&box, true);
    assert(box.state == RUBRAVIEW_BOX_LOCKED_OPEN && box.pinned);
    rubraview_box_dismiss(&box);
    assert(box.state == RUBRAVIEW_BOX_LOCKED_OPEN);                 /* Esc and outside clicks leave it */
    assert(!rubraview_box_tick(&box, 10.0, 0.5));
    rubraview_box_click_anchor(&box);
    assert(box.state == RUBRAVIEW_BOX_COLLAPSED && !box.pinned);    /* its own anchor closes it, pin and all */

    /* Open, and dragged by its anchor past the right edge, it detaches —
       even though the open grid itself is kept inside the view. */
    box.view_width = 1000.0;
    box.view_height = 800.0;
    rubraview_box_click_anchor(&box);
    rubraview_box_drag_to(&box, &m, 1100.0, 300.0, 1000.0, 800.0);
    assert(rubraview_box_bounds(&box, &m).x + rubraview_box_bounds(&box, &m).width <= 1000.0);
    assert(rubraview_box_update_detach(&box, &m, 1000.0, 800.0, 24.0));
    assert(box.state == RUBRAVIEW_BOX_DETACHED);
    rubraview_box_dismiss(&box);
    assert(box.state == RUBRAVIEW_BOX_DETACHED);
    assert(rubraview_box_dock(&box) && box.state == RUBRAVIEW_BOX_LOCKED_OPEN);
    printf("  [PASS] A pinned box stays; dragged out by its anchor the toolbox detaches, and docks back open\n");
}

/* D-15: Alt + wheel opacity — the 5 % grid, the 30 % floor, alpha only. */
static void test_opacity(void) {
    assert(rubraview_box_opacity_step(90.0, 1.0) == 95.0);
    assert(rubraview_box_opacity_step(95.0, 3.0) == 100.0);          /* stops at opaque */
    assert(rubraview_box_opacity_step(40.0, -1.0) == 35.0);
    assert(rubraview_box_opacity_step(35.0, -4.0) == 30.0);          /* never below the floor */
    assert(rubraview_box_opacity_step(88.0, 0.0) == 90.0);           /* snapped to the grid */
    assert(rubraview_box_opacity_step(0.0 / 0.0, -1.0) == 95.0);     /* nonsense reads as opaque */
    assert(rubraview_box_fade(0xE01A1A1Au, 100.0) == 0xE01A1A1Au);
    assert(rubraview_box_fade(0xE01A1A1Au, 50.0) == 0x701A1A1Au);
    assert(rubraview_box_fade(0xFF102030u, 10.0) == ((uint32_t)lround(255 * 0.30) << 24 | 0x102030u));   /* floor applies here too */
    printf("  [PASS] Box opacity steps 5 %% at a time and never below 30 %%\n");
}

/* What a tile says about its action: a submenu says so, a toggle says
   whether it is on, and what cannot do anything here is dimmed — the
   same rules for the menu box and the toolbox. */
static void test_just_opened(void) {
    rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_MENU, 0.0, 0.0, 4);
    bool was_open = false;
    assert(!rubraview_box_just_opened(&box, &was_open));      /* collapsed */
    rubraview_box_click_anchor(&box);
    assert(rubraview_box_just_opened(&box, &was_open));       /* opened: once */
    assert(!rubraview_box_just_opened(&box, &was_open));
    rubraview_box_click_anchor(&box);
    assert(!rubraview_box_just_opened(&box, &was_open));      /* closed */
    rubraview_box_click_anchor(&box);
    assert(rubraview_box_just_opened(&box, &was_open));       /* and again */
    assert(!rubraview_box_just_opened(NULL, &was_open));
    printf("  [PASS] Opening a box is noticed once, however often it is asked\n");
}

static void test_action_tiles(void) {
    char buf[64];
    assert(str_eq(rubraview_tile_caption(U8("File"), true, RUBRAVIEW_MARK_NONE, NULL, buf, sizeof(buf)), "File >"));
    assert(str_eq(rubraview_tile_caption(U8("Crisp"), false, RUBRAVIEW_MARK_ON, NULL, buf, sizeof(buf)), "Crisp: on"));
    assert(str_eq(rubraview_tile_caption(U8("Crisp"), false, RUBRAVIEW_MARK_OFF, NULL, buf, sizeof(buf)), "Crisp: off"));
    assert(str_eq(rubraview_tile_caption(U8("Single"), false, RUBRAVIEW_MARK_CURRENT, NULL, buf, sizeof(buf)), "Single"));
    assert(str_eq(rubraview_tile_caption(U8("Rotate"), false, RUBRAVIEW_MARK_NONE, NULL, buf, sizeof(buf)), "Rotate"));
    /* Too small a buffer gives the plain label rather than a cut one. */
    char tiny[4];
    assert(str_eq(rubraview_tile_caption(U8("File"), true, RUBRAVIEW_MARK_NONE, NULL, tiny, sizeof(tiny)), "File"));

    rubraview_action_facts_t f = { .has_page = true };
    rubraview_action_state_t st;
    /* A single picture: nothing to go to beyond it, no frames, no sound. */
    assert(!rubraview_action_state(U8("next_archive"), &f).enabled);
    assert(!rubraview_action_state(U8("prev_archive"), &f).enabled);
    assert(rubraview_action_state(U8("rotate_cw"), &f).enabled);
    assert(rubraview_action_state(U8("quick_export"), &f).enabled);
    f.archive_series = true;
    assert(rubraview_action_state(U8("next_archive"), &f).enabled);

    /* Sound only: no frames to step, nothing to export. */
    rubraview_action_facts_t music = { .has_page = true, .media = true };
    assert(!rubraview_action_state(U8("anim_step_forward"), &music).enabled);
    assert(!rubraview_action_state(U8("quick_export"), &music).enabled);
    assert(!rubraview_action_state(U8("next_audio_track"), &music).enabled);
    assert(!rubraview_action_state(U8("subtitle_later"), &music).enabled);
    music.other_audio_track = true;
    assert(rubraview_action_state(U8("next_audio_track"), &music).enabled);
    rubraview_action_facts_t film = { .has_page = true, .media = true, .video = true, .subtitle_shown = true };
    assert(rubraview_action_state(U8("anim_step_forward"), &film).enabled);
    assert(rubraview_action_state(U8("subtitle_later"), &film).enabled);
    assert(!rubraview_action_state(U8("next_subtitle_track"), &film).enabled);

    /* Nothing on screen: the view's own actions wait for a picture. */
    rubraview_action_facts_t empty = {0};
    assert(!rubraview_action_state(U8("fit_width"), &empty).enabled);
    assert(!rubraview_action_state(U8("rename_file"), &empty).enabled);
    assert(rubraview_action_state(U8("open_picker"), &empty).enabled);
    assert(rubraview_action_state(U8("quit"), &empty).enabled);
    assert(rubraview_action_state(U8("an_action_nobody_knows"), &empty).enabled);

    /* Toggles say on or off. */
    rubraview_action_facts_t t = { .has_page = true, .nearest = true, .always_on_top = false, .muted = true };
    assert(rubraview_action_state(U8("toggle_nearest"), &t).mark == RUBRAVIEW_MARK_ON);
    assert(rubraview_action_state(U8("toggle_pixel_grid"), &t).mark == RUBRAVIEW_MARK_OFF);
    assert(rubraview_action_state(U8("toggle_always_on_top"), &t).mark == RUBRAVIEW_MARK_OFF);
    assert(rubraview_action_state(U8("media_mute"), &t).mark == RUBRAVIEW_MARK_ON);
    assert(rubraview_action_state(U8("rotate_cw"), &t).mark == RUBRAVIEW_MARK_NONE);

    /* Of several, the one in use is marked. */
    t.layout = RUBRAVIEW_PAGE_LAYOUT_DUAL;
    t.fit = RUBRAVIEW_FIT_WIDTH;
    assert(rubraview_action_state(U8("layout_dual"), &t).mark == RUBRAVIEW_MARK_CURRENT);
    assert(rubraview_action_state(U8("layout_single"), &t).mark == RUBRAVIEW_MARK_NONE);
    assert(rubraview_action_state(U8("fit_width"), &t).mark == RUBRAVIEW_MARK_CURRENT);
    assert(rubraview_action_state(U8("actual_size"), &t).mark == RUBRAVIEW_MARK_NONE);
    t.fit = RUBRAVIEW_FIT_ACTUAL_SIZE;
    st = rubraview_action_state(U8("actual_size"), &t);
    assert(st.mark == RUBRAVIEW_MARK_CURRENT && st.enabled);
    /* The reading order says which way it runs (owner, 2026-09-21). */
    rubraview_action_facts_t o = { .has_page = true };
    st = rubraview_action_state(U8("toggle_reading_order"), &o);
    assert(st.value && strcmp(st.value, "L>R") == 0);
    o.rtl = true;
    st = rubraview_action_state(U8("toggle_reading_order"), &o);
    assert(st.value && strcmp(st.value, "R>L") == 0);
    assert(str_eq(rubraview_tile_caption(U8("Order"), false, st.mark, st.value, buf, sizeof(buf)), "Order: R>L"));
    assert(rubraview_action_state(U8("rotate_cw"), &o).value == NULL);
    printf("  [PASS] Tiles say submenu, on/off and the current choice; what cannot act here is dimmed\n");
}

/* A destructive menu item asks first: the first tap arms it, a second
   tap on the same item within the window acts (owner, 2026-09-21). */
static void test_confirm(void) {
    rubraview_confirm_t c = {0};
    assert(!rubraview_confirm_press(&c, 7, 10.0));            /* armed, not fired */
    assert(rubraview_confirm_armed(&c, 7, 11.0));
    assert(!rubraview_confirm_armed(&c, 8, 11.0));
    assert(rubraview_confirm_press(&c, 7, 12.0));             /* second tap: fires */
    assert(!rubraview_confirm_armed(&c, 7, 12.0));            /* and disarms */
    assert(!rubraview_confirm_press(&c, 7, 20.0));            /* arms again */
    assert(!rubraview_confirm_press(&c, 7, 20.0 + RUBRAVIEW_CONFIRM_SECONDS + 0.1)); /* too late: re-arms */
    assert(rubraview_confirm_press(&c, 7, 20.0 + RUBRAVIEW_CONFIRM_SECONDS + 1.0));
    assert(!rubraview_confirm_press(&c, 3, 30.0));
    assert(!rubraview_confirm_press(&c, 4, 30.5));            /* another item takes the arm over */
    assert(!rubraview_confirm_armed(&c, 3, 30.5));
    rubraview_confirm_clear(&c);
    assert(!rubraview_confirm_armed(&c, 4, 30.6));
    printf("  [PASS] A destructive item fires on the second tap only, and only soon after the first\n");
}

/* The toolbox is a strip (owner, 2026-09-22): a pin and the seek bar on
   top, the file's name under them, then small buttons, eight a row. */
static void test_toolbox_strip(void) {
    rubraview_tile_metrics_t m = rubraview_tile_metrics_default(1.0);
    assert(approx(m.button_size, 32.0) && approx(m.button_size * m.button_size * 4.0, m.tile_size * m.tile_size));
    rubraview_toolbox_layout_t l = rubraview_toolbox_layout(&m, 14, true);
    assert(l.columns == 8 && l.rows == 2);
    assert(approx(l.width, 2.0 * m.padding + 8.0 * 32.0 + 7.0 * 4.0));
    assert(approx(l.pin.x, m.padding) && approx(l.pin.y, m.padding) && approx(l.pin.width, m.header_height));
    assert(l.timeline.width > 0.0 && l.timeline.x > l.pin.x + l.pin.width);                 /* beside the pin */
    assert(approx(l.timeline.x + l.timeline.width, l.width - m.padding));
    assert(l.title.y >= l.pin.y + l.pin.height && approx(l.title.width, l.width - 2.0 * m.padding)); /* under it */
    rubraview_rect_t b0 = rubraview_toolbox_button_rect(&l, &m, 0), b8 = rubraview_toolbox_button_rect(&l, &m, 8);
    assert(b0.y >= l.title.y + l.title.height);                                              /* buttons below the name */
    assert(approx(b8.x, b0.x) && approx(b8.y - b0.y, 32.0 + 4.0));                             /* second row */
    assert(approx(l.height, b8.y + 32.0 + m.padding));
    /* No film: no seek bar, the rest as before. */
    rubraview_toolbox_layout_t still = rubraview_toolbox_layout(&m, 8, false);
    assert(still.timeline.width == 0.0 && still.rows == 1);

    /* The box uses it, and keeps it inside the window like any box. */
    rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 1060.0, 700.0, 14);
    box.timeline = true;
    box.view_width = 1280.0;
    box.view_height = 752.0;
    rubraview_box_hover_enter(&box);
    rubraview_rect_t body = rubraview_box_bounds(&box, &m);
    assert(approx(body.width, l.width) && approx(body.height, l.height));
    assert(body.x >= 0.0 && body.x + body.width <= 1280.0 && body.y >= 0.0 && body.y + body.height <= 752.0);
    rubraview_rect_t tl = rubraview_box_timeline_rect(&box, &m);
    assert(approx(tl.x, body.x + l.timeline.x) && approx(tl.y, body.y + l.timeline.y));
    assert(rubraview_box_tile_at(&box, &m, body.x + b8.x + 2.0, body.y + b8.y + 2.0) == 8);
    printf("  [PASS] The toolbox is a strip: pin and seek bar, the name, then small buttons in rows\n");
}

/* Each box has a pin at its top left (owner, 2026-09-22): pinned, the
   box stays open when the pointer leaves; unpinned, it folds again. */
static void test_box_pin(void) {
    rubraview_tile_metrics_t m = rubraview_tile_metrics_default(1.0);
    rubraview_box_kind_t kinds[2] = { RUBRAVIEW_BOX_MENU, RUBRAVIEW_BOX_TOOLBOX };
    for (int k = 0; k < 2; ++k) {
        rubraview_box_t box = rubraview_box_create(kinds[k], 100.0, 100.0, 6);
        assert(rubraview_box_pin_rect(&box, &m).width == 0.0);        /* collapsed: no pin */
        rubraview_box_hover_enter(&box);
        rubraview_rect_t body = rubraview_box_bounds(&box, &m);
        rubraview_rect_t pin = rubraview_box_pin_rect(&box, &m);
        assert(approx(pin.x, body.x + m.padding) && approx(pin.y, body.y + m.padding));
        assert(!rubraview_box_pin_shown_on(&box));

        assert(rubraview_box_pin_click(&box, &m, pin.x + 2.0, pin.y + 2.0));
        assert(rubraview_box_pin_shown_on(&box) && box.pinned);
        rubraview_box_pointer(&box, &m, 3000.0, 3000.0);
        assert(!rubraview_box_tick(&box, 5.0, 0.5));                    /* stays when the pointer leaves */

        assert(rubraview_box_pin_click(&box, &m, pin.x + 2.0, pin.y + 2.0));
        assert(!rubraview_box_pin_shown_on(&box) && box.state == RUBRAVIEW_BOX_EXPANDED);
        assert(rubraview_box_tick(&box, 5.0, 0.5));                     /* unpinned: folds */
        assert(!rubraview_box_pin_click(&box, &m, pin.x + 2.0, pin.y + 2.0));   /* folded: nothing there */

        /* Opened by the left half's click, it shows pinned, and the pin undoes it. */
        rubraview_box_click_anchor(&box);
        assert(rubraview_box_pin_shown_on(&box));
        pin = rubraview_box_pin_rect(&box, &m);
        assert(rubraview_box_pin_click(&box, &m, pin.x + 1.0, pin.y + 1.0));
        assert(box.state == RUBRAVIEW_BOX_EXPANDED && !box.pinned);
    }
    printf("  [PASS] Each box has a pin: on, it stays open; off, it folds when left\n");
}

/* Toolbox buttons draw an icon (owner, 2026-09-22), from the Segoe MDL2
   Assets font; the ones that change with state say what a tap does. An
   action with no icon returns 0 and keeps its short caption. */
static void test_action_icons(void) {
    rubraview_action_facts_t f = { .has_page = true, .media = true, .video = true, .playing = true };
    assert(rubraview_action_icon(U8("media_play_pause"), &f) == 0xE769);   /* playing: the pause sign */
    f.playing = false;
    assert(rubraview_action_icon(U8("media_play_pause"), &f) == 0xE768);   /* paused: the play sign */
    assert(rubraview_action_icon(U8("media_stop"), &f) == 0xE71A);
    assert(rubraview_action_icon(U8("prev_page"), &f) == 0xE892 && rubraview_action_icon(U8("next_page"), &f) == 0xE893);
    assert(rubraview_action_icon(U8("media_seek_back"), &f) == 0xEB9E && rubraview_action_icon(U8("media_seek_forward"), &f) == 0xEB9D);
    assert(rubraview_action_icon(U8("media_mute"), &f) == 0xE74F);         /* sound on: tap mutes */
    f.muted = true;
    assert(rubraview_action_icon(U8("media_mute"), &f) == 0xE767);         /* muted: tap brings it back */
    assert(rubraview_action_icon(U8("toggle_fullscreen"), &f) == 0xE740);
    f.fullscreen = true;
    assert(rubraview_action_icon(U8("toggle_fullscreen"), &f) == 0xE73F);
    assert(rubraview_action_icon(U8("media_speed_cycle"), &f) == 0);       /* "1x" says more than an icon */
    assert(rubraview_action_icon(U8("no_such_action"), &f) == 0);
    assert(rubraview_pin_icon(false) == 0xE718 && rubraview_pin_icon(true) == 0xE840);
    printf("  [PASS] Toolbox buttons have icons that follow the state; captions stay where there is none\n");
}

int main(void) {
    printf("[test_ui_box] Starting floating box and menu hierarchy unit tests...\n");
    test_boxes();
    test_two_part_anchor();
    test_menu();
    test_opacity();
    test_pin_and_detach();
    test_action_tiles();
    test_just_opened();
    test_confirm();
    test_toolbox_strip();
    test_box_pin();
    test_action_icons();
    printf("[test_ui_box] All tests passed successfully!\n");
    return 0;
}
