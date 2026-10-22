#ifndef RUBRAVIEW_PLAYLIST_H
#define RUBRAVIEW_PLAYLIST_H

#include "rubraview/core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Playlist file formats (RFC-0001 §3.12 "File Formats"): native
 * `.rvlist` (plaintext, one path per line, relative to the list's folder
 * or whole — D-111; blank lines and
 * `;`-prefixed comments ignored) and standard Extended M3U `.m3u8`
 * (`#EXTINF:duration,title` optionally preceding each path). Mixed
 * still/animated/video media is inherent — any path is accepted, and
 * media kind is determined later by the caller from its extension, not
 * by this module. Bookmarks, quick-collection actions (Insert/Ctrl+D),
 * and last-viewed-index tracking are UI state (§3.12 point 3) for a
 * later milestone, not this file-format module.
 */

typedef struct rubraview_playlist_entry {
    proven_u8str_view_t path;             /* as written in the file; the caller resolves relative paths */
    double  duration_seconds; /* from EXTINF; -1 if unknown or absent (.rvlist has no duration concept) */
    proven_u8str_view_t title;            /* from EXTINF; empty if absent */
    /* RV-158: an .asx entry's other refs, in order: tried when `path` does not answer (not saved again) */
    proven_u8str_view_t *alternates;
    size_t alternate_count;
} rubraview_playlist_entry_t;

/** Fall-backs kept for one entry. */
#define RUBRAVIEW_PLAYLIST_MAX_ALTERNATES 7

typedef struct rubraview_playlist {
    rubraview_playlist_entry_t *entries;
    size_t count;
} rubraview_playlist_t;

rubraview_playlist_t rubraview_playlist_parse_rvlist(proven_arena_t *arena, proven_u8str_view_t text);
rubraview_playlist_t rubraview_playlist_parse_m3u8(proven_arena_t *arena, proven_u8str_view_t text);

/**
 * RV-157: the lists internet radio comes in, read only. `.pls` (Winamp and
 * SHOUTcast: `[playlist]`, `FileN=`, `TitleN=`, `LengthN=`, in N's order,
 * keys in any case) and `.asx` (Windows Media, where `mms://` addresses come
 * from: each `<entry>`'s first `<ref href>` with its `<title>`; `.wax` and
 * `.wvx` are the same). Entities in a title or address are decoded.
 */
rubraview_playlist_t rubraview_playlist_parse_pls(proven_arena_t *arena, proven_u8str_view_t text);
rubraview_playlist_t rubraview_playlist_parse_asx(proven_arena_t *arena, proven_u8str_view_t text);

/**
 * Any list by its name, or by its text when the name says nothing a reader
 * knows (`[playlist]` is a `.pls`, `<asx` an `.asx`): `.rvlist`, `.pls`,
 * `.asx` / `.wax` / `.wvx`, else Extended M3U. A UTF-8 byte order mark is
 * passed over.
 */
rubraview_playlist_t rubraview_playlist_parse_named(proven_arena_t *arena, proven_u8str_view_t name, proven_u8str_view_t text);

proven_u8str_view_t rubraview_playlist_serialize_rvlist(proven_arena_t *arena, const rubraview_playlist_t *pl);
proven_u8str_view_t rubraview_playlist_serialize_m3u8(proven_arena_t *arena, const rubraview_playlist_t *pl);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PLAYLIST_H */
