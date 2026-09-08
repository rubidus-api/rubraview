#include "rubraview/color.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float s_srgb_to_lin[256];
static bool s_lut_initialized = false;

void rubraview_color_lut_init(void) {
    if (s_lut_initialized) return;
    for (int i = 0; i < 256; ++i) {
        float c = (float)i / 255.0f;
        if (c <= 0.04045f) {
            s_srgb_to_lin[i] = c / 12.92f;
        } else {
            s_srgb_to_lin[i] = powf((c + 0.055f) / 1.055f, 2.4f);
        }
    }
    s_lut_initialized = true;
}

float rubraview_srgb_to_linear(uint8_t srgb) {
    if (!s_lut_initialized) rubraview_color_lut_init();
    return s_srgb_to_lin[srgb];
}

uint8_t rubraview_linear_to_srgb(float lin) {
    if (lin <= 0.0f) return 0;
    if (lin >= 1.0f) return 255;
    float s;
    if (lin <= 0.0031308f) {
        s = lin * 12.92f;
    } else {
        s = 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f;
    }
    int val = (int)(s * 255.0f + 0.5f);
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return (uint8_t)val;
}

static inline float clampf(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

void rubraview_color_adjust(rubraview_pixbuf_t *pb, const rubraview_color_adjust_params_t *params) {
    if (!rubraview_pixbuf_is_valid(pb) || params == NULL) return;
    if (pb->format != RUBRAVIEW_PIXFMT_RGBA8 && pb->format != RUBRAVIEW_PIXFMT_BGRA8) return;

    if (!s_lut_initialized) rubraview_color_lut_init();

    float ev_factor = powf(2.0f, params->exposure_ev);
    float contrast_factor = tanf((clampf(params->contrast, -100.0f, 100.0f) + 100.0f) * ((float)M_PI / 400.0f));
    float sat = clampf(params->saturation, 0.0f, 4.0f);
    float inv_gamma = (params->gamma > 0.01f) ? (1.0f / params->gamma) : 1.0f;

    bool is_bgra = (pb->format == RUBRAVIEW_PIXFMT_BGRA8);

    for (int32_t y = 0; y < pb->height; ++y) {
        uint8_t *row = pb->pixels + ((ptrdiff_t)y * pb->stride);
        for (int32_t x = 0; x < pb->width; ++x) {
            uint8_t *px = row + (x * 4);
            int idx_r = is_bgra ? 2 : 0;
            int idx_b = is_bgra ? 0 : 2;

            /* 1. Linearize sRGB and apply exposure in linear space */
            float r_lin = s_srgb_to_lin[px[idx_r]] * ev_factor;
            float g_lin = s_srgb_to_lin[px[1]] * ev_factor;
            float b_lin = s_srgb_to_lin[px[idx_b]] * ev_factor;

            /* 2. Convert back to [0.0, 1.0] perceptual space */
            float r = clampf(powf(r_lin, 1.0f / 2.2f), 0.0f, 1.0f);
            float g = clampf(powf(g_lin, 1.0f / 2.2f), 0.0f, 1.0f);
            float b = clampf(powf(b_lin, 1.0f / 2.2f), 0.0f, 1.0f);

            /* 3. Contrast adjustment around 0.5 midtone */
            if (params->contrast != 0.0f) {
                r = clampf((r - 0.5f) * contrast_factor + 0.5f, 0.0f, 1.0f);
                g = clampf((g - 0.5f) * contrast_factor + 0.5f, 0.0f, 1.0f);
                b = clampf((b - 0.5f) * contrast_factor + 0.5f, 0.0f, 1.0f);
            }

            /* 4. Gamma midtone correction */
            if (params->gamma != 1.0f) {
                r = powf(r, inv_gamma);
                g = powf(g, inv_gamma);
                b = powf(b, inv_gamma);
            }

            /* 5. Saturation */
            if (sat != 1.0f) {
                float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                r = clampf(lum + (r - lum) * sat, 0.0f, 1.0f);
                g = clampf(lum + (g - lum) * sat, 0.0f, 1.0f);
                b = clampf(lum + (b - lum) * sat, 0.0f, 1.0f);
            }

            px[idx_r] = (uint8_t)(r * 255.0f + 0.5f);
            px[1]     = (uint8_t)(g * 255.0f + 0.5f);
            px[idx_b] = (uint8_t)(b * 255.0f + 0.5f);
            /* Alpha preserved */
        }
    }
}

void rubraview_histogram_compute(const rubraview_pixbuf_t *pb,
                          uint32_t hist_r[256],
                          uint32_t hist_g[256],
                          uint32_t hist_b[256],
                          uint32_t hist_lum[256]) {
    if (!rubraview_pixbuf_is_valid(pb)) return;

    if (hist_r) memset(hist_r, 0, 256 * sizeof(uint32_t));
    if (hist_g) memset(hist_g, 0, 256 * sizeof(uint32_t));
    if (hist_b) memset(hist_b, 0, 256 * sizeof(uint32_t));
    if (hist_lum) memset(hist_lum, 0, 256 * sizeof(uint32_t));

    bool is_bgra = (pb->format == RUBRAVIEW_PIXFMT_BGRA8);
    bool is_gray = (pb->format == RUBRAVIEW_PIXFMT_GRAY8);

    for (int32_t y = 0; y < pb->height; ++y) {
        const uint8_t *row = pb->pixels + ((ptrdiff_t)y * pb->stride);
        if (is_gray) {
            for (int32_t x = 0; x < pb->width; ++x) {
                uint8_t val = row[x];
                if (hist_r) hist_r[val]++;
                if (hist_g) hist_g[val]++;
                if (hist_b) hist_b[val]++;
                if (hist_lum) hist_lum[val]++;
            }
        } else {
            for (int32_t x = 0; x < pb->width; ++x) {
                uint8_t r = is_bgra ? row[x * 4 + 2] : row[x * 4 + 0];
                uint8_t g = row[x * 4 + 1];
                uint8_t b = is_bgra ? row[x * 4 + 0] : row[x * 4 + 2];
                uint8_t lum = (uint8_t)((2126 * (uint32_t)r + 7152 * (uint32_t)g + 722 * (uint32_t)b + 5000) / 10000);

                if (hist_r) hist_r[r]++;
                if (hist_g) hist_g[g]++;
                if (hist_b) hist_b[b]++;
                if (hist_lum) hist_lum[lum]++;
            }
        }
    }
}

void rubraview_levels_build_lut(uint8_t lut_out[256], uint8_t black_point, uint8_t white_point, float gamma) {
    if (!lut_out) return;
    if (white_point <= black_point) white_point = black_point + 1;
    float range = (float)(white_point - black_point);
    float inv_gamma = (gamma > 0.001f) ? (1.0f / gamma) : 1.0f;

    for (int i = 0; i < 256; ++i) {
        if (i <= black_point) {
            lut_out[i] = 0;
        } else if (i >= white_point) {
            lut_out[i] = 255;
        } else {
            float norm = (float)(i - black_point) / range;
            float curved = powf(norm, inv_gamma);
            int val = (int)(curved * 255.0f + 0.5f);
            lut_out[i] = (uint8_t)(val < 0 ? 0 : (val > 255 ? 255 : val));
        }
    }
}

void rubraview_curve_build_lut(uint8_t lut_out[256], const rubraview_curve_point_t *points, size_t count) {
    if (!lut_out) return;
    if (points == NULL || count < 2) {
        for (int i = 0; i < 256; ++i) {
            lut_out[i] = (uint8_t)i;
        }
        return;
    }

    /* Stack buffers for up to 32 control points */
    #define MAX_SPLINE_POINTS 32
    if (count > MAX_SPLINE_POINTS) count = MAX_SPLINE_POINTS;

    float delta[MAX_SPLINE_POINTS - 1];
    float d[MAX_SPLINE_POINTS];

    /* 1. Calculate secant slopes */
    for (size_t k = 0; k < count - 1; ++k) {
        float h = points[k + 1].x - points[k].x;
        if (h <= 0.0001f) {
            delta[k] = 0.0f;
        } else {
            delta[k] = (points[k + 1].y - points[k].y) / h;
        }
    }

    /* 2. Initial slopes */
    d[0] = delta[0];
    d[count - 1] = delta[count - 2];
    for (size_t k = 1; k < count - 1; ++k) {
        d[k] = (delta[k - 1] + delta[k]) * 0.5f;
    }

    /* 3. Fritsch-Carlson monotonicity check */
    for (size_t k = 0; k < count - 1; ++k) {
        if (delta[k] == 0.0f) {
            d[k] = 0.0f;
            d[k + 1] = 0.0f;
        } else {
            float alpha = d[k] / delta[k];
            float beta = d[k + 1] / delta[k];
            float sum_sq = alpha * alpha + beta * beta;
            if (sum_sq > 9.0f) {
                float tau = 3.0f / sqrtf(sum_sq);
                d[k] = tau * alpha * delta[k];
                d[k + 1] = tau * beta * delta[k];
            }
        }
    }

    /* 4. Interpolate over 0..255 */
    size_t cur_seg = 0;
    for (int i = 0; i < 256; ++i) {
        float x = (float)i;

        /* Outside domain clamping */
        if (x <= points[0].x) {
            float y = clampf(points[0].y, 0.0f, 255.0f);
            lut_out[i] = (uint8_t)(y + 0.5f);
            continue;
        }
        if (x >= points[count - 1].x) {
            float y = clampf(points[count - 1].y, 0.0f, 255.0f);
            lut_out[i] = (uint8_t)(y + 0.5f);
            continue;
        }

        while (cur_seg < count - 2 && points[cur_seg + 1].x < x) {
            cur_seg++;
        }

        float h = points[cur_seg + 1].x - points[cur_seg].x;
        if (h <= 0.0001f) {
            float y = clampf(points[cur_seg].y, 0.0f, 255.0f);
            lut_out[i] = (uint8_t)(y + 0.5f);
            continue;
        }

        float t = (x - points[cur_seg].x) / h;
        float t2 = t * t;
        float t3 = t2 * t;

        float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        float h10 = t3 - 2.0f * t2 + t;
        float h01 = -2.0f * t3 + 3.0f * t2;
        float h11 = t3 - t2;

        float y = h00 * points[cur_seg].y +
                  h10 * h * d[cur_seg] +
                  h01 * points[cur_seg + 1].y +
                  h11 * h * d[cur_seg + 1];

        y = clampf(y, 0.0f, 255.0f);
        lut_out[i] = (uint8_t)(y + 0.5f);
    }
}

void rubraview_lut_apply(rubraview_pixbuf_t *pb,
                  const uint8_t lut_r[256],
                  const uint8_t lut_g[256],
                  const uint8_t lut_b[256]) {
    if (!rubraview_pixbuf_is_valid(pb)) return;
    if (!lut_r && !lut_g && !lut_b) return;

    bool is_bgra = (pb->format == RUBRAVIEW_PIXFMT_BGRA8);
    bool is_gray = (pb->format == RUBRAVIEW_PIXFMT_GRAY8);

    for (int32_t y = 0; y < pb->height; ++y) {
        uint8_t *row = pb->pixels + ((ptrdiff_t)y * pb->stride);
        if (is_gray) {
            const uint8_t *lut = lut_r ? lut_r : (lut_g ? lut_g : lut_b);
            for (int32_t x = 0; x < pb->width; ++x) {
                row[x] = lut[row[x]];
            }
        } else {
            int idx_r = is_bgra ? 2 : 0;
            int idx_b = is_bgra ? 0 : 2;
            for (int32_t x = 0; x < pb->width; ++x) {
                uint8_t *px = row + (x * 4);
                if (lut_r) px[idx_r] = lut_r[px[idx_r]];
                if (lut_g) px[1]     = lut_g[px[1]];
                if (lut_b) px[idx_b] = lut_b[px[idx_b]];
            }
        }
    }
}
