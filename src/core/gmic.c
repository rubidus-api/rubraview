#include "rubraview/gmic.h"
#include "rubraview/lang.h"
#include "rubraview/number.h"
#include "rubraview/utf8.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- JSON, enough of it: what `gmic parse_gui json` writes, read into the arena ---- */

#define JSON_MAX_DEPTH 32

typedef enum { J_NULL = 0, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype_t;

typedef struct jnode {
    jtype_t type;
    proven_u8str_view_t text;          /* a string (decoded) or a number or literal as written */
    struct jnode *kids;                /* an array's items, an object's values */
    proven_u8str_view_t *keys;         /* an object's keys */
    size_t count;
} jnode_t;

typedef struct jparser {
    proven_arena_t *arena;             /* the tree: a scratch arena, given back once the list is read */
    proven_arena_t *strings;           /* decoded strings: the caller's, since the list keeps them */
    const proven_byte_t *p, *end;
    bool failed, full;
} jparser_t;

static void skip_space(jparser_t *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int hex4(const proven_byte_t *p) {
    int v = 0;
    for (int i = 0; i < 4; ++i) {
        int c = p[i], d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return -1;
        v = v * 16 + d;
    }
    return v;
}

/* A string after its opening quote: a view into the text when it holds no escape, else decoded into the arena. */
static proven_u8str_view_t parse_string(jparser_t *j) {
    const proven_byte_t *start = j->p;
    bool escaped = false;
    while (j->p < j->end && *j->p != '"') {
        if (*j->p == '\\') { escaped = true; j->p++; }
        if (j->p < j->end) j->p++;
    }
    if (j->p >= j->end) { j->failed = true; return PROVEN_LIT(""); }
    size_t len = (size_t)(j->p - start);
    j->p++;   /* the closing quote */
    if (!escaped) return rubraview_u8(start, len);
    proven_result_mem_mut_t res = proven_arena_alloc(j->strings, len + 1);   /* never longer once decoded */
    if (!proven_is_ok(res.err)) { j->failed = j->full = true; return PROVEN_LIT(""); }
    uint8_t *out = res.value.ptr;
    size_t n = 0;
    for (size_t i = 0; i < len; ++i) {
        if (start[i] != '\\') { out[n++] = start[i]; continue; }
        if (++i >= len) break;
        switch (start[i]) {
            case 'n': out[n++] = '\n'; break;
            case 't': out[n++] = '\t'; break;
            case 'r': out[n++] = '\r'; break;
            case 'b': out[n++] = '\b'; break;
            case 'f': out[n++] = '\f'; break;
            case 'u': {
                int cp = i + 4 < len ? hex4(start + i + 1) : -1;
                if (cp < 0) { j->failed = true; return PROVEN_LIT(""); }
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < len && start[i + 1] == '\\' && start[i + 2] == 'u') {
                    int lo = hex4(start + i + 3);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
                }
                if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                size_t at = n;
                if (rubraview_utf8_encode((uint32_t)cp, out, len, &at) == 0) { j->failed = true; return PROVEN_LIT(""); }
                n = at;
                break;
            }
            default: out[n++] = start[i]; break;   /* \" \\ \/ */
        }
    }
    out[n] = '\0';
    return rubraview_u8(out, n);
}

typedef struct jlist {
    jnode_t *nodes;
    proven_u8str_view_t *keys;
    size_t count, cap;
} jlist_t;

static bool jlist_push(jparser_t *j, jlist_t *l, proven_u8str_view_t key, jnode_t node, bool keyed) {
    if (l->count == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 8;
        proven_result_mem_mut_t n = rubraview_arena_alloc_array(j->arena, cap, sizeof(jnode_t));
        if (!proven_is_ok(n.err)) return false;
        if (l->count) memcpy(n.value.ptr, l->nodes, l->count * sizeof(jnode_t));
        l->nodes = (jnode_t *)(void *)n.value.ptr;
        if (keyed) {
            proven_result_mem_mut_t k = rubraview_arena_alloc_array(j->arena, cap, sizeof(proven_u8str_view_t));
            if (!proven_is_ok(k.err)) return false;
            if (l->count) memcpy(k.value.ptr, l->keys, l->count * sizeof(proven_u8str_view_t));
            l->keys = (proven_u8str_view_t *)(void *)k.value.ptr;
        }
        l->cap = cap;
    }
    l->nodes[l->count] = node;
    if (keyed) l->keys[l->count] = key;
    l->count++;
    return true;
}

static jnode_t parse_value(jparser_t *j, int depth) {
    jnode_t node = { .type = J_NULL };
    skip_space(j);
    if (j->failed || j->p >= j->end || depth > JSON_MAX_DEPTH) { j->failed = true; return node; }
    proven_byte_t c = *j->p;
    if (c == '"') {
        j->p++;
        node.type = J_STR;
        node.text = parse_string(j);
    } else if (c == '[' || c == '{') {
        bool object = c == '{';
        node.type = object ? J_OBJ : J_ARR;
        j->p++;
        jlist_t list = {0};
        skip_space(j);
        if (j->p < j->end && *j->p == (object ? '}' : ']')) { j->p++; return node; }
        for (;;) {
            proven_u8str_view_t key = PROVEN_LIT("");
            if (object) {
                skip_space(j);
                if (j->p >= j->end || *j->p != '"') { j->failed = true; return node; }
                j->p++;
                key = parse_string(j);
                skip_space(j);
                if (j->p >= j->end || *j->p != ':') { j->failed = true; return node; }
                j->p++;
            }
            jnode_t kid = parse_value(j, depth + 1);
            if (j->failed) return node;
            if (!jlist_push(j, &list, key, kid, object)) { j->failed = j->full = true; return node; }
            skip_space(j);
            if (j->p >= j->end) { j->failed = true; return node; }
            if (*j->p == ',') { j->p++; continue; }
            if (*j->p == (object ? '}' : ']')) { j->p++; break; }
            j->failed = true;
            return node;
        }
        node.kids = list.nodes;
        node.keys = list.keys;
        node.count = list.count;
    } else {
        const proven_byte_t *start = j->p;
        while (j->p < j->end && *j->p != ',' && *j->p != '}' && *j->p != ']' && *j->p != ' ' &&
               *j->p != '\n' && *j->p != '\r' && *j->p != '\t') j->p++;
        node.text = rubraview_u8(start, (size_t)(j->p - start));
        if (node.text.size == 0) { j->failed = true; return node; }
        node.type = c == 't' || c == 'f' ? J_BOOL : c == 'n' ? J_NULL : J_NUM;
    }
    return node;
}

static const jnode_t *member(const jnode_t *obj, const char *key) {
    if (!obj || obj->type != J_OBJ) return NULL;
    for (size_t i = 0; i < obj->count; ++i) {
        if (proven_u8str_view_eq(obj->keys[i], proven_u8str_view_from_cstr(key))) return &obj->kids[i];
    }
    return NULL;
}

/* A member that is a string or a number, as text; empty otherwise. */
static proven_u8str_view_t member_text(const jnode_t *obj, const char *key) {
    const jnode_t *n = member(obj, key);
    return n && (n->type == J_STR || n->type == J_NUM) ? n->text : PROVEN_LIT("");
}

/* ---- the filter list ---- */

/* A name as shown: G'MIC's markup (`<i>`, `<span ...>`) taken out, its five entities decoded. */
static proven_u8str_view_t plain(proven_arena_t *arena, proven_u8str_view_t s) {
    if (s.size == 0 || (!memchr(s.ptr, '<', s.size) && !memchr(s.ptr, '&', s.size))) return s;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, s.size + 1);
    if (!proven_is_ok(res.err)) return s;
    uint8_t *out = res.value.ptr;
    size_t n = 0;
    static const struct { const char *name; char c; } ENT[] = {
        { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' },
    };
    for (size_t i = 0; i < s.size;) {
        if (s.ptr[i] == '<') {
            while (i < s.size && s.ptr[i] != '>') i++;
            i++;
            continue;
        }
        bool done = false;
        if (s.ptr[i] == '&') {
            for (size_t e = 0; e < sizeof(ENT) / sizeof(ENT[0]) && !done; ++e) {
                size_t el = strlen(ENT[e].name);
                if (s.size - i >= el && memcmp(s.ptr + i, ENT[e].name, el) == 0) { out[n++] = (uint8_t)ENT[e].c; i += el; done = true; }
            }
        }
        if (!done) out[n++] = s.ptr[i++];
    }
    out[n] = '\0';
    return proven_u8str_view_trim(rubraview_u8(out, n));
}

static double number_or(proven_u8str_view_t s, double fallback) {
    double v = 0.0;
    return rubraview_parse_double(proven_u8str_view_trim(s), &v) && isfinite(v) ? v : fallback;
}

static bool kind_of(proven_u8str_view_t type, rubraview_gmic_kind_t *out) {
    static const struct { const char *word; rubraview_gmic_kind_t kind; } KINDS[] = {
        { "int", RUBRAVIEW_GMIC_INT }, { "float", RUBRAVIEW_GMIC_FLOAT }, { "bool", RUBRAVIEW_GMIC_BOOL },
        { "choice", RUBRAVIEW_GMIC_CHOICE }, { "color", RUBRAVIEW_GMIC_COLOR }, { "point", RUBRAVIEW_GMIC_POINT },
        { "text", RUBRAVIEW_GMIC_TEXT }, { "file", RUBRAVIEW_GMIC_FILE }, { "folder", RUBRAVIEW_GMIC_FOLDER },
        { "value", RUBRAVIEW_GMIC_VALUE }, { "button", RUBRAVIEW_GMIC_BUTTON },
    };
    for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); ++i) {
        if (proven_u8str_view_eq(type, proven_u8str_view_from_cstr(KINDS[i].word))) { *out = KINDS[i].kind; return true; }
    }
    return false;
}

static bool read_param(proven_arena_t *arena, const jnode_t *p, rubraview_gmic_param_t *out) {
    proven_u8str_view_t pos = member_text(p, "pos");
    rubraview_gmic_kind_t kind;
    double place = number_or(pos, 0.0);
    if (pos.size == 0 || place < 1.0 || place > 100000.0 || !kind_of(member_text(p, "type"), &kind)) return false;
    *out = (rubraview_gmic_param_t){ .kind = kind, .pos = (int32_t)place, .name = plain(arena, member_text(p, "name")) };
    out->text = kind == RUBRAVIEW_GMIC_POINT ? member_text(p, "position")
              : kind == RUBRAVIEW_GMIC_VALUE ? member_text(p, "value") : member_text(p, "default");
    out->hidden = kind == RUBRAVIEW_GMIC_VALUE || proven_u8str_view_eq(member_text(p, "visibility"), PROVEN_LIT("0"));
    switch (kind) {
        case RUBRAVIEW_GMIC_INT:
        case RUBRAVIEW_GMIC_FLOAT:
            out->min = number_or(member_text(p, "min"), 0.0);
            out->max = number_or(member_text(p, "max"), out->min);
            if (out->max < out->min) { double t = out->min; out->min = out->max; out->max = t; }
            out->value = fmin(fmax(number_or(out->text, out->min), out->min), out->max);
            break;
        case RUBRAVIEW_GMIC_BOOL:
            out->max = 1.0;
            out->value = number_or(out->text, 0.0) > 0.5 ? 1.0 : 0.0;
            break;
        case RUBRAVIEW_GMIC_CHOICE: {
            const jnode_t *choices = member(p, "choices");
            size_t count = choices && (choices->type == J_OBJ || choices->type == J_ARR) ? choices->count : 0;
            if (count == 0) return false;
            proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, count, sizeof(proven_u8str_view_t));
            if (!proven_is_ok(res.err)) return false;
            out->choices = (proven_u8str_view_t *)(void *)res.value.ptr;
            for (size_t i = 0; i < count; ++i) {
                out->choices[i] = choices->kids[i].type == J_STR ? plain(arena, choices->kids[i].text) : PROVEN_LIT("");
            }
            out->choice_count = (int32_t)count;
            out->max = (double)(count - 1);
            out->value = fmin(fmax(floor(number_or(out->text, 0.0)), 0.0), out->max);
            break;
        }
        default:
            break;
    }
    return true;
}

static int param_cmp(const void *a, const void *b) {
    const rubraview_gmic_param_t *x = (const rubraview_gmic_param_t *)a, *y = (const rubraview_gmic_param_t *)b;
    return x->pos < y->pos ? -1 : x->pos > y->pos ? 1 : 0;
}

bool rubraview_gmic_is_command(proven_u8str_view_t word) {
    if (word.size == 0 || word.size > 128) return false;
    for (size_t i = 0; i < word.size; ++i) {
        proven_byte_t c = word.ptr[i];
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if (!letter && !(i > 0 && c >= '0' && c <= '9')) return false;
    }
    return true;
}

static rubraview_gmic_catalog_t catalog_of(proven_arena_t *arena, const jnode_t *root, const char **why);

rubraview_gmic_catalog_t rubraview_gmic_catalog_parse(proven_arena_t *arena, proven_u8str_view_t json, const char **why) {
    rubraview_gmic_catalog_t cat = {0};
    if (why) *why = NULL;
    if (!arena || json.size == 0) { if (why) *why = N_("the filter list is empty"); return cat; }
    if (json.size > RUBRAVIEW_GMIC_LIST_MAX) { if (why) *why = N_("the filter list is too large"); return cat; }
    /* The tree takes about ten times the text; it lives only while the list is read out of it. */
    size_t room = json.size * 12u + (1u << 20);
    void *scratch_mem = malloc(room);
    if (!scratch_mem) { if (why) *why = N_("out of memory"); return cat; }
    proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = scratch_mem, .size = room });
    jparser_t j = { .arena = &scratch, .strings = arena, .p = json.ptr, .end = json.ptr + json.size };
    jnode_t root = parse_value(&j, 0);
    if (!j.failed) { skip_space(&j); if (j.p != j.end) j.failed = true; }   /* nothing after the list */
    if (j.failed || root.type != J_OBJ) {
        free(scratch_mem);
        if (why) *why = j.full ? N_("out of memory") : N_("the filter list is not G'MIC's JSON");
        return cat;
    }
    cat = catalog_of(arena, &root, why);
    free(scratch_mem);
    return cat;
}

static rubraview_gmic_catalog_t catalog_of(proven_arena_t *arena, const jnode_t *rootp, const char **why) {
    rubraview_gmic_catalog_t cat = {0};
    const jnode_t root = *rootp;
    const jnode_t *cats = member(&root, "categories");
    if (!cats || cats->type != J_ARR || cats->count == 0) { if (why) *why = N_("the filter list has no categories"); return cat; }
    cat.version = member_text(&root, "gmic_version");

    size_t filters = 0;
    for (size_t c = 0; c < cats->count; ++c) {
        const jnode_t *list = member(&cats->kids[c], "filters");
        if (list && list->type == J_ARR) filters += list->count;
    }
    proven_result_mem_mut_t cres = rubraview_arena_alloc_array(arena, cats->count, sizeof(proven_u8str_view_t));
    proven_result_mem_mut_t fres = rubraview_arena_alloc_array(arena, filters ? filters : 1, sizeof(rubraview_gmic_filter_t));
    if (!proven_is_ok(cres.err) || !proven_is_ok(fres.err)) { if (why) *why = N_("out of memory"); return cat; }
    cat.categories = (proven_u8str_view_t *)(void *)cres.value.ptr;
    cat.filters = (rubraview_gmic_filter_t *)(void *)fres.value.ptr;

    for (size_t c = 0; c < cats->count; ++c) {
        const jnode_t *list = member(&cats->kids[c], "filters");
        if (!list || list->type != J_ARR) continue;
        int32_t category = cat.category_count;
        bool used = false;
        for (size_t f = 0; f < list->count; ++f) {
            const jnode_t *fn = &list->kids[f];
            proven_u8str_view_t command = member_text(fn, "command");
            if (!rubraview_gmic_is_command(command)) continue;   /* a list from elsewhere cannot slip in a second command */
            const jnode_t *params = member(fn, "parameters");
            size_t pc = params && params->type == J_ARR ? params->count : 0;
            rubraview_gmic_filter_t *out = &cat.filters[cat.filter_count];
            *out = (rubraview_gmic_filter_t){ .name = plain(arena, member_text(fn, "name")), .command = command, .category = category };
            if (pc > 0) {
                proven_result_mem_mut_t pres = rubraview_arena_alloc_array(arena, pc, sizeof(rubraview_gmic_param_t));
                if (!proven_is_ok(pres.err)) continue;
                out->params = (rubraview_gmic_param_t *)(void *)pres.value.ptr;
                for (size_t k = 0; k < pc; ++k) {
                    if (read_param(arena, &params->kids[k], &out->params[out->param_count])) out->param_count++;
                }
                qsort(out->params, (size_t)out->param_count, sizeof(rubraview_gmic_param_t), param_cmp);
            }
            if (out->name.size == 0) out->name = command;
            cat.filter_count++;
            used = true;
        }
        if (used) cat.categories[cat.category_count++] = plain(arena, member_text(&cats->kids[c], "name"));
    }
    if (cat.filter_count == 0 && why) *why = N_("the filter list names no filter");
    return cat;
}

/* ---- running one ---- */

size_t rubraview_gmic_split(proven_u8str_view_t line, proven_u8str_view_t *items, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < line.size;) {
        while (i < line.size && (line.ptr[i] == ' ' || line.ptr[i] == '\t' || line.ptr[i] == '\r' || line.ptr[i] == '\n')) i++;
        if (i >= line.size) break;
        size_t start = i;
        bool quoted = false;
        for (; i < line.size; ++i) {
            proven_byte_t c = line.ptr[i];
            if (c == '\\' && i + 1 < line.size) { i++; continue; }
            if (c == '"') quoted = !quoted;
            else if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) break;
        }
        if (n == cap) return 0;
        items[n++] = rubraview_u8(line.ptr + start, i - start);
    }
    return n;
}

typedef struct out_buf { char *p; size_t cap, n; bool full; } out_buf_t;

static void put(out_buf_t *o, const char *s, size_t n) {
    if (o->full || o->n + n + 1 > o->cap) { o->full = true; return; }
    memcpy(o->p + o->n, s, n);
    o->n += n;
    o->p[o->n] = '\0';
}

/* Colour and point: numbers and the commas between them, or nothing. */
static bool numbers_only(proven_u8str_view_t s) {
    if (s.size == 0) return false;
    for (size_t i = 0; i < s.size; ++i) {
        proven_byte_t c = s.ptr[i];
        if (!((c >= '0' && c <= '9') || c == '.' || c == ',' || c == '-' || c == '+' || c == 'e' || c == 'E' || c == ' ')) return false;
    }
    return true;
}

/* Text in double quotes. `\` and `"` are escaped, and so are `$`, `{` and `}`, so that what was typed stays
   text and is never a substitution G'MIC would work out. */
static void put_quoted(out_buf_t *o, proven_u8str_view_t s) {
    put(o, "\"", 1);
    for (size_t i = 0; i < s.size; ++i) {
        char c = (char)s.ptr[i];
        if (c == '\\' || c == '"' || c == '$' || c == '{' || c == '}') put(o, "\\", 1);
        if (c == '\n' || c == '\r') c = ' ';
        put(o, &c, 1);
    }
    put(o, "\"", 1);
}

size_t rubraview_gmic_args(const rubraview_gmic_filter_t *filter, const double *values, const proven_u8str_view_t *texts,
                           char *out, size_t cap) {
    if (!filter || !out || cap == 0) return 0;
    out_buf_t o = { .p = out, .cap = cap };
    out[0] = '\0';
    for (int32_t i = 0; i < filter->param_count; ++i) {
        const rubraview_gmic_param_t *p = &filter->params[i];
        if (i > 0) put(&o, ",", 1);
        double v = values && i < RUBRAVIEW_GMIC_MAX_SHOWN && !p->hidden ? values[i] : p->value;
        if (!isfinite(v)) v = p->value;
        proven_u8str_view_t t = texts && i < RUBRAVIEW_GMIC_MAX_SHOWN && !p->hidden && texts[i].ptr ? texts[i] : p->text;
        char num[64];
        int n = 0;
        switch (p->kind) {
            case RUBRAVIEW_GMIC_INT:
                n = snprintf(num, sizeof(num), "%lld", (long long)llround(fmin(fmax(v, p->min), p->max)));
                break;
            case RUBRAVIEW_GMIC_FLOAT:
                n = snprintf(num, sizeof(num), "%.9g", fmin(fmax(v, p->min), p->max));
                break;
            case RUBRAVIEW_GMIC_BOOL:
                n = snprintf(num, sizeof(num), "%d", v > 0.5 ? 1 : 0);
                break;
            case RUBRAVIEW_GMIC_CHOICE:
                n = snprintf(num, sizeof(num), "%d", (int)fmin(fmax(floor(v), 0.0), p->max));
                break;
            case RUBRAVIEW_GMIC_BUTTON:
                n = snprintf(num, sizeof(num), "0");
                break;
            case RUBRAVIEW_GMIC_COLOR:
            case RUBRAVIEW_GMIC_POINT:
                if (!numbers_only(t)) t = numbers_only(p->text) ? p->text : PROVEN_LIT("0");
                put(&o, (const char *)t.ptr, t.size);
                break;
            case RUBRAVIEW_GMIC_VALUE:
                if (numbers_only(t)) put(&o, (const char *)t.ptr, t.size);
                else put_quoted(&o, t);
                break;
            default:   /* text, file, folder */
                put_quoted(&o, t);
                break;
        }
        if (n > 0) put(&o, num, (size_t)n);
    }
    if (o.full) { out[0] = '\0'; return 0; }
    return o.n;
}
