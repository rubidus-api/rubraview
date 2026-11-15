/* RV-292: a job's note, written, read back and judged (include/rubraview/jobnote.h). */
#include "rubraview/jobnote.h"

/* `value` with a line break written as a space, appended after "key=" and before "\n"; false when it does not fit. */
static bool put(char *out, size_t cap, size_t *at, const char *key, const char *value) {
    size_t k = proven_cstr_len(key), v = proven_cstr_len(value);
    if (*at + k + 1 + v + 1 >= cap) return false;
    proven_sys_mem_copy(out + *at, key, k);
    *at += k;
    out[(*at)++] = '=';
    for (size_t i = 0; i < v; ++i) out[(*at)++] = (value[i] == '\n' || value[i] == '\r') ? ' ' : value[i];
    out[(*at)++] = '\n';
    out[*at] = '\0';
    return true;
}

static bool put_u64(char *out, size_t cap, size_t *at, const char *key, uint64_t value) {
    char num[32];
    int n = rubraview_fmt(num, sizeof num, "{}", PROVEN_ARG(value));
    return n > 0 && put(out, cap, at, key, num);
}

size_t rubraview_jobnote_format(const rubraview_jobnote_t *note, char *out, size_t cap) {
    if (!note || !out || cap == 0) return 0;
    size_t at = 0;
    out[0] = '\0';
    bool ok = put(out, cap, &at, "# a Rubraview job", "s note: deleted when it ends as it should") &&
              put(out, cap, &at, "kind", note->kind) && put_u64(out, cap, &at, "pid", note->pid) &&
              put_u64(out, cap, &at, "started", note->started) && put_u64(out, cap, &at, "when", (uint64_t)note->when) &&
              put_u64(out, cap, &at, "viewer", note->viewer) && put(out, cap, &at, "where", note->where) &&
              put(out, cap, &at, "first", note->first) && put_u64(out, cap, &at, "count", note->count) &&
              put_u64(out, cap, &at, "done", note->done);
    return ok ? at : 0;
}

static uint64_t number(proven_u8str_view_t v) {
    uint64_t n = 0;
    for (size_t i = 0; i < v.size; ++i) {
        if (v.ptr[i] < '0' || v.ptr[i] > '9') return 0;
        n = n * 10u + (uint64_t)(v.ptr[i] - '0');
    }
    return n;
}

static void text_into(char *dst, size_t cap, proven_u8str_view_t v) {
    size_t n = v.size < cap - 1 ? v.size : cap - 1;
    proven_sys_mem_copy(dst, v.ptr, n);
    dst[n] = '\0';
}

bool rubraview_jobnote_parse(proven_u8str_view_t text, rubraview_jobnote_t *note) {
    if (!note) return false;
    *note = (rubraview_jobnote_t){0};
    size_t i = 0;
    while (i < text.size) {
        size_t start = i;
        while (i < text.size && text.ptr[i] != '\n') i++;
        size_t end = i;
        if (i < text.size) i++;
        if (end > start && text.ptr[end - 1] == '\r') end--;
        if (end == start || text.ptr[start] == '#') continue;
        size_t eq = start;
        while (eq < end && text.ptr[eq] != '=') eq++;
        if (eq == end) continue;
        proven_u8str_view_t key = { .ptr = text.ptr + start, .size = eq - start };
        proven_u8str_view_t val = { .ptr = text.ptr + eq + 1, .size = end - eq - 1 };
        if (proven_u8str_view_eq(key, PROVEN_LIT("kind"))) text_into(note->kind, sizeof note->kind, val);
        else if (proven_u8str_view_eq(key, PROVEN_LIT("pid"))) note->pid = (uint32_t)number(val);
        else if (proven_u8str_view_eq(key, PROVEN_LIT("started"))) note->started = number(val);
        else if (proven_u8str_view_eq(key, PROVEN_LIT("when"))) note->when = (int64_t)number(val);
        else if (proven_u8str_view_eq(key, PROVEN_LIT("viewer"))) note->viewer = (uint32_t)number(val);
        else if (proven_u8str_view_eq(key, PROVEN_LIT("where"))) text_into(note->where, sizeof note->where, val);
        else if (proven_u8str_view_eq(key, PROVEN_LIT("first"))) text_into(note->first, sizeof note->first, val);
        else if (proven_u8str_view_eq(key, PROVEN_LIT("count"))) note->count = number(val);
        else if (proven_u8str_view_eq(key, PROVEN_LIT("done"))) note->done = number(val);
    }
    return note->kind[0] != '\0' && note->pid != 0;
}

size_t rubraview_jobnote_name(const rubraview_jobnote_t *note, char *out, size_t cap) {
    if (!note || !out) return 0;
    int n = rubraview_fmt(out, cap, "{}-{}.job", PROVEN_ARG(note->pid), PROVEN_ARG(note->started));
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

bool rubraview_jobnote_stale(const rubraview_jobnote_t *note, bool running, uint64_t running_started) {
    if (!note) return false;
    return !running || running_started != note->started;
}
