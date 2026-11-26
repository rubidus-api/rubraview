/* RV-227: five baskets of paths for the open-file screen (D-282). */
#include "rubraview/basket.h"
#include "rubraview/path.h"

static rubraview_basket_t *basket_at(rubraview_baskets_t *b, int which) {
    return b && which >= 0 && which < RUBRAVIEW_BASKETS ? &b->basket[which] : NULL;
}

static const rubraview_basket_t *basket_of(const rubraview_baskets_t *b, int which) {
    return b && which >= 0 && which < RUBRAVIEW_BASKETS ? &b->basket[which] : NULL;
}

void rubraview_basket_empty(rubraview_baskets_t *b, int which) {
    rubraview_basket_t *k = basket_at(b, which);
    if (!k) return;
    for (size_t i = 0; i < k->count; ++i) { rubraview_free(k->paths[i]); rubraview_free(k->entries[i]); }
    rubraview_free(k->paths);
    rubraview_free(k->entries);
    *k = (rubraview_basket_t){ 0 };
}

void rubraview_baskets_free(rubraview_baskets_t *b) {
    for (int i = 0; i < RUBRAVIEW_BASKETS; ++i) rubraview_basket_empty(b, i);
}

static bool same_entry(const char *have, proven_u8str_view_t entry) {
    if (!have) return entry.size == 0;
    size_t n = proven_cstr_len(have);
    return n == entry.size && (n == 0 || proven_sys_mem_cmp(have, entry.ptr, n) == 0);
}

/* the same path and the same entry (none for a file on disk) */
static bool basket_holds(const rubraview_basket_t *k, proven_u8str_view_t path, proven_u8str_view_t entry) {
    for (size_t i = 0; i < k->count; ++i)
        if (same_entry(k->entries[i], entry) && rubraview_path_same(proven_u8str_view_from_cstr(k->paths[i]), path)) return true;
    return false;
}

static bool basket_grow(rubraview_basket_t *k) {
    if (k->count < k->capacity) return true;
    size_t cap = k->capacity ? k->capacity * 2 : 16;
    char **grown = (char **)rubraview_realloc((proven_mem_mut_t){ .ptr = (proven_byte_t *)(void *)k->paths, .size = cap * sizeof(char *) });
    if (!grown) return false;
    k->paths = grown;
    char **more = (char **)rubraview_realloc((proven_mem_mut_t){ .ptr = (proven_byte_t *)(void *)k->entries, .size = cap * sizeof(char *) });
    if (!more) return false;   /* paths grown, capacity not: the next try grows both again */
    k->entries = more;
    k->capacity = cap;
    return true;
}

static char *copy_text(proven_u8str_view_t text) {
    char *out = (char *)rubraview_alloc(text.size + 1);
    if (!out) return NULL;
    if (text.size > 0) proven_sys_mem_copy(out, text.ptr, text.size);
    out[text.size] = '\0';
    return out;
}

static size_t add_items(rubraview_basket_t *k, const proven_u8str_view_t *paths, proven_u8str_view_t archive,
                        const proven_u8str_view_t *names, size_t n) {
    size_t added = 0;
    for (size_t i = 0; i < n; ++i) {
        proven_u8str_view_t path = names ? archive : paths[i], entry = names ? names[i] : PROVEN_LIT("");
        if (path.size == 0 || (names && entry.size == 0) || basket_holds(k, path, entry)) continue;
        if (!basket_grow(k)) break;
        char *copy = copy_text(path);
        char *name = names ? copy_text(entry) : NULL;
        if (!copy || (names && !name)) { rubraview_free(copy); rubraview_free(name); break; }
        k->paths[k->count] = copy;
        k->entries[k->count] = name;
        k->count++;
        added++;
    }
    return added;
}

size_t rubraview_basket_add(rubraview_baskets_t *b, int which, const proven_u8str_view_t *paths, size_t n) {
    rubraview_basket_t *k = basket_at(b, which);
    return k && paths ? add_items(k, paths, PROVEN_LIT(""), NULL, n) : 0;
}

size_t rubraview_basket_add_entries(rubraview_baskets_t *b, int which, proven_u8str_view_t archive, const proven_u8str_view_t *names, size_t n) {
    rubraview_basket_t *k = basket_at(b, which);
    return k && names && archive.size > 0 ? add_items(k, NULL, archive, names, n) : 0;
}

proven_u8str_view_t rubraview_basket_entry(const rubraview_baskets_t *b, int which, size_t index) {
    const rubraview_basket_t *k = basket_of(b, which);
    if (!k || index >= k->count || !k->entries[index]) return PROVEN_LIT("");
    return proven_u8str_view_from_cstr(k->entries[index]);
}

proven_u8str_view_t rubraview_basket_entry_folder(proven_u8str_view_t entry) {
    size_t n = entry.size;
    while (n > 0 && entry.ptr[n - 1] != '/') n--;
    return (proven_u8str_view_t){ .ptr = entry.ptr, .size = n };
}

size_t rubraview_basket_groups(const rubraview_baskets_t *b, int which, rubraview_basket_group_t *groups, size_t cap, size_t *members) {
    const rubraview_basket_t *k = basket_of(b, which);
    if (!k || !groups || !members || cap == 0) return 0;
    size_t ng = 0, at = 0;
    for (size_t i = 0; i < k->count && ng < cap; ++i) {   /* each archive at its first entry */
        if (!k->entries[i]) continue;
        proven_u8str_view_t arc = proven_u8str_view_from_cstr(k->paths[i]);
        bool seen = false;
        for (size_t j = 0; j < i && !seen; ++j) seen = k->entries[j] && rubraview_path_same(proven_u8str_view_from_cstr(k->paths[j]), arc);
        if (seen) continue;
        for (size_t f = i; f < k->count && ng < cap; ++f) {   /* its folders, each at its first entry */
            if (!k->entries[f] || !rubraview_path_same(proven_u8str_view_from_cstr(k->paths[f]), arc)) continue;
            proven_u8str_view_t inner = rubraview_basket_entry_folder(proven_u8str_view_from_cstr(k->entries[f]));
            bool had = false;
            for (size_t g = 0; g < ng && !had; ++g) had = rubraview_path_same(groups[g].archive, arc) && proven_u8str_view_eq(groups[g].inner, inner);
            if (had) continue;
            rubraview_basket_group_t *grp = &groups[ng++];
            *grp = (rubraview_basket_group_t){ .archive = arc, .inner = inner, .first = at };
            for (size_t m = f; m < k->count; ++m) {
                if (!k->entries[m] || !rubraview_path_same(proven_u8str_view_from_cstr(k->paths[m]), arc)) continue;
                if (!proven_u8str_view_eq(rubraview_basket_entry_folder(proven_u8str_view_from_cstr(k->entries[m])), inner)) continue;
                members[at++] = m;
                grp->count++;
            }
        }
    }
    return ng;
}

size_t rubraview_basket_count(const rubraview_baskets_t *b, int which) {
    const rubraview_basket_t *k = basket_of(b, which);
    return k ? k->count : 0;
}

proven_u8str_view_t rubraview_basket_path(const rubraview_baskets_t *b, int which, size_t index) {
    const rubraview_basket_t *k = basket_of(b, which);
    if (!k || index >= k->count) return PROVEN_LIT("");
    return proven_u8str_view_from_cstr(k->paths[index]);
}

bool rubraview_basket_remove(rubraview_baskets_t *b, int which, size_t index) {
    rubraview_basket_t *k = basket_at(b, which);
    if (!k || index >= k->count) return false;
    rubraview_free(k->paths[index]);
    rubraview_free(k->entries[index]);
    for (size_t i = index + 1; i < k->count; ++i) { k->paths[i - 1] = k->paths[i]; k->entries[i - 1] = k->entries[i]; }
    k->count--;
    return true;
}

size_t rubraview_basket_keep_unmoved(rubraview_baskets_t *b, int which, const bool *moved) {
    rubraview_basket_t *k = basket_at(b, which);
    if (!k) return 0;
    if (!moved) return k->count;
    size_t kept = 0;
    for (size_t i = 0; i < k->count; ++i) {
        if (moved[i]) { rubraview_free(k->paths[i]); rubraview_free(k->entries[i]); continue; }
        k->entries[kept] = k->entries[i];
        k->paths[kept++] = k->paths[i];
    }
    k->count = kept;
    return kept;
}
