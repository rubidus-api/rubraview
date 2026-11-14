#include "rubraview/ui_edgenav.h"
#include "rubraview/lang.h"

rubraview_edgenav_t rubraview_edgenav_create(double dpi_scale) {
    if (dpi_scale <= 0.0) dpi_scale = 1.0;
    return (rubraview_edgenav_t){
        .button = 64.0 * dpi_scale,
        .gap = 8.0 * dpi_scale,
        .reach = 80.0 * dpi_scale,   /* RV-285: the least; 10 % of the width when more */
    };
}

/* RV-206: the column: seven tenths of the height, at least four buttons as tall as they are wide, centred. */
static double column_height(const rubraview_edgenav_t *nav, double view_h) {
    double least = RUBRAVIEW_EDGE_SLOTS * nav->button * 0.75 + (RUBRAVIEW_EDGE_SLOTS - 1) * nav->gap;
    double want = view_h * 0.7;
    return want > least ? want : least;
}

static double stack_top(const rubraview_edgenav_t *nav, double view_h) {
    return (view_h - column_height(nav, view_h)) * 0.5;
}

static double slot_height(const rubraview_edgenav_t *nav, double view_h) {
    return (column_height(nav, view_h) - (RUBRAVIEW_EDGE_SLOTS - 1) * nav->gap) / RUBRAVIEW_EDGE_SLOTS;
}

double rubraview_edgenav_reach(const rubraview_edgenav_t *nav, double view_w) {
    if (!nav) return 0.0;
    double share = view_w * RUBRAVIEW_EDGE_SIDE_SHARE;
    return share > nav->reach ? share : nav->reach;
}

/* Both columns, the reach on each side, and a picture's worth between them. */
static bool room_for(const rubraview_edgenav_t *nav, double view_w, double view_h) {
    return view_w >= 2.0 * rubraview_edgenav_reach(nav, view_w) + nav->button && view_h >= column_height(nav, view_h) + 2.0 * nav->gap;
}

rubraview_rect_t rubraview_edgenav_rect(const rubraview_edgenav_t *nav, rubraview_edge_side_t side, int32_t slot,
                                        double view_w, double view_h) {
    if (!nav || side == RUBRAVIEW_EDGE_NONE || slot < 0 || slot >= RUBRAVIEW_EDGE_SLOTS) return (rubraview_rect_t){0};
    double x = side == RUBRAVIEW_EDGE_LEFT ? nav->gap : view_w - nav->gap - nav->button;
    double sh = slot_height(nav, view_h);
    double y = stack_top(nav, view_h) + slot * (sh + nav->gap);
    return (rubraview_rect_t){ x, y, nav->button, sh };
}

rubraview_edge_side_t rubraview_edgenav_revealed(const rubraview_edgenav_t *nav, double px, double py,
                                                 double view_w, double view_h) {
    if (!nav || !room_for(nav, view_w, view_h)) return RUBRAVIEW_EDGE_NONE;
    double top = stack_top(nav, view_h) - nav->gap * 2.0;   /* RV-206: the column's height, a little more */
    double bottom = view_h - stack_top(nav, view_h) + nav->gap * 2.0;
    if (py < top || py >= bottom) return RUBRAVIEW_EDGE_NONE;
    double reach = rubraview_edgenav_reach(nav, view_w);
    if (px >= 0.0 && px < reach) return RUBRAVIEW_EDGE_LEFT;
    if (px < view_w && px >= view_w - reach) return RUBRAVIEW_EDGE_RIGHT;
    return RUBRAVIEW_EDGE_NONE;
}

rubraview_edge_hit_t rubraview_edgenav_hit(const rubraview_edgenav_t *nav, double px, double py,
                                           double view_w, double view_h) {
    rubraview_edge_hit_t none = { RUBRAVIEW_EDGE_NONE, -1 };
    rubraview_edge_side_t side = rubraview_edgenav_revealed(nav, px, py, view_w, view_h);
    if (side == RUBRAVIEW_EDGE_NONE) return none;
    for (int32_t slot = 0; slot < RUBRAVIEW_EDGE_SLOTS; ++slot) {
        if (rubraview_rect_contains(rubraview_edgenav_rect(nav, side, slot, view_w, view_h), px, py)) {
            return (rubraview_edge_hit_t){ side, slot };
        }
    }
    return none;
}

/* [media][side][slot] */
static const char *const ACTIONS[2][2][RUBRAVIEW_EDGE_SLOTS] = {
    { { "prev_page", "skip_backward", "first_page", "prev_archive" },
      { "next_page", "skip_forward", "last_page", "next_archive" } },
    { { "media_seek_back", "media_seek_back_percent", "prev_page", "prev_archive" },   /* RV-206: 5 % */
      { "media_seek_forward", "media_seek_forward_percent", "next_page", "next_archive" } },
};
static const char *const CAPTIONS[2][2][RUBRAVIEW_EDGE_SLOTS] = {
    { { N_("Page back"), N_("10 back"), N_("First"), N_("Folder or archive before") },   /* RV-285: a page turn */
      { N_("Page on"), N_("10 on"), N_("Last"), N_("Folder or archive after") } },
    { { N_("Back 5 s"), N_("Back 5 %"), N_("Previous file"), N_("Folder or archive before") },
      { N_("On 5 s"), N_("On 5 %"), N_("Next file"), N_("Folder or archive after") } },
};

const char *rubraview_edgenav_plain_action(rubraview_edge_side_t side, bool media) {
    if (side == RUBRAVIEW_EDGE_LEFT) return media ? "media_seek_back" : "prev_page";
    if (side == RUBRAVIEW_EDGE_RIGHT) return media ? "media_seek_forward" : "next_page";
    return "";
}

bool rubraview_edge_press_repeats(rubraview_edge_side_t last_side, double last_at, rubraview_edge_side_t side, double now) {
    return side != RUBRAVIEW_EDGE_NONE && side == last_side && now >= last_at && now - last_at < RUBRAVIEW_EDGE_REPEAT;
}

void rubraview_edge_press_begin(rubraview_edge_press_t *press, rubraview_edge_side_t side, bool column, double x, double y) {
    if (!press) return;
    *press = (rubraview_edge_press_t){ .on = side != RUBRAVIEW_EDGE_NONE, .side = side, .column = column, .x0 = x, .y0 = y };
}

void rubraview_edge_press_move(rubraview_edge_press_t *press, double x, double y, double slop) {
    if (!press || !press->on || press->dragged) return;
    double dx = x - press->x0, dy = y - press->y0;
    if (dx * dx + dy * dy > slop * slop) press->dragged = true;
}

bool rubraview_edge_press_shows(const rubraview_edge_press_t *press) {
    return press && press->on && (press->column || press->dragged);
}

int32_t rubraview_edge_press_slot(const rubraview_edge_press_t *press, const rubraview_edgenav_t *nav, double x, double y,
                                  double view_w, double view_h) {
    if (!press || !nav || !press->on) return -1;
    for (int32_t slot = 0; slot < RUBRAVIEW_EDGE_SLOTS; ++slot) {
        rubraview_rect_t r = rubraview_edgenav_rect(nav, press->side, slot, view_w, view_h);
        /* the whole side's width counts, not only the drawn button: a finger is wide */
        double left = press->side == RUBRAVIEW_EDGE_LEFT ? 0.0 : view_w - rubraview_edgenav_reach(nav, view_w);
        rubraview_rect_t lane = { left, r.y, rubraview_edgenav_reach(nav, view_w), r.height };
        if (rubraview_rect_contains(lane, x, y)) return slot;
    }
    return -1;
}

const char *rubraview_edge_press_release(rubraview_edge_press_t *press, const rubraview_edgenav_t *nav, double x, double y,
                                         double view_w, double view_h, bool media) {
    if (!press || !press->on) return NULL;
    const char *action = "";
    if (!press->column && !press->dragged) action = rubraview_edgenav_plain_action(press->side, media);
    else {
        int32_t slot = rubraview_edge_press_slot(press, nav, x, y, view_w, view_h);
        if (slot >= 0) action = rubraview_edgenav_action(press->side, slot, media);
    }
    press->on = false;
    return action;
}

const char *rubraview_edgenav_action(rubraview_edge_side_t side, int32_t slot, bool media) {
    if (side == RUBRAVIEW_EDGE_NONE || slot < 0 || slot >= RUBRAVIEW_EDGE_SLOTS) return "";
    return ACTIONS[media ? 1 : 0][side][slot];
}

const char *rubraview_edgenav_caption(rubraview_edge_side_t side, int32_t slot, bool media) {
    if (side == RUBRAVIEW_EDGE_NONE || slot < 0 || slot >= RUBRAVIEW_EDGE_SLOTS) return "";
    return rubraview_tr(CAPTIONS[media ? 1 : 0][side][slot]);
}
