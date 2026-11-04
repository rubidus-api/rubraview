/* Language packs (RV-114, D-149): see rubraview/lang.h. */
#include "rubraview/core.h"
#include "rubraview/lang.h"

#include <stdlib.h>
#include <string.h>

static const rubraview_lang_t *g_current;

static uint32_t hash_key(proven_u8str_view_t context_view, proven_u8str_view_t english_view) {
    const char *context = (const char *)context_view.ptr;
    size_t context_len = (size_t)context_view.size;
    const char *english = (const char *)english_view.ptr;
    size_t english_len = (size_t)english_view.size;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < context_len; i++) { h ^= (uint8_t)context[i]; h *= 16777619u; }
    h ^= 0x1Fu; h *= 16777619u;            /* between the context and the English */
    for (size_t i = 0; i < english_len; i++) { h ^= (uint8_t)english[i]; h *= 16777619u; }
    return h ? h : 1u;                     /* 0 marks an empty slot */
}

/* ---- placeholders ---- */

/* RV-220 (D-275): formats are proven's: `{}` takes the next argument, `{N}` the N-th, `{:spec}` / `{N:spec}` how;
   `{{` and `}}` are braces. A format's placeholders as (argument, spec) pairs, sorted; false when its braces do not
   pair, or there are more than `cap`. */
typedef struct placeholder { uint32_t arg; proven_u8str_view_t spec; } placeholder_t;

static int placeholder_order(const placeholder_t *a, const placeholder_t *b) {
    if (a->arg != b->arg) return a->arg < b->arg ? -1 : 1;
    return proven_u8str_view_cmp(a->spec, b->spec);
}

static bool placeholders(proven_u8str_view_t s, placeholder_t *out, size_t cap, size_t *count) {
    size_t n = 0;
    uint32_t next = 0;
    for (size_t i = 0; i < s.size; ++i) {
        proven_byte_t c = s.ptr[i];
        if (c == '}') {
            if (i + 1 < s.size && s.ptr[i + 1] == '}') { i++; continue; }
            return false;   /* a lone closing brace */
        }
        if (c != '{') continue;
        if (i + 1 < s.size && s.ptr[i + 1] == '{') { i++; continue; }
        size_t j = i + 1;
        uint32_t arg = 0;
        bool numbered = false;
        while (j < s.size && s.ptr[j] >= '0' && s.ptr[j] <= '9') { arg = arg * 10u + (uint32_t)(s.ptr[j] - '0'); numbered = true; j++; }
        size_t spec_at = j;
        if (j < s.size && s.ptr[j] == ':') while (j < s.size && s.ptr[j] != '}') j++;
        if (j >= s.size || s.ptr[j] != '}') return false;
        if (n >= cap) return false;
        out[n++] = (placeholder_t){ .arg = numbered ? arg : next, .spec = proven_u8str_view_slice(s, spec_at, j - spec_at) };
        if (!numbered) next++;
        i = j;
    }
    for (size_t a = 1; a < n; ++a)   /* insertion sort: a handful */
        for (size_t b = a; b > 0 && placeholder_order(&out[b], &out[b - 1]) < 0; --b) {
            placeholder_t t = out[b]; out[b] = out[b - 1]; out[b - 1] = t;
        }
    *count = n;
    return true;
}

bool rubraview_lang_same_conversions(proven_u8str_view_t english, proven_u8str_view_t translation) {
    placeholder_t e[16], t[16];
    size_t en = 0, tn = 0;
    if (!placeholders(english, e, 16, &en)) return true;   /* no format: a text such as "{parent}" is shown as it is */
    if (!placeholders(translation, t, 16, &tn) || en != tn) return false;
    for (size_t i = 0; i < en; ++i) if (placeholder_order(&e[i], &t[i]) != 0) return false;
    return true;
}

/* ---- reading ---- */

static char *store_put(rubraview_lang_t *lang, proven_u8str_view_t s_view) {
    const char *s = (const char *)s_view.ptr;
    size_t n = (size_t)s_view.size;
    if (lang->store_used + n + 1 > lang->store_size) return NULL;
    char *dst = lang->store + lang->store_used;
    proven_sys_mem_copy(dst, s, n);
    dst[n] = '\0';
    lang->store_used += n + 1;
    return dst;
}

static void skip_spaces(const char **p, const char *end) {
    while (*p < end && (**p == ' ' || **p == '\t')) (*p)++;
}

/* A quoted string at *p, unescaped into out (at most cap bytes). */
static bool read_quoted(const char **p, const char *end, char *out, size_t cap, size_t *out_len) {
    if (*p >= end || **p != '"') return false;
    (*p)++;
    size_t n = 0;
    while (*p < end && **p != '"') {
        char c = **p;
        if (c == '\\') {
            (*p)++;
            if (*p >= end) return false;
            switch (**p) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            default: return false;
            }
        }
        if (n >= cap) return false;
        out[n++] = c;
        (*p)++;
    }
    if (*p >= end) return false;
    (*p)++;                                /* the closing quote */
    *out_len = n;
    return true;
}

static bool read_word(const char **p, const char *end, char *out, size_t cap, size_t *out_len) {
    size_t n = 0;
    while (*p < end) {
        char c = **p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) break;
        if (n >= cap) return false;
        out[n++] = c;
        (*p)++;
    }
    *out_len = n;
    return n > 0;
}

static bool insert(rubraview_lang_t *lang, const char *ctx, size_t ctx_len, const char *en, size_t en_len,
                   const char *text, size_t text_len) {
    uint32_t h = hash_key(rubraview_bytes_view(ctx, ctx_len), rubraview_bytes_view(en, en_len));
    size_t mask = lang->slot_count - 1;
    for (size_t i = h & mask;; i = (i + 1) & mask) {
        rubraview_lang_entry_t *e = &lang->slots[i];
        if (e->hash == 0) {
            const char *c = ctx_len ? store_put(lang, rubraview_bytes_view(ctx, ctx_len)) : "";
            const char *k = store_put(lang, rubraview_bytes_view(en, en_len));
            const char *t = store_put(lang, rubraview_bytes_view(text, text_len));
            if (!c || !k || !t) return false;
            e->hash = h; e->context = c; e->english = k; e->text = t; e->text_len = text_len;
            lang->count++;
            return true;
        }
        if (e->hash == h && proven_cstr_len(e->context) == ctx_len && proven_sys_mem_cmp(e->context, ctx, ctx_len) == 0 &&
            proven_cstr_len(e->english) == en_len && proven_sys_mem_cmp(e->english, en, en_len) == 0) {
            const char *t = store_put(lang, rubraview_bytes_view(text, text_len));   /* a later line wins */
            if (!t) return false;
            e->text = t; e->text_len = text_len;
            return true;
        }
    }
}

void rubraview_lang_free(rubraview_lang_t *lang) {
    if (!lang) return;
    if (g_current == lang) g_current = NULL;
    rubraview_free(lang->slots);
    rubraview_free(lang->store);
    proven_sys_mem_set(lang, 0, sizeof(*lang));
}

bool rubraview_lang_load(rubraview_lang_t *lang, proven_u8str_view_t text, rubraview_lang_stats_t *stats) {
    rubraview_lang_stats_t local;
    if (!stats) stats = &local;
    proven_sys_mem_set(stats, 0, sizeof(*stats));
    proven_sys_mem_set(lang, 0, sizeof(*lang));
    if (text.size > RUBRAVIEW_LANG_MAX_FILE) return false;

    size_t lines = 1;
    for (size_t i = 0; i < text.size; i++) if (text.ptr[i] == '\n') lines++;
    size_t want = lines < RUBRAVIEW_LANG_MAX_ENTRIES ? lines : RUBRAVIEW_LANG_MAX_ENTRIES;
    lang->slot_count = 64;
    while (lang->slot_count < want * 2) lang->slot_count <<= 1;
    lang->slots = (rubraview_lang_entry_t*)rubraview_alloc_zero(lang->slot_count, sizeof(*lang->slots));
    /* Every string is at most as long as its quoted form, plus a NUL each. */
    lang->store_size = text.size + lines * 3 + 16;
    lang->store = (char*)rubraview_alloc(lang->store_size);
    if (!lang->slots || !lang->store) { rubraview_lang_free(lang); return false; }

    char *a = (char*)rubraview_alloc(RUBRAVIEW_LANG_MAX_TEXT), *b = (char*)rubraview_alloc(RUBRAVIEW_LANG_MAX_TEXT);
    char ctx[64];
    if (!a || !b) { rubraview_free(a); rubraview_free(b); rubraview_lang_free(lang); return false; }

    const char *p = (const char *)text.ptr, *end = (const char *)text.ptr + text.size;
    if (end - p >= 3 && (uint8_t)p[0] == 0xEF && (uint8_t)p[1] == 0xBB && (uint8_t)p[2] == 0xBF) p += 3;
    while (p < end) {
        const char *eol = rubraview_mem_chr(p, '\n', (size_t)(end - p));
        const char *line_end = eol ? eol : end;
        const char *q = p;
        p = eol ? eol + 1 : end;
        const char *le = line_end;
        if (le > q && le[-1] == '\r') le--;
        skip_spaces(&q, le);
        if (q >= le || *q == '#') continue;

        size_t ctx_len = 0, a_len = 0, b_len = 0;
        bool ok;
        if (*q == '"') {
            ok = read_quoted(&q, le, a, RUBRAVIEW_LANG_MAX_TEXT, &a_len);
        } else {
            ok = read_word(&q, le, ctx, sizeof(ctx), &ctx_len);
            skip_spaces(&q, le);
            if (ok && q < le && *q == '=') {           /* language = "..." / name = "..." */
                q++; skip_spaces(&q, le);
                ok = read_quoted(&q, le, b, RUBRAVIEW_LANG_MAX_TEXT, &b_len);
                skip_spaces(&q, le);
                if (ok && q == le) {
                    if (ctx_len == 8 && proven_sys_mem_cmp(ctx, "language", 8) == 0 && b_len < sizeof(lang->code)) {
                        proven_sys_mem_copy(lang->code, b, b_len); lang->code[b_len] = '\0';
                    } else if (ctx_len == 4 && proven_sys_mem_cmp(ctx, "name", 4) == 0 && b_len < sizeof(lang->name)) {
                        proven_sys_mem_copy(lang->name, b, b_len); lang->name[b_len] = '\0';
                    }
                    continue;
                }
                stats->broken++;
                continue;
            }
            ok = ok && read_quoted(&q, le, a, RUBRAVIEW_LANG_MAX_TEXT, &a_len);
        }
        skip_spaces(&q, le);
        ok = ok && q < le && *q == '=';
        if (ok) { q++; skip_spaces(&q, le); ok = read_quoted(&q, le, b, RUBRAVIEW_LANG_MAX_TEXT, &b_len); }
        skip_spaces(&q, le);
        if (!ok || q != le || a_len == 0) { stats->broken++; continue; }
        if (!rubraview_lang_same_conversions((proven_u8str_view_t){ (const proven_byte_t *)a, a_len }, (proven_u8str_view_t){ (const proven_byte_t *)b, b_len })) { stats->refused++; continue; }
        if (lang->count >= RUBRAVIEW_LANG_MAX_ENTRIES) { stats->too_many++; continue; }
        if (!insert(lang, ctx, ctx_len, a, a_len, b, b_len)) { stats->too_many++; continue; }
    }
    rubraview_free(a); rubraview_free(b);
    stats->entries = lang->count;
    return true;
}

const rubraview_lang_entry_t *rubraview_lang_find(const rubraview_lang_t *lang, const char *context,
                                                  proven_u8str_view_t english_view) {
    const char *english = (const char *)english_view.ptr;
    size_t english_len = (size_t)english_view.size;
    if (!lang || !lang->slots || lang->count == 0 || !english) return NULL;
    size_t ctx_len = context ? proven_cstr_len(context) : 0;
    if (!context) context = "";
    uint32_t h = hash_key(rubraview_bytes_view(context, ctx_len), rubraview_bytes_view(english, english_len));
    size_t mask = lang->slot_count - 1;
    for (size_t i = h & mask;; i = (i + 1) & mask) {
        const rubraview_lang_entry_t *e = &lang->slots[i];
        if (e->hash == 0) return NULL;
        if (e->hash == h && rubraview_cstr_cmp(e->context, context) == 0 && rubraview_cstr_cmp_n(e->english, rubraview_bytes_view(english, english_len)) == 0 &&
            e->english[english_len] == '\0') {
            return e;
        }
    }
}

/* ---- the pack in use ---- */

void rubraview_lang_use(const rubraview_lang_t *lang) { g_current = lang; }
const rubraview_lang_t *rubraview_lang_current(void) { return g_current; }

const char *rubraview_tr(const char *english) {
    if (!g_current || !english) return english;
    const rubraview_lang_entry_t *e = rubraview_lang_find(g_current, NULL, rubraview_bytes_view(english, proven_cstr_len(english)));
    return e ? e->text : english;
}

const char *rubraview_tr_ctx(const char *context, const char *english) {
    if (!g_current || !english) return english;
    const rubraview_lang_entry_t *e = rubraview_lang_find(g_current, context, rubraview_bytes_view(english, proven_cstr_len(english)));
    if (!e && context && *context) e = rubraview_lang_find(g_current, NULL, rubraview_bytes_view(english, proven_cstr_len(english)));
    return e ? e->text : english;
}

proven_u8str_view_t rubraview_tr_ctx_u8(const char *context, proven_u8str_view_t english) {
    if (!g_current || english.size == 0) return english;
    const rubraview_lang_entry_t *e = rubraview_lang_find(g_current, context, rubraview_bytes_view((const char *)english.ptr, english.size));
    if (!e && context && *context) e = rubraview_lang_find(g_current, NULL, rubraview_bytes_view((const char *)english.ptr, english.size));
    return e ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)e->text, .size = e->text_len } : english;
}

proven_u8str_view_t rubraview_tr_u8(proven_u8str_view_t english) { return rubraview_tr_ctx_u8(NULL, english); }

/* ---- columns ---- */

bool rubraview_codepoint_is_wide(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) || (cp >= 0x3041 && cp <= 0x33FF) ||
           (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE4F) ||
           (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
           (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD);
}

static bool codepoint_is_zero_width(uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1160 && cp <= 0x11FF) || cp == 0x200B ||
           (cp >= 0x200C && cp <= 0x200F) || (cp >= 0xFE00 && cp <= 0xFE0F);
}

/* One UTF-8 character at s[i]: its code point and length (1 for a broken byte). */
static size_t utf8_next(proven_u8str_view_t s, size_t i, uint32_t *cp) {
    uint8_t c = (uint8_t)s.ptr[i];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (n == 0 || i + n > s.size) { *cp = c; return 1; }
    uint32_t v = n == 1 ? c : n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (size_t k = 1; k < n; k++) {
        uint8_t d = (uint8_t)s.ptr[i + k];
        if ((d & 0xC0) != 0x80) { *cp = c; return 1; }
        v = (v << 6) | (d & 0x3Fu);
    }
    *cp = v;
    return n;
}

static size_t cp_columns(uint32_t cp) {
    if (codepoint_is_zero_width(cp)) return 0;
    return rubraview_codepoint_is_wide(cp) ? 2 : 1;
}

size_t rubraview_text_columns(proven_u8str_view_t text) {
    size_t cols = 0;
    for (size_t i = 0; i < text.size;) {
        uint32_t cp;
        i += utf8_next(text, i, &cp);
        cols += cp_columns(cp);
    }
    return cols;
}

size_t rubraview_text_fit(proven_u8str_view_t text, size_t columns) {
    size_t cols = 0, i = 0;
    while (i < text.size) {
        uint32_t cp;
        size_t n = utf8_next(text, i, &cp);
        size_t w = cp_columns(cp);
        if (cols + w > columns) break;
        cols += w;
        i += n;
    }
    return i;
}

/* ---- choosing a pack ---- */

static bool tag_eq(proven_u8str_view_t a_view, const char *b);

const char *rubraview_lang_native_name(const char *code) {
    static const struct { const char *code, *name; } NAMES[] = {
        { "en", "English" },
        { "ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4" },                         /* 한국어 */
        { "ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" },                         /* 日本語 */
        { "zh-hans", "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87" },          /* 简体中文 */
        { "zh-hant", "\xE7\xB9\x81\xE9\xAB\x94\xE4\xB8\xAD\xE6\x96\x87" },          /* 繁體中文 */
        { "fa", "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C" },                         /* فارسی */
    };
    if (!code) return NULL;
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
        if (tag_eq(rubraview_bytes_view(code, proven_cstr_len(code)), NAMES[i].code)) return NAMES[i].name;
    }
    return NULL;
}

static bool tag_eq(proven_u8str_view_t a_view, const char *b) {
    const char *a = (const char *)a_view.ptr;
    size_t n = (size_t)a_view.size;
    return proven_u8str_view_eq_ascii_ci(proven_u8str_view_from_bytes(a, n), proven_u8str_view_from_cstr(b));
}

const char *rubraview_lang_pick(const char *tag, const char *const *available, size_t count) {
    if (!tag || !*tag || !available) return NULL;
    size_t len = proven_cstr_len(tag);
    for (size_t i = 0; i < count; i++) if (tag_eq(rubraview_bytes_view(tag, len), available[i])) return available[i];
    size_t lang_len = strcspn(tag, "-_");
    /* Chinese by script, or by region when Windows names none. */
    if (tag_eq(rubraview_bytes_view(tag, lang_len), "zh")) {
        const char *script = NULL;
        if (rubraview_cstr_str(tag, "Hant") || rubraview_cstr_str(tag, "hant") || rubraview_cstr_str(tag, "-TW") || rubraview_cstr_str(tag, "-HK") || rubraview_cstr_str(tag, "-MO") ||
            rubraview_cstr_str(tag, "_TW") || rubraview_cstr_str(tag, "_HK") || rubraview_cstr_str(tag, "_MO")) {
            script = "zh-Hant";
        } else {
            script = "zh-Hans";
        }
        for (size_t i = 0; i < count; i++) if (tag_eq(rubraview_bytes_view(script, proven_cstr_len(script)), available[i])) return available[i];
        return NULL;
    }
    for (size_t i = 0; i < count; i++) if (tag_eq(rubraview_bytes_view(tag, lang_len), available[i])) return available[i];
    return NULL;
}
