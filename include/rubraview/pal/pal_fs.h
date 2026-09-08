#ifndef RUBRAVIEW_PAL_FS_H
#define RUBRAVIEW_PAL_FS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"
#include "rubraview/sort.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Filesystem enumeration PAL (RFC-0001 §8.2, §3.1 "File & Directory
 * Traversal"). Windows uses FindFirstFileW/FindNextFileW with UTF-16 to
 * UTF-8 conversion at the OS boundary; the host build uses POSIX
 * opendir/readdir/stat. Every path and name returned honours the
 * null-terminated allocation invariant (§7.2.3): allocated as len + 1
 * bytes with ptr[len] == '\0', so it can be handed straight to an OS
 * call without copying.
 */

typedef struct rubraview_fs_entry {
    u8str_t  name;         /* basename, e.g. "page_042.webp" */
    u8str_t  path;         /* full path, e.g. "D:/Manga/Ch01/page_042.webp" */
    uint64_t size_bytes;
    int64_t  mtime;        /* modification time, seconds since the Unix epoch */
    int64_t  ctime;        /* creation time on Windows; status-change time on POSIX (the closest available) */
    bool     is_directory;
} rubraview_fs_entry_t;

typedef struct rubraview_fs_listing {
    rubraview_fs_entry_t *entries;
    size_t count;
} rubraview_fs_listing_t;

/**
 * List a directory's immediate children (excluding "." and ".."), in
 * whatever order the OS reports them — the caller orders them itself
 * (rubraview_fs_index_siblings, or rubraview_sort_items directly).
 * Returns an empty listing if the directory cannot be opened.
 */
rubraview_fs_listing_t rubraview_pal_fs_list_dir(proven_arena_t *arena, u8str_t dir_path);

/** Metadata for a single path. Returns false if it does not exist or cannot be read. */
bool rubraview_pal_fs_stat(proven_arena_t *arena, u8str_t path, rubraview_fs_entry_t *out_entry);

bool rubraview_pal_fs_exists(u8str_t path);

/**
 * Read an entire file into the arena, NUL-terminated per §7.2.3 so the
 * result can be handed straight to a text parser. Returns an empty slice
 * if the file cannot be read or exceeds `max_bytes` (a bound, since this
 * reads caller-supplied paths such as keymap.ini and settings.ini).
 */
u8str_t rubraview_pal_fs_read_file(proven_arena_t *arena, u8str_t path, size_t max_bytes);

/**
 * §3.8.3 step 3: transcode bytes from a legacy code page into UTF-8.
 * `codepage_id` is a Windows code page number, or 0 for the host's
 * active one; rubraview/encoding.h decides which applies. Returns an
 * empty slice when the platform cannot transcode (the host build has no
 * code-page tables), so the caller keeps the raw bytes rather than
 * showing nothing.
 */
u8str_t rubraview_pal_transcode_codepage(proven_arena_t *arena, u8str_t bytes, uint32_t codepage_id);

/**
 * Write a whole file, replacing it. Used for the small state files
 * §3.17 keeps (history.ini, settings.ini); returns false if the file
 * cannot be written, which in portable mode on read-only media is a
 * normal outcome rather than an error worth interrupting the reader.
 */
bool rubraview_pal_fs_write_file(u8str_t path, u8str_t contents);

/**
 * §3.1: "Automatically indexes sibling media files when an image is
 * opened." Filters a listing to files matching `extension_filter` (a
 * ';'-separated glob list from rubraview/glob.h; empty matches every
 * file), orders them by `mode`/`ascending`, and reports where
 * `current_file_path` landed in that order.
 *
 * This is portable logic built on top of the PAL rather than part of any
 * backend — it lives in src/pal/pal_fs_common.c and is compiled into
 * both the host and the Windows build.
 */
typedef struct rubraview_sibling_index {
    u8str_t *paths;  /* full paths, filtered and ordered */
    size_t   count;
    size_t   current; /* index of current_file_path within `paths`; 0 when not found */
    bool     found;   /* whether current_file_path was present after filtering */
} rubraview_sibling_index_t;

rubraview_sibling_index_t rubraview_fs_index_siblings(proven_arena_t *arena,
                                                      const rubraview_fs_listing_t *listing,
                                                      u8str_t current_file_path,
                                                      u8str_t extension_filter,
                                                      rubraview_sort_mode_t mode,
                                                      bool ascending);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_FS_H */
