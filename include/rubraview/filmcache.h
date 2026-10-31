#ifndef RUBRAVIEW_FILMCACHE_H
#define RUBRAVIEW_FILMCACHE_H

#include "rubraview/core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-216 (owner, 2026-10-31: "적어도 한번 완전히 재생된 파일이라면 그냥 부드럽게 캐쉬된 데이터가 재생이
 * 되어야 할텐데"): a film on a network drive is kept on a local drive as it is read. The copy is the
 * file's bytes at their own offsets (`<name>.bin`); its map (`<name>.map`) says which blocks of it are
 * filled, for which source (its path, size and modified time). A map that does not parse, or names
 * another source, another size or another time, is no map: the copy is started again.
 *
 * The map's text:
 *   rubraview film cache 1
 *   path <the source's path, UTF-8>
 *   size <bytes>
 *   time <the source's modified time, as the platform gives it>
 *   block <bytes a block>
 *   blocks <a hex digit for four blocks, the first block in the digit's lowest bit>
 */

#define RUBRAVIEW_FILMCACHE_BLOCK  (256u * 1024u)    /* bytes a block */
#define RUBRAVIEW_FILMCACHE_FLOOR  (16u << 20)       /* a smaller file is not copied */

typedef struct rubraview_filmcache_map {
    uint64_t size;        /* the source's bytes */
    int64_t  time;        /* its modified time */
    uint64_t blocks;      /* how many blocks the size takes */
    uint64_t filled;      /* how many are in the copy */
    uint8_t *bits;        /* a bit a block (malloc'd) */
} rubraview_filmcache_map_t;

/* The copy's name for a path: 16 hex digits of a hash of the path, its
   letters' case and its slashes made alike. `out` holds 17 bytes. */
void rubraview_filmcache_name(char out[17], proven_u8str_view_t path);

/* An empty map for a source of `size` bytes modified at `time`. */
bool rubraview_filmcache_map_make(rubraview_filmcache_map_t *map, uint64_t size, int64_t time);
void rubraview_filmcache_map_free(rubraview_filmcache_map_t *map);

bool rubraview_filmcache_map_has(const rubraview_filmcache_map_t *map, uint64_t block);
void rubraview_filmcache_map_mark(rubraview_filmcache_map_t *map, uint64_t block);
/* Whether every block holding [at, at + len) is filled (len 0: true). */
bool rubraview_filmcache_map_holds(const rubraview_filmcache_map_t *map, uint64_t at, uint64_t len);
/* The first block from `from` on that is not filled; `blocks` when none. */
uint64_t rubraview_filmcache_map_next_gap(const rubraview_filmcache_map_t *map, uint64_t from);

/* The map's text into `buf` (ended with a NUL); its length, or 0 when
   `cap` is too small. rubraview_filmcache_map_text_size: the room it needs. */
size_t rubraview_filmcache_map_text_size(const rubraview_filmcache_map_t *map, proven_u8str_view_t path);
size_t rubraview_filmcache_map_text(const rubraview_filmcache_map_t *map, proven_u8str_view_t path, char *buf, size_t cap);

/* The map read from `text`, when it is of `path` (as rubraview_path_same
   compares), `size` and `time`; false otherwise, and `map` left empty. */
bool rubraview_filmcache_map_parse(rubraview_filmcache_map_t *map, const char *text, size_t len,
                                   proven_u8str_view_t path, uint64_t size, int64_t time);

/* A copy in the cache's folder, for the limit. */
typedef struct rubraview_filmcache_entry {
    uint64_t bytes;       /* the copy's size on the disk */
    int64_t  used;        /* when it was last played (any order that grows with time) */
    bool     open;        /* playing now: never taken */
} rubraview_filmcache_entry_t;

/* Which copies go so that the rest and `incoming` more bytes fit in
   `limit`: the least recently played first, never one open. Their indexes
   into `victims` (at most `cap`); how many. Nothing goes when everything
   already fits. */
size_t rubraview_filmcache_evict(const rubraview_filmcache_entry_t *entries, size_t count, uint64_t limit,
                                 uint64_t incoming, size_t *victims, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_FILMCACHE_H */
