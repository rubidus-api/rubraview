#include "rubraview/ui_virtual.h"

rubraview_virtual_range_t rubraview_virtual_range(size_t item_count,
                                                  double item_extent,
                                                  double viewport_extent,
                                                  double scroll_offset,
                                                  size_t lookahead) {
    rubraview_virtual_range_t range = { .first = 0, .count = 0 };
    if (item_count == 0 || item_extent <= 0.0 || viewport_extent <= 0.0) return range;
    if (scroll_offset < 0.0) scroll_offset = 0.0;

    double first_visible = scroll_offset / item_extent;
    size_t first = (size_t)first_visible;

    /* How many items an aligned viewport spans, plus one for the item
       straddling each edge. */
    size_t visible = (size_t)(viewport_extent / item_extent) + 2;

    first = (first > lookahead) ? first - lookahead : 0;
    size_t count = visible + lookahead * 2;

    if (first >= item_count) {
        range.first = item_count;
        range.count = 0;
        return range;
    }
    if (first + count > item_count) count = item_count - first;

    range.first = first;
    range.count = count;
    return range;
}

double rubraview_virtual_clamp_offset(size_t item_count, double item_extent, double viewport_extent, double offset) {
    if (item_count == 0 || item_extent <= 0.0) return 0.0;

    double total = (double)item_count * item_extent;
    double max_offset = total - viewport_extent;
    if (max_offset < 0.0) max_offset = 0.0; /* everything fits: no scrolling */

    if (offset < 0.0) return 0.0;
    if (offset > max_offset) return max_offset;
    return offset;
}

double rubraview_virtual_scroll_to(size_t index, double item_extent, double viewport_extent, double current_offset) {
    if (item_extent <= 0.0) return current_offset;

    double item_start = (double)index * item_extent;
    double item_end = item_start + item_extent;

    /* Already fully visible: leave the offset alone rather than jumping. */
    if (item_start >= current_offset && item_end <= current_offset + viewport_extent) {
        return current_offset;
    }
    /* Scroll the minimum distance that brings it into view. */
    if (item_start < current_offset) return item_start;
    return item_end - viewport_extent;
}
