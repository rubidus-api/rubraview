#include "rubraview/rar.h"
#include "rar_unpack.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/*
 * The headers, read for this project from the format as UnRAR's
 * arcread.cpp / headers.hpp / headers5.hpp and libarchive's
 * archive_read_support_format_rar*.c describe it (both read side by side,
 * 2026-09-30). Every length is checked against the mapped archive.
 */

static const uint8_t SIG4[7] = { 'R', 'a', 'r', '!', 0x1A, 0x07, 0x00 };
static const uint8_t SIG5[8] = { 'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00 };
#define SFX_SEARCH (1u << 20)

typedef struct rar_state {
    rar_unpack_t *unpack;
    size_t next;            /* the entry a solid stream continues with; SIZE_MAX when none */
    _Atomic bool cancel;
    _Atomic uint64_t done, total;
} rar_state_t;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t crc_table[256];
static void crc_init(void) {
    if (crc_table[1]) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
}
static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) crc = crc_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return crc;
}

static long find_signature(const uint8_t *data, size_t size, bool *rar5) {
    size_t limit = size < SFX_SEARCH ? size : SFX_SEARCH;
    for (size_t i = 0; i + 7 <= limit; ++i) {
        if (data[i] != 'R' || memcmp(data + i, SIG4, 6) != 0) continue;
        if (data[i + 6] == 0x00) { *rar5 = false; return (long)i; }
        if (i + 8 <= size && memcmp(data + i, SIG5, 8) == 0) { *rar5 = true; return (long)i; }
    }
    return -1;
}

bool rubraview_rar_is_rar(const uint8_t *data, size_t size) {
    bool rar5 = false;
    return data && find_signature(data, size, &rar5) >= 0;
}

/* ---- names ---- */

static size_t utf8_put(uint8_t *out, uint32_t cp) {
    if (cp < 0x80) { out[0] = (uint8_t)cp; return 1; }
    if (cp < 0x800) { out[0] = (uint8_t)(0xC0 | cp >> 6); out[1] = (uint8_t)(0x80 | (cp & 0x3F)); return 2; }
    if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
    if (cp < 0x10000) {
        out[0] = (uint8_t)(0xE0 | cp >> 12); out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (uint8_t)(0xF0 | cp >> 18); out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (uint8_t)(0x80 | (cp & 0x3F));
    return 4;
}

static u8str_t keep(proven_arena_t *arena, const uint8_t *bytes, size_t len) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    char *p = (char*)res.value.ptr;
    memcpy(p, bytes, len);
    p[len] = '\0';
    for (size_t i = 0; i < len; ++i) if (p[i] == '\\') p[i] = '/';   /* one separator, as ZIP's */
    return (u8str_t){ .ptr = p, .len = len };
}

/* RAR 4's Unicode names: the plain part, then a small code that says,
   character by character, how to widen it (UnRAR's EncodeFileName::Decode). */
static u8str_t decode_rar4_unicode(proven_arena_t *arena, const uint8_t *name, size_t name_size,
                                   const uint8_t *enc, size_t enc_size) {
    uint16_t wide[1024];
    size_t dec = 0, pos = 0;
    uint8_t high = pos < enc_size ? enc[pos++] : 0;
    uint8_t flags = 0;
    int flag_bits = 0;
    while (pos < enc_size && dec < 1024) {
        if (flag_bits == 0) { flags = enc[pos++]; flag_bits = 8; }
        switch (flags >> 6) {
            case 0:
                if (pos >= enc_size) break;
                wide[dec++] = enc[pos++];
                break;
            case 1:
                if (pos >= enc_size) break;
                wide[dec++] = (uint16_t)(enc[pos++] + (high << 8));
                break;
            case 2:
                if (pos + 1 >= enc_size) break;
                wide[dec++] = (uint16_t)(enc[pos] + (enc[pos + 1] << 8));
                pos += 2;
                break;
            case 3: {
                if (pos >= enc_size) break;
                int length = enc[pos++];
                if (length & 0x80) {
                    if (pos >= enc_size) break;
                    uint8_t correction = enc[pos++];
                    for (length = (length & 0x7f) + 2; length > 0 && dec < name_size && dec < 1024; length--, dec++)
                        wide[dec] = (uint16_t)(((name[dec] + correction) & 0xff) + (high << 8));
                } else {
                    for (length += 2; length > 0 && dec < name_size && dec < 1024; length--, dec++) wide[dec] = name[dec];
                }
                break;
            }
        }
        flags = (uint8_t)(flags << 2);
        flag_bits -= 2;
    }
    uint8_t out[4096];
    size_t n = 0;
    for (size_t i = 0; i < dec && n + 4 < sizeof(out); ++i) {
        uint32_t cp = wide[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < dec && wide[i + 1] >= 0xDC00 && wide[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (wide[i + 1] - 0xDC00);
            ++i;
        }
        n += utf8_put(out + n, cp);
    }
    return keep(arena, out, n);
}

/* ---- the entry list ---- */

typedef struct list {
    proven_arena_t *arena;
    rubraview_rar_entry_t *items;
    size_t count, cap;
} list_t;

static bool list_push(list_t *l, const rubraview_rar_entry_t *e) {
    if (l->count == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 64;
        proven_result_mem_mut_t res = proven_arena_alloc(l->arena, cap * sizeof(rubraview_rar_entry_t));
        if (!proven_is_ok(res.err)) return false;
        if (l->count) memcpy(res.value.ptr, l->items, l->count * sizeof(rubraview_rar_entry_t));
        l->items = (rubraview_rar_entry_t*)(void*)res.value.ptr;
        l->cap = cap;
    }
    l->items[l->count++] = *e;
    return true;
}

static rubraview_rar_err_t read_rar4(proven_arena_t *arena, rubraview_rar_archive_t *a, size_t pos, list_t *l) {
    while (pos + 7 <= a->size) {
        const uint8_t *h = a->data + pos;
        uint8_t type = h[2];
        uint16_t flags = le16(h + 3);
        uint16_t head_size = le16(h + 5);
        if (head_size < 7 || pos + head_size > a->size) return l->count ? RUBRAVIEW_RAR_OK : RUBRAVIEW_RAR_ERR_CORRUPT;
        uint64_t next = pos + head_size;
        if (type == 0x73) {                                  /* main */
            if (flags & 0x0080) { a->headers_encrypted = true; return RUBRAVIEW_RAR_ERR_ENCRYPTED; }
            a->solid_archive = (flags & 0x0008) != 0;
        } else if (type == 0x74 || type == 0x7a) {           /* file, service */
            if (head_size < 32) return RUBRAVIEW_RAR_ERR_CORRUPT;
            uint64_t pack = le32(h + 7), unp = le32(h + 11);
            uint32_t crc = le32(h + 16);
            uint8_t unp_ver = h[24], method = h[25];
            uint16_t name_size = le16(h + 26);
            size_t name_at = 32;
            if (flags & 0x0100) {                            /* large */
                if (head_size < 40) return RUBRAVIEW_RAR_ERR_CORRUPT;
                pack |= (uint64_t)le32(h + 32) << 32;
                unp |= (uint64_t)le32(h + 36) << 32;
                name_at = 40;
            }
            if (name_at + name_size > head_size) return RUBRAVIEW_RAR_ERR_CORRUPT;
            if (pack > a->size || next + pack > a->size) {
                /* the last file cut short: what came before is still readable */
                return l->count ? RUBRAVIEW_RAR_OK : RUBRAVIEW_RAR_ERR_CORRUPT;
            }
            bool dir = (flags & 0x00e0) == 0x00e0 || (unp_ver < 20 && (le32(h + 28) & 0x10));
            if (type == 0x74 && !dir) {
                rubraview_rar_entry_t e = {0};
                const uint8_t *name = h + name_at;
                size_t plain = 0;
                while (plain < name_size && name[plain]) plain++;
                if ((flags & 0x0200) && plain + 1 < name_size) {
                    e.name = decode_rar4_unicode(arena, name, plain, name + plain + 1, name_size - plain - 1);
                } else {
                    e.name = keep(arena, name, plain);
                    /* plain ASCII is what it is; anything else is a code page's */
                    for (size_t i = 0; i < plain; ++i) if (name[i] >= 0x80) { e.name_is_legacy = true; break; }
                }
                e.size = unp;
                e.packed_size = pack;
                e.data_offset = next;
                e.crc32 = crc;
                e.has_crc = true;
                e.method = method == 0x30 ? 0 : unp_ver;
                e.dict_size = (uint64_t)0x10000 << ((flags & 0x00e0) >> 5);
                e.solid = (flags & 0x0010) != 0;
                e.encrypted = (flags & 0x0004) != 0;
                e.split = (flags & 0x0003) != 0;
                if (!list_push(l, &e)) return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
            }
            next += pack;
        } else if (type == 0x7b) {                           /* end of archive */
            break;
        } else if (flags & 0x8000) {                         /* a long block: its data follows */
            if (head_size < 11) return RUBRAVIEW_RAR_ERR_CORRUPT;
            next += le32(h + 7);
        }
        if (next <= pos || next > a->size) break;
        pos = (size_t)next;
    }
    return RUBRAVIEW_RAR_OK;
}

/* RAR 5's numbers: seven bits a byte, low first, the top bit saying more follow. */
static bool vint(const uint8_t *p, size_t end, size_t *at, uint64_t *out) {
    uint64_t v = 0;
    for (int shift = 0; shift < 64 && *at < end; shift += 7) {
        uint8_t b = p[(*at)++];
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) { *out = v; return true; }
    }
    return false;
}

static rubraview_rar_err_t read_rar5(proven_arena_t *arena, rubraview_rar_archive_t *a, size_t pos, list_t *l) {
    const uint8_t *d = a->data;
    while (pos + 7 <= a->size) {
        size_t at = pos + 4;
        uint64_t header_size = 0;
        if (!vint(d, a->size, &at, &header_size) || header_size == 0 || header_size > a->size - at)
            return l->count ? RUBRAVIEW_RAR_OK : RUBRAVIEW_RAR_ERR_CORRUPT;
        size_t end = at + (size_t)header_size;
        uint64_t type = 0, hflags = 0, extra = 0, data_size = 0;
        if (!vint(d, end, &at, &type) || !vint(d, end, &at, &hflags)) return RUBRAVIEW_RAR_ERR_CORRUPT;
        if ((hflags & 0x0001) && !vint(d, end, &at, &extra)) return RUBRAVIEW_RAR_ERR_CORRUPT;
        if ((hflags & 0x0002) && !vint(d, end, &at, &data_size)) return RUBRAVIEW_RAR_ERR_CORRUPT;
        if (extra > header_size) return RUBRAVIEW_RAR_ERR_CORRUPT;
        if (data_size > a->size - end) return l->count ? RUBRAVIEW_RAR_OK : RUBRAVIEW_RAR_ERR_CORRUPT;
        if (type == 4) { a->headers_encrypted = true; return RUBRAVIEW_RAR_ERR_ENCRYPTED; }
        if (type == 1) {
            uint64_t arc_flags = 0;
            if (!vint(d, end, &at, &arc_flags)) return RUBRAVIEW_RAR_ERR_CORRUPT;
            a->solid_archive = (arc_flags & 0x0004) != 0;
        } else if (type == 2) {
            uint64_t file_flags = 0, unp = 0, attrs = 0, comp = 0, host = 0, name_len = 0;
            if (!vint(d, end, &at, &file_flags) || !vint(d, end, &at, &unp) || !vint(d, end, &at, &attrs))
                return RUBRAVIEW_RAR_ERR_CORRUPT;
            if (file_flags & 0x0002) at += 4;                /* mtime */
            uint32_t crc = 0;
            if (file_flags & 0x0004) {
                if (at + 4 > end) return RUBRAVIEW_RAR_ERR_CORRUPT;
                crc = le32(d + at);
                at += 4;
            }
            if (!vint(d, end, &at, &comp) || !vint(d, end, &at, &host) || !vint(d, end, &at, &name_len) ||
                name_len > end - at)
                return RUBRAVIEW_RAR_ERR_CORRUPT;
            const uint8_t *name = d + at;
            at += (size_t)name_len;
            bool encrypted = false;
            size_t extra_end = end, extra_at = end - (size_t)extra;
            while (extra && extra_at < extra_end) {          /* the file's extra records: only encryption matters here */
                uint64_t rec_size = 0, rec_type = 0;
                size_t rec = extra_at;
                if (!vint(d, extra_end, &rec, &rec_size) || rec_size == 0 || rec_size > extra_end - rec) break;
                size_t rec_end = rec + (size_t)rec_size;
                if (vint(d, rec_end, &rec, &rec_type) && rec_type == 0x01) encrypted = true;
                extra_at = rec_end;
            }
            if (!(file_flags & 0x0001)) {                    /* not a folder */
                rubraview_rar_entry_t e = {0};
                e.name = keep(arena, name, (size_t)name_len);
                e.size = unp;
                e.packed_size = data_size;
                e.data_offset = end;
                e.crc32 = crc;
                e.has_crc = (file_flags & 0x0004) != 0;
                uint64_t ver = comp & 0x3f;
                unsigned method = (unsigned)((comp >> 7) & 7);
                e.method = method == 0 ? 0 : ver == 0 ? 50 : ver == 1 ? 70 : 9999;
                uint64_t dict = (uint64_t)0x20000 << ((comp >> 10) & (ver == 0 ? 0x0f : 0x1f));
                if (ver == 1) {
                    dict += dict / 32 * ((comp >> 15) & 0x1f);
                    if (comp & 0x00100000) e.method = method == 0 ? 0 : 50;
                }
                e.dict_size = dict;
                e.solid = (comp & 0x0040) != 0;
                e.encrypted = encrypted;
                e.split = (hflags & 0x0018) != 0;
                if (!list_push(l, &e)) return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
            }
        } else if (type == 5) {
            break;
        }
        uint64_t next = (uint64_t)end + data_size;
        if (next <= pos || next > a->size) break;
        pos = (size_t)next;
    }
    return RUBRAVIEW_RAR_OK;
}

rubraview_rar_result_t rubraview_rar_open(proven_arena_t *arena, const uint8_t *data, size_t size) {
    rubraview_rar_result_t r = { .err = RUBRAVIEW_RAR_ERR_NOT_A_RAR };
    if (!arena || !data) return r;
    bool rar5 = false;
    long sig = find_signature(data, size, &rar5);
    if (sig < 0) return r;
    crc_init();
    rubraview_rar_archive_t a = { .data = data, .size = size, .rar5 = rar5 };
    list_t l = { .arena = arena };
    r.err = rar5 ? read_rar5(arena, &a, (size_t)sig + 8, &l) : read_rar4(arena, &a, (size_t)sig + 7, &l);
    if (r.err != RUBRAVIEW_RAR_OK) return r;
    rar_state_t *st = (rar_state_t*)calloc(1, sizeof(rar_state_t));
    if (!st) { r.err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; return r; }
    st->next = SIZE_MAX;
    a.entries = l.items;
    a.entry_count = l.count;
    a.state = st;
    r.value = a;
    return r;
}

void rubraview_rar_close(rubraview_rar_archive_t *archive) {
    if (!archive || !archive->state) return;
    rar_state_t *st = (rar_state_t*)archive->state;
    rar_unpack_destroy(st->unpack);
    free(st);
    archive->state = NULL;
}

/* ---- decoding ---- */

typedef struct source { const uint8_t *p; size_t left; } source_t;
static size_t source_read(void *ctx, uint8_t *buffer, size_t capacity) {
    source_t *s = (source_t*)ctx;
    size_t n = s->left < capacity ? s->left : capacity;
    memcpy(buffer, s->p, n);
    s->p += n;
    s->left -= n;
    return n;
}

typedef struct sink {
    uint8_t *out;           /* NULL: count and forget (a solid chain's earlier files) */
    uint64_t capacity, written;
    uint32_t crc;
    rar_state_t *st;
} sink_t;
static bool sink_write(void *ctx, const uint8_t *data, size_t size) {
    sink_t *s = (sink_t*)ctx;
    if (atomic_load(&s->st->cancel)) return false;
    uint64_t room = s->capacity - s->written;
    size_t n = size < room ? size : (size_t)room;
    if (s->out && n) memcpy(s->out + s->written, data, n);
    s->crc = crc_update(s->crc, data, n);
    s->written += n;
    atomic_fetch_add(&s->st->done, n);
    return true;
}

/* One entry through the decoder, into `out` or nowhere. */
static rubraview_rar_err_t decode(rubraview_rar_archive_t *a, rar_state_t *st, size_t index, uint8_t *out) {
    const rubraview_rar_entry_t *e = &a->entries[index];
    if (e->encrypted) return RUBRAVIEW_RAR_ERR_ENCRYPTED;
    if (e->split) return RUBRAVIEW_RAR_ERR_UNSUPPORTED;
    sink_t sink = { .out = out, .capacity = e->size, .crc = 0xFFFFFFFFu, .st = st };
    source_t src = { .p = a->data + e->data_offset, .left = (size_t)e->packed_size };
    if (e->method == 0) {
        if (e->packed_size < e->size) return RUBRAVIEW_RAR_ERR_CORRUPT_STREAM;
        size_t chunk = 1u << 20;
        for (uint64_t at = 0; at < e->size; at += chunk) {
            size_t n = e->size - at < chunk ? (size_t)(e->size - at) : chunk;
            if (!sink_write(&sink, src.p + at, n)) return RUBRAVIEW_RAR_ERR_CANCELLED;
        }
    } else {
        if (e->method == 9999) return RUBRAVIEW_RAR_ERR_UNSUPPORTED;
        if (!st->unpack && !(st->unpack = rar_unpack_create())) return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
        bool solid = e->solid && st->next == index;
        if (!rar_unpack_file(st->unpack, e->method, solid, e->dict_size, e->size, source_read, &src, sink_write, &sink)) {
            if (atomic_load(&st->cancel)) return RUBRAVIEW_RAR_ERR_CANCELLED;
            if (e->dict_size > RAR_UNPACK_MAX_DICT) return RUBRAVIEW_RAR_ERR_TOO_LARGE;
            return RUBRAVIEW_RAR_ERR_UNSUPPORTED;
        }
        if (atomic_load(&st->cancel)) return RUBRAVIEW_RAR_ERR_CANCELLED;
    }
    st->next = index + 1;
    if (sink.written < e->size) return RUBRAVIEW_RAR_ERR_CORRUPT_STREAM;
    if (e->has_crc && (sink.crc ^ 0xFFFFFFFFu) != e->crc32) return RUBRAVIEW_RAR_ERR_CORRUPT_STREAM;
    return RUBRAVIEW_RAR_OK;
}

/* Where decoding must start to reach `index`: itself, unless it continues a solid stream. */
static size_t chain_start(const rubraview_rar_archive_t *a, const rar_state_t *st, size_t index) {
    if (!a->entries[index].solid || a->entries[index].method == 0) return index;
    if (st && st->next != SIZE_MAX && st->next <= index) {
        bool unbroken = true;
        for (size_t i = st->next; i <= index; ++i) if (i > st->next && !a->entries[i].solid) unbroken = false;
        if (unbroken) return st->next;
    }
    size_t j = index;
    while (j > 0 && (a->entries[j].solid || a->entries[j].method == 0)) --j;
    return j;
}

uint64_t rubraview_rar_read_cost(const rubraview_rar_archive_t *archive, size_t index) {
    if (!archive || index >= archive->entry_count) return 0;
    size_t start = chain_start(archive, (const rar_state_t*)archive->state, index);
    uint64_t cost = 0;
    for (size_t i = start; i <= index; ++i) cost += archive->entries[i].size;
    return cost;
}

rubraview_rar_data_result_t rubraview_rar_read_entry(proven_arena_t *arena, rubraview_rar_archive_t *archive,
                                                      size_t index, uint64_t max_entry_bytes) {
    rubraview_rar_data_result_t r = { .err = RUBRAVIEW_RAR_ERR_BAD_INDEX };
    if (!arena || !archive || !archive->state || index >= archive->entry_count) return r;
    rar_state_t *st = (rar_state_t*)archive->state;
    const rubraview_rar_entry_t *e = &archive->entries[index];
    if (e->size > max_entry_bytes || e->size > SIZE_MAX - 1) { r.err = RUBRAVIEW_RAR_ERR_TOO_LARGE; return r; }
    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)e->size + 1);
    if (!proven_is_ok(res.err)) { r.err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; return r; }
    size_t start = chain_start(archive, st, index);
    if (start != st->next) st->next = SIZE_MAX;              /* a fresh stream */
    uint64_t total = 0;
    for (size_t i = start; i <= index; ++i) total += archive->entries[i].size;
    atomic_store(&st->done, 0);
    atomic_store(&st->total, total);
    for (size_t i = start; i < index; ++i) {                 /* the chain before it, decoded and let go */
        if (archive->entries[i].method == 0) { st->next = i + 1; atomic_fetch_add(&st->done, archive->entries[i].size); continue; }
        rubraview_rar_err_t err = decode(archive, st, i, NULL);
        if (err != RUBRAVIEW_RAR_OK && err != RUBRAVIEW_RAR_ERR_CORRUPT_STREAM) { st->next = SIZE_MAX; r.err = err; return r; }
    }
    r.err = decode(archive, st, index, (uint8_t*)res.value.ptr);
    if (r.err == RUBRAVIEW_RAR_ERR_CANCELLED) st->next = SIZE_MAX;
    if (r.err == RUBRAVIEW_RAR_OK) {
        ((uint8_t*)res.value.ptr)[e->size] = 0;
        r.data = (u8str_t){ .ptr = (const char*)res.value.ptr, .len = (size_t)e->size };
    }
    return r;
}

void rubraview_rar_cancel(rubraview_rar_archive_t *archive, bool cancel) {
    if (archive && archive->state) atomic_store(&((rar_state_t*)archive->state)->cancel, cancel);
}

void rubraview_rar_progress(const rubraview_rar_archive_t *archive, uint64_t *out_done, uint64_t *out_total) {
    uint64_t done = 0, total = 0;
    if (archive && archive->state) {
        rar_state_t *st = (rar_state_t*)archive->state;
        done = atomic_load(&st->done);
        total = atomic_load(&st->total);
    }
    if (out_done) *out_done = done;
    if (out_total) *out_total = total;
}
