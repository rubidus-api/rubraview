#ifndef RUBRAVIEW_FFILTER_H
#define RUBRAVIEW_FFILTER_H

/*
 * RFC-0009 D (owner, 2026-10-22: "남은 필터들도 구현해 주시고"): FFmpeg's video filters, run by the user's own
 * avfilter DLL inside the program, as FFmpeg's decoders already are (D-8). The PAL lists the filters and their
 * options (AVOptions) into these shapes; this is the core's part: the graph a filter's rows make, the names a
 * graph may not use, and the filters grouped for the Edit window's rows.
 */

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Options a filter shows; the rest keep FFmpeg's defaults. */
#define RUBRAVIEW_FFILTER_MAX_SHOWN 32
/** Choices an option shows. */
#define RUBRAVIEW_FFILTER_MAX_CHOICES 64
/** Groups the list is cut into, at most. */
#define RUBRAVIEW_FFILTER_MAX_GROUPS 32

typedef enum rubraview_ffilter_kind {
    RUBRAVIEW_FFILTER_INT = 0,
    RUBRAVIEW_FFILTER_FLOAT,
    RUBRAVIEW_FFILTER_BOOL,
    RUBRAVIEW_FFILTER_CHOICE,   /* by name: one of the option's named constants */
    RUBRAVIEW_FFILTER_TEXT,     /* anything else, typed (sizes, colours, expressions, files, numbers too wide for a slider) */
} rubraview_ffilter_kind_t;

typedef struct rubraview_ffilter_option {
    rubraview_ffilter_kind_t kind;
    proven_u8str_view_t name;          /* as the graph names it */
    proven_u8str_view_t help;
    proven_u8str_view_t text;          /* TEXT: the default as text, or empty when FFmpeg's is not one */
    double value, min, max;            /* numbers, bool (0/1) and choice (its index) */
    proven_u8str_view_t *choices;      /* CHOICE: the constants' names */
    int32_t choice_count;
} rubraview_ffilter_option_t;

typedef struct rubraview_ffilter {
    proven_u8str_view_t name;
    proven_u8str_view_t description;
    rubraview_ffilter_option_t *options;
    int32_t option_count;
} rubraview_ffilter_t;

typedef struct rubraview_ffilter_catalog {
    rubraview_ffilter_t *filters;      /* by name, once sorted */
    int32_t filter_count;
} rubraview_ffilter_catalog_t;

/** The filters in name order (the groups need it). */
void rubraview_ffilter_catalog_sort(rubraview_ffilter_catalog_t *catalog);

/**
 * The sorted list cut into groups of about `size` filters, each at a change of the first letter where there is
 * one: `first[g]` is group g's first filter. How many groups (at most `cap`); 0 for an empty list.
 */
int32_t rubraview_ffilter_groups(const rubraview_ffilter_catalog_t *catalog, int32_t size, int32_t *first, int32_t cap);

/** A group's words, "a–c" by its first and last filters' first letters. */
void rubraview_ffilter_group_name(const rubraview_ffilter_catalog_t *catalog, const int32_t *first, int32_t groups,
                                  int32_t g, char *out, size_t cap);

/**
 * `filter` as a graph, into `out`: its name, then `=` and each shown option the rows changed from its default
 * as `name=value`, `:` between — an int rounded and each number clamped to its range, a bool 1/0, a choice by
 * its name, text as typed. A value is escaped for the filter's options (`\` before `\ ' :`) and the whole for
 * the graph (`\` before `\ ' [ ] , ;`); a control character becomes a space. `values` / `texts` are by option
 * index; NULL takes the defaults. The length, or 0 when it does not fit.
 */
size_t rubraview_ffilter_graph(const rubraview_ffilter_t *filter, const double *values, const proven_u8str_view_t *texts,
                               char *out, size_t cap);

/**
 * A filter a graph may not use, or NULL. These reach outside the picture — files or addresses opened while the
 * graph is built (`movie`, `amovie`), a network port (`zmq`, `azmq`) — or load other code into the program
 * (`frei0r`, `frei0r_src`, `ladspa`, `lv2`; D-189). Read before FFmpeg sees the graph, with `'` and `\` taken
 * out (FFmpeg's unquoting): any word of the graph that is such a name refuses it, an option's value too.
 */
const char *rubraview_ffilter_refused(proven_u8str_view_t graph);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_FFILTER_H */
