#include "rubraview/tiles.h"
#include <math.h>

bool rubraview_tiles_wanted(double screen_scale, int32_t picture_w, int32_t texture_w) {
    if (screen_scale <= 0.0 || picture_w <= 0 || texture_w <= 0 || texture_w >= picture_w) return false;
    return screen_scale * (double)picture_w / (double)texture_w > 1.05;
}

int32_t rubraview_tile_level(double screen_scale) {
    if (!(screen_scale > 0.0) || screen_scale >= 1.0) return 0;
    /* 2^-L >= s: the largest such L is floor(log2(1/s)). The small bias
       keeps an exact power of two on its own level despite rounding. */
    int32_t level = (int32_t)floor(log2(1.0 / screen_scale) + 1e-9);
    if (level < 0) level = 0;
    if (level > RUBRAVIEW_TILE_MAX_LEVEL) level = RUBRAVIEW_TILE_MAX_LEVEL;
    return level;
}

int32_t rubraview_tile_level_enlarged(double screen_scale) {
    if (!(screen_scale > 1.05)) return 0;
    int32_t k = (int32_t)ceil(log2(screen_scale) - 1e-9);
    if (k < 1) k = 1;
    if (k > -RUBRAVIEW_TILE_MIN_LEVEL) k = -RUBRAVIEW_TILE_MIN_LEVEL;
    return -k;
}

double rubraview_tile_span(int32_t level) {
    if (level >= 0) return (double)((int64_t)RUBRAVIEW_TILE_SIDE << (level > RUBRAVIEW_TILE_MAX_LEVEL ? RUBRAVIEW_TILE_MAX_LEVEL : level));
    return (double)(RUBRAVIEW_TILE_SIDE >> (level < RUBRAVIEW_TILE_MIN_LEVEL ? -RUBRAVIEW_TILE_MIN_LEVEL : -level));
}

bool rubraview_tile_geometry(rubraview_tile_key_t key, int32_t picture_w, int32_t picture_h,
                             int32_t *x, int32_t *y, int32_t *w, int32_t *h,
                             int32_t *out_w, int32_t *out_h) {
    if (key.level < RUBRAVIEW_TILE_MIN_LEVEL || key.level > RUBRAVIEW_TILE_MAX_LEVEL || key.tx < 0 || key.ty < 0 ||
        picture_w <= 0 || picture_h <= 0) return false;
    int64_t span = (int64_t)rubraview_tile_span(key.level);   /* picture pixels a tile covers */
    int64_t x0 = (int64_t)key.tx * span, y0 = (int64_t)key.ty * span;
    if (x0 >= picture_w || y0 >= picture_h) return false;
    int64_t x1 = x0 + span < picture_w ? x0 + span : picture_w;
    int64_t y1 = y0 + span < picture_h ? y0 + span : picture_h;
    *x = (int32_t)x0;
    *y = (int32_t)y0;
    *w = (int32_t)(x1 - x0);
    *h = (int32_t)(y1 - y0);
    if (key.level < 0) {
        /* Enlarged: every picture pixel is 2^-level output pixels a side. */
        *out_w = *w << -key.level;
        *out_h = *h << -key.level;
        return true;
    }
    int64_t step = (int64_t)1 << key.level;
    *out_w = (int32_t)((*w + step - 1) / step);
    *out_h = (int32_t)((*h + step - 1) / step);
    return true;
}

size_t rubraview_tiles_visible(double x0, double y0, double x1, double y1, int32_t level,
                               int32_t picture_w, int32_t picture_h,
                               rubraview_tile_key_t *out, size_t cap) {
    if (!out || cap == 0 || picture_w <= 0 || picture_h <= 0 ||
        level < RUBRAVIEW_TILE_MIN_LEVEL || level > RUBRAVIEW_TILE_MAX_LEVEL) return 0;
    if (x0 > x1) { double t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { double t = y0; y0 = y1; y1 = t; }
    if (x0 < 0.0) x0 = 0.0;
    if (y0 < 0.0) y0 = 0.0;
    if (x1 > (double)picture_w) x1 = (double)picture_w;
    if (y1 > (double)picture_h) y1 = (double)picture_h;
    if (x1 <= x0 || y1 <= y0) return 0;

    double span = rubraview_tile_span(level);
    int32_t tx0 = (int32_t)floor(x0 / span), ty0 = (int32_t)floor(y0 / span);
    int32_t tx1 = (int32_t)ceil(x1 / span) - 1, ty1 = (int32_t)ceil(y1 / span) - 1;

    size_t n = 0;
    for (int32_t ty = ty0; ty <= ty1 && n < cap; ++ty) {
        for (int32_t tx = tx0; tx <= tx1 && n < cap; ++tx) {
            out[n++] = (rubraview_tile_key_t){ .level = level, .tx = tx, .ty = ty };
        }
    }

    /* The middle of the view first: it is what the reader is looking at.
       Insertion sort — a view holds a few dozen tiles at most. */
    double cx = (x0 + x1) / 2.0, cy = (y0 + y1) / 2.0;
    for (size_t i = 1; i < n; ++i) {
        rubraview_tile_key_t k = out[i];
        double kx = ((double)k.tx + 0.5) * span - cx, ky = ((double)k.ty + 0.5) * span - cy;
        double kd = kx * kx + ky * ky;
        size_t j = i;
        while (j > 0) {
            double px = ((double)out[j - 1].tx + 0.5) * span - cx, py = ((double)out[j - 1].ty + 0.5) * span - cy;
            if (px * px + py * py <= kd) break;
            out[j] = out[j - 1];
            --j;
        }
        out[j] = k;
    }
    return n;
}

bool rubraview_mat3x2_invert(rubraview_mat3x2_t m, rubraview_mat3x2_t *out) {
    /* Row vectors: (x, y) -> (x*a + y*c + e, x*b + y*d + f). */
    double det = m.a * m.d - m.b * m.c;
    if (!(fabs(det) > 1e-12)) return false;
    double ia = m.d / det, ib = -m.b / det, ic = -m.c / det, id = m.a / det;
    *out = (rubraview_mat3x2_t){
        .a = ia, .b = ib, .c = ic, .d = id,
        .e = -(m.e * ia + m.f * ic),
        .f = -(m.e * ib + m.f * id),
    };
    return true;
}

void rubraview_exif_upright_size(int orientation, int32_t stored_w, int32_t stored_h,
                                 int32_t *out_w, int32_t *out_h) {
    bool turned = orientation >= 5 && orientation <= 8;
    *out_w = turned ? stored_h : stored_w;
    *out_h = turned ? stored_w : stored_h;
}

void rubraview_exif_point_to_stored(int orientation, int32_t W, int32_t H,
                                    int32_t ux, int32_t uy, int32_t *out_x, int32_t *out_y) {
    int32_t x = ux, y = uy;
    switch (orientation) {
        case 2: x = W - 1 - ux; y = uy;         break;   /* mirrored */
        case 3: x = W - 1 - ux; y = H - 1 - uy; break;   /* upside down */
        case 4: x = ux;         y = H - 1 - uy; break;   /* flipped */
        case 5: x = uy;         y = ux;         break;   /* transposed */
        case 6: x = uy;         y = H - 1 - ux; break;   /* stored a quarter turn left */
        case 7: x = W - 1 - uy; y = H - 1 - ux; break;   /* transversed */
        case 8: x = W - 1 - uy; y = ux;         break;   /* stored a quarter turn right */
        default: break;
    }
    *out_x = x;
    *out_y = y;
}

void rubraview_exif_rect_to_stored(int orientation, int32_t W, int32_t H,
                                   int32_t x, int32_t y, int32_t w, int32_t h,
                                   int32_t *out_x, int32_t *out_y, int32_t *out_w, int32_t *out_h) {
    int32_t ax, ay, bx, by;
    rubraview_exif_point_to_stored(orientation, W, H, x, y, &ax, &ay);
    rubraview_exif_point_to_stored(orientation, W, H, x + w - 1, y + h - 1, &bx, &by);
    *out_x = ax < bx ? ax : bx;
    *out_y = ay < by ? ay : by;
    *out_w = (ax > bx ? ax - bx : bx - ax) + 1;
    *out_h = (ay > by ? ay - by : by - ay) + 1;
}
