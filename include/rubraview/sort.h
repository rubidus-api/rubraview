#ifndef RUBRAVIEW_SORT_H
#define RUBRAVIEW_SORT_H

#include "rubraview/core.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rubraview_sort_mode {
    RUBRAVIEW_SORT_NAME_NATURAL = 0, /* Windows logical / natural numeric: (1) < (2) < (10) < (100) */
    RUBRAVIEW_SORT_NAME_LEXICAL,     /* Strict lexicographical / ordinal ASCII: (1) < (10) < (2) */
    RUBRAVIEW_SORT_DATE_MODIFIED,    /* Timestamp modified */
    RUBRAVIEW_SORT_DATE_CREATED,     /* Timestamp created */
    RUBRAVIEW_SORT_FILE_SIZE,        /* Byte size */
    RUBRAVIEW_SORT_RANDOM,           /* Fisher-Yates shuffle */
} rubraview_sort_mode_t;

/**
 * Strict lexicographical comparison between two UTF-8 slices.
 * Case-insensitive primary, byte-exact tie-breaker.
 */
int rubraview_str_lexcmp(u8str_t a, u8str_t b);

/**
 * Natural alphanumeric comparison between two UTF-8 slices.
 * Contiguous digit sequences are evaluated as single numerical values:
 * e.g. "img (1).jpg" < "img (2).jpg" < "img (9).jpg" < "img (10).jpg" < "img (99).jpg" < "img (100).jpg".
 * Handles arbitrary digit length without 64-bit integer overflow.
 */
int rubraview_str_natcmp(u8str_t a, u8str_t b);

/**
 * Sort an array of u8str_t paths/names in-place according to the chosen mode.
 * RUBRAVIEW_SORT_RANDOM here uses the C library's global rand() (a simple,
 * one-shot shuffle for callers who don't need reproducible history); for
 * date, size, or a reproducible seeded shuffle, use rubraview_sort_items.
 */
void rubraview_sort_paths(u8str_t *paths, size_t count, rubraview_sort_mode_t mode, bool ascending);

/**
 * RV-031: the full multi-criteria sort (§3.2.3), carrying the metadata
 * that name-only sorting cannot: modification/creation time and file
 * size. The caller populates these from its own directory listing (this
 * module has no filesystem access).
 */
typedef struct rubraview_sort_item {
    u8str_t  name;
    int64_t  mtime;      /* used by RUBRAVIEW_SORT_DATE_MODIFIED */
    int64_t  ctime;      /* used by RUBRAVIEW_SORT_DATE_CREATED */
    uint64_t size_bytes; /* used by RUBRAVIEW_SORT_FILE_SIZE */
} rubraview_sort_item_t;

/**
 * A shuffle seed, held by the caller across a slideshow session so that
 * `Previous` can step back through the shuffled order without
 * re-randomizing (§3.2.3 point 5: "seed preservation"). The same seed and
 * item count always reproduce the same permutation.
 */
typedef struct rubraview_shuffle_state {
    uint64_t seed;
} rubraview_shuffle_state_t;

/**
 * Sort an array of items in-place. For RUBRAVIEW_SORT_RANDOM, `shuffle`
 * drives a deterministic Fisher-Yates permutation (seed 0 if `shuffle` is
 * NULL) instead of the global rand() — `ascending` is ignored in that
 * mode. For every other mode, `shuffle` is ignored.
 */
void rubraview_sort_items(rubraview_sort_item_t *items, size_t count, rubraview_sort_mode_t mode, bool ascending, const rubraview_shuffle_state_t *shuffle);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SORT_H */
