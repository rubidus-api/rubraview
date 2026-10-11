#ifndef RUBRAVIEW_AVIF_READ_H
#define RUBRAVIEW_AVIF_READ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reading AVIF (RV-110, D-107): the vendored libavif with dav1d under it,
 * for a Windows that has no AV1 codec, and for an AVIF that moves — which
 * Windows' own codec shows as one still picture. Nothing here touches a
 * file or the operating system.
 *
 * A picture of 10 or 12 bits is given as 8; a wide-gamut or HDR one is
 * converted by its matrix alone, with no tone mapping.
 */
typedef struct rubraview_avif rubraview_avif_t;

/** Whether the bytes begin as an AVIF file does (`ftyp` with the brand avif or avis). */
bool rubraview_avif_is(const uint8_t *bytes, size_t size);

/**
 * Open an AVIF held in memory; the bytes must stay where they are until
 * it is closed. NULL when it is not one, is damaged, or is larger than
 * `max_side` on a side. An animation has more than one frame.
 */
rubraview_avif_t *rubraview_avif_open(const uint8_t *bytes, size_t size, int32_t max_side,
                                      int32_t *out_width, int32_t *out_height, size_t *out_frames);

/**
 * Decode frame `index` as BGRA, top row first, into `dst` (`stride` bytes
 * a row, at least width * 4; height rows), premultiplied or not, and say
 * how long it is shown (0 for a still picture).
 */
bool rubraview_avif_frame(rubraview_avif_t *avif, size_t index, uint8_t *dst, size_t stride,
                          bool premultiplied, double *out_seconds);

/**
 * How the picture is to be turned to stand upright, as an Exif orientation
 * (1..8; 1: as it is), from the container's own rotation and mirroring.
 * The pixels are given as stored: the caller turns them.
 */
int32_t rubraview_avif_orientation(const rubraview_avif_t *avif);

/** How long frame `index` is shown, read from the container alone; 0 when it says nothing. */
double rubraview_avif_frame_seconds(rubraview_avif_t *avif, size_t index);

void rubraview_avif_close(rubraview_avif_t *avif);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_AVIF_READ_H */
