#ifndef RUBRAVIEW_EDITCTL_H
#define RUBRAVIEW_EDITCTL_H

#include "rubraview/core.h"
#include "rubraview/textedit.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-293 (owner, 2026-11-15: "에디트 컨트롤을 정식으로 만들어 주세요 ... 많은 곳에서 같이 쓰도록"; what went wrong: the mouse,
 * words and undo, the right-click menu, long text and the IME, Shift selection): the one edit control's logic, over
 * textedit, for every field. Words (Ctrl+arrows, Ctrl+Backspace / Delete, a double click), undo and redo (a run of
 * typing one step), the caret from a point given where each character begins, the scroll that keeps the caret in
 * view, and lines for the fields that have them (the subtitles editor). Measuring is the app's: it hands over the
 * x of every character boundary of a line. Host-tested.
 */

typedef struct rubraview_editctl_state { char *text; size_t len, caret, anchor; } rubraview_editctl_state_t;

typedef enum rubraview_editctl_kind {
    RUBRAVIEW_EDITCTL_OTHER = 0,   /* a paste, a cut, a delete of a selection: a step of its own */
    RUBRAVIEW_EDITCTL_TYPING,      /* characters typed one after another: one step */
    RUBRAVIEW_EDITCTL_ERASING,     /* Backspace / Delete one after another: one step */
} rubraview_editctl_kind_t;

typedef struct rubraview_editctl {
    rubraview_textedit_t te;
    bool lines;                       /* Enter breaks the line; Up / Down move by lines */
    rubraview_editctl_state_t *undo;  /* the states before each step, oldest first */
    size_t undo_n, undo_cap;
    rubraview_editctl_state_t *redo;  /* the states an undo left, newest last */
    size_t redo_n, redo_cap;
    rubraview_editctl_kind_t last;    /* what the last step was, to join a run */
    size_t last_caret;                /* where it left the caret: typing elsewhere is a new step */
} rubraview_editctl_t;

#define RUBRAVIEW_EDITCTL_UNDO_MAX 200

/** Over the caller's buffer; `lines` for a field of several lines. */
void rubraview_editctl_init(rubraview_editctl_t *ec, char *buf, size_t cap, bool lines);
void rubraview_editctl_free(rubraview_editctl_t *ec);
/** The text set anew (nothing to undo), the caret at its end, nothing selected. */
void rubraview_editctl_set(rubraview_editctl_t *ec, proven_u8str_view_t text);

/** Typed (`kind` TYPING) or pasted (OTHER) text in place of the selection. Returns the bytes put in. */
size_t rubraview_editctl_insert(rubraview_editctl_t *ec, proven_u8str_view_t text, rubraview_editctl_kind_t kind);
/** Backspace / Delete: the selection, else a character, else (`word`) back to the word's start / on to the next. */
void rubraview_editctl_backspace(rubraview_editctl_t *ec, bool word);
void rubraview_editctl_delete(rubraview_editctl_t *ec, bool word);
/** The selection taken away (a cut), as one step. */
void rubraview_editctl_cut(rubraview_editctl_t *ec);

typedef enum rubraview_editctl_move {
    RUBRAVIEW_EDITCTL_LEFT = 0, RUBRAVIEW_EDITCTL_RIGHT,
    RUBRAVIEW_EDITCTL_WORD_LEFT, RUBRAVIEW_EDITCTL_WORD_RIGHT,
    RUBRAVIEW_EDITCTL_HOME, RUBRAVIEW_EDITCTL_END,               /* of the line (of the text in a one-line field) */
    RUBRAVIEW_EDITCTL_START, RUBRAVIEW_EDITCTL_FINISH,           /* of the whole text (Ctrl+Home / Ctrl+End) */
} rubraview_editctl_move_t;

/** The caret moved; with `extend` the selection grows from the anchor, without it collapses as editors do. */
void rubraview_editctl_move(rubraview_editctl_t *ec, rubraview_editctl_move_t move, bool extend);
/** The caret put at byte `at` (a click), or the selection taken to it (`extend`: a drag, Shift+click). */
void rubraview_editctl_place(rubraview_editctl_t *ec, size_t at, bool extend);
/** A double click at `at`: the word (or the run of spaces or of signs) there selected. */
void rubraview_editctl_select_word(rubraview_editctl_t *ec, size_t at);
/** A triple click: the line (all, in a one-line field). */
void rubraview_editctl_select_line(rubraview_editctl_t *ec, size_t at);

bool rubraview_editctl_can_undo(const rubraview_editctl_t *ec);
bool rubraview_editctl_can_redo(const rubraview_editctl_t *ec);
void rubraview_editctl_undo(rubraview_editctl_t *ec);
void rubraview_editctl_redo(rubraview_editctl_t *ec);

/** The line `at` is on: its first byte and its end (the '\n' or the text's end). */
void rubraview_editctl_line_of(const rubraview_editctl_t *ec, size_t at, size_t *start, size_t *end);

/**
 * The byte boundary nearest `x` on a line whose character boundaries are `offsets[i]` (bytes from the text's start)
 * at `xs[i]` (pixels from the line's left), `n` of them in order (the line's start first, its end last).
 */
size_t rubraview_editctl_at_x(const size_t *offsets, const double *xs, size_t n, double x);

/** The scroll (pixels hidden at the left) that shows the caret at `caret_x` in a box `width` wide, `margin` kept. */
double rubraview_editctl_scroll(double scroll, double caret_x, double text_width, double width, double margin);

#ifdef __cplusplus
}
#endif

#endif
