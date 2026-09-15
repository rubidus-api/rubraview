#include "rubraview/ui_box.h"
#include "rubraview/ui_menu.h"
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
        assert(box.state == RUBRAVIEW_BOX_LOCKED_OPEN);

        rubraview_box_pointer(&box, &m, 900.0, 900.0);
        assert(!rubraview_box_tick(&box, 5.0, 0.5));
        assert(box.state == RUBRAVIEW_BOX_LOCKED_OPEN);
    }
    printf("  [PASS] Clicking the hover half keeps what hovering opened\n");

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
        assert(approx(expanded.height, 8.0 * 2 + 64.0 * 2 + 8.0 * 1));
    }
    printf("  [PASS] Collapsed anchor expands into a correctly sized tile grid\n");

    /* Test 2: Tiles are laid out row by row, and hit testing finds them. */
    {
        rubraview_box_t box = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, 0.0, 0.0, 6);
        rubraview_box_hover_enter(&box);

        rubraview_rect_t t0 = rubraview_box_tile_rect(&box, &m, 0);
        rubraview_rect_t t1 = rubraview_box_tile_rect(&box, &m, 1);
        rubraview_rect_t t4 = rubraview_box_tile_rect(&box, &m, 4); /* second row, first column */

        assert(approx(t1.x - t0.x, m.tile_size + m.gutter));
        assert(approx(t1.y, t0.y));
        assert(approx(t4.x, t0.x));
        assert(approx(t4.y - t0.y, m.tile_size + m.gutter));

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

int main(void) {
    printf("[test_ui_box] Starting floating box and menu hierarchy unit tests...\n");
    test_boxes();
    test_two_part_anchor();
    test_menu();
    test_opacity();
    printf("[test_ui_box] All tests passed successfully!\n");
    return 0;
}
