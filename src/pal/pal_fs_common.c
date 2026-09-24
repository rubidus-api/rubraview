/*
 * Portable logic layered on top of the filesystem PAL: filtering,
 * ordering, and locating the current file among its siblings (§3.1).
 * Compiled into both the host and the Windows build — it contains no
 * OS calls of its own, only rubraview core modules.
 */
#include "rubraview/pal/pal_fs.h"
#include "rubraview/glob.h"
#include "rubraview/path.h"
#include <string.h>

static bool u8str_eq(u8str_t a, u8str_t b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

rubraview_sibling_index_t rubraview_fs_index_siblings(proven_arena_t *arena,
                                                      const rubraview_fs_listing_t *listing,
                                                      u8str_t current_file_path,
                                                      u8str_t extension_filter,
                                                      rubraview_sort_mode_t mode,
                                                      bool ascending) {
    rubraview_sibling_index_t result = {0};
    if (!arena || !listing || listing->count == 0) return result;

    proven_result_mem_mut_t items_res = rubraview_arena_alloc_array(arena, listing->count, sizeof(rubraview_sort_item_t));
    if (!proven_is_ok(items_res.err)) return result;
    rubraview_sort_item_t *items = (rubraview_sort_item_t*)(void*)items_res.value.ptr;

    /* Keep files (not directories) whose name passes the extension
       filter, tagging each with its index in the source listing so the
       full path can be recovered after sorting. */
    size_t kept = 0;
    for (size_t i = 0; i < listing->count; ++i) {
        const rubraview_fs_entry_t *e = &listing->entries[i];
        if (e->is_directory) continue;
        if (!rubraview_glob_match_list(e->name, extension_filter)) continue;

        items[kept++] = (rubraview_sort_item_t){
            .name = e->name,
            .mtime = e->mtime,
            .ctime = e->ctime,
            .size_bytes = e->size_bytes,
            .tag = (uint64_t)i,
        };
    }
    if (kept == 0) return result;

    /* A fixed seed keeps RUBRAVIEW_SORT_RANDOM reproducible for a given
       listing; callers wanting a fresh shuffle order per session pass
       their own seed to rubraview_sort_items directly (§3.2.3). */
    rubraview_shuffle_state_t shuffle = { .seed = 0x9E3779B97F4A7C15ULL };
    rubraview_sort_items(items, kept, mode, ascending, &shuffle);

    proven_result_mem_mut_t out_res = rubraview_arena_alloc_array(arena, kept, sizeof(u8str_t));
    if (!proven_is_ok(out_res.err)) return result;
    u8str_t *ordered_paths = (u8str_t*)(void*)out_res.value.ptr;

    for (size_t i = 0; i < kept; ++i) {
        ordered_paths[i] = listing->entries[items[i].tag].path;
    }

    result.paths = ordered_paths;
    result.count = kept;
    result.current = 0;
    result.found = false;

    /* The current file is found by its name, not its whole path: the
       listing joins the folder and the name with '/', while the path the
       reader opened may be spelt with backslashes, as Explorer hands it
       over, and then no whole path would ever match. Every entry is in
       the one folder, so the name is enough. An exact match wins; only
       without one is case ignored, as Windows ignores it. */
    u8str_t wanted = rubraview_path_basename(current_file_path);
    for (int pass = 0; pass < 2 && !result.found && wanted.len > 0; ++pass) {
        for (size_t i = 0; i < kept; ++i) {
            u8str_t name = listing->entries[items[i].tag].name;
            if (pass == 0 ? u8str_eq(name, wanted) : rubraview_path_same(name, wanted)) {
                result.current = i;
                result.found = true;
                break;
            }
        }
    }

    return result;
}
