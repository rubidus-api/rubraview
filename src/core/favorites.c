#include "rubraview/favorites.h"
#include "rubraview/ini.h"
#include "rubraview/path.h"
#include <stdio.h>
#include <string.h>

rubraview_favorites_t rubraview_favorites_parse(proven_arena_t *arena, u8str_t text) {
    rubraview_favorites_t favorites = {0};
    if (!arena || text.len == 0) return favorites;
    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, text);
    u8str_t sections[RUBRAVIEW_FAVORITES_MAX];
    for (size_t i = 0; i < doc.count && favorites.count < RUBRAVIEW_FAVORITES_MAX; ++i) {
        const rubraview_ini_entry_t *e = &doc.entries[i];
        if (!rubraview_u8_starts_with(e->section, "favorite-") || !rubraview_u8_eq(e->key, U8("path"))) continue;
        if (e->value.len == 0 || rubraview_favorites_contains(&favorites, e->value)) continue;
        sections[favorites.count] = e->section;
        favorites.names[favorites.count] = (u8str_t){ .ptr = "", .len = 0 };
        favorites.paths[favorites.count++] = e->value;
    }
    /* A name belongs to the path in its own section, wherever it stands in it. */
    for (size_t i = 0; i < doc.count; ++i) {
        const rubraview_ini_entry_t *e = &doc.entries[i];
        if (!rubraview_u8_eq(e->key, U8("name"))) continue;
        for (size_t f = 0; f < favorites.count; ++f) {
            if (rubraview_u8_eq(sections[f], e->section)) favorites.names[f] = e->value;
        }
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
        if (favorites->names[i].len > 0) rubraview_ini_set_string(arena, &doc, section, U8("name"), favorites->names[i]);
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
        memmove(&favorites->names[i], &favorites->names[i + 1], (favorites->count - i - 1) * sizeof(favorites->names[0]));
        favorites->count--;
        return false;
    }
    if (favorites->count >= RUBRAVIEW_FAVORITES_MAX) return false;
    favorites->names[favorites->count] = (u8str_t){ .ptr = "", .len = 0 };
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

u8str_t rubraview_favorites_title(const rubraview_favorites_t *favorites, size_t index) {
    if (!favorites || index >= favorites->count) return (u8str_t){ .ptr = "", .len = 0 };
    return favorites->names[index].len > 0 ? favorites->names[index] : rubraview_favorites_label(favorites->paths[index]);
}

bool rubraview_favorites_rename(rubraview_favorites_t *favorites, size_t index, u8str_t name) {
    if (!favorites || index >= favorites->count) return false;
    while (name.len > 0 && (name.ptr[0] == ' ' || name.ptr[0] == '\t')) { name.ptr++; name.len--; }
    while (name.len > 0 && (name.ptr[name.len - 1] == ' ' || name.ptr[name.len - 1] == '\t')) name.len--;
    /* The folder's own name, typed back, is no name of the reader's. */
    if (rubraview_u8_eq(name, rubraview_favorites_label(favorites->paths[index]))) name.len = 0;
    favorites->names[index] = name;
    return true;
}

bool rubraview_favorites_move(rubraview_favorites_t *favorites, size_t from, size_t to) {
    if (!favorites || from >= favorites->count || to >= favorites->count) return false;
    u8str_t path = favorites->paths[from], name = favorites->names[from];
    if (from < to) {
        memmove(&favorites->paths[from], &favorites->paths[from + 1], (to - from) * sizeof(path));
        memmove(&favorites->names[from], &favorites->names[from + 1], (to - from) * sizeof(name));
    } else if (from > to) {
        memmove(&favorites->paths[to + 1], &favorites->paths[to], (from - to) * sizeof(path));
        memmove(&favorites->names[to + 1], &favorites->names[to], (from - to) * sizeof(name));
    }
    favorites->paths[to] = path;
    favorites->names[to] = name;
    return true;
}
