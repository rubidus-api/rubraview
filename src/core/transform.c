#include "rubraview/transform.h"

bool rubraview_orientation_swaps_axes(rubraview_orientation_t o) {
    return o.rotation == RUBRAVIEW_ROTATE_90 || o.rotation == RUBRAVIEW_ROTATE_270;
}

void rubraview_orientation_apply_size(rubraview_orientation_t o, double width, double height,
                                      double *out_width, double *out_height) {
    /* Flipping never changes the extent; only a quarter turn does. */
    if (rubraview_orientation_swaps_axes(o)) {
        if (out_width) *out_width = height;
        if (out_height) *out_height = width;
    } else {
        if (out_width) *out_width = width;
        if (out_height) *out_height = height;
    }
}

/*
 * Rotation is stored before the flips, so with a mirror active a raw
 * increment would appear to turn the wrong way on screen. Stepping the
 * other way when exactly one flip is active keeps `R` looking clockwise
 * to the reader no matter what else is set.
 */
static rubraview_rotation_t step_rotation(rubraview_orientation_t o, int direction) {
    bool mirrored = (o.flip_horizontal != o.flip_vertical);
    if (mirrored) direction = -direction;

    int next = (int)o.rotation + direction;
    next %= 4;
    if (next < 0) next += 4;
    return (rubraview_rotation_t)next;
}

rubraview_orientation_t rubraview_orientation_rotate_cw(rubraview_orientation_t o) {
    o.rotation = step_rotation(o, 1);
    return o;
}

rubraview_orientation_t rubraview_orientation_rotate_ccw(rubraview_orientation_t o) {
    o.rotation = step_rotation(o, -1);
    return o;
}

rubraview_orientation_t rubraview_orientation_flip_h(rubraview_orientation_t o) {
    o.flip_horizontal = !o.flip_horizontal;
    return o;
}

rubraview_orientation_t rubraview_orientation_flip_v(rubraview_orientation_t o) {
    o.flip_vertical = !o.flip_vertical;
    return o;
}

rubraview_mat3x2_t rubraview_orientation_matrix(rubraview_orientation_t o, double width, double height) {
    /* Rotation maps the source rectangle onto the oriented rectangle,
       keeping the result in the positive quadrant. */
    rubraview_mat3x2_t rotation;
    switch (o.rotation) {
        case RUBRAVIEW_ROTATE_90:
            /* (x,y) -> (h - y, x) */
            rotation = (rubraview_mat3x2_t){ .a = 0.0, .b = 1.0, .c = -1.0, .d = 0.0, .e = height, .f = 0.0 };
            break;
        case RUBRAVIEW_ROTATE_180:
            /* (x,y) -> (w - x, h - y) */
            rotation = (rubraview_mat3x2_t){ .a = -1.0, .b = 0.0, .c = 0.0, .d = -1.0, .e = width, .f = height };
            break;
        case RUBRAVIEW_ROTATE_270:
            /* (x,y) -> (y, w - x) */
            rotation = (rubraview_mat3x2_t){ .a = 0.0, .b = -1.0, .c = 1.0, .d = 0.0, .e = 0.0, .f = width };
            break;
        case RUBRAVIEW_ROTATE_0:
        default:
            rotation = rubraview_mat3x2_identity();
            break;
    }

    if (!o.flip_horizontal && !o.flip_vertical) {
        return rotation;
    }

    /* Flips act in the oriented space, so they use the post-rotation extent. */
    double oriented_w = 0.0, oriented_h = 0.0;
    rubraview_orientation_apply_size(o, width, height, &oriented_w, &oriented_h);

    rubraview_mat3x2_t flip = rubraview_mat3x2_identity();
    if (o.flip_horizontal) {
        flip.a = -1.0;
        flip.e = oriented_w;
    }
    if (o.flip_vertical) {
        flip.d = -1.0;
        flip.f = oriented_h;
    }

    return rubraview_mat3x2_multiply(rotation, flip);
}

rubraview_pixbuf_t rubraview_pixbuf_orient(proven_arena_t *arena,
                                           const rubraview_pixbuf_t *src,
                                           rubraview_orientation_t orientation) {
    rubraview_pixbuf_t empty = {0};
    if (!arena || !rubraview_pixbuf_is_valid(src)) return empty;

    bool swaps = rubraview_orientation_swaps_axes(orientation);
    int32_t dst_w = swaps ? src->height : src->width;
    int32_t dst_h = swaps ? src->width : src->height;

    rubraview_pixbuf_t dst = rubraview_pixbuf_create(arena, dst_w, dst_h, src->format);
    if (!rubraview_pixbuf_is_valid(&dst)) return empty;

    int32_t bpp = rubraview_bytes_per_pixel(src->format);

    /* Walk the destination and ask where each pixel came from. Going
       this way round means every destination pixel is written exactly
       once, whatever the rotation is — the reverse mapping is where
       off-by-ones and gaps come from. */
    for (int32_t y = 0; y < dst_h; ++y) {
        uint8_t *dst_row = dst.pixels + (ptrdiff_t)y * dst.stride;
        for (int32_t x = 0; x < dst_w; ++x) {
            int32_t sx = x, sy = y;

            switch (orientation.rotation) {
                case RUBRAVIEW_ROTATE_90:  sx = y;                 sy = dst_w - 1 - x;    break;
                case RUBRAVIEW_ROTATE_180: sx = dst_w - 1 - x;     sy = dst_h - 1 - y;    break;
                case RUBRAVIEW_ROTATE_270: sx = dst_h - 1 - y;     sy = x;                break;
                case RUBRAVIEW_ROTATE_0:
                default: break;
            }

            /* The flips are applied in the source's own space, after the
               rotation has decided which source pixel this is. */
            if (orientation.flip_horizontal) sx = src->width - 1 - sx;
            if (orientation.flip_vertical)   sy = src->height - 1 - sy;

            if (sx < 0 || sy < 0 || sx >= src->width || sy >= src->height) continue;

            const uint8_t *spx = src->pixels + (ptrdiff_t)sy * src->stride + (ptrdiff_t)sx * bpp;
            uint8_t *dpx = dst_row + (ptrdiff_t)x * bpp;
            for (int32_t c = 0; c < bpp; ++c) dpx[c] = spx[c];
        }
    }

    return dst;
}
