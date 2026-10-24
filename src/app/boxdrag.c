/*
 * A floating box pressed, dragged and let go (§3.6; RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* §3.6: dragging a floating box. The press is held: travel more than a
   few pixels and it was a drag, otherwise it was the click the anchor
   would have done anyway. */
#define BOX_DRAG_SLOP 4.0

bool box_press(app_state_t *app, double x, double y) {
    rubraview_tile_metrics_t metrics =
        tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_t *boxes[2] = { &app->menubox, &app->toolbox };
    for (size_t i = 0; i < 2; ++i) {
        if (boxes[i]->state == RUBRAVIEW_BOX_DETACHED) continue;
        /* The grip beside the pin sizes the box (owner, 2026-09-29). */
        if (rubraview_box_grip_begin(boxes[i], &metrics, x, y)) {
            app->box_sizing = boxes[i];
            return true;
        }
        /* Either half is a handle (owner, 2026-10-15; the right one only
           hovered before): a press without a drag is the half's own click. */
        rubraview_anchor_half_t half = rubraview_box_anchor_half_at(boxes[i], &metrics, x, y);
        if (half == RUBRAVIEW_ANCHOR_NONE) continue;
        if (boxes[i] == &app->toolbox && half == RUBRAVIEW_ANCHOR_HOVER) {
            /* 1.25.0 (owner, 2026-10-20): T held and dragged — up and down the files, sideways the folders or
               the archives beside this one, a wider step; F still moves the Floating Widget */
            app->nav_drag = true;
            app->box_press_x = x;
            app->box_press_y = y;
            app->nav_done_x = app->nav_done_y = 0;
            return true;
        }
        app->box_drag = boxes[i];
        app->box_grab_dx = x - boxes[i]->anchor_x;
        app->box_grab_dy = y - boxes[i]->anchor_y;
        app->box_press_x = x;
        app->box_press_y = y;
        app->box_drag_moved = false;
        return true;
    }
    return false;
}

void box_drag_motion(app_state_t *app, double x, double y) {
    if (app->nav_drag) {
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        int32_t sy = (int32_t)((y - app->box_press_y) / (36.0 * dpi)), sx = (int32_t)((x - app->box_press_x) / (110.0 * dpi));
        if (sx != app->nav_done_x) {   /* sideways first: a folder or an archive is the bigger step */
            bool forward = sx > app->nav_done_x;
            app->nav_done_x += forward ? 1 : -1;
            app->nav_done_y = sy;      /* what the hand did up or down while going sideways is not a file step */
            bool opened = app->source.archive_path.size > 0 ? open_sibling_archive(app, forward) : open_sibling_folder(app, forward);
            if (!opened) osd_say(app, forward ? TU8("nothing after this one") : TU8("nothing before this one"));
            return;
        }
        while (sy != app->nav_done_y) {
            bool forward = sy > app->nav_done_y;
            app->nav_done_y += forward ? 1 : -1;
            handle_action(app, forward ? PROVEN_LIT("next_page") : PROVEN_LIT("prev_page"));
        }
        return;
    }
    if (app->box_sizing) {
        rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
        (void)rubraview_box_grip_drag(app->box_sizing, &metrics, x, y);
        return;
    }
    if (!app->box_drag) return;
    if (!app->box_drag_moved) {
        double dx = x - app->box_press_x, dy = y - app->box_press_y;
        if (dx * dx + dy * dy < BOX_DRAG_SLOP * BOX_DRAG_SLOP) return;
        app->box_drag_moved = true;
    }
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    rubraview_tile_metrics_t metrics =
        tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_drag_to(app->box_drag, &metrics, x - app->box_grab_dx, y - app->box_grab_dy,
                          (double)win_w, (double)win_h);
    app->box_drag->open_side = 0;   /* 1.25.0: the open toolbar takes the side with room as the widget moves, live */
    /* §3.6.1 / RFC-0002 Q6: the toolbox dragged past the edge becomes a
       window of its own, and the drag carries on moving that window. */
    if (app->box_drag == &app->toolbox &&
        rubraview_box_update_detach(&app->toolbox, &metrics, (double)win_w, (double)win_h, 24.0 * rubraview_pal_window_dpi_scale(app->window))) {
        int32_t fx = 0, fy = 0, fw = 0, fh = 0;
        rubraview_pal_window_get_frame(app->window, &fx, &fy, &fw, &fh);
        app->box_drag = NULL;
        toolbox_detach(app, fx + (int32_t)app->toolbox.anchor_x, fy + (int32_t)app->toolbox.anchor_y, true);
    }
}

void box_release(app_state_t *app, double x, double y) {
    if (app->nav_drag) { app->nav_drag = false; return; }   /* T: a press without a drag only hovers */
    if (app->box_sizing) {
        /* Kept in settings.ini, as the boxes' opacity is. */
        rubraview_box_t *box = app->box_sizing;
        app->box_sizing = NULL;
        rubraview_box_grip_end(box);
        bool menu = box == &app->menubox;
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), menu ? PROVEN_LIT("menubox_columns") : PROVEN_LIT("toolbox_columns"), (double)box->columns);
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), menu ? PROVEN_LIT("menubox_size") : PROVEN_LIT("toolbox_size"), box->scale * 100.0);
        return;
    }
    if (!app->box_drag) return;
    rubraview_box_t *box = app->box_drag;
    app->box_drag = NULL;
    if (app->box_drag_moved) return;   /* a drag: the box stays where it was let go */
    rubraview_tile_metrics_t metrics =
        tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    if (box == &app->toolbox && rubraview_box_anchor_half_at(box, &metrics, x, y) == RUBRAVIEW_ANCHOR_CLICK) {
        rubraview_box_click_open(box);   /* 1.25.0: F opens and folds it, never pins it */
        return;
    }
    if (rubraview_box_click(box, &metrics, x, y) && box == &app->menubox) sync_menubox_tiles(app);
}
