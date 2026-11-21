/*
 * RV-310 (an outside review, 2026-11-19) and RV-311: Settings › Keys — the table by function and by key, the keys by
 * keyboard row, the keyboard drawn with its dots — taken out of settingswin.c as they were.
 */
#include "app.h"
#include "rubraview/keyboard.h"
#include "settingslook.h"
#include "settings_keys.h"

/* RV-184: the Keys table, by function (headings and bindings) or by key */
bool g_keys_by_key;
bool g_keys_by_row;                       /* RV-303 (RFC-0010 §4.8): a keyboard row's heading, then what each of its keys does on one screen */
rubraview_keyrow_entry_t g_key_rows[KEY_ROWS_MAX];
bool g_keys_layout;                       /* RV-217: the keyboard drawn, a dot a kind of binding on each key */
int32_t g_keys_dot_hover = -1;   /* RV-252: the circle the pointer rests on, drawn lit */
static int32_t g_key_row[KEY_ROWS_MAX];          /* by function: a binding, or -(context + 1) for a heading */
static key_combo_row_t g_key_combo[KEY_ROWS_MAX];  /* by key: every key of every binding, sorted (RV-193) */
size_t g_key_row_count;

/* RV-303: the screen Keys › by keyboard row shows — everywhere (the common keys alone), a picture, a film, music, a text */
static rubraview_key_mode_t keys_row_mode(app_state_t *app) {
    static const rubraview_key_mode_t MODES[] = { RUBRAVIEW_KEY_MODE_COUNT, RUBRAVIEW_KEY_MODE_PICTURE, RUBRAVIEW_KEY_MODE_FILM,
                                                  RUBRAVIEW_KEY_MODE_MUSIC, RUBRAVIEW_KEY_MODE_TEXT };
    double at = rubraview_settings_get(&app->settings, PROVEN_LIT("keys"), PROVEN_LIT("row_screen"));
    int i = at >= 0.0 && at < 5.0 ? (int)at : 1;
    return MODES[i];
}

/* Its name, as the Keys page's headings by function call it */
static const char *keys_row_screen_words(app_state_t *app) {
    static const char *const CONTEXT_OF[] = { [RUBRAVIEW_KEY_MODE_PICTURE] = "image", [RUBRAVIEW_KEY_MODE_FILM] = "video",
                                              [RUBRAVIEW_KEY_MODE_MUSIC] = "audio", [RUBRAVIEW_KEY_MODE_TEXT] = "text", [RUBRAVIEW_KEY_MODE_COUNT] = "" };
    rubraview_key_mode_t m = keys_row_mode(app);
    const char *name = CONTEXT_OF[m] ? CONTEXT_OF[m] : "";
    const rubraview_key_context_info_t *info = rubraview_keymap_context_info(proven_u8str_view_from_cstr(name));
    return info ? rubraview_tr(info->heading) : "";
}

/* RV-217 (owner, 2026-10-31): the keyboard over the Keys table's rows; a dot a kind of binding, always in the
   same place on a key: alone red, Shift yellow, Ctrl blue, Alt green, two or more modifiers purple. */
static const uint32_t KEYMARK_COLOR[RUBRAVIEW_KEYMARK_KINDS] = { 0xFFE5484Du, 0xFFF2C230u, 0xFF3E8BF0u, 0xFF3DBE6Au, 0xFFA66BE8u };

/* The Keys table's text: a row of whichever way it is shown (settingswin.c's table rows, for the "keymap" source). */
size_t keys_table_row(app_state_t *app, size_t index, char *buffer, size_t capacity) {
    if (g_keys_layout) {   /* RV-217: the rows are the keyboard's room; drawn by keys_layout_draw */
        int n = 0;
        if (index == 0) n = rubraview_fmt(buffer, capacity, "{}", PROVEN_ARG(T("the keyboard: a dot for each kind of key that does something (click a key: what it does)")));
        else if (index == KEYS_LAYOUT_ROWS) {
            proven_u8str_view_t off[24];
            size_t k = rubraview_keyboard_off_layout(&app->keymap, off, 24);
            if (k > 0) {
                n = rubraview_fmt(buffer, capacity, "{}", PROVEN_ARG(T("not on this keyboard:")));
                for (size_t i = 0; i < k && n > 0 && (size_t)n < capacity; ++i)
                    n += rubraview_fmt(buffer + n, capacity - (size_t)n, " {}", PROVEN_ARG(rubraview_bytes_view((const char *)off[i].ptr, (int)off[i].size)));
            }
        }
        return n > 0 ? (size_t)n < capacity ? (size_t)n : capacity - 1 : 0;
    }
    if (g_keys_by_row) {   /* RV-303 */
        int n = 0;
        if (index == 0) {
            n = rubraview_fmt(buffer, capacity, T("on {}: what each key does  (Enter: another key for it, Delete: take it off)"), PROVEN_ARG(keys_row_screen_words(app)));
            proven_u8str_view_t find = rubraview_settings_get_text(&app->settings, PROVEN_LIT("keys"), PROVEN_LIT("row_find"));
            if (n > 0 && find.size > 0 && (size_t)n < capacity)   /* RFC-0010 §4.8: a search kept in the settings is said */
                n += rubraview_fmt(buffer + n, capacity - (size_t)n, T("  finding: {}"), PROVEN_ARG(find));
        } else if (index <= g_key_row_count) {
            const rubraview_keyrow_entry_t *e = &g_key_rows[index - 1];
            if (e->heading) {
                n = rubraview_fmt(buffer, capacity, "-- {}", PROVEN_ARG(rubraview_tr(rubraview_keyboard_row_heading(e->row))));
                size_t count = 0;
                const rubraview_keycap_t *caps = rubraview_keyboard_caps(&count);
                bool first = true;   /* the row's keys as written on them, in their order */
                for (size_t k = 0; k < count && n > 0 && (size_t)n + 16 < capacity; ++k) {
                    if (!caps[k].name || rubraview_keyboard_row_of(k) != e->row) continue;
                    n += rubraview_fmt(buffer + n, capacity - (size_t)n, "{}{}", PROVEN_ARG(first ? ": " : "  "), PROVEN_ARG(caps[k].label));
                    first = false;
                }
                if (n > 0 && (size_t)n + 4 < capacity) n += rubraview_fmt(buffer + n, capacity - (size_t)n, " --");
                if (n > 0 && e->left_out > 0 && (size_t)n + 4 < capacity)   /* RFC-0010 §4.8: what the filter took away */
                    n += rubraview_fmt(buffer + n, capacity - (size_t)n, T("  ({} more not shown)"), PROVEN_ARG((int64_t)e->left_out));
            } else {
                char one[64];
                proven_u8str_view_t named = rubraview_key_combo_format(one, sizeof(one), e->combo);
                int32_t pad = 18 - (int32_t)named.size;
                n = rubraview_fmt(buffer, capacity, "  {}{} ", PROVEN_ARG(named), PROVEN_ARG(rubraview_spaces(pad > 0 ? (int)pad : 0)));
                if (n > 0 && app->key_capture == (int32_t)index) {
                    n += rubraview_fmt(buffer + n, capacity - (size_t)n, "{}", PROVEN_ARG(T("> press the key to use instead (Esc: leave it as it is)")));
                } else if (n > 0 && e->binding < 0) {
                    n += rubraview_fmt(buffer + n, capacity - (size_t)n, "{}", PROVEN_ARG(T("not set")));
                } else if (n > 0) {
                    const rubraview_key_binding_t *b = &app->keymap.bindings[e->binding];
                    const char *words = rubraview_action_words(b->action);
                    n += words ? rubraview_fmt(buffer + n, capacity - (size_t)n, "{}", PROVEN_ARG(rubraview_tr(words)))
                               : rubraview_fmt(buffer + n, capacity - (size_t)n, "{}", PROVEN_ARG(b->action));
                    /* RFC-0010 §4.8: a common key named so on a screen's own list */
                    if (n > 0 && (size_t)n < capacity && b->context.size == 0 && keys_row_mode(app) != RUBRAVIEW_KEY_MODE_COUNT)
                        n += rubraview_fmt(buffer + n, capacity - (size_t)n, "  [{}]", PROVEN_ARG(T("everywhere")));
                    if (n > 0 && (size_t)n < capacity && e->hidden >= 0) {
                        const rubraview_key_binding_t *h = &app->keymap.bindings[e->hidden];
                        const char *hw = rubraview_action_words(h->action);
                        n += hw ? rubraview_fmt(buffer + n, capacity - (size_t)n, T("  (hides: {})"), PROVEN_ARG(rubraview_tr(hw)))
                                : rubraview_fmt(buffer + n, capacity - (size_t)n, T("  (hides: {})"), PROVEN_ARG(h->action));
                    }
                }
            }
        }
        return n > 0 ? (size_t)n < capacity ? (size_t)n : capacity - 1 : 0;
    }
    if (index == 0) {
        int32_t pad_key = 20 - rubraview_settings_text_cells(proven_u8str_view_from_cstr(T("key")));
        int32_t pad = 14 - rubraview_settings_text_cells(proven_u8str_view_from_cstr(T("where")));
        int n = g_keys_by_key ? rubraview_fmt(buffer, capacity, "{}{} {}{} {}", PROVEN_ARG(T("key")), PROVEN_ARG(rubraview_spaces((int)pad_key)), PROVEN_ARG(T("where")),
                                              PROVEN_ARG(rubraview_spaces((int)pad)), PROVEN_ARG(T("what it does  (Show by function to change them)")))
                              : rubraview_fmt(buffer, capacity, "{:<24} {}", PROVEN_ARG(T("action")), PROVEN_ARG(T("keys  (Enter adds a key, Delete takes the last off)")));
        return n > 0 ? (size_t)n < capacity ? (size_t)n : capacity - 1 : 0;
    }
    if (index > g_key_row_count) return 0;
    if (g_keys_by_key) {   /* RV-193: a key and what it does in one context; the next context's on the next row, the key left blank */
        key_combo_row_t row = g_key_combo[index - 1];
        const rubraview_key_binding_t *o = &app->keymap.bindings[row.binding];
        rubraview_key_combo_t combo = o->combos[row.combo];
        bool again = index >= 2 && rubraview_key_combo_equal(app->keymap.bindings[g_key_combo[index - 2].binding].combos[g_key_combo[index - 2].combo], combo);
        char one[64];
        proven_u8str_view_t named = again ? PROVEN_LIT("") : rubraview_key_combo_format(one, sizeof(one), combo);
        const rubraview_key_context_info_t *info = rubraview_keymap_context_info(o->context);
        const char *where = info ? rubraview_tr(info->heading) : T("other");
        const char *words = rubraview_action_words(o->action);
        /* the context's column by cells, not bytes: a Korean heading is two cells a letter */
        int32_t pad = 14 - rubraview_settings_text_cells(proven_u8str_view_from_cstr(where));
        int n = words ? rubraview_fmt(buffer, capacity, "{:<20} {}{} {}", PROVEN_ARG(named), PROVEN_ARG(where), PROVEN_ARG(rubraview_spaces((int)pad)),
                                      PROVEN_ARG(rubraview_tr(words)))
                      : rubraview_fmt(buffer, capacity, "{:<20} {}{} {}", PROVEN_ARG(named), PROVEN_ARG(where), PROVEN_ARG(rubraview_spaces((int)pad)),
                                      PROVEN_ARG(o->action));
        return n > 0 ? (size_t)n < capacity ? (size_t)n : capacity - 1 : 0;
    }
    int32_t at = g_key_row[index - 1];
    if (at < 0) {   /* RV-184: a context's heading, with when it is asked */
        size_t count = 0;
        const rubraview_key_context_info_t *all = rubraview_keymap_contexts(&count);
        size_t c = (size_t)(-at - 1);
        int n = c < count ? rubraview_fmt(buffer, capacity, "-- {} ({}) --", PROVEN_ARG(rubraview_tr(all[c].heading)), PROVEN_ARG(rubraview_tr(all[c].when)))
                          : rubraview_fmt(buffer, capacity, "-- {} --", PROVEN_ARG(T("other")));
        return n > 0 ? (size_t)n < capacity ? (size_t)n : capacity - 1 : 0;
    }
    const rubraview_key_binding_t *b = &app->keymap.bindings[at];
    char keys[160];
    size_t used = 0;
    keys[0] = '\0';
    if (app->key_capture == (int32_t)index) {
        used = (size_t)rubraview_fmt(keys, sizeof(keys), T("> press a key (Esc: leave it)"));
    } else {
        for (size_t c = 0; c < b->combo_count && used + 32 < sizeof(keys); ++c) {
            char one[64];
            proven_u8str_view_t text = rubraview_key_combo_format(one, sizeof(one), b->combos[c]);
            int n = rubraview_fmt(keys + used, sizeof(keys) - used, "{}{}", PROVEN_ARG(c ? ", " : ""), PROVEN_ARG(rubraview_bytes_view(text.ptr, (int)text.size)));
            if (n > 0) used += (size_t)n;
        }
        /* §3.22.2: a key another action also claims where the two meet —
           only one of them is ever reached. Named, not fixed (D-14). */
        for (size_t i = 0; i < app->keymap.count && used + 40 < sizeof(keys); ++i) {
            const rubraview_key_binding_t *o = &app->keymap.bindings[i];
            if (i == (size_t)at || !rubraview_keymap_contexts_meet(o->context, b->context)) continue;
            bool clash = false;
            for (size_t x = 0; x < b->combo_count && !clash; ++x)
                for (size_t y = 0; y < o->combo_count && !clash; ++y)
                    clash = rubraview_key_combo_equal(b->combos[x], o->combos[y]);
            if (!clash) continue;
            int n = rubraview_fmt(keys + used, sizeof(keys) - used, T("  ! also {}"), PROVEN_ARG(rubraview_bytes_view(o->action.ptr, (int)o->action.size)));
            if (n > 0) used += (size_t)n;
            break;
        }
        if (used >= sizeof(keys)) used = sizeof(keys) - 1;
        if (b->combo_count == 0 && used == 0) used = (size_t)rubraview_fmt(keys, sizeof(keys), T("(no key)"));
    }
    int n = rubraview_fmt(buffer, capacity, "  {:<22} {}", PROVEN_ARG(rubraview_bytes_view(b->action.ptr, (int)b->action.size)), PROVEN_ARG(rubraview_bytes_view(keys, (int)used)));
    if (n <= 0) return 0;
    return (size_t)n < capacity ? (size_t)n : capacity - 1;
}

/* RV-184 (D-219): the Keys table's rows — by function, a heading a context (in the order of
   rubraview_keymap_contexts, then any other) with its actions under it; by key, each key once. Built again
   whenever the keymap or the way it is shown changes. */
static const rubraview_keymap_t *g_sort_keymap;
static rubraview_key_order_t g_sort_order;

static size_t context_place(proven_u8str_view_t context) {
    size_t count = 0;
    const rubraview_key_context_info_t *all = rubraview_keymap_contexts(&count);
    for (size_t c = 0; c < count; ++c) if (proven_u8str_view_eq(context, proven_u8str_view_from_cstr(all[c].name))) return c;
    return count;
}

static int key_row_compare(const void *pa, const void *pb) {
    const key_combo_row_t *a = pa, *b = pb;
    const rubraview_key_binding_t *ba = &g_sort_keymap->bindings[a->binding], *bb = &g_sort_keymap->bindings[b->binding];
    int k = rubraview_key_combo_order(ba->combos[a->combo], bb->combos[b->combo], g_sort_order);
    if (k != 0) return k;
    size_t ca = context_place(ba->context), cb = context_place(bb->context);
    if (ca != cb) return ca < cb ? -1 : 1;
    return a->binding < b->binding ? -1 : a->binding > b->binding ? 1 : 0;
}

/* RFC-0010 §4.8: a key's line found by its name, or its action's words in the reader's language or in English */
static bool keys_row_found(const rubraview_keyrow_entry_t *e, void *ctx) {
    app_state_t *app = ctx;
    proven_u8str_view_t find = rubraview_settings_get_text(&app->settings, PROVEN_LIT("keys"), PROVEN_LIT("row_find"));
    char one[64];
    if (rubraview_text_has(rubraview_key_combo_format(one, sizeof(one), e->combo), find)) return true;
    if (e->binding < 0) return false;
    const rubraview_key_binding_t *b = &app->keymap.bindings[e->binding];
    const char *words = rubraview_action_words(b->action);
    if (words && (rubraview_text_has(proven_u8str_view_from_cstr(words), find) ||
                  rubraview_text_has(proven_u8str_view_from_cstr(rubraview_tr(words)), find))) return true;
    return rubraview_text_has(b->action, find);
}

void keymap_table_refresh(app_state_t *app) {
    g_key_row_count = 0;
    if (g_keys_by_row) {   /* RV-303 */
        g_key_row_count = rubraview_keymap_rows(&app->keymap, keys_row_mode(app), g_key_rows, KEY_ROWS_MAX);
        /* RFC-0010 §4.8: which combinations, and the letters looked for */
        int mods = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("keys"), PROVEN_LIT("row_mods")));
        proven_u8str_view_t find = rubraview_settings_get_text(&app->settings, PROVEN_LIT("keys"), PROVEN_LIT("row_find"));
        g_key_row_count = rubraview_keyrows_filter(g_key_rows, g_key_row_count, mods, find.size > 0 ? keys_row_found : NULL, app);
        rubraview_settings_view_set_table_rows(&app->settings_view, (int32_t)g_key_row_count + 1);
        return;
    }
    if (g_keys_layout) {   /* RV-217 */
        g_key_row_count = KEYS_LAYOUT_ROWS;
        rubraview_settings_view_set_table_rows(&app->settings_view, KEYS_LAYOUT_ROWS + 1);
        return;
    }
    if (g_keys_by_key) {
        /* RV-193 (owner, 2026-10-30): every key of every context, a key's family together (A, Shift+A, Ctrl+A, ...),
           the keys as the keyboard lies or A-Z (Keys › order); one key's contexts in the order of the headings */
        for (size_t i = 0; i < app->keymap.count; ++i)
            for (size_t c = 0; c < app->keymap.bindings[i].combo_count && g_key_row_count < KEY_ROWS_MAX; ++c)
                g_key_combo[g_key_row_count++] = (key_combo_row_t){ .binding = i, .combo = c };
        g_sort_keymap = &app->keymap;
        g_sort_order = rubraview_settings_get(&app->settings, PROVEN_LIT("keys"), PROVEN_LIT("key_order")) > 0.5 ? RUBRAVIEW_KEY_ORDER_ALPHABET
                                                                                                                : RUBRAVIEW_KEY_ORDER_KEYBOARD;
        qsort(g_key_combo, g_key_row_count, sizeof(g_key_combo[0]), key_row_compare);
    } else {
        size_t count = 0;
        const rubraview_key_context_info_t *all = rubraview_keymap_contexts(&count);
        for (size_t c = 0; c <= count; ++c) {
            bool heading = false;
            for (size_t i = 0; i < app->keymap.count && g_key_row_count + 1 < KEY_ROWS_MAX; ++i) {
                proven_u8str_view_t ctx = app->keymap.bindings[i].context;
                bool here = c < count ? proven_u8str_view_eq(ctx, proven_u8str_view_from_cstr(all[c].name))
                                      : rubraview_keymap_context_info(ctx) == NULL;
                if (!here) continue;
                if (!heading) { g_key_row[g_key_row_count++] = -(int32_t)c - 1; heading = true; }
                g_key_row[g_key_row_count++] = (int32_t)i;
            }
        }
    }
    rubraview_settings_view_set_table_rows(&app->settings_view, (int32_t)g_key_row_count + 1);   /* heading + rows */
}

/* The binding a Keys row is, or -1 for a heading or a row of the by-key list. */
int32_t keymap_row_binding(int32_t row) {
    if (g_keys_by_key || g_keys_layout || row < 1 || (size_t)row > g_key_row_count) return -1;
    if (g_keys_by_row) return g_key_rows[row - 1].heading ? -1 : g_key_rows[row - 1].binding;   /* RV-303 */
    return g_key_row[row - 1];
}

typedef struct { double x, y, unit; bool ok; } keys_layout_box_t;

/* Where the keyboard is on the window: its left top and the size of a key unit. */
static keys_layout_box_t keys_layout_box(app_state_t *app) {
    const rubraview_settings_view_t *v = &app->settings_view;
    keys_layout_box_t b = { 0 };
    if (!g_keys_layout || v->page != RUBRAVIEW_TAB_KEYS) return b;
    int32_t first = -1, at = -1;
    for (int32_t k = 1; k <= KEYS_LAYOUT_ROWS && at < 0; ++k) {
        int32_t r = rubraview_settings_view_table_screen_row(v, PROVEN_LIT("keymap"), k);
        if (r >= 0) { at = r; first = k; }
    }
    if (at < 0) return b;
    double cw = app->settings_cell_w, ch = app->settings_cell_h, w = 0, h = 0;
    rubraview_keyboard_extent(&w, &h);
    double room = cw * (double)(v->cols - v->content_col - 2);
    b.unit = room / w < ch * 2.3 ? room / w : ch * 2.3;   /* RV-244: a little bigger, the pad and the arrows under it */
    b.x = cw * v->content_col;
    b.y = settings_row_y(app, at) - ch * (double)(first - 1);   /* table row 1's top, wherever the page is scrolled */
    b.ok = b.unit > 4.0;
    return b;
}

/* RV-252: the circle under (px, py) — its key's index times 8 plus its kind — or -1 */
int32_t keys_dot_under(app_state_t *app, double px, double py) {
    keys_layout_box_t b = keys_layout_box(app);
    if (!b.ok) return -1;
    int32_t at = rubraview_keyboard_cap_at((px - b.x) / b.unit, (py - b.y) / b.unit);
    if (at < 0) return -1;
    size_t count = 0;
    const rubraview_keycap_t *k = &rubraview_keyboard_caps(&count)[at];
    if (!k->name) return -1;
    double gap = b.unit * 0.08;
    int m = rubraview_keyboard_dot_at(b.x + b.unit * k->x + gap / 2, b.y + b.unit * k->y + gap / 2, b.unit * k->w - gap, b.unit * k->h - gap, b.unit,
                                      rubraview_keyboard_marks(&app->keymap, proven_u8str_view_from_cstr(k->name)), px, py);
    return m < 0 ? -1 : at * 8 + m;
}

void keys_layout_draw(app_state_t *app) {
    keys_layout_box_t b = keys_layout_box(app);
    if (!b.ok) return;
    rubraview_renderer_t *r = app->settings_renderer;
    const rubraview_settings_view_t *v = &app->settings_view;
    double cw = app->settings_cell_w, ch = app->settings_cell_h;
    double top = app->settings_content_top, bottom = top + ch * (double)rubraview_settings_view_visible_rows(v);
    size_t count = 0;
    const rubraview_keycap_t *caps = rubraview_keyboard_caps(&count);
    double gap = b.unit * 0.08;
    for (size_t i = 0; i < count; ++i) {
        const rubraview_keycap_t *k = &caps[i];
        rubraview_pal_rect_t cap = { b.x + b.unit * k->x + gap / 2, b.y + b.unit * k->y + gap / 2, b.unit * k->w - gap, b.unit * k->h - gap };
        if (cap.y < top || cap.y + cap.height > bottom) continue;   /* scrolled off: the buttons stay clear */
        uint32_t marks = k->name ? rubraview_keyboard_marks(&app->keymap, proven_u8str_view_from_cstr(k->name)) : 0;
        rubraview_pal_render_fill_rect(r, cap, k->name ? 0xFF2E3640u : 0xFF22282Eu, b.unit * 0.12);
        rubraview_pal_render_stroke_rect(r, cap, marks ? 0xFF6A7685u : 0xFF3A434Du, 1.0, b.unit * 0.12);
        rubraview_pal_rect_t words = { cap.x, cap.y + cap.height * 0.04, cap.width, cap.height * 0.5 };
        rubraview_pal_render_draw_text(r, proven_u8str_view_from_cstr(k->label), words, b.unit * 0.30,
                                       k->name ? (marks ? SETTINGS_TEXT : SETTINGS_DIM) : 0xFF59626Du, RUBRAVIEW_TEXT_CENTER);
        /* RV-252 (owner, 2026-11-07: "반투명 좀 더 큰 원으로 3, 2 모양으로 w 모양으로 원을 배치하자. 그래서 마우스로 호버링하기 편하게"):
           larger see-through circles as a W, each kind in its own place; the one under the pointer full and ringed */
        for (int m = 0; m < RUBRAVIEW_KEYMARK_KINDS; ++m) {
            if (!(marks & (1u << m))) continue;
            double cx = 0, cy = 0, d = 0;
            rubraview_keyboard_dot_place(cap.x, cap.y, cap.width, cap.height, b.unit, m, &cx, &cy, &d);
            bool lit = g_keys_dot_hover == (int32_t)i * 8 + m;
            rubraview_pal_rect_t c = { cx - d / 2, cy - d / 2, d, d };
            rubraview_pal_render_fill_rect(r, c, lit ? KEYMARK_COLOR[m] : (KEYMARK_COLOR[m] & 0x00FFFFFFu) | 0xA0000000u, d / 2);
            if (lit) rubraview_pal_render_stroke_rect(r, c, 0xFFFFFFFFu, 1.5, d / 2);
        }
    }
    /* the dots' key, wrapped in rows under the keyboard so narrow windows keep it legible */
    double w = 0, h = 0;
    rubraview_keyboard_extent(&w, &h);
    double y = b.y + b.unit * h + ch * 0.3;
    if (y < top || y + ch > bottom) return;
    const char *const WORDS[RUBRAVIEW_KEYMARK_KINDS] = { T("alone"), "Shift", "Ctrl", "Alt", T("two or more") };
    double lw[RUBRAVIEW_KEYMARK_KINDS] = { 0 }, lx[RUBRAVIEW_KEYMARK_KINDS] = { 0 };
    uint32_t lrow[RUBRAVIEW_KEYMARK_KINDS] = { 0 };
    double room = cw * (double)(v->cols - v->content_col - 2);
    if (room < cw * 10.0) room = cw * 10.0;
    for (int m = 0; m < RUBRAVIEW_KEYMARK_KINDS; ++m) {
        lw[m] = ch * 0.6 + cw * (double)(rubraview_settings_text_cells(proven_u8str_view_from_cstr(WORDS[m])) + 1);
        if (lw[m] > room) lw[m] = room;
    }
    (void)rubraview_flow_inline(lw, RUBRAVIEW_KEYMARK_KINDS, cw * 2.0, room, lrow, lx);
    for (int m = 0; m < RUBRAVIEW_KEYMARK_KINDS; ++m) {
        double ly = y + (double)lrow[m] * ch;
        if (ly + ch > bottom) continue;
        double ix = b.x + lx[m];
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ ix, ly + ch * 0.3, ch * 0.4, ch * 0.4 }, KEYMARK_COLOR[m], ch * 0.2);
        rubraview_pal_render_draw_text_mono(r, proven_u8str_view_from_cstr(WORDS[m]), ix + ch * 0.6, ly, app->settings_font, SETTINGS_TEXT);
    }
}

/* A click on a key: what it does, everywhere it does something, under the page. True when it was on a key. */
bool keys_layout_click(app_state_t *app, double px, double py) {
    keys_layout_box_t b = keys_layout_box(app);
    if (!b.ok) return false;
    int32_t at = rubraview_keyboard_cap_at((px - b.x) / b.unit, (py - b.y) / b.unit);
    if (at < 0) return false;
    size_t count = 0;
    const rubraview_keycap_t *k = &rubraview_keyboard_caps(&count)[at];
    /* RV-244 (owner, 2026-11-07: "각 색깔의 원 크기를 좀 더 크게 키워서 클릭할 수 있게"): a click on a dot names only that
       kind of binding (alone, Shift, Ctrl, Alt, two or more); elsewhere on the key, all of them */
    uint32_t only = 0;
    {
        int32_t dot = keys_dot_under(app, px, py);   /* RV-252: the W's circles */
        if (dot >= 0 && dot / 8 == at) only = 1u << (dot % 8);
    }
    char said[sizeof(app->settings_message)];
    int n = rubraview_fmt(said, sizeof said, "{}:", PROVEN_ARG(k->label));
    bool any = false;
    for (size_t i = 0; k->name && i < app->keymap.count && n > 0 && (size_t)n < sizeof said; ++i) {
        const rubraview_key_binding_t *bd = &app->keymap.bindings[i];
        for (size_t c = 0; c < bd->combo_count && (size_t)n < sizeof said; ++c) {
            if (!proven_u8str_view_eq_ascii_ci(bd->combos[c].key_name, proven_u8str_view_from_cstr(k->name))) continue;
            if (only && !(rubraview_keyboard_mark_of(bd->combos[c]) & only)) continue;   /* RV-244: the dot's kind only */
            char combo[64];
            proven_u8str_view_t keys = rubraview_key_combo_format(combo, sizeof combo, bd->combos[c]);
            n += rubraview_fmt(said + n, sizeof said - (size_t)n, "{} {} {}", PROVEN_ARG(any ? ";" : ""), PROVEN_ARG(rubraview_bytes_view((const char *)keys.ptr, (int)keys.size)),
                          PROVEN_ARG(rubraview_bytes_view((const char *)bd->action.ptr, (int)bd->action.size)));
            any = true;
        }
    }
    if (!any) rubraview_fmt(said, sizeof said, k->name ? T("{}: nothing") : T("{}: a modifier, held with other keys"), PROVEN_ARG(k->label));
    settings_say(app, said);
    return true;
}
