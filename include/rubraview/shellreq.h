#ifndef RUBRAVIEW_SHELLREQ_H
#define RUBRAVIEW_SHELLREQ_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * D-86: what Explorer's right-click menu asks of the viewer. A request is
 * a verb and the files it is about. It arrives on the command line — one
 * process with every selected file from the Windows 11 menu, one process
 * a file from the classic one — and is handed to the window already open,
 * which gathers the pieces of one selection before acting.
 */

typedef enum rubraview_shell_verb {
    RUBRAVIEW_SHELL_VERB_NONE = 0,   /* a plain launch: a double click, a shortcut */
    RUBRAVIEW_SHELL_VERB_OPEN,       /* --open       as a double click does: its folder is the list */
    RUBRAVIEW_SHELL_VERB_OPEN_ONLY,  /* --open-only  these files are the list, nothing else */
    RUBRAVIEW_SHELL_VERB_ADD,        /* --add        appended to the list being read */
    RUBRAVIEW_SHELL_VERB_BROWSE,     /* --browse     the archive's entries, in the picker */
    RUBRAVIEW_SHELL_VERB_CONVERT,    /* --convert    the batch panel, on these files */
    RUBRAVIEW_SHELL_VERB_PRINT,      /* --print      the print dialog, one picture a sheet */
    RUBRAVIEW_SHELL_VERB_COUNT
} rubraview_shell_verb_t;

/** "--open" and the others; "" for NONE. */
const char *rubraview_shell_verb_flag(rubraview_shell_verb_t verb);

/** The verb a command-line argument names, NONE when it names none. */
rubraview_shell_verb_t rubraview_shell_verb_for_flag(u8str_t arg);

/* ---- the menu's items ---- */

typedef struct rubraview_shell_menu_item {
    rubraview_shell_verb_t verb;
    const char *id;        /* the registry key's name; its order is the menu's */
    const char *text;
    const char *text_ko;
    uint32_t    kinds;     /* RUBRAVIEW_SHELL_* of filemanage.h: the files it is offered on */
    bool        single;    /* shown only when one file is selected */
} rubraview_shell_menu_item_t;

const rubraview_shell_menu_item_t *rubraview_shell_menu_items(size_t *out_count);

/* ---- a request, as it crosses from one process to another ---- */

/**
 * The bytes for a request: the verb, whether the selection is whole, and
 * each path with a NUL after it. Returns how many bytes that takes;
 * written only when `cap` holds them.
 */
size_t rubraview_shellreq_pack(rubraview_shell_verb_t verb, bool whole, const u8str_t *paths, size_t count,
                               char *dst, size_t cap);

/**
 * Read it back. The paths point into `blob`. False for bytes that are
 * not a request; paths past `cap` are counted out, not stored.
 */
bool rubraview_shellreq_unpack(const char *blob, size_t len, rubraview_shell_verb_t *out_verb, bool *out_whole,
                               u8str_t *paths, size_t cap, size_t *out_count);

/**
 * A file of paths, one a line (UTF-8, with or without a byte-order mark,
 * either line ending). The paths point into `text`. Returns how many
 * there are; stored up to `cap`.
 */
size_t rubraview_shellreq_split_lines(u8str_t text, u8str_t *paths, size_t cap);

/* ---- gathering one selection ---- */

/* The classic menu starts the program once for every selected file. What
   arrives within this long of the piece before it is one selection. */
#define RUBRAVIEW_SHELLREQ_GATHER_SECONDS 0.5

typedef struct rubraview_shellreq_collector {
    rubraview_shell_verb_t verb;
    char   *text;        /* the paths, each with a NUL after it */
    size_t  text_len, text_cap;
    size_t  count;
    double  last;        /* when the latest piece came */
    bool    pending;
    bool    whole;       /* nothing more is coming: act now */
} rubraview_shellreq_collector_t;

/**
 * Add a piece. False when a request with another verb is waiting — it is
 * to be taken first — or when there is no memory.
 */
bool rubraview_shellreq_collect(rubraview_shellreq_collector_t *c, rubraview_shell_verb_t verb, bool whole,
                                const u8str_t *paths, size_t count, double now);

/** True when what was gathered is to be acted on. */
bool rubraview_shellreq_ready(const rubraview_shellreq_collector_t *c, double now);

/** The path after `prev` (the first for NULL), or NULL after the last. */
const char *rubraview_shellreq_next(const rubraview_shellreq_collector_t *c, const char *prev);

/** Forget what was gathered; the memory is kept for the next. */
void rubraview_shellreq_clear(rubraview_shellreq_collector_t *c);

void rubraview_shellreq_free(rubraview_shellreq_collector_t *c);

/* ---- a picture on a sheet ---- */

/**
 * Where a picture goes on the printable part of a sheet: as large as
 * fits with its proportions, in the middle. `out_turn` says the picture
 * is to be turned a quarter first, which is done when it then fills more
 * of the sheet (a wide picture on an upright sheet). The rectangle is
 * for the picture as it is drawn — turned, when it is turned.
 */
void rubraview_print_fit(int32_t sheet_w, int32_t sheet_h, int32_t picture_w, int32_t picture_h,
                         int32_t *out_x, int32_t *out_y, int32_t *out_w, int32_t *out_h, bool *out_turn);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SHELLREQ_H */
