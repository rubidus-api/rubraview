#include "rubraview/viewport.h"
#include <math.h>

static double dmin(double a, double b) { return a < b ? a : b; }

rubraview_viewport_transform_t rubraview_fit_compute(rubraview_fit_mode_t mode, double img_w, double img_h, double win_w, double win_h) {
    rubraview_viewport_transform_t t = { .scale_x = 1.0, .scale_y = 1.0, .offset_x = 0.0, .offset_y = 0.0 };
    if (img_w <= 0.0 || img_h <= 0.0 || win_w <= 0.0 || win_h <= 0.0) return t;

    double sx = 1.0, sy = 1.0;

    switch (mode) {
        case RUBRAVIEW_FIT_WINDOW: {
            double s = dmin(win_w / img_w, win_h / img_h);
            sx = sy = s;
            break;
        }
        case RUBRAVIEW_FIT_WIDTH:
            sx = sy = win_w / img_w;
            break;
        case RUBRAVIEW_FIT_HEIGHT:
            sx = sy = win_h / img_h;
            break;
        case RUBRAVIEW_FIT_STRETCH:
            sx = win_w / img_w;
            sy = win_h / img_h;
            break;
        case RUBRAVIEW_FIT_ACTUAL_SIZE:
            sx = sy = 1.0;
            break;
        case RUBRAVIEW_FIT_SMART:
            if (img_w > win_w || img_h > win_h) {
                sx = sy = dmin(win_w / img_w, win_h / img_h);
            } else {
                sx = sy = 1.0;
            }
            break;
        default:
            sx = sy = 1.0;
            break;
    }

    t.scale_x = sx;
    t.scale_y = sy;
    t.offset_x = (win_w - img_w * sx) / 2.0;
    t.offset_y = (win_h - img_h * sy) / 2.0;
    return t;
}

rubraview_mat3x2_t rubraview_mat3x2_identity(void) {
    return (rubraview_mat3x2_t){ .a = 1.0, .b = 0.0, .c = 0.0, .d = 1.0, .e = 0.0, .f = 0.0 };
}

rubraview_mat3x2_t rubraview_mat3x2_translate(double dx, double dy) {
    return (rubraview_mat3x2_t){ .a = 1.0, .b = 0.0, .c = 0.0, .d = 1.0, .e = dx, .f = dy };
}

rubraview_mat3x2_t rubraview_mat3x2_scale(double sx, double sy) {
    return (rubraview_mat3x2_t){ .a = sx, .b = 0.0, .c = 0.0, .d = sy, .e = 0.0, .f = 0.0 };
}

rubraview_mat3x2_t rubraview_mat3x2_multiply(rubraview_mat3x2_t first, rubraview_mat3x2_t second) {
    /* Result of applying `first` then `second`. */
    return (rubraview_mat3x2_t){
        .a = second.a * first.a + second.c * first.b,
        .b = second.b * first.a + second.d * first.b,
        .c = second.a * first.c + second.c * first.d,
        .d = second.b * first.c + second.d * first.d,
        .e = second.a * first.e + second.c * first.f + second.e,
        .f = second.b * first.e + second.d * first.f + second.f,
    };
}

void rubraview_mat3x2_apply(rubraview_mat3x2_t m, double x, double y, double *out_x, double *out_y) {
    if (out_x) *out_x = m.a * x + m.c * y + m.e;
    if (out_y) *out_y = m.b * x + m.d * y + m.f;
}

rubraview_mat3x2_t rubraview_viewport_matrix(double cx, double cy, double scale, double dx, double dy) {
    rubraview_mat3x2_t t1 = rubraview_mat3x2_translate(-cx, -cy);
    rubraview_mat3x2_t s = rubraview_mat3x2_scale(scale, scale);
    rubraview_mat3x2_t t2 = rubraview_mat3x2_translate(dx, dy);
    return rubraview_mat3x2_multiply(rubraview_mat3x2_multiply(t1, s), t2);
}

bool rubraview_fit_within_limits(int32_t width, int32_t height, int32_t max_side, uint64_t max_pixels,
                                 int32_t *out_width, int32_t *out_height) {
    *out_width = width;
    *out_height = height;
    if (width <= 0 || height <= 0) return false;
    double k = 1.0;
    if (max_side > 0) {
        int32_t longest = width > height ? width : height;
        if (longest > max_side) k = (double)max_side / (double)longest;
    }
    if (max_pixels > 0) {
        double pixels = (double)width * (double)height * k * k;
        if (pixels > (double)max_pixels) k *= sqrt((double)max_pixels / pixels);
    }
    if (k >= 1.0) return false;
    int32_t w = (int32_t)floor((double)width * k);
    int32_t h = (int32_t)floor((double)height * k);
    /* floor may leave the product a hair over the budget */
    while (max_pixels > 0 && (uint64_t)w * (uint64_t)h > max_pixels && w > 1 && h > 1) {
        if (w >= h) w--; else h--;
    }
    *out_width = w < 1 ? 1 : w;
    *out_height = h < 1 ? 1 : h;
    return true;
}
