#ifndef RUBRAVIEW_BOXES_DOC_H
#define RUBRAVIEW_BOXES_DOC_H

#include "rubraview/core.h"
#include "rubraview/ui_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What the two floating boxes hold (RFC-0002, D-15), as a small internal
 * document — the toolbox's tiles for each kind of page, and the menu
 * box's tree — read by one interpreter, the way D-13 did the settings
 * window.
 *
 *     # a comment
 *     toolbox video
 *       tile media_play_pause "Play"
 *       tile media_stop       "Stop"
 *     menu "File"
 *       item open_picker "Open file"
 *       menu "Recent"
 *         recent
 *       end
 *     end
 *     menu "Playback" when media
 *       item media_play_pause "Play/Pause"
 *     end
 *
 * `toolbox <profile>` starts a profile; its `tile` lines follow, each an
 * action id and a caption. `menu "<label>"` opens a submenu, `end` closes
 * it; menus nest. `item` is a leaf. `recent` stands for the reading
 * history's entries, filled in when the menu is built. `when media` shows
 * a menu only while a video or music page is on screen.
 *
 * Every action id is checked against what the viewer handles by
 * scripts/check-actions.py.
 */

typedef struct rubraview_box_tile {
    u8str_t action;
    u8str_t caption;
} rubraview_box_tile_t;

typedef struct rubraview_toolbox_profile {
    u8str_t name;
    int32_t first_tile;   /* index into the document's tiles */
    int32_t tile_count;
} rubraview_toolbox_profile_t;

typedef enum rubraview_box_node_kind {
    RUBRAVIEW_BOX_NODE_MENU = 0,
    RUBRAVIEW_BOX_NODE_ITEM,
    RUBRAVIEW_BOX_NODE_RECENT,
} rubraview_box_node_kind_t;

#define RUBRAVIEW_BOXES_WHEN_MEDIA 1u   /* a video or music page is on screen */

typedef struct rubraview_box_node {
    rubraview_box_node_kind_t kind;
    u8str_t  label;
    u8str_t  action;      /* ITEM only */
    int32_t  parent;      /* index of the enclosing MENU node, or -1 at the top */
    uint32_t when;        /* RUBRAVIEW_BOXES_WHEN_* the node needs, 0 for always */
    uint32_t line;
} rubraview_box_node_t;

#define RUBRAVIEW_BOXES_MAX_TILES 128
#define RUBRAVIEW_BOXES_MAX_PROFILES 16
#define RUBRAVIEW_BOXES_MAX_NODES 160
#define RUBRAVIEW_TOOLBOX_MAX_TILES 12    /* in one profile */

typedef struct rubraview_boxes_doc {
    rubraview_box_tile_t        tiles[RUBRAVIEW_BOXES_MAX_TILES];
    size_t                      tile_count;
    rubraview_toolbox_profile_t profiles[RUBRAVIEW_BOXES_MAX_PROFILES];
    size_t                      profile_count;
    rubraview_box_node_t        nodes[RUBRAVIEW_BOXES_MAX_NODES];
    size_t                      node_count;
    const char                 *error;       /* NULL when it parsed */
    uint32_t                    error_line;
} rubraview_boxes_doc_t;

/** Parse into `out`; the strings point into `text`, which must outlive it. False with `out->error` set on the first mistake. */
bool rubraview_boxes_doc_parse(u8str_t text, rubraview_boxes_doc_t *out);

/** The document compiled into the viewer. */
u8str_t rubraview_default_boxes_document(void);

/** The built-in document, parsed once. Call from the main thread. */
const rubraview_boxes_doc_t *rubraview_boxes_document(void);

/** A profile by name, or NULL. */
const rubraview_toolbox_profile_t *rubraview_boxes_profile(const rubraview_boxes_doc_t *doc, u8str_t name);

/** One reading-history entry, as a `recent` line turns it into an item. */
typedef struct rubraview_recent_entry {
    u8str_t label;    /* what the tile says */
    u8str_t action;   /* what it does, e.g. "open_recent:3" — the caller makes it */
} rubraview_recent_entry_t;

/**
 * The menu tree for this moment: menus whose `when` is not met left out,
 * `recent` replaced by `recent` entries. Children are contiguous, as
 * rubraview_menu_tree_t needs. Everything lives in `arena`; building again
 * into a reset arena is how the tree is refreshed. A menu level holding
 * more than `max_children` keeps the first ones. Returns an empty tree
 * when the arena is too small.
 */
rubraview_menu_tree_t rubraview_boxes_menu(proven_arena_t *arena, const rubraview_boxes_doc_t *doc, uint32_t when,
                                           const rubraview_recent_entry_t *recent, size_t recent_count,
                                           int32_t max_children);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_BOXES_DOC_H */
