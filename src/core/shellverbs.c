/* RV-279: a file's Explorer verbs from the registry (shellverbs.h). */
#include "rubraview/shellverbs.h"
#include "rubraview/shellreq.h"
#include "rubraview/glob.h"
#include "rubraview/lang.h"

#define KEY_CAP 600
#define TEXT_CAP 1100

static bool copy_text(char *dst, size_t cap, const char *src) {
    size_t n = src ? proven_cstr_len(src) : 0;
    if (cap == 0) return false;
    bool fits = n < cap;
    if (!fits) n = cap - 1;
    proven_sys_mem_copy(dst, src, n);
    dst[n] = '\0';
    return fits;
}

static bool join3(char *dst, size_t cap, const char *a, const char *b, const char *c) {
    int n = rubraview_fmt(dst, cap, "{}{}{}", PROVEN_ARG(a), PROVEN_ARG(b), PROVEN_ARG(c));
    return n >= 0 && (size_t)n < cap;
}

static bool reg_value(const rubraview_shellreg_t *reg, rubraview_shellreg_root_t root, const char *key, const char *name,
                      char *out, size_t cap) {
    out[0] = '\0';
    return reg->value && reg->value(reg->ctx, root, key, name, out, cap);
}

static bool reg_has(const rubraview_shellreg_t *reg, const char *key, const char *name) {
    char scratch[TEXT_CAP];   /* room for what the value holds: a short buffer read as "not there" (VM: "Apartment") */
    if (!reg->value) return false;
    return reg->value(reg->ctx, RUBRAVIEW_SHELLREG_CLASSES, key, name, scratch, sizeof scratch);
}

static bool eq_ci(const char *a, const char *b) { return rubraview_cstr_cmp_ci(a, b) == 0; }

/* The file name after the last separator, and its extension with the dot ("" when it has none). */
static const char *file_name_of(const char *path) {
    const char *name = path;
    for (const char *p = path; *p; ++p) if (*p == '\\' || *p == '/') name = p + 1;
    return name;
}

static const char *extension_of(const char *name) {
    const char *dot = rubraview_cstr_rchr(name, '.');
    return dot && dot != name ? dot : "";
}

/* ---- AppliesTo: the simple forms only ---- */

static proven_u8str_view_t trim(proven_u8str_view_t v) { return proven_u8str_view_trim(v); }

static proven_u8str_view_t unquote(proven_u8str_view_t v) {
    v = trim(v);
    if (v.size >= 2 && v.ptr[0] == '"' && v.ptr[v.size - 1] == '"') v = proven_u8str_view_slice(v, 1, v.size - 2);
    return v;
}

/* One term: true and *match when it is one of the forms read here. */
static bool applies_term(proven_u8str_view_t term, const char *name, bool *match) {
    term = trim(term);
    static const char *const FIELDS[] = { "System.FileName:", "System.ItemName:", "System.FileExtension:", "System.ItemNameDisplay:" };
    for (size_t f = 0; f < sizeof FIELDS / sizeof FIELDS[0]; ++f) {
        proven_u8str_view_t field = proven_u8str_view_from_cstr(FIELDS[f]);
        if (!proven_u8str_view_starts_with_ascii_ci(term, field)) continue;
        proven_u8str_view_t rest = proven_u8str_view_slice(term, field.size, term.size - field.size);
        rest = trim(rest);
        bool exact = rest.size > 0 && rest.ptr[0] == '=';   /* `:=` is equality, `:` alone is the pattern Explorer matches */
        if (exact) rest = proven_u8str_view_slice(rest, 1, rest.size - 1);
        rest = unquote(rest);
        if (rest.size == 0) return false;
        if (f == 2) {   /* the extension, with its dot or without */
            const char *ext = extension_of(name);
            proven_u8str_view_t have = proven_u8str_view_from_cstr(ext[0] ? ext + 1 : "");
            if (rest.ptr[0] == '.') rest = proven_u8str_view_slice(rest, 1, rest.size - 1);
            *match = proven_u8str_view_eq_ascii_ci(have, rest);
            return true;
        }
        proven_u8str_view_t n = proven_u8str_view_from_cstr(name);
        *match = exact ? proven_u8str_view_eq_ascii_ci(n, rest) : rubraview_glob_match(n, rest);
        return true;
    }
    return false;
}

/* The whole: terms joined by OR. False when any part is not a simple form: the verb is then left out. */
static bool applies_to(const char *query, const char *name, bool *match) {
    proven_u8str_view_t q = proven_u8str_view_from_cstr(query);
    *match = false;
    size_t start = 0;
    for (size_t i = 0; i <= q.size; ) {
        bool at_or = i + 4 <= q.size && proven_u8str_view_eq_ascii_ci(proven_u8str_view_slice(q, i, 4), PROVEN_LIT(" OR "));
        if (i == q.size || at_or) {
            bool m = false;
            if (!applies_term(proven_u8str_view_slice(q, start, i - start), name, &m)) return false;
            *match = *match || m;
            if (i == q.size) break;
            i += 4;
            start = i;
            continue;
        }
        char c = (char)q.ptr[i];
        if (c == '(' || c == ')' || c == '~' || c == '<' || c == '>') return false;   /* the query language's other parts */
        ++i;
    }
    return true;
}

/* ---- the words ---- */

/* Explorer's own words for a verb with none of its own. */
static const char *standard_words(const char *name) {
    static const struct { const char *name, *words; } WORDS[] = {
        { "open", N_("Open") }, { "edit", N_("Edit") }, { "print", N_("Print") }, { "play", N_("Play") },
        { "runas", N_("Run as administrator") }, { "runasuser", N_("Run as a different user") },
        { "explore", N_("Explore") }, { "opennew", N_("Open in new window") }, { "find", N_("Search...") },
        { "openas", N_("Open with...") }, { "properties", N_("Properties") },
    };
    for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; ++i) if (eq_ci(name, WORDS[i].name)) return WORDS[i].words;
    return NULL;
}

/* '&' dropped ("&&" is one '&'), as a menu shows the words; a key in brackets after the words, as East Asian
   Windows writes it ("설정(&B)"), dropped whole, as Windows 11's menu shows it. */
static void drop_mnemonics(char *text) {
    size_t w = 0;
    for (size_t r = 0; text[r]; ++r) {
        if (text[r] == '(' && text[r + 1] == '&' && text[r + 2] && text[r + 2] != '&' && text[r + 3] == ')') {
            r += 3;
            while (w > 0 && text[w - 1] == ' ') --w;
            continue;
        }
        if (text[r] == '&') {
            if (text[r + 1] == '&') { text[w++] = '&'; ++r; }
            continue;
        }
        text[w++] = text[r];
    }
    text[w] = '\0';
}

static void verb_words(const rubraview_shellreg_t *reg, const char *key, const char *name, rubraview_shellverb_t *v) {
    char text[TEXT_CAP];
    bool got = reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, key, "MUIVerb", text, sizeof text) && text[0];
    if (!got) got = reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, key, "", text, sizeof text) && text[0];
    if (got && text[0] == '@') {
        char read[TEXT_CAP];
        got = reg->indirect && reg->indirect(reg->ctx, text, read, sizeof read) && read[0];
        if (got) copy_text(text, sizeof text, read);
    }
    v->standard = false;
    if (!got) {
        const char *words = standard_words(name);
        v->standard = words != NULL;
        copy_text(text, sizeof text, words ? words : name);
    }
    copy_text(v->caption, sizeof v->caption, text);
    if (!v->standard) drop_mnemonics(v->caption);
}

/* ---- the command line ---- */

bool rubraview_shellverb_program(const char *command, char *out, size_t cap) {
    if (!command || !out || cap == 0) return false;
    const char *p = command;
    while (*p == ' ' || *p == '\t') ++p;
    const char *start = p, *end;
    if (*p == '"') {
        start = ++p;
        while (*p && *p != '"') ++p;
        end = p;
    } else {
        while (*p && *p != ' ' && *p != '\t') ++p;
        end = p;
    }
    size_t n = (size_t)(end - start);
    if (n == 0 || n >= cap) { out[0] = '\0'; return false; }
    proven_sys_mem_copy(out, start, n);
    out[n] = '\0';
    return true;
}

static bool is_name_char(char c) { return rubraview_ascii_isalpha((unsigned char)c) || rubraview_ascii_isdigit((unsigned char)c) || c == '_'; }

bool rubraview_shellverb_command(const char *command, const char *path, char *out, size_t cap) {
    if (!command || !path || !out || cap == 0) return false;
    const char *name = file_name_of(path);
    size_t folder_len = (size_t)(name - path);
    if (folder_len > 0 && (path[folder_len - 1] == '\\' || path[folder_len - 1] == '/') && folder_len > 3) folder_len--;   /* "C:\" keeps its separator */
    size_t w = 0;
    bool placed = false;
#define PUT(src, n) do { size_t n_ = (n); if (w + n_ >= cap) return false; proven_sys_mem_copy(out + w, (src), n_); w += n_; } while (0)
    for (const char *p = command; *p; ++p) {
        if (*p != '%') { PUT(p, 1); continue; }
        char c = p[1];
        if (c == '\0') return false;
        if (c == '%') { PUT("%", 1); ++p; continue; }
        if (is_name_char(c) && is_name_char(p[2])) {   /* %SystemRoot% and its kind: not expanded, Windows' to run */
            const char *q = p + 1;
            while (is_name_char(*q)) ++q;
            if (*q == '%') return false;
        }
        switch (c) {
            case '1': case 'L': case 'l': case 'V': case 'v': case 'D': case 'd': case '0':
                PUT(path, proven_cstr_len(path)); placed = true; break;
            case 'W': case 'w':
                PUT(path, folder_len); break;
            case '*': case '~': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                break;
            default:
                return false;   /* %I, %S, %H and the others: an item list, a show state, a hot key */
        }
        ++p;
    }
#undef PUT
    out[w] = '\0';
    return placed;
}

/* ---- the verbs ---- */

typedef struct gather {
    const rubraview_shellreg_t *reg;
    const char *name;    /* the file's name, for AppliesTo */
    bool shift;
    size_t files;
    rubraview_shellverb_t *out;
    size_t cap, count;
    size_t top;          /* how many `Position=Top` ones are at the start */
    int32_t position[RUBRAVIEW_SHELLVERBS_MAX * 2];   /* -1 top, 0 as read, 1 bottom */
} gather_t;

static bool seen(const gather_t *g, const char *name) {
    for (size_t i = 0; i < g->count; ++i) if (eq_ci(g->out[i].name, name)) return true;
    return false;
}

static bool hidden(const gather_t *g, const char *key, const char *name) {
    const rubraview_shellreg_t *reg = g->reg;
    if (eq_ci(name, "printto")) return true;   /* a printer's drop target, never a menu item */
    static const char *const HIDE[] = { "ProgrammaticAccessOnly", "LegacyDisable", "OnlyInBrowserWindow",
                                        "HideBasedOnVelocityId", "CommandStateHandler" };
    for (size_t i = 0; i < sizeof HIDE / sizeof HIDE[0]; ++i) if (reg_has(reg, key, HIDE[i])) return true;
    if (!g->shift && reg_has(reg, key, "Extended")) return true;
    char text[TEXT_CAP];
    if (reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, key, "AppliesTo", text, sizeof text) && text[0]) {
        bool match = false;
        if (!applies_to(text, g->name, &match) || !match) return true;
    }
    return false;
}

static int32_t multi_model(const rubraview_shellreg_t *reg, const char *key) {
    char text[32];
    if (!reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, key, "MultiSelectModel", text, sizeof text)) return RUBRAVIEW_SHELLVERB_MULTI_DOCUMENT;
    if (eq_ci(text, "Single")) return RUBRAVIEW_SHELLVERB_MULTI_SINGLE;
    if (eq_ci(text, "Player")) return RUBRAVIEW_SHELLVERB_MULTI_PLAYER;
    return RUBRAVIEW_SHELLVERB_MULTI_DOCUMENT;
}

static bool multi_allows(int32_t multi, size_t files) {
    if (multi == RUBRAVIEW_SHELLVERB_MULTI_SINGLE) return files <= 1;
    if (multi == RUBRAVIEW_SHELLVERB_MULTI_DOCUMENT) return files <= RUBRAVIEW_SHELLVERB_DOCUMENT_MAX;
    return true;
}

/* The verb at `key` (its own words, its command) into a fresh row; false when it is left out. */
static bool read_verb(gather_t *g, const char *key, const char *name, const char *caption_prefix, bool child,
                      rubraview_shellverb_t *v) {
    const rubraview_shellreg_t *reg = g->reg;
    *v = (rubraview_shellverb_t){0};
    if (!copy_text(v->name, sizeof v->name, name)) return false;
    verb_words(reg, key, child ? file_name_of(name) : name, v);
    if (caption_prefix && caption_prefix[0]) {
        char both[sizeof v->caption];
        if (rubraview_fmt(both, sizeof both, "{} \xe2\x80\xba {}", PROVEN_ARG(caption_prefix), PROVEN_ARG(v->caption)) >= (int)sizeof both) return false;
        copy_text(v->caption, sizeof v->caption, both);
        v->standard = false;
    }
    v->multi = multi_model(reg, key);
    if (!multi_allows(v->multi, g->files)) return false;
    char command_key[KEY_CAP], command[TEXT_CAP];
    if (!join3(command_key, sizeof command_key, key, "\\command", "")) return false;
    bool has_command = reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, command_key, "", command, sizeof command) && command[0];
    if (rubraview_shell_entry_is_ours(proven_u8str_view_from_cstr(file_name_of(name)), proven_u8str_view_from_cstr(has_command ? command : "")))
        return false;
    char drop_key[KEY_CAP];
    bool delegated = reg_has(reg, command_key, "DelegateExecute") || reg_has(reg, key, "ExplorerCommandHandler") ||
                     (join3(drop_key, sizeof drop_key, key, "\\DropTarget", "") && reg_has(reg, drop_key, "Clsid"));
    char probe[TEXT_CAP];
    v->by_shell = delegated || !has_command || !rubraview_shellverb_command(command, "C:\\x\\y.z", probe, sizeof probe);
    if (!v->by_shell && !copy_text(v->command, sizeof v->command, command)) v->by_shell = true;
    if (v->by_shell) v->command[0] = '\0';
    if (child && v->by_shell) return false;   /* Windows finds a verb by name among a file's own, never inside a cascade */
    return true;
}

static void push(gather_t *g, const rubraview_shellverb_t *v, int32_t position) {
    if (g->count >= g->cap || g->count >= sizeof g->position / sizeof g->position[0]) return;
    g->out[g->count] = *v;
    g->position[g->count] = position;
    g->count++;
}

/* A cascade's children, from `shell` (ExtendedSubCommandsKey's `shell`, under HKCR). */
static void cascade_children(gather_t *g, const char *shell, const char *parent_name, const char *parent_caption, int32_t position) {
    char verb[256], key[KEY_CAP], name[200];
    for (size_t i = 0; g->reg->subkey && g->reg->subkey(g->reg->ctx, RUBRAVIEW_SHELLREG_CLASSES, shell, i, verb, sizeof verb); ++i) {
        if (!join3(key, sizeof key, shell, "\\", verb)) continue;
        if (hidden(g, key, verb)) continue;
        if (rubraview_fmt(name, sizeof name, "{}\\{}", PROVEN_ARG(parent_name), PROVEN_ARG(verb)) >= (int)sizeof name) continue;
        rubraview_shellverb_t v;
        if (read_verb(g, key, name, parent_caption, true, &v)) push(g, &v, position);
    }
}

/* SubCommands "a;b;c": the verbs of those names in the machine's CommandStore. */
#define COMMAND_STORE "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CommandStore\\shell\\"

static void store_children(gather_t *g, const char *list, const char *parent_name, const char *parent_caption, int32_t position) {
    const char *p = list;
    while (*p) {
        const char *end = p;
        while (*end && *end != ';') ++end;
        char one[160];
        size_t n = (size_t)(end - p);
        if (n > 0 && n < sizeof one) {
            proven_sys_mem_copy(one, p, n);
            one[n] = '\0';
            char key[KEY_CAP], command[TEXT_CAP], words[TEXT_CAP], name[240];
            bool has_command = join3(key, sizeof key, COMMAND_STORE, one, "\\command") &&
                               reg_value(g->reg, RUBRAVIEW_SHELLREG_MACHINE, key, "", command, sizeof command) && command[0];
            char probe[TEXT_CAP], vkey[KEY_CAP];
            if (has_command && rubraview_shellverb_command(command, "C:\\x\\y.z", probe, sizeof probe) &&
                join3(vkey, sizeof vkey, COMMAND_STORE, one, "") &&
                rubraview_fmt(name, sizeof name, "{}\\{}", PROVEN_ARG(parent_name), PROVEN_ARG(one)) < (int)sizeof name) {
                rubraview_shellverb_t v = {0};
                copy_text(v.name, sizeof v.name, name);
                bool got = reg_value(g->reg, RUBRAVIEW_SHELLREG_MACHINE, vkey, "MUIVerb", words, sizeof words) && words[0];
                if (!got) got = reg_value(g->reg, RUBRAVIEW_SHELLREG_MACHINE, vkey, "", words, sizeof words) && words[0];
                if (got && words[0] == '@') {
                    char read[TEXT_CAP];
                    got = g->reg->indirect && g->reg->indirect(g->reg->ctx, words, read, sizeof read) && read[0];
                    if (got) copy_text(words, sizeof words, read);
                }
                if (!got) copy_text(words, sizeof words, one);
                drop_mnemonics(words);
                if (rubraview_fmt(v.caption, sizeof v.caption, "{} \xe2\x80\xba {}", PROVEN_ARG(parent_caption), PROVEN_ARG(words)) < (int)sizeof v.caption &&
                    !rubraview_shell_entry_is_ours(proven_u8str_view_from_cstr(one), proven_u8str_view_from_cstr(command)) &&
                    copy_text(v.command, sizeof v.command, command)) {
                    push(g, &v, position);
                }
            }
        }
        p = *end ? end + 1 : end;
    }
}

/* Every verb under `base`\shell not taken by an earlier key. */
static void gather_from(gather_t *g, const char *base, bool progid) {
    const rubraview_shellreg_t *reg = g->reg;
    char shell[KEY_CAP], verb[256], key[KEY_CAP], default_verb[128] = "";
    if (!join3(shell, sizeof shell, base, "\\shell", "")) return;
    if (progid) reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, shell, "", default_verb, sizeof default_verb);
    bool any_default = false;
    for (size_t i = 0; reg->subkey && reg->subkey(reg->ctx, RUBRAVIEW_SHELLREG_CLASSES, shell, i, verb, sizeof verb); ++i) {
        if (seen(g, verb)) continue;
        if (!join3(key, sizeof key, shell, "\\", verb)) continue;
        if (hidden(g, key, verb)) continue;
        char text[TEXT_CAP];
        int32_t position = 0;
        if (reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, key, "Position", text, sizeof text))
            position = eq_ci(text, "Top") ? -1 : eq_ci(text, "Bottom") ? 1 : 0;
        bool has_sub = reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, key, "SubCommands", text, sizeof text);
        char ext_key[KEY_CAP];
        bool has_ext = reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, key, "ExtendedSubCommandsKey", ext_key, sizeof ext_key) && ext_key[0];
        if (has_sub || has_ext) {   /* a cascade: its children, one level down */
            if (rubraview_shell_entry_is_ours(proven_u8str_view_from_cstr(verb), PROVEN_LIT(""))) continue;
            rubraview_shellverb_t parent = {0};
            verb_words(reg, key, verb, &parent);
            if (has_ext) {
                char sub_shell[KEY_CAP];
                if (join3(sub_shell, sizeof sub_shell, ext_key, "\\shell", "")) cascade_children(g, sub_shell, verb, parent.caption, position);
            }
            if (has_sub && text[0]) store_children(g, text, verb, parent.caption, position);
            continue;
        }
        rubraview_shellverb_t v;
        if (!read_verb(g, key, verb, NULL, false, &v)) continue;
        if (progid && (default_verb[0] ? eq_ci(verb, default_verb) : (!any_default && eq_ci(verb, "open")))) {
            v.is_default = true;
            any_default = true;
        }
        push(g, &v, position);
    }
}

/* The ProgID of an extension: the user's choice first, its CurVer followed. */
static bool progid_of(const rubraview_shellreg_t *reg, const char *ext, char *out, size_t cap) {
    char key[KEY_CAP];
    out[0] = '\0';
    if (join3(key, sizeof key, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\", ext, "\\UserChoice"))
        reg_value(reg, RUBRAVIEW_SHELLREG_USER, key, "ProgId", out, cap);
    if (!out[0]) reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, ext, "", out, cap);
    if (!out[0]) return false;
    char cur[256];
    if (join3(key, sizeof key, out, "\\CurVer", "") && reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, key, "", cur, sizeof cur) && cur[0])
        copy_text(out, cap, cur);
    return true;
}

size_t rubraview_shellverbs_list(const rubraview_shellreg_t *reg, const char *path, bool is_folder, bool shift,
                                 size_t files, rubraview_shellverb_t *out, size_t cap) {
    if (!reg || !path || !out || cap == 0) return 0;
    gather_t g = { .reg = reg, .name = file_name_of(path), .shift = shift, .files = files ? files : 1, .out = out,
                   .cap = cap < RUBRAVIEW_SHELLVERBS_MAX * 2 ? cap : RUBRAVIEW_SHELLVERBS_MAX * 2 };
    if (is_folder) {
        gather_from(&g, "Directory", true);
        gather_from(&g, "Folder", false);
        gather_from(&g, "AllFilesystemObjects", false);
    } else {
        const char *ext = extension_of(g.name);
        char progid[256], key[KEY_CAP], perceived[64];
        if (ext[0] && progid_of(reg, ext, progid, sizeof progid)) gather_from(&g, progid, true);
        if (ext[0] && join3(key, sizeof key, "SystemFileAssociations\\", ext, "")) gather_from(&g, key, false);
        if (ext[0] && reg_value(reg, RUBRAVIEW_SHELLREG_CLASSES, ext, "PerceivedType", perceived, sizeof perceived) && perceived[0] &&
            join3(key, sizeof key, "SystemFileAssociations\\", perceived, ""))
            gather_from(&g, key, false);
        gather_from(&g, "*", false);
        gather_from(&g, "AllFilesystemObjects", false);
    }
    /* The order: the default, Position=Top, as read, Position=Bottom (each kept in the order read). */
    size_t n = 0;
    rubraview_shellverb_t *sorted = (rubraview_shellverb_t *)rubraview_alloc_zero(g.count ? g.count : 1, sizeof *sorted);
    if (!sorted) return 0;
    for (size_t i = 0; i < g.count; ++i) if (g.out[i].is_default) sorted[n++] = g.out[i];
    for (int32_t pos = -1; pos <= 1; ++pos)
        for (size_t i = 0; i < g.count; ++i) if (!g.out[i].is_default && g.position[i] == pos) sorted[n++] = g.out[i];
    size_t keep = n < cap ? n : cap;
    if (keep > RUBRAVIEW_SHELLVERBS_MAX) keep = RUBRAVIEW_SHELLVERBS_MAX;
    for (size_t i = 0; i < keep; ++i) out[i] = sorted[i];
    rubraview_free(sorted);
    return keep;
}
