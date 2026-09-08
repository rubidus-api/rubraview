#ifndef RUBRAVIEW_UI_MENU_H
#define RUBRAVIEW_UI_MENU_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The In-Window Menu Box's hierarchical tile grid (RFC-0001 §3.6.2):
 * a multi-level drill-down where the top-left tile becomes a `Back`
 * tile below the root, plus the breadcrumb string shown along the top
 * border. The tree is static data the application declares; this module
 * holds only the navigation state.
 */

typedef struct rubraview_menu_item {
    u8str_t label;          /* tile caption, e.g. "Layout" */
    u8str_t action;         /* action id dispatched on tap; empty for a submenu */
    int32_t first_child;    /* index of the first child item, or -1 for a leaf */
    int32_t child_count;
} rubraview_menu_item_t;

typedef struct rubraview_menu_tree {
    const rubraview_menu_item_t *items; /* flat array; children are contiguous */
    size_t item_count;
    int32_t root_first;                 /* index of the first root item */
    int32_t root_count;
} rubraview_menu_tree_t;

#define RUBRAVIEW_MENU_MAX_DEPTH 8

typedef struct rubraview_menu_state {
    const rubraview_menu_tree_t *tree;
    int32_t path[RUBRAVIEW_MENU_MAX_DEPTH]; /* item index entered at each level */
    int32_t depth;                          /* 0 = root */
} rubraview_menu_state_t;

rubraview_menu_state_t rubraview_menu_create(const rubraview_menu_tree_t *tree);

/** How many tiles the current level shows, including the Back tile below the root. */
int32_t rubraview_menu_visible_count(const rubraview_menu_state_t *state);

/** True when tile 0 of the current level is the `[ Back ]` tile (§3.6.2). */
bool rubraview_menu_has_back_tile(const rubraview_menu_state_t *state);

/** The item a visible tile refers to, or NULL for the Back tile. */
const rubraview_menu_item_t *rubraview_menu_item_at(const rubraview_menu_state_t *state, int32_t tile_index);

typedef enum rubraview_menu_result {
    RUBRAVIEW_MENU_NOTHING = 0,
    RUBRAVIEW_MENU_DESCENDED,  /* entered a submenu */
    RUBRAVIEW_MENU_WENT_BACK,
    RUBRAVIEW_MENU_ACTIVATED,  /* a leaf was tapped; `out_action` names it */
} rubraview_menu_result_t;

/**
 * Tap a visible tile: descends into a submenu, goes back a level, or
 * reports the leaf's action id.
 */
rubraview_menu_result_t rubraview_menu_activate(rubraview_menu_state_t *state, int32_t tile_index, u8str_t *out_action);

/** Go up one level; false at the root. */
bool rubraview_menu_back(rubraview_menu_state_t *state);

/** Return to the root (on dismiss, so the menu reopens where it started). */
void rubraview_menu_reset(rubraview_menu_state_t *state);

/**
 * §3.6.2: the breadcrumb, e.g. "Menu > Adjust > Tone Curves", written
 * into `buffer` and returned as a slice. Always NUL-terminated.
 */
u8str_t rubraview_menu_breadcrumb(const rubraview_menu_state_t *state, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_MENU_H */
