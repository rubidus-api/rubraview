/* RFC-0007's sample filter: posterise — each colour channel brought to a few levels. MIT, as Rubraview.
   Build (from docs/manual/plugins): x86_64-w64-mingw32-gcc -O2 -shared -I../../../include -o posterise.rvf.dll posterise.c */
#include "rubraview/plugin_filter.h"

static const rvf_param PARAMS[] = {
    { "Levels", RVF_INT, 2, 32, 1, 4, 0 },
    { "Keep the alpha", RVF_BOOL, 0, 1, 1, 1, 0 },
};
static const rvf_info INFO = { RVF_ABI, "Posterise", "Rubraview", "1.0", "MIT", 2, PARAMS };

RVF_EXPORT const rvf_info *rvf_describe(void) { return &INFO; }

RVF_EXPORT int rvf_apply(const rvf_image *in, rvf_image *out, const double *params, rvf_progress progress, void *context) {
    int levels = (int)params[0];
    if (levels < 2) levels = 2;
    int keep_alpha = params[1] > 0.5;
    for (int y = 0; y < in->height; ++y) {
        const unsigned char *s = in->pixels + (long long)y * in->stride;
        unsigned char *d = out->pixels + (long long)y * out->stride;
        for (int x = 0; x < in->width; ++x) {
            for (int c = 0; c < 3; ++c) {
                int q = (s[x * 4 + c] * (levels - 1) + 127) / 255;
                d[x * 4 + c] = (unsigned char)(q * 255 / (levels - 1));
            }
            d[x * 4 + 3] = keep_alpha ? s[x * 4 + 3] : 255;
        }
        if (progress && (y & 63) == 0 && progress(context, (double)y / in->height)) return 1;
    }
    return 0;
}
