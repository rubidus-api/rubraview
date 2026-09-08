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

/** Serialize back to `history.ini` text, newest entry last. */
u8str_t rubraview_history_serialize(proven_arena_t *arena, const rubraview_history_t *history);

/**
 * §3.17.1: whether reopening this path should offer to resume. A
 * position at the very first page, or one at or past the end, is not
 * worth a prompt.
 */
bool rubraview_history_should_offer_resume(const rubraview_history_entry_t *entry);

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
