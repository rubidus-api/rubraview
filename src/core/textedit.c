/*
 * A one-line text field (rename box, "Change ext"). See textedit.h.
 */
#include "rubraview/textedit.h"
#include <string.h>

static bool is_continuation(char c) {
    return ((unsigned char)c & 0xC0) == 0x80;
}

static size_t prev_char(const rubraview_textedit_t *te, size_t at) {
    if (at == 0) return 0;
    at--;
    while (at > 0 && is_continuation(te->buf[at])) at--;
    return at;
}

static size_t next_char(const rubraview_textedit_t *te, size_t at) {
    if (at >= te->len) return te->len;
    at++;
    while (at < te->len && is_continuation(te->buf[at])) at++;
    return at;
}

static void terminate(rubraview_textedit_t *te) {
    if (te->buf && te->cap > 0) te->buf[te->len] = '\0';
}

rubraview_textedit_t rubraview_textedit_make(char *buf, size_t cap) {
    rubraview_textedit_t te = { .buf = buf, .cap = cap };
    terminate(&te);
    return te;
}

void rubraview_textedit_selection_range(const rubraview_textedit_t *te, size_t *start, size_t *end) {
    size_t a = te->anchor < te->caret ? te->anchor : te->caret;
    size_t b = te->anchor < te->caret ? te->caret : te->anchor;
    if (start) *start = a;
    if (end) *end = b;
}

bool rubraview_textedit_has_selection(const rubraview_textedit_t *te) {
    return te && te->anchor != te->caret;
}

u8str_t rubraview_textedit_selection(const rubraview_textedit_t *te) {
    if (!rubraview_textedit_has_selection(te)) return (u8str_t){ .ptr = "", .len = 0 };
    size_t a = 0, b = 0;
    rubraview_textedit_selection_range(te, &a, &b);
    return (u8str_t){ .ptr = te->buf + a, .len = b - a };
}

/* Removes [a, b) and puts the caret at a. */
static void remove_range(rubraview_textedit_t *te, size_t a, size_t b) {
    if (b <= a) return;
    memmove(te->buf + a, te->buf + b, te->len - b);
    te->len -= b - a;
    te->caret = te->anchor = a;
    terminate(te);
}

static void remove_selection(rubraview_textedit_t *te) {
    size_t a = 0, b = 0;
    rubraview_textedit_selection_range(te, &a, &b);
    remove_range(te, a, b);
}

void rubraview_textedit_set(rubraview_textedit_t *te, u8str_t text) {
    if (!te || !te->buf || te->cap == 0) return;
    te->len = te->caret = te->anchor = 0;
    terminate(te);
    (void)rubraview_textedit_insert(te, text);
}

size_t rubraview_textedit_insert(rubraview_textedit_t *te, u8str_t text) {
    if (!te || !te->buf || te->cap == 0) return 0;
    if (rubraview_textedit_has_selection(te)) remove_selection(te);
    size_t room = te->cap - 1 - te->len;
    size_t n = text.len < room ? text.len : room;
    /* Never half a character: step back to the start of a cut one. */
    if (n < text.len) {
        while (n > 0 && is_continuation(text.ptr[n])) n--;
    }
    if (n == 0) return 0;
    memmove(te->buf + te->caret + n, te->buf + te->caret, te->len - te->caret);
    for (size_t i = 0; i < n; ++i) {
        char c = text.ptr[i];
        te->buf[te->caret + i] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    te->len += n;
    te->caret += n;
    te->anchor = te->caret;
    terminate(te);
    return n;
}

void rubraview_textedit_backspace(rubraview_textedit_t *te) {
    if (!te) return;
    if (rubraview_textedit_has_selection(te)) { remove_selection(te); return; }
    remove_range(te, prev_char(te, te->caret), te->caret);
}

void rubraview_textedit_delete(rubraview_textedit_t *te) {
    if (!te) return;
    if (rubraview_textedit_has_selection(te)) { remove_selection(te); return; }
    size_t at = te->caret;
    remove_range(te, at, next_char(te, at));
}

void rubraview_textedit_move(rubraview_textedit_t *te, rubraview_textedit_move_t move, bool extend) {
    if (!te) return;
    size_t a = 0, b = 0;
    rubraview_textedit_selection_range(te, &a, &b);
    bool selected = a != b;
    switch (move) {
        case RUBRAVIEW_TEXTEDIT_LEFT:
            te->caret = (!extend && selected) ? a : prev_char(te, te->caret);
            break;
        case RUBRAVIEW_TEXTEDIT_RIGHT:
            te->caret = (!extend && selected) ? b : next_char(te, te->caret);
            break;
        case RUBRAVIEW_TEXTEDIT_HOME:
            te->caret = 0;
            break;
        case RUBRAVIEW_TEXTEDIT_END:
            te->caret = te->len;
            break;
    }
    if (!extend) te->anchor = te->caret;
}

void rubraview_textedit_select_all(rubraview_textedit_t *te) {
    if (!te) return;
    te->anchor = 0;
    te->caret = te->len;
}
