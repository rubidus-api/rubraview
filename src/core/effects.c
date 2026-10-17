#include "rubraview/effects.h"

#include <math.h>
#include <string.h>

static const struct {
    const char *name;
    double lo, hi, start;
    bool pixels;
} EFFECTS[RUBRAVIEW_EFFECT_COUNT] = {
    [RUBRAVIEW_EFFECT_NONE]        = { "None",               0, 1, 0, false },
    [RUBRAVIEW_EFFECT_GRAYSCALE]   = { "Grey",               0, 1, 1, false },
    [RUBRAVIEW_EFFECT_SEPIA]       = { "Sepia",              0, 100, 100, false },
    [RUBRAVIEW_EFFECT_NEGATIVE]    = { "Negative",           0, 1, 1, false },
    [RUBRAVIEW_EFFECT_SOLARIZE]    = { "Solarize",           0, 255, 128, false },
    [RUBRAVIEW_EFFECT_POSTERIZE]   = { "Posterize",          2, 32, 6, false },
    [RUBRAVIEW_EFFECT_EMBOSS]      = { "Emboss",             1, 10, 3, false },
    [RUBRAVIEW_EFFECT_EDGES]       = { "Edges",              1, 10, 3, false },
    [RUBRAVIEW_EFFECT_MEDIAN]      = { "Median (noise out)", 1, 10, 2, true },
    [RUBRAVIEW_EFFECT_OIL]         = { "Oil paint",          1, 10, 3, true },
    [RUBRAVIEW_EFFECT_PIXELIZE]    = { "Pixelize",           2, 100, 12, true },
    [RUBRAVIEW_EFFECT_NOISE]       = { "Noise",              1, 100, 20, false },
    [RUBRAVIEW_EFFECT_VIGNETTE]    = { "Vignette",           1, 100, 50, false },
    [RUBRAVIEW_EFFECT_SWIRL]       = { "Swirl",              -720, 720, 180, false },
    [RUBRAVIEW_EFFECT_RADIAL_BLUR] = { "Radial blur",        1, 30, 5, false },
    [RUBRAVIEW_EFFECT_ZOOM_BLUR]   = { "Zoom blur",          1, 50, 10, false },
    [RUBRAVIEW_EFFECT_WAVE]        = { "Wave",               1, 100, 10, true },
    [RUBRAVIEW_EFFECT_RED_EYE]     = { "Red eye reduction",  1, 100, 80, false },
    [RUBRAVIEW_EFFECT_CHROMATIC]   = { "Colour fringe fix",  -20, 20, 2, true },
    [RUBRAVIEW_EFFECT_BUTTON_3D]   = { "3D button",          2, 100, 12, true },
    [RUBRAVIEW_EFFECT_FRAGMENT]    = { "Fragment",           1, 50, 6, true },
    [RUBRAVIEW_EFFECT_EXPLOSION]   = { "Explosion",          1, 50, 8, true },
};

const char *rubraview_effect_name(rubraview_effect_t effect) {
    return effect < RUBRAVIEW_EFFECT_COUNT ? EFFECTS[effect].name : "?";
}

void rubraview_effect_range(rubraview_effect_t effect, double *lo, double *hi, double *start) {
    if (effect >= RUBRAVIEW_EFFECT_COUNT) effect = RUBRAVIEW_EFFECT_NONE;
    if (lo) *lo = EFFECTS[effect].lo;
    if (hi) *hi = EFFECTS[effect].hi;
    if (start) *start = EFFECTS[effect].start;
}

bool rubraview_effect_amount_is_pixels(rubraview_effect_t effect) {
    return effect < RUBRAVIEW_EFFECT_COUNT && EFFECTS[effect].pixels;
}

/* ---- helpers ---- */

static uint8_t clamp8(double v) { return (uint8_t)(v < 0.0 ? 0.0 : v > 255.0 ? 255.0 : v + 0.5); }
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static const uint8_t *at(const rubraview_pixbuf_t *p, int32_t x, int32_t y) {
    x = clampi(x, 0, p->width - 1);
    y = clampi(y, 0, p->height - 1);
    return p->pixels + (size_t)y * (size_t)p->stride + (size_t)x * 4u;
}

static uint8_t *out_at(rubraview_pixbuf_t *p, int32_t x, int32_t y) {
    return p->pixels + (size_t)y * (size_t)p->stride + (size_t)x * 4u;
}

/* Bilinear sample at a real position, clamped to the edge. */
static void sample(const rubraview_pixbuf_t *p, double fx, double fy, uint8_t out[4]) {
    int32_t x0 = (int32_t)floor(fx), y0 = (int32_t)floor(fy);
    double ax = fx - x0, ay = fy - y0;
    const uint8_t *a = at(p, x0, y0), *b = at(p, x0 + 1, y0), *c = at(p, x0, y0 + 1), *d = at(p, x0 + 1, y0 + 1);
    for (int k = 0; k < 4; ++k) {
        double top = a[k] + (b[k] - a[k]) * ax, bottom = c[k] + (d[k] - c[k]) * ax;
        out[k] = clamp8(top + (bottom - top) * ay);
    }
}

static double luma(const uint8_t *p, bool bgra) {
    double r = bgra ? p[2] : p[0], b = bgra ? p[0] : p[2];
    return 0.299 * r + 0.587 * p[1] + 0.114 * b;
}

/* A small deterministic generator: the same picture gets the same noise. */
static uint32_t next_random(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return *s;
}

/* ---- the effects ---- */

rubraview_pixbuf_t rubraview_effect_apply(proven_arena_t *arena, const rubraview_pixbuf_t *src,
                                          rubraview_effect_t effect, double amount) {
    rubraview_pixbuf_t empty = {0};
    if (!arena || !rubraview_pixbuf_is_valid(src) || effect == RUBRAVIEW_EFFECT_NONE || effect >= RUBRAVIEW_EFFECT_COUNT) return empty;
    if (src->format != RUBRAVIEW_PIXFMT_RGBA8 && src->format != RUBRAVIEW_PIXFMT_BGRA8) return empty;
    double lo = 0, hi = 1;
    rubraview_effect_range(effect, &lo, &hi, NULL);
    if (amount < lo) amount = lo;
    if (amount > hi) amount = hi;
    rubraview_pixbuf_t out = rubraview_pixbuf_create(arena, src->width, src->height, src->format);
    if (!rubraview_pixbuf_is_valid(&out)) return empty;
    const bool bgra = src->format == RUBRAVIEW_PIXFMT_BGRA8;
    const int R = bgra ? 2 : 0, B = bgra ? 0 : 2;
    const int32_t w = src->width, h = src->height;
    const double cx = (w - 1) * 0.5, cy = (h - 1) * 0.5, reach = sqrt(cx * cx + cy * cy) + 1e-9;
    uint32_t seed = 0x9E3779B9u;

    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            const uint8_t *s = at(src, x, y);
            uint8_t *o = out_at(&out, x, y);
            o[3] = s[3];
            switch (effect) {
                case RUBRAVIEW_EFFECT_GRAYSCALE: {
                    uint8_t g = clamp8(luma(s, bgra));
                    o[0] = o[1] = o[2] = g;
                    break;
                }
                case RUBRAVIEW_EFFECT_SEPIA: {
                    /* The usual sepia matrix, mixed with the original by amount %. */
                    double r = s[R], g = s[1], b = s[B], k = amount / 100.0;
                    double sr = 0.393 * r + 0.769 * g + 0.189 * b, sg = 0.349 * r + 0.686 * g + 0.168 * b,
                           sb = 0.272 * r + 0.534 * g + 0.131 * b;
                    o[R] = clamp8(r + (sr - r) * k); o[1] = clamp8(g + (sg - g) * k); o[B] = clamp8(b + (sb - b) * k);
                    break;
                }
                case RUBRAVIEW_EFFECT_NEGATIVE:
                    o[0] = (uint8_t)(255 - s[0]); o[1] = (uint8_t)(255 - s[1]); o[2] = (uint8_t)(255 - s[2]);
                    break;
                case RUBRAVIEW_EFFECT_SOLARIZE:
                    for (int k = 0; k < 3; ++k) o[k] = s[k] >= amount ? (uint8_t)(255 - s[k]) : s[k];
                    break;
                case RUBRAVIEW_EFFECT_POSTERIZE: {
                    int levels = (int)lround(amount);
                    for (int k = 0; k < 3; ++k) o[k] = clamp8(floor(s[k] * levels / 256.0) * 255.0 / (levels - 1));
                    break;
                }
                case RUBRAVIEW_EFFECT_EMBOSS: {
                    /* The difference along the diagonal, on grey. */
                    double d = (luma(at(src, x + 1, y + 1), bgra) - luma(at(src, x - 1, y - 1), bgra)) * amount * 0.5;
                    o[0] = o[1] = o[2] = clamp8(128.0 + d);
                    break;
                }
                case RUBRAVIEW_EFFECT_EDGES: {
                    /* Sobel on each channel, the edges bright on black. */
                    for (int k = 0; k < 3; ++k) {
                        double gx = -at(src, x - 1, y - 1)[k] - 2.0 * at(src, x - 1, y)[k] - at(src, x - 1, y + 1)[k]
                                    + at(src, x + 1, y - 1)[k] + 2.0 * at(src, x + 1, y)[k] + at(src, x + 1, y + 1)[k];
                        double gy = -at(src, x - 1, y - 1)[k] - 2.0 * at(src, x, y - 1)[k] - at(src, x + 1, y - 1)[k]
                                    + at(src, x - 1, y + 1)[k] + 2.0 * at(src, x, y + 1)[k] + at(src, x + 1, y + 1)[k];
                        o[k] = clamp8(sqrt(gx * gx + gy * gy) * amount / 8.0);
                    }
                    break;
                }
                case RUBRAVIEW_EFFECT_MEDIAN: {
                    /* Each channel's median of the square around it, by a 256-bin count. */
                    int r = (int)lround(amount), n = (2 * r + 1) * (2 * r + 1);
                    for (int k = 0; k < 3; ++k) {
                        uint16_t count[256];
                        memset(count, 0, sizeof(count));
                        for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) count[at(src, x + dx, y + dy)[k]]++;
                        int seen = 0, v = 0;
                        while (v < 255 && seen + count[v] <= n / 2) { seen += count[v]; v++; }
                        o[k] = (uint8_t)v;
                    }
                    break;
                }
                case RUBRAVIEW_EFFECT_OIL: {
                    /* The commonest of 20 intensity bins near the pixel, and that bin's mean colour. */
                    int r = (int)lround(amount);
                    int count[20] = {0};
                    double sum[20][3];
                    memset(sum, 0, sizeof(sum));
                    for (int dy = -r; dy <= r; ++dy) {
                        for (int dx = -r; dx <= r; ++dx) {
                            const uint8_t *q = at(src, x + dx, y + dy);
                            int bin = (int)(luma(q, bgra) * 20.0 / 256.0);
                            count[bin]++;
                            sum[bin][0] += q[0]; sum[bin][1] += q[1]; sum[bin][2] += q[2];
                        }
                    }
                    int best = 0;
                    for (int i = 1; i < 20; ++i) if (count[i] > count[best]) best = i;
                    for (int k = 0; k < 3; ++k) o[k] = clamp8(sum[best][k] / count[best]);
                    break;
                }
                case RUBRAVIEW_EFFECT_PIXELIZE: {
                    int bsz = (int)lround(amount);
                    int32_t bx = x / bsz * bsz, by = y / bsz * bsz;
                    double acc[3] = { 0, 0, 0 };
                    int n = 0;
                    for (int32_t yy = by; yy < by + bsz && yy < h; ++yy)
                        for (int32_t xx = bx; xx < bx + bsz && xx < w; ++xx) {
                            const uint8_t *q = at(src, xx, yy);
                            acc[0] += q[0]; acc[1] += q[1]; acc[2] += q[2]; n++;
                        }
                    for (int k = 0; k < 3; ++k) o[k] = clamp8(acc[k] / n);
                    break;
                }
                case RUBRAVIEW_EFFECT_NOISE: {
                    double d = ((double)(next_random(&seed) % 2001u) / 1000.0 - 1.0) * amount * 1.28;
                    for (int k = 0; k < 3; ++k) o[k] = clamp8(s[k] + d);
                    break;
                }
                case RUBRAVIEW_EFFECT_VIGNETTE: {
                    double t = sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) / reach;
                    double f = 1.0 - amount / 100.0 * t * t;
                    for (int k = 0; k < 3; ++k) o[k] = clamp8(s[k] * f);
                    break;
                }
                case RUBRAVIEW_EFFECT_SWIRL: {
                    /* Turned round the centre, most at the centre, nothing at the edge. */
                    double dx = x - cx, dy = y - cy, rr = sqrt(dx * dx + dy * dy) / reach;
                    double angle = amount * 3.141592653589793 / 180.0 * (1.0 - rr) * (1.0 - rr);
                    double c = cos(angle), sn = sin(angle);
                    uint8_t q[4];
                    sample(src, cx + dx * c - dy * sn, cy + dx * sn + dy * c, q);
                    o[0] = q[0]; o[1] = q[1]; o[2] = q[2]; o[3] = q[3];
                    break;
                }
                case RUBRAVIEW_EFFECT_RADIAL_BLUR:
                case RUBRAVIEW_EFFECT_ZOOM_BLUR: {
                    /* Samples along an arc round the centre, or along the line to it. */
                    double acc[4] = { 0, 0, 0, 0 };
                    int n = 12;
                    for (int i = 0; i < n; ++i) {
                        double f = (double)i / (n - 1) - 0.5;
                        double sx, sy;
                        if (effect == RUBRAVIEW_EFFECT_RADIAL_BLUR) {
                            double a = f * amount * 3.141592653589793 / 180.0, c = cos(a), sn = sin(a);
                            sx = cx + (x - cx) * c - (y - cy) * sn;
                            sy = cy + (x - cx) * sn + (y - cy) * c;
                        } else {
                            double z = 1.0 - (f + 0.5) * amount / 100.0;
                            sx = cx + (x - cx) * z;
                            sy = cy + (y - cy) * z;
                        }
                        uint8_t q[4];
                        sample(src, sx, sy, q);
                        for (int k = 0; k < 4; ++k) acc[k] += q[k];
                    }
                    for (int k = 0; k < 4; ++k) o[k] = clamp8(acc[k] / n);
                    break;
                }
                case RUBRAVIEW_EFFECT_WAVE: {
                    double shift = amount * sin(y * 2.0 * 3.141592653589793 / (amount * 6.0 + 20.0));
                    uint8_t q[4];
                    sample(src, x + shift, y, q);
                    o[0] = q[0]; o[1] = q[1]; o[2] = q[2]; o[3] = q[3];
                    break;
                }
                case RUBRAVIEW_EFFECT_RED_EYE: {
                    /* Where red stands well above green and blue, it is brought down to their level. */
                    double r = s[R], g = s[1], b = s[B];
                    double redness = r - (g + b) * 0.5;
                    o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
                    if (redness > 40.0 && r > 80.0) o[R] = clamp8(r - (r - (g + b) * 0.5) * amount / 100.0);
                    break;
                }
                case RUBRAVIEW_EFFECT_CHROMATIC: {
                    /* Red and blue scaled about the centre by `amount` pixels at the corners, in opposite ways. */
                    double k = amount / reach;
                    uint8_t qr[4], qb[4];
                    sample(src, cx + (x - cx) * (1.0 - k), cy + (y - cy) * (1.0 - k), qr);
                    sample(src, cx + (x - cx) * (1.0 + k), cy + (y - cy) * (1.0 + k), qb);
                    o[R] = qr[R]; o[1] = s[1]; o[B] = qb[B];
                    break;
                }
                case RUBRAVIEW_EFFECT_BUTTON_3D: {
                    /* A bevel: lighter along the top and left, darker along the bottom and right. */
                    int bsz = (int)lround(amount);
                    int32_t d = x < y ? x : y, e = (w - 1 - x) < (h - 1 - y) ? (w - 1 - x) : (h - 1 - y);
                    double f = 0.0;
                    if (d < bsz && d <= e) f = 0.45 * (1.0 - (double)d / bsz);
                    else if (e < bsz) f = -0.45 * (1.0 - (double)e / bsz);
                    for (int k = 0; k < 3; ++k) o[k] = clamp8(f > 0 ? s[k] + (255 - s[k]) * f : s[k] * (1.0 + f));
                    break;
                }
                case RUBRAVIEW_EFFECT_FRAGMENT: {
                    int d = (int)lround(amount);
                    const uint8_t *a = at(src, x - d, y - d), *b = at(src, x + d, y - d), *c = at(src, x - d, y + d), *e = at(src, x + d, y + d);
                    for (int k = 0; k < 4; ++k) o[k] = (uint8_t)((a[k] + b[k] + c[k] + e[k] + 2) / 4);
                    break;
                }
                case RUBRAVIEW_EFFECT_EXPLOSION: {
                    int d = (int)lround(amount);
                    int32_t dx = (int32_t)(next_random(&seed) % (uint32_t)(2 * d + 1)) - d;
                    int32_t dy = (int32_t)(next_random(&seed) % (uint32_t)(2 * d + 1)) - d;
                    const uint8_t *q = at(src, x + dx, y + dy);
                    o[0] = q[0]; o[1] = q[1]; o[2] = q[2]; o[3] = q[3];
                    break;
                }
                default:
                    memcpy(o, s, 4);
                    break;
            }
        }
    }
    return out;
}

const char *rubraview_effect_key(rubraview_effect_t effect) {
    static const char *const KEYS[RUBRAVIEW_EFFECT_COUNT] = {
        "", "grayscale", "sepia", "negative", "solarize", "posterize", "emboss", "edges", "median", "oil",
        "pixelize", "noise", "vignette", "swirl", "radial-blur", "zoom-blur", "wave", "red-eye", "chromatic",
        "button", "fragment", "explosion",
    };
    return (int)effect >= 0 && effect < RUBRAVIEW_EFFECT_COUNT ? KEYS[effect] : "";
}
