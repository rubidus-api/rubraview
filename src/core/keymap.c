#include "rubraview/keymap.h"
#include "rubraview/ini.h"
#include <string.h>
#include <ctype.h>

static u8str_t trim(const char *ptr, size_t len) {
    size_t start = 0, end = len;
    while (start < end && (ptr[start] == ' ' || ptr[start] == '\t')) start++;
    while (end > start && (ptr[end - 1] == ' ' || ptr[end - 1] == '\t')) end--;
    return (u8str_t){ .ptr = ptr + start, .len = end - start };
}

static bool word_ci_eq(u8str_t word, const char *lit) {
    size_t n = strlen(lit);
    if (word.len != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (tolower((unsigned char)word.ptr[i]) != tolower((unsigned char)lit[i])) return false;
    }
    return true;
}

static rubraview_key_combo_t parse_combo(u8str_t token) {
    rubraview_key_combo_t combo = { .modifiers = 0, .key_name = token };

    while (true) {
        size_t plus = (size_t)-1;
        for (size_t i = 0; i < combo.key_name.len; ++i) {
            if (combo.key_name.ptr[i] == '+') { plus = i; break; }
        }
        if (plus == (size_t)-1) break;

        u8str_t word = { .ptr = combo.key_name.ptr, .len = plus };
        u8str_t rest = { .ptr = combo.key_name.ptr + plus + 1, .len = combo.key_name.len - plus - 1 };

        uint32_t mod = 0;
        if (word_ci_eq(word, "shift")) mod = RUBRAVIEW_MOD_SHIFT;
        else if (word_ci_eq(word, "ctrl") || word_ci_eq(word, "control")) mod = RUBRAVIEW_MOD_CTRL;
        else if (word_ci_eq(word, "alt")) mod = RUBRAVIEW_MOD_ALT;
        else break; /* not a recognized modifier word: stop peeling */

        combo.modifiers |= mod;
        combo.key_name = rest;
    }

    return combo;
}

typedef struct combo_buf {
    rubraview_key_combo_t *data;
    size_t count;
    size_t capacity;
} combo_buf_t;

static bool combo_buf_push(proven_arena_t *arena, combo_buf_t *b, rubraview_key_combo_t combo) {
    if (b->count >= b->capacity) {
        size_t new_cap = b->capacity == 0 ? 4 : b->capacity * 2;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap * sizeof(rubraview_key_combo_t));
        if (!proven_is_ok(res.err)) return false;
        rubraview_key_combo_t *new_data = (rubraview_key_combo_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(new_data, b->data, b->count * sizeof(rubraview_key_combo_t));
        b->data = new_data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = combo;
    return true;
}

static void parse_combo_list(proven_arena_t *arena, u8str_t value, rubraview_key_binding_t *binding) {
    combo_buf_t buf = {0};
    size_t start = 0;
    for (size_t i = 0; i <= value.len; ++i) {
        if (i == value.len || value.ptr[i] == ',') {
            u8str_t token = trim(value.ptr + start, i - start);
            if (token.len > 0) {
                combo_buf_push(arena, &buf, parse_combo(token));
            }
            start = i + 1;
        }
    }
    binding->combos = buf.data;
    binding->combo_count = buf.count;
}

typedef struct binding_buf {
    rubraview_key_binding_t *data;
    size_t count;
    size_t capacity;
} binding_buf_t;

static bool binding_buf_push(proven_arena_t *arena, binding_buf_t *b, rubraview_key_binding_t binding) {
    if (b->count >= b->capacity) {
        size_t new_cap = b->capacity == 0 ? 8 : b->capacity * 2;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap * sizeof(rubraview_key_binding_t));
        if (!proven_is_ok(res.err)) return false;
        rubraview_key_binding_t *new_data = (rubraview_key_binding_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(new_data, b->data, b->count * sizeof(rubraview_key_binding_t));
        b->data = new_data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = binding;
    return true;
}

rubraview_keymap_t rubraview_keymap_parse(proven_arena_t *arena, u8str_t ini_text) {
    rubraview_keymap_t keymap = {0};
    if (!arena) return keymap;

    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, ini_text);

    binding_buf_t buf = {0};
    for (size_t i = 0; i < doc.count; ++i) {
        /* D-13: `[ui]` is the context everything falls back to. Before it
           had a name those bindings sat above the first section, and an
           older keymap.ini that still does that reads the same way. */
        u8str_t context = doc.entries[i].section;
        if (context.len == 2 && memcmp(context.ptr, "ui", 2) == 0) context = (u8str_t){ .ptr = "", .len = 0 };
        rubraview_key_binding_t binding = {
            .context = context,
            .action = doc.entries[i].key,
            .combos = NULL,
            .combo_count = 0,
        };
        parse_combo_list(arena, doc.entries[i].value, &binding);
        binding_buf_push(arena, &buf, binding);
    }

    keymap.bindings = buf.data;
    keymap.count = buf.count;
    return keymap;
}

static bool u8str_eq(u8str_t a, u8str_t b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

static bool combo_matches(rubraview_key_combo_t a, rubraview_key_combo_t b) {
    if (a.modifiers != b.modifiers) return false;
    if (a.key_name.len != b.key_name.len) return false;
    for (size_t i = 0; i < a.key_name.len; ++i) {
        if (tolower((unsigned char)a.key_name.ptr[i]) != tolower((unsigned char)b.key_name.ptr[i])) return false;
    }
    return true;
}

static u8str_t find_action_in_context(const rubraview_keymap_t *keymap, u8str_t context, rubraview_key_combo_t combo) {
    for (size_t i = 0; i < keymap->count; ++i) {
        const rubraview_key_binding_t *b = &keymap->bindings[i];
        if (!u8str_eq(b->context, context)) continue;
        for (size_t c = 0; c < b->combo_count; ++c) {
            if (combo_matches(b->combos[c], combo)) return b->action;
        }
    }
    return (u8str_t){ .ptr = "", .len = 0 };
}

u8str_t rubraview_keymap_find_action(const rubraview_keymap_t *keymap, u8str_t context, rubraview_key_combo_t combo) {
    if (!keymap || !keymap->bindings) return (u8str_t){ .ptr = "", .len = 0 };

    u8str_t found = find_action_in_context(keymap, context, combo);
    if (found.len > 0) return found;

    if (context.len > 0) {
        return find_action_in_context(keymap, (u8str_t){ .ptr = "", .len = 0 }, combo);
    }
    return found;
}

const rubraview_key_binding_t *rubraview_keymap_find_binding(const rubraview_keymap_t *keymap, u8str_t context, u8str_t action) {
    if (!keymap || !keymap->bindings) return NULL;
    for (size_t i = 0; i < keymap->count; ++i) {
        if (u8str_eq(keymap->bindings[i].context, context) && u8str_eq(keymap->bindings[i].action, action)) {
            return &keymap->bindings[i];
        }
    }
    return NULL;
}
