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
static bool is_reserved_stem(u8str_t stem) {
    static const char *const RESERVED[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };

    for (size_t i = 0; i < sizeof(RESERVED) / sizeof(RESERVED[0]); ++i) {
        size_t n = strlen(RESERVED[i]);
        if (stem.len != n) continue;

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

rubraview_rename_err_t rubraview_rename_validate(u8str_t filename) {
    if (filename.len == 0) return RUBRAVIEW_RENAME_ERR_EMPTY;
    if (filename.len > 255) return RUBRAVIEW_RENAME_ERR_TOO_LONG;

    for (size_t i = 0; i < filename.len; ++i) {
        if (is_illegal_char(filename.ptr[i])) return RUBRAVIEW_RENAME_ERR_ILLEGAL_CHAR;
    }

    /* Windows strips a trailing dot or space rather than refusing it,
       which means the file ends up with a name the reader did not
       choose. Refusing here is the honest answer. */
    char last = filename.ptr[filename.len - 1];
    if (last == '.' || last == ' ') return RUBRAVIEW_RENAME_ERR_TRAILING;

    u8str_t stem = rubraview_path_stem(filename);
    if (is_reserved_stem(stem)) return RUBRAVIEW_RENAME_ERR_RESERVED_NAME;
    if (stem.len == 0) return RUBRAVIEW_RENAME_ERR_EMPTY;

    return RUBRAVIEW_RENAME_OK;
}

u8str_t rubraview_rename_error_text(rubraview_rename_err_t err) {
    switch (err) {
        case RUBRAVIEW_RENAME_OK: return U8("");
        case RUBRAVIEW_RENAME_ERR_EMPTY: return U8("a name is needed");
        case RUBRAVIEW_RENAME_ERR_ILLEGAL_CHAR: return U8("a filename cannot contain \\ / : * ? \" < > |");
        case RUBRAVIEW_RENAME_ERR_RESERVED_NAME: return U8("Windows keeps this name for a device");
        case RUBRAVIEW_RENAME_ERR_TRAILING: return U8("a filename cannot end in a dot or a space");
        case RUBRAVIEW_RENAME_ERR_TOO_LONG: return U8("that name is too long");
        default: return U8("that name cannot be used");
    }
}

size_t rubraview_rename_stem_length(u8str_t filename) {
    return rubraview_path_stem(filename).len;
}

u8str_t rubraview_rename_compose(proven_arena_t *arena, u8str_t old_filename, u8str_t new_stem) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!arena) return empty;

    u8str_t ext = rubraview_path_ext(old_filename);   /* includes the dot, or empty */
    size_t total = new_stem.len + ext.len;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, total + 1);
    if (!proven_is_ok(res.err)) return empty;

    char *out = (char*)(void*)res.value.ptr;
    if (new_stem.len > 0) memcpy(out, new_stem.ptr, new_stem.len);
    if (ext.len > 0) memcpy(out + new_stem.len, ext.ptr, ext.len);
    out[total] = '\0';

    return (u8str_t){ .ptr = out, .len = total };
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

/* ---- §3.18.3 curation ---- */

rubraview_curation_t rubraview_curation_parse(proven_arena_t *arena, u8str_t settings_ini) {
    rubraview_curation_t curation = {0};
    curation.mode = RUBRAVIEW_CURATION_MOVE;   /* §3.18.3's default */
    if (!arena || settings_ini.len == 0) return curation;

    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, settings_ini);

    for (int i = 1; i <= 9; ++i) {
        char key[8] = { 'd', 'i', 'r', '_', (char)('0' + i), '\0', '\0', '\0' };
        const u8str_t *value = rubraview_ini_get(&doc, U8("curation"), (u8str_t){ .ptr = key, .len = 5 });
        if (value && value->len > 0) curation.dirs[i - 1] = *value;
    }

    const u8str_t *mode = rubraview_ini_get(&doc, U8("curation"), U8("curation_mode"));
    if (mode && mode->len == 4 && memcmp(mode->ptr, "copy", 4) == 0) {
        curation.mode = RUBRAVIEW_CURATION_COPY;
    }

    return curation;
}

u8str_t rubraview_curation_target(const rubraview_curation_t *curation, int32_t digit) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!curation || digit < 1 || digit > 9) return empty;
    return curation->dirs[digit - 1];
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

u8str_t rubraview_shell_progid(proven_arena_t *arena, u8str_t extension) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!arena) return empty;

    /* A leading dot is accepted and dropped, so a caller can pass either
       ".jpg" or "jpg" and get the same ProgID. */
    if (extension.len > 0 && extension.ptr[0] == '.') {
        extension.ptr++;
        extension.len--;
    }
    if (extension.len == 0) return empty;

    static const char PREFIX[] = "Rubraview.";
    size_t prefix_len = sizeof(PREFIX) - 1;
    size_t total = prefix_len + extension.len;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, total + 1);
    if (!proven_is_ok(res.err)) return empty;

    char *out = (char*)(void*)res.value.ptr;
    memcpy(out, PREFIX, prefix_len);
    for (size_t i = 0; i < extension.len; ++i) {
        char c = extension.ptr[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[prefix_len + i] = c;
    }
    out[total] = '\0';

    return (u8str_t){ .ptr = out, .len = total };
}

u8str_t rubraview_shell_extensions(void) {
    return U8("jpg;jpeg;png;gif;bmp;tif;tiff;webp;ico;cbz;cb7;zip;mp4;mkv;webm;avi;mov;mp3;flac;wav;ogg;opus;m4a");
}
