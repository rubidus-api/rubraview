#include "rubraview/edit.h"
#include "rubraview/filter.h"
#include <string.h>
#include <math.h>

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void rubraview_edit_curve_reset(rubraview_edit_session_t *session, rubraview_edit_channel_t channel) {
    if (!session || (int)channel < 0 || (int)channel > 4) return;
    rubraview_edit_curve_t *curve = &session->curves[channel];
    curve->points[0] = (rubraview_curve_point_t){ .x = 0.0f, .y = 0.0f };
    curve->points[1] = (rubraview_curve_point_t){ .x = 255.0f, .y = 255.0f };
    curve->point_count = 2;
}

rubraview_edit_session_t rubraview_edit_begin(int32_t image_width, int32_t image_height) {
    rubraview_edit_session_t session = {0};
    session.image_width = image_width > 0 ? image_width : 0;
    session.image_height = image_height > 0 ? image_height : 0;

    session.params.white_point = 255;
    session.params.midtone_gamma = 1.0f;
    session.active_channel = RUBRAVIEW_EDIT_CHANNEL_RGB;
    session.crop_ratio = RUBRAVIEW_CROP_FREE;
    session.resize_lock_aspect = true;
    session.resize_filter = RUBRAVIEW_FILTER_LANCZOS3;
    session.sharpen_radius = 1.0f;

    for (int i = 0; i < 5; ++i) rubraview_edit_curve_reset(&session, (rubraview_edit_channel_t)i);
    return session;
}

static bool curve_is_identity(const rubraview_edit_curve_t *curve) {
    return curve->point_count == 2 &&
           curve->points[0].x == 0.0f && curve->points[0].y == 0.0f &&
           curve->points[1].x == 255.0f && curve->points[1].y == 255.0f;
}

bool rubraview_edit_is_neutral(const rubraview_edit_session_t *session) {
    if (!session) return true;
    const rubraview_edit_params_t *p = &session->params;

    if (p->exposure_ev != 0.0f || p->brightness != 0.0f || p->contrast != 0.0f ||
        p->saturation != 0.0f || p->temperature != 0.0f || p->tint != 0.0f) return false;
    if (p->black_point != 0 || p->white_point != 255 || p->midtone_gamma != 1.0f) return false;
    if (session->crop_active) return false;
    if (session->resize_width != 0 || session->resize_height != 0) return false;
    if (session->blur_sigma != 0.0f || session->sharpen_amount != 0.0f) return false;

    for (int i = 0; i < 5; ++i) {
        if (!curve_is_identity(&session->curves[i])) return false;
    }
    return true;
}

void rubraview_edit_reset(rubraview_edit_session_t *session) {
    if (!session) return;
    int32_t w = session->image_width, h = session->image_height;
    *session = rubraview_edit_begin(w, h);
}

/* Each slider's range, written once so the setter and the getter cannot
   disagree about what is legal. */
static void slider_range(rubraview_edit_slider_t slider, float *lo, float *hi) {
    switch (slider) {
        case RUBRAVIEW_SLIDER_EXPOSURE:        *lo = -3.0f;  *hi = 3.0f;   break;
        case RUBRAVIEW_SLIDER_BRIGHTNESS:      *lo = -100.0f;*hi = 100.0f; break;
        case RUBRAVIEW_SLIDER_CONTRAST:        *lo = -100.0f;*hi = 100.0f; break;
        case RUBRAVIEW_SLIDER_SATURATION:      *lo = -100.0f;*hi = 100.0f; break;
        case RUBRAVIEW_SLIDER_TEMPERATURE:     *lo = -100.0f;*hi = 100.0f; break;
        case RUBRAVIEW_SLIDER_TINT:            *lo = -100.0f;*hi = 100.0f; break;
        case RUBRAVIEW_SLIDER_MIDTONE_GAMMA:   *lo = 0.1f;   *hi = 10.0f;  break;
        case RUBRAVIEW_SLIDER_BLUR_SIGMA:      *lo = 0.0f;   *hi = 50.0f;  break;
        case RUBRAVIEW_SLIDER_SHARPEN_AMOUNT:  *lo = 0.0f;   *hi = 300.0f; break;
        case RUBRAVIEW_SLIDER_SHARPEN_RADIUS:  *lo = 0.5f;   *hi = 10.0f;  break;
        default:                               *lo = 0.0f;   *hi = 0.0f;   break;
    }
}

static float *slider_slot(rubraview_edit_session_t *session, rubraview_edit_slider_t slider) {
    switch (slider) {
        case RUBRAVIEW_SLIDER_EXPOSURE:       return &session->params.exposure_ev;
        case RUBRAVIEW_SLIDER_BRIGHTNESS:     return &session->params.brightness;
        case RUBRAVIEW_SLIDER_CONTRAST:       return &session->params.contrast;
        case RUBRAVIEW_SLIDER_SATURATION:     return &session->params.saturation;
        case RUBRAVIEW_SLIDER_TEMPERATURE:    return &session->params.temperature;
        case RUBRAVIEW_SLIDER_TINT:           return &session->params.tint;
        case RUBRAVIEW_SLIDER_MIDTONE_GAMMA:  return &session->params.midtone_gamma;
        case RUBRAVIEW_SLIDER_BLUR_SIGMA:     return &session->blur_sigma;
        case RUBRAVIEW_SLIDER_SHARPEN_AMOUNT: return &session->sharpen_amount;
        case RUBRAVIEW_SLIDER_SHARPEN_RADIUS: return &session->sharpen_radius;
        default: return NULL;
    }
}

void rubraview_edit_set_slider(rubraview_edit_session_t *session,
                               rubraview_edit_slider_t slider, float value) {
    if (!session) return;
    float *slot = slider_slot(session, slider);
    if (!slot) return;

    float lo = 0.0f, hi = 0.0f;
    slider_range(slider, &lo, &hi);

    /* A blur of "0.3" is not a very small blur; §3.13 starts the radius
       at 0.5, so anything under it means off. */
    if (slider == RUBRAVIEW_SLIDER_BLUR_SIGMA && value > 0.0f && value < 0.5f) value = 0.5f;

    *slot = clampf(value, lo, hi);
    session->dirty = true;
}

float rubraview_edit_get_slider(const rubraview_edit_session_t *session,
                                rubraview_edit_slider_t slider) {
    if (!session) return 0.0f;
    float *slot = slider_slot((rubraview_edit_session_t*)session, slider);
    return slot ? *slot : 0.0f;
}

void rubraview_edit_set_black_point(rubraview_edit_session_t *session, int32_t value) {
    if (!session) return;
    value = clampi(value, 0, 254);
    session->params.black_point = (uint8_t)value;
    if (session->params.white_point <= session->params.black_point) {
        session->params.white_point = (uint8_t)(session->params.black_point + 1);
    }
    session->dirty = true;
}

void rubraview_edit_set_white_point(rubraview_edit_session_t *session, int32_t value) {
    if (!session) return;
    value = clampi(value, 1, 255);
    session->params.white_point = (uint8_t)value;
    if (session->params.black_point >= session->params.white_point) {
        session->params.black_point = (uint8_t)(session->params.white_point - 1);
    }
    session->dirty = true;
}

/* ---- curve ---- */

static void curve_insert_sorted(rubraview_edit_curve_t *curve, rubraview_curve_point_t point, int32_t *out_index) {
    size_t at = 0;
    while (at < curve->point_count && curve->points[at].x < point.x) at++;
    for (size_t i = curve->point_count; i > at; --i) curve->points[i] = curve->points[i - 1];
    curve->points[at] = point;
    curve->point_count++;
    *out_index = (int32_t)at;
}

int32_t rubraview_edit_curve_grab(rubraview_edit_session_t *session, float x, float y, float hit_radius) {
    if (!session) return -1;
    rubraview_edit_curve_t *curve = &session->curves[session->active_channel];

    /* An existing point within reach is grabbed rather than a new one
       being stacked on top of it. */
    for (size_t i = 0; i < curve->point_count; ++i) {
        float dx = curve->points[i].x - x, dy = curve->points[i].y - y;
        if (dx * dx + dy * dy <= hit_radius * hit_radius) return (int32_t)i;
    }

    if (curve->point_count >= RUBRAVIEW_EDIT_MAX_CURVE_POINTS) return -1;

    rubraview_curve_point_t point = { .x = clampf(x, 0.0f, 255.0f), .y = clampf(y, 0.0f, 255.0f) };
    int32_t index = -1;
    curve_insert_sorted(curve, point, &index);
    session->dirty = true;
    return index;
}

void rubraview_edit_curve_move(rubraview_edit_session_t *session, int32_t index, float x, float y) {
    if (!session || index < 0) return;
    rubraview_edit_curve_t *curve = &session->curves[session->active_channel];
    if ((size_t)index >= curve->point_count) return;

    /* The endpoints stay at the ends: a curve that does not span the
       whole range is not a transfer function. They move vertically only. */
    bool is_first = index == 0;
    bool is_last = (size_t)index == curve->point_count - 1;

    float lo_x = is_first ? 0.0f : curve->points[index - 1].x + 1.0f;
    float hi_x = is_last ? 255.0f : curve->points[index + 1].x - 1.0f;
    if (is_first) hi_x = 0.0f;
    if (is_last) lo_x = 255.0f;

    curve->points[index].x = clampf(x, lo_x, hi_x);
    curve->points[index].y = clampf(y, 0.0f, 255.0f);
    session->dirty = true;
}

bool rubraview_edit_curve_remove(rubraview_edit_session_t *session, int32_t index) {
    if (!session || index <= 0) return false;
    rubraview_edit_curve_t *curve = &session->curves[session->active_channel];
    if ((size_t)index >= curve->point_count - 1) return false; /* last point is an endpoint */

    for (size_t i = (size_t)index; i + 1 < curve->point_count; ++i) curve->points[i] = curve->points[i + 1];
    curve->point_count--;
    session->dirty = true;
    return true;
}

/* ---- crop ---- */

double rubraview_crop_ratio_value(rubraview_crop_ratio_t ratio, int32_t image_width, int32_t image_height) {
    switch (ratio) {
        case RUBRAVIEW_CROP_1_1:  return 1.0;
        case RUBRAVIEW_CROP_4_3:  return 4.0 / 3.0;
        case RUBRAVIEW_CROP_16_9: return 16.0 / 9.0;
        case RUBRAVIEW_CROP_ORIGINAL:
            return image_height > 0 ? (double)image_width / (double)image_height : 0.0;
        case RUBRAVIEW_CROP_FREE:
        default: return 0.0;
    }
}

/* Forces a rectangle to the locked ratio and keeps it inside the image.
   The width is the side that gives: a reader dragging a corner is
   steering the diagonal, and shrinking is always possible where growing
   might not be. */
static void apply_ratio_and_clamp(rubraview_edit_session_t *session, rubraview_crop_rect_t *rect) {
    double ratio = rubraview_crop_ratio_value(session->crop_ratio, session->image_width, session->image_height);

    if (ratio > 0.0 && rect->height > 0) {
        int32_t want_w = (int32_t)((double)rect->height * ratio + 0.5);
        if (want_w > session->image_width) {
            want_w = session->image_width;
            rect->height = (int32_t)((double)want_w / ratio + 0.5);
        }
        rect->width = want_w;
    }

    if (rect->width > session->image_width) rect->width = session->image_width;
    if (rect->height > session->image_height) rect->height = session->image_height;
    if (rect->width < 1) rect->width = 1;
    if (rect->height < 1) rect->height = 1;

    rect->x = clampi(rect->x, 0, session->image_width - rect->width);
    rect->y = clampi(rect->y, 0, session->image_height - rect->height);
}

void rubraview_edit_crop_drag(rubraview_edit_session_t *session,
                              int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (!session || session->image_width <= 0 || session->image_height <= 0) return;

    /* Dragging up-and-left describes the same rectangle as down-and-right. */
    int32_t left = x0 < x1 ? x0 : x1;
    int32_t top = y0 < y1 ? y0 : y1;
    int32_t right = x0 < x1 ? x1 : x0;
    int32_t bottom = y0 < y1 ? y1 : y0;

    left = clampi(left, 0, session->image_width - 1);
    top = clampi(top, 0, session->image_height - 1);
    right = clampi(right, 1, session->image_width);
    bottom = clampi(bottom, 1, session->image_height);

    rubraview_crop_rect_t rect = { .x = left, .y = top, .width = right - left, .height = bottom - top };
    apply_ratio_and_clamp(session, &rect);

    session->crop = rect;
    session->crop_active = true;
    session->dirty = true;
}

void rubraview_edit_crop_set_ratio(rubraview_edit_session_t *session, rubraview_crop_ratio_t ratio) {
    if (!session) return;
    session->crop_ratio = ratio;
    if (session->crop_active) {
        apply_ratio_and_clamp(session, &session->crop);
    }
    session->dirty = true;
}

/* ---- resize ---- */

void rubraview_edit_set_resize(rubraview_edit_session_t *session, int32_t width, int32_t height, bool from_width) {
    if (!session) return;

    int32_t src_w = session->crop_active ? session->crop.width : session->image_width;
    int32_t src_h = session->crop_active ? session->crop.height : session->image_height;

    if (session->resize_lock_aspect && src_w > 0 && src_h > 0) {
        if (from_width && width > 0) {
            height = (int32_t)((double)width * (double)src_h / (double)src_w + 0.5);
        } else if (!from_width && height > 0) {
            width = (int32_t)((double)height * (double)src_w / (double)src_h + 0.5);
        }
    }

    session->resize_width = width > 0 ? width : 0;
    session->resize_height = height > 0 ? height : 0;
    session->dirty = true;
}

bool rubraview_edit_resize_percent(const rubraview_edit_session_t *session, double percent,
                                   int32_t *out_width, int32_t *out_height) {
    if (!session || !out_width || !out_height) return false;

    int32_t src_w = session->crop_active ? session->crop.width : session->image_width;
    int32_t src_h = session->crop_active ? session->crop.height : session->image_height;
    if (src_w <= 0 || src_h <= 0) return false;

    if (percent < 10.0) percent = 10.0;    /* §3.13's range */
    if (percent > 500.0) percent = 500.0;

    int32_t w = (int32_t)((double)src_w * percent / 100.0 + 0.5);
    int32_t h = (int32_t)((double)src_h * percent / 100.0 + 0.5);
    *out_width = w > 0 ? w : 1;
    *out_height = h > 0 ? h : 1;
    return true;
}

/* ---- preview ---- */

void rubraview_edit_preview_size(const rubraview_edit_session_t *session,
                                 int32_t view_width, int32_t view_height,
                                 int32_t *out_width, int32_t *out_height) {
    if (!out_width || !out_height) return;
    *out_width = 0;
    *out_height = 0;
    if (!session) return;

    int32_t src_w = session->crop_active ? session->crop.width : session->image_width;
    int32_t src_h = session->crop_active ? session->crop.height : session->image_height;
    if (src_w <= 0 || src_h <= 0) return;

    if (view_width <= 0 || view_height <= 0) {
        *out_width = src_w;
        *out_height = src_h;
        return;
    }

    /* Fit inside the view, and never magnify: previewing an icon at
       eight times its size would cost more than the icon is worth and
       show nothing extra. */
    double scale_w = (double)view_width / (double)src_w;
    double scale_h = (double)view_height / (double)src_h;
    double scale = scale_w < scale_h ? scale_w : scale_h;
    if (scale >= 1.0) {
        *out_width = src_w;
        *out_height = src_h;
        return;
    }

    int32_t w = (int32_t)((double)src_w * scale + 0.5);
    int32_t h = (int32_t)((double)src_h * scale + 0.5);
    *out_width = w > 0 ? w : 1;
    *out_height = h > 0 ? h : 1;
}

/* ---- commit ---- */

/* Temperature and tint are the two sliders with no direct equivalent in
   the core colour engine, so they become channel gains: warming an image
   means lifting red and dropping blue, and tint trades green against
   the other two. */
static void build_white_balance(const rubraview_edit_params_t *p,
                                uint8_t lut_r[256], uint8_t lut_g[256], uint8_t lut_b[256]) {
    double warm = (double)p->temperature / 100.0;   /* -1 .. +1 */
    double tint = (double)p->tint / 100.0;

    double gain_r = 1.0 + warm * 0.25;
    double gain_b = 1.0 - warm * 0.25;
    double gain_g = 1.0 - tint * 0.20;
    double bright = (double)p->brightness / 100.0 * 64.0;  /* ±100 -> ±64 levels */

    for (int i = 0; i < 256; ++i) {
        double base = (double)i + bright;
        double r = base * gain_r, g = base * gain_g, b = base * gain_b;
        lut_r[i] = (uint8_t)clampf((float)r, 0.0f, 255.0f);
        lut_g[i] = (uint8_t)clampf((float)g, 0.0f, 255.0f);
        lut_b[i] = (uint8_t)clampf((float)b, 0.0f, 255.0f);
    }
}

/* Composes two 256-entry maps: out[i] = second[first[i]]. Chaining LUTs
   this way means the pixels are walked once however many stages the
   session has. */
static void compose_lut(uint8_t out[256], const uint8_t first[256], const uint8_t second[256]) {
    for (int i = 0; i < 256; ++i) out[i] = second[first[i]];
}

rubraview_pixbuf_t rubraview_edit_commit(proven_arena_t *arena,
                                         const rubraview_edit_session_t *session,
                                         const rubraview_pixbuf_t *src) {
    rubraview_pixbuf_t empty = {0};
    if (!arena || !session || !rubraview_pixbuf_is_valid(src)) return empty;

    /* 1. Crop first: everything after it costs less for having been done
       in this order, and the reader expects the adjustments to apply to
       what is left rather than to what was thrown away. */
    rubraview_pixbuf_t work;
    if (session->crop_active) {
        work = rubraview_pixbuf_crop(arena, src, session->crop.x, session->crop.y,
                                     session->crop.width, session->crop.height);
    } else {
        work = rubraview_pixbuf_clone(arena, src);
    }
    if (!rubraview_pixbuf_is_valid(&work)) return empty;

    /* 2. Levels, curves and white balance are all 256-entry maps, so
       they are composed into one per channel and applied in a single
       pass. */
    uint8_t levels[256], curve_rgb[256], wb_r[256], wb_g[256], wb_b[256];
    rubraview_levels_build_lut(levels, session->params.black_point, session->params.white_point,
                               session->params.midtone_gamma);
    rubraview_curve_build_lut(curve_rgb, session->curves[RUBRAVIEW_EDIT_CHANNEL_RGB].points,
                              session->curves[RUBRAVIEW_EDIT_CHANNEL_RGB].point_count);
    build_white_balance(&session->params, wb_r, wb_g, wb_b);

    uint8_t chan[3][256];
    static const rubraview_edit_channel_t PER_CHANNEL[3] = {
        RUBRAVIEW_EDIT_CHANNEL_RED, RUBRAVIEW_EDIT_CHANNEL_GREEN, RUBRAVIEW_EDIT_CHANNEL_BLUE
    };
    const uint8_t *wb[3] = { wb_r, wb_g, wb_b };

    for (int c = 0; c < 3; ++c) {
        uint8_t per[256], stage1[256], stage2[256];
        rubraview_curve_build_lut(per, session->curves[PER_CHANNEL[c]].points,
                                  session->curves[PER_CHANNEL[c]].point_count);
        compose_lut(stage1, levels, curve_rgb);   /* levels, then the composite curve */
        compose_lut(stage2, stage1, per);         /* then this channel's own curve */
        compose_lut(chan[c], stage2, wb[c]);      /* then white balance and brightness */
    }
    rubraview_lut_apply(&work, chan[0], chan[1], chan[2]);

    /* 3. Exposure, contrast, saturation and gamma go through the core
       engine, which the batch path uses too — one implementation, so a
       preview and a batch run cannot disagree. */
    rubraview_color_adjust_params_t adjust = {
        .exposure_ev = session->params.exposure_ev,
        .contrast = session->params.contrast,
        /* §3.13's -100..+100 is the engine's 0..2 factor. */
        .saturation = 1.0f + session->params.saturation / 100.0f,
        .gamma = 1.0f,
    };
    if (adjust.exposure_ev != 0.0f || adjust.contrast != 0.0f || adjust.saturation != 1.0f) {
        rubraview_color_adjust(&work, &adjust);
    }

    /* 4. Spatial filters. Blur before sharpen: sharpening a blurred
       image is a legitimate effect, blurring a sharpened one throws the
       sharpening away. */
    if (session->blur_sigma > 0.0f) {
        rubraview_pixbuf_t blurred = rubraview_filter_gaussian_blur(arena, &work, session->blur_sigma);
        if (rubraview_pixbuf_is_valid(&blurred)) work = blurred;
    }
    if (session->sharpen_amount > 0.0f) {
        rubraview_pixbuf_t sharp = rubraview_filter_unsharp_mask(arena, &work,
                                                                 session->sharpen_radius,
                                                                 session->sharpen_amount / 100.0f,
                                                                 session->sharpen_threshold);
        if (rubraview_pixbuf_is_valid(&sharp)) work = sharp;
    }

    /* 5. Resize last, so the filters ran at the resolution the reader
       was looking at when they chose their radius. */
    if (session->resize_width > 0 && session->resize_height > 0 &&
        (session->resize_width != work.width || session->resize_height != work.height)) {
        rubraview_pixbuf_t scaled = rubraview_pixbuf_resample(arena, &work,
                                                              session->resize_width, session->resize_height,
                                                              session->resize_filter);
        if (rubraview_pixbuf_is_valid(&scaled)) work = scaled;
    }

    return work;
}
