#include "rubraview/picker.h"
#include "rubraview/glob.h"
#include "rubraview/path.h"
#include <ctype.h>

rubraview_breadcrumbs_t rubraview_picker_breadcrumbs(u8str_t path) {
    rubraview_breadcrumbs_t crumbs = {0};
    if (!path.ptr || path.len == 0) return crumbs;

    size_t start = 0;
    for (size_t i = 0; i <= path.len; ++i) {
        bool at_end = (i == path.len);
        if (!at_end && !rubraview_path_is_sep(path.ptr[i])) continue;

        if (i > start) {
            if (crumbs.count >= RUBRAVIEW_PICKER_MAX_CRUMBS) break;
            crumbs.items[crumbs.count].label = (u8str_t){ .ptr = path.ptr + start, .len = i - start };
            /* The prefix runs from the start of the path through this
               segment, so tapping a crumb navigates straight there. */
            crumbs.items[crumbs.count].prefix = (u8str_t){ .ptr = path.ptr, .len = i };
            crumbs.count++;
        }
        start = i + 1;
    }

    return crumbs;
}

rubraview_picker_t rubraview_picker_create(const rubraview_fs_listing_t *listing,
                                           double tile_extent, double viewport_extent, int32_t columns) {
    return (rubraview_picker_t){
        .listing = listing,
        .tile_extent = tile_extent > 0.0 ? tile_extent : 1.0,
        .viewport_extent = viewport_extent > 0.0 ? viewport_extent : 0.0,
        .scroll_offset = 0.0,
        .columns = columns > 0 ? columns : 1,
        .focus = 0,
        .multi_select = false,
        .selected = NULL,
    };
}

/* The grid scrolls by rows, so the virtual range works in rows and is
   then expanded back into item indices. */
static size_t row_count(const rubraview_picker_t *picker) {
    if (!picker->listing || picker->listing->count == 0) return 0;
    size_t columns = (size_t)picker->columns;
    return (picker->listing->count + columns - 1) / columns;
}

rubraview_virtual_range_t rubraview_picker_visible(const rubraview_picker_t *picker) {
    rubraview_virtual_range_t empty = { .first = 0, .count = 0 };
    if (!picker || !picker->listing || picker->listing->count == 0) return empty;

    rubraview_virtual_range_t rows = rubraview_virtual_range(row_count(picker), picker->tile_extent,
                                                             picker->viewport_extent, picker->scroll_offset, 1);
    if (rows.count == 0) return empty;

    size_t columns = (size_t)picker->columns;
    size_t first_item = rows.first * columns;
    size_t item_count = rows.count * columns;
    if (first_item >= picker->listing->count) return empty;
    if (first_item + item_count > picker->listing->count) {
        item_count = picker->listing->count - first_item;
    }

    return (rubraview_virtual_range_t){ .first = first_item, .count = item_count };
}

void rubraview_picker_scroll_by(rubraview_picker_t *picker, double delta) {
    if (!picker) return;
    picker->scroll_offset = rubraview_virtual_clamp_offset(row_count(picker), picker->tile_extent,
                                                            picker->viewport_extent,
                                                            picker->scroll_offset + delta);
}

void rubraview_picker_reveal_focus(rubraview_picker_t *picker) {
    if (!picker || !picker->listing || picker->listing->count == 0) return;

    size_t row = picker->focus / (size_t)picker->columns;
    double target = rubraview_virtual_scroll_to(row, picker->tile_extent, picker->viewport_extent, picker->scroll_offset);
    picker->scroll_offset = rubraview_virtual_clamp_offset(row_count(picker), picker->tile_extent,
                                                            picker->viewport_extent, target);
}

bool rubraview_picker_type_ahead(rubraview_picker_t *picker, char letter) {
    if (!picker || !picker->listing || picker->listing->count == 0) return false;

    int wanted = tolower((unsigned char)letter);
    size_t count = picker->listing->count;

    /* Start after the current focus so pressing the same letter again
       steps to the next match rather than sticking on the first. */
    for (size_t step = 1; step <= count; ++step) {
        size_t index = (picker->focus + step) % count;
        u8str_t name = picker->listing->entries[index].name;
        if (name.len == 0) continue;

        if (tolower((unsigned char)name.ptr[0]) == wanted) {
            picker->focus = index;
            rubraview_picker_reveal_focus(picker);
            return true;
        }
    }
    return false;
}

void rubraview_picker_toggle(rubraview_picker_t *picker, size_t index) {
    if (!picker || !picker->multi_select || !picker->selected) return;
    if (!picker->listing || index >= picker->listing->count) return;
    picker->selected[index] = !picker->selected[index];
}

void rubraview_picker_selection_metrics(const rubraview_picker_t *picker,
                                        size_t *out_count, uint64_t *out_total_bytes) {
    size_t count = 0;
    uint64_t bytes = 0;

    if (picker && picker->listing && picker->selected) {
        for (size_t i = 0; i < picker->listing->count; ++i) {
            if (!picker->selected[i]) continue;
            count++;
            bytes += picker->listing->entries[i].size_bytes;
        }
    }

    if (out_count) *out_count = count;
    if (out_total_bytes) *out_total_bytes = bytes;
}

size_t rubraview_picker_keep_openable(rubraview_fs_listing_t *listing, u8str_t patterns) {
    if (!listing || !listing->entries) return 0;
    size_t kept = 0;
    for (size_t i = 0; i < listing->count; ++i) {
        const rubraview_fs_entry_t *e = &listing->entries[i];
        if (e->is_directory || rubraview_glob_match_list(e->name, patterns)) {
            listing->entries[kept++] = *e;
        }
    }
    size_t hidden = listing->count - kept;
    listing->count = kept;
    return hidden;
}
