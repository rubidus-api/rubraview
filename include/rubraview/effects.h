#ifndef RUBRAVIEW_EFFECTS_H
#define RUBRAVIEW_EFFECTS_H

/*
 * The owner's image effects (2026-10-17, after IrfanView's list, not its
 * code): each written here from the textbook algorithm, MIT like the rest.
 * Every effect takes an RGBA8 or BGRA8 picture and gives one of the same
 * size; one number (`amount`) sets it, within the effect's own range.
 */

#include <stdbool.h>
#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rubraview_effect {
    RUBRAVIEW_EFFECT_NONE = 0,
    RUBRAVIEW_EFFECT_GRAYSCALE,
    RUBRAVIEW_EFFECT_SEPIA,
    RUBRAVIEW_EFFECT_NEGATIVE,
    RUBRAVIEW_EFFECT_SOLARIZE,      /* amount: the threshold, 0..255 */
    RUBRAVIEW_EFFECT_POSTERIZE,     /* amount: levels a channel, 2..32 */
    RUBRAVIEW_EFFECT_EMBOSS,        /* amount: strength, 1..10 */
    RUBRAVIEW_EFFECT_EDGES,         /* amount: strength, 1..10 (Sobel) */
    RUBRAVIEW_EFFECT_MEDIAN,        /* amount: radius, 1..10 px */
    RUBRAVIEW_EFFECT_OIL,           /* amount: radius, 1..10 px (the commonest intensity near each pixel) */
    RUBRAVIEW_EFFECT_PIXELIZE,      /* amount: block, 2..100 px */
    RUBRAVIEW_EFFECT_NOISE,         /* amount: 1..100 */
    RUBRAVIEW_EFFECT_VIGNETTE,      /* amount: 1..100 */
    RUBRAVIEW_EFFECT_SWIRL,         /* amount: degrees at the centre, -720..720 */
    RUBRAVIEW_EFFECT_RADIAL_BLUR,   /* amount: degrees, 1..30 (round the centre) */
    RUBRAVIEW_EFFECT_ZOOM_BLUR,     /* amount: 1..50 (% toward the centre) */
    RUBRAVIEW_EFFECT_WAVE,          /* amount: rows shifted sideways, 1..100 px */
    RUBRAVIEW_EFFECT_RED_EYE,       /* amount: strength, 1..100 */
    RUBRAVIEW_EFFECT_CHROMATIC,     /* amount: red and blue drawn in, -20..20 px at the corners */
    RUBRAVIEW_EFFECT_BUTTON_3D,     /* amount: bevel, 2..100 px */
    RUBRAVIEW_EFFECT_FRAGMENT,      /* amount: offset, 1..50 px (four copies laid over) */
    RUBRAVIEW_EFFECT_EXPLOSION,     /* amount: scatter, 1..50 px */
    RUBRAVIEW_EFFECT_COUNT
} rubraview_effect_t;

/** The effect's name as the panel says it ("Oil paint"). */
const char *rubraview_effect_name(rubraview_effect_t effect);
/** RV-129: its name on a command line (--effect=oil:4): lower case, never translated; "" for none. */
const char *rubraview_effect_key(rubraview_effect_t effect);
/** Its amount's range and the value it starts at. */
void rubraview_effect_range(rubraview_effect_t effect, double *lo, double *hi, double *start);
/** Whether its amount is in pixels (scaled with the picture for a preview). */
bool rubraview_effect_amount_is_pixels(rubraview_effect_t effect);

/** The effect on `src`, a new picture in `arena`; empty for none or out of memory. */
rubraview_pixbuf_t rubraview_effect_apply(proven_arena_t *arena, const rubraview_pixbuf_t *src,
                                          rubraview_effect_t effect, double amount);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_EFFECTS_H */
