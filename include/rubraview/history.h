#ifndef RUBRAVIEW_HISTORY_H
#define RUBRAVIEW_HISTORY_H

#include "rubraview/core.h"
#include "rubraview/ini.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reading session persistence and the configuration hierarchy
 * (RFC-0001 §3.17). Two things live here, both pure:
 *
 *  - the per-archive last-read position, in the INI shape §3.17.1 shows
 *    (`path = page:58, total:192, time:1725792000`), so reopening a long
 *    volume offers to resume where the reader stopped;
 *  - the §3.17.2 storage hierarchy: a `settings.ini` sitting beside the
 *    executable means Portable Mode, and nothing is written to the host
 *    machine; otherwise state goes to the per-user application data
 *    directory.
 *
 * Reading and writing the files themselves is the caller's job through
 * the filesystem PAL; this module only parses, formats and decides.
 */

typedef struct rubraview_history_entry {
    proven_u8str_view_t path;      /* the archive or folder this position belongs to */
    int32_t page;      /* zero-based page index */
    int32_t total;     /* pages in that archive when it was recorded */
    int64_t timestamp; /* Unix seconds, for pruning the oldest entries */
} rubraview_history_entry_t;

typedef struct rubraview_history {
    rubraview_history_entry_t *entries;
    size_t count;
    size_t capacity;
} rubraview_history_t;

/** Parse `history.ini`. An unparseable line is skipped, not fatal. */
rubraview_history_t rubraview_history_parse(proven_arena_t *arena, proven_u8str_view_t text);

/** The recorded position for a path, or NULL if it has not been read before. */
const rubraview_history_entry_t *rubraview_history_find(const rubraview_history_t *history, proven_u8str_view_t path);

/**
 * Record where the reader stopped. An existing entry for the same path
 * is updated in place, so the file does not grow without bound as one
 * volume is reopened.
 */
void rubraview_history_record(proven_arena_t *arena, rubraview_history_t *history,
                              proven_u8str_view_t path, int32_t page, int32_t total, int64_t timestamp);

/**
 * Drop the oldest entries until at most `max_entries` remain, so a
 * history that has been accumulating for years stays bounded.
 */
void rubraview_history_prune(rubraview_history_t *history, size_t max_entries);
/**
 * D-99: several viewers may be open at once. `from`'s entries are put into
 * `into`: a book in both keeps the newer of its two places, a book only
 * in `from` is added.
 */
void rubraview_history_merge(proven_arena_t *arena, rubraview_history_t *into, const rubraview_history_t *from);


/** Serialize back to `history.ini` text, newest entry last. */
proven_u8str_view_t rubraview_history_serialize(proven_arena_t *arena, const rubraview_history_t *history);

/**
 * §3.17.1: whether reopening this path should offer to resume. A
 * position at the very first page, or one at or past the end, is not
 * worth a prompt.
 */
bool rubraview_history_should_offer_resume(const rubraview_history_entry_t *entry);

/* ---- the files opened lately (D-104) ---- */

/**
 * What File > Recent lists: the paths the reader opened, each with the
 * time it was last opened, kept in `recent.ini` beside the history. It is
 * a list of its own: the reading history is keyed by the folder a picture
 * is in, and holds a book only once a page of it was read.
 *
 * `cleared` is when the list was last emptied. Several viewers may be
 * open: one that still holds older entries must not bring them back when
 * it writes the file, so an entry no newer than `cleared` is dropped
 * wherever two lists are put together.
 */
typedef struct rubraview_recent {
    rubraview_history_t list;   /* `page` and `total` are not used */
    int64_t cleared;            /* Unix seconds; 0: never */
    /* The owner (2026-10-17): one file taken off the list, and when: kept so
       that another viewer's older copy does not bring it back. An entry no
       newer than its removal is dropped wherever lists are put together. */
    rubraview_history_t removed;
} rubraview_recent_t;

rubraview_recent_t rubraview_recent_parse(proven_arena_t *arena, proven_u8str_view_t text);

/**
 * RFC-0006: the list moved whole into `arena` — its entries and every path —
 * so what it was built from (an older list, a file read) can be given back.
 * False, with the list as it was, when the arena cannot hold it.
 */
bool rubraview_recent_rehome(proven_arena_t *arena, rubraview_recent_t *recent);
proven_u8str_view_t rubraview_recent_serialize(proven_arena_t *arena, const rubraview_recent_t *recent);

/** `path` was opened at `timestamp`: it becomes the newest; at most `max_entries` are kept. */
void rubraview_recent_add(proven_arena_t *arena, rubraview_recent_t *recent, proven_u8str_view_t path,
                          int64_t timestamp, size_t max_entries);

/** Take `path` off the list at `timestamp` (the file itself is not touched). */
void rubraview_recent_remove(proven_arena_t *arena, rubraview_recent_t *recent, proven_u8str_view_t path, int64_t timestamp);

/** Empty the list, at `timestamp`. */
void rubraview_recent_clear(rubraview_recent_t *recent, int64_t timestamp);

/** `from` is put into `into`: the later clearing counts, and what it cleared goes from both. */
void rubraview_recent_merge(proven_arena_t *arena, rubraview_recent_t *into, const rubraview_recent_t *from,
                            size_t max_entries);

/**
 * The entries' indices, newest first, at most `capacity` of them.
 * Returns how many were written.
 */
size_t rubraview_recent_newest(const rubraview_recent_t *recent, size_t *out_indices, size_t capacity);

/**
 * Whether `path` is `folder` itself or lies anywhere under it, compared
 * as `rubraview_path_same` compares. "C:/a" does not hold "C:/ab/x.jpg".
 * An empty folder holds nothing.
 */
bool rubraview_path_is_within(proven_u8str_view_t path, proven_u8str_view_t folder);

/** Whether `path` lies in any of `folders` (empty ones are skipped). */
bool rubraview_recent_is_kept_out(proven_u8str_view_t path, const proven_u8str_view_t *folders, size_t count);

/**
 * 1.20.0 (owner, 2026-10-18: "그 폴더와 그 폴더의 하위 폴더 썸네일만 안 보이게"): is a tile's
 * thumbnail kept out by `thumbs.skip_dirs`? Only a folder's own tile is — one of those folders, or
 * a folder inside one — as it is seen from the folder above it. The files in them, and an
 * archive, show theirs.
 */
bool rubraview_thumb_folder_kept_out(proven_u8str_view_t path, bool is_directory, const proven_u8str_view_t *folders, size_t count);

/*
 * The folders kept out of Recent, as many as wanted (owner, 2026-10-16):
 * one setting, the folders joined by '|', a character no Windows path holds.
 */

/** The folders in `joined`, in order, empty pieces skipped; returns how many (at most `capacity`). */
size_t rubraview_folder_list_split(proven_u8str_view_t joined, proven_u8str_view_t *out, size_t capacity);

/**
 * `joined` with `folder` added at the end, or put in place of the one at
 * `replace` (0-based; past the end adds). A folder already there is not
 * added twice. Written into `buffer`; the result is empty when it does not fit.
 */
proven_u8str_view_t rubraview_folder_list_put(char *buffer, size_t capacity, proven_u8str_view_t joined, proven_u8str_view_t folder, size_t replace);

/** `joined` without the folder at `index` (0-based), written into `buffer`. */
proven_u8str_view_t rubraview_folder_list_remove(char *buffer, size_t capacity, proven_u8str_view_t joined, size_t index);

/** D-104: the entries lying in any of `folders` are taken out. Returns how many went. */
size_t rubraview_history_drop_within(rubraview_history_t *history, const proven_u8str_view_t *folders, size_t count);

/* ---- §3.17.2 configuration hierarchy ---- */

typedef enum rubraview_config_mode {
    RUBRAVIEW_CONFIG_PORTABLE = 0, /* settings.ini sits beside the executable */
    RUBRAVIEW_CONFIG_APPDATA,      /* per-user application data directory */
} rubraview_config_mode_t;

/**
 * Decide where configuration lives. `portable_probe_exists` is whether a
 * `settings.ini` was found next to the executable — the single fact
 * §3.17.2 keys the decision on. Portable mode writes nothing to the host
 * machine.
 */
rubraview_config_mode_t rubraview_config_mode_for(bool portable_probe_exists);

/**
 * Build the full path of a configuration file under the chosen mode:
 * the executable's own directory in portable mode, or
 * `<appdata>/rubraview/` otherwise.
 */
proven_u8str_view_t rubraview_config_path(proven_arena_t *arena,
                              rubraview_config_mode_t mode,
                              proven_u8str_view_t executable_dir,
                              proven_u8str_view_t appdata_dir,
                              proven_u8str_view_t file_name);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_HISTORY_H */
