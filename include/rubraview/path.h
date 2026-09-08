#ifndef RUBRAVIEW_PATH_H
#define RUBRAVIEW_PATH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check if a character is a path separator ('/' or '\\').
 */
static inline bool rubraview_path_is_sep(char c) {
    return c == '/' || c == '\\';
}

/**
 * Extract directory portion of path without trailing separator (Zero-copy slice).
 * E.g. "D:/Photos/vacation/img.jpg" -> "D:/Photos/vacation"
 * E.g. "img.jpg" -> "" (empty slice)
 */
u8str_t rubraview_path_dirname(u8str_t path);

/**
 * Extract filename portion of path with extension (Zero-copy slice).
 * E.g. "D:/Photos/vacation/img.jpg" -> "img.jpg"
 */
u8str_t rubraview_path_basename(u8str_t path);

/**
 * Extract file extension including leading dot in lowercase comparison (Zero-copy slice).
 * E.g. "D:/Photos/vacation/img.jpg" -> ".jpg"
 * E.g. "archive.tar.gz" -> ".gz"
 * E.g. "README" -> "" (empty slice)
 */
u8str_t rubraview_path_ext(u8str_t path);

/**
 * Extract filename stem without extension (Zero-copy slice).
 * E.g. "D:/Photos/vacation/img.jpg" -> "img"
 */
u8str_t rubraview_path_stem(u8str_t path);

/**
 * Case-insensitive file extension match.
 * E.g. rubraview_path_has_ext(path, ".jpg") returns true for "test.JPG" or "test.jpg".
 */
bool rubraview_path_has_ext(u8str_t path, const char *ext);

/**
 * Join directory and filename into an arena-allocated path.
 * Strictly guarantees the null-terminated allocation invariant:
 * (allocated size = len + 1, and ptr[len] == '\0').
 */
u8str_t rubraview_path_join(proven_arena_t *arena, u8str_t dir, u8str_t filename);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PATH_H */
