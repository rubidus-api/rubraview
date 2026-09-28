#ifndef RUBRAVIEW_FAVORITES_H
#define RUBRAVIEW_FAVORITES_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The picker's own favourites (D-43; owner, 2026-09-28): folders the
 * reader starred, shown first on the places bar and kept in
 * favorites.ini beside history.ini, one `[favorite-N]` section with a
 * quoted `path` each (D-13's INI and TOML subset — a path cannot be a
 * key). In the order they were added.
 */

#define RUBRAVIEW_FAVORITES_MAX 32

typedef struct rubraview_favorites {
    u8str_t paths[RUBRAVIEW_FAVORITES_MAX];
    size_t count;
} rubraview_favorites_t;

rubraview_favorites_t rubraview_favorites_parse(proven_arena_t *arena, u8str_t text);
u8str_t rubraview_favorites_serialize(proven_arena_t *arena, const rubraview_favorites_t *favorites);

/* Whether the folder is starred, compared as Windows compares paths. */
bool rubraview_favorites_contains(const rubraview_favorites_t *favorites, u8str_t path);

/*
 * The star pressed on `path`: added at the end, or removed if it was
 * there. True when it is a favourite afterwards. The PC page, an empty
 * path and a full list are refused. `path` is kept as given: it must
 * outlive the list (an arena slice).
 */
bool rubraview_favorites_toggle(proven_arena_t *arena, rubraview_favorites_t *favorites, u8str_t path);

/* What the bar shows: the folder's own name, or a drive's letter. A slice of `path`. */
u8str_t rubraview_favorites_label(u8str_t path);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_FAVORITES_H */
