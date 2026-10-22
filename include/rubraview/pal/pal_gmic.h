#ifndef RUBRAVIEW_PAL_GMIC_H
#define RUBRAVIEW_PAL_GMIC_H

/*
 * RFC-0009 C: the user's `gmic.exe`, a program of its own. It is started with its arguments, no window and
 * nothing of ours handed down, in a job that ends it with everything it started; it is waited for with a
 * time limit, and Esc (the caller's tick) ends it early. Pictures go to it and come back as files.
 */

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most arguments one run takes. */
#define RUBRAVIEW_GMIC_MAX_ITEMS 64

/** The gmic.exe at `where`: the program itself or the folder it is in. Empty when it is not there. */
proven_u8str_view_t rubraview_pal_gmic_find(proven_arena_t *arena, proven_u8str_view_t where);

typedef struct rubraview_gmic_exec {
    bool ok;                 /* it ran to the end and said 0 */
    bool cut;                /* ended by us: late, or Esc */
    bool cancelled;          /* ...by Esc */
    int  exit_code;
    char said[512];          /* the last of what it wrote, its errors among them */
} rubraview_gmic_exec_t;

/**
 * `exe` with `items` as its arguments, each quoted as Windows' command line wants. `timeout_ms` 0: no limit.
 * `tick` is asked every 50 ms (true: stop it), and may be NULL.
 */
rubraview_gmic_exec_t rubraview_pal_gmic_exec(proven_u8str_view_t exe, const proven_u8str_view_t *items, size_t count,
                                              uint32_t timeout_ms, bool (*tick)(void *context), void *context);

/**
 * A folder of our own for the pictures handed to G'MIC, spelt in ASCII (a short name when the temporary
 * folder's name is not), with a trailing separator; empty when there is none. `tag` makes the names
 * this run's: `<folder>rubraview-gmic-<pid>-<tag>-in.png`.
 */
proven_u8str_view_t rubraview_pal_gmic_temp(proven_arena_t *arena, uint32_t tag, bool output);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_GMIC_H */
