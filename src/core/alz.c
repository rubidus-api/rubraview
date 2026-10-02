/*
 * ALZ archive reading. Written from docs/specs/alz-format.md, with RFC 1951
 * (deflate, through vendor/miniz), bzip2's own documentation and source
 * (vendor/bzip2, its stream framing changed as the spec's section 3.2 says)
 * and PKWARE's APPNOTE, "Traditional PKWARE Encryption".
 * Licence: MIT (the project's, see LICENSE).
 */
#include "rubraview/alz.h"
#include <stdlib.h>
#include <string.h>

/* miniz for raw deflate and CRC-32, the cut-down bzip2 for method 1. Only
   this module includes the latter (RFC-0001 §8.1's one-module rule). */
#include "miniz.h"
#define BZ_NO_STDIO
#define BZ_EXPORT          /* plain functions, not a DLL's pointers, on Windows too */
#include "bzlib.h"

/* libbzip2 built without stdio asks its user for this: an internal
   consistency check failed, which input alone must never cause. */
void bz_internal_error(int errcode);
void bz_internal_error(int errcode) {
    (void)errcode;
    abort();
}

#define ALZ_NAME_MAX 255                 /* spec 1.2: longer names are refused */
#define ALZ_ENC_HEADER 12                /* spec 4.1 */
#define ALZ_CHUNK (64u * 1024u)
#define ALZ_CHECK_DECODE_MAX (64u << 20) /* set_password decodes one file up to this size */

/* ---- the joined stream (spec 2) ---------------------------------------- */

/* Volume `i`'s part of the stream: all of it but the 8-byte header of every
   volume after the first and the 16-byte tail of every volume before the last. */
static void segment(const rubraview_alz_archive_t *a, size_t i, const uint8_t **out_p, uint64_t *out_len) {
    uint64_t head = i > 0 ? 8 : 0;
    uint64_t tail = i + 1 < a->volume_count ? 16 : 0;
    uint64_t size = a->volumes[i].size;
    *out_p = a->volumes[i].data ? a->volumes[i].data + head : NULL;
    *out_len = size > head + tail && a->volumes[i].data ? size - head - tail : 0;
}

/* The bytes at `off` that lie in one piece: a pointer and how many. */
static const uint8_t *view(const rubraview_alz_archive_t *a, uint64_t off, uint64_t *out_len) {
    for (size_t i = 0; i < a->volume_count; ++i) {
        const uint8_t *p;
        uint64_t len;
        segment(a, i, &p, &len);
        if (off < len) {
            *out_len = len - off;
            return p + off;
        }
        off -= len;
    }
    *out_len = 0;
    return NULL;
}

/* `n` bytes from `off`, across volumes. False when they are not all there. */
static bool read_at(const rubraview_alz_archive_t *a, uint64_t off, void *dst, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    while (n > 0) {
        uint64_t len;
        const uint8_t *p = view(a, off, &len);
        if (!p) return false;
        size_t take = len < n ? (size_t)len : n;
        memcpy(d, p, take);
        d += take;
        off += take;
        n -= take;
    }
    return true;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le_n(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = n; i-- > 0;) v = (v << 8) | p[i];
    return v;
}

/* ---- traditional PKWARE encryption (spec 4) ----------------------------- */

typedef struct zip_keys {
    uint32_t k0, k1, k2;
    uint32_t table[256];
} zip_keys_t;

static uint32_t crc_step(const zip_keys_t *z, uint32_t k, uint8_t b) {
    return z->table[(k ^ b) & 0xFF] ^ (k >> 8);
}

static void keys_update(zip_keys_t *z, uint8_t b) {
    z->k0 = crc_step(z, z->k0, b);
    z->k1 = (z->k1 + (z->k0 & 0xFF)) * 134775813u + 1u;
    z->k2 = crc_step(z, z->k2, (uint8_t)(z->k1 >> 24));
}

static void keys_init(zip_keys_t *z, const uint8_t *password, size_t len) {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        z->table[n] = c;
    }
    z->k0 = 0x12345678u;
    z->k1 = 0x23456789u;
    z->k2 = 0x34567890u;
    for (size_t i = 0; i < len; ++i) keys_update(z, password[i]);
}

static void keys_decrypt(zip_keys_t *z, uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        uint32_t t = (z->k2 | 2u) & 0xFFFFu;
        uint8_t c = (uint8_t)(p[i] ^ (((t * (t ^ 1u)) >> 8) & 0xFFu));
        keys_update(z, c);
        p[i] = c;
    }
}

/* Wipe in a way the compiler may not drop. */
static void wipe(void *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t*)p;
    while (n--) *v++ = 0;
}

typedef struct alz_state {
    uint8_t *password;
    size_t password_len;
    bool password_ok;
} alz_state_t;

static void state_forget(alz_state_t *s) {
    if (!s) return;
    if (s->password) {
        wipe(s->password, s->password_len);
        free(s->password);
    }
    s->password = NULL;
    s->password_len = 0;
    s->password_ok = false;
}

/* Keys ready for the entry's data: the 12-byte header decrypted and its
   last byte checked (spec 4.2). */
static rubraview_alz_err_t keys_for(const rubraview_alz_archive_t *a, const rubraview_alz_entry_t *e,
                                    const uint8_t *password, size_t password_len, zip_keys_t *z) {
    uint8_t header[ALZ_ENC_HEADER];
    if (e->data_offset < ALZ_ENC_HEADER || !read_at(a, e->data_offset - ALZ_ENC_HEADER, header, sizeof(header)))
        return RUBRAVIEW_ALZ_ERR_TRUNCATED;
    keys_init(z, password, password_len);
    keys_decrypt(z, header, sizeof(header));
    uint8_t want = (e->descriptor & 0x08) ? (uint8_t)(e->dos_time >> 8) : (uint8_t)(e->crc32 >> 24);
    return header[ALZ_ENC_HEADER - 1] == want ? RUBRAVIEW_ALZ_OK : RUBRAVIEW_ALZ_ERR_BAD_PASSWORD;
}

/* ---- an entry's packed bytes, a piece at a time ------------------------- */

typedef struct packed_reader {
    const rubraview_alz_archive_t *a;
    uint64_t off, left;
    zip_keys_t *keys;            /* NULL when not encrypted */
    uint8_t *buf;                /* ALZ_CHUNK bytes, for decrypting into */
} packed_reader_t;

/* The next piece: a view straight into a volume, or decrypted into `buf`.
   0 when nothing is left, or the rest is not there. */
static size_t packed_next(packed_reader_t *r, const uint8_t **out) {
    if (r->left == 0) return 0;
    uint64_t len;
    const uint8_t *p = view(r->a, r->off, &len);
    if (!p) return 0;
    if (len > r->left) len = r->left;
    if (len > ALZ_CHUNK) len = ALZ_CHUNK;
    size_t n = (size_t)len;
    if (r->keys) {
        memcpy(r->buf, p, n);
        keys_decrypt(r->keys, r->buf, n);
        p = r->buf;
    }
    r->off += n;
    r->left -= n;
    *out = p;
    return n;
}

static rubraview_alz_err_t copy_stored(packed_reader_t *r, uint8_t *out, uint64_t size) {
    uint64_t done = 0;
    const uint8_t *p;
    size_t n;
    while ((n = packed_next(r, &p)) > 0) {
        memcpy(out + done, p, n);
        done += n;
    }
    return done == size ? RUBRAVIEW_ALZ_OK : RUBRAVIEW_ALZ_ERR_TRUNCATED;
}

/* Spec 3.1: raw deflate, until the size is reached or the stream ends. */
static rubraview_alz_err_t inflate_raw(packed_reader_t *r, uint8_t *out, uint64_t size) {
    tinfl_decompressor *d = (tinfl_decompressor*)malloc(sizeof(*d));
    if (!d) return RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY;
    tinfl_init(d);
    rubraview_alz_err_t err = RUBRAVIEW_ALZ_ERR_CORRUPT_STREAM;
    uint64_t produced = 0;
    const uint8_t *in = NULL;
    size_t in_len = 0;
    for (;;) {
        if (in_len == 0) in_len = packed_next(r, &in);
        uint64_t room = size - produced;
        size_t in_bytes = in_len;
        size_t out_bytes = room > SIZE_MAX ? SIZE_MAX : (size_t)room;
        mz_uint32 flags = TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | (r->left > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0);
        tinfl_status st = tinfl_decompress(d, in_len ? in : (const mz_uint8*)"", &in_bytes, out, out + produced, &out_bytes, flags);
        in += in_bytes;
        in_len -= in_bytes;
        produced += out_bytes;
        if (st == TINFL_STATUS_DONE || produced == size) {
            err = produced == size ? RUBRAVIEW_ALZ_OK : RUBRAVIEW_ALZ_ERR_CORRUPT_STREAM;
            break;
        }
        if (st < 0) break;
        if (st == TINFL_STATUS_NEEDS_MORE_INPUT && in_len == 0 && r->left == 0) break;   /* cut short */
        if (in_bytes == 0 && out_bytes == 0 && in_len > 0) break;                         /* stuck */
    }
    free(d);
    return err;
}

/* Spec 3.2: the cut-down bzip2, through vendor/bzip2. */
static rubraview_alz_err_t bunzip_alz(packed_reader_t *r, uint8_t *out, uint64_t size) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    int init = BZ2_bzDecompressInit(&s, 0, 0);
    if (init == BZ_MEM_ERROR) return RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY;
    if (init != BZ_OK) return RUBRAVIEW_ALZ_ERR_CORRUPT_STREAM;
    rubraview_alz_err_t err = RUBRAVIEW_ALZ_ERR_CORRUPT_STREAM;
    uint64_t produced = 0;
    for (;;) {
        if (s.avail_in == 0) {
            const uint8_t *in = NULL;
            size_t n = packed_next(r, &in);
            /* The decoder only reads through next_in; its type is libbzip2's. */
            s.next_in = n ? (char*)(uintptr_t)in : NULL;
            s.avail_in = (unsigned)n;
        }
        uint64_t room = size - produced;
        if (room > (1u << 30)) room = 1u << 30;
        s.next_out = (char*)(out + produced);
        s.avail_out = (unsigned)room;
        unsigned had_in = s.avail_in;
        int ret = BZ2_bzDecompress(&s);
        uint64_t made = room - s.avail_out;
        produced += made;
        if (ret == BZ_STREAM_END || (ret == BZ_OK && produced == size)) {
            err = produced == size ? RUBRAVIEW_ALZ_OK : RUBRAVIEW_ALZ_ERR_CORRUPT_STREAM;
            break;
        }
        if (ret == BZ_MEM_ERROR) { err = RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY; break; }
        if (ret != BZ_OK) break;
        if (s.avail_in == 0 && r->left == 0 && made == 0 && had_in == 0) break;   /* cut short */
    }
    BZ2_bzDecompressEnd(&s);
    return err;
}

/* Decode entry `e` into `out` (its size, plus one byte for a NUL). */
static rubraview_alz_err_t decode(const rubraview_alz_archive_t *a, const rubraview_alz_entry_t *e,
                                  const uint8_t *password, size_t password_len, uint8_t *out) {
    if (e->truncated) return RUBRAVIEW_ALZ_ERR_TRUNCATED;
    if (e->method > RUBRAVIEW_ALZ_METHOD_DEFLATE) return RUBRAVIEW_ALZ_ERR_UNSUPPORTED;
    if (e->method == RUBRAVIEW_ALZ_METHOD_STORED && e->packed_size != e->size) return RUBRAVIEW_ALZ_ERR_CORRUPT_STREAM;

    zip_keys_t *keys = NULL;
    uint8_t *buf = NULL;
    rubraview_alz_err_t err = RUBRAVIEW_ALZ_OK;
    if (e->encrypted) {
        if (!password) return RUBRAVIEW_ALZ_ERR_ENCRYPTED;
        keys = (zip_keys_t*)malloc(sizeof(*keys));
        buf = (uint8_t*)malloc(ALZ_CHUNK);
        if (!keys || !buf) { err = RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY; goto done; }
        err = keys_for(a, e, password, password_len, keys);
        if (err != RUBRAVIEW_ALZ_OK) goto done;
    }

    packed_reader_t r = { .a = a, .off = e->data_offset, .left = e->packed_size, .keys = keys, .buf = buf };
    if (e->size > 0) {
        if (e->method == RUBRAVIEW_ALZ_METHOD_STORED) err = copy_stored(&r, out, e->size);
        else if (e->method == RUBRAVIEW_ALZ_METHOD_DEFLATE) err = inflate_raw(&r, out, e->size);
        else err = bunzip_alz(&r, out, e->size);
    }
    if (err == RUBRAVIEW_ALZ_OK) {
        /* The one check the format keeps (spec 3): CRC-32 of the result. */
        uint32_t crc = (uint32_t)MZ_CRC32_INIT;
        uint64_t done = 0;
        while (done < e->size) {
            size_t step = e->size - done > (1u << 30) ? (1u << 30) : (size_t)(e->size - done);
            crc = (uint32_t)mz_crc32(crc, out + done, step);
            done += step;
        }
        if (crc != e->crc32) err = RUBRAVIEW_ALZ_ERR_CORRUPT_STREAM;
    }
done:
    if (keys) { wipe(keys, sizeof(*keys)); free(keys); }
    if (buf) { wipe(buf, ALZ_CHUNK); free(buf); }
    return err;
}

/* ---- the listing (spec 1) ---------------------------------------------- */

bool rubraview_alz_is_alz(const uint8_t *data, size_t size) {
    return data && size >= 4 && data[0] == 'A' && data[1] == 'L' && data[2] == 'Z' && data[3] == 0x01;
}

/* Spec 5: no `..` component, no absolute name. On the raw bytes: `.`, `/`,
   `\` and `:` are never the trail byte of a CP949 character. */
static bool name_is_safe(const uint8_t *p, size_t n) {
    if (n == 0 || p[0] == '\\' || p[0] == '/') return false;
    if (n >= 2 && p[1] == ':') return false;
    size_t start = 0;
    for (size_t i = 0; i <= n; ++i) {
        if (i < n && p[i] == 0) return false;
        if (i == n || p[i] == '\\' || p[i] == '/') {
            if (i - start == 2 && p[start] == '.' && p[start + 1] == '.') return false;
            start = i + 1;
        }
    }
    return true;
}

typedef struct entry_buf {
    rubraview_alz_entry_t *data;
    size_t count, cap;
} entry_buf_t;

static bool entry_push(entry_buf_t *b, const rubraview_alz_entry_t *e) {
    if (b->count == b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 32;
        rubraview_alz_entry_t *grown = (rubraview_alz_entry_t*)realloc(b->data, cap * sizeof(*grown));
        if (!grown) return false;
        b->data = grown;
        b->cap = cap;
    }
    b->data[b->count++] = *e;
    return true;
}

rubraview_alz_result_t rubraview_alz_open(proven_arena_t *arena, const uint8_t *data, size_t size) {
    rubraview_alz_volume_t one = { .data = data, .size = size };
    return rubraview_alz_open_volumes(arena, &one, 1);
}

rubraview_alz_result_t rubraview_alz_open_volumes(proven_arena_t *arena, const rubraview_alz_volume_t *volumes,
                                                  size_t volume_count) {
    rubraview_alz_result_t result = { .err = RUBRAVIEW_ALZ_ERR_NOT_AN_ALZ };
    if (!arena || !volumes || volume_count == 0 || !rubraview_alz_is_alz(volumes[0].data, volumes[0].size)) return result;

    proven_result_mem_mut_t vres = rubraview_arena_alloc_array(arena, volume_count, sizeof(rubraview_alz_volume_t));
    if (!proven_is_ok(vres.err)) { result.err = RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY; return result; }
    rubraview_alz_volume_t *vols = (rubraview_alz_volume_t*)(void*)vres.value.ptr;
    memcpy(vols, volumes, volume_count * sizeof(*vols));

    rubraview_alz_archive_t a = { .volumes = vols, .volume_count = volume_count };
    for (size_t i = 0; i < volume_count; ++i) {
        const uint8_t *p;
        uint64_t len;
        segment(&a, i, &p, &len);
        a.size += len;
    }

    entry_buf_t list = {0};
    bool ended = false;
    uint64_t off = 8;                                  /* "ALZ" 01 and 4 unknown bytes (spec 1.1) */
    while (!ended) {
        uint8_t sig[4];
        if (off > a.size || a.size - off < 4 || !read_at(&a, off, sig, 4)) break;   /* the data ends */
        off += 4;
        if (sig[0] == 'C' && sig[1] == 'L' && sig[2] == 'Z' && (sig[3] == 0x01 || sig[3] == 0x02)) {
            /* The central directory carries no list (spec 1.3), and what
               follows it is the end record: the files are all listed. */
            ended = true;
            break;
        }
        if (!(sig[0] == 'B' && sig[1] == 'L' && sig[2] == 'Z' && sig[3] == 0x01)) break;   /* damaged from here */

        uint8_t fixed[9];
        if (!read_at(&a, off, fixed, sizeof(fixed))) break;
        off += sizeof(fixed);
        rubraview_alz_entry_t e = {
            .attributes = fixed[2],
            .dos_time = le32(fixed + 3),
            .descriptor = fixed[7],
        };
        uint16_t name_len = le16(fixed);
        unsigned width = e.descriptor >> 4;
        e.encrypted = (e.descriptor & 0x01) != 0;
        if (width != 0 && width != 1 && width != 2 && width != 4 && width != 8) break;
        if (width) {
            uint8_t sizes[6 + 16];
            if (!read_at(&a, off, sizes, 6 + 2 * width)) break;
            off += 6 + 2 * width;
            e.method = sizes[0];
            e.crc32 = le32(sizes + 2);
            e.packed_size = le_n(sizes + 6, width);
            e.size = le_n(sizes + 6 + width, width);
        }

        uint8_t name[ALZ_NAME_MAX];
        bool listed = name_len > 0 && name_len <= ALZ_NAME_MAX && !(e.attributes & 0x10);
        if (listed) {
            if (!read_at(&a, off, name, name_len)) break;
            listed = name_is_safe(name, name_len);
        } else if (a.size - off < name_len) {
            break;
        }
        off += name_len;
        if (e.encrypted) off += ALZ_ENC_HEADER;
        e.data_offset = off;
        if (off > a.size || e.packed_size > a.size - off) {
            /* Cut off (spec 1: what was listed before stays readable). */
            e.truncated = true;
            a.damaged = true;
        }
        if (listed) {
            proven_result_mem_mut_t nres = proven_arena_alloc(arena, name_len);
            if (!proven_is_ok(nres.err)) { free(list.data); result.err = RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY; return result; }
            memcpy(nres.value.ptr, name, name_len);
            e.name = (u8str_t){ .ptr = (const char*)nres.value.ptr, .len = name_len };
            if (!entry_push(&list, &e)) { free(list.data); result.err = RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY; return result; }
        }
        if (e.truncated) break;
        off += e.packed_size;
    }
    if (!ended) a.damaged = true;

    if (list.count > 0) {
        proven_result_mem_mut_t eres = rubraview_arena_alloc_array(arena, list.count, sizeof(rubraview_alz_entry_t));
        if (!proven_is_ok(eres.err)) { free(list.data); result.err = RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY; return result; }
        a.entries = (rubraview_alz_entry_t*)(void*)eres.value.ptr;
        memcpy(a.entries, list.data, list.count * sizeof(*a.entries));
        a.entry_count = list.count;
    }
    free(list.data);
    result.err = RUBRAVIEW_ALZ_OK;
    result.value = a;
    return result;
}

/* ---- reading ------------------------------------------------------------ */

bool rubraview_alz_needs_password(const rubraview_alz_archive_t *archive) {
    if (!archive) return false;
    const alz_state_t *s = (const alz_state_t*)archive->state;
    if (s && s->password_ok) return false;
    for (size_t i = 0; i < archive->entry_count; ++i)
        if (archive->entries[i].encrypted) return true;
    return false;
}

rubraview_alz_err_t rubraview_alz_set_password(rubraview_alz_archive_t *archive, u8str_t password) {
    if (!archive) return RUBRAVIEW_ALZ_ERR_BAD_INDEX;
    alz_state_t *s = (alz_state_t*)archive->state;
    if (!s) {
        s = (alz_state_t*)calloc(1, sizeof(*s));
        if (!s) return RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY;
        archive->state = s;
    }
    state_forget(s);
    s->password = (uint8_t*)malloc(password.len ? password.len : 1);
    if (!s->password) return RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY;
    if (password.len) memcpy(s->password, password.ptr, password.len);
    s->password_len = password.len;

    /* Every encrypted file's check byte: one byte each, so a wrong password
       that passes them all is rare once there are a few. */
    zip_keys_t *z = (zip_keys_t*)malloc(sizeof(*z));
    if (!z) { state_forget(s); return RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY; }
    const rubraview_alz_entry_t *smallest = NULL;
    rubraview_alz_err_t err = RUBRAVIEW_ALZ_OK;
    for (size_t i = 0; i < archive->entry_count && err == RUBRAVIEW_ALZ_OK; ++i) {
        const rubraview_alz_entry_t *e = &archive->entries[i];
        if (!e->encrypted || e->truncated) continue;
        err = keys_for(archive, e, s->password, s->password_len, z);
        if (e->method <= RUBRAVIEW_ALZ_METHOD_DEFLATE && e->size <= ALZ_CHECK_DECODE_MAX &&
            (!smallest || e->size < smallest->size))
            smallest = e;
    }
    wipe(z, sizeof(*z));
    free(z);

    /* Then one of them decoded, and its CRC-32 compared (spec 4.2). */
    if (err == RUBRAVIEW_ALZ_OK && smallest) {
        uint8_t *out = (uint8_t*)malloc((size_t)smallest->size + 1);
        if (!out) err = RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY;
        else {
            err = decode(archive, smallest, s->password, s->password_len, out);
            wipe(out, (size_t)smallest->size);
            free(out);
            if (err == RUBRAVIEW_ALZ_ERR_CORRUPT_STREAM) err = RUBRAVIEW_ALZ_ERR_BAD_PASSWORD;
        }
    }
    if (err != RUBRAVIEW_ALZ_OK) {
        state_forget(s);
        return err;
    }
    s->password_ok = true;
    return RUBRAVIEW_ALZ_OK;
}

rubraview_alz_data_result_t rubraview_alz_read_entry(proven_arena_t *arena, const rubraview_alz_archive_t *archive,
                                                      size_t index, uint64_t max_entry_bytes) {
    rubraview_alz_data_result_t result = { .err = RUBRAVIEW_ALZ_ERR_BAD_INDEX, .data = { .ptr = "", .len = 0 } };
    if (!arena || !archive || index >= archive->entry_count) return result;
    const rubraview_alz_entry_t *e = &archive->entries[index];
    if (e->truncated) { result.err = RUBRAVIEW_ALZ_ERR_TRUNCATED; return result; }
    if (e->method > RUBRAVIEW_ALZ_METHOD_DEFLATE) { result.err = RUBRAVIEW_ALZ_ERR_UNSUPPORTED; return result; }
    if (e->size > max_entry_bytes || e->size >= SIZE_MAX) { result.err = RUBRAVIEW_ALZ_ERR_TOO_LARGE; return result; }
    const alz_state_t *s = (const alz_state_t*)archive->state;
    if (e->encrypted && !(s && s->password)) { result.err = RUBRAVIEW_ALZ_ERR_ENCRYPTED; return result; }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)e->size + 1);
    if (!proven_is_ok(res.err)) { result.err = RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY; return result; }
    uint8_t *out = (uint8_t*)res.value.ptr;
    result.err = decode(archive, e, s ? s->password : NULL, s ? s->password_len : 0, out);
    if (result.err != RUBRAVIEW_ALZ_OK) return result;
    out[e->size] = '\0';
    result.data = (u8str_t){ .ptr = (const char*)out, .len = (size_t)e->size };
    return result;
}

u8str_t rubraview_alz_volume_path(proven_arena_t *arena, u8str_t first_path, unsigned number) {
    u8str_t none = { .ptr = "", .len = 0 };
    if (!arena || number == 0 || number > 26 * 100 || first_path.len < 4) return none;
    const char *ext = first_path.ptr + first_path.len - 4;
    if (ext[0] != '.' || (ext[1] | 0x20) != 'a' || (ext[2] | 0x20) != 'l' || (ext[3] | 0x20) != 'z') return none;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, first_path.len + 1);
    if (!proven_is_ok(res.err)) return none;
    char *out = (char*)res.value.ptr;
    memcpy(out, first_path.ptr, first_path.len);
    unsigned i = number - 1;
    char letter = (char)('a' + i / 100);
    if (ext[1] == 'A') letter = (char)(letter - 'a' + 'A');
    out[first_path.len - 3] = letter;
    out[first_path.len - 2] = (char)('0' + (i % 100) / 10);
    out[first_path.len - 1] = (char)('0' + i % 10);
    out[first_path.len] = '\0';
    return (u8str_t){ .ptr = out, .len = first_path.len };
}

int32_t rubraview_alz_volume_number(const uint8_t *data, size_t size) {
    if (!data || size < 8 || !rubraview_alz_is_alz(data, size)) return -1;
    return (int32_t)le16(data + 6);
}

bool rubraview_alz_volume_is_last(const uint8_t *data, size_t size) {
    return data && size >= 4 && data[size - 4] == 'C' && data[size - 3] == 'L' && data[size - 2] == 'Z' && data[size - 1] == 0x02;
}

void rubraview_alz_close(rubraview_alz_archive_t *archive) {
    if (!archive) return;
    alz_state_t *s = (alz_state_t*)archive->state;
    if (s) {
        state_forget(s);
        free(s);
    }
    archive->state = NULL;
}
