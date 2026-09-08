#ifndef RUBRAVIEW_LAYOUT_H
#define RUBRAVIEW_LAYOUT_H

#include "rubraview/core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The intelligent multi-page / book layout engine (RFC-0001 §3.3):
 * pure page-geometry pagination, independent of any renderer. Given a
 * sequence of page dimensions and a requested layout mode, produces the
 * ordered sequence of on-screen "spreads" (one or two pages shown
 * together) that §4.1.3's compositor draws.
 */

typedef enum rubraview_page_layout {
    RUBRAVIEW_PAGE_LAYOUT_SINGLE = 0,
    RUBRAVIEW_PAGE_LAYOUT_DUAL,
    RUBRAVIEW_PAGE_LAYOUT_BOOK,
    RUBRAVIEW_PAGE_LAYOUT_WEBTOON,
} rubraview_page_layout_t;

typedef enum rubraview_reading_dir {
    RUBRAVIEW_READING_LTR = 0,
    RUBRAVIEW_READING_RTL,
} rubraview_reading_dir_t;

typedef enum rubraview_spread_half {
    RUBRAVIEW_SPREAD_WHOLE = 0, /* show the full page */
    RUBRAVIEW_SPREAD_LEFT_HALF, /* §3.3.7 split: show only the left half of a wide spread */
    RUBRAVIEW_SPREAD_RIGHT_HALF,
} rubraview_spread_half_t;

typedef struct rubraview_page_info {
    double width, height;
    /* §3.8.5: a page ComicInfo.xml tags as a cover never pairs with a
       neighbour, however it happens to be numbered. Pre-merged spread
       detection reaches the same conclusion from the aspect ratio; this
       is the metadata saying so outright. */
    bool force_standalone;
} rubraview_page_info_t;

typedef struct rubraview_spread {
    int32_t left_index;             /* source page index shown on screen-left, or the sole page for a single/split spread */
    int32_t right_index;            /* source page index shown on screen-right, -1 if this spread shows only one page */
    rubraview_spread_half_t left_half; /* WHOLE unless this is one half of a split wide spread (right_index is always -1 in that case) */
    bool is_premerged_spread;       /* true when left_index is a pre-scanned wide spread (AR >= threshold) shown whole, un-split, un-paired */
} rubraview_spread_t;

typedef struct rubraview_layout_opts {
    rubraview_page_layout_t mode;
    rubraview_reading_dir_t direction;
    double spread_ar_threshold;     /* width/height >= this counts as a pre-merged spread; RFC default 1.15 */
    double portrait_collapse_ar;    /* win_w/win_h below this forces effective SINGLE, except WEBTOON; RFC default 1.0 */
    bool   auto_split_wide_spreads; /* §3.3.7: bisect wide spreads into two virtual pages instead of showing them whole */
} rubraview_layout_opts_t;

/**
 * Sensible RFC defaults (spread_ar_threshold 1.15, portrait_collapse_ar
 * 1.0, splitting off) for the given mode and reading direction.
 *
 * Note: §3.3.5's "resume side-by-side spreads at win_ar >= 1.3" half of
 * the orientation rule is a hysteresis behavior (it depends on which way
 * the window was just resized) and belongs to the stateful M3 UI loop,
 * not this pure, stateless geometry function — only the collapse
 * threshold is applied here.
 */
rubraview_layout_opts_t rubraview_layout_opts_default(rubraview_page_layout_t mode, rubraview_reading_dir_t direction);

typedef struct rubraview_layout_result {
    rubraview_spread_t *spreads;
    size_t count;
} rubraview_layout_result_t;

/**
 * Compute the spread sequence for `pages` under `opts`, given the current
 * window dimensions (used only for the portrait auto-collapse rule).
 * Returns an empty result for a NULL arena/pages or a zero page_count.
 */
rubraview_layout_result_t rubraview_layout_compute(proven_arena_t *arena, const rubraview_page_info_t *pages, size_t page_count, double win_w, double win_h, rubraview_layout_opts_t opts);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_LAYOUT_H */
