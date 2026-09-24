#ifndef RUBRAVIEW_THUMB_H
#define RUBRAVIEW_THUMB_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A picker tile's picture (D-34, owner 2026-09-25: "썸네일은 선명하게가 아니라
 * 약간 흐릿하게"). From whatever the shell or the decoder gave:
 *
 *   1. the middle of it, cut to the tile's shape (`aspect` = width / height),
 *      so the picture fills the tile rather than floating in it;
 *   2. made small — no wider than `max_width` — which the renderer then
 *      stretches, a softness of its own;
 *   3. blurred by a box of `blur_radius`;
 *   4. darkened to `keep` (0..1) of its brightness, so the name drawn over it
 *      reads;
 *   5. made opaque (a shell bitmap often says alpha 0 for every pixel).
 *
 * The result has the source's 4-byte format (BGRA or RGBA). An invalid
 * buffer comes back when the source is not usable.
 */
rubraview_pixbuf_t rubraview_thumb_make(proven_arena_t *arena, const rubraview_pixbuf_t *src,
                                        double aspect, int32_t max_width, int32_t blur_radius, double keep);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_THUMB_H */
