/* write.c - FultaArc: writing ZIP and 7z archives. MIT.
 * Ported from Rubraview's src/core/archive_write.c (MIT, D-68 there; written by Rubraview's owner-side session from
 * PKWARE's APPNOTE and the LZMA SDK's DOC/7zFormat.txt — no clean room is needed for these formats) to FultaArc's
 * API (D-005 #6); folders added. Compressors: miniz's tdefl (MIT), the LZMA SDK's LzmaEnc (public domain). */
#include "../core/internal.h"

#include "miniz.h"
#include "LzmaEnc.h"
#include "Lzma2Enc.h"
#include "bzlib.h"
#include "Ppmd7.h"
#include "Ppmd8.h"
#define ZSTD_STATIC_LINKING_ONLY   /* ZSTD_customMem: zstd allocates through the writer's allocator */
#include "zstd.h"
#include "Delta.h"
#include "Bra.h"
#include "Bcj2.h"

#include "proven.h"

#include <limits.h>
#include <stdlib.h>

/*
 * ZIP from PKWARE's APPNOTE.TXT (6.3.x): 4.3.7 local file header, 4.3.12 central directory header,
 * 4.3.14-4.3.16 ZIP64 end records, 4.5.3 the ZIP64 extra field, 4.4.4 bit 11 (UTF-8 names), and Info-ZIP's
 * extended timestamp extra field (0x5455). 7z from the LZMA SDK's DOC/7zFormat.txt (public domain).
 */

/* ---- little-endian output ---- */

typedef struct out {
    proven_allocator_t alloc;   /* the writer's (D-038): every buffer of this write lives here */
    const fulta_arc_write_sink_t *sink;
    uint64_t at;            /* bytes appended so far */
    bool failed;
    const char *hdr_pw;     /* 7z: encrypt the header with this password (encrypt_names), else NULL */
} out_t;

static void put(out_t *o, const void *data, size_t size) {
    if (o->failed || size == 0) return;
    if (o->sink->write(o->sink->ctx, data, size)) { o->failed = true; return; }
    o->at += size;
}

/* The writer's allocator (D-038): the options' fulta_arc_allocator_t (the bridge points into the options, which live
 * as long as the write), else proven's heap. The entry points refuse a partial set first (write_alloc_ok). */
proven_allocator_t fa_write_alloc(const fulta_arc_write_options_t *opt) {
    proven_allocator_t a = proven_heap_allocator();
    if (opt && fa_allocator_pick(&opt->allocator, &a)) a = proven_heap_allocator();
    return a;
}
bool fa_write_alloc_ok(const fulta_arc_write_options_t *opt) {
    proven_allocator_t a;
    return !opt || fa_allocator_pick(&opt->allocator, &a) == FULTA_ARC_OK;
}

static void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void le64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i)); }

static uint32_t crc32_of(const uint8_t *data, uint64_t size) {
    uint32_t crc = 0;
    while (size > 0) {
        size_t n = size > (1u << 30) ? (1u << 30) : (size_t)size;
        crc = fa_crc32(crc, fa_bytes(data, n));
        data += n;
        size -= n;
    }
    return crc;
}

/* ---- names ---- */

/* A name that may be stored: not empty or absolute, no drive, no "." or ".." part, no empty part, no control
 * character. A folder's name may end in '/'. */
static bool name_ok(proven_u8str_view_t name) {
    if (name.size && (name.ptr[name.size - 1] == '/' || name.ptr[name.size - 1] == '\\')) name.size--;
    if (name.size == 0 || name.ptr[0] == '/' || name.ptr[0] == '\\') return false;
    if (name.size >= 2 && name.ptr[1] == ':') return false;                     /* C:... */
    size_t part = 0;
    for (size_t i = 0; i <= name.size; ++i) {
        proven_byte_t c = i < name.size ? name.ptr[i] : (proven_byte_t)'/';
        if (i < name.size && c < 0x20) return false;
        if (c == '/' || c == '\\') {
            size_t len = i - part;
            if (len == 0 && i < name.size) return false;                       /* "a//b" */
            if (len == 2 && name.ptr[part] == '.' && name.ptr[part + 1] == '.') return false;
            if (len == 1 && name.ptr[part] == '.') return false;
            part = i + 1;
        }
    }
    return name.ptr[name.size - 1] != '/' && name.ptr[name.size - 1] != '\\';
}

/* The name as stored: '/' between folders; a folder's ZIP name ends in '/', its 7z name does not. */
static char *stored_name(proven_allocator_t a, const char *name, bool dir, bool zip) {
    size_t len = strlen(name);
    while (len && (name[len - 1] == '/' || name[len - 1] == '\\')) len--;
    char *s = fa_alloc(a, len + 2);
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
    uint64_t size, packed, offset;  /* offset: of the local header, within its disk */
    uint32_t disk;                  /* the volume holding the local header (0 unless split) */
    uint32_t crc, ext_attr;
    uint16_t method, time, date, flags, made_by_host;
    bool zip64;
    char *name;                 /* owned; raw bytes (may be non-ASCII on a copy), not necessarily NUL-clean */
    size_t name_len;
    int64_t mtime;
    bool dir;
    bool encrypted;             /* WinZip AES: `method` is 99, the real method is `real_method` in a 0x9901 extra */
    uint16_t real_method;
    uint8_t aes_strength;       /* 1 = AES-128, 2 = AES-192, 3 = AES-256 */
} zip_record_t;

/* The WinZip AES "AE" extra field (0x9901), 11 bytes, in the local and central headers of an encrypted entry. */
static void put_aes_extra(out_t *o, const zip_record_t *r) {
    uint8_t x[11];
    le16(x, 0x9901); le16(x + 2, 7);
    le16(x + 4, 2);                       /* AE-2 (no plaintext CRC) */
    x[6] = 'A'; x[7] = 'E';
    x[8] = r->aes_strength;
    le16(x + 9, r->real_method);
    put(o, x, sizeof x);
}
static uint16_t zip_version_needed(const zip_record_t *r) {
    uint16_t v = r->zip64 ? 45 : 20;
    if (r->encrypted && v < 51) v = 51;   /* AES needs 5.1 */
    return v;
}
static uint64_t zip_local_header(out_t *o, zip_record_t *r);

/* Where a record of `len` bytes starts: on split output (src/write/volumes.c) in a fresh volume when it would not fit
 * whole in this one, after settling the volumes no patch can reach any more (patches go only to the entry being
 * written, whose header is not out yet). Gives the disk and the offset within it. */
static void zip_here(out_t *o, uint64_t len, uint32_t *disk, uint64_t *off) {
    fa_vol_t *v = fa_vol_of(o->sink);
    if (!v) { *disk = 0; *off = o->at; return; }
    if (fa_vol_settle(v, o->at)) o->failed = true;
    if (fa_vol_room(v) < len) fa_vol_next(v);
    *disk = fa_vol_disk(v);
    *off = fa_vol_disk_offset(v);
}

/* A split set starts with the spanning signature PK\7\8 (APPNOTE 8.5.3); write_zip_central turns it into the
 * single-segment marker PK00 if the set ends in one volume. */
static void zip_begin(out_t *o) {
    static const uint8_t span[4] = {0x50, 0x4B, 0x07, 0x08};
    if (fa_vol_of(o->sink) && o->at == 0) put(o, span, 4);
}

/* WinZip-AES strength (1/2/3) for a ZIP password + scheme, or 0 if the scheme is not a ZIP one. */
static const size_t aes_salt_len[4] = {0, 8, 12, 16}, aes_key_len[4] = {0, 16, 24, 32};
static int zip_aes_strength(fulta_arc_encrypt_t enc) {
    switch (enc) {
        case FULTA_ARC_ENCRYPT_DEFAULT: case FULTA_ARC_ENCRYPT_ZIP_AES256: return 3;
        case FULTA_ARC_ENCRYPT_ZIP_AES128: return 1;
        case FULTA_ARC_ENCRYPT_ZIP_AES192: return 2;
        default: return 0;   /* FULTA_ARC_ENCRYPT_7Z_AES256 is not valid for ZIP */
    }
}

/* Encrypt a compressed/stored body into a freshly allocated WinZip-AES blob (salt | verifier | ciphertext | mac).
 * Fills the record's encryption fields. Returns the blob (caller frees) and its length in *out_len, or NULL. */
/* A fresh random salt (`aes_salt_len[strength]` bytes) and the keys derived from it: k = encryption key | HMAC key |
 * 2-byte password verifier (PBKDF2-HMAC-SHA1, 1000 iterations). false => no entropy: refuse to encrypt. */
static bool zip_aes_keys(const char *pw, size_t pwlen, int strength, uint8_t *salt, uint8_t k[66]) {
    size_t sl = aes_salt_len[strength], kl = aes_key_len[strength];
    if (!fa_random_bytes(salt, sl)) return false;
    fa_pbkdf2_sha1((const uint8_t *)pw, pwlen, salt, sl, 1000, k, 2 * kl + 2);
    return true;
}

/* The record's fields for a WinZip-AES (AE-2) entry: method 99 with the real method in the 0x9901 extra, bit 0, no
 * plaintext CRC. */
static void zip_aes_mark(zip_record_t *r, int strength) {
    r->encrypted = true;
    r->real_method = r->method;
    r->method = 99;
    r->aes_strength = (uint8_t)strength;
    r->flags |= 1;
    r->crc = 0;                                                        /* AE-2: no plaintext CRC */
}

static uint8_t *zip_aes_seal(proven_allocator_t a, const char *pw, size_t pwlen, int strength, const uint8_t *body, size_t blen,
                             zip_record_t *r, size_t *out_len, fulta_arc_err_t *err) {
    size_t sl = aes_salt_len[strength], kl = aes_key_len[strength];
    uint8_t *blob = fa_alloc(a, sl + 2 + blen + 10);
    if (!blob) { *err = FULTA_ARC_ERR_NOMEM; return NULL; }
    uint8_t k[66];
    if (!zip_aes_keys(pw, pwlen, strength, blob, k)) { fa_release(a, blob); *err = FULTA_ARC_ERR_IO; return NULL; }   /* no entropy */
    blob[sl] = k[2 * kl]; blob[sl + 1] = k[2 * kl + 1];                 /* 2-byte password verifier */
    if (blen) memcpy(blob + sl + 2, body, blen);
    fa_aes_ctr_le1_xor(k, kl, blob + sl + 2, blen);                    /* AES-CTR */
    uint8_t mac[20];
    fa_hmac_sha1(k + kl, kl, blob + sl + 2, blen, mac);                /* HMAC-SHA1 authentication, 10 bytes */
    memcpy(blob + sl + 2 + blen, mac, 10);
    memset(k, 0, sizeof k);
    zip_aes_mark(r, strength);
    *out_len = sl + 2 + blen + 10;
    return blob;
}

static bool ascii(const char *s) {
    for (; *s; ++s) if ((unsigned char)*s >= 0x80) return false;
    return true;
}

/* The central directory and end records, from the per-entry records written in the first pass. Shared by the plain
 * writer and the editing writer (src/write/edit.c). */
static fulta_arc_err_t write_zip_central(out_t *o, const zip_record_t *rec, size_t count, bool force64) {
    uint64_t cd_start = o->at;               /* logical, for the size */
    uint32_t cd_disk = 0, disk = 0;
    uint64_t cd_off = 0, off = 0, on_disk = 0;   /* records on the disk the end records land on */
    for (size_t i = 0; i < count; ++i) {
        const zip_record_t *r = &rec[i];
        bool big_offset = force64 || r->offset >= 0xFFFFFFFFu;
        bool big_disk = r->disk >= 0xFFFFu;
        bool big = r->zip64 || big_offset || big_disk;
        uint8_t x64[32];
        size_t x64_len = 0;
        if (big) {                                    /* only what does not fit, in APPNOTE's order */
            le16(x64, 0x0001);
            x64_len = 4;
            if (r->zip64) { le64(x64 + x64_len, r->size); x64_len += 8; le64(x64 + x64_len, r->packed); x64_len += 8; }
            if (big_offset) { le64(x64 + x64_len, r->offset); x64_len += 8; }
            if (big_disk) { le32(x64 + x64_len, r->disk); x64_len += 4; }
            le16(x64 + 2, (uint32_t)(x64_len - 4));
        }
        size_t extra = x64_len + (r->mtime ? 9u : 0u) + (r->encrypted ? 11u : 0u);
        uint32_t d; uint64_t at;
        zip_here(o, 46 + r->name_len + extra, &d, &at);   /* a central record stays whole on one volume */
        if (i == 0) { cd_start = o->at; cd_disk = d; cd_off = at; }
        if (d != disk) { disk = d; on_disk = 0; }
        on_disk++;
        uint16_t ver = zip_version_needed(r);
        uint8_t h[46];
        le32(h, 0x02014b50);
        le16(h + 4, ((uint32_t)r->made_by_host << 8) | (uint32_t)(big ? 45u : ver));   /* made by: host + version */
        le16(h + 6, big ? 45 : ver);
        le16(h + 8, r->flags);
        le16(h + 10, r->method);
        le16(h + 12, r->time);
        le16(h + 14, r->date);
        le32(h + 16, r->crc);
        le32(h + 20, r->zip64 ? 0xFFFFFFFFu : (uint32_t)r->packed);
        le32(h + 24, r->zip64 ? 0xFFFFFFFFu : (uint32_t)r->size);
        le16(h + 28, (uint32_t)r->name_len);
        le16(h + 30, (uint32_t)extra);
        le16(h + 32, 0);                              /* comment */
        le16(h + 34, big_disk ? 0xFFFFu : r->disk);   /* the disk the local header is on */
        le16(h + 36, 0);                              /* internal attributes */
        le32(h + 38, r->ext_attr);                    /* external attributes (preserved on copy) */
        le32(h + 42, big_offset ? 0xFFFFFFFFu : (uint32_t)r->offset);
        put(o, h, sizeof(h));
        put(o, r->name, r->name_len);
        put(o, x64, x64_len);
        if (r->mtime) {
            uint8_t x[9];
            le16(x, 0x5455); le16(x + 2, 5); x[4] = 1; le32(x + 5, (uint32_t)r->mtime);
            put(o, x, sizeof(x));
        }
        if (r->encrypted) put_aes_extra(o, r);
    }
    uint64_t cd_size = o->at - cd_start;
    uint32_t end_disk;
    zip_here(o, 56 + 20 + 22, &end_disk, &off);       /* the end records together, on the last volume */
    if (count == 0) { cd_disk = end_disk; cd_off = off; }
    if (end_disk != disk) on_disk = 0;
    bool end64 = force64 || count >= 0xFFFF || cd_size >= 0xFFFFFFFFu || cd_off >= 0xFFFFFFFFu ||
                 end_disk >= 0xFFFF || cd_disk >= 0xFFFF;
    if (end64) {
        uint64_t end64_off = off;
        uint8_t z[56];
        le32(z, 0x06064b50);
        le64(z + 4, 44);                              /* the size of what follows this field */
        le16(z + 12, 45); le16(z + 14, 45);
        le32(z + 16, end_disk); le32(z + 20, cd_disk);
        le64(z + 24, on_disk); le64(z + 32, count);
        le64(z + 40, cd_size); le64(z + 48, cd_off);
        put(o, z, sizeof(z));
        uint8_t l[20];
        le32(l, 0x07064b50); le32(l + 4, end_disk); le64(l + 8, end64_off); le32(l + 16, end_disk + 1);
        put(o, l, sizeof(l));
    }
    uint8_t e[22];
    le32(e, 0x06054b50);
    le16(e + 4, end64 ? 0xFFFFu : end_disk);
    le16(e + 6, end64 ? 0xFFFFu : cd_disk);
    le16(e + 8, end64 ? 0xFFFFu : (uint32_t)on_disk);
    le16(e + 10, end64 ? 0xFFFFu : (uint32_t)count);
    le32(e + 12, end64 ? 0xFFFFFFFFu : (uint32_t)cd_size);
    le32(e + 16, end64 ? 0xFFFFFFFFu : (uint32_t)cd_off);
    le16(e + 20, 0);
    put(o, e, sizeof(e));
    if (o->failed) return FULTA_ARC_ERR_IO;
    fa_vol_t *v = fa_vol_of(o->sink);
    if (v && fa_vol_count(v) == 1) {                  /* one volume after all: the single-segment marker */
        static const uint8_t pk00[4] = {0x50, 0x4B, 0x30, 0x30};
        if (o->sink->patch(o->sink->ctx, 0, pk00, 4)) return FULTA_ARC_ERR_IO;
    }
    return FULTA_ARC_OK;
}

/* The vendored encoders' own allocations, through the writer's allocator (D-038): the LZMA SDK's ISzAlloc, bzip2's
 * bzalloc/bzfree and zstd's customMem each carry it (a pointer to an allocator that outlives the encoder). */
typedef struct fa_isz { ISzAlloc vt; proven_allocator_t a; } fa_isz_t;
static void *isz_alloc(ISzAllocPtr p, size_t size) { return fa_alloc(((const fa_isz_t *)(const void *)p)->a, size); }
static void isz_free(ISzAllocPtr p, void *address) { fa_release(((const fa_isz_t *)(const void *)p)->a, address); }
static fa_isz_t isz_of(proven_allocator_t a) { return (fa_isz_t){ .vt = { isz_alloc, isz_free }, .a = a }; }

static void *bz_alloc_cb(void *opaque, int n, int m) {
    if (n < 0 || m < 0) return NULL;
    return fa_alloc(*(const proven_allocator_t *)opaque, (size_t)n * (size_t)m);
}
static void bz_free_cb(void *opaque, void *p) { fa_release(*(const proven_allocator_t *)opaque, p); }
static void bz_hook(bz_stream *bz, const proven_allocator_t *a) {
    bz->bzalloc = bz_alloc_cb; bz->bzfree = bz_free_cb; bz->opaque = (void *)(uintptr_t)a;
}

static void *zs_alloc_cb(void *opaque, size_t n) { return fa_alloc(*(const proven_allocator_t *)opaque, n); }
static void zs_free_cb(void *opaque, void *p) { fa_release(*(const proven_allocator_t *)opaque, p); }
static ZSTD_CCtx *zstd_cctx(const proven_allocator_t *a) {
    ZSTD_customMem m = { zs_alloc_cb, zs_free_cb, (void *)(uintptr_t)a };
    return ZSTD_createCCtx_advanced(m);
}

/* miniz's deflate into a growing block of `a`: the compressor is ours (tdefl_compress_mem_to_heap would take it and
 * the output from the C library). */
typedef struct defl_mem { proven_allocator_t a; uint8_t *p; size_t len, cap; bool oom; } defl_mem_t;
static mz_bool defl_mem_put(const void *buf, int len, void *user) {
    defl_mem_t *d = user;
    if (len <= 0) return MZ_TRUE;
    if (d->len + (size_t)len > d->cap) {
        size_t nc = d->cap ? d->cap : 4096;
        while (nc < d->len + (size_t)len) nc *= 2;
        uint8_t *np = fa_resize(d->a, d->p, d->cap, nc);
        if (!np) { d->oom = true; return MZ_FALSE; }
        d->p = np; d->cap = nc;
    }
    memcpy(d->p + d->len, buf, (size_t)len);
    d->len += (size_t)len;
    return MZ_TRUE;
}

/* ---- a growable IByteOut for the PPMd range encoder (PPMd can expand, so there is no fixed output bound) ---- */
typedef struct byteout {
    IByteOut vt;
    proven_allocator_t alloc;
    uint8_t *buf;
    size_t len, cap;
    bool oom;
} byteout_t;

static void byteout_write(IByteOutPtr pp, Byte b) {
    byteout_t *o = (byteout_t *)((char *)pp - offsetof(byteout_t, vt));
    if (o->oom) return;
    if (o->len == o->cap) {
        size_t nc = o->cap ? o->cap * 2 : 4096;
        uint8_t *nb = fa_resize(o->alloc, o->buf, o->cap, nc);
        if (!nb) { o->oom = true; return; }
        o->buf = nb; o->cap = nc;
    }
    o->buf[o->len++] = b;
}

/* 7-Zip's genuine ZIP-PPMd (var.I) parameters by level, measured from 7z 25.01 (-mm=PPMd -mx=N):
   order = level+3, memory ramps 1..32 MB, restore switches to CUT_OFF at level 7. Encoded into the 2-byte
   header the reader parses at codecs.c (fa_dec_ppmd8_zip): v = (order-1) | ((memMB-1)<<4) | (restore<<12). */
static void ppmd_params_for_level(int level, unsigned *order, unsigned *memMB, unsigned *restore) {
    static const unsigned mem_by_level[10] = {1, 1, 2, 4, 8, 16, 24, 32, 32, 32};
    int l = level < 1 ? 1 : level > 9 ? 9 : level;
    *order = (unsigned)(l + 3);                 /* 4..12, within the reader's [2,16] */
    *memMB = mem_by_level[l];
    *restore = l >= 7 ? 1u : 0u;                /* 0 = RESTART, 1 = CUT_OFF (FREEZE=2 needs unbuilt support) */
}

/* 7-Zip's genuine 7z-PPMd (var.H) parameters by level, measured from 7z 25.01 (-m0=PPMd -mx=N): order 4..32,
   memory a power of two 2^20..2^25. Stored as the 5-byte coder property [order, memSize LE32] the reader parses
   at codecs.c (fa_dec_ppmd7z). */
static void ppmd7_params_for_level(int level, unsigned *order, uint32_t *mem) {
    static const unsigned order_by_level[10] = {0, 4, 4, 5, 6, 6, 8, 16, 24, 32};
    static const unsigned log2_by_level[10]  = {0, 20, 21, 22, 23, 24, 24, 25, 25, 25};
    int l = level < 1 ? 1 : level > 9 ? 9 : level;
    *order = order_by_level[l];                 /* within the reader's [PPMD7_MIN_ORDER, PPMD7_MAX_ORDER] */
    *mem = (uint32_t)1 << log2_by_level[l];
}

/* Compress one in-memory buffer for a ZIP entry with method `m`. On return either *packed is a heap buffer holding
   the stored body (including any per-method header, e.g. ZIP LZMA's 4-byte head + 5 props) and *method_out is the
   ZIP method number, or *packed is NULL meaning "store the raw data" (*method_out = 0). DEFLATE falls back to store
   when the input is empty, level 0, or incompressible; STORE always stores; BZIP2/LZMA honour the chosen method.
   `zf` is the precomputed tdefl flags for DEFLATE. */
static fulta_arc_err_t zip_encode_body(proven_allocator_t a, fulta_arc_method_t m, int level, mz_uint zf,
                                       const void *data, uint64_t size,
                                       void **packed, size_t *packed_len, uint16_t *method_out) {
    *packed = NULL; *packed_len = 0; *method_out = 0;
    if (size == 0 || m == FULTA_ARC_METHOD_STORE) return FULTA_ARC_OK;

    if (m == FULTA_ARC_METHOD_DEFLATE) {
        if (level <= 0 || size > SIZE_MAX) return FULTA_ARC_OK;    /* store */
        tdefl_compressor *comp = fa_alloc(a, sizeof *comp);
        if (!comp) return FULTA_ARC_ERR_NOMEM;
        defl_mem_t d = { .a = a };
        tdefl_status st = tdefl_init(comp, defl_mem_put, &d, (int)zf);
        if (st == TDEFL_STATUS_OKAY) st = tdefl_compress_buffer(comp, data, (size_t)size, TDEFL_FINISH);
        fa_release(a, comp);
        if (st != TDEFL_STATUS_DONE) { fa_release(a, d.p); return d.oom ? FULTA_ARC_ERR_NOMEM : FULTA_ARC_ERR_CORRUPT; }
        if (d.len >= size) { fa_release(a, d.p); return FULTA_ARC_OK; }   /* store: no gain */
        *packed = d.p; *packed_len = d.len; *method_out = 8;
        return FULTA_ARC_OK;
    }

    if (m == FULTA_ARC_METHOD_BZIP2) {
        if (size > 0xFFFFFFFFu) return FULTA_ARC_ERR_UNSUPPORTED;   /* one 32-bit input, as before */
        unsigned dlen = (unsigned)size + (unsigned)(size / 100) + 600;  /* libbzip2's recommended bound */
        char *dst = fa_alloc(a, dlen);
        if (!dst) return FULTA_ARC_ERR_NOMEM;
        int bs = level < 1 ? 1 : level > 9 ? 9 : level;
        bz_stream bz; memset(&bz, 0, sizeof bz);
        bz_hook(&bz, &a);
        if (BZ2_bzCompressInit(&bz, bs, 0, 0) != BZ_OK) { fa_release(a, dst); return FULTA_ARC_ERR_NOMEM; }
        bz.next_in = (char *)(uintptr_t)data; bz.avail_in = (unsigned)size;
        bz.next_out = dst; bz.avail_out = dlen;
        int rc;
        do rc = BZ2_bzCompress(&bz, BZ_FINISH); while (rc == BZ_FINISH_OK && bz.avail_out);
        unsigned got = dlen - bz.avail_out;
        BZ2_bzCompressEnd(&bz);
        if (rc != BZ_STREAM_END) { fa_release(a, dst); return FULTA_ARC_ERR_CORRUPT; }
        *packed = dst; *packed_len = got; *method_out = 12;
        return FULTA_ARC_OK;
    }

    if (m == FULTA_ARC_METHOD_LZMA) {
        if (size > SIZE_MAX - 4096) return FULTA_ARC_ERR_UNSUPPORTED;
        CLzmaEncProps ep; LzmaEncProps_Init(&ep);
        ep.level = level < 1 ? 1 : level > 9 ? 9 : level;
        ep.reduceSize = size;                                       /* dictionary no larger than the data needs */
        size_t cap = (size_t)size + size / 2 + 4096;
        uint8_t *dst = fa_alloc(a, cap);
        if (!dst) return FULTA_ARC_ERR_NOMEM;
        /* body layout the reader expects (zip.c:502): [verMaj, verMin, propsLen LE16 = 5] then the 5 LZMA props. */
        dst[0] = 9; dst[1] = 20; dst[2] = 5; dst[3] = 0;
        uint8_t props[LZMA_PROPS_SIZE]; SizeT props_size = sizeof props;
        SizeT dlen = cap - 4 - LZMA_PROPS_SIZE;
        fa_isz_t isz = isz_of(a);
        SRes res = LzmaEncode(dst + 4 + LZMA_PROPS_SIZE, &dlen, data, (SizeT)size,
                              &ep, props, &props_size, 0 /* no end marker: decoded by size */, NULL,
                              &isz.vt, &isz.vt);
        if (res != SZ_OK || props_size != LZMA_PROPS_SIZE) { fa_release(a, dst); return res == SZ_ERROR_MEM ? FULTA_ARC_ERR_NOMEM : FULTA_ARC_ERR_CORRUPT; }
        memcpy(dst + 4, props, LZMA_PROPS_SIZE);
        *packed = dst; *packed_len = 4 + LZMA_PROPS_SIZE + (size_t)dlen; *method_out = 14;
        return FULTA_ARC_OK;
    }

    if (m == FULTA_ARC_METHOD_PPMD) {
        /* ZIP method 98 = WinZip/7-Zip PPMd var.I: 2-byte param header then the Ppmd8 range-coded stream, decoded
           by uncompressed size (no end marker). Mirrors fa_dec_ppmd8_zip. */
        unsigned order, memMB, restore;
        ppmd_params_for_level(level, &order, &memMB, &restore);
        byteout_t bo = {.vt = {byteout_write}, .alloc = a};
        /* the 2-byte header first */
        uint16_t v = (uint16_t)((order - 1) | ((memMB - 1) << 4) | (restore << 12));
        byteout_write(&bo.vt, (Byte)(v & 0xFF));
        byteout_write(&bo.vt, (Byte)(v >> 8));
        fa_isz_t isz = isz_of(a);
        CPpmd8 p;
        Ppmd8_Construct(&p);
        if (!Ppmd8_Alloc(&p, (UInt32)memMB << 20, &isz.vt)) { fa_release(a, bo.buf); return FULTA_ARC_ERR_NOMEM; }
        p.Stream.Out = &bo.vt;
        Ppmd8_Init_RangeEnc(&p);
        Ppmd8_Init(&p, order, restore);
        const uint8_t *src = data;
        for (uint64_t i = 0; i < size; i++) Ppmd8_EncodeSymbol(&p, src[i]);
        Ppmd8_EncodeSymbol(&p, -1);                 /* end marker: 7-Zip's ZIP-PPMd decoder reads to it, not by size */
        Ppmd8_Flush_RangeEnc(&p);
        Ppmd8_Free(&p, &isz.vt);
        if (bo.oom) { fa_release(a, bo.buf); return FULTA_ARC_ERR_NOMEM; }
        *packed = bo.buf; *packed_len = bo.len; *method_out = 98;
        return FULTA_ARC_OK;
    }

    if (m == FULTA_ARC_METHOD_ZSTD) {
        /* ZIP method 93 = Zstandard: a raw zstd frame (no WinZip wrapper); the reader feeds it straight to
           ZSTD_decompressStream. Map level 1..9 onto a zstd level capped at 19 (higher costs memory for no real gain). */
        static const int z_by_level[10] = {0, 1, 3, 6, 9, 12, 15, 17, 19, 19};
        int l = level < 1 ? 1 : level > 9 ? 9 : level;
        size_t bound = ZSTD_compressBound((size_t)size);
        uint8_t *dst = fa_alloc(a, bound ? bound : 1);
        if (!dst) return FULTA_ARC_ERR_NOMEM;
        ZSTD_CCtx *cc = zstd_cctx(&a);
        if (!cc) { fa_release(a, dst); return FULTA_ARC_ERR_NOMEM; }
        size_t r = ZSTD_compressCCtx(cc, dst, bound, data, (size_t)size, z_by_level[l]);
        ZSTD_freeCCtx(cc);
        if (ZSTD_isError(r)) { fa_release(a, dst); return FULTA_ARC_ERR_CORRUPT; }
        *packed = dst; *packed_len = r; *method_out = 93;
        return FULTA_ARC_OK;
    }

    return FULTA_ARC_ERR_UNSUPPORTED;
}

/* Resolve a requested method (DEFAULT -> the format's historical choice) to a concrete method, and reject a method
   that can never apply to the format. INVALID_ARG = impossible pairing (a caller error). The caller then checks
   whether the concrete method is implemented for its route (UNSUPPORTED until RFC-0004 wires it). */
static fulta_arc_err_t method_concrete(fulta_arc_format_t fmt, int level, fulta_arc_method_t in,
                                       fulta_arc_method_t *out) {
    if (fmt == FULTA_ARC_FORMAT_ZIP) {
        switch (in) {
            case FULTA_ARC_METHOD_DEFAULT:  *out = FULTA_ARC_METHOD_DEFLATE; return FULTA_ARC_OK;
            case FULTA_ARC_METHOD_STORE: case FULTA_ARC_METHOD_DEFLATE: case FULTA_ARC_METHOD_BZIP2:
            case FULTA_ARC_METHOD_LZMA:  case FULTA_ARC_METHOD_PPMD:    case FULTA_ARC_METHOD_ZSTD:
                *out = in; return FULTA_ARC_OK;
            default: return FULTA_ARC_ERR_INVALID_ARG;      /* LZMA2 is not a ZIP method */
        }
    }
    if (fmt == FULTA_ARC_FORMAT_7Z) {
        switch (in) {
            case FULTA_ARC_METHOD_DEFAULT: *out = (level == 0) ? FULTA_ARC_METHOD_STORE : FULTA_ARC_METHOD_LZMA; return FULTA_ARC_OK;
            case FULTA_ARC_METHOD_STORE: case FULTA_ARC_METHOD_DEFLATE: case FULTA_ARC_METHOD_BZIP2:
            case FULTA_ARC_METHOD_LZMA:  case FULTA_ARC_METHOD_LZMA2:   case FULTA_ARC_METHOD_PPMD:
                *out = in; return FULTA_ARC_OK;
            default: return FULTA_ARC_ERR_INVALID_ARG;      /* zstd is not a stock-7z method (D-018) */
        }
    }
    return FULTA_ARC_ERR_INVALID_ARG;
}

/* 7z content filters (RFC-0004 phase 6): Delta and the branch converters chain before the main coder. BCJ2 (a
 * multi-output filter) is phase 7, still UNSUPPORTED. ZIP ignores filters. */
static fulta_arc_err_t filter_check(fulta_arc_format_t fmt, const fulta_arc_write_options_t *opt) {
    if (fmt != FULTA_ARC_FORMAT_7Z || !opt) return FULTA_ARC_OK;
    switch (opt->filter) {
        case FULTA_ARC_FILTER_NONE:
            return FULTA_ARC_OK;
        case FULTA_ARC_FILTER_DELTA:
            return opt->filter_delta_dist > 256 ? FULTA_ARC_ERR_INVALID_ARG : FULTA_ARC_OK;   /* 0 -> 1 */
        case FULTA_ARC_FILTER_BCJ_X86: case FULTA_ARC_FILTER_ARM: case FULTA_ARC_FILTER_ARM64:
        case FULTA_ARC_FILTER_PPC: case FULTA_ARC_FILTER_SPARC: case FULTA_ARC_FILTER_IA64:
        case FULTA_ARC_FILTER_ARMT: case FULTA_ARC_FILTER_RISCV:
        case FULTA_ARC_FILTER_BCJ2:                      /* phase 7: multi-output folder (BCJ2 + sub-stream coders) */
            return FULTA_ARC_OK;
        default:
            return FULTA_ARC_ERR_INVALID_ARG;
    }
}

static fulta_arc_err_t write_zip(const fulta_arc_write_entry_t *entries, size_t count, int level, bool force64,
                                    const fulta_arc_write_options_t *opt, out_t *o) {
    const char *pw = opt ? opt->password : NULL;
    size_t pwlen = pw ? strlen(pw) : 0;
    int enc_strength = 0;
    if (pw) {
        enc_strength = zip_aes_strength(opt->encryption);
        if (!enc_strength) return FULTA_ARC_ERR_INVALID_ARG;   /* a 7z scheme for a ZIP */
    }
    fulta_arc_method_t m;
    fulta_arc_err_t me = method_concrete(FULTA_ARC_FORMAT_ZIP, level, opt ? opt->method : FULTA_ARC_METHOD_DEFAULT, &m);
    if (me) return me;                                         /* INVALID_ARG: method cannot apply to ZIP */
    zip_begin(o);
    zip_record_t *rec = fa_zalloc(o->alloc, count ? count : 1, sizeof(zip_record_t));
    if (!rec) return FULTA_ARC_ERR_NOMEM;
    fulta_arc_err_t err = FULTA_ARC_OK;
    uint64_t total = 0, done = 0;
    for (size_t i = 0; i < count; ++i) total += entries[i].size;
    mz_uint flags = tdefl_create_comp_flags_from_zip_params(level, -15, MZ_DEFAULT_STRATEGY);
    for (size_t i = 0; i < count && err == FULTA_ARC_OK; ++i) {
        const fulta_arc_write_entry_t *e = &entries[i];
        zip_record_t *r = &rec[i];
        bool dir = is_dir(e);
        r->name = stored_name(o->alloc, e->name, dir, true);
        r->dir = dir;
        if (!r->name) { err = FULTA_ARC_ERR_NOMEM; break; }
        r->name_len = strlen(r->name);
        r->made_by_host = 0;                              /* MS-DOS */
        r->ext_attr = dir ? 0x10u : 0u;                   /* MS-DOS directory bit */
        r->size = dir ? 0 : e->size;
        r->crc = dir ? 0 : crc32_of(e->data, e->size);
        r->mtime = e->mtime;
        dos_time(e->mtime, &r->time, &r->date);
        r->flags = ascii(r->name) ? 0 : 0x0800;
        r->offset = o->at;
        /* compress per the chosen method; a NULL result means store (STORE, empty, or no gain) */
        void *packed = NULL;
        size_t packed_len = 0;
        uint16_t zmethod = 0;
        if (!dir && e->size > 0) {
            err = zip_encode_body(o->alloc, m, level, flags, e->data, e->size, &packed, &packed_len, &zmethod);
            if (err) break;
        }
        r->method = zmethod;
        r->packed = packed ? packed_len : e->size;
        /* WinZip AES: seal the body (salt | verifier | ciphertext | mac) and rewrite method/flags/crc/packed */
        uint8_t *blob = NULL; size_t blob_len = 0;
        if (enc_strength && !dir) {
            const uint8_t *body = packed ? (const uint8_t *)packed : (const uint8_t *)e->data;
            blob = zip_aes_seal(o->alloc, pw, pwlen, enc_strength, body, (size_t)r->packed, r, &blob_len, &err);
            if (!blob) { fa_release(o->alloc, packed); break; }
            r->packed = blob_len;
        }
        r->zip64 = force64 || r->size >= 0xFFFFFFFFu || r->packed >= 0xFFFFFFFFu;
        zip_local_header(o, r);
        if (blob) put(o, blob, blob_len);
        else if (packed) put(o, packed, packed_len);
        else if (e->size && !dir) {
            for (uint64_t at = 0; at < e->size && !o->failed;) {
                size_t n = e->size - at > (1u << 30) ? (1u << 30) : (size_t)(e->size - at);
                put(o, (const uint8_t *)e->data + at, n);
                at += n;
            }
        }
        fa_release(o->alloc, blob);
        fa_release(o->alloc, packed);
        if (o->failed) { err = FULTA_ARC_ERR_IO; break; }
        done += e->size;
        if (opt && opt->progress && opt->progress(opt->progress_ctx, done, total)) err = FULTA_ARC_ERR_CANCELLED;
    }
    if (err == FULTA_ARC_OK) err = write_zip_central(o, rec, count, force64);
    for (size_t i = 0; i < count; ++i) fa_release(o->alloc, rec[i].name);
    fa_release(o->alloc, rec);
    return err;
}

/* Write a regenerated local file header (no data descriptor; real sizes/crc) from a filled record; sets its disk and
 * offset, and returns where it starts in the output (for the patches). */
static uint64_t zip_local_header(out_t *o, zip_record_t *r) {
    size_t extra = (r->zip64 ? 20u : 0u) + (r->mtime ? 9u : 0u) + (r->encrypted ? 11u : 0u);
    zip_here(o, 30 + r->name_len + extra, &r->disk, &r->offset);   /* sets the record's disk and offset */
    uint64_t start = o->at;
    uint8_t h[30];
    le32(h, 0x04034b50);
    le16(h + 4, zip_version_needed(r));
    le16(h + 6, r->flags);
    le16(h + 8, r->method);
    le16(h + 10, r->time);
    le16(h + 12, r->date);
    le32(h + 14, r->crc);
    le32(h + 18, r->zip64 ? 0xFFFFFFFFu : (uint32_t)r->packed);
    le32(h + 22, r->zip64 ? 0xFFFFFFFFu : (uint32_t)r->size);
    le16(h + 26, (uint32_t)r->name_len);
    le16(h + 28, (uint32_t)extra);
    put(o, h, sizeof(h));
    put(o, r->name, r->name_len);
    if (r->zip64) {
        uint8_t x[20];
        le16(x, 0x0001); le16(x + 2, 16); le64(x + 4, r->size); le64(x + 12, r->packed);
        put(o, x, sizeof(x));
    }
    if (r->mtime) {
        uint8_t x[9];
        le16(x, 0x5455); le16(x + 2, 5); x[4] = 1; le32(x + 5, (uint32_t)r->mtime);
        put(o, x, sizeof(x));
    }
    if (r->encrypted) put_aes_extra(o, r);
    return start;
}

static bool name_is_dir(const char *name) {
    size_t n = strlen(name);
    return n && (name[n - 1] == '/' || name[n - 1] == '\\');
}

/* ---- the convert routes' disk spill (shared by ZIP-LZMA and the 7z convert; see the 7z convert section) ---- */

/* The spill file as the SDK's input stream, read sequentially. */
typedef struct sz_spill_r {
    ISeqInStream vt;
    proven_file_t f;
    bool io_err;
} sz_spill_r_t;

static SRes sz_spill_read(ISeqInStreamPtr pp, void *buf, size_t *size) {
    sz_spill_r_t *p = (sz_spill_r_t *)(void *)pp;
    proven_result_size_t r = proven_fs_read(p->f, (proven_mem_mut_t){.ptr = buf, .size = *size});
    if (r.err == PROVEN_ERR_EOF) { *size = r.value; return SZ_OK; }   /* 0 bytes left -> EOF to the encoder */
    if (r.err) { *size = 0; p->io_err = true; return SZ_ERROR_READ; }
    *size = r.value;
    return SZ_OK;
}

/* The spill directory's name: beside the output (`<hint>.d-<random>`) when a file path is known, else
 * `<temp>/fa7z-<random>`. */
static fulta_arc_err_t spill_path(proven_allocator_t a, const char *hint, char **out) {
    *out = NULL;
    const char *pre = hint, *sep = ".d-";
    if (!pre) {
        pre = getenv("TMPDIR");
        if (!pre || !*pre) pre = getenv("TMP");
        if (!pre || !*pre) pre = getenv("TEMP");
        if (!pre || !*pre) pre = "/tmp";
        sep = "/fa7z-";
    }
    uint8_t rnd[12];
    if (!fa_random_bytes(rnd, sizeof rnd)) return FULTA_ARC_ERR_IO;
    size_t pn = strlen(pre), sn = strlen(sep);
    char *s = fa_alloc(a, pn + sn + 24 + 1);
    if (!s) return FULTA_ARC_ERR_NOMEM;
    static const char hexd[] = "0123456789abcdef";
    char *q = s;
    memcpy(q, pre, pn); q += pn;
    memcpy(q, sep, sn); q += sn;
    for (int i = 0; i < 12; ++i) { *q++ = hexd[rnd[i] >> 4]; *q++ = hexd[rnd[i] & 15]; }
    *q = 0;
    *out = s;
    return FULTA_ARC_OK;
}

/* The spill: one handle, read and written; its names removed as soon as it is open (see the section comment). */
typedef struct spill {
    proven_allocator_t alloc;   /* the names' */
    proven_file_t f;
    bool open;
    char *dir, *file;
    bool dir_named, file_named;   /* still to be removed at the close */
} spill_t;

static void spill_close(spill_t *s) {
    proven_allocator_t heap = s->alloc;
    if (s->open) { proven_err_t ig = proven_fs_close(s->f); (void)ig; }
    if (s->file_named) { proven_err_t ig = proven_fs_remove(heap, proven_u8str_view_from_cstr(s->file)); (void)ig; }
    if (s->dir_named) { proven_err_t ig = proven_fs_rmdir(heap, proven_u8str_view_from_cstr(s->dir)); (void)ig; }
    fa_release(s->alloc, s->dir); fa_release(s->alloc, s->file);
    memset(s, 0, sizeof *s);
}

static fulta_arc_err_t spill_open(proven_allocator_t a, const char *hint, spill_t *s) {
    memset(s, 0, sizeof *s);
    s->alloc = a;
    proven_allocator_t heap = a;
    fulta_arc_err_t err = spill_path(a, hint, &s->dir);
    if (err) return err;
    if (proven_fs_mkdir(heap, proven_u8str_view_from_cstr(s->dir))) { fa_release(s->alloc, s->dir); s->dir = NULL; return FULTA_ARC_ERR_IO; }
    s->dir_named = true;
#ifndef _WIN32
    if (proven_fs_chmod(heap, proven_u8str_view_from_cstr(s->dir),
                        PROVEN_FS_PERM_OWNER_R | PROVEN_FS_PERM_OWNER_W | PROVEN_FS_PERM_OWNER_X)) {
        spill_close(s);
        return FULTA_ARC_ERR_IO;
    }
#endif
    size_t dn = strlen(s->dir);
    s->file = fa_alloc(s->alloc, dn + 3);
    if (!s->file) { spill_close(s); return FULTA_ARC_ERR_NOMEM; }
    memcpy(s->file, s->dir, dn);
    memcpy(s->file + dn, "/s", 3);
    proven_result_file_t rf = proven_fs_open(heap, proven_u8str_view_from_cstr(s->file),
                                             PROVEN_FS_READ | PROVEN_FS_WRITE | PROVEN_FS_CREATE_NEW);
    if (rf.err) { spill_close(s); return FULTA_ARC_ERR_IO; }
    s->f = rf.value; s->open = true; s->file_named = true;
#ifndef _WIN32
    if (proven_fs_chmod(heap, proven_u8str_view_from_cstr(s->file), PROVEN_FS_PERM_OWNER_R | PROVEN_FS_PERM_OWNER_W)) {
        spill_close(s);
        return FULTA_ARC_ERR_IO;
    }
#endif
    if (!proven_fs_remove(heap, proven_u8str_view_from_cstr(s->file))) s->file_named = false;
    if (!s->file_named && !proven_fs_rmdir(heap, proven_u8str_view_from_cstr(s->dir))) s->dir_named = false;
    return FULTA_ARC_OK;
}

/* Back to the start of the spill for pass 2. */
static fulta_arc_err_t spill_rewind(spill_t *s) {
    return proven_fs_seek(s->f, 0, PROVEN_FS_SEEK_SET).err ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;
}

/* ---- convert route: re-encode any open archive's entries into a fresh ZIP, streaming -------------------------
 * Each entry's plaintext is produced once (fulta_arc_extract pushes it for a source entry, or the item's reader is
 * pulled for an add/replace) and fed straight into an incremental encoder whose output goes to the sink; nothing is
 * held whole in memory, so a multi-GiB entry converts in bounded space. Deflate, bzip2, PPMd (var.I) and zstd take
 * pushed input; the LZMA encoder pulls, so it reads an add's reader directly and a source entry from a disk spill
 * (the same private, nameless spill as the 7z convert). The local header is written with zeroed crc/sizes, then
 * patched (sink->patch) once the real values are known. With a password every file entry is WinZip-AES (AE-2)
 * encrypted on the way out, as fulta_arc_write does it but in pieces: AES-CTR and the HMAC run over the compressed
 * bytes as they leave the compressor, so memory stays bounded. Editing an encrypted source is refused earlier
 * (fulta_arc_editable). The copy route (fa_write_zip_edited) writes its added entries through the same code. */

static fulta_arc_err_t patch_u32(out_t *o, uint64_t off, uint32_t v) {
    uint8_t b[4]; le32(b, v);
    return o->sink->patch(o->sink->ctx, off, b, 4);
}
static fulta_arc_err_t patch_u64(out_t *o, uint64_t off, uint64_t v) {
    uint8_t b[8]; le64(b, v);
    return o->sink->patch(o->sink->ctx, off, b, 8);
}

/* The running state while one entry is pushed through its encoder (or stored) into the ZIP sink. */
typedef struct conv_zip conv_zip_t;
typedef struct conv_ppmd_out {             /* the PPMd range coder's bytes, batched into conv_emit */
    IByteOut vt;
    conv_zip_t *c;
    size_t len;
    uint8_t buf[4096];
} conv_ppmd_out_t;
struct conv_zip {
    out_t *o;
    fulta_arc_method_t m;      /* STORE, DEFLATE, BZIP2, PPMD or ZSTD here; LZMA runs conv_lzma instead */
    tdefl_compressor *comp;    /* DEFLATE (owned by the caller) */
    bz_stream bz; bool bz_on;
    ZSTD_CCtx *zs;
    CPpmd8 ppmd; bool ppmd_on;
    conv_ppmd_out_t pout;
    uint32_t crc;
    uint64_t usize, csize;
    const fulta_arc_write_options_t *opt;
    uint64_t *done, total;     /* progress across the whole convert */
    fulta_arc_err_t err;
    bool aes;                  /* WinZip AES: what goes out is encrypted and authenticated (csize counts ciphertext) */
    fa_aes_ctr_t ctr;
    fa_hmac_sha1_t mac;
    fa_isz_t isz;              /* the PPMd/LZMA encoders allocate through the writer's allocator */
};

/* The entry's packed bytes out: as they are, or AES-CTR encrypted and fed to the HMAC. */
static void conv_emit(conv_zip_t *c, const void *data, size_t n) {
    if (!c->aes) { put(c->o, data, n); c->csize += n; return; }
    uint8_t tmp[4096];
    const uint8_t *p = (const uint8_t *)data;
    while (n && !c->o->failed) {
        size_t k = n < sizeof tmp ? n : sizeof tmp;
        memcpy(tmp, p, k);
        fa_aes_ctr_xor(&c->ctr, tmp, k);
        fa_hmac_sha1_update(&c->mac, tmp, k);
        put(c->o, tmp, k);
        c->csize += k; p += k; n -= k;
    }
    memset(tmp, 0, sizeof tmp);
}

/* miniz put-buf callback: the deflated bytes of the current entry. */
static mz_bool conv_put(const void *buf, int len, void *user) {
    conv_zip_t *c = (conv_zip_t *)user;
    conv_emit(c, buf, (size_t)len);
    return c->o->failed ? MZ_FALSE : MZ_TRUE;
}

static void conv_ppmd_byte(IByteOutPtr pp, Byte b) {
    conv_ppmd_out_t *w = (conv_ppmd_out_t *)((char *)pp - offsetof(conv_ppmd_out_t, vt));
    w->buf[w->len++] = b;
    if (w->len == sizeof w->buf) { conv_emit(w->c, w->buf, w->len); w->len = 0; }
}

/* Start the entry's encoder. ZIP's per-method body headers (PPMd's 2-byte parameters) go out first. */
static fulta_arc_err_t conv_begin(conv_zip_t *c, fulta_arc_method_t m, int level, tdefl_compressor *comp, mz_uint zf) {
    c->m = m;
    int l = level < 1 ? 1 : level > 9 ? 9 : level;
    switch (m) {
        case FULTA_ARC_METHOD_DEFLATE:
            if (tdefl_init(comp, conv_put, c, (int)zf) != TDEFL_STATUS_OKAY) return FULTA_ARC_ERR_CORRUPT;
            c->comp = comp;
            return FULTA_ARC_OK;
        case FULTA_ARC_METHOD_BZIP2:
            memset(&c->bz, 0, sizeof c->bz);
            bz_hook(&c->bz, &c->o->alloc);
            if (BZ2_bzCompressInit(&c->bz, l, 0, 0) != BZ_OK) return FULTA_ARC_ERR_NOMEM;
            c->bz_on = true;
            return FULTA_ARC_OK;
        case FULTA_ARC_METHOD_ZSTD: {
            /* the same level map as zip_encode_body: 1..9 onto zstd 1..19 */
            static const int z_by_level[10] = {0, 1, 3, 6, 9, 12, 15, 17, 19, 19};
            c->zs = zstd_cctx(&c->o->alloc);
            if (!c->zs) return FULTA_ARC_ERR_NOMEM;
            if (ZSTD_isError(ZSTD_CCtx_setParameter(c->zs, ZSTD_c_compressionLevel, z_by_level[l])))
                return FULTA_ARC_ERR_CORRUPT;
            return FULTA_ARC_OK;
        }
        case FULTA_ARC_METHOD_PPMD: {
            unsigned order, memMB, restore;
            ppmd_params_for_level(level, &order, &memMB, &restore);
            uint8_t h[2];
            uint16_t v = (uint16_t)((order - 1) | ((memMB - 1) << 4) | (restore << 12));
            h[0] = (uint8_t)(v & 0xFF); h[1] = (uint8_t)(v >> 8);
            conv_emit(c, h, 2);
            Ppmd8_Construct(&c->ppmd);
            if (!Ppmd8_Alloc(&c->ppmd, (UInt32)memMB << 20, &c->isz.vt)) return FULTA_ARC_ERR_NOMEM;
            c->ppmd_on = true;
            c->pout.vt.Write = conv_ppmd_byte;
            c->pout.c = c;
            c->pout.len = 0;
            c->ppmd.Stream.Out = &c->pout.vt;
            Ppmd8_Init_RangeEnc(&c->ppmd);
            Ppmd8_Init(&c->ppmd, order, restore);
            return FULTA_ARC_OK;
        }
        default:                                         /* STORE */
            return FULTA_ARC_OK;
    }
}

/* Push `n` plaintext bytes through the entry's encoder (or store them). `finish` flushes the encoder's end. */
static fulta_arc_err_t conv_code(conv_zip_t *c, const void *data, size_t n, bool finish) {
    uint8_t outbuf[1 << 16];
    switch (c->m) {
        case FULTA_ARC_METHOD_DEFLATE: {
            tdefl_status st = tdefl_compress_buffer(c->comp, data, n, finish ? TDEFL_FINISH : TDEFL_NO_FLUSH);
            if (st != (finish ? TDEFL_STATUS_DONE : TDEFL_STATUS_OKAY))
                return c->o->failed ? FULTA_ARC_ERR_IO : FULTA_ARC_ERR_CORRUPT;
            break;
        }
        case FULTA_ARC_METHOD_BZIP2: {
            c->bz.next_in = (char *)(uintptr_t)data;
            c->bz.avail_in = 0;
            const uint8_t *p = data;
            size_t left = n;
            for (;;) {                                   /* avail_in is 32-bit: feed at most 1 GiB a time */
                if (!c->bz.avail_in && left) {
                    unsigned k = left > (1u << 30) ? (1u << 30) : (unsigned)left;
                    c->bz.next_in = (char *)(uintptr_t)p; c->bz.avail_in = k; p += k; left -= k;
                }
                c->bz.next_out = (char *)outbuf; c->bz.avail_out = sizeof outbuf;
                bool last = finish && !left;
                int rc = BZ2_bzCompress(&c->bz, last ? BZ_FINISH : BZ_RUN);
                if (rc < 0) return FULTA_ARC_ERR_CORRUPT;
                size_t produced = sizeof outbuf - c->bz.avail_out;
                if (produced) conv_emit(c, outbuf, produced);
                if (c->o->failed) return FULTA_ARC_ERR_IO;
                if (last ? rc == BZ_STREAM_END : (!c->bz.avail_in && !left)) break;
            }
            break;
        }
        case FULTA_ARC_METHOD_ZSTD: {
            ZSTD_inBuffer in = {data, n, 0};
            for (;;) {
                ZSTD_outBuffer out = {outbuf, sizeof outbuf, 0};
                size_t rem = ZSTD_compressStream2(c->zs, &out, &in, finish ? ZSTD_e_end : ZSTD_e_continue);
                if (ZSTD_isError(rem)) return FULTA_ARC_ERR_CORRUPT;
                if (out.pos) conv_emit(c, outbuf, out.pos);
                if (c->o->failed) return FULTA_ARC_ERR_IO;
                if (finish ? rem == 0 : in.pos == in.size) break;
            }
            break;
        }
        case FULTA_ARC_METHOD_PPMD: {
            const uint8_t *p = data;
            for (size_t i = 0; i < n; i++) Ppmd8_EncodeSymbol(&c->ppmd, p[i]);
            if (finish) {
                Ppmd8_EncodeSymbol(&c->ppmd, -1);        /* end marker: 7-Zip's ZIP-PPMd decoder reads to it */
                Ppmd8_Flush_RangeEnc(&c->ppmd);
                if (c->pout.len) { conv_emit(c, c->pout.buf, c->pout.len); c->pout.len = 0; }
            }
            if (c->o->failed) return FULTA_ARC_ERR_IO;
            break;
        }
        default:
            if (n) conv_emit(c, data, n);
            if (c->o->failed) return FULTA_ARC_ERR_IO;
            break;
    }
    return FULTA_ARC_OK;
}

static void conv_end(conv_zip_t *c) {
    if (c->bz_on) { BZ2_bzCompressEnd(&c->bz); c->bz_on = false; }
    if (c->zs) { ZSTD_freeCCtx(c->zs); c->zs = NULL; }
    if (c->ppmd_on) { Ppmd8_Free(&c->ppmd, &c->isz.vt); c->ppmd_on = false; }
}

/* Count `n` plaintext bytes into crc, size and progress; poll cancel. */
static fulta_arc_err_t conv_account(conv_zip_t *c, const void *data, size_t n) {
    if (n) {
        c->crc = fa_crc32(c->crc, fa_bytes(data, n));
        c->usize += n;
    }
    if (c->opt && c->opt->progress) {
        *c->done += n;
        if (c->opt->progress(c->opt->progress_ctx, *c->done, c->total)) return c->err = FULTA_ARC_ERR_CANCELLED;
    }
    return FULTA_ARC_OK;
}

/* Feed `n` plaintext bytes: update crc, count, compress-or-store, poll progress/cancel. */
static fulta_arc_err_t conv_feed(conv_zip_t *c, const void *data, size_t n) {
    if (c->err) return c->err;
    if (n) {
        fulta_arc_err_t e = conv_code(c, data, n, false);
        if (e) return c->err = e;
    }
    return conv_account(c, data, n);
}

/* fulta_arc_sink adaptor so a source entry's extraction pushes straight into conv_feed. */
static fulta_arc_err_t conv_sink_write(void *ctx, const void *data, size_t n) {
    return conv_feed((conv_zip_t *)ctx, data, n);
}

/* ZIP LZMA (method 14), pulled: from the add's reader, or from a spill the source entry was extracted into. */
typedef struct conv_lzma_in {
    ISeqInStream vt;
    conv_zip_t *c;
    const fulta_arc_reader_t *rd;   /* or NULL: read the spill */
    sz_spill_r_t sp;
} conv_lzma_in_t;

static SRes conv_lzma_read(ISeqInStreamPtr pp, void *buf, size_t *size) {
    conv_lzma_in_t *p = (conv_lzma_in_t *)(void *)pp;
    size_t got = 0;
    if (p->rd) {
        fulta_arc_err_t e = p->rd->read(p->rd->ctx, buf, *size, &got);
        if (e) { p->c->err = e; *size = 0; return SZ_ERROR_READ; }
    } else {
        got = *size;
        if (sz_spill_read(&p->sp.vt, buf, &got)) { p->c->err = FULTA_ARC_ERR_IO; *size = 0; return SZ_ERROR_READ; }
    }
    if (conv_account(p->c, buf, got)) { *size = 0; return SZ_ERROR_READ; }
    *size = got;
    return SZ_OK;
}

typedef struct conv_lzma_out {
    ISeqOutStream vt;
    conv_zip_t *c;
} conv_lzma_out_t;

static size_t conv_lzma_write(ISeqOutStreamPtr pp, const void *data, size_t size) {
    conv_lzma_out_t *w = (conv_lzma_out_t *)(void *)pp;
    conv_emit(w->c, data, size);
    return w->c->o->failed ? 0 : size;
}

/* A spill-side sink for a source entry: plaintext to the spill, cancel polled (progress counts at the encode). */
typedef struct conv_spill_w {
    proven_file_t f;
    conv_zip_t *c;
} conv_spill_w_t;

static fulta_arc_err_t conv_spill_write(void *ctx, const void *data, size_t n) {
    conv_spill_w_t *w = ctx;
    if (n && proven_fs_write_all(w->f, (proven_mem_view_t){.ptr = data, .size = n})) return FULTA_ARC_ERR_IO;
    const fulta_arc_write_options_t *opt = w->c->opt;
    if (opt && opt->progress && opt->progress(opt->progress_ctx, *w->c->done, w->c->total)) return FULTA_ARC_ERR_CANCELLED;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t conv_lzma(conv_zip_t *c, const fa_edit_item_t *it, int level, uint64_t known_usize,
                                 const char *spill_hint) {
    spill_t spill = {0};
    conv_lzma_in_t in = { .vt = { conv_lzma_read }, .c = c, .rd = it->is_copy ? NULL : &it->reader };
    fulta_arc_err_t err = FULTA_ARC_OK;
    if (it->is_copy) {
        err = spill_open(c->o->alloc, spill_hint, &spill);
        if (err) return err;
        conv_spill_w_t w = { .f = spill.f, .c = c };
        fulta_arc_sink_t s = { .ctx = &w, .write = conv_spill_write };
        err = fulta_arc_extract(it->src, it->src_index, &s);
        if (!err) err = spill_rewind(&spill);
        in.sp.vt.Read = sz_spill_read;
        in.sp.f = spill.f;
    }
    CLzmaEncHandle ench = err ? NULL : LzmaEnc_Create(&c->isz.vt);
    if (!err && !ench) err = FULTA_ARC_ERR_NOMEM;
    if (!err) {
        CLzmaEncProps ep; LzmaEncProps_Init(&ep);
        ep.level = level < 1 ? 1 : level > 9 ? 9 : level;
        if (known_usize != UINT64_MAX) ep.reduceSize = known_usize;   /* no larger a dictionary than the data needs */
        uint8_t head[4 + LZMA_PROPS_SIZE] = {9, 20, LZMA_PROPS_SIZE, 0};  /* [verMaj, verMin, propsLen LE16] props */
        SizeT ps = LZMA_PROPS_SIZE;
        SRes res = LzmaEnc_SetProps(ench, &ep);
        if (res == SZ_OK) res = LzmaEnc_WriteProperties(ench, head + 4, &ps);
        if (res == SZ_OK) conv_emit(c, head, sizeof head);
        conv_lzma_out_t out = { .vt = { conv_lzma_write }, .c = c };
        if (res == SZ_OK && !c->o->failed) res = LzmaEnc_Encode(ench, &out.vt, &in.vt, NULL, &c->isz.vt, &c->isz.vt);
        if (c->err) err = c->err;
        else if (c->o->failed) err = FULTA_ARC_ERR_IO;
        else if (res == SZ_ERROR_MEM) err = FULTA_ARC_ERR_NOMEM;
        else if (res != SZ_OK) err = FULTA_ARC_ERR_CORRUPT;
    }
    if (ench) LzmaEnc_Destroy(ench, &c->isz.vt, &c->isz.vt);
    if (it->is_copy) spill_close(&spill);
    return err;
}

/* What every streamed entry of one ZIP write shares. */
typedef struct zip_stream_cfg {
    proven_allocator_t alloc;      /* the writer's */
    fulta_arc_method_t m;          /* concrete (method_concrete) */
    int level;
    mz_uint zf;
    tdefl_compressor *comp;        /* for DEFLATE; NULL when no entry deflates */
    int enc_strength;              /* 0 = no encryption */
    const char *pw;
    size_t pwlen;
    const fulta_arc_write_options_t *opt;
    uint64_t *done, total;
    const char *spill_hint;
    bool force64;
} zip_stream_cfg_t;

/* Write one entry streamed: the local header from `r` (name, flags, times, attributes and offset already set), the
 * body encoded from the item's plaintext, then the header patched with the crc and sizes. */
static fulta_arc_err_t zip_stream_entry(out_t *o, zip_record_t *r, const fa_edit_item_t *it, uint64_t known_usize,
                                        const zip_stream_cfg_t *k) {
    bool dir = r->dir;
    fulta_arc_method_t m = k->m;
    if (dir || known_usize == 0 || (m == FULTA_ARC_METHOD_DEFLATE && k->level <= 0)) m = FULTA_ARC_METHOD_STORE;
    static const uint16_t zip_id[] = {
        [FULTA_ARC_METHOD_STORE] = 0, [FULTA_ARC_METHOD_DEFLATE] = 8, [FULTA_ARC_METHOD_BZIP2] = 12,
        [FULTA_ARC_METHOD_LZMA] = 14, [FULTA_ARC_METHOD_PPMD] = 98, [FULTA_ARC_METHOD_ZSTD] = 93 };
    r->crc = 0; r->size = 0; r->packed = 0;
    r->method = zip_id[m];
    if (k->enc_strength && !dir) zip_aes_mark(r, k->enc_strength);
    /* Decide Zip64 before writing the header: force it when the size is unknown (we cannot rewind the decision),
     * otherwise from the known uncompressed size, with a margin for what the encoder may add on incompressible data
     * (deflate's stored blocks and the AES fields stay well under 1 MiB at 4 GiB; bzip2 and PPMd can grow ~1-3%). */
    uint64_t lim = (m == FULTA_ARC_METHOD_STORE || m == FULTA_ARC_METHOD_DEFLATE) ? 0xFFF00000u : 0xF0000000u;
    r->zip64 = k->force64 || known_usize == UINT64_MAX || known_usize >= lim;

    uint64_t hdr_off = zip_local_header(o, r);
    if (o->failed) return FULTA_ARC_ERR_IO;
    if (dir) return FULTA_ARC_OK;

    conv_zip_t c = { .o = o, .opt = k->opt, .done = k->done, .total = k->total, .isz = isz_of(o->alloc) };
    fulta_arc_err_t err = FULTA_ARC_OK;
    uint64_t aes_extra = 0;                              /* salt + verifier before the data, MAC after */
    if (k->enc_strength) {
        size_t sl = aes_salt_len[k->enc_strength], kl = aes_key_len[k->enc_strength];
        uint8_t salt[16], key[66];
        if (!zip_aes_keys(k->pw, k->pwlen, k->enc_strength, salt, key)) return FULTA_ARC_ERR_IO;   /* no entropy */
        put(o, salt, sl);
        put(o, key + 2 * kl, 2);                         /* 2-byte password verifier */
        fa_aes_ctr_le1_begin(&c.ctr, key, kl);
        fa_hmac_sha1_init(&c.mac, key + kl, kl);
        memset(key, 0, sizeof key);
        c.aes = true;
        aes_extra = sl + 2 + 10;
        if (o->failed) err = FULTA_ARC_ERR_IO;           /* fall through: the keys are wiped below */
    }
    if (!err && m == FULTA_ARC_METHOD_LZMA) {
        err = conv_lzma(&c, it, k->level, known_usize, k->spill_hint);
    } else if (!err) {
        err = conv_begin(&c, m, k->level, k->comp, k->zf);
        if (!err && it->is_copy) {
            fulta_arc_sink_t s = { .ctx = &c, .write = conv_sink_write };
            err = fulta_arc_extract(it->src, it->src_index, &s);
            if (err == FULTA_ARC_OK) err = c.err;
        } else if (!err && it->reader.read) {
            uint8_t chunk[1 << 16];
            for (;;) {
                size_t got = 0;
                err = it->reader.read(it->reader.ctx, chunk, sizeof chunk, &got);
                if (err || got == 0) break;
                err = conv_feed(&c, chunk, got);
                if (err) break;
            }
        }
        if (!err) err = conv_code(&c, NULL, 0, true);
        conv_end(&c);
    }
    if (c.aes) {
        uint8_t mac[20];
        fa_hmac_sha1_final(&c.mac, mac);
        if (err == FULTA_ARC_OK) put(o, mac, 10);        /* HMAC-SHA1 authentication, 10 bytes */
        fa_aes_ctr_end(&c.ctr);
        memset(&c.mac, 0, sizeof c.mac);
        if (err == FULTA_ARC_OK && o->failed) err = FULTA_ARC_ERR_IO;
    }
    if (err) return err;
    r->crc = c.aes ? 0 : c.crc;                          /* AE-2 stores no plaintext CRC */
    r->size = c.usize;
    r->packed = c.csize + aes_extra;
    if (!r->zip64 && (r->size >= 0xFFFFFFFFu || r->packed >= 0xFFFFFFFFu)) return FULTA_ARC_ERR_LIMIT;
    /* Patch the header now that crc and the sizes are known. */
    err = patch_u32(o, hdr_off + 14, r->crc);
    if (!err) {
        if (r->zip64) {
            uint64_t xoff = hdr_off + 30 + r->name_len;  /* the Zip64 extra is written first */
            err = patch_u64(o, xoff + 4, r->size);
            if (!err) err = patch_u64(o, xoff + 12, r->packed);
        } else {
            err = patch_u32(o, hdr_off + 18, (uint32_t)r->packed);
            if (!err) err = patch_u32(o, hdr_off + 22, (uint32_t)r->size);
        }
    }
    return err;
}

/* The shared set-up of a streamed ZIP write: method, encryption and the deflate state. */
static fulta_arc_err_t zip_stream_cfg_init(zip_stream_cfg_t *k, int level, bool force64,
                                           const fulta_arc_write_options_t *opt, const char *spill_hint) {
    memset(k, 0, sizeof *k);
    k->alloc = fa_write_alloc(opt);
    k->level = level; k->force64 = force64; k->opt = opt; k->spill_hint = spill_hint;
    k->pw = opt ? opt->password : NULL;
    k->pwlen = k->pw ? strlen(k->pw) : 0;
    if (k->pw) {
        k->enc_strength = zip_aes_strength(opt->encryption);
        if (!k->enc_strength) return FULTA_ARC_ERR_INVALID_ARG;   /* a 7z scheme for a ZIP */
    }
    fulta_arc_err_t e = method_concrete(FULTA_ARC_FORMAT_ZIP, level, opt ? opt->method : FULTA_ARC_METHOD_DEFAULT, &k->m);
    if (e) return e;                                     /* INVALID_ARG: LZMA2 is not a ZIP method */
    k->zf = tdefl_create_comp_flags_from_zip_params(level > 0 ? level : 6, -15, MZ_DEFAULT_STRATEGY);
    if (k->m == FULTA_ARC_METHOD_DEFLATE && level > 0) {
        k->comp = fa_alloc(k->alloc, sizeof *k->comp);
        if (!k->comp) return FULTA_ARC_ERR_NOMEM;
    }
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_write_zip_convert(const fa_edit_item_t *items, size_t count, int level, bool force64,
                                     const fulta_arc_write_options_t *opt, const fulta_arc_write_sink_t *sink,
                                     const char *spill_hint) {
    proven_allocator_t a = fa_write_alloc(opt);
    if (!sink || !sink->write || !sink->patch) return FULTA_ARC_ERR_INVALID_ARG;
    zip_stream_cfg_t k;
    uint64_t done = 0;
    fulta_arc_err_t err = zip_stream_cfg_init(&k, level, force64, opt, spill_hint);
    if (err) { fa_release(a, k.comp); return err; }
    out_t o = { .sink = sink, .alloc = fa_write_alloc(opt) };
    zip_begin(&o);
    zip_record_t *rec = fa_zalloc(a, count ? count : 1, sizeof *rec);
    if (!rec) { fa_release(a, k.comp); return FULTA_ARC_ERR_NOMEM; }

    /* progress total: the sum of the plaintext sizes we know up front */
    for (size_t i = 0; i < count; ++i) {
        if (items[i].is_copy) {
            const fulta_arc_entry_t *e = fulta_arc_entry(items[i].src, items[i].src_index);
            if (e && !(e->flags & (FULTA_ARC_ENTRY_DIR | FULTA_ARC_ENTRY_UNKNOWN_SIZE))) k.total += e->size;
        } else if (items[i].reader.read && items[i].reader.size != UINT64_MAX) {
            k.total += items[i].reader.size;
        }
    }
    k.done = &done;

    for (size_t i = 0; i < count && err == FULTA_ARC_OK; ++i) {
        const fa_edit_item_t *it = &items[i];
        zip_record_t *r = &rec[i];
        r->offset = o.at;

        bool dir;
        const char *uname;
        int64_t mtime_sec = 0;
        uint64_t known_usize = UINT64_MAX;
        if (it->is_copy) {
            const fulta_arc_entry_t *e = fulta_arc_entry(it->src, it->src_index);
            if (!e) { err = FULTA_ARC_ERR_INVALID_ARG; break; }
            dir = (e->flags & FULTA_ARC_ENTRY_DIR) != 0;
            uname = it->name ? it->name : e->name;
            mtime_sec = (e->flags & FULTA_ARC_ENTRY_HAS_MTIME) ? e->mtime / 1000000000 : 0;
            known_usize = dir ? 0 : ((e->flags & FULTA_ARC_ENTRY_UNKNOWN_SIZE) ? UINT64_MAX : e->size);
        } else {
            dir = name_is_dir(it->name);
            uname = it->name;
            mtime_sec = it->reader.mtime;                /* already seconds */
            known_usize = dir ? 0 : (it->reader.read ? it->reader.size : 0);
        }
        if (!uname) { err = FULTA_ARC_ERR_INVALID_ARG; break; }
        r->name = stored_name(k.alloc, uname, dir, true);
        if (!r->name) { err = FULTA_ARC_ERR_NOMEM; break; }
        r->name_len = strlen(r->name);
        r->flags = ascii(r->name) ? 0 : 0x0800;
        r->made_by_host = 0;
        r->ext_attr = dir ? 0x10u : 0u;
        r->dir = dir;
        r->mtime = mtime_sec;
        dos_time(mtime_sec, &r->time, &r->date);
        err = zip_stream_entry(&o, r, it, known_usize, &k);
    }
    if (err == FULTA_ARC_OK) err = write_zip_central(&o, rec, count, force64);
    for (size_t i = 0; i < count; ++i) fa_release(a, rec[i].name);
    fa_release(a, rec);
    fa_release(a, k.comp);
    return err;
}

fulta_arc_err_t fa_write_zip_edited(const fa_edit_item_t *items, size_t count, int level, bool force64,
                                    const fulta_arc_write_options_t *opt, const fulta_arc_write_sink_t *sink) {
    proven_allocator_t a = fa_write_alloc(opt);
    if (!sink || !sink->write || !sink->patch) return FULTA_ARC_ERR_INVALID_ARG;
    if (opt && opt->password) return FULTA_ARC_ERR_UNSUPPORTED;   /* encrypting write is D-016, not yet */
    zip_stream_cfg_t k;                                /* added entries: streamed with the chosen method */
    fulta_arc_err_t err = zip_stream_cfg_init(&k, level, force64, opt, NULL);
    if (err) { fa_release(a, k.comp); return err; }
    out_t o = { .sink = sink, .alloc = fa_write_alloc(opt) };
    zip_begin(&o);
    zip_record_t *rec = fa_zalloc(a, count ? count : 1, sizeof *rec);
    fa_zip_copy_t *cis = fa_zalloc(a, count ? count : 1, sizeof *cis);
    if (!rec || !cis) { fa_release(a, rec); fa_release(a, cis); fa_release(a, k.comp); return FULTA_ARC_ERR_NOMEM; }

    /* total for progress: copied packed bytes + bytes to re-encode */
    uint64_t total = 0, done = 0;
    for (size_t i = 0; i < count && err == FULTA_ARC_OK; ++i) {
        if (items[i].is_copy) {
            err = fa_zip_copy_info(items[i].src, items[i].src_index, &cis[i]);
            if (!err) total += cis[i].csize;
        } else if (items[i].reader.read && items[i].reader.size != UINT64_MAX) {
            total += items[i].reader.size;
        }
    }

    k.done = &done; k.total = total;
    for (size_t i = 0; i < count && err == FULTA_ARC_OK; ++i) {
        const fa_edit_item_t *it = &items[i];
        zip_record_t *r = &rec[i];
        r->offset = o.at;
        if (it->is_copy) {
            const fa_zip_copy_t *ci = &cis[i];
            r->dir = ci->is_dir;
            if (it->name) {                               /* renamed: UTF-8 name */
                r->name = stored_name(k.alloc, it->name, ci->is_dir, true);
                if (!r->name) { err = FULTA_ARC_ERR_NOMEM; break; }
                r->name_len = strlen(r->name);
                r->flags = ascii(r->name) ? 0 : 0x0800;
            } else {                                      /* keep the raw name bytes and the UTF-8 flag bit */
                r->name = fa_alloc(a, ci->name_raw_len ? ci->name_raw_len : 1);
                if (!r->name) { err = FULTA_ARC_ERR_NOMEM; break; }
                memcpy(r->name, ci->name_raw, ci->name_raw_len);
                r->name_len = ci->name_raw_len;
                r->flags = ci->flags & 0x0800;
            }
            r->method = ci->method; r->crc = ci->crc; r->size = ci->usize; r->packed = ci->csize;
            r->time = ci->time; r->date = ci->date; r->mtime = ci->mtime;
            r->made_by_host = ci->made_by_host; r->ext_attr = ci->ext_attr;
            r->zip64 = force64 || r->size >= 0xFFFFFFFFu || r->packed >= 0xFFFFFFFFu;
            zip_local_header(&o, r);
            /* copy the packed bytes verbatim, in chunks */
            uint64_t at = 0;
            uint8_t chunk[1 << 16];
            while (at < ci->csize && !o.failed && !err) {
                size_t n = ci->csize - at > sizeof chunk ? sizeof chunk : (size_t)(ci->csize - at);
                err = fa_zip_copy_read(it->src, ci->data_off + at, chunk, n);
                if (err) break;
                put(&o, chunk, n);
                at += n;
                done += n;
                if (opt && opt->progress && opt->progress(opt->progress_ctx, done, total)) { err = FULTA_ARC_ERR_CANCELLED; break; }
            }
        } else {
            bool dir = name_is_dir(it->name);
            r->dir = dir;
            r->name = stored_name(k.alloc, it->name, dir, true);
            if (!r->name) { err = FULTA_ARC_ERR_NOMEM; break; }
            r->name_len = strlen(r->name);
            r->flags = ascii(r->name) ? 0 : 0x0800;
            r->made_by_host = 0;
            r->ext_attr = dir ? 0x10u : 0u;
            r->mtime = it->reader.mtime;
            dos_time(it->reader.mtime, &r->time, &r->date);
            err = zip_stream_entry(&o, r, it, dir ? 0 : (it->reader.read ? it->reader.size : 0), &k);
        }
        if (!err && o.failed) err = FULTA_ARC_ERR_IO;
    }
    if (err == FULTA_ARC_OK) err = write_zip_central(&o, rec, count, force64);
    for (size_t i = 0; i < count; ++i) fa_release(a, rec[i].name);
    fa_release(a, rec); fa_release(a, cis); fa_release(a, k.comp);
    return err;
}

/* ---- 7z ---- */

/* A growing byte buffer for the header. */
typedef struct buf {
    proven_allocator_t alloc;
    uint8_t *p;
    size_t len, cap;
    bool failed;
} buf_t;

static void b_put(buf_t *b, const void *data, size_t size) {
    if (b->failed) return;
    if (b->len + size > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + size) cap *= 2;
        uint8_t *p = fa_resize(b->alloc, b->p, b->cap, cap);
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

/* 7zAES on the way out, in pieces (the 7z convert route): AES-256-CBC over the coder's output as it comes, the last
 * partial block zero-filled at the end, as encode_7z_folder pads its captured output. */
typedef struct sz_cbc {
    uint8_t key[32], iv[16], pend[16];
    size_t npend;
    uint64_t clen;           /* the coder's output length, before padding (the AES coder's unpack size) */
} sz_cbc_t;

static void cbc_put(out_t *o, sz_cbc_t *s, const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    uint8_t buf[1 << 14];
    s->clen += n;
    while (n && !o->failed) {
        size_t k = s->npend;
        memcpy(buf, s->pend, k);
        size_t take = sizeof buf - k < n ? sizeof buf - k : n;
        memcpy(buf + k, p, take);
        p += take; n -= take; k += take;
        size_t full = k & ~(size_t)15;
        if (full) { fa_aes_cbc_encrypt(s->key, 32, s->iv, buf, full); put(o, buf, full); }
        s->npend = k - full;
        memcpy(s->pend, buf + full, s->npend);
    }
    memset(buf, 0, sizeof buf);
}

/* Pad and write the last block; wipes the key. */
static void cbc_finish(out_t *o, sz_cbc_t *s) {
    if (s->npend) {
        memset(s->pend + s->npend, 0, 16 - s->npend);
        fa_aes_cbc_encrypt(s->key, 32, s->iv, s->pend, 16);
        put(o, s->pend, 16);
    }
    memset(s->key, 0, sizeof s->key);
    memset(s->pend, 0, sizeof s->pend);
}

typedef struct sz_out {
    ISeqOutStream vt;
    out_t *o;
    buf_t *buf;              /* when set, the compressed output is captured here (encryption) instead of emitted */
    sz_cbc_t *cbc;           /* when set, the output is encrypted as it is emitted (7z convert) */
} sz_out_t;

static size_t sz_out_write(ISeqOutStreamPtr pp, const void *data, size_t size) {
    sz_out_t *p = (sz_out_t*)(void*)pp;
    if (p->buf) { b_put(p->buf, data, size); return p->buf->failed ? 0 : size; }
    if (p->cbc) cbc_put(p->o, p->cbc, data, size);
    else put(p->o, data, size);
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

/* miniz put-buf callback writing a 7z Deflate folder's bytes through an sz_out_t (so it honours the AES capture). */
static mz_bool sz_tdefl_put(const void *buf, int len, void *user) {
    sz_out_t *os = (sz_out_t *)user;
    return sz_out_write(&os->vt, buf, (size_t)len) == (size_t)len ? MZ_TRUE : MZ_FALSE;
}

/* Push every non-dir entry's bytes (one solid stream) through BZip2 or raw Deflate into `os`, polling progress.
   Returns OK, CANCELLED, IO or CORRUPT/NOMEM. Used for the 7z folder coders that have no SDK pull-encoder here. */
static fulta_arc_err_t encode_7z_push(fulta_arc_method_t m, int level, const fulta_arc_write_entry_t *entries,
                                      size_t count, uint64_t total, sz_out_t *os,
                                      const fulta_arc_write_options_t *opt) {
    proven_allocator_t a = fa_write_alloc(opt);
    uint64_t done = 0;
    if (m == FULTA_ARC_METHOD_DEFLATE) {
        tdefl_compressor *c = fa_alloc(a, sizeof *c);
        if (!c) return FULTA_ARC_ERR_NOMEM;
        mz_uint zf = tdefl_create_comp_flags_from_zip_params(level > 0 ? level : 6, -15, MZ_DEFAULT_STRATEGY);
        tdefl_init(c, sz_tdefl_put, os, (int)zf);
        for (size_t i = 0; i < count; ++i) {
            if (is_dir(&entries[i]) || !entries[i].size) continue;
            if (tdefl_compress_buffer(c, entries[i].data, (size_t)entries[i].size, TDEFL_NO_FLUSH) != TDEFL_STATUS_OKAY) {
                fa_release(a, c); return FULTA_ARC_ERR_IO;
            }
            done += entries[i].size;
            if (opt && opt->progress && opt->progress(opt->progress_ctx, done, total)) { fa_release(a, c); return FULTA_ARC_ERR_CANCELLED; }
        }
        tdefl_status st = tdefl_compress_buffer(c, NULL, 0, TDEFL_FINISH);
        fa_release(a, c);
        return st == TDEFL_STATUS_DONE ? FULTA_ARC_OK : FULTA_ARC_ERR_IO;
    }
    /* BZip2 (streaming, so no 32-bit length cap and no doubled plaintext). bz.avail_in is unsigned (32-bit), so an
       entry larger than UINT_MAX is fed in <=UINT_MAX chunks; the usual case is one chunk. All entries form one
       continuous stream (BZ_RUN throughout), finished once after the last (i == count). */
    bz_stream bz; memset(&bz, 0, sizeof bz);
    bz_hook(&bz, &a);
    int bs = level < 1 ? 1 : level > 9 ? 9 : level;
    if (BZ2_bzCompressInit(&bz, bs, 0, 0) != BZ_OK) return FULTA_ARC_ERR_NOMEM;
    char outbuf[1 << 16];
    for (size_t i = 0; i <= count; ++i) {
        bool finish = (i == count);
        const uint8_t *data = NULL; uint64_t sz = 0, at = 0;
        if (!finish) {
            if (is_dir(&entries[i]) || !entries[i].size) continue;
            data = entries[i].data; sz = entries[i].size;
        }
        do {                                             /* one pass per <=UINT_MAX input chunk (finish: one pass) */
            if (!finish) {
                unsigned n = sz - at > UINT_MAX ? UINT_MAX : (unsigned)(sz - at);
                bz.next_in = (char *)(uintptr_t)(data + at);
                bz.avail_in = n;
                at += n;
            }
            int r;
            do {                                         /* drain this chunk / the finish into the output */
                bz.next_out = outbuf; bz.avail_out = sizeof outbuf;
                r = BZ2_bzCompress(&bz, finish ? BZ_FINISH : BZ_RUN);
                if (r != BZ_RUN_OK && r != BZ_FINISH_OK && r != BZ_STREAM_END) { BZ2_bzCompressEnd(&bz); return FULTA_ARC_ERR_CORRUPT; }
                size_t produced = sizeof outbuf - bz.avail_out;
                if (produced && sz_out_write(&os->vt, outbuf, produced) != produced) { BZ2_bzCompressEnd(&bz); return FULTA_ARC_ERR_IO; }
            } while (finish ? r != BZ_STREAM_END : bz.avail_in > 0);
        } while (!finish && at < sz);
        if (!finish) {
            done += sz;
            if (opt && opt->progress && opt->progress(opt->progress_ctx, done, total)) { BZ2_bzCompressEnd(&bz); return FULTA_ARC_ERR_CANCELLED; }
        }
    }
    BZ2_bzCompressEnd(&bz);
    return FULTA_ARC_OK;
}


/* One entry as the 7z header needs it (name + size + crc + time); it does not need the bytes themselves, so the
 * writer and the convert route both describe their entries this way and share write_7z_finish. */
typedef struct sz_desc {
    const char *name;   /* UTF-8, not owned */
    bool dir;
    uint64_t size;      /* plaintext bytes; a stream iff size != 0 && !dir */
    uint32_t crc;       /* crc32 of the plaintext, when size != 0 && !dir */
    int64_t mtime;      /* seconds since 1970, 0 = none */
} sz_desc_t;

/* 7-Zip's -mhe=on: the header is LZMA-packed and 7zAES-encrypted as a packed stream of its own, and what the
 * signature header points at is a kEncodedHeader record describing that stream (one folder: AES -> LZMA, the CRC of
 * the plain header). Replaces `h` with that record; the packed stream is written at `o` now. */
static void emit_folder_coders(buf_t *h, const fa_7z_copy_t *F);
static fulta_arc_err_t encode_7z_header(out_t *o, buf_t *h) {
    fa_isz_t isz = isz_of(o->alloc);   /* the encoders' own allocations go to the writer's allocator */
    CLzmaEncProps ep; LzmaEncProps_Init(&ep);
    ep.level = 5;
    ep.reduceSize = h->len;
    size_t cap = h->len + h->len / 2 + 4096;
    uint8_t *c = fa_alloc(o->alloc, cap + 16);
    if (!c) return FULTA_ARC_ERR_NOMEM;
    uint8_t props[LZMA_PROPS_SIZE]; SizeT ps = sizeof props;
    SizeT clen = cap;
    SRes res = LzmaEncode(c, &clen, h->p, h->len, &ep, props, &ps, 0, NULL, &isz.vt, &isz.vt);
    if (res != SZ_OK || ps != LZMA_PROPS_SIZE) { fa_release(o->alloc, c); return res == SZ_ERROR_MEM ? FULTA_ARC_ERR_NOMEM : FULTA_ARC_ERR_CORRUPT; }
    uint8_t salt[16], iv[16], key[32], ivc[16];
    if (!fa_random_bytes(salt, 16) || !fa_random_bytes(iv, 16)) { fa_release(o->alloc, c); return FULTA_ARC_ERR_IO; }   /* no entropy */
    fulta_arc_err_t err = fa_7z_derive_key(proven_heap_allocator(), o->hdr_pw, salt, 16, 19, key);
    if (err) { fa_release(o->alloc, c); return err; }
    size_t padded = ((size_t)clen + 15) & ~(size_t)15;
    memset(c + clen, 0, padded - clen);
    memcpy(ivc, iv, 16);
    fa_aes_cbc_encrypt(key, 32, ivc, c, padded);
    memset(key, 0, sizeof key);
    uint64_t pack_pos = o->at - 32;
    put(o, c, padded);
    fa_release(o->alloc, c);
    if (o->failed) return FULTA_ARC_ERR_IO;

    uint8_t aesprops[34];
    aesprops[0] = (uint8_t)(0xC0 | 19);                  /* salt & iv present, NumCyclesPower = 19 */
    aesprops[1] = 0xFF;                                  /* salt_len = iv_len = 16 */
    memcpy(aesprops + 2, salt, 16);
    memcpy(aesprops + 18, iv, 16);
    fa_7z_copy_t F = { .has_folder = true, .ncoders = 2, .npacked = 1, .nbinds = 1 };
    F.coders[0] = (fa_7z_coder_t){ .id = {0x03, 0x01, 0x01}, .idlen = 3, .nin = 1, .props = props, .nprops = LZMA_PROPS_SIZE };
    F.coders[1] = (fa_7z_coder_t){ .id = {0x06, 0xF1, 0x07, 0x01}, .idlen = 4, .nin = 1, .props = aesprops, .nprops = 34 };
    F.binds[0][0] = 0; F.binds[0][1] = 1;                /* LZMA.in0 <- AES.out */
    F.unpack_sizes[0] = h->len; F.unpack_sizes[1] = clen;

    buf_t e = {.alloc = o->alloc};
    b_byte(&e, 0x17);                                    /* kEncodedHeader */
    b_byte(&e, 0x06);                                    /* kPackInfo */
    b_num(&e, pack_pos);
    b_num(&e, 1);
    b_byte(&e, 0x09); b_num(&e, padded);
    b_byte(&e, 0x00);
    b_byte(&e, 0x07);                                    /* kUnPackInfo */
    b_byte(&e, 0x0B); b_num(&e, 1); b_byte(&e, 0);
    emit_folder_coders(&e, &F);
    b_byte(&e, 0x0C); b_num(&e, F.unpack_sizes[0]); b_num(&e, F.unpack_sizes[1]);
    b_byte(&e, 0x0A); b_byte(&e, 1); b_u32(&e, crc32_of(h->p, h->len));   /* the plain header's CRC */
    b_byte(&e, 0x00);
    b_byte(&e, 0x00);                                    /* end of StreamsInfo */
    if (e.failed) { fa_release(o->alloc, e.p); return FULTA_ARC_ERR_NOMEM; }
    memset(h->p, 0, h->len);                             /* the names in the clear go */
    fa_release(o->alloc, h->p);
    *h = e;
    return FULTA_ARC_OK;
}

/* Append kFilesInfo for the `count` entries `d` to `h` (which already holds kHeader + StreamsInfo), end the header,
 * write it after the packed data at `o`, and patch the 32-byte signature header `sig`. Takes ownership of `h->p`
 * (frees it). `streams` is how many entries have a stream (size != 0 && !dir). */
static fulta_arc_err_t write_7z_tail(out_t *o, uint8_t *sig, buf_t *h, const sz_desc_t *d, size_t count,
                                     size_t streams) {
    bool *flags = fa_zalloc(o->alloc, count ? count : 1, sizeof(bool));
    char **names = (char**)fa_zalloc(o->alloc, count ? count : 1, sizeof(char*));
    if (!flags || !names) { fa_release(o->alloc, flags); fa_release(o->alloc, names); fa_release(o->alloc, h->p); return FULTA_ARC_ERR_NOMEM; }
    fulta_arc_err_t err = FULTA_ARC_OK;
    for (size_t i = 0; i < count; ++i) {
        names[i] = stored_name(o->alloc, d[i].name, d[i].dir, false);
        if (!names[i]) err = FULTA_ARC_ERR_NOMEM;
    }
    b_byte(h, 0x05);                                     /* kFilesInfo */
    b_num(h, count);
    if (streams < count) {
        for (size_t i = 0; i < count; ++i) flags[i] = d[i].size == 0 || d[i].dir;
        b_byte(h, 0x0E); b_num(h, (count + 7) / 8); b_bits(h, flags, count);
        size_t empty = 0;
        for (size_t i = 0; i < count; ++i)                       /* over the streamless items: file or folder */
            if (d[i].size == 0 || d[i].dir) flags[empty++] = !d[i].dir;
        b_byte(h, 0x0F); b_num(h, (empty + 7) / 8); b_bits(h, flags, empty);
    }
    {
        buf_t n = {.alloc = o->alloc};
        b_byte(&n, 0);                                   /* not external */
        for (size_t i = 0; i < count && names[i]; ++i) b_name16(&n, names[i]);
        b_byte(h, 0x11); b_num(h, n.len); b_put(h, n.p, n.len);
        if (n.failed) h->failed = true;
        fa_release(o->alloc, n.p);
    }
    size_t timed = 0;
    for (size_t i = 0; i < count; ++i) if (d[i].mtime) timed++;
    if (timed > 0) {
        buf_t t = {.alloc = o->alloc};
        if (timed == count) {
            b_byte(&t, 1);
        } else {
            b_byte(&t, 0);
            for (size_t i = 0; i < count; ++i) flags[i] = d[i].mtime != 0;
            b_bits(&t, flags, count);
        }
        b_byte(&t, 0);                                   /* not external */
        for (size_t i = 0; i < count; ++i) {
            if (!d[i].mtime) continue;
            b_u64(&t, (uint64_t)(d[i].mtime + 11644473600LL) * 10000000u);   /* FILETIME */
        }
        b_byte(h, 0x14); b_num(h, t.len); b_put(h, t.p, t.len);
        if (t.failed) h->failed = true;
        fa_release(o->alloc, t.p);
    }
    b_byte(h, 0x00);                                     /* end of FilesInfo */
    b_byte(h, 0x00);                                     /* end of Header */
    if (h->failed) err = FULTA_ARC_ERR_NOMEM;

    if (err == FULTA_ARC_OK && o->hdr_pw) err = encode_7z_header(o, h);   /* names hidden: h becomes the stub */
    if (err == FULTA_ARC_OK) {
        uint64_t header_at = o->at;
        put(o, h->p, h->len);
        if (o->failed) err = FULTA_ARC_ERR_IO;
        else {
            le64(sig + 12, header_at - 32);              /* NextHeaderOffset: from the end of the signature header */
            le64(sig + 20, h->len);
            le32(sig + 28, crc32_of(h->p, h->len));
            le32(sig + 8, crc32_of(sig + 12, 20));       /* StartHeaderCRC */
            if (o->sink->patch(o->sink->ctx, 0, sig, 32)) err = FULTA_ARC_ERR_IO;
        }
    }
    for (size_t i = 0; i < count; ++i) fa_release(o->alloc, names[i]);
    fa_release(o->alloc, names);
    fa_release(o->alloc, flags);
    fa_release(o->alloc, h->p);
    return err;
}

/* Emit the 7z header for `count` entries described by `d`, whose single folder is already encoded: `packed` packed
 * bytes, `total` unpacked, the inner coder being `inner` (id + optional properties: Copy, LZMA, LZMA2, BZip2 or
 * Deflate), optionally wrapped in 7zAES (`enc`, `aesprops` [34 bytes], `clen` = inner length before padding). Writes
 * the header after the packed data at `o`, then patches the 32-byte signature header `sig`. `streams` is the count. */
/* Emit a folder's coder list, bind pairs and (when >1) packed-stream indices into h. Shared by the single-folder
 * finisher and the multi-folder header (write_7z_header_multi) so the two emitters never drift. */
static void emit_folder_coders(buf_t *h, const fa_7z_copy_t *F) {
    b_num(h, F->ncoders);
    for (uint32_t c = 0; c < F->ncoders; ++c) {
        const fa_7z_coder_t *cd = &F->coders[c];
        uint8_t fl = (uint8_t)(cd->idlen & 0x0F);
        if (cd->nin != 1) fl |= 0x10;                    /* complex: NumInStreams/NumOutStreams follow */
        if (cd->nprops) fl |= 0x20;                      /* has properties */
        b_byte(h, fl);
        b_put(h, cd->id, cd->idlen);
        if (cd->nin != 1) { b_num(h, cd->nin); b_num(h, 1); }   /* NumOutStreams is always 1 */
        if (cd->nprops) { b_num(h, cd->nprops); b_put(h, cd->props, cd->nprops); }
    }
    for (uint32_t b = 0; b < F->nbinds; ++b) { b_num(h, F->binds[b][0]); b_num(h, F->binds[b][1]); }
    if (F->npacked > 1)                                  /* the single-pack index is implied (the unbound input) */
        for (uint32_t k = 0; k < F->npacked; ++k) b_num(h, F->packed[k]);
}

/* Finish a single-folder 7z (the fulta_arc_write / convert routes): one solid folder whose coders, bind pairs,
 * packed-stream sizes and per-coder unpack sizes are in `F`, holding `streams` substreams. */
static fulta_arc_err_t write_7z_finish(out_t *o, uint8_t *sig, const sz_desc_t *d, size_t count, size_t streams,
                                       const fa_7z_copy_t *F) {
    buf_t h = {.alloc = o->alloc};
    b_byte(&h, 0x01);                                    /* kHeader */
    if (streams > 0) {
        b_byte(&h, 0x04);                                /* kMainStreamsInfo */
        b_byte(&h, 0x06);                                /* kPackInfo */
        b_num(&h, 0);                                    /* PackPos: right after the signature header */
        b_num(&h, F->npacked);                           /* NumPackStreams (BCJ2: 4) */
        b_byte(&h, 0x09);
        for (uint32_t k = 0; k < F->npacked; ++k) b_num(&h, F->pack_sizes[k]);
        b_byte(&h, 0x00);
        b_byte(&h, 0x07);                                /* kUnPackInfo */
        b_byte(&h, 0x0B); b_num(&h, 1); b_byte(&h, 0);   /* one folder, not external */
        emit_folder_coders(&h, F);                       /* coders (main [+ filter] [+ AES]) and their bind pairs */
        b_byte(&h, 0x0C);                                /* kCodersUnPackSize: every coder's output size, in order */
        for (uint32_t c = 0; c < F->ncoders; ++c) b_num(&h, F->unpack_sizes[c]);
        b_byte(&h, 0x00);
        b_byte(&h, 0x08);                                /* kSubStreamsInfo */
        b_byte(&h, 0x0D); b_num(&h, streams);
        if (streams > 1) {
            b_byte(&h, 0x09);
            size_t seen = 0;
            for (size_t i = 0; i < count; ++i) {
                if (!d[i].size || d[i].dir) continue;
                if (++seen == streams) break;            /* the last is what is left */
                b_num(&h, d[i].size);
            }
        }
        b_byte(&h, 0x0A); b_byte(&h, 1);                 /* all CRCs defined */
        for (size_t i = 0; i < count; ++i)
            if (d[i].size && !d[i].dir) b_u32(&h, d[i].crc);
        b_byte(&h, 0x00);
        b_byte(&h, 0x00);                                /* end of StreamsInfo */
    }
    return write_7z_tail(o, sig, &h, d, count, streams);
}

/* Apply a 7z content filter (phase 6) to the whole solid stream. The filter is 1:1 in size and runs continuously
 * across the concatenation of every entry (so its running position/state spans the folder, as the reader's does),
 * in one call over the full buffer: a trailing partial instruction is left unconverted, which the reader matches at
 * EOF. On success *out holds the filtered bytes (caller frees) and *fc is the filter coder (id + any properties;
 * `dprop` backs Delta's one byte). Coder ids mirror the reader's map (codecs.c): x86 03030103, Delta 03,
 * ARM 03030501, ARM64 0A, PPC 03030205, SPARC 03030805, IA64 03030401. */
static fulta_arc_err_t apply_filter_7z(proven_allocator_t a, fulta_arc_filter_t f, uint32_t dist,
                                       const fulta_arc_write_entry_t *entries, size_t count, uint64_t total,
                                       uint8_t **out, fa_7z_coder_t *fc, uint8_t *dprop) {
    uint8_t *b = fa_alloc(a, total ? (size_t)total : 1);
    if (!b) return FULTA_ARC_ERR_NOMEM;
    uint64_t at = 0;
    for (size_t i = 0; i < count; ++i) {
        if (is_dir(&entries[i]) || !entries[i].size) continue;
        memcpy(b + at, entries[i].data, (size_t)entries[i].size);
        at += entries[i].size;
    }
    SizeT n = (SizeT)total;
    memset(fc, 0, sizeof *fc);
    fc->nin = 1;
    switch (f) {
        case FULTA_ARC_FILTER_DELTA: {
            unsigned d = dist ? dist : 1;                /* 1..256; stored as d-1 */
            Byte st[DELTA_STATE_SIZE]; Delta_Init(st);
            Delta_Encode(st, d, b, n);
            fc->id[0] = 0x03; fc->idlen = 1;
            *dprop = (uint8_t)(d - 1); fc->props = dprop; fc->nprops = 1;
            break;
        }
        case FULTA_ARC_FILTER_BCJ_X86: {
            UInt32 state = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
            z7_BranchConvSt_X86_Enc(b, n, 0, &state);
            fc->id[0] = 0x03; fc->id[1] = 0x03; fc->id[2] = 0x01; fc->id[3] = 0x03; fc->idlen = 4;
            break;
        }
        case FULTA_ARC_FILTER_ARM:
            z7_BranchConv_ARM_Enc(b, n, 0);
            fc->id[0] = 0x03; fc->id[1] = 0x03; fc->id[2] = 0x05; fc->id[3] = 0x01; fc->idlen = 4;
            break;
        case FULTA_ARC_FILTER_ARM64:
            z7_BranchConv_ARM64_Enc(b, n, 0);
            fc->id[0] = 0x0A; fc->idlen = 1;
            break;
        case FULTA_ARC_FILTER_PPC:
            z7_BranchConv_PPC_Enc(b, n, 0);
            fc->id[0] = 0x03; fc->id[1] = 0x03; fc->id[2] = 0x02; fc->id[3] = 0x05; fc->idlen = 4;
            break;
        case FULTA_ARC_FILTER_SPARC:
            z7_BranchConv_SPARC_Enc(b, n, 0);
            fc->id[0] = 0x03; fc->id[1] = 0x03; fc->id[2] = 0x08; fc->id[3] = 0x05; fc->idlen = 4;
            break;
        case FULTA_ARC_FILTER_IA64:
            z7_BranchConv_IA64_Enc(b, n, 0);
            fc->id[0] = 0x03; fc->id[1] = 0x03; fc->id[2] = 0x04; fc->id[3] = 0x01; fc->idlen = 4;
            break;
        case FULTA_ARC_FILTER_ARMT:
            z7_BranchConv_ARMT_Enc(b, n, 0);
            fc->id[0] = 0x03; fc->id[1] = 0x03; fc->id[2] = 0x07; fc->id[3] = 0x01; fc->idlen = 4;
            break;
        case FULTA_ARC_FILTER_RISCV:
            z7_BranchConv_RISCV_Enc(b, n, 0);
            fc->id[0] = 0x0B; fc->idlen = 1;
            break;
        default:
            fa_release(a, b); return FULTA_ARC_ERR_INVALID_ARG;   /* NONE/BCJ2 never reach here (filter_check gates) */
    }
    *out = b;
    return FULTA_ARC_OK;
}

/* Compress the entries ents[0..nents) (their data concatenated) with method m into `outs`, filling *coder with the
 * 7z coder for that stream (id + properties, the properties living in the caller's props[]). The caller owns `outs`
 * and `props` and this frees nothing. `size` is the uncompressed total; `o` detects sink failure; `opt` may be NULL
 * to suppress progress/cancel (BCJ2 sub-streams pass NULL). One place for the per-method dispatch, used by write_7z's
 * main coder and reused per sub-stream by BCJ2 (phase 7). */
static fulta_arc_err_t encode_7z_stream(fulta_arc_method_t m, int level,
                                        const fulta_arc_write_entry_t *ents, size_t nents, uint64_t size,
                                        const fulta_arc_write_options_t *opt, out_t *o, sz_out_t *outs,
                                        fa_7z_coder_t *coder, uint8_t props[LZMA_PROPS_SIZE]) {
    fa_isz_t isz = isz_of(o->alloc);   /* the encoders' own allocations go to the writer's allocator */
    coder->nin = 1; coder->nprops = 0;
    if (m == FULTA_ARC_METHOD_STORE) {                   /* Copy: the bytes as they are */
        coder->id[0] = 0x00; coder->idlen = 1;
        uint64_t done = 0;
        for (size_t i = 0; i < nents; ++i) {
            if (is_dir(&ents[i]) || !ents[i].size) continue;
            if (sz_out_write(&outs->vt, ents[i].data, (size_t)ents[i].size) != ents[i].size)
                return o->failed ? FULTA_ARC_ERR_IO : FULTA_ARC_ERR_NOMEM;
            done += ents[i].size;
            if (opt && opt->progress && opt->progress(opt->progress_ctx, done, size)) return FULTA_ARC_ERR_CANCELLED;
        }
        return FULTA_ARC_OK;
    }
    if (m == FULTA_ARC_METHOD_LZMA) {
        coder->id[0] = 0x03; coder->id[1] = 0x01; coder->id[2] = 0x01; coder->idlen = 3;
        CLzmaEncHandle ench = LzmaEnc_Create(&isz.vt);
        if (!ench) return FULTA_ARC_ERR_NOMEM;
        CLzmaEncProps ep;
        LzmaEncProps_Init(&ep);
        ep.level = level;
        ep.reduceSize = size;                            /* no larger a dictionary than the data needs */
        SizeT ps = LZMA_PROPS_SIZE;
        SRes res = LzmaEnc_SetProps(ench, &ep);
        if (res == SZ_OK) res = LzmaEnc_WriteProperties(ench, props, &ps);
        sz_in_t in = { .vt = { sz_in_read }, .entries = ents, .count = nents };
        sz_progress_t prog = { .vt = { sz_progress }, .opt = opt, .total = size };
        if (res == SZ_OK) res = LzmaEnc_Encode(ench, &outs->vt, &in.vt, &prog.vt, &isz.vt, &isz.vt);
        LzmaEnc_Destroy(ench, &isz.vt, &isz.vt);
        if (o->failed) return FULTA_ARC_ERR_IO;
        if (outs->buf && outs->buf->failed) return FULTA_ARC_ERR_NOMEM;
        if (res == SZ_ERROR_PROGRESS) return FULTA_ARC_ERR_CANCELLED;
        if (res == SZ_ERROR_MEM) return FULTA_ARC_ERR_NOMEM;
        if (res != SZ_OK) return FULTA_ARC_ERR_CORRUPT;
        coder->props = props; coder->nprops = (uint32_t)ps;
        return FULTA_ARC_OK;
    }
    if (m == FULTA_ARC_METHOD_LZMA2) {
        coder->id[0] = 0x21; coder->idlen = 1;
        CLzma2EncHandle ench = Lzma2Enc_Create(&isz.vt, &isz.vt);
        if (!ench) return FULTA_ARC_ERR_NOMEM;
        CLzma2EncProps ep;
        Lzma2EncProps_Init(&ep);
        ep.lzmaProps.level = level;
        ep.lzmaProps.reduceSize = size;
        SRes res = Lzma2Enc_SetProps(ench, &ep);
        if (res == SZ_OK) {
            Lzma2Enc_SetDataSize(ench, size);
            props[0] = Lzma2Enc_WriteProperties(ench);   /* the 1 dict-size byte; must match the props just set */
            sz_in_t in = { .vt = { sz_in_read }, .entries = ents, .count = nents };
            sz_progress_t prog = { .vt = { sz_progress }, .opt = opt, .total = size };
            res = Lzma2Enc_Encode2(ench, &outs->vt, NULL, NULL, &in.vt, NULL, 0, &prog.vt);
        }
        Lzma2Enc_Destroy(ench);
        if (o->failed) return FULTA_ARC_ERR_IO;
        if (outs->buf && outs->buf->failed) return FULTA_ARC_ERR_NOMEM;
        if (res == SZ_ERROR_PROGRESS) return FULTA_ARC_ERR_CANCELLED;
        if (res == SZ_ERROR_MEM) return FULTA_ARC_ERR_NOMEM;
        if (res != SZ_OK) return FULTA_ARC_ERR_CORRUPT;
        coder->props = props; coder->nprops = 1;
        return FULTA_ARC_OK;
    }
    if (m == FULTA_ARC_METHOD_PPMD) {
        coder->id[0] = 0x03; coder->id[1] = 0x04; coder->id[2] = 0x01; coder->idlen = 3;   /* 7z PPMd var.H */
        unsigned order; uint32_t mem;
        ppmd7_params_for_level(level, &order, &mem);
        props[0] = (uint8_t)order; le32(props + 1, mem);  /* 5-byte property: [order, memSize LE32] */
        CPpmd7 p;
        Ppmd7_Construct(&p);
        if (!Ppmd7_Alloc(&p, mem, &isz.vt)) return FULTA_ARC_ERR_NOMEM;
        byteout_t bo = {.vt = {byteout_write}, .alloc = o->alloc};
        p.rc.enc.Stream = &bo.vt;
        Ppmd7z_Init_RangeEnc(&p);
        Ppmd7_Init(&p, order);
        uint64_t done = 0;
        bool cancelled = false;
        for (size_t i = 0; i < nents && !bo.oom && !cancelled; ++i) {
            if (is_dir(&ents[i]) || !ents[i].size) continue;
            const uint8_t *s = ents[i].data;
            Ppmd7z_EncodeSymbols(&p, s, s + ents[i].size);  /* 7z PPMd is size-bounded: no end marker */
            done += ents[i].size;
            if (opt && opt->progress && opt->progress(opt->progress_ctx, done, size)) cancelled = true;
        }
        Ppmd7z_Flush_RangeEnc(&p);
        Ppmd7_Free(&p, &isz.vt);
        fulta_arc_err_t r = FULTA_ARC_OK;
        if (cancelled) r = FULTA_ARC_ERR_CANCELLED;
        else if (bo.oom) r = FULTA_ARC_ERR_NOMEM;
        else if (bo.len && sz_out_write(&outs->vt, bo.buf, bo.len) != bo.len) r = o->failed ? FULTA_ARC_ERR_IO : FULTA_ARC_ERR_NOMEM;
        fa_release(o->alloc, bo.buf);
        if (r == FULTA_ARC_OK) { coder->props = props; coder->nprops = 5; }
        return r;
    }
    /* BZip2 or Deflate: push-encode, no properties */
    if (m == FULTA_ARC_METHOD_BZIP2) { coder->id[0] = 0x04; coder->id[1] = 0x02; coder->id[2] = 0x02; }
    else                             { coder->id[0] = 0x04; coder->id[1] = 0x01; coder->id[2] = 0x08; }
    coder->idlen = 3;
    fulta_arc_err_t pe = encode_7z_push(m, level, ents, nents, size, outs, opt);
    if (pe) return pe;
    if (o->failed) return FULTA_ARC_ERR_IO;
    if (outs->buf && outs->buf->failed) return FULTA_ARC_ERR_NOMEM;
    return FULTA_ARC_OK;
}

/* Split the x86 stream `src[0..n)` into the four BCJ2 streams (MAIN/CALL/JUMP/RC), each into a freshly malloc'd
 * buffer in out[] with its length in sizes[] (indices BCJ2_STREAM_*). The whole input is given at once with
 * END_STREAM; output buffers grow on demand (CALL/JUMP kept a multiple of 4, as Bcj2.h requires). The caller frees
 * out[0..3] (set to NULL on failure). */
static fulta_arc_err_t bcj2_split(proven_allocator_t a, const uint8_t *src, size_t n, uint8_t *out[4], size_t sizes[4]) {
    for (int i = 0; i < 4; i++) out[i] = NULL;
    CBcj2Enc e;
    Bcj2Enc_Init(&e);
    e.finishMode = BCJ2_ENC_FINISH_MODE_END_STREAM;      /* the whole input is available in one shot */
    e.src = src; e.srcLim = src + n;
    Bcj2Enc_SET_FileSize(&e, n);
    size_t cap[4]; uint8_t *base[4];
    for (int i = 0; i < 4; i++) {
        cap[i] = (i == BCJ2_STREAM_MAIN) ? n + 16 : 256;
        cap[i] = (cap[i] + 3) & ~(size_t)3;              /* CALL/JUMP buffer sizes must be a multiple of 4 */
        base[i] = fa_alloc(a, cap[i]);
        if (!base[i]) { for (int j = 0; j < i; j++) fa_release(a, base[j]); return FULTA_ARC_ERR_NOMEM; }
        e.bufs[i] = base[i]; e.lims[i] = base[i] + cap[i];
    }
    size_t last_progress = SIZE_MAX; int stall = 0;
    for (;;) {
        Bcj2Enc_Encode(&e);
        if (e.state == BCJ2_ENC_STATE_FINISHED) break;
        size_t prog = (size_t)(e.src - src);
        for (int i = 0; i < 4; i++) prog += (size_t)(e.bufs[i] - base[i]);
        if (prog == last_progress) { if (++stall >= 2) { for (int j = 0; j < 4; j++) fa_release(a, base[j]); return FULTA_ARC_ERR_CORRUPT; } }
        else { stall = 0; last_progress = prog; }
        if (e.state < BCJ2_NUM_STREAMS) {                /* that output stream's buffer is full: grow it */
            int s = (int)e.state;
            size_t used = (size_t)(e.bufs[s] - base[s]);
            size_t ncap = (cap[s] * 2 + 3) & ~(size_t)3;
            uint8_t *nb = fa_resize(a, base[s], cap[s], ncap);
            if (!nb) { for (int j = 0; j < 4; j++) fa_release(a, base[j]); return FULTA_ARC_ERR_NOMEM; }
            base[s] = nb; cap[s] = ncap;
            e.bufs[s] = base[s] + used; e.lims[s] = base[s] + cap[s];
        } else {                                         /* ORIG: wants more src, but END_STREAM had it all */
            for (int j = 0; j < 4; j++) fa_release(a, base[j]);
            return FULTA_ARC_ERR_CORRUPT;
        }
    }
    for (int i = 0; i < 4; i++) { out[i] = base[i]; sizes[i] = (size_t)(e.bufs[i] - base[i]); }
    return FULTA_ARC_OK;
}

/* Write a BCJ2 folder (phase 7): split the solid plaintext into MAIN/CALL/JUMP/RC, compress the first three with
 * method `m` (RC stored raw), emit their packed bytes to `o` in that order, and fill *F with the 4-coder folder
 * (main/call/jump compressors + the 4-input BCJ2 coder) and its bind pairs. `pr[3]` backs the three compressors'
 * properties (must outlive the write_7z_finish call). */
static fulta_arc_err_t encode_7z_bcj2(const fulta_arc_write_entry_t *entries, size_t count, fulta_arc_method_t m,
                                      int level, out_t *o, uint64_t total,
                                      uint8_t pr[3][LZMA_PROPS_SIZE], fa_7z_copy_t *F) {
    uint8_t *buf = fa_alloc(o->alloc, total ? (size_t)total : 1);
    if (!buf) return FULTA_ARC_ERR_NOMEM;
    uint64_t at = 0;
    for (size_t i = 0; i < count; ++i) {
        if (is_dir(&entries[i]) || !entries[i].size) continue;
        memcpy(buf + at, entries[i].data, (size_t)entries[i].size); at += entries[i].size;
    }
    uint8_t *s[4]; size_t sz[4];
    fulta_arc_err_t e = bcj2_split(o->alloc, buf, (size_t)total, s, sz);
    fa_release(o->alloc, buf);
    if (e) return e;

    memset(F, 0, sizeof *F);
    F->has_folder = true;
    F->ncoders = 4;
    F->npacked = 4;
    /* three sub-streams, each compressed by `m` into its own coder; RC stored raw as the fourth packed stream */
    const int order[3] = { BCJ2_STREAM_MAIN, BCJ2_STREAM_CALL, BCJ2_STREAM_JUMP };
    for (int k = 0; k < 3 && !e; ++k) {
        fulta_arc_write_entry_t one = { .name = "s", .data = s[order[k]], .size = sz[order[k]] };
        sz_out_t outs = { .vt = { sz_out_write }, .o = o, .buf = NULL };
        uint64_t start = o->at;
        e = encode_7z_stream(m, level, &one, 1, sz[order[k]], NULL, o, &outs, &F->coders[k], pr[k]);
        F->unpack_sizes[k] = sz[order[k]];
        F->pack_sizes[k] = o->at - start;
        F->packed[k] = (uint32_t)k;                      /* unbound inputs: coder k's input (global index k) */
    }
    if (!e) {                                            /* RC: stored raw */
        put(o, s[BCJ2_STREAM_RC], sz[BCJ2_STREAM_RC]);
        if (o->failed) e = FULTA_ARC_ERR_IO;
    }
    for (int i = 0; i < 4; i++) fa_release(o->alloc, s[i]);
    if (e) return e;

    F->coders[3] = (fa_7z_coder_t){ .id = {0x03, 0x03, 0x01, 0x1B}, .idlen = 4, .nin = 4, .nprops = 0 };
    F->unpack_sizes[3] = total;
    /* input indices: coder0->0, coder1->1, coder2->2, BCJ2->3,4,5,6 (local 0=main,1=call,2=jump,3=rc).
     * bind each BCJ2 compressed input to its compressor's output; RC (global 6) stays an unbound packed stream. */
    F->nbinds = 3;
    F->binds[0][0] = 3; F->binds[0][1] = 0;              /* BCJ2.in0 (main) <- coder0.out */
    F->binds[1][0] = 4; F->binds[1][1] = 1;              /* BCJ2.in1 (call) <- coder1.out */
    F->binds[2][0] = 5; F->binds[2][1] = 2;              /* BCJ2.in2 (jump) <- coder2.out */
    F->packed[3] = 6;                                    /* BCJ2.in3 (rc) <- the fourth packed stream */
    F->pack_sizes[3] = sz[BCJ2_STREAM_RC];
    return FULTA_ARC_OK;
}

/* Emit the multi-folder 7z header (one substream per folder); defined below, used by write_7z's SOLID_OFF path. */
static fulta_arc_err_t write_7z_header_multi(out_t *o, uint8_t *sig, const sz_desc_t *d, size_t count,
                                             const fa_7z_copy_t *of, size_t nf);

/* Persistent per-folder property storage: a folder's coders point their `props` into this, so it must outlive the
 * write_7z_finish / write_7z_header_multi call that emits the header (one instance per folder). */
typedef struct {
    uint8_t mprops[LZMA_PROPS_SIZE];                     /* the main coder's properties */
    uint8_t aesprops[34];                                /* 7zAES coder properties (power+flags, salt16, iv16) */
    uint8_t fprop;                                       /* Delta's one byte (distance - 1) */
    uint8_t bprops[3][LZMA_PROPS_SIZE];                  /* BCJ2 sub-stream compressors' properties */
} fold_scratch_t;

/* Encode one folder from the group ents[0..nents): compress (with the method, any content filter, or BCJ2) and,
 * when `pw`, 7zAES over the result, writing the packed bytes to `o` and filling *F (coders, binds, packed/unpack
 * sizes, and the folder's crc+size). `sc` backs the coders' properties and must outlive the header emission. For a
 * solid folder `ents` is every entry (dirs/empty skipped); for a non-solid (SOLID_OFF) folder it is one entry. */
static fulta_arc_err_t encode_7z_folder(const fulta_arc_write_entry_t *ents, size_t nents, fulta_arc_method_t m,
                                        int level, bool is_bcj2, const fulta_arc_write_options_t *opt,
                                        const char *pw, out_t *o, fold_scratch_t *sc, fa_7z_copy_t *F) {
    bool enc = pw != NULL;
    uint64_t total = 0; uint32_t crc = 0;
    for (size_t i = 0; i < nents; ++i)
        if (ents[i].size && !is_dir(&ents[i])) { total += ents[i].size; crc = fa_crc32(crc, fa_bytes(ents[i].data, (size_t)ents[i].size)); }
    memset(F, 0, sizeof *F);
    if (total == 0) return FULTA_ARC_OK;                 /* no stream: caller does not emit a folder */

    if (is_bcj2) {
        fulta_arc_err_t be = encode_7z_bcj2(ents, nents, m, level, o, total, sc->bprops, F);
        if (be) return be;
    } else {
        /* a content filter (phase 6) transforms the plaintext first; the main coder then compresses the filtered
         * bytes, presented as one synthetic entry so every method path is reused unchanged */
        uint8_t *fbuf = NULL;
        const fulta_arc_write_entry_t *ments = ents; size_t mcount = nents;
        fulta_arc_write_entry_t syn;
        fa_7z_coder_t fcoder = {0};
        bool have_filter = false;
        if (opt && opt->filter != FULTA_ARC_FILTER_NONE) {
            fulta_arc_err_t ae = apply_filter_7z(o->alloc, opt->filter, opt->filter_delta_dist, ents, nents, total, &fbuf, &fcoder, &sc->fprop);
            if (ae) return ae;
            have_filter = true;
            syn = (fulta_arc_write_entry_t){ .name = "f", .data = fbuf, .size = total, .mtime = 0 };
            ments = &syn; mcount = 1;
        }
        fa_7z_coder_t inner = { .nin = 1 };
        buf_t comp = {.alloc = o->alloc};                                /* AES: the inner coder's output, captured for encrypting */
        sz_out_t outs = { .vt = { sz_out_write }, .o = o, .buf = enc ? &comp : NULL };
        uint64_t start = o->at;
        fulta_arc_err_t ce = encode_7z_stream(m, level, ments, mcount, total, opt, o, &outs, &inner, sc->mprops);
        fa_release(o->alloc, fbuf);                                      /* the filtered plaintext is fully consumed by the main coder */
        if (ce) { fa_release(o->alloc, comp.p); return ce; }
        uint64_t packed = 0, clen = 0;
        if (enc) {                                       /* 7zAES: AES-256-CBC over the inner output, random salt+iv */
            clen = comp.len;
            uint8_t salt[16], iv[16], key[32];
            if (!fa_random_bytes(salt, 16) || !fa_random_bytes(iv, 16)) { fa_release(o->alloc, comp.p); return FULTA_ARC_ERR_IO; }
            fulta_arc_err_t ke = fa_7z_derive_key(proven_heap_allocator(), pw, salt, 16, 19, key);
            if (ke) { fa_release(o->alloc, comp.p); return ke; }
            size_t ct = (size_t)((clen + 15) & ~(uint64_t)15);
            uint8_t *cbuf = fa_zalloc(o->alloc, ct ? ct : 1, 1);
            if (!cbuf) { fa_release(o->alloc, comp.p); memset(key, 0, 32); return FULTA_ARC_ERR_NOMEM; }
            memcpy(cbuf, comp.p, (size_t)clen);
            uint8_t ivc[16]; memcpy(ivc, iv, 16);
            fa_aes_cbc_encrypt(key, 32, ivc, cbuf, ct);
            memset(key, 0, 32);
            put(o, cbuf, ct);
            fa_release(o->alloc, cbuf);
            fa_release(o->alloc, comp.p);
            packed = ct;
            sc->aesprops[0] = (uint8_t)(0xC0 | 19);      /* salt & iv present, NumCyclesPower = 19 */
            sc->aesprops[1] = 0xFF;                      /* salt_len = iv_len = 16 (high bits in [0]) */
            memcpy(sc->aesprops + 2, salt, 16);
            memcpy(sc->aesprops + 18, iv, 16);
            if (o->failed) return FULTA_ARC_ERR_IO;
        } else {
            packed = o->at - start;
        }
        /* Assemble: coder 0 is the main method; a filter and 7zAES chain after it. Decode flows
         * packed -> [AES] -> main -> [filter] -> plaintext. */
        F->has_folder = true;
        F->npacked = 1;
        F->pack_sizes[0] = packed;
        uint32_t nc = 0;
        F->coders[nc] = inner; F->unpack_sizes[nc] = total; nc++;
        uint32_t filt_idx = 0, aes_idx = 0;
        if (have_filter) { F->coders[nc] = fcoder; F->unpack_sizes[nc] = total; filt_idx = nc; nc++; }
        if (enc) {
            fa_7z_coder_t ac = { .id = {0x06, 0xF1, 0x07, 0x01}, .idlen = 4, .nin = 1, .props = sc->aesprops, .nprops = 34 };
            F->coders[nc] = ac; F->unpack_sizes[nc] = clen; aes_idx = nc; nc++;
        }
        F->ncoders = nc;
        uint32_t nb = 0;
        if (enc)         { F->binds[nb][0] = 0;        F->binds[nb][1] = aes_idx; nb++; }   /* main.in0 <- AES.out */
        if (have_filter) { F->binds[nb][0] = filt_idx; F->binds[nb][1] = 0;       nb++; }   /* filter.in <- main.out */
        F->nbinds = nb;
    }
    F->size = total; F->crc = crc; F->has_crc = true;    /* used by write_7z_header_multi (SOLID_OFF) */
    return FULTA_ARC_OK;
}

static fulta_arc_err_t write_7z(const fulta_arc_write_entry_t *entries, size_t count, int level,
                                   const fulta_arc_write_options_t *opt, out_t *o) {
    const char *pw = opt ? opt->password : NULL;
    if (pw && opt->encryption != FULTA_ARC_ENCRYPT_DEFAULT && opt->encryption != FULTA_ARC_ENCRYPT_7Z_AES256)
        return FULTA_ARC_ERR_INVALID_ARG;                /* a ZIP scheme for a 7z */
    bool enc = pw != NULL;

    fulta_arc_method_t m;
    fulta_arc_err_t me = method_concrete(FULTA_ARC_FORMAT_7Z, level, opt ? opt->method : FULTA_ARC_METHOD_DEFAULT, &m);
    if (me) return me;                                   /* INVALID_ARG: method cannot apply to 7z (e.g. zstd) */
    fulta_arc_err_t fe = filter_check(FULTA_ARC_FORMAT_7Z, opt);
    if (fe) return fe;                                   /* filters: RFC-0004 phases 6-7 */
    bool is_bcj2 = opt && opt->filter == FULTA_ARC_FILTER_BCJ2;
    if (is_bcj2 && enc) return FULTA_ARC_ERR_UNSUPPORTED;   /* BCJ2 + 7zAES (per-packed-stream AES) is a later increment */

    /* the signature header, written again at the end with where the header is */
    uint8_t sig[32] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C, 0, 4 };
    put(o, sig, sizeof(sig));
    if (o->failed) return FULTA_ARC_ERR_IO;

    size_t streams = 0;
    for (size_t i = 0; i < count; ++i) if (entries[i].size && !is_dir(&entries[i])) streams++;
    bool solid_off = opt && opt->solid == FULTA_ARC_SOLID_OFF;

    /* describe each entry for the header (its bytes are written/encoded below) */
    sz_desc_t *desc = fa_zalloc(o->alloc, count ? count : 1, sizeof *desc);
    if (!desc) return FULTA_ARC_ERR_NOMEM;
    for (size_t i = 0; i < count; ++i) {
        bool dir = is_dir(&entries[i]);
        desc[i] = (sz_desc_t){ .name = entries[i].name, .dir = dir, .size = entries[i].size,
                               .crc = (entries[i].size && !dir) ? crc32_of(entries[i].data, entries[i].size) : 0,
                               .mtime = entries[i].mtime };
    }

    fulta_arc_err_t err = FULTA_ARC_OK;
    if (solid_off) {
        /* one folder per file -> the result is editable in place (RFC-0004 §5a). Each streaming entry becomes its
         * own folder (same coder machinery as a solid folder, applied to a single entry); directories and empty
         * files get no folder, in the entry order write_7z_header_multi expects. */
        fold_scratch_t *scs = fa_zalloc(o->alloc, streams ? streams : 1, sizeof *scs);
        fa_7z_copy_t *fols = fa_zalloc(o->alloc, streams ? streams : 1, sizeof *fols);
        if (!scs || !fols) { fa_release(o->alloc, scs); fa_release(o->alloc, fols); fa_release(o->alloc, desc); return FULTA_ARC_ERR_NOMEM; }
        size_t nf = 0;
        for (size_t i = 0; i < count && err == FULTA_ARC_OK; ++i) {
            if (is_dir(&entries[i]) || !entries[i].size) continue;
            err = encode_7z_folder(&entries[i], 1, m, level, is_bcj2, opt, pw, o, &scs[nf], &fols[nf]);
            if (!err) nf++;
        }
        if (err == FULTA_ARC_OK) err = write_7z_header_multi(o, sig, desc, count, fols, nf);
        fa_release(o->alloc, scs); fa_release(o->alloc, fols);
    } else {
        /* a single solid folder for all files (the default / SOLID_ON) */
        fa_7z_copy_t F = {0};
        fold_scratch_t sc;
        if (streams > 0) err = encode_7z_folder(entries, count, m, level, is_bcj2, opt, pw, o, &sc, &F);
        if (err == FULTA_ARC_OK) err = write_7z_finish(o, sig, desc, count, streams, &F);
    }
    fa_release(o->alloc, desc);
    return err;
}

/* ---- 7z convert route (any open archive -> a fresh 7z) ----
 *
 * The LZMA encoder PULLS its input (ISeqInStream), but extraction PUSHES into a sink, and an entry may be multi-GiB,
 * so we cannot hold it all in memory. Bridge the two with a disk spill: pass 1 extracts/pulls every entry's
 * plaintext into one spill file (recording each entry's size and crc as it goes, bounded memory); pass 2 LZMA-
 * encodes by reading that spill sequentially into the encoder; pass 3 emits the 7z header from the recorded sizes/
 * crcs. Memory stays bounded; disk does not (the spill holds the whole plaintext). With a password the folder is
 * 7zAES-encrypted as the encoder writes it (sz_cbc_t). Because the spill then holds plaintext the output protects,
 * it is made private and nameless at once, whether or not a password is set: a fresh directory only the owner may
 * enter, the file in it mode 0600, both removed by name right after the open, the open handle used for both passes
 * (Windows: the per-user temp directory's ACL, and the name removed while open, which proven_fs_open's delete sharing
 * allows; what the platform keeps until the close goes then). */

/* Pass 1 state: one entry's plaintext streamed to the spill, its size and crc accruing. */
typedef struct sz_spill_w {
    proven_file_t f;
    uint32_t crc;
    uint64_t size;
    const fulta_arc_write_options_t *opt;
    uint64_t *done, total;
    fulta_arc_err_t err;
} sz_spill_w_t;

static fulta_arc_err_t spill_feed(sz_spill_w_t *w, const void *data, size_t n) {
    if (w->err) return w->err;
    if (n) {
        if (proven_fs_write_all(w->f, (proven_mem_view_t){.ptr = data, .size = n})) return w->err = FULTA_ARC_ERR_IO;
        w->crc = fa_crc32(w->crc, fa_bytes(data, n));
        w->size += n;
    }
    if (w->opt && w->opt->progress) {
        *w->done += n;
        if (w->opt->progress(w->opt->progress_ctx, *w->done, w->total)) return w->err = FULTA_ARC_ERR_CANCELLED;
    }
    return FULTA_ARC_OK;
}
static fulta_arc_err_t spill_sink_write(void *ctx, const void *data, size_t n) {
    return spill_feed((sz_spill_w_t *)ctx, data, n);
}

/* Convert pass 2/3 for a non-default selector (step 2): read the spill into memory, present each streaming entry as
 * a synthetic in-memory entry, and reuse write_7z's folder machinery so method/filter/BCJ2/SOLID_OFF all apply. This
 * is where the convert route's "bounded memory" weakens to the total (solid) or the largest entry (SOLID_OFF),
 * which is why the DEFAULT path keeps the streaming spill encode. A password encrypts each folder as write_7z does. */
static fulta_arc_err_t convert_7z_buffered(spill_t *spill, uint64_t total_un,
                                           const sz_desc_t *desc, size_t count, size_t streams,
                                           fulta_arc_method_t m, int level, bool is_bcj2, bool solid_off,
                                           const fulta_arc_write_options_t *opt, const char *pw, out_t *o, uint8_t *sig) {
    uint8_t *allbuf = fa_alloc(o->alloc, total_un ? (size_t)total_un : 1);
    if (!allbuf) return FULTA_ARC_ERR_NOMEM;
    fulta_arc_err_t err = spill_rewind(spill);
    uint64_t gotn = 0;
    while (!err && gotn < total_un) {
        proven_result_size_t r = proven_fs_read(spill->f, (proven_mem_mut_t){.ptr = allbuf + gotn, .size = (size_t)(total_un - gotn)});
        if (r.err == PROVEN_ERR_EOF) { gotn += r.value; break; }
        if (r.err) { err = FULTA_ARC_ERR_IO; break; }
        gotn += r.value;
    }
    if (!err && gotn != total_un) err = FULTA_ARC_ERR_IO;
    if (err) { memset(allbuf, 0, (size_t)gotn); fa_release(o->alloc, allbuf); return err; }

    fulta_arc_write_entry_t *synth = fa_zalloc(o->alloc, streams ? streams : 1, sizeof *synth);
    if (!synth) { fa_release(o->alloc, allbuf); return FULTA_ARC_ERR_NOMEM; }
    size_t ns = 0; uint64_t off = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!desc[i].size || desc[i].dir) continue;
        synth[ns++] = (fulta_arc_write_entry_t){ .name = "e", .data = allbuf + off, .size = desc[i].size };
        off += desc[i].size;
    }

    if (!solid_off) {                                    /* one solid folder for all entries */
        fold_scratch_t sc; fa_7z_copy_t F = {0};
        err = encode_7z_folder(synth, ns, m, level, is_bcj2, opt, pw, o, &sc, &F);
        if (!err) err = write_7z_finish(o, sig, desc, count, streams, &F);
    } else {                                             /* one folder per entry -> editable in place */
        fold_scratch_t *scs = fa_zalloc(o->alloc, ns ? ns : 1, sizeof *scs);
        fa_7z_copy_t *fols = fa_zalloc(o->alloc, ns ? ns : 1, sizeof *fols);
        if (!scs || !fols) { fa_release(o->alloc, scs); fa_release(o->alloc, fols); fa_release(o->alloc, synth); fa_release(o->alloc, allbuf); return FULTA_ARC_ERR_NOMEM; }
        for (size_t k = 0; k < ns && !err; ++k)
            err = encode_7z_folder(&synth[k], 1, m, level, is_bcj2, opt, pw, o, &scs[k], &fols[k]);
        if (!err) err = write_7z_header_multi(o, sig, desc, count, fols, ns);
        fa_release(o->alloc, scs); fa_release(o->alloc, fols);
    }
    fa_release(o->alloc, synth);
    memset(allbuf, 0, (size_t)total_un);
    fa_release(o->alloc, allbuf);
    return err;
}

fulta_arc_err_t fa_write_7z_convert(const fa_edit_item_t *items, size_t count, int level,
                                    const fulta_arc_write_options_t *opt, const fulta_arc_write_sink_t *sink,
                                    const char *spill_hint) {
    fa_isz_t isz = isz_of(fa_write_alloc(opt));   /* the encoders' own allocations go to the writer's allocator */
    proven_allocator_t a = fa_write_alloc(opt);
    if (!sink || !sink->write || !sink->patch) return FULTA_ARC_ERR_INVALID_ARG;
    const char *pw = opt ? opt->password : NULL;
    if (pw && opt->encryption != FULTA_ARC_ENCRYPT_DEFAULT && opt->encryption != FULTA_ARC_ENCRYPT_7Z_AES256)
        return FULTA_ARC_ERR_INVALID_ARG;                /* a ZIP scheme for a 7z */
    if (pw && opt->filter == FULTA_ARC_FILTER_BCJ2) return FULTA_ARC_ERR_UNSUPPORTED;   /* as write_7z */

    spill_t spill;
    fulta_arc_err_t err = spill_open(a, spill_hint, &spill);
    if (err) return err;

    sz_desc_t *desc = fa_zalloc(a, count ? count : 1, sizeof *desc);
    if (!desc) { spill_close(&spill); return FULTA_ARC_ERR_NOMEM; }

    /* progress total: the plaintext sizes we know up front (same as the ZIP convert) */
    uint64_t total = 0, done = 0;
    for (size_t i = 0; i < count; ++i) {
        if (items[i].is_copy) {
            const fulta_arc_entry_t *e = fulta_arc_entry(items[i].src, items[i].src_index);
            if (e && !(e->flags & (FULTA_ARC_ENTRY_DIR | FULTA_ARC_ENTRY_UNKNOWN_SIZE))) total += e->size;
        } else if (items[i].reader.read && items[i].reader.size != UINT64_MAX) {
            total += items[i].reader.size;
        }
    }

    /* pass 1: every entry's plaintext to the spill, recording size + crc (source order == folder order) */
    sz_spill_w_t w = { .f = spill.f, .opt = opt, .done = &done, .total = total };
    for (size_t i = 0; i < count && err == FULTA_ARC_OK; ++i) {
        const fa_edit_item_t *it = &items[i];
        const char *uname;
        bool dir;
        int64_t mtime_sec = 0;
        if (it->is_copy) {
            const fulta_arc_entry_t *e = fulta_arc_entry(it->src, it->src_index);
            if (!e) { err = FULTA_ARC_ERR_INVALID_ARG; break; }
            dir = (e->flags & FULTA_ARC_ENTRY_DIR) != 0;
            uname = it->name ? it->name : e->name;
            mtime_sec = (e->flags & FULTA_ARC_ENTRY_HAS_MTIME) ? e->mtime / 1000000000 : 0;
        } else {
            dir = name_is_dir(it->name);
            uname = it->name;
            mtime_sec = it->reader.mtime;                /* already seconds */
        }
        if (!uname) { err = FULTA_ARC_ERR_INVALID_ARG; break; }
        w.crc = 0; w.size = 0;
        if (!dir) {
            if (it->is_copy) {
                fulta_arc_sink_t s = { .ctx = &w, .write = spill_sink_write };
                err = fulta_arc_extract(it->src, it->src_index, &s);
                if (err == FULTA_ARC_OK) err = w.err;
            } else if (it->reader.read) {
                uint8_t chunk[1 << 16];
                for (;;) {
                    size_t got = 0;
                    err = it->reader.read(it->reader.ctx, chunk, sizeof chunk, &got);
                    if (err || got == 0) break;
                    err = spill_feed(&w, chunk, got);
                    if (err) break;
                }
            }
            if (err) break;
        }
        /* decide stream-ness from the bytes actually produced, not the declared size */
        desc[i] = (sz_desc_t){ .name = uname, .dir = dir, .size = dir ? 0 : w.size, .crc = w.crc, .mtime = mtime_sec };
    }
    if (err == FULTA_ARC_OK && proven_fs_sync(spill.f)) err = FULTA_ARC_ERR_IO;   /* surface a deferred write error */

    /* streams and the exact unpacked total, from pass 1 */
    size_t streams = 0;
    uint64_t total_un = 0;
    for (size_t i = 0; i < count; ++i)
        if (desc[i].size && !desc[i].dir) { streams++; total_un += desc[i].size; }

    out_t o = { .sink = sink, .alloc = fa_write_alloc(opt) };
    o.hdr_pw = opt && opt->password && opt->encrypt_names ? opt->password : NULL;   /* 7z -mhe */
    uint8_t props[LZMA_PROPS_SIZE];
    SizeT props_size = sizeof props;
    uint64_t packed = 0;
    fa_7z_coder_t inner = { .nin = 1 };                  /* DEFAULT path: Copy (level 0) or LZMA */
    sz_cbc_t cbc = {0};                                  /* DEFAULT path with a password: 7zAES as it is written */
    uint8_t aesprops[34];
    bool enc = false;

    /* resolve the selector (step 2). DEFAULT (method DEFAULT, no filter, solid != OFF) keeps the streaming
       spill->single-folder encode below (bounded memory); anything else buffers via convert_7z_buffered. */
    fulta_arc_method_t m = FULTA_ARC_METHOD_LZMA;
    bool is_bcj2 = false, solid_off = false, dflt = true;
    if (err == FULTA_ARC_OK && opt) {
        fulta_arc_err_t me = method_concrete(FULTA_ARC_FORMAT_7Z, level, opt->method, &m);
        if (me) err = me;
        else { fulta_arc_err_t fe = filter_check(FULTA_ARC_FORMAT_7Z, opt); if (fe) err = fe; }
        is_bcj2 = opt->filter == FULTA_ARC_FILTER_BCJ2;
        solid_off = opt->solid == FULTA_ARC_SOLID_OFF;
        dflt = opt->method == FULTA_ARC_METHOD_DEFAULT && opt->filter == FULTA_ARC_FILTER_NONE && !solid_off;
    }

    if (err == FULTA_ARC_OK) {
        uint8_t sig[32] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C, 0, 4 };
        put(&o, sig, sizeof sig);
        if (o.failed) err = FULTA_ARC_ERR_IO;

        if (err == FULTA_ARC_OK && streams > 0 && !dflt) {   /* step 2: non-default selector -> buffered folders */
            err = convert_7z_buffered(&spill, total_un, desc, count, streams, m, level, is_bcj2, solid_off, opt, pw, &o, sig);
        } else if (err == FULTA_ARC_OK && streams > 0) {     /* pass 2: encode the spill into the folder (DEFAULT) */
            if (pw) {                                    /* 7zAES-256: random salt + iv, key as write_7z derives it */
                uint8_t salt[16];
                if (!fa_random_bytes(salt, 16) || !fa_random_bytes(cbc.iv, 16)) err = FULTA_ARC_ERR_IO;   /* no entropy */
                else err = fa_7z_derive_key(proven_heap_allocator(), pw, salt, 16, 19, cbc.key);
                aesprops[0] = (uint8_t)(0xC0 | 19);      /* salt & iv present, NumCyclesPower = 19 */
                aesprops[1] = 0xFF;                      /* salt_len = iv_len = 16 */
                memcpy(aesprops + 2, salt, 16);
                memcpy(aesprops + 18, cbc.iv, 16);
                enc = err == FULTA_ARC_OK;
            }
            proven_file_t rf2 = spill.f;
            if (err == FULTA_ARC_OK) err = spill_rewind(&spill);
            if (err == FULTA_ARC_OK) {
                uint64_t start = o.at;
                if (level == 0) {                        /* Copy: the spill bytes verbatim */
                    uint8_t buf[1 << 16];
                    for (;;) {
                        proven_result_size_t r = proven_fs_read(rf2, (proven_mem_mut_t){.ptr = buf, .size = sizeof buf});
                        if (r.err && r.err != PROVEN_ERR_EOF) { err = FULTA_ARC_ERR_IO; break; }
                        if (r.value) { if (enc) cbc_put(&o, &cbc, buf, r.value); else put(&o, buf, r.value); }
                        if (o.failed) { err = FULTA_ARC_ERR_IO; break; }
                        if (r.err == PROVEN_ERR_EOF) break;
                    }
                    memset(buf, 0, sizeof buf);
                    if (enc) cbc_finish(&o, &cbc);
                    if (err == FULTA_ARC_OK && o.failed) err = FULTA_ARC_ERR_IO;
                    if (err == FULTA_ARC_OK) packed = o.at - start;
                } else {
                    CLzmaEncHandle ench = LzmaEnc_Create(&isz.vt);
                    if (!ench) err = FULTA_ARC_ERR_NOMEM;
                    else {
                        CLzmaEncProps ep;
                        LzmaEncProps_Init(&ep);
                        ep.level = level;
                        ep.reduceSize = total_un;
                        SRes res = LzmaEnc_SetProps(ench, &ep);
                        if (res == SZ_OK) res = LzmaEnc_WriteProperties(ench, props, &props_size);
                        sz_spill_r_t in = { .vt = { sz_spill_read }, .f = rf2 };
                        sz_out_t outs = { .vt = { sz_out_write }, .o = &o, .buf = NULL, .cbc = enc ? &cbc : NULL };
                        sz_progress_t prog = { .vt = { sz_progress }, .opt = opt, .total = total_un };
                        if (res == SZ_OK) res = LzmaEnc_Encode(ench, &outs.vt, &in.vt, &prog.vt, &isz.vt, &isz.vt);
                        LzmaEnc_Destroy(ench, &isz.vt, &isz.vt);
                        if (enc) cbc_finish(&o, &cbc);
                        if (o.failed) err = FULTA_ARC_ERR_IO;
                        else if (res == SZ_ERROR_PROGRESS) err = FULTA_ARC_ERR_CANCELLED;
                        else if (res == SZ_ERROR_MEM) err = FULTA_ARC_ERR_NOMEM;
                        else if (res == SZ_ERROR_READ || in.io_err) err = FULTA_ARC_ERR_IO;
                        else if (res != SZ_OK) err = FULTA_ARC_ERR_CORRUPT;
                        else packed = o.at - start;
                    }
                }
            }
            memset(cbc.key, 0, sizeof cbc.key);          /* wiped on every path (cbc_finish also does) */
        }

        if (level == 0) { inner.id[0] = 0x00; inner.idlen = 1; }   /* Copy */
        else { inner.id[0] = 0x03; inner.id[1] = 0x01; inner.id[2] = 0x01; inner.idlen = 3;
               inner.props = props; inner.nprops = (uint32_t)props_size; }
        if (err == FULTA_ARC_OK && (dflt || streams == 0)) {   /* pass 3: the header (DEFAULT = single coder; the
                                                                  buffered path writes its own multi-coder header) */
            fa_7z_copy_t F = { .has_folder = true, .ncoders = 1, .npacked = 1 };
            F.coders[0] = inner; F.unpack_sizes[0] = total_un; F.pack_sizes[0] = packed;
            if (enc) {                                   /* packed -> 7zAES -> main: as encode_7z_folder chains it */
                fa_7z_coder_t ac = { .id = {0x06, 0xF1, 0x07, 0x01}, .idlen = 4, .nin = 1, .props = aesprops, .nprops = 34 };
                F.coders[1] = ac; F.unpack_sizes[1] = cbc.clen;
                F.ncoders = 2;
                F.binds[0][0] = 0; F.binds[0][1] = 1;    /* main.in0 <- AES.out */
                F.nbinds = 1;
            }
            err = write_7z_finish(&o, sig, desc, count, streams, &F);
        }
    }

    fa_release(a, desc);
    spill_close(&spill);
    return err;
}

/* ---- 7z copy route (edit a non-solid 7z into a 7z) ----
 *
 * Reached only when fulta_arc_editable() == OK, i.e. every source folder holds exactly one file. Each kept entry's
 * folder is re-emitted byte-for-byte and its packed bytes copied without re-encoding; each added/replaced entry
 * becomes a fresh one-coder folder, encoded straight from its (pull) reader — no spill is needed, unlike the
 * convert route, because the new data is already a pull source. The output is a sequence of folders, one per
 * non-empty entry, in entry order; empty files and directories carry no folder. */

/* Copy `len` source bytes at `off` straight into the output (verbatim packed data). */
static fulta_arc_err_t copy_pack_bytes(fulta_arc_t *src, uint64_t off, uint64_t len, out_t *o) {
    uint8_t buf[1 << 16];
    while (len) {
        size_t n = len > sizeof buf ? sizeof buf : (size_t)len;
        fulta_arc_err_t e = fa_7z_copy_read(src, off, buf, n);
        if (e) return e;
        put(o, buf, n);
        if (o->failed) return FULTA_ARC_ERR_IO;
        off += n;
        len -= n;
    }
    return FULTA_ARC_OK;
}

/* Encode one added/replaced entry of the 7z copy route into its own folder. The reader is buffered (bounded by the
 * entry, like the convert route's spill) and then re-encoded through the same folder machinery as write_7z, so the
 * edit route honours the method/filter/BCJ2 selector (RFC-0004 step 7b); copied folders stay verbatim. `sc` backs
 * the folder's coder properties. For DEFAULT this is byte-identical to the previous streaming Copy/LZMA path (same
 * level and reduceSize over the same bytes). */
static fulta_arc_err_t encode_one_folder(const fulta_arc_reader_t *rd, int level,
                                         const fulta_arc_write_options_t *opt, out_t *o,
                                         fold_scratch_t *sc, fa_7z_copy_t *F) {
    fulta_arc_method_t m;
    fulta_arc_err_t me = method_concrete(FULTA_ARC_FORMAT_7Z, level, opt ? opt->method : FULTA_ARC_METHOD_DEFAULT, &m);
    if (me) return me;
    fulta_arc_err_t fe = filter_check(FULTA_ARC_FORMAT_7Z, opt);
    if (fe) return fe;
    bool is_bcj2 = opt && opt->filter == FULTA_ARC_FILTER_BCJ2;

    /* buffer the reader (size known -> one malloc; unknown -> grow) */
    uint64_t cap = (rd->size != UINT64_MAX && rd->size > 0) ? rd->size : 65536;
    uint8_t *buf = fa_alloc(o->alloc, cap);
    if (!buf) return FULTA_ARC_ERR_NOMEM;
    uint64_t n = 0;
    for (;;) {
        if (n == cap) {
            uint64_t ncap = cap * 2;
            uint8_t *nb = fa_resize(fa_write_alloc(opt), buf, (size_t)cap, ncap ? (size_t)ncap : 1);
            if (!nb) { fa_release(o->alloc, buf); return FULTA_ARC_ERR_NOMEM; }
            buf = nb; cap = ncap;
        }
        size_t got = 0;
        fulta_arc_err_t e = rd->read ? rd->read(rd->ctx, buf + n, (size_t)(cap - n), &got) : FULTA_ARC_OK;
        if (e) { fa_release(o->alloc, buf); return e; }
        if (!got) break;
        n += got;
    }

    /* n == 0 (an empty added file): encode_7z_folder leaves F->has_folder false, so the caller emits no folder and
     * the entry becomes a zero-size stream, exactly as write_7z and the convert route treat an empty file. */
    fulta_arc_write_entry_t syn = { .name = "e", .data = buf, .size = n };
    fulta_arc_err_t r = encode_7z_folder(&syn, 1, m, level, is_bcj2, opt, NULL, o, sc, F);
    fa_release(o->alloc, buf);
    return r;
}

/* Emit the multi-folder 7z header for an edited archive. `of[0..nf)` are the output folders (already written to `o`
 * in order); folder i is the i-th entry of `d` that has a stream. CRCs go in kSubStreamsInfo (no folder-level CRC),
 * one substream per folder. */
static fulta_arc_err_t write_7z_header_multi(out_t *o, uint8_t *sig, const sz_desc_t *d, size_t count,
                                             const fa_7z_copy_t *of, size_t nf) {
    buf_t h = {.alloc = o->alloc};
    b_byte(&h, 0x01);                                    /* kHeader */
    if (nf > 0) {
        b_byte(&h, 0x04);                                /* kMainStreamsInfo */
        uint64_t npack_total = 0;
        for (size_t i = 0; i < nf; ++i) npack_total += of[i].npacked;
        b_byte(&h, 0x06);                                /* kPackInfo */
        b_num(&h, 0);                                    /* PackPos: right after the signature header */
        b_num(&h, npack_total);
        b_byte(&h, 0x09);
        for (size_t i = 0; i < nf; ++i)
            for (uint32_t k = 0; k < of[i].npacked; ++k) b_num(&h, of[i].pack_sizes[k]);
        b_byte(&h, 0x00);
        b_byte(&h, 0x07);                                /* kUnPackInfo */
        b_byte(&h, 0x0B); b_num(&h, nf); b_byte(&h, 0);  /* folders, not external */
        for (size_t i = 0; i < nf; ++i) emit_folder_coders(&h, &of[i]);
        b_byte(&h, 0x0C);                                /* kCodersUnPackSize: every coder's output size, in order */
        for (size_t i = 0; i < nf; ++i)
            for (uint32_t c = 0; c < of[i].ncoders; ++c) b_num(&h, of[i].unpack_sizes[c]);
        b_byte(&h, 0x00);                                /* end kUnPackInfo (no folder-level CRC) */
        b_byte(&h, 0x08);                                /* kSubStreamsInfo (one substream per folder) */
        bool all_crc = true;
        for (size_t i = 0; i < nf; ++i) if (!of[i].has_crc) all_crc = false;
        b_byte(&h, 0x0A);                                /* kCRC */
        if (all_crc) {
            b_byte(&h, 1);
            for (size_t i = 0; i < nf; ++i) b_u32(&h, of[i].crc);
        } else {
            b_byte(&h, 0);
            bool *dv = fa_zalloc(o->alloc, nf, sizeof(bool));
            if (!dv) { fa_release(o->alloc, h.p); return FULTA_ARC_ERR_NOMEM; }
            for (size_t i = 0; i < nf; ++i) dv[i] = of[i].has_crc;
            b_bits(&h, dv, nf);
            fa_release(o->alloc, dv);
            for (size_t i = 0; i < nf; ++i) if (of[i].has_crc) b_u32(&h, of[i].crc);
        }
        b_byte(&h, 0x00);                                /* end kSubStreamsInfo */
        b_byte(&h, 0x00);                                /* end StreamsInfo */
    }
    return write_7z_tail(o, sig, &h, d, count, nf);
}

fulta_arc_err_t fa_write_7z_edited(const fa_edit_item_t *items, size_t count, int level,
                                   const fulta_arc_write_options_t *opt, const fulta_arc_write_sink_t *sink) {
    proven_allocator_t a = fa_write_alloc(opt);
    if (!sink || !sink->write || !sink->patch) return FULTA_ARC_ERR_INVALID_ARG;
    if (opt && opt->password) return FULTA_ARC_ERR_UNSUPPORTED;   /* encrypting the output: later increment */

    sz_desc_t *fd = fa_zalloc(a, count ? count : 1, sizeof *fd);
    fa_7z_copy_t *fol = fa_zalloc(a, count ? count : 1, sizeof *fol);
    fold_scratch_t *scr = fa_zalloc(a, count ? count : 1, sizeof *scr);
    if (!fd || !fol || !scr) { fa_release(a, fd); fa_release(a, fol); fa_release(a, scr); return FULTA_ARC_ERR_NOMEM; }

    out_t o = { .sink = sink, .alloc = fa_write_alloc(opt) };
    o.hdr_pw = opt && opt->password && opt->encrypt_names ? opt->password : NULL;   /* 7z -mhe */
    uint8_t sig[32] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C, 0, 4 };
    put(&o, sig, sizeof sig);
    fulta_arc_err_t err = o.failed ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;

    size_t nf = 0;
    for (size_t i = 0; i < count && err == FULTA_ARC_OK; ++i) {
        const fa_edit_item_t *it = &items[i];
        const char *uname;
        bool dir;
        int64_t mtime_sec = 0;
        if (it->is_copy) {
            const fulta_arc_entry_t *e = fulta_arc_entry(it->src, it->src_index);
            if (!e) { err = FULTA_ARC_ERR_INVALID_ARG; break; }
            dir = (e->flags & FULTA_ARC_ENTRY_DIR) != 0;
            uname = it->name ? it->name : e->name;
            mtime_sec = (e->flags & FULTA_ARC_ENTRY_HAS_MTIME) ? e->mtime / 1000000000 : 0;
        } else {
            dir = name_is_dir(it->name);
            uname = it->name;
            mtime_sec = it->reader.mtime;
        }
        if (!uname) { err = FULTA_ARC_ERR_INVALID_ARG; break; }
        fd[i] = (sz_desc_t){ .name = uname, .dir = dir, .size = 0, .crc = 0, .mtime = mtime_sec };
        if (dir) continue;

        if (it->is_copy) {
            fa_7z_copy_t cp;
            err = fa_7z_copy_info(it->src, it->src_index, &cp);
            if (err) break;
            if (!cp.has_folder) continue;                /* an empty file: no stream */
            uint64_t len = 0;
            for (uint32_t k = 0; k < cp.npacked; ++k) len += cp.pack_sizes[k];
            err = copy_pack_bytes(it->src, cp.pack_off, len, &o);
            if (err) break;
            fol[nf] = cp;
            fd[i].size = cp.size;
            nf++;
        } else {
            if (!it->reader.read) continue;              /* an add with no data: an empty file */
            err = encode_one_folder(&it->reader, level, opt, &o, &scr[nf], &fol[nf]);
            if (err) break;
            if (!fol[nf].has_folder) continue;           /* the reader yielded no bytes: a zero-size stream, no folder */
            fd[i].size = fol[nf].size;
            nf++;
        }
    }

    if (err == FULTA_ARC_OK) err = write_7z_header_multi(&o, sig, fd, count, fol, nf);
    fa_release(a, fd);
    fa_release(a, fol);
    fa_release(a, scr);
    return err;
}

static fulta_arc_err_t write_any(fulta_arc_format_t format, const fulta_arc_write_entry_t *entries, size_t count,
                                 const fulta_arc_write_options_t *options, const fulta_arc_write_sink_t *sink) {
    if (!sink || !sink->write || !sink->patch || (count && !entries)) return FULTA_ARC_ERR_INVALID_ARG;
    for (size_t i = 0; i < count; ++i) {
        if (!entries[i].name) return FULTA_ARC_ERR_INVALID_ARG;
        if (!name_ok(proven_u8str_view_from_cstr(entries[i].name))) return FULTA_ARC_ERR_BAD_NAME;
        if (entries[i].size && !entries[i].data && !is_dir(&entries[i])) return FULTA_ARC_ERR_INVALID_ARG;
    }
    if (!fa_write_alloc_ok(options)) return FULTA_ARC_ERR_INVALID_ARG;   /* one or two allocator functions set */
    int level = options ? options->level : 6;
    if (level < 0 || level > 9) level = 6;
    out_t o = { .sink = sink, .alloc = fa_write_alloc(options) };
    o.hdr_pw = options && options->password && options->encrypt_names ? options->password : NULL;   /* 7z -mhe */
    switch (format) {
        case FULTA_ARC_FORMAT_ZIP:
            if (options && options->password && options->encrypt_names) return FULTA_ARC_ERR_INVALID_ARG;
            return write_zip(entries, count, level, options && options->zip64, options, &o);
        case FULTA_ARC_FORMAT_7Z:  return write_7z(entries, count, level, options, &o);
        default:               return FULTA_ARC_ERR_INVALID_ARG;
    }
}

fulta_arc_err_t fulta_arc_write(fulta_arc_format_t format, const fulta_arc_write_entry_t *entries, size_t count,
                                const fulta_arc_write_options_t *options, const fulta_arc_write_sink_t *sink) {
    if (options && options->volume_size) return FULTA_ARC_ERR_INVALID_ARG;   /* one sink holds one file */
    return write_any(format, entries, count, options, sink);
}

/* ---- split output (src/write/volumes.c does the cutting) ---- */

typedef struct write_job {
    fulta_arc_format_t format;
    const fulta_arc_write_entry_t *entries;
    size_t count;
    const fulta_arc_write_options_t *options;
} write_job_t;

static fulta_arc_err_t write_job_run(void *ctx, const fulta_arc_write_sink_t *sink) {
    const write_job_t *j = ctx;
    return write_any(j->format, j->entries, j->count, j->options, sink);
}

fulta_arc_err_t fulta_arc_write_volumes(fulta_arc_format_t format, const fulta_arc_write_entry_t *entries,
                                        size_t count, const fulta_arc_write_options_t *options,
                                        const fulta_arc_volume_sink_t *volumes) {
    if (!options) return FULTA_ARC_ERR_INVALID_ARG;
    write_job_t j = {format, entries, count, options};
    return fa_vol_run(fa_write_alloc(options), format, options->volume_size, volumes, write_job_run, &j);
}

fulta_arc_err_t fa_write_file_split(const char *path, fulta_arc_format_t format, const fulta_arc_write_entry_t *entries,
                                    size_t count, const fulta_arc_write_options_t *options) {
    write_job_t j = {format, entries, count, options};
    return fa_vol_files(fa_write_alloc(options), path, format, options->volume_size, write_job_run, &j);
}
