#include "rubraview/sort.h"
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
