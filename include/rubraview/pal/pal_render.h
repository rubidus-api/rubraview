#ifndef RUBRAVIEW_PAL_RENDER_H
#define RUBRAVIEW_PAL_RENDER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"
#include "rubraview/viewport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 2D canvas PAL (RFC-0001 §8.2, §4.1). The Windows backend is Direct2D:
 * an image lives as an ID2D1Bitmap resident in GPU memory and pan/zoom
 * is a transform update, never a CPU rescale (§4.1.1-§4.1.2). Textures
 * and the renderer are opaque handles; the transform is the portable
 * rubraview_mat3x2_t that rubraview_viewport_matrix (RV-022) produces.
 */

typedef struct rubraview_renderer rubraview_renderer_t;
typedef struct rubraview_texture rubraview_texture_t;

/** §3.5: interpolation choice, including the crisp mode pixel art needs. */
typedef enum rubraview_interpolation {
    RUBRAVIEW_INTERP_NEAREST = 0,      /* pixel-art / integer scaling: no smoothing */
    RUBRAVIEW_INTERP_LINEAR,
    RUBRAVIEW_INTERP_CUBIC,
    RUBRAVIEW_INTERP_HIGH_QUALITY_CUBIC,
} rubraview_interpolation_t;

/** A sub-rectangle of a texture, used for §3.3.7 spread half-splitting. */
typedef struct rubraview_src_rect {
    double left, top, right, bottom;
} rubraview_src_rect_t;

/**
 * Why the renderer would not start.
 *
 * A viewer that shows nothing is the least informative failure there is,
 * and "it does not render" cannot be debugged from another machine. So
 * every step of bringing the graphics device up records itself, and this
 * reports the one that failed — with the operating system's own error
 * code, which is the part that actually identifies the cause.
 *
 * Returns an empty slice when nothing has failed.
 */
u8str_t rubraview_pal_render_last_error(void);

/** The same, as a number: the HRESULT the OS returned, or 0. */
uint32_t rubraview_pal_render_last_hresult(void);

/**
 * What the renderer ended up using — driver kind, feature level, buffer
 * format. Written into `buffer`. Useful in a bug report, and the
 * difference between "the GPU path" and "the software fallback" is
 * exactly the sort of thing that explains a slow or wrong picture.
 */
u8str_t rubraview_pal_render_describe(rubraview_renderer_t *renderer, char *buffer, size_t buffer_size);

rubraview_renderer_t *rubraview_pal_render_create(proven_arena_t *arena, void *native_window_handle, int32_t width, int32_t height);
void rubraview_pal_render_destroy(rubraview_renderer_t *renderer);

/** Resize the back buffer after RUBRAVIEW_WINDOW_EVENT_RESIZE. */
bool rubraview_pal_render_resize(rubraview_renderer_t *renderer, int32_t width, int32_t height);

void rubraview_pal_render_begin(rubraview_renderer_t *renderer, uint32_t clear_argb);

/**
 * Present the frame. Returns false when the device was lost and the
 * renderer's resources must be recreated (the caller drops its textures
 * and reloads); true on a normal present.
 */
bool rubraview_pal_render_end(rubraview_renderer_t *renderer);

/**
 * Draw a whole texture under `transform` (typically from
 * rubraview_viewport_matrix). The transform maps texture pixel
 * coordinates to client-area pixels.
 */
void rubraview_pal_render_draw_texture(rubraview_renderer_t *renderer,
                                       const rubraview_texture_t *texture,
                                       rubraview_mat3x2_t transform,
                                       rubraview_interpolation_t interpolation);

/** Draw only `src` of the texture — the half-page case from RV-021. */
/**
 * The same, with an opacity: what the slide show's cross-fade needs
 * (§3.2.5). Drawing the outgoing page and then the incoming one over it
 * at rising opacity is the whole transition. Direct2D 1.0 could not do
 * it; the device context (RV-064) can.
 */
void rubraview_pal_render_draw_texture_opacity(rubraview_renderer_t *renderer,
                                               const rubraview_texture_t *texture,
                                               rubraview_mat3x2_t transform,
                                               rubraview_interpolation_t interpolation,
                                               double opacity);

void rubraview_pal_render_draw_texture_region(rubraview_renderer_t *renderer,
                                              const rubraview_texture_t *texture,
                                              rubraview_src_rect_t src,
                                              rubraview_mat3x2_t transform,
                                              rubraview_interpolation_t interpolation);

/**
 * §3.5: 1-pixel hairline grid over texture pixel boundaries, drawn only
 * when the on-screen scale is large enough to be useful (the caller
 * applies the >= 400% rule). `transform` is the same one used to draw
 * the texture.
 */
void rubraview_pal_render_draw_pixel_grid(rubraview_renderer_t *renderer,
                                          rubraview_mat3x2_t transform,
                                          int32_t texture_width,
                                          int32_t texture_height,
                                          uint32_t line_argb);

/**
 * A texture filled from CPU memory — what a video frame needs, since it
 * arrives as pixels rather than as a file. 32-bit BGRA; the alpha byte is
 * ignored, because decoders leave it undefined.
 */
rubraview_texture_t *rubraview_pal_texture_create_bgra(rubraview_renderer_t *renderer,
                                                       int32_t width, int32_t height);

/** Replace a BGRA texture's pixels. `stride` is bytes per row, top row first. */
bool rubraview_pal_texture_upload_bgra(rubraview_texture_t *texture,
                                       const uint8_t *pixels, int32_t stride);

/*
 * RV-062, a film decoded on the graphics card. The renderer lends its
 * device to the decoder (an opaque pointer here; D-1 keeps Win32 types out
 * of PAL headers) and says how many decoder profiles the card offers — 0
 * means the device must not be lent (T065: attaching it then breaks the
 * decode). A film's texture made by create_video lives on the card, and a
 * decoded frame is copied into it there, never through system memory.
 */
void *rubraview_pal_render_video_device(rubraview_renderer_t *renderer, uint32_t *out_decoder_profiles);
rubraview_texture_t *rubraview_pal_texture_create_video(rubraview_renderer_t *renderer,
                                                        int32_t width, int32_t height);
/** False when the frame was decoded on another device, or the texture is not a video one. */
bool rubraview_pal_texture_copy_video_frame(rubraview_texture_t *texture, const void *frame_texture,
                                            uint32_t subresource);

void rubraview_pal_texture_size(const rubraview_texture_t *texture, int32_t *out_width, int32_t *out_height);
void rubraview_pal_texture_destroy(rubraview_texture_t *texture);

/* ---- UI chrome primitives (M3) ----
 *
 * The floating boxes, OSD, titlebar and filmstrip (§3.6, §3.1, §3.21)
 * are drawn from flat rectangles and DirectWrite text — deliberately no
 * gradients or animation loops, since §3.6.4 requires the overlay to
 * stay cheap to encode over Remote Desktop.
 */

typedef struct rubraview_pal_rect {
    double x, y, width, height;
} rubraview_pal_rect_t;

/** Filled rectangle in client pixels, `corner_radius` 0 for square Metro tiles. */
void rubraview_pal_render_fill_rect(rubraview_renderer_t *renderer,
                                    rubraview_pal_rect_t rect,
                                    uint32_t argb,
                                    double corner_radius);

/** 1-pixel outline, used for the crisp high-contrast tile borders (§3.6.4). */
void rubraview_pal_render_stroke_rect(rubraview_renderer_t *renderer,
                                      rubraview_pal_rect_t rect,
                                      uint32_t argb,
                                      double stroke_width,
                                      double corner_radius);

typedef enum rubraview_text_align {
    RUBRAVIEW_TEXT_LEFT = 0,
    RUBRAVIEW_TEXT_CENTER,
    RUBRAVIEW_TEXT_RIGHT,
} rubraview_text_align_t;

/**
 * Draw UTF-8 text inside `rect`, vertically centred. Returns false when
 * no text backend is available. `text` need not be NUL-terminated.
 */
bool rubraview_pal_render_draw_text(rubraview_renderer_t *renderer,
                                    u8str_t text,
                                    rubraview_pal_rect_t rect,
                                    double font_size,
                                    uint32_t argb,
                                    rubraview_text_align_t align);

/**
 * §3.22 / D-13: the settings window is a grid of fixed-width cells. This
 * is how big one cell is at `font_size` in the fixed-width face — the
 * width of one character and the height of one line.
 */
bool rubraview_pal_render_mono_cell(rubraview_renderer_t *renderer, double font_size,
                                    double *out_width, double *out_height);

/** One line of fixed-width text, its top-left at (x, y), never wrapped. */
bool rubraview_pal_render_draw_text_mono(rubraview_renderer_t *renderer, u8str_t text,
                                         double x, double y, double font_size, uint32_t argb);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_RENDER_H */
