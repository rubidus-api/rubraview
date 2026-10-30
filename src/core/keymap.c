#include "rubraview/keymap.h"
#include "rubraview/ini.h"
#include "rubraview/lang.h"
#include <stdio.h>
#include <stdlib.h>
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
        if (proven_u8str_view_eq_ascii_ci(word, PROVEN_LIT("shift"))) mod = RUBRAVIEW_MOD_SHIFT;
        else if (proven_u8str_view_eq_ascii_ci(word, PROVEN_LIT("ctrl")) || proven_u8str_view_eq_ascii_ci(word, PROVEN_LIT("control"))) mod = RUBRAVIEW_MOD_CTRL;
        else if (proven_u8str_view_eq_ascii_ci(word, PROVEN_LIT("alt"))) mod = RUBRAVIEW_MOD_ALT;
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
            proven_u8str_view_t token = proven_u8str_view_trim(proven_u8str_view_from_bytes((const char *)value.ptr + start, i - start));
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
        /* RV-184: [common] is the name now; [ui] still reads the same */
        if (proven_u8str_view_eq(context, PROVEN_LIT("ui")) || proven_u8str_view_eq(context, PROVEN_LIT("common"))) context = PROVEN_LIT("");
        /* D-16: [animation] became [media], which RV-184 split; what is not placed by its action is playback's. */
        else if (proven_u8str_view_eq(context, PROVEN_LIT("animation")) || proven_u8str_view_eq(context, PROVEN_LIT("media"))) context = PROVEN_LIT("playback");
        else if (proven_u8str_view_eq(context, PROVEN_LIT("view"))) context = PROVEN_LIT("image");
        else if (proven_u8str_view_eq(context, PROVEN_LIT("subpage"))) context = PROVEN_LIT("multipage");
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
    return proven_u8str_view_eq_ascii_ci(a.key_name, b.key_name);
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

/* RV-193: the keys as they lie on a keyboard, row by row, left to right; then the editing and arrow keys
   and the number pad. Letters, digits and F-keys are worked out, not listed. */
static const char *const KEYBOARD_REST[] = {
    "Backquote", "Minus", "Plus", "Backspace", "Tab", "BracketLeft", "BracketRight", "Backslash",
    "Semicolon", "Quote", "Enter", "Comma", "Period", "Slash", "Space",
    "Insert", "Home", "PageUp", "Delete", "End", "PageDown", "Up", "Left", "Down", "Right",
};
static const char QWERTY[] = "QWERTYUIOPASDFGHJKLZXCVBNM";

/* A key's place: smaller first; 10000 and up for a name not known (then by name). */
static int key_rank(proven_u8str_view_t name, rubraview_key_order_t order) {
    const char *s = (const char *)name.ptr;
    size_t n = name.size;
    bool alpha = order == RUBRAVIEW_KEY_ORDER_ALPHABET;
    if (n == 1 && isalpha((unsigned char)s[0])) {
        int up = toupper((unsigned char)s[0]);
        int at = (int)(strchr(QWERTY, up) - QWERTY);   /* rows of 10, 9 and 7: Q-P 400, A-L 420, Z-M 440 */
        return alpha ? 100 + (up - 'A') : at < 10 ? 400 + at : at < 19 ? 420 + (at - 10) : 440 + (at - 19);
    }
    if (n == 1 && isdigit((unsigned char)s[0])) {
        int d = s[0] - '0';
        return alpha ? 200 + d : 300 + (d == 0 ? 10 : d);   /* the number row runs 1 ... 9, 0 */
    }
    if (n >= 2 && n <= 3 && (s[0] == 'F' || s[0] == 'f') && isdigit((unsigned char)s[1]) && (n == 2 || isdigit((unsigned char)s[2]))) {
        int f = atoi(s + 1);
        if (f >= 1 && f <= 24) return (alpha ? 250 : 10) + f;
    }
    if (proven_u8str_view_eq_ascii_ci(name, PROVEN_LIT("Escape"))) return alpha ? 500 : 0;
    if (n == 7 && strncmp(s, "Numpad", 6) == 0 && isdigit((unsigned char)s[6])) return 700 + (s[6] - '0');
    for (size_t i = 0; i < sizeof(KEYBOARD_REST) / sizeof(KEYBOARD_REST[0]); ++i) {
        if (proven_u8str_view_eq_ascii_ci(name, proven_u8str_view_from_cstr(KEYBOARD_REST[i]))) {
            /* the keyboard's own place: the number row's two after 0, Tab's row's after P, and so on */
            static const int AT[] = { 299, 311, 312, 313, 399, 410, 411, 412, 429, 430, 431, 447, 448, 449, 450,
                                      600, 601, 602, 603, 604, 605, 606, 607, 608, 609 };
            return alpha ? 510 + (int)i : AT[i];
        }
    }
    return 10000;
}

int rubraview_key_combo_order(rubraview_key_combo_t a, rubraview_key_combo_t b, rubraview_key_order_t order) {
    int ra = key_rank(a.key_name, order), rb = key_rank(b.key_name, order);
    if (ra != rb) return ra < rb ? -1 : 1;
    if (ra == 10000) {   /* two names not known: by name, without case */
        size_t n = a.key_name.size < b.key_name.size ? a.key_name.size : b.key_name.size;
        for (size_t i = 0; i < n; ++i) {
            int ca = tolower(a.key_name.ptr[i]), cb = tolower(b.key_name.ptr[i]);
            if (ca != cb) return ca < cb ? -1 : 1;
        }
        if (a.key_name.size != b.key_name.size) return a.key_name.size < b.key_name.size ? -1 : 1;
    }
    /* Shift 1, Ctrl 2, Alt 4: the mask is the order the modifiers are wanted in */
    uint32_t ma = a.modifiers & 7u, mb = b.modifiers & 7u;
    return ma == mb ? 0 : ma < mb ? -1 : 1;
}

/* RV-184 (D-219): the contexts, in the order they are shown. */
static const rubraview_key_context_info_t CONTEXTS[] = {
    { "",           N_("Everywhere"),                N_("always") },
    { "file",       N_("Files"),                     N_("always") },
    { "navigation", N_("Moving about"),              N_("always") },
    { "image",      N_("Pictures"),                  N_("a picture, a comic's page, an animation, a film") },
    { "multipage",  N_("Pages inside one file"),     N_("a TIFF or an ICO of several pages") },
    { "slideshow",  N_("Slide show"),                N_("while a slide show runs; S starts one") },
    { "playback",   N_("Playing"),                   N_("a film, music, an animation") },
    { "video",      N_("Films"),                     N_("a film") },
    { "audio",      N_("Music"),                     N_("music") },
    { "text",       N_("Text"),                      N_("a text page") },
};
#define CONTEXT_COUNT (sizeof(CONTEXTS) / sizeof(CONTEXTS[0]))

const rubraview_key_context_info_t *rubraview_keymap_contexts(size_t *count) {
    if (count) *count = CONTEXT_COUNT;
    return CONTEXTS;
}

const rubraview_key_context_info_t *rubraview_keymap_context_info(proven_u8str_view_t context) {
    for (size_t i = 0; i < CONTEXT_COUNT; ++i) {
        if (proven_u8str_view_eq(context, proven_u8str_view_from_cstr(CONTEXTS[i].name))) return &CONTEXTS[i];
    }
    return NULL;
}

/* Each mode's own contexts, first to last; navigation, file and common follow every one. */
static const char *const MODE_OWN[RUBRAVIEW_KEY_MODE_COUNT][3] = {
    [RUBRAVIEW_KEY_MODE_PICTURE]   = { "image", NULL, NULL },
    [RUBRAVIEW_KEY_MODE_MULTIPAGE] = { "multipage", "image", NULL },
    [RUBRAVIEW_KEY_MODE_ANIMATION] = { "playback", "image", NULL },
    [RUBRAVIEW_KEY_MODE_FILM]      = { "video", "playback", "image" },   /* a film is fitted, zoomed and turned too */
    [RUBRAVIEW_KEY_MODE_MUSIC]     = { "audio", "playback", NULL },
    [RUBRAVIEW_KEY_MODE_TEXT]      = { "text", NULL, NULL },
};

size_t rubraview_keymap_mode_chain(rubraview_key_mode_t mode, bool slideshow_running, proven_u8str_view_t *out, size_t cap) {
    size_t n = 0;
    if (!out || (unsigned)mode >= RUBRAVIEW_KEY_MODE_COUNT) return 0;
#define CHAIN_PUSH(text) do { if (n < cap) out[n++] = proven_u8str_view_from_cstr(text); } while (0)
    if (slideshow_running) CHAIN_PUSH("slideshow");
    for (size_t i = 0; i < 3 && MODE_OWN[mode][i]; ++i) CHAIN_PUSH(MODE_OWN[mode][i]);
    CHAIN_PUSH("navigation");
    CHAIN_PUSH("file");
    CHAIN_PUSH("");
    if (!slideshow_running) CHAIN_PUSH("slideshow");
#undef CHAIN_PUSH
    return n;
}

proven_u8str_view_t rubraview_keymap_resolve(const rubraview_keymap_t *keymap, rubraview_key_mode_t mode, bool slideshow_running,
                                             rubraview_key_combo_t combo) {
    proven_u8str_view_t chain[8];
    size_t n = rubraview_keymap_mode_chain(mode, slideshow_running, chain, 8);
    if (!keymap || !keymap->bindings) return PROVEN_LIT("");
    for (size_t i = 0; i < n; ++i) {
        proven_u8str_view_t found = find_action_in_context(keymap, chain[i], combo);
        if (found.size > 0) return found;
    }
    return PROVEN_LIT("");
}

void rubraview_keymap_place_by_action(rubraview_keymap_t *keymap, const rubraview_keymap_t *defaults) {
    if (!keymap || !defaults) return;
    for (size_t i = 0; i < keymap->count; ++i) {
        for (size_t d = 0; d < defaults->count; ++d) {
            if (!proven_u8str_view_eq(keymap->bindings[i].action, defaults->bindings[d].action)) continue;
            keymap->bindings[i].context = defaults->bindings[d].context;
            break;
        }
    }
}

/* The layer under the kinds of file (D-219): common, file and navigation, asked in every mode, and image, asked
   with them wherever a picture is shown. A key in two of these reaches only one of them; the slide show's,
   a multi-page file's, playing's, a film's, music's and a text's own keys are asked first on purpose. */
static bool in_base_layer(proven_u8str_view_t context) {
    return context.size == 0 ||
           (proven_u8str_view_eq(context, PROVEN_LIT("navigation"))) ||
           (proven_u8str_view_eq(context, PROVEN_LIT("file"))) ||
           (proven_u8str_view_eq(context, PROVEN_LIT("image")));
}

bool rubraview_keymap_contexts_meet(proven_u8str_view_t a, proven_u8str_view_t b) {
    if (proven_u8str_view_eq(a, b)) return true;
    return in_base_layer(a) && in_base_layer(b);
}

size_t rubraview_keymap_add_missing(proven_arena_t *arena, rubraview_keymap_t *keymap, const rubraview_keymap_t *defaults) {
    if (!arena || !keymap || !defaults || defaults->count == 0) return 0;
    size_t missing = 0;
    for (size_t d = 0; d < defaults->count; ++d) {
        bool have = false;
        for (size_t i = 0; i < keymap->count && !have; ++i) have = proven_u8str_view_eq(keymap->bindings[i].action, defaults->bindings[d].action);
        if (!have) missing++;
    }
    if (missing == 0) return 0;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, keymap->count + missing, sizeof(rubraview_key_binding_t));
    if (!proven_is_ok(res.err)) return 0;
    rubraview_key_binding_t *all = (rubraview_key_binding_t*)(void*)res.value.ptr;
    if (keymap->count > 0) memcpy(all, keymap->bindings, keymap->count * sizeof(rubraview_key_binding_t));
    size_t count = keymap->count, added = 0;
    for (size_t d = 0; d < defaults->count; ++d) {
        const rubraview_key_binding_t *want = &defaults->bindings[d];
        bool have = false;
        for (size_t i = 0; i < keymap->count && !have; ++i) have = proven_u8str_view_eq(keymap->bindings[i].action, want->action);
        if (have) continue;
        rubraview_key_binding_t b = { .context = want->context, .action = want->action, .combos = NULL, .combo_count = 0 };
        if (want->combo_count > 0) {
            proven_result_mem_mut_t c = rubraview_arena_alloc_array(arena, want->combo_count, sizeof(rubraview_key_combo_t));
            if (!proven_is_ok(c.err)) break;
            b.combos = (rubraview_key_combo_t*)(void*)c.value.ptr;
            for (size_t k = 0; k < want->combo_count; ++k) {
                bool taken = false;   /* the user's own key stays theirs */
                for (size_t i = 0; i < count && !taken; ++i) {
                    if (!rubraview_keymap_contexts_meet(all[i].context, want->context)) continue;
                    for (size_t j = 0; j < all[i].combo_count && !taken; ++j) taken = combo_matches(all[i].combos[j], want->combos[k]);
                }
                if (!taken) b.combos[b.combo_count++] = want->combos[k];
            }
        }
        all[count++] = b;
        added++;
    }
    keymap->bindings = all;
    keymap->count = count;
    return added;
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
        proven_u8str_view_t section = b->context.size == 0 ? PROVEN_LIT("common") : b->context;
        rubraview_ini_set_string(arena, &doc, section, b->action,
                                 (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)res.value.ptr, .size = used });
    }
    return rubraview_ini_serialize(arena, &doc);
}
