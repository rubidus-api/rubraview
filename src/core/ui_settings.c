#include "rubraview/ui_settings.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * The settings window's interpreter (§3.22, D-13). Everything is in
 * character cells; nothing draws. The document says what is on a page,
 * this file says where it goes and what a key or a click does to it, and
 * the Windows side prints the lines this file writes.
 */

#define TITLE_ROWS 2      /* the page title, and a blank row under it */
#define FOOTER_ROWS 2     /* a blank row, and the buttons */
#define PAGE_LIST_TOP 2   /* the first page's row in the list */

/* ---- cells: how wide text is on a fixed-width grid ---- */

/* One code point from UTF-8; returns how many bytes it took (at least 1). */
static size_t next_code_point(const char *p, size_t len, uint32_t *out) {
    unsigned char c = (unsigned char)p[0];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (n > len) n = 1;
    uint32_t cp = n == 1 ? c : n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (size_t i = 1; i < n; ++i) cp = (cp << 6) | ((unsigned char)p[i] & 0x3Fu);
    *out = cp;
    return n;
}

/* East Asian wide characters take two cells: Hangul, CJK, fullwidth forms. */
static int cell_width(uint32_t cp) {
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6)) {
        return 2;
    }
    return 1;
}

static int32_t cells_of(u8str_t text) {
    int32_t cells = 0;
    for (size_t i = 0; i < text.len;) {
        uint32_t cp = 0;
        i += next_code_point(text.ptr + i, text.len - i, &cp);
        cells += cell_width(cp);
    }
    return cells;
}

size_t rubraview_cell_runs(u8str_t text, rubraview_cell_run_t *out, size_t capacity) {
    if (!out || capacity == 0) return 0;
    size_t count = 0;
    int32_t col = 0;
    bool open_narrow = false;
    for (size_t i = 0; i < text.len;) {
        uint32_t cp = 0;
        size_t n = next_code_point(text.ptr + i, text.len - i, &cp);
        int w = cell_width(cp);
        if (w == 1 && open_narrow) {
            out[count - 1].length += n;
        } else if (count == capacity) {
            out[count - 1].length += n;      /* out of room: the rest rides on the last run */
        } else {
            out[count++] = (rubraview_cell_run_t){ .offset = i, .length = n, .col = col };
            open_narrow = w == 1;
        }
        if (count == capacity) open_narrow = true;
        col += w;
        i += n;
    }
    return count;
}

/* Writes into a buffer by cells: text cut to fit, then spaces to fill. */
typedef struct cell_writer {
    char  *buffer;
    size_t capacity, used;
    int32_t cells;
} cell_writer_t;

static void put_bytes(cell_writer_t *w, const char *p, size_t n, int32_t cells) {
    if (w->used + n + 1 > w->capacity) return;
    memcpy(w->buffer + w->used, p, n);
    w->used += n;
    w->cells += cells;
}

/* At most `limit` cells of `text`. */
static void put_text(cell_writer_t *w, u8str_t text, int32_t limit) {
    int32_t written = 0;
    for (size_t i = 0; i < text.len;) {
        uint32_t cp = 0;
        size_t n = next_code_point(text.ptr + i, text.len - i, &cp);
        int cw = cell_width(cp);
        if (written + cw > limit) break;
        put_bytes(w, text.ptr + i, n, cw);
        written += cw;
        i += n;
    }
}

static void pad_to(cell_writer_t *w, int32_t cells) {
    while (w->cells < cells) put_bytes(w, " ", 1, 1);
}

static void repeat(cell_writer_t *w, const char *glyph, int32_t count) {
    size_t n = strlen(glyph);
    for (int32_t i = 0; i < count; ++i) put_bytes(w, glyph, n, 1);
}

static u8str_t finish(cell_writer_t *w) {
    if (w->capacity == 0) return (u8str_t){ .ptr = "", .len = 0 };
    w->buffer[w->used] = '\0';
    return (u8str_t){ .ptr = w->buffer, .len = w->used };
}

/* ---- layout ---- */

static const rubraview_settings_node_t *node_of(const rubraview_settings_view_t *view, size_t line) {
    return &view->doc->nodes[view->lines[line].node];
}

static const rubraview_setting_def_t *def_of(const rubraview_settings_view_t *view, size_t line) {
    const rubraview_settings_node_t *node = node_of(view, line);
    return node->kind == RUBRAVIEW_NODE_SETTING ? &view->doc->defs[node->setting] : NULL;
}

int32_t rubraview_settings_view_visible_rows(const rubraview_settings_view_t *view) {
    int32_t visible = view->rows - TITLE_ROWS - FOOTER_ROWS;
    return visible > 1 ? visible : 1;
}

static void layout(rubraview_settings_view_t *view) {
    const rubraview_settings_doc_t *doc = view->doc;
    view->line_count = 0;
    view->content_height = 0;

    int32_t longest_title = 0;
    for (size_t i = 0; i < doc->node_count; ++i) {
        if (doc->nodes[i].kind == RUBRAVIEW_NODE_PAGE) {
            int32_t c = cells_of(doc->nodes[i].text);
            if (c > longest_title) longest_title = c;
        }
    }
    view->list_cols = longest_title + 4;
    if (view->list_cols < 12) view->list_cols = 12;
    if (view->list_cols > view->cols / 3) view->list_cols = view->cols / 3;
    view->content_col = view->list_cols + 2;

    int32_t row = 0, longest_label = 0;
    bool first = true;
    for (size_t i = 0; i < doc->node_count && view->line_count < RUBRAVIEW_SETTINGS_VIEW_MAX_LINES; ++i) {
        const rubraview_settings_node_t *node = &doc->nodes[i];
        if (node->page != view->page || node->kind == RUBRAVIEW_NODE_PAGE) continue;

        rubraview_settings_line_t line = { .node = (int32_t)i, .height = 1 };
        switch (node->kind) {
            case RUBRAVIEW_NODE_SECTION:
                if (!first) row++;   /* a blank row before every heading but the first */
                line.kind = RUBRAVIEW_LINE_SECTION;
                break;
            case RUBRAVIEW_NODE_SETTING: {
                line.kind = RUBRAVIEW_LINE_SETTING;
                int32_t c = cells_of(doc->defs[node->setting].label);
                if (c > longest_label) longest_label = c;
                break;
            }
            case RUBRAVIEW_NODE_INFO: {
                line.kind = RUBRAVIEW_LINE_INFO;
                int32_t c = cells_of(node->text);
                if (c > longest_label) longest_label = c;
                break;
            }
            case RUBRAVIEW_NODE_PREVIEW:
                line.kind = RUBRAVIEW_LINE_PREVIEW;
                line.height = node->rows > 0 ? node->rows : 1;
                break;
            case RUBRAVIEW_NODE_TABLE:
                line.kind = RUBRAVIEW_LINE_TABLE;
                line.height = view->table_rows > 0 ? view->table_rows : RUBRAVIEW_TABLE_ROWS;
                break;
            case RUBRAVIEW_NODE_ACTION:
                line.kind = RUBRAVIEW_LINE_ACTION;
                break;
            case RUBRAVIEW_NODE_NOTE:
                line.kind = RUBRAVIEW_LINE_NOTE;
                break;
            default:
                continue;
        }
        first = false;
        line.row = row;
        row += line.height;
        view->lines[view->line_count++] = line;
    }
    view->content_height = row;

    int32_t content_width = view->cols - view->content_col - 1;
    view->label_cols = longest_label + 2;
    if (view->label_cols > (content_width * 45) / 100) view->label_cols = (content_width * 45) / 100;
    if (view->label_cols < 8) view->label_cols = 8;
}

static void keep_focus_visible(rubraview_settings_view_t *view) {
    int32_t visible = rubraview_settings_view_visible_rows(view);
    if (view->focus_line >= 0) {
        const rubraview_settings_line_t *line = &view->lines[view->focus_line];
        int32_t top = line->row, bottom = line->row + line->height;
        if (line->kind == RUBRAVIEW_LINE_TABLE) {
            /* The row in focus, and the heading with the first one. */
            top = line->row + (view->focus_row <= 1 ? 0 : view->focus_row);
            bottom = line->row + view->focus_row + 1;
        }
        if (top < view->scroll) view->scroll = top;
        if (bottom > view->scroll + visible) view->scroll = bottom - visible;
    }
    int32_t max_scroll = view->content_height - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (view->scroll > max_scroll) view->scroll = max_scroll;
    if (view->scroll < 0) view->scroll = 0;
}

/* What the focus can rest on: a setting, an action, or a row of a table. */
static bool focusable(rubraview_settings_line_kind_t kind) {
    return kind == RUBRAVIEW_LINE_SETTING || kind == RUBRAVIEW_LINE_ACTION || kind == RUBRAVIEW_LINE_TABLE;
}

/* Focus a line; a table is entered at its first row or, from below, its last. */
static void focus_on(rubraview_settings_view_t *view, int32_t line, bool from_below) {
    view->focus_line = line;
    view->focus_button = RUBRAVIEW_BUTTON_NONE;
    if (line >= 0 && view->lines[line].kind == RUBRAVIEW_LINE_TABLE) {
        view->focus_row = from_below && view->lines[line].height > 1 ? view->lines[line].height - 1 : 1;
    }
}

int32_t rubraview_settings_view_focused_table_row(const rubraview_settings_view_t *view) {
    if (!view || view->focus_line < 0 || (size_t)view->focus_line >= view->line_count) return -1;
    return view->lines[view->focus_line].kind == RUBRAVIEW_LINE_TABLE ? view->focus_row : -1;
}

const rubraview_settings_node_t *rubraview_settings_view_focused_node(const rubraview_settings_view_t *view) {
    if (!view || !view->doc || view->focus_line < 0 || (size_t)view->focus_line >= view->line_count) return NULL;
    return &view->doc->nodes[view->lines[view->focus_line].node];
}

static int32_t first_setting(const rubraview_settings_view_t *view) {
    for (size_t i = 0; i < view->line_count; ++i) {
        if (focusable(view->lines[i].kind)) return (int32_t)i;
    }
    return -1;
}

static int32_t last_setting(const rubraview_settings_view_t *view) {
    for (size_t i = view->line_count; i > 0; --i) {
        if (focusable(view->lines[i - 1].kind)) return (int32_t)(i - 1);
    }
    return -1;
}

rubraview_settings_view_t rubraview_settings_view_create(const rubraview_settings_doc_t *doc,
                                                         int32_t cols, int32_t rows) {
    rubraview_settings_view_t view = {
        .doc = doc, .cols = cols, .rows = rows, .page = 0,
        .focus_line = -1, .focus_button = RUBRAVIEW_BUTTON_NONE, .dragging_line = -1,
        .table_rows = RUBRAVIEW_TABLE_ROWS,
    };
    if (!doc || doc->page_count == 0) return view;
    layout(&view);
    focus_on(&view, first_setting(&view), false);
    if (view.focus_line < 0) view.focus_button = RUBRAVIEW_BUTTON_CLOSE;
    return view;
}

void rubraview_settings_view_resize(rubraview_settings_view_t *view, int32_t cols, int32_t rows) {
    if (!view || !view->doc) return;
    view->cols = cols;
    view->rows = rows;
    layout(view);
    keep_focus_visible(view);
}

void rubraview_settings_view_set_table_rows(rubraview_settings_view_t *view, int32_t rows) {
    if (!view || !view->doc) return;
    if (rows < 1) rows = 1;
    if (rows > RUBRAVIEW_TABLE_ROWS_MAX) rows = RUBRAVIEW_TABLE_ROWS_MAX;
    view->table_rows = rows;
    layout(view);
    if (view->focus_row >= rows) view->focus_row = rows > 1 ? rows - 1 : 1;
    keep_focus_visible(view);
}

void rubraview_settings_view_set_page(rubraview_settings_view_t *view, int32_t page) {
    if (!view || !view->doc || view->doc->page_count == 0) return;
    int32_t count = (int32_t)view->doc->page_count;
    view->page = ((page % count) + count) % count;
    view->scroll = 0;
    view->dragging_line = -1;
    layout(view);
    focus_on(view, first_setting(view), false);
    view->focus_button = view->focus_line < 0 ? RUBRAVIEW_BUTTON_CLOSE : RUBRAVIEW_BUTTON_NONE;
}

int32_t rubraview_settings_view_screen_row(const rubraview_settings_view_t *view, size_t line) {
    if (!view || line >= view->line_count) return -1;
    int32_t row = view->lines[line].row - view->scroll;
    if (row < 0 || row >= rubraview_settings_view_visible_rows(view)) return -1;
    return TITLE_ROWS + row;
}

static const char *const BUTTON_TEXT[RUBRAVIEW_BUTTON_COUNT] = { "[ Revert ]", "[ Defaults ]", "[ Close ]" };

u8str_t rubraview_settings_button_text(rubraview_settings_button_t button) {
    if (button < 0 || button >= RUBRAVIEW_BUTTON_COUNT) return (u8str_t){ .ptr = "", .len = 0 };
    return (u8str_t){ .ptr = BUTTON_TEXT[button], .len = strlen(BUTTON_TEXT[button]) };
}

void rubraview_settings_view_button_cells(const rubraview_settings_view_t *view,
                                          rubraview_settings_button_t button,
                                          int32_t *out_col, int32_t *out_width) {
    int32_t col = view->content_col;
    for (int32_t b = 0; b < RUBRAVIEW_BUTTON_COUNT; ++b) {
        int32_t width = (int32_t)strlen(BUTTON_TEXT[b]);
        if (b == (int32_t)button) {
            *out_col = col;
            *out_width = width;
            return;
        }
        col += width + 2;
    }
    *out_col = -1;
    *out_width = 0;
}

/* ---- values ---- */

static rubraview_settings_event_t nudge(rubraview_settings_view_t *view, rubraview_settings_t *settings,
                                        size_t line, int direction, double times, bool wrap) {
    const rubraview_setting_def_t *def = def_of(view, line);
    if (!def) return RUBRAVIEW_SEVENT_NONE;
    uint32_t before = settings->revision_total;
    double value = rubraview_settings_get(settings, def->section, def->key);

    switch (def->type) {
        case RUBRAVIEW_SETTING_BOOL:
            rubraview_settings_set(settings, def->section, def->key, value > 0.5 ? 0.0 : 1.0);
            break;
        case RUBRAVIEW_SETTING_CHOICE: {
            double next = value + (double)direction;
            if (wrap && next > def->max_value) next = 0.0;
            if (wrap && next < 0.0) next = def->max_value;
            rubraview_settings_set(settings, def->section, def->key, next);
            break;
        }
        case RUBRAVIEW_SETTING_INT:
        case RUBRAVIEW_SETTING_FLOAT:
            rubraview_settings_set(settings, def->section, def->key, value + (double)direction * def->step * times);
            break;
        case RUBRAVIEW_SETTING_PATH:
        default:
            return RUBRAVIEW_SEVENT_EDIT_TEXT;
    }
    return settings->revision_total != before ? RUBRAVIEW_SEVENT_CHANGED : RUBRAVIEW_SEVENT_NONE;
}

/* The slider's inner cells, for a number line: first column and count. */
static void slider_cells(const rubraview_settings_view_t *view, const rubraview_setting_def_t *def,
                         int32_t *out_col, int32_t *out_cells) {
    int32_t width = view->cols - view->content_col - 1 - view->label_cols;
    char value_text[48];
    int n = def->type == RUBRAVIEW_SETTING_FLOAT
        ? snprintf(value_text, sizeof(value_text), " %.1f", def->max_value)
        : snprintf(value_text, sizeof(value_text), " %.0f", def->max_value);
    int32_t value_cells = (n > 0 ? n : 0) + (def->unit.len > 0 ? (int32_t)def->unit.len + 1 : 0);
    int32_t inner = width - 2 - value_cells;
    if (inner < 4) inner = 4;
    *out_col = view->content_col + view->label_cols + 1;
    *out_cells = inner;
}

static rubraview_settings_event_t set_from_column(rubraview_settings_view_t *view, rubraview_settings_t *settings,
                                                  size_t line, int32_t col) {
    const rubraview_setting_def_t *def = def_of(view, line);
    if (!def || (def->type != RUBRAVIEW_SETTING_INT && def->type != RUBRAVIEW_SETTING_FLOAT)) {
        return RUBRAVIEW_SEVENT_NONE;
    }
    int32_t first = 0, cells = 0;
    slider_cells(view, def, &first, &cells);
    double fraction = cells > 1 ? (double)(col - first) / (double)(cells - 1) : 0.0;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    uint32_t before = settings->revision_total;
    rubraview_settings_set(settings, def->section, def->key,
                           def->min_value + fraction * (def->max_value - def->min_value));
    return settings->revision_total != before ? RUBRAVIEW_SEVENT_CHANGED : RUBRAVIEW_SEVENT_NONE;
}

/* ---- keys ---- */

static rubraview_settings_event_t move_focus(rubraview_settings_view_t *view, int direction) {
    if (view->focus_button != RUBRAVIEW_BUTTON_NONE) {
        if (direction < 0) {
            int32_t last = last_setting(view);
            if (last < 0) return RUBRAVIEW_SEVENT_NONE;
            focus_on(view, last, true);
            keep_focus_visible(view);
            return RUBRAVIEW_SEVENT_MOVED;
        }
        return RUBRAVIEW_SEVENT_NONE;
    }
    const rubraview_settings_line_t *here = view->focus_line >= 0 ? &view->lines[view->focus_line] : NULL;
    if (here && here->kind == RUBRAVIEW_LINE_TABLE) {
        int32_t next = view->focus_row + direction;
        if (next >= 1 && next < here->height) {
            view->focus_row = next;
            keep_focus_visible(view);
            return RUBRAVIEW_SEVENT_MOVED;
        }
    }
    for (int32_t i = view->focus_line + direction; i >= 0 && i < (int32_t)view->line_count; i += direction) {
        if (!focusable(view->lines[i].kind)) continue;
        focus_on(view, i, direction < 0);
        keep_focus_visible(view);
        return RUBRAVIEW_SEVENT_MOVED;
    }
    if (direction > 0) {
        /* Past the last setting, the buttons. */
        view->focus_line = -1;
        view->focus_button = RUBRAVIEW_BUTTON_REVERT;
        view->scroll = view->content_height;   /* the end of the page, clamped below */
        keep_focus_visible(view);
        return RUBRAVIEW_SEVENT_MOVED;
    }
    return RUBRAVIEW_SEVENT_NONE;
}

static rubraview_settings_event_t press_button(rubraview_settings_button_t button) {
    switch (button) {
        case RUBRAVIEW_BUTTON_REVERT:   return RUBRAVIEW_SEVENT_REVERT;
        case RUBRAVIEW_BUTTON_DEFAULTS: return RUBRAVIEW_SEVENT_DEFAULTS;
        case RUBRAVIEW_BUTTON_CLOSE:    return RUBRAVIEW_SEVENT_CLOSE;
        default:                        return RUBRAVIEW_SEVENT_NONE;
    }
}

rubraview_settings_event_t rubraview_settings_view_key(rubraview_settings_view_t *view,
                                                        rubraview_settings_t *settings,
                                                        rubraview_settings_key_t key) {
    if (!view || !view->doc || !settings) return RUBRAVIEW_SEVENT_NONE;
    bool on_button = view->focus_button != RUBRAVIEW_BUTTON_NONE;

    switch (key) {
        case RUBRAVIEW_SKEY_ESCAPE:
            return RUBRAVIEW_SEVENT_CLOSE;
        case RUBRAVIEW_SKEY_DELETE: {
            if (on_button || view->focus_line < 0) return RUBRAVIEW_SEVENT_NONE;
            if (view->lines[view->focus_line].kind == RUBRAVIEW_LINE_TABLE) return RUBRAVIEW_SEVENT_TABLE_CLEAR;
            const rubraview_setting_def_t *def = def_of(view, (size_t)view->focus_line);
            return def && def->type == RUBRAVIEW_SETTING_PATH ? RUBRAVIEW_SEVENT_CLEAR_TEXT : RUBRAVIEW_SEVENT_NONE;
        }
        case RUBRAVIEW_SKEY_TAB:
            rubraview_settings_view_set_page(view, view->page + 1);
            return RUBRAVIEW_SEVENT_MOVED;
        case RUBRAVIEW_SKEY_SHIFT_TAB:
            rubraview_settings_view_set_page(view, view->page - 1);
            return RUBRAVIEW_SEVENT_MOVED;
        case RUBRAVIEW_SKEY_UP:
            return move_focus(view, -1);
        case RUBRAVIEW_SKEY_DOWN:
            return move_focus(view, +1);
        case RUBRAVIEW_SKEY_HOME:
        case RUBRAVIEW_SKEY_END: {
            int32_t target = key == RUBRAVIEW_SKEY_HOME ? first_setting(view) : last_setting(view);
            if (target < 0) return RUBRAVIEW_SEVENT_NONE;
            focus_on(view, target, key == RUBRAVIEW_SKEY_END);
            keep_focus_visible(view);
            return RUBRAVIEW_SEVENT_MOVED;
        }
        case RUBRAVIEW_SKEY_LEFT:
        case RUBRAVIEW_SKEY_RIGHT: {
            int direction = key == RUBRAVIEW_SKEY_LEFT ? -1 : +1;
            if (on_button) {
                int32_t next = view->focus_button + direction;
                if (next < 0 || next >= RUBRAVIEW_BUTTON_COUNT) return RUBRAVIEW_SEVENT_NONE;
                view->focus_button = next;
                return RUBRAVIEW_SEVENT_MOVED;
            }
            if (view->focus_line < 0) return RUBRAVIEW_SEVENT_NONE;
            const rubraview_setting_def_t *def = def_of(view, (size_t)view->focus_line);
            if (!def || def->type == RUBRAVIEW_SETTING_PATH) return RUBRAVIEW_SEVENT_NONE;
            return nudge(view, settings, (size_t)view->focus_line, direction, 1.0, false);
        }
        case RUBRAVIEW_SKEY_PAGE_UP:
        case RUBRAVIEW_SKEY_PAGE_DOWN:
            if (on_button || view->focus_line < 0 || !def_of(view, (size_t)view->focus_line)) return RUBRAVIEW_SEVENT_NONE;
            return nudge(view, settings, (size_t)view->focus_line,
                         key == RUBRAVIEW_SKEY_PAGE_DOWN ? -1 : +1, 10.0, false);
        case RUBRAVIEW_SKEY_SPACE:
        case RUBRAVIEW_SKEY_ENTER:
            if (on_button) return press_button((rubraview_settings_button_t)view->focus_button);
            if (view->focus_line < 0) return RUBRAVIEW_SEVENT_NONE;
            if (view->lines[view->focus_line].kind == RUBRAVIEW_LINE_ACTION) return RUBRAVIEW_SEVENT_ACTION;
            if (view->lines[view->focus_line].kind == RUBRAVIEW_LINE_TABLE) return RUBRAVIEW_SEVENT_TABLE_EDIT;
            return nudge(view, settings, (size_t)view->focus_line, +1, 1.0, true);
    }
    return RUBRAVIEW_SEVENT_NONE;
}

/* ---- the pointer ---- */

rubraview_settings_event_t rubraview_settings_view_press(rubraview_settings_view_t *view,
                                                          rubraview_settings_t *settings,
                                                          int32_t col, int32_t row) {
    if (!view || !view->doc || !settings) return RUBRAVIEW_SEVENT_NONE;

    if (col < view->list_cols) {
        int32_t page = row - PAGE_LIST_TOP;
        if (page < 0 || page >= (int32_t)view->doc->page_count) return RUBRAVIEW_SEVENT_NONE;
        if (page == view->page) return RUBRAVIEW_SEVENT_NONE;
        rubraview_settings_view_set_page(view, page);
        return RUBRAVIEW_SEVENT_MOVED;
    }

    if (row == view->rows - 1) {
        for (int32_t b = 0; b < RUBRAVIEW_BUTTON_COUNT; ++b) {
            int32_t first = 0, width = 0;
            rubraview_settings_view_button_cells(view, (rubraview_settings_button_t)b, &first, &width);
            if (col >= first && col < first + width) {
                view->focus_line = -1;
                view->focus_button = b;
                return press_button((rubraview_settings_button_t)b);
            }
        }
        return RUBRAVIEW_SEVENT_NONE;
    }

    int32_t content_row = row - TITLE_ROWS;
    if (content_row < 0 || content_row >= rubraview_settings_view_visible_rows(view)) return RUBRAVIEW_SEVENT_NONE;
    content_row += view->scroll;
    for (size_t i = 0; i < view->line_count; ++i) {
        const rubraview_settings_line_t *line = &view->lines[i];
        if (content_row < line->row || content_row >= line->row + line->height) continue;
        if (!focusable(line->kind)) return RUBRAVIEW_SEVENT_NONE;
        if (line->kind == RUBRAVIEW_LINE_TABLE) {
            int32_t k = content_row - line->row;
            if (k == 0) return RUBRAVIEW_SEVENT_NONE;           /* the heading */
            if (view->focus_line == (int32_t)i && view->focus_row == k) return RUBRAVIEW_SEVENT_TABLE_EDIT;
            view->focus_line = (int32_t)i;
            view->focus_button = RUBRAVIEW_BUTTON_NONE;
            view->focus_row = k;
            return RUBRAVIEW_SEVENT_MOVED;
        }
        if (line->kind == RUBRAVIEW_LINE_ACTION) {
            view->focus_line = (int32_t)i;
            view->focus_button = RUBRAVIEW_BUTTON_NONE;
            return RUBRAVIEW_SEVENT_ACTION;
        }

        bool moved = view->focus_line != (int32_t)i;
        view->focus_line = (int32_t)i;
        view->focus_button = RUBRAVIEW_BUTTON_NONE;
        if (col < view->content_col + view->label_cols) return moved ? RUBRAVIEW_SEVENT_MOVED : RUBRAVIEW_SEVENT_NONE;

        const rubraview_setting_def_t *def = def_of(view, i);
        switch (def->type) {
            case RUBRAVIEW_SETTING_BOOL:
                return nudge(view, settings, i, +1, 1.0, true);
            case RUBRAVIEW_SETTING_CHOICE: {
                /* The left half of `< word >` goes back, the right half on. */
                int32_t middle = view->content_col + view->label_cols +
                                 (view->cols - view->content_col - 1 - view->label_cols) / 4;
                return nudge(view, settings, i, col < middle ? -1 : +1, 1.0, true);
            }
            case RUBRAVIEW_SETTING_INT:
            case RUBRAVIEW_SETTING_FLOAT: {
                view->dragging_line = (int32_t)i;
                rubraview_settings_event_t e = set_from_column(view, settings, i, col);
                return e == RUBRAVIEW_SEVENT_NONE && moved ? RUBRAVIEW_SEVENT_MOVED : e;
            }
            case RUBRAVIEW_SETTING_PATH:
            default:
                return RUBRAVIEW_SEVENT_EDIT_TEXT;
        }
    }
    return RUBRAVIEW_SEVENT_NONE;
}

rubraview_settings_event_t rubraview_settings_view_drag(rubraview_settings_view_t *view,
                                                         rubraview_settings_t *settings,
                                                         int32_t col) {
    if (!view || !settings || view->dragging_line < 0) return RUBRAVIEW_SEVENT_NONE;
    return set_from_column(view, settings, (size_t)view->dragging_line, col);
}

void rubraview_settings_view_release(rubraview_settings_view_t *view) {
    if (view) view->dragging_line = -1;
}

rubraview_settings_event_t rubraview_settings_view_scroll(rubraview_settings_view_t *view, int32_t rows) {
    if (!view) return RUBRAVIEW_SEVENT_NONE;
    int32_t before = view->scroll;
    view->scroll += rows;
    int32_t focus = view->focus_line;
    view->focus_line = -1;              /* scrolling does not drag the focus along */
    keep_focus_visible(view);
    view->focus_line = focus;
    return view->scroll != before ? RUBRAVIEW_SEVENT_MOVED : RUBRAVIEW_SEVENT_NONE;
}

/* ---- text ---- */

u8str_t rubraview_settings_page_text(const rubraview_settings_view_t *view, int32_t page,
                                     char *buffer, size_t capacity) {
    cell_writer_t w = { .buffer = buffer, .capacity = capacity };
    if (!view || !view->doc || capacity == 0) return finish(&w);
    put_text(&w, page == view->page ? U8(" > ") : U8("   "), 3);
    put_text(&w, rubraview_settings_page_title((size_t)page), view->list_cols - 4);
    pad_to(&w, view->list_cols - 1);
    return finish(&w);
}

u8str_t rubraview_settings_line_text(const rubraview_settings_view_t *view,
                                      const rubraview_settings_t *settings,
                                      const rubraview_settings_sources_t *sources,
                                      size_t line, int32_t table_index,
                                      char *buffer, size_t capacity) {
    cell_writer_t w = { .buffer = buffer, .capacity = capacity };
    if (!view || !view->doc || line >= view->line_count || capacity == 0) return finish(&w);
    const rubraview_settings_node_t *node = node_of(view, line);
    int32_t width = view->cols - view->content_col - 1;
    if (width < 1) return finish(&w);

    switch (view->lines[line].kind) {
        case RUBRAVIEW_LINE_SECTION:
            repeat(&w, "\xE2\x94\x80", 2);                 /* ── */
            put_bytes(&w, " ", 1, 1);
            put_text(&w, node->text, width - 4);
            put_bytes(&w, " ", 1, 1);
            repeat(&w, "\xE2\x94\x80", width - w.cells);
            break;

        case RUBRAVIEW_LINE_INFO: {
            put_text(&w, node->text, view->label_cols - 1);
            pad_to(&w, view->label_cols);
            char value[256];
            size_t n = sources && sources->info ? sources->info(sources->user, node->name, value, sizeof(value)) : 0;
            put_text(&w, n > 0 ? (u8str_t){ .ptr = value, .len = n } : U8("-"), width - w.cells);
            break;
        }

        case RUBRAVIEW_LINE_NOTE:
            put_text(&w, node->text, width);
            break;

        case RUBRAVIEW_LINE_TABLE: {
            char row[512];
            size_t n = sources && sources->table_row
                ? sources->table_row(sources->user, node->name, (size_t)(table_index < 0 ? 0 : table_index), row, sizeof(row))
                : 0;
            put_text(&w, (u8str_t){ .ptr = row, .len = n }, width);
            break;
        }

        case RUBRAVIEW_LINE_PREVIEW:
            break;   /* the viewer paints it */

        case RUBRAVIEW_LINE_ACTION:
            pad_to(&w, view->label_cols);
            put_bytes(&w, "<", 1, 1);
            put_bytes(&w, " ", 1, 1);
            put_text(&w, node->text, width - w.cells - 2);
            put_bytes(&w, " ", 1, 1);
            put_bytes(&w, ">", 1, 1);
            break;

        case RUBRAVIEW_LINE_SETTING: {
            const rubraview_setting_def_t *def = &view->doc->defs[node->setting];
            put_text(&w, def->label, view->label_cols - 1);
            pad_to(&w, view->label_cols);
            int32_t room = width - view->label_cols;
            double value = settings ? rubraview_settings_get(settings, def->section, def->key) : def->default_value;

            switch (def->type) {
                case RUBRAVIEW_SETTING_BOOL:
                    put_text(&w, value > 0.5 ? U8("[x] on") : U8("[ ] off"), room);
                    break;
                case RUBRAVIEW_SETTING_CHOICE: {
                    int32_t index = (int32_t)value;
                    const char *word = index >= 0 && index < def->choice_count ? def->choices[index] : "?";
                    char text[96];
                    int n = snprintf(text, sizeof(text), "< %s >  %d/%d", word, index + 1, def->choice_count);
                    put_text(&w, (u8str_t){ .ptr = text, .len = n > 0 ? (size_t)n : 0 }, room);
                    break;
                }
                case RUBRAVIEW_SETTING_INT:
                case RUBRAVIEW_SETTING_FLOAT: {
                    int32_t first = 0, cells = 0;
                    slider_cells(view, def, &first, &cells);
                    double span = def->max_value - def->min_value;
                    int32_t filled = span > 0.0 ? (int32_t)floor((value - def->min_value) / span * (double)cells + 0.5) : 0;
                    if (filled < 0) filled = 0;
                    if (filled > cells) filled = cells;
                    put_bytes(&w, "[", 1, 1);
                    repeat(&w, "#", filled);
                    repeat(&w, "-", cells - filled);
                    put_bytes(&w, "]", 1, 1);
                    char text[64];
                    int n = def->type == RUBRAVIEW_SETTING_FLOAT
                        ? snprintf(text, sizeof(text), " %.1f", value)
                        : snprintf(text, sizeof(text), " %.0f", value);
                    put_text(&w, (u8str_t){ .ptr = text, .len = n > 0 ? (size_t)n : 0 }, width - w.cells);
                    if (def->unit.len > 0) {
                        put_bytes(&w, " ", 1, 1);
                        put_text(&w, def->unit, width - w.cells);
                    }
                    break;
                }
                case RUBRAVIEW_SETTING_PATH:
                default: {
                    u8str_t text = settings ? rubraview_settings_get_text(settings, def->section, def->key)
                                            : (u8str_t){ .ptr = "", .len = 0 };
                    put_bytes(&w, "[", 1, 1);
                    int32_t inner = room - 2;
                    put_text(&w, text.len > 0 ? text : U8("(not set)"), inner);
                    pad_to(&w, view->label_cols + room - 1);
                    put_bytes(&w, "]", 1, 1);
                    break;
                }
            }
            break;
        }
    }
    pad_to(&w, width);
    return finish(&w);
}
