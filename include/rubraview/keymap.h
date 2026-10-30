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
 * viewing context is active — "navigation", "playback", "slideshow", ...);
 * `[common]` (once `[ui]`, or keys before any section) holds the bindings
 * asked everywhere, kept as the context "" (RV-184, D-219). Each `key = value` line is one action id bound
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
    proven_u8str_view_t  key_name;  /* symbolic name, e.g. "Right", "Space", "BracketLeft" */
} rubraview_key_combo_t;

typedef struct rubraview_key_binding {
    proven_u8str_view_t context; /* the ini section this action was bound under ("" = global) */
    proven_u8str_view_t action;  /* the ini key, e.g. "next_page" */
    rubraview_key_combo_t *combos;
    size_t                 combo_count;
} rubraview_key_binding_t;

typedef struct rubraview_keymap {
    rubraview_key_binding_t *bindings;
    size_t                    count;
} rubraview_keymap_t;

rubraview_keymap_t rubraview_keymap_parse(proven_arena_t *arena, proven_u8str_view_t ini_text);

/**
 * Reverse lookup: given the currently active context and a pressed key
 * combo, return the bound action id, or an empty slice if none matches.
 * Searches `context`'s bindings first, then falls back to the global
 * ("") section — a context-specific binding always wins over a global
 * one for the same key combo.
 */
proven_u8str_view_t rubraview_keymap_find_action(const rubraview_keymap_t *keymap, proven_u8str_view_t context, rubraview_key_combo_t combo);

/**
 * Forward lookup: the binding (and its full combo list) for a given
 * (context, action) pair, or NULL if unbound.
 */
const rubraview_key_binding_t *rubraview_keymap_find_binding(const rubraview_keymap_t *keymap, proven_u8str_view_t context, proven_u8str_view_t action);

/* ---- §3.22.2 tab 8: changing bindings in place (D-14) ---- */

/** A combo as keymap.ini spells it, "Ctrl+Shift+O"; returns the text written. */
proven_u8str_view_t rubraview_key_combo_format(char *buffer, size_t capacity, rubraview_key_combo_t combo);

/** Whether two combos are the same key (names compare without case). */
bool rubraview_key_combo_equal(rubraview_key_combo_t a, rubraview_key_combo_t b);

/* RV-193 (owner, 2026-10-30): the Keys page by key, each key's family together (A, Shift+A, Ctrl+A, ...). */
typedef enum rubraview_key_order {
    RUBRAVIEW_KEY_ORDER_KEYBOARD = 0,   /* as the keys lie: Esc, F1-F12, the number row, Q W E R T Y ..., then the rest */
    RUBRAVIEW_KEY_ORDER_ALPHABET,       /* A-Z, 0-9, F1-F24, then the rest as the keyboard has them */
} rubraview_key_order_t;

/**
 * <0, 0 or >0: whether `a` comes before `b` — first by its key (names without case; one this list does not
 * know after all it knows, by name), then by its modifiers: none, Shift, Ctrl, Ctrl+Shift, Alt, Shift+Alt,
 * Ctrl+Alt, all three.
 */
int rubraview_key_combo_order(rubraview_key_combo_t a, rubraview_key_combo_t b, rubraview_key_order_t order);

/**
 * Whether one key bound in both contexts would leave one of the two
 * actions unreachable. The rule follows the mode chains
 * (rubraview_keymap_mode_chain, D-219): common, file, navigation and image
 * are the layer under every kind of file, so a key in two of them always
 * reaches the same one. The slide show's, a multi-page file's, playback's,
 * a film's, music's and a text's own contexts are asked before that layer
 * only while theirs is on screen; a key they share with it keeps both
 * meanings (Space plays a film and turns a page), so that is not a clash.
 * The same context always is.
 */
bool rubraview_keymap_contexts_meet(proven_u8str_view_t a, proven_u8str_view_t b);

/* ---- RV-184 (D-219): contexts by the kind of file on screen ---- */

/** What is on screen, which decides the contexts a key is looked up in. */
typedef enum rubraview_key_mode {
    RUBRAVIEW_KEY_MODE_PICTURE = 0,   /* a picture or a comic's page */
    RUBRAVIEW_KEY_MODE_MULTIPAGE,     /* a TIFF or an ICO of several pages */
    RUBRAVIEW_KEY_MODE_ANIMATION,     /* an animated picture */
    RUBRAVIEW_KEY_MODE_FILM,
    RUBRAVIEW_KEY_MODE_MUSIC,
    RUBRAVIEW_KEY_MODE_TEXT,
    RUBRAVIEW_KEY_MODE_COUNT
} rubraview_key_mode_t;

typedef struct rubraview_key_context_info {
    const char *name;      /* the ini section; "" is common, written [common] */
    const char *heading;   /* the reader's words, for the Keymap page and F1 (N_) */
    const char *when;      /* when it is asked (N_) */
} rubraview_key_context_info_t;

/** The contexts in the order the Keymap page and F1 show them; `*count` set. */
const rubraview_key_context_info_t *rubraview_keymap_contexts(size_t *count);

/** The context's entry, or NULL for a name this build does not have. */
const rubraview_key_context_info_t *rubraview_keymap_context_info(proven_u8str_view_t context);

/**
 * The contexts `mode` asks, first to last, into `out` (at most `cap`): the slide show's first while one runs,
 * the mode's own, then navigation, file and common, and the slide show's last (so S starts one).
 * The one table dispatch, the clash rule, the Keymap page and the tests read. Returns how many.
 */
size_t rubraview_keymap_mode_chain(rubraview_key_mode_t mode, bool slideshow_running, proven_u8str_view_t *out, size_t cap);

/** The action `combo` means in `mode`: the first context of its chain that binds it; empty for none. */
proven_u8str_view_t rubraview_keymap_resolve(const rubraview_keymap_t *keymap, rubraview_key_mode_t mode, bool slideshow_running,
                                             rubraview_key_combo_t combo);

/**
 * An older keymap.ini's sections ([ui], [view], [media], [subpage], [animation]) read into today's: each binding
 * whose action `defaults` binds goes to that action's context there; any other keeps its section, renamed by
 * the old names (view → image, media and animation → playback, subpage → multipage). Changes `keymap` in place.
 */
void rubraview_keymap_place_by_action(rubraview_keymap_t *keymap, const rubraview_keymap_t *defaults);

/**
 * RV-196: an action `defaults` binds that `keymap` has nowhere (a keymap.ini saved before the action was made)
 * is added with its default keys, in its default context — each key only where no binding of a context that
 * meets that one holds it already. A file names every action it knew, a cleared one as "", so one it lacks is
 * one it never had. Returns how many actions were added; `keymap` is rebuilt in `arena`.
 */
size_t rubraview_keymap_add_missing(proven_arena_t *arena, rubraview_keymap_t *keymap, const rubraview_keymap_t *defaults);

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
 * order of first appearance, the global one as `[common]`, every value a
 * quoted comma-separated list. Parsing the result gives an equal keymap.
 */
proven_u8str_view_t rubraview_keymap_serialize(proven_arena_t *arena, const rubraview_keymap_t *keymap);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_KEYMAP_H */
