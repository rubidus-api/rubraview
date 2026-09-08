#ifndef RUBRAVIEW_FILMSTRIP_H
#define RUBRAVIEW_FILMSTRIP_H

#include "rubraview/core.h"
#include "rubraview/lru.h"
#include "rubraview/ui_virtual.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The collapsible thumbnail filmstrip (RFC-0001 §3.1). Thumbnails are
 * decoded asynchronously and cached, so the strip's job here is to
 * decide *which* ones are worth holding: it virtualises the visible
 * range and drives the shared byte budget (RV-028), which reports the
 * thumbnails to release. That keeps a 10,000-image directory bounded by
 * the viewport rather than the directory (§7.4).
 *
 * Decoding itself is the Windows backend's work (WIC low-resolution
 * decode); this module only says what to ask for and what to drop.
 */

typedef struct rubraview_filmstrip {
    bool    visible;          /* toggled by F4 (§3.7.2) */
    double  thumb_extent;     /* one thumbnail's width plus its gutter, in pixels */
    double  viewport_extent;  /* the strip's width */
    double  scroll_offset;
    size_t  item_count;
    size_t  lookahead;        /* extra thumbnails realised either side */
    size_t  estimated_bytes;  /* per decoded thumbnail, for the budget */
} rubraview_filmstrip_t;

rubraview_filmstrip_t rubraview_filmstrip_create(size_t item_count, double thumb_extent, double viewport_extent);

/** The slice of thumbnails currently worth holding. */
rubraview_virtual_range_t rubraview_filmstrip_visible(const rubraview_filmstrip_t *strip);

/** Scroll so `index` is in view — used when the current page changes. */
void rubraview_filmstrip_reveal(rubraview_filmstrip_t *strip, size_t index);

void rubraview_filmstrip_scroll_by(rubraview_filmstrip_t *strip, double delta);

/**
 * Register the visible thumbnails with the cache, newest-first from
 * `current_index` outwards so the ones nearest the reader survive
 * eviction. Indices the cache evicted are written to `out_evicted`
 * (bounded by `out_cap`) and the count returned — the caller releases
 * those bitmaps. Indices that are visible but not yet cached are
 * reported in `out_needed`, which is what the decoder should fetch.
 */
size_t rubraview_filmstrip_sync_cache(const rubraview_filmstrip_t *strip,
                                      rubraview_lru_cache_t *cache,
                                      size_t current_index,
                                      uint64_t *out_evicted, size_t evicted_cap,
                                      size_t *out_needed, size_t needed_cap,
                                      size_t *out_needed_count);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_FILMSTRIP_H */
