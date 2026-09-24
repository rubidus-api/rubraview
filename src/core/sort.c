#include "rubraview/sort.h"
#include "proven/algorithm.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>

int rubraview_str_lexcmp(u8str_t a, u8str_t b) {
    if (!a.ptr && !b.ptr) return 0;
    if (!a.ptr) return -1;
    if (!b.ptr) return 1;

    size_t min_len = a.len < b.len ? a.len : b.len;
    int tie_breaker = 0;

    for (size_t i = 0; i < min_len; ++i) {
        char ca = a.ptr[i];
        char cb = b.ptr[i];
        char la = (char)tolower((unsigned char)ca);
        char lb = (char)tolower((unsigned char)cb);

        if (la != lb) {
            return (int)(unsigned char)la - (int)(unsigned char)lb;
        }
        if (tie_breaker == 0 && ca != cb) {
            tie_breaker = (int)(unsigned char)ca - (int)(unsigned char)cb;
        }
    }

    if (a.len != b.len) {
        return a.len < b.len ? -1 : 1;
    }
    return tie_breaker;
}

int rubraview_str_natcmp(u8str_t a, u8str_t b) {
    if (!a.ptr && !b.ptr) return 0;
    if (!a.ptr) return -1;
    if (!b.ptr) return 1;

    size_t ia = 0, ib = 0;
    int tie_breaker = 0;

    while (ia < a.len && ib < b.len) {
        char ca = a.ptr[ia];
        char cb = b.ptr[ib];
        bool da = (ca >= '0' && ca <= '9');
        bool db = (cb >= '0' && cb <= '9');

        if (da && db) {
            // Contiguous digit block encountered
            // 1. Skip and count leading zeros
            size_t za = 0, zb = 0;
            while (ia < a.len && a.ptr[ia] == '0') { za++; ia++; }
            while (ib < b.len && b.ptr[ib] == '0') { zb++; ib++; }

            // 2. Measure length of significant digits
            size_t start_a = ia;
            while (ia < a.len && a.ptr[ia] >= '0' && a.ptr[ia] <= '9') ia++;
            size_t sig_a = ia - start_a;

            size_t start_b = ib;
            while (ib < b.len && b.ptr[ib] >= '0' && b.ptr[ib] <= '9') ib++;
            size_t sig_b = ib - start_b;

            // 3. Number with more significant digits is numerically greater
            if (sig_a != sig_b) {
                return sig_a < sig_b ? -1 : 1;
            }

            // 4. If same length, compare digit by digit
            for (size_t k = 0; k < sig_a; ++k) {
                char d_a = a.ptr[start_a + k];
                char d_b = b.ptr[start_b + k];
                if (d_a != d_b) {
                    return (int)(unsigned char)d_a - (int)(unsigned char)d_b;
                }
            }

            // 5. Numerical values are equal; record tie-breaker for leading zeros
            if (tie_breaker == 0 && za != zb) {
                // String with fewer leading zeros comes first: "1" < "01"
                tie_breaker = (za < zb) ? -1 : 1;
            }
        } else {
            // Compare as characters (case-insensitive primary)
            char la = (char)tolower((unsigned char)ca);
            char lb = (char)tolower((unsigned char)cb);
            if (la != lb) {
                return (int)(unsigned char)la - (int)(unsigned char)lb;
            }
            if (tie_breaker == 0 && ca != cb) {
                tie_breaker = (int)(unsigned char)ca - (int)(unsigned char)cb;
            }
            ia++;
            ib++;
        }
    }

    if (ia < a.len) return 1;
    if (ib < b.len) return -1;
    return tie_breaker;
}

static int compare_paths(u8str_t a, u8str_t b, rubraview_sort_mode_t mode, bool ascending) {
    int cmp = 0;
    switch (mode) {
        case RUBRAVIEW_SORT_NAME_LEXICAL:
            cmp = rubraview_str_lexcmp(a, b);
            break;
        case RUBRAVIEW_SORT_NAME_NATURAL:
        default:
            cmp = rubraview_str_natcmp(a, b);
            break;
    }
    return ascending ? cmp : -cmp;
}

static void quicksort_paths(u8str_t *paths, int low, int high, rubraview_sort_mode_t mode, bool ascending) {
    if (low >= high) return;

    u8str_t pivot = paths[(low + high) / 2];
    int i = low;
    int j = high;

    while (i <= j) {
        while (compare_paths(paths[i], pivot, mode, ascending) < 0) i++;
        while (compare_paths(paths[j], pivot, mode, ascending) > 0) j--;

        if (i <= j) {
            u8str_t tmp = paths[i];
            paths[i] = paths[j];
            paths[j] = tmp;
            i++;
            j--;
        }
    }

    if (low < j) quicksort_paths(paths, low, j, mode, ascending);
    if (i < high) quicksort_paths(paths, i, high, mode, ascending);
}

void rubraview_sort_paths(u8str_t *paths, size_t count, rubraview_sort_mode_t mode, bool ascending) {
    if (!paths || count <= 1) return;

    if (mode == RUBRAVIEW_SORT_RANDOM) {
        // Fisher-Yates shuffle
        for (size_t i = count - 1; i > 0; --i) {
            size_t j = (size_t)(rand() % (int)(i + 1));
            u8str_t tmp = paths[i];
            paths[i] = paths[j];
            paths[j] = tmp;
        }
        return;
    }

    quicksort_paths(paths, 0, (int)(count - 1), mode, ascending);
}

/* RV-031: multi-criteria sort over rubraview_sort_item_t. */

/* The key of one mode, ascending. */
static int compare_key(const rubraview_sort_item_t *a, const rubraview_sort_item_t *b, rubraview_sort_mode_t mode) {
    switch (mode) {
        case RUBRAVIEW_SORT_NAME_LEXICAL:
            return rubraview_str_lexcmp(a->name, b->name);
        case RUBRAVIEW_SORT_DATE_MODIFIED:
            return (a->mtime < b->mtime) ? -1 : (a->mtime > b->mtime ? 1 : 0);
        case RUBRAVIEW_SORT_DATE_CREATED:
            return (a->ctime < b->ctime) ? -1 : (a->ctime > b->ctime ? 1 : 0);
        case RUBRAVIEW_SORT_FILE_SIZE:
            return (a->size_bytes < b->size_bytes) ? -1 : (a->size_bytes > b->size_bytes ? 1 : 0);
        case RUBRAVIEW_SORT_NAME_NATURAL:
        default:
            return rubraview_str_natcmp(a->name, b->name);
    }
}

/* Every order is total: a tie on the key goes to the natural name, then
   the name's bytes, and only then the listing order (`tag`). Without that
   a folder of photos sharing one modified time opened in whatever order
   the filesystem listed it, and proven's introsort, like any fast sort,
   is not stable. Descending reverses the whole comparison except the
   last resort, so equal files still keep their listing order. */
static int compare_items(const rubraview_sort_item_t *a, const rubraview_sort_item_t *b,
                         rubraview_sort_mode_t mode, bool ascending) {
    int cmp = compare_key(a, b, mode);
    if (cmp == 0 && mode != RUBRAVIEW_SORT_NAME_NATURAL) cmp = rubraview_str_natcmp(a->name, b->name);
    if (cmp == 0 && mode != RUBRAVIEW_SORT_NAME_LEXICAL) cmp = rubraview_str_lexcmp(a->name, b->name);
    if (cmp != 0) return ascending ? cmp : -cmp;
    return (a->tag < b->tag) ? -1 : (a->tag > b->tag ? 1 : 0);
}

/* proven's comparator takes no context, so each mode and direction has
   its own; the sort runs on the main thread only. */
#define RV_SORT_CMP(fn, mode, asc) \
    static int fn(const void *a, const void *b) { \
        return compare_items((const rubraview_sort_item_t *)a, (const rubraview_sort_item_t *)b, mode, asc); \
    }
RV_SORT_CMP(cmp_natural_up, RUBRAVIEW_SORT_NAME_NATURAL, true)
RV_SORT_CMP(cmp_natural_down, RUBRAVIEW_SORT_NAME_NATURAL, false)
RV_SORT_CMP(cmp_lexical_up, RUBRAVIEW_SORT_NAME_LEXICAL, true)
RV_SORT_CMP(cmp_lexical_down, RUBRAVIEW_SORT_NAME_LEXICAL, false)
RV_SORT_CMP(cmp_mtime_up, RUBRAVIEW_SORT_DATE_MODIFIED, true)
RV_SORT_CMP(cmp_mtime_down, RUBRAVIEW_SORT_DATE_MODIFIED, false)
RV_SORT_CMP(cmp_ctime_up, RUBRAVIEW_SORT_DATE_CREATED, true)
RV_SORT_CMP(cmp_ctime_down, RUBRAVIEW_SORT_DATE_CREATED, false)
RV_SORT_CMP(cmp_size_up, RUBRAVIEW_SORT_FILE_SIZE, true)
RV_SORT_CMP(cmp_size_down, RUBRAVIEW_SORT_FILE_SIZE, false)
#undef RV_SORT_CMP

static proven_compare_fn_t comparator_for(rubraview_sort_mode_t mode, bool ascending) {
    switch (mode) {
        case RUBRAVIEW_SORT_NAME_LEXICAL: return ascending ? cmp_lexical_up : cmp_lexical_down;
        case RUBRAVIEW_SORT_DATE_MODIFIED: return ascending ? cmp_mtime_up : cmp_mtime_down;
        case RUBRAVIEW_SORT_DATE_CREATED: return ascending ? cmp_ctime_up : cmp_ctime_down;
        case RUBRAVIEW_SORT_FILE_SIZE: return ascending ? cmp_size_up : cmp_size_down;
        case RUBRAVIEW_SORT_NAME_NATURAL:
        default: return ascending ? cmp_natural_up : cmp_natural_down;
    }
}

/* splitmix64 (public domain): a small, fast, well-distributed generator,
   used here purely for its determinism (same seed -> same permutation),
   not for cryptographic strength. */
static uint64_t splitmix64_next(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void rubraview_sort_items(rubraview_sort_item_t *items, size_t count, rubraview_sort_mode_t mode, bool ascending, const rubraview_shuffle_state_t *shuffle) {
    if (!items || count <= 1) return;

    if (mode == RUBRAVIEW_SORT_RANDOM) {
        uint64_t state = shuffle ? shuffle->seed : 0;
        for (size_t i = count - 1; i > 0; --i) {
            uint64_t r = splitmix64_next(&state);
            size_t j = (size_t)(r % (uint64_t)(i + 1));
            rubraview_sort_item_t tmp = items[i];
            items[i] = items[j];
            items[j] = tmp;
        }
        return;
    }

    /* proven's introsort: O(n log n) whatever the input, where the old
       quicksort went quadratic and recursed n deep on the wrong shape.
       The array only borrows the caller's items; sorting allocates
       nothing, so it needs no allocator. */
    proven_array_t view = {
        .data = (proven_byte_t *)items,
        .len = count,
        .cap = count,
        .elem_size = sizeof(*items),
        .align = alignof(rubraview_sort_item_t),
    };
    proven_array_sort(&view, comparator_for(mode, ascending));
}
