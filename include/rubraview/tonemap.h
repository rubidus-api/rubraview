#ifndef RUBRAVIEW_TONEMAP_H
#define RUBRAVIEW_TONEMAP_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * RV-150 (D-180): an HDR film brought to an ordinary screen. The film's
 * BT.2020 Y'CbCr, coded with PQ (SMPTE ST 2084) or HLG (ARIB STD-B67), is
 * turned to light, its brightest part folded under the screen's white by
 * BT.2390's EETF, its primaries brought to BT.709 and written with sRGB's
 * curve.
 *
 * The graphics card does this in a pixel shader (pal_hdr_d3d11.c); this is
 * the same arithmetic in C, which the host suite checks against the
 * standards and the VM checks the shader against.
 */

typedef enum rubraview_hdr_transfer {
    RUBRAVIEW_HDR_NONE = 0,
    RUBRAVIEW_HDR_PQ = 1,
    RUBRAVIEW_HDR_HLG = 2,
} rubraview_hdr_transfer_t;

#define RUBRAVIEW_SDR_WHITE_NITS 203.0   /* BT.2408's graphics white */
#define RUBRAVIEW_HDR_DEFAULT_PEAK 1000.0

typedef struct rubraview_tonemap {
    rubraview_hdr_transfer_t transfer;
    bool   full_range;       /* the film's codes span 0..max; otherwise 64..940 at 10 bits */
    int    bits;             /* 8, 10, 12 */
    double peak_nits;        /* the film's brightest, from its metadata */
    double white_nits;       /* what the screen's white stands for */
} rubraview_tonemap_t;

/** PQ's signal (0..1) to nits, and back. */
double rubraview_pq_to_nits(double signal);
double rubraview_nits_to_pq(double nits);

/** HLG's signal (0..1) to scene light (0..1), its inverse OETF. */
double rubraview_hlg_to_scene(double signal);

/** BT.2390's EETF: nits of a film whose brightest is `source_peak` to nits under `target_peak`. */
double rubraview_bt2390(double nits, double source_peak, double target_peak);

/** The film's peak: MaxCLL when given, else the mastering display's, else 1000; kept within 100..10000. */
double rubraview_hdr_peak(double max_cll, double mastering_max);

/** One pixel: Y', Cb, Cr as codes normalised to the bit depth's largest (code / (2^bits - 1)), to sRGB 0..1. */
void rubraview_tonemap_pixel(const rubraview_tonemap_t *t, double y, double cb, double cr, double out_rgb[3]);

/** RV-151 (D-181): the factor that turns a 16-bit sample, as the shader reads it (0..1), back into a code:
    FFmpeg's planar pictures keep a code in the sample's low bits, the card's P010 (and P016) in its high ones. */
double rubraview_tonemap_sample_scale(int bits, bool high_bits);

/** The setting's word: `[video] hdr_tone_mapping` 0 off, 1 bt2390. */
#define RUBRAVIEW_TONEMAP_OFF 0
#define RUBRAVIEW_TONEMAP_BT2390 1

#ifdef __cplusplus
}
#endif

#endif
