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
    u8str_t path;      /* the archive or folder this position belongs to */
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
rubraview_history_t rubraview_history_parse(proven_arena_t *arena, u8str_t text);

/** The recorded position for a path, or NULL if it has not been read before. */
const rubraview_history_entry_t *rubraview_history_find(const rubraview_history_t *history, u8str_t path);

/**
 * Record where the reader stopped. An existing entry for the same path
 * is updated in place, so the file does not grow without bound as one
 * volume is reopened.
 */
void rubraview_history_record(proven_arena_t *arena, rubraview_history_t *history,
                              u8str_t path, int32_t page, int32_t total, int64_t timestamp);

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
u8str_t rubraview_history_serialize(proven_arena_t *arena, const rubraview_history_t *history);

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
} rubraview_recent_t;

rubraview_recent_t rubraview_recent_parse(proven_arena_t *arena, u8str_t text);
u8str_t rubraview_recent_serialize(proven_arena_t *arena, const rubraview_recent_t *recent);

/** `path` was opened at `timestamp`: it becomes the newest; at most `max_entries` are kept. */
void rubraview_recent_add(proven_arena_t *arena, rubraview_recent_t *recent, u8str_t path,
                          int64_t timestamp, size_t max_entries);

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
bool rubraview_path_is_within(u8str_t path, u8str_t folder);

/** Whether `path` lies in any of `folders` (empty ones are skipped). */
bool rubraview_recent_is_kept_out(u8str_t path, const u8str_t *folders, size_t count);

/** D-104: the entries lying in any of `folders` are taken out. Returns how many went. */
size_t rubraview_history_drop_within(rubraview_history_t *history, const u8str_t *folders, size_t count);

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
u8str_t rubraview_config_path(proven_arena_t *arena,
                              rubraview_config_mode_t mode,
                              u8str_t executable_dir,
                              u8str_t appdata_dir,
                              u8str_t file_name);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_HISTORY_H */
