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
proven_u8str_view_t rubraview_path_dirname(proven_u8str_view_t path);

/**
 * RV-326 (R375): a network (SMB) path's parts: "\\server" or "\\server\share\..." (either separator). Returns
 * RUBRAVIEW_UNC_NONE for any other path (a drive, a relative one, a device's "\\?\" or "\\.\"),
 * RUBRAVIEW_UNC_SERVER for the server alone, RUBRAVIEW_UNC_SHARE for a share or below it. `server` and `share`
 * (either may be NULL) are slices of `path`, `share` empty for the server alone.
 */
/**
 * RV-327 (R376): whether `name` can be a new file's or folder's name on Windows as Explorer allows it: not empty,
 * none of \ / : * ? " < > | or a control character, not "." or "..", not ending in a space or a dot, not a
 * device's name (CON, PRN, AUX, NUL, COM1-9, LPT1-9, with or without an extension). Its UTF-8 at most 255 bytes.
 */
bool rubraview_path_name_ok(proven_u8str_view_t name);

/**
 * RV-328 (R377, D-414): Windows' literal path form, as 7-Zip's file manager uses it: "\\?\C:\a\b" for a drive's,
 * "\\?\UNC\server\share\a" for a network one. Windows then takes the path as it is: past 260 characters (to
 * about 32,767), and names it otherwise changes or refuses (ending in a dot or a space, "CON", "aux.txt").
 * `rubraview_path_win_literal` writes it, NUL-ended, the separators made '\\', empty and "." parts dropped and
 * ".." taken back (Windows no longer does it then); a path already "\\?\" or "\\.\" is copied as it is. 0 for
 * a relative path or when `cap` is short.
 * `rubraview_path_win_wants_literal`: an absolute path that needs it: 248 bytes or more (a folder's limit), or a part
 * ending in a dot or a space, or a device's name.
 */
size_t rubraview_path_win_literal(proven_u8str_view_t path, char *out, size_t cap);
bool rubraview_path_win_wants_literal(proven_u8str_view_t path);

typedef enum rubraview_unc_kind { RUBRAVIEW_UNC_NONE = 0, RUBRAVIEW_UNC_SERVER, RUBRAVIEW_UNC_SHARE } rubraview_unc_kind_t;
rubraview_unc_kind_t rubraview_path_unc(proven_u8str_view_t path, proven_u8str_view_t *server, proven_u8str_view_t *share);

/**
 * Extract filename portion of path with extension (Zero-copy slice).
 * E.g. "D:/Photos/vacation/img.jpg" -> "img.jpg"
 */
proven_u8str_view_t rubraview_path_basename(proven_u8str_view_t path);

/**
 * Extract file extension including leading dot in lowercase comparison (Zero-copy slice).
 * E.g. "D:/Photos/vacation/img.jpg" -> ".jpg"
 * E.g. "archive.tar.gz" -> ".gz"
 * E.g. "README" -> "" (empty slice)
 */
proven_u8str_view_t rubraview_path_ext(proven_u8str_view_t path);

/**
 * Extract filename stem without extension (Zero-copy slice).
 * E.g. "D:/Photos/vacation/img.jpg" -> "img"
 */
proven_u8str_view_t rubraview_path_stem(proven_u8str_view_t path);

/**
 * Case-insensitive file extension match.
 * E.g. rubraview_path_has_ext(path, ".jpg") returns true for "test.JPG" or "test.jpg".
 */
bool rubraview_path_has_ext(proven_u8str_view_t path, const char *ext);

/**
 * Whether two paths name the same file the way Windows decides it:
 * '/' and '\\' are one separator and ASCII letters ignore case. The shell
 * hands over "C:\\c\\Vol 02.cbz", a directory listing joins "C:\\c" and
 * "Vol 02.cbz" with '/', and both are the same volume. Nothing is
 * resolved on disk: "a/../b" and "b" still differ.
 */
bool rubraview_path_same(proven_u8str_view_t a, proven_u8str_view_t b);

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
proven_u8str_view_t rubraview_path_safe_name(char *buf, size_t cap, proven_u8str_view_t name);

/**
 * Join directory and filename into an arena-allocated path.
 * Strictly guarantees the null-terminated allocation invariant:
 * (allocated size = len + 1, and ptr[len] == '\0').
 */
proven_u8str_view_t rubraview_path_join(proven_arena_t *arena, proven_u8str_view_t dir, proven_u8str_view_t filename);

/**
 * The file beside this one with another extension, as a whole path:
 * "D:/films/movie.idx" + ".sub" -> "D:/films/movie.sub". The folder is
 * kept, so the answer does not depend on where the program was started
 * from (a bug that cost the DVD subtitles their pictures, 2026-09-23).
 * `new_ext` carries its own dot.
 */
/**
 * D-111: `path` as seen from the folder `base`: "sub/a.jpg", "../b/a.jpg".
 * Both are whole paths; names are compared as Windows compares them
 * (either separator, letters in either case) and the result uses `/`.
 * On another drive or share there is no way from one to the other, and
 * `path` comes back as it is. NUL-terminated, in `arena`.
 */
proven_u8str_view_t rubraview_path_relative(proven_arena_t *arena, proven_u8str_view_t base, proven_u8str_view_t path);

proven_u8str_view_t rubraview_path_with_ext(proven_arena_t *arena, proven_u8str_view_t path, const char *new_ext);

/**
 * A folder as typed or pasted into the settings: spaces and quotes taken
 * off both ends, and the separators at its end ("C:\\x\\" is "C:\\x"; a
 * drive's root "C:\\" stays). A view into `typed`, not NUL-terminated.
 */
proven_u8str_view_t rubraview_path_typed_folder(proven_u8str_view_t typed);

/*
 * D-120: `name` is what a viewer writes a state file through before it
 * exchanges it for `base` — "<base>.<pid>-<tick>.tmp" (D-99). True with
 * the process's number, so a file left by a viewer that died can be told
 * from one being written now. Names, not paths; no case folding.
 */
bool rubraview_path_state_temp(proven_u8str_view_t name, proven_u8str_view_t base, uint32_t *out_pid);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PATH_H */
