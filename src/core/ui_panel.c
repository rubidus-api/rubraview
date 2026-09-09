#include "rubraview/ui_panel.h"
#include <string.h>
#include <math.h>

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

rubraview_panel_t rubraview_panel_create(u8str_t title, double dpi_scale) {
    if (dpi_scale <= 0.0) dpi_scale = 1.0;
    rubraview_panel_t panel = {0};
    panel.title = title;
    panel.row_height = 28.0 * dpi_scale;
    panel.padding = 10.0 * dpi_scale;
    panel.label_width = 110.0 * dpi_scale;
    panel.bounds = (rubraview_rect_t){ .x = 0.0, .y = 0.0, .width = 320.0 * dpi_scale, .height = 0.0 };
    panel.active_row = -1;
    panel.hover_row = -1;
    return panel;
}

static rubraview_panel_row_t *push_row(rubraview_panel_t *panel) {
    if (!panel || panel->row_count >= RUBRAVIEW_PANEL_MAX_ROWS) return NULL;
    rubraview_panel_row_t *row = &panel->rows[panel->row_count++];
    memset(row, 0, sizeof(*row));
    row->enabled = true;
    return row;
}

bool rubraview_panel_add_slider(rubraview_panel_t *panel, int32_t id, u8str_t label,
                                double value, double min_value, double max_value, double step) {
    rubraview_panel_row_t *row = push_row(panel);
    if (!row) return false;
    if (max_value <= min_value) max_value = min_value + 1.0;
    row->kind = RUBRAVIEW_ROW_SLIDER;
    row->id = id;
    row->label = label;
    row->min_value = min_value;
    row->max_value = max_value;
    row->step = step;
    row->value = clampd(value, min_value, max_value);
    return true;
}

bool rubraview_panel_add_toggle(rubraview_panel_t *panel, int32_t id, u8str_t label, bool on) {
    rubraview_panel_row_t *row = push_row(panel);
    if (!row) return false;
    row->kind = RUBRAVIEW_ROW_TOGGLE;
    row->id = id;
    row->label = label;
    row->value = on ? 1.0 : 0.0;
    row->min_value = 0.0;
    row->max_value = 1.0;
    return true;
}

bool rubraview_panel_add_choice(rubraview_panel_t *panel, int32_t id, u8str_t label,
                                int32_t index, int32_t choice_count) {
    rubraview_panel_row_t *row = push_row(panel);
    if (!row) return false;
    if (choice_count < 1) choice_count = 1;
    row->kind = RUBRAVIEW_ROW_CHOICE;
    row->id = id;
    row->label = label;
    row->choice_count = choice_count;
    row->min_value = 0.0;
    row->max_value = (double)(choice_count - 1);
    row->step = 1.0;
    row->value = clampd((double)index, 0.0, row->max_value);
    return true;
}

bool rubraview_panel_add_button(rubraview_panel_t *panel, int32_t id, u8str_t label) {
    rubraview_panel_row_t *row = push_row(panel);
    if (!row) return false;
    row->kind = RUBRAVIEW_ROW_BUTTON;
    row->id = id;
    row->label = label;
    return true;
}

bool rubraview_panel_add_separator(rubraview_panel_t *panel) {
    rubraview_panel_row_t *row = push_row(panel);
    if (!row) return false;
    row->kind = RUBRAVIEW_ROW_SEPARATOR;
    row->id = -1;
    row->enabled = false;
    return true;
}

/* A separator is a thin rule rather than a full row: it groups without
   costing the vertical space a row does. */
static double row_extent(const rubraview_panel_t *panel, const rubraview_panel_row_t *row) {
    return row->kind == RUBRAVIEW_ROW_SEPARATOR ? panel->row_height * 0.4 : panel->row_height;
}

void rubraview_panel_layout(rubraview_panel_t *panel, double window_width, double window_height) {
    if (!panel) return;

    double content = 0.0;
    for (size_t i = 0; i < panel->row_count; ++i) content += row_extent(panel, &panel->rows[i]);

    double title_height = panel->row_height;
    double height = title_height + content + panel->padding * 2.0;

    /* A panel taller than the window is clipped to it rather than
       running off the bottom — the same rule §3.6 gives the menu box. */
    if (height > window_height) height = window_height;

    panel->bounds.height = height;
    if (panel->bounds.width > window_width) panel->bounds.width = window_width;

    panel->bounds.x = window_width - panel->bounds.width - panel->padding;
    if (panel->bounds.x < 0.0) panel->bounds.x = 0.0;
    panel->bounds.y = (window_height - height) * 0.5;
    if (panel->bounds.y < 0.0) panel->bounds.y = 0.0;
}

rubraview_rect_t rubraview_panel_row_rect(const rubraview_panel_t *panel, size_t index) {
    rubraview_rect_t empty = {0};
    if (!panel || index >= panel->row_count) return empty;

    double y = panel->bounds.y + panel->padding + panel->row_height; /* below the title */
    for (size_t i = 0; i < index; ++i) y += row_extent(panel, &panel->rows[i]);

    return (rubraview_rect_t){
        .x = panel->bounds.x + panel->padding,
        .y = y,
        .width = panel->bounds.width - panel->padding * 2.0,
        .height = row_extent(panel, &panel->rows[index]),
    };
}

rubraview_rect_t rubraview_panel_control_rect(const rubraview_panel_t *panel, size_t index) {
    rubraview_rect_t row = rubraview_panel_row_rect(panel, index);
    if (row.width <= 0.0) return row;

    /* A button has no label column: it is its own label. */
    if (panel->rows[index].kind == RUBRAVIEW_ROW_BUTTON) return row;

    double label = panel->label_width;
    if (label > row.width * 0.6) label = row.width * 0.6;

    row.x += label;
    row.width -= label;
    /* Leave a little air above and below so a track does not touch its
       neighbours. */
    double inset = row.height * 0.25;
    row.y += inset;
    row.height -= inset * 2.0;
    return row;
}

int32_t rubraview_panel_row_at(const rubraview_panel_t *panel, double px, double py) {
    if (!panel || !panel->open) return -1;
    if (!rubraview_rect_contains(panel->bounds, px, py)) return -1;

    for (size_t i = 0; i < panel->row_count; ++i) {
        if (panel->rows[i].kind == RUBRAVIEW_ROW_SEPARATOR) continue;
        if (!panel->rows[i].enabled) continue;
        rubraview_rect_t r = rubraview_panel_row_rect(panel, i);
        if (rubraview_rect_contains(r, px, py)) return (int32_t)i;
    }
    return -1;
}

/* Rounds to the row's step, so a stepped slider cannot come to rest
   between two legal values. */
static double snap(const rubraview_panel_row_t *row, double value) {
    value = clampd(value, row->min_value, row->max_value);
    if (row->step > 0.0) {
        double steps = (value - row->min_value) / row->step;
        value = row->min_value + floor(steps + 0.5) * row->step;
        value = clampd(value, row->min_value, row->max_value);
    }
    return value;
}

static double value_from_x(const rubraview_panel_t *panel, size_t index, double px) {
    rubraview_rect_t control = rubraview_panel_control_rect(panel, index);
    const rubraview_panel_row_t *row = &panel->rows[index];
    if (control.width <= 0.0) return row->value;

    double t = (px - control.x) / control.width;
    t = clampd(t, 0.0, 1.0);
    return snap(row, row->min_value + t * (row->max_value - row->min_value));
}

rubraview_panel_event_t rubraview_panel_press(rubraview_panel_t *panel, double px, double py,
                                              int32_t *out_row) {
    if (out_row) *out_row = -1;
    int32_t index = rubraview_panel_row_at(panel, px, py);
    if (index < 0) return RUBRAVIEW_PANEL_NONE;

    rubraview_panel_row_t *row = &panel->rows[index];
    if (out_row) *out_row = index;

    switch (row->kind) {
        case RUBRAVIEW_ROW_SLIDER: {
            /* Clicking a track jumps to that spot and starts dragging,
               which is what a reader expects; hunting for the handle is
               not. */
            panel->active_row = index;
            double before = row->value;
            row->value = value_from_x(panel, (size_t)index, px);
            return row->value != before ? RUBRAVIEW_PANEL_VALUE_CHANGED : RUBRAVIEW_PANEL_NONE;
        }
        case RUBRAVIEW_ROW_TOGGLE:
            row->value = row->value > 0.5 ? 0.0 : 1.0;
            return RUBRAVIEW_PANEL_VALUE_CHANGED;
        case RUBRAVIEW_ROW_CHOICE:
            row->value += 1.0;
            if (row->value > row->max_value) row->value = 0.0;
            return RUBRAVIEW_PANEL_VALUE_CHANGED;
        case RUBRAVIEW_ROW_BUTTON:
            return RUBRAVIEW_PANEL_BUTTON_PRESSED;
        default:
            return RUBRAVIEW_PANEL_NONE;
    }
}

rubraview_panel_event_t rubraview_panel_drag(rubraview_panel_t *panel, double px, double py,
                                             int32_t *out_row) {
    (void)py;
    if (out_row) *out_row = -1;
    if (!panel || panel->active_row < 0) return RUBRAVIEW_PANEL_NONE;
    if ((size_t)panel->active_row >= panel->row_count) return RUBRAVIEW_PANEL_NONE;

    size_t index = (size_t)panel->active_row;
    rubraview_panel_row_t *row = &panel->rows[index];
    if (row->kind != RUBRAVIEW_ROW_SLIDER) return RUBRAVIEW_PANEL_NONE;

    /* The vertical position is ignored on purpose: once a slider is
       held, dragging away from it must not drop it. */
    double before = row->value;
    row->value = value_from_x(panel, index, px);
    if (out_row) *out_row = (int32_t)index;
    return row->value != before ? RUBRAVIEW_PANEL_VALUE_CHANGED : RUBRAVIEW_PANEL_NONE;
}

void rubraview_panel_release(rubraview_panel_t *panel) {
    if (panel) panel->active_row = -1;
}

double rubraview_panel_fill_fraction(const rubraview_panel_t *panel, size_t index) {
    if (!panel || index >= panel->row_count) return 0.0;
    const rubraview_panel_row_t *row = &panel->rows[index];
    double span = row->max_value - row->min_value;
    if (span <= 0.0) return 0.0;
    return clampd((row->value - row->min_value) / span, 0.0, 1.0);
}

int32_t rubraview_panel_find(const rubraview_panel_t *panel, int32_t id) {
    if (!panel) return -1;
    for (size_t i = 0; i < panel->row_count; ++i) {
        if (panel->rows[i].kind != RUBRAVIEW_ROW_SEPARATOR && panel->rows[i].id == id) return (int32_t)i;
    }
    return -1;
}

void rubraview_panel_set_value(rubraview_panel_t *panel, int32_t id, double value) {
    int32_t index = rubraview_panel_find(panel, id);
    if (index < 0) return;
    panel->rows[index].value = snap(&panel->rows[index], value);
}
