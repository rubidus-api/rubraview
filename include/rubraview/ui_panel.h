#ifndef RUBRAVIEW_UI_PANEL_H
#define RUBRAVIEW_UI_PANEL_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The panel model behind §3.13's adjustment workbench, §3.10's export
 * dialog and §3.11's batch dialog (RV-065, RV-018, RV-068).
 *
 * All three are the same shape: a titled column of rows, where a row is
 * a slider, a toggle, a choice or a button. What differs is the rows,
 * not the behaviour — so the behaviour is written once, here, where the
 * host suite can check it, and the Windows side only draws.
 *
 * Nothing here knows what a row *means*. A row carries an id; the caller
 * maps the id to a slider on its edit session or a field in its export
 * options. That is what keeps this module free of both Direct2D and the
 * editing model.
 */

typedef enum rubraview_row_kind {
    RUBRAVIEW_ROW_SLIDER = 0,
    RUBRAVIEW_ROW_TOGGLE,
    RUBRAVIEW_ROW_CHOICE,   /* one of a small set, cycled or picked */
    RUBRAVIEW_ROW_BUTTON,
    RUBRAVIEW_ROW_SEPARATOR,
} rubraview_row_kind_t;

typedef struct rubraview_panel_row {
    rubraview_row_kind_t kind;
    int32_t id;             /* the caller's own meaning */
    u8str_t label;

    double  value;          /* sliders: the current value; toggles: 0 or 1; choices: the index */
    double  min_value, max_value;   /* sliders only */
    double  step;           /* 0 for continuous */
    int32_t choice_count;   /* choices only */
    bool    enabled;
} rubraview_panel_row_t;

#define RUBRAVIEW_PANEL_MAX_ROWS 64

typedef struct rubraview_panel {
    u8str_t title;
    rubraview_panel_row_t rows[RUBRAVIEW_PANEL_MAX_ROWS];
    size_t  row_count;

    rubraview_rect_t bounds;     /* where the panel sits, in client pixels */
    double  row_height;
    double  padding;
    double  label_width;         /* the left column; the control fills the rest */

    int32_t active_row;          /* the row being dragged, or -1 */
    int32_t hover_row;
    bool    open;
    /* The owner's edit (2026-10-17): moved by its title, scrolled when taller than the window. */
    bool    placed;              /* moved by the reader: `place_x/y` is where its top-left goes */
    double  place_x, place_y;
    bool    moving;              /* its title is held */
    double  grab_x, grab_y;      /* where in the panel it was taken */
    double  scroll;              /* how far its rows are scrolled up */
    double  content_height;      /* its rows, all of them */
} rubraview_panel_t;

/**
 * A panel anchored to the right-hand edge of a window of the given size,
 * sized to its rows. The right edge is where §3.13 puts the workbench,
 * and it is the side that does not fight the reading area.
 */
rubraview_panel_t rubraview_panel_create(u8str_t title, double dpi_scale);

/** Add a row. Returns false when the panel is full. */
bool rubraview_panel_add_slider(rubraview_panel_t *panel, int32_t id, u8str_t label,
                                double value, double min_value, double max_value, double step);
bool rubraview_panel_add_toggle(rubraview_panel_t *panel, int32_t id, u8str_t label, bool on);
bool rubraview_panel_add_choice(rubraview_panel_t *panel, int32_t id, u8str_t label,
                                int32_t index, int32_t choice_count);
bool rubraview_panel_add_button(rubraview_panel_t *panel, int32_t id, u8str_t label);
bool rubraview_panel_add_separator(rubraview_panel_t *panel);

/**
 * Place the panel inside a window. It is kept fully on screen: a panel
 * with more rows than the window is tall is clamped rather than running
 * off the bottom, which is the same rule §3.6 applies to the menu box.
 */
void rubraview_panel_layout(rubraview_panel_t *panel, double window_width, double window_height);

/** The rectangle of one row, and of its control area. */
rubraview_rect_t rubraview_panel_row_rect(const rubraview_panel_t *panel, size_t index);
rubraview_rect_t rubraview_panel_control_rect(const rubraview_panel_t *panel, size_t index);

/** The row under a point, or -1. Separators are never hit. */
int32_t rubraview_panel_row_at(const rubraview_panel_t *panel, double px, double py);

typedef enum rubraview_panel_event {
    RUBRAVIEW_PANEL_NONE = 0,
    RUBRAVIEW_PANEL_VALUE_CHANGED,
    RUBRAVIEW_PANEL_BUTTON_PRESSED,
    RUBRAVIEW_PANEL_CLOSE,          /* the X at the title's right end (owner, 2026-10-15) */
} rubraview_panel_event_t;

/** The X at the right end of the title row, a row tall and square; empty while closed. */
rubraview_rect_t rubraview_panel_close_rect(const rubraview_panel_t *panel);

/**
 * A press. A slider starts dragging and jumps to where it was clicked —
 * the behaviour a reader expects from a track; a toggle flips; a choice
 * advances; a button reports itself.
 *
 * `out_row` is the row the event belongs to.
 */
rubraview_panel_event_t rubraview_panel_press(rubraview_panel_t *panel, double px, double py,
                                              int32_t *out_row);

/** A drag while a slider is held. Does nothing when nothing is held. */
rubraview_panel_event_t rubraview_panel_drag(rubraview_panel_t *panel, double px, double py,
                                             int32_t *out_row);

/** Release: the drag ends. */
void rubraview_panel_release(rubraview_panel_t *panel);

/** Where a row's value sits along its control, as 0..1 — what the drawing code needs. */
double rubraview_panel_fill_fraction(const rubraview_panel_t *panel, size_t index);

/** Find a row by the caller's id, or -1. */
int32_t rubraview_panel_find(const rubraview_panel_t *panel, int32_t id);

/** Set a row's value from outside, clamped and stepped like a drag would be. */
void rubraview_panel_set_value(rubraview_panel_t *panel, int32_t id, double value);

/** The title row, less its X: where a press takes the panel to move it. */
bool rubraview_panel_title_hit(const rubraview_panel_t *panel, double px, double py);
/** Take it by the title at (px, py); move it there; let it go. */
void rubraview_panel_move_begin(rubraview_panel_t *panel, double px, double py);
void rubraview_panel_move_to(rubraview_panel_t *panel, double px, double py, double window_width, double window_height);
/** Its rows scrolled by `dy` pixels (down for a positive one), kept within them. */
void rubraview_panel_scroll_by(rubraview_panel_t *panel, double dy);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_PANEL_H */
