#ifndef RUBRAVIEW_GLOB_H
#define RUBRAVIEW_GLOB_H

#include "rubraview/core.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shared glob matcher used by RV-032 (slide-show extension filter, §3.2.4)
 * and RV-033 (batch include/exclude patterns, §3.11). Case-insensitive.
 * '*' matches any run of characters (including none); '?' matches exactly
 * one character.
 */
bool rubraview_glob_match(u8str_t name, u8str_t pattern);

/**
 * Match against a ';'-separated list of patterns (e.g. "*.jpg;*.png").
 * An empty list matches everything (no filter configured).
 */
bool rubraview_glob_match_list(u8str_t name, u8str_t pattern_list);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_GLOB_H */
