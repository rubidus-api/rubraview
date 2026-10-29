#ifndef RUBRAVIEW_GMIC_H
#define RUBRAVIEW_GMIC_H

/*
 * RFC-0009 C (owner, 2026-10-22: "남은 필터들도 구현해 주시고"): G'MIC's filters, run by the user's own
 * `gmic.exe` — a program of its own, talked to through files and its command line (D-189's line). This is
 * the core's part: the filter list G'MIC writes (`gmic parse_gui json`), read into categories, filters and
 * their parameters, and the arguments a filter is run with.
 */

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Parameters a filter shows; a filter with more runs the rest at their defaults. */
#define RUBRAVIEW_GMIC_MAX_SHOWN 64
/** The longest filter list read (G'MIC 4.0.6 writes 2.8 MB). */
#define RUBRAVIEW_GMIC_LIST_MAX (32u << 20)
/** Longest argument string built for a filter, NUL included. */
#define RUBRAVIEW_GMIC_ARGS_MAX 16384u

typedef enum rubraview_gmic_kind {
    RUBRAVIEW_GMIC_INT = 0,
    RUBRAVIEW_GMIC_FLOAT,
    RUBRAVIEW_GMIC_BOOL,
    RUBRAVIEW_GMIC_CHOICE,
    RUBRAVIEW_GMIC_COLOR,     /* "r,g,b" or "r,g,b,a" */
    RUBRAVIEW_GMIC_POINT,     /* "x,y" in percent */
    RUBRAVIEW_GMIC_TEXT,
    RUBRAVIEW_GMIC_FILE,
    RUBRAVIEW_GMIC_FOLDER,
    RUBRAVIEW_GMIC_VALUE,     /* a fixed value the user does not see */
    RUBRAVIEW_GMIC_BUTTON,    /* 0 unless pressed; never pressed here */
} rubraview_gmic_kind_t;

typedef struct rubraview_gmic_param {
    rubraview_gmic_kind_t kind;
    proven_u8str_view_t name;          /* markup taken out */
    proven_u8str_view_t text;          /* the default as G'MIC writes it (every kind) */
    double value, min, max;            /* numbers, bool (0/1) and choice (its index) */
    int32_t pos;                       /* the argument's place, from 1 */
    proven_u8str_view_t *choices;
    int32_t choice_count;
    bool hidden;                       /* visibility 0, or a value */
} rubraview_gmic_param_t;

typedef struct rubraview_gmic_filter {
    proven_u8str_view_t name;          /* markup taken out */
    proven_u8str_view_t command;       /* the G'MIC command, a word */
    int32_t category;
    rubraview_gmic_param_t *params;    /* the ones with a place, in their place's order */
    int32_t param_count;
} rubraview_gmic_filter_t;

typedef struct rubraview_gmic_catalog {
    proven_u8str_view_t version;       /* gmic_version */
    proven_u8str_view_t *categories;
    int32_t category_count;
    rubraview_gmic_filter_t *filters;
    int32_t filter_count;
} rubraview_gmic_catalog_t;

/**
 * The filter list `gmic parse_gui json` writes; its strings are views into `json` or copies in `arena`, and
 * the reading takes room of its own that it gives back. Anything that is not that shape — not JSON, nested too deep,
 * a filter with no command — is passed over; `*why` (when given) says why nothing at all could be read.
 */
rubraview_gmic_catalog_t rubraview_gmic_catalog_parse(proven_arena_t *arena, proven_u8str_view_t json, const char **why);

/**
 * `filter`'s arguments, comma-separated in their places' order, into `out`: numbers as numbers (an int
 * rounded, each clamped to its range), bool 0/1, a choice's index, colour and point as written, text, file
 * and folder in double quotes with `\` and `"` escaped. `values` / `texts` are by parameter index; NULL takes
 * the defaults. The length, or 0 when it does not fit.
 */
size_t rubraview_gmic_args(const rubraview_gmic_filter_t *filter, const double *values, const proven_u8str_view_t *texts,
                           char *out, size_t cap);

/**
 * A command line as G'MIC is given it, one argument an item: split at spaces and tabs outside double quotes
 * (the quotes stay in the item; G'MIC reads them). How many items, at most `cap`; 0 when there are more.
 */
size_t rubraview_gmic_split(proven_u8str_view_t line, proven_u8str_view_t *items, size_t cap);

/** Whether `word` is a G'MIC command name as the list gives one: letters, digits and `_`, a letter first. */
bool rubraview_gmic_is_command(proven_u8str_view_t word);

/**
 * RV-186: how far making the filter list is, in percent, reckoned from the last run's times: `step` 1 fetching,
 * 2 listing, `in_step` seconds into it, `fetch` and `list` the seconds each took last time. A step that runs longer
 * than it did stays at its end; never 100 while it runs. -1 when there is no last run to go by.
 */
int rubraview_gmic_list_percent(int step, double in_step, double fetch, double list);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_GMIC_H */
