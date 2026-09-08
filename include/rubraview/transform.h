#ifndef RUBRAVIEW_TRANSFORM_H
#define RUBRAVIEW_TRANSFORM_H

#include "rubraview/core.h"
#include "rubraview/viewport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Non-destructive geometric transforms (RFC-0001 §3.9). Rotation and
 * flipping are held as orientation state and folded into the draw
 * transform — the pixels are never re-encoded, and nothing is written
 * back to disk (that is RV-069's lossless JPEG path in M6).
 *
 * The state composes with the EXIF orientation applied at decode
 * (RV-014/RV-029): the decoder hands over an already-upright image, and
 * this is the further rotation the reader asks for on top.
 */

typedef enum rubraview_rotation {
    RUBRAVIEW_ROTATE_0 = 0,
    RUBRAVIEW_ROTATE_90,   /* clockwise */
    RUBRAVIEW_ROTATE_180,
    RUBRAVIEW_ROTATE_270,
} rubraview_rotation_t;

typedef struct rubraview_orientation {
    rubraview_rotation_t rotation;
    bool flip_horizontal;
    bool flip_vertical;
} rubraview_orientation_t;

static inline rubraview_orientation_t rubraview_orientation_identity(void) {
    return (rubraview_orientation_t){ .rotation = RUBRAVIEW_ROTATE_0, .flip_horizontal = false, .flip_vertical = false };
}

/** Rotate 90 degrees clockwise (the `R` key), wrapping at 360. */
rubraview_orientation_t rubraview_orientation_rotate_cw(rubraview_orientation_t o);

/** Rotate 90 degrees counter-clockwise (`Shift+R`). */
rubraview_orientation_t rubraview_orientation_rotate_ccw(rubraview_orientation_t o);

rubraview_orientation_t rubraview_orientation_flip_h(rubraview_orientation_t o);
rubraview_orientation_t rubraview_orientation_flip_v(rubraview_orientation_t o);

/** True when the orientation swaps width and height (90 or 270). */
bool rubraview_orientation_swaps_axes(rubraview_orientation_t o);

/**
 * The size the image presents after the orientation is applied — what
 * the fit and layout maths must use, since a rotated portrait page fits
 * the window as a landscape one.
 */
void rubraview_orientation_apply_size(rubraview_orientation_t o, double width, double height,
                                      double *out_width, double *out_height);

/**
 * The transform mapping the source image's own pixel space onto the
 * oriented space whose extent is rubraview_orientation_apply_size. Feed
 * the result into the viewport/compositor transform:
 *   final = orientation_matrix . viewport_matrix
 */
rubraview_mat3x2_t rubraview_orientation_matrix(rubraview_orientation_t o, double width, double height);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_TRANSFORM_H */
