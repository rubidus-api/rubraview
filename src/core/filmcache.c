/*
 * RV-216: the network film cache's map, its copies' names and the limit (see filmcache.h). Nothing here
 * touches a file: the platform reads and writes the copy and hands the map's text in and out.
 */
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
    snprintf(out, 17, "%016" PRIx64, h);
}

bool rubraview_filmcache_map_make(rubraview_filmcache_map_t *map, uint64_t size, int64_t time) {
    if (!map) return false;
    *map = (rubraview_filmcache_map_t){ .size = size, .time = time };
    map->blocks = (size + RUBRAVIEW_FILMCACHE_BLOCK - 1) / RUBRAVIEW_FILMCACHE_BLOCK;
    if (map->blocks > ((uint64_t)SIZE_MAX - 7) / 8 * 8) return false;
    map->bits = (uint8_t *)calloc((size_t)((map->blocks + 7) / 8) + 1, 1);
    return map->bits != NULL;
}

void rubraview_filmcache_map_free(rubraview_filmcache_map_t *map) {
    if (!map) return;
    free(map->bits);
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
    int n = snprintf(buf, cap, "%spath %.*s\nsize %" PRIu64 "\ntime %" PRId64 "\nblock %u\nblocks ",
                     MAGIC, (int)path.size, (const char *)path.ptr, map->size, map->time, RUBRAVIEW_FILMCACHE_BLOCK);
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
    size_t k = strlen(key);
    if (len - *at < k + 1 || memcmp(text + *at, key, k) != 0 || text[*at + k] != ' ') return false;
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

bool rubraview_filmcache_map_parse(rubraview_filmcache_map_t *map, const char *text, size_t len,
                                   proven_u8str_view_t path, uint64_t size, int64_t time) {
    if (!map) return false;
    *map = (rubraview_filmcache_map_t){ 0 };
    size_t at = sizeof(MAGIC) - 1;
    if (!text || len < at || memcmp(text, MAGIC, at) != 0) return false;
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
