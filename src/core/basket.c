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
    for (size_t i = 0; i < k->count; ++i) rubraview_free(k->paths[i]);
    rubraview_free(k->paths);
    *k = (rubraview_basket_t){ 0 };
}

void rubraview_baskets_free(rubraview_baskets_t *b) {
    for (int i = 0; i < RUBRAVIEW_BASKETS; ++i) rubraview_basket_empty(b, i);
}

static bool basket_holds(const rubraview_basket_t *k, proven_u8str_view_t path) {
    for (size_t i = 0; i < k->count; ++i)
        if (rubraview_path_same(proven_u8str_view_from_cstr(k->paths[i]), path)) return true;
    return false;
}

static char *copy_text(proven_u8str_view_t text) {
    char *out = (char *)rubraview_alloc(text.size + 1);
    if (!out) return NULL;
    if (text.size > 0) proven_sys_mem_copy(out, text.ptr, text.size);
    out[text.size] = '\0';
    return out;
}

size_t rubraview_basket_add(rubraview_baskets_t *b, int which, const proven_u8str_view_t *paths, size_t n) {
    rubraview_basket_t *k = basket_at(b, which);
    if (!k || !paths) return 0;
    size_t added = 0;
    for (size_t i = 0; i < n; ++i) {
        if (paths[i].size == 0 || basket_holds(k, paths[i])) continue;
        if (k->count == k->capacity) {
            size_t cap = k->capacity ? k->capacity * 2 : 16;
            char **grown = (char **)rubraview_realloc((proven_mem_mut_t){ .ptr = (proven_byte_t *)(void *)k->paths, .size = cap * sizeof(char *) });
            if (!grown) break;
            k->paths = grown;
            k->capacity = cap;
        }
        char *copy = copy_text(paths[i]);
        if (!copy) break;
        k->paths[k->count++] = copy;
        added++;
    }
    return added;
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
    for (size_t i = index + 1; i < k->count; ++i) k->paths[i - 1] = k->paths[i];
    k->count--;
    return true;
}

size_t rubraview_basket_keep_unmoved(rubraview_baskets_t *b, int which, const bool *moved) {
    rubraview_basket_t *k = basket_at(b, which);
    if (!k) return 0;
    if (!moved) return k->count;
    size_t kept = 0;
    for (size_t i = 0; i < k->count; ++i) {
        if (moved[i]) { rubraview_free(k->paths[i]); continue; }
        k->paths[kept++] = k->paths[i];
    }
    k->count = kept;
    return kept;
}
