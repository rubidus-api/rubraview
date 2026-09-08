#include "rubraview/history.h"
#include "rubraview/path.h"
#include <stdio.h>
#include <string.h>

#define HISTORY_SECTION "history"

static bool u8str_eq(u8str_t a, u8str_t b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

static bool entries_reserve(proven_arena_t *arena, rubraview_history_t *history, size_t min_capacity) {
    if (history->capacity >= min_capacity) return true;
    size_t new_cap = history->capacity == 0 ? 16 : history->capacity * 2;
    if (new_cap < min_capacity) new_cap = min_capacity;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap * sizeof(rubraview_history_entry_t));
    if (!proven_is_ok(res.err)) return false;

    rubraview_history_entry_t *data = (rubraview_history_entry_t*)(void*)res.value.ptr;
    if (history->entries && history->count > 0) {
        memcpy(data, history->entries, history->count * sizeof(rubraview_history_entry_t));
    }
    history->entries = data;
    history->capacity = new_cap;
    return true;
}

/* Reads one `page:58, total:192, time:172...` value. A field that is
   missing or malformed leaves its default rather than failing the line,
   since a half-written history should still restore what it can. */
static void parse_position(u8str_t value, int32_t *out_page, int32_t *out_total, int64_t *out_time) {
    *out_page = 0;
    *out_total = 0;
    *out_time = 0;

    size_t i = 0;
    while (i < value.len) {
        while (i < value.len && (value.ptr[i] == ' ' || value.ptr[i] == ',')) i++;
        size_t key_start = i;
        while (i < value.len && value.ptr[i] != ':' && value.ptr[i] != ',') i++;
        if (i >= value.len || value.ptr[i] != ':') break;

        size_t key_len = i - key_start;
        i++; /* skip ':' */

        bool negative = false;
        if (i < value.len && value.ptr[i] == '-') { negative = true; i++; }

        int64_t number = 0;
        bool any_digit = false;
        while (i < value.len && value.ptr[i] >= '0' && value.ptr[i] <= '9') {
            number = number * 10 + (value.ptr[i] - '0');
            any_digit = true;
            i++;
        }
        if (!any_digit) continue;
        if (negative) number = -number;

        if (key_len == 4 && memcmp(value.ptr + key_start, "page", 4) == 0) {
            *out_page = (int32_t)number;
        } else if (key_len == 5 && memcmp(value.ptr + key_start, "total", 5) == 0) {
            *out_total = (int32_t)number;
        } else if (key_len == 4 && memcmp(value.ptr + key_start, "time", 4) == 0) {
            *out_time = number;
        }
    }
}

rubraview_history_t rubraview_history_parse(proven_arena_t *arena, u8str_t text) {
    rubraview_history_t history = {0};
    if (!arena || text.len == 0) return history;

    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, text);
    for (size_t i = 0; i < doc.count; ++i) {
        /* §3.17.1 puts the positions under [history]; a stray key
           elsewhere in the file is not one of ours. */
        if (doc.entries[i].section.len != sizeof(HISTORY_SECTION) - 1 ||
            memcmp(doc.entries[i].section.ptr, HISTORY_SECTION, sizeof(HISTORY_SECTION) - 1) != 0) {
            continue;
        }
        if (doc.entries[i].key.len == 0) continue;

        int32_t page = 0, total = 0;
        int64_t timestamp = 0;
        parse_position(doc.entries[i].value, &page, &total, &timestamp);

        if (!entries_reserve(arena, &history, history.count + 1)) break;
        history.entries[history.count++] = (rubraview_history_entry_t){
            .path = doc.entries[i].key,
            .page = page,
            .total = total,
            .timestamp = timestamp,
        };
    }

    return history;
}

const rubraview_history_entry_t *rubraview_history_find(const rubraview_history_t *history, u8str_t path) {
    if (!history || !history->entries) return NULL;
    for (size_t i = 0; i < history->count; ++i) {
        if (u8str_eq(history->entries[i].path, path)) return &history->entries[i];
    }
    return NULL;
}

void rubraview_history_record(proven_arena_t *arena, rubraview_history_t *history,
                              u8str_t path, int32_t page, int32_t total, int64_t timestamp) {
    if (!arena || !history || path.len == 0) return;

    for (size_t i = 0; i < history->count; ++i) {
        if (!u8str_eq(history->entries[i].path, path)) continue;
        history->entries[i].page = page;
        history->entries[i].total = total;
        history->entries[i].timestamp = timestamp;
        return;
    }

    if (!entries_reserve(arena, history, history->count + 1)) return;
    history->entries[history->count++] = (rubraview_history_entry_t){
        .path = path, .page = page, .total = total, .timestamp = timestamp,
    };
}

void rubraview_history_prune(rubraview_history_t *history, size_t max_entries) {
    if (!history || !history->entries || history->count <= max_entries) return;

    /* Repeatedly drop the oldest by timestamp. The history is small
       (hundreds at most), so a simple selection keeps this obvious. */
    while (history->count > max_entries) {
        size_t oldest = 0;
        for (size_t i = 1; i < history->count; ++i) {
            if (history->entries[i].timestamp < history->entries[oldest].timestamp) oldest = i;
        }
        for (size_t i = oldest; i + 1 < history->count; ++i) {
            history->entries[i] = history->entries[i + 1];
        }
        history->count--;
    }
}

typedef struct byte_buf {
    uint8_t *data;
    size_t len, cap;
} byte_buf_t;

static bool byte_buf_append(proven_arena_t *arena, byte_buf_t *b, const char *s, size_t n) {
    if (n == 0) return true;
    if (b->len + n > b->cap) {
        size_t new_cap = b->cap == 0 ? 512 : b->cap * 2;
        while (new_cap < b->len + n) new_cap *= 2;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap);
        if (!proven_is_ok(res.err)) return false;
        if (b->data && b->len > 0) memcpy(res.value.ptr, b->data, b->len);
        b->data = res.value.ptr;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    return true;
}

u8str_t rubraview_history_serialize(proven_arena_t *arena, const rubraview_history_t *history) {
    if (!arena || !history) return (u8str_t){ .ptr = "", .len = 0 };

    byte_buf_t buf = {0};
    byte_buf_append(arena, &buf, "[" HISTORY_SECTION "]\n", sizeof("[" HISTORY_SECTION "]\n") - 1);

    for (size_t i = 0; i < history->count; ++i) {
        const rubraview_history_entry_t *e = &history->entries[i];
        byte_buf_append(arena, &buf, e->path.ptr, e->path.len);

        char tail[96];
        int written = snprintf(tail, sizeof(tail), " = page:%d, total:%d, time:%lld\n",
                               e->page, e->total, (long long)e->timestamp);
        if (written > 0) byte_buf_append(arena, &buf, tail, (size_t)written);
    }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, buf.len + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    if (buf.len > 0) memcpy(res.value.ptr, buf.data, buf.len);
    res.value.ptr[buf.len] = '\0';

    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = buf.len };
}

bool rubraview_history_should_offer_resume(const rubraview_history_entry_t *entry) {
    if (!entry) return false;
    if (entry->page <= 0) return false;                     /* already at the start */
    if (entry->total > 0 && entry->page >= entry->total - 1) return false; /* finished it */
    return true;
}

rubraview_config_mode_t rubraview_config_mode_for(bool portable_probe_exists) {
    return portable_probe_exists ? RUBRAVIEW_CONFIG_PORTABLE : RUBRAVIEW_CONFIG_APPDATA;
}

u8str_t rubraview_config_path(proven_arena_t *arena,
                              rubraview_config_mode_t mode,
                              u8str_t executable_dir,
                              u8str_t appdata_dir,
                              u8str_t file_name) {
    if (!arena || file_name.len == 0) return (u8str_t){ .ptr = "", .len = 0 };

    if (mode == RUBRAVIEW_CONFIG_PORTABLE) {
        return rubraview_path_join(arena, executable_dir, file_name);
    }

    /* §3.17.2: the standard location is a rubraview folder inside the
       per-user application data directory. */
    u8str_t app_folder = rubraview_path_join(arena, appdata_dir, U8("rubraview"));
    return rubraview_path_join(arena, app_folder, file_name);
}
