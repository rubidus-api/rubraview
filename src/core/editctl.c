/* RV-293: the one edit control's logic (include/rubraview/editctl.h). */
#include "rubraview/editctl.h"

static bool cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

static size_t prev_char(const rubraview_textedit_t *te, size_t at) {
    if (at == 0) return 0;
    at--;
    while (at > 0 && cont(te->buf[at])) at--;
    return at;
}

static size_t next_char(const rubraview_textedit_t *te, size_t at) {
    if (at >= te->len) return te->len;
    at++;
    while (at < te->len && cont(te->buf[at])) at++;
    return at;
}

/* What a character is for words: 0 a line break, 1 a space, 2 a sign, 3 a word's (letters, digits, '_', and every
   character past ASCII: Hangul, Han, kana, accented letters). */
static int kind_at(const rubraview_textedit_t *te, size_t at) {
    unsigned char c = (unsigned char)te->buf[at];
    if (c == '\n') return 0;
    if (c == ' ' || c == '\t') return 1;
    if (c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 3;
    return 2;
}

static size_t word_right(const rubraview_textedit_t *te, size_t at) {
    if (at >= te->len) return te->len;
    int k = kind_at(te, at);
    if (k == 0) return next_char(te, at);
    if (k != 1) while (at < te->len && kind_at(te, at) == k) at = next_char(te, at);
    while (at < te->len && kind_at(te, at) == 1) at = next_char(te, at);
    return at;
}

static size_t word_left(const rubraview_textedit_t *te, size_t at) {
    while (at > 0 && kind_at(te, prev_char(te, at)) == 1) at = prev_char(te, at);
    if (at == 0) return 0;
    int k = kind_at(te, prev_char(te, at));
    if (k == 0) return prev_char(te, at);
    while (at > 0 && kind_at(te, prev_char(te, at)) == k) at = prev_char(te, at);
    return at;
}

/* ---- undo ---- */

static void state_free(rubraview_editctl_state_t *s) { rubraview_free(s->text); s->text = NULL; }

static bool state_take(const rubraview_editctl_t *ec, rubraview_editctl_state_t *s) {
    s->text = (char *)rubraview_alloc(ec->te.len + 1);
    if (!s->text) return false;
    proven_sys_mem_copy(s->text, ec->te.buf, ec->te.len);
    s->text[ec->te.len] = '\0';
    s->len = ec->te.len;
    s->caret = ec->te.caret;
    s->anchor = ec->te.anchor;
    return true;
}

static void state_put(rubraview_editctl_t *ec, const rubraview_editctl_state_t *s) {
    size_t n = s->len < ec->te.cap ? s->len : ec->te.cap - 1;
    proven_sys_mem_copy(ec->te.buf, s->text, n);
    ec->te.len = n;
    ec->te.buf[n] = '\0';
    ec->te.caret = s->caret <= n ? s->caret : n;
    ec->te.anchor = s->anchor <= n ? s->anchor : n;
}

static bool push(rubraview_editctl_state_t **list, size_t *n, size_t *cap, rubraview_editctl_state_t s) {
    if (*n == RUBRAVIEW_EDITCTL_UNDO_MAX) {   /* the oldest goes */
        state_free(&(*list)[0]);
        proven_sys_mem_move(*list, *list + 1, (*n - 1) * sizeof **list);
        (*n)--;
    }
    if (*n == *cap) {
        size_t c = *cap ? *cap * 2 : 16;
        rubraview_editctl_state_t *more = (rubraview_editctl_state_t *)rubraview_realloc((proven_mem_mut_t){ .ptr = (proven_byte_t *)*list, .size = c * sizeof *more });
        if (!more) return false;
        *list = more;
        *cap = c;
    }
    (*list)[(*n)++] = s;
    return true;
}

static void clear(rubraview_editctl_state_t *list, size_t *n) {
    for (size_t i = 0; i < *n; ++i) state_free(&list[i]);
    *n = 0;
}

/* A step about to change the text: what it was kept to undo, unless it goes on the run before it. */
static void step(rubraview_editctl_t *ec, rubraview_editctl_kind_t kind) {
    bool joins = kind != RUBRAVIEW_EDITCTL_OTHER && kind == ec->last && ec->te.caret == ec->last_caret && ec->te.caret == ec->te.anchor &&
                 ec->undo_n > 0;
    clear(ec->redo, &ec->redo_n);
    if (!joins) {
        rubraview_editctl_state_t s;
        if (state_take(ec, &s) && !push(&ec->undo, &ec->undo_n, &ec->undo_cap, s)) state_free(&s);
    }
    ec->last = kind;
}

static void stepped(rubraview_editctl_t *ec) { ec->last_caret = ec->te.caret; }

static void broken(rubraview_editctl_t *ec) { ec->last = RUBRAVIEW_EDITCTL_OTHER; }

/* ---- the control ---- */

void rubraview_editctl_init(rubraview_editctl_t *ec, char *buf, size_t cap, bool lines) {
    if (!ec) return;
    *ec = (rubraview_editctl_t){ .te = rubraview_textedit_make(buf, cap), .lines = lines };
    ec->te.keep_lines = lines;
}

void rubraview_editctl_free(rubraview_editctl_t *ec) {
    if (!ec) return;
    clear(ec->undo, &ec->undo_n);
    clear(ec->redo, &ec->redo_n);
    rubraview_free(ec->undo);
    rubraview_free(ec->redo);
    ec->undo = ec->redo = NULL;
    ec->undo_cap = ec->redo_cap = 0;
}

void rubraview_editctl_set(rubraview_editctl_t *ec, proven_u8str_view_t text) {
    if (!ec) return;
    clear(ec->undo, &ec->undo_n);
    clear(ec->redo, &ec->redo_n);
    rubraview_textedit_set(&ec->te, text);
    broken(ec);
}

size_t rubraview_editctl_insert(rubraview_editctl_t *ec, proven_u8str_view_t text, rubraview_editctl_kind_t kind) {
    if (!ec || text.size == 0) return 0;
    if (ec->te.caret != ec->te.anchor) kind = RUBRAVIEW_EDITCTL_OTHER;   /* typing over a selection: a step of its own */
    step(ec, kind);
    size_t n = rubraview_textedit_insert(&ec->te, text);
    stepped(ec);
    return n;
}

static void remove_to(rubraview_editctl_t *ec, size_t a, size_t b) {
    if (b <= a) return;
    ec->te.anchor = a;
    ec->te.caret = b;
    rubraview_textedit_backspace(&ec->te);   /* the selection [a, b) */
}

void rubraview_editctl_backspace(rubraview_editctl_t *ec, bool word) {
    if (!ec) return;
    bool sel = ec->te.caret != ec->te.anchor;
    if (!sel && ec->te.caret == 0) return;
    step(ec, sel || word ? RUBRAVIEW_EDITCTL_OTHER : RUBRAVIEW_EDITCTL_ERASING);
    if (sel) rubraview_textedit_backspace(&ec->te);
    else if (word) remove_to(ec, word_left(&ec->te, ec->te.caret), ec->te.caret);
    else rubraview_textedit_backspace(&ec->te);
    stepped(ec);
}

void rubraview_editctl_delete(rubraview_editctl_t *ec, bool word) {
    if (!ec) return;
    bool sel = ec->te.caret != ec->te.anchor;
    if (!sel && ec->te.caret >= ec->te.len) return;
    step(ec, sel || word ? RUBRAVIEW_EDITCTL_OTHER : RUBRAVIEW_EDITCTL_ERASING);
    if (sel) rubraview_textedit_delete(&ec->te);
    else if (word) remove_to(ec, ec->te.caret, word_right(&ec->te, ec->te.caret));
    else rubraview_textedit_delete(&ec->te);
    stepped(ec);
}

void rubraview_editctl_cut(rubraview_editctl_t *ec) {
    if (!ec || ec->te.caret == ec->te.anchor) return;
    step(ec, RUBRAVIEW_EDITCTL_OTHER);
    rubraview_textedit_backspace(&ec->te);
    stepped(ec);
}

void rubraview_editctl_line_of(const rubraview_editctl_t *ec, size_t at, size_t *start, size_t *end) {
    size_t a = at <= ec->te.len ? at : ec->te.len, b = a;
    if (ec->lines) {
        while (a > 0 && ec->te.buf[a - 1] != '\n') a--;
        while (b < ec->te.len && ec->te.buf[b] != '\n') b++;
    } else {
        a = 0;
        b = ec->te.len;
    }
    if (start) *start = a;
    if (end) *end = b;
}

void rubraview_editctl_move(rubraview_editctl_t *ec, rubraview_editctl_move_t move, bool extend) {
    if (!ec) return;
    rubraview_textedit_t *te = &ec->te;
    size_t a = te->anchor < te->caret ? te->anchor : te->caret, b = te->anchor < te->caret ? te->caret : te->anchor;
    bool selected = a != b;
    size_t ls = 0, le = 0;
    switch (move) {
        case RUBRAVIEW_EDITCTL_LEFT:  te->caret = (!extend && selected) ? a : prev_char(te, te->caret); break;
        case RUBRAVIEW_EDITCTL_RIGHT: te->caret = (!extend && selected) ? b : next_char(te, te->caret); break;
        case RUBRAVIEW_EDITCTL_WORD_LEFT:  te->caret = word_left(te, te->caret); break;
        case RUBRAVIEW_EDITCTL_WORD_RIGHT: te->caret = word_right(te, te->caret); break;
        case RUBRAVIEW_EDITCTL_HOME:  rubraview_editctl_line_of(ec, te->caret, &ls, &le); te->caret = ls; break;
        case RUBRAVIEW_EDITCTL_END:   rubraview_editctl_line_of(ec, te->caret, &ls, &le); te->caret = le; break;
        case RUBRAVIEW_EDITCTL_START: te->caret = 0; break;
        case RUBRAVIEW_EDITCTL_FINISH: te->caret = te->len; break;
    }
    if (!extend) te->anchor = te->caret;
    broken(ec);
}

void rubraview_editctl_place(rubraview_editctl_t *ec, size_t at, bool extend) {
    if (!ec) return;
    if (at > ec->te.len) at = ec->te.len;
    while (at > 0 && at < ec->te.len && cont(ec->te.buf[at])) at--;   /* never inside a character */
    ec->te.caret = at;
    if (!extend) ec->te.anchor = at;
    broken(ec);
}

void rubraview_editctl_select_word(rubraview_editctl_t *ec, size_t at) {
    if (!ec) return;
    rubraview_textedit_t *te = &ec->te;
    if (te->len == 0) return;
    if (at >= te->len) at = prev_char(te, te->len);
    while (at > 0 && cont(te->buf[at])) at--;
    int k = kind_at(te, at);
    size_t a = at, b = next_char(te, at);
    if (k != 0) {
        while (a > 0 && kind_at(te, prev_char(te, a)) == k) a = prev_char(te, a);
        while (b < te->len && kind_at(te, b) == k) b = next_char(te, b);
    }
    te->anchor = a;
    te->caret = b;
    broken(ec);
}

void rubraview_editctl_select_line(rubraview_editctl_t *ec, size_t at) {
    if (!ec) return;
    size_t a = 0, b = 0;
    rubraview_editctl_line_of(ec, at, &a, &b);
    ec->te.anchor = a;
    ec->te.caret = b;
    broken(ec);
}

bool rubraview_editctl_can_undo(const rubraview_editctl_t *ec) { return ec && ec->undo_n > 0; }
bool rubraview_editctl_can_redo(const rubraview_editctl_t *ec) { return ec && ec->redo_n > 0; }

void rubraview_editctl_undo(rubraview_editctl_t *ec) {
    if (!rubraview_editctl_can_undo(ec)) return;
    rubraview_editctl_state_t now;
    if (state_take(ec, &now) && !push(&ec->redo, &ec->redo_n, &ec->redo_cap, now)) state_free(&now);
    rubraview_editctl_state_t *s = &ec->undo[--ec->undo_n];
    state_put(ec, s);
    state_free(s);
    broken(ec);
}

void rubraview_editctl_redo(rubraview_editctl_t *ec) {
    if (!rubraview_editctl_can_redo(ec)) return;
    rubraview_editctl_state_t now;
    if (state_take(ec, &now) && !push(&ec->undo, &ec->undo_n, &ec->undo_cap, now)) state_free(&now);
    rubraview_editctl_state_t *s = &ec->redo[--ec->redo_n];
    state_put(ec, s);
    state_free(s);
    broken(ec);
}

size_t rubraview_editctl_at_x(const size_t *offsets, const double *xs, size_t n, double x) {
    if (!offsets || !xs || n == 0) return 0;
    size_t best = 0;
    double bd = x > xs[0] ? x - xs[0] : xs[0] - x;
    for (size_t i = 1; i < n; ++i) {
        double d = x > xs[i] ? x - xs[i] : xs[i] - x;
        if (d < bd) { bd = d; best = i; }
    }
    return offsets[best];
}

double rubraview_editctl_scroll(double scroll, double caret_x, double text_width, double width, double margin) {
    double most = text_width + margin - width;
    if (most < 0.0) most = 0.0;
    if (caret_x - scroll > width - margin) scroll = caret_x - (width - margin);
    if (caret_x - scroll < margin) scroll = caret_x - margin;
    if (scroll > most) scroll = most;
    if (scroll < 0.0) scroll = 0.0;
    return scroll;
}
