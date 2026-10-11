/*
 * The subtitle box and the Sub tile's taps (D-33). See subbox.h.
 */
#include "rubraview/subbox.h"

static double clampd(double v, double lo, double hi) {
    if (hi < lo) hi = lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

rubraview_rect_t rubraview_subbox_rect(const rubraview_subbox_t *box, double win_w, double win_h,
                                       double height, rubraview_rect_t default_rect) {
    double x = default_rect.x, w = default_rect.width;
    double bottom = default_rect.y + default_rect.height;
    if (box && box->placed && win_w > 0.0 && win_h > 0.0) {
        x = box->left * win_w;
        w = box->width * win_w;
        bottom = box->bottom * win_h;
    }
    if (w > win_w) w = win_w;
    if (height > win_h) height = win_h;
    x = clampd(x, 0.0, win_w - w);
    bottom = clampd(bottom, height, win_h);
    return (rubraview_rect_t){ x, bottom - height, w, height };
}

void rubraview_subbox_buttons(rubraview_rect_t rect, double size, double gap,
                              rubraview_rect_t out[RUBRAVIEW_SUBBOX_BUTTONS]) {
    double total = size * RUBRAVIEW_SUBBOX_BUTTONS + gap * (RUBRAVIEW_SUBBOX_BUTTONS - 1);
    double x = rect.x + rect.width - total;
    if (x < 0.0) x = 0.0;
    double y = rect.y - size - gap;
    if (y < 0.0) y = rect.y + gap;   /* no room above: inside the top-right corner */
    for (int i = 0; i < RUBRAVIEW_SUBBOX_BUTTONS; ++i) {
        out[i] = (rubraview_rect_t){ x + (size + gap) * i, y, size, size };
    }
}

rubraview_subbox_part_t rubraview_subbox_hit(const rubraview_subbox_t *box, rubraview_rect_t rect,
                                             double button_size, double gap, bool body_live,
                                             double x, double y) {
    if (!box) return RUBRAVIEW_SUBBOX_NONE;
    if (box->selected) {
        rubraview_rect_t buttons[RUBRAVIEW_SUBBOX_BUTTONS];
        rubraview_subbox_buttons(rect, button_size, gap, buttons);
        static const rubraview_subbox_part_t PARTS[RUBRAVIEW_SUBBOX_BUTTONS] = {
            RUBRAVIEW_SUBBOX_SETTINGS, RUBRAVIEW_SUBBOX_MOVE, RUBRAVIEW_SUBBOX_RESIZE, RUBRAVIEW_SUBBOX_CLOSE,
        };
        for (int i = 0; i < RUBRAVIEW_SUBBOX_BUTTONS; ++i) {
            if (rubraview_rect_contains(buttons[i], x, y)) return PARTS[i];
        }
    }
    if ((body_live || box->selected) && rubraview_rect_contains(rect, x, y)) return RUBRAVIEW_SUBBOX_BODY;
    return RUBRAVIEW_SUBBOX_NONE;
}

bool rubraview_subbox_begin_drag(rubraview_subbox_t *box, rubraview_subbox_part_t part,
                                 rubraview_rect_t rect, double x, double y) {
    if (!box) return false;
    if (part == RUBRAVIEW_SUBBOX_MOVE) {
        box->grab_x = x - rect.x;
        box->grab_y = y - (rect.y + rect.height);
    } else if (part == RUBRAVIEW_SUBBOX_RESIZE) {
        box->grab_x = x - (rect.x + rect.width);
        box->grab_y = y - rect.y;
    } else {
        return false;
    }
    box->dragging = part;
    return true;
}

void rubraview_subbox_drag(rubraview_subbox_t *box, rubraview_rect_t rect, double x, double y,
                           double win_w, double win_h, double min_w, double min_h, double max_h,
                           double *out_height) {
    if (out_height) *out_height = rect.height;
    if (!box || box->dragging == RUBRAVIEW_SUBBOX_NONE || win_w <= 0.0 || win_h <= 0.0) return;

    double left = rect.x, bottom = rect.y + rect.height, w = rect.width, h = rect.height;
    if (box->dragging == RUBRAVIEW_SUBBOX_MOVE) {
        left = clampd(x - box->grab_x, 0.0, win_w - w);
        bottom = clampd(y - box->grab_y, h, win_h);
    } else {
        double corner_x = x - box->grab_x, corner_y = y - box->grab_y;
        w = clampd(corner_x - left, min_w, win_w - left);
        h = clampd(bottom - corner_y, min_h, max_h < bottom ? max_h : bottom);
        if (out_height) *out_height = h;
    }
    box->placed = true;
    box->left = left / win_w;
    box->bottom = bottom / win_h;
    box->width = w / win_w;
}

void rubraview_subbox_end_drag(rubraview_subbox_t *box) {
    if (box) box->dragging = RUBRAVIEW_SUBBOX_NONE;
}

double rubraview_subbox_padding(double font) {
    return font * 0.25;
}

static double anchor_offset(int align, double size) {
    return align == RUBRAVIEW_SUBBOX_ALIGN_START ? 0.0 : (align == RUBRAVIEW_SUBBOX_ALIGN_END ? size : size * 0.5);
}

rubraview_subbox_layout_t rubraview_subbox_layout(const rubraview_subbox_place_t *place,
                                                  double win_w, double win_h, double limit_bottom,
                                                  double font, double min_font,
                                                  rubraview_subbox_measure_fn measure, void *context) {
    rubraview_subbox_layout_t out = { .font = font };
    if (!place || win_w <= 0.0 || win_h <= 0.0) return out;
    if (limit_bottom <= 0.0 || limit_bottom > win_h) limit_bottom = win_h;
    if (min_font <= 0.0 || min_font > font) min_font = font;

    double w = clampd(place->width * win_w, 1.0, win_w);
    double h = clampd(place->height * win_h, 1.0, limit_bottom);
    double pad = rubraview_subbox_padding(font);
    double text_w = 0.0, text_h = 0.0;
    bool measured = measure && measure(context, font, w - 2.0 * pad > 1.0 ? w - 2.0 * pad : 1.0, &text_w, &text_h);

    if (measured && text_h + 2.0 * pad > h) {
        if (place->fit == RUBRAVIEW_SUBBOX_FIT_SHRINK) {
            /* A twelfth smaller each step: the size that fits is within
               that of the largest that would. */
            double f = font;
            while (f > min_font) {
                f *= 11.0 / 12.0;
                if (f < min_font) f = min_font;
                double p = rubraview_subbox_padding(f), tw = 0.0, th = 0.0;
                if (!measure(context, f, w - 2.0 * p > 1.0 ? w - 2.0 * p : 1.0, &tw, &th)) break;
                out.font = f; pad = p; text_w = tw; text_h = th;
                if (th + 2.0 * p <= h) break;
            }
        }
        /* Growing, or a font at its smallest that still does not fit. */
        if (text_h + 2.0 * pad > h) h = clampd(text_h + 2.0 * pad, 1.0, limit_bottom);
    }

    double x = place->x * win_w - anchor_offset(place->align, w);
    double y = place->y * win_h - anchor_offset(place->valign, h);
    x = clampd(x, 0.0, win_w - w);
    y = clampd(y, 0.0, limit_bottom - h);
    out.box = (rubraview_rect_t){ x, y, w, h };

    double inner_w = w - 2.0 * pad > 1.0 ? w - 2.0 * pad : 1.0;
    double inner_h = h - 2.0 * pad > 1.0 ? h - 2.0 * pad : 1.0;
    if (!measured || text_h > inner_h) text_h = inner_h;
    double ty = y + pad + anchor_offset(place->valign, inner_h - text_h);
    out.text = (rubraview_rect_t){ x + pad, ty, inner_w, text_h };
    (void)text_w;
    return out;
}

void rubraview_subbox_place_from_rect(rubraview_subbox_place_t *place, rubraview_rect_t rect,
                                      double win_w, double win_h, bool size_too) {
    if (!place || win_w <= 0.0 || win_h <= 0.0) return;
    place->x = clampd((rect.x + anchor_offset(place->align, rect.width)) / win_w, 0.0, 1.0);
    place->y = clampd((rect.y + anchor_offset(place->valign, rect.height)) / win_h, 0.0, 1.0);
    if (size_too) {
        place->width = clampd(rect.width / win_w, 0.0, 1.0);
        place->height = clampd(rect.height / win_h, 0.0, 1.0);
    }
}

rubraview_tap_result_t rubraview_tap_press(rubraview_tap_t *tap, double now) {
    if (!tap) return RUBRAVIEW_TAP_NOTHING;
    if (tap->pending && now - tap->released_at <= RUBRAVIEW_TAP_DOUBLE_SECONDS) {
        tap->pending = false;
        tap->down = true;
        tap->decided = true;
        tap->pressed_at = now;
        return RUBRAVIEW_TAP_CHOOSE;
    }
    tap->pending = false;
    tap->down = true;
    tap->decided = false;
    tap->pressed_at = now;
    return RUBRAVIEW_TAP_NOTHING;
}

rubraview_tap_result_t rubraview_tap_release(rubraview_tap_t *tap, double now) {
    if (!tap || !tap->down) return RUBRAVIEW_TAP_NOTHING;
    tap->down = false;
    if (tap->decided) return RUBRAVIEW_TAP_NOTHING;
    if (now - tap->pressed_at >= RUBRAVIEW_TAP_HOLD_SECONDS) {
        tap->decided = true;          /* held, and let go before a tick saw it */
        return RUBRAVIEW_TAP_CHOOSE;
    }
    tap->pending = true;
    tap->released_at = now;
    return RUBRAVIEW_TAP_NOTHING;
}

rubraview_tap_result_t rubraview_tap_tick(rubraview_tap_t *tap, double now) {
    if (!tap) return RUBRAVIEW_TAP_NOTHING;
    if (tap->down && !tap->decided && now - tap->pressed_at >= RUBRAVIEW_TAP_HOLD_SECONDS) {
        tap->decided = true;
        return RUBRAVIEW_TAP_CHOOSE;
    }
    if (tap->pending && now - tap->released_at > RUBRAVIEW_TAP_DOUBLE_SECONDS) {
        tap->pending = false;
        return RUBRAVIEW_TAP_SINGLE;
    }
    return RUBRAVIEW_TAP_NOTHING;
}

bool rubraview_tap_waiting(const rubraview_tap_t *tap) {
    return tap && ((tap->down && !tap->decided) || tap->pending);
}

rubraview_rect_t rubraview_choice_list_rect(double win_w, double win_h, size_t rows, double row_h,
                                            double width, double anchor_x, double anchor_y) {
    double h = row_h * (double)(rows + 1);
    if (width > win_w) width = win_w;
    if (h > win_h) h = win_h;
    double x = clampd(anchor_x - width * 0.5, 0.0, win_w - width);
    double y = clampd(anchor_y - h, 0.0, win_h - h);
    return (rubraview_rect_t){ x, y, width, h };
}

int32_t rubraview_choice_list_row_at(rubraview_rect_t list, double row_h, size_t rows, double x, double y) {
    if (row_h <= 0.0 || !rubraview_rect_contains(list, x, y)) return -1;
    double row = (y - list.y) / row_h - 1.0;   /* row 0 of the list is its title */
    if (row < 0.0) return -1;
    size_t index = (size_t)row;
    return index < rows ? (int32_t)index : -1;
}
