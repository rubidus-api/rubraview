/* RV-293 (owner, 2026-11-15: "에디트 컨트롤을 정식으로 만들어 주세요 ... 많은 곳에서 같이 쓰도록"): the edit control's keys,
   typing and pointer for any field (core editctl does the editing). Keys as Windows' fields take them: arrows,
   Home / End, Ctrl+arrows by words, Shift with any of them to select, Ctrl+Home / End, Backspace / Delete and with
   Ctrl by words, Ctrl+A / C / X / V, Ctrl+Z / Y (Ctrl+Shift+Z) to undo and redo; the pointer: a click puts the
   caret, a drag or Shift+click selects, a double click a word, a triple click the line; a right click a list of
   Undo, Cut, Copy, Paste, Delete, Select all. A secret field (a password) is never copied out. */
#include "app.h"
#include "rubraview/editctl.h"

/* Typed, composed-and-done or pasted: whole UTF-8 characters, no control characters (a field of lines keeps its
   breaks), in place of the selection. */
void editbox_text(rubraview_editctl_t *ec, proven_u8str_view_t text, bool typed) {
    if (!ec || text.size == 0) return;
    char clean[4096];
    size_t n = 0;
    if (ec->lines) {   /* the rename filter drops '\n': keep the breaks of a field of lines */
        for (size_t i = 0; i < text.size && n < sizeof clean; ++i) {
            unsigned char c = text.ptr[i];
            if (c == '\n' || c == '\r' || c >= 0x20) clean[n++] = (char)c;
        }
    } else {
        (void)rubraview_rename_append(clean, sizeof clean, &n, text);
    }
    (void)rubraview_editctl_insert(ec, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)clean, .size = n },
                                   typed ? RUBRAVIEW_EDITCTL_TYPING : RUBRAVIEW_EDITCTL_OTHER);
}

static void paste(rubraview_window_t *window, rubraview_editctl_t *ec) {
    char pasted[4096];
    size_t n = rubraview_pal_clipboard_get_text(window, pasted, sizeof pasted);
    editbox_text(ec, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)pasted, .size = n }, false);
}

static void copy(rubraview_window_t *window, rubraview_editctl_t *ec) {
    proven_u8str_view_t chosen = rubraview_textedit_selection(&ec->te);
    if (chosen.size > 0) (void)rubraview_pal_clipboard_set_text(window, chosen);
}

/* The editing keys; true when the key was the field's. Enter, Esc and Tab are the caller's. */
bool editbox_key(rubraview_window_t *window, rubraview_editctl_t *ec, rubraview_key_combo_t combo, bool secret) {
    bool shift = (combo.modifiers & RUBRAVIEW_MOD_SHIFT) != 0, ctrl = (combo.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
    if (key_is(combo, "Left"))        rubraview_editctl_move(ec, ctrl ? RUBRAVIEW_EDITCTL_WORD_LEFT : RUBRAVIEW_EDITCTL_LEFT, shift);
    else if (key_is(combo, "Right"))  rubraview_editctl_move(ec, ctrl ? RUBRAVIEW_EDITCTL_WORD_RIGHT : RUBRAVIEW_EDITCTL_RIGHT, shift);
    else if (key_is(combo, "Home"))   rubraview_editctl_move(ec, ctrl ? RUBRAVIEW_EDITCTL_START : RUBRAVIEW_EDITCTL_HOME, shift);
    else if (key_is(combo, "End"))    rubraview_editctl_move(ec, ctrl ? RUBRAVIEW_EDITCTL_FINISH : RUBRAVIEW_EDITCTL_END, shift);
    else if (key_is(combo, "Backspace")) rubraview_editctl_backspace(ec, ctrl);
    else if (key_is(combo, "Delete")) {
        if (shift && !ctrl) { if (!secret) copy(window, ec); rubraview_editctl_cut(ec); }   /* Shift+Delete: cut */
        else rubraview_editctl_delete(ec, ctrl);
    }
    else if (key_is(combo, "Insert") && ctrl) { if (!secret) copy(window, ec); }       /* Ctrl+Insert: copy */
    else if (key_is(combo, "Insert") && shift) paste(window, ec);                      /* Shift+Insert: paste */
    else if (ctrl && key_is(combo, "A")) { ec->te.anchor = 0; ec->te.caret = ec->te.len; }
    else if (ctrl && key_is(combo, "C")) { if (!secret) copy(window, ec); }
    else if (ctrl && key_is(combo, "X")) { if (!secret) { copy(window, ec); rubraview_editctl_cut(ec); } }
    else if (ctrl && key_is(combo, "V")) paste(window, ec);
    else if (ctrl && key_is(combo, "Z")) { if (shift) rubraview_editctl_redo(ec); else rubraview_editctl_undo(ec); }
    else if (ctrl && key_is(combo, "Y")) rubraview_editctl_redo(ec);
    else return false;
    return true;
}

/* Where a point falls in the text shown from `x0` (`shown`, as drawn: the composition in it, dots for a secret):
   the byte of `shown` nearest it, by the widths of its characters in the face and size it is drawn in. */
size_t editbox_shown_at(rubraview_renderer_t *r, proven_u8str_view_t shown, double size, double x0, double x) {
    size_t best = 0;
    double best_d = 1e30;
    for (size_t i = 0; i <= shown.size; ++i) {
        if (i < shown.size && (shown.ptr[i] & 0xC0) == 0x80) continue;   /* only at characters' starts */
        double w = 0.0;
        if (i > 0) (void)rubraview_pal_render_measure_text(r, (proven_u8str_view_t){ .ptr = shown.ptr, .size = i }, size, &w);
        double d = fabs(x0 + w - x);
        if (d < best_d) { best_d = d; best = i; }
        if (x0 + w > x + size * 2.0) break;   /* far past it */
    }
    return best;
}

/* A press: how many in a row (a double click within the system's time and a few pixels of the last). */
int editbox_clicks(editbox_clicks_t *c, double x, double y, double now) {
    if (now - c->at < 0.5 && fabs(x - c->x) < 5.0 && fabs(y - c->y) < 5.0) c->count = c->count % 3 + 1;
    else c->count = 1;
    c->at = now; c->x = x; c->y = y;
    return c->count;
}

/* A left press at byte `at` of the text (not of what is shown): the caret, a word, the line, or Shift's selection. */
void editbox_press(rubraview_editctl_t *ec, size_t at, int clicks, bool shift) {
    if (clicks == 2) rubraview_editctl_select_word(ec, at);
    else if (clicks >= 3) rubraview_editctl_select_line(ec, at);
    else rubraview_editctl_place(ec, at, shift);
}

/* The right click's list: what each row does. */
void editbox_menu_do(rubraview_window_t *window, rubraview_editctl_t *ec, int what, bool secret) {
    switch (what) {
        case EDITBOX_UNDO: rubraview_editctl_undo(ec); break;
        case EDITBOX_CUT: if (!secret) { copy(window, ec); rubraview_editctl_cut(ec); } break;
        case EDITBOX_COPY: if (!secret) copy(window, ec); break;
        case EDITBOX_PASTE: paste(window, ec); break;
        case EDITBOX_DELETE: rubraview_editctl_delete(ec, false); break;
        case EDITBOX_ALL: ec->te.anchor = 0; ec->te.caret = ec->te.len; break;
        default: break;
    }
}
