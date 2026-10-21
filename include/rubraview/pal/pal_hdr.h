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

/**
 * RV-151 (D-181): a P010 picture (ten bits high in each 16-bit sample; Y', then Cb Cr interleaved at half
 * size), as FFmpeg's D3D11VA decoder gives it: a slice of a texture on the pass's device, copied on the card
 * into the pass's own, or else the two planes in memory, uploaded.
 */
typedef struct rubraview_hdr_p010 {
    void *texture;            /* an ID3D11Texture2D (DXGI_FORMAT_P010, maybe an array), or NULL for memory */
    uint32_t slice;
    const uint8_t *data[2];   /* in memory: Y', Cb Cr */
    int32_t stride[2];
    int32_t width, height;
} rubraview_hdr_p010_t;

/** False when this source cannot be read here (the card has no P010 views, another format, odd sizes):
    the caller brings the picture to memory and tries again with `data`. */
bool rubraview_pal_hdr_pass_run_p010(rubraview_hdr_pass_t *pass, const rubraview_hdr_p010_t *source,
                                     const rubraview_tonemap_t *tone, void *target);

/** The VM's check of the card's way (no decoder there): 10-bit 4:2:0 planes packed as P010 into slice 1 of
    a two-slice texture, then run as the decoder's picture would be; `in_memory`: packed in memory instead,
    as a picture brought back from the card is. False when it cannot. */
bool rubraview_pal_hdr_pass_run_as_p010(rubraview_hdr_pass_t *pass, const rubraview_hdr_planes_t *planes,
                                        const rubraview_tonemap_t *tone, void *target, bool in_memory);

void rubraview_pal_hdr_pass_free(rubraview_hdr_pass_t *pass);

#ifdef __cplusplus
}
#endif

#endif
