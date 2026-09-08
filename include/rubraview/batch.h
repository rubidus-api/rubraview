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
} rubraview_batch_resize_mode_t;

typedef struct rubraview_batch_resize_params {
    rubraview_batch_resize_mode_t mode;
    double value_a, value_b;
    rubraview_resample_filter_t filter; /* rubraview/resample.h */
} rubraview_batch_resize_params_t;

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

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_BATCH_H */
