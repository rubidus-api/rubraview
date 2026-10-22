#include "rubraview/keymap.h"
#include "rubraview/ini.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>


static rubraview_key_combo_t parse_combo(proven_u8str_view_t token) {
    rubraview_key_combo_t combo = { .modifiers = 0, .key_name = token };

    while (true) {
        size_t plus = (size_t)-1;
        for (size_t i = 0; i < combo.key_name.size; ++i) {
            if (combo.key_name.ptr[i] == '+') { plus = i; break; }
        }
        if (plus == (size_t)-1) break;

        proven_u8str_view_t word = { .ptr = combo.key_name.ptr, .size = plus };
        proven_u8str_view_t rest = { .ptr = combo.key_name.ptr + plus + 1, .size = combo.key_name.size - plus - 1 };

        uint32_t mod = 0;
        if (rubraview_u8_eq_lit_ci(word, "shift")) mod = RUBRAVIEW_MOD_SHIFT;
        else if (rubraview_u8_eq_lit_ci(word, "ctrl") || rubraview_u8_eq_lit_ci(word, "control")) mod = RUBRAVIEW_MOD_CTRL;
        else if (rubraview_u8_eq_lit_ci(word, "alt")) mod = RUBRAVIEW_MOD_ALT;
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
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(rubraview_key_combo_t));
        if (!proven_is_ok(res.err)) return false;
        rubraview_key_combo_t *new_data = (rubraview_key_combo_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(new_data, b->data, b->count * sizeof(rubraview_key_combo_t));
        b->data = new_data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = combo;
    return true;
}

static void parse_combo_list(proven_arena_t *arena, proven_u8str_view_t value, rubraview_key_binding_t *binding) {
    combo_buf_t buf = {0};
    size_t start = 0;
    for (size_t i = 0; i <= value.size; ++i) {
        if (i == value.size || value.ptr[i] == ',') {
            proven_u8str_view_t token = proven_u8str_view_trim(rubraview_u8((const char *)value.ptr + start, i - start));
            if (token.size > 0) {
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
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(rubraview_key_binding_t));
        if (!proven_is_ok(res.err)) return false;
        rubraview_key_binding_t *new_data = (rubraview_key_binding_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(new_data, b->data, b->count * sizeof(rubraview_key_binding_t));
        b->data = new_data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = binding;
    return true;
}

rubraview_keymap_t rubraview_keymap_parse(proven_arena_t *arena, proven_u8str_view_t ini_text) {
    rubraview_keymap_t keymap = {0};
    if (!arena) return keymap;

    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, ini_text);

    binding_buf_t buf = {0};
    for (size_t i = 0; i < doc.count; ++i) {
        /* D-13: `[ui]` is the context everything falls back to. Before it
           had a name those bindings sat above the first section, and an
           older keymap.ini that still does that reads the same way. */
        proven_u8str_view_t context = doc.entries[i].section;
        if (proven_u8str_view_eq(context, PROVEN_LIT("ui"))) context = PROVEN_LIT("");
        /* D-16: [animation] became [media]; a keymap.ini saved before still loads. */
        if (proven_u8str_view_eq(context, PROVEN_LIT("animation"))) context = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"media", .size = 5 };
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

static bool combo_matches(rubraview_key_combo_t a, rubraview_key_combo_t b) {
    if (a.modifiers != b.modifiers) return false;
    if (a.key_name.size != b.key_name.size) return false;
    for (size_t i = 0; i < a.key_name.size; ++i) {
        if (tolower((unsigned char)a.key_name.ptr[i]) != tolower((unsigned char)b.key_name.ptr[i])) return false;
    }
    return true;
}

static proven_u8str_view_t find_action_in_context(const rubraview_keymap_t *keymap, proven_u8str_view_t context, rubraview_key_combo_t combo) {
    for (size_t i = 0; i < keymap->count; ++i) {
        const rubraview_key_binding_t *b = &keymap->bindings[i];
        if (!proven_u8str_view_eq(b->context, context)) continue;
        for (size_t c = 0; c < b->combo_count; ++c) {
            if (combo_matches(b->combos[c], combo)) return b->action;
        }
    }
    return PROVEN_LIT("");
}

proven_u8str_view_t rubraview_keymap_find_action(const rubraview_keymap_t *keymap, proven_u8str_view_t context, rubraview_key_combo_t combo) {
    if (!keymap || !keymap->bindings) return PROVEN_LIT("");

    proven_u8str_view_t found = find_action_in_context(keymap, context, combo);
    if (found.size > 0) return found;

    if (context.size > 0) {
        return find_action_in_context(keymap, PROVEN_LIT(""), combo);
    }
    return found;
}

const rubraview_key_binding_t *rubraview_keymap_find_binding(const rubraview_keymap_t *keymap, proven_u8str_view_t context, proven_u8str_view_t action) {
    if (!keymap || !keymap->bindings) return NULL;
    for (size_t i = 0; i < keymap->count; ++i) {
        if (proven_u8str_view_eq(keymap->bindings[i].context, context) && proven_u8str_view_eq(keymap->bindings[i].action, action)) {
            return &keymap->bindings[i];
        }
    }
    return NULL;
}

/* ---- §3.22.2 tab 8: changing bindings in place (D-14) ---- */

proven_u8str_view_t rubraview_key_combo_format(char *buffer, size_t capacity, rubraview_key_combo_t combo) {
    if (!buffer || capacity == 0) return PROVEN_LIT("");
    int n = snprintf(buffer, capacity, "%s%s%s%.*s",
                     (combo.modifiers & RUBRAVIEW_MOD_CTRL) ? "Ctrl+" : "",
                     (combo.modifiers & RUBRAVIEW_MOD_SHIFT) ? "Shift+" : "",
                     (combo.modifiers & RUBRAVIEW_MOD_ALT) ? "Alt+" : "",
                     (int)combo.key_name.size, combo.key_name.ptr);
    if (n <= 0) { buffer[0] = '\0'; return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = 0 }; }
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = (size_t)n < capacity ? (size_t)n : capacity - 1 };
}

bool rubraview_key_combo_equal(rubraview_key_combo_t a, rubraview_key_combo_t b) {
    return combo_matches(a, b);
}

static bool in_base_layer(proven_u8str_view_t context) {
    return context.size == 0 ||
           (proven_u8str_view_eq(context, PROVEN_LIT("navigation"))) ||
           (proven_u8str_view_eq(context, PROVEN_LIT("view")));
}

bool rubraview_keymap_contexts_meet(proven_u8str_view_t a, proven_u8str_view_t b) {
    if (proven_u8str_view_eq(a, b)) return true;
    return in_base_layer(a) && in_base_layer(b);
}

rubraview_keymap_t rubraview_keymap_copy(proven_arena_t *arena, const rubraview_keymap_t *source) {
    rubraview_keymap_t copy = {0};
    if (!arena || !source || source->count == 0) return copy;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, source->count, sizeof(rubraview_key_binding_t));
    if (!proven_is_ok(res.err)) return copy;
    copy.bindings = (rubraview_key_binding_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < source->count; ++i) {
        rubraview_key_binding_t b = source->bindings[i];
        if (b.combo_count > 0) {
            proven_result_mem_mut_t c = rubraview_arena_alloc_array(arena, b.combo_count, sizeof(rubraview_key_combo_t));
            if (!proven_is_ok(c.err)) return (rubraview_keymap_t){0};
            memcpy(c.value.ptr, b.combos, b.combo_count * sizeof(rubraview_key_combo_t));
            b.combos = (rubraview_key_combo_t*)(void*)c.value.ptr;
        } else {
            b.combos = NULL;
        }
        copy.bindings[i] = b;
    }
    copy.count = source->count;
    return copy;
}

bool rubraview_keymap_equal(const rubraview_keymap_t *a, const rubraview_keymap_t *b) {
    if (!a || !b || a->count != b->count) return false;
    for (size_t i = 0; i < a->count; ++i) {
        const rubraview_key_binding_t *x = &a->bindings[i], *y = &b->bindings[i];
        if (!proven_u8str_view_eq(x->context, y->context) || !proven_u8str_view_eq(x->action, y->action) ||
            x->combo_count != y->combo_count) return false;
        for (size_t c = 0; c < x->combo_count; ++c) {
            if (!combo_matches(x->combos[c], y->combos[c])) return false;
        }
    }
    return true;
}

rubraview_bind_result_t rubraview_keymap_bind(proven_arena_t *arena, rubraview_keymap_t *keymap, size_t index,
                                              rubraview_key_combo_t combo,
                                              const rubraview_key_binding_t **out_holder) {
    if (out_holder) *out_holder = NULL;
    if (!arena || !keymap || index >= keymap->count || combo.key_name.size == 0) return RUBRAVIEW_BIND_FAILED;
    rubraview_key_binding_t *target = &keymap->bindings[index];
    for (size_t c = 0; c < target->combo_count; ++c) {
        if (combo_matches(target->combos[c], combo)) return RUBRAVIEW_BIND_ALREADY;
    }
    for (size_t i = 0; i < keymap->count; ++i) {
        const rubraview_key_binding_t *other = &keymap->bindings[i];
        if (i == index || !rubraview_keymap_contexts_meet(other->context, target->context)) continue;
        for (size_t c = 0; c < other->combo_count; ++c) {
            if (!combo_matches(other->combos[c], combo)) continue;
            if (out_holder) *out_holder = other;
            return RUBRAVIEW_BIND_TAKEN;
        }
    }
    /* A new array each time: the old one may be shared with a copy (Revert). */
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, (target->combo_count + 1), sizeof(rubraview_key_combo_t));
    if (!proven_is_ok(res.err)) return RUBRAVIEW_BIND_FAILED;
    rubraview_key_combo_t *combos = (rubraview_key_combo_t*)(void*)res.value.ptr;
    if (target->combo_count > 0) memcpy(combos, target->combos, target->combo_count * sizeof(rubraview_key_combo_t));
    /* The event's key name points at a PAL literal; keep a copy of our own. */
    proven_result_mem_mut_t name = proven_arena_alloc(arena, combo.key_name.size + 1);
    if (!proven_is_ok(name.err)) return RUBRAVIEW_BIND_FAILED;
    memcpy(name.value.ptr, combo.key_name.ptr, combo.key_name.size);
    name.value.ptr[combo.key_name.size] = '\0';
    combos[target->combo_count] = (rubraview_key_combo_t){
        .modifiers = combo.modifiers,
        .key_name = { .ptr = (const proven_byte_t *)(const char*)name.value.ptr, .size = combo.key_name.size },
    };
    target->combos = combos;
    target->combo_count++;
    return RUBRAVIEW_BIND_ADDED;
}

bool rubraview_keymap_unbind_last(rubraview_keymap_t *keymap, size_t index) {
    if (!keymap || index >= keymap->count || keymap->bindings[index].combo_count == 0) return false;
    keymap->bindings[index].combo_count--;   /* the array itself is left as it is: a copy may share it */
    return true;
}

proven_u8str_view_t rubraview_keymap_serialize(proven_arena_t *arena, const rubraview_keymap_t *keymap) {
    if (!arena || !keymap) return PROVEN_LIT("");
    rubraview_ini_doc_t doc = {0};
    for (size_t i = 0; i < keymap->count; ++i) {
        const rubraview_key_binding_t *b = &keymap->bindings[i];
        char list[512];
        size_t used = 0;
        for (size_t c = 0; c < b->combo_count && used < sizeof(list); ++c) {
            if (c > 0 && used + 2 < sizeof(list)) { memcpy(list + used, ", ", 2); used += 2; }
            proven_u8str_view_t one = rubraview_key_combo_format(list + used, sizeof(list) - used, b->combos[c]);
            used += one.size;
        }
        proven_result_mem_mut_t res = proven_arena_alloc(arena, used + 1);
        if (!proven_is_ok(res.err)) return PROVEN_LIT("");
        memcpy(res.value.ptr, list, used);
        res.value.ptr[used] = '\0';
        proven_u8str_view_t section = b->context.size == 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"ui", .size = 2 } : b->context;
        rubraview_ini_set_string(arena, &doc, section, b->action,
                                 (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)res.value.ptr, .size = used });
    }
    return rubraview_ini_serialize(arena, &doc);
}
