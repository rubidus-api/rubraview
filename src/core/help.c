#include "rubraview/help.h"
#include "rubraview/lang.h"
#include "rubraview/action_words.h"
#include <stdio.h>
#include <string.h>

static proven_u8str_view_t arena_text(proven_arena_t *arena, const char *text, size_t len) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len + 1);
    if (!proven_is_ok(res.err)) return PROVEN_LIT("");
    char *out = (char*)res.value.ptr;
    memcpy(out, text, len);
    out[len] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = len };
}

proven_u8str_view_t rubraview_help_action_label(const rubraview_boxes_doc_t *boxes, proven_u8str_view_t action) {
    proven_u8str_view_t none = PROVEN_LIT("");
    if (!boxes || action.size == 0) return none;
    /* The menu first: its labels are sentences ("Open folder"), while a
       tile's caption is squeezed to fit ("Next"). */
    for (size_t i = 0; i < boxes->node_count; ++i) {
        const rubraview_box_node_t *node = &boxes->nodes[i];
        if (node->kind != RUBRAVIEW_BOX_NODE_ITEM) continue;
        if (node->action.size == action.size && memcmp(node->action.ptr, action.ptr, action.size) == 0) {
            return rubraview_tr_ctx_u8("menu", node->label);
        }
    }
    /* 1.23.0: the built-in menu is gone; the words the key tables say it in are the sentence now. */
    const char *words = rubraview_action_words(action);
    if (words) return rubraview_tr_u8((proven_u8str_view_t){ .ptr = (const proven_byte_t *)words, .size = strlen(words) });
    for (size_t i = 0; i < boxes->tile_count; ++i) {
        const rubraview_box_tile_t *tile = &boxes->tiles[i];
        if (tile->action.size == action.size && memcmp(tile->action.ptr, action.ptr, action.size) == 0) {
            return rubraview_tr_ctx_u8("toolbox", tile->caption);
        }
    }
    return none;
}

/* "next_page" -> "next page", for an action no box names. */
static proven_u8str_view_t spell_out(proven_arena_t *arena, proven_u8str_view_t action) {
    proven_u8str_view_t text = arena_text(arena, (const char *)action.ptr, action.size);
    char *out = (char*)text.ptr;
    for (size_t i = 0; i < text.size; ++i) {
        if (out[i] == '_') out[i] = ' ';
    }
    /* A sentence, like the labels beside it: "toggle menu" -> "Toggle menu". */
    if (text.size > 0 && out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
    return text;
}

/* Every combo this binding has, "Right, Space". */
static proven_u8str_view_t combo_list(proven_arena_t *arena, const rubraview_key_binding_t *binding) {
    char line[160];
    size_t at = 0;
    line[0] = '\0';
    for (size_t i = 0; i < binding->combo_count && at + 2 < sizeof(line); ++i) {
        char one[64];
        proven_u8str_view_t text = rubraview_key_combo_format(one, sizeof(one), binding->combos[i]);
        if (text.size == 0) continue;
        if (at > 0) {
            line[at++] = ',';
            line[at++] = ' ';
        }
        size_t room = sizeof(line) - at - 1;
        size_t n = text.size < room ? text.size : room;
        memcpy(line + at, text.ptr, n);
        at += n;
    }
    line[at] = '\0';
    return arena_text(arena, line, at);
}

static proven_u8str_view_t lit_text(const char *text) {
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = strlen(text) };
}

/* Writes one line while there is room; false when the caller's array is full. */
static bool push(rubraview_help_line_t *out, size_t max, size_t *count,
                 rubraview_help_line_kind_t kind, proven_u8str_view_t keys, proven_u8str_view_t text) {
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

    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!push(out, max, &count, RUBRAVIEW_HELP_NOTE, empty, lit_text(T("? (Shift+/) opens and closes this window. It stays open while you read.")))) return count;
    if (!push(out, max, &count, RUBRAVIEW_HELP_NOTE, empty, lit_text(T("Every key below can be changed on the settings window's Keys page.")))) return count;

    /* RV-184 (D-219): the keymap's contexts in their order, each named with when it is asked; then anything
       else the keymap has, under its own name. */
    size_t group_count = 0;
    const rubraview_key_context_info_t *groups = rubraview_keymap_contexts(&group_count);
    for (size_t pass = 0; pass <= group_count; ++pass) {
        bool wrote_heading = false;
        for (size_t i = 0; i < keymap->count; ++i) {
            const rubraview_key_binding_t *binding = &keymap->bindings[i];
            if (binding->combo_count == 0) continue;

            if (pass < group_count) {
                if (!proven_u8str_view_eq(binding->context, proven_u8str_view_from_cstr(groups[pass].name))) continue;
            } else if (rubraview_keymap_context_info(binding->context)) {
                continue;
            }

            if (!wrote_heading) {
                proven_u8str_view_t heading = binding->context;
                if (pass < group_count) {
                    char line[160];
                    int n = snprintf(line, sizeof(line), "%s (%s)", rubraview_tr(groups[pass].heading), rubraview_tr(groups[pass].when));
                    heading = arena_text(arena, line, n > 0 ? ((size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1) : 0);
                }
                if (!push(out, max, &count, RUBRAVIEW_HELP_BLANK, empty, empty)) return count;
                if (!push(out, max, &count, RUBRAVIEW_HELP_HEADING, empty, heading)) return count;
                wrote_heading = true;
            }

            proven_u8str_view_t label = rubraview_help_action_label(boxes, binding->action);
            if (label.size == 0) label = spell_out(arena, binding->action);
            if (!push(out, max, &count, RUBRAVIEW_HELP_ENTRY, combo_list(arena, binding), label)) return count;
        }
    }

    /* RV-184 (owner, 2026-10-29: "키->기능 화면도"): each key once, with what it does where — the first time it is
       met in the keymap's order, every context that binds it after the first named. */
    if (!push(out, max, &count, RUBRAVIEW_HELP_BLANK, empty, empty)) return count;
    if (!push(out, max, &count, RUBRAVIEW_HELP_HEADING, empty, lit_text(T("By key")))) return count;
    for (size_t i = 0; i < keymap->count; ++i) {
        for (size_t c = 0; c < keymap->bindings[i].combo_count; ++c) {
            rubraview_key_combo_t combo = keymap->bindings[i].combos[c];
            bool seen = false;
            for (size_t e = 0; e < i && !seen; ++e)
                for (size_t f = 0; f < keymap->bindings[e].combo_count && !seen; ++f) seen = rubraview_key_combo_equal(keymap->bindings[e].combos[f], combo);
            for (size_t f = 0; f < c && !seen; ++f) seen = rubraview_key_combo_equal(keymap->bindings[i].combos[f], combo);
            if (seen) continue;
            char line[512];
            size_t at = 0;
            line[0] = '\0';
            for (size_t e = i; e < keymap->count; ++e) {
                const rubraview_key_binding_t *b = &keymap->bindings[e];
                bool binds = false;
                for (size_t f = 0; f < b->combo_count && !binds; ++f) binds = rubraview_key_combo_equal(b->combos[f], combo);
                if (!binds) continue;
                proven_u8str_view_t label = rubraview_help_action_label(boxes, b->action);
                if (label.size == 0) label = spell_out(arena, b->action);
                const rubraview_key_context_info_t *info = rubraview_keymap_context_info(b->context);
                proven_u8str_view_t where = info ? lit_text(rubraview_tr(info->heading)) : b->context;
                int n = snprintf(line + at, sizeof(line) - at, "%s%.*s (%.*s)", at ? "  ·  " : "", (int)label.size, (const char *)label.ptr,
                                 (int)where.size, (const char *)where.ptr);
                if (n <= 0 || (size_t)n >= sizeof(line) - at) break;
                at += (size_t)n;
            }
            char key[64];
            proven_u8str_view_t named = rubraview_key_combo_format(key, sizeof(key), combo);
            if (!push(out, max, &count, RUBRAVIEW_HELP_ENTRY, arena_text(arena, (const char *)named.ptr, named.size),
                      arena_text(arena, line, at))) return count;
        }
    }

    return count;
}
