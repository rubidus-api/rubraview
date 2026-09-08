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
 */
void rubraview_sort_paths(u8str_t *paths, size_t count, rubraview_sort_mode_t mode, bool ascending);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SORT_H */
