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

/* The cap at (x, y) key units, or -1. */
int32_t rubraview_keyboard_cap_at(double x, double y);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_KEYBOARD_H */
