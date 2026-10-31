#ifndef RUBRAVIEW_PAL_FILMCACHE_H
#define RUBRAVIEW_PAL_FILMCACHE_H

#include "rubraview/core.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-216 (D-249): films on a network drive read through a copy on a local drive. A read is served
 * from the copy where the copy holds it; otherwise from the network a block at a time, and the block
 * is written to the copy and marked in its map (filmcache.h). The copy is used while the source's
 * size and modified time are what its map says. One copy is written by one process at a time; another
 * that finds it taken reads the network as before. Media Foundation reads through an IStream over the
 * stream, FFmpeg through its own I/O callbacks, in the viewer and in the FFmpeg child alike.
 */

typedef struct rubraview_filmcache_stream rubraview_filmcache_stream_t;

/* The cache's settings, in the viewer and (from each open request) in the FFmpeg child. `folder`
   empty: %LOCALAPPDATA%\rubraview\film-cache. `limit_bytes` 0: off. `ahead`: a thread fetches the
   blocks past the one playing as fast as the link gives them. */
void rubraview_pal_filmcache_configure(proven_u8str_view_t folder, uint64_t limit_bytes, bool ahead);
/* The settings as configured, for the child: the folder (UTF-8, "" for the default) into `folder`. */
void rubraview_pal_filmcache_settings(char *folder, size_t cap, uint64_t *limit_bytes, bool *ahead);
/* The folder the copies go to (the default when none was set), UTF-8. */
void rubraview_pal_filmcache_folder(char *out, size_t cap);

/* A stream over `path` through its copy; NULL when the cache is off, the file is not on a network
   drive or is smaller than RUBRAVIEW_FILMCACHE_FLOOR, or anything fails: then read the file as before. */
rubraview_filmcache_stream_t *rubraview_pal_filmcache_open(const wchar_t *path);
uint64_t rubraview_pal_filmcache_size(const rubraview_filmcache_stream_t *stream);
/* Up to `len` bytes at `at`; `*got` fewer only at the end. False on a read that failed. */
bool rubraview_pal_filmcache_read(rubraview_filmcache_stream_t *stream, uint64_t at, void *dst, uint32_t len, uint32_t *got);
void rubraview_pal_filmcache_close(rubraview_filmcache_stream_t *stream);
/* An IStream (as void *) over a new reference to `stream`, its own position; the stream is closed
   with the last reference. NULL on failure. */
void *rubraview_pal_filmcache_istream(rubraview_filmcache_stream_t *stream);

typedef struct rubraview_filmcache_stats {
    uint64_t from_copy, from_source;   /* bytes served since the program started */
    uint64_t filled, blocks;           /* the copy last opened: its blocks filled, of how many */
    bool     open;                     /* a film is read through the cache now */
} rubraview_filmcache_stats_t;
/* This process's, its FFmpeg children's added (in the viewer). */
void rubraview_pal_filmcache_stats(rubraview_filmcache_stats_t *out);
/* This process's alone (the child puts it where the viewer reads it). */
void rubraview_pal_filmcache_own_stats(rubraview_filmcache_stats_t *out);

/* How many copies there are, and the room they take (`*bytes`). */
size_t rubraview_pal_filmcache_usage(uint64_t *bytes);
/* Every copy not open now deleted; how many bytes went. */
uint64_t rubraview_pal_filmcache_clear(void);
/* The copy of `path` deleted, unless it is open (Recent's "forget"). */
void rubraview_pal_filmcache_forget(proven_u8str_view_t path);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_FILMCACHE_H */
