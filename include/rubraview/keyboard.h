#ifndef RUBRAVIEW_KEYBOARD_H
#define RUBRAVIEW_KEYBOARD_H

#include "rubraview/keymap.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-217 (owner, 2026-10-31: "배치 보기를 추가해서, qwerty 키 맵 보여주고 그 위에 각 키에서 그냥, Shift, Ctrl,
 * Alt, 기타(특수키 중 두개 이상) 이것들을 각각 색으로 (빨노파녹) 점을 찍어서"; "두개 이상은 보라색이"): a US
 * QWERTY keyboard (the main block, the editing keys, the arrows and the number pad) in key units, and
 * which kinds of binding each key has. A cap's `name` is the keymap's own name for the key; a cap with
 * none (Shift, Ctrl, Caps Lock ...) is drawn but never bound.
 */

typedef struct rubraview_keycap {
    const char *name;      /* the keymap's name ("Q", "BracketLeft", "Numpad7"), or NULL for a modifier */
    const char *label;     /* what is written on it */
    float x, y, w, h;      /* in key units from the top left */
} rubraview_keycap_t;

const rubraview_keycap_t *rubraview_keyboard_caps(size_t *count);
/* The whole keyboard's width and height, in key units. */
void rubraview_keyboard_extent(double *width, double *height);

/* The kinds of binding a key can have; a dot each, in this order. */
enum {
    RUBRAVIEW_KEYMARK_PLAIN = 1u << 0,   /* the key alone: red */
    RUBRAVIEW_KEYMARK_SHIFT = 1u << 1,   /* Shift and the key: yellow */
    RUBRAVIEW_KEYMARK_CTRL  = 1u << 2,   /* Ctrl and the key: blue */
    RUBRAVIEW_KEYMARK_ALT   = 1u << 3,   /* Alt and the key: green */
    RUBRAVIEW_KEYMARK_MANY  = 1u << 4,   /* two modifiers or more: purple */
};
#define RUBRAVIEW_KEYMARK_KINDS 5

/* The kinds `key_name` (as the keymap names keys, any case) is bound with, in any context. */
uint32_t rubraview_keyboard_marks(const rubraview_keymap_t *keymap, proven_u8str_view_t key_name);
/* The kind of one combination. */
uint32_t rubraview_keyboard_mark_of(rubraview_key_combo_t combo);

/* Keys bound that the keyboard does not show (another name, a key it leaves out): their names into
   `out`, each once, in the keymap's order; how many (at most `cap`). */
size_t rubraview_keyboard_off_layout(const rubraview_keymap_t *keymap, proven_u8str_view_t *out, size_t cap);

/* RV-252 (owner, 2026-11-07: "키 배치 보기에서 꼭 하단에 색깔 점이 한줄 가로로 있을 필요는 없다. 반투명 좀 더 큰 원으로 3, 2 모양으로 w 모양으로 원을 배치하자. 그래서 마우스로 호버링하기 편하게 하자."): where a kind's circle sits on a key drawn at (x, y, w, h), `unit` a key unit:
   in the key's lower half as a W — alone, Ctrl and two or more on the top line, Shift and Alt on the line under
   between them, in the order of a W drawn left to right. Its centre and its diameter. */
void rubraview_keyboard_dot_place(double x, double y, double w, double h, double unit, int kind,
                                  double *cx, double *cy, double *diameter);

/* The kind whose circle (of those in `marks`) is under (px, py) on that key — the nearest, a little past its edge
   counted in — or -1. */
int rubraview_keyboard_dot_at(double x, double y, double w, double h, double unit, uint32_t marks, double px, double py);

/* The cap at (x, y) key units, or -1. */
int32_t rubraview_keyboard_cap_at(double x, double y);

/* ---- RV-303 (RFC-0010 §4.8, D-367): the keys by keyboard row ---- */

/* The keyboard's rows, top to bottom, then the blocks under it, then the keys it does not show. */
enum {
    RUBRAVIEW_KEYROW_FUNCTION = 0,   /* Esc, F1-F12 */
    RUBRAVIEW_KEYROW_NUMBER,         /* ` 1 2 ... 0 - = Backspace */
    RUBRAVIEW_KEYROW_TOP,            /* Tab Q W E R T Y ... \ */
    RUBRAVIEW_KEYROW_HOME,           /* A S D F ... Enter */
    RUBRAVIEW_KEYROW_BOTTOM,         /* Z X C V ... / */
    RUBRAVIEW_KEYROW_SPACE,          /* Space */
    RUBRAVIEW_KEYROW_EDITING,        /* Insert, Home, Page Up, Delete, End, Page Down */
    RUBRAVIEW_KEYROW_ARROWS,
    RUBRAVIEW_KEYROW_PAD,            /* the number pad */
    RUBRAVIEW_KEYROW_OTHER,          /* bound, but not on the keyboard drawn */
    RUBRAVIEW_KEYROW_COUNT
};

/* A row's name, the reader's words (N_). */
const char *rubraview_keyboard_row_heading(size_t row);
/* The row a cap of rubraview_keyboard_caps is in. */
size_t rubraview_keyboard_row_of(size_t cap);

typedef struct rubraview_keyrow_entry {
    uint8_t row;                   /* RUBRAVIEW_KEYROW_* */
    bool heading;                  /* the row's own line: its name and its keys */
    rubraview_key_combo_t combo;   /* a key and its modifiers; the key alone for one with nothing bound */
    int32_t binding;               /* the binding that acts on this screen, -1 for none ("not set") */
    int32_t hidden;                /* a binding of a later context in the chain with the same key, never reached; -1 */
    int32_t left_out;              /* a heading: how many of its row's bound combinations rubraview_keyrows_filter left out */
} rubraview_keyrow_entry_t;

/* What every key does on one screen, row by row: each row's heading line, then each of its keys — every
   combination of it bound in a context `mode` asks (rubraview_keymap_mode_chain, no slide show running), in the
   order none, Shift, Ctrl, Ctrl+Shift, Alt, ..., each with the binding that acts and one it hides; a key with none
   bound once, "not set". `mode` RUBRAVIEW_KEY_MODE_COUNT is the common context alone. The keys bound that the
   keyboard does not show come last under RUBRAVIEW_KEYROW_OTHER (that row only when there are some). Returns how
   many entries (at most `cap` are written; the count is of what was written). */
size_t rubraview_keymap_rows(const rubraview_keymap_t *keymap, rubraview_key_mode_t mode, rubraview_keyrow_entry_t *out, size_t cap);

/* RFC-0010 §4.8: which combinations the rows show — all, a key alone, with Shift, Ctrl or Alt alone, or with two
   or more of them. */
enum {
    RUBRAVIEW_KEYMODS_ALL = 0,
    RUBRAVIEW_KEYMODS_ALONE,
    RUBRAVIEW_KEYMODS_SHIFT,
    RUBRAVIEW_KEYMODS_CTRL,
    RUBRAVIEW_KEYMODS_ALT,
    RUBRAVIEW_KEYMODS_OTHER,
    RUBRAVIEW_KEYMODS_COUNT
};
bool rubraview_keymods_pass(uint32_t modifiers, int filter);

/* `needle` in `hay`, ASCII letters in either case; an empty needle is in everything. */
bool rubraview_text_has(proven_u8str_view_t hay, proven_u8str_view_t needle);

/* A line the search keeps (a heading is never asked). */
typedef bool (*rubraview_keyrow_keep_fn)(const rubraview_keyrow_entry_t *entry, void *ctx);

/* Filters rubraview_keymap_rows's `rows` in place, in their order: a line stays when its modifiers pass `mods` and
   `keep` (when given) says so. Every heading stays with `left_out` its row's lines taken away — except, when
   `keep` is given (a search), a row left with nothing found. Returns how many entries are left. */
size_t rubraview_keyrows_filter(rubraview_keyrow_entry_t *rows, size_t n, int mods, rubraview_keyrow_keep_fn keep, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_KEYBOARD_H */
