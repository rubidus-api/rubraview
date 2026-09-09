#ifndef RUBRAVIEW_JPEGTRAN_H
#define RUBRAVIEW_JPEGTRAN_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lossless JPEG rotation and flipping (RFC-0001 §3.9, owner decision
 * D-5), RV-069.
 *
 * Rotating a JPEG the ordinary way — decode to pixels, rotate, encode
 * again — loses quality every time, because the second encode quantises
 * a picture that has already been quantised once. A JPEG can instead be
 * rotated by rearranging its *DCT coefficient blocks*, which touches no
 * pixel at all: the same coefficients come out, in a different order.
 * That is what this does, through the vendored libjpeg-turbo's
 * coefficient API — the `jpegtran` path.
 *
 * WIC remains the only pixel codec in this program (D-5). Nothing here
 * decodes an image; it reads coefficients, moves them, and writes them.
 *
 * The one honest limitation is the format's own: a rotation is exact
 * only when the image's dimensions are whole multiples of the MCU size
 * (usually 8 or 16 pixels). Otherwise the edge strip cannot be rotated
 * without inventing data, and the caller has to choose — see
 * `rubraview_jpegtran_edge_t`.
 */

typedef enum rubraview_jpegtran_op {
    RUBRAVIEW_JPEGTRAN_NONE = 0,
    RUBRAVIEW_JPEGTRAN_ROT_90,
    RUBRAVIEW_JPEGTRAN_ROT_180,
    RUBRAVIEW_JPEGTRAN_ROT_270,
    RUBRAVIEW_JPEGTRAN_FLIP_H,
    RUBRAVIEW_JPEGTRAN_FLIP_V,
    RUBRAVIEW_JPEGTRAN_TRANSPOSE,
    RUBRAVIEW_JPEGTRAN_TRANSVERSE,
} rubraview_jpegtran_op_t;

typedef enum rubraview_jpegtran_edge {
    /** Trim the partial edge blocks away. The result is exact, and a few
        pixels smaller. This is what `jpegtran -trim` does. */
    RUBRAVIEW_JPEGTRAN_TRIM = 0,
    /** Keep every pixel. The partial edge is copied rather than rotated,
        which is what plain `jpegtran` does, and is not exact there. */
    RUBRAVIEW_JPEGTRAN_KEEP_EDGE,
    /** Refuse rather than do either: for a caller that would rather fall
        back to a re-encode than hand back a subtly wrong image. */
    RUBRAVIEW_JPEGTRAN_REFUSE_IF_INEXACT,
} rubraview_jpegtran_edge_t;

typedef enum rubraview_jpegtran_err {
    RUBRAVIEW_JPEGTRAN_OK = 0,
    RUBRAVIEW_JPEGTRAN_ERR_NOT_A_JPEG,
    RUBRAVIEW_JPEGTRAN_ERR_CORRUPT,
    RUBRAVIEW_JPEGTRAN_ERR_UNSUPPORTED,   /* e.g. arithmetic coding, which is not built in */
    RUBRAVIEW_JPEGTRAN_ERR_INEXACT,       /* REFUSE_IF_INEXACT, and it would have been */
    RUBRAVIEW_JPEGTRAN_ERR_OUT_OF_MEMORY,
} rubraview_jpegtran_err_t;

typedef struct rubraview_jpegtran_result {
    rubraview_jpegtran_err_t err;
    u8str_t data;      /* the new JPEG, in the caller's arena */
    bool    was_exact; /* false when an edge strip had to be copied rather than rotated */
} rubraview_jpegtran_result_t;

/**
 * Whether a transform can be exact for an image of this size — i.e.
 * whether the dimensions divide by the MCU block size the operation
 * needs. A 90-degree rotation needs both; a 180-degree one needs both;
 * a horizontal flip needs only the width.
 */
bool rubraview_jpegtran_is_exact(rubraview_jpegtran_op_t op,
                                 int32_t width, int32_t height,
                                 int32_t mcu_width, int32_t mcu_height);

/**
 * Read a JPEG's coefficients, apply the transform, and write a new JPEG.
 * `privacy_clean` drops the metadata markers while it is at it, which is
 * §3.10's zero-touch strip arriving for free — the markers are simply
 * not copied across.
 */
rubraview_jpegtran_result_t rubraview_jpegtran_apply(proven_arena_t *arena,
                                                     const uint8_t *data, size_t size,
                                                     rubraview_jpegtran_op_t op,
                                                     rubraview_jpegtran_edge_t edge_policy,
                                                     bool privacy_clean);

/**
 * §3.10's zero-touch strip on its own: the same JPEG with its EXIF, XMP
 * and IPTC segments removed and every DCT coefficient untouched.
 */
rubraview_jpegtran_result_t rubraview_jpegtran_strip_metadata(proven_arena_t *arena,
                                                              const uint8_t *data, size_t size);

/** Read a JPEG's size and MCU geometry without decoding it. */
bool rubraview_jpegtran_probe(const uint8_t *data, size_t size,
                              int32_t *out_width, int32_t *out_height,
                              int32_t *out_mcu_width, int32_t *out_mcu_height);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_JPEGTRAN_H */
