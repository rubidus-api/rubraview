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

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_RENDER_H */
