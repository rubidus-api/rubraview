#include "rubraview/edit.h"
#include "rubraview/filter.h"
#include "rubraview/effects.h"
#include "rubraview/ini.h"
#include "rubraview/lang.h"
#include <stdio.h>
#include <stdlib.h>
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
    if (p->balance_r != 0.0f || p->balance_g != 0.0f || p->balance_b != 0.0f) return false;
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

void rubraview_edit_cycle_channel(rubraview_edit_session_t *session, int step) {
    if (!session) return;
    int n = (int)RUBRAVIEW_EDIT_CHANNEL_LUMA + 1;
    int c = ((int)session->active_channel + step % n + n) % n;
    session->active_channel = (rubraview_edit_channel_t)c;
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

bool rubraview_edit_crop_set(rubraview_edit_session_t *session, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!session || session->image_width <= 0 || session->image_height <= 0) return false;
    if (x < 0 || y < 0 || x >= session->image_width || y >= session->image_height || width < 1 || height < 1) return false;
    if (width > session->image_width - x) width = session->image_width - x;
    if (height > session->image_height - y) height = session->image_height - y;
    session->crop_ratio = RUBRAVIEW_CROP_FREE;
    session->crop = (rubraview_crop_rect_t){ .x = x, .y = y, .width = width, .height = height };
    session->crop_active = true;
    session->dirty = true;
    return true;
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

/* ---- RV-128: sizes by name, and lengths in any unit ---- */

static bool fit_box(int32_t w, int32_t h, double bw, double bh, bool keep, int32_t *ow, int32_t *oh) {
    if (bw < 1 || bh < 1) return false;
    if (!keep) { *ow = (int32_t)lround(bw); *oh = (int32_t)lround(bh); return true; }
    double s = bw / w < bh / h ? bw / w : bh / h;
    *ow = (int32_t)lround(w * s); *oh = (int32_t)lround(h * s);
    if (*ow < 1) *ow = 1;
    if (*oh < 1) *oh = 1;
    return true;
}

bool rubraview_resize_preset_size(rubraview_resize_preset_t preset, int32_t w, int32_t h,
                                  int32_t screen_w, int32_t screen_h, bool keep,
                                  int32_t *ow, int32_t *oh) {
    if (!ow || !oh || w <= 0 || h <= 0) return false;
    static const int32_t BOX[][2] = { {640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {7680, 4320} };
    bool portrait = h > w;
    int32_t r_w = 0, r_h = 0;
    bool ok;
    switch (preset) {
        case RUBRAVIEW_RESIZE_PRESET_HALF:   r_w = (int32_t)lround(w / 2.0); r_h = (int32_t)lround(h / 2.0); ok = true; break;
        case RUBRAVIEW_RESIZE_PRESET_DOUBLE: r_w = w * 2; r_h = h * 2; ok = true; break;
        case RUBRAVIEW_RESIZE_PRESET_SCREEN: ok = fit_box(w, h, screen_w, screen_h, true, &r_w, &r_h); break;   /* the screen as it stands */
        case RUBRAVIEW_RESIZE_PRESET_LONG_1024: case RUBRAVIEW_RESIZE_PRESET_LONG_2048: {
            double l = preset == RUBRAVIEW_RESIZE_PRESET_LONG_1024 ? 1024.0 : 2048.0;
            ok = fit_box(w, h, l, l, true, &r_w, &r_h);
            break;
        }
        case RUBRAVIEW_RESIZE_PRESET_MP_2: case RUBRAVIEW_RESIZE_PRESET_MP_8: case RUBRAVIEW_RESIZE_PRESET_MP_12: {
            double mp = preset == RUBRAVIEW_RESIZE_PRESET_MP_2 ? 2e6 : preset == RUBRAVIEW_RESIZE_PRESET_MP_8 ? 8e6 : 12e6;
            double s = sqrt(mp / ((double)w * (double)h));
            r_w = (int32_t)lround(w * s); r_h = (int32_t)lround(h * s);
            ok = r_w >= 1 && r_h >= 1;
            break;
        }
        default: {
            int32_t i = (int32_t)preset - (int32_t)RUBRAVIEW_RESIZE_PRESET_640;
            if (i < 0 || i >= (int32_t)(sizeof(BOX) / sizeof(BOX[0]))) return false;
            double bw = BOX[i][portrait ? 1 : 0], bh = BOX[i][portrait ? 0 : 1];
            ok = fit_box(w, h, bw, bh, keep, &r_w, &r_h);
            break;
        }
    }
    if (!ok || r_w < 1 || r_h < 1 || r_w > 65535 || r_h > 65535) return false;
    *ow = r_w; *oh = r_h;
    return true;
}

bool rubraview_resize_parse_length(proven_u8str_view_t text, int32_t whole_px, double dpi, int32_t *out_px) {
    if (!out_px || !text.ptr) return false;
    size_t i = 0, n = text.size;
    while (i < n && text.ptr[i] == ' ') ++i;
    double v = 0.0, place = 0.0;
    bool digits = false;
    for (; i < n; ++i) {
        char c = text.ptr[i];
        if (c >= '0' && c <= '9') {
            digits = true;
            if (place > 0.0) { v += (c - '0') * place; place /= 10.0; }
            else v = v * 10.0 + (c - '0');
            if (v > 1e7) return false;
        } else if ((c == '.' || c == ',') && place == 0.0) place = 0.1;
        else break;
    }
    if (!digits) return false;
    while (i < n && text.ptr[i] == ' ') ++i;
    char unit[4] = {0};
    size_t u = 0;
    for (; i < n && u < 3; ++i) {
        char c = text.ptr[i];
        if (c == ' ') break;
        unit[u++] = rubraview_ascii_lower(c);
    }
    while (i < n && text.ptr[i] == ' ') ++i;
    if (i < n) return false;
    if (dpi <= 0.0) dpi = 300.0;
    double px;
    if (u == 0 || strcmp(unit, "px") == 0) px = v;
    else if (strcmp(unit, "%") == 0) { if (whole_px <= 0) return false; px = whole_px * v / 100.0; }
    else if (strcmp(unit, "cm") == 0) px = v / 2.54 * dpi;
    else if (strcmp(unit, "mm") == 0) px = v / 25.4 * dpi;
    else if (strcmp(unit, "in") == 0 || strcmp(unit, "\"") == 0) px = v * dpi;
    else return false;
    long r = lround(px);
    if (r < 1 || r > 65535) return false;
    *out_px = (int32_t)r;
    return true;
}

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

bool rubraview_edit_resize_pair(const rubraview_edit_session_t *session, int32_t typed, bool from_width,
                                int32_t *out_width, int32_t *out_height) {
    if (!session || !out_width || !out_height || typed < 1 || typed > 65535) return false;
    int32_t src_w = session->crop_active ? session->crop.width : session->image_width;
    int32_t src_h = session->crop_active ? session->crop.height : session->image_height;
    if (src_w <= 0 || src_h <= 0) return false;
    int32_t w = from_width ? typed : *out_width, h = from_width ? *out_height : typed;
    if (session->resize_lock_aspect) {
        if (from_width) h = (int32_t)((double)typed * (double)src_h / (double)src_w + 0.5);
        else            w = (int32_t)((double)typed * (double)src_w / (double)src_h + 0.5);
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > 65535 || h > 65535) return false;
    *out_width = w;
    *out_height = h;
    return true;
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
        double r = base * gain_r + p->balance_r * 1.28, g = base * gain_g + p->balance_g * 1.28, b = base * gain_b + p->balance_b * 1.28;
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
        int32_t box = (int32_t)(session->blur_sigma + 0.5f);
        rubraview_pixbuf_t blurred = session->blur_box
            ? rubraview_filter_box_blur(arena, &work, box < 1 ? 1 : box)
            : rubraview_filter_gaussian_blur(arena, &work, session->blur_sigma);
        if (rubraview_pixbuf_is_valid(&blurred)) work = blurred;
    }
    if (session->sharpen_amount > 0.0f) {
        rubraview_pixbuf_t sharp = session->sharpen_laplacian
            ? rubraview_filter_laplacian_sharpen(arena, &work, session->sharpen_amount / 100.0f)
            : rubraview_filter_unsharp_mask(arena, &work,
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

/* ---- the owner's edit, 2026-10-17: steps on a selection ---- */

void rubraview_edit_steps_begin(rubraview_edit_steps_t *steps, int32_t width, int32_t height) {
    if (!steps) return;
    memset(steps, 0, sizeof(*steps));
    steps->width = width;
    steps->height = height;
}

bool rubraview_edit_step_does_something(const rubraview_edit_step_t *step) {
    if (!step) return false;
    switch (step->kind) {
        case RUBRAVIEW_STEP_ADJUST: {
            rubraview_edit_session_t v = step->values;
            v.crop_active = false;
            v.resize_width = v.resize_height = 0;
            return !rubraview_edit_is_neutral(&v);
        }
        case RUBRAVIEW_STEP_CROP:
            return step->area.rect.width > 0 && step->area.rect.height > 0;
        case RUBRAVIEW_STEP_RESIZE:
            return step->values.resize_width > 0 && step->values.resize_height > 0;
        case RUBRAVIEW_STEP_EFFECT:
            return step->effect > RUBRAVIEW_EFFECT_NONE && step->effect < RUBRAVIEW_EFFECT_COUNT;
        case RUBRAVIEW_STEP_PLUGIN:
            return step->plugin[0] != '\0';
        case RUBRAVIEW_STEP_GMIC:
        case RUBRAVIEW_STEP_FFMPEG:
            return step->command[0] != '\0';
    }
    return false;
}

bool rubraview_edit_steps_push(rubraview_edit_steps_t *steps, const rubraview_edit_step_t *step) {
    if (!steps || !rubraview_edit_step_does_something(step) || steps->cursor >= RUBRAVIEW_EDIT_MAX_STEPS) return false;
    steps->steps[steps->cursor++] = *step;
    steps->count = steps->cursor;
    return true;
}

bool rubraview_edit_steps_set_last(rubraview_edit_steps_t *steps, const rubraview_edit_step_t *step) {
    if (!steps || !step || steps->cursor == 0) return false;
    if (!rubraview_edit_step_does_something(step)) steps->cursor--;
    else steps->steps[steps->cursor - 1] = *step;
    steps->count = steps->cursor;
    return true;
}

rubraview_crop_rect_t rubraview_edit_rect_rebase(rubraview_crop_rect_t r, int32_t from_w, int32_t from_h, int32_t to_w, int32_t to_h) {
    if (from_w <= 0 || from_h <= 0 || to_w <= 0 || to_h <= 0) return r;
    double sx = (double)to_w / from_w, sy = (double)to_h / from_h;
    int32_t x = (int32_t)lround(r.x * sx), y = (int32_t)lround(r.y * sy);
    int32_t w = (int32_t)lround(r.width * sx), h = (int32_t)lround(r.height * sy);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > to_w - 1) x = to_w - 1;
    if (y > to_h - 1) y = to_h - 1;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (x + w > to_w) w = to_w - x;
    if (y + h > to_h) h = to_h - y;
    return (rubraview_crop_rect_t){ x, y, w, h };
}

/* RV-187: a pixel position or length of a `base`-long side held inside it. */
static int32_t held(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

void rubraview_edit_step_set_ratio(rubraview_edit_step_t *step, uint32_t bits, int32_t base_w, int32_t base_h) {
    if (!step || base_w <= 0 || base_h <= 0) return;
    bits &= RUBRAVIEW_RATIO_ALL;
    const double px[6] = { step->area.rect.x, step->area.rect.y, step->area.rect.width, step->area.rect.height,
                           step->values.resize_width, step->values.resize_height };
    for (int k = 0; k < 6; ++k) {
        uint32_t bit = 1u << k;
        if ((bits & bit) && !(step->ratio & bit)) step->frac[k] = px[k] / (double)((k % 2 == 0) ? base_w : base_h);
        if (!(bits & bit)) step->frac[k] = 0.0;
    }
    step->ratio = bits;
}

void rubraview_edit_step_fit(rubraview_edit_step_t *step, int32_t base_w, int32_t base_h) {
    if (!step || base_w <= 0 || base_h <= 0) return;
    rubraview_crop_rect_t *r = &step->area.rect;
    int32_t v[4] = { r->x, r->y, r->width, r->height };
    for (int k = 0; k < 4; ++k) {
        if (step->ratio & (1u << k)) v[k] = (int32_t)lround(step->frac[k] * ((k % 2 == 0) ? base_w : base_h));
    }
    if (step->area.shape != RUBRAVIEW_AREA_ALL || step->kind == RUBRAVIEW_STEP_CROP) {
        v[0] = held(v[0], 0, base_w - 1);
        v[1] = held(v[1], 0, base_h - 1);
        v[2] = held(v[2], 1, base_w - v[0]);
        v[3] = held(v[3], 1, base_h - v[1]);
        *r = (rubraview_crop_rect_t){ v[0], v[1], v[2], v[3] };
    }
    if (step->kind == RUBRAVIEW_STEP_RESIZE) {
        int32_t rw = step->values.resize_width, rh = step->values.resize_height;
        if (step->ratio & RUBRAVIEW_RATIO_RESIZE_W) rw = (int32_t)lround(step->frac[4] * base_w);
        if (step->ratio & RUBRAVIEW_RATIO_RESIZE_H) rh = (int32_t)lround(step->frac[5] * base_h);
        step->values.resize_width = held(rw, 1, 65535);
        step->values.resize_height = held(rh, 1, 65535);
    }
}

void rubraview_edit_steps_rebase(rubraview_edit_steps_t *steps, int32_t width, int32_t height) {
    if (!steps || width <= 0 || height <= 0) return;
    steps->width = width;
    steps->height = height;
    for (size_t i = 0; i < steps->count; ++i) {   /* each on the size the ones before it leave, already fitted */
        int32_t bw = 0, bh = 0;
        rubraview_edit_steps_size(steps, i, &bw, &bh);
        rubraview_edit_step_fit(&steps->steps[i], bw, bh);
    }
}

/* Each step fitted again now that the size it is made on may have changed (a crop or a resize moved or
   gone): a ratio the same part, pixels held inside (RV-187). */
static void steps_refit(rubraview_edit_steps_t *steps, const int32_t *old_w, const int32_t *old_h) {
    (void)old_w; (void)old_h;
    for (size_t i = 0; i < steps->count; ++i) {
        int32_t nw = 0, nh = 0;
        rubraview_edit_steps_size(steps, i, &nw, &nh);   /* the steps before it are already put right */
        rubraview_edit_step_fit(&steps->steps[i], nw, nh);
    }
}

bool rubraview_edit_steps_move(rubraview_edit_steps_t *steps, size_t index, bool up) {
    if (!steps) return false;
    steps->count = steps->cursor;   /* only the steps in force are in the list to change */
    if (index >= steps->count || (up && index == 0) || (!up && index + 1 >= steps->count)) return false;
    int32_t old_w[RUBRAVIEW_EDIT_MAX_STEPS], old_h[RUBRAVIEW_EDIT_MAX_STEPS];
    for (size_t i = 0; i < steps->count; ++i) rubraview_edit_steps_size(steps, i, &old_w[i], &old_h[i]);
    size_t other = up ? index - 1 : index + 1;
    rubraview_edit_step_t t = steps->steps[index];
    steps->steps[index] = steps->steps[other];
    steps->steps[other] = t;
    int32_t tw = old_w[index], th = old_h[index];
    old_w[index] = old_w[other]; old_h[index] = old_h[other];
    old_w[other] = tw; old_h[other] = th;
    steps_refit(steps, old_w, old_h);
    return true;
}

bool rubraview_edit_steps_move_to(rubraview_edit_steps_t *steps, size_t from, size_t to) {
    if (!steps) return false;
    size_t in_force = steps->cursor < steps->count ? steps->cursor : steps->count;
    if (from >= in_force || to >= in_force || from == to) return false;
    while (from != to) {   /* a neighbour at a time, each keeping the areas in proportion */
        if (!rubraview_edit_steps_move(steps, from, to < from)) return false;
        from = to < from ? from - 1 : from + 1;
    }
    return true;
}

size_t rubraview_edit_steps_remove(rubraview_edit_steps_t *steps, const bool *gone) {
    if (!steps || !gone) return 0;
    steps->count = steps->cursor;
    int32_t old_w[RUBRAVIEW_EDIT_MAX_STEPS], old_h[RUBRAVIEW_EDIT_MAX_STEPS];
    for (size_t i = 0; i < steps->count; ++i) rubraview_edit_steps_size(steps, i, &old_w[i], &old_h[i]);
    size_t kept = 0;
    for (size_t i = 0; i < steps->count; ++i) {
        if (gone[i]) continue;
        steps->steps[kept] = steps->steps[i];
        old_w[kept] = old_w[i];
        old_h[kept] = old_h[i];
        kept++;
    }
    size_t removed = steps->count - kept;
    steps->count = steps->cursor = kept;
    steps_refit(steps, old_w, old_h);
    return removed;
}

bool rubraview_edit_steps_undo(rubraview_edit_steps_t *steps) {
    if (!steps || steps->cursor == 0) return false;
    steps->cursor--;
    return true;
}

bool rubraview_edit_steps_redo(rubraview_edit_steps_t *steps) {
    if (!steps || steps->cursor >= steps->count) return false;
    steps->cursor++;
    return true;
}

bool rubraview_edit_steps_go(rubraview_edit_steps_t *steps, size_t cursor) {
    if (!steps || cursor > steps->count) return false;
    steps->cursor = cursor;
    return true;
}

void rubraview_edit_steps_reset(rubraview_edit_steps_t *steps) {
    if (steps) steps->count = steps->cursor = 0;
}

void rubraview_edit_steps_size(const rubraview_edit_steps_t *steps, size_t upto, int32_t *width, int32_t *height) {
    int32_t w = steps ? steps->width : 0, h = steps ? steps->height : 0;
    for (size_t i = 0; steps && i < upto && i < steps->count; ++i) {
        const rubraview_edit_step_t *s = &steps->steps[i];
        if (s->kind == RUBRAVIEW_STEP_CROP) {
            rubraview_crop_rect_t r = s->area.rect;
            if (r.x < 0) { r.width += r.x; r.x = 0; }
            if (r.y < 0) { r.height += r.y; r.y = 0; }
            if (r.x + r.width > w) r.width = w - r.x;
            if (r.y + r.height > h) r.height = h - r.y;
            if (r.width > 0 && r.height > 0) { w = r.width; h = r.height; }
        } else if (s->kind == RUBRAVIEW_STEP_RESIZE && s->values.resize_width > 0 && s->values.resize_height > 0) {
            w = s->values.resize_width;
            h = s->values.resize_height;
        }
    }
    if (width) *width = w;
    if (height) *height = h;
}

float rubraview_edit_area_weight(const rubraview_edit_area_t *area, int32_t x, int32_t y) {
    if (!area || area->shape == RUBRAVIEW_AREA_ALL) return 1.0f;
    const rubraview_crop_rect_t *r = &area->rect;
    if (r->width <= 0 || r->height <= 0) return 0.0f;
    double px = x + 0.5, py = y + 0.5, d;
    if (area->shape == RUBRAVIEW_AREA_RECT) {
        double a = px - r->x, b = r->x + r->width - px, c = py - r->y, e = r->y + r->height - py;
        d = a < b ? a : b;
        if (c < d) d = c;
        if (e < d) d = e;
    } else {
        double rx = r->width * 0.5, ry = r->height * 0.5;
        double nx = (px - (r->x + rx)) / rx, ny = (py - (r->y + ry)) / ry;
        d = (1.0 - sqrt(nx * nx + ny * ny)) * (rx < ry ? rx : ry);
    }
    if (area->feather <= 0) return d >= 0.0 ? 1.0f : 0.0f;
    double w = 0.5 + d / (double)area->feather;   /* the feather straddles the edge */
    return (float)(w < 0.0 ? 0.0 : w > 1.0 ? 1.0 : w);
}

static int32_t clamp_i(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static rubraview_plugin_runner_fn g_plugin_runner;
static void *g_plugin_runner_user;

void rubraview_edit_set_plugin_runner(rubraview_plugin_runner_fn runner, void *user) {
    g_plugin_runner = runner;
    g_plugin_runner_user = user;
}

static rubraview_pixbuf_t plugin_run(proven_arena_t *arena, const rubraview_edit_step_t *step, const rubraview_pixbuf_t *src) {
    rubraview_pixbuf_t empty = {0};
    return g_plugin_runner ? g_plugin_runner(g_plugin_runner_user, arena, step, src) : empty;
}

rubraview_pixbuf_t rubraview_edit_step_apply(proven_arena_t *arena, const rubraview_edit_step_t *step,
                                             const rubraview_pixbuf_t *src, double scale) {
    rubraview_pixbuf_t empty = {0};
    if (!arena || !step || !rubraview_pixbuf_is_valid(src)) return empty;
    if (scale <= 0.0) scale = 1.0;
    rubraview_edit_area_t area = step->area;
    area.rect.x = (int32_t)lround(area.rect.x * scale);
    area.rect.y = (int32_t)lround(area.rect.y * scale);
    area.rect.width = (int32_t)lround(area.rect.width * scale);
    area.rect.height = (int32_t)lround(area.rect.height * scale);
    area.feather = (int32_t)lround(area.feather * scale);

    if (step->kind == RUBRAVIEW_STEP_CROP) {
        int32_t x = clamp_i(area.rect.x, 0, src->width - 1), y = clamp_i(area.rect.y, 0, src->height - 1);
        int32_t w = clamp_i(area.rect.x + area.rect.width, x + 1, src->width) - x;
        int32_t h = clamp_i(area.rect.y + area.rect.height, y + 1, src->height) - y;
        return rubraview_pixbuf_crop(arena, src, x, y, w, h);
    }
    if (step->kind == RUBRAVIEW_STEP_RESIZE) {
        int32_t w = (int32_t)lround(step->values.resize_width * scale), h = (int32_t)lround(step->values.resize_height * scale);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        rubraview_pixbuf_t out = step->values.resize_linear
            ? rubraview_pixbuf_resample_linear(arena, src, w, h, step->values.resize_filter)
            : rubraview_pixbuf_resample(arena, src, w, h, step->values.resize_filter);
        /* RV-128: a reduction softens; a light unsharp mask after it, if asked. */
        if (out.pixels && step->values.resize_sharpen > 0.0f) {
            rubraview_pixbuf_t sharp = rubraview_filter_unsharp_mask(arena, &out, 0.6f, step->values.resize_sharpen / 100.0f * 1.5f, 2);
            if (sharp.pixels) out = sharp;
        }
        return out;
    }

    rubraview_edit_session_t v = step->values;
    v.crop_active = false;
    v.resize_width = v.resize_height = 0;
    v.blur_sigma = v.blur_sigma > 0.0f ? (float)(v.blur_sigma * scale) : 0.0f;
    if (v.blur_sigma > 0.0f && v.blur_sigma < 0.3f) v.blur_sigma = 0.3f;
    v.sharpen_radius = (float)(v.sharpen_radius * scale);
    if (v.sharpen_radius < 0.5f) v.sharpen_radius = 0.5f;
    bool effect = step->kind == RUBRAVIEW_STEP_EFFECT;
    /* RFC-0007: blended by the area as an effect is; RFC-0009 C and D: G'MIC and FFmpeg steps run the same way */
    bool plugin = step->kind == RUBRAVIEW_STEP_PLUGIN || step->kind == RUBRAVIEW_STEP_GMIC || step->kind == RUBRAVIEW_STEP_FFMPEG;
    double amount = step->amount;
    if (effect && rubraview_effect_amount_is_pixels((rubraview_effect_t)step->effect)) {
        amount *= scale;
        double lo = 0.0;
        rubraview_effect_range((rubraview_effect_t)step->effect, &lo, NULL, NULL);
        if (fabs(amount) < fabs(lo) || (lo > 0.0 && amount < lo)) amount = amount < 0.0 ? -fabs(lo) : lo;
    }
    if (area.shape == RUBRAVIEW_AREA_ALL) {
        return plugin ? plugin_run(arena, step, src)
             : effect ? rubraview_effect_apply(arena, src, (rubraview_effect_t)step->effect, amount)
                      : rubraview_edit_commit(arena, &v, src);
    }

    /* Only what the area touches is worked on, with a margin for the blur. */
    int32_t margin = area.feather + (int32_t)ceil(v.blur_sigma * 3.0f + v.sharpen_radius * 3.0f) + 2 +
                     (effect ? (int32_t)ceil(fabs(amount)) + 2 : 0);
    int32_t x0 = clamp_i(area.rect.x - margin, 0, src->width), y0 = clamp_i(area.rect.y - margin, 0, src->height);
    int32_t x1 = clamp_i(area.rect.x + area.rect.width + margin, 0, src->width);
    int32_t y1 = clamp_i(area.rect.y + area.rect.height + margin, 0, src->height);
    rubraview_pixbuf_t out = rubraview_pixbuf_clone(arena, src);
    if (!rubraview_pixbuf_is_valid(&out) || x1 <= x0 || y1 <= y0) return out;
    rubraview_pixbuf_t piece = rubraview_pixbuf_crop(arena, src, x0, y0, x1 - x0, y1 - y0);
    rubraview_pixbuf_t done = !rubraview_pixbuf_is_valid(&piece) ? empty
        : plugin ? plugin_run(arena, step, &piece)
        : effect ? rubraview_effect_apply(arena, &piece, (rubraview_effect_t)step->effect, amount)
                 : rubraview_edit_commit(arena, &v, &piece);
    if (plugin && !rubraview_pixbuf_is_valid(&done)) return empty;   /* it did not run: the step fails */
    if (!rubraview_pixbuf_is_valid(&done) || done.width != piece.width || done.height != piece.height) return out;
    for (int32_t y = y0; y < y1; ++y) {
        uint8_t *o = out.pixels + (size_t)y * (size_t)out.stride;
        const uint8_t *a = done.pixels + (size_t)(y - y0) * (size_t)done.stride;
        for (int32_t x = x0; x < x1; ++x) {
            float w = rubraview_edit_area_weight(&area, x, y);
            if (w <= 0.0f) continue;
            uint8_t *p = o + (size_t)x * 4u;
            const uint8_t *q = a + (size_t)(x - x0) * 4u;
            for (int c = 0; c < 4; ++c) p[c] = (uint8_t)lroundf(p[c] + (q[c] - p[c]) * w);
        }
    }
    return out;
}

rubraview_pixbuf_t rubraview_edit_steps_apply(proven_arena_t *arena, const rubraview_edit_steps_t *steps,
                                              const rubraview_pixbuf_t *src, double scale) {
    rubraview_pixbuf_t empty = {0};
    if (!arena || !steps || !rubraview_pixbuf_is_valid(src)) return empty;
    rubraview_pixbuf_t work = rubraview_pixbuf_clone(arena, src);
    for (size_t i = 0; i < steps->cursor && i < steps->count && rubraview_pixbuf_is_valid(&work); ++i) {
        rubraview_pixbuf_t next = rubraview_edit_step_apply(arena, &steps->steps[i], &work, scale);
        if (rubraview_pixbuf_is_valid(&next)) work = next;
    }
    return work;
}

void rubraview_edit_auto_colour(rubraview_edit_session_t *session, const rubraview_pixbuf_t *image) {
    if (!session || !rubraview_pixbuf_is_valid(image)) return;
    if (image->format != RUBRAVIEW_PIXFMT_RGBA8 && image->format != RUBRAVIEW_PIXFMT_BGRA8) return;
    uint32_t hist[3][256];
    memset(hist, 0, sizeof(hist));
    bool bgra = image->format == RUBRAVIEW_PIXFMT_BGRA8;
    uint64_t total = 0;
    for (int32_t y = 0; y < image->height; ++y) {
        const uint8_t *row = image->pixels + (size_t)y * (size_t)image->stride;
        for (int32_t x = 0; x < image->width; ++x) {
            const uint8_t *p = row + (size_t)x * 4u;
            if (p[3] < 16) continue;   /* see-through pixels say nothing of the colour */
            hist[0][bgra ? p[2] : p[0]]++;
            hist[1][p[1]]++;
            hist[2][bgra ? p[0] : p[2]]++;
            total++;
        }
    }
    if (total == 0) return;
    static const rubraview_edit_channel_t CH[3] = { RUBRAVIEW_EDIT_CHANNEL_RED, RUBRAVIEW_EDIT_CHANNEL_GREEN, RUBRAVIEW_EDIT_CHANNEL_BLUE };
    double mean_after[3];
    for (int c = 0; c < 3; ++c) {
        uint64_t cut = total / 200u, seen = 0;   /* 0.5 % at each end */
        int lo = 0, hi = 255;
        for (int i = 0; i < 256; ++i) { seen += hist[c][i]; if (seen > cut) { lo = i; break; } }
        seen = 0;
        for (int i = 255; i >= 0; --i) { seen += hist[c][i]; if (seen > cut) { hi = i; break; } }
        if (hi - lo < 16) { lo = 0; hi = 255; }   /* nearly flat: left as it is */
        rubraview_edit_curve_t *curve = &session->curves[CH[c]];
        curve->point_count = 2;
        curve->points[0] = (rubraview_curve_point_t){ (float)lo, 0.0f };
        curve->points[1] = (rubraview_curve_point_t){ (float)hi, 255.0f };
        /* the channel's mean once stretched */
        double sum = 0.0;
        for (int i = 0; i < 256; ++i) {
            double v = i <= lo ? 0.0 : i >= hi ? 255.0 : (double)(i - lo) * 255.0 / (double)(hi - lo);
            sum += v * hist[c][i];
        }
        mean_after[c] = sum / (double)total;
    }
    /* Grey world, halfway: each channel's mean moved half the way to the three's. */
    double grey = (mean_after[0] + mean_after[1] + mean_after[2]) / 3.0;
    float *bal[3] = { &session->params.balance_r, &session->params.balance_g, &session->params.balance_b };
    for (int c = 0; c < 3; ++c) {
        double shift = (grey - mean_after[c]) * 0.5 / 1.28;
        *bal[c] = (float)(shift < -100.0 ? -100.0 : shift > 100.0 ? 100.0 : shift);
    }
    session->dirty = true;
}

/* ---- RV-187 (D-220): the steps as a .rvsteps file ---- */

/* The session's numbers a step carries, one list for the writer, the reader and the comparison. */
#define STEP_FLOATS(X) \
    X(exposure, params.exposure_ev, -3.0, 3.0) X(brightness, params.brightness, -100.0, 100.0) \
    X(contrast, params.contrast, -100.0, 100.0) X(saturation, params.saturation, -100.0, 100.0) \
    X(temperature, params.temperature, -100.0, 100.0) X(tint, params.tint, -100.0, 100.0) \
    X(gamma, params.midtone_gamma, 0.1, 10.0) X(balance_r, params.balance_r, -100.0, 100.0) \
    X(balance_g, params.balance_g, -100.0, 100.0) X(balance_b, params.balance_b, -100.0, 100.0) \
    X(blur, blur_sigma, 0.0, 50.0) X(sharpen, sharpen_amount, 0.0, 300.0) X(sharpen_radius, sharpen_radius, 0.0, 10.0) \
    X(resize_sharpen, resize_sharpen, 0.0, 100.0)
#define STEP_INTS(X) \
    X(black, params.black_point, 0, 254) X(white, params.white_point, 1, 255) X(sharpen_threshold, sharpen_threshold, 0, 255) \
    X(resize_width, resize_width, 0, 65535) X(resize_height, resize_height, 0, 65535) X(resize_filter, resize_filter, 0, 3)
#define STEP_BOOLS(X) \
    X(blur_box, blur_box) X(sharpen_laplacian, sharpen_laplacian) X(resize_lock, resize_lock_aspect) X(resize_linear, resize_linear)

static const char *const STEP_KINDS[] = { "adjust", "crop", "resize", "effect", "plugin", "gmic", "ffmpeg" };
static const char *const AREA_SHAPES[] = { "all", "rect", "ellipse" };
static const char *const CURVE_NAMES[5] = { "curve_rgb", "curve_red", "curve_green", "curve_blue", "curve_luma" };
static const char *const RATIO_WORDS[6] = { "x", "y", "w", "h", "rw", "rh" };
static const char *const FRAC_KEYS[6] = { "frac_x", "frac_y", "frac_w", "frac_h", "frac_rw", "frac_rh" };

static size_t bounded_len(const char *text, size_t cap) { size_t n = 0; while (n < cap && text[n]) ++n; return n; }

static proven_u8str_view_t steps_cstr(const char *text) { return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = strlen(text) }; }

/* A number written so it reads back the same (TOML: digits both sides of the point, no exponent). */
static void steps_set_number(proven_arena_t *arena, rubraview_ini_doc_t *doc, proven_u8str_view_t section, const char *key, double v) {
    char text[64];
    int n = snprintf(text, sizeof(text), "%.17g", v);
    if (n > 0 && (strchr(text, 'e') || strchr(text, 'E'))) {
        n = snprintf(text, sizeof(text), "%.30f", v);
        while (n > 2 && text[n - 1] == '0' && text[n - 2] != '.') text[--n] = '\0';
    } else if (n > 0 && (size_t)n + 2 < sizeof(text) && !strchr(text, '.') && !strchr(text, 'n') && !strchr(text, 'i')) {
        text[n++] = '.'; text[n++] = '0'; text[n] = '\0';
    }
    if (n <= 0 || !isfinite(v)) { text[0] = '0'; text[1] = '.'; text[2] = '0'; text[3] = '\0'; }
    size_t len = strlen(text);
    proven_result_mem_mut_t m = proven_arena_alloc(arena, len + 1);   /* the document keeps a view of it */
    if (!proven_is_ok(m.err)) return;
    memcpy(m.value.ptr, text, len + 1);
    rubraview_ini_set(arena, doc, section, steps_cstr(key), (proven_u8str_view_t){ .ptr = m.value.ptr, .size = len });
}

proven_u8str_view_t rubraview_edit_steps_serialize(proven_arena_t *arena, const rubraview_edit_steps_t *steps) {
    if (!arena || !steps) return PROVEN_LIT("");
    rubraview_ini_doc_t doc = {0};
    proven_u8str_view_t head = PROVEN_LIT("steps");
    size_t in_force = steps->cursor < steps->count ? steps->cursor : steps->count;
    rubraview_ini_set_int(arena, &doc, head, PROVEN_LIT("version"), 1);
    rubraview_ini_set_int(arena, &doc, head, PROVEN_LIT("width"), steps->width);
    rubraview_ini_set_int(arena, &doc, head, PROVEN_LIT("height"), steps->height);
    rubraview_ini_set_int(arena, &doc, head, PROVEN_LIT("count"), (long long)in_force);
    for (size_t i = 0; i < in_force; ++i) {
        const rubraview_edit_step_t *st = &steps->steps[i];
        char name[32];
        snprintf(name, sizeof(name), "step-%u", (unsigned)(i + 1));
        proven_result_mem_mut_t m = proven_arena_alloc(arena, strlen(name) + 1);
        if (!proven_is_ok(m.err)) return PROVEN_LIT("");
        memcpy(m.value.ptr, name, strlen(name) + 1);
        proven_u8str_view_t sec = { .ptr = m.value.ptr, .size = strlen(name) };
        rubraview_ini_set_string(arena, &doc, sec, PROVEN_LIT("kind"), steps_cstr(STEP_KINDS[st->kind <= RUBRAVIEW_STEP_FFMPEG ? st->kind : 0]));
        rubraview_ini_set_string(arena, &doc, sec, PROVEN_LIT("area"), steps_cstr(AREA_SHAPES[st->area.shape <= RUBRAVIEW_AREA_ELLIPSE ? st->area.shape : 0]));
        rubraview_ini_set_int(arena, &doc, sec, PROVEN_LIT("x"), st->area.rect.x);
        rubraview_ini_set_int(arena, &doc, sec, PROVEN_LIT("y"), st->area.rect.y);
        rubraview_ini_set_int(arena, &doc, sec, PROVEN_LIT("w"), st->area.rect.width);
        rubraview_ini_set_int(arena, &doc, sec, PROVEN_LIT("h"), st->area.rect.height);
        rubraview_ini_set_int(arena, &doc, sec, PROVEN_LIT("feather"), st->area.feather);
        char words[40] = "";
        size_t used = 0;
        for (int k = 0; k < 6; ++k) {
            if (!(st->ratio & (1u << k))) continue;
            int n = snprintf(words + used, sizeof(words) - used, "%s%s", used ? " " : "", RATIO_WORDS[k]);
            if (n > 0) used += (size_t)n;
            steps_set_number(arena, &doc, sec, FRAC_KEYS[k], st->frac[k]);
        }
        proven_result_mem_mut_t wm = proven_arena_alloc(arena, used + 1);
        if (!proven_is_ok(wm.err)) return PROVEN_LIT("");
        memcpy(wm.value.ptr, words, used + 1);
        rubraview_ini_set_string(arena, &doc, sec, PROVEN_LIT("ratio"), (proven_u8str_view_t){ .ptr = wm.value.ptr, .size = used });
        const rubraview_edit_session_t *v = &st->values;
        bool valued = st->kind == RUBRAVIEW_STEP_ADJUST || st->kind == RUBRAVIEW_STEP_RESIZE;   /* the others have no values */
#define WRITE_FLOAT(key, field, lo, hi) steps_set_number(arena, &doc, sec, #key, (double)v->field);
#define WRITE_INT(key, field, lo, hi) rubraview_ini_set_int(arena, &doc, sec, PROVEN_LIT(#key), (long long)v->field);
#define WRITE_BOOL(key, field) rubraview_ini_set_bool(arena, &doc, sec, PROVEN_LIT(#key), v->field);
        if (valued) {
        STEP_FLOATS(WRITE_FLOAT)
        steps_set_number(arena, &doc, sec, "resize_dpi", v->resize_dpi);
        STEP_INTS(WRITE_INT)
        STEP_BOOLS(WRITE_BOOL)
        }
#undef WRITE_FLOAT
#undef WRITE_INT
#undef WRITE_BOOL
        for (int c = 0; valued && c < 5; ++c) {   /* "x,y x,y ..." */
            char line[16 * 48] = "";
            size_t at = 0;
            for (size_t p = 0; p < v->curves[c].point_count && p < RUBRAVIEW_EDIT_MAX_CURVE_POINTS; ++p) {
                int n = snprintf(line + at, sizeof(line) - at, "%s%.9g,%.9g", at ? " " : "", (double)v->curves[c].points[p].x,
                                 (double)v->curves[c].points[p].y);
                if (n > 0 && (size_t)n < sizeof(line) - at) at += (size_t)n;
            }
            proven_result_mem_mut_t cm = proven_arena_alloc(arena, at + 1);
            if (!proven_is_ok(cm.err)) return PROVEN_LIT("");
            memcpy(cm.value.ptr, line, at + 1);
            rubraview_ini_set_string(arena, &doc, sec, steps_cstr(CURVE_NAMES[c]), (proven_u8str_view_t){ .ptr = cm.value.ptr, .size = at });
        }
        if (st->kind == RUBRAVIEW_STEP_EFFECT) {
            rubraview_ini_set_int(arena, &doc, sec, PROVEN_LIT("effect"), st->effect);
            steps_set_number(arena, &doc, sec, "amount", st->amount);
        }
        if (st->kind == RUBRAVIEW_STEP_PLUGIN) {
            char hex[65];
            for (int b = 0; b < 32; ++b) snprintf(hex + b * 2, 3, "%02x", st->plugin_sha256[b]);
            char params[8 * 32] = "";
            size_t at = 0;
            for (int32_t p = 0; p < st->plugin_param_count && p < 8; ++p) {
                int n = snprintf(params + at, sizeof(params) - at, "%s%.17g", at ? " " : "", st->plugin_params[p]);
                if (n > 0 && (size_t)n < sizeof(params) - at) at += (size_t)n;
            }
            proven_result_mem_mut_t pm = proven_arena_alloc(arena, sizeof(st->plugin) + 65 + at + 3);
            if (!proven_is_ok(pm.err)) return PROVEN_LIT("");
            char *store = (char *)pm.value.ptr;
            size_t pn = bounded_len(st->plugin, sizeof(st->plugin) - 1);
            memcpy(store, st->plugin, pn); store[pn] = '\0';
            memcpy(store + pn + 1, hex, 65);
            memcpy(store + pn + 66, params, at + 1);
            rubraview_ini_set_string(arena, &doc, sec, PROVEN_LIT("plugin"), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)store, .size = pn });
            rubraview_ini_set_string(arena, &doc, sec, PROVEN_LIT("plugin_sha256"), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)store + pn + 1, .size = 64 });
            rubraview_ini_set_string(arena, &doc, sec, PROVEN_LIT("plugin_params"), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)store + pn + 66, .size = at });
        }
        if (st->kind == RUBRAVIEW_STEP_GMIC || st->kind == RUBRAVIEW_STEP_FFMPEG) {
            size_t cn = bounded_len(st->command, sizeof(st->command) - 1);
            rubraview_ini_set_string(arena, &doc, sec, PROVEN_LIT("command"), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)st->command, .size = cn });
        }
    }
    proven_u8str_view_t body = rubraview_ini_serialize(arena, &doc);
    if (body.size == 0) return body;
    static const char HEAD[] = "# Rubraview edit steps (Edit > Save steps; Batch > Edit's steps)\n";
    proven_result_mem_mut_t out = proven_arena_alloc(arena, sizeof(HEAD) - 1 + body.size + 1);
    if (!proven_is_ok(out.err)) return PROVEN_LIT("");
    memcpy(out.value.ptr, HEAD, sizeof(HEAD) - 1);
    memcpy(out.value.ptr + sizeof(HEAD) - 1, body.ptr, body.size);
    out.value.ptr[sizeof(HEAD) - 1 + body.size] = '\0';
    return (proven_u8str_view_t){ .ptr = out.value.ptr, .size = sizeof(HEAD) - 1 + body.size };
}

static bool steps_word(proven_u8str_view_t v, const char *const *words, int count, int *out) {
    for (int i = 0; i < count; ++i) if (proven_u8str_view_eq(v, steps_cstr(words[i]))) { *out = i; return true; }
    return false;
}

static double held_d(double v, double lo, double hi) { return !(v >= lo) ? lo : v > hi ? hi : v; }

bool rubraview_edit_steps_parse(proven_arena_t *arena, proven_u8str_view_t text, rubraview_edit_steps_t *out, const char **why) {
    const char *said = NULL;
    if (!why) why = &said;
    *why = NULL;
    if (!arena || !out) { *why = N_("nothing to read into"); return false; }
    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, text);
    proven_u8str_view_t head = PROVEN_LIT("steps");
    if (!rubraview_ini_get(&doc, head, PROVEN_LIT("version"))) { *why = N_("not a file of Edit's steps"); return false; }
    if (rubraview_ini_get_int(&doc, head, PROVEN_LIT("version"), 0) != 1) { *why = N_("a newer version of Edit's steps"); return false; }
    long long count = rubraview_ini_get_int(&doc, head, PROVEN_LIT("count"), -1);
    long long w = rubraview_ini_get_int(&doc, head, PROVEN_LIT("width"), 0), h = rubraview_ini_get_int(&doc, head, PROVEN_LIT("height"), 0);
    if (count < 0 || count > RUBRAVIEW_EDIT_MAX_STEPS) { *why = N_("more steps than Edit holds (64)"); return false; }
    if (w < 1 || h < 1 || w > 1000000 || h > 1000000) { *why = N_("the picture's size is missing"); return false; }
    rubraview_edit_steps_t steps;
    rubraview_edit_steps_begin(&steps, (int32_t)w, (int32_t)h);
    for (long long i = 0; i < count; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "step-%u", (unsigned)(i + 1));
        proven_u8str_view_t sec = steps_cstr(name);
        rubraview_edit_step_t st = {0};
        st.values = rubraview_edit_begin((int32_t)w, (int32_t)h);
        int kind = 0, shape = 0;
        const proven_u8str_view_t *k = rubraview_ini_get(&doc, sec, PROVEN_LIT("kind"));
        if (!k || !steps_word(*k, STEP_KINDS, 7, &kind)) { *why = N_("a step of a kind this version has not"); return false; }
        st.kind = (rubraview_edit_step_kind_t)kind;
        const proven_u8str_view_t *a = rubraview_ini_get(&doc, sec, PROVEN_LIT("area"));
        if (a && !steps_word(*a, AREA_SHAPES, 3, &shape)) { *why = N_("a step's area is of no shape this version has"); return false; }
        st.area.shape = (rubraview_area_shape_t)shape;
        st.area.rect.x = (int32_t)held_d((double)rubraview_ini_get_int(&doc, sec, PROVEN_LIT("x"), 0), 0, 1000000);
        st.area.rect.y = (int32_t)held_d((double)rubraview_ini_get_int(&doc, sec, PROVEN_LIT("y"), 0), 0, 1000000);
        st.area.rect.width = (int32_t)held_d((double)rubraview_ini_get_int(&doc, sec, PROVEN_LIT("w"), 0), 0, 1000000);
        st.area.rect.height = (int32_t)held_d((double)rubraview_ini_get_int(&doc, sec, PROVEN_LIT("h"), 0), 0, 1000000);
        st.area.feather = (int32_t)held_d((double)rubraview_ini_get_int(&doc, sec, PROVEN_LIT("feather"), 0), 0, 10000);
        const proven_u8str_view_t *r = rubraview_ini_get(&doc, sec, PROVEN_LIT("ratio"));
        for (size_t p = 0; r && p < r->size;) {   /* "x y w h rw rh", words between spaces */
            size_t e = p;
            while (e < r->size && r->ptr[e] != ' ') ++e;
            int bit = -1;
            if (e > p) (void)steps_word((proven_u8str_view_t){ .ptr = r->ptr + p, .size = e - p }, RATIO_WORDS, 6, &bit);
            if (bit >= 0) {
                st.ratio |= 1u << bit;
                st.frac[bit] = held_d(rubraview_ini_get_float(&doc, sec, steps_cstr(FRAC_KEYS[bit]), 0.0), 0.0, bit >= 4 ? 100.0 : 1.0);
            }
            p = e + 1;
        }
        rubraview_edit_session_t *v = &st.values;
#define READ_FLOAT(key, field, lo, hi) v->field = (float)held_d(rubraview_ini_get_float(&doc, sec, PROVEN_LIT(#key), (double)v->field), lo, hi);
#define READ_INT(key, field, lo, hi) v->field = (typeof(v->field))held_d((double)rubraview_ini_get_int(&doc, sec, PROVEN_LIT(#key), (long long)v->field), lo, hi);
#define READ_BOOL(key, field) v->field = rubraview_ini_get_bool(&doc, sec, PROVEN_LIT(#key), v->field);
        STEP_FLOATS(READ_FLOAT)
        v->resize_dpi = held_d(rubraview_ini_get_float(&doc, sec, PROVEN_LIT("resize_dpi"), v->resize_dpi), 0.0, 10000.0);
        STEP_INTS(READ_INT)
        STEP_BOOLS(READ_BOOL)
#undef READ_FLOAT
#undef READ_INT
#undef READ_BOOL
        if (v->params.white_point <= v->params.black_point) v->params.white_point = (uint8_t)(v->params.black_point + 1);
        for (int c = 0; c < 5; ++c) {
            const proven_u8str_view_t *cv = rubraview_ini_get(&doc, sec, steps_cstr(CURVE_NAMES[c]));
            if (!cv || cv->size == 0) continue;
            char line[16 * 48];
            size_t n = cv->size < sizeof(line) - 1 ? cv->size : sizeof(line) - 1;
            memcpy(line, cv->ptr, n);
            line[n] = '\0';
            rubraview_edit_curve_t curve = { .point_count = 0 };
            char *at = line;
            while (*at && curve.point_count < RUBRAVIEW_EDIT_MAX_CURVE_POINTS) {
                char *end = NULL;
                double x = strtod(at, &end);
                if (end == at || *end != ',') break;
                at = end + 1;
                double y = strtod(at, &end);
                if (end == at) break;
                curve.points[curve.point_count++] = (rubraview_curve_point_t){ .x = (float)held_d(x, 0.0, 255.0), .y = (float)held_d(y, 0.0, 255.0) };
                at = end;
                while (*at == ' ') ++at;
            }
            if (curve.point_count >= 2) v->curves[c] = curve;
        }
        st.effect = (int32_t)held_d((double)rubraview_ini_get_int(&doc, sec, PROVEN_LIT("effect"), 0), 0, RUBRAVIEW_EFFECT_COUNT - 1);
        st.amount = held_d(rubraview_ini_get_float(&doc, sec, PROVEN_LIT("amount"), 0.0), -100000.0, 100000.0);
        const proven_u8str_view_t *pl = rubraview_ini_get(&doc, sec, PROVEN_LIT("plugin"));
        if (pl) {
            if (pl->size >= sizeof(st.plugin)) { *why = N_("a plug-in's name is too long"); return false; }
            memcpy(st.plugin, pl->ptr, pl->size);
            st.plugin[pl->size] = '\0';
            const proven_u8str_view_t *hx = rubraview_ini_get(&doc, sec, PROVEN_LIT("plugin_sha256"));
            for (int b = 0; hx && hx->size == 64 && b < 32; ++b) {
                char two[3] = { (char)hx->ptr[b * 2], (char)hx->ptr[b * 2 + 1], '\0' };
                st.plugin_sha256[b] = (uint8_t)strtoul(two, NULL, 16);
            }
            const proven_u8str_view_t *pp = rubraview_ini_get(&doc, sec, PROVEN_LIT("plugin_params"));
            char line[8 * 40];
            size_t n = pp ? (pp->size < sizeof(line) - 1 ? pp->size : sizeof(line) - 1) : 0;
            if (pp && n > 0) memcpy(line, pp->ptr, n);   /* an empty value has no bytes behind it */
            line[n] = '\0';
            char *at = line;
            while (*at && st.plugin_param_count < 8) {
                char *end = NULL;
                double p = strtod(at, &end);
                if (end == at) break;
                st.plugin_params[st.plugin_param_count++] = isfinite(p) ? p : 0.0;
                at = end;
                while (*at == ' ') ++at;
            }
        }
        const proven_u8str_view_t *cmd = rubraview_ini_get(&doc, sec, PROVEN_LIT("command"));
        if (cmd) {
            if (cmd->size >= sizeof(st.command)) { *why = N_("a step's command is longer than Edit takes (1 KiB)"); return false; }
            if (cmd->size > 0) memcpy(st.command, cmd->ptr, cmd->size);   /* `command = ""`: no bytes (fuzz, 2026-10-31) */
            st.command[cmd->size] = '\0';
        }
        steps.steps[steps.count++] = st;
    }
    steps.cursor = steps.count;
    *out = steps;
    return true;
}

bool rubraview_edit_steps_equal(const rubraview_edit_steps_t *a, const rubraview_edit_steps_t *b) {
    if (!a || !b) return false;
    size_t na = a->cursor < a->count ? a->cursor : a->count, nb = b->cursor < b->count ? b->cursor : b->count;
    if (na != nb || a->width != b->width || a->height != b->height) return false;
    for (size_t i = 0; i < na; ++i) {
        const rubraview_edit_step_t *x = &a->steps[i], *y = &b->steps[i];
        if (x->kind != y->kind || x->area.shape != y->area.shape || x->area.feather != y->area.feather ||
            x->area.rect.x != y->area.rect.x || x->area.rect.y != y->area.rect.y ||
            x->area.rect.width != y->area.rect.width || x->area.rect.height != y->area.rect.height ||
            x->ratio != y->ratio) return false;
        if (x->kind == RUBRAVIEW_STEP_EFFECT && (x->effect != y->effect || x->amount != y->amount)) return false;
        for (int k = 0; k < 6; ++k) if ((x->ratio & (1u << k)) && x->frac[k] != y->frac[k]) return false;
        const rubraview_edit_session_t *u = &x->values, *v = &y->values;
        bool valued = x->kind == RUBRAVIEW_STEP_ADJUST || x->kind == RUBRAVIEW_STEP_RESIZE;
        if (valued) {
#define SAME(key, field, ...) if (u->field != v->field) return false;
#define SAME_BOOL(key, field) if (u->field != v->field) return false;
        STEP_FLOATS(SAME)
        if (u->resize_dpi != v->resize_dpi) return false;
        STEP_INTS(SAME)
        STEP_BOOLS(SAME_BOOL)
#undef SAME
#undef SAME_BOOL
        for (int c = 0; c < 5; ++c) {
            if (u->curves[c].point_count != v->curves[c].point_count) return false;
            for (size_t p = 0; p < u->curves[c].point_count; ++p)
                if (u->curves[c].points[p].x != v->curves[c].points[p].x || u->curves[c].points[p].y != v->curves[c].points[p].y) return false;
        }
        }
        if (x->kind == RUBRAVIEW_STEP_PLUGIN) {
            if (strcmp(x->plugin, y->plugin) != 0 || memcmp(x->plugin_sha256, y->plugin_sha256, 32) != 0 ||
                x->plugin_param_count != y->plugin_param_count) return false;
            for (int32_t p = 0; p < x->plugin_param_count; ++p) if (x->plugin_params[p] != y->plugin_params[p]) return false;
        }
        if ((x->kind == RUBRAVIEW_STEP_GMIC || x->kind == RUBRAVIEW_STEP_FFMPEG) && strcmp(x->command, y->command) != 0) return false;
    }
    return true;
}
