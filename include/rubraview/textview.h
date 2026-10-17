#ifndef RUBRAVIEW_TEXTVIEW_H
#define RUBRAVIEW_TEXTVIEW_H

/*
 * RFC-0005 §3 and §5: a text file as it is shown — its line breaks taken
 * one of four ways (the owner, 2026-10-17), leading spaces kept or not —
 * and found in. The view is the file's text rebuilt as paragraphs, each
 * laid out on its own by the renderer, with the place in the file each
 * one came from (the resume place is the file's, not the view's).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rubraview_text_breaks {
    RUBRAVIEW_TEXT_BREAKS_KEEP = 0,     /* every break a break, blank lines kept */
    RUBRAVIEW_TEXT_BREAKS_ONE_LINE,     /* all of it in one run, no breaks at all */
    RUBRAVIEW_TEXT_BREAKS_NO_BLANK,     /* blank lines dropped, the other breaks kept */
    RUBRAVIEW_TEXT_BREAKS_PARAGRAPHS,   /* lines joined into paragraphs, guessed (see textview.c) */
    RUBRAVIEW_TEXT_BREAKS_COUNT
} rubraview_text_breaks_t;

typedef struct rubraview_text_para {
    size_t start;    /* in the view's text */
    size_t len;      /* without the "\n" after it */
    size_t source;   /* where it begins in the file's (cleaned) text */
    bool gap;        /* a paragraph's space above it */
    bool joined;     /* the run of the one before goes on (ONE_LINE's pieces): no "\n" before it */
} rubraview_text_para_t;

typedef struct rubraview_text_view {
    char *text;      /* the paragraphs one after another, "\n" between them unless `joined`; NUL-terminated */
    size_t len;
    rubraview_text_para_t *paras;
    size_t count;
} rubraview_text_view_t;

/** ONE_LINE's run is laid out in pieces of about this many bytes, cut after a sentence. */
#define RUBRAVIEW_TEXT_PIECE 4096

/**
 * Build the view of `source` (UTF-8, "\n" line ends: rubraview_text_clean's).
 * False when out of memory; `out` is then empty. Free with _free.
 */
bool rubraview_text_view_build(rubraview_text_view_t *out, const char *source, size_t len,
                               rubraview_text_breaks_t breaks, bool trim_lead);
void rubraview_text_view_free(rubraview_text_view_t *view);

/** The paragraph a place in the view's text is in (the last one for the end). */
size_t rubraview_text_view_para_at(const rubraview_text_view_t *view, size_t offset);
/** The paragraph that holds a place in the file's text: how a place survives a change of breaks. */
size_t rubraview_text_view_para_of_source(const rubraview_text_view_t *view, size_t source);

/** Whether a line ends a sentence (its last mark is . ! ? 。 a closing quote or the like). */
bool rubraview_text_ends_sentence(const char *line, size_t len);

/**
 * Find `needle` in the view's text from `from` on (or, `backward`, before
 * it), letters' case and full-width forms ignored. Wraps round once.
 * The match's place and length in bytes of the view's text.
 */
bool rubraview_text_find(const rubraview_text_view_t *view, u8str_t needle, size_t from, bool backward,
                         size_t *at, size_t *len);
/** How many times `needle` is in the view, and which of them begins at `at` (1-based; 0 none). */
size_t rubraview_text_count(const rubraview_text_view_t *view, u8str_t needle, size_t at, size_t *which);

/** One code point at `*i` of `s`, `*i` moved past it; U+FFFD for a bad byte. */
uint32_t rubraview_text_next_cp(const char *s, size_t len, size_t *i);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_TEXTVIEW_H */
