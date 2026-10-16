/* Language packs (RV-114, D-148): see rubraview/lang.h. */
#include "rubraview/lang.h"

#include <stdlib.h>
#include <string.h>

static const rubraview_lang_t *g_current;

static uint32_t hash_key(const char *context, size_t context_len, const char *english, size_t english_len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < context_len; i++) { h ^= (uint8_t)context[i]; h *= 16777619u; }
    h ^= 0x1Fu; h *= 16777619u;            /* between the context and the English */
    for (size_t i = 0; i < english_len; i++) { h ^= (uint8_t)english[i]; h *= 16777619u; }
    return h ? h : 1u;                     /* 0 marks an empty slot */
}

/* ---- conversions ---- */

/* The next printf conversion at or after *at, as [start, end); false when none. */
static bool next_conversion(u8str_t s, size_t *at, size_t *start, size_t *end) {
    size_t i = *at;
    while (i < s.len) {
        if (s.ptr[i] != '%') { i++; continue; }
        if (i + 1 < s.len && s.ptr[i + 1] == '%') { i += 2; continue; }
        size_t j = i + 1;
        while (j < s.len && strchr("-+ #0'", s.ptr[j])) j++;
        while (j < s.len && ((s.ptr[j] >= '0' && s.ptr[j] <= '9') || s.ptr[j] == '*')) j++;
        if (j < s.len && s.ptr[j] == '.') {
            j++;
            while (j < s.len && ((s.ptr[j] >= '0' && s.ptr[j] <= '9') || s.ptr[j] == '*')) j++;
        }
        while (j < s.len && strchr("hljztL", s.ptr[j])) j++;
        if (j < s.len) j++;                /* the conversion letter */
        *start = i; *end = j; *at = j;
        return true;
    }
    *at = i;
    return false;
}

bool rubraview_lang_same_conversions(u8str_t english, u8str_t translation) {
    size_t ea = 0, ta = 0, es, ee, ts, te;
    for (;;) {
        bool e = next_conversion(english, &ea, &es, &ee);
        bool t = next_conversion(translation, &ta, &ts, &te);
        if (e != t) return false;
        if (!e) return true;
        if (ee - es != te - ts || memcmp(english.ptr + es, translation.ptr + ts, ee - es) != 0) return false;
    }
}

/* ---- reading ---- */

static char *store_put(rubraview_lang_t *lang, const char *s, size_t n) {
    if (lang->store_used + n + 1 > lang->store_size) return NULL;
    char *dst = lang->store + lang->store_used;
    memcpy(dst, s, n);
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
    uint32_t h = hash_key(ctx, ctx_len, en, en_len);
    size_t mask = lang->slot_count - 1;
    for (size_t i = h & mask;; i = (i + 1) & mask) {
        rubraview_lang_entry_t *e = &lang->slots[i];
        if (e->hash == 0) {
            const char *c = ctx_len ? store_put(lang, ctx, ctx_len) : "";
            const char *k = store_put(lang, en, en_len);
            const char *t = store_put(lang, text, text_len);
            if (!c || !k || !t) return false;
            e->hash = h; e->context = c; e->english = k; e->text = t; e->text_len = text_len;
            lang->count++;
            return true;
        }
        if (e->hash == h && strlen(e->context) == ctx_len && memcmp(e->context, ctx, ctx_len) == 0 &&
            strlen(e->english) == en_len && memcmp(e->english, en, en_len) == 0) {
            const char *t = store_put(lang, text, text_len);   /* a later line wins */
            if (!t) return false;
            e->text = t; e->text_len = text_len;
            return true;
        }
    }
}

void rubraview_lang_free(rubraview_lang_t *lang) {
    if (!lang) return;
    if (g_current == lang) g_current = NULL;
    free(lang->slots);
    free(lang->store);
    memset(lang, 0, sizeof(*lang));
}

bool rubraview_lang_load(rubraview_lang_t *lang, u8str_t text, rubraview_lang_stats_t *stats) {
    rubraview_lang_stats_t local;
    if (!stats) stats = &local;
    memset(stats, 0, sizeof(*stats));
    memset(lang, 0, sizeof(*lang));
    if (text.len > RUBRAVIEW_LANG_MAX_FILE) return false;

    size_t lines = 1;
    for (size_t i = 0; i < text.len; i++) if (text.ptr[i] == '\n') lines++;
    size_t want = lines < RUBRAVIEW_LANG_MAX_ENTRIES ? lines : RUBRAVIEW_LANG_MAX_ENTRIES;
    lang->slot_count = 64;
    while (lang->slot_count < want * 2) lang->slot_count <<= 1;
    lang->slots = (rubraview_lang_entry_t*)calloc(lang->slot_count, sizeof(*lang->slots));
    /* Every string is at most as long as its quoted form, plus a NUL each. */
    lang->store_size = text.len + lines * 3 + 16;
    lang->store = (char*)malloc(lang->store_size);
    if (!lang->slots || !lang->store) { rubraview_lang_free(lang); return false; }

    char *a = (char*)malloc(RUBRAVIEW_LANG_MAX_TEXT), *b = (char*)malloc(RUBRAVIEW_LANG_MAX_TEXT);
    char ctx[64];
    if (!a || !b) { free(a); free(b); rubraview_lang_free(lang); return false; }

    const char *p = text.ptr, *end = text.ptr + text.len;
    if (end - p >= 3 && (uint8_t)p[0] == 0xEF && (uint8_t)p[1] == 0xBB && (uint8_t)p[2] == 0xBF) p += 3;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
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
                    if (ctx_len == 8 && memcmp(ctx, "language", 8) == 0 && b_len < sizeof(lang->code)) {
                        memcpy(lang->code, b, b_len); lang->code[b_len] = '\0';
                    } else if (ctx_len == 4 && memcmp(ctx, "name", 4) == 0 && b_len < sizeof(lang->name)) {
                        memcpy(lang->name, b, b_len); lang->name[b_len] = '\0';
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
        if (!rubraview_lang_same_conversions((u8str_t){ a, a_len }, (u8str_t){ b, b_len })) { stats->refused++; continue; }
        if (lang->count >= RUBRAVIEW_LANG_MAX_ENTRIES) { stats->too_many++; continue; }
        if (!insert(lang, ctx, ctx_len, a, a_len, b, b_len)) { stats->too_many++; continue; }
    }
    free(a); free(b);
    stats->entries = lang->count;
    return true;
}

const rubraview_lang_entry_t *rubraview_lang_find(const rubraview_lang_t *lang, const char *context,
                                                  const char *english, size_t english_len) {
    if (!lang || !lang->slots || lang->count == 0 || !english) return NULL;
    size_t ctx_len = context ? strlen(context) : 0;
    if (!context) context = "";
    uint32_t h = hash_key(context, ctx_len, english, english_len);
    size_t mask = lang->slot_count - 1;
    for (size_t i = h & mask;; i = (i + 1) & mask) {
        const rubraview_lang_entry_t *e = &lang->slots[i];
        if (e->hash == 0) return NULL;
        if (e->hash == h && strcmp(e->context, context) == 0 && strncmp(e->english, english, english_len) == 0 &&
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
    const rubraview_lang_entry_t *e = rubraview_lang_find(g_current, NULL, english, strlen(english));
    return e ? e->text : english;
}

const char *rubraview_tr_ctx(const char *context, const char *english) {
    if (!g_current || !english) return english;
    const rubraview_lang_entry_t *e = rubraview_lang_find(g_current, context, english, strlen(english));
    if (!e && context && *context) e = rubraview_lang_find(g_current, NULL, english, strlen(english));
    return e ? e->text : english;
}

u8str_t rubraview_tr_ctx_u8(const char *context, u8str_t english) {
    if (!g_current || english.len == 0) return english;
    const rubraview_lang_entry_t *e = rubraview_lang_find(g_current, context, english.ptr, english.len);
    if (!e && context && *context) e = rubraview_lang_find(g_current, NULL, english.ptr, english.len);
    return e ? (u8str_t){ .ptr = e->text, .len = e->text_len } : english;
}

u8str_t rubraview_tr_u8(u8str_t english) { return rubraview_tr_ctx_u8(NULL, english); }

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
static size_t utf8_next(u8str_t s, size_t i, uint32_t *cp) {
    uint8_t c = (uint8_t)s.ptr[i];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (n == 0 || i + n > s.len) { *cp = c; return 1; }
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

size_t rubraview_text_columns(u8str_t text) {
    size_t cols = 0;
    for (size_t i = 0; i < text.len;) {
        uint32_t cp;
        i += utf8_next(text, i, &cp);
        cols += cp_columns(cp);
    }
    return cols;
}

size_t rubraview_text_fit(u8str_t text, size_t columns) {
    size_t cols = 0, i = 0;
    while (i < text.len) {
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

static bool tag_eq(const char *a, size_t n, const char *b) {
    if (strlen(b) != n) return false;
    for (size_t i = 0; i < n; i++) if (rubraview_ascii_lower(a[i]) != rubraview_ascii_lower(b[i])) return false;
    return true;
}

const char *rubraview_lang_pick(const char *tag, const char *const *available, size_t count) {
    if (!tag || !*tag || !available) return NULL;
    size_t len = strlen(tag);
    for (size_t i = 0; i < count; i++) if (tag_eq(tag, len, available[i])) return available[i];
    size_t lang_len = strcspn(tag, "-_");
    /* Chinese by script, or by region when Windows names none. */
    if (tag_eq(tag, lang_len, "zh")) {
        const char *script = NULL;
        if (strstr(tag, "Hant") || strstr(tag, "hant") || strstr(tag, "-TW") || strstr(tag, "-HK") || strstr(tag, "-MO") ||
            strstr(tag, "_TW") || strstr(tag, "_HK") || strstr(tag, "_MO")) {
            script = "zh-Hant";
        } else {
            script = "zh-Hans";
        }
        for (size_t i = 0; i < count; i++) if (tag_eq(script, strlen(script), available[i])) return available[i];
        return NULL;
    }
    for (size_t i = 0; i < count; i++) if (tag_eq(tag, lang_len, available[i])) return available[i];
    return NULL;
}
