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
proven_u8str_view_t rubraview_pal_render_last_error(void);

/** The same, as a number: the HRESULT the OS returned, or 0. */
uint32_t rubraview_pal_render_last_hresult(void);

/**
 * What the renderer ended up using — driver kind, feature level, buffer
 * format. Written into `buffer`. Useful in a bug report, and the
 * difference between "the GPU path" and "the software fallback" is
 * exactly the sort of thing that explains a slow or wrong picture.
 */
proven_u8str_view_t rubraview_pal_render_describe(rubraview_renderer_t *renderer, char *buffer, size_t buffer_size);

/* D-179: how the swap chain hands a frame to the compositor. */
enum { RUBRAVIEW_PRESENT_COPY = 0, RUBRAVIEW_PRESENT_FLIP_DISCARD = 1, RUBRAVIEW_PRESENT_FLIP_SEQUENTIAL = 2 };

/* 1.25.1: what draws the window, for the information window (owner, 2026-10-20). */
typedef struct rubraview_render_facts {
    char     adapter[128];          /* the graphics card's name, UTF-8 */
    uint64_t video_memory;          /* its own memory, bytes; 0 when it has none (shared) */
    bool     warp;                  /* Windows' software rasteriser, no card */
    uint32_t feature_level;         /* D3D_FEATURE_LEVEL, e.g. 0xb100 for 11_1 */
    int32_t  buffers;               /* the swap chain's */
    bool     vsync;                 /* presented with the screen's refresh */
    int32_t  present_model;         /* RUBRAVIEW_PRESENT_* (D-179) */
    bool     decode_device;         /* a second device was made on the card for decoding */
    uint32_t decoder_profiles;      /* the video decoders that device offers */
    bool     hdr_output;            /* RV-153: the window goes out as scRGB (FP16) to an HDR screen */
    double   hdr_sdr_white;         /* ...the nits SDR's white is shown at */
    bool     screen_copies;         /* RV-166: a settled page drawn from a copy at screen size (software adapter) */
} rubraview_render_facts_t;

bool rubraview_pal_render_facts(rubraview_renderer_t *renderer, rubraview_render_facts_t *out);

/* RV-153 (D-183): the screen the window is on, as Windows sees it. */
typedef struct rubraview_hdr_display {
    bool   hdr;                     /* Windows' HDR is on for it (BT.2100 PQ) */
    double peak_nits;               /* its brightest (MaxLuminance), within 100..10000; 0 when it says nothing */
    double sdr_white_nits;          /* where Windows shows SDR's white ("SDR content brightness"); 80 when unknown */
} rubraview_hdr_display_t;
bool rubraview_pal_render_hdr_display(rubraview_renderer_t *renderer, rubraview_hdr_display_t *out);

/** Sends the window as scRGB, FP16 (on), or as before (off); `sdr_white_nits` is what the drawing's 1.0 is
    shown at. False, and `why` (an N_ literal), when it cannot: the copy model, a colour space the swap chain
    refuses, no shader. Nothing is done when nothing changes. */
bool rubraview_pal_render_set_hdr(rubraview_renderer_t *renderer, bool on, double sdr_white_nits, const char **why);

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
/** RV-153: the same in FP16, for a film sent to an HDR screen (sRGB-encoded, 1.0 the SDR white). Read back,
    uploaded or measured as BGRA, clipped at that white. */
rubraview_texture_t *rubraview_pal_texture_create_video_hdr(rubraview_renderer_t *renderer,
                                                            int32_t width, int32_t height);
/** Diagnostic (--probe-gpu): the average brightness, 0-255, of a video texture. */
bool rubraview_pal_texture_video_brightness(rubraview_texture_t *texture, double *out_brightness);

/**
 * RV-108: a film's frame that is on the graphics card, read back as BGRA
 * rows of `stride` bytes into `dst` (height rows). False for a texture
 * that is not a film's on the card — its pixels were the caller's.
 */
bool rubraview_pal_texture_read_video(rubraview_texture_t *texture, uint8_t *dst, size_t stride);

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
/**
 * RV-107: the font family draw_text and the measures use from now on —
 * a subtitle's own font. An empty name is the program's face again, and a
 * family the machine does not have is drawn in what DirectWrite falls
 * back to.
 */
void rubraview_pal_render_set_text_family(rubraview_renderer_t *renderer, proven_u8str_view_t family);

/** RV-136: the same, with a weight (100..900, 0 for 400) and italic. */
void rubraview_pal_render_set_text_font(rubraview_renderer_t *renderer, proven_u8str_view_t family, int32_t weight, bool italic);

/**
 * RV-136: the program's own face — what draw_text and the measures use
 * when no other family is set (Settings › General › UI font). An empty
 * family is Segoe UI.
 */
void rubraview_pal_render_set_ui_font(rubraview_renderer_t *renderer, proven_u8str_view_t family, int32_t weight, bool italic);

/** 1.25.0: the face of the fixed-width text (the settings window); empty is Consolas. A face whose letters are not all
    one width still lays out on the grid, its letters each in a cell. */
void rubraview_pal_render_set_mono_family(rubraview_renderer_t *renderer, proven_u8str_view_t family);
/** 1.25.0: room drawn above each fixed-width line, so a taller row holds its text in its middle. */
void rubraview_pal_render_set_mono_pad(rubraview_renderer_t *renderer, double pad);

/** One font family the machine has: its name in the user's language and its English one (stored). */
typedef struct rubraview_font_family {
    proven_u8str_view_t shown;
    proven_u8str_view_t name;
} rubraview_font_family_t;

/**
 * RV-136: the machine's font families, sorted by the name shown, into
 * `arena`; 0 when DirectWrite gives none. Vertical (`@`) faces are not listed.
 */
size_t rubraview_pal_font_families(proven_arena_t *arena, rubraview_font_family_t **out);

bool rubraview_pal_render_draw_text(rubraview_renderer_t *renderer,
                                    proven_u8str_view_t text,
                                    rubraview_pal_rect_t rect,
                                    double font_size,
                                    uint32_t argb,
                                    rubraview_text_align_t align);

/**
 * Draw one icon, centred in `rect`: a code point of the Segoe MDL2 Assets
 * font that Windows 10 and 11 carry (the toolbox's buttons, the boxes'
 * pins). False when that font is not on this machine; the caller then
 * draws a caption instead.
 */
bool rubraview_pal_render_draw_icon(rubraview_renderer_t *renderer, uint32_t code_point,
                                    rubraview_pal_rect_t rect, double size, uint32_t argb);

/**
 * §3.22 / D-13: the settings window is a grid of fixed-width cells. This
 * is how big one cell is at `font_size` in the fixed-width face — the
 * width of one character and the height of one line.
 */
/* How wide `text` is in the proportional face draw_text uses, on one line
   (trailing spaces counted) — what a text field needs to put its caret and
   its selection under the right characters. False when it cannot tell. */
/* The next frame's present is treated as a lost device: the renderer is
   rebuilt and the caller told to reload, exactly as after a driver reset.
   A test's trigger — a real loss cannot be caused on demand. */
void rubraview_pal_render_force_lost(rubraview_renderer_t *renderer);

bool rubraview_pal_render_measure_text(rubraview_renderer_t *renderer, proven_u8str_view_t text, double font_size,
                                       double *out_width);

/* How wide and tall `text` is as draw_text lays it out in a rectangle
   `wrap_width` wide: wrapped there, its own line ends kept (D-92: the
   subtitle box is made to hold its text). False when it cannot tell. */
bool rubraview_pal_render_measure_text_wrapped(rubraview_renderer_t *renderer, proven_u8str_view_t text, double font_size,
                                               double wrap_width, double *out_width, double *out_height);

bool rubraview_pal_render_mono_cell(rubraview_renderer_t *renderer, double font_size,
                                    double *out_width, double *out_height);

/* ---- RFC-0005: a paragraph of a text file, laid out once and drawn often ---- */

typedef struct rubraview_text_style {
    proven_u8str_view_t family;        /* "" for the program's face */
    double  size;          /* in DIPs */
    double  line_spacing;  /* a line's height as a multiple of the size (1.5 = 150 %) */
    double  width;         /* the column's width: lines wrap there when `wrap` */
    bool    wrap;
    int32_t weight;        /* RV-136: 100..900, 0 for 400 */
    bool    italic;
} rubraview_text_style_t;

typedef struct rubraview_text_block rubraview_text_block_t;

/* A mark drawn under part of a block: a selection, a match. Byte offsets in the block's UTF-8. */
typedef struct rubraview_text_mark {
    size_t   start, len;
    uint32_t argb;
} rubraview_text_mark_t;

/** `text` (UTF-8, one paragraph) laid out by DirectWrite; NULL when it cannot be. Kept across a lost device. */
rubraview_text_block_t *rubraview_pal_text_block_make(rubraview_renderer_t *renderer, proven_u8str_view_t text,
                                                      const rubraview_text_style_t *style);
void rubraview_pal_text_block_free(rubraview_text_block_t *block);
/** Its size as laid out: the widest line, and the height of all its lines. */
void rubraview_pal_text_block_size(const rubraview_text_block_t *block, double *width, double *height);
/** Draw it with its top-left at (x, y), the marks under it first. */
void rubraview_pal_text_block_draw(rubraview_renderer_t *renderer, const rubraview_text_block_t *block,
                                   double x, double y, uint32_t argb,
                                   const rubraview_text_mark_t *marks, size_t mark_count);
/** 1.25.0: as `draw`, only what lies above `y + height` (the rest left to the block after it). */
void rubraview_pal_text_block_draw_upto(rubraview_renderer_t *renderer, const rubraview_text_block_t *block,
                                        double x, double y, double height, uint32_t argb,
                                        const rubraview_text_mark_t *marks, size_t mark_count);
/** 1.25.0: where its last line starts (a byte offset) and how far down it is; false for an empty block. */
bool rubraview_pal_text_block_last_line(const rubraview_text_block_t *block, size_t *start, double *top);
/** The byte offset nearest a point given from the block's top-left. */
size_t rubraview_pal_text_block_hit(const rubraview_text_block_t *block, double x, double y);
/** The top and height of the line that holds byte offset `at`. */
bool rubraview_pal_text_block_line_of(const rubraview_text_block_t *block, size_t at, double *top, double *height);

/** One line of fixed-width text, its top-left at (x, y), never wrapped. */
bool rubraview_pal_render_draw_text_mono(rubraview_renderer_t *renderer, proven_u8str_view_t text,
                                         double x, double y, double font_size, uint32_t argb);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_RENDER_H */
