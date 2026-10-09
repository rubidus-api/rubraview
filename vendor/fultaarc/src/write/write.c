/* write.c - FultaArc: writing ZIP and 7z archives. MIT.
 * Ported from Rubraview's src/core/archive_write.c (MIT, D-68 there; written by Rubraview's owner-side session from
 * PKWARE's APPNOTE and the LZMA SDK's DOC/7zFormat.txt — no clean room is needed for these formats) to FultaArc's
 * API (D-005 #6); folders added. Compressors: miniz's tdefl (MIT), the LZMA SDK's LzmaEnc (public domain). */
#include "../core/internal.h"

#include "miniz.h"
#include "LzmaEnc.h"

#include "proven.h"

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
static void zip_local_header(out_t *o, const zip_record_t *r);

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
static uint8_t *zip_aes_seal(const char *pw, size_t pwlen, int strength, const uint8_t *body, size_t blen,
                             zip_record_t *r, size_t *out_len, fulta_arc_err_t *err) {
    size_t sl = aes_salt_len[strength], kl = aes_key_len[strength];
    uint8_t *blob = malloc(sl + 2 + blen + 10);
    if (!blob) { *err = FULTA_ARC_ERR_NOMEM; return NULL; }
    if (!fa_random_bytes(blob, sl)) { free(blob); *err = FULTA_ARC_ERR_IO; return NULL; }   /* no entropy */
    uint8_t k[66];
    fa_pbkdf2_sha1((const uint8_t *)pw, pwlen, blob, sl, 1000, k, 2 * kl + 2);
    blob[sl] = k[2 * kl]; blob[sl + 1] = k[2 * kl + 1];                 /* 2-byte password verifier */
    if (blen) memcpy(blob + sl + 2, body, blen);
    fa_aes_ctr_le1_xor(k, kl, blob + sl + 2, blen);                    /* AES-CTR */
    uint8_t mac[20];
    fa_hmac_sha1(k + kl, kl, blob + sl + 2, blen, mac);                /* HMAC-SHA1 authentication, 10 bytes */
    memcpy(blob + sl + 2 + blen, mac, 10);
    memset(k, 0, sizeof k);
    r->encrypted = true;
    r->real_method = r->method;
    r->method = 99;
    r->aes_strength = (uint8_t)strength;
    r->flags |= 1;
    r->crc = 0;                                                        /* AE-2: no plaintext CRC */
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
    uint64_t cd_start = o->at;
    for (size_t i = 0; i < count; ++i) {
        const zip_record_t *r = &rec[i];
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
        le16(h + 30, (uint32_t)x64_len + (r->mtime ? 9u : 0u) + (r->encrypted ? 11u : 0u));
        le16(h + 32, 0);                              /* comment */
        le16(h + 34, 0);                              /* disk */
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
    return o->failed ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;
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
        r->name_len = strlen(r->name);
        r->made_by_host = 0;                              /* MS-DOS */
        r->ext_attr = dir ? 0x10u : 0u;                   /* MS-DOS directory bit */
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
        /* WinZip AES: seal the body (salt | verifier | ciphertext | mac) and rewrite method/flags/crc/packed */
        uint8_t *blob = NULL; size_t blob_len = 0;
        if (enc_strength && !dir) {
            const uint8_t *body = packed ? (const uint8_t *)packed : (const uint8_t *)e->data;
            blob = zip_aes_seal(pw, pwlen, enc_strength, body, (size_t)r->packed, r, &blob_len, &err);
            if (!blob) { free(packed); break; }
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
        free(blob);
        free(packed);
        if (o->failed) { err = FULTA_ARC_ERR_IO; break; }
        done += e->size;
        if (opt && opt->progress && opt->progress(opt->progress_ctx, done, total)) err = FULTA_ARC_ERR_CANCELLED;
    }
    if (err == FULTA_ARC_OK) err = write_zip_central(o, rec, count, force64);
    for (size_t i = 0; i < count; ++i) free(rec[i].name);
    free(rec);
    return err;
}

/* Write a regenerated local file header (no data descriptor; real sizes/crc) from a filled record. */
static void zip_local_header(out_t *o, const zip_record_t *r) {
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
    le16(h + 28, (r->zip64 ? 20u : 0u) + (r->mtime ? 9u : 0u) + (r->encrypted ? 11u : 0u));
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
}

/* Pull every byte from a reader into a freshly malloc'd buffer (*out, *n). Caller frees. */
static fulta_arc_err_t slurp_reader(const fulta_arc_reader_t *rd, uint8_t **out, uint64_t *n) {
    *out = NULL; *n = 0;
    size_t cap = rd->size != UINT64_MAX && rd->size < (1u << 20) ? (size_t)rd->size : 1u << 16;
    uint8_t *buf = malloc(cap ? cap : 1);
    if (!buf) return FULTA_ARC_ERR_NOMEM;
    uint64_t len = 0;
    for (;;) {
        if (len == cap) {
            size_t ncap = cap < (1u << 26) ? cap * 2 : cap + (1u << 26);
            uint8_t *nb = realloc(buf, ncap);
            if (!nb) { free(buf); return FULTA_ARC_ERR_NOMEM; }
            buf = nb; cap = ncap;
        }
        size_t got = 0;
        fulta_arc_err_t e = rd->read(rd->ctx, buf + len, cap - (size_t)len, &got);
        if (e) { free(buf); return e; }
        if (got == 0) break;
        len += got;
    }
    *out = buf; *n = len;
    return FULTA_ARC_OK;
}

static bool name_is_dir(const char *name) {
    size_t n = strlen(name);
    return n && (name[n - 1] == '/' || name[n - 1] == '\\');
}

fulta_arc_err_t fa_write_zip_edited(const fa_edit_item_t *items, size_t count, int level, bool force64,
                                    const fulta_arc_write_options_t *opt, const fulta_arc_write_sink_t *sink) {
    if (!sink || !sink->write || !sink->patch) return FULTA_ARC_ERR_INVALID_ARG;
    if (opt && opt->password) return FULTA_ARC_ERR_UNSUPPORTED;   /* encrypting write is D-016, not yet */
    out_t o = { .sink = sink };
    zip_record_t *rec = calloc(count ? count : 1, sizeof *rec);
    fa_zip_copy_t *cis = calloc(count ? count : 1, sizeof *cis);
    if (!rec || !cis) { free(rec); free(cis); return FULTA_ARC_ERR_NOMEM; }
    fulta_arc_err_t err = FULTA_ARC_OK;
    mz_uint zf = tdefl_create_comp_flags_from_zip_params(level, -15, MZ_DEFAULT_STRATEGY);

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

    for (size_t i = 0; i < count && err == FULTA_ARC_OK; ++i) {
        const fa_edit_item_t *it = &items[i];
        zip_record_t *r = &rec[i];
        r->offset = o.at;
        if (it->is_copy) {
            const fa_zip_copy_t *ci = &cis[i];
            r->dir = ci->is_dir;
            if (it->name) {                               /* renamed: UTF-8 name */
                r->name = stored_name(it->name, ci->is_dir, true);
                if (!r->name) { err = FULTA_ARC_ERR_NOMEM; break; }
                r->name_len = strlen(r->name);
                r->flags = ascii(r->name) ? 0 : 0x0800;
            } else {                                      /* keep the raw name bytes and the UTF-8 flag bit */
                r->name = malloc(ci->name_raw_len ? ci->name_raw_len : 1);
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
            r->name = stored_name(it->name, dir, true);
            if (!r->name) { err = FULTA_ARC_ERR_NOMEM; break; }
            r->name_len = strlen(r->name);
            r->flags = ascii(r->name) ? 0 : 0x0800;
            r->made_by_host = 0;
            r->ext_attr = dir ? 0x10u : 0u;
            r->mtime = it->reader.mtime;
            dos_time(it->reader.mtime, &r->time, &r->date);
            uint8_t *data = NULL; uint64_t dn = 0;
            if (!dir && it->reader.read) { err = slurp_reader(&it->reader, &data, &dn); if (err) break; }
            r->size = dn;
            r->crc = dir ? 0 : crc32_of(data, dn);
            void *packed = NULL; size_t packed_len = 0;
            if (level > 0 && dn > 0 && dn <= SIZE_MAX) {
                packed = tdefl_compress_mem_to_heap(data, (size_t)dn, &packed_len, (int)zf);
                if (packed && packed_len >= dn) { free(packed); packed = NULL; }
            }
            r->method = packed ? 8 : 0;
            r->packed = packed ? packed_len : dn;
            r->zip64 = force64 || r->size >= 0xFFFFFFFFu || r->packed >= 0xFFFFFFFFu;
            zip_local_header(&o, r);
            if (packed) put(&o, packed, packed_len);
            else if (dn) {
                for (uint64_t a = 0; a < dn && !o.failed;) {
                    size_t n = dn - a > (1u << 30) ? (1u << 30) : (size_t)(dn - a);
                    put(&o, data + a, n); a += n;
                }
            }
            free(packed); free(data);
            done += dn;
            if (opt && opt->progress && opt->progress(opt->progress_ctx, done, total)) err = FULTA_ARC_ERR_CANCELLED;
        }
        if (!err && o.failed) err = FULTA_ARC_ERR_IO;
    }
    if (err == FULTA_ARC_OK) err = write_zip_central(&o, rec, count, force64);
    for (size_t i = 0; i < count; ++i) free(rec[i].name);
    free(rec); free(cis);
    return err;
}

/* ---- convert route: re-encode any open archive's entries into a fresh ZIP, streaming -------------------------
 * Each entry's plaintext is produced once (fulta_arc_extract pushes it for a source entry, or the item's reader is
 * pulled for an add/replace) and fed straight into an incremental deflate whose output goes to the sink; nothing is
 * held whole in memory, so a multi-GiB entry converts in bounded space. The local header is written with zeroed
 * crc/sizes, then patched (sink->patch) once the real values are known. Encrypting the output is a later increment
 * (refused here); editing an encrypted source is refused earlier (fulta_arc_editable). */

static fulta_arc_err_t patch_u32(out_t *o, uint64_t off, uint32_t v) {
    uint8_t b[4]; le32(b, v);
    return o->sink->patch(o->sink->ctx, off, b, 4);
}
static fulta_arc_err_t patch_u64(out_t *o, uint64_t off, uint64_t v) {
    uint8_t b[8]; le64(b, v);
    return o->sink->patch(o->sink->ctx, off, b, 8);
}

/* The running state while one entry is pushed through deflate (or store) into the ZIP sink. */
typedef struct conv_zip {
    out_t *o;
    tdefl_compressor *comp;    /* NULL => store */
    uint32_t crc;
    uint64_t usize, csize;
    const fulta_arc_write_options_t *opt;
    uint64_t *done, total;     /* progress across the whole convert */
    fulta_arc_err_t err;
} conv_zip_t;

/* miniz put-buf callback: the deflated bytes of the current entry. */
static mz_bool conv_put(const void *buf, int len, void *user) {
    conv_zip_t *c = (conv_zip_t *)user;
    put(c->o, buf, (size_t)len);
    c->csize += (size_t)len;
    return c->o->failed ? MZ_FALSE : MZ_TRUE;
}

/* Feed `n` plaintext bytes: update crc, count, compress-or-store, poll progress/cancel. */
static fulta_arc_err_t conv_feed(conv_zip_t *c, const void *data, size_t n) {
    if (c->err) return c->err;
    if (n) {
        c->crc = fa_crc32(c->crc, data, n);
        c->usize += n;
        if (c->comp) {
            if (tdefl_compress_buffer(c->comp, data, n, TDEFL_NO_FLUSH) != TDEFL_STATUS_OKAY)
                return c->err = (c->o->failed ? FULTA_ARC_ERR_IO : FULTA_ARC_ERR_CORRUPT);
        } else {
            put(c->o, data, n);
            c->csize += n;
        }
        if (c->o->failed) return c->err = FULTA_ARC_ERR_IO;
    }
    if (c->opt && c->opt->progress) {
        *c->done += n;
        if (c->opt->progress(c->opt->progress_ctx, *c->done, c->total)) return c->err = FULTA_ARC_ERR_CANCELLED;
    }
    return FULTA_ARC_OK;
}

/* fulta_arc_sink adaptor so a source entry's extraction pushes straight into conv_feed. */
static fulta_arc_err_t conv_sink_write(void *ctx, const void *data, size_t n) {
    return conv_feed((conv_zip_t *)ctx, data, n);
}

fulta_arc_err_t fa_write_zip_convert(const fa_edit_item_t *items, size_t count, int level, bool force64,
                                     const fulta_arc_write_options_t *opt, const fulta_arc_write_sink_t *sink) {
    if (!sink || !sink->write || !sink->patch) return FULTA_ARC_ERR_INVALID_ARG;
    if (opt && opt->password) return FULTA_ARC_ERR_UNSUPPORTED;   /* encrypting convert: later increment */
    out_t o = { .sink = sink };
    zip_record_t *rec = calloc(count ? count : 1, sizeof *rec);
    if (!rec) return FULTA_ARC_ERR_NOMEM;
    tdefl_compressor *comp = NULL;
    if (level > 0) {
        comp = malloc(sizeof *comp);
        if (!comp) { free(rec); return FULTA_ARC_ERR_NOMEM; }
    }
    mz_uint zf = tdefl_create_comp_flags_from_zip_params(level > 0 ? level : 6, -15, MZ_DEFAULT_STRATEGY);
    fulta_arc_err_t err = FULTA_ARC_OK;

    /* progress total: the sum of the plaintext sizes we know up front */
    uint64_t total = 0, done = 0;
    for (size_t i = 0; i < count; ++i) {
        if (items[i].is_copy) {
            const fulta_arc_entry_t *e = fulta_arc_entry(items[i].src, items[i].src_index);
            if (e && !(e->flags & (FULTA_ARC_ENTRY_DIR | FULTA_ARC_ENTRY_UNKNOWN_SIZE))) total += e->size;
        } else if (items[i].reader.read && items[i].reader.size != UINT64_MAX) {
            total += items[i].reader.size;
        }
    }

    for (size_t i = 0; i < count && err == FULTA_ARC_OK; ++i) {
        const fa_edit_item_t *it = &items[i];
        zip_record_t *r = &rec[i];
        r->offset = o.at;

        bool dir;
        const char *uname;
        int64_t mtime_sec = 0;
        uint32_t ext_attr = 0;
        uint64_t known_usize = UINT64_MAX;
        if (it->is_copy) {
            const fulta_arc_entry_t *e = fulta_arc_entry(it->src, it->src_index);
            if (!e) { err = FULTA_ARC_ERR_INVALID_ARG; break; }
            dir = (e->flags & FULTA_ARC_ENTRY_DIR) != 0;
            uname = it->name ? it->name : e->name;
            mtime_sec = (e->flags & FULTA_ARC_ENTRY_HAS_MTIME) ? e->mtime / 1000000000 : 0;
            ext_attr = dir ? 0x10u : 0u;
            known_usize = dir ? 0 : ((e->flags & FULTA_ARC_ENTRY_UNKNOWN_SIZE) ? UINT64_MAX : e->size);
        } else {
            dir = name_is_dir(it->name);
            uname = it->name;
            mtime_sec = it->reader.mtime;                /* already seconds */
            ext_attr = dir ? 0x10u : 0u;
            known_usize = dir ? 0 : (it->reader.read ? it->reader.size : 0);
        }
        if (!uname) { err = FULTA_ARC_ERR_INVALID_ARG; break; }
        r->name = stored_name(uname, dir, true);
        if (!r->name) { err = FULTA_ARC_ERR_NOMEM; break; }
        r->name_len = strlen(r->name);
        r->flags = ascii(r->name) ? 0 : 0x0800;
        r->made_by_host = 0;
        r->ext_attr = ext_attr;
        r->dir = dir;
        r->mtime = mtime_sec;
        dos_time(mtime_sec, &r->time, &r->date);
        r->crc = 0; r->size = 0; r->packed = 0;
        bool store = dir || level <= 0 || known_usize == 0;
        r->method = store ? 0 : 8;
        /* Decide Zip64 before writing the header: force it when the size is unknown (we cannot rewind the
         * decision), otherwise from the known uncompressed size (deflate never grows past it by 0xFFFFFFFF). */
        r->zip64 = force64 || known_usize == UINT64_MAX || known_usize >= 0xFFFFFFFFu;

        uint64_t hdr_off = o.at;
        zip_local_header(&o, r);
        if (o.failed) { err = FULTA_ARC_ERR_IO; break; }

        if (!dir) {
            conv_zip_t c = { .o = &o, .comp = NULL, .opt = opt, .done = &done, .total = total };
            if (!store) {
                if (tdefl_init(comp, conv_put, &c, (int)zf) != TDEFL_STATUS_OKAY) { err = FULTA_ARC_ERR_CORRUPT; break; }
                c.comp = comp;
            }
            if (it->is_copy) {
                fulta_arc_sink_t s = { .ctx = &c, .write = conv_sink_write };
                err = fulta_arc_extract(it->src, it->src_index, &s);
                if (err == FULTA_ARC_OK) err = c.err;
            } else if (it->reader.read) {
                uint8_t chunk[1 << 16];
                for (;;) {
                    size_t got = 0;
                    err = it->reader.read(it->reader.ctx, chunk, sizeof chunk, &got);
                    if (err || got == 0) break;
                    err = conv_feed(&c, chunk, got);
                    if (err) break;
                }
            }
            if (err == FULTA_ARC_OK && !store) {
                if (tdefl_compress_buffer(comp, NULL, 0, TDEFL_FINISH) != TDEFL_STATUS_DONE)
                    err = c.err ? c.err : (o.failed ? FULTA_ARC_ERR_IO : FULTA_ARC_ERR_CORRUPT);
            }
            if (err == FULTA_ARC_OK) err = c.err;
            if (err) break;
            r->crc = c.crc;
            r->size = c.usize;
            r->packed = c.csize;
            /* Patch the header now that crc and the sizes are known. */
            err = patch_u32(&o, hdr_off + 14, r->crc);
            if (!err) {
                if (r->zip64) {
                    uint64_t xoff = hdr_off + 30 + r->name_len;   /* the Zip64 extra is written first */
                    err = patch_u64(&o, xoff + 4, r->size);
                    if (!err) err = patch_u64(&o, xoff + 12, r->packed);
                } else {
                    err = patch_u32(&o, hdr_off + 18, (uint32_t)r->packed);
                    if (!err) err = patch_u32(&o, hdr_off + 22, (uint32_t)r->size);
                }
            }
            if (err) break;
        }
    }
    if (err == FULTA_ARC_OK) err = write_zip_central(&o, rec, count, force64);
    for (size_t i = 0; i < count; ++i) free(rec[i].name);
    free(rec);
    free(comp);
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
    buf_t *buf;              /* when set, the compressed output is captured here (encryption) instead of emitted */
} sz_out_t;

static size_t sz_out_write(ISeqOutStreamPtr pp, const void *data, size_t size) {
    sz_out_t *p = (sz_out_t*)(void*)pp;
    if (p->buf) { b_put(p->buf, data, size); return p->buf->failed ? 0 : size; }
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

/* One entry as the 7z header needs it (name + size + crc + time); it does not need the bytes themselves, so the
 * writer and the convert route both describe their entries this way and share write_7z_finish. */
typedef struct sz_desc {
    const char *name;   /* UTF-8, not owned */
    bool dir;
    uint64_t size;      /* plaintext bytes; a stream iff size != 0 && !dir */
    uint32_t crc;       /* crc32 of the plaintext, when size != 0 && !dir */
    int64_t mtime;      /* seconds since 1970, 0 = none */
} sz_desc_t;

/* Append kFilesInfo for the `count` entries `d` to `h` (which already holds kHeader + StreamsInfo), end the header,
 * write it after the packed data at `o`, and patch the 32-byte signature header `sig`. Takes ownership of `h->p`
 * (frees it). `streams` is how many entries have a stream (size != 0 && !dir). */
static fulta_arc_err_t write_7z_tail(out_t *o, uint8_t *sig, buf_t *h, const sz_desc_t *d, size_t count,
                                     size_t streams) {
    bool *flags = (bool*)calloc(count ? count : 1, sizeof(bool));
    char **names = (char**)calloc(count ? count : 1, sizeof(char*));
    if (!flags || !names) { free(flags); free(names); free(h->p); return FULTA_ARC_ERR_NOMEM; }
    fulta_arc_err_t err = FULTA_ARC_OK;
    for (size_t i = 0; i < count; ++i) {
        names[i] = stored_name(d[i].name, d[i].dir, false);
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
        buf_t n = {0};
        b_byte(&n, 0);                                   /* not external */
        for (size_t i = 0; i < count && names[i]; ++i) b_name16(&n, names[i]);
        b_byte(h, 0x11); b_num(h, n.len); b_put(h, n.p, n.len);
        if (n.failed) h->failed = true;
        free(n.p);
    }
    size_t timed = 0;
    for (size_t i = 0; i < count; ++i) if (d[i].mtime) timed++;
    if (timed > 0) {
        buf_t t = {0};
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
        free(t.p);
    }
    b_byte(h, 0x00);                                     /* end of FilesInfo */
    b_byte(h, 0x00);                                     /* end of Header */
    if (h->failed) err = FULTA_ARC_ERR_NOMEM;

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
    for (size_t i = 0; i < count; ++i) free(names[i]);
    free(names);
    free(flags);
    free(h->p);
    return err;
}

/* Emit the 7z header for `count` entries described by `d`, whose single folder is already encoded: `packed` packed
 * bytes, `total` unpacked, the inner coder being Copy when `copy` else LZMA with `props`/`props_size`, optionally
 * wrapped in 7zAES (`enc`, `aesprops` [34 bytes], `clen` = inner length before padding). Writes the header after
 * the packed data at `o`, then patches the 32-byte signature header `sig`. `streams` is the stream count. */
static fulta_arc_err_t write_7z_finish(out_t *o, uint8_t *sig, const sz_desc_t *d, size_t count, size_t streams,
                                       uint64_t packed, const uint8_t *props, size_t props_size, bool copy,
                                       bool enc, const uint8_t *aesprops, uint64_t clen, uint64_t total) {
    buf_t h = {0};
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
        b_num(&h, enc ? 2 : 1);                          /* coders: inner (+ AES when encrypting) */
        if (copy) {
            b_byte(&h, 0x01); b_byte(&h, 0x00);          /* Copy: id 00, no properties */
        } else {
            b_byte(&h, 0x23);                            /* id size 3, has properties */
            b_byte(&h, 0x03); b_byte(&h, 0x01); b_byte(&h, 0x01);   /* LZMA (Methods.txt: 03 01 01) */
            b_num(&h, props_size);
            b_put(&h, props, props_size);
        }
        if (enc) {
            b_byte(&h, 0x24);                            /* id size 4, has properties */
            b_byte(&h, 0x06); b_byte(&h, 0xF1); b_byte(&h, 0x07); b_byte(&h, 0x01);   /* 7zAES (06 F1 07 01) */
            b_num(&h, 34);
            b_put(&h, aesprops, 34);
            b_num(&h, 0); b_num(&h, 1);                  /* bind pair: inner input (in 0) <- AES output (coder 1) */
            b_byte(&h, 0x0C); b_num(&h, total); b_num(&h, clen);   /* unpack sizes: inner out, then AES out */
        } else {
            b_byte(&h, 0x0C); b_num(&h, total);          /* kCodersUnPackSize */
        }
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

static fulta_arc_err_t write_7z(const fulta_arc_write_entry_t *entries, size_t count, int level,
                                   const fulta_arc_write_options_t *opt, out_t *o) {
    const char *pw = opt ? opt->password : NULL;
    if (pw && opt->encryption != FULTA_ARC_ENCRYPT_DEFAULT && opt->encryption != FULTA_ARC_ENCRYPT_7Z_AES256)
        return FULTA_ARC_ERR_INVALID_ARG;                /* a ZIP scheme for a 7z */
    bool enc = pw != NULL;

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
    uint64_t clen = 0;                                   /* AES: inner (compressed/stored) length before padding */
    uint8_t aesprops[34];                                /* 7zAES coder properties (power+flags, salt16, iv16) */
    if (streams > 0) {
        uint64_t start = o->at;
        buf_t comp = {0};                                /* AES: the inner coder's output, captured for encrypting */
        if (level == 0) {                                /* Copy: the bytes as they are */
            uint64_t done = 0;
            for (size_t i = 0; i < count && !o->failed && !comp.failed; ++i) {
                if (is_dir(&entries[i])) continue;
                if (enc) b_put(&comp, entries[i].data, (size_t)entries[i].size);
                else for (uint64_t at = 0; at < entries[i].size && !o->failed;) {
                    size_t n = entries[i].size - at > (1u << 30) ? (1u << 30) : (size_t)(entries[i].size - at);
                    put(o, (const uint8_t *)entries[i].data + at, n);
                    at += n;
                }
                done += entries[i].size;
                if (opt && opt->progress && opt->progress(opt->progress_ctx, done, total)) { free(comp.p); return FULTA_ARC_ERR_CANCELLED; }
            }
            if (o->failed || comp.failed) { free(comp.p); return o->failed ? FULTA_ARC_ERR_IO : FULTA_ARC_ERR_NOMEM; }
        } else {
            CLzmaEncHandle ench = LzmaEnc_Create(&SZ_ALLOC);
            if (!ench) return FULTA_ARC_ERR_NOMEM;
            CLzmaEncProps ep;
            LzmaEncProps_Init(&ep);
            ep.level = level;
            ep.reduceSize = total;                       /* no larger a dictionary than the data needs */
            SRes res = LzmaEnc_SetProps(ench, &ep);
            if (res == SZ_OK) res = LzmaEnc_WriteProperties(ench, props, &props_size);
            sz_in_t in = { .vt = { sz_in_read }, .entries = entries, .count = count };
            sz_out_t outs = { .vt = { sz_out_write }, .o = o, .buf = enc ? &comp : NULL };
            sz_progress_t prog = { .vt = { sz_progress }, .opt = opt, .total = total };
            if (res == SZ_OK) res = LzmaEnc_Encode(ench, &outs.vt, &in.vt, &prog.vt, &SZ_ALLOC, &SZ_ALLOC);
            LzmaEnc_Destroy(ench, &SZ_ALLOC, &SZ_ALLOC);
            if (o->failed || comp.failed) { free(comp.p); return o->failed ? FULTA_ARC_ERR_IO : FULTA_ARC_ERR_NOMEM; }
            if (res == SZ_ERROR_PROGRESS) { free(comp.p); return FULTA_ARC_ERR_CANCELLED; }
            if (res == SZ_ERROR_MEM) { free(comp.p); return FULTA_ARC_ERR_NOMEM; }
            if (res != SZ_OK) { free(comp.p); return FULTA_ARC_ERR_CORRUPT; }
        }
        if (enc) {                                       /* 7zAES: AES-256-CBC over the inner output, random salt+iv */
            clen = comp.len;
            uint8_t salt[16], iv[16], key[32];
            if (!fa_random_bytes(salt, 16) || !fa_random_bytes(iv, 16)) { free(comp.p); return FULTA_ARC_ERR_IO; }
            fulta_arc_err_t ke = fa_7z_derive_key(pw, salt, 16, 19, key);
            if (ke) { free(comp.p); return ke; }
            size_t ct = (size_t)((clen + 15) & ~(uint64_t)15);
            uint8_t *cbuf = calloc(ct ? ct : 1, 1);
            if (!cbuf) { free(comp.p); memset(key, 0, 32); return FULTA_ARC_ERR_NOMEM; }
            memcpy(cbuf, comp.p, (size_t)clen);
            uint8_t ivc[16]; memcpy(ivc, iv, 16);
            fa_aes_cbc_encrypt(key, 32, ivc, cbuf, ct);
            memset(key, 0, 32);
            put(o, cbuf, ct);
            free(cbuf);
            free(comp.p);
            packed = ct;
            aesprops[0] = (uint8_t)(0xC0 | 19);          /* salt & iv present, NumCyclesPower = 19 */
            aesprops[1] = 0xFF;                          /* salt_len = iv_len = 16 (high bits in [0]) */
            memcpy(aesprops + 2, salt, 16);
            memcpy(aesprops + 18, iv, 16);
            if (o->failed) return FULTA_ARC_ERR_IO;
        } else {
            packed = o->at - start;
        }
    }

    /* describe each entry for the header (its bytes are already written/encoded) */
    sz_desc_t *desc = (sz_desc_t*)calloc(count ? count : 1, sizeof *desc);
    if (!desc) return FULTA_ARC_ERR_NOMEM;
    for (size_t i = 0; i < count; ++i) {
        bool dir = is_dir(&entries[i]);
        desc[i] = (sz_desc_t){ .name = entries[i].name, .dir = dir, .size = entries[i].size,
                               .crc = (entries[i].size && !dir) ? crc32_of(entries[i].data, entries[i].size) : 0,
                               .mtime = entries[i].mtime };
    }
    fulta_arc_err_t err = write_7z_finish(o, sig, desc, count, streams, packed, props, props_size,
                                          level == 0, enc, aesprops, clen, total);
    free(desc);
    return err;
}

/* ---- 7z convert route (any open archive -> a fresh 7z) ----
 *
 * The LZMA encoder PULLS its input (ISeqInStream), but extraction PUSHES into a sink, and an entry may be multi-GiB,
 * so we cannot hold it all in memory. Bridge the two with a disk spill: pass 1 extracts/pulls every entry's
 * plaintext into one spill file (recording each entry's size and crc as it goes, bounded memory); pass 2 LZMA-
 * encodes by reading that spill sequentially into the encoder; pass 3 emits the 7z header from the recorded sizes/
 * crcs. Memory stays bounded; disk does not (the spill holds the whole plaintext). Encrypting the output is refused
 * here, as in the ZIP convert route. */

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
        w->crc = fa_crc32(w->crc, data, n);
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

/* Pass 2: the spill file as the SDK's input stream, read sequentially. */
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

/* Build the spill path: beside the output (`<hint>.d`) when a file path is known, else a random temp name. */
static fulta_arc_err_t spill_path(const char *hint, char **out) {
    *out = NULL;
    if (hint) {
        size_t n = strlen(hint);
        char *s = malloc(n + 3);
        if (!s) return FULTA_ARC_ERR_NOMEM;
        memcpy(s, hint, n);
        memcpy(s + n, ".d", 3);
        *out = s;
        return FULTA_ARC_OK;
    }
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = getenv("TMP");
    if (!dir || !*dir) dir = getenv("TEMP");
    if (!dir || !*dir) dir = "/tmp";
    uint8_t rnd[12];
    if (!fa_random_bytes(rnd, sizeof rnd)) return FULTA_ARC_ERR_IO;
    size_t dn = strlen(dir);
    char *s = malloc(dn + 1 + 5 + 24 + 1);               /* dir '/' "fa7z-" 24hex NUL */
    if (!s) return FULTA_ARC_ERR_NOMEM;
    static const char hexd[] = "0123456789abcdef";
    char *q = s;
    memcpy(q, dir, dn); q += dn;
    *q++ = '/';
    memcpy(q, "fa7z-", 5); q += 5;
    for (int i = 0; i < 12; ++i) { *q++ = hexd[rnd[i] >> 4]; *q++ = hexd[rnd[i] & 15]; }
    *q = 0;
    *out = s;
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_write_7z_convert(const fa_edit_item_t *items, size_t count, int level,
                                    const fulta_arc_write_options_t *opt, const fulta_arc_write_sink_t *sink,
                                    const char *spill_hint) {
    if (!sink || !sink->write || !sink->patch) return FULTA_ARC_ERR_INVALID_ARG;
    if (opt && opt->password) return FULTA_ARC_ERR_UNSUPPORTED;   /* encrypting convert: later increment */

    char *spill = NULL;
    fulta_arc_err_t err = spill_path(spill_hint, &spill);
    if (err) return err;

    proven_allocator_t heap = proven_heap_allocator();
    proven_fs_mode_t wmode = spill_hint ? (PROVEN_FS_WRITE | PROVEN_FS_CREATE | PROVEN_FS_TRUNC)
                                        : (PROVEN_FS_WRITE | PROVEN_FS_CREATE_NEW);
    proven_result_file_t rf = proven_fs_open(heap, proven_u8str_view_from_cstr(spill), wmode);
    if (rf.err) { free(spill); return FULTA_ARC_ERR_IO; }
    proven_file_t sf = rf.value;

    sz_desc_t *desc = (sz_desc_t *)calloc(count ? count : 1, sizeof *desc);
    if (!desc) {
        proven_err_t igc = proven_fs_close(sf); (void)igc;
        proven_err_t ig = proven_fs_remove(heap, proven_u8str_view_from_cstr(spill)); (void)ig;
        free(spill);
        return FULTA_ARC_ERR_NOMEM;
    }

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
    sz_spill_w_t w = { .f = sf, .opt = opt, .done = &done, .total = total };
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
    if (proven_fs_close(sf) && err == FULTA_ARC_OK) err = FULTA_ARC_ERR_IO;

    /* streams and the exact unpacked total, from pass 1 */
    size_t streams = 0;
    uint64_t total_un = 0;
    for (size_t i = 0; i < count; ++i)
        if (desc[i].size && !desc[i].dir) { streams++; total_un += desc[i].size; }

    out_t o = { .sink = sink };
    uint8_t props[LZMA_PROPS_SIZE];
    SizeT props_size = sizeof props;
    uint64_t packed = 0;
    if (err == FULTA_ARC_OK) {
        uint8_t sig[32] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C, 0, 4 };
        put(&o, sig, sizeof sig);
        if (o.failed) err = FULTA_ARC_ERR_IO;

        if (err == FULTA_ARC_OK && streams > 0) {        /* pass 2: encode the spill into the folder */
            proven_result_file_t rr = proven_fs_open(heap, proven_u8str_view_from_cstr(spill), PROVEN_FS_READ);
            if (rr.err) err = FULTA_ARC_ERR_IO;
            else {
                proven_file_t rf2 = rr.value;
                uint64_t start = o.at;
                if (level == 0) {                        /* Copy: the spill bytes verbatim */
                    uint8_t buf[1 << 16];
                    for (;;) {
                        proven_result_size_t r = proven_fs_read(rf2, (proven_mem_mut_t){.ptr = buf, .size = sizeof buf});
                        if (r.err == PROVEN_ERR_EOF) { if (r.value) put(&o, buf, r.value); break; }
                        if (r.err) { err = FULTA_ARC_ERR_IO; break; }
                        put(&o, buf, r.value);
                        if (o.failed) { err = FULTA_ARC_ERR_IO; break; }
                    }
                    if (err == FULTA_ARC_OK) packed = o.at - start;
                } else {
                    CLzmaEncHandle ench = LzmaEnc_Create(&SZ_ALLOC);
                    if (!ench) err = FULTA_ARC_ERR_NOMEM;
                    else {
                        CLzmaEncProps ep;
                        LzmaEncProps_Init(&ep);
                        ep.level = level;
                        ep.reduceSize = total_un;
                        SRes res = LzmaEnc_SetProps(ench, &ep);
                        if (res == SZ_OK) res = LzmaEnc_WriteProperties(ench, props, &props_size);
                        sz_spill_r_t in = { .vt = { sz_spill_read }, .f = rf2 };
                        sz_out_t outs = { .vt = { sz_out_write }, .o = &o, .buf = NULL };
                        sz_progress_t prog = { .vt = { sz_progress }, .opt = opt, .total = total_un };
                        if (res == SZ_OK) res = LzmaEnc_Encode(ench, &outs.vt, &in.vt, &prog.vt, &SZ_ALLOC, &SZ_ALLOC);
                        LzmaEnc_Destroy(ench, &SZ_ALLOC, &SZ_ALLOC);
                        if (o.failed) err = FULTA_ARC_ERR_IO;
                        else if (res == SZ_ERROR_PROGRESS) err = FULTA_ARC_ERR_CANCELLED;
                        else if (res == SZ_ERROR_MEM) err = FULTA_ARC_ERR_NOMEM;
                        else if (res == SZ_ERROR_READ || in.io_err) err = FULTA_ARC_ERR_IO;
                        else if (res != SZ_OK) err = FULTA_ARC_ERR_CORRUPT;
                        else packed = o.at - start;
                    }
                }
                if (proven_fs_close(rf2) && err == FULTA_ARC_OK) err = FULTA_ARC_ERR_IO;
            }
        }

        if (err == FULTA_ARC_OK)                         /* pass 3: the header */
            err = write_7z_finish(&o, sig, desc, count, streams, packed, props, props_size,
                                  level == 0, false, NULL, 0, total_un);
    }

    free(desc);
    proven_err_t ig = proven_fs_remove(heap, proven_u8str_view_from_cstr(spill)); (void)ig;
    free(spill);
    return err;
}

/* ---- 7z copy route (edit a non-solid 7z into a 7z) ----
 *
 * Reached only when fulta_arc_editable() == OK, i.e. every source folder holds exactly one file. Each kept entry's
 * folder is re-emitted byte-for-byte and its packed bytes copied without re-encoding; each added/replaced entry
 * becomes a fresh one-coder folder, encoded straight from its (pull) reader — no spill is needed, unlike the
 * convert route, because the new data is already a pull source. The output is a sequence of folders, one per
 * non-empty entry, in entry order; empty files and directories carry no folder. */

/* A reader as the SDK's input stream, counting size and crc as the encoder pulls. */
typedef struct sz_rd_in {
    ISeqInStream vt;
    const fulta_arc_reader_t *rd;
    uint32_t crc;
    uint64_t size;
    fulta_arc_err_t err;
} sz_rd_in_t;

static SRes sz_rd_in_read(ISeqInStreamPtr pp, void *buf, size_t *size) {
    sz_rd_in_t *p = (sz_rd_in_t *)(void *)pp;
    size_t want = *size, got = 0;
    *size = 0;
    if (want == 0) return SZ_OK;
    fulta_arc_err_t e = p->rd->read(p->rd->ctx, buf, want, &got);
    if (e) { p->err = e; return SZ_ERROR_READ; }
    if (got) { p->crc = fa_crc32(p->crc, buf, got); p->size += got; }
    *size = got;                                         /* got == 0 => EOF to the encoder */
    return SZ_OK;
}

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

/* Encode one entry (its reader) as a fresh single-coder folder, writing the packed bytes to `o`. Fills *F's coder,
 * sizes, crc; `pbuf` is stable storage for the LZMA properties (when level > 0). */
static fulta_arc_err_t encode_one_folder(const fulta_arc_reader_t *rd, int level,
                                         const fulta_arc_write_options_t *opt, out_t *o,
                                         uint8_t pbuf[LZMA_PROPS_SIZE], fa_7z_copy_t *F) {
    memset(F, 0, sizeof *F);
    F->has_folder = true;
    F->ncoders = 1;
    F->nbinds = 0;
    F->npacked = 1;
    F->packed[0] = 0;
    F->coders[0].nin = 1;
    uint64_t start = o->at;
    uint32_t crc = 0;
    uint64_t usize = 0;
    if (level <= 0) {                                    /* Copy: the bytes as they are */
        F->coders[0].id[0] = 0x00; F->coders[0].idlen = 1; F->coders[0].nprops = 0;
        uint8_t chunk[1 << 16];
        for (;;) {
            size_t got = 0;
            fulta_arc_err_t e = rd->read ? rd->read(rd->ctx, chunk, sizeof chunk, &got) : FULTA_ARC_OK;
            if (e) return e;
            if (!got) break;
            crc = fa_crc32(crc, chunk, got);
            usize += got;
            put(o, chunk, got);
            if (o->failed) return FULTA_ARC_ERR_IO;
            if (opt && opt->progress && opt->progress(opt->progress_ctx, usize, rd->size != UINT64_MAX ? rd->size : usize))
                return FULTA_ARC_ERR_CANCELLED;
        }
    } else {                                             /* LZMA */
        F->coders[0].id[0] = 0x03; F->coders[0].id[1] = 0x01; F->coders[0].id[2] = 0x01; F->coders[0].idlen = 3;
        CLzmaEncHandle ench = LzmaEnc_Create(&SZ_ALLOC);
        if (!ench) return FULTA_ARC_ERR_NOMEM;
        CLzmaEncProps ep;
        LzmaEncProps_Init(&ep);
        ep.level = level;
        if (rd->size != UINT64_MAX) ep.reduceSize = rd->size;
        SizeT ps = LZMA_PROPS_SIZE;
        SRes res = LzmaEnc_SetProps(ench, &ep);
        if (res == SZ_OK) res = LzmaEnc_WriteProperties(ench, pbuf, &ps);
        sz_rd_in_t in = { .vt = { sz_rd_in_read }, .rd = rd };
        sz_out_t outs = { .vt = { sz_out_write }, .o = o, .buf = NULL };
        sz_progress_t prog = { .vt = { sz_progress }, .opt = opt, .total = rd->size != UINT64_MAX ? rd->size : 0 };
        if (res == SZ_OK) res = LzmaEnc_Encode(ench, &outs.vt, &in.vt, &prog.vt, &SZ_ALLOC, &SZ_ALLOC);
        LzmaEnc_Destroy(ench, &SZ_ALLOC, &SZ_ALLOC);
        if (o->failed) return FULTA_ARC_ERR_IO;
        if (in.err) return in.err;
        if (res == SZ_ERROR_PROGRESS) return FULTA_ARC_ERR_CANCELLED;
        if (res == SZ_ERROR_MEM) return FULTA_ARC_ERR_NOMEM;
        if (res == SZ_ERROR_READ) return FULTA_ARC_ERR_IO;
        if (res != SZ_OK) return FULTA_ARC_ERR_CORRUPT;
        F->coders[0].props = pbuf;
        F->coders[0].nprops = (uint32_t)ps;
        crc = in.crc;
        usize = in.size;
    }
    F->unpack_sizes[0] = usize;
    F->pack_sizes[0] = o->at - start;
    F->size = usize;
    F->has_crc = true;
    F->crc = crc;
    return FULTA_ARC_OK;
}

/* Emit the multi-folder 7z header for an edited archive. `of[0..nf)` are the output folders (already written to `o`
 * in order); folder i is the i-th entry of `d` that has a stream. CRCs go in kSubStreamsInfo (no folder-level CRC),
 * one substream per folder. */
static fulta_arc_err_t write_7z_header_multi(out_t *o, uint8_t *sig, const sz_desc_t *d, size_t count,
                                             const fa_7z_copy_t *of, size_t nf) {
    buf_t h = {0};
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
        for (size_t i = 0; i < nf; ++i) {
            const fa_7z_copy_t *F = &of[i];
            b_num(&h, F->ncoders);
            for (uint32_t c = 0; c < F->ncoders; ++c) {
                const fa_7z_coder_t *cd = &F->coders[c];
                uint8_t fl = (uint8_t)(cd->idlen & 0x0F);
                if (cd->nin != 1) fl |= 0x10;            /* complex: NumInStreams/NumOutStreams follow */
                if (cd->nprops) fl |= 0x20;              /* has properties */
                b_byte(&h, fl);
                b_put(&h, cd->id, cd->idlen);
                if (cd->nin != 1) { b_num(&h, cd->nin); b_num(&h, 1); }   /* NumOutStreams is always 1 */
                if (cd->nprops) { b_num(&h, cd->nprops); b_put(&h, cd->props, cd->nprops); }
            }
            for (uint32_t b = 0; b < F->nbinds; ++b) { b_num(&h, F->binds[b][0]); b_num(&h, F->binds[b][1]); }
            if (F->npacked > 1)                          /* the single-pack index is implied */
                for (uint32_t k = 0; k < F->npacked; ++k) b_num(&h, F->packed[k]);
        }
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
            bool *dv = (bool*)calloc(nf, sizeof(bool));
            if (!dv) { free(h.p); return FULTA_ARC_ERR_NOMEM; }
            for (size_t i = 0; i < nf; ++i) dv[i] = of[i].has_crc;
            b_bits(&h, dv, nf);
            free(dv);
            for (size_t i = 0; i < nf; ++i) if (of[i].has_crc) b_u32(&h, of[i].crc);
        }
        b_byte(&h, 0x00);                                /* end kSubStreamsInfo */
        b_byte(&h, 0x00);                                /* end StreamsInfo */
    }
    return write_7z_tail(o, sig, &h, d, count, nf);
}

fulta_arc_err_t fa_write_7z_edited(const fa_edit_item_t *items, size_t count, int level,
                                   const fulta_arc_write_options_t *opt, const fulta_arc_write_sink_t *sink) {
    if (!sink || !sink->write || !sink->patch) return FULTA_ARC_ERR_INVALID_ARG;
    if (opt && opt->password) return FULTA_ARC_ERR_UNSUPPORTED;   /* encrypting the output: later increment */

    sz_desc_t *fd = (sz_desc_t *)calloc(count ? count : 1, sizeof *fd);
    fa_7z_copy_t *fol = (fa_7z_copy_t *)calloc(count ? count : 1, sizeof *fol);
    uint8_t (*pbuf)[LZMA_PROPS_SIZE] = (uint8_t(*)[LZMA_PROPS_SIZE])calloc(count ? count : 1, LZMA_PROPS_SIZE);
    if (!fd || !fol || !pbuf) { free(fd); free(fol); free(pbuf); return FULTA_ARC_ERR_NOMEM; }

    out_t o = { .sink = sink };
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
            err = encode_one_folder(&it->reader, level, opt, &o, pbuf[nf], &fol[nf]);
            if (err) break;
            fd[i].size = fol[nf].size;
            nf++;
        }
    }

    if (err == FULTA_ARC_OK) err = write_7z_header_multi(&o, sig, fd, count, fol, nf);
    free(fd);
    free(fol);
    free(pbuf);
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
