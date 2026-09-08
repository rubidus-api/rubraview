#include "rubraview/filmstrip.h"

#define DEFAULT_THUMB_BYTES (160u * 160u * 4u) /* a 160 px RGBA thumbnail */

rubraview_filmstrip_t rubraview_filmstrip_create(size_t item_count, double thumb_extent, double viewport_extent) {
    return (rubraview_filmstrip_t){
        .visible = false,
        .thumb_extent = thumb_extent > 0.0 ? thumb_extent : 1.0,
        .viewport_extent = viewport_extent > 0.0 ? viewport_extent : 0.0,
        .scroll_offset = 0.0,
        .item_count = item_count,
        .lookahead = 4,
        .estimated_bytes = DEFAULT_THUMB_BYTES,
    };
}

rubraview_virtual_range_t rubraview_filmstrip_visible(const rubraview_filmstrip_t *strip) {
    if (!strip) return (rubraview_virtual_range_t){ .first = 0, .count = 0 };
    return rubraview_virtual_range(strip->item_count, strip->thumb_extent,
                                   strip->viewport_extent, strip->scroll_offset,
                                   strip->lookahead);
}

void rubraview_filmstrip_reveal(rubraview_filmstrip_t *strip, size_t index) {
    if (!strip) return;
    double target = rubraview_virtual_scroll_to(index, strip->thumb_extent, strip->viewport_extent, strip->scroll_offset);
    strip->scroll_offset = rubraview_virtual_clamp_offset(strip->item_count, strip->thumb_extent,
                                                          strip->viewport_extent, target);
}

void rubraview_filmstrip_scroll_by(rubraview_filmstrip_t *strip, double delta) {
    if (!strip) return;
    strip->scroll_offset = rubraview_virtual_clamp_offset(strip->item_count, strip->thumb_extent,
                                                          strip->viewport_extent,
                                                          strip->scroll_offset + delta);
}

/* Distance from the reader's current page, so touch order can run
   nearest-last and leave the closest thumbnails as most-recently-used. */
static size_t distance_from(size_t index, size_t current) {
    return index > current ? index - current : current - index;
}

size_t rubraview_filmstrip_sync_cache(const rubraview_filmstrip_t *strip,
                                      rubraview_lru_cache_t *cache,
                                      size_t current_index,
                                      uint64_t *out_evicted, size_t evicted_cap,
                                      size_t *out_needed, size_t needed_cap,
                                      size_t *out_needed_count) {
    if (out_needed_count) *out_needed_count = 0;
    if (!strip || !cache) return 0;

    rubraview_virtual_range_t range = rubraview_filmstrip_visible(strip);
    if (range.count == 0) return 0;

    size_t evicted_total = 0;
    size_t needed_total = 0;

    /* Touch furthest-first so the nearest thumbnail ends up newest and is
       the last thing the budget would ever drop. */
    size_t max_distance = 0;
    for (size_t i = 0; i < range.count; ++i) {
        size_t d = distance_from(range.first + i, current_index);
        if (d > max_distance) max_distance = d;
    }

    for (size_t pass = max_distance + 1; pass-- > 0;) {
        for (size_t i = 0; i < range.count; ++i) {
            size_t index = range.first + i;
            if (distance_from(index, current_index) != pass) continue;

            if (!rubraview_lru_contains(cache, (uint64_t)index)) {
                if (out_needed && needed_total < needed_cap) out_needed[needed_total] = index;
                needed_total++;
            }

            uint64_t evicted_scratch[8];
            size_t evicted = rubraview_lru_touch(cache, (uint64_t)index, strip->estimated_bytes,
                                                 evicted_scratch,
                                                 sizeof(evicted_scratch) / sizeof(evicted_scratch[0]));
            for (size_t e = 0; e < evicted && e < sizeof(evicted_scratch) / sizeof(evicted_scratch[0]); ++e) {
                if (out_evicted && evicted_total < evicted_cap) {
                    out_evicted[evicted_total] = evicted_scratch[e];
                }
                evicted_total++;
            }
        }
    }

    if (out_needed_count) *out_needed_count = needed_total;
    return evicted_total;
}
