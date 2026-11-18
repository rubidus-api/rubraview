/*
 * RV-217: the keyboard drawn in Settings › Keys and the kinds of binding on each key (see keyboard.h).
 * The caps are a US ANSI keyboard's, a key unit wide each unless said; the gaps are the keyboard's own.
 */
#include "rubraview/core.h"
#include "rubraview/keyboard.h"
#include "rubraview/lang.h"

#include <string.h>

/* RV-244 (owner, 2026-11-07: "키패드랑 이동키 등은 메인 키보드 영역 아래로 배치해서 더 좁은 폭에서 스크롤하며 볼 수 있게"):
   the editing keys, the arrows and the number pad under the main block, so the keyboard is narrower and taller */
#define NAV 0.0f     /* the editing keys and the arrows, at the left under the main block */
#define PAD 4.5f     /* the number pad beside them */
#define LOW 6.0f     /* how far below the rows they had beside the main block */

static const rubraview_keycap_t CAPS[] = {
    /* the function row */
    { "Escape", "Esc", 0, 0, 1, 1 },
    { "F1", "F1", 2, 0, 1, 1 }, { "F2", "F2", 3, 0, 1, 1 }, { "F3", "F3", 4, 0, 1, 1 }, { "F4", "F4", 5, 0, 1, 1 },
    { "F5", "F5", 6.5f, 0, 1, 1 }, { "F6", "F6", 7.5f, 0, 1, 1 }, { "F7", "F7", 8.5f, 0, 1, 1 }, { "F8", "F8", 9.5f, 0, 1, 1 },
    { "F9", "F9", 11, 0, 1, 1 }, { "F10", "F10", 12, 0, 1, 1 }, { "F11", "F11", 13, 0, 1, 1 }, { "F12", "F12", 14, 0, 1, 1 },
    /* the number row */
    { "Backquote", "`", 0, 1.5f, 1, 1 },
    { "1", "1", 1, 1.5f, 1, 1 }, { "2", "2", 2, 1.5f, 1, 1 }, { "3", "3", 3, 1.5f, 1, 1 }, { "4", "4", 4, 1.5f, 1, 1 },
    { "5", "5", 5, 1.5f, 1, 1 }, { "6", "6", 6, 1.5f, 1, 1 }, { "7", "7", 7, 1.5f, 1, 1 }, { "8", "8", 8, 1.5f, 1, 1 },
    { "9", "9", 9, 1.5f, 1, 1 }, { "0", "0", 10, 1.5f, 1, 1 }, { "Minus", "-", 11, 1.5f, 1, 1 }, { "Plus", "=", 12, 1.5f, 1, 1 },
    { "Backspace", "Bksp", 13, 1.5f, 2, 1 },
    /* Tab's row */
    { "Tab", "Tab", 0, 2.5f, 1.5f, 1 },
    { "Q", "Q", 1.5f, 2.5f, 1, 1 }, { "W", "W", 2.5f, 2.5f, 1, 1 }, { "E", "E", 3.5f, 2.5f, 1, 1 }, { "R", "R", 4.5f, 2.5f, 1, 1 },
    { "T", "T", 5.5f, 2.5f, 1, 1 }, { "Y", "Y", 6.5f, 2.5f, 1, 1 }, { "U", "U", 7.5f, 2.5f, 1, 1 }, { "I", "I", 8.5f, 2.5f, 1, 1 },
    { "O", "O", 9.5f, 2.5f, 1, 1 }, { "P", "P", 10.5f, 2.5f, 1, 1 },
    { "BracketLeft", "[", 11.5f, 2.5f, 1, 1 }, { "BracketRight", "]", 12.5f, 2.5f, 1, 1 }, { "Backslash", "\\", 13.5f, 2.5f, 1.5f, 1 },
    /* Caps Lock's row */
    { NULL, "Caps", 0, 3.5f, 1.75f, 1 },
    { "A", "A", 1.75f, 3.5f, 1, 1 }, { "S", "S", 2.75f, 3.5f, 1, 1 }, { "D", "D", 3.75f, 3.5f, 1, 1 }, { "F", "F", 4.75f, 3.5f, 1, 1 },
    { "G", "G", 5.75f, 3.5f, 1, 1 }, { "H", "H", 6.75f, 3.5f, 1, 1 }, { "J", "J", 7.75f, 3.5f, 1, 1 }, { "K", "K", 8.75f, 3.5f, 1, 1 },
    { "L", "L", 9.75f, 3.5f, 1, 1 }, { "Semicolon", ";", 10.75f, 3.5f, 1, 1 }, { "Quote", "'", 11.75f, 3.5f, 1, 1 },
    { "Enter", "Enter", 12.75f, 3.5f, 2.25f, 1 },
    /* Shift's row */
    { NULL, "Shift", 0, 4.5f, 2.25f, 1 },
    { "Z", "Z", 2.25f, 4.5f, 1, 1 }, { "X", "X", 3.25f, 4.5f, 1, 1 }, { "C", "C", 4.25f, 4.5f, 1, 1 }, { "V", "V", 5.25f, 4.5f, 1, 1 },
    { "B", "B", 6.25f, 4.5f, 1, 1 }, { "N", "N", 7.25f, 4.5f, 1, 1 }, { "M", "M", 8.25f, 4.5f, 1, 1 },
    { "Comma", ",", 9.25f, 4.5f, 1, 1 }, { "Period", ".", 10.25f, 4.5f, 1, 1 }, { "Slash", "/", 11.25f, 4.5f, 1, 1 },
    { NULL, "Shift", 12.25f, 4.5f, 2.75f, 1 },
    /* the bottom row */
    { NULL, "Ctrl", 0, 5.5f, 1.25f, 1 }, { NULL, "Win", 1.25f, 5.5f, 1.25f, 1 }, { NULL, "Alt", 2.5f, 5.5f, 1.25f, 1 },
    { "Space", "Space", 3.75f, 5.5f, 6.25f, 1 },
    { NULL, "Alt", 10, 5.5f, 1.25f, 1 }, { NULL, "Win", 11.25f, 5.5f, 1.25f, 1 }, { NULL, "Menu", 12.5f, 5.5f, 1.25f, 1 },
    { NULL, "Ctrl", 13.75f, 5.5f, 1.25f, 1 },
    /* the editing keys and the arrows */
    { "Insert", "Ins", NAV, LOW + 1.5f, 1, 1 }, { "Home", "Home", NAV + 1, LOW + 1.5f, 1, 1 }, { "PageUp", "PgUp", NAV + 2, LOW + 1.5f, 1, 1 },
    { "Delete", "Del", NAV, LOW + 2.5f, 1, 1 }, { "End", "End", NAV + 1, LOW + 2.5f, 1, 1 }, { "PageDown", "PgDn", NAV + 2, LOW + 2.5f, 1, 1 },
    { "Up", "\xE2\x86\x91", NAV + 1, LOW + 3.75f, 1, 1 },
    { "Left", "\xE2\x86\x90", NAV, LOW + 4.75f, 1, 1 }, { "Down", "\xE2\x86\x93", NAV + 1, LOW + 4.75f, 1, 1 }, { "Right", "\xE2\x86\x92", NAV + 2, LOW + 4.75f, 1, 1 },
    /* the number pad */
    { NULL, "Num", PAD, LOW + 1.5f, 1, 1 }, { "NumpadDivide", "/", PAD + 1, LOW + 1.5f, 1, 1 },
    { "NumpadMultiply", "*", PAD + 2, LOW + 1.5f, 1, 1 }, { "NumpadSubtract", "-", PAD + 3, LOW + 1.5f, 1, 1 },
    { "Numpad7", "7", PAD, LOW + 2.5f, 1, 1 }, { "Numpad8", "8", PAD + 1, LOW + 2.5f, 1, 1 }, { "Numpad9", "9", PAD + 2, LOW + 2.5f, 1, 1 },
    { "NumpadAdd", "+", PAD + 3, LOW + 2.5f, 1, 2 },
    { "Numpad4", "4", PAD, LOW + 3.5f, 1, 1 }, { "Numpad5", "5", PAD + 1, LOW + 3.5f, 1, 1 }, { "Numpad6", "6", PAD + 2, LOW + 3.5f, 1, 1 },
    { "Numpad1", "1", PAD, LOW + 4.5f, 1, 1 }, { "Numpad2", "2", PAD + 1, LOW + 4.5f, 1, 1 }, { "Numpad3", "3", PAD + 2, LOW + 4.5f, 1, 1 },
    { "NumpadEnter", "Ent", PAD + 3, LOW + 4.5f, 1, 2 },
    { "Numpad0", "0", PAD, LOW + 5.5f, 2, 1 }, { "NumpadDecimal", ".", PAD + 2, LOW + 5.5f, 1, 1 },
};

const rubraview_keycap_t *rubraview_keyboard_caps(size_t *count) {
    if (count) *count = sizeof CAPS / sizeof CAPS[0];
    return CAPS;
}

void rubraview_keyboard_extent(double *width, double *height) {
    if (width) *width = 15.0;          /* the main block's */
    if (height) *height = LOW + 6.5;   /* RV-244: the pad's last row under it */
}

static bool same_name(proven_u8str_view_t a, const char *b) {
    size_t n = proven_cstr_len(b);
    if (a.size != n) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char x = a.ptr[i], y = (unsigned char)b[i];
        if (x >= 'a' && x <= 'z') x = (unsigned char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (unsigned char)(y - 32);
        if (x != y) return false;
    }
    return true;
}

uint32_t rubraview_keyboard_mark_of(rubraview_key_combo_t combo) {
    uint32_t m = combo.modifiers & (RUBRAVIEW_MOD_SHIFT | RUBRAVIEW_MOD_CTRL | RUBRAVIEW_MOD_ALT);
    if (m == 0) return RUBRAVIEW_KEYMARK_PLAIN;
    if (m == RUBRAVIEW_MOD_SHIFT) return RUBRAVIEW_KEYMARK_SHIFT;
    if (m == RUBRAVIEW_MOD_CTRL) return RUBRAVIEW_KEYMARK_CTRL;
    if (m == RUBRAVIEW_MOD_ALT) return RUBRAVIEW_KEYMARK_ALT;
    return RUBRAVIEW_KEYMARK_MANY;
}

uint32_t rubraview_keyboard_marks(const rubraview_keymap_t *keymap, proven_u8str_view_t key_name) {
    uint32_t marks = 0;
    if (!keymap || key_name.size == 0) return 0;
    char name[32];
    if (key_name.size >= sizeof name) return 0;
    proven_sys_mem_copy(name, key_name.ptr, key_name.size);
    name[key_name.size] = '\0';
    for (size_t i = 0; i < keymap->count; ++i)
        for (size_t c = 0; c < keymap->bindings[i].combo_count; ++c)
            if (same_name(keymap->bindings[i].combos[c].key_name, name)) marks |= rubraview_keyboard_mark_of(keymap->bindings[i].combos[c]);
    return marks;
}

static bool on_layout(proven_u8str_view_t name) {
    for (size_t i = 0; i < sizeof CAPS / sizeof CAPS[0]; ++i) if (CAPS[i].name && same_name(name, CAPS[i].name)) return true;
    return false;
}

size_t rubraview_keyboard_off_layout(const rubraview_keymap_t *keymap, proven_u8str_view_t *out, size_t cap) {
    size_t n = 0;
    if (!keymap || !out) return 0;
    for (size_t i = 0; i < keymap->count; ++i) {
        for (size_t c = 0; c < keymap->bindings[i].combo_count && n < cap; ++c) {
            proven_u8str_view_t name = keymap->bindings[i].combos[c].key_name;
            if (name.size == 0 || on_layout(name)) continue;
            bool seen = false;
            for (size_t k = 0; k < n && !seen; ++k) seen = proven_u8str_view_eq_ascii_ci(out[k], name);
            if (!seen) out[n++] = name;
        }
    }
    return n;
}

void rubraview_keyboard_dot_place(double x, double y, double w, double h, double unit, int kind,
                                  double *cx, double *cy, double *diameter) {
    double d = unit * 0.24, s = unit * 0.30;           /* the diameter, and the step between the top line's circles */
    double fit = (w * 0.94 - d) / 2.0;                 /* a narrow key: closer, and smaller if it must */
    if (s > fit) s = fit > 0.0 ? fit : 0.0;
    if (d > s * 1.6 && s > 0.0) d = s * 1.6;
    static const double COL[RUBRAVIEW_KEYMARK_KINDS] = { -1.0, -0.5, 0.0, 0.5, 1.0 };   /* W: top, under, top, under, top */
    int k = kind < 0 ? 0 : kind >= RUBRAVIEW_KEYMARK_KINDS ? RUBRAVIEW_KEYMARK_KINDS - 1 : kind;
    double top = y + h * 0.5 + d * 0.5, under = top + d * 0.8;
    if (under + d * 0.5 > y + h) { under = y + h - d * 0.5; top = under - d * 0.8; }
    if (cx) *cx = x + w * 0.5 + COL[k] * s;
    if (cy) *cy = (k % 2 == 0) ? top : under;
    if (diameter) *diameter = d;
}

int rubraview_keyboard_dot_at(double x, double y, double w, double h, double unit, uint32_t marks, double px, double py) {
    int best = -1;
    double best_d2 = 0.0;
    for (int m = 0; m < RUBRAVIEW_KEYMARK_KINDS; ++m) {
        if (!(marks & (1u << m))) continue;
        double cx = 0, cy = 0, d = 0;
        rubraview_keyboard_dot_place(x, y, w, h, unit, m, &cx, &cy, &d);
        double d2 = (px - cx) * (px - cx) + (py - cy) * (py - cy), reach = d * 0.6;   /* its radius and a little more */
        if (d2 <= reach * reach && (best < 0 || d2 < best_d2)) { best = m; best_d2 = d2; }
    }
    return best;
}

int32_t rubraview_keyboard_cap_at(double x, double y) {
    for (size_t i = 0; i < sizeof CAPS / sizeof CAPS[0]; ++i) {
        const rubraview_keycap_t *k = &CAPS[i];
        if (x >= k->x && x < k->x + k->w && y >= k->y && y < k->y + k->h) return (int32_t)i;
    }
    return -1;
}

/* ---- RV-303 (RFC-0010 §4.8, D-367): the keys by keyboard row ---- */

static const char *const ROW_HEADING[RUBRAVIEW_KEYROW_COUNT] = {
    [RUBRAVIEW_KEYROW_FUNCTION] = N_("Esc and the function keys"),
    [RUBRAVIEW_KEYROW_NUMBER]   = N_("The number row"),
    [RUBRAVIEW_KEYROW_TOP]      = N_("The top letter row"),
    [RUBRAVIEW_KEYROW_HOME]     = N_("The middle letter row"),
    [RUBRAVIEW_KEYROW_BOTTOM]   = N_("The bottom letter row"),
    [RUBRAVIEW_KEYROW_SPACE]    = N_("The space bar"),
    [RUBRAVIEW_KEYROW_EDITING]  = N_("The editing keys"),
    [RUBRAVIEW_KEYROW_ARROWS]   = N_("The arrows"),
    [RUBRAVIEW_KEYROW_PAD]      = N_("The number pad"),
    [RUBRAVIEW_KEYROW_OTHER]    = N_("Other keys"),
};

const char *rubraview_keyboard_row_heading(size_t row) {
    return row < RUBRAVIEW_KEYROW_COUNT ? ROW_HEADING[row] : ROW_HEADING[RUBRAVIEW_KEYROW_OTHER];
}

size_t rubraview_keyboard_row_of(size_t cap) {
    if (cap >= sizeof CAPS / sizeof CAPS[0]) return RUBRAVIEW_KEYROW_OTHER;
    const rubraview_keycap_t *k = &CAPS[cap];
    if (k->y >= LOW) return k->x >= PAD ? RUBRAVIEW_KEYROW_PAD : k->y < LOW + 3.5f ? RUBRAVIEW_KEYROW_EDITING : RUBRAVIEW_KEYROW_ARROWS;
    if (k->y < 1.0f) return RUBRAVIEW_KEYROW_FUNCTION;
    if (k->y < 2.0f) return RUBRAVIEW_KEYROW_NUMBER;
    if (k->y < 3.0f) return RUBRAVIEW_KEYROW_TOP;
    if (k->y < 4.0f) return RUBRAVIEW_KEYROW_HOME;
    if (k->y < 5.0f) return RUBRAVIEW_KEYROW_BOTTOM;
    return RUBRAVIEW_KEYROW_SPACE;
}

/* The bindings of `chain` that hold `combo`, in the chain's order: the first acts, the second is the one it hides. */
static void rows_resolve(const rubraview_keymap_t *keymap, const proven_u8str_view_t *chain, size_t links, rubraview_key_combo_t combo,
                         int32_t *acts, int32_t *hidden) {
    *acts = -1;
    *hidden = -1;
    for (size_t l = 0; l < links && *hidden < 0; ++l)
        for (size_t i = 0; i < keymap->count && *hidden < 0; ++i) {
            const rubraview_key_binding_t *b = &keymap->bindings[i];
            if (!proven_u8str_view_eq(b->context, chain[l])) continue;
            for (size_t c = 0; c < b->combo_count; ++c) {
                if (!rubraview_key_combo_equal(b->combos[c], combo)) continue;
                if (*acts < 0) *acts = (int32_t)i;
                else if (*acts != (int32_t)i) *hidden = (int32_t)i;
                break;
            }
        }
}

static bool rows_in_chain(proven_u8str_view_t context, const proven_u8str_view_t *chain, size_t links) {
    for (size_t l = 0; l < links; ++l) if (proven_u8str_view_eq(context, chain[l])) return true;
    return false;
}

size_t rubraview_keymap_rows(const rubraview_keymap_t *keymap, rubraview_key_mode_t mode, rubraview_keyrow_entry_t *out, size_t cap) {
    if (!keymap || !out || cap == 0) return 0;
    proven_u8str_view_t chain[8];
    size_t links = 0;
    if (mode == RUBRAVIEW_KEY_MODE_COUNT) chain[links++] = PROVEN_LIT("");
    else links = rubraview_keymap_mode_chain(mode, false, chain, 8);
    size_t n = 0;
#define ROWS_PUSH(...) do { if (n < cap) out[n++] = (rubraview_keyrow_entry_t){ __VA_ARGS__ }; } while (0)
    for (size_t row = 0; row < RUBRAVIEW_KEYROW_OTHER; ++row) {
        ROWS_PUSH(.row = (uint8_t)row, .heading = true, .binding = -1, .hidden = -1);
        for (size_t k = 0; k < sizeof CAPS / sizeof CAPS[0]; ++k) {
            if (!CAPS[k].name || rubraview_keyboard_row_of(k) != row) continue;
            proven_u8str_view_t name = proven_u8str_view_from_cstr(CAPS[k].name);
            uint32_t mods = 0;   /* bit m: the key with modifiers m is bound in the chain */
            for (size_t i = 0; i < keymap->count; ++i) {
                const rubraview_key_binding_t *b = &keymap->bindings[i];
                if (!rows_in_chain(b->context, chain, links)) continue;
                for (size_t c = 0; c < b->combo_count; ++c)
                    if (same_name(b->combos[c].key_name, CAPS[k].name)) mods |= 1u << (b->combos[c].modifiers & 7u);
            }
            if (mods == 0) { ROWS_PUSH(.row = (uint8_t)row, .combo = { .modifiers = 0, .key_name = name }, .binding = -1, .hidden = -1); continue; }
            for (uint32_t m = 0; m < 8; ++m) {
                if (!(mods & (1u << m))) continue;
                rubraview_key_combo_t combo = { .modifiers = m, .key_name = name };
                int32_t acts = -1, hidden = -1;
                rows_resolve(keymap, chain, links, combo, &acts, &hidden);
                ROWS_PUSH(.row = (uint8_t)row, .combo = combo, .binding = acts, .hidden = hidden);
            }
        }
    }
    size_t other = n;
    for (size_t l = 0; l < links; ++l)
        for (size_t i = 0; i < keymap->count; ++i) {
            const rubraview_key_binding_t *b = &keymap->bindings[i];
            if (!proven_u8str_view_eq(b->context, chain[l])) continue;
            for (size_t c = 0; c < b->combo_count; ++c) {
                rubraview_key_combo_t combo = b->combos[c];
                if (combo.key_name.size == 0 || on_layout(combo.key_name)) continue;
                bool seen = false;
                for (size_t e = other; e < n && !seen; ++e) seen = !out[e].heading && rubraview_key_combo_equal(out[e].combo, combo);
                if (seen) continue;
                if (n == other) ROWS_PUSH(.row = RUBRAVIEW_KEYROW_OTHER, .heading = true, .binding = -1, .hidden = -1);
                int32_t acts = -1, hidden = -1;
                rows_resolve(keymap, chain, links, combo, &acts, &hidden);
                ROWS_PUSH(.row = RUBRAVIEW_KEYROW_OTHER, .combo = combo, .binding = acts, .hidden = hidden);
            }
        }
#undef ROWS_PUSH
    return n;
}
