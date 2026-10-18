#ifndef RUBRAVIEW_EDIT_H
#define RUBRAVIEW_EDIT_H

#include "rubraview/core.h"
#include "rubraview/color.h"
#include "rubraview/resample.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The editing session (RFC-0001 §3.13), RV-065.
 *
 * §3.13 describes two layers: a GPU preview that moves while a slider
 * moves, and a commit that runs the same maths on the pixel buffer when
 * the reader accepts. What both layers need is the same *state* — which
 * adjustments are set to what, where the curve's control points are,
 * where the crop rectangle is — and that state has no Direct2D in it.
 * It lives here, so the arithmetic that decides whether a drag is legal
 * or where a control point lands is checked by the host suite rather
 * than by looking at the screen.
 *
 * Nothing here touches pixels. `rubraview_edit_commit` is the one call
 * that does, and it runs the existing core engine — the same colour,
 * curve, filter and resample code the batch engine uses, so a preview
 * and a batch conversion cannot drift apart.
 */

/* §3.13's slider set. The ranges are the specification's, and every
   setter clamps to them: a panel cannot ask for an exposure the commit
   layer would refuse. */
typedef struct rubraview_edit_params {
    float exposure_ev;    /* -3.0 .. +3.0, 0 neutral */
    float brightness;     /* -100 .. +100, 0 neutral */
    float contrast;       /* -100 .. +100, 0 neutral */
    float saturation;     /* -100 .. +100, 0 neutral (mapped to a factor on commit) */
    float temperature;    /* -100 (cool) .. +100 (warm), 0 neutral */
    float tint;           /* -100 (green) .. +100 (magenta), 0 neutral */

    uint8_t black_point;  /* levels, 0 .. 254 */
    uint8_t white_point;  /* levels, 1 .. 255; always above black_point */
    float   midtone_gamma;/* 0.1 .. 10.0, 1.0 neutral */
    /* The owner's colour corrections (2026-10-17): each channel moved on its own. */
    float   balance_r, balance_g, balance_b;   /* -100 .. +100 levels-ish, 0 neutral */
} rubraview_edit_params_t;

typedef enum rubraview_edit_channel {
    RUBRAVIEW_EDIT_CHANNEL_RGB = 0,
    RUBRAVIEW_EDIT_CHANNEL_RED,
    RUBRAVIEW_EDIT_CHANNEL_GREEN,
    RUBRAVIEW_EDIT_CHANNEL_BLUE,
    RUBRAVIEW_EDIT_CHANNEL_LUMA,
} rubraview_edit_channel_t;

#define RUBRAVIEW_EDIT_MAX_CURVE_POINTS 16

/* One channel's tone curve. A curve always keeps its two endpoints, so
   there is no way to drag it into a state with nothing to interpolate. */
typedef struct rubraview_edit_curve {
    rubraview_curve_point_t points[RUBRAVIEW_EDIT_MAX_CURVE_POINTS];
    size_t point_count;
} rubraview_edit_curve_t;

typedef enum rubraview_crop_ratio {
    RUBRAVIEW_CROP_FREE = 0,
    RUBRAVIEW_CROP_1_1,
    RUBRAVIEW_CROP_4_3,
    RUBRAVIEW_CROP_16_9,
    RUBRAVIEW_CROP_ORIGINAL,
} rubraview_crop_ratio_t;

typedef struct rubraview_crop_rect {
    int32_t x, y, width, height;
} rubraview_crop_rect_t;

typedef struct rubraview_edit_session {
    int32_t image_width, image_height;

    rubraview_edit_params_t params;
    rubraview_edit_curve_t  curves[5];   /* indexed by rubraview_edit_channel_t */
    rubraview_edit_channel_t active_channel;

    bool                  crop_active;
    rubraview_crop_rect_t crop;
    rubraview_crop_ratio_t crop_ratio;

    /* §3.13's resize controls. Zero width and height mean "unchanged". */
    int32_t resize_width, resize_height;
    bool    resize_lock_aspect;
    rubraview_resample_filter_t resize_filter;
    /* RV-128 (owner, 2026-10-17: beyond IrfanView's resize) */
    bool    resize_linear;       /* resampled in linear light (gamma-correct) */
    float   resize_sharpen;      /* 0 = off, else 1 .. 100: an unsharp mask after a reduction */
    double  resize_dpi;          /* for sizes typed in cm, mm or inches; 0 = 300 */

    /* §3.13's spatial filters, applied on commit in this order. */
    float blur_sigma;        /* 0 = off, else 0.5 .. 50.0 */
    float sharpen_amount;    /* 0 = off, else 0 .. 300 (%) */
    float sharpen_radius;    /* 0.5 .. 10.0 */
    uint8_t sharpen_threshold;
    /* RV-108 (RFC-0001 §6.4): the other blur and the other sharpening. A
       box blur's radius is the same slider, in pixels; Laplacian
       sharpening has no radius and no threshold. */
    bool blur_box;
    bool sharpen_laplacian;

    bool dirty;              /* something has changed since the last commit */
} rubraview_edit_session_t;

/** A session over an image of the given size, with everything neutral. */
rubraview_edit_session_t rubraview_edit_begin(int32_t image_width, int32_t image_height);

/** True when the session would change nothing — the Apply button's state. */
bool rubraview_edit_is_neutral(const rubraview_edit_session_t *session);

/** Put everything back to neutral without ending the session. */
void rubraview_edit_reset(rubraview_edit_session_t *session);

/**
 * Set one slider, clamped to §3.13's range. Passing a value outside the
 * range is not an error — a slider dragged past its end simply stops,
 * which is what the widget does anyway.
 */
typedef enum rubraview_edit_slider {
    RUBRAVIEW_SLIDER_EXPOSURE = 0,
    RUBRAVIEW_SLIDER_BRIGHTNESS,
    RUBRAVIEW_SLIDER_CONTRAST,
    RUBRAVIEW_SLIDER_SATURATION,
    RUBRAVIEW_SLIDER_TEMPERATURE,
    RUBRAVIEW_SLIDER_TINT,
    RUBRAVIEW_SLIDER_MIDTONE_GAMMA,
    RUBRAVIEW_SLIDER_BLUR_SIGMA,
    RUBRAVIEW_SLIDER_SHARPEN_AMOUNT,
    RUBRAVIEW_SLIDER_SHARPEN_RADIUS,
} rubraview_edit_slider_t;

void rubraview_edit_set_slider(rubraview_edit_session_t *session,
                               rubraview_edit_slider_t slider, float value);
float rubraview_edit_get_slider(const rubraview_edit_session_t *session,
                                rubraview_edit_slider_t slider);

/**
 * Levels: the black and white points cannot cross, so setting one
 * pushes the other out of the way rather than producing an inverted
 * range the LUT builder would have to guess about.
 */
void rubraview_edit_set_black_point(rubraview_edit_session_t *session, int32_t value);
void rubraview_edit_set_white_point(rubraview_edit_session_t *session, int32_t value);

/* ---- the curve widget (§3.13) ---- */

/**
 * Add a control point to the active channel's curve, or move the
 * nearest one if the click landed on it. Returns the index of the point
 * that is now being dragged, or -1 if the curve is full.
 *
 * Points are kept sorted by x, and the two endpoints cannot be removed
 * or dragged past each other — a curve is a function, and this is where
 * that is enforced.
 */
int32_t rubraview_edit_curve_grab(rubraview_edit_session_t *session, float x, float y, float hit_radius);

/** Move the point being dragged. Clamped to the curve's box and to its neighbours. */
void rubraview_edit_curve_move(rubraview_edit_session_t *session, int32_t index, float x, float y);

/** Remove an interior point. The endpoints are refused. */
bool rubraview_edit_curve_remove(rubraview_edit_session_t *session, int32_t index);

/** Step the channel the curve widget edits, `step` +1 forward or -1 back,
 *  round RGB, Red, Green, Blue, Luma. Each channel keeps its own curve. */
void rubraview_edit_cycle_channel(rubraview_edit_session_t *session, int step);

/** Straight line from (0,0) to (255,255) — the identity curve. */
void rubraview_edit_curve_reset(rubraview_edit_session_t *session, rubraview_edit_channel_t channel);

/* ---- the crop overlay (§3.13) ---- */

/**
 * Start a crop from a drag. The rectangle is normalised (a drag up and
 * to the left is the same rectangle as one down and to the right), kept
 * inside the image, and forced to the active aspect ratio.
 */
void rubraview_edit_crop_drag(rubraview_edit_session_t *session,
                              int32_t x0, int32_t y0, int32_t x1, int32_t y1);

/**
 * A crop typed as numbers (owner, 2026-10-15): the left and top corner and
 * the size, in image pixels. The ratio lock is let go (the numbers are
 * what was asked for); a corner outside the picture or a size under one
 * pixel is refused, and a size running past the edge is cut at it.
 */
bool rubraview_edit_crop_set(rubraview_edit_session_t *session, int32_t x, int32_t y, int32_t width, int32_t height);

/** Change the ratio lock, re-shaping the current rectangle to match. */
void rubraview_edit_crop_set_ratio(rubraview_edit_session_t *session, rubraview_crop_ratio_t ratio);

/** The ratio as width/height, or 0 for Free. */
double rubraview_crop_ratio_value(rubraview_crop_ratio_t ratio, int32_t image_width, int32_t image_height);

/* ---- resize (§3.13) ---- */

/**
 * Set a target width; with the aspect lock on, the height follows. A
 * percentage is expressed by the caller as a target size, since that is
 * what the commit layer needs either way.
 */
void rubraview_edit_set_resize(rubraview_edit_session_t *session, int32_t width, int32_t height, bool from_width);

/**
 * §3.13's percentage scaling, 10% to 500%, as the two numbers the
 * resize actually uses. Returns false if the source size is unusable.
 */
bool rubraview_edit_resize_percent(const rubraview_edit_session_t *session, double percent,
                                   int32_t *out_width, int32_t *out_height);

/**
 * A size typed in pixels (owner, 2026-10-15): `typed` is the width (or,
 * `from_width` false, the height); the other follows the picture's shape
 * (of what the cut leaves) while the aspect lock is on. False for a
 * number under 1 or over 65535, or a source of no size.
 */
bool rubraview_edit_resize_pair(const rubraview_edit_session_t *session, int32_t typed, bool from_width,
                                int32_t *out_width, int32_t *out_height);

/* RV-128: sizes by name. Standard sizes turn to the picture's way (a
   portrait picture fits 1080 x 1920); with the shape kept the picture fits
   inside, otherwise it is stretched to the size. */
typedef enum rubraview_resize_preset {
    RUBRAVIEW_RESIZE_PRESET_NONE = 0,
    RUBRAVIEW_RESIZE_PRESET_HALF, RUBRAVIEW_RESIZE_PRESET_DOUBLE, RUBRAVIEW_RESIZE_PRESET_SCREEN,
    RUBRAVIEW_RESIZE_PRESET_640, RUBRAVIEW_RESIZE_PRESET_800, RUBRAVIEW_RESIZE_PRESET_1024,
    RUBRAVIEW_RESIZE_PRESET_720P, RUBRAVIEW_RESIZE_PRESET_1080P, RUBRAVIEW_RESIZE_PRESET_1440P,
    RUBRAVIEW_RESIZE_PRESET_4K, RUBRAVIEW_RESIZE_PRESET_8K,
    RUBRAVIEW_RESIZE_PRESET_LONG_1024, RUBRAVIEW_RESIZE_PRESET_LONG_2048,
    RUBRAVIEW_RESIZE_PRESET_MP_2, RUBRAVIEW_RESIZE_PRESET_MP_8, RUBRAVIEW_RESIZE_PRESET_MP_12,
    RUBRAVIEW_RESIZE_PRESET_COUNT
} rubraview_resize_preset_t;

/** The size a preset gives a `width` x `height` picture; `screen_*` for SCREEN. False for none or a bad size. */
bool rubraview_resize_preset_size(rubraview_resize_preset_t preset, int32_t width, int32_t height,
                                  int32_t screen_width, int32_t screen_height, bool keep_shape,
                                  int32_t *out_width, int32_t *out_height);

/**
 * RV-128: a length as typed into the size box — "1200", "1200px", "50%"
 * (of `whole_px`), "21cm", "210 mm", "8.5in" or 8.5" at `dpi` (0 = 300);
 * a comma works as the decimal point. False when it is not a length, or
 * gives under 1 or over 65535 pixels.
 */
bool rubraview_resize_parse_length(u8str_t text, int32_t whole_px, double dpi, int32_t *out_px);

/* ---- preview ---- */

/**
 * §3.13 asks for a preview that moves while a slider moves. The Direct2D
 * effect graph it names is not reachable from C on the build toolchain
 * (see the note in `src/pal/win32/pal_render_d2d.c`), so the preview
 * runs the *same* commit code on a reduced copy of the image instead.
 *
 * This decides how reduced: no larger than the area it will be shown in,
 * and never larger than the source. A preview of a 60-megapixel photo
 * displayed in a 2000-pixel-wide window costs 2000 pixels of work, not
 * 60 million — and because it is the same code as the commit, what the
 * reader sees is what they will get.
 */
void rubraview_edit_preview_size(const rubraview_edit_session_t *session,
                                 int32_t view_width, int32_t view_height,
                                 int32_t *out_width, int32_t *out_height);

/* ---- commit ---- */

/**
 * Run the session over a pixel buffer, in §3.13's order: crop, then
 * levels and curves, then the colour sliders, then the spatial filters,
 * then the resize. Returns a new buffer in `arena`; the source is not
 * modified, which is what "non-destructive" means here.
 */
rubraview_pixbuf_t rubraview_edit_commit(proven_arena_t *arena,
                                         const rubraview_edit_session_t *session,
                                         const rubraview_pixbuf_t *src);

/**
 * The owner's automatic colour correction (2026-10-17; written here, MIT):
 * each channel stretched between its 0.5th and 99.5th percentile (which
 * takes a colour cast out with the haze), then the three channels' means
 * drawn halfway together (the grey-world assumption). Written into the
 * session's red, green and blue curves and its colour balance, so it can
 * be seen, changed and undone like any other value.
 */
void rubraview_edit_auto_colour(rubraview_edit_session_t *session, const rubraview_pixbuf_t *image);

/* ---- the owner's edit, 2026-10-17: steps on a selection ---- */

/* An area of the picture an effect is put on: the whole of it, or a
   rectangle or an ellipse, its edge hard or softened over `feather` pixels. */
typedef enum rubraview_area_shape {
    RUBRAVIEW_AREA_ALL = 0,
    RUBRAVIEW_AREA_RECT,
    RUBRAVIEW_AREA_ELLIPSE,
} rubraview_area_shape_t;

typedef struct rubraview_edit_area {
    rubraview_area_shape_t shape;
    rubraview_crop_rect_t  rect;      /* in the picture as the steps before left it */
    int32_t                feather;   /* 0: a hard edge */
} rubraview_edit_area_t;

typedef enum rubraview_edit_step_kind {
    RUBRAVIEW_STEP_ADJUST = 0,   /* the values' light, colour, levels, curves, blur and sharpening, on the area */
    RUBRAVIEW_STEP_CROP,         /* to the area's rectangle */
    RUBRAVIEW_STEP_RESIZE,       /* the whole picture to values.resize_width x resize_height */
    RUBRAVIEW_STEP_EFFECT,       /* `effect` at `amount` (effects.h), on the area */
} rubraview_edit_step_kind_t;

typedef struct rubraview_edit_step {
    rubraview_edit_step_kind_t kind;
    rubraview_edit_area_t      area;
    rubraview_edit_session_t   values;   /* what the step does (its crop and resize fields unused by ADJUST) */
    int32_t                    effect;   /* EFFECT: a rubraview_effect_t */
    double                     amount;
} rubraview_edit_step_t;

#define RUBRAVIEW_EDIT_MAX_STEPS 64

/* The steps made, and how many of them are in force: undo moves `cursor`
   back, redo forward, a new step drops those after it. */
typedef struct rubraview_edit_steps {
    rubraview_edit_step_t steps[RUBRAVIEW_EDIT_MAX_STEPS];
    size_t  count, cursor;
    int32_t width, height;   /* the picture before any step */
} rubraview_edit_steps_t;

void rubraview_edit_steps_begin(rubraview_edit_steps_t *steps, int32_t width, int32_t height);
/** A new step after the ones in force; false when it does nothing or there is no room (the oldest is then kept). */
bool rubraview_edit_steps_push(rubraview_edit_steps_t *steps, const rubraview_edit_step_t *step);
/**
 * The owner (2026-10-18): a change is a step at once. The last step in force
 * is put in `step`'s place (a slider still moving); the ones undone after it
 * go. With `step` doing nothing the last step is taken off instead. False
 * when no step is in force.
 */
bool rubraview_edit_steps_set_last(rubraview_edit_steps_t *steps, const rubraview_edit_step_t *step);
/**
 * The owner (2026-10-18): the steps made on one picture, for another of
 * `width` x `height`: every area, cut and size scaled across by width and
 * down by height (a feather by the smaller), so the same parts of the
 * picture are changed. Pixel amounts (an effect's) are kept.
 */
void rubraview_edit_steps_rebase(rubraview_edit_steps_t *steps, int32_t width, int32_t height);
/** A rectangle of a `from_w` x `from_h` picture, the same part of a `to_w` x `to_h` one (at least 1 x 1, inside it). */
rubraview_crop_rect_t rubraview_edit_rect_rebase(rubraview_crop_rect_t r, int32_t from_w, int32_t from_h, int32_t to_w, int32_t to_h);
/**
 * RV-137 (owner, 2026-10-18): the steps in force reordered or taken out
 * from the list. The steps undone after them go first. A step that now
 * comes after another size (a crop or a resize moved or gone) has its
 * area, and a crop its rectangle, put in proportion to that size.
 * move: false at an end; remove: how many went (`gone[i]` for step i).
 */
bool rubraview_edit_steps_move(rubraview_edit_steps_t *steps, size_t index, bool up);
size_t rubraview_edit_steps_remove(rubraview_edit_steps_t *steps, const bool *gone);

bool rubraview_edit_steps_undo(rubraview_edit_steps_t *steps);
bool rubraview_edit_steps_redo(rubraview_edit_steps_t *steps);
/** To `cursor` steps in force (0 is the picture as it was); false past the end. */
bool rubraview_edit_steps_go(rubraview_edit_steps_t *steps, size_t cursor);
void rubraview_edit_steps_reset(rubraview_edit_steps_t *steps);
/** The picture's size after the first `upto` steps. */
void rubraview_edit_steps_size(const rubraview_edit_steps_t *steps, size_t upto, int32_t *width, int32_t *height);

/** How much of an effect a pixel takes, 0..1, for an area (pixel centres). */
float rubraview_edit_area_weight(const rubraview_edit_area_t *area, int32_t x, int32_t y);

/**
 * One step on `src` (RGBA8 or BGRA8), the result in `arena`. `scale` is
 * how big `src` is against the picture the step was made on (1 for the
 * picture itself, less for the preview): the area, the crop, the resize
 * and the blur and sharpening radii are scaled by it.
 */
rubraview_pixbuf_t rubraview_edit_step_apply(proven_arena_t *arena, const rubraview_edit_step_t *step,
                                             const rubraview_pixbuf_t *src, double scale);
/** The steps in force, in order. */
rubraview_pixbuf_t rubraview_edit_steps_apply(proven_arena_t *arena, const rubraview_edit_steps_t *steps,
                                              const rubraview_pixbuf_t *src, double scale);
/** Whether a step's values change anything. */
bool rubraview_edit_step_does_something(const rubraview_edit_step_t *step);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_EDIT_H */
