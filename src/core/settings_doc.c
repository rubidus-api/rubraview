#include "rubraview/settings_doc.h"
#include "rubraview/number.h"
#include "rubraview/ini.h"
#include <string.h>
#include <stdlib.h>

/*
 * The settings page document's reader (§3.22, D-13). One line is one
 * node; a line is a row of tokens — words, "quoted text", {sources},
 * `=`, `|`, and ranges written `lo..hi`. The grammar is small on purpose:
 * a mistake names its line and stops, rather than being read some other
 * way than it was meant.
 */

typedef enum token_kind {
    TOKEN_WORD = 0,     /* anything not below: names, numbers, true/false */
    TOKEN_QUOTED,       /* "text", quotes removed */
    TOKEN_SOURCE,       /* {name}, braces removed */
    TOKEN_EQUALS,
    TOKEN_BAR,
} token_kind_t;

typedef struct token {
    token_kind_t kind;
    u8str_t text;
} token_t;

#define MAX_TOKENS 48
#define MAX_CHOICES 32

typedef struct parser {
    proven_arena_t *arena;
    rubraview_settings_doc_t doc;
    int32_t page;                 /* the page lines are going on; -1 before the first */
    uint32_t line;
} parser_t;

static bool fail(parser_t *p, const char *message) {
    if (!p->doc.error) {
        p->doc.error = message;
        p->doc.error_line = p->line;
    }
    return false;
}

static bool word_is(token_t t, const char *word) {
    size_t n = strlen(word);
    return t.kind == TOKEN_WORD && t.text.len == n && memcmp(t.text.ptr, word, n) == 0;
}

/* Splits one line. A `#` outside quotes ends it. */
static int tokenize(parser_t *p, u8str_t line, token_t *out) {
    int count = 0;
    size_t i = 0;
    while (i < line.len) {
        char c = line.ptr[i];
        if (c == ' ' || c == '\t' || c == '\r') { ++i; continue; }
        if (c == '#') break;
        if (count >= MAX_TOKENS) { fail(p, "too many words on one line"); return -1; }

        if (c == '"') {
            size_t start = ++i;
            while (i < line.len && line.ptr[i] != '"') ++i;
            if (i >= line.len) { fail(p, "a quoted text is not closed"); return -1; }
            out[count++] = (token_t){ TOKEN_QUOTED, { .ptr = line.ptr + start, .len = i - start } };
            ++i;
            continue;
        }
        if (c == '{') {
            size_t start = ++i;
            while (i < line.len && line.ptr[i] != '}') ++i;
            if (i >= line.len) { fail(p, "a {source} is not closed"); return -1; }
            out[count++] = (token_t){ TOKEN_SOURCE, { .ptr = line.ptr + start, .len = i - start } };
            ++i;
            continue;
        }
        if (c == '=') { out[count++] = (token_t){ TOKEN_EQUALS, { .ptr = line.ptr + i, .len = 1 } }; ++i; continue; }
        if (c == '|') { out[count++] = (token_t){ TOKEN_BAR, { .ptr = line.ptr + i, .len = 1 } }; ++i; continue; }

        size_t start = i;
        while (i < line.len && line.ptr[i] != ' ' && line.ptr[i] != '\t' && line.ptr[i] != '\r' &&
               line.ptr[i] != '"' && line.ptr[i] != '=' && line.ptr[i] != '|' && line.ptr[i] != '#') {
            ++i;
        }
        out[count++] = (token_t){ TOKEN_WORD, { .ptr = line.ptr + start, .len = i - start } };
    }
    return count;
}

static bool number(u8str_t text, double *out) {
    return rubraview_parse_double(text, out);
}

/* `lo..hi` */
static bool range(u8str_t text, double *lo, double *hi) {
    for (size_t i = 0; i + 1 < text.len; ++i) {
        if (text.ptr[i] == '.' && text.ptr[i + 1] == '.') {
            return number((u8str_t){ .ptr = text.ptr, .len = i }, lo) &&
                   number((u8str_t){ .ptr = text.ptr + i + 2, .len = text.len - i - 2 }, hi);
        }
    }
    return false;
}

/* `section.key`, each part a name settings.ini can hold (D-13). */
static bool place(u8str_t text, u8str_t *section, u8str_t *key) {
    for (size_t i = 0; i < text.len; ++i) {
        if (text.ptr[i] != '.') continue;
        *section = (u8str_t){ .ptr = text.ptr, .len = i };
        *key = (u8str_t){ .ptr = text.ptr + i + 1, .len = text.len - i - 1 };
        return rubraview_ini_name_ok(*section) && rubraview_ini_name_ok(*key);
    }
    return false;
}

static bool add_node(parser_t *p, rubraview_settings_node_t node) {
    if (p->doc.node_count >= RUBRAVIEW_SETTINGS_DOC_MAX_NODES) return fail(p, "too many lines for one document");
    node.line = p->line;
    p->doc.nodes[p->doc.node_count++] = node;
    return true;
}

static const char *copy_word(parser_t *p, u8str_t word) {
    proven_result_mem_mut_t res = proven_arena_alloc(p->arena, word.len + 1);
    if (!proven_is_ok(res.err)) return NULL;
    memcpy(res.value.ptr, word.ptr, word.len);
    res.value.ptr[word.len] = '\0';
    return (const char*)res.value.ptr;
}

/* The words after the label: a range, `step`, `unit`, choices, `= default`, `wired`. */
static bool setting(parser_t *p, const token_t *t, int n, rubraview_setting_type_t type) {
    if (p->page < 0) return fail(p, "a setting before the first page");
    if (n < 3 || t[1].kind != TOKEN_WORD || t[2].kind != TOKEN_QUOTED) {
        return fail(p, "a setting needs section.key and a \"label\"");
    }
    if (p->doc.def_count >= RUBRAVIEW_SETTINGS_MAX) return fail(p, "more settings than the store holds");

    rubraview_setting_def_t def = {
        .label = t[2].text, .tab = (rubraview_settings_tab_t)p->page, .type = type,
        .step = type == RUBRAVIEW_SETTING_BOOL || type == RUBRAVIEW_SETTING_CHOICE ? 1.0 : 0.0,
        .max_value = type == RUBRAVIEW_SETTING_BOOL ? 1.0 : 0.0,
        .unit = { .ptr = "", .len = 0 },
    };
    if (!place(t[1].text, &def.section, &def.key)) {
        return fail(p, "section.key must be two names of a-z, 0-9, _ and -");
    }
    for (size_t i = 0; i < p->doc.def_count; ++i) {
        const rubraview_setting_def_t *other = &p->doc.defs[i];
        if (other->section.len == def.section.len && other->key.len == def.key.len &&
            memcmp(other->section.ptr, def.section.ptr, def.section.len) == 0 &&
            memcmp(other->key.ptr, def.key.ptr, def.key.len) == 0) {
            return fail(p, "this section.key is already on a page");
        }
    }

    const char *choices[MAX_CHOICES];
    int choice_count = 0;
    bool have_range = false, have_default = false;
    int i = 3;
    while (i < n) {
        if (t[i].kind == TOKEN_EQUALS) {
            if (i + 1 >= n || t[i + 1].kind != TOKEN_WORD) return fail(p, "`=` needs a default after it");
            u8str_t v = t[i + 1].text;
            if (type == RUBRAVIEW_SETTING_BOOL) {
                if (word_is(t[i + 1], "true")) def.default_value = 1.0;
                else if (word_is(t[i + 1], "false")) def.default_value = 0.0;
                else return fail(p, "a toggle's default is true or false");
            } else if (type == RUBRAVIEW_SETTING_CHOICE) {
                bool found = false;
                for (int k = 0; k < choice_count; ++k) {
                    if (strlen(choices[k]) == v.len && memcmp(choices[k], v.ptr, v.len) == 0) {
                        def.default_value = (double)k;
                        found = true;
                    }
                }
                if (!found) return fail(p, "a choice's default must be one of its choices");
            } else if (type == RUBRAVIEW_SETTING_INT || type == RUBRAVIEW_SETTING_FLOAT) {
                if (!number(v, &def.default_value)) return fail(p, "a number's default must be a number");
            } else {
                return fail(p, "a path has no default");
            }
            have_default = true;
            i += 2;
        } else if (word_is(t[i], "wired")) {
            def.wired = true;
            ++i;
        } else if (word_is(t[i], "step")) {
            if (i + 1 >= n || !number(t[i + 1].text, &def.step) || def.step <= 0.0) {
                return fail(p, "`step` needs a positive number");
            }
            i += 2;
        } else if (word_is(t[i], "unit")) {
            if (i + 1 >= n || t[i + 1].kind != TOKEN_QUOTED) return fail(p, "`unit` needs a \"text\"");
            def.unit = t[i + 1].text;
            i += 2;
        } else if (t[i].kind == TOKEN_WORD && (type == RUBRAVIEW_SETTING_INT || type == RUBRAVIEW_SETTING_FLOAT) &&
                   !have_range) {
            if (!range(t[i].text, &def.min_value, &def.max_value) || def.min_value > def.max_value) {
                return fail(p, "a number needs a range written lo..hi");
            }
            have_range = true;
            ++i;
        } else if (t[i].kind == TOKEN_WORD && type == RUBRAVIEW_SETTING_CHOICE) {
            if (!rubraview_ini_name_ok(t[i].text)) return fail(p, "a choice is a name of a-z, 0-9, _ and -");
            if (choice_count >= (int)(sizeof(choices) / sizeof(choices[0])) - 1) return fail(p, "too many choices");
            choices[choice_count] = copy_word(p, t[i].text);
            if (!choices[choice_count]) return fail(p, "out of memory");
            choice_count++;
            ++i;
            if (i < n && t[i].kind == TOKEN_BAR) ++i;
        } else {
            return fail(p, "a word this kind of setting does not take");
        }
    }

    if (type == RUBRAVIEW_SETTING_INT || type == RUBRAVIEW_SETTING_FLOAT) {
        if (!have_range) return fail(p, "a number needs a range written lo..hi");
        if (def.step <= 0.0) def.step = type == RUBRAVIEW_SETTING_INT ? 1.0 : 0.1;
        if (def.default_value < def.min_value || def.default_value > def.max_value) {
            return fail(p, "the default is outside the range");
        }
    }
    if (type == RUBRAVIEW_SETTING_CHOICE) {
        if (choice_count < 2) return fail(p, "a choice needs at least two choices");
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(p->arena, (size_t)(choice_count + 1), sizeof(char*));
        if (!proven_is_ok(res.err)) return fail(p, "out of memory");
        const char **list = (const char**)(void*)res.value.ptr;
        for (int k = 0; k < choice_count; ++k) list[k] = choices[k];
        list[choice_count] = NULL;
        def.choices = list;
        def.choice_count = choice_count;
        def.max_value = (double)(choice_count - 1);
    }
    if (!have_default && type != RUBRAVIEW_SETTING_PATH) return fail(p, "a setting needs `= default`");

    size_t index = p->doc.def_count++;
    p->doc.defs[index] = def;
    return add_node(p, (rubraview_settings_node_t){
        .kind = RUBRAVIEW_NODE_SETTING, .page = p->page, .setting = (int32_t)index,
    });
}

static bool one_line(parser_t *p, u8str_t line) {
    token_t t[MAX_TOKENS];
    int n = tokenize(p, line, t);
    if (n < 0) return false;
    if (n == 0) return true;
    if (t[0].kind != TOKEN_WORD) return fail(p, "a line starts with what it is: page, section, toggle, ...");

    if (word_is(t[0], "page")) {
        if (n != 3 || t[1].kind != TOKEN_WORD || t[2].kind != TOKEN_QUOTED || !rubraview_ini_name_ok(t[1].text)) {
            return fail(p, "page needs an id and a \"title\"");
        }
        if (p->doc.page_count >= RUBRAVIEW_SETTINGS_DOC_MAX_PAGES) return fail(p, "too many pages");
        p->page = (int32_t)p->doc.page_count++;
        return add_node(p, (rubraview_settings_node_t){
            .kind = RUBRAVIEW_NODE_PAGE, .page = p->page, .name = t[1].text, .text = t[2].text,
        });
    }
    if (p->page < 0) return fail(p, "everything goes on a page, and there is none yet");

    if (word_is(t[0], "section")) {
        if (n != 2 || t[1].kind != TOKEN_QUOTED) return fail(p, "section needs a \"title\"");
        return add_node(p, (rubraview_settings_node_t){ .kind = RUBRAVIEW_NODE_SECTION, .page = p->page, .text = t[1].text });
    }
    if (word_is(t[0], "toggle")) return setting(p, t, n, RUBRAVIEW_SETTING_BOOL);
    if (word_is(t[0], "choice")) return setting(p, t, n, RUBRAVIEW_SETTING_CHOICE);
    if (word_is(t[0], "int"))    return setting(p, t, n, RUBRAVIEW_SETTING_INT);
    if (word_is(t[0], "float"))  return setting(p, t, n, RUBRAVIEW_SETTING_FLOAT);
    if (word_is(t[0], "path"))   return setting(p, t, n, RUBRAVIEW_SETTING_PATH);

    if (word_is(t[0], "info")) {
        if (n != 3 || t[1].kind != TOKEN_QUOTED || t[2].kind != TOKEN_SOURCE) return fail(p, "info needs a \"label\" and a {source}");
        return add_node(p, (rubraview_settings_node_t){
            .kind = RUBRAVIEW_NODE_INFO, .page = p->page, .text = t[1].text, .name = t[2].text,
        });
    }
    if (word_is(t[0], "note")) {
        if (n != 2 || t[1].kind != TOKEN_QUOTED) return fail(p, "note needs its \"text\"");
        return add_node(p, (rubraview_settings_node_t){ .kind = RUBRAVIEW_NODE_NOTE, .page = p->page, .text = t[1].text });
    }
    if (word_is(t[0], "preview")) {
        double rows = 1.0;
        if (n < 2 || n > 3 || t[1].kind != TOKEN_WORD || (n == 3 && (!number(t[2].text, &rows) || rows < 1.0 || rows > 20.0))) {
            return fail(p, "preview needs a sample name and, if it is taller than a row, how many rows");
        }
        return add_node(p, (rubraview_settings_node_t){
            .kind = RUBRAVIEW_NODE_PREVIEW, .page = p->page, .name = t[1].text, .rows = (int32_t)rows,
        });
    }
    if (word_is(t[0], "table")) {
        if (n != 2 || t[1].kind != TOKEN_WORD) return fail(p, "table needs a source name");
        return add_node(p, (rubraview_settings_node_t){ .kind = RUBRAVIEW_NODE_TABLE, .page = p->page, .name = t[1].text });
    }
    if (word_is(t[0], "action")) {
        if (n != 3 || t[1].kind != TOKEN_WORD || t[2].kind != TOKEN_QUOTED) return fail(p, "action needs a name and a \"label\"");
        return add_node(p, (rubraview_settings_node_t){
            .kind = RUBRAVIEW_NODE_ACTION, .page = p->page, .name = t[1].text, .text = t[2].text,
        });
    }
    return fail(p, "not a kind of line this document knows");
}

rubraview_settings_doc_t rubraview_settings_doc_parse(proven_arena_t *arena, u8str_t text) {
    parser_t p = { .arena = arena, .page = -1, .line = 0 };
    if (!arena) {
        p.doc.error = "no memory to parse into";
        return p.doc;
    }
    proven_result_mem_mut_t defs = rubraview_arena_alloc_array(arena, RUBRAVIEW_SETTINGS_MAX, sizeof(rubraview_setting_def_t));
    proven_result_mem_mut_t nodes = rubraview_arena_alloc_array(arena, RUBRAVIEW_SETTINGS_DOC_MAX_NODES, sizeof(rubraview_settings_node_t));
    if (!proven_is_ok(defs.err) || !proven_is_ok(nodes.err)) {
        p.doc.error = "no memory to parse into";
        return p.doc;
    }
    p.doc.defs = (rubraview_setting_def_t*)(void*)defs.value.ptr;
    p.doc.nodes = (rubraview_settings_node_t*)(void*)nodes.value.ptr;

    size_t start = 0;
    for (size_t i = 0; i <= text.len; ++i) {
        if (i < text.len && text.ptr[i] != '\n') continue;
        p.line++;
        if (!one_line(&p, (u8str_t){ .ptr = text.ptr + start, .len = i - start })) break;
        start = i + 1;
    }
    if (!p.doc.error && p.doc.page_count == 0) {
        p.line = 0;
        fail(&p, "the document has no page");
    }
    return p.doc;
}

/* ---- the built-in document ---- */

static unsigned char g_doc_memory[128u * 1024u];
static proven_arena_t g_doc_arena;
static rubraview_settings_doc_t g_doc;
static bool g_doc_ready;

const rubraview_settings_doc_t *rubraview_settings_document(void) {
    if (!g_doc_ready) {
        g_doc_arena = proven_arena_create((proven_mem_mut_t){ .ptr = g_doc_memory, .size = sizeof(g_doc_memory) });
        g_doc = rubraview_settings_doc_parse(&g_doc_arena, rubraview_default_settings_document());
        g_doc_ready = true;
    }
    return &g_doc;
}

u8str_t rubraview_settings_page_title(size_t page) {
    const rubraview_settings_doc_t *doc = rubraview_settings_document();
    for (size_t i = 0; i < doc->node_count; ++i) {
        if (doc->nodes[i].kind == RUBRAVIEW_NODE_PAGE && doc->nodes[i].page == (int32_t)page) return doc->nodes[i].text;
    }
    return (u8str_t){ .ptr = "", .len = 0 };
}
