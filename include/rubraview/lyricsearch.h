#ifndef RUBRAVIEW_LYRICSEARCH_H
#define RUBRAVIEW_LYRICSEARCH_H

#include "rubraview/core.h"
#include "rubraview/tags.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-323 (R369, D-411): a song's words found online, at LRCLIB (https://lrclib.net, an open lyrics database with
 * an API made for players). Pure C: what to ask (the artist and title guessed from the tags and the file's name,
 * cleaned of "feat.", "(Official Video)" and the like), the addresses to ask, the answers read (its JSON), and the
 * answer that fits the song best by its names and length. The asking itself is the platform's.
 */

#define RUBRAVIEW_LYRICS_FIELD 256
#define RUBRAVIEW_LYRICS_MAX_QUERIES 6

typedef struct rubraview_lyrics_query {
    char   artist[RUBRAVIEW_LYRICS_FIELD];
    char   title[RUBRAVIEW_LYRICS_FIELD];
    char   album[RUBRAVIEW_LYRICS_FIELD];
    double seconds;                     /* the song's length; 0 not known */
} rubraview_lyrics_query_t;

/* A title or an artist made fit to search with, in place: "(feat. X)", "[Official Video]", "(Remastered 2011)",
   "- Lyrics" and such marks dropped, "feat. X" to the end dropped, blanks made single, the ends trimmed. */
void rubraview_lyrics_clean(char *text);

/* The queries to try, best first, from the song's tags and its path (either may be empty): the tags' artist and
   title; the file name's "Artist - Title" (a leading track number dropped); the first of several artists; the
   title alone. Duplicates left out. Returns how many (at most `cap`). */
size_t rubraview_lyrics_queries(const rubraview_tags_t *tags, proven_u8str_view_t path,
                                rubraview_lyrics_query_t *out, size_t cap);

/* LRCLIB's addresses: "get" the one record of exactly this artist, title, album and length; "search" by artist
   and title (`q` empty) or by any words (`q`). Percent-encoded UTF-8; the length written; 0 when `cap` is short. */
size_t rubraview_lrclib_get_url(const rubraview_lyrics_query_t *q, char *out, size_t cap);
size_t rubraview_lrclib_search_url(const rubraview_lyrics_query_t *q, proven_u8str_view_t words, char *out, size_t cap);

typedef struct rubraview_lrclib_hit {
    int64_t             id;
    proven_u8str_view_t track, artist, album;
    double              seconds;
    bool                instrumental;
    proven_u8str_view_t plain, synced;  /* the words, and the same with [mm:ss.xx] times; either may be empty */
} rubraview_lrclib_hit_t;

/* LRCLIB's answer read: one record ("get") or an array of them ("search"); strings unescaped into `arena`.
   Returns how many records (at most `cap`); 0 for anything else. */
size_t rubraview_lrclib_parse(proven_arena_t *arena, proven_u8str_view_t json, rubraview_lrclib_hit_t *out, size_t cap);

/* How well a record fits the query, 0..1: the titles' and artists' likeness (letters and digits only, the case of
   ASCII aside) and the lengths' difference. A record without words scores 0. */
double rubraview_lyrics_score(const rubraview_lyrics_query_t *q, const rubraview_lrclib_hit_t *hit);

/* The best of `n` records for the query and its score; -1 when none has words. */
int32_t rubraview_lyrics_best(const rubraview_lyrics_query_t *q, const rubraview_lrclib_hit_t *hits, size_t n, double *score);

/* A score from which a record is taken without asking. */
#define RUBRAVIEW_LYRICS_SURE 0.72

/* The words to keep beside the song: the timed ones when there are, else the plain ones. */
proven_u8str_view_t rubraview_lyrics_words(const rubraview_lrclib_hit_t *hit);

/* ---- uploading (owner, 2026-11-27): LRCLIB's publish, no account; a proof of work for each upload ---- */

/* The words without their times: "[mm:ss.xx]" stamps and "[ar:…]"-style header lines dropped, line ends kept. Into
   `arena`; empty when nothing is left. */
proven_u8str_view_t rubraview_lyrics_plain_of(proven_arena_t *arena, proven_u8str_view_t lrc);

/* Whether the words carry times ("[mm:ss" at a line's start). */
bool rubraview_lyrics_has_times(proven_u8str_view_t lrc);

/* LRCLIB's challenge read: {"prefix":"…","target":"…"} (the target 64 hex digits). */
bool rubraview_lrclib_challenge(proven_u8str_view_t json, char prefix[128], uint8_t target[32]);

/* The proof: the smallest nonce from `from` whose SHA-256 of prefix and the nonce in decimal is at most `target`
   (big-endian); false when none in `tries` (the caller goes on from `from + tries`, so it can stop between). */
bool rubraview_lrclib_solve(const char *prefix, const uint8_t target[32], uint64_t from, uint64_t tries, uint64_t *nonce);

/* The body of an upload: trackName, artistName, albumName, duration (whole seconds), plainLyrics, syncedLyrics
   (left out when empty). JSON-escaped, into `arena`; empty without a title, an artist or a length (an album may be
   empty, written as ""). */
proven_u8str_view_t rubraview_lrclib_publish_body(proven_arena_t *arena, const rubraview_lyrics_query_t *song,
                                                  proven_u8str_view_t plain, proven_u8str_view_t synced);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_LYRICSEARCH_H */
