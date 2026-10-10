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

/* ---- how a picture is printed (D-87) ---- */

typedef enum rubraview_print_turn {
    RUBRAVIEW_PRINT_TURN_AUTO = 0,   /* a quarter when it then fills more of the sheet */
    RUBRAVIEW_PRINT_TURN_NONE,
    RUBRAVIEW_PRINT_TURN_RIGHT,      /* a quarter clockwise */
    RUBRAVIEW_PRINT_TURN_HALF,
    RUBRAVIEW_PRINT_TURN_LEFT,
    RUBRAVIEW_PRINT_TURN_COUNT
} rubraview_print_turn_t;

typedef enum rubraview_print_size {
    RUBRAVIEW_PRINT_SIZE_FIT = 0,    /* all of it, as large as the sheet takes */
    RUBRAVIEW_PRINT_SIZE_FILL,       /* the sheet covered; what hangs over is cut */
    RUBRAVIEW_PRINT_SIZE_STRETCH,    /* both sides to the sheet's, proportions given up */
    RUBRAVIEW_PRINT_SIZE_ACTUAL,     /* 96 of its pixels an inch, as a screen shows it */
    RUBRAVIEW_PRINT_SIZE_COUNT
} rubraview_print_size_t;

typedef enum rubraview_print_place {
    RUBRAVIEW_PRINT_PLACE_CENTRE = 0,
    RUBRAVIEW_PRINT_PLACE_TOP_LEFT, RUBRAVIEW_PRINT_PLACE_TOP, RUBRAVIEW_PRINT_PLACE_TOP_RIGHT,
    RUBRAVIEW_PRINT_PLACE_LEFT, RUBRAVIEW_PRINT_PLACE_RIGHT,
    RUBRAVIEW_PRINT_PLACE_BOTTOM_LEFT, RUBRAVIEW_PRINT_PLACE_BOTTOM, RUBRAVIEW_PRINT_PLACE_BOTTOM_RIGHT,
    RUBRAVIEW_PRINT_PLACE_COUNT
} rubraview_print_place_t;

#define RUBRAVIEW_PRINT_SCALE_MIN 10
#define RUBRAVIEW_PRINT_SCALE_MAX 400
#define RUBRAVIEW_PRINT_MARGIN_MAX_MM 50

typedef struct rubraview_print_options {
    rubraview_print_turn_t  turn;
    rubraview_print_size_t  size;
    int32_t                 scale_percent;   /* of the size chosen; 100 leaves it */
    rubraview_print_place_t place;
    int32_t                 margin_mm;       /* kept clear inside the printable part */
    int32_t                 per_sheet;       /* 1, 2, 4, 6 or 9 pictures a sheet */
} rubraview_print_options_t;

/** Fit, turned when that fills more, in the middle, one a sheet: as before D-87. */
rubraview_print_options_t rubraview_print_options_default(void);

/** Every field brought inside what it may be. */
void rubraview_print_options_clamp(rubraview_print_options_t *options);

/** The words the panel shows for a choice. */
const char *rubraview_print_turn_name(rubraview_print_turn_t turn);
const char *rubraview_print_size_name(rubraview_print_size_t size);
const char *rubraview_print_place_name(rubraview_print_place_t place);

/** `per_sheet` as a choice's index (0..4) and back. */
int32_t rubraview_print_per_sheet_index(int32_t per_sheet);
int32_t rubraview_print_per_sheet_of(int32_t index);

/**
 * The part of a sheet the picture numbered `index` on it (0 first) is
 * given: the printable part less the margin, cut into as many cells as
 * pictures go on a sheet, a gap between them. Lengths are the printer's
 * dots; `dpi_x` / `dpi_y` are its dots an inch.
 */
void rubraview_print_cell(int32_t sheet_w, int32_t sheet_h, int32_t dpi_x, int32_t dpi_y,
                          const rubraview_print_options_t *options, int32_t index,
                          int32_t *out_x, int32_t *out_y, int32_t *out_w, int32_t *out_h);

typedef struct rubraview_print_placement {
    int32_t quarter_turns;               /* clockwise, 0..3, done to the picture first */
    int32_t turned_w, turned_h;          /* the picture's size after them */
    int32_t src_x, src_y, src_w, src_h;  /* the part of the turned picture that is printed */
    int32_t dst_x, dst_y, dst_w, dst_h;  /* where on the sheet; all of it inside the cell */
} rubraview_print_placement_t;

/**
 * Where a picture goes in a cell. What would hang over the cell is cut
 * from the picture instead (`src_*`), so nothing is drawn outside it.
 * False, and nothing to print, for an empty cell or picture.
 */
bool rubraview_print_place(int32_t cell_x, int32_t cell_y, int32_t cell_w, int32_t cell_h,
                           int32_t dpi_x, int32_t dpi_y, int32_t picture_w, int32_t picture_h,
                           const rubraview_print_options_t *options, rubraview_print_placement_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SHELLREQ_H */
