#ifndef RUBRAVIEW_UI_CTXMENU_H
#define RUBRAVIEW_UI_CTXMENU_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The right-click list (owner, 2026-10-14, RV-116): a column of rows at the
 * pointer, in groups with a little room and a line between two. Its rows
 * are the boxes document's `context` items for what is on screen
 * (`boxes_doc.h`); what is here is only where it stands and which row a
 * point or a key means. The viewer paints it and runs the rows.
 */

typedef struct rubraview_ctxmenu_metrics {
    double row_height;   /* one row */
    double gap;          /* the room between two groups (the line is drawn in it) */
    double pad;          /* above the first row and below the last */
    double width;
} rubraview_ctxmenu_metrics_t;

/**
 * Where the list stands for a click at (x, y): its top left there, as a
 * menu opens; moved to the left of the point when it would pass the
 * window's right edge, above it when it would pass the bottom; and kept
 * inside the window whatever it is (a window smaller than the list shows
 * its top). `groups[i]` is row i's group; rows are in order of group.
 */
rubraview_rect_t rubraview_ctxmenu_frame(double window_width, double window_height, double x, double y,
                                         const rubraview_ctxmenu_metrics_t *metrics, const int32_t *groups, size_t count);

/** Row `index`'s place inside `frame`. */
rubraview_rect_t rubraview_ctxmenu_row(rubraview_rect_t frame, const rubraview_ctxmenu_metrics_t *metrics,
                                       const int32_t *groups, size_t index);

/** The row under a point, or -1: outside the list, or in the room between two groups. */
int32_t rubraview_ctxmenu_row_at(rubraview_rect_t frame, const rubraview_ctxmenu_metrics_t *metrics,
                                 const int32_t *groups, size_t count, double x, double y);

/**
 * The arrow keys: the next row that can act after `from` (-1: none chosen
 * yet) going `direction` (+1 down, -1 up), round from the end to the
 * start; -1 when no row can act.
 */
int32_t rubraview_ctxmenu_step(const bool *enabled, size_t count, int32_t from, int direction);

/*
 * RV-287 (owner, 2026-11-15: "창 모양과 크기에 맞게 ... 각 항목이 필요한 만큼의 가로폭 ... html의 inline 요소처럼 가로로 길게
 * 나열 ... 오른쪽 끝에서 워드랩"; it was RV-205's two columns): the title bar's ☰ as buttons each as wide as its
 * words, laid left to right and wrapped at `max_width`, like inline elements. Two buttons of a group are `gap`
 * apart, two groups `group_gap`; a group that would break over the line's end starts the next line when it fits
 * on one; a button wider than the line is cut to it. Every line is `cell_height` high, `gap` below the last.
 */
typedef struct rubraview_flowmenu_metrics {
    double cell_height;
    double gap;         /* between two buttons of a group, and between two lines */
    double group_gap;   /* between two groups on one line */
    double pad;         /* inside the frame, all round */
    bool group_lines;   /* RV-303 (RFC-0010 §6): each group starts a line of its own */
} rubraview_flowmenu_metrics_t;

/**
 * Lays out `count` buttons of `widths` into `out_cells` (may be NULL) from the frame's top left (x, y) and
 * returns the frame round them: as wide as its widest line, as high as all its lines.
 */
rubraview_rect_t rubraview_flowmenu_layout(double x, double y, double max_width, const rubraview_flowmenu_metrics_t *metrics,
                                           const double *widths, const int32_t *groups, size_t count, rubraview_rect_t *out_cells);
/** The button under a point, or -1. */
int32_t rubraview_flowmenu_at(const rubraview_rect_t *cells, size_t count, double x, double y);
/**
 * Up and Down: on the nearest line above or below `from` that has a button that can act, the one whose
 * middle is nearest `from`'s (-1: the first that can act); `from` when there is none that way.
 */
int32_t rubraview_flowmenu_step_vertical(const rubraview_rect_t *cells, const bool *enabled, size_t count, int32_t from, int direction);
/**
 * A frame higher than the room scrolls by whole lines (`pitch` = cell_height + gap): `scroll` brought into
 * [0, the last line at the bottom] on a whole line. `content` is the frame's height, `view` the height shown:
 * pad * 2 + a whole number of pitches.
 */
double rubraview_flowmenu_scroll_clamp(double scroll, double content, double view, double pitch);
/** The scroll that shows a button whose top is `top` below the frame's (unscrolled) top, moving as little as it can. */
double rubraview_flowmenu_scroll_to(double scroll, double top, double content, double view, double pitch, double pad);

/*
 * RV-303 (RFC-0010 §6, D-367): the ☰'s categories, in the order shown, each under its heading (words, not a
 * button). Every action is in one; one this table does not know is in the last.
 */
enum {
    RUBRAVIEW_MENUCAT_OPEN = 0,    /* Open and lists */
    RUBRAVIEW_MENUCAT_MOVE,        /* Move */
    RUBRAVIEW_MENUCAT_FILES,       /* File and folders */
    RUBRAVIEW_MENUCAT_VIEW,        /* View and reading */
    RUBRAVIEW_MENUCAT_PLAY,        /* Play and sound */
    RUBRAVIEW_MENUCAT_SUBTITLES,   /* Subtitles */
    RUBRAVIEW_MENUCAT_EDIT,        /* Edit and output */
    RUBRAVIEW_MENUCAT_ARCHIVES,    /* Archives */
    RUBRAVIEW_MENUCAT_WIDGETS,     /* Widgets and help */
    RUBRAVIEW_MENUCAT_COUNT
};
/** The category `action` is shown under. */
int rubraview_menu_category(proven_u8str_view_t action);
/** A category's heading, the reader's words (N_). */
const char *rubraview_menu_category_heading(int category);

#ifdef __cplusplus
}
#endif

#endif
