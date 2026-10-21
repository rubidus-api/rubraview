#ifndef RUBRAVIEW_PAL_HDR_H
#define RUBRAVIEW_PAL_HDR_H

#include "rubraview/core.h"
#include "rubraview/tonemap.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * RV-150 (D-180): an HDR picture tone mapped to SDR on the graphics card.
 * The decoder's planes go up as they are (16-bit samples, the codes in
 * their low bits); a pixel shader does tonemap.c's arithmetic and writes
 * BGRA into a texture of the caller's (a film's shared slot).
 *
 * One pass belongs to one decode thread; the device is the renderer's
 * decode device, which is multithread-protected.
 */
typedef struct rubraview_hdr_pass rubraview_hdr_pass_t;

typedef struct rubraview_hdr_planes {
    const uint8_t *data[3];   /* Y', Cb, Cr: 16-bit little-endian samples */
    int32_t stride[3];        /* bytes a row */
    int32_t width, height;    /* the luma plane's */
    int32_t chroma_width, chroma_height;
} rubraview_hdr_planes_t;

/** NULL when it cannot be made, and `*why` says why (an N_ literal). */
rubraview_hdr_pass_t *rubraview_pal_hdr_pass_create(void *d3d11_device, const char **why);

/** Draws the planes, tone mapped, into `target` (an ID3D11Texture2D, BGRA, a render target, the
    planes' size). The caller holds whatever lock the target needs. */
bool rubraview_pal_hdr_pass_run(rubraview_hdr_pass_t *pass, const rubraview_hdr_planes_t *planes,
                                const rubraview_tonemap_t *tone, void *target);

void rubraview_pal_hdr_pass_free(rubraview_hdr_pass_t *pass);

#ifdef __cplusplus
}
#endif

#endif
