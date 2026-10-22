#include "rubraview/boxes_doc.h"
#include "rubraview/lang.h"
#include <string.h>

/*
 * The floating boxes' document reader (RFC-0002, D-15). One line, one
 * thing; a mistake names its line and stops.
 */

typedef struct box_parser {
    rubraview_boxes_doc_t *doc;
    uint32_t line;
    int32_t profile;       /* the toolbox profile tiles go to, or -1 */
    int32_t group;         /* the group the profile's next tile goes to (D-124) */
    int32_t open_menu;     /* the innermost open menu node, or -1 */
    int32_t context;       /* RV-116: the right-click list `item` lines go to, or -1 */
    uint32_t context_line; /* where it began */
} box_parser_t;

static bool box_fail(box_parser_t *p, const char *message) {
    if (!p->doc->error) {
        p->doc->error = message;
        p->doc->error_line = p->line;
    }
    return false;
}

static bool action_name_ok(proven_u8str_view_t w) {
    if (w.size == 0) return false;
    for (size_t i = 0; i < w.size; ++i) {
        char c = w.ptr[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

/* The next word or "quoted text" from *rest; false at the end of the line. */
static bool next_token(box_parser_t *p, proven_u8str_view_t *rest, proven_u8str_view_t *out, bool *quoted) {
    size_t i = 0;
    while (i < rest->size && (rest->ptr[i] == ' ' || rest->ptr[i] == '\t' || rest->ptr[i] == '\r')) ++i;
    if (i >= rest->size || rest->ptr[i] == '#') { *rest = (proven_u8str_view_t){ .ptr = rest->ptr + rest->size, .size = 0 }; return false; }
    size_t start = i;
    if (rest->ptr[i] == '"') {
        start = ++i;
        while (i < rest->size && rest->ptr[i] != '"') ++i;
        if (i >= rest->size) { box_fail(p, "a quoted text is not closed"); return false; }
        *out = (proven_u8str_view_t){ .ptr = rest->ptr + start, .size = i - start };
        *quoted = true;
        ++i;
    } else {
        while (i < rest->size && rest->ptr[i] != ' ' && rest->ptr[i] != '\t' && rest->ptr[i] != '\r' &&
               rest->ptr[i] != '"' && rest->ptr[i] != '#') ++i;
        *out = (proven_u8str_view_t){ .ptr = rest->ptr + start, .size = i - start };
        *quoted = false;
    }
    *rest = (proven_u8str_view_t){ .ptr = rest->ptr + i, .size = rest->size - i };
    return true;
}

static bool add_box_node(box_parser_t *p, rubraview_box_node_t node) {
    if (p->doc->node_count >= RUBRAVIEW_BOXES_MAX_NODES) return box_fail(p, "too many menu lines");
    node.parent = p->open_menu;
    node.line = p->line;
    p->doc->nodes[p->doc->node_count++] = node;
    return true;
}

static bool parse_line(box_parser_t *p, proven_u8str_view_t line) {
    proven_u8str_view_t rest = line, word = {0}, second = {0}, third = {0};
    bool q1 = false, q2 = false, q3 = false;
    if (!next_token(p, &rest, &word, &q1)) return p->doc->error == NULL;
    if (q1) return box_fail(p, "a line starts with a keyword, not with quoted text");

    if (p->context >= 0) {
        /* RV-116: inside `context`: items, groups and its `end` only */
        rubraview_toolbox_profile_t *list = &p->doc->contexts[p->context];
        if (proven_u8str_view_eq(word, PROVEN_LIT("end"))) {
            p->context = -1;
        } else if (proven_u8str_view_eq(word, PROVEN_LIT("group"))) {
            if (list->tile_count > 0 && p->doc->context_items[p->doc->context_item_count - 1].group == p->group) p->group++;
        } else if (proven_u8str_view_eq(word, PROVEN_LIT("item"))) {
            if (!next_token(p, &rest, &second, &q2) || q2 || !action_name_ok(second)) return box_fail(p, "`item` needs an action id");
            if (!next_token(p, &rest, &third, &q3) || !q3) return box_fail(p, "`item` needs a \"label\"");
            if (list->tile_count >= RUBRAVIEW_CONTEXT_MAX_ITEMS) return box_fail(p, "too many items in one right-click list");
            if (p->doc->context_item_count >= RUBRAVIEW_BOXES_MAX_CONTEXT_ITEMS) return box_fail(p, "too many right-click items");
            p->doc->context_items[p->doc->context_item_count++] = (rubraview_box_tile_t){ .action = second, .caption = third, .group = p->group };
            list->tile_count++;
        } else {
            return box_fail(p, "inside `context` only `item`, `group` and `end`");
        }
        if (p->doc->error) return false;
        proven_u8str_view_t extra = {0};
        bool qe = false;
        if (next_token(p, &rest, &extra, &qe)) return box_fail(p, "more on the line than the keyword takes");
        return p->doc->error == NULL;
    }
    if (proven_u8str_view_eq(word, PROVEN_LIT("context"))) {
        if (p->open_menu >= 0) return box_fail(p, "a `context` inside a menu — close the menu with `end` first");
        if (!next_token(p, &rest, &second, &q2) || q2 || !action_name_ok(second)) return box_fail(p, "`context` needs a profile name");
        if (p->doc->context_count >= RUBRAVIEW_BOXES_MAX_PROFILES) return box_fail(p, "too many right-click lists");
        if (rubraview_boxes_context(p->doc, second)) return box_fail(p, "this right-click list is already defined");
        p->profile = -1;
        p->group = 0;
        p->context = (int32_t)p->doc->context_count;
        p->context_line = p->line;
        p->doc->contexts[p->doc->context_count++] = (rubraview_toolbox_profile_t){
            .name = second, .first_tile = (int32_t)p->doc->context_item_count, .tile_count = 0,
        };
        proven_u8str_view_t extra = {0};
        bool qe = false;
        if (next_token(p, &rest, &extra, &qe)) return box_fail(p, "more on the line than the keyword takes");
        return p->doc->error == NULL;
    }
    if (proven_u8str_view_eq(word, PROVEN_LIT("toolbox"))) {
        if (p->open_menu >= 0) return box_fail(p, "a toolbox inside a menu — close the menu with `end` first");
        if (!next_token(p, &rest, &second, &q2) || q2 || !action_name_ok(second)) return box_fail(p, "`toolbox` needs a profile name");
        if (p->doc->profile_count >= RUBRAVIEW_BOXES_MAX_PROFILES) return box_fail(p, "too many toolbox profiles");
        for (size_t i = 0; i < p->doc->profile_count; ++i) {
            if (p->doc->profiles[i].name.size == second.size && memcmp(p->doc->profiles[i].name.ptr, second.ptr, second.size) == 0) {
                return box_fail(p, "this toolbox profile is already defined");
            }
        }
        p->profile = (int32_t)p->doc->profile_count;
        p->group = 0;
        p->doc->profiles[p->doc->profile_count++] = (rubraview_toolbox_profile_t){
            .name = second, .first_tile = (int32_t)p->doc->tile_count, .tile_count = 0,
        };
        return true;
    }
    if (proven_u8str_view_eq(word, PROVEN_LIT("group"))) {
        if (p->profile < 0) return box_fail(p, "a `group` before any `toolbox`");
        /* A group with no tile yet is not counted: two `group` lines are one. */
        const rubraview_toolbox_profile_t *profile = &p->doc->profiles[p->profile];
        if (profile->tile_count > 0 && p->doc->tiles[p->doc->tile_count - 1].group == p->group) p->group++;
        return true;
    }
    if (proven_u8str_view_eq(word, PROVEN_LIT("tile"))) {
        if (p->profile < 0) return box_fail(p, "a `tile` before any `toolbox`");
        if (!next_token(p, &rest, &second, &q2) || q2 || !action_name_ok(second)) return box_fail(p, "`tile` needs an action id");
        if (!next_token(p, &rest, &third, &q3) || !q3) return box_fail(p, "`tile` needs a \"caption\"");
        rubraview_toolbox_profile_t *profile = &p->doc->profiles[p->profile];
        if (profile->tile_count >= RUBRAVIEW_TOOLBOX_MAX_TILES) return box_fail(p, "too many tiles in one toolbox profile");
        if (p->doc->tile_count >= RUBRAVIEW_BOXES_MAX_TILES) return box_fail(p, "too many tiles");
        p->doc->tiles[p->doc->tile_count++] = (rubraview_box_tile_t){ .action = second, .caption = third, .group = p->group };
        profile->tile_count++;
    } else if (proven_u8str_view_eq(word, PROVEN_LIT("menu"))) {
        p->profile = -1;
        if (!next_token(p, &rest, &second, &q2) || !q2) return box_fail(p, "`menu` needs a \"label\"");
        rubraview_box_node_t node = { .kind = RUBRAVIEW_BOX_NODE_MENU, .label = second };
        if (next_token(p, &rest, &third, &q3)) {
            proven_u8str_view_t condition = {0};
            bool qc = false;
            if (q3 || !proven_u8str_view_eq(third, PROVEN_LIT("when")) || !next_token(p, &rest, &condition, &qc) || qc) {
                return box_fail(p, "after a menu's label only `when <condition>` may follow");
            }
            if (proven_u8str_view_eq(condition, PROVEN_LIT("media"))) node.when = RUBRAVIEW_BOXES_WHEN_MEDIA;
            else if (proven_u8str_view_eq(condition, PROVEN_LIT("plays"))) node.when = RUBRAVIEW_BOXES_WHEN_PLAYS;
            else return box_fail(p, "the conditions are `when media` and `when plays`");
        }
        if (p->doc->error) return false;
        if (!add_box_node(p, node)) return false;
        p->open_menu = (int32_t)p->doc->node_count - 1;
        return true;
    } else if (proven_u8str_view_eq(word, PROVEN_LIT("item"))) {
        if (!next_token(p, &rest, &second, &q2) || q2 || !action_name_ok(second)) return box_fail(p, "`item` needs an action id");
        if (!next_token(p, &rest, &third, &q3) || !q3) return box_fail(p, "`item` needs a \"label\"");
        if (!add_box_node(p, (rubraview_box_node_t){ .kind = RUBRAVIEW_BOX_NODE_ITEM, .label = third, .action = second })) return false;
    } else if (proven_u8str_view_eq(word, PROVEN_LIT("recent"))) {
        if (p->open_menu < 0) return box_fail(p, "`recent` outside any `menu`");
        if (!add_box_node(p, (rubraview_box_node_t){ .kind = RUBRAVIEW_BOX_NODE_RECENT })) return false;
    } else if (proven_u8str_view_eq(word, PROVEN_LIT("end"))) {
        if (p->open_menu < 0) return box_fail(p, "an `end` with no open `menu`");
        p->open_menu = p->doc->nodes[p->open_menu].parent;
    } else {
        return box_fail(p, "unknown keyword (toolbox, tile, context, menu, item, recent, end)");
    }
    if (p->doc->error) return false;
    proven_u8str_view_t extra = {0};
    bool qe = false;
    if (next_token(p, &rest, &extra, &qe)) return box_fail(p, "more on the line than the keyword takes");
    return p->doc->error == NULL;
}

bool rubraview_boxes_doc_parse(proven_u8str_view_t text, rubraview_boxes_doc_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    box_parser_t p = { .doc = out, .line = 0, .profile = -1, .open_menu = -1, .context = -1 };
    size_t start = 0;
    for (size_t i = 0; i <= text.size; ++i) {
        if (i < text.size && text.ptr[i] != '\n') continue;
        p.line++;
        if (!parse_line(&p, (proven_u8str_view_t){ .ptr = text.ptr + start, .size = i - start })) return false;
        start = i + 1;
    }
    if (p.open_menu >= 0) {
        p.line = out->nodes[p.open_menu].line;
        return box_fail(&p, "this `menu` is never closed with `end`");
    }
    if (p.context >= 0) {
        p.line = p.context_line;
        return box_fail(&p, "a `context` is never closed with `end`");
    }
    return true;
}

static rubraview_boxes_doc_t g_boxes;
static bool g_boxes_ready;

const rubraview_boxes_doc_t *rubraview_boxes_document(void) {
    if (!g_boxes_ready) {
        rubraview_boxes_doc_parse(rubraview_default_boxes_document(), &g_boxes);
        g_boxes_ready = true;
    }
    return &g_boxes;
}

const rubraview_toolbox_profile_t *rubraview_boxes_profile(const rubraview_boxes_doc_t *doc, proven_u8str_view_t name) {
    if (!doc) return NULL;
    for (size_t i = 0; i < doc->profile_count; ++i) {
        if (doc->profiles[i].name.size == name.size && memcmp(doc->profiles[i].name.ptr, name.ptr, name.size) == 0) {
            return &doc->profiles[i];
        }
    }
    return NULL;
}

const rubraview_toolbox_profile_t *rubraview_boxes_context(const rubraview_boxes_doc_t *doc, proven_u8str_view_t name) {
    if (!doc) return NULL;
    for (size_t i = 0; i < doc->context_count; ++i) {
        if (doc->contexts[i].name.size == name.size && memcmp(doc->contexts[i].name.ptr, name.ptr, name.size) == 0) {
            return &doc->contexts[i];
        }
    }
    return NULL;
}

/* ---- the menu tree for this moment ---- */

/* A node, or a recent entry standing in for a RECENT node, as it will be shown. */
typedef struct shown {
    int32_t node;      /* the document node */
    int32_t recent;    /* >= 0: this is recent entry `recent`, not the node itself */
} shown_t;

static bool node_visible(const rubraview_boxes_doc_t *doc, int32_t node, uint32_t when) {
    for (int32_t n = node; n >= 0; n = doc->nodes[n].parent) {
        if ((doc->nodes[n].when & ~when) != 0) return false;
    }
    return true;
}

/* The shown children of `parent` (-1 = the top), in document order, RECENT expanded. */
static size_t children_of(const rubraview_boxes_doc_t *doc, int32_t parent, uint32_t when,
                          size_t recent_count, shown_t *out, size_t capacity) {
    size_t n = 0;
    for (size_t i = 0; i < doc->node_count; ++i) {
        if (doc->nodes[i].parent != parent || !node_visible(doc, (int32_t)i, when)) continue;
        if (doc->nodes[i].kind == RUBRAVIEW_BOX_NODE_RECENT) {
            for (size_t r = 0; r < recent_count && n < capacity; ++r) out[n++] = (shown_t){ (int32_t)i, (int32_t)r };
        } else if (n < capacity) {
            out[n++] = (shown_t){ (int32_t)i, -1 };
        }
    }
    return n;
}

rubraview_menu_tree_t rubraview_boxes_menu(proven_arena_t *arena, const rubraview_boxes_doc_t *doc, uint32_t when,
                                           const rubraview_recent_entry_t *recent, size_t recent_count,
                                           int32_t max_children) {
    rubraview_menu_tree_t empty = {0};
    if (!arena || !doc || doc->error || max_children <= 0) return empty;
    if (!recent) recent_count = 0;

    size_t capacity = doc->node_count + recent_count + 1;
    proven_result_mem_mut_t items_mem = rubraview_arena_alloc_array(arena, capacity, sizeof(rubraview_menu_item_t));
    proven_result_mem_mut_t queue_mem = rubraview_arena_alloc_array(arena, capacity, sizeof(shown_t));
    proven_result_mem_mut_t kids_mem = rubraview_arena_alloc_array(arena, capacity, sizeof(shown_t));
    if (!proven_is_ok(items_mem.err) || !proven_is_ok(queue_mem.err) || !proven_is_ok(kids_mem.err)) return empty;
    rubraview_menu_item_t *items = (rubraview_menu_item_t*)(void*)items_mem.value.ptr;
    shown_t *queue = (shown_t*)(void*)queue_mem.value.ptr;
    shown_t *kids = (shown_t*)(void*)kids_mem.value.ptr;

    /* Breadth first: the top level first, then each item's children placed
       together at the end — which is what makes them contiguous. */
    size_t count = children_of(doc, -1, when, recent_count, queue, capacity);
    if ((int32_t)count > max_children) count = (size_t)max_children;
    int32_t root_count = (int32_t)count;
    for (size_t i = 0; i < count; ++i) {
        const shown_t s = queue[i];
        const rubraview_box_node_t *node = &doc->nodes[s.node];
        rubraview_menu_item_t item = { .first_child = -1, .child_count = 0 };
        if (s.recent >= 0) {
            item.label = recent[s.recent].label;
            item.action = recent[s.recent].action;
        } else {
            item.label = rubraview_tr_ctx_u8("menu", node->label);   /* RV-114: built again when the language changes */
            item.action = node->kind == RUBRAVIEW_BOX_NODE_ITEM ? node->action : PROVEN_LIT("");
            if (node->kind == RUBRAVIEW_BOX_NODE_MENU) {
                size_t k = children_of(doc, s.node, when, recent_count, kids, capacity);
                if ((int32_t)k > max_children) k = (size_t)max_children;
                if (count + k > capacity) k = capacity - count;
                item.first_child = (int32_t)count;
                item.child_count = (int32_t)k;
                for (size_t c = 0; c < k; ++c) queue[count++] = kids[c];
            }
        }
        items[i] = item;
    }
    return (rubraview_menu_tree_t){ .items = items, .item_count = count, .root_first = 0, .root_count = root_count };
}
