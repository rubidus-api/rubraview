#include "rubraview/ini.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static bool u8str_eq(u8str_t a, u8str_t b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

static u8str_t trim(const char *ptr, size_t len) {
    size_t start = 0, end = len;
    while (start < end && (ptr[start] == ' ' || ptr[start] == '\t')) start++;
    while (end > start && (ptr[end - 1] == ' ' || ptr[end - 1] == '\t')) end--;
    return (u8str_t){ .ptr = ptr + start, .len = end - start };
}

static bool ini_entries_reserve(proven_arena_t *arena, rubraview_ini_doc_t *doc, size_t min_capacity) {
    if (doc->capacity >= min_capacity) return true;
    size_t new_cap = doc->capacity == 0 ? 8 : doc->capacity * 2;
    if (new_cap < min_capacity) new_cap = min_capacity;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap * sizeof(rubraview_ini_entry_t));
    if (!proven_is_ok(res.err)) return false;

    rubraview_ini_entry_t *new_entries = (rubraview_ini_entry_t*)(void*)res.value.ptr;
    if (doc->entries && doc->count > 0) {
        memcpy(new_entries, doc->entries, doc->count * sizeof(rubraview_ini_entry_t));
    }
    doc->entries = new_entries;
    doc->capacity = new_cap;
    return true;
}

rubraview_ini_doc_t rubraview_ini_parse(proven_arena_t *arena, u8str_t text) {
    rubraview_ini_doc_t doc = {0};
    if (!arena || !text.ptr || text.len == 0) return doc;

    u8str_t current_section = (u8str_t){ .ptr = "", .len = 0 };

    size_t line_start = 0;
    for (size_t i = 0; i <= text.len; ++i) {
        if (i == text.len || text.ptr[i] == '\n') {
            size_t line_end = i;
            /* Strip a trailing '\r' from CRLF line endings */
            if (line_end > line_start && text.ptr[line_end - 1] == '\r') line_end--;

            u8str_t line = trim(text.ptr + line_start, line_end - line_start);
            line_start = i + 1;

            if (line.len == 0) continue;
            if (line.ptr[0] == ';' || line.ptr[0] == '#') continue;

            if (line.ptr[0] == '[') {
                size_t close = 0;
                while (close < line.len && line.ptr[close] != ']') close++;
                if (close < line.len) {
                    current_section = trim(line.ptr + 1, close - 1);
                }
                continue;
            }

            size_t eq = 0;
            while (eq < line.len && line.ptr[eq] != '=') eq++;
            if (eq >= line.len) continue; /* malformed line, skip leniently */

            u8str_t key = trim(line.ptr, eq);
            u8str_t value = trim(line.ptr + eq + 1, line.len - eq - 1);
            if (key.len == 0) continue;

            if (!ini_entries_reserve(arena, &doc, doc.count + 1)) return doc;
            doc.entries[doc.count++] = (rubraview_ini_entry_t){
                .section = current_section, .key = key, .value = value
            };
        }
    }

    return doc;
}

const u8str_t *rubraview_ini_get(const rubraview_ini_doc_t *doc, u8str_t section, u8str_t key) {
    if (!doc || !doc->entries) return NULL;
    const u8str_t *found = NULL;
    for (size_t i = 0; i < doc->count; ++i) {
        if (u8str_eq(doc->entries[i].section, section) && u8str_eq(doc->entries[i].key, key)) {
            found = &doc->entries[i].value; /* last occurrence wins */
        }
    }
    return found;
}

bool rubraview_ini_get_bool(const rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, bool default_value) {
    const u8str_t *v = rubraview_ini_get(doc, section, key);
    if (!v) return default_value;

    char buf[8];
    size_t n = v->len < sizeof(buf) - 1 ? v->len : sizeof(buf) - 1;
    for (size_t i = 0; i < n; ++i) buf[i] = (char)tolower((unsigned char)v->ptr[i]);
    buf[n] = '\0';

    if (n == v->len) {
        if (strcmp(buf, "true") == 0 || strcmp(buf, "1") == 0 || strcmp(buf, "yes") == 0 || strcmp(buf, "on") == 0) return true;
        if (strcmp(buf, "false") == 0 || strcmp(buf, "0") == 0 || strcmp(buf, "no") == 0 || strcmp(buf, "off") == 0) return false;
    }
    return default_value;
}

long long rubraview_ini_get_int(const rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, long long default_value) {
    const u8str_t *v = rubraview_ini_get(doc, section, key);
    if (!v || v->len == 0) return default_value;

    size_t i = 0;
    bool neg = false;
    if (v->ptr[0] == '-' || v->ptr[0] == '+') { neg = (v->ptr[0] == '-'); i = 1; }
    if (i >= v->len) return default_value;

    long long result = 0;
    for (; i < v->len; ++i) {
        char c = v->ptr[i];
        if (c < '0' || c > '9') return default_value;
        result = result * 10 + (c - '0');
    }
    return neg ? -result : result;
}

double rubraview_ini_get_float(const rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, double default_value) {
    const u8str_t *v = rubraview_ini_get(doc, section, key);
    if (!v || v->len == 0) return default_value;

    /* v->ptr may not be null-terminated (it can be a slice of caller text);
       copy into a bounded stack buffer before handing off to strtod. */
    char buf[64];
    if (v->len >= sizeof(buf)) return default_value;
    memcpy(buf, v->ptr, v->len);
    buf[v->len] = '\0';

    char *endptr = NULL;
    double result = strtod(buf, &endptr);
    if (endptr == buf || *endptr != '\0') return default_value;
    return result;
}

void rubraview_ini_set(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, u8str_t value) {
    if (!arena || !doc) return;

    for (size_t i = 0; i < doc->count; ++i) {
        if (u8str_eq(doc->entries[i].section, section) && u8str_eq(doc->entries[i].key, key)) {
            doc->entries[i].value = value;
            return;
        }
    }

    if (!ini_entries_reserve(arena, doc, doc->count + 1)) return;
    doc->entries[doc->count++] = (rubraview_ini_entry_t){ .section = section, .key = key, .value = value };
}

typedef struct byte_buf {
    uint8_t *data;
    size_t   len;
    size_t   cap;
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

u8str_t rubraview_ini_serialize(proven_arena_t *arena, const rubraview_ini_doc_t *doc) {
    if (!arena || !doc) return (u8str_t){ .ptr = "", .len = 0 };

    byte_buf_t buf = {0};

    /* Emit global-section entries first, with no [section] header. */
    for (size_t i = 0; i < doc->count; ++i) {
        if (doc->entries[i].section.len != 0) continue;
        byte_buf_append_str(arena, &buf, doc->entries[i].key);
        byte_buf_append(arena, &buf, " = ", 3);
        byte_buf_append_str(arena, &buf, doc->entries[i].value);
        byte_buf_append(arena, &buf, "\n", 1);
    }

    /* Emit each remaining section, once, in first-appearance order. */
    for (size_t s = 0; s < doc->count; ++s) {
        u8str_t section = doc->entries[s].section;
        if (section.len == 0) continue;

        bool already_flushed = false;
        for (size_t j = 0; j < s; ++j) {
            if (doc->entries[j].section.len != 0 && u8str_eq(doc->entries[j].section, section)) {
                already_flushed = true;
                break;
            }
        }
        if (already_flushed) continue;

        byte_buf_append(arena, &buf, "[", 1);
        byte_buf_append_str(arena, &buf, section);
        byte_buf_append(arena, &buf, "]\n", 2);

        for (size_t i = 0; i < doc->count; ++i) {
            if (!u8str_eq(doc->entries[i].section, section)) continue;
            byte_buf_append_str(arena, &buf, doc->entries[i].key);
            byte_buf_append(arena, &buf, " = ", 3);
            byte_buf_append_str(arena, &buf, doc->entries[i].value);
            byte_buf_append(arena, &buf, "\n", 1);
        }
    }

    /* Copy into a final, right-sized, null-terminated allocation. */
    proven_result_mem_mut_t res = proven_arena_alloc(arena, buf.len + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    if (buf.len > 0) memcpy(res.value.ptr, buf.data, buf.len);
    res.value.ptr[buf.len] = '\0';

    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = buf.len };
}
