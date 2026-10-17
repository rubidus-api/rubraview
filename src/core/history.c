#include "rubraview/history.h"
#include "rubraview/number.h"
#include "rubraview/path.h"
#include <stdio.h>
#include <string.h>

#define HISTORY_SECTION "history"

static bool entries_reserve(proven_arena_t *arena, rubraview_history_t *history, size_t min_capacity) {
    if (history->capacity >= min_capacity) return true;
    size_t new_cap = history->capacity == 0 ? 16 : history->capacity * 2;
    if (new_cap < min_capacity) new_cap = min_capacity;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(rubraview_history_entry_t));
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
            number = rubraview_digit_append(number, value.ptr[i]);
            any_digit = true;
            i++;
        }
        if (!any_digit) continue;
        if (negative) number = -number;

        if (rubraview_u8_eq_lit((u8str_t){ .ptr = value.ptr + key_start, .len = key_len }, "page")) {
            *out_page = (int32_t)number;
        } else if (rubraview_u8_eq_lit((u8str_t){ .ptr = value.ptr + key_start, .len = key_len }, "total")) {
            *out_total = (int32_t)number;
        } else if (rubraview_u8_eq_lit((u8str_t){ .ptr = value.ptr + key_start, .len = key_len }, "time")) {
            *out_time = number;
        }
    }
}

static int64_t whole_number(u8str_t v) {
    int64_t n = 0;
    size_t i = 0;
    bool negative = false;
    if (v.len > 0 && v.ptr[0] == '-') { negative = true; i = 1; }
    for (; i < v.len && v.ptr[i] >= '0' && v.ptr[i] <= '9'; ++i) n = rubraview_digit_append(n, v.ptr[i]);
    return negative ? -n : n;
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
        bool boundary = i == doc.count || !rubraview_u8_eq(doc.entries[i].section, section);
        if (boundary) {
            if (rubraview_u8_starts_with(section, "entry-")) add_entry(arena, &history, path, page, total, timestamp);
            if (i == doc.count) break;
            section = doc.entries[i].section;
            path = (u8str_t){ .ptr = "", .len = 0 };
            page = 0; total = 0; timestamp = 0;
        }
        const rubraview_ini_entry_t *e = &doc.entries[i];
        if (rubraview_u8_starts_with(e->section, "entry-")) {
            if (rubraview_u8_eq(e->key, U8("path")))       path = e->value;
            else if (rubraview_u8_eq(e->key, U8("page")))  page = (int32_t)whole_number(e->value);
            else if (rubraview_u8_eq(e->key, U8("total"))) total = (int32_t)whole_number(e->value);
            else if (rubraview_u8_eq(e->key, U8("time")))  timestamp = whole_number(e->value);
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

void rubraview_history_merge(proven_arena_t *arena, rubraview_history_t *into, const rubraview_history_t *from) {
    if (!arena || !into || !from) return;
    for (size_t i = 0; i < from->count; ++i) {
        const rubraview_history_entry_t *e = &from->entries[i];
        const rubraview_history_entry_t *have = rubraview_history_find(into, e->path);
        if (have && have->timestamp >= e->timestamp) continue;
        rubraview_history_record(arena, into, e->path, e->page, e->total, e->timestamp);
    }
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

/* ---- the files opened lately (D-104) ---- */

static void drop_at(rubraview_history_t *history, size_t at) {
    for (size_t i = at; i + 1 < history->count; ++i) history->entries[i] = history->entries[i + 1];
    history->count--;
}

/* The removal of `path` that counts, or 0 (none). */
static int64_t removed_at(const rubraview_recent_t *recent, u8str_t path) {
    int64_t at = 0;
    for (size_t k = 0; k < recent->removed.count; ++k) {
        if (rubraview_path_same(recent->removed.entries[k].path, path) && recent->removed.entries[k].timestamp > at)
            at = recent->removed.entries[k].timestamp;
    }
    return at;
}

static void recent_drop_cleared(rubraview_recent_t *recent) {
    for (size_t i = recent->list.count; i-- > 0;) {
        const rubraview_history_entry_t *e = &recent->list.entries[i];
        if (e->timestamp <= recent->cleared || e->timestamp <= removed_at(recent, e->path)) drop_at(&recent->list, i);
    }
    /* a removal from before the last clearing has nothing left to keep away */
    for (size_t k = recent->removed.count; k-- > 0;) {
        if (recent->removed.entries[k].timestamp <= recent->cleared) drop_at(&recent->removed, k);
    }
}

rubraview_recent_t rubraview_recent_parse(proven_arena_t *arena, u8str_t text) {
    rubraview_recent_t recent = { .list = rubraview_history_parse(arena, text), .cleared = 0 };
    if (!arena || text.len == 0) return recent;
    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, text);
    for (size_t i = 0; i < doc.count; ++i) {
        const rubraview_ini_entry_t *e = &doc.entries[i];
        if (rubraview_u8_eq_lit(e->section, "cleared") && rubraview_u8_eq_lit(e->key, "time")) recent.cleared = whole_number(e->value);
    }
    /* [removed-N]: path, time */
    u8str_t section = { .ptr = "", .len = 0 }, path = { .ptr = "", .len = 0 };
    int64_t when = 0;
    for (size_t i = 0; i <= doc.count; ++i) {
        bool boundary = i == doc.count || !rubraview_u8_eq(doc.entries[i].section, section);
        if (boundary) {
            if (rubraview_u8_starts_with(section, "removed-") && path.len > 0) rubraview_history_record(arena, &recent.removed, path, 0, 0, when);
            if (i == doc.count) break;
            section = doc.entries[i].section;
            path = (u8str_t){ .ptr = "", .len = 0 };
            when = 0;
        }
        const rubraview_ini_entry_t *e = &doc.entries[i];
        if (rubraview_u8_eq_lit(e->key, "path")) path = e->value;
        else if (rubraview_u8_eq_lit(e->key, "time")) when = whole_number(e->value);
    }
    recent_drop_cleared(&recent);
    return recent;
}

u8str_t rubraview_recent_serialize(proven_arena_t *arena, const rubraview_recent_t *recent) {
    if (!arena || !recent) return (u8str_t){ .ptr = "", .len = 0 };
    rubraview_ini_doc_t doc = {0};
    rubraview_ini_set_int(arena, &doc, U8("cleared"), U8("time"), recent->cleared);
    for (size_t i = 0; i < recent->list.count; ++i) {
        char name[32];
        int n = snprintf(name, sizeof(name), "entry-%zu", i + 1);
        if (n <= 0) continue;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)n + 1);
        if (!proven_is_ok(res.err)) break;
        memcpy(res.value.ptr, name, (size_t)n + 1);
        u8str_t section = { .ptr = (const char*)res.value.ptr, .len = (size_t)n };
        rubraview_ini_set_string(arena, &doc, section, U8("path"), recent->list.entries[i].path);
        rubraview_ini_set_int(arena, &doc, section, U8("time"), recent->list.entries[i].timestamp);
    }
    for (size_t i = 0; i < recent->removed.count; ++i) {
        char name[32];
        int n = snprintf(name, sizeof(name), "removed-%zu", i + 1);
        if (n <= 0) continue;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)n + 1);
        if (!proven_is_ok(res.err)) break;
        memcpy(res.value.ptr, name, (size_t)n + 1);
        u8str_t section = { .ptr = (const char*)res.value.ptr, .len = (size_t)n };
        rubraview_ini_set_string(arena, &doc, section, U8("path"), recent->removed.entries[i].path);
        rubraview_ini_set_int(arena, &doc, section, U8("time"), recent->removed.entries[i].timestamp);
    }
    return rubraview_ini_serialize(arena, &doc);
}

void rubraview_recent_remove(proven_arena_t *arena, rubraview_recent_t *recent, u8str_t path, int64_t timestamp) {
    if (!arena || !recent || path.len == 0) return;
    /* never earlier than the entry it removes, so it counts */
    for (size_t i = 0; i < recent->list.count; ++i) {
        if (rubraview_path_same(recent->list.entries[i].path, path) && recent->list.entries[i].timestamp >= timestamp)
            timestamp = recent->list.entries[i].timestamp;
    }
    rubraview_history_record(arena, &recent->removed, path, 0, 0, timestamp);
    rubraview_history_prune(&recent->removed, 500);
    recent_drop_cleared(recent);
}

void rubraview_recent_add(proven_arena_t *arena, rubraview_recent_t *recent, u8str_t path,
                          int64_t timestamp, size_t max_entries) {
    if (!arena || !recent || path.len == 0) return;
    /* Opened in the very second the list was emptied, it is still new. */
    if (timestamp <= recent->cleared) timestamp = recent->cleared + 1;
    int64_t gone = removed_at(recent, path);   /* opened again after it was taken off: new again */
    if (timestamp <= gone) timestamp = gone + 1;
    rubraview_history_record(arena, &recent->list, path, 0, 0, timestamp);
    rubraview_history_prune(&recent->list, max_entries);
}

void rubraview_recent_clear(rubraview_recent_t *recent, int64_t timestamp) {
    if (!recent) return;
    if (timestamp > recent->cleared) recent->cleared = timestamp;
    recent->list.count = 0;
    recent->removed.count = 0;   /* the clearing keeps them all away now */
}

void rubraview_recent_merge(proven_arena_t *arena, rubraview_recent_t *into, const rubraview_recent_t *from,
                            size_t max_entries) {
    if (!arena || !into || !from) return;
    if (from->cleared > into->cleared) into->cleared = from->cleared;
    rubraview_history_merge(arena, &into->list, &from->list);
    rubraview_history_merge(arena, &into->removed, &from->removed);   /* the newer removal of a path counts */
    rubraview_history_prune(&into->removed, 500);
    recent_drop_cleared(into);
    rubraview_history_prune(&into->list, max_entries);
}

size_t rubraview_recent_newest(const rubraview_recent_t *recent, size_t *out_indices, size_t capacity) {
    if (!recent || !out_indices) return 0;
    size_t n = 0;
    for (; n < capacity; ++n) {
        size_t best = recent->list.count;
        for (size_t i = 0; i < recent->list.count; ++i) {
            bool taken = false;
            for (size_t k = 0; k < n && !taken; ++k) taken = out_indices[k] == i;
            if (taken) continue;
            if (best == recent->list.count ||
                recent->list.entries[i].timestamp > recent->list.entries[best].timestamp) best = i;
        }
        if (best == recent->list.count) break;
        out_indices[n] = best;
    }
    return n;
}

bool rubraview_path_is_within(u8str_t path, u8str_t folder) {
    while (folder.len > 0 && rubraview_path_is_sep(folder.ptr[folder.len - 1])) folder.len--;
    if (folder.len == 0 || path.len < folder.len) return false;
    if (!rubraview_path_same((u8str_t){ .ptr = path.ptr, .len = folder.len }, folder)) return false;
    return path.len == folder.len || rubraview_path_is_sep(path.ptr[folder.len]);
}

bool rubraview_recent_is_kept_out(u8str_t path, const u8str_t *folders, size_t count) {
    if (!folders) return false;
    for (size_t i = 0; i < count; ++i) {
        if (rubraview_path_is_within(path, folders[i])) return true;
    }
    return false;
}

size_t rubraview_folder_list_split(u8str_t joined, u8str_t *out, size_t capacity) {
    size_t n = 0, start = 0;
    for (size_t i = 0; i <= joined.len; ++i) {
        if (i < joined.len && joined.ptr[i] != '|') continue;
        if (i > start && n < capacity && out) out[n++] = (u8str_t){ .ptr = joined.ptr + start, .len = i - start };
        else if (i > start && !out) n++;
        start = i + 1;
    }
    return n;
}

/* Every piece of `joined` but `skip`, with `insert` at `at`, joined again. */
static u8str_t folder_list_build(char *buffer, size_t capacity, u8str_t joined, size_t skip, u8str_t insert, size_t at) {
    u8str_t none = { .ptr = buffer ? buffer : "", .len = 0 };
    if (!buffer || capacity == 0) return none;
    u8str_t parts[256];
    size_t n = rubraview_folder_list_split(joined, parts, 256);
    size_t len = 0;
    bool placed = insert.len == 0;
    for (size_t i = 0; i <= n; ++i) {
        u8str_t piece = { .ptr = "", .len = 0 };
        if (!placed && i == at) { piece = insert; placed = true; --i; }
        else if (i < n && i != skip) piece = parts[i];
        else if (i == n && !placed) { piece = insert; placed = true; }
        else continue;
        if (piece.len == 0) continue;
        if (len + (len ? 1 : 0) + piece.len + 1 > capacity) { buffer[0] = '\0'; return none; }
        if (len) buffer[len++] = '|';
        memcpy(buffer + len, piece.ptr, piece.len);
        len += piece.len;
    }
    buffer[len] = '\0';
    return (u8str_t){ .ptr = buffer, .len = len };
}

u8str_t rubraview_folder_list_put(char *buffer, size_t capacity, u8str_t joined, u8str_t folder, size_t replace) {
    u8str_t parts[256];
    size_t n = rubraview_folder_list_split(joined, parts, 256);
    for (size_t i = 0; i < n; ++i) {
        if (i != replace && rubraview_path_same(parts[i], folder)) return folder_list_build(buffer, capacity, joined, (size_t)-1, (u8str_t){ 0 }, 0);
    }
    if (replace < n) return folder_list_build(buffer, capacity, joined, replace, folder, replace);
    return folder_list_build(buffer, capacity, joined, (size_t)-1, folder, n);
}

u8str_t rubraview_folder_list_remove(char *buffer, size_t capacity, u8str_t joined, size_t index) {
    return folder_list_build(buffer, capacity, joined, index, (u8str_t){ .ptr = "", .len = 0 }, 0);
}

size_t rubraview_history_drop_within(rubraview_history_t *history, const u8str_t *folders, size_t count) {
    if (!history || !history->entries) return 0;
    size_t gone = 0;
    for (size_t i = history->count; i-- > 0;) {
        if (!rubraview_recent_is_kept_out(history->entries[i].path, folders, count)) continue;
        drop_at(history, i);
        gone++;
    }
    return gone;
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
