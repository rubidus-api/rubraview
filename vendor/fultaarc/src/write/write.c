/* write.c - FultaArc: writing ZIP and 7z archives. MIT.
 * Ported from Rubraview's src/core/archive_write.c (MIT, D-68 there; written by Rubraview's owner-side session from
 * PKWARE's APPNOTE and the LZMA SDK's DOC/7zFormat.txt — no clean room is needed for these formats) to FultaArc's
 * API (D-005 #6); folders added. Compressors: miniz's tdefl (MIT), the LZMA SDK's LzmaEnc (public domain). */
#include "../core/internal.h"

#include "miniz.h"
#include "LzmaEnc.h"

#include <stdlib.h>

/*
 * ZIP from PKWARE's APPNOTE.TXT (6.3.x): 4.3.7 local file header, 4.3.12 central directory header,
 * 4.3.14-4.3.16 ZIP64 end records, 4.5.3 the ZIP64 extra field, 4.4.4 bit 11 (UTF-8 names), and Info-ZIP's
 * extended timestamp extra field (0x5455). 7z from the LZMA SDK's DOC/7zFormat.txt (public domain).
 */

/* ---- little-endian output ---- */

typedef struct out {
    const fulta_arc_write_sink_t *sink;
    uint64_t at;            /* bytes appended so far */
    bool failed;
} out_t;

static void put(out_t *o, const void *data, size_t size) {
    if (o->failed || size == 0) return;
    if (o->sink->write(o->sink->ctx, data, size)) { o->failed = true; return; }
    o->at += size;
}

static void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void le64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i)); }

static uint32_t crc32_of(const uint8_t *data, uint64_t size) {
    uint32_t crc = 0;
    while (size > 0) {
        size_t n = size > (1u << 30) ? (1u << 30) : (size_t)size;
        crc = fa_crc32(crc, data, n);
        data += n;
        size -= n;
    }
    return crc;
}

/* ---- names ---- */

typedef struct u8str { const char *ptr; size_t len; } u8str_t;

/* A name that may be stored: not empty or absolute, no drive, no "." or ".." part, no empty part, no control
 * character. A folder's name may end in '/'. */
static bool name_ok(u8str_t name) {
    if (name.len && (name.ptr[name.len - 1] == '/' || name.ptr[name.len - 1] == '\\')) name.len--;
    if (name.len == 0 || name.ptr[0] == '/' || name.ptr[0] == '\\') return false;
    if (name.len >= 2 && name.ptr[1] == ':') return false;                     /* C:... */
    size_t part = 0;
    for (size_t i = 0; i <= name.len; ++i) {
        char c = i < name.len ? name.ptr[i] : '/';
        if (i < name.len && (unsigned char)c < 0x20) return false;
        if (c == '/' || c == '\\') {
            size_t len = i - part;
            if (len == 0 && i < name.len) return false;                       /* "a//b" */
            if (len == 2 && name.ptr[part] == '.' && name.ptr[part + 1] == '.') return false;
            if (len == 1 && name.ptr[part] == '.') return false;
            part = i + 1;
        }
    }
    return name.ptr[name.len - 1] != '/' && name.ptr[name.len - 1] != '\\';
}

/* The name as stored: '/' between folders; a folder's ZIP name ends in '/', its 7z name does not. */
static char *stored_name(const char *name, bool dir, bool zip) {
    size_t len = strlen(name);
    while (len && (name[len - 1] == '/' || name[len - 1] == '\\')) len--;
    char *s = (char*)malloc(len + 2);
    if (!s) return NULL;
    for (size_t i = 0; i < len; ++i) s[i] = name[i] == '\\' ? '/' : name[i];
    if (dir && zip) s[len++] = '/';
    s[len] = '\0';
    return s;
}

static bool is_dir(const fulta_arc_write_entry_t *e) {
    size_t n = strlen(e->name);
    return e->is_dir || (n && (e->name[n - 1] == '/' || e->name[n - 1] == '\\'));
}

/* ---- MS-DOS time, from UTC ---- */

/* Days since 1970-01-01 to a civil date (H. Hinnant's algorithm, public domain description). */
static void civil(int64_t days, int *y, int *m, int *d) {
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int64_t doe = days - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

static void dos_time(int64_t t, uint16_t *out_time, uint16_t *out_date) {
    if (t < 315532800) t = 315532800;                  /* DOS time starts in 1980 */
    int y, m, d;
    int64_t days = t / 86400, secs = t % 86400;
    civil(days, &y, &m, &d);
    if (y > 2107) { y = 2107; m = 12; d = 31; secs = 86399; }
    *out_time = (uint16_t)((secs / 3600) << 11 | ((secs / 60) % 60) << 5 | (secs % 60) / 2);
    *out_date = (uint16_t)((y - 1980) << 9 | m << 5 | d);
}

/* ---- ZIP ---- */

typedef struct zip_record {
    uint64_t size, packed, offset;
    uint32_t crc;
    uint16_t method, time, date, flags;
    bool zip64;
    char *name;
    int64_t mtime;
    bool dir;
} zip_record_t;

static bool ascii(const char *s) {
    for (; *s; ++s) if ((unsigned char)*s >= 0x80) return false;
    return true;
}

static fulta_arc_err_t write_zip(const fulta_arc_write_entry_t *entries, size_t count, int level, bool force64,
                                    const fulta_arc_write_options_t *opt, out_t *o) {
    zip_record_t *rec = (zip_record_t*)calloc(count ? count : 1, sizeof(zip_record_t));
    if (!rec) return FULTA_ARC_ERR_NOMEM;
    fulta_arc_err_t err = FULTA_ARC_OK;
    uint64_t total = 0, done = 0;
    for (size_t i = 0; i < count; ++i) total += entries[i].size;
    mz_uint flags = tdefl_create_comp_flags_from_zip_params(level, -15, MZ_DEFAULT_STRATEGY);
    for (size_t i = 0; i < count && err == FULTA_ARC_OK; ++i) {
        const fulta_arc_write_entry_t *e = &entries[i];
        zip_record_t *r = &rec[i];
        bool dir = is_dir(e);
        r->name = stored_name(e->name, dir, true);
        r->dir = dir;
        if (!r->name) { err = FULTA_ARC_ERR_NOMEM; break; }
        r->size = dir ? 0 : e->size;
        r->crc = dir ? 0 : crc32_of(e->data, e->size);
        r->mtime = e->mtime;
        dos_time(e->mtime, &r->time, &r->date);
        r->flags = ascii(r->name) ? 0 : 0x0800;
        r->offset = o->at;
        /* deflate when it is smaller; stored otherwise */
        void *packed = NULL;
        size_t packed_len = 0;
        if (level > 0 && e->size > 0 && e->size <= SIZE_MAX && !dir) {
            packed = tdefl_compress_mem_to_heap(e->data, (size_t)e->size, &packed_len, (int)flags);
            if (!packed) { err = FULTA_ARC_ERR_CORRUPT; break; }
            if (packed_len >= e->size) { free(packed); packed = NULL; }
        }
        r->method = packed ? 8 : 0;
        r->packed = packed ? packed_len : e->size;
        r->zip64 = force64 || r->size >= 0xFFFFFFFFu || r->packed >= 0xFFFFFFFFu;
        size_t name_len = strlen(r->name);
        uint8_t h[30];
        le32(h, 0x04034b50);
        le16(h + 4, r->zip64 ? 45 : 20);
        le16(h + 6, r->flags);
        le16(h + 8, r->method);
        le16(h + 10, r->time);
        le16(h + 12, r->date);
        le32(h + 14, r->crc);
        le32(h + 18, r->zip64 ? 0xFFFFFFFFu : (uint32_t)r->packed);
        le32(h + 22, r->zip64 ? 0xFFFFFFFFu : (uint32_t)r->size);
        le16(h + 26, (uint32_t)name_len);
        le16(h + 28, (r->zip64 ? 20u : 0u) + (r->mtime ? 9u : 0u));
        put(o, h, sizeof(h));
        put(o, r->name, name_len);
        if (r->zip64) {                                   /* APPNOTE 4.5.3: in the local header, both sizes */
            uint8_t x[20];
            le16(x, 0x0001); le16(x + 2, 16); le64(x + 4, r->size); le64(x + 12, r->packed);
            put(o, x, sizeof(x));
        }
        if (r->mtime) {                                   /* Info-ZIP extended timestamp: mtime, UTC */
            uint8_t x[9];
            le16(x, 0x5455); le16(x + 2, 5); x[4] = 1; le32(x + 5, (uint32_t)r->mtime);
            put(o, x, sizeof(x));
        }
        if (packed) put(o, packed, packed_len);
        else if (e->size && !dir) {
            for (uint64_t at = 0; at < e->size && !o->failed;) {
                size_t n = e->size - at > (1u << 30) ? (1u << 30) : (size_t)(e->size - at);
                put(o, (const uint8_t *)e->data + at, n);
                at += n;
            }
        }
        free(packed);
        if (o->failed) { err = FULTA_ARC_ERR_IO; break; }
        done += e->size;
        if (opt && opt->progress && opt->progress(opt->progress_ctx, done, total)) err = FULTA_ARC_ERR_CANCELLED;
    }
    if (err == FULTA_ARC_OK) {
        uint64_t cd_start = o->at;
        for (size_t i = 0; i < count; ++i) {
            zip_record_t *r = &rec[i];
            size_t name_len = strlen(r->name);
            bool big_offset = force64 || r->offset >= 0xFFFFFFFFu;
            bool big = r->zip64 || big_offset;
            uint8_t x64[28];
            size_t x64_len = 0;
            if (big) {                                    /* only what does not fit, in APPNOTE's order */
                le16(x64, 0x0001);
                x64_len = 4;
                if (r->zip64) { le64(x64 + x64_len, r->size); x64_len += 8; le64(x64 + x64_len, r->packed); x64_len += 8; }
                if (big_offset) { le64(x64 + x64_len, r->offset); x64_len += 8; }
                le16(x64 + 2, (uint32_t)(x64_len - 4));
            }
            uint8_t h[46];
            le32(h, 0x02014b50);
            le16(h + 4, big ? 45 : 20);                   /* made by: MS-DOS, version 2.0 / 4.5 */
            le16(h + 6, big ? 45 : 20);
            le16(h + 8, r->flags);
            le16(h + 10, r->method);
            le16(h + 12, r->time);
            le16(h + 14, r->date);
            le32(h + 16, r->crc);
            le32(h + 20, r->zip64 ? 0xFFFFFFFFu : (uint32_t)r->packed);
            le32(h + 24, r->zip64 ? 0xFFFFFFFFu : (uint32_t)r->size);
            le16(h + 28, (uint32_t)name_len);
            le16(h + 30, (uint32_t)x64_len + (r->mtime ? 9u : 0u));
            le16(h + 32, 0);                              /* comment */
            le16(h + 34, 0);                              /* disk */
            le16(h + 36, 0);                              /* internal attributes */
            le32(h + 38, r->dir ? 0x10u : 0u);            /* external attributes: MS-DOS directory bit */
            le32(h + 42, big_offset ? 0xFFFFFFFFu : (uint32_t)r->offset);
            put(o, h, sizeof(h));
            put(o, r->name, name_len);
            put(o, x64, x64_len);
            if (r->mtime) {
                uint8_t x[9];
                le16(x, 0x5455); le16(x + 2, 5); x[4] = 1; le32(x + 5, (uint32_t)r->mtime);
                put(o, x, sizeof(x));
            }
        }
        uint64_t cd_size = o->at - cd_start;
        bool end64 = force64 || count >= 0xFFFF || cd_size >= 0xFFFFFFFFu || cd_start >= 0xFFFFFFFFu;
        if (end64) {
            uint64_t end64_at = o->at;
            uint8_t z[56];
            le32(z, 0x06064b50);
            le64(z + 4, 44);                              /* the size of what follows this field */
            le16(z + 12, 45); le16(z + 14, 45);
            le32(z + 16, 0); le32(z + 20, 0);
            le64(z + 24, count); le64(z + 32, count);
            le64(z + 40, cd_size); le64(z + 48, cd_start);
            put(o, z, sizeof(z));
            uint8_t l[20];
            le32(l, 0x07064b50); le32(l + 4, 0); le64(l + 8, end64_at); le32(l + 16, 1);
            put(o, l, sizeof(l));
        }
        uint8_t e[22];
        le32(e, 0x06054b50);
        le16(e + 4, 0); le16(e + 6, 0);
        le16(e + 8, end64 ? 0xFFFFu : (uint32_t)count);
        le16(e + 10, end64 ? 0xFFFFu : (uint32_t)count);
        le32(e + 12, end64 ? 0xFFFFFFFFu : (uint32_t)cd_size);
        le32(e + 16, end64 ? 0xFFFFFFFFu : (uint32_t)cd_start);
        le16(e + 20, 0);
        put(o, e, sizeof(e));
        if (o->failed) err = FULTA_ARC_ERR_IO;
    }
    for (size_t i = 0; i < count; ++i) free(rec[i].name);
    free(rec);
    return err;
}

/* ---- 7z ---- */

/* A growing byte buffer for the header. */
typedef struct buf {
    uint8_t *p;
    size_t len, cap;
    bool failed;
} buf_t;

static void b_put(buf_t *b, const void *data, size_t size) {
    if (b->failed) return;
    if (b->len + size > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + size) cap *= 2;
        uint8_t *p = (uint8_t*)realloc(b->p, cap);
        if (!p) { b->failed = true; return; }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->len, data, size);
    b->len += size;
}
static void b_byte(buf_t *b, uint8_t v) { b_put(b, &v, 1); }

/* 7zFormat.txt "UINT64": the first byte's leading 1 bits say how many bytes follow. */
static void b_num(buf_t *b, uint64_t v) {
    uint8_t first = 0, mask = 0x80;
    int i;
    for (i = 0; i < 8; i++) {
        if (v < ((uint64_t)1 << (7 * (i + 1)))) {
            first |= (uint8_t)(v >> (8 * i));
            break;
        }
        first |= mask;
        mask >>= 1;
    }
    b_byte(b, first);
    for (int k = 0; k < i; k++) b_byte(b, (uint8_t)(v >> (8 * k)));
}

static void b_u32(buf_t *b, uint32_t v) { uint8_t x[4]; le32(x, v); b_put(b, x, 4); }
static void b_u64(buf_t *b, uint64_t v) { uint8_t x[8]; le64(x, v); b_put(b, x, 8); }

/* A bit vector, most significant bit first (7zFormat.txt "BIT"). */
static void b_bits(buf_t *b, const bool *bits, size_t n) {
    uint8_t cur = 0;
    for (size_t i = 0; i < n; ++i) {
        if (bits[i]) cur |= (uint8_t)(0x80 >> (i & 7));
        if ((i & 7) == 7) { b_byte(b, cur); cur = 0; }
    }
    if (n & 7) b_byte(b, cur);
}

/* UTF-8 to UTF-16LE, a NUL after. */
static void b_name16(buf_t *b, const char *s) {
    const uint8_t *p = (const uint8_t*)s;
    while (*p) {
        uint32_t cp = *p++;
        if (cp >= 0xF0 && p[0] && p[1] && p[2]) { cp = (cp & 7) << 18 | (p[0] & 0x3Fu) << 12 | (p[1] & 0x3Fu) << 6 | (p[2] & 0x3Fu); p += 3; }
        else if (cp >= 0xE0 && p[0] && p[1]) { cp = (cp & 15) << 12 | (p[0] & 0x3Fu) << 6 | (p[1] & 0x3Fu); p += 2; }
        else if (cp >= 0xC0 && p[0]) { cp = (cp & 31) << 6 | (p[0] & 0x3Fu); p += 1; }
        else if (cp >= 0x80) cp = 0xFFFD;
        if (cp >= 0x10000) {
            uint32_t v = cp - 0x10000;
            uint8_t u[4];
            le16(u, 0xD800 + (v >> 10)); le16(u + 2, 0xDC00 + (v & 0x3FF));
            b_put(b, u, 4);
        } else {
            uint8_t u[2];
            le16(u, cp);
            b_put(b, u, 2);
        }
    }
    b_put(b, "\0\0", 2);
}

/* The non-empty entries, one after another, as the SDK's input stream. */
typedef struct sz_in {
    ISeqInStream vt;
    const fulta_arc_write_entry_t *entries;
    size_t count, index;
    uint64_t at;
} sz_in_t;

static SRes sz_in_read(ISeqInStreamPtr pp, void *buf, size_t *size) {
    sz_in_t *p = (sz_in_t*)(void*)pp;
    size_t want = *size, got = 0;
    while (got < want && p->index < p->count) {
        const fulta_arc_write_entry_t *e = &p->entries[p->index];
        uint64_t left = is_dir(e) ? 0 : e->size - p->at;
        if (left == 0) { p->index++; p->at = 0; continue; }
        size_t n = want - got < left ? want - got : (size_t)left;
        memcpy((uint8_t*)buf + got, (const uint8_t *)e->data + p->at, n);
        got += n;
        p->at += n;
    }
    *size = got;
    return SZ_OK;
}

typedef struct sz_out {
    ISeqOutStream vt;
    out_t *o;
} sz_out_t;

static size_t sz_out_write(ISeqOutStreamPtr pp, const void *data, size_t size) {
    sz_out_t *p = (sz_out_t*)(void*)pp;
    put(p->o, data, size);
    return p->o->failed ? 0 : size;
}

typedef struct sz_progress {
    ICompressProgress vt;
    const fulta_arc_write_options_t *opt;
    uint64_t total;
} sz_progress_t;

static SRes sz_progress(ICompressProgressPtr pp, UInt64 in_size, UInt64 out_size) {
    (void)out_size;
    sz_progress_t *p = (sz_progress_t*)(void*)pp;
    if (p->opt && p->opt->progress && p->opt->progress(p->opt->progress_ctx, in_size, p->total)) return SZ_ERROR_PROGRESS;
    return SZ_OK;
}

static void *sz_alloc(ISzAllocPtr p, size_t size) { (void)p; return malloc(size); }
static void sz_free(ISzAllocPtr p, void *address) { (void)p; free(address); }
static const ISzAlloc SZ_ALLOC = { sz_alloc, sz_free };

static fulta_arc_err_t write_7z(const fulta_arc_write_entry_t *entries, size_t count, int level,
                                   const fulta_arc_write_options_t *opt, out_t *o) {
    /* the signature header, written again at the end with where the header is */
    uint8_t sig[32] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C, 0, 4 };
    put(o, sig, sizeof(sig));
    if (o->failed) return FULTA_ARC_ERR_IO;

    size_t streams = 0;
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) if (entries[i].size && !is_dir(&entries[i])) { streams++; total += entries[i].size; }

    uint8_t props[LZMA_PROPS_SIZE];
    SizeT props_size = sizeof(props);
    uint64_t packed = 0;
    if (streams > 0) {
        uint64_t start = o->at;
        if (level == 0) {                                /* Copy: the bytes as they are */
            uint64_t done = 0;
            for (size_t i = 0; i < count && !o->failed; ++i) {
                if (is_dir(&entries[i])) continue;
                for (uint64_t at = 0; at < entries[i].size && !o->failed;) {
                    size_t n = entries[i].size - at > (1u << 30) ? (1u << 30) : (size_t)(entries[i].size - at);
                    put(o, (const uint8_t *)entries[i].data + at, n);
                    at += n;
                }
                done += entries[i].size;
                if (opt && opt->progress && opt->progress(opt->progress_ctx, done, total)) return FULTA_ARC_ERR_CANCELLED;
            }
            if (o->failed) return FULTA_ARC_ERR_IO;
        } else {
            CLzmaEncHandle enc = LzmaEnc_Create(&SZ_ALLOC);
            if (!enc) return FULTA_ARC_ERR_NOMEM;
            CLzmaEncProps ep;
            LzmaEncProps_Init(&ep);
            ep.level = level;
            ep.reduceSize = total;                       /* no larger a dictionary than the data needs */
            SRes res = LzmaEnc_SetProps(enc, &ep);
            if (res == SZ_OK) res = LzmaEnc_WriteProperties(enc, props, &props_size);
            sz_in_t in = { .vt = { sz_in_read }, .entries = entries, .count = count };
            sz_out_t outs = { .vt = { sz_out_write }, .o = o };
            sz_progress_t prog = { .vt = { sz_progress }, .opt = opt, .total = total };
            if (res == SZ_OK) res = LzmaEnc_Encode(enc, &outs.vt, &in.vt, &prog.vt, &SZ_ALLOC, &SZ_ALLOC);
            LzmaEnc_Destroy(enc, &SZ_ALLOC, &SZ_ALLOC);
            if (o->failed) return FULTA_ARC_ERR_IO;
            if (res == SZ_ERROR_PROGRESS) return FULTA_ARC_ERR_CANCELLED;
            if (res == SZ_ERROR_MEM) return FULTA_ARC_ERR_NOMEM;
            if (res != SZ_OK) return FULTA_ARC_ERR_CORRUPT;
        }
        packed = o->at - start;
    }

    /* the header (7zFormat.txt "Header") */
    buf_t h = {0};
    bool *flags = (bool*)calloc(count ? count : 1, sizeof(bool));
    char **names = (char**)calloc(count ? count : 1, sizeof(char*));
    if (!flags || !names) { free(flags); free(names); return FULTA_ARC_ERR_NOMEM; }
    fulta_arc_err_t err = FULTA_ARC_OK;
    for (size_t i = 0; i < count; ++i) {
        names[i] = stored_name(entries[i].name, is_dir(&entries[i]), false);
        if (!names[i]) err = FULTA_ARC_ERR_NOMEM;
    }
    b_byte(&h, 0x01);                                    /* kHeader */
    if (streams > 0) {
        b_byte(&h, 0x04);                                /* kMainStreamsInfo */
        b_byte(&h, 0x06);                                /* kPackInfo */
        b_num(&h, 0);                                    /* PackPos: right after the signature header */
        b_num(&h, 1);
        b_byte(&h, 0x09); b_num(&h, packed);
        b_byte(&h, 0x00);
        b_byte(&h, 0x07);                                /* kUnPackInfo */
        b_byte(&h, 0x0B); b_num(&h, 1); b_byte(&h, 0);   /* one folder, not external */
        b_num(&h, 1);                                    /* one coder */
        if (level == 0) {
            b_byte(&h, 0x01); b_byte(&h, 0x00);          /* Copy: id 00, no properties */
        } else {
            b_byte(&h, 0x23);                            /* id size 3, has properties */
            b_byte(&h, 0x03); b_byte(&h, 0x01); b_byte(&h, 0x01);   /* LZMA (Methods.txt: 03 01 01) */
            b_num(&h, props_size);
            b_put(&h, props, props_size);
        }
        b_byte(&h, 0x0C); b_num(&h, total);              /* kCodersUnPackSize */
        b_byte(&h, 0x00);
        b_byte(&h, 0x08);                                /* kSubStreamsInfo */
        b_byte(&h, 0x0D); b_num(&h, streams);
        if (streams > 1) {
            b_byte(&h, 0x09);
            size_t seen = 0;
            for (size_t i = 0; i < count; ++i) {
                if (!entries[i].size || is_dir(&entries[i])) continue;
                if (++seen == streams) break;            /* the last is what is left */
                b_num(&h, entries[i].size);
            }
        }
        b_byte(&h, 0x0A); b_byte(&h, 1);                 /* all CRCs defined */
        for (size_t i = 0; i < count; ++i)
            if (entries[i].size && !is_dir(&entries[i])) b_u32(&h, crc32_of(entries[i].data, entries[i].size));
        b_byte(&h, 0x00);
        b_byte(&h, 0x00);                                /* end of StreamsInfo */
    }
    b_byte(&h, 0x05);                                    /* kFilesInfo */
    b_num(&h, count);
    if (streams < count) {
        for (size_t i = 0; i < count; ++i) flags[i] = entries[i].size == 0 || is_dir(&entries[i]);
        b_byte(&h, 0x0E); b_num(&h, (count + 7) / 8); b_bits(&h, flags, count);
        size_t empty = 0;
        for (size_t i = 0; i < count; ++i)                       /* over the streamless items: file or folder */
            if (entries[i].size == 0 || is_dir(&entries[i])) flags[empty++] = !is_dir(&entries[i]);
        b_byte(&h, 0x0F); b_num(&h, (empty + 7) / 8); b_bits(&h, flags, empty);
    }
    {
        buf_t n = {0};
        b_byte(&n, 0);                                   /* not external */
        for (size_t i = 0; i < count && names[i]; ++i) b_name16(&n, names[i]);
        b_byte(&h, 0x11); b_num(&h, n.len); b_put(&h, n.p, n.len);
        if (n.failed) h.failed = true;
        free(n.p);
    }
    size_t timed = 0;
    for (size_t i = 0; i < count; ++i) if (entries[i].mtime) timed++;
    if (timed > 0) {
        buf_t t = {0};
        if (timed == count) {
            b_byte(&t, 1);
        } else {
            b_byte(&t, 0);
            for (size_t i = 0; i < count; ++i) flags[i] = entries[i].mtime != 0;
            b_bits(&t, flags, count);
        }
        b_byte(&t, 0);                                   /* not external */
        for (size_t i = 0; i < count; ++i) {
            if (!entries[i].mtime) continue;
            b_u64(&t, (uint64_t)(entries[i].mtime + 11644473600LL) * 10000000u);   /* FILETIME */
        }
        b_byte(&h, 0x14); b_num(&h, t.len); b_put(&h, t.p, t.len);
        if (t.failed) h.failed = true;
        free(t.p);
    }
    b_byte(&h, 0x00);                                    /* end of FilesInfo */
    b_byte(&h, 0x00);                                    /* end of Header */
    if (h.failed) err = FULTA_ARC_ERR_NOMEM;

    if (err == FULTA_ARC_OK) {
        uint64_t header_at = o->at;
        put(o, h.p, h.len);
        if (o->failed) err = FULTA_ARC_ERR_IO;
        else {
            le64(sig + 12, header_at - 32);              /* NextHeaderOffset: from the end of the signature header */
            le64(sig + 20, h.len);
            le32(sig + 28, crc32_of(h.p, h.len));
            le32(sig + 8, crc32_of(sig + 12, 20));       /* StartHeaderCRC */
            if (o->sink->patch(o->sink->ctx, 0, sig, sizeof(sig))) err = FULTA_ARC_ERR_IO;
        }
    }
    for (size_t i = 0; i < count; ++i) free(names[i]);
    free(names);
    free(flags);
    free(h.p);
    return err;
}

fulta_arc_err_t fulta_arc_write(fulta_arc_format_t format, const fulta_arc_write_entry_t *entries, size_t count,
                                const fulta_arc_write_options_t *options, const fulta_arc_write_sink_t *sink) {
    if (!sink || !sink->write || !sink->patch || (count && !entries)) return FULTA_ARC_ERR_INVALID_ARG;
    for (size_t i = 0; i < count; ++i) {
        if (!entries[i].name) return FULTA_ARC_ERR_INVALID_ARG;
        if (!name_ok((u8str_t){entries[i].name, strlen(entries[i].name)})) return FULTA_ARC_ERR_BAD_NAME;
        if (entries[i].size && !entries[i].data && !is_dir(&entries[i])) return FULTA_ARC_ERR_INVALID_ARG;
    }
    int level = options ? options->level : 6;
    if (level < 0 || level > 9) level = 6;
    out_t o = { .sink = sink };
    switch (format) {
        case FULTA_ARC_FORMAT_ZIP: return write_zip(entries, count, level, options && options->zip64, options, &o);
        case FULTA_ARC_FORMAT_7Z:  return write_7z(entries, count, level, options, &o);
        default:               return FULTA_ARC_ERR_INVALID_ARG;
    }
}
