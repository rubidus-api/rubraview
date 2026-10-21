#include "rubraview/tonemap.h"
#include <math.h>

/* SMPTE ST 2084's constants. */
#define PQ_M1 (2610.0 / 16384.0)
#define PQ_M2 (2523.0 / 4096.0 * 128.0)
#define PQ_C1 (3424.0 / 4096.0)
#define PQ_C2 (2413.0 / 4096.0 * 32.0)
#define PQ_C3 (2392.0 / 4096.0 * 32.0)

/* ARIB STD-B67's. */
#define HLG_A 0.17883277
#define HLG_B 0.28466892
#define HLG_C 0.55991073

static double clamp01(double v) { return v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v; }

double rubraview_pq_to_nits(double signal) {
    double e = pow(clamp01(signal), 1.0 / PQ_M2);
    double num = e - PQ_C1;
    if (num < 0.0) num = 0.0;
    return 10000.0 * pow(num / (PQ_C2 - PQ_C3 * e), 1.0 / PQ_M1);
}

double rubraview_nits_to_pq(double nits) {
    double y = pow((nits < 0.0 ? 0.0 : nits) / 10000.0, PQ_M1);
    return pow((PQ_C1 + PQ_C2 * y) / (1.0 + PQ_C3 * y), PQ_M2);
}

double rubraview_hlg_to_scene(double signal) {
    double e = clamp01(signal);
    return e <= 0.5 ? e * e / 3.0 : (exp((e - HLG_C) / HLG_A) + HLG_B) / 12.0;
}

/* Worked in PQ's signal, which is near to how bright things look: below the knee nothing moves, above it a
   Hermite spline bends the rest of the film's range under the target. */
double rubraview_bt2390(double nits, double source_peak, double target_peak) {
    if (!(nits > 0.0)) return 0.0;
    if (!(source_peak > target_peak)) return nits < target_peak ? nits : target_peak;
    double source_pq = rubraview_nits_to_pq(source_peak);
    double e1 = rubraview_nits_to_pq(nits) / source_pq;
    if (e1 > 1.0) e1 = 1.0;
    double max_lum = rubraview_nits_to_pq(target_peak) / source_pq;
    double ks = 1.5 * max_lum - 0.5;
    if (ks < 0.0) ks = 0.0;
    double e2 = e1;
    if (e1 > ks) {
        double t = (e1 - ks) / (1.0 - ks), t2 = t * t, t3 = t2 * t;
        e2 = (2.0 * t3 - 3.0 * t2 + 1.0) * ks + (t3 - 2.0 * t2 + t) * (1.0 - ks) + (-2.0 * t3 + 3.0 * t2) * max_lum;
    }
    return rubraview_pq_to_nits(e2 * source_pq);
}

double rubraview_hdr_peak(double max_cll, double mastering_max) {
    double peak = max_cll > 0.0 ? max_cll : mastering_max > 0.0 ? mastering_max : RUBRAVIEW_HDR_DEFAULT_PEAK;
    return peak < 100.0 ? 100.0 : peak > 10000.0 ? 10000.0 : peak;
}

static double srgb_encode(double v) {
    v = clamp01(v);
    return v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

void rubraview_tonemap_pixel(const rubraview_tonemap_t *t, double y, double cb, double cr, double out_rgb[3]) {
    /* the codes back to Y' 0..1 and Cb, Cr -0.5..0.5 */
    int bits = t->bits >= 8 && t->bits <= 16 ? t->bits : 10;
    double max_code = (double)((1 << bits) - 1), unit = (double)(1 << (bits - 8));
    double yc = y * max_code, cbc = cb * max_code, crc = cr * max_code;
    double yv, cbv, crv;
    if (t->full_range) {
        yv = yc / max_code;
        cbv = (cbc - (double)(1 << (bits - 1))) / max_code;
        crv = (crc - (double)(1 << (bits - 1))) / max_code;
    } else {
        yv = (yc - 16.0 * unit) / (219.0 * unit);
        cbv = (cbc - 128.0 * unit) / (224.0 * unit);
        crv = (crc - 128.0 * unit) / (224.0 * unit);
    }
    /* BT.2020's non-constant luminance matrix */
    double r = clamp01(yv + 1.4746 * crv);
    double b = clamp01(yv + 1.8814 * cbv);
    double g = clamp01((yv - 0.2627 * (yv + 1.4746 * crv) - 0.0593 * (yv + 1.8814 * cbv)) / 0.6780);

    double lr, lg, lb, peak = t->peak_nits;
    if (t->transfer == RUBRAVIEW_HDR_HLG) {
        /* scene light, then the OOTF of a 1000-nit display (system gamma 1.2) */
        double sr = rubraview_hlg_to_scene(r), sg = rubraview_hlg_to_scene(g), sb = rubraview_hlg_to_scene(b);
        double ys = 0.2627 * sr + 0.6780 * sg + 0.0593 * sb;
        double gain = ys > 0.0 ? 1000.0 * pow(ys, 0.2) : 0.0;
        lr = gain * sr; lg = gain * sg; lb = gain * sb;
        peak = 1000.0;
    } else {
        lr = rubraview_pq_to_nits(r); lg = rubraview_pq_to_nits(g); lb = rubraview_pq_to_nits(b);
    }

    /* the brightest channel folded under the white, the others kept in proportion (hue kept) */
    double white = t->white_nits > 0.0 ? t->white_nits : RUBRAVIEW_SDR_WHITE_NITS;
    double m = fmax(lr, fmax(lg, lb));
    double scale = m > 0.0 ? rubraview_bt2390(m, peak, white) / m / white : 0.0;
    lr *= scale; lg *= scale; lb *= scale;

    /* BT.2020 primaries to BT.709's, what falls outside clipped */
    double r7 =  1.6605 * lr - 0.5876 * lg - 0.0728 * lb;
    double g7 = -0.1246 * lr + 1.1329 * lg - 0.0083 * lb;
    double b7 = -0.0182 * lr - 0.1006 * lg + 1.1187 * lb;
    out_rgb[0] = srgb_encode(r7);
    out_rgb[1] = srgb_encode(g7);
    out_rgb[2] = srgb_encode(b7);
}

double rubraview_tonemap_sample_scale(int bits, bool high_bits) {
    if (!high_bits || bits < 8 || bits > 16) return 65535.0;
    return 65535.0 / (double)(1 << (16 - bits));
}
