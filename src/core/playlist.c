#include "rubraview/playlist.h"
#include "rubraview/number.h"
#include "rubraview/path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u8str_t trim(const char *ptr, size_t len) {
    size_t start = 0, end = len;
    while (start < end && (ptr[start] == ' ' || ptr[start] == '\t')) start++;
    while (end > start && (ptr[end - 1] == ' ' || ptr[end - 1] == '\t')) end--;
    return (u8str_t){ .ptr = ptr + start, .len = end - start };
}

static double parse_double_or(u8str_t s, double default_value) {
    double value = 0.0;
    return rubraview_parse_double(s, &value) ? value : default_value;
}

typedef struct entry_buf {
    rubraview_playlist_entry_t *data;
    size_t count, capacity;
} entry_buf_t;

static bool entry_buf_push(proven_arena_t *arena, entry_buf_t *b, rubraview_playlist_entry_t entry) {
    if (b->count >= b->capacity) {
        size_t new_cap = b->capacity == 0 ? 16 : b->capacity * 2;
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(rubraview_playlist_entry_t));
        if (!proven_is_ok(res.err)) return false;
        rubraview_playlist_entry_t *new_data = (rubraview_playlist_entry_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(new_data, b->data, b->count * sizeof(rubraview_playlist_entry_t));
        b->data = new_data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = entry;
    return true;
}

/* Splits `text` into trimmed, CRLF-safe lines and calls `on_line` for
   each non-empty one. Shared by both parsers' line-walking. */
static void for_each_line(u8str_t text, void (*on_line)(u8str_t line, void *ctx), void *ctx) {
    size_t line_start = 0;
    for (size_t i = 0; i <= text.len; ++i) {
        if (i == text.len || text.ptr[i] == '\n') {
            size_t line_end = i;
            if (line_end > line_start && text.ptr[line_end - 1] == '\r') line_end--;
            u8str_t line = trim(text.ptr + line_start, line_end - line_start);
            line_start = i + 1;
            if (line.len > 0) on_line(line, ctx);
        }
    }
}

typedef struct parse_ctx {
    proven_arena_t *arena;
    entry_buf_t buf;
} parse_ctx_t;

static void rvlist_line_cb(u8str_t line, void *raw_ctx) {
    parse_ctx_t *ctx = (parse_ctx_t*)raw_ctx;
    if (line.ptr[0] == ';') return;
    entry_buf_push(ctx->arena, &ctx->buf, (rubraview_playlist_entry_t){
        .path = line, .duration_seconds = -1.0, .title = { .ptr = "", .len = 0 },
    });
}

rubraview_playlist_t rubraview_playlist_parse_rvlist(proven_arena_t *arena, u8str_t text) {
    rubraview_playlist_t pl = {0};
    if (!arena) return pl;

    parse_ctx_t ctx = { .arena = arena, .buf = {0} };
    for_each_line(text, rvlist_line_cb, &ctx);

    pl.entries = ctx.buf.data;
    pl.count = ctx.buf.count;
    return pl;
}

typedef struct m3u8_ctx {
    proven_arena_t *arena;
    entry_buf_t buf;
    bool has_pending_extinf;
    double pending_duration;
    u8str_t pending_title;
} m3u8_ctx_t;

static void m3u8_line_cb(u8str_t line, void *raw_ctx) {
    m3u8_ctx_t *ctx = (m3u8_ctx_t*)raw_ctx;

    static const char extinf_prefix[] = "#EXTINF:";
    size_t prefix_len = sizeof(extinf_prefix) - 1;

    if (line.len >= prefix_len && memcmp(line.ptr, extinf_prefix, prefix_len) == 0) {
        u8str_t rest = { .ptr = line.ptr + prefix_len, .len = line.len - prefix_len };
        size_t comma = 0;
        while (comma < rest.len && rest.ptr[comma] != ',') comma++;

        ctx->pending_duration = parse_double_or((u8str_t){ .ptr = rest.ptr, .len = comma }, -1.0);
        ctx->pending_title = (comma < rest.len)
            ? trim(rest.ptr + comma + 1, rest.len - comma - 1)
            : (u8str_t){ .ptr = "", .len = 0 };
        ctx->has_pending_extinf = true;
        return;
    }

    if (line.ptr[0] == '#') return; /* #EXTM3U or any other tag: ignored */

    entry_buf_push(ctx->arena, &ctx->buf, (rubraview_playlist_entry_t){
        .path = line,
        .duration_seconds = ctx->has_pending_extinf ? ctx->pending_duration : -1.0,
        .title = ctx->has_pending_extinf ? ctx->pending_title : (u8str_t){ .ptr = "", .len = 0 },
    });
    ctx->has_pending_extinf = false;
}

rubraview_playlist_t rubraview_playlist_parse_m3u8(proven_arena_t *arena, u8str_t text) {
    rubraview_playlist_t pl = {0};
    if (!arena) return pl;

    m3u8_ctx_t ctx = { .arena = arena, .buf = {0}, .has_pending_extinf = false, .pending_duration = -1.0, .pending_title = { .ptr = "", .len = 0 } };
    for_each_line(text, m3u8_line_cb, &ctx);

    pl.entries = ctx.buf.data;
    pl.count = ctx.buf.count;
    return pl;
}

typedef struct byte_buf {
    uint8_t *data;
    size_t len, cap;
} byte_buf_t;

static bool byte_buf_reserve(proven_arena_t *arena, byte_buf_t *b, size_t extra) {
    if (b->len + extra <= b->cap) return true;
    size_t new_cap = b->cap == 0 ? 256 : b->cap * 2;
    while (new_cap < b->len + extra) new_cap *= 2;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap);
    if (!proven_is_ok(res.err)) return false;
    if (b->data && b->len > 0) memcpy(res.value.ptr, b->data, b->len);
    b->data = res.value.ptr;
    b->cap = new_cap;
    return true;
}

static bool byte_buf_append(proven_arena_t *arena, byte_buf_t *b, const char *s, size_t n) {
    if (n == 0) return true;
    if (!byte_buf_reserve(arena, b, n)) return false;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    return true;
}

static bool byte_buf_append_str(proven_arena_t *arena, byte_buf_t *b, u8str_t s) {
    return byte_buf_append(arena, b, s.ptr, s.len);
}

static u8str_t byte_buf_finish(proven_arena_t *arena, byte_buf_t *b) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, b->len + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    if (b->len > 0) memcpy(res.value.ptr, b->data, b->len);
    res.value.ptr[b->len] = '\0';
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = b->len };
}

u8str_t rubraview_playlist_serialize_rvlist(proven_arena_t *arena, const rubraview_playlist_t *pl) {
    if (!arena || !pl) return (u8str_t){ .ptr = "", .len = 0 };

    byte_buf_t buf = {0};
    for (size_t i = 0; i < pl->count; ++i) {
        byte_buf_append_str(arena, &buf, pl->entries[i].path);
        byte_buf_append(arena, &buf, "\n", 1);
    }
    return byte_buf_finish(arena, &buf);
}

u8str_t rubraview_playlist_serialize_m3u8(proven_arena_t *arena, const rubraview_playlist_t *pl) {
    if (!arena || !pl) return (u8str_t){ .ptr = "", .len = 0 };

    byte_buf_t buf = {0};
    byte_buf_append(arena, &buf, "#EXTM3U\n", 8);

    for (size_t i = 0; i < pl->count; ++i) {
        const rubraview_playlist_entry_t *e = &pl->entries[i];
        if (e->title.len > 0 || e->duration_seconds >= 0.0) {
            byte_buf_append(arena, &buf, "#EXTINF:", 8);
            char num[32];
            int n = snprintf(num, sizeof(num), "%g", e->duration_seconds);
            if (n > 0) byte_buf_append(arena, &buf, num, (size_t)n);
            byte_buf_append(arena, &buf, ",", 1);
            byte_buf_append_str(arena, &buf, e->title);
            byte_buf_append(arena, &buf, "\n", 1);
        }
        byte_buf_append_str(arena, &buf, e->path);
        byte_buf_append(arena, &buf, "\n", 1);
    }

    return byte_buf_finish(arena, &buf);
}

/* ---- RV-157: .pls and .asx, read only ---- */

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* `s` starts with `word`, ASCII case aside. */
static bool starts_nocase(const char *s, size_t len, const char *word) {
    size_t n = strlen(word);
    if (len < n) return false;
    for (size_t i = 0; i < n; ++i) if (lower(s[i]) != word[i]) return false;
    return true;
}

typedef struct pls_item {
    uint32_t number;
    uint32_t order;      /* the line's place, so a key given twice keeps its last */
    uint8_t  key;        /* 0 File, 1 Title, 2 Length */
    u8str_t  value;
} pls_item_t;

static int pls_item_cmp(const void *a, const void *b) {
    const pls_item_t *x = (const pls_item_t*)a, *y = (const pls_item_t*)b;
    if (x->number != y->number) return x->number < y->number ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order ? 1 : 0;
}

typedef struct pls_ctx {
    proven_arena_t *arena;
    pls_item_t *items;
    size_t count, capacity;
    bool failed;
} pls_ctx_t;

static void pls_line_cb(u8str_t line, void *raw_ctx) {
    pls_ctx_t *ctx = (pls_ctx_t*)raw_ctx;
    if (ctx->failed) return;
    static const char *const KEYS[] = { "file", "title", "length" };
    uint8_t key = 0;
    size_t at = 0;
    for (; key < 3; ++key) {
        if (starts_nocase(line.ptr, line.len, KEYS[key])) { at = strlen(KEYS[key]); break; }
    }
    if (key == 3) return;   /* [playlist], NumberOfEntries, Version, comments */
    uint64_t number = 0;
    size_t digits = 0;
    while (at < line.len && line.ptr[at] >= '0' && line.ptr[at] <= '9' && digits < 9) {
        number = number * 10u + (uint64_t)(line.ptr[at] - '0');
        at++; digits++;
    }
    if (digits == 0 || at >= line.len || line.ptr[at] != '=') return;
    if (ctx->count >= ctx->capacity) {
        size_t cap = ctx->capacity ? ctx->capacity * 2 : 32;
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(ctx->arena, cap, sizeof(pls_item_t));
        if (!proven_is_ok(res.err)) { ctx->failed = true; return; }
        if (ctx->count) memcpy(res.value.ptr, ctx->items, ctx->count * sizeof(pls_item_t));
        ctx->items = (pls_item_t*)(void*)res.value.ptr;
        ctx->capacity = cap;
    }
    ctx->items[ctx->count] = (pls_item_t){
        .number = (uint32_t)number, .order = (uint32_t)ctx->count, .key = key,
        .value = trim(line.ptr + at + 1, line.len - at - 1),
    };
    ctx->count++;
}

rubraview_playlist_t rubraview_playlist_parse_pls(proven_arena_t *arena, u8str_t text) {
    rubraview_playlist_t pl = {0};
    if (!arena) return pl;
    pls_ctx_t ctx = { .arena = arena };
    for_each_line(text, pls_line_cb, &ctx);
    if (ctx.count == 0) return pl;
    qsort(ctx.items, ctx.count, sizeof(pls_item_t), pls_item_cmp);

    entry_buf_t buf = {0};
    for (size_t i = 0; i < ctx.count;) {
        uint32_t number = ctx.items[i].number;
        rubraview_playlist_entry_t e = { .path = { .ptr = "", .len = 0 }, .duration_seconds = -1.0, .title = { .ptr = "", .len = 0 } };
        for (; i < ctx.count && ctx.items[i].number == number; ++i) {
            const pls_item_t *it = &ctx.items[i];
            if (it->key == 0) e.path = it->value;
            else if (it->key == 1) e.title = it->value;
            else e.duration_seconds = parse_double_or(it->value, -1.0);
        }
        if (e.duration_seconds < 0.0) e.duration_seconds = -1.0;
        if (e.path.len > 0 && !entry_buf_push(arena, &buf, e)) break;
    }
    pl.entries = buf.data;
    pl.count = buf.count;
    return pl;
}

/* The text between two tags or in an attribute, its entities decoded and its
   spaces trimmed: into the arena when it holds one, else a view. */
static u8str_t xml_text(proven_arena_t *arena, const char *ptr, size_t len) {
    u8str_t t = trim(ptr, len);
    while (t.len > 0 && (t.ptr[0] == '\r' || t.ptr[0] == '\n')) { t.ptr++; t.len--; t = trim(t.ptr, t.len); }
    while (t.len > 0 && (t.ptr[t.len - 1] == '\r' || t.ptr[t.len - 1] == '\n')) { t.len--; t = trim(t.ptr, t.len); }
    if (!memchr(t.ptr, '&', t.len)) return t;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, t.len + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    char *out = (char*)res.value.ptr;
    size_t n = 0;
    static const struct { const char *name; char c; } ENT[] = {
        { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' },
    };
    for (size_t i = 0; i < t.len;) {
        bool done = false;
        if (t.ptr[i] == '&') {
            for (size_t e = 0; e < sizeof(ENT) / sizeof(ENT[0]) && !done; ++e) {
                size_t el = strlen(ENT[e].name);
                if (t.len - i >= el && memcmp(t.ptr + i, ENT[e].name, el) == 0) { out[n++] = ENT[e].c; i += el; done = true; }
            }
        }
        if (!done) out[n++] = t.ptr[i++];
    }
    out[n] = '\0';
    return (u8str_t){ .ptr = out, .len = n };
}

/* The value of attribute `name` within a tag's text, or empty. */
static u8str_t xml_attr(proven_arena_t *arena, const char *tag, size_t len, const char *name) {
    size_t nl = strlen(name);
    for (size_t i = 0; i + nl < len; ++i) {
        bool before = i == 0 || tag[i - 1] == ' ' || tag[i - 1] == '\t' || tag[i - 1] == '\r' || tag[i - 1] == '\n';
        if (!before || !starts_nocase(tag + i, len - i, name)) continue;
        size_t at = i + nl;
        while (at < len && (tag[at] == ' ' || tag[at] == '\t')) at++;
        if (at >= len || tag[at] != '=') continue;
        at++;
        while (at < len && (tag[at] == ' ' || tag[at] == '\t')) at++;
        if (at >= len) break;
        char quote = tag[at];
        size_t start, end;
        if (quote == '"' || quote == '\'') {
            start = ++at;
            while (at < len && tag[at] != quote) at++;
            end = at;
        } else {
            start = at;
            while (at < len && tag[at] != ' ' && tag[at] != '\t' && tag[at] != '/') at++;
            end = at;
        }
        return xml_text(arena, tag + start, end - start);
    }
    return (u8str_t){ .ptr = "", .len = 0 };
}

rubraview_playlist_t rubraview_playlist_parse_asx(proven_arena_t *arena, u8str_t text) {
    rubraview_playlist_t pl = {0};
    if (!arena || !text.ptr) return pl;
    entry_buf_t buf = {0};
    bool in_entry = false, have_ref = false;
    u8str_t title = { .ptr = "", .len = 0 }, ref = { .ptr = "", .len = 0 };
    const char *s = text.ptr;
    size_t len = text.len;
    for (size_t i = 0; i < len;) {
        if (s[i] != '<') { i++; continue; }
        if (len - i >= 4 && memcmp(s + i, "<!--", 4) == 0) {
            size_t j = i + 4;
            while (j + 2 < len && memcmp(s + j, "-->", 3) != 0) j++;
            i = j + 3;
            continue;
        }
        size_t end = i + 1;
        while (end < len && s[end] != '>') end++;
        if (end >= len) break;
        const char *tag = s + i + 1;
        size_t tl = end - i - 1;
        bool closing = tl > 0 && tag[0] == '/';
        const char *word = closing ? tag + 1 : tag;
        size_t wl = closing ? tl - 1 : tl;
        size_t name_len = 0;
        while (name_len < wl && word[name_len] != ' ' && word[name_len] != '\t' && word[name_len] != '/' &&
               word[name_len] != '\r' && word[name_len] != '\n') name_len++;
        bool is_entry = name_len == 5 && starts_nocase(word, wl, "entry");
        bool is_ref = name_len == 3 && starts_nocase(word, wl, "ref");
        bool is_title = name_len == 5 && starts_nocase(word, wl, "title");
        if (is_entry && !closing) {
            in_entry = true; have_ref = false;
            title = ref = (u8str_t){ .ptr = "", .len = 0 };
        } else if (is_entry && closing) {
            if (in_entry && have_ref && !entry_buf_push(arena, &buf, (rubraview_playlist_entry_t){ .path = ref, .duration_seconds = -1.0, .title = title })) break;
            in_entry = false; have_ref = false;
        } else if (is_ref && !closing && !have_ref) {
            u8str_t href = xml_attr(arena, word + name_len, wl - name_len, "href");
            if (href.len > 0) {
                if (in_entry) { ref = href; have_ref = true; }
                else if (!entry_buf_push(arena, &buf, (rubraview_playlist_entry_t){ .path = href, .duration_seconds = -1.0, .title = { .ptr = "", .len = 0 } })) break;
            }
        } else if (is_title && !closing && in_entry) {
            size_t from = end + 1, to = from;
            while (to < len && s[to] != '<') to++;
            title = xml_text(arena, s + from, to - from);
            i = to;
            continue;
        }
        i = end + 1;
    }
    pl.entries = buf.data;
    pl.count = buf.count;
    return pl;
}

rubraview_playlist_t rubraview_playlist_parse_named(proven_arena_t *arena, u8str_t name, u8str_t text) {
    if (text.len >= 3 && memcmp(text.ptr, "\xEF\xBB\xBF", 3) == 0) { text.ptr += 3; text.len -= 3; }
    if (rubraview_path_has_ext(name, ".rvlist")) return rubraview_playlist_parse_rvlist(arena, text);
    if (rubraview_path_has_ext(name, ".pls")) return rubraview_playlist_parse_pls(arena, text);
    if (rubraview_path_has_ext(name, ".asx") || rubraview_path_has_ext(name, ".wax") || rubraview_path_has_ext(name, ".wvx"))
        return rubraview_playlist_parse_asx(arena, text);
    size_t at = 0;
    while (at < text.len && (text.ptr[at] == ' ' || text.ptr[at] == '\t' || text.ptr[at] == '\r' || text.ptr[at] == '\n')) at++;
    if (starts_nocase(text.ptr + at, text.len - at, "[playlist]")) return rubraview_playlist_parse_pls(arena, text);
    if (starts_nocase(text.ptr + at, text.len - at, "<asx")) return rubraview_playlist_parse_asx(arena, text);
    return rubraview_playlist_parse_m3u8(arena, text);
}
