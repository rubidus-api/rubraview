#include "rubraview/filemanage.h"
#include "rubraview/ini.h"
#include "rubraview/path.h"
#include <string.h>

/* ---- §3.18.2 renaming ---- */

static bool is_illegal_char(char c) {
    switch (c) {
        case '\\': case '/': case ':': case '*': case '?':
        case '"': case '<': case '>': case '|':
            return true;
        default:
            return (unsigned char)c < 0x20;   /* control characters are illegal too */
    }
}

/* The DOS device names. A file cannot be called any of these, with or
   without an extension, and the failure happens at the moment of the
   rename — after the old name is gone. */
static bool is_reserved_stem(proven_u8str_view_t stem) {
    static const char *const RESERVED[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };

    for (size_t i = 0; i < sizeof(RESERVED) / sizeof(RESERVED[0]); ++i) {
        size_t n = strlen(RESERVED[i]);
        if (stem.size != n) continue;

        bool same = true;
        for (size_t k = 0; k < n; ++k) {
            char a = stem.ptr[k];
            if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
            if (a != RESERVED[i][k]) { same = false; break; }
        }
        if (same) return true;
    }
    return false;
}

rubraview_rename_err_t rubraview_rename_validate(proven_u8str_view_t filename) {
    if (filename.size == 0) return RUBRAVIEW_RENAME_ERR_EMPTY;
    if (filename.size > 255) return RUBRAVIEW_RENAME_ERR_TOO_LONG;

    for (size_t i = 0; i < filename.size; ++i) {
        if (is_illegal_char(filename.ptr[i])) return RUBRAVIEW_RENAME_ERR_ILLEGAL_CHAR;
    }

    /* Windows strips a trailing dot or space rather than refusing it,
       which means the file ends up with a name the reader did not
       choose. Refusing here is the honest answer. */
    char last = filename.ptr[filename.size - 1];
    if (last == '.' || last == ' ') return RUBRAVIEW_RENAME_ERR_TRAILING;

    proven_u8str_view_t stem = rubraview_path_stem(filename);
    if (is_reserved_stem(stem)) return RUBRAVIEW_RENAME_ERR_RESERVED_NAME;
    if (stem.size == 0) return RUBRAVIEW_RENAME_ERR_EMPTY;

    return RUBRAVIEW_RENAME_OK;
}

proven_u8str_view_t rubraview_rename_error_text(rubraview_rename_err_t err) {
    switch (err) {
        case RUBRAVIEW_RENAME_OK: return PROVEN_LIT("");
        case RUBRAVIEW_RENAME_ERR_EMPTY: return PROVEN_LIT("a name is needed");
        case RUBRAVIEW_RENAME_ERR_ILLEGAL_CHAR: return PROVEN_LIT("a filename cannot contain \\ / : * ? \" < > |");
        case RUBRAVIEW_RENAME_ERR_RESERVED_NAME: return PROVEN_LIT("Windows keeps this name for a device");
        case RUBRAVIEW_RENAME_ERR_TRAILING: return PROVEN_LIT("a filename cannot end in a dot or a space");
        case RUBRAVIEW_RENAME_ERR_TOO_LONG: return PROVEN_LIT("that name is too long");
        default: return PROVEN_LIT("that name cannot be used");
    }
}

size_t rubraview_rename_stem_length(proven_u8str_view_t filename) {
    return rubraview_path_stem(filename).size;
}

/* The length of the UTF-8 character that starts with `lead`, or 0 for a
   byte that cannot start one. */
static size_t utf8_char_length(unsigned char lead) {
    if (lead < 0x80u) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 0;
}

size_t rubraview_rename_append(char *buffer, size_t capacity, size_t *length, proven_u8str_view_t text) {
    if (!buffer || !length || capacity == 0 || *length >= capacity) return 0;
    size_t added = 0;
    size_t i = 0;
    while (i < text.size) {
        unsigned char lead = (unsigned char)text.ptr[i];
        size_t n = utf8_char_length(lead);
        if (n == 0 || i + n > text.size) break;                  /* not UTF-8: stop rather than guess */
        bool whole = true;
        for (size_t k = 1; k < n; ++k) whole = whole && (((unsigned char)text.ptr[i + k] & 0xC0u) == 0x80u);
        if (!whole) break;
        bool control = n == 1 && (lead < 0x20u || lead == 0x7Fu);
        if (!control) {
            if (*length + n + 1 > capacity) break;                /* no room: leave it out, not half of it */
            memcpy(buffer + *length, text.ptr + i, n);
            *length += n;
            added += n;
        }
        i += n;
    }
    buffer[*length] = '\0';
    return added;
}

void rubraview_rename_backspace(char *buffer, size_t *length) {
    if (!buffer || !length || *length == 0) return;
    size_t at = *length;
    while (at > 0 && ((unsigned char)buffer[at - 1] & 0xC0u) == 0x80u) at--;
    if (at > 0) at--;
    *length = at;
    buffer[at] = '\0';
}

proven_u8str_view_t rubraview_rename_compose(proven_arena_t *arena, proven_u8str_view_t old_filename, proven_u8str_view_t new_stem) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!arena) return empty;

    proven_u8str_view_t ext = rubraview_path_ext(old_filename);   /* includes the dot, or empty */
    size_t total = new_stem.size + ext.size;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, total + 1);
    if (!proven_is_ok(res.err)) return empty;

    char *out = (char*)(void*)res.value.ptr;
    if (new_stem.size > 0) memcpy(out, new_stem.ptr, new_stem.size);
    if (ext.size > 0) memcpy(out + new_stem.size, ext.ptr, ext.size);
    out[total] = '\0';

    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = total };
}

/* ---- the undo stack ---- */

bool rubraview_file_op_is_undoable(rubraview_file_op_t op) {
    return op != RUBRAVIEW_FILE_OP_PURGE;
}

void rubraview_undo_push(rubraview_undo_stack_t *stack, rubraview_file_action_t action) {
    if (!stack) return;

    if (stack->count >= RUBRAVIEW_UNDO_CAPACITY) {
        /* Drop the oldest. A session that deletes more than this has
           long since stopped caring about its first deletion, and an
           unbounded stack in a viewer that runs for days is a leak. */
        for (size_t i = 1; i < RUBRAVIEW_UNDO_CAPACITY; ++i) {
            stack->actions[i - 1] = stack->actions[i];
        }
        stack->count = RUBRAVIEW_UNDO_CAPACITY - 1;
    }

    stack->actions[stack->count++] = action;
}

rubraview_undo_result_t rubraview_undo_peek(const rubraview_undo_stack_t *stack,
                                            rubraview_file_action_t *out_action) {
    if (!stack || stack->count == 0) return RUBRAVIEW_UNDO_NOTHING;

    const rubraview_file_action_t *action = &stack->actions[stack->count - 1];
    if (out_action) *out_action = *action;

    /* A permanent delete is on the stack precisely so this can be said.
       Skipping past it to undo the action *before* it would restore the
       wrong file and leave the reader believing the purge was reversed. */
    if (!rubraview_file_op_is_undoable(action->op)) return RUBRAVIEW_UNDO_IRREVERSIBLE;

    return RUBRAVIEW_UNDO_AVAILABLE;
}

void rubraview_undo_commit(rubraview_undo_stack_t *stack) {
    if (stack && stack->count > 0) stack->count--;
}

bool rubraview_undo_same_batch(const rubraview_undo_stack_t *stack, uint32_t batch) {
    return stack && batch != 0 && stack->count > 0 && stack->actions[stack->count - 1].batch == batch;
}

/* ---- §3.18.3 curation ---- */

rubraview_curation_t rubraview_curation_parse(proven_arena_t *arena, proven_u8str_view_t settings_ini) {
    rubraview_curation_t curation = {0};
    curation.mode = RUBRAVIEW_CURATION_MOVE;   /* §3.18.3's default */
    if (!arena || settings_ini.size == 0) return curation;

    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, settings_ini);

    for (int i = 1; i <= 9; ++i) {
        char key[8] = { 'd', 'i', 'r', '_', (char)('0' + i), '\0', '\0', '\0' };
        const proven_u8str_view_t *value = rubraview_ini_get(&doc, PROVEN_LIT("curation"), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)key, .size = 5 });
        if (value && value->size > 0) curation.dirs[i - 1] = *value;
    }

    const proven_u8str_view_t *copy_dir = rubraview_ini_get(&doc, PROVEN_LIT("curation"), PROVEN_LIT("copy_dir"));
    if (copy_dir && copy_dir->size > 0) curation.copy_dir = *copy_dir;
    const proven_u8str_view_t *move_dir = rubraview_ini_get(&doc, PROVEN_LIT("curation"), PROVEN_LIT("move_dir"));
    if (move_dir && move_dir->size > 0) curation.move_dir = *move_dir;

    const proven_u8str_view_t *mode = rubraview_ini_get(&doc, PROVEN_LIT("curation"), PROVEN_LIT("curation_mode"));
    if (mode && proven_u8str_view_eq(*mode, PROVEN_LIT("copy"))) {
        curation.mode = RUBRAVIEW_CURATION_COPY;
    }

    return curation;
}

proven_u8str_view_t rubraview_filing_target(proven_arena_t *arena, proven_u8str_view_t dir, proven_u8str_view_t filename,
                                rubraview_path_exists_fn exists, void *ctx) {
    proven_u8str_view_t none = PROVEN_LIT("");
    if (!arena || !exists || dir.size == 0 || filename.size == 0) return none;
    proven_u8str_view_t target = rubraview_path_join(arena, dir, filename);
    if (target.size == 0 || !exists(ctx, target)) return target;

    /* The number goes before the extension; a name with none, or one that
       only starts with a dot, takes it at its end. */
    size_t stem = rubraview_rename_stem_length(filename);
    for (uint32_t n = 1; n <= 9999; ++n) {
        char digits[8];
        size_t d = 0;
        for (uint32_t v = n; v > 0; v /= 10) digits[d++] = (char)('0' + v % 10);
        proven_result_mem_mut_t res = proven_arena_alloc(arena, filename.size + d + 2);
        if (!proven_is_ok(res.err)) return none;
        char *name = (char*)res.value.ptr;
        size_t at = 0;
        memcpy(name, filename.ptr, stem); at = stem;
        name[at++] = '-';
        while (d > 0) name[at++] = digits[--d];
        memcpy(name + at, filename.ptr + stem, filename.size - stem); at += filename.size - stem;
        name[at] = '\0';
        target = rubraview_path_join(arena, dir, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)name, .size = at });
        if (target.size == 0) return none;
        if (!exists(ctx, target)) return target;
    }
    return none;
}

proven_u8str_view_t rubraview_curation_target(const rubraview_curation_t *curation, int32_t digit) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!curation || digit < 1 || digit > 9) return empty;
    return curation->dirs[digit - 1];
}

rubraview_target_step_t rubraview_target_folder_step(proven_u8str_view_t path, bool exists, bool is_directory, proven_u8str_view_t offered) {
    if (path.size == 0) return RUBRAVIEW_TARGET_NOTHING;
    if (exists) return is_directory ? RUBRAVIEW_TARGET_USE : RUBRAVIEW_TARGET_NOT_FOLDER;
    return offered.size > 0 && rubraview_path_same(path, offered) ? RUBRAVIEW_TARGET_MAKE : RUBRAVIEW_TARGET_OFFER;
}

proven_u8str_view_t rubraview_target_folder_trim(proven_u8str_view_t typed) {
    while (typed.size > 0 && (typed.ptr[0] == ' ' || typed.ptr[0] == '"')) { typed.ptr++; typed.size--; }
    while (typed.size > 0 && (typed.ptr[typed.size - 1] == ' ' || typed.ptr[typed.size - 1] == '"')) typed.size--;
    return typed;
}

bool rubraview_curation_advances(const rubraview_curation_t *curation) {
    /* A move takes the file out of the sequence, so staying on it would
       mean staying on something that is no longer there. A copy leaves
       it, so advancing would rush the reader past a picture they were
       still looking at. */
    return curation && curation->mode == RUBRAVIEW_CURATION_MOVE;
}

/* ---- §3.19.2 drops ---- */

rubraview_drop_kind_t rubraview_drop_classify(const rubraview_drop_item_t *items, size_t count) {
    if (!items || count == 0) return RUBRAVIEW_DROP_NOTHING;

    if (count == 1) {
        return items[0].is_directory ? RUBRAVIEW_DROP_OPEN_FOLDER : RUBRAVIEW_DROP_OPEN_FILE;
    }

    /* Several things were dropped. Whatever mixture they are, the only
       reading that does not throw part of the drop away is "show me
       these" — a temporary playlist of exactly what was dropped. */
    return RUBRAVIEW_DROP_PLAYLIST;
}

/* ---- §3.19.1 single instance ---- */

rubraview_instance_action_t rubraview_instance_decide(bool single_instance_enabled,
                                                      bool another_is_running) {
    if (!single_instance_enabled) return RUBRAVIEW_INSTANCE_RUN;
    if (!another_is_running) return RUBRAVIEW_INSTANCE_RUN;

    /* Even with no file to hand over, the running window is brought
       forward — which is what a reader who double-clicked the icon
       again is asking for. */
    return RUBRAVIEW_INSTANCE_HAND_OVER;
}

/* ---- §3.19.3 shell registration ---- */

proven_u8str_view_t rubraview_shell_progid(proven_arena_t *arena, proven_u8str_view_t extension) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!arena) return empty;

    /* A leading dot is accepted and dropped, so a caller can pass either
       ".jpg" or "jpg" and get the same ProgID. */
    if (extension.size > 0 && extension.ptr[0] == '.') {
        extension.ptr++;
        extension.size--;
    }
    if (extension.size == 0) return empty;

    static const char PREFIX[] = "Rubraview.";
    size_t prefix_len = sizeof(PREFIX) - 1;
    size_t total = prefix_len + extension.size;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, total + 1);
    if (!proven_is_ok(res.err)) return empty;

    char *out = (char*)(void*)res.value.ptr;
    memcpy(out, PREFIX, prefix_len);
    for (size_t i = 0; i < extension.size; ++i) {
        char c = extension.ptr[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[prefix_len + i] = c;
    }
    out[total] = '\0';

    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = total };
}

proven_u8str_view_t rubraview_shell_extensions(void) {
    return PROVEN_LIT("jpg;jpeg;png;apng;gif;bmp;tif;tiff;webp;avif;jxl;ico;cbz;cb7;cbr;mp4;mkv;webm;avi;mov;mp3;flac;wav;ogg;opus;m4a;txt;log;nfo;md");
}

proven_u8str_view_t rubraview_shell_extensions_all(void) {
    return PROVEN_LIT("jpg;jpeg;png;apng;gif;bmp;tif;tiff;webp;avif;jxl;ico;cbz;cb7;cbr;mp4;mkv;webm;avi;mov;mp3;flac;wav;ogg;opus;m4a;txt;log;nfo;md;zip;7z;rar;alz;egg");
}

static const struct { uint32_t group; const char *word; const char *extensions; } SHELL_GROUPS[] = {
    { RUBRAVIEW_SHELL_PICTURES, "pictures", "jpg;jpeg;png;apng;gif;bmp;tif;tiff;webp;avif;jxl;ico" },
    { RUBRAVIEW_SHELL_COMICS,   "comics",   "cbz;cb7;cbr" },
    { RUBRAVIEW_SHELL_VIDEO,    "video",    "mp4;mkv;webm;avi;mov" },
    { RUBRAVIEW_SHELL_MUSIC,    "music",    "mp3;flac;wav;ogg;opus;m4a" },
    { RUBRAVIEW_SHELL_TEXT,     "text",     "txt;log;nfo;md" },
    /* D-86: offered the right-click menu only, never made a file type of ours. */
    { RUBRAVIEW_SHELL_ARCHIVES, "archives", "zip;7z;rar;alz;egg" },
};

proven_u8str_view_t rubraview_shell_extensions_for(proven_arena_t *arena, uint32_t groups) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!arena) return empty;
    size_t total = 0;
    for (size_t i = 0; i < sizeof(SHELL_GROUPS) / sizeof(SHELL_GROUPS[0]); ++i) {
        if (groups & SHELL_GROUPS[i].group) total += strlen(SHELL_GROUPS[i].extensions) + 1;
    }
    if (total == 0) return empty;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, total);
    if (!proven_is_ok(res.err)) return empty;
    char *out = (char*)(void*)res.value.ptr;
    size_t at = 0;
    for (size_t i = 0; i < sizeof(SHELL_GROUPS) / sizeof(SHELL_GROUPS[0]); ++i) {
        if (!(groups & SHELL_GROUPS[i].group)) continue;
        if (at > 0) out[at++] = ';';
        size_t n = strlen(SHELL_GROUPS[i].extensions);
        memcpy(out + at, SHELL_GROUPS[i].extensions, n);
        at += n;
    }
    out[at] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = at };
}

uint32_t rubraview_shell_groups_parse(proven_u8str_view_t words) {
    uint32_t mask = 0;
    size_t start = 0;
    for (size_t i = 0; i <= words.size; ++i) {
        if (i != words.size && words.ptr[i] != ',') continue;
        proven_u8str_view_t word = { .ptr = words.ptr + start, .size = i - start };
        start = i + 1;
        if (word.size == 0) continue;
        uint32_t found = 0;
        for (size_t g = 0; g < sizeof(SHELL_GROUPS) / sizeof(SHELL_GROUPS[0]); ++g) {
            if (word.size == strlen(SHELL_GROUPS[g].word) && memcmp(word.ptr, SHELL_GROUPS[g].word, word.size) == 0) {
                found = SHELL_GROUPS[g].group;
            }
        }
        if (!found) return 0;
        mask |= found;
    }
    return mask;
}

/* Walks a list's words; false after the last. */
static bool next_word(proven_u8str_view_t list, char separator, size_t *at, proven_u8str_view_t *out) {
    while (*at <= list.size) {
        size_t start = *at, end = start;
        while (end < list.size && list.ptr[end] != separator) end++;
        *at = end + 1;
        if (end > start) { *out = (proven_u8str_view_t){ .ptr = list.ptr + start, .size = end - start }; return true; }
    }
    return false;
}

static bool word_is(proven_u8str_view_t word, const char *text, size_t n) {
    if (word.size != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = word.ptr[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != text[i]) return false;
    }
    return true;
}

static bool list_has(const char *list, proven_u8str_view_t word) {
    proven_u8str_view_t all = { .ptr = (const proven_byte_t *)list, .size = strlen(list) }, one;
    for (size_t at = 0; next_word(all, ';', &at, &one);) {
        if (word_is(word, (const char *)one.ptr, one.size)) return true;
    }
    return false;
}

uint32_t rubraview_shell_kind_of(proven_u8str_view_t extension) {
    if (extension.size > 0 && extension.ptr[0] == '.') { extension.ptr++; extension.size--; }
    if (extension.size == 0) return 0;
    for (size_t g = 0; g < sizeof(SHELL_GROUPS) / sizeof(SHELL_GROUPS[0]); ++g) {
        if (list_has(SHELL_GROUPS[g].extensions, extension)) return SHELL_GROUPS[g].group;
    }
    return 0;
}

/* Is `extension` chosen by `words` — by its own name, its kind's word, or "all"? */
static bool words_choose(proven_u8str_view_t words, proven_u8str_view_t extension, const char *kind_word) {
    proven_u8str_view_t word;
    for (size_t at = 0; next_word(words, ',', &at, &word);) {
        if (word.size > 0 && word.ptr[0] == '.') { word.ptr++; word.size--; }
        if (word_is(word, (const char *)extension.ptr, extension.size) || word_is(word, kind_word, strlen(kind_word)) ||
            word_is(word, "all", 3)) {
            return true;
        }
    }
    return false;
}

proven_u8str_view_t rubraview_shell_selection_parse(proven_arena_t *arena, proven_u8str_view_t words) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!arena) return empty;

    /* Every word is something we know, or the whole list is refused: a
       typo must not register less than was asked without a word. */
    proven_u8str_view_t word;
    size_t seen = 0;
    for (size_t at = 0; next_word(words, ',', &at, &word);) {
        if (word.size > 0 && word.ptr[0] == '.') { word.ptr++; word.size--; }
        bool known = word_is(word, "all", 3) || rubraview_shell_kind_of(word) != 0;
        for (size_t g = 0; !known && g < sizeof(SHELL_GROUPS) / sizeof(SHELL_GROUPS[0]); ++g) {
            known = word_is(word, SHELL_GROUPS[g].word, strlen(SHELL_GROUPS[g].word));
        }
        if (!known) return empty;
        seen++;
    }
    if (seen == 0) return empty;

    size_t cap = 0;
    for (size_t g = 0; g < sizeof(SHELL_GROUPS) / sizeof(SHELL_GROUPS[0]); ++g) cap += strlen(SHELL_GROUPS[g].extensions) + 1;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, cap + 1);
    if (!proven_is_ok(res.err)) return empty;
    char *out = (char*)(void*)res.value.ptr;
    size_t len = 0;
    for (size_t g = 0; g < sizeof(SHELL_GROUPS) / sizeof(SHELL_GROUPS[0]); ++g) {
        proven_u8str_view_t all = { .ptr = (const proven_byte_t *)SHELL_GROUPS[g].extensions, .size = strlen(SHELL_GROUPS[g].extensions) }, one;
        for (size_t at = 0; next_word(all, ';', &at, &one);) {
            if (!words_choose(words, one, SHELL_GROUPS[g].word)) continue;
            if (len > 0) out[len++] = ';';
            memcpy(out + len, one.ptr, one.size);
            len += one.size;
        }
    }
    out[len] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = len };
}

proven_u8str_view_t rubraview_shell_selection_of_kinds(proven_arena_t *arena, proven_u8str_view_t selection, uint32_t kinds) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!arena || selection.size == 0) return empty;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, selection.size + 1);
    if (!proven_is_ok(res.err)) return empty;
    char *out = (char*)(void*)res.value.ptr;
    size_t len = 0;
    proven_u8str_view_t one;
    for (size_t at = 0; next_word(selection, ';', &at, &one);) {
        if (!(rubraview_shell_kind_of(one) & kinds)) continue;
        if (len > 0) out[len++] = ';';
        memcpy(out + len, one.ptr, one.size);
        len += one.size;
    }
    out[len] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = len };
}
