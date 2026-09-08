#ifndef RUBRAVIEW_COMICINFO_H
#define RUBRAVIEW_COMICINFO_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Minimal ComicInfo.xml reader (RFC-0001 §3.8.5): a purpose-built scanner
 * for this document's known, non-recursive shape (a flat set of scalar
 * text elements plus a `<Pages>` container of self-closing `<Page/>`
 * elements) — not a general XML parser. Unrecognized elements and
 * attributes are ignored rather than rejected.
 */

typedef enum rubraview_manga_mode {
    RUBRAVIEW_MANGA_NO = 0,     /* absent, or explicit "No": Western LTR (default) */
    RUBRAVIEW_MANGA_YES,        /* "Yes": still LTR per §3.8.5 point 2 */
    RUBRAVIEW_MANGA_YES_RTL,    /* "YesAndRightToLeft": switches to Book Mode RTL */
} rubraview_manga_mode_t;

typedef struct rubraview_comicinfo_page {
    int32_t image_index; /* the Page element's Image attribute, or -1 if absent/unparseable */
    u8str_t type;         /* the raw Type attribute value (e.g. "FrontCover"), or empty if absent */
} rubraview_comicinfo_page_t;

typedef struct rubraview_comicinfo {
    rubraview_manga_mode_t manga;
    u8str_t title;
    u8str_t series;
    int32_t volume; /* -1 if absent/unparseable */
    rubraview_comicinfo_page_t *pages;
    size_t   page_count;
} rubraview_comicinfo_t;

rubraview_comicinfo_t rubraview_comicinfo_parse(proven_arena_t *arena, u8str_t xml);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_COMICINFO_H */
