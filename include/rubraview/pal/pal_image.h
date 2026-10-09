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
    int32_t full_width, full_height; /* the picture's own size; width/height are smaller when reduced */
    bool    reduced;               /* held smaller than the picture: too big for the device (the fall-back) */
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

/*
 * D-82: an animated GIF or WebP kept open, so its frames come one after
 * another without the file being opened again for each. Each frame is
 * given as the container holds it — a GIF's may be a part of the picture;
 * `rubraview_frame_canvas_show` lays them together. NULL for anything that
 * is not an animation of those two kinds, which then goes the way of
 * `rubraview_pal_image_frame_info` (a TIFF's pages, an ICO's sizes).
 *
 * `out_frames` gets up to `cap` frames with their delay, place and manner
 * of leaving; `out_count` how many were described; the canvas is the whole
 * picture's size. Bytes given without a path are copied.
 */
typedef struct rubraview_anim_reader rubraview_anim_reader_t;

rubraview_anim_reader_t *rubraview_pal_anim_open(u8str_t path, const uint8_t *data, size_t size,
                                                 rubraview_frame_t *out_frames, size_t cap, size_t *out_count,
                                                 int32_t *out_canvas_width, int32_t *out_canvas_height);

/** One frame as premultiplied BGRA into `dst`: `stride` bytes a row, `rows` rows of room. */
bool rubraview_pal_anim_read(rubraview_anim_reader_t *reader, size_t index, uint8_t *dst, size_t stride, int32_t rows);

void rubraview_pal_anim_close(rubraview_anim_reader_t *reader);

/**
 * Bring the imaging codecs up, once, before anything else needs them.
 *
 * This exists because of *when* the factory is created, not whether.
 * COM was started as a single-threaded apartment (the file dialog
 * requires it), and in such an apartment activating an object while a
 * window exists on the same thread that nobody is pumping messages for
 * can deadlock. The viewer opens its first file before its message loop
 * has run even once — so the first image would take the whole program
 * down with it, silently, looking exactly like "images do not render".
 *
 * Called immediately after COM starts and before any window exists,
 * there is no window to deadlock against. Safe to call more than once.
 */
bool rubraview_pal_image_startup(void);

/** A thread other than the main one that read or wrote pictures lets go of its WIC factory before it ends. */
void rubraview_pal_image_thread_end(void);

/**
 * Walk the decode path for one file and report what happened at each
 * step, into `buffer`.
 *
 * "The image does not render" is a report with no information in it: the
 * failure could be the codec, the colour transform, the conversion, or
 * the upload to the GPU, and from another machine there is no way to
 * tell which. This runs the real path — the same calls the viewer makes
 * — and says where it stopped and what the operating system said.
 */
u8str_t rubraview_pal_image_diagnose(rubraview_renderer_t *renderer, u8str_t path,
                                     char *buffer, size_t buffer_size);

/**
 * §3.13's commit layer needs the pixels on the CPU, which the viewing
 * path deliberately never does. This reads one decoded image back into
 * a pixel buffer in `arena` — a folder file by path, or an archive page
 * from its bytes.
 */
/**
 * What a picture is, for the information window (owner, 2026-09-29):
 * its format, pixel format, resolution, frames and whether it carries a
 * colour profile. Nothing is decoded. A folder page by `path`, an archive
 * page by its bytes.
 */
typedef struct rubraview_image_desc {
    char     format[32];          /* "JPEG", "PNG", "WebP", "HEIF" … */
    char     pixels[48];          /* "24-bit colour", "8-bit grey", "32-bit colour with transparency" */
    int32_t  width, height;       /* as stored */
    uint32_t bits_per_pixel, channels;
    double   dpi_x, dpi_y;
    uint32_t frames;
    bool     color_profile;
} rubraview_image_desc_t;

bool rubraview_pal_image_describe(u8str_t path, const uint8_t *data, size_t size, rubraview_image_desc_t *out);

/**
 * A picture's size as stored, without decoding its pixels — enough for a
 * batch run to give a large picture room of its own first. False when it
 * cannot be read.
 */
bool rubraview_pal_image_size(u8str_t path, int32_t *out_width, int32_t *out_height);

rubraview_pixbuf_t rubraview_pal_image_read_pixels(proven_arena_t *arena,
                                                   u8str_t path,
                                                   const uint8_t *data, size_t size,
                                                   bool apply_exif_orientation);

/**
 * The same, but no larger than `max_width` x `max_height` (the aspect
 * kept, never enlarged): the decoder is read through a scaler, so a
 * picture too large to hold whole — the adjust panel's preview of a very
 * large page — never is.
 */
rubraview_pixbuf_t rubraview_pal_image_read_pixels_within(proven_arena_t *arena,
                                                          u8str_t path,
                                                          const uint8_t *data, size_t size,
                                                          bool apply_exif_orientation,
                                                          int32_t max_width, int32_t max_height);

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

/* Settings › Display › Use embedded ICC profiles (owner, 2026-09-30): on,
   a picture with its own profile is converted to sRGB as it is decoded;
   off, its numbers are shown as they are. Applies to what is decoded next. */
void rubraview_pal_image_set_color_management(bool on);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_IMAGE_H */
