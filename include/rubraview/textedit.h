#ifndef RUBRAVIEW_TEXTEDIT_H
#define RUBRAVIEW_TEXTEDIT_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A one-line text field: the rename box and the picker's "Change ext" box.
 * UTF-8 in a caller's fixed buffer, a caret, and an anchor — the other end
 * of the selection (anchor == caret: nothing selected). The caret moves by
 * whole characters, never into the middle of a Hangul syllable's three
 * bytes, and typing replaces what is selected.
 */
typedef struct rubraview_textedit {
    char  *buf;       /* NUL-terminated after `len` */
    size_t cap;       /* bytes including the terminator */
    size_t len;
    size_t caret;
    size_t anchor;
} rubraview_textedit_t;

rubraview_textedit_t rubraview_textedit_make(char *buf, size_t cap);

/* The whole text, the caret after it, nothing selected. */
void rubraview_textedit_set(rubraview_textedit_t *te, u8str_t text);

/* Typed or pasted text in place of the selection, at the caret. What does
   not fit is dropped, whole characters only. Line breaks become spaces (a
   file name has none). Returns the bytes inserted. */
size_t rubraview_textedit_insert(rubraview_textedit_t *te, u8str_t text);

/* Backspace and Delete: the selection, else the character before / after. */
void rubraview_textedit_backspace(rubraview_textedit_t *te);
void rubraview_textedit_delete(rubraview_textedit_t *te);

typedef enum rubraview_textedit_move {
    RUBRAVIEW_TEXTEDIT_LEFT = 0,
    RUBRAVIEW_TEXTEDIT_RIGHT,
    RUBRAVIEW_TEXTEDIT_HOME,
    RUBRAVIEW_TEXTEDIT_END,
} rubraview_textedit_move_t;

/* The caret moves; with `extend` (Shift held) the anchor stays and the
   selection grows or shrinks; without it a selection collapses — Left to
   its start, Right to its end, as editors do. */
void rubraview_textedit_move(rubraview_textedit_t *te, rubraview_textedit_move_t move, bool extend);

void rubraview_textedit_select_all(rubraview_textedit_t *te);
bool rubraview_textedit_has_selection(const rubraview_textedit_t *te);
/* The selected bytes, a view into the buffer (empty when none). */
u8str_t rubraview_textedit_selection(const rubraview_textedit_t *te);
/* The selection's ends in order. */
void rubraview_textedit_selection_range(const rubraview_textedit_t *te, size_t *start, size_t *end);

static inline u8str_t rubraview_textedit_text(const rubraview_textedit_t *te) {
    return (u8str_t){ .ptr = te->buf, .len = te->len };
}

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_TEXTEDIT_H */
