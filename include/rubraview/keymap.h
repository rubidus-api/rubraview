#ifndef RUBRAVIEW_KEYMAP_H
#define RUBRAVIEW_KEYMAP_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Keymap action table and dispatcher core (RFC-0001 §3.7.1, §3.7.2,
 * §3.7.5): parses `keymap.ini` (via rubraview/ini.h) into a lookup table
 * and answers both directions of dispatch — "what key combos are bound
 * to this action" and "what action does this key combo trigger". Key
 * events themselves (Win32 VK codes, WM_KEYDOWN wiring) are M3 (RV-038);
 * this module works entirely on the symbolic key names keymap.ini already
 * uses ("Right", "PageDown", "BracketLeft", ...), which the M3 Win32
 * layer maps to and from VK codes.
 *
 * A keymap.ini `[section]` is the binding's *context* (§3.7.1: which
 * viewing context is active — "navigation", "media", "slideshow", ...);
 * keys before any `[section]` (the ini global section) are context-
 * independent bindings. Each `key = value` line is one action id bound
 * to a comma-separated list of key combos, e.g.
 * `next_page = Right, PageDown, Space, J, D`.
 */

typedef enum rubraview_key_mod {
    RUBRAVIEW_MOD_NONE  = 0,
    RUBRAVIEW_MOD_SHIFT = 1u << 0,
    RUBRAVIEW_MOD_CTRL  = 1u << 1,
    RUBRAVIEW_MOD_ALT   = 1u << 2,
} rubraview_key_mod_t;

typedef struct rubraview_key_combo {
    uint32_t modifiers; /* bitmask of rubraview_key_mod_t */
    u8str_t  key_name;  /* symbolic name, e.g. "Right", "Space", "BracketLeft" */
} rubraview_key_combo_t;

typedef struct rubraview_key_binding {
    u8str_t context; /* the ini section this action was bound under ("" = global) */
    u8str_t action;  /* the ini key, e.g. "next_page" */
    rubraview_key_combo_t *combos;
    size_t                 combo_count;
} rubraview_key_binding_t;

typedef struct rubraview_keymap {
    rubraview_key_binding_t *bindings;
    size_t                    count;
} rubraview_keymap_t;

rubraview_keymap_t rubraview_keymap_parse(proven_arena_t *arena, u8str_t ini_text);

/**
 * Reverse lookup: given the currently active context and a pressed key
 * combo, return the bound action id, or an empty slice if none matches.
 * Searches `context`'s bindings first, then falls back to the global
 * ("") section — a context-specific binding always wins over a global
 * one for the same key combo.
 */
u8str_t rubraview_keymap_find_action(const rubraview_keymap_t *keymap, u8str_t context, rubraview_key_combo_t combo);

/**
 * Forward lookup: the binding (and its full combo list) for a given
 * (context, action) pair, or NULL if unbound.
 */
const rubraview_key_binding_t *rubraview_keymap_find_binding(const rubraview_keymap_t *keymap, u8str_t context, u8str_t action);

/* ---- §3.22.2 tab 8: changing bindings in place (D-14) ---- */

/** A combo as keymap.ini spells it, "Ctrl+Shift+O"; returns the text written. */
u8str_t rubraview_key_combo_format(char *buffer, size_t capacity, rubraview_key_combo_t combo);

/** Whether two combos are the same key (names compare without case). */
bool rubraview_key_combo_equal(rubraview_key_combo_t a, rubraview_key_combo_t b);

/**
 * Whether one key bound in both contexts would leave one of the two
 * actions unreachable. The rule follows the viewer's dispatch: the
 * `navigation` context is asked first, then the global one, then `view`
 * — so a key in two of those three always reaches the same action and
 * never the other. `slideshow`, `animation` and `subpage` are asked
 * *before* them only while a slide show runs or an animation or
 * multi-page file is open; a key they share with the rest keeps both
 * meanings (Space pauses a GIF and turns a page), so that is not a clash.
 * Neither is the same context, which is always a clash.
 */
bool rubraview_keymap_contexts_meet(u8str_t a, u8str_t b);

/** A copy that shares nothing mutable with `source` — what Revert goes back to. */
rubraview_keymap_t rubraview_keymap_copy(proven_arena_t *arena, const rubraview_keymap_t *source);

/** Same bindings, same keys, in the same order. */
bool rubraview_keymap_equal(const rubraview_keymap_t *a, const rubraview_keymap_t *b);

typedef enum rubraview_bind_result {
    RUBRAVIEW_BIND_ADDED = 0,
    RUBRAVIEW_BIND_ALREADY,     /* the action already has this key */
    RUBRAVIEW_BIND_TAKEN,       /* another action reachable in a meeting context has it: nothing changed */
    RUBRAVIEW_BIND_FAILED,      /* no such binding, or no memory */
} rubraview_bind_result_t;

/**
 * Add `combo` to binding `index`'s keys. A key another action already
 * holds where the two contexts meet is refused, and `out_holder` (when
 * given) points at that action's binding — owner decision 2026-09-14:
 * refuse and say who has it, rather than take the key away.
 */
rubraview_bind_result_t rubraview_keymap_bind(proven_arena_t *arena, rubraview_keymap_t *keymap, size_t index,
                                              rubraview_key_combo_t combo,
                                              const rubraview_key_binding_t **out_holder);

/** Remove binding `index`'s last key; false when it had none. */
bool rubraview_keymap_unbind_last(rubraview_keymap_t *keymap, size_t index);

/**
 * keymap.ini in the INI and TOML subset (D-13): a section per context in
 * order of first appearance, the global one as `[ui]`, every value a
 * quoted comma-separated list. Parsing the result gives an equal keymap.
 */
u8str_t rubraview_keymap_serialize(proven_arena_t *arena, const rubraview_keymap_t *keymap);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_KEYMAP_H */
