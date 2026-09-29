#include "rubraview/ui_edgenav.h"

rubraview_edgenav_t rubraview_edgenav_create(double dpi_scale) {
    if (dpi_scale <= 0.0) dpi_scale = 1.0;
    return (rubraview_edgenav_t){
        .button = 40.0 * dpi_scale,
        .gap = 8.0 * dpi_scale,
        .reach = 96.0 * dpi_scale,
    };
}

static double stack_top(const rubraview_edgenav_t *nav, double view_h) {
    double height = RUBRAVIEW_EDGE_SLOTS * nav->button + (RUBRAVIEW_EDGE_SLOTS - 1) * nav->gap;
    return (view_h - height) * 0.5;
}

/* Both stacks, the reach on each side, and a picture's worth between them. */
static bool room_for(const rubraview_edgenav_t *nav, double view_w, double view_h) {
    double height = RUBRAVIEW_EDGE_SLOTS * nav->button + (RUBRAVIEW_EDGE_SLOTS - 1) * nav->gap;
    return view_w >= 2.0 * nav->reach + nav->button && view_h >= height + 2.0 * nav->gap;
}

rubraview_rect_t rubraview_edgenav_rect(const rubraview_edgenav_t *nav, rubraview_edge_side_t side, int32_t slot,
                                        double view_w, double view_h) {
    if (!nav || side == RUBRAVIEW_EDGE_NONE || slot < 0 || slot >= RUBRAVIEW_EDGE_SLOTS) return (rubraview_rect_t){0};
    double x = side == RUBRAVIEW_EDGE_LEFT ? nav->gap : view_w - nav->gap - nav->button;
    double y = stack_top(nav, view_h) + slot * (nav->button + nav->gap);
    return (rubraview_rect_t){ x, y, nav->button, nav->button };
}

rubraview_edge_side_t rubraview_edgenav_revealed(const rubraview_edgenav_t *nav, double px, double py,
                                                 double view_w, double view_h) {
    if (!nav || !room_for(nav, view_w, view_h)) return RUBRAVIEW_EDGE_NONE;
    double top = stack_top(nav, view_h) - nav->button;
    double bottom = view_h - stack_top(nav, view_h) + nav->button;
    if (py < top || py >= bottom) return RUBRAVIEW_EDGE_NONE;
    if (px >= 0.0 && px < nav->reach) return RUBRAVIEW_EDGE_LEFT;
    if (px < view_w && px >= view_w - nav->reach) return RUBRAVIEW_EDGE_RIGHT;
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
    { { "prev_page", "skip_backward", "first_page" },
      { "next_page", "skip_forward", "last_page" } },
    { { "media_seek_back", "media_seek_back_long", "prev_page" },
      { "media_seek_forward", "media_seek_forward_long", "next_page" } },
};
static const char *const CAPTIONS[2][2][RUBRAVIEW_EDGE_SLOTS] = {
    { { "Previous", "10 back", "First" },
      { "Next", "10 on", "Last" } },
    { { "Back 5 s", "Back 30 s", "Previous file" },
      { "On 5 s", "On 30 s", "Next file" } },
};

const char *rubraview_edgenav_action(rubraview_edge_side_t side, int32_t slot, bool media) {
    if (side == RUBRAVIEW_EDGE_NONE || slot < 0 || slot >= RUBRAVIEW_EDGE_SLOTS) return "";
    return ACTIONS[media ? 1 : 0][side][slot];
}

const char *rubraview_edgenav_caption(rubraview_edge_side_t side, int32_t slot, bool media) {
    if (side == RUBRAVIEW_EDGE_NONE || slot < 0 || slot >= RUBRAVIEW_EDGE_SLOTS) return "";
    return CAPTIONS[media ? 1 : 0][side][slot];
}
