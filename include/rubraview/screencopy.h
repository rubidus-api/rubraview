#ifndef RUBRAVIEW_SCREENCOPY_H
#define RUBRAVIEW_SCREENCOPY_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A settled page drawn from a copy at screen size (RV-166, D-199). Without a
 * graphics card, Direct2D's high-quality cubic reduction of a large picture
 * costs about 180 ms a frame on the CPU, and the loop draws every pass for
 * 3 s after an input. A draw seen with the same key in two frames running
 * is drawn once into a bitmap the size of the target, at its own place and
 * with the same filter, and the page's rectangle of that bitmap is then
 * copied 1:1 while the key stays the same. This file decides; the renderer
 * draws.
 */

#define RUBRAVIEW_SCREENCOPY_SLOTS 2   /* a spread's two pages */

/* What a draw is, as far as its pixels go. Compared exactly. */
typedef struct rubraview_screencopy_key {
    const void *texture;
    uint64_t    content;               /* bumped on every write into the texture */
    double      a, b, c, d, e, f;      /* the texture's pixels to the target's: x' = a*x + c*y + e, y' = b*x + d*y + f */
    double      src_w, src_h;          /* the whole texture is drawn */
    int32_t     filter;                /* the renderer's interpolation mode */
    int32_t     target_w, target_h;
} rubraview_screencopy_key_t;

typedef struct rubraview_screencopy_slot {
    rubraview_screencopy_key_t key;
    bool    used;                      /* holds a key */
    bool    made;                      /* a copy exists for it */
    bool    failed;                    /* making it failed: drawn directly while the key stays */
    bool    touched;                   /* drawn in this frame */
    bool    seen_before;               /* drawn in the frame before */
    int32_t x, y, w, h;                /* the page's rectangle on the target: what is copied */
} rubraview_screencopy_slot_t;

typedef struct rubraview_screencopy {
    rubraview_screencopy_slot_t slots[RUBRAVIEW_SCREENCOPY_SLOTS];
} rubraview_screencopy_t;

typedef enum rubraview_screencopy_action {
    RUBRAVIEW_SCREENCOPY_DIRECT = 0,   /* draw the texture as before */
    RUBRAVIEW_SCREENCOPY_MAKE,         /* draw it into a new copy of the target, then copy its rectangle */
    RUBRAVIEW_SCREENCOPY_USE,          /* copy the slot's copy 1:1 to its place */
} rubraview_screencopy_action_t;

/** May this draw be served by a copy at all? Only on a software adapter, never a film's texture or an HDR
 *  target, only at full opacity, with a smoothing filter (`smooth`), and only a reduction (the transform's
 *  area scale under 1). */
bool rubraview_screencopy_eligible(const rubraview_screencopy_key_t *key, bool software, bool video, bool hdr,
                                   double opacity, bool smooth);

/** The whole-pixel rectangle the drawn texture covers on the target, cut to the target. False when empty. */
bool rubraview_screencopy_bounds(const rubraview_screencopy_key_t *key,
                                 int32_t *x, int32_t *y, int32_t *w, int32_t *h);

/** A frame begins. Slots not drawn in the frame before are let go: `drop` gets true for each whose copy the
 *  caller must free (it had one). */
void rubraview_screencopy_frame(rubraview_screencopy_t *sc, bool drop[RUBRAVIEW_SCREENCOPY_SLOTS]);

/** What to do with an eligible draw. `*slot` is the slot it went to (-1 for none); `*evict` is true when that
 *  slot's old copy must be freed first. On MAKE, the caller calls rubraview_screencopy_made with the result. */
rubraview_screencopy_action_t rubraview_screencopy_step(rubraview_screencopy_t *sc,
                                                        const rubraview_screencopy_key_t *key,
                                                        int *slot, bool *evict);

/** After MAKE: the copy was made and (x, y, w, h) is what is copied from it, or (ok false) it could not be. */
void rubraview_screencopy_made(rubraview_screencopy_t *sc, int slot, bool ok,
                               int32_t x, int32_t y, int32_t w, int32_t h);

/** The texture is destroyed: `drop` gets true for each slot whose copy the caller must free. */
void rubraview_screencopy_forget(rubraview_screencopy_t *sc, const void *texture,
                                 bool drop[RUBRAVIEW_SCREENCOPY_SLOTS]);

/** Everything goes (a lost device, a resized or re-formatted target): `drop` as above. */
void rubraview_screencopy_reset(rubraview_screencopy_t *sc, bool drop[RUBRAVIEW_SCREENCOPY_SLOTS]);

#ifdef __cplusplus
}
#endif

#endif
