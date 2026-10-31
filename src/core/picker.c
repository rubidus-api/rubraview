#include "rubraview/picker.h"
#include "rubraview/lang.h"
#include <stdio.h>
#include "rubraview/glob.h"
#include "rubraview/path.h"
#include "rubraview/sort.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

bool rubraview_picker_is_places(proven_u8str_view_t dir) {
    return proven_u8str_view_eq(dir, proven_u8str_view_from_cstr(RUBRAVIEW_PICKER_PLACES));
}

bool rubraview_picker_is_recent(proven_u8str_view_t dir) {
    return proven_u8str_view_eq(dir, proven_u8str_view_from_cstr(RUBRAVIEW_PICKER_RECENT));
}

proven_u8str_view_t rubraview_picker_parent(proven_u8str_view_t dir) {
    const proven_u8str_view_t none = PROVEN_LIT("");
    const proven_u8str_view_t places = PROVEN_LIT(RUBRAVIEW_PICKER_PLACES);
    if (!dir.ptr || dir.size == 0 || rubraview_picker_is_places(dir)) return none;
    if (rubraview_picker_is_recent(dir)) return places;

    size_t len = dir.size;
    while (len > 0 && rubraview_path_is_sep(dir.ptr[len - 1])) --len;
    if (len == 0) return places;                                   /* "/" */
    if (len == 2 && dir.ptr[1] == ':') return places;              /* "C:", "C:/" */
    if (dir.size >= 2 && rubraview_path_is_sep(dir.ptr[0]) && rubraview_path_is_sep(dir.ptr[1])) {
        /* "//server/share" is a root too: count the names after the "//". */
        size_t names = 0;
        bool in_name = false;
        for (size_t i = 2; i < len; ++i) {
            bool sep = rubraview_path_is_sep(dir.ptr[i]);
            if (!sep && !in_name) ++names;
            in_name = !sep;
        }
        if (names <= 2) return places;
    }
    proven_u8str_view_t parent = rubraview_path_dirname((proven_u8str_view_t){ .ptr = dir.ptr, .size = len });
    return parent.size > 0 ? parent : places;
}

static char fold(char c) {
    if (c == '\\') return '/';
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool rubraview_picker_inside_archive(proven_u8str_view_t dir, proven_u8str_view_t archive_path, proven_u8str_view_t *out_inner) {
    size_t n = archive_path.size;
    while (n > 0 && rubraview_path_is_sep(archive_path.ptr[n - 1])) --n;
    if (n == 0 || !dir.ptr || dir.size < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (fold(dir.ptr[i]) != fold(archive_path.ptr[i])) return false;
    }
    if (dir.size > n && !rubraview_path_is_sep(dir.ptr[n])) return false;   /* "Big.cbz2" is another file */
    size_t start = n, end = dir.size;
    while (start < end && rubraview_path_is_sep(dir.ptr[start])) ++start;
    while (end > start && rubraview_path_is_sep(dir.ptr[end - 1])) --end;
    if (out_inner) *out_inner = (proven_u8str_view_t){ .ptr = dir.ptr + start, .size = end - start };
    return true;
}

/* The part of `name` after the folder `inner`, when the page is inside it.
   Separators alike; the book's own names are compared as they are. */
static bool after_folder(proven_u8str_view_t name, proven_u8str_view_t inner, proven_u8str_view_t *rest) {
    if (inner.size == 0) { *rest = name; return true; }
    if (name.size <= inner.size + 1) return false;
    for (size_t i = 0; i < inner.size; ++i) {
        char a = name.ptr[i], b = inner.ptr[i];
        if (a == b || (rubraview_path_is_sep(a) && rubraview_path_is_sep(b))) continue;
        return false;
    }
    if (!rubraview_path_is_sep(name.ptr[inner.size])) return false;
    *rest = (proven_u8str_view_t){ .ptr = name.ptr + inner.size + 1, .size = name.size - inner.size - 1 };
    return true;
}

static proven_u8str_view_t join3(proven_arena_t *arena, proven_u8str_view_t a, proven_u8str_view_t b, proven_u8str_view_t c) {
    proven_u8str_view_t ab = b.size > 0 ? rubraview_path_join(arena, a, b) : a;
    return rubraview_path_join(arena, ab, c);
}

rubraview_archive_level_t rubraview_picker_archive_level(proven_arena_t *arena, proven_u8str_view_t archive_path,
                                                         const rubraview_page_ref_t *pages, size_t page_count,
                                                         proven_u8str_view_t inner) {
    rubraview_archive_level_t level = {0};
    if (!arena || !pages || page_count == 0) return level;
    proven_result_mem_mut_t er = rubraview_arena_alloc_array(arena, page_count, sizeof(rubraview_fs_entry_t));
    proven_result_mem_mut_t pr = rubraview_arena_alloc_array(arena, page_count, sizeof(size_t));
    proven_result_mem_mut_t fr = rubraview_arena_alloc_array(arena, page_count, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(er.err) || !proven_is_ok(pr.err) || !proven_is_ok(fr.err)) return level;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)er.value.ptr;
    size_t *page_of = (size_t*)(void*)pr.value.ptr;
    proven_u8str_view_t *folders = (proven_u8str_view_t*)(void*)fr.value.ptr;

    /* Folders first, in the order the book reaches them; a folder's pages
       are together, so only the last one found needs comparing. */
    size_t folder_count = 0;
    for (size_t i = 0; i < page_count; ++i) {
        proven_u8str_view_t rest;
        if (!after_folder(pages[i].name, inner, &rest)) continue;
        size_t cut = 0;
        while (cut < rest.size && !rubraview_path_is_sep(rest.ptr[cut])) ++cut;
        if (cut == rest.size || cut == 0) continue;   /* a page here, or an empty part */
        proven_u8str_view_t folder = { .ptr = rest.ptr, .size = cut };
        if (folder_count > 0 && folders[folder_count - 1].size == cut &&
            memcmp(folders[folder_count - 1].ptr, folder.ptr, cut) == 0) continue;
        folders[folder_count++] = folder;
    }
    size_t count = 0;
    for (size_t f = 0; f < folder_count; ++f) {
        entries[count] = (rubraview_fs_entry_t){
            .name = folders[f], .path = join3(arena, archive_path, inner, folders[f]), .is_directory = true,
        };
        page_of[count++] = RUBRAVIEW_PICKER_NOT_A_PAGE;
    }
    for (size_t i = 0; i < page_count; ++i) {
        proven_u8str_view_t rest;
        if (!after_folder(pages[i].name, inner, &rest) || rest.size == 0) continue;
        bool nested = false;
        for (size_t k = 0; k < rest.size && !nested; ++k) nested = rubraview_path_is_sep(rest.ptr[k]);
        if (nested) continue;
        entries[count] = (rubraview_fs_entry_t){ .name = rest, .path = join3(arena, archive_path, inner, rest) };
        page_of[count++] = i;
    }
    level.listing = (rubraview_fs_listing_t){ .entries = entries, .count = count };
    level.page_of = page_of;
    return level;
}

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

proven_u8str_view_t rubraview_picker_typed_path(proven_arena_t *arena, proven_u8str_view_t typed, proven_u8str_view_t current) {
    const proven_u8str_view_t none = PROVEN_LIT("");
    if (!arena || !typed.ptr) return none;
    const char *p = (const char *)typed.ptr;
    size_t n = typed.size;
    while (n > 0 && is_space(p[0])) { ++p; --n; }
    while (n > 0 && is_space(p[n - 1])) --n;
    if (n >= 2 && p[0] == '"' && p[n - 1] == '"') { ++p; n -= 2; }
    while (n > 0 && is_space(p[0])) { ++p; --n; }
    while (n > 0 && is_space(p[n - 1])) --n;
    if (n == 0) return none;

    bool drive = n >= 2 && p[1] == ':' && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'));
    /* A trailing separator goes, but "/" and "C:/" are roots and keep theirs. */
    size_t keep = drive ? 3 : 1;
    while (n > keep && rubraview_path_is_sep(p[n - 1])) --n;

    proven_u8str_view_t cleaned = { .ptr = (const proven_byte_t *)p, .size = n };
    if (drive && n == 2) cleaned = rubraview_path_join(arena, cleaned, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"/", .size = 1 });
    else if (!drive && !rubraview_path_is_sep(p[0]) && current.size > 0 && !rubraview_picker_is_places(current)) {
        cleaned = rubraview_path_join(arena, current, cleaned);
    }
    if ((const char *)cleaned.ptr == p) {
        /* A slice of what was typed: copied, so the OS gets its NUL. */
        proven_result_mem_mut_t res = proven_arena_alloc(arena, n + 1);
        if (!proven_is_ok(res.err)) return none;
        memcpy(res.value.ptr, p, n);
        res.value.ptr[n] = '\0';
        cleaned = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)res.value.ptr, .size = n };
    }
    return cleaned;
}

rubraview_breadcrumbs_t rubraview_picker_breadcrumbs(proven_u8str_view_t path) {
    rubraview_breadcrumbs_t crumbs = {0};
    if (!path.ptr || path.size == 0) return crumbs;

    crumbs.items[0].label = proven_u8str_view_from_bytes(T("PC"), strlen(T("PC")));
    crumbs.items[0].prefix = PROVEN_LIT(RUBRAVIEW_PICKER_PLACES);
    crumbs.count = 1;
    if (rubraview_picker_is_places(path)) return crumbs;
    if (rubraview_picker_is_recent(path)) {
        crumbs.items[1].label = proven_u8str_view_from_bytes(T("Recent"), strlen(T("Recent")));
        crumbs.items[1].prefix = PROVEN_LIT(RUBRAVIEW_PICKER_RECENT);
        crumbs.count = 2;
        return crumbs;
    }

    size_t start = 0;
    for (size_t i = 0; i <= path.size; ++i) {
        bool at_end = (i == path.size);
        if (!at_end && !rubraview_path_is_sep(path.ptr[i])) continue;

        if (i > start) {
            if (crumbs.count >= RUBRAVIEW_PICKER_MAX_CRUMBS) break;
            crumbs.items[crumbs.count].label = (proven_u8str_view_t){ .ptr = path.ptr + start, .size = i - start };
            /* The prefix runs from the start of the path through this
               segment, so tapping a crumb navigates straight there. */
            crumbs.items[crumbs.count].prefix = (proven_u8str_view_t){ .ptr = path.ptr, .size = i };
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
        proven_u8str_view_t name = picker->listing->entries[index].name;
        if (name.size == 0) continue;

        if (tolower((unsigned char)name.ptr[0]) == wanted) {
            picker->focus = index;
            rubraview_picker_reveal_focus(picker);
            return true;
        }
    }
    return false;
}

/* 1.25.0 (owner, 2026-10-20: "하나씩 혹은 범위버튼을 눌렀을 때 ... 선택과 범위 지정의 역할만"): a folder is picked
   like a file in those modes; only `..` is still the way up. */
static bool pickable(const rubraview_picker_t *picker, size_t index) {
    if (!picker || !picker->listing || !picker->selected || index >= picker->listing->count) return false;
    proven_u8str_view_t name = picker->listing->entries[index].name;
    return !(picker->listing->entries[index].is_directory && name.size == 2 && name.ptr[0] == '.' && name.ptr[1] == '.');
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

size_t rubraview_picker_select_extension(rubraview_picker_t *picker, proven_u8str_view_t ext, bool on) {
    if (!picker || !picker->listing || !picker->selected || ext.size == 0) return 0;
    size_t changed = 0;
    for (size_t i = 0; i < picker->listing->count; ++i) {
        if (!pickable(picker, i)) continue;
        proven_u8str_view_t name = picker->listing->entries[i].name;
        if (name.size < ext.size) continue;
        bool same = proven_u8str_view_eq_ascii_ci(proven_u8str_view_slice(name, name.size - ext.size, ext.size), ext);
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

size_t rubraview_picker_keep_openable(rubraview_fs_listing_t *listing, proven_u8str_view_t patterns) {
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

size_t rubraview_picker_size_text(char *buf, size_t cap, uint64_t bytes) {
    int n;
    if (!buf || cap == 0) return 0;
    if (bytes < 1024u) n = snprintf(buf, cap, "%u B", (unsigned)bytes);
    else if (bytes < 1024u * 1024u) n = snprintf(buf, cap, T("%.0f KB"), (double)bytes / 1024.0);
    else if (bytes < 1024ull * 1024ull * 1024ull) n = snprintf(buf, cap, T("%.1f MB"), (double)bytes / (1024.0 * 1024.0));
    else n = snprintf(buf, cap, T("%.2f GB"), (double)bytes / (1024.0 * 1024.0 * 1024.0));
    if (n <= 0 || (size_t)n >= cap) { buf[0] = '\0'; return 0; }
    return (size_t)n;
}

size_t rubraview_picker_type_text(char *buf, size_t cap, proven_u8str_view_t name, bool is_directory) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    const char *word = is_directory ? T("Folder") : T("File");
    size_t dot = name.size;
    for (size_t i = name.size; !is_directory && i-- > 1;) {
        if (name.ptr[i] == '.') { dot = i; break; }
        if (name.ptr[i] == '/' || name.ptr[i] == '\\') break;
    }
    size_t ext = dot < name.size ? name.size - dot - 1 : 0;
    if (ext > 0 && ext <= 8 && ext < cap) {
        for (size_t i = 0; i < ext; ++i) {
            char c = name.ptr[dot + 1 + i];
            buf[i] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
        }
        buf[ext] = '\0';
        return ext;
    }
    size_t n = 0;
    while (word[n]) n++;
    if (n >= cap) return 0;
    for (size_t i = 0; i <= n; ++i) buf[i] = word[i];
    return n;
}

size_t rubraview_picker_select_all(rubraview_picker_t *picker) {
    if (!picker || !picker->selected || !picker->listing) return 0;
    size_t n = 0;
    for (size_t i = 0; i < picker->listing->count; ++i) {
        picker->selected[i] = !picker->listing->entries[i].is_directory;
        if (picker->selected[i]) n++;
    }
    picker->range_anchor = RUBRAVIEW_PICKER_NO_ANCHOR;
    return n;
}

bool rubraview_picker_all_selected(const rubraview_picker_t *picker) {
    if (!picker || !picker->selected || !picker->listing) return false;
    size_t files = 0;
    for (size_t i = 0; i < picker->listing->count; ++i) {
        if (picker->listing->entries[i].is_directory) continue;
        files++;
        if (!picker->selected[i]) return false;
    }
    return files > 0;
}

const char *rubraview_picker_select_label(const rubraview_picker_t *picker) {
    return rubraview_picker_all_selected(picker) ? T("Deselect all") : T("Select all");
}

const char *rubraview_picker_sort_name(rubraview_picker_sort_key_t key) {
    const char *const NAMES[RUBRAVIEW_PICKER_SORT_COUNT] = {
        T("Name"), T("Type"), T("Size"), T("Created"), T("Modified"), T("Resolution"), T("Length"),
    };
    return key >= 0 && key < RUBRAVIEW_PICKER_SORT_COUNT ? NAMES[key] : "";
}

/* The extension, without its dot; empty for a folder or a name without one. */
static proven_u8str_view_t sort_ext(const rubraview_fs_entry_t *e) {
    if (e->is_directory) return PROVEN_LIT("");
    for (size_t k = e->name.size; k > 0; --k) {
        if (e->name.ptr[k - 1] == '.') return (proven_u8str_view_t){ .ptr = e->name.ptr + k, .size = e->name.size - k };
    }
    return PROVEN_LIT("");
}

typedef struct sort_ctx {
    const rubraview_fs_listing_t *listing;
    const rubraview_picker_media_t *media;
    rubraview_picker_sort_key_t key;
    bool descending;
} sort_ctx_t;

static bool is_dotdot(const rubraview_fs_entry_t *e) {
    return e->is_directory && e->name.size == 2 && e->name.ptr[0] == '.' && e->name.ptr[1] == '.';
}

/* <0 when entry a goes before entry b. */
static int sort_cmp(const sort_ctx_t *c, size_t ia, size_t ib) {
    const rubraview_fs_entry_t *a = &c->listing->entries[ia], *b = &c->listing->entries[ib];
    if (is_dotdot(a) != is_dotdot(b)) return is_dotdot(a) ? -1 : 1;
    if (a->is_directory != b->is_directory) return a->is_directory ? -1 : 1;
    int r = 0;
    bool unread_a = false, unread_b = false;
    switch (c->key) {
        case RUBRAVIEW_PICKER_SORT_TYPE: r = rubraview_str_lexcmp(sort_ext(a), sort_ext(b)); break;
        case RUBRAVIEW_PICKER_SORT_SIZE: r = a->size_bytes < b->size_bytes ? -1 : a->size_bytes > b->size_bytes; break;
        case RUBRAVIEW_PICKER_SORT_CREATED: r = a->ctime < b->ctime ? -1 : a->ctime > b->ctime; break;
        case RUBRAVIEW_PICKER_SORT_MODIFIED: r = a->mtime < b->mtime ? -1 : a->mtime > b->mtime; break;
        case RUBRAVIEW_PICKER_SORT_RESOLUTION: {
            int64_t pa = c->media ? (int64_t)c->media[ia].width * c->media[ia].height : 0;
            int64_t pb = c->media ? (int64_t)c->media[ib].width * c->media[ib].height : 0;
            unread_a = pa <= 0; unread_b = pb <= 0;
            r = pa < pb ? -1 : pa > pb;
            break;
        }
        case RUBRAVIEW_PICKER_SORT_LENGTH: {
            double sa = c->media ? c->media[ia].seconds : 0.0, sb = c->media ? c->media[ib].seconds : 0.0;
            unread_a = !(sa > 0.0); unread_b = !(sb > 0.0);
            r = sa < sb ? -1 : sa > sb;
            break;
        }
        case RUBRAVIEW_PICKER_SORT_NAME:
        default: r = 0; break;
    }
    /* not read yet: after the read ones, whichever way */
    if (unread_a != unread_b) return unread_a ? 1 : -1;
    if (c->descending) r = -r;
    if (r == 0) {
        r = rubraview_str_natcmp(a->name, b->name);
        if (c->descending && c->key == RUBRAVIEW_PICKER_SORT_NAME) r = -r;
    }
    return r;
}

void rubraview_picker_sort_order(const rubraview_fs_listing_t *listing, const rubraview_picker_media_t *media,
                                 rubraview_picker_sort_key_t key, bool descending, size_t *order) {
    if (!listing || !order) return;
    size_t n = listing->count;
    for (size_t i = 0; i < n; ++i) order[i] = i;
    sort_ctx_t c = { .listing = listing, .media = media, .key = key, .descending = descending };
    size_t *tmp = n > 1 ? (size_t*)malloc(n * sizeof(size_t)) : NULL;
    if (!tmp) {
        /* no room for the merge: insertion, stable too */
        for (size_t i = 1; i < n; ++i) {
            size_t v = order[i], j = i;
            while (j > 0 && sort_cmp(&c, order[j - 1], v) > 0) { order[j] = order[j - 1]; --j; }
            order[j] = v;
        }
        return;
    }
    /* bottom-up merge sort: stable, n log n for a folder of ten thousand */
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n, hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) tmp[k++] = sort_cmp(&c, order[j], order[i]) < 0 ? order[j++] : order[i++];
            while (i < mid) tmp[k++] = order[i++];
            while (j < hi) tmp[k++] = order[j++];
        }
        memcpy(order, tmp, n * sizeof(size_t));
    }
    free(tmp);
}

int32_t rubraview_folder_show_for(int32_t show, int32_t opened_kind, bool same_kind) {
    if (show < 0 || show > 3) show = 0;
    return show == 0 && same_kind && opened_kind >= 1 && opened_kind <= 3 ? opened_kind : show;
}

/* ---- RV-192: by shape ---- */

rubraview_shape_t rubraview_shape_of(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return RUBRAVIEW_SHAPE_UNKNOWN;
    return width > height ? RUBRAVIEW_SHAPE_WIDE : width < height ? RUBRAVIEW_SHAPE_TALL : RUBRAVIEW_SHAPE_SQUARE;
}

void rubraview_shape_count(const bool *selected, const uint8_t *shapes, const size_t *rows, size_t n,
                           size_t total[RUBRAVIEW_SHAPE_COUNT], size_t picked[RUBRAVIEW_SHAPE_COUNT]) {
    for (size_t k = 0; k < RUBRAVIEW_SHAPE_COUNT; ++k) total[k] = picked[k] = 0;
    if (!shapes) return;
    for (size_t r = 0; r < n; ++r) {
        size_t i = rows ? rows[r] : r;
        uint8_t k = shapes[i] < RUBRAVIEW_SHAPE_COUNT ? shapes[i] : RUBRAVIEW_SHAPE_UNKNOWN;
        total[k]++;
        if (selected && selected[i]) picked[k]++;
    }
}

size_t rubraview_shape_toggle(bool *selected, const uint8_t *shapes, const size_t *rows, size_t n,
                              rubraview_shape_t which, bool *out_picked) {
    if (out_picked) *out_picked = false;
    if (!selected || !shapes || which <= RUBRAVIEW_SHAPE_UNKNOWN || which >= RUBRAVIEW_SHAPE_COUNT) return 0;
    size_t total[RUBRAVIEW_SHAPE_COUNT], picked[RUBRAVIEW_SHAPE_COUNT];
    rubraview_shape_count(selected, shapes, rows, n, total, picked);
    if (total[which] == 0) return 0;
    bool pick = picked[which] < total[which];
    for (size_t r = 0; r < n; ++r) {
        size_t i = rows ? rows[r] : r;
        if (shapes[i] == (uint8_t)which) selected[i] = pick;
    }
    if (out_picked) *out_picked = pick;
    return total[which];
}

struct rubraview_shape_slot {
    char *path;          /* NULL: empty */
    size_t len;
    int64_t mtime;
    uint64_t hash;       /* of the path: one slot a path (RV-210) */
    uint8_t shape;
    double seconds;      /* RV-202: below 0 not known */
    int32_t width, height;   /* RV-210: as shown; 0 not known */
    bool verified;       /* RV-210: read or its time seen this run; one from the file on disk is not, until then */
    int64_t stamp;       /* RV-210: when last put, for which ones the file keeps */
};

/* One file however it is written: case and either slash alike, as rubraview_path_same compares (the open-file
   screen's listing and a list's pages may name one file both ways). */
static uint64_t shape_hash(proven_u8str_view_t path) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < path.size; ++i) {
        uint8_t c = (uint8_t)path.ptr[i];
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + 'a');
        if (rubraview_path_is_sep((char)c)) c = '/';
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

/* The path's slot, or the empty one it would take; NULL when the table is full or not made. */
static struct rubraview_shape_slot *shape_find(const rubraview_shape_cache_t *cache, proven_u8str_view_t path, uint64_t h) {
    if (!cache->slots || cache->capacity == 0) return NULL;
    size_t mask = cache->capacity - 1;
    for (size_t at = (size_t)h & mask, tries = 0; tries < cache->capacity; at = (at + 1) & mask, ++tries) {
        struct rubraview_shape_slot *s = &cache->slots[at];
        if (!s->path) return s;
        if (s->hash == h && rubraview_path_same((proven_u8str_view_t){ .ptr = (const proven_byte_t *)s->path, .size = s->len }, path)) return s;
    }
    return NULL;
}

/* The slot kept for the file as it is: its time the one given, or (time 0, a list's page whose time is not
   known) one read or seen this run. */
static const struct rubraview_shape_slot *shape_known(const rubraview_shape_cache_t *cache, proven_u8str_view_t path, int64_t mtime) {
    if (!cache || path.size == 0) return NULL;
    const struct rubraview_shape_slot *s = shape_find(cache, path, shape_hash(path));
    if (!s || !s->path) return NULL;
    return (mtime != 0 ? s->mtime == mtime : s->verified) ? s : NULL;
}

bool rubraview_shape_cache_get(const rubraview_shape_cache_t *cache, proven_u8str_view_t path, int64_t mtime,
                               rubraview_shape_t *out) {
    const struct rubraview_shape_slot *s = shape_known(cache, path, mtime);
    if (s && out) *out = (rubraview_shape_t)s->shape;
    return s != NULL;
}

bool rubraview_shape_cache_put(rubraview_shape_cache_t *cache, proven_u8str_view_t path, int64_t mtime,
                               rubraview_shape_t shape) {
    double seconds = -1.0;
    (void)rubraview_shape_cache_length(cache, path, mtime, &seconds);   /* a length kept stays */
    return rubraview_shape_cache_put_length(cache, path, mtime, shape, seconds);
}

bool rubraview_shape_cache_length(const rubraview_shape_cache_t *cache, proven_u8str_view_t path, int64_t mtime, double *out_seconds) {
    const struct rubraview_shape_slot *s = shape_known(cache, path, mtime);
    if (!s || !(s->seconds >= 0.0)) return false;
    if (out_seconds) *out_seconds = s->seconds;
    return true;
}

bool rubraview_shape_cache_facts(const rubraview_shape_cache_t *cache, proven_u8str_view_t path, int64_t mtime,
                                 int32_t *out_width, int32_t *out_height, double *out_seconds) {
    const struct rubraview_shape_slot *s = shape_known(cache, path, mtime);
    if (!s) return false;
    if (out_width) *out_width = s->width;
    if (out_height) *out_height = s->height;
    if (out_seconds) *out_seconds = s->seconds;
    return true;
}

bool rubraview_shape_cache_unverified(const rubraview_shape_cache_t *cache, proven_u8str_view_t path, int64_t *out_mtime) {
    if (!cache || path.size == 0) return false;
    const struct rubraview_shape_slot *s = shape_find(cache, path, shape_hash(path));
    if (!s || !s->path || s->verified || s->mtime == 0) return false;
    if (out_mtime) *out_mtime = s->mtime;
    return true;
}

bool rubraview_shape_cache_confirm(rubraview_shape_cache_t *cache, proven_u8str_view_t path, int64_t mtime) {
    if (!cache || path.size == 0) return false;
    struct rubraview_shape_slot *s = shape_find(cache, path, shape_hash(path));
    if (!s || !s->path || s->mtime != mtime) return false;
    s->verified = true;
    return true;
}

static struct rubraview_shape_slot *shape_slot(rubraview_shape_cache_t *cache, proven_u8str_view_t path) {
    if (!cache || path.size == 0) return NULL;
    if (cache->count >= RUBRAVIEW_SHAPE_CACHE_MAX) rubraview_shape_cache_free(cache);   /* starts over */
    if (!cache->slots || (cache->count + 1) * 10 > cache->capacity * 7) {
        size_t capacity = cache->capacity ? cache->capacity * 2 : 256;
        struct rubraview_shape_slot *slots = (struct rubraview_shape_slot*)calloc(capacity, sizeof(*slots));
        if (!slots) return NULL;
        rubraview_shape_cache_t grown = { .slots = slots, .capacity = capacity, .count = cache->count, .clock = cache->clock };
        for (size_t i = 0; i < cache->capacity; ++i) {
            if (!cache->slots[i].path) continue;
            struct rubraview_shape_slot *to = shape_find(&grown, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)cache->slots[i].path,
                                                                                        .size = cache->slots[i].len },
                                                         cache->slots[i].hash);
            *to = cache->slots[i];
        }
        free(cache->slots);
        *cache = grown;
    }
    uint64_t h = shape_hash(path);
    struct rubraview_shape_slot *s = shape_find(cache, path, h);
    if (!s) return NULL;
    if (!s->path) {
        char *copy = (char*)malloc(path.size);
        if (!copy) return NULL;
        memcpy(copy, path.ptr, path.size);
        *s = (struct rubraview_shape_slot){ .path = copy, .len = path.size, .hash = h, .seconds = -1.0 };
        cache->count++;
    }
    return s;
}

bool rubraview_shape_cache_put_length(rubraview_shape_cache_t *cache, proven_u8str_view_t path, int64_t mtime,
                                      rubraview_shape_t shape, double seconds) {
    struct rubraview_shape_slot *s = shape_slot(cache, path);
    if (!s) return false;
    if (s->mtime != mtime) { s->width = 0; s->height = 0; }   /* another file now: what was kept of the old one goes */
    s->mtime = mtime;
    s->shape = (uint8_t)shape;
    s->seconds = seconds >= 0.0 ? seconds : -1.0;
    s->verified = true;
    s->stamp = ++cache->clock;
    return true;
}

bool rubraview_shape_cache_put_facts(rubraview_shape_cache_t *cache, proven_u8str_view_t path, int64_t mtime,
                                     int32_t width, int32_t height, double seconds) {
    if (!rubraview_shape_cache_put_length(cache, path, mtime, rubraview_shape_of(width, height), seconds > 0.0 ? seconds : -1.0)) return false;
    struct rubraview_shape_slot *s = shape_slot(cache, path);
    s->width = width > 0 && height > 0 ? width : 0;
    s->height = width > 0 && height > 0 ? height : 0;
    return true;
}

/* ---- RV-210: the facts kept on disk ----
   A line a file: its time, width, height, length (- when not known), shape, path; tabs between, the newest first. */

#define FACTS_HEAD "rubraview facts 1\n"

size_t rubraview_shape_cache_parse(rubraview_shape_cache_t *cache, const char *text, size_t size) {
    if (!cache || !text || size < sizeof(FACTS_HEAD) - 1 || memcmp(text, FACTS_HEAD, sizeof(FACTS_HEAD) - 1) != 0) return 0;
    size_t added = 0, line = 0;
    for (size_t at = sizeof(FACTS_HEAD) - 1; at < size; ++line) {
        const char *start = text + at;
        const char *nl = memchr(start, '\n', size - at);
        size_t n = nl ? (size_t)(nl - start) : size - at;
        at += n + 1;
        char buf[64];
        const char *tab = start;
        int fields = 0;
        long long values[5] = {0};
        double seconds = -1.0;
        bool ok = true;
        for (; fields < 5 && ok; ++fields) {   /* five numbers, then the path */
            const char *end = memchr(tab, '\t', n - (size_t)(tab - start));
            if (!end || end == tab || (size_t)(end - tab) >= sizeof(buf)) { ok = false; break; }
            memcpy(buf, tab, (size_t)(end - tab));
            buf[end - tab] = '\0';
            char *stop = NULL;
            if (fields == 3) { seconds = buf[0] == '-' ? -1.0 : strtod(buf, &stop); ok = buf[0] == '-' || (stop && *stop == '\0'); }
            else { values[fields] = strtoll(buf, &stop, 10); ok = stop && *stop == '\0'; }
            tab = end + 1;
        }
        size_t path_len = n - (size_t)(tab - start);
        if (!ok || path_len == 0 || values[0] == 0 || values[4] < 0 || values[4] >= RUBRAVIEW_SHAPE_COUNT) continue;
        proven_u8str_view_t path = { .ptr = (const proven_byte_t *)tab, .size = path_len };
        if (cache->count >= RUBRAVIEW_SHAPE_CACHE_MAX - 1) break;
        struct rubraview_shape_slot *s = shape_find(cache, path, shape_hash(path));
        if (s && s->path) continue;   /* this run's own stays */
        s = shape_slot(cache, path);
        if (!s) break;
        s->mtime = values[0];
        s->width = values[1] > 0 && values[1] <= INT32_MAX && values[2] > 0 && values[2] <= INT32_MAX ? (int32_t)values[1] : 0;
        s->height = s->width ? (int32_t)values[2] : 0;
        s->seconds = seconds >= 0.0 ? seconds : -1.0;
        s->shape = (uint8_t)values[4];
        s->verified = false;
        s->stamp = -(int64_t)line - 1;   /* older than all of this run's, in the file's order */
        added++;
    }
    return added;
}

static int stamp_newer(const void *a, const void *b) {
    int64_t x = (*(const struct rubraview_shape_slot *const *)a)->stamp, y = (*(const struct rubraview_shape_slot *const *)b)->stamp;
    return x > y ? -1 : x < y ? 1 : 0;
}

char *rubraview_shape_cache_serialize(const rubraview_shape_cache_t *cache, size_t max_entries,
                                      bool (*kept_out)(void *context, proven_u8str_view_t path), void *context, size_t *out_size) {
    *out_size = 0;
    size_t n = 0;
    const struct rubraview_shape_slot **order = cache && cache->count ? (const struct rubraview_shape_slot **)malloc(cache->count * sizeof(*order)) : NULL;
    for (size_t i = 0; order && i < cache->capacity; ++i) {
        const struct rubraview_shape_slot *s = &cache->slots[i];
        if (!s->path || s->mtime == 0 || memchr(s->path, '\n', s->len) || memchr(s->path, '\t', s->len)) continue;
        if (kept_out && kept_out(context, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s->path, .size = s->len })) continue;
        order[n++] = s;
    }
    if (n > 1) qsort(order, n, sizeof(*order), stamp_newer);
    if (n > max_entries) n = max_entries;
    size_t cap = sizeof(FACTS_HEAD) + 1;
    for (size_t i = 0; i < n; ++i) cap += order[i]->len + 96;
    char *out = (char *)malloc(cap);
    if (!out) { free(order); return NULL; }
    size_t at = (size_t)snprintf(out, cap, "%s", FACTS_HEAD);
    for (size_t i = 0; i < n; ++i) {
        const struct rubraview_shape_slot *s = order[i];
        char secs[32] = "-";
        if (s->seconds >= 0.0) snprintf(secs, sizeof(secs), "%.3f", s->seconds);
        int k = snprintf(out + at, cap - at, "%lld\t%d\t%d\t%s\t%d\t", (long long)s->mtime, (int)s->width, (int)s->height, secs, (int)s->shape);
        if (k < 0 || (size_t)k + s->len + 2 > cap - at) break;
        at += (size_t)k;
        memcpy(out + at, s->path, s->len);
        at += s->len;
        out[at++] = '\n';
    }
    free(order);
    out[at] = '\0';   /* text: ended, not counted */
    *out_size = at;
    return out;
}

void rubraview_shape_cache_free(rubraview_shape_cache_t *cache) {
    if (!cache) return;
    for (size_t i = 0; i < cache->capacity; ++i) free(cache->slots[i].path);
    free(cache->slots);
    *cache = (rubraview_shape_cache_t){0};
}
