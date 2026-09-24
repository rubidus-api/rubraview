/*
 * A picker tile's picture: cut, shrink, soften, darken (D-34). See thumb.h.
 */
#include "rubraview/thumb.h"
#include "rubraview/filter.h"
#include "rubraview/resample.h"

rubraview_pixbuf_t rubraview_thumb_make(proven_arena_t *arena, const rubraview_pixbuf_t *src,
                                        double aspect, int32_t max_width, int32_t blur_radius, double keep) {
    rubraview_pixbuf_t none = {0};
    if (!arena || !rubraview_pixbuf_is_valid(src) || rubraview_bytes_per_pixel(src->format) != 4) return none;
    if (!(aspect > 0.0) || max_width <= 0) return none;

    /* 1. the middle, in the tile's shape */
    int32_t cw = src->width, ch = src->height;
    if ((double)src->width / (double)src->height > aspect) {
        cw = (int32_t)((double)src->height * aspect + 0.5);
    } else {
        ch = (int32_t)((double)src->width / aspect + 0.5);
    }
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    rubraview_pixbuf_t cut = rubraview_pixbuf_crop(arena, src, (src->width - cw) / 2, (src->height - ch) / 2, cw, ch);
    if (!rubraview_pixbuf_is_valid(&cut)) return none;

    /* 2. small */
    int32_t tw = cw < max_width ? cw : max_width;
    int32_t th = (int32_t)((double)tw / aspect + 0.5);
    if (th < 1) th = 1;
    rubraview_pixbuf_t small = rubraview_pixbuf_resample(arena, &cut, tw, th, RUBRAVIEW_FILTER_BILINEAR);
    if (!rubraview_pixbuf_is_valid(&small)) return none;

    /* 3. soft */
    rubraview_pixbuf_t soft = small;
    if (blur_radius > 0) {
        rubraview_pixbuf_t blurred = rubraview_filter_box_blur(arena, &small, blur_radius);
        if (rubraview_pixbuf_is_valid(&blurred)) soft = blurred;
    }

    /* 4 and 5. darker, and opaque; the colour bytes are the same three
       whichever order they are in, and alpha is the fourth either way. */
    if (keep < 0.0) keep = 0.0;
    if (keep > 1.0) keep = 1.0;
    uint32_t k = (uint32_t)(keep * 256.0 + 0.5);
    for (int32_t y = 0; y < soft.height; ++y) {
        uint8_t *row = soft.pixels + (ptrdiff_t)y * soft.stride;
        for (int32_t x = 0; x < soft.width; ++x) {
            uint8_t *p = row + x * 4;
            for (int c = 0; c < 3; ++c) {
                uint32_t v = ((uint32_t)p[c] * k) >> 8;
                p[c] = (uint8_t)(v > 255 ? 255 : v);
            }
            p[3] = 255;
        }
    }
    return soft;
}
