#ifndef RUBRAVIEW_PAL_IMAGE_H
#define RUBRAVIEW_PAL_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"
#include "rubraview/animation.h"
#include "rubraview/pal/pal_render.h"
#include "rubraview/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Native image codec PAL (RFC-0001 §8.2, §4.1.1). The Windows backend is
 * the Windows Imaging Component: it decodes JPEG, PNG, GIF, WebP, TIFF,
 * BMP and ICO with no third-party image library, converts to
 * 32bppPBGRA, and hands the frame to Direct2D via
 * CreateBitmapFromWicBitmap so the pixels land in GPU memory directly.
 *
 * Decoding produces a renderer-owned texture rather than a
 * rubraview_pixbuf_t: the viewer's hot path never needs the pixels on
 * the CPU (§4.1.1). The editing workbench that does need them (M6)
 * reads them back separately.
 */

typedef struct rubraview_image_load_result {
    rubraview_texture_t *texture;  /* NULL when ok == false */
    int32_t width, height;         /* after any EXIF orientation transform */
    int32_t exif_orientation;      /* the tag value found (1-8); 1 when absent */
    bool    ok;
} rubraview_image_load_result_t;

/**
 * Decode a file into a GPU texture. When `apply_exif_orientation` is
 * true the EXIF 0x0112 tag (RV-029) is honoured during decode, so the
 * returned texture is already upright and `width`/`height` describe the
 * rotated result.
 */
rubraview_image_load_result_t rubraview_pal_image_load_texture(rubraview_renderer_t *renderer,
                                                               u8str_t path,
                                                               bool apply_exif_orientation);

/**
 * Same, from a memory buffer — the path CBZ pages take (RV-045, M4),
 * where the bytes come from the archive VFS and never touch the disk.
 */
rubraview_image_load_result_t rubraview_pal_image_load_texture_from_memory(rubraview_renderer_t *renderer,
                                                                           const uint8_t *data,
                                                                           size_t size,
                                                                           bool apply_exif_orientation);

/**
 * §3.20: how many frames or sub-pages a file holds — animation frames
 * for GIF/WebP/APNG, pages for a multi-page TIFF, mipmaps for an ICO —
 * and what each of them is: its delay, and its size, which is what tells
 * an ICO's mipmaps apart.
 *
 * A page inside an archive has no path, so the source is given either
 * way: pass a path, or pass the bytes with an empty path. Up to `cap`
 * frames are described; the return value is the true count, which may be
 * larger. Returns 1 for an ordinary image and 0 if it cannot be read.
 */
size_t rubraview_pal_image_frame_info(u8str_t path,
                                      const uint8_t *data, size_t size,
                                      rubraview_frame_t *out_frames, size_t cap);

/**
 * §3.20: decode one frame of a multi-frame file, from a path or from
 * bytes, and report the frame's own delay so the animation clock can
 * pace it (0 when the container states none, or for sub-page formats
 * that do not animate).
 */
rubraview_image_load_result_t rubraview_pal_image_load_frame(rubraview_renderer_t *renderer,
                                                             u8str_t path,
                                                             const uint8_t *data, size_t size,
                                                             size_t frame_index,
                                                             bool apply_exif_orientation,
                                                             double *out_delay_seconds);

/**
 * §3.13's commit layer needs the pixels on the CPU, which the viewing
 * path deliberately never does. This reads one decoded image back into
 * a pixel buffer in `arena` — a folder file by path, or an archive page
 * from its bytes.
 */
rubraview_pixbuf_t rubraview_pal_image_read_pixels(proven_arena_t *arena,
                                                   u8str_t path,
                                                   const uint8_t *data, size_t size,
                                                   bool apply_exif_orientation);

/**
 * §3.10: encode a pixel buffer and write it to `path`. The format and
 * its quality settings come from the export options, which were already
 * clamped and decided by `rubraview_export_*` — this call does no
 * deciding, it only asks WIC for what was chosen.
 *
 * `privacy_clean` is honoured by construction rather than by stripping:
 * the encoder is given pixels and no metadata, so there is none to
 * carry over.
 */
bool rubraview_pal_image_save(u8str_t path,
                              const rubraview_pixbuf_t *pixels,
                              const rubraview_export_options_t *options);

/**
 * §3.10's multi-size ICO. The same image is written at each of the sizes
 * `rubraview_export_ico_sizes` names, into one file.
 */
bool rubraview_pal_image_save_ico(u8str_t path,
                                  const rubraview_pixbuf_t *pixels,
                                  const int32_t *sizes, size_t size_count);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_IMAGE_H */
