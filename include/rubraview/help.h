#ifndef RUBRAVIEW_HELP_H
#define RUBRAVIEW_HELP_H

#include "rubraview/core.h"
#include "rubraview/keymap.h"
#include "rubraview/boxes_doc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The help the reader can keep open (owner, 2026-09-23: "모달리스 창으로
 * F1 도움말 ... 가장 중요한건 단축키").
 *
 * The keys are not written down here: they are read out of the keymap
 * that is in force, so a rebound key (D-14) shows its new binding and a
 * key nobody bound shows nothing at all. The names come from the boxes
 * document — the same words the menu and the toolbox use — so one thing
 * is called one name wherever the reader meets it.
 */

typedef enum rubraview_help_line_kind {
    RUBRAVIEW_HELP_BLANK = 0,
    RUBRAVIEW_HELP_HEADING,   /* a group: "Everywhere", "Film and music", ... */
    RUBRAVIEW_HELP_ENTRY,     /* `keys` does `text` */
    RUBRAVIEW_HELP_NOTE,      /* a line of plain text */
} rubraview_help_line_kind_t;

typedef struct rubraview_help_line {
    rubraview_help_line_kind_t kind;
    u8str_t keys;   /* "Ctrl+O", or "Right, Space" when several are bound */
    u8str_t text;   /* what it does, or the heading */
} rubraview_help_line_t;

/**
 * Build the help. Returns how many lines were written (at most `max`);
 * every string is allocated from `arena` or points into the documents.
 *
 * `boxes` may be NULL, and then an action is named by its own id with the
 * underscores opened out ("next_page" -> "next page").
 */
size_t rubraview_help_build(proven_arena_t *arena,
                            const rubraview_keymap_t *keymap,
                            const rubraview_boxes_doc_t *boxes,
                            rubraview_help_line_t *out, size_t max);

/** What an action is called in the menu or the toolbox, or an empty slice. */
u8str_t rubraview_help_action_label(const rubraview_boxes_doc_t *boxes, u8str_t action);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_HELP_H */
