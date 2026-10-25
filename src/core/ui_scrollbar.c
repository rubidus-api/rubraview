#include "rubraview/ui_scrollbar.h"

bool rubraview_scrollbar_needed(const rubraview_scrollbar_t *sb) {
    return sb->shown > 0.0 && sb->total > sb->shown && sb->area.width > 0.0 &&
           sb->area.height > 2.0 * sb->button;
}

double rubraview_scrollbar_max_first(const rubraview_scrollbar_t *sb) {
    return sb->total > sb->shown ? sb->total - sb->shown : 0.0;
}

double rubraview_scrollbar_clamp(const rubraview_scrollbar_t *sb, double first) {
    double max = rubraview_scrollbar_max_first(sb);
    return first < 0.0 || first != first ? 0.0 : first > max ? max : first;
}

rubraview_rect_t rubraview_scrollbar_track(const rubraview_scrollbar_t *sb) {
    double h = sb->area.height - 2.0 * sb->button;
    return (rubraview_rect_t){ sb->area.x, sb->area.y, sb->area.width, h > 0.0 ? h : 0.0 };
}

rubraview_rect_t rubraview_scrollbar_up(const rubraview_scrollbar_t *sb) {
    return (rubraview_rect_t){ sb->area.x, sb->area.y + sb->area.height - 2.0 * sb->button, sb->area.width, sb->button };
}

rubraview_rect_t rubraview_scrollbar_down(const rubraview_scrollbar_t *sb) {
    return (rubraview_rect_t){ sb->area.x, sb->area.y + sb->area.height - sb->button, sb->area.width, sb->button };
}

/* The thumb's height and the room it travels in. */
static double thumb_height(const rubraview_scrollbar_t *sb, double track_h) {
    double h = sb->total > 0.0 ? track_h * sb->shown / sb->total : track_h;
    if (h < sb->min_thumb) h = sb->min_thumb;
    return h > track_h ? track_h : h;
}

rubraview_rect_t rubraview_scrollbar_thumb(const rubraview_scrollbar_t *sb) {
    rubraview_rect_t t = rubraview_scrollbar_track(sb);
    double h = thumb_height(sb, t.height), max = rubraview_scrollbar_max_first(sb);
    double at = max > 0.0 ? rubraview_scrollbar_clamp(sb, sb->first) / max : 0.0;
    return (rubraview_rect_t){ t.x, t.y + (t.height - h) * at, t.width, h };
}

rubraview_scrollbar_part_t rubraview_scrollbar_hit(const rubraview_scrollbar_t *sb, double x, double y) {
    if (!rubraview_scrollbar_needed(sb) || !rubraview_rect_contains(sb->area, x, y)) return RUBRAVIEW_SCROLLBAR_NONE;
    if (rubraview_rect_contains(rubraview_scrollbar_down(sb), x, y)) return RUBRAVIEW_SCROLLBAR_DOWN;
    if (rubraview_rect_contains(rubraview_scrollbar_up(sb), x, y)) return RUBRAVIEW_SCROLLBAR_UP;
    rubraview_rect_t th = rubraview_scrollbar_thumb(sb);
    if (y < th.y) return RUBRAVIEW_SCROLLBAR_BEFORE;
    if (y >= th.y + th.height) return RUBRAVIEW_SCROLLBAR_AFTER;
    return RUBRAVIEW_SCROLLBAR_THUMB;
}

double rubraview_scrollbar_first_for_top(const rubraview_scrollbar_t *sb, double top) {
    rubraview_rect_t t = rubraview_scrollbar_track(sb);
    double room = t.height - thumb_height(sb, t.height);
    if (room <= 0.0) return 0.0;
    return rubraview_scrollbar_clamp(sb, (top - t.y) / room * rubraview_scrollbar_max_first(sb));
}

/* One move of a held part. */
static double move(const rubraview_scrollbar_t *sb, rubraview_scrollbar_part_t part) {
    double by = part == RUBRAVIEW_SCROLLBAR_BEFORE ? -sb->shown : part == RUBRAVIEW_SCROLLBAR_AFTER ? sb->shown
              : part == RUBRAVIEW_SCROLLBAR_UP ? -sb->step : part == RUBRAVIEW_SCROLLBAR_DOWN ? sb->step : 0.0;
    return rubraview_scrollbar_clamp(sb, sb->first + by);
}

double rubraview_scrollbar_press(const rubraview_scrollbar_t *sb, rubraview_scrollbar_hold_t *hold,
                                 double x, double y, double now) {
    *hold = (rubraview_scrollbar_hold_t){ .part = rubraview_scrollbar_hit(sb, x, y) };
    if (hold->part == RUBRAVIEW_SCROLLBAR_NONE) return sb->first;
    if (hold->part == RUBRAVIEW_SCROLLBAR_THUMB) {
        hold->grab = y - rubraview_scrollbar_thumb(sb).y;
        return sb->first;
    }
    hold->next = now + RUBRAVIEW_SCROLLBAR_DELAY;
    return move(sb, hold->part);
}

double rubraview_scrollbar_held(const rubraview_scrollbar_t *sb, rubraview_scrollbar_hold_t *hold,
                                double x, double y, double now) {
    if (hold->part == RUBRAVIEW_SCROLLBAR_NONE) return sb->first;
    if (hold->part == RUBRAVIEW_SCROLLBAR_THUMB) return rubraview_scrollbar_first_for_top(sb, y - hold->grab);
    if (now < hold->next) return sb->first;
    hold->next = now + RUBRAVIEW_SCROLLBAR_REPEAT;
    return rubraview_scrollbar_hit(sb, x, y) == hold->part ? move(sb, hold->part) : sb->first;
}
