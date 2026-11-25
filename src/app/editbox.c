/* RV-293 (owner, 2026-11-15: "에디트 컨트롤을 정식으로 만들어 주세요 ... 많은 곳에서 같이 쓰도록"): the edit control's keys,
   typing and pointer for any field (core editctl does the editing). Keys as Windows' fields take them: arrows,
   Home / End, Ctrl+arrows by words, Shift with any of them to select, Ctrl+Home / End, Backspace / Delete and with
   Ctrl by words, Ctrl+A / C / X / V, Ctrl+Z / Y (Ctrl+Shift+Z) to undo and redo; the pointer: a click puts the
   caret, a drag or Shift+click selects, a double click a word, a triple click the line; a right click a list of
   Undo, Cut, Copy, Paste, Delete, Select all. A secret field (a password) is never copied out. */
#include "app.h"
#include "rubraview/editctl.h"

/* A field begun anew on `buf` (what an earlier text could undo goes). */
void editbox_begin(rubraview_editctl_t *ec, char *buf, size_t cap, bool lines) {
    rubraview_editctl_free(ec);
    rubraview_editctl_init(ec, buf, cap, lines);
}

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

/* ---- RV-293 E2: one way to draw a field and find a point in it ----
   What is shown: the text with the IME's syllable at the caret, a line break as "↵" (a field of lines shows its lines
   on one row), a dot a character for a secret. map[i] is where byte i of the text is in what is shown (len + 1). */
static size_t shown_of(const rubraview_editctl_t *ec, proven_u8str_view_t comp, bool secret, char *out, size_t cap, size_t *map) {
    const rubraview_textedit_t *te = &ec->te;
    size_t n = 0;
    for (size_t i = 0; i <= te->len; ++i) {
        if (i == te->caret) {   /* the syllable being made goes where the caret is */
            for (size_t k = 0; k < comp.size && n + 3 < cap; ++k) {
                if (secret) { if ((comp.ptr[k] & 0xC0) != 0x80) { proven_sys_mem_copy(out + n, "\xE2\x97\x8F", 3); n += 3; } }
                else out[n++] = (char)comp.ptr[k];
            }
        }
        map[i] = n;
        if (i == te->len || n + 3 >= cap) continue;
        unsigned char c = (unsigned char)te->buf[i];
        if (secret) {
            if ((c & 0xC0) != 0x80) { proven_sys_mem_copy(out + n, "\xE2\x97\x8F", 3); n += 3; }
        } else if (c == '\n') {
            proven_sys_mem_copy(out + n, "\xE2\x86\xB5", 3); n += 3;   /* ↵ */
        } else {
            out[n++] = (char)c;
        }
    }
    return n;
}

void editbox_draw(rubraview_renderer_t *r, const rubraview_editctl_t *ec, editbox_view_t *v, proven_u8str_view_t comp, bool focused,
                  uint32_t text_argb, double dpi) {
    static char shown[16384];
    static size_t map[8192];
    const rubraview_textedit_t *te = &ec->te;
    if (te->len + 1 > sizeof map / sizeof map[0]) return;
    size_t n = shown_of(ec, comp, v->secret, shown, sizeof shown, map);
    proven_u8str_view_t all = { .ptr = (const proven_byte_t *)shown, .size = n };
    double full = 0.0, to_caret = 0.0, pad = 6.0 * dpi;
    (void)rubraview_pal_render_measure_text(r, all, v->size, &full);
    size_t caret_at = map[te->caret <= te->len ? te->caret : te->len];   /* after the syllable being made */
    if (caret_at > n) caret_at = n;
    (void)rubraview_pal_render_measure_text(r, (proven_u8str_view_t){ .ptr = all.ptr, .size = caret_at }, v->size, &to_caret);
    double inner = v->box.width - 2.0 * pad;
    v->scroll = rubraview_editctl_scroll(v->scroll, to_caret, full, inner, 8.0 * dpi);
    v->x0 = v->box.x + pad - v->scroll;
    rubraview_pal_rect_t clip = { v->box.x + 1.0, v->box.y + 1.0, fmax(0.0, v->box.width - 2.0), fmax(0.0, v->box.height - 2.0) };
    if (clip.width <= 0.0 || clip.height <= 0.0) return;
    rubraview_pal_render_push_clip(r, clip);
    size_t a = 0, b = 0;
    rubraview_textedit_selection_range(te, &a, &b);
    if (a != b && focused) {   /* RV-294: a field left keeps its selection unshown, as Windows' do */
        double xa = 0.0, xb = 0.0;
        (void)rubraview_pal_render_measure_text(r, (proven_u8str_view_t){ .ptr = all.ptr, .size = map[a] }, v->size, &xa);
        (void)rubraview_pal_render_measure_text(r, (proven_u8str_view_t){ .ptr = all.ptr, .size = map[b] }, v->size, &xb);
        double left = fmax(v->box.x + 1.0, v->x0 + xa), right = fmin(v->box.x + v->box.width - 1.0, v->x0 + xb);
        if (right > left) rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ left, v->box.y + v->box.height * 0.18, right - left, v->box.height * 0.64 }, 0xA05B9BD5u, 2.0);
    }
    /* the text from where the scroll puts it: what lies left of the box is not drawn */
    size_t from = 0;
    double from_x = 0.0;
    for (size_t i = 0; i < n && v->scroll > 0.0; ++i) {
        if ((shown[i] & 0xC0) == 0x80) continue;
        double w = 0.0;
        (void)rubraview_pal_render_measure_text(r, (proven_u8str_view_t){ .ptr = all.ptr, .size = i }, v->size, &w);
        if (w + 0.5 >= v->scroll) { from = i; from_x = w; break; }
    }
    rubraview_pal_rect_t line = { v->x0 + from_x, v->box.y, v->box.x + v->box.width - pad - (v->x0 + from_x), v->box.height };
    if (n > from && line.width > 0.0)
        rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = all.ptr + from, .size = n - from }, line, v->size, text_argb, RUBRAVIEW_TEXT_LEFT);
    if (comp.size && !v->secret) {   /* the syllable being made, underlined */
        double xs = 0.0, xe = 0.0;
        size_t cs = map[te->caret <= te->len ? te->caret : te->len] - comp.size;
        (void)rubraview_pal_render_measure_text(r, (proven_u8str_view_t){ .ptr = all.ptr, .size = cs }, v->size, &xs);
        (void)rubraview_pal_render_measure_text(r, (proven_u8str_view_t){ .ptr = all.ptr, .size = cs + comp.size }, v->size, &xe);
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ v->x0 + xs, v->box.y + v->box.height * 0.8, xe - xs, 1.5 * dpi }, text_argb, 0.0);
    }
    if (focused && fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5) {
        double cx = v->x0 + to_caret;
        if (cx >= v->box.x && cx <= v->box.x + v->box.width)
            rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ cx, v->box.y + v->box.height * 0.2, 1.5 * dpi, v->box.height * 0.6 }, text_argb, 0.0);
    }
    rubraview_pal_render_pop_clip(r);
}

/* The text's byte under x in a field drawn by editbox_draw (with no syllable being made). */
size_t editbox_hit(rubraview_renderer_t *r, const rubraview_editctl_t *ec, const editbox_view_t *v, double x) {
    static char shown[16384];
    static size_t map[8192];
    const rubraview_textedit_t *te = &ec->te;
    if (te->len + 1 > sizeof map / sizeof map[0]) return te->len;
    size_t n = shown_of(ec, PROVEN_LIT(""), v->secret, shown, sizeof shown, map);
    size_t at = editbox_shown_at(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = n }, v->size, v->x0, x);
    for (size_t i = 0; i <= te->len; ++i) if (map[i] >= at) return i;
    return te->len;
}

/* A press in a field: the caret / word / line / Shift's selection, and a drag after it. True when it was in it. */
bool editbox_pointer_down(rubraview_renderer_t *r, rubraview_editctl_t *ec, editbox_view_t *v, double x, double y, bool shift) {
    if (!pal_rect_has(v->box, x, y)) return false;
    int clicks = editbox_clicks(&v->clicks, x, y, rubraview_pal_time_now_seconds());
    editbox_press(ec, editbox_hit(r, ec, v, x), clicks, shift);
    v->dragging = clicks == 1;
    return true;
}

bool editbox_pointer_move(rubraview_renderer_t *r, rubraview_editctl_t *ec, editbox_view_t *v, double x) {
    if (!v->dragging) return false;
    rubraview_editctl_place(ec, editbox_hit(r, ec, v, x), true);
    return true;
}
