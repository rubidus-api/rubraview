#ifndef RUBRAVIEW_UI_VIRTUAL_H
#define RUBRAVIEW_UI_VIRTUAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Virtual scrolling (RFC-0001 §3.15.2: "even in directories containing
 * 10,000+ media files, the renderer only allocates and draws tiles
 * currently visible"). Shared by the filmstrip (§3.1) and the in-app
 * file picker (§3.15.2): given the scroll offset, work out which slice
 * of the list is on screen, so cost tracks the viewport and not the
 * directory.
 */

typedef struct rubraview_virtual_range {
    size_t first;  /* index of the first item to realise */
    size_t count;  /* how many, including the lookahead margin */
} rubraview_virtual_range_t;

/**
 * `item_extent` is one item's size along the scroll axis including its
 * gutter. `lookahead` realises extra items either side so scrolling does
 * not reveal blanks.
 */
rubraview_virtual_range_t rubraview_virtual_range(size_t item_count,
                                                  double item_extent,
                                                  double viewport_extent,
                                                  double scroll_offset,
                                                  size_t lookahead);

/** The scroll offset that brings `index` fully into view, given the current offset. */
double rubraview_virtual_scroll_to(size_t index, double item_extent, double viewport_extent, double current_offset);

/** Clamp a scroll offset to the scrollable span (never negative, never past the end). */
double rubraview_virtual_clamp_offset(size_t item_count, double item_extent, double viewport_extent, double offset);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_VIRTUAL_H */
