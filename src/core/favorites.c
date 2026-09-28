#include "rubraview/favorites.h"
#include "rubraview/ini.h"
#include "rubraview/path.h"
#include <stdio.h>
#include <string.h>

rubraview_favorites_t rubraview_favorites_parse(proven_arena_t *arena, u8str_t text) {
    rubraview_favorites_t favorites = {0};
    if (!arena || text.len == 0) return favorites;
    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, text);
    for (size_t i = 0; i < doc.count && favorites.count < RUBRAVIEW_FAVORITES_MAX; ++i) {
        const rubraview_ini_entry_t *e = &doc.entries[i];
        if (!rubraview_u8_starts_with(e->section, "favorite-") || !rubraview_u8_eq(e->key, U8("path"))) continue;
        if (e->value.len == 0 || rubraview_favorites_contains(&favorites, e->value)) continue;
        favorites.paths[favorites.count++] = e->value;
    }
    return favorites;
}

u8str_t rubraview_favorites_serialize(proven_arena_t *arena, const rubraview_favorites_t *favorites) {
    if (!arena || !favorites) return (u8str_t){ .ptr = "", .len = 0 };
    /* Through the configuration writer, so the file is in the subset by construction. */
    rubraview_ini_doc_t doc = {0};
    for (size_t i = 0; i < favorites->count; ++i) {
        char name[32];
        int n = snprintf(name, sizeof(name), "favorite-%zu", i + 1);
        if (n <= 0) continue;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)n + 1);
        if (!proven_is_ok(res.err)) break;
        memcpy(res.value.ptr, name, (size_t)n + 1);
        u8str_t section = { .ptr = (const char*)res.value.ptr, .len = (size_t)n };
        rubraview_ini_set_string(arena, &doc, section, U8("path"), favorites->paths[i]);
    }
    return rubraview_ini_serialize(arena, &doc);
}

bool rubraview_favorites_contains(const rubraview_favorites_t *favorites, u8str_t path) {
    if (!favorites || path.len == 0) return false;
    for (size_t i = 0; i < favorites->count; ++i) {
        if (rubraview_path_same(favorites->paths[i], path)) return true;
    }
    return false;
}

bool rubraview_favorites_toggle(proven_arena_t *arena, rubraview_favorites_t *favorites, u8str_t path) {
    (void)arena;
    if (!favorites || path.len == 0 || rubraview_u8_starts_with(path, "::")) return false;
    for (size_t i = 0; i < favorites->count; ++i) {
        if (!rubraview_path_same(favorites->paths[i], path)) continue;
        memmove(&favorites->paths[i], &favorites->paths[i + 1], (favorites->count - i - 1) * sizeof(favorites->paths[0]));
        favorites->count--;
        return false;
    }
    if (favorites->count >= RUBRAVIEW_FAVORITES_MAX) return false;
    favorites->paths[favorites->count++] = path;
    return true;
}

u8str_t rubraview_favorites_label(u8str_t path) {
    size_t len = path.len;
    while (len > 1 && rubraview_path_is_sep(path.ptr[len - 1])) --len;
    u8str_t trimmed = { .ptr = path.ptr, .len = len };
    u8str_t name = rubraview_path_basename(trimmed);
    return name.len > 0 ? name : trimmed;
}
