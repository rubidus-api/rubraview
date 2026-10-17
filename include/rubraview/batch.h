#ifndef RUBRAVIEW_BATCH_H
#define RUBRAVIEW_BATCH_H

#include "rubraview/core.h"
#include "rubraview/color.h"
#include "rubraview/resample.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Batch job data model (RFC-0001 §3.11): the action chain, file filter,
 * and output naming pattern, with no execution engine or threads —
 * RV-017 (M6) builds the worker pool that runs this model.
 */

typedef enum rubraview_batch_action_kind {
    RUBRAVIEW_BATCH_ORIENT = 0,
    RUBRAVIEW_BATCH_RESIZE,
    RUBRAVIEW_BATCH_COLOR_ADJUST,
    RUBRAVIEW_BATCH_PRIVACY_SCRUB,
    RUBRAVIEW_BATCH_CONVERT,
    RUBRAVIEW_BATCH_EXTRA,          /* RV-129: crop, colour, effect, border, canvas, watermark */
} rubraview_batch_action_kind_t;

typedef struct rubraview_batch_orient_params {
    int32_t rotate_degrees;        /* 0, 90, 180, or 270 */
    bool    flip_horizontal;
    bool    flip_vertical;
    bool    use_exif_auto_orient;  /* if true, ignore the fields above and apply the EXIF 0x0112 tag (RV-029) instead */
} rubraview_batch_orient_params_t;

typedef enum rubraview_batch_resize_mode {
    RUBRAVIEW_RESIZE_PERCENT = 0,   /* value_a: percentage, e.g. 50.0 */
    RUBRAVIEW_RESIZE_BOUNDING_BOX,  /* value_a/value_b: max width/height, aspect preserved */
    RUBRAVIEW_RESIZE_FIXED_WIDTH,   /* value_a: target width */
    RUBRAVIEW_RESIZE_FIXED_HEIGHT,  /* value_a: target height */
    /* RV-128/129 (owner, 2026-10-17) */
    RUBRAVIEW_RESIZE_LONG_SIDE,     /* value_a: the longer side */
    RUBRAVIEW_RESIZE_SHORT_SIDE,    /* value_a: the shorter side */
    RUBRAVIEW_RESIZE_MEGAPIXELS,    /* value_a: megapixels, the shape kept */
} rubraview_batch_resize_mode_t;

typedef struct rubraview_batch_resize_params {
    rubraview_batch_resize_mode_t mode;
    double value_a, value_b;
    rubraview_resample_filter_t filter; /* rubraview/resample.h */
    bool no_enlarge;                    /* RV-129: a picture already smaller is left its size */
    bool linear;                        /* RV-128: resampled in linear light */
} rubraview_batch_resize_params_t;

/**
 * RV-129: the size `r` gives a `src_w` x `src_h` picture (at least 1 x 1).
 * A bounding box never enlarges; with `no_enlarge` no mode does.
 */
void rubraview_batch_resize_target(const rubraview_batch_resize_params_t *r, int32_t src_w, int32_t src_h,
                                   int32_t *out_w, int32_t *out_h);

/* RV-129 (owner, 2026-10-17: "배치 처리 ... 그 이상"): what IrfanView's
   batch does beside a resize, and more. Applied in this order. */
typedef enum rubraview_batch_corner {
    RUBRAVIEW_CORNER_BOTTOM_RIGHT = 0, RUBRAVIEW_CORNER_BOTTOM_LEFT, RUBRAVIEW_CORNER_TOP_RIGHT,
    RUBRAVIEW_CORNER_TOP_LEFT, RUBRAVIEW_CORNER_CENTRE,
} rubraview_batch_corner_t;

typedef struct rubraview_batch_extra_params {
    bool     crop;                 /* first: this rectangle, held inside the picture */
    int32_t  crop_x, crop_y, crop_w, crop_h;
    bool     autocrop;             /* the plain border (a scan's white, a letterbox's black) cut off */
    bool     auto_colour;          /* the levels stretched per channel, the cast drawn in */
    bool     negative;
    int32_t  effect;               /* rubraview/effects.h; 0 none */
    double   effect_amount;
    int32_t  canvas_w, canvas_h;   /* > 0: the picture centred on a canvas this size (never cut) */
    uint32_t canvas_rgb;
    int32_t  border_px;            /* > 0: a frame this wide round it */
    uint32_t border_rgb;
    u8str_t  watermark;            /* a picture's path, laid over the result */
    rubraview_batch_corner_t watermark_corner;
    int32_t  watermark_opacity;    /* 1 .. 100 */
    /* RV-134: words drawn on each picture ({name} {ext} {n} {n:3} {parent} {date} {w} {h}), last of all */
    u8str_t  text;
    u8str_t  text_font;            /* a family Windows has; empty: Segoe UI */
    rubraview_batch_corner_t text_corner;
    int32_t  text_size;            /* the letters' height, % of the picture's shorter side, 1 .. 50 */
    uint32_t text_rgb;
} rubraview_batch_extra_params_t;

/**
 * RV-134: a mask of text (its alpha in every pixel, colour 0xRRGGBB) made
 * into a picture to lay on: the letters at `opacity`, with a soft dark
 * shadow under them so they read on any picture. `mask` is GRAY8 coverage;
 * the result is `format` (RGBA8 or BGRA8), its own alpha carrying it.
 */
rubraview_pixbuf_t rubraview_batch_text_layer(proven_arena_t *arena, const rubraview_pixbuf_t *mask, uint32_t rgb,
                                              rubraview_pixel_format_t format);

/** A frame `px` wide of colour 0xRRGGBB round `src` (a new picture). */
rubraview_pixbuf_t rubraview_batch_border(proven_arena_t *arena, const rubraview_pixbuf_t *src, int32_t px, uint32_t rgb);
/** `src` centred on a `w` x `h` canvas of 0xRRGGBB, each side at least the picture's. */
rubraview_pixbuf_t rubraview_batch_canvas(proven_arena_t *arena, const rubraview_pixbuf_t *src, int32_t w, int32_t h, uint32_t rgb);
/**
 * `mark` laid over `dst` in place, at its corner (a margin of 2 % of the
 * shorter side), at `opacity` percent, its own alpha kept. A mark wider
 * than a quarter of `dst` is drawn smaller to that width. Both RGBA8 or
 * both BGRA8.
 */
bool rubraview_batch_watermark(proven_arena_t *arena, rubraview_pixbuf_t *dst, const rubraview_pixbuf_t *mark,
                               rubraview_batch_corner_t corner, int32_t opacity);
/** RV-134: the same with the widest the mark may be drawn (`max_width` pixels; a text layer may take the picture's width). */
bool rubraview_batch_overlay(proven_arena_t *arena, rubraview_pixbuf_t *dst, const rubraview_pixbuf_t *mark,
                             rubraview_batch_corner_t corner, int32_t opacity, int32_t max_width);

typedef struct rubraview_batch_color_params {
    bool has_color_adjust;
    rubraview_color_adjust_params_t adjust; /* rubraview/color.h: exposure/contrast/saturation/gamma */
    bool has_unsharp;
    float sigma, amount;
    uint8_t threshold;
    bool grayscale;
} rubraview_batch_color_params_t;

typedef struct rubraview_batch_privacy_params {
    bool strip_all_exif; /* GPS, camera serial, and every other EXIF tag together (RV-029 strips the whole APP1/Exif segment) */
    bool strip_xmp;
    bool strip_iptc;
} rubraview_batch_privacy_params_t;

typedef struct rubraview_batch_convert_params {
    u8str_t target_ext; /* e.g. "jpg", "png", "webp" */
    int32_t quality;    /* 1-100; meaning is format-dependent */
} rubraview_batch_convert_params_t;

typedef struct rubraview_batch_action {
    rubraview_batch_action_kind_t kind;
    union {
        rubraview_batch_orient_params_t orient;
        rubraview_batch_resize_params_t resize;
        rubraview_batch_color_params_t color;
        rubraview_batch_privacy_params_t privacy;
        rubraview_batch_convert_params_t convert;
        rubraview_batch_extra_params_t extra;
    } params;
} rubraview_batch_action_t;

typedef struct rubraview_batch_job {
    rubraview_batch_action_t *actions;
    size_t action_count;
    u8str_t include_pattern; /* ';'-separated glob list (rubraview/glob.h); empty = include everything */
    u8str_t exclude_pattern; /* ';'-separated glob list; empty = exclude nothing */
    uint64_t min_size_bytes; /* 0 = no lower bound */
    uint64_t max_size_bytes; /* 0 = no upper bound */
    u8str_t naming_pattern;  /* e.g. "{name}_thumb.{ext}"; see rubraview_batch_format_name */
    int32_t counter_start;   /* RV-129: {n} of the first file written; 0 is taken as 1 */
    u8str_t replace_from;    /* RV-129: in each name's stem, every `replace_from` becomes `replace_to` */
    u8str_t replace_to;
} rubraview_batch_job_t;

/** Whether `filename` (with `file_size` bytes) passes the job's include/exclude/size filters. */
bool rubraview_batch_file_matches(const rubraview_batch_job_t *job, u8str_t filename, uint64_t file_size);

/**
 * Expand a naming pattern's tokens: `{name}` (stem), `{ext}` (without
 * leading dot), `{w}`/`{h}` (output dimensions), `{date}` (caller-
 * supplied, e.g. "2026-09-08"). Any other `{...}` token, or a `{`
 * without a matching `}`, is copied through unchanged.
 */
u8str_t rubraview_batch_format_name(proven_arena_t *arena, u8str_t pattern, u8str_t name_stem, u8str_t ext, int32_t width, int32_t height, u8str_t date_str);

/**
 * RV-129: the same with {n} (the counter), {n:3} (padded with zeros to
 * three digits), and {parent} (the name of the folder the file is in).
 */
u8str_t rubraview_batch_format_name_ex(proven_arena_t *arena, u8str_t pattern, u8str_t name_stem, u8str_t ext,
                                       int32_t width, int32_t height, u8str_t date_str, int64_t counter, u8str_t parent);

/** RV-129: every `from` in `text` replaced by `to` (a new string; `text` itself when `from` is empty or absent). */
u8str_t rubraview_batch_replace_all(proven_arena_t *arena, u8str_t text, u8str_t from, u8str_t to);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_BATCH_H */
