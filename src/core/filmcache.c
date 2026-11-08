/*
 * RV-216: the network film cache's map, its copies' names and the limit (see filmcache.h). Nothing here
 * touches a file: the platform reads and writes the copy and hands the map's text in and out.
 */
#include "rubraview/core.h"
#include "rubraview/filmcache.h"
#include "rubraview/path.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char MAGIC[] = "rubraview film cache 1\n";

void rubraview_filmcache_name(char out[17], proven_u8str_view_t path) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < path.size; ++i) {
        uint8_t c = path.ptr[i];
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + 'a');
        if (c == '/') c = '\\';
        h = (h ^ c) * 0x100000001B3ull;
    }
    (void)rubraview_fmt(out, 17, "{:016x}", PROVEN_ARG((unsigned long long)h));
}

bool rubraview_filmcache_map_make(rubraview_filmcache_map_t *map, uint64_t size, int64_t time) {
    if (!map) return false;
    *map = (rubraview_filmcache_map_t){ .size = size, .time = time };
    map->blocks = (size + RUBRAVIEW_FILMCACHE_BLOCK - 1) / RUBRAVIEW_FILMCACHE_BLOCK;
    if (map->blocks > ((uint64_t)SIZE_MAX - 7) / 8 * 8) return false;
    map->bits = (uint8_t *)rubraview_alloc_zero((size_t)((map->blocks + 7) / 8) + 1, 1);
    return map->bits != NULL;
}

void rubraview_filmcache_map_free(rubraview_filmcache_map_t *map) {
    if (!map) return;
    rubraview_free(map->bits);
    *map = (rubraview_filmcache_map_t){ 0 };
}

bool rubraview_filmcache_map_has(const rubraview_filmcache_map_t *map, uint64_t block) {
    return map && map->bits && block < map->blocks && (map->bits[block / 8] >> (block % 8)) & 1u;
}

void rubraview_filmcache_map_mark(rubraview_filmcache_map_t *map, uint64_t block) {
    if (!map || !map->bits || block >= map->blocks || rubraview_filmcache_map_has(map, block)) return;
    map->bits[block / 8] |= (uint8_t)(1u << (block % 8));
    map->filled++;
}

bool rubraview_filmcache_map_holds(const rubraview_filmcache_map_t *map, uint64_t at, uint64_t len) {
    if (!map || len == 0) return true;
    if (at >= map->size || len > map->size - at) return false;
    for (uint64_t b = at / RUBRAVIEW_FILMCACHE_BLOCK; b <= (at + len - 1) / RUBRAVIEW_FILMCACHE_BLOCK; ++b)
        if (!rubraview_filmcache_map_has(map, b)) return false;
    return true;
}

uint64_t rubraview_filmcache_map_next_gap(const rubraview_filmcache_map_t *map, uint64_t from) {
    if (!map) return 0;
    for (uint64_t b = from; b < map->blocks; ++b) {
        if (map->bits[b / 8] == 0xFF && b % 8 == 0 && b + 8 <= map->blocks) { b += 7; continue; }
        if (!rubraview_filmcache_map_has(map, b)) return b;
    }
    return map->blocks;
}

static size_t digits(uint64_t blocks) { return (size_t)((blocks + 3) / 4); }

size_t rubraview_filmcache_map_text_size(const rubraview_filmcache_map_t *map, proven_u8str_view_t path) {
    if (!map) return 0;
    return sizeof(MAGIC) + 5 + path.size + 1 + 6 + 20 + 1 + 6 + 21 + 1 + 7 + 10 + 1 + 7 + digits(map->blocks) + 2;
}

size_t rubraview_filmcache_map_text(const rubraview_filmcache_map_t *map, proven_u8str_view_t path, char *buf, size_t cap) {
    if (!map || !buf || cap < rubraview_filmcache_map_text_size(map, path)) return 0;
    for (size_t i = 0; i < path.size; ++i) if (path.ptr[i] == '\n' || path.ptr[i] == '\r' || path.ptr[i] == 0) return 0;
    int n = rubraview_fmt(buf, cap, "{}path {}\nsize {}\ntime {}\nblock {}\nblocks ",
                          PROVEN_ARG(MAGIC), PROVEN_ARG(path), PROVEN_ARG((unsigned long long)map->size), PROVEN_ARG((long long)map->time),
                          PROVEN_ARG((unsigned)RUBRAVIEW_FILMCACHE_BLOCK));
    if (n <= 0 || (size_t)n >= cap) return 0;
    size_t at = (size_t)n;
    for (uint64_t d = 0; d < digits(map->blocks); ++d) {
        unsigned v = 0;
        for (unsigned k = 0; k < 4; ++k) if (rubraview_filmcache_map_has(map, d * 4 + k)) v |= 1u << k;
        buf[at++] = "0123456789abcdef"[v];
    }
    buf[at++] = '\n';
    buf[at] = '\0';
    return at;
}

/* The line after `key ` at `*at`, its end; false when it is not there. */
static bool line(const char *text, size_t len, size_t *at, const char *key, const char **value, size_t *value_len) {
    size_t k = proven_cstr_len(key);
    if (len - *at < k + 1 || proven_sys_mem_cmp(text + *at, key, k) != 0 || text[*at + k] != ' ') return false;
    size_t from = *at + k + 1, end = from;
    while (end < len && text[end] != '\n') ++end;
    if (end >= len) return false;
    *value = text + from;
    *value_len = end - from;
    *at = end + 1;
    return true;
}

static bool number(const char *s, size_t n, bool sign, uint64_t *out) {
    bool negative = sign && n > 0 && s[0] == '-';
    size_t i = negative ? 1 : 0;
    if (i >= n || n - i > 20) return false;
    uint64_t v = 0;
    for (; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        uint64_t d = (uint64_t)(s[i] - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    if (negative) { if (v > (uint64_t)INT64_MAX) return false; v = (uint64_t)(-(int64_t)v); }
    *out = v;
    return true;
}

bool rubraview_filmcache_map_parse(rubraview_filmcache_map_t *map, proven_u8str_view_t text_view,
                                   proven_u8str_view_t path, uint64_t size, int64_t time) {
    const char *text = (const char *)text_view.ptr;
    size_t len = (size_t)text_view.size;
    if (!map) return false;
    *map = (rubraview_filmcache_map_t){ 0 };
    size_t at = sizeof(MAGIC) - 1;
    if (!text || len < at || proven_sys_mem_cmp(text, MAGIC, at) != 0) return false;
    const char *v; size_t n; uint64_t got;
    if (!line(text, len, &at, "path", &v, &n) ||
        !rubraview_path_same((proven_u8str_view_t){ .ptr = (const proven_byte_t *)v, .size = n }, path)) return false;
    if (!line(text, len, &at, "size", &v, &n) || !number(v, n, false, &got) || got != size) return false;
    if (!line(text, len, &at, "time", &v, &n) || !number(v, n, true, &got) || (int64_t)got != time) return false;
    if (!line(text, len, &at, "block", &v, &n) || !number(v, n, false, &got) || got != RUBRAVIEW_FILMCACHE_BLOCK) return false;
    if (!line(text, len, &at, "blocks", &v, &n)) return false;
    rubraview_filmcache_map_t m;
    if (!rubraview_filmcache_map_make(&m, size, time)) return false;
    if (n != digits(m.blocks)) { rubraview_filmcache_map_free(&m); return false; }
    for (size_t d = 0; d < n; ++d) {
        char c = v[d];
        unsigned x = c >= '0' && c <= '9' ? (unsigned)(c - '0') : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) : 16u;
        if (x > 15) { rubraview_filmcache_map_free(&m); return false; }
        for (unsigned k = 0; k < 4; ++k) {
            uint64_t b = (uint64_t)d * 4 + k;
            if (!(x >> k & 1u)) continue;
            if (b >= m.blocks) { rubraview_filmcache_map_free(&m); return false; }   /* a bit past the end */
            rubraview_filmcache_map_mark(&m, b);
        }
    }
    *map = m;
    return true;
}

size_t rubraview_filmcache_evict(const rubraview_filmcache_entry_t *entries, size_t count, uint64_t limit,
                                 uint64_t incoming, size_t *victims, size_t cap) {
    if (!entries || !victims) return 0;
    uint64_t total = incoming;
    for (size_t i = 0; i < count; ++i) total += entries[i].bytes;
    size_t chosen = 0;
    while (total > limit && chosen < cap) {
        size_t oldest = count;
        for (size_t i = 0; i < count; ++i) {
            if (entries[i].open) continue;
            bool taken = false;
            for (size_t c = 0; c < chosen && !taken; ++c) taken = victims[c] == i;
            if (taken) continue;
            if (oldest == count || entries[i].used < entries[oldest].used) oldest = i;
        }
        if (oldest == count) break;
        victims[chosen++] = oldest;
        total -= entries[oldest].bytes;
    }
    return chosen;
}

/* ---- RV-261: a film in memory ---- */

uint64_t rubraview_filmbuf_budget(uint64_t file_size, uint64_t setting, uint64_t total_mem, uint64_t avail_mem) {
    if (setting == 0 || file_size == 0) return 0;
    uint64_t keep = total_mem / 5;                                   /* 20 % stays free */
    uint64_t room = avail_mem > keep ? avail_mem - keep : 0;
    uint64_t cap = setting < room ? setting : room;
    if (cap < RUBRAVIEW_FILMBUF_FLOOR) cap = RUBRAVIEW_FILMBUF_FLOOR;
    return cap < file_size ? cap : file_size;
}

bool rubraview_filmbuf_may_fill(uint64_t total_mem, uint64_t avail_mem, uint64_t held_bytes) {
    return held_bytes < RUBRAVIEW_FILMBUF_FLOOR || avail_mem > total_mem / 5;
}

uint64_t rubraview_filmbuf_head_bytes(double bits_per_second) {
    const uint64_t MB = 1u << 20;
    if (!(bits_per_second > 0.0)) return 32 * MB;
    double bytes = bits_per_second / 8.0 * 20.0;
    if (bytes < (double)(16 * MB)) return 16 * MB;
    if (bytes > (double)(128 * MB)) return 128 * MB;
    return (uint64_t)bytes;
}

bool rubraview_filmbuf_make(rubraview_filmbuf_t *w, uint64_t file_size, uint64_t budget) {
    *w = (rubraview_filmbuf_t){0};
    if (file_size == 0 || budget == 0) return false;
    w->file_blocks = (file_size + RUBRAVIEW_FILMCACHE_BLOCK - 1) / RUBRAVIEW_FILMCACHE_BLOCK;
    w->slots = (budget + RUBRAVIEW_FILMCACHE_BLOCK - 1) / RUBRAVIEW_FILMCACHE_BLOCK;
    if (w->slots >= w->file_blocks) { w->slots = w->file_blocks; w->whole = true; }
    w->tags = (uint64_t *)rubraview_alloc_zero((size_t)w->slots, sizeof *w->tags);
    if (!w->tags) { *w = (rubraview_filmbuf_t){0}; return false; }
    return true;
}

void rubraview_filmbuf_free(rubraview_filmbuf_t *w) {
    rubraview_free(w->tags);
    *w = (rubraview_filmbuf_t){0};
}

uint64_t rubraview_filmbuf_slot(const rubraview_filmbuf_t *w, uint64_t block) {
    return w->slots ? block % w->slots : 0;
}

bool rubraview_filmbuf_has(const rubraview_filmbuf_t *w, uint64_t block) {
    return w->tags && block < w->file_blocks && w->tags[block % w->slots] == block + 1;
}

void rubraview_filmbuf_put(rubraview_filmbuf_t *w, uint64_t block) {
    if (!w->tags || block >= w->file_blocks) return;
    uint64_t *t = &w->tags[block % w->slots];
    if (*t == 0) w->held++;
    *t = block + 1;
}

void rubraview_filmbuf_drop(rubraview_filmbuf_t *w, uint64_t block) {
    if (!w->tags || block >= w->file_blocks) return;
    uint64_t *t = &w->tags[block % w->slots];
    if (*t != 0) w->held--;
    *t = 0;
}

uint64_t rubraview_filmbuf_next(const rubraview_filmbuf_t *w, uint64_t from, uint64_t limit) {
    if (!w->tags) return UINT64_MAX;
    uint64_t end = limit < w->file_blocks ? limit : w->file_blocks;
    if (from >= w->file_blocks) from = w->file_blocks - 1;
    uint64_t stop = w->whole ? end : (from + w->slots < end ? from + w->slots : end);
    for (uint64_t b = from; b < stop; ++b) if (!rubraview_filmbuf_has(w, b)) return b;
    if (w->whole) for (uint64_t b = 0; b < from && b < end; ++b) if (!rubraview_filmbuf_has(w, b)) return b;
    return UINT64_MAX;
}
