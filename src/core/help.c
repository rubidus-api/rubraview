#include "rubraview/help.h"
#include <stdio.h>
#include <string.h>

/* The contexts, in the order the help shows them, each with the words a
   reader would use for it. A context the keymap has and this table does
   not still appears, under its own name. */
static const struct { const char *context; const char *heading; } GROUPS[] = {
    { "",           "Everywhere" },
    { "navigation", "Moving about" },
    { "view",       "Looking at a picture" },
    { "media",      "Film and music" },
    { "subpage",    "Pages inside one file" },
    { "slideshow",  "Slide show" },
};

static u8str_t arena_text(proven_arena_t *arena, const char *text, size_t len) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    char *out = (char*)res.value.ptr;
    memcpy(out, text, len);
    out[len] = '\0';
    return (u8str_t){ .ptr = out, .len = len };
}

u8str_t rubraview_help_action_label(const rubraview_boxes_doc_t *boxes, u8str_t action) {
    u8str_t none = { .ptr = "", .len = 0 };
    if (!boxes || action.len == 0) return none;
    /* The menu first: its labels are sentences ("Open folder"), while a
       tile's caption is squeezed to fit ("Next"). */
    for (size_t i = 0; i < boxes->node_count; ++i) {
        const rubraview_box_node_t *node = &boxes->nodes[i];
        if (node->kind != RUBRAVIEW_BOX_NODE_ITEM) continue;
        if (node->action.len == action.len && memcmp(node->action.ptr, action.ptr, action.len) == 0) {
            return node->label;
        }
    }
    for (size_t i = 0; i < boxes->tile_count; ++i) {
        const rubraview_box_tile_t *tile = &boxes->tiles[i];
        if (tile->action.len == action.len && memcmp(tile->action.ptr, action.ptr, action.len) == 0) {
            return tile->caption;
        }
    }
    return none;
}

/* "next_page" -> "next page", for an action no box names. */
static u8str_t spell_out(proven_arena_t *arena, u8str_t action) {
    u8str_t text = arena_text(arena, action.ptr, action.len);
    char *out = (char*)text.ptr;
    for (size_t i = 0; i < text.len; ++i) {
        if (out[i] == '_') out[i] = ' ';
    }
    /* A sentence, like the labels beside it: "toggle menu" -> "Toggle menu". */
    if (text.len > 0 && out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
    return text;
}

/* Every combo this binding has, "Right, Space". */
static u8str_t combo_list(proven_arena_t *arena, const rubraview_key_binding_t *binding) {
    char line[160];
    size_t at = 0;
    line[0] = '\0';
    for (size_t i = 0; i < binding->combo_count && at + 2 < sizeof(line); ++i) {
        char one[64];
        u8str_t text = rubraview_key_combo_format(one, sizeof(one), binding->combos[i]);
        if (text.len == 0) continue;
        if (at > 0) {
            line[at++] = ',';
            line[at++] = ' ';
        }
        size_t room = sizeof(line) - at - 1;
        size_t n = text.len < room ? text.len : room;
        memcpy(line + at, text.ptr, n);
        at += n;
    }
    line[at] = '\0';
    return arena_text(arena, line, at);
}

static u8str_t lit_text(const char *text) {
    return (u8str_t){ .ptr = text, .len = strlen(text) };
}

/* Writes one line while there is room; false when the caller's array is full. */
static bool push(rubraview_help_line_t *out, size_t max, size_t *count,
                 rubraview_help_line_kind_t kind, u8str_t keys, u8str_t text) {
    if (*count >= max) return false;
    out[*count].kind = kind;
    out[*count].keys = keys;
    out[*count].text = text;
    (*count)++;
    return true;
}

size_t rubraview_help_build(proven_arena_t *arena,
                            const rubraview_keymap_t *keymap,
                            const rubraview_boxes_doc_t *boxes,
                            rubraview_help_line_t *out, size_t max) {
    if (!arena || !keymap || !out || max == 0) return 0;
    size_t count = 0;

    u8str_t empty = { .ptr = "", .len = 0 };
    if (!push(out, max, &count, RUBRAVIEW_HELP_NOTE, empty, lit_text("F1 opens and closes this window. It stays open while you read."))) return count;
    if (!push(out, max, &count, RUBRAVIEW_HELP_NOTE, empty, lit_text("Every key below can be changed on the settings window's Keys page."))) return count;

    size_t group_count = sizeof(GROUPS) / sizeof(GROUPS[0]);
    /* The known groups in their order, then anything else the keymap has. */
    for (size_t pass = 0; pass <= group_count; ++pass) {
        bool wrote_heading = false;
        for (size_t i = 0; i < keymap->count; ++i) {
            const rubraview_key_binding_t *binding = &keymap->bindings[i];
            if (binding->combo_count == 0) continue;

            if (pass < group_count) {
                if (!rubraview_u8_eq_lit(binding->context, GROUPS[pass].context)) continue;
            } else {
                bool known = false;
                for (size_t g = 0; g < group_count && !known; ++g) known = rubraview_u8_eq_lit(binding->context, GROUPS[g].context);
                if (known) continue;
            }

            if (!wrote_heading) {
                u8str_t heading = pass < group_count ? lit_text(GROUPS[pass].heading) : binding->context;
                if (!push(out, max, &count, RUBRAVIEW_HELP_BLANK, empty, empty)) return count;
                if (!push(out, max, &count, RUBRAVIEW_HELP_HEADING, empty, heading)) return count;
                wrote_heading = true;
            }

            u8str_t label = rubraview_help_action_label(boxes, binding->action);
            if (label.len == 0) label = spell_out(arena, binding->action);
            if (!push(out, max, &count, RUBRAVIEW_HELP_ENTRY, combo_list(arena, binding), label)) return count;
        }
    }

    return count;
}
