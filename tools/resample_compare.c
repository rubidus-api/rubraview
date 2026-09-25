/*
 * The separable resampler (D-37) against the one before it
 * (tools/resample_reference.c): speed, and how far the output moved.
 * Not part of the viewer. Build and run on a host:
 *
 *   cc -std=gnu2x -O2 -D_POSIX_C_SOURCE=200809L -Iinclude -Ivendor/proven/include \
 *      -Ivendor/proven/platform tools/resample_compare.c tools/resample_reference.c \
 *      src/core/resample.c src/core/pixbuf.c vendor/proven/src/proven/arena.c \
 *      vendor/proven/src/proven/memory.c vendor/proven/src/proven/panic.c \
 *      vendor/proven/platform/proven_sys_mem.c -lm -o build/resample_compare
 *   ./build/resample_compare
 *
 * The source is a photograph-like image (smooth gradients, noise and hard
 * edges), not random bytes, so the differences reported are the ones a
 * picture would show.
 */
#include "rubraview/resample.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void rubraview_resample_band_reference(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst,
                                       rubraview_resample_filter_t filter, int32_t y0, int32_t y1);

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void paint(rubraview_pixbuf_t *pb) {
    int bpp = rubraview_bytes_per_pixel(pb->format);
    unsigned seed = 7;
    for (int y = 0; y < pb->height; ++y) {
        for (int x = 0; x < pb->width; ++x) {
            uint8_t *p = pb->pixels + (ptrdiff_t)y * pb->stride + x * bpp;
            double fx = (double)x / pb->width, fy = (double)y / pb->height;
            seed = seed * 1103515245u + 12345u;
            double noise = (double)((seed >> 16) & 15) - 7.5;
            bool edge = ((x / 97) + (y / 61)) % 2 == 0;
            double base[4] = { 255 * fx, 255 * fy, 128 + 100 * sin(fx * 9 + fy * 5), 255 };
            for (int c = 0; c < bpp; ++c) {
                double v = (c < 3 ? base[c] : 255) + (c < 3 ? noise : 0) + (edge && c < 3 ? 40 : 0);
                p[c] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
        }
    }
}

int main(void) {
    size_t cap = (size_t)1 << 30;
    void *raw = malloc(cap);
    proven_arena_t a = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = cap });
    static const int sizes[][4] = { {640, 480, 1920, 1080}, {333, 217, 1000, 701}, {4000, 3000, 1200, 900},
                                    {1920, 1080, 480, 270}, {17, 9, 5, 3}, {5, 3, 200, 130} };
    static const rubraview_pixel_format_t fmts[2] = { RUBRAVIEW_PIXFMT_RGBA8, RUBRAVIEW_PIXFMT_GRAY8 };
    static const rubraview_resample_filter_t filters[4] = { RUBRAVIEW_FILTER_BICUBIC, RUBRAVIEW_FILTER_LANCZOS3,
                                                            RUBRAVIEW_FILTER_BILINEAR, RUBRAVIEW_FILTER_NEAREST };
    static const char *names[4] = { "bicubic", "lanczos3", "bilinear", "nearest" };
    printf("| Filter | Pixels | Size | Before s | After s | Speed-up | Max diff | Bytes changed |\n");
    printf("|---|---|---|---|---|---|---|---|\n");
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        for (int f = 0; f < 2; ++f)
            for (int k = 0; k < 4; ++k) {
                proven_arena_reset(&a);
                rubraview_pixbuf_t src = rubraview_pixbuf_create(&a, sizes[s][0], sizes[s][1], fmts[f]);
                paint(&src);
                rubraview_pixbuf_t d1 = rubraview_pixbuf_create(&a, sizes[s][2], sizes[s][3], fmts[f]);
                rubraview_pixbuf_t d2 = rubraview_pixbuf_create(&a, sizes[s][2], sizes[s][3], fmts[f]);
                double t0 = now();
                rubraview_resample_band_reference(&src, &d1, filters[k], 0, d1.height);
                double t1 = now();
                rubraview_resample_band(&src, &d2, filters[k], 0, d2.height);
                double t2 = now();
                size_t n = (size_t)d1.stride * (size_t)d1.height, changed = 0;
                int maxd = 0;
                for (size_t i = 0; i < n; ++i) {
                    int d = abs((int)d1.pixels[i] - (int)d2.pixels[i]);
                    if (d) changed++;
                    if (d > maxd) maxd = d;
                }
                printf("| %s | %s | %dx%d -> %dx%d | %.3f | %.3f | x%.1f | %d | %.3f%% |\n", names[k], f ? "gray" : "rgba",
                       sizes[s][0], sizes[s][1], sizes[s][2], sizes[s][3], t1 - t0, t2 - t1,
                       (t2 - t1) > 0 ? (t1 - t0) / (t2 - t1) : 0.0, maxd, 100.0 * (double)changed / (double)n);
            }
    free(raw);
    return 0;
}
