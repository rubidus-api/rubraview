#include "rubraview/compositor.h"

/* The visible extent of one side: a split spread shows half the page. */
static void side_extent(const rubraview_page_size_t *size, rubraview_spread_half_t half,
                        double *out_width, double *out_height,
                        double *out_src_left, double *out_src_right) {
    double w = size->width;
    double h = size->height;

    switch (half) {
        case RUBRAVIEW_SPREAD_LEFT_HALF:
            *out_src_left = 0.0;
            *out_src_right = w / 2.0;
            *out_width = w / 2.0;
            break;
        case RUBRAVIEW_SPREAD_RIGHT_HALF:
            *out_src_left = w / 2.0;
            *out_src_right = w;
            *out_width = w - w / 2.0;
            break;
        case RUBRAVIEW_SPREAD_WHOLE:
        default:
            *out_src_left = 0.0;
            *out_src_right = w;
            *out_width = w;
            break;
    }
    *out_height = h;
}

rubraview_composition_t rubraview_compose_spread(const rubraview_spread_t *spread,
                                                 const rubraview_page_size_t *left_size,
                                                 const rubraview_page_size_t *right_size,
                                                 double window_width, double window_height,
                                                 rubraview_fit_mode_t fit_mode,
                                                 double gutter,
                                                 double zoom,
                                                 double pan_x, double pan_y) {
    rubraview_composition_t out = {0};
    if (!spread || window_width <= 0.0 || window_height <= 0.0) return out;
    if (zoom <= 0.0) zoom = 1.0;
    if (gutter < 0.0) gutter = 0.0;

    bool has_left = (spread->left_index >= 0) && (left_size != NULL) &&
                    left_size->width > 0.0 && left_size->height > 0.0;
    bool has_right = (spread->right_index >= 0) && (right_size != NULL) &&
                     right_size->width > 0.0 && right_size->height > 0.0;
    if (!has_left && !has_right) return out;

    double lw = 0.0, lh = 0.0, l_src_left = 0.0, l_src_right = 0.0;
    if (has_left) {
        side_extent(left_size, spread->left_half, &lw, &lh, &l_src_left, &l_src_right);
    }

    double rw = 0.0, rh = 0.0, r_src_left = 0.0, r_src_right = 0.0;
    if (has_right) {
        /* Only a single-page spread can be a split half, so the right
           side is always drawn whole. */
        side_extent(right_size, RUBRAVIEW_SPREAD_WHOLE, &rw, &rh, &r_src_left, &r_src_right);
    }

    /* A pair is shown at one height: the lower-resolution page is scaled
       up to the taller one's, in page units, so a 1000-pixel scan beside
       a 3000-pixel one is not a third of its size. At actual size the
       reader asked for each page's own pixels, so there nothing is. */
    double l_unit = 1.0, r_unit = 1.0;
    if (has_left && has_right && fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) {
        double target = (lh > rh) ? lh : rh;
        l_unit = target / lh;
        r_unit = target / rh;
    }
    double lw_u = lw * l_unit, lh_u = lh * l_unit;
    double rw_u = rw * r_unit, rh_u = rh * r_unit;

    double effective_gutter = (has_left && has_right) ? gutter : 0.0;
    double content_w = lw_u + effective_gutter + rw_u;
    double content_h = (lh_u > rh_u) ? lh_u : rh_u;
    if (content_w <= 0.0 || content_h <= 0.0) return out;

    /* Fit the combined spread as one unit, then apply zoom and pan. */
    rubraview_viewport_transform_t fit = rubraview_fit_compute(fit_mode, content_w, content_h, window_width, window_height);
    double scale = fit.scale_x * zoom;
    double scale_y = fit.scale_y * zoom;

    /* Re-centre for the zoomed size so zooming grows about the middle,
       then apply the caller's pan. */
    double origin_x = (window_width - content_w * scale) / 2.0 + pan_x;
    double origin_y = (window_height - content_h * scale_y) / 2.0 + pan_y;

    rubraview_mat3x2_t viewport = rubraview_mat3x2_multiply(
        rubraview_mat3x2_scale(scale, scale_y),
        rubraview_mat3x2_translate(origin_x, origin_y));

    out.content_width = content_w;
    out.content_height = content_h;
    out.scale = scale;

    if (has_left) {
        rubraview_draw_command_t cmd = {
            .page_index = spread->left_index,
            .src_left = l_src_left, .src_top = 0.0,
            .src_right = l_src_right, .src_bottom = lh,
            .scale = scale * l_unit,
        };
        /* Centre this page vertically within the spread's height. */
        double centre = (content_h - lh_u) / 2.0;
        cmd.transform = rubraview_mat3x2_multiply(
            rubraview_mat3x2_multiply(rubraview_mat3x2_scale(l_unit, l_unit),
                                      rubraview_mat3x2_translate(0.0, centre)),
            viewport);
        out.commands[out.count++] = cmd;
    }

    if (has_right) {
        rubraview_draw_command_t cmd = {
            .page_index = spread->right_index,
            .src_left = r_src_left, .src_top = 0.0,
            .src_right = r_src_right, .src_bottom = rh,
            .scale = scale * r_unit,
        };
        double centre = (content_h - rh_u) / 2.0;
        cmd.transform = rubraview_mat3x2_multiply(
            rubraview_mat3x2_multiply(rubraview_mat3x2_scale(r_unit, r_unit),
                                      rubraview_mat3x2_translate(lw_u + effective_gutter, centre)),
            viewport);
        out.commands[out.count++] = cmd;
    }

    return out;
}
