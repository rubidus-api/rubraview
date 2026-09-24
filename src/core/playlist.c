#include "rubraview/playlist.h"
#include "rubraview/number.h"
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
