#ifndef RUBRAVIEW_PICKER_H
#define RUBRAVIEW_PICKER_H

#include "rubraview/core.h"
#include "rubraview/pal/pal_fs.h"
#include "rubraview/ui_virtual.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The in-app Metro tile file picker (RFC-0001 §3.15.2), modelled without
 * a renderer: breadcrumb segmentation, the virtualised tile grid,
 * type-ahead jumping (§3.15.4 point 1), and multi-select with its
 * selection metrics. The native COM dialog (§3.15.1) already exists as a
 * PAL backend; this is the touch-first alternative for mobile Remote
 * Desktop, where a 12 px list font is unusable.
 */

#define RUBRAVIEW_PICKER_MAX_CRUMBS 32

/**
 * §3.15.2: every path segment becomes its own tappable tile. Each crumb
 * carries the prefix that navigating to it would open, so tapping
 * "Comics" in "C:/Comics/Berserk" opens "C:/Comics".
 */
typedef struct rubraview_breadcrumb {
    u8str_t label;  /* the segment itself, e.g. "Comics" */
    u8str_t prefix; /* the path up to and including it */
} rubraview_breadcrumb_t;

typedef struct rubraview_breadcrumbs {
    rubraview_breadcrumb_t items[RUBRAVIEW_PICKER_MAX_CRUMBS];
    size_t count;
} rubraview_breadcrumbs_t;

/** Split a path into tappable segments. Slices point into `path`. */
rubraview_breadcrumbs_t rubraview_picker_breadcrumbs(u8str_t path);

typedef struct rubraview_picker {
    const rubraview_fs_listing_t *listing; /* the current directory, already sorted by the caller */
    double tile_extent;      /* one tile's height plus gutter, along the scroll axis */
    double viewport_extent;
    double scroll_offset;
    int32_t columns;         /* tiles per row */
    size_t  focus;           /* the item type-ahead and the keyboard move */
    bool    multi_select;
    bool   *selected;        /* caller-owned, listing->count entries, or NULL when single-select */
} rubraview_picker_t;

rubraview_picker_t rubraview_picker_create(const rubraview_fs_listing_t *listing,
                                           double tile_extent, double viewport_extent, int32_t columns);

/** The rows currently on screen, expressed as an item range. */
rubraview_virtual_range_t rubraview_picker_visible(const rubraview_picker_t *picker);

void rubraview_picker_scroll_by(rubraview_picker_t *picker, double delta);

/** Scroll so the focused item is on screen. */
void rubraview_picker_reveal_focus(rubraview_picker_t *picker);

/**
 * §3.15.4 point 1: pressing a letter jumps to the next entry starting
 * with it, wrapping around, so repeated presses cycle through matches.
 * Returns true when the focus moved.
 */
bool rubraview_picker_type_ahead(rubraview_picker_t *picker, char letter);

/** Toggle one item's selection; a no-op outside multi-select mode. */
void rubraview_picker_toggle(rubraview_picker_t *picker, size_t index);

/** §3.15.2: the bottom bar's "N files (M bytes)" metrics. */
void rubraview_picker_selection_metrics(const rubraview_picker_t *picker,
                                        size_t *out_count, uint64_t *out_total_bytes);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PICKER_H */
