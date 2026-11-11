#include "rubraview/vgrade.h"
#include "rubraview/color.h"
#include "rubraview/filter.h"
#include "rubraview/lang.h"
#include <math.h>

/* RV-270: a film's live adjustment (vgrade.h). */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const rubraview_vgrade_range_t RANGES[RUBRAVIEW_VGRADE_COUNT] = {
    [RUBRAVIEW_VGRADE_EXPOSURE]       = { "exposure", N_("Exposure"),       -3.0f,   3.0f,  0.0f, 0.05f },
    [RUBRAVIEW_VGRADE_BRIGHTNESS]     = { "brightness", N_("Brightness"),   -100.0f, 100.0f,  0.0f, 1.0f },
    [RUBRAVIEW_VGRADE_CONTRAST]       = { "contrast", N_("Contrast"),     -100.0f, 100.0f,  0.0f, 1.0f },
    [RUBRAVIEW_VGRADE_SATURATION]     = { "saturation", N_("Saturation"),   -100.0f, 100.0f,  0.0f, 1.0f },
    [RUBRAVIEW_VGRADE_TEMPERATURE]    = { "temperature", N_("Temperature"),  -100.0f, 100.0f,  0.0f, 1.0f },
    [RUBRAVIEW_VGRADE_TINT]           = { "tint", N_("Tint"),         -100.0f, 100.0f,  0.0f, 1.0f },
    [RUBRAVIEW_VGRADE_GAMMA]          = { "gamma", N_("Midtone gamma"),   0.2f,   5.0f,  1.0f, 0.01f },
    [RUBRAVIEW_VGRADE_SHADOWS]        = { "shadows", N_("Curve: shadows"), -100.0f, 100.0f, 0.0f, 1.0f },
    [RUBRAVIEW_VGRADE_MIDTONES]       = { "midtones", N_("Curve: midtones"), -100.0f, 100.0f, 0.0f, 1.0f },
    [RUBRAVIEW_VGRADE_HIGHLIGHTS]     = { "highlights", N_("Curve: highlights"), -100.0f, 100.0f, 0.0f, 1.0f },
    [RUBRAVIEW_VGRADE_BLUR]           = { "blur", N_("Blur"),            0.0f,   3.0f,  0.0f, 0.1f },
    [RUBRAVIEW_VGRADE_SHARPEN]        = { "sharpen", N_("Sharpen"),         0.0f, 300.0f,  0.0f, 5.0f },
    [RUBRAVIEW_VGRADE_SHARPEN_RADIUS] = { "sharpen_radius", N_("Sharpen radius"),  0.5f,   3.0f,  1.0f, 0.1f },
    [RUBRAVIEW_VGRADE_UPSCALE]        = { "upscale", N_("Upscale"),         0.0f, 100.0f,  0.0f, 1.0f },
};

/* The curve's three points: 100 on a slider moves its point 50 levels. */
#define CURVE_LEVELS_PER_UNIT 0.5f
/* A blur under this sigma does nothing visible (and the Gaussian blur passes it by under 0.1). */
#define BLUR_FROM 0.15f

const rubraview_vgrade_range_t *rubraview_vgrade_range(rubraview_vgrade_value_t value) {
    if ((int)value < 0 || value >= RUBRAVIEW_VGRADE_COUNT) return NULL;
    return &RANGES[value];
}

void rubraview_vgrade_reset(rubraview_vgrade_t *g) {
    if (!g) return;
    for (int i = 0; i < RUBRAVIEW_VGRADE_COUNT; ++i) g->v[i] = RANGES[i].neutral;
}

void rubraview_vgrade_clamp(rubraview_vgrade_t *g) {
    if (!g) return;
    for (int i = 0; i < RUBRAVIEW_VGRADE_COUNT; ++i) {
        float v = g->v[i];
        if (!isfinite(v)) v = RANGES[i].neutral;
        if (v < RANGES[i].min) v = RANGES[i].min;
        if (v > RANGES[i].max) v = RANGES[i].max;
        g->v[i] = v;
    }
}

bool rubraview_vgrade_is_neutral(const rubraview_vgrade_t *g) {
    if (!g) return true;
    for (int i = 0; i < RUBRAVIEW_VGRADE_COUNT; ++i) {
        if (i == RUBRAVIEW_VGRADE_SHARPEN_RADIUS) continue;
        if (g->v[i] != RANGES[i].neutral) return false;
    }
    return true;
}

static bool curve_moved(const rubraview_vgrade_t *g) {
    return g->v[RUBRAVIEW_VGRADE_SHADOWS] != 0.0f || g->v[RUBRAVIEW_VGRADE_MIDTONES] != 0.0f ||
           g->v[RUBRAVIEW_VGRADE_HIGHLIGHTS] != 0.0f;
}

static float clamp255(float v) { return v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v; }

void rubraview_vgrade_session(const rubraview_vgrade_t *g, int32_t width, int32_t height, rubraview_edit_session_t *out) {
    if (!out) return;
    *out = rubraview_edit_begin(width, height);
    if (!g) return;
    rubraview_vgrade_t c = *g;
    rubraview_vgrade_clamp(&c);
    out->params.exposure_ev = c.v[RUBRAVIEW_VGRADE_EXPOSURE];
    out->params.brightness = c.v[RUBRAVIEW_VGRADE_BRIGHTNESS];
    out->params.contrast = c.v[RUBRAVIEW_VGRADE_CONTRAST];
    out->params.saturation = c.v[RUBRAVIEW_VGRADE_SATURATION];
    out->params.temperature = c.v[RUBRAVIEW_VGRADE_TEMPERATURE];
    out->params.tint = c.v[RUBRAVIEW_VGRADE_TINT];
    out->params.midtone_gamma = c.v[RUBRAVIEW_VGRADE_GAMMA];
    if (curve_moved(&c)) {
        rubraview_edit_curve_t *curve = &out->curves[RUBRAVIEW_EDIT_CHANNEL_RGB];
        static const float AT[3] = { 64.0f, 128.0f, 192.0f };
        static const rubraview_vgrade_value_t BY[3] = {
            RUBRAVIEW_VGRADE_SHADOWS, RUBRAVIEW_VGRADE_MIDTONES, RUBRAVIEW_VGRADE_HIGHLIGHTS
        };
        curve->point_count = 5;
        curve->points[0] = (rubraview_curve_point_t){ 0.0f, 0.0f };
        for (int k = 0; k < 3; ++k)
            curve->points[k + 1] = (rubraview_curve_point_t){ AT[k], clamp255(AT[k] + c.v[BY[k]] * CURVE_LEVELS_PER_UNIT) };
        curve->points[4] = (rubraview_curve_point_t){ 255.0f, 255.0f };
    }
    float blur = c.v[RUBRAVIEW_VGRADE_BLUR];
    out->blur_sigma = blur >= BLUR_FROM ? blur : 0.0f;
    out->blur_box = false;
    out->sharpen_amount = c.v[RUBRAVIEW_VGRADE_SHARPEN];
    out->sharpen_radius = c.v[RUBRAVIEW_VGRADE_SHARPEN_RADIUS];
    out->sharpen_threshold = 0;
    out->sharpen_laplacian = false;
}

static int32_t kernel_half(float sigma, float out[RUBRAVIEW_VGRADE_TAPS]) {
    float full[2 * RUBRAVIEW_VGRADE_TAPS - 1];
    int32_t radius = rubraview_filter_gaussian_kernel(sigma, full, 2 * RUBRAVIEW_VGRADE_TAPS - 1);
    for (int32_t k = 0; k < RUBRAVIEW_VGRADE_TAPS; ++k) out[k] = k <= radius ? full[radius + k] : 0.0f;
    return radius;
}

bool rubraview_vgrade_prepare(const rubraview_vgrade_t *g, rubraview_vgrade_prepared_t *out) {
    if (!out) return false;
    proven_sys_mem_set(out, 0, sizeof(*out));
    out->saturation = 1.0f;
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) out->table[c][i] = (float)i / 255.0f;
    if (!g) return false;
    rubraview_vgrade_t c = *g;
    rubraview_vgrade_clamp(&c);
    rubraview_edit_session_t s;
    rubraview_vgrade_session(&c, 1, 1, &s);
    s.blur_sigma = 0.0f;
    s.sharpen_amount = 0.0f;
    if (!rubraview_edit_is_neutral(&s)) {
        rubraview_edit_tone_tables(&s, out->table, &out->saturation);
        out->tone = true;
    }
    float blur = c.v[RUBRAVIEW_VGRADE_BLUR];
    if (blur >= BLUR_FROM) out->blur_radius = kernel_half(blur, out->blur_weights);
    if (c.v[RUBRAVIEW_VGRADE_SHARPEN] > 0.0f) {
        out->sharpen_radius = kernel_half(c.v[RUBRAVIEW_VGRADE_SHARPEN_RADIUS], out->sharpen_weights);
        out->sharpen_amount = c.v[RUBRAVIEW_VGRADE_SHARPEN] / 100.0f;
    }
    out->upscale = c.v[RUBRAVIEW_VGRADE_UPSCALE] / 100.0f;
    return out->tone || out->blur_radius > 0 || out->sharpen_radius > 0;
}

float rubraview_vgrade_tone_value(const rubraview_vgrade_prepared_t *p, int channel, float coded) {
    if (!p || channel < 0 || channel > 2) return coded;
    const float *t = p->table[channel];
    if (!(coded > 0.0f)) return t[0];
    if (coded >= 1.0f) return coded * t[255];
    float at = coded * 255.0f;
    int32_t i = (int32_t)at;
    if (i > 254) i = 254;
    float f = at - (float)i;
    return t[i] + (t[i + 1] - t[i]) * f;
}

void rubraview_vgrade_tone_apply(const rubraview_vgrade_prepared_t *p, rubraview_pixbuf_t *pb) {
    if (!p || !rubraview_pixbuf_is_valid(pb)) return;
    if (pb->format != RUBRAVIEW_PIXFMT_RGBA8 && pb->format != RUBRAVIEW_PIXFMT_BGRA8) return;
    int ir = pb->format == RUBRAVIEW_PIXFMT_BGRA8 ? 2 : 0, ib = 2 - ir;
    for (int32_t y = 0; y < pb->height; ++y) {
        uint8_t *row = pb->pixels + (ptrdiff_t)y * pb->stride;
        for (int32_t x = 0; x < pb->width; ++x) {
            uint8_t *px = row + x * 4;
            float r = p->table[0][px[ir]], g = p->table[1][px[1]], b = p->table[2][px[ib]];
            rubraview_color_saturate(&r, &g, &b, p->saturation);
            px[ir] = (uint8_t)(r * 255.0f + 0.5f);
            px[1] = (uint8_t)(g * 255.0f + 0.5f);
            px[ib] = (uint8_t)(b * 255.0f + 0.5f);
        }
    }
}

bool rubraview_vgrade_upscale_size(int32_t w, int32_t h, double scale, int32_t *out_w, int32_t *out_h) {
    if (w <= 0 || h <= 0 || !(scale > RUBRAVIEW_VGRADE_UPSCALE_FROM) || !isfinite(scale)) return false;
    int32_t longest = w > h ? w : h;
    if ((double)longest * scale > (double)RUBRAVIEW_VGRADE_UPSCALE_MAX) scale = (double)RUBRAVIEW_VGRADE_UPSCALE_MAX / (double)longest;
    if (!(scale > RUBRAVIEW_VGRADE_UPSCALE_FROM)) return false;
    int32_t dw = (int32_t)lround((double)w * scale), dh = (int32_t)lround((double)h * scale);
    if (dw > RUBRAVIEW_VGRADE_UPSCALE_MAX) dw = RUBRAVIEW_VGRADE_UPSCALE_MAX;
    if (dh > RUBRAVIEW_VGRADE_UPSCALE_MAX) dh = RUBRAVIEW_VGRADE_UPSCALE_MAX;
    if (out_w) *out_w = dw;
    if (out_h) *out_h = dh;
    return true;
}

float rubraview_vgrade_lanczos2(float d) {
    d = fabsf(d);
    if (d < 1e-5f) return 1.0f;
    if (d >= 2.0f) return 0.0f;
    float pd = (float)M_PI * d;
    return 2.0f * sinf(pd) * sinf(pd * 0.5f) / (pd * pd);
}

/* ---- the upscaler's reference (the shader in pal_vgrade_d3d11.c follows it) ---- */

typedef struct { float c[3]; } rgb_t;

static rgb_t texel(const rubraview_pixbuf_t *src, int32_t x, int32_t y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= src->width) x = src->width - 1;
    if (y >= src->height) y = src->height - 1;
    const uint8_t *px = src->pixels + (ptrdiff_t)y * src->stride + x * 4;
    int ir = src->format == RUBRAVIEW_PIXFMT_BGRA8 ? 2 : 0;
    return (rgb_t){ { px[ir] / 255.0f, px[1] / 255.0f, px[2 - ir] / 255.0f } };
}

static float luma(rgb_t v) { return 0.2126f * v.c[0] + 0.7152f * v.c[1] + 0.0722f * v.c[2]; }

/* Step 1 at one output pixel, its centre at (px, py) in the source's pixels (0 the first pixel's centre). */
static rgb_t upscale_at(const rubraview_pixbuf_t *src, float px, float py, float strength) {
    float fx = floorf(px), fy = floorf(py);
    float tx = px - fx, ty = py - fy;
    int32_t bx = (int32_t)fx, by = (int32_t)fy;
    rgb_t t[4][4];
    float l[4][4];
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) { t[j][i] = texel(src, bx + i - 1, by + j - 1); l[j][i] = luma(t[j][i]); }
    /* The gradient of the light at the point: central differences at the four nearest, mixed bilinearly. */
    float gx = 0.0f, gy = 0.0f;
    for (int j = 1; j <= 2; ++j)
        for (int i = 1; i <= 2; ++i) {
            float w = (i == 1 ? 1.0f - tx : tx) * (j == 1 ? 1.0f - ty : ty);
            gx += w * (l[j][i + 1] - l[j][i - 1]) * 0.5f;
            gy += w * (l[j + 1][i] - l[j - 1][i]) * 0.5f;
        }
    float len = sqrtf(gx * gx + gy * gy);
    float dx = 1.0f, dy = 0.0f, e = 0.0f;
    if (len > 1.0f / 1024.0f) {
        dx = gx / len; dy = gy / len;
        e = fminf(len * 4.0f, 1.0f) * strength;
    }
    /* Across the edge (along the gradient) the window is narrowed, along it widened: the edge stays sharp
       and its steps are smoothed. */
    float across = 1.0f + 0.5f * e, along = 1.0f / (1.0f + e);
    rgb_t sum = { { 0.0f, 0.0f, 0.0f } };
    float wsum = 0.0f;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            float ox = (float)(i - 1) - tx, oy = (float)(j - 1) - ty;
            float u = ox * dx + oy * dy, v = -ox * dy + oy * dx;
            float d = sqrtf(u * across * u * across + v * along * v * along);
            float w = rubraview_vgrade_lanczos2(d);
            for (int k = 0; k < 3; ++k) sum.c[k] += w * t[j][i].c[k];
            wsum += w;
        }
    rgb_t out;
    for (int k = 0; k < 3; ++k) {
        float lo = fminf(fminf(t[1][1].c[k], t[1][2].c[k]), fminf(t[2][1].c[k], t[2][2].c[k]));
        float hi = fmaxf(fmaxf(t[1][1].c[k], t[1][2].c[k]), fmaxf(t[2][1].c[k], t[2][2].c[k]));
        float v = wsum > 1e-4f ? sum.c[k] / wsum : t[1][1].c[k];
        out.c[k] = v < lo ? lo : v > hi ? hi : v;   /* held inside the four nearest: no ringing */
    }
    return out;
}

rubraview_pixbuf_t rubraview_vgrade_upscale(proven_arena_t *arena, const rubraview_pixbuf_t *src,
                                            int32_t dw, int32_t dh, float strength) {
    rubraview_pixbuf_t empty = {0};
    if (!arena || !rubraview_pixbuf_is_valid(src) || dw <= 0 || dh <= 0) return empty;
    if (src->format != RUBRAVIEW_PIXFMT_RGBA8 && src->format != RUBRAVIEW_PIXFMT_BGRA8) return empty;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    size_t n = (size_t)dw * (size_t)dh;
    proven_result_mem_mut_t held = proven_arena_alloc(arena, n * 3u * sizeof(float));
    if (held.err != PROVEN_OK) return empty;
    float *mid = (float*)held.value.ptr;
    rubraview_pixbuf_t out = rubraview_pixbuf_create(arena, dw, dh, src->format);
    if (!rubraview_pixbuf_is_valid(&out)) return empty;
    float sx = (float)src->width / (float)dw, sy = (float)src->height / (float)dh;
    for (int32_t y = 0; y < dh; ++y)
        for (int32_t x = 0; x < dw; ++x) {
            rgb_t v = upscale_at(src, ((float)x + 0.5f) * sx - 0.5f, ((float)y + 0.5f) * sy - 0.5f, strength);
            for (int k = 0; k < 3; ++k) mid[((size_t)y * (size_t)dw + (size_t)x) * 3u + (size_t)k] = v.c[k];
        }
    /* Step 2: each pixel pushed from the mean of its four neighbours, by as much as its headroom (how far its
       neighbourhood is from black and white, against its brightest) allows, and never past the darkest or the
       brightest of the five. */
    int ir = src->format == RUBRAVIEW_PIXFMT_BGRA8 ? 2 : 0;
    for (int32_t y = 0; y < dh; ++y)
        for (int32_t x = 0; x < dw; ++x) {
            int32_t xl = x > 0 ? x - 1 : 0, xr = x + 1 < dw ? x + 1 : dw - 1;
            int32_t yu = y > 0 ? y - 1 : 0, yd = y + 1 < dh ? y + 1 : dh - 1;
            uint8_t *px = out.pixels + (ptrdiff_t)y * out.stride + x * 4;
            for (int k = 0; k < 3; ++k) {
                float c = mid[((size_t)y * (size_t)dw + (size_t)x) * 3u + (size_t)k];
                float nl = mid[((size_t)y * (size_t)dw + (size_t)xl) * 3u + (size_t)k];
                float nr = mid[((size_t)y * (size_t)dw + (size_t)xr) * 3u + (size_t)k];
                float nu = mid[((size_t)yu * (size_t)dw + (size_t)x) * 3u + (size_t)k];
                float nd = mid[((size_t)yd * (size_t)dw + (size_t)x) * 3u + (size_t)k];
                float mn = fminf(fminf(fminf(c, nl), fminf(nr, nu)), nd);
                float mx = fmaxf(fmaxf(fmaxf(c, nl), fmaxf(nr, nu)), nd);
                float room = fminf(mn, 1.0f - mx) / fmaxf(mx, 1e-5f);
                float amp = sqrtf(room < 0.0f ? 0.0f : room > 1.0f ? 1.0f : room);
                float v = c + amp * strength * 0.5f * (4.0f * c - nl - nr - nu - nd) * 0.25f;
                v = v < mn ? mn : v > mx ? mx : v;   /* inside its neighbours: no ringing */
                px[k == 0 ? ir : k == 2 ? 2 - ir : 1] = (uint8_t)(v * 255.0f + 0.5f);
            }
            px[3] = 255;
        }
    return out;
}
