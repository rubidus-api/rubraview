#ifndef RUBRAVIEW_PICKER_H
#define RUBRAVIEW_PICKER_H

#include "rubraview/core.h"
#include "rubraview/pal/pal_fs.h"
#include "rubraview/ui_virtual.h"
#include "rubraview/pagesource.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The in-app Metro tile file picker (RFC-0001 §3.15.2), modelled without
 * a renderer: breadcrumb segmentation, the virtualised tile grid,
 * type-ahead jumping (§3.15.4 point 1), and multi-select with its
 * selection metrics. The native COM dialog (§3.15.1) already exists as a
 * PAL backend; this is the touch-first alternative for mobile Remote
 * Desktop, where a 12 px list font is unusable.
 */

#define RUBRAVIEW_PICKER_MAX_CRUMBS 32

/**
 * §3.15.2: every path segment becomes its own tappable tile. Each crumb
 * carries the prefix that navigating to it would open, so tapping
 * "Comics" in "C:/Comics/Berserk" opens "C:/Comics".
 */
typedef struct rubraview_breadcrumb {
    proven_u8str_view_t label;  /* the segment itself, e.g. "Comics" */
    proven_u8str_view_t prefix; /* the path up to and including it */
} rubraview_breadcrumb_t;

typedef struct rubraview_breadcrumbs {
    rubraview_breadcrumb_t items[RUBRAVIEW_PICKER_MAX_CRUMBS];
    size_t count;
} rubraview_breadcrumbs_t;

/**
 * The PC page (owner, 2026-09-28: "시스템 전체 다 볼 수 있게"): every
 * drive and the usual folders, listed by rubraview_pal_fs_list_places.
 * The picker keeps it as its folder under this name, which no real path
 * can spell.
 */
#define RUBRAVIEW_PICKER_PLACES "::pc"

bool rubraview_picker_is_places(proven_u8str_view_t dir);

/**
 * The recent files as a place of their own (owner, 2026-10-16): the same
 * screen, tiles or details, sorting and choosing as a folder. Its crumbs
 * are PC > Recent, and `..` is the PC page.
 */
#define RUBRAVIEW_PICKER_RECENT "::recent"

bool rubraview_picker_is_recent(proven_u8str_view_t dir);

/**
 * The folder `..` opens: the parent, or the PC page from a root ("C:",
 * "C:/", "/", "//server/share") or a bare relative name. Empty for the
 * PC page itself and for an empty path.
 */
proven_u8str_view_t rubraview_picker_parent(proven_u8str_view_t dir);

/**
 * Inside the archive being read (owner, 2026-09-28: moving around a comic
 * quickly). The picker names a place in it as the archive's own path and
 * the folder inside, "D:/Comics/Big.cbz/Vol 1"; the archive's path alone
 * is its top. True when `dir` is such a place; `inner` is the part after
 * the archive, without separators at its ends. Either separator, and
 * letters in either case, as Windows compares paths.
 */
bool rubraview_picker_inside_archive(proven_u8str_view_t dir, proven_u8str_view_t archive_path, proven_u8str_view_t *out_inner);

#define RUBRAVIEW_PICKER_NOT_A_PAGE ((size_t)-1)

typedef struct rubraview_archive_level {
    rubraview_fs_listing_t listing;   /* its folders, then its pages, in the book's order */
    size_t *page_of;                  /* one per entry: the page's index, or NOT_A_PAGE for a folder */
} rubraview_archive_level_t;

/**
 * One level of an open archive: the folders and pages directly in
 * `inner`, from the source's pages (which are in the book's order, a
 * folder's pages together). Paths are the archive's path joined with the
 * inside, NUL-terminated in `arena`.
 */
rubraview_archive_level_t rubraview_picker_archive_level(proven_arena_t *arena, proven_u8str_view_t archive_path,
                                                         const rubraview_page_ref_t *pages, size_t page_count,
                                                         proven_u8str_view_t inner);

/**
 * A path typed into the picker's path box (owner, 2026-09-28), cleaned:
 * spaces and the quotes Explorer's "Copy as path" adds are dropped, a
 * trailing separator goes (not a root's), a bare drive "C:" becomes
 * "C:/", and a relative name is joined to `current` unless that is the
 * PC page. Empty when nothing is left. NUL-terminated, in `arena`.
 */
proven_u8str_view_t rubraview_picker_typed_path(proven_arena_t *arena, proven_u8str_view_t typed, proven_u8str_view_t current);

/**
 * Split a path into tappable segments, after a first "PC" chip that
 * opens the PC page. Slices point into `path`.
 */
rubraview_breadcrumbs_t rubraview_picker_breadcrumbs(proven_u8str_view_t path);

/*
 * How a tap picks (owner, 2026-09-23: "쉬프트나 컨트롤 입력 없이"). A mode
 * is turned on by its own button and stays on until it is turned off, so
 * a reader on a touch screen never holds a modifier down.
 */
typedef enum rubraview_picker_mode {
    RUBRAVIEW_PICK_SINGLE = 0,   /* a tap opens the item */
    RUBRAVIEW_PICK_INDIVIDUAL,   /* a tap turns one item's selection on or off */
    RUBRAVIEW_PICK_RANGE,        /* two taps invert everything between them */
} rubraview_picker_mode_t;

/** No range is being marked out. */
#define RUBRAVIEW_PICKER_NO_ANCHOR ((size_t)-1)

typedef struct rubraview_picker {
    const rubraview_fs_listing_t *listing; /* the current directory, already sorted by the caller */
    double tile_extent;      /* one tile's height plus gutter, along the scroll axis */
    double viewport_extent;
    double scroll_offset;
    int32_t columns;         /* tiles per row */
    size_t  focus;           /* the item type-ahead and the keyboard move */
    bool    multi_select;
    bool   *selected;        /* caller-owned, listing->count entries, or NULL when single-select */
    rubraview_picker_mode_t mode;
    size_t  range_anchor;    /* the first tap of a range, or RUBRAVIEW_PICKER_NO_ANCHOR */
} rubraview_picker_t;

rubraview_picker_t rubraview_picker_create(const rubraview_fs_listing_t *listing,
                                           double tile_extent, double viewport_extent, int32_t columns);

/** The rows currently on screen, expressed as an item range. */
rubraview_virtual_range_t rubraview_picker_visible(const rubraview_picker_t *picker);

void rubraview_picker_scroll_by(rubraview_picker_t *picker, double delta);

/** Scroll so the focused item is on screen. */
void rubraview_picker_reveal_focus(rubraview_picker_t *picker);

/**
 * §3.15.4 point 1: pressing a letter jumps to the next entry starting
 * with it, wrapping around, so repeated presses cycle through matches.
 * Returns true when the focus moved.
 */
bool rubraview_picker_type_ahead(rubraview_picker_t *picker, char letter);

/** Toggle one item's selection; a no-op outside multi-select mode. */
void rubraview_picker_toggle(rubraview_picker_t *picker, size_t index);

/** Turn a mode on or off. Switching forgets a half-marked range; the selection stays. */
void rubraview_picker_set_mode(rubraview_picker_t *picker, rubraview_picker_mode_t mode);

/**
 * A tap on an item. True when the tap picked (so the caller does not open
 * it): in `INDIVIDUAL` it turned that one on or off; in `RANGE` it either
 * marked the start — which is inverted at once, so the reader sees it —
 * or inverted the rest of the way to it. False in `SINGLE`, and for a
 * folder, which is opened rather than picked.
 */
bool rubraview_picker_tap(rubraview_picker_t *picker, size_t index);

/**
 * Every file whose name ends in `ext` (".jpg", case does not matter) is
 * turned on, or off. Returns how many changed.
 */
size_t rubraview_picker_select_extension(rubraview_picker_t *picker, proven_u8str_view_t ext, bool on);

/** Nothing selected, and no range half-marked. */
void rubraview_picker_clear_selection(rubraview_picker_t *picker);

/** Every file selected (folders are opened, not picked); returns how many are. */
size_t rubraview_picker_select_all(rubraview_picker_t *picker);

/** True when every file is selected (and there is at least one). */
bool rubraview_picker_all_selected(const rubraview_picker_t *picker);

/**
 * The one button that selects all and lets all go (owner, 2026-10-16):
 * "Select all" until every file is selected, then "Deselect all".
 */
const char *rubraview_picker_select_label(const rubraview_picker_t *picker);

/* ---- The details view's columns and their order (owner, 2026-10-16) ---- */

typedef enum rubraview_picker_sort_key {
    RUBRAVIEW_PICKER_SORT_NAME = 0,
    RUBRAVIEW_PICKER_SORT_TYPE,        /* the extension */
    RUBRAVIEW_PICKER_SORT_SIZE,
    RUBRAVIEW_PICKER_SORT_CREATED,
    RUBRAVIEW_PICKER_SORT_MODIFIED,
    RUBRAVIEW_PICKER_SORT_RESOLUTION,  /* width x height, as pixels */
    RUBRAVIEW_PICKER_SORT_LENGTH,      /* a film's or a song's */
    RUBRAVIEW_PICKER_SORT_COUNT,
} rubraview_picker_sort_key_t;

/** What a file is, once it has been opened for its thumbnail; zero until then. */
typedef struct rubraview_picker_media {
    int32_t width, height;
    double  seconds;
} rubraview_picker_media_t;

/** The column's heading ("Name", "Type", ...). */
const char *rubraview_picker_sort_name(rubraview_picker_sort_key_t key);

/**
 * The order to show `listing` in, by `key`, ascending or not, written into
 * `order` (a permutation of 0..count-1). ".." stays first and folders come
 * before files whichever way; names break ties in the viewer's natural
 * order. `media` (one per entry, or NULL) gives resolution and length; a
 * file not read yet goes after the read ones in both directions.
 */
void rubraview_picker_sort_order(const rubraview_fs_listing_t *listing, const rubraview_picker_media_t *media,
                                 rubraview_picker_sort_key_t key, bool descending, size_t *order);

/**
 * Keep the folders and the files whose names match `patterns` (a glob
 * list, "*.jpg;*.png"), in their order, and return how many files were
 * dropped (owner, 2026-09-21: the picker shows only what can be opened).
 */
size_t rubraview_picker_keep_openable(rubraview_fs_listing_t *listing, proven_u8str_view_t patterns);

/** §3.15.2: the bottom bar's "N files (M bytes)" metrics. */
void rubraview_picker_selection_metrics(const rubraview_picker_t *picker,
                                        size_t *out_count, uint64_t *out_total_bytes);

/**
 * D-110, the details view's cells. A size short enough for a column
 * ("1.2 MB"), and what kind of thing an entry is: "Folder", or its
 * extension in capitals ("JPG"), "File" without one. Both return the
 * length written, NUL-terminated, 0 when `cap` is too small.
 */
size_t rubraview_picker_size_text(char *buf, size_t cap, uint64_t bytes);
size_t rubraview_picker_type_text(char *buf, size_t cap, proven_u8str_view_t name, bool is_directory);

/**
 * RV-168: which of Settings > Files > Show's kinds (0 all, 1 pictures, 2 films, 3 music) a folder is listed with
 * when a file of it is opened. A picture opened while Show is "all" brings only the pictures; a kind the reader
 * chose stays. Out of range is "all".
 */
int32_t rubraview_folder_show_for(int32_t show, bool opened_picture);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PICKER_H */
