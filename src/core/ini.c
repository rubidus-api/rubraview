#include "rubraview/ini.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

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

/* ---- values: what the subset writes, and what older files hold ---- */

static bool all_digits(const char *p, size_t n) {
    if (n == 0) return false;
    for (size_t i = 0; i < n; ++i) if (p[i] < '0' || p[i] > '9') return false;
    return true;
}

/* The kind a piece of unquoted text is, read as TOML would read it.
   TOML refuses a leading zero (`010`), so that stays a string — which is
   also what an INI reader saw. */
static rubraview_ini_kind_t kind_of_bare(u8str_t v) {
    if (v.len == 4 && memcmp(v.ptr, "true", 4) == 0) return RUBRAVIEW_INI_BOOL;
    if (v.len == 5 && memcmp(v.ptr, "false", 5) == 0) return RUBRAVIEW_INI_BOOL;

    size_t i = 0;
    if (v.len > 0 && (v.ptr[0] == '-' || v.ptr[0] == '+')) i = 1;
    const char *p = v.ptr + i;
    size_t n = v.len - i;
    size_t dot = n;
    for (size_t k = 0; k < n; ++k) if (p[k] == '.') { dot = k; break; }

    if (dot == n) {
        if (!all_digits(p, n)) return RUBRAVIEW_INI_STRING;
        if (n > 1 && p[0] == '0') return RUBRAVIEW_INI_STRING;
        return RUBRAVIEW_INI_INT;
    }
    if (!all_digits(p, dot) || !all_digits(p + dot + 1, n - dot - 1)) return RUBRAVIEW_INI_STRING;
    if (dot > 1 && p[0] == '0') return RUBRAVIEW_INI_STRING;
    return RUBRAVIEW_INI_FLOAT;
}

/* Reads the value part of a line. A quoted string loses its quotes and
   its two escapes; anything else is taken as it stands, with its kind
   worked out from the text. */
static u8str_t read_value(proven_arena_t *arena, u8str_t raw, rubraview_ini_kind_t *out_kind) {
    if (raw.len >= 2 && raw.ptr[0] == '"') {
        size_t close = 0;
        bool escaped = false;
        for (size_t i = 1; i < raw.len; ++i) {
            if (raw.ptr[i] == '\\' && i + 1 < raw.len && (raw.ptr[i + 1] == '\\' || raw.ptr[i + 1] == '"')) {
                escaped = true;
                ++i;
                continue;
            }
            if (raw.ptr[i] == '"') { close = i; break; }
        }
        if (close > 0) {
            *out_kind = RUBRAVIEW_INI_STRING;
            u8str_t inner = { .ptr = raw.ptr + 1, .len = close - 1 };
            if (!escaped) return inner;
            proven_result_mem_mut_t res = proven_arena_alloc(arena, inner.len + 1);
            if (!proven_is_ok(res.err)) return inner;
            char *out = (char*)(void*)res.value.ptr;
            size_t n = 0;
            for (size_t i = 0; i < inner.len; ++i) {
                if (inner.ptr[i] == '\\' && i + 1 < inner.len &&
                    (inner.ptr[i + 1] == '\\' || inner.ptr[i + 1] == '"')) {
                    ++i;
                }
                out[n++] = inner.ptr[i];
            }
            out[n] = '\0';
            return (u8str_t){ .ptr = out, .len = n };
        }
        /* No closing quote: an older hand edit. Keep it as it stands. */
    }
    *out_kind = kind_of_bare(raw);
    return raw;
}

bool rubraview_ini_name_ok(u8str_t name) {
    if (name.len == 0) return false;
    for (size_t i = 0; i < name.len; ++i) {
        char c = name.ptr[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

static bool ini_entries_reserve(proven_arena_t *arena, rubraview_ini_doc_t *doc, size_t min_capacity) {
    if (doc->capacity >= min_capacity) return true;
    size_t new_cap = doc->capacity == 0 ? 8 : doc->capacity * 2;
    if (new_cap < min_capacity) new_cap = min_capacity;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(rubraview_ini_entry_t));
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

    /* A byte-order mark is not part of the first line. Left in, it made
       `[video]` unrecognisable and hid the whole section (VM, 2026-09-13). */
    size_t line_start = 0;
    if (text.len >= 3 && (unsigned char)text.ptr[0] == 0xEF &&
        (unsigned char)text.ptr[1] == 0xBB && (unsigned char)text.ptr[2] == 0xBF) {
        line_start = 3;
    }
    for (size_t i = line_start; i <= text.len; ++i) {
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
            u8str_t raw = trim(line.ptr + eq + 1, line.len - eq - 1);
            if (key.len == 0) continue;

            rubraview_ini_kind_t kind = RUBRAVIEW_INI_STRING;
            u8str_t value = read_value(arena, raw, &kind);

            if (!ini_entries_reserve(arena, &doc, doc.count + 1)) return doc;
            doc.entries[doc.count++] = (rubraview_ini_entry_t){
                .section = current_section, .key = key, .value = value, .kind = kind
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

static void set_kind(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key,
                     u8str_t value, rubraview_ini_kind_t kind) {
    if (!arena || !doc) return;

    for (size_t i = 0; i < doc->count; ++i) {
        if (u8str_eq(doc->entries[i].section, section) && u8str_eq(doc->entries[i].key, key)) {
            doc->entries[i].value = value;
            doc->entries[i].kind = kind;
            return;
        }
    }

    if (!ini_entries_reserve(arena, doc, doc->count + 1)) return;
    doc->entries[doc->count++] = (rubraview_ini_entry_t){
        .section = section, .key = key, .value = value, .kind = kind
    };
}

void rubraview_ini_set(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, u8str_t value) {
    set_kind(arena, doc, section, key, value, kind_of_bare(value));
}

void rubraview_ini_set_string(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, u8str_t value) {
    set_kind(arena, doc, section, key, value, RUBRAVIEW_INI_STRING);
}

void rubraview_ini_set_bool(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, bool value) {
    set_kind(arena, doc, section, key, value ? U8("true") : U8("false"), RUBRAVIEW_INI_BOOL);
}

static u8str_t arena_text(proven_arena_t *arena, const char *text, int len) {
    if (!arena || len <= 0) return (u8str_t){ .ptr = "", .len = 0 };
    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)len + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    memcpy(res.value.ptr, text, (size_t)len);
    res.value.ptr[len] = '\0';
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = (size_t)len };
}

void rubraview_ini_set_int(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, long long value) {
    char buffer[32];
    int n = snprintf(buffer, sizeof(buffer), "%lld", value);
    set_kind(arena, doc, section, key, arena_text(arena, buffer, n), RUBRAVIEW_INI_INT);
}

void rubraview_ini_set_float(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, double value) {
    /* TOML wants digits on both sides of the point and no exponent; %.6f
       then trailing zeros trimmed keeps one digit after the point. */
    char buffer[64];
    int n = snprintf(buffer, sizeof(buffer), "%.6f", value);
    if (n <= 0 || n >= (int)sizeof(buffer)) { n = snprintf(buffer, sizeof(buffer), "0.0"); }
    while (n > 2 && buffer[n - 1] == '0' && buffer[n - 2] != '.') buffer[--n] = '\0';
    set_kind(arena, doc, section, key, arena_text(arena, buffer, n), RUBRAVIEW_INI_FLOAT);
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

/* One value, in the subset: bare for the typed kinds, quoted with the two
   escapes for text. A line break inside text would end the line, so it
   becomes a space — the subset has no multi-line strings. */
static void append_value(proven_arena_t *arena, byte_buf_t *b, const rubraview_ini_entry_t *e) {
    if (e->kind != RUBRAVIEW_INI_STRING) {
        byte_buf_append_str(arena, b, e->value);
        return;
    }
    byte_buf_append(arena, b, "\"", 1);
    for (size_t i = 0; i < e->value.len; ++i) {
        char c = e->value.ptr[i];
        if (c == '\\') byte_buf_append(arena, b, "\\\\", 2);
        else if (c == '"') byte_buf_append(arena, b, "\\\"", 2);
        else if (c == '\n' || c == '\r' || c == '\t') byte_buf_append(arena, b, " ", 1);
        else byte_buf_append(arena, b, &c, 1);
    }
    byte_buf_append(arena, b, "\"", 1);
}

u8str_t rubraview_ini_serialize(proven_arena_t *arena, const rubraview_ini_doc_t *doc) {
    if (!arena || !doc) return (u8str_t){ .ptr = "", .len = 0 };

    byte_buf_t buf = {0};

    /* Emit global-section entries first, with no [section] header. */
    for (size_t i = 0; i < doc->count; ++i) {
        if (doc->entries[i].section.len != 0) continue;
        byte_buf_append_str(arena, &buf, doc->entries[i].key);
        byte_buf_append(arena, &buf, " = ", 3);
        append_value(arena, &buf, &doc->entries[i]);
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
            /* A key repeated in the document is written once, with its
               last value: TOML refuses a repeat. */
            bool later = false;
            for (size_t k = i + 1; k < doc->count; ++k) {
                if (u8str_eq(doc->entries[k].section, section) && u8str_eq(doc->entries[k].key, doc->entries[i].key)) {
                    later = true;
                    break;
                }
            }
            if (later) continue;
            byte_buf_append_str(arena, &buf, doc->entries[i].key);
            byte_buf_append(arena, &buf, " = ", 3);
            append_value(arena, &buf, &doc->entries[i]);
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
