#include "rubraview/ui_slidepop.h"

#include <math.h>

/* At a scale of 1: the window, its rows, and the column the parts start in. */
#define POP_WIDTH   430.0
#define POP_HEIGHT  246.0
#define POP_PAD     12.0
#define POP_ROW     36.0
#define POP_COLUMN  112.0   /* after the rows' labels */
#define POP_TITLE   32.0

static double row_top(int row) {   /* 0 delay, 1 change, 2 at the end, 3 buttons */
    static const double TOP[4] = { 48.0, 94.0, 140.0, 198.0 };
    return TOP[row];
}

rubraview_rect_t rubraview_slidepop_frame(double window_width, double window_height, double x, double y, double scale) {
    double w = POP_WIDTH * scale, h = POP_HEIGHT * scale;
    double left = x, top = y;
    if (left + w > window_width) left = x - w;
    if (top + h > window_height) top = y - h;
    if (left + w > window_width) left = window_width - w;
    if (top + h > window_height) top = window_height - h;
    if (left < 0.0) left = 0.0;
    if (top < 0.0) top = 0.0;
    return (rubraview_rect_t){ left, top, w, h };
}

rubraview_rect_t rubraview_slidepop_title(rubraview_rect_t f, double s) {
    return (rubraview_rect_t){ f.x + POP_PAD * s, f.y + 8.0 * s, f.width - 2.0 * POP_PAD * s, POP_TITLE * s };
}

rubraview_rect_t rubraview_slidepop_label(rubraview_rect_t f, double s, rubraview_slidepop_label_t label) {
    int row = label == RUBRAVIEW_SLIDEPOP_LABEL_DELAY ? 0 : label == RUBRAVIEW_SLIDEPOP_LABEL_FX ? 1 : 2;
    return (rubraview_rect_t){ f.x + POP_PAD * s, f.y + row_top(row) * s, (POP_COLUMN - POP_PAD - 4.0) * s, POP_ROW * s };
}

rubraview_rect_t rubraview_slidepop_part(rubraview_rect_t f, double s, rubraview_slidepop_part_t part) {
    double x = 0.0, w = 0.0;
    int row = 0;
    switch (part) {
        case RUBRAVIEW_SLIDEPOP_DELAY_DOWN:  row = 0; x = POP_COLUMN;         w = 44.0; break;
        case RUBRAVIEW_SLIDEPOP_DELAY_FIELD: row = 0; x = POP_COLUMN + 50.0;  w = 120.0; break;
        case RUBRAVIEW_SLIDEPOP_DELAY_UP:    row = 0; x = POP_COLUMN + 176.0; w = 44.0; break;
        case RUBRAVIEW_SLIDEPOP_FX_CUT:
        case RUBRAVIEW_SLIDEPOP_FX_FADE:
        case RUBRAVIEW_SLIDEPOP_FX_SLIDE:
        case RUBRAVIEW_SLIDEPOP_FX_ZOOM:
            row = 1; x = POP_COLUMN + 77.0 * (double)(part - RUBRAVIEW_SLIDEPOP_FX_CUT); w = 71.0; break;
        case RUBRAVIEW_SLIDEPOP_LOOP:        row = 2; x = POP_COLUMN;         w = 140.0; break;
        case RUBRAVIEW_SLIDEPOP_ONCE:        row = 2; x = POP_COLUMN + 146.0; w = 160.0; break;
        case RUBRAVIEW_SLIDEPOP_PAUSE:
        case RUBRAVIEW_SLIDEPOP_STOP:
        case RUBRAVIEW_SLIDEPOP_FULL:
        case RUBRAVIEW_SLIDEPOP_CLOSE:
            row = 3; x = POP_PAD + 103.0 * (double)(part - RUBRAVIEW_SLIDEPOP_PAUSE); w = 97.0; break;
        default:
            return (rubraview_rect_t){ 0.0, 0.0, 0.0, 0.0 };
    }
    return (rubraview_rect_t){ f.x + x * s, f.y + row_top(row) * s, w * s, POP_ROW * s };
}

rubraview_slidepop_part_t rubraview_slidepop_part_at(rubraview_rect_t f, double s, double x, double y) {
    for (int p = 0; p < RUBRAVIEW_SLIDEPOP_PART_COUNT; ++p) {
        rubraview_rect_t r = rubraview_slidepop_part(f, s, (rubraview_slidepop_part_t)p);
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) return (rubraview_slidepop_part_t)p;
    }
    return RUBRAVIEW_SLIDEPOP_NONE;
}

double rubraview_slidepop_step(double seconds, int direction) {
    if (!(seconds > 0.0)) seconds = 0.5;
    double halves = seconds * 2.0;
    /* 2.3 goes to 2.5 or 2.0, the half seconds either side; 2.5 to 3.0 or 2.0 */
    double next = direction > 0 ? floor(halves + 1e-9) + 1.0 : ceil(halves - 1e-9) - 1.0;
    if (next < 1.0) next = 1.0;
    if (next > 600.0) next = 600.0;
    return next / 2.0;
}

/* ---- RV-237: the volume's bar ---- */
#define VOL_WIDTH  248.0
#define VOL_HEIGHT 40.0
#define VOL_PAD    12.0
#define VOL_FIELD  56.0

rubraview_rect_t rubraview_volpop_frame(rubraview_rect_t button, double window_width, double window_height, double scale) {
    double w = VOL_WIDTH * scale, h = VOL_HEIGHT * scale, gap = 6.0 * scale;
    double left = button.x + button.width * 0.5 - w * 0.5;
    double top = button.y - gap - h;
    if (top < 0.0) top = button.y + button.height + gap;
    if (left + w > window_width) left = window_width - w;
    if (top + h > window_height) top = window_height - h;
    if (left < 0.0) left = 0.0;
    if (top < 0.0) top = 0.0;
    return (rubraview_rect_t){ left, top, w, h };
}

rubraview_rect_t rubraview_volpop_part(rubraview_rect_t f, double s, rubraview_volpop_part_t part) {
    if (part == RUBRAVIEW_VOLPOP_FIELD)
        return (rubraview_rect_t){ f.x + f.width - (VOL_PAD + VOL_FIELD) * s, f.y + 6.0 * s, VOL_FIELD * s, f.height - 12.0 * s };
    if (part == RUBRAVIEW_VOLPOP_BAR)
        return (rubraview_rect_t){ f.x + VOL_PAD * s, f.y + 6.0 * s, f.width - (3.0 * VOL_PAD + VOL_FIELD) * s, f.height - 12.0 * s };
    return (rubraview_rect_t){0};
}

rubraview_volpop_part_t rubraview_volpop_part_at(rubraview_rect_t f, double s, double x, double y) {
    for (int p = RUBRAVIEW_VOLPOP_BAR; p <= RUBRAVIEW_VOLPOP_FIELD; ++p) {
        rubraview_rect_t r = rubraview_volpop_part(f, s, (rubraview_volpop_part_t)p);
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) return (rubraview_volpop_part_t)p;
    }
    return RUBRAVIEW_VOLPOP_NONE;
}

double rubraview_volpop_value_at(rubraview_rect_t bar, double x) {
    if (!(bar.width > 0.0)) return 0.0;
    double t = (x - bar.x) / bar.width;
    if (!(t > 0.0)) t = 0.0;
    if (t > 1.0) t = 1.0;
    return round(t * 100.0);
}

bool rubraview_volpop_parse(proven_u8str_view_t text, double *out_percent) {
    double value = 0.0, scale = 0.0;
    bool digits = false;
    for (size_t i = 0; i < text.size; ++i) {
        char c = (char)text.ptr[i];
        if (c >= '0' && c <= '9') {
            digits = true;
            if (scale > 0.0) { value += (c - '0') * scale; scale *= 0.1; }
            else value = value * 10.0 + (c - '0');
            if (value > 1e6) value = 1e6;
        } else if ((c == '.' || c == ',') && scale == 0.0) {
            scale = 0.1;
        } else if (c != ' ' && c != '%') {
            return false;
        }
    }
    if (!digits || !out_percent) return false;
    value = round(value);
    *out_percent = value > 100.0 ? 100.0 : value;
    return true;
}
