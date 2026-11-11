#ifndef RUBRAVIEW_PAL_VGRADE_H
#define RUBRAVIEW_PAL_VGRADE_H

#include "rubraview/core.h"
#include "rubraview/vgrade.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * RV-270 (D-330): a film's adjustment and upscaler on the graphics card (pal_vgrade_d3d11.c), the renderer's
 * engine: the renderer (pal_render.h, rubraview_pal_render_vgrade*) makes it on its device and hands it its
 * textures. Not for portable code.
 */
typedef struct rubraview_vgrade_gpu rubraview_vgrade_gpu_t;

/** NULL when it cannot be made, and `*why` says why (an N_ literal). */
rubraview_vgrade_gpu_t *rubraview_pal_vgrade_gpu_create(void *d3d11_device, const char **why);
void rubraview_pal_vgrade_gpu_free(rubraview_vgrade_gpu_t *gpu);

/** `src` (an ID3D11Texture2D, `w` x `h`) adjusted into `dst` (the same size, a render target); `hdr` when they
    are FP16 with values past 1. False when nothing was drawn (nothing to do, or a failure). */
bool rubraview_pal_vgrade_gpu_grade(rubraview_vgrade_gpu_t *gpu, const rubraview_vgrade_prepared_t *prepared,
                                    void *src, void *dst, int32_t w, int32_t h, bool hdr);

/** `src` (`sw` x `sh`) upscaled into `dst` (`dw` x `dh`, a render target) at `strength` 0..1. */
bool rubraview_pal_vgrade_gpu_upscale(rubraview_vgrade_gpu_t *gpu, float strength, void *src, int32_t sw, int32_t sh,
                                      void *dst, int32_t dw, int32_t dh, bool hdr);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_VGRADE_H */
