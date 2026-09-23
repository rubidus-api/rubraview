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
        .mode = RUBRAVIEW_PICK_SINGLE,
        .range_anchor = RUBRAVIEW_PICKER_NO_ANCHOR,
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

static bool pickable(const rubraview_picker_t *picker, size_t index) {
    return picker && picker->listing && picker->selected && index < picker->listing->count &&
           !picker->listing->entries[index].is_directory;
}

void rubraview_picker_set_mode(rubraview_picker_t *picker, rubraview_picker_mode_t mode) {
    if (!picker) return;
    picker->mode = mode;
    picker->range_anchor = RUBRAVIEW_PICKER_NO_ANCHOR;
    picker->multi_select = mode != RUBRAVIEW_PICK_SINGLE;
}

bool rubraview_picker_tap(rubraview_picker_t *picker, size_t index) {
    if (!picker || picker->mode == RUBRAVIEW_PICK_SINGLE) return false;
    if (!pickable(picker, index)) return false;

    if (picker->mode == RUBRAVIEW_PICK_INDIVIDUAL) {
        picker->selected[index] = !picker->selected[index];
        return true;
    }

    /* A range: the first tap marks where it starts and turns that one
       over, so the reader can see it; the second turns over the rest of
       the way there. Between them, every item is inverted exactly once. */
    if (picker->range_anchor == RUBRAVIEW_PICKER_NO_ANCHOR) {
        picker->range_anchor = index;
        picker->selected[index] = !picker->selected[index];
        return true;
    }
    size_t from = picker->range_anchor < index ? picker->range_anchor : index;
    size_t to = picker->range_anchor < index ? index : picker->range_anchor;
    for (size_t i = from; i <= to; ++i) {
        if (i == picker->range_anchor) continue;   /* already turned over by the first tap */
        if (!pickable(picker, i)) continue;
        picker->selected[i] = !picker->selected[i];
    }
    picker->range_anchor = RUBRAVIEW_PICKER_NO_ANCHOR;
    return true;
}

size_t rubraview_picker_select_extension(rubraview_picker_t *picker, u8str_t ext, bool on) {
    if (!picker || !picker->listing || !picker->selected || ext.len == 0) return 0;
    size_t changed = 0;
    for (size_t i = 0; i < picker->listing->count; ++i) {
        if (!pickable(picker, i)) continue;
        u8str_t name = picker->listing->entries[i].name;
        if (name.len < ext.len) continue;
        const char *tail = name.ptr + (name.len - ext.len);
        bool same = true;
        for (size_t k = 0; k < ext.len && same; ++k) {
            char a = tail[k], b = ext.ptr[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            same = a == b;
        }
        if (!same || picker->selected[i] == on) continue;
        picker->selected[i] = on;
        changed++;
    }
    return changed;
}

void rubraview_picker_clear_selection(rubraview_picker_t *picker) {
    if (!picker || !picker->listing || !picker->selected) return;
    for (size_t i = 0; i < picker->listing->count; ++i) picker->selected[i] = false;
    picker->range_anchor = RUBRAVIEW_PICKER_NO_ANCHOR;
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
