#ifndef RUBRAVIEW_PLAYLIST_H
#define RUBRAVIEW_PLAYLIST_H

#include "rubraview/core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Playlist file formats (RFC-0001 §3.12 "File Formats"): native
 * `.rvlist` (plaintext, one relative path per line; blank lines and
 * `;`-prefixed comments ignored) and standard Extended M3U `.m3u8`
 * (`#EXTINF:duration,title` optionally preceding each path). Mixed
 * still/animated/video media is inherent — any path is accepted, and
 * media kind is determined later by the caller from its extension, not
 * by this module. Bookmarks, quick-collection actions (Insert/Ctrl+D),
 * and last-viewed-index tracking are UI state (§3.12 point 3) for a
 * later milestone, not this file-format module.
 */

typedef struct rubraview_playlist_entry {
    u8str_t path;             /* as written in the file; the caller resolves relative paths */
    double  duration_seconds; /* from EXTINF; -1 if unknown or absent (.rvlist has no duration concept) */
    u8str_t title;            /* from EXTINF; empty if absent */
} rubraview_playlist_entry_t;

typedef struct rubraview_playlist {
    rubraview_playlist_entry_t *entries;
    size_t count;
} rubraview_playlist_t;

rubraview_playlist_t rubraview_playlist_parse_rvlist(proven_arena_t *arena, u8str_t text);
rubraview_playlist_t rubraview_playlist_parse_m3u8(proven_arena_t *arena, u8str_t text);

u8str_t rubraview_playlist_serialize_rvlist(proven_arena_t *arena, const rubraview_playlist_t *pl);
u8str_t rubraview_playlist_serialize_m3u8(proven_arena_t *arena, const rubraview_playlist_t *pl);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PLAYLIST_H */
