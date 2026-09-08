#include "rubraview/ui_menu.h"
#include <string.h>

rubraview_menu_state_t rubraview_menu_create(const rubraview_menu_tree_t *tree) {
    rubraview_menu_state_t state = { .tree = tree, .depth = 0 };
    for (int32_t i = 0; i < RUBRAVIEW_MENU_MAX_DEPTH; ++i) state.path[i] = -1;
    return state;
}

/* The item range the current level lists: the roots at depth 0, or the
   children of the item entered at the level above. */
static bool current_range(const rubraview_menu_state_t *state, int32_t *out_first, int32_t *out_count) {
    if (!state || !state->tree || !state->tree->items) return false;

    if (state->depth == 0) {
        *out_first = state->tree->root_first;
        *out_count = state->tree->root_count;
        return *out_count > 0;
    }

    int32_t parent_index = state->path[state->depth - 1];
    if (parent_index < 0 || (size_t)parent_index >= state->tree->item_count) return false;

    const rubraview_menu_item_t *parent = &state->tree->items[parent_index];
    if (parent->first_child < 0 || parent->child_count <= 0) return false;

    *out_first = parent->first_child;
    *out_count = parent->child_count;
    return true;
}

bool rubraview_menu_has_back_tile(const rubraview_menu_state_t *state) {
    return state && state->depth > 0;
}

int32_t rubraview_menu_visible_count(const rubraview_menu_state_t *state) {
    int32_t first = 0, count = 0;
    if (!current_range(state, &first, &count)) return 0;
    return count + (rubraview_menu_has_back_tile(state) ? 1 : 0);
}

const rubraview_menu_item_t *rubraview_menu_item_at(const rubraview_menu_state_t *state, int32_t tile_index) {
    int32_t first = 0, count = 0;
    if (!current_range(state, &first, &count)) return NULL;
    if (tile_index < 0) return NULL;

    if (rubraview_menu_has_back_tile(state)) {
        if (tile_index == 0) return NULL; /* the Back tile has no item */
        tile_index -= 1;
    }
    if (tile_index >= count) return NULL;

    return &state->tree->items[first + tile_index];
}

rubraview_menu_result_t rubraview_menu_activate(rubraview_menu_state_t *state, int32_t tile_index, u8str_t *out_action) {
    if (out_action) *out_action = (u8str_t){ .ptr = "", .len = 0 };
    if (!state || !state->tree) return RUBRAVIEW_MENU_NOTHING;

    if (rubraview_menu_has_back_tile(state) && tile_index == 0) {
        return rubraview_menu_back(state) ? RUBRAVIEW_MENU_WENT_BACK : RUBRAVIEW_MENU_NOTHING;
    }

    const rubraview_menu_item_t *item = rubraview_menu_item_at(state, tile_index);
    if (!item) return RUBRAVIEW_MENU_NOTHING;

    if (item->first_child >= 0 && item->child_count > 0) {
        if (state->depth >= RUBRAVIEW_MENU_MAX_DEPTH) return RUBRAVIEW_MENU_NOTHING;

        /* Record which item we entered, so the level below can list its
           children and the breadcrumb can name it. */
        int32_t first = 0, count = 0;
        if (!current_range(state, &first, &count)) return RUBRAVIEW_MENU_NOTHING;
        int32_t offset = tile_index - (rubraview_menu_has_back_tile(state) ? 1 : 0);

        state->path[state->depth] = first + offset;
        state->depth += 1;
        return RUBRAVIEW_MENU_DESCENDED;
    }

    if (out_action) *out_action = item->action;
    return RUBRAVIEW_MENU_ACTIVATED;
}

bool rubraview_menu_back(rubraview_menu_state_t *state) {
    if (!state || state->depth <= 0) return false;
    state->depth -= 1;
    state->path[state->depth] = -1;
    return true;
}

void rubraview_menu_reset(rubraview_menu_state_t *state) {
    if (!state) return;
    for (int32_t i = 0; i < RUBRAVIEW_MENU_MAX_DEPTH; ++i) state->path[i] = -1;
    state->depth = 0;
}

static size_t append_bounded(char *buffer, size_t buffer_size, size_t pos, const char *text, size_t len) {
    if (pos >= buffer_size - 1) return pos;
    size_t room = buffer_size - 1 - pos;
    size_t copy = len < room ? len : room;
    memcpy(buffer + pos, text, copy);
    return pos + copy;
}

u8str_t rubraview_menu_breadcrumb(const rubraview_menu_state_t *state, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return (u8str_t){ .ptr = "", .len = 0 };

    size_t pos = 0;
    static const char ROOT[] = "Menu";
    static const char SEPARATOR[] = " > ";

    pos = append_bounded(buffer, buffer_size, pos, ROOT, sizeof(ROOT) - 1);

    if (state && state->tree && state->tree->items) {
        for (int32_t level = 0; level < state->depth; ++level) {
            int32_t index = state->path[level];
            if (index < 0 || (size_t)index >= state->tree->item_count) break;
            const rubraview_menu_item_t *item = &state->tree->items[index];
            pos = append_bounded(buffer, buffer_size, pos, SEPARATOR, sizeof(SEPARATOR) - 1);
            pos = append_bounded(buffer, buffer_size, pos, item->label.ptr, item->label.len);
        }
    }

    buffer[pos] = '\0';
    return (u8str_t){ .ptr = buffer, .len = pos };
}
