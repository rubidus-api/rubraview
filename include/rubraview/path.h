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
 * Whether two paths name the same file the way Windows decides it:
 * '/' and '\\' are one separator and ASCII letters ignore case. The shell
 * hands over "C:\\c\\Vol 02.cbz", a directory listing joins "C:\\c" and
 * "Vol 02.cbz" with '/', and both are the same volume. Nothing is
 * resolved on disk: "a/../b" and "b" still differ.
 */
bool rubraview_path_same(u8str_t a, u8str_t b);

/**
 * A file name another program handed over (a browser's dragged picture, a
 * mail attachment — §3.19.2), made safe to create in a folder of ours:
 * only its last part is kept (no "..\\" or "C:\\" walks out), the
 * characters Windows refuses (< > : " / \\ | ? * and control bytes) become
 * '_', trailing dots and spaces go, a reserved device name (CON, NUL,
 * COM1...) gets a '_' in front, and it is cut to fit `cap` - 1 bytes on a
 * UTF-8 boundary, keeping its extension when it can. An empty result is
 * "dropped". Written into `buf`, NUL-terminated; the returned view is it.
 */
u8str_t rubraview_path_safe_name(char *buf, size_t cap, u8str_t name);

/**
 * Join directory and filename into an arena-allocated path.
 * Strictly guarantees the null-terminated allocation invariant:
 * (allocated size = len + 1, and ptr[len] == '\0').
 */
u8str_t rubraview_path_join(proven_arena_t *arena, u8str_t dir, u8str_t filename);

/**
 * The file beside this one with another extension, as a whole path:
 * "D:/films/movie.idx" + ".sub" -> "D:/films/movie.sub". The folder is
 * kept, so the answer does not depend on where the program was started
 * from (a bug that cost the DVD subtitles their pictures, 2026-09-23).
 * `new_ext` carries its own dot.
 */
u8str_t rubraview_path_with_ext(proven_arena_t *arena, u8str_t path, const char *new_ext);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PATH_H */
