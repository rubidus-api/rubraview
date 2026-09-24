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

static int64_t whole_number(u8str_t v) {
    int64_t n = 0;
    size_t i = 0;
    bool negative = false;
    if (v.len > 0 && v.ptr[0] == '-') { negative = true; i = 1; }
    for (; i < v.len && v.ptr[i] >= '0' && v.ptr[i] <= '9'; ++i) n = n * 10 + (v.ptr[i] - '0');
    return negative ? -n : n;
}

static bool starts_with(u8str_t s, const char *prefix) {
    size_t n = strlen(prefix);
    return s.len >= n && memcmp(s.ptr, prefix, n) == 0;
}

static void add_entry(proven_arena_t *arena, rubraview_history_t *history,
                      u8str_t path, int32_t page, int32_t total, int64_t timestamp) {
    if (path.len == 0) return;
    if (!entries_reserve(arena, history, history->count + 1)) return;
    history->entries[history->count++] = (rubraview_history_entry_t){
        .path = path, .page = page, .total = total, .timestamp = timestamp,
    };
}

rubraview_history_t rubraview_history_parse(proven_arena_t *arena, u8str_t text) {
    rubraview_history_t history = {0};
    if (!arena || text.len == 0) return history;

    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, text);

    /* D-13: one `[entry-N]` section per book, its path a quoted string —
       a path cannot be a key in a file that must also be TOML. */
    u8str_t section = { .ptr = "", .len = 0 };
    u8str_t path = { .ptr = "", .len = 0 };
    int32_t page = 0, total = 0;
    int64_t timestamp = 0;
    for (size_t i = 0; i <= doc.count; ++i) {
        bool boundary = i == doc.count || !u8str_eq(doc.entries[i].section, section);
        if (boundary) {
            if (starts_with(section, "entry-")) add_entry(arena, &history, path, page, total, timestamp);
            if (i == doc.count) break;
            section = doc.entries[i].section;
            path = (u8str_t){ .ptr = "", .len = 0 };
            page = 0; total = 0; timestamp = 0;
        }
        const rubraview_ini_entry_t *e = &doc.entries[i];
        if (starts_with(e->section, "entry-")) {
            if (u8str_eq(e->key, U8("path")))       path = e->value;
            else if (u8str_eq(e->key, U8("page")))  page = (int32_t)whole_number(e->value);
            else if (u8str_eq(e->key, U8("total"))) total = (int32_t)whole_number(e->value);
            else if (u8str_eq(e->key, U8("time")))  timestamp = whole_number(e->value);
            continue;
        }

        /* The format before D-13, still read: under [history], the path
           as the key and `page:58, total:192, time:172...` as the value. */
        if (e->section.len != sizeof(HISTORY_SECTION) - 1 ||
            memcmp(e->section.ptr, HISTORY_SECTION, sizeof(HISTORY_SECTION) - 1) != 0) {
            continue;
        }
        if (e->key.len == 0) continue;
        int32_t old_page = 0, old_total = 0;
        int64_t old_time = 0;
        parse_position(e->value, &old_page, &old_total, &old_time);
        add_entry(arena, &history, e->key, old_page, old_total, old_time);
    }

    return history;
}

const rubraview_history_entry_t *rubraview_history_find(const rubraview_history_t *history, u8str_t path) {
    if (!history || !history->entries) return NULL;
    for (size_t i = 0; i < history->count; ++i) {
        if (rubraview_path_same(history->entries[i].path, path)) return &history->entries[i];
    }
    return NULL;
}

void rubraview_history_record(proven_arena_t *arena, rubraview_history_t *history,
                              u8str_t path, int32_t page, int32_t total, int64_t timestamp) {
    if (!arena || !history || path.len == 0) return;

    for (size_t i = 0; i < history->count; ++i) {
        if (!rubraview_path_same(history->entries[i].path, path)) continue;
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

u8str_t rubraview_history_serialize(proven_arena_t *arena, const rubraview_history_t *history) {
    if (!arena || !history) return (u8str_t){ .ptr = "", .len = 0 };

    /* Through the configuration writer, so the file is in the INI and
       TOML subset by construction (D-13). */
    rubraview_ini_doc_t doc = {0};
    for (size_t i = 0; i < history->count; ++i) {
        const rubraview_history_entry_t *e = &history->entries[i];
        char name[32];
        int n = snprintf(name, sizeof(name), "entry-%zu", i + 1);
        if (n <= 0) continue;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)n + 1);
        if (!proven_is_ok(res.err)) break;
        memcpy(res.value.ptr, name, (size_t)n + 1);
        u8str_t section = { .ptr = (const char*)res.value.ptr, .len = (size_t)n };

        rubraview_ini_set_string(arena, &doc, section, U8("path"), e->path);
        rubraview_ini_set_int(arena, &doc, section, U8("page"), e->page);
        rubraview_ini_set_int(arena, &doc, section, U8("total"), e->total);
        rubraview_ini_set_int(arena, &doc, section, U8("time"), e->timestamp);
    }
    return rubraview_ini_serialize(arena, &doc);
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
