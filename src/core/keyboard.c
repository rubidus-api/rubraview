/*
 * RV-217: the keyboard drawn in Settings › Keys and the kinds of binding on each key (see keyboard.h).
 * The caps are a US ANSI keyboard's, a key unit wide each unless said; the gaps are the keyboard's own.
 */
#include "rubraview/keyboard.h"

#include <string.h>

#define NAV 15.25f   /* the editing keys and the arrows */
#define PAD 18.5f    /* the number pad */

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
    { "Insert", "Ins", NAV, 1.5f, 1, 1 }, { "Home", "Home", NAV + 1, 1.5f, 1, 1 }, { "PageUp", "PgUp", NAV + 2, 1.5f, 1, 1 },
    { "Delete", "Del", NAV, 2.5f, 1, 1 }, { "End", "End", NAV + 1, 2.5f, 1, 1 }, { "PageDown", "PgDn", NAV + 2, 2.5f, 1, 1 },
    { "Up", "\xE2\x86\x91", NAV + 1, 4.5f, 1, 1 },
    { "Left", "\xE2\x86\x90", NAV, 5.5f, 1, 1 }, { "Down", "\xE2\x86\x93", NAV + 1, 5.5f, 1, 1 }, { "Right", "\xE2\x86\x92", NAV + 2, 5.5f, 1, 1 },
    /* the number pad */
    { NULL, "Num", PAD, 1.5f, 1, 1 }, { "NumpadDivide", "/", PAD + 1, 1.5f, 1, 1 },
    { "NumpadMultiply", "*", PAD + 2, 1.5f, 1, 1 }, { "NumpadSubtract", "-", PAD + 3, 1.5f, 1, 1 },
    { "Numpad7", "7", PAD, 2.5f, 1, 1 }, { "Numpad8", "8", PAD + 1, 2.5f, 1, 1 }, { "Numpad9", "9", PAD + 2, 2.5f, 1, 1 },
    { "NumpadAdd", "+", PAD + 3, 2.5f, 1, 2 },
    { "Numpad4", "4", PAD, 3.5f, 1, 1 }, { "Numpad5", "5", PAD + 1, 3.5f, 1, 1 }, { "Numpad6", "6", PAD + 2, 3.5f, 1, 1 },
    { "Numpad1", "1", PAD, 4.5f, 1, 1 }, { "Numpad2", "2", PAD + 1, 4.5f, 1, 1 }, { "Numpad3", "3", PAD + 2, 4.5f, 1, 1 },
    { "NumpadEnter", "Ent", PAD + 3, 4.5f, 1, 2 },
    { "Numpad0", "0", PAD, 5.5f, 2, 1 }, { "NumpadDecimal", ".", PAD + 2, 5.5f, 1, 1 },
};

const rubraview_keycap_t *rubraview_keyboard_caps(size_t *count) {
    if (count) *count = sizeof CAPS / sizeof CAPS[0];
    return CAPS;
}

void rubraview_keyboard_extent(double *width, double *height) {
    if (width) *width = PAD + 4.0;
    if (height) *height = 6.5;
}

static bool same_name(proven_u8str_view_t a, const char *b) {
    size_t n = strlen(b);
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
    memcpy(name, key_name.ptr, key_name.size);
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

int32_t rubraview_keyboard_cap_at(double x, double y) {
    for (size_t i = 0; i < sizeof CAPS / sizeof CAPS[0]; ++i) {
        const rubraview_keycap_t *k = &CAPS[i];
        if (x >= k->x && x < k->x + k->w && y >= k->y && y < k->y + k->h) return (int32_t)i;
    }
    return -1;
}
