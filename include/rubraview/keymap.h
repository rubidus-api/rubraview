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

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_KEYMAP_H */
