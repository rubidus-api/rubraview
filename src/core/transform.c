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
