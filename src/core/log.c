/* RV-256 (D-308): a window's log, a ring of whole lines on the heap of its own. */
#include "rubraview/log.h"

#define LOG_MIN ((size_t)1024)

bool rubraview_log_init(rubraview_log_t *log, size_t cap) {
    if (!log) return false;
    *log = (rubraview_log_t){ 0 };
    if (cap < LOG_MIN) cap = LOG_MIN;
    log->ring = (char *)rubraview_alloc(cap);
    if (!log->ring) return false;
    log->cap = cap;
    return true;
}

void rubraview_log_free(rubraview_log_t *log) {
    if (!log) return;
    rubraview_free(log->ring);
    *log = (rubraview_log_t){ 0 };
}

void rubraview_log_clear(rubraview_log_t *log) {
    if (!log) return;
    log->start = log->len = log->lines = 0;
}

static char ring_at(const rubraview_log_t *log, size_t i) { return log->ring[(log->start + i) % log->cap]; }

/* The oldest line given up, through its '\n'. */
static void drop_oldest(rubraview_log_t *log) {
    size_t i = 0;
    while (i < log->len && ring_at(log, i) != '\n') ++i;
    size_t gone = i < log->len ? i + 1 : log->len;
    log->start = (log->start + gone) % log->cap;
    log->len -= gone;
    if (log->lines) log->lines--;
    log->dropped++;
}

void rubraview_log_add(rubraview_log_t *log, double seconds, const char *kind, proven_u8str_view_t text) {
    if (!log || !log->ring) return;
    char line[RUBRAVIEW_LOG_LINE_MAX + 64];
    if (seconds < 0.0) seconds = 0.0;
    unsigned long long ms = (unsigned long long)(seconds * 1000.0 + 0.5);
    int head = rubraview_fmt(line, sizeof line, "{:>7}.{:03} {:<8} ", PROVEN_ARG(ms / 1000u), PROVEN_ARG((unsigned)(ms % 1000u)),
                             PROVEN_ARG(kind ? kind : ""));
    if (head <= 0) return;
    size_t n = (size_t)head < sizeof line ? (size_t)head : sizeof line - 1;
    size_t take = text.size;
    bool cut = take > RUBRAVIEW_LOG_LINE_MAX;
    if (cut) take = RUBRAVIEW_LOG_LINE_MAX;
    while (cut && take > 0 && (text.ptr[take] & 0xC0) == 0x80) --take;   /* never half a character */
    for (size_t i = 0; i < take; ++i) {
        char c = (char)text.ptr[i];
        line[n++] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    if (cut) { line[n++] = '.'; line[n++] = '.'; line[n++] = '.'; }
    line[n++] = '\n';
    if (n > log->cap) return;   /* a ring too small for one line: nothing */
    while (log->len + n > log->cap) drop_oldest(log);
    for (size_t i = 0; i < n; ++i) log->ring[(log->start + log->len + i) % log->cap] = line[i];
    log->len += n;
    log->lines++;
}

size_t rubraview_log_copy(const rubraview_log_t *log, char *out, size_t cap) {
    if (!log || !log->ring) { if (out && cap) out[0] = '\0'; return 0; }
    if (out && cap) {
        size_t n = log->len < cap - 1 ? log->len : cap - 1;
        for (size_t i = 0; i < n; ++i) out[i] = ring_at(log, i);
        out[n] = '\0';
    }
    return log->len;
}

bool rubraview_log_resize(rubraview_log_t *log, size_t cap) {
    if (!log || !log->ring) return false;
    if (cap < LOG_MIN) cap = LOG_MIN;
    if (cap == log->cap) return true;
    char *ring = (char *)rubraview_alloc(cap);
    if (!ring) return false;
    rubraview_log_t smaller = *log;   /* the oldest lines given up until the rest fits */
    while (smaller.len > cap) drop_oldest(&smaller);
    for (size_t i = 0; i < smaller.len; ++i) ring[i] = ring_at(&smaller, i);
    rubraview_free(log->ring);
    log->ring = ring;
    log->cap = cap;
    log->start = 0;
    log->len = smaller.len;
    log->lines = smaller.lines;
    log->dropped = smaller.dropped;
    return true;
}
