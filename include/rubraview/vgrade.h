#ifndef RUBRAVIEW_VGRADE_H
#define RUBRAVIEW_VGRADE_H

/*
 * RV-270 (owner, 2026-11-11: "동영상에 대해 색상 명도 채도 색온도 밝기곡선 같은 것 조절해서 볼 수 있게 ... 샤픈이나
 * 블러, 해상도 업 같은 필터"): a film's picture adjusted as it plays, on the graphics card, the file untouched.
 *
 * The values are Edit's (§3.13): they become an Edit session (rubraview_vgrade_session), whose commit
 * arithmetic gives the per-channel tables the card looks up (rubraview_edit_tone_tables), so a frame adjusted
 * here and a picture committed in Edit agree. The blur and the sharpening use the Gaussian blur's own kernel
 * (rubraview_filter_gaussian_kernel). The upscaler, for a film drawn larger than its pixels, is this module's
 * own: an edge-directed Lanczos-2 and a sharpening held by each pixel's headroom, written here from the
 * published idea, with a CPU reference the card's shader follows step for step.
 */

#include <stdbool.h>
#include "rubraview/core.h"
#include "rubraview/edit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rubraview_vgrade_value {
    RUBRAVIEW_VGRADE_EXPOSURE = 0,   /* EV, -3 .. 3 */
    RUBRAVIEW_VGRADE_BRIGHTNESS,     /* -100 .. 100 */
    RUBRAVIEW_VGRADE_CONTRAST,       /* -100 .. 100 */
    RUBRAVIEW_VGRADE_SATURATION,     /* -100 .. 100 (-100 grey) */
    RUBRAVIEW_VGRADE_TEMPERATURE,    /* -100 cool .. 100 warm */
    RUBRAVIEW_VGRADE_TINT,           /* -100 green .. 100 magenta */
    RUBRAVIEW_VGRADE_GAMMA,          /* midtones, 0.2 .. 5, 1 neutral */
    RUBRAVIEW_VGRADE_SHADOWS,        /* the brightness curve at 64: -100 .. 100 (±50 levels) */
    RUBRAVIEW_VGRADE_MIDTONES,       /* at 128 */
    RUBRAVIEW_VGRADE_HIGHLIGHTS,     /* at 192 */
    RUBRAVIEW_VGRADE_BLUR,           /* sigma in film pixels, 0 off .. 3 */
    RUBRAVIEW_VGRADE_SHARPEN,        /* an unsharp mask, 0 off .. 300 % */
    RUBRAVIEW_VGRADE_SHARPEN_RADIUS, /* its sigma, 0.5 .. 3 */
    RUBRAVIEW_VGRADE_UPSCALE,        /* the upscaler's strength, 0 off .. 100 */
    RUBRAVIEW_VGRADE_COUNT
} rubraview_vgrade_value_t;

typedef struct rubraview_vgrade {
    float v[RUBRAVIEW_VGRADE_COUNT];
} rubraview_vgrade_t;

typedef struct rubraview_vgrade_range {
    const char *key;      /* `[video] adjust_<key>` */
    const char *label;    /* the panel's, English (translated where drawn) */
    float min, max, neutral, step;
} rubraview_vgrade_range_t;

const rubraview_vgrade_range_t *rubraview_vgrade_range(rubraview_vgrade_value_t value);

/** Every value at its neutral. */
void rubraview_vgrade_reset(rubraview_vgrade_t *g);
/** Every value inside its range (a value not a number is its neutral). */
void rubraview_vgrade_clamp(rubraview_vgrade_t *g);
/** Whether it changes nothing at all (the sharpening's radius alone does not count). */
bool rubraview_vgrade_is_neutral(const rubraview_vgrade_t *g);

/** The values as an Edit session of a picture `width` x `height`: Edit's sliders, the composite curve
    through the three points, the blur and the unsharp mask. */
void rubraview_vgrade_session(const rubraview_vgrade_t *g, int32_t width, int32_t height, rubraview_edit_session_t *out);

#define RUBRAVIEW_VGRADE_TAPS 10   /* a blur's weights from the centre out: sigma 3 reaches 9 pixels */

/** What the card's passes take. */
typedef struct rubraview_vgrade_prepared {
    bool  tone;                        /* the tables and the saturation do something */
    float table[3][256];               /* red, green, blue: each code's value before saturation */
    float saturation;                  /* rubraview_color_saturate's factor; 1 none */
    int32_t blur_radius;               /* 0 none */
    float blur_weights[RUBRAVIEW_VGRADE_TAPS];      /* [0] the centre, [k] at ±k */
    int32_t sharpen_radius;            /* 0 none */
    float sharpen_weights[RUBRAVIEW_VGRADE_TAPS];
    float sharpen_amount;              /* the unsharp mask's factor (300 % is 3) */
    float upscale;                     /* 0 none, else 0.01 .. 1 */
} rubraview_vgrade_prepared_t;

/** Fills `out`; true when a pass at the film's own size does something (tone, blur or sharpening). */
bool rubraview_vgrade_prepare(const rubraview_vgrade_t *g, rubraview_vgrade_prepared_t *out);

/** One channel's coded value through its table, as the card does it: between two codes the table is read
    linearly; above 1 (an HDR film's highlights, sRGB-coded past the SDR white) it is the value times the
    table's white, so the highlights move with the picture and are not clipped; at or below 0, the table's black. */
float rubraview_vgrade_tone_value(const rubraview_vgrade_prepared_t *p, int channel, float coded);

/** The tone pass on an 8-bit picture (RGBA8 or BGRA8) in place, as the card does it: the tables, the
    saturation, rounded to a code. Equal to Edit's commit of rubraview_vgrade_session. */
void rubraview_vgrade_tone_apply(const rubraview_vgrade_prepared_t *p, rubraview_pixbuf_t *pb);

/** The display scale over which the upscaler runs (a film drawn at most this much larger is left to the
    canvas's own filtering) and the largest side it makes. */
#define RUBRAVIEW_VGRADE_UPSCALE_FROM 1.05
#define RUBRAVIEW_VGRADE_UPSCALE_MAX 8192

/** The size the upscaler makes for a film of `w` x `h` drawn `scale` times larger (rounded, the side kept
    at most RUBRAVIEW_VGRADE_UPSCALE_MAX by a smaller scale); false when it should not run. */
bool rubraview_vgrade_upscale_size(int32_t w, int32_t h, double scale, int32_t *out_w, int32_t *out_h);

/** The Lanczos-2 window (sinc(d) sinc(d/2), 0 from 2). */
float rubraview_vgrade_lanczos2(float d);

/** The upscaler's two steps on an 8-bit picture, the CPU's reference for the shader: `src` to `dw` x `dh`
    (edge-directed, held inside the four nearest pixels), then the sharpening at that size, `strength` 0..1.
    Empty when it cannot. */
rubraview_pixbuf_t rubraview_vgrade_upscale(proven_arena_t *arena, const rubraview_pixbuf_t *src,
                                            int32_t dw, int32_t dh, float strength);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_VGRADE_H */
