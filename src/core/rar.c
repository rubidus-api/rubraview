/*
 * CBR / RAR archives: headers, volumes, passwords, and reading an entry through the codec
 * (rar_codec.h). Written from docs/specs/rar-decompression.md (sections 1-6, 13, 14) in the
 * rar-decoder clean-room session; CRC-32 as ISO 3309 describes it. MIT, like the rest of Rubraview.
 */
#include "rubraview/rar.h"
#include "rubraview/rar_codec.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* the registered codec */

static const rubraview_rar_codec_t *g_codec;

void rubraview_rar_set_codec(const struct rubraview_rar_codec *codec) {
    g_codec = (codec && codec->version == RUBRAVIEW_RAR_CODEC_VERSION) ? codec : NULL;
}

const struct rubraview_rar_codec *rubraview_rar_codec(void) { return g_codec; }

/* ------------------------------------------------------------------ */
/* small helpers */

static uint32_t crc_table[256];
static bool crc_ready;

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    if (!crc_ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
            crc_table[i] = c;
        }
        crc_ready = true;
    }
    crc = ~crc;
    while (n--) crc = (crc >> 8) ^ crc_table[(crc ^ *p++) & 0xFF];
    return ~crc;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wipe(void *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t*)p;
    while (n--) *v++ = 0;
}

static const uint8_t SIG4[7] = { 0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00 };
static const uint8_t SIG5[8] = { 0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x01, 0x00 };

/* Spec 1: the signature at any offset in the first MiB. Returns 4, 5 or 0. */
static int find_signature(const uint8_t *data, size_t size, size_t *at) {
    size_t limit = size < (1u << 20) ? size : (1u << 20);
    for (size_t i = 0; i < limit && i + 7 <= size; ++i) {
        if (data[i] != 0x52) continue;
        if (i + 8 <= size && memcmp(data + i, SIG5, 8) == 0) { *at = i; return 5; }
        if (memcmp(data + i, SIG4, 7) == 0) { *at = i; return 4; }
    }
    return 0;
}

bool rubraview_rar_is_rar(const uint8_t *data, size_t size) {
    size_t at;
    return data && find_signature(data, size, &at) != 0;
}

/* ------------------------------------------------------------------ */
/* the archive's heap state */

typedef struct key30 { uint8_t salt[8]; bool has_salt; uint8_t key[16], iv[16]; } key30_t;
typedef struct key50 { uint8_t salt[16]; uint8_t lg2; uint8_t key[32], hash_key[32], check[8]; } key50_t;

typedef struct rar_state {
    void *unpack;
    size_t next;                   /* the entry a solid stream continues with; SIZE_MAX when none */
    bool have_password;
    uint8_t pwd8[512];  size_t pwd8_len;
    uint8_t pwd16[256]; size_t pwd16_len;
    key30_t k30[4]; size_t k30_count, k30_next;
    key50_t k50[4]; size_t k50_count, k50_next;
    atomic_bool cancel;
    _Atomic uint64_t done, total;
} rar_state_t;

static void state_free(rar_state_t *st) {
    if (!st) return;
    if (st->unpack && g_codec) g_codec->unpack_destroy(st->unpack);
    wipe(st, sizeof(*st));
    free(st);
}

/* Spec 6.1.1 / 6.2.1 / 14: the password as UTF-8 and as UTF-16LE, 128 characters at most. */
static void state_set_password(rar_state_t *st, u8str_t pw) {
    size_t i = 0, chars = 0, n8 = 0, n16 = 0;
    const uint8_t *s = (const uint8_t*)pw.ptr;
    while (i < pw.len && chars < 128) {
        uint32_t cp = s[i];
        size_t len = 1;
        if (cp >= 0xF0 && i + 3 < pw.len) { cp = (cp & 7) << 18 | (s[i + 1] & 0x3Fu) << 12 | (s[i + 2] & 0x3Fu) << 6 | (s[i + 3] & 0x3Fu); len = 4; }
        else if (cp >= 0xE0 && i + 2 < pw.len) { cp = (cp & 15) << 12 | (s[i + 1] & 0x3Fu) << 6 | (s[i + 2] & 0x3Fu); len = 3; }
        else if (cp >= 0xC0 && i + 1 < pw.len) { cp = (cp & 31) << 6 | (s[i + 1] & 0x3Fu); len = 2; }
        if (n8 + len > sizeof(st->pwd8) || n16 + 4 > sizeof(st->pwd16)) break;
        memcpy(st->pwd8 + n8, s + i, len);
        n8 += len;
        if (cp >= 0x10000) {
            uint32_t v = cp - 0x10000, hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
            st->pwd16[n16++] = (uint8_t)hi; st->pwd16[n16++] = (uint8_t)(hi >> 8);
            st->pwd16[n16++] = (uint8_t)lo; st->pwd16[n16++] = (uint8_t)(lo >> 8);
        } else {
            st->pwd16[n16++] = (uint8_t)cp; st->pwd16[n16++] = (uint8_t)(cp >> 8);
        }
        i += len;
        chars++;
    }
    st->pwd8_len = n8;
    st->pwd16_len = n16;
    st->have_password = true;
    st->k30_count = st->k50_count = 0;
}

static void state_clear_password(rar_state_t *st) {
    wipe(st->pwd8, sizeof(st->pwd8));
    wipe(st->pwd16, sizeof(st->pwd16));
    wipe(st->k30, sizeof(st->k30));
    wipe(st->k50, sizeof(st->k50));
    st->pwd8_len = st->pwd16_len = 0;
    st->k30_count = st->k50_count = 0;
    st->have_password = false;
}

/* Key derivations are slow by design: the last four are kept (spec 6.1.1). */
static const key30_t *keys30(rar_state_t *st, const uint8_t *salt) {
    for (size_t i = 0; i < st->k30_count; ++i) {
        const key30_t *k = &st->k30[i];
        if (k->has_salt == (salt != NULL) && (!salt || memcmp(k->salt, salt, 8) == 0)) return k;
    }
    key30_t *k = &st->k30[st->k30_next];
    st->k30_next = (st->k30_next + 1) % 4;
    if (st->k30_count < 4) st->k30_count++;
    k->has_salt = salt != NULL;
    if (salt) memcpy(k->salt, salt, 8);
    g_codec->kdf30(st->pwd16, st->pwd16_len, salt, k->key, k->iv);
    return k;
}

static const key50_t *keys50(rar_state_t *st, const uint8_t salt[16], uint8_t lg2) {
    for (size_t i = 0; i < st->k50_count; ++i) {
        const key50_t *k = &st->k50[i];
        if (k->lg2 == lg2 && memcmp(k->salt, salt, 16) == 0) return k;
    }
    key50_t tmp;
    memcpy(tmp.salt, salt, 16);
    tmp.lg2 = lg2;
    if (!g_codec->kdf50(st->pwd8, st->pwd8_len, salt, lg2, tmp.key, tmp.hash_key, tmp.check)) return NULL;
    key50_t *k = &st->k50[st->k50_next];
    st->k50_next = (st->k50_next + 1) % 4;
    if (st->k50_count < 4) st->k50_count++;
    *k = tmp;
    wipe(&tmp, sizeof(tmp));
    return k;
}

/* ------------------------------------------------------------------ */
/* growable arrays while parsing */

typedef struct vec { void *data; size_t count, cap, elem; } vec_t;

static void *vec_push(vec_t *v) {
    if (v->count == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 16;
        void *d = realloc(v->data, cap * v->elem);
        if (!d) return NULL;
        v->data = d;
        v->cap = cap;
    }
    void *slot = (uint8_t*)v->data + v->count * v->elem;
    memset(slot, 0, v->elem);
    v->count++;
    return slot;
}

/* ------------------------------------------------------------------ */
/* parsing */

typedef struct parser {
    proven_arena_t *arena;
    rar_state_t *st;
    const rubraview_rar_volume_t *vols;
    size_t vol_count;
    vec_t entries, pieces;
    long pending;                  /* the entry whose next part is due, or -1 */
    bool rar5, solid, headers_encrypted, is_volume, new_numbering;
    bool password_given;
    uint8_t hkey[32];              /* RAR 5 header key of the current volume */
    uint8_t *buf; size_t cap;      /* one header, decrypted */
} parser_t;

typedef enum perr { P_OK = 0, P_END, P_CORRUPT, P_ENCRYPTED, P_BAD_PASSWORD, P_NO_CODEC, P_MEMORY } perr_t;

static bool reserve(parser_t *p, size_t n) {
    if (n <= p->cap) return true;
    size_t cap = p->cap ? p->cap : 256;
    while (cap < n) cap *= 2;
    uint8_t *b = (uint8_t*)realloc(p->buf, cap);
    if (!b) return false;
    p->buf = b;
    p->cap = cap;
    return true;
}

static u8str_t arena_copy(proven_arena_t *arena, const void *src, size_t n) {
    proven_result_mem_mut_t r = proven_arena_alloc(arena, n + 1);
    if (!proven_is_ok(r.err)) return (u8str_t){ .ptr = NULL, .len = 0 };
    if (n) memcpy(r.value.ptr, src, n);
    r.value.ptr[n] = 0;
    return (u8str_t){ .ptr = (const char*)r.value.ptr, .len = n };
}

static size_t utf8_put(uint8_t *o, uint32_t cp) {
    if (cp < 0x80) { o[0] = (uint8_t)cp; return 1; }
    if (cp < 0x800) { o[0] = (uint8_t)(0xC0 | cp >> 6); o[1] = (uint8_t)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        o[0] = (uint8_t)(0xE0 | cp >> 12); o[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (uint8_t)(0xF0 | cp >> 18); o[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (uint8_t)(0x80 | (cp & 0x3F));
    return 4;
}

/* Spec 2.4: the narrow name, then the encoded UTF-16 one; to UTF-8 with '/' separators. */
static u8str_t rar4_name(proven_arena_t *arena, const uint8_t *name, size_t len, bool unicode, bool *legacy) {
    *legacy = false;
    const uint8_t *zero = unicode ? memchr(name, 0, len) : NULL;
    uint8_t *tmp = NULL;
    size_t out_len = 0;
    if (zero) {
        size_t nlen = (size_t)(zero - name);
        const uint8_t *enc = zero + 1;
        size_t elen = len - nlen - 1;
        uint16_t *u = (uint16_t*)malloc((nlen + 1) * sizeof(uint16_t));
        tmp = (uint8_t*)malloc(nlen * 4 + 4);
        if (!u || !tmp) { free(u); free(tmp); return (u8str_t){ .ptr = NULL, .len = 0 }; }
        size_t n = 0;
        if (elen > 0) {
            uint8_t high = enc[0], flags = 0;
            size_t pos = 1;
            int flagbits = 0;
            while (pos < elen && n < nlen) {
                if (flagbits == 0) {
                    flags = enc[pos++];
                    flagbits = 8;
                    if (pos == elen) break;
                }
                flagbits -= 2;
                int mode = (flags >> flagbits) & 3;
                if (mode == 0) u[n++] = enc[pos++];
                else if (mode == 1) u[n++] = (uint16_t)(high << 8 | enc[pos++]);
                else if (mode == 2) {
                    if (pos + 2 > elen) break;
                    u[n++] = (uint16_t)(enc[pos] | enc[pos + 1] << 8);
                    pos += 2;
                } else {
                    uint8_t l = enc[pos++];
                    size_t count = (size_t)(l & 0x7F) + 2;
                    if (l & 0x80) {
                        if (pos >= elen) break;
                        uint8_t corr = enc[pos++];
                        for (size_t k = 0; k < count && n < nlen; ++k) u[n] = (uint16_t)(high << 8 | ((name[n] + corr) & 0xFF)), n++;
                    } else {
                        for (size_t k = 0; k < count && n < nlen; ++k) u[n] = name[n], n++;
                    }
                }
            }
        }
        for (size_t i = 0; i < n; ++i) {
            uint32_t cp = u[i];
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (u[i + 1] - 0xDC00u);
                i++;
            } else if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
            out_len += utf8_put(tmp + out_len, cp);
        }
        free(u);
        if (n == 0) { memcpy(tmp, name, nlen); out_len = nlen; }   /* nothing encoded: the narrow name */
    } else {
        tmp = (uint8_t*)malloc(len + 1);
        if (!tmp) return (u8str_t){ .ptr = NULL, .len = 0 };
        memcpy(tmp, name, len);
        out_len = len;
        *legacy = !unicode;
    }
    for (size_t i = 0; i < out_len; ++i) if (tmp[i] == '\\') tmp[i] = '/';
    u8str_t r = arena_copy(arena, tmp, out_len);
    free(tmp);
    return r;
}

static rubraview_rar_entry_t *entry_at(parser_t *p, long i) {
    return &((rubraview_rar_entry_t*)p->entries.data)[i];
}

static bool add_piece(parser_t *p, rubraview_rar_entry_t *e, uint32_t vol, uint64_t off, uint64_t size) {
    rubraview_rar_piece_t *pc = (rubraview_rar_piece_t*)vec_push(&p->pieces);
    if (!pc) return false;
    pc->volume = vol;
    pc->offset = off;
    pc->size = size;
    e->piece_count++;
    return true;
}

/* A file part, after its header is parsed. `cont` is a part that continues one before it. */
typedef struct part {
    u8str_t name;                  /* raw, for matching parts */
    bool before, after;
    uint64_t data_off, data_size;
    uint32_t crc; bool has_crc;
    uint8_t hash[32]; bool has_hash;
    uint8_t mac_salt[16]; uint8_t mac_lg2; bool mac_set;
} part_t;

static perr_t take_part(parser_t *p, uint32_t vol, const part_t *pt, const rubraview_rar_entry_t *proto, bool skip) {
    if (skip) return P_OK;   /* directories and links */
    if (pt->before) {
        if (p->pending < 0) return P_OK;   /* the rest of a file that began in a volume not opened */
        rubraview_rar_entry_t *e = entry_at(p, p->pending);
        if (!rubraview_u8_eq(e->name, pt->name)) {
            e->split = true;               /* "mismatch of file parts" (spec 4.2) */
            p->pending = -1;
            return P_OK;
        }
        if (!add_piece(p, e, vol, pt->data_off, pt->data_size)) return P_MEMORY;
        e->packed_size += pt->data_size;
        if (!pt->after) {
            e->crc32 = pt->crc; e->has_crc = pt->has_crc;
            memcpy(e->hash, pt->hash, 32); e->has_hash = pt->has_hash;
            if (pt->mac_set) { memcpy(e->mac_salt, pt->mac_salt, 16); e->mac_lg2 = pt->mac_lg2; }
            p->pending = -1;
        }
        return P_OK;
    }
    if (p->pending >= 0) { entry_at(p, p->pending)->split = true; p->pending = -1; }
    rubraview_rar_entry_t *e = (rubraview_rar_entry_t*)vec_push(&p->entries);
    if (!e) return P_MEMORY;
    *e = *proto;
    e->first_piece = (uint32_t)p->pieces.count;
    e->piece_count = 0;
    e->data_offset = pt->data_off;
    e->packed_size = pt->data_size;
    if (!add_piece(p, e, vol, pt->data_off, pt->data_size)) return P_MEMORY;
    if (pt->after) p->pending = (long)(p->entries.count - 1);
    else {
        e->crc32 = pt->crc; e->has_crc = pt->has_crc;
        memcpy(e->hash, pt->hash, 32); e->has_hash = pt->has_hash;
    }
    return P_OK;
}

/* ---- RAR 4 (spec 2) ---- */

static perr_t rar4_volume(parser_t *p, uint32_t vol, size_t start, bool *more) {
    const uint8_t *d = p->vols[vol].data;
    size_t size = p->vols[vol].size;
    size_t pos = start + 7;
    bool enc = false, first_block = true;
    *more = false;
    for (;;) {
        size_t hsize, block_end;
        const uint8_t *h;
        if (enc) {
            if (size - pos < 8 + 16 || pos > size) return P_END;
            if (!g_codec) return P_NO_CODEC;
            const uint8_t *salt = d + pos;
            const key30_t *k = keys30(p->st, salt);
            if (!reserve(p, 16)) return P_MEMORY;
            memcpy(p->buf, d + pos + 8, 16);
            void *aes = g_codec->aes_create(k->key, 128, k->iv);
            if (!aes) return P_MEMORY;
            g_codec->aes_decrypt(aes, p->buf, 16);
            hsize = rd16(p->buf + 5);
            size_t n = (hsize + 15) & ~(size_t)15;
            if (hsize < 7 || n > size - pos - 8) {
                g_codec->aes_destroy(aes);
                return first_block ? P_BAD_PASSWORD : P_CORRUPT;
            }
            if (!reserve(p, n)) { g_codec->aes_destroy(aes); return P_MEMORY; }
            memcpy(p->buf + 16, d + pos + 8 + 16, n - 16);
            g_codec->aes_decrypt(aes, p->buf + 16, n - 16);
            g_codec->aes_destroy(aes);
            h = p->buf;
            block_end = pos + 8 + n;
            if ((crc32_update(0, h + 2, hsize - 2) & 0xFFFF) != rd16(h)) return first_block ? P_BAD_PASSWORD : P_CORRUPT;
        } else {
            if (pos > size || size - pos < 7) return P_END;
            h = d + pos;
            hsize = rd16(h + 5);
            if (hsize < 7 || hsize > size - pos) return P_CORRUPT;
            block_end = pos + hsize;
        }
        first_block = false;
        uint8_t type = h[2];
        uint16_t flags = rd16(h + 3);
        uint64_t add = 0;
        if ((flags & 0x8000) && hsize >= 11) add = rd32(h + 7);
        if (!enc) {
            uint32_t crc;
            if (type == 0x75) crc = crc32_update(0, h + 2, hsize < 13 ? hsize - 2 : 11);
            else if (type == 0x73 && (flags & 0x0002) && hsize > 13) crc = crc32_update(0, h + 2, 11);
            else crc = crc32_update(0, h + 2, hsize - 2);
            if ((crc & 0xFFFF) != rd16(h)) {
                bool ok = false;   /* spec 15 #18: a skippable block may count its data too */
                if (type != 0x73 && type != 0x74 && type != 0x7A && (flags & 0x8000) && add <= size - block_end) {
                    crc = crc32_update(crc32_update(0, h + 2, hsize - 2), d + block_end, (size_t)add);
                    ok = (crc & 0xFFFF) == rd16(h);
                }
                if (!ok) return P_CORRUPT;
            }
        }
        switch (type) {
        case 0x73:
            p->is_volume = flags & 0x0001;
            p->solid = flags & 0x0008;
            p->new_numbering = flags & 0x0010;
            if (flags & 0x0080) {
                p->headers_encrypted = true;
                if (!p->password_given) return P_ENCRYPTED;
                enc = true;
                first_block = true;
            }
            if (!enc && (flags & 0x0002) && hsize > 13) block_end = pos + 13;   /* spec 2.2: old comment follows */
            pos = block_end;
            continue;
        case 0x74: case 0x7A: {
            if (hsize < 32) return P_CORRUPT;
            uint64_t pack = rd32(h + 7), unp = rd32(h + 11);
            uint8_t host = h[15];
            uint32_t fcrc = rd32(h + 16);
            uint8_t unp_ver = h[24], method = h[25];
            size_t name_size = rd16(h + 26);
            uint32_t attr = rd32(h + 28);
            size_t at = 32;
            bool unknown = false;
            if (flags & 0x0100) {
                if (hsize < 40) return P_CORRUPT;
                pack |= (uint64_t)rd32(h + 32) << 32;
                uint32_t hi = rd32(h + 36);
                unknown = unp == 0xFFFFFFFFu && hi == 0xFFFFFFFFu;
                unp |= (uint64_t)hi << 32;
                at = 40;
                if (!unknown && (unp >> 63)) return P_CORRUPT;
                if (pack >> 63) return P_CORRUPT;
            } else unknown = unp == 0xFFFFFFFFu;
            if (name_size > hsize - at) return P_CORRUPT;
            const uint8_t *name = h + at;
            at += name_size;
            const uint8_t *salt = NULL;
            if (flags & 0x0400) {
                if (hsize - at < 8) return P_CORRUPT;
                salt = h + at;
                at += 8;
            }
            if (pack > size - block_end) {
                if (type == 0x7A) return P_END;
                pack = size - block_end;   /* cut short: reading it will say so */
            }
            uint64_t data_off = block_end;
            pos = block_end + (size_t)pack;
            if (type == 0x7A) continue;
            bool dir = (flags & 0x00E0) == 0x00E0;
            bool link = host >= 3 && host <= 5 && (attr & 0xF000) == 0xA000;
            size_t nlen = name_size;
            if (flags & 0x0800) {   /* ";n" version suffix */
                for (size_t i = nlen; i-- > 0;) {
                    if (name[i] == ';') { nlen = i; break; }
                    if (name[i] < '0' || name[i] > '9') break;
                }
            }
            rubraview_rar_entry_t proto = {0};
            bool legacy = false;
            proto.name = rar4_name(p->arena, name, nlen, flags & 0x0200, &legacy);
            if (!proto.name.ptr) return P_MEMORY;
            part_t pt = {0};
            pt.name = proto.name;
            pt.before = flags & 0x0001;
            pt.after = flags & 0x0002;
            pt.data_off = data_off;
            pt.data_size = pack;
            pt.crc = fcrc;
            pt.has_crc = true;
            proto.name_is_legacy = legacy;
            proto.size = unknown ? RUBRAVIEW_RAR_SIZE_UNKNOWN : unp;
            proto.method = method == 0x30 ? 0 : unp_ver;
            proto.dict_size = (uint64_t)0x10000 << ((flags >> 5) & 7);
            proto.solid = flags & 0x0010;
            if (flags & 0x0004) {
                proto.encrypted = true;
                proto.crypt = salt ? RUBRAVIEW_RAR_CRYPT_30 : RUBRAVIEW_RAR_CRYPT_OLD;
                if (salt) { memcpy(proto.salt, salt, 8); proto.salt_set = true; }
            }
            perr_t r = take_part(p, vol, &pt, &proto, dir || link);
            if (r != P_OK) return r;
            continue;
        }
        case 0x7B:
            *more = flags & 0x0001;
            return P_END;
        default:
            pos = block_end;
            if (flags & 0x8000) {
                if (add > size - pos) return P_END;
                pos += (size_t)add;
            }
            continue;
        }
    }
}

/* ---- RAR 5 (spec 3) ---- */

typedef struct rd5 { const uint8_t *p; size_t n, at; bool bad; } rd5_t;

static uint64_t vint(rd5_t *r) {
    uint64_t v = 0;
    for (int i = 0; i < 10; ++i) {
        if (r->at >= r->n) { r->bad = true; return 0; }
        uint8_t b = r->p[r->at++];
        v |= (uint64_t)(b & 0x7F) << (7 * i);
        if (!(b & 0x80)) return v;
    }
    r->bad = true;
    return 0;
}

static const uint8_t *bytes5(rd5_t *r, size_t n) {
    if (r->n - r->at < n || r->at > r->n) { r->bad = true; return NULL; }
    const uint8_t *q = r->p + r->at;
    r->at += n;
    return q;
}

/* Reads one header at `pos`: plain, or decrypted with the volume's header key (spec 6.2.4). */
static perr_t rar5_header(parser_t *p, uint32_t vol, size_t pos, bool enc, const uint8_t **out, size_t *len, size_t *end) {
    const uint8_t *d = p->vols[vol].data;
    size_t size = p->vols[vol].size;
    if (pos > size) return P_END;
    if (enc) {
        if (size - pos < 32) return P_END;
        if (!reserve(p, 16)) return P_MEMORY;
        memcpy(p->buf, d + pos + 16, 16);
        void *aes = g_codec->aes_create(p->hkey, 256, d + pos);
        if (!aes) return P_MEMORY;
        g_codec->aes_decrypt(aes, p->buf, 16);
        rd5_t r = { p->buf + 4, 12, 0, false };
        uint64_t hs = vint(&r);
        if (r.bad || r.at > 3 || hs == 0) { g_codec->aes_destroy(aes); return P_CORRUPT; }
        size_t total = 4 + r.at + (size_t)hs;
        size_t n = (total + 15) & ~(size_t)15;
        if (n > size - pos - 16) { g_codec->aes_destroy(aes); return P_CORRUPT; }
        if (!reserve(p, n)) { g_codec->aes_destroy(aes); return P_MEMORY; }
        memcpy(p->buf + 16, d + pos + 32, n - 16);
        g_codec->aes_decrypt(aes, p->buf + 16, n - 16);
        g_codec->aes_destroy(aes);
        if (crc32_update(0, p->buf + 4, total - 4) != rd32(p->buf)) return P_CORRUPT;
        *out = p->buf;
        *len = total;
        *end = pos + 16 + n;
        return P_OK;
    }
    if (size - pos < 5) return P_END;
    rd5_t r = { d + pos + 4, size - pos - 4, 0, false };
    uint64_t hs = vint(&r);
    if (r.bad || r.at > 3 || hs == 0 || hs > r.n - r.at) return P_CORRUPT;
    size_t total = 4 + r.at + (size_t)hs;
    if (crc32_update(0, d + pos + 4, total - 4) != rd32(d + pos)) return P_CORRUPT;
    *out = d + pos;
    *len = total;
    *end = pos + total;
    return P_OK;
}

static perr_t rar5_volume(parser_t *p, uint32_t vol, size_t start, bool *more) {
    size_t pos = start + 8;
    size_t size = p->vols[vol].size;
    bool enc = false, first_enc = false;
    *more = false;
    for (;;) {
        const uint8_t *h;
        size_t hlen, hend;
        perr_t e = rar5_header(p, vol, pos, enc, &h, &hlen, &hend);
        if (e == P_CORRUPT && first_enc) return P_BAD_PASSWORD;   /* no check value, and it does not decrypt */
        if (e != P_OK) return e;
        first_enc = false;
        rd5_t r = { h + 4, hlen - 4, 0, false };
        (void)vint(&r);
        uint64_t type = vint(&r), hflags = vint(&r);
        uint64_t extra_size = (hflags & 1) ? vint(&r) : 0;
        uint64_t data_size = (hflags & 2) ? vint(&r) : 0;
        if (r.bad || extra_size > r.n - r.at) return P_CORRUPT;
        size_t extra_at = r.n - (size_t)extra_size;
        if (data_size > size - hend) {
            if (type != 2) return P_END;
            data_size = size - hend;
        }
        pos = hend + (size_t)data_size;
        if (type == 4) {   /* spec 3.3 */
            uint64_t ver = vint(&r), eflags = vint(&r);
            const uint8_t *kdf = bytes5(&r, 1), *salt = bytes5(&r, 16);
            const uint8_t *check = (eflags & 1) ? bytes5(&r, 12) : NULL;
            if (r.bad || ver != 0) return P_CORRUPT;
            p->headers_encrypted = true;
            if (!g_codec) return P_NO_CODEC;
            if (!p->password_given) return P_ENCRYPTED;
            const key50_t *k = keys50(p->st, salt, kdf[0]);
            if (!k) return P_CORRUPT;
            if (check) {
                uint8_t sum[32];
                g_codec->sha256(check, 8, sum);
                if (memcmp(sum, check + 8, 4) == 0 && memcmp(check, k->check, 8) != 0) return P_BAD_PASSWORD;
            }
            memcpy(p->hkey, k->key, 32);
            enc = true;
            first_enc = !check;
            continue;
        }
        if (type == 1) {   /* spec 3.4 */
            uint64_t aflags = vint(&r);
            uint64_t number = (aflags & 2) ? vint(&r) : 0;
            if (r.bad) return P_CORRUPT;
            if (vol == 0) {
                p->is_volume = aflags & 1;
                p->solid = aflags & 4;
            }
            if (number != vol) return P_CORRUPT;
            continue;
        }
        if (type == 5) { *more = vint(&r) & 1; return P_END; }
        if (type != 2) continue;
        /* spec 3.5 */
        uint64_t fflags = vint(&r), unp = vint(&r);
        (void)vint(&r);   /* attributes */
        if (fflags & 2) (void)bytes5(&r, 4);
        const uint8_t *crc = (fflags & 4) ? bytes5(&r, 4) : NULL;
        uint64_t ci = vint(&r);
        (void)vint(&r);   /* host OS */
        uint64_t nlen = vint(&r);
        if (r.bad || nlen > r.n - r.at) return P_CORRUPT;
        const uint8_t *name = bytes5(&r, (size_t)nlen);
        if (r.bad) return P_CORRUPT;
        part_t pt = {0};
        rubraview_rar_entry_t proto = {0};
        bool link = false;
        rd5_t x = { r.p, r.n, extra_at, false };
        while (x.at < x.n) {   /* spec 3.6 */
            uint64_t rsize = vint(&x);
            if (x.bad || rsize == 0 || rsize > x.n - x.at) return P_CORRUPT;
            size_t rend = x.at + (size_t)rsize;
            rd5_t rr = { x.p, rend, x.at, false };
            uint64_t rtype = vint(&rr);
            if (rtype == 1) {
                uint64_t ver = vint(&rr), ef = vint(&rr);
                const uint8_t *kdf = bytes5(&rr, 1), *salt = bytes5(&rr, 16), *iv = bytes5(&rr, 16);
                const uint8_t *check = (ef & 1) ? bytes5(&rr, 12) : NULL;
                proto.encrypted = true;
                if (rr.bad || ver != 0) proto.crypt = RUBRAVIEW_RAR_CRYPT_OLD;   /* nothing this reader knows */
                else {
                    proto.crypt = RUBRAVIEW_RAR_CRYPT_50;
                    proto.lg2_count = kdf[0];
                    memcpy(proto.salt, salt, 16); proto.salt_set = true;
                    memcpy(proto.iv, iv, 16);
                    proto.hash_mac = ef & 2;
                    memcpy(pt.mac_salt, salt, 16); pt.mac_lg2 = kdf[0]; pt.mac_set = true;
                    memcpy(proto.mac_salt, salt, 16); proto.mac_lg2 = kdf[0];
                    if (check) {
                        memcpy(proto.psw_check, check, 8);
                        memcpy(proto.psw_check_sum, check + 8, 4);
                        proto.psw_check_set = true;
                    }
                }
            } else if (rtype == 2) {
                uint64_t ht = vint(&rr);
                const uint8_t *hv = bytes5(&rr, 32);
                if (!rr.bad && ht == 0) { memcpy(pt.hash, hv, 32); pt.has_hash = true; }
            } else if (rtype == 5) link = true;
            x.at = rend;
        }
        if (fflags & 1) link = true;   /* a directory: not listed */
        pt.name = arena_copy(p->arena, name, (size_t)nlen);
        if (!pt.name.ptr) return P_MEMORY;
        pt.before = hflags & 0x0008;
        pt.after = hflags & 0x0010;
        pt.data_off = hend;
        pt.data_size = data_size;
        if (crc) { pt.crc = rd32(crc); pt.has_crc = true; }
        proto.name = pt.name;
        proto.size = (fflags & 8) ? RUBRAVIEW_RAR_SIZE_UNKNOWN : unp;
        unsigned ver = (unsigned)(ci & 0x3F), meth = (unsigned)((ci >> 7) & 7), n = (unsigned)((ci >> 10) & 0x1F);
        proto.solid = ci & 0x40;
        if (meth == 0) proto.method = 0;
        else if (ver == 0) proto.method = n <= 15 ? 50 : 255;
        else if (ver == 1) proto.method = (ci & 0x100000) ? 50 : 70;
        else proto.method = 255;   /* unknown: refused when read */
        uint64_t dict = (uint64_t)0x20000 << n;
        if (ver == 1) dict += (dict / 32) * ((ci >> 15) & 0x1F);
        proto.dict_size = dict;
        perr_t pr = take_part(p, vol, &pt, &proto, link);
        if (pr != P_OK) return pr;
    }
}

/* ---- the whole set ---- */

static rubraview_rar_err_t perr_map(perr_t e) {
    switch (e) {
    case P_OK: case P_END: return RUBRAVIEW_RAR_OK;
    case P_ENCRYPTED: return RUBRAVIEW_RAR_ERR_ENCRYPTED;
    case P_BAD_PASSWORD: return RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
    case P_NO_CODEC: return RUBRAVIEW_RAR_ERR_NO_CODEC;
    case P_MEMORY: return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
    default: return RUBRAVIEW_RAR_ERR_CORRUPT;
    }
}

rubraview_rar_result_t rubraview_rar_open_volumes(proven_arena_t *arena, const rubraview_rar_volume_t *volumes,
                                                  size_t volume_count, u8str_t password) {
    rubraview_rar_result_t out = { .err = RUBRAVIEW_RAR_ERR_NOT_A_RAR };
    if (!arena || !volumes || volume_count == 0 || !volumes[0].data) return out;
    size_t start;
    int fmt = find_signature(volumes[0].data, volumes[0].size, &start);
    if (fmt == 0) return out;

    parser_t p = { .arena = arena, .vols = volumes, .vol_count = volume_count, .pending = -1 };
    p.entries.elem = sizeof(rubraview_rar_entry_t);
    p.pieces.elem = sizeof(rubraview_rar_piece_t);
    p.rar5 = fmt == 5;
    p.st = (rar_state_t*)calloc(1, sizeof(rar_state_t));
    if (!p.st) { out.err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; return out; }
    p.st->next = SIZE_MAX;
    if (password.len > 0) { state_set_password(p.st, password); p.password_given = true; }

    perr_t err = P_OK;
    size_t used = 0;
    for (size_t v = 0; v < volume_count; ++v) {
        size_t vstart = start;
        if (v > 0) {
            if (!volumes[v].data || find_signature(volumes[v].data, volumes[v].size, &vstart) != fmt) break;
        }
        bool more = false;
        perr_t e = p.rar5 ? rar5_volume(&p, (uint32_t)v, vstart, &more) : rar4_volume(&p, (uint32_t)v, vstart, &more);
        if (e != P_END && e != P_OK) {
            if (v == 0 || e == P_MEMORY) { err = e; break; }
            break;   /* a later volume that does not read: what is in it is missing */
        }
        used = v + 1;
        if (v == 0 && !p.is_volume) break;
        if (!more && p.pending < 0) break;
    }
    if (err == P_OK && p.pending >= 0) entry_at(&p, p.pending)->split = true;
    if (err != P_OK) {
        out.err = perr_map(err);
        if (out.err == RUBRAVIEW_RAR_OK) out.err = RUBRAVIEW_RAR_ERR_CORRUPT;
        goto fail;
    }
    (void)used;

    rubraview_rar_archive_t *a = &out.value;
    a->data = volumes[0].data;
    a->size = volumes[0].size;
    proven_result_mem_mut_t vr = rubraview_arena_alloc_array(arena, volume_count, sizeof(rubraview_rar_volume_t));
    proven_result_mem_mut_t er = rubraview_arena_alloc_array(arena, p.entries.count + 1, sizeof(rubraview_rar_entry_t));
    proven_result_mem_mut_t pr = rubraview_arena_alloc_array(arena, p.pieces.count + 1, sizeof(rubraview_rar_piece_t));
    if (!proven_is_ok(vr.err) || !proven_is_ok(er.err) || !proven_is_ok(pr.err)) { out.err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; goto fail; }
    memcpy(vr.value.ptr, volumes, volume_count * sizeof(rubraview_rar_volume_t));
    if (p.entries.count) memcpy(er.value.ptr, p.entries.data, p.entries.count * sizeof(rubraview_rar_entry_t));
    if (p.pieces.count) memcpy(pr.value.ptr, p.pieces.data, p.pieces.count * sizeof(rubraview_rar_piece_t));
    a->volumes = (const rubraview_rar_volume_t*)(void*)vr.value.ptr;
    a->volume_count = volume_count;
    a->entries = (rubraview_rar_entry_t*)(void*)er.value.ptr;
    a->entry_count = p.entries.count;
    a->pieces = (rubraview_rar_piece_t*)(void*)pr.value.ptr;
    a->piece_count = p.pieces.count;
    a->rar5 = p.rar5;
    a->solid_archive = p.solid;
    a->headers_encrypted = p.headers_encrypted;
    a->is_volume = p.is_volume;
    a->new_numbering = p.rar5 || p.new_numbering;
    /* Headers that opened with the password prove it (RAR 4: their CRCs; RAR 5: the check value). */
    if (!p.headers_encrypted && p.st->have_password) state_clear_password(p.st);
    a->state = p.st;
    for (size_t i = 0; i < a->entry_count; ++i) {
        if (a->entries[i].first_piece + a->entries[i].piece_count > a->piece_count) a->entries[i].split = true;
    }
    free(p.entries.data);
    free(p.pieces.data);
    free(p.buf);
    out.err = RUBRAVIEW_RAR_OK;
    return out;

fail:
    free(p.entries.data);
    free(p.pieces.data);
    free(p.buf);
    state_free(p.st);
    out.value = (rubraview_rar_archive_t){0};
    return out;
}

rubraview_rar_result_t rubraview_rar_open(proven_arena_t *arena, const uint8_t *data, size_t size) {
    rubraview_rar_volume_t one = { .data = data, .size = size };
    return rubraview_rar_open_volumes(arena, &one, 1, (u8str_t){ .ptr = "", .len = 0 });
}

void rubraview_rar_close(rubraview_rar_archive_t *archive) {
    if (!archive) return;
    state_free((rar_state_t*)archive->state);
    archive->state = NULL;
}

/* ------------------------------------------------------------------ */
/* questions about entries */

bool rubraview_rar_needs_codec(const rubraview_rar_archive_t *archive, size_t index) {
    if (!archive || index >= archive->entry_count) return false;
    const rubraview_rar_entry_t *e = &archive->entries[index];
    return !g_codec && (e->method != 0 || e->encrypted);
}

bool rubraview_rar_needs_password(const rubraview_rar_archive_t *archive) {
    if (!archive || !archive->state) return false;
    const rar_state_t *st = (const rar_state_t*)archive->state;
    if (st->have_password) return false;
    for (size_t i = 0; i < archive->entry_count; ++i) {
        const rubraview_rar_entry_t *e = &archive->entries[i];
        if (e->encrypted && e->crypt != RUBRAVIEW_RAR_CRYPT_OLD) return true;
    }
    return false;
}

/* The entries decoded before `index` when it is read now (spec 7.5): from the start of its solid chain,
   or on from where the decoder stopped. */
static size_t chain_start(const rubraview_rar_archive_t *a, size_t index) {
    size_t s = index;
    while (a->entries[s].solid && a->entries[s].method != 0) {
        size_t q = s;
        while (q > 0 && a->entries[q - 1].method == 0) q--;
        if (q == 0) break;
        s = q - 1;
    }
    return s;
}

static size_t read_from(const rubraview_rar_archive_t *a, size_t index) {
    if (a->entries[index].method == 0) return index;
    const rar_state_t *st = (const rar_state_t*)a->state;
    size_t s = chain_start(a, index);
    if (st && st->next != SIZE_MAX && st->next > s && st->next <= index) return st->next;
    return s;
}

uint64_t rubraview_rar_read_cost(const rubraview_rar_archive_t *archive, size_t index) {
    if (!archive || !archive->state || index >= archive->entry_count) return 0;
    uint64_t cost = 0;
    for (size_t i = read_from(archive, index); i <= index; ++i) {
        const rubraview_rar_entry_t *e = &archive->entries[i];
        if (i != index && e->method == 0) continue;
        cost += e->size == RUBRAVIEW_RAR_SIZE_UNKNOWN ? e->packed_size : e->size;
    }
    return cost;
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

/* ------------------------------------------------------------------ */
/* reading */

typedef struct src {
    const rubraview_rar_archive_t *a;
    const rubraview_rar_entry_t *e;
    uint32_t piece;
    uint64_t off;
    void *aes;
    rar_state_t *st;
} src_t;

static size_t src_raw(src_t *s, uint8_t *buf, size_t cap) {
    size_t got = 0;
    while (got < cap && s->piece < s->e->piece_count) {
        const rubraview_rar_piece_t *pc = &s->a->pieces[s->e->first_piece + s->piece];
        const rubraview_rar_volume_t *v = &s->a->volumes[pc->volume];
        uint64_t avail = pc->size - s->off;
        if (pc->offset > v->size || pc->size > v->size - pc->offset) avail = 0;   /* checked when opened; never trusted */
        if (avail == 0) { s->piece++; s->off = 0; continue; }
        size_t n = cap - got < avail ? cap - got : (size_t)avail;
        memcpy(buf + got, v->data + pc->offset + s->off, n);
        got += n;
        s->off += n;
    }
    return got;
}

static size_t src_read(void *ctx, uint8_t *buf, size_t cap) {
    src_t *s = (src_t*)ctx;
    if (atomic_load(&s->st->cancel)) return 0;
    if (!s->aes) return src_raw(s, buf, cap);
    cap &= ~(size_t)15;   /* one CBC chain over all the parts (spec 4.2) */
    size_t got = src_raw(s, buf, cap);
    got &= ~(size_t)15;
    g_codec->aes_decrypt(s->aes, buf, got);
    return got;
}

typedef struct sink {
    rar_state_t *st;
    uint32_t crc;
    void *b2;
    uint64_t written, limit;
    rubraview_rar_write_fn write;
    void *ctx;
    bool stopped;
} sink_t;

static bool sink_write(void *ctx, const uint8_t *data, size_t size) {
    sink_t *k = (sink_t*)ctx;
    if (atomic_load(&k->st->cancel)) { k->stopped = true; return false; }
    if (k->limit != RUBRAVIEW_RAR_SIZE_UNKNOWN && size > k->limit - k->written) size = (size_t)(k->limit - k->written);
    k->crc = crc32_update(k->crc, data, size);
    if (k->b2) g_codec->blake2sp_update(k->b2, data, size);
    k->written += size;
    atomic_fetch_add(&k->st->done, size);
    if (k->write && size && !k->write(k->ctx, data, size)) { k->stopped = true; return false; }
    return true;
}

static rubraview_rar_err_t map_unpack(rubraview_rar_unpack_status_t s) {
    switch (s) {
    case RUBRAVIEW_RAR_UNPACK_OK: return RUBRAVIEW_RAR_OK;
    case RUBRAVIEW_RAR_UNPACK_UNSUPPORTED: return RUBRAVIEW_RAR_ERR_UNSUPPORTED;
    case RUBRAVIEW_RAR_UNPACK_TOO_LARGE: return RUBRAVIEW_RAR_ERR_TOO_LARGE;
    case RUBRAVIEW_RAR_UNPACK_NO_MEMORY: return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
    case RUBRAVIEW_RAR_UNPACK_STOPPED: return RUBRAVIEW_RAR_ERR_CANCELLED;
    default: return RUBRAVIEW_RAR_ERR_CORRUPT_STREAM;
    }
}

/* What makes an entry unreadable before any byte of it is touched. */
static rubraview_rar_err_t entry_check(const rubraview_rar_archive_t *a, const rubraview_rar_entry_t *e) {
    const rar_state_t *st = (const rar_state_t*)a->state;
    if (e->split) return RUBRAVIEW_RAR_ERR_MISSING_VOLUME;
    if (e->crypt == RUBRAVIEW_RAR_CRYPT_OLD) return RUBRAVIEW_RAR_ERR_UNSUPPORTED;
    if ((e->method != 0 || e->encrypted) && !g_codec) return RUBRAVIEW_RAR_ERR_NO_CODEC;
    if (e->method != 0 && e->method != 20 && e->method != 26 && e->method != 29 && e->method != 50 && e->method != 70)
        return RUBRAVIEW_RAR_ERR_UNSUPPORTED;   /* RAR 1.5 (spec 2.4) and the unknown */
    if (g_codec && e->method != 0 && e->dict_size > g_codec->max_dict) return RUBRAVIEW_RAR_ERR_TOO_LARGE;
    if (e->encrypted && !st->have_password) return RUBRAVIEW_RAR_ERR_ENCRYPTED;
    return RUBRAVIEW_RAR_OK;
}

/* One entry, its decoder state continued (`solid`) or not, into `write` (NULL: discarded).
   Checks its CRC / BLAKE2sp. */
static rubraview_rar_err_t extract_one(rubraview_rar_archive_t *a, size_t index, bool check,
                                       rubraview_rar_write_fn write, void *ctx) {
    rar_state_t *st = (rar_state_t*)a->state;
    const rubraview_rar_entry_t *e = &a->entries[index];
    rubraview_rar_err_t err = entry_check(a, e);
    if (err != RUBRAVIEW_RAR_OK) return err;

    src_t src = { .a = a, .e = e, .st = st };
    uint8_t hash_key[32];
    bool have_hash_key = false;
    if (e->encrypted) {
        if (e->crypt == RUBRAVIEW_RAR_CRYPT_30) {
            const key30_t *k = keys30(st, e->salt_set ? e->salt : NULL);
            src.aes = g_codec->aes_create(k->key, 128, k->iv);
        } else {
            const key50_t *k = keys50(st, e->salt, e->lg2_count);
            if (!k) return RUBRAVIEW_RAR_ERR_UNSUPPORTED;
            if (e->psw_check_set && memcmp(k->check, e->psw_check, 8) != 0) {
                uint8_t sum[32];
                g_codec->sha256(e->psw_check, 8, sum);
                if (memcmp(sum, e->psw_check_sum, 4) == 0) return RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
            }
            src.aes = g_codec->aes_create(k->key, 256, e->iv);
            const key50_t *m = memcmp(e->mac_salt, e->salt, 16) == 0 && e->mac_lg2 == e->lg2_count
                                   ? k : keys50(st, e->mac_salt, e->mac_lg2);
            if (m) { memcpy(hash_key, m->hash_key, 32); have_hash_key = true; }
        }
        if (!src.aes) return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
    }

    sink_t sink = { .st = st, .limit = e->size, .write = write, .ctx = ctx };
    if (check && e->has_hash && g_codec) sink.b2 = g_codec->blake2sp_create();

    if (e->method == 0) {
        uint8_t buf[65536];
        err = RUBRAVIEW_RAR_OK;
        for (;;) {
            if (e->size != RUBRAVIEW_RAR_SIZE_UNKNOWN && sink.written >= e->size) break;
            size_t n = src_read(&src, buf, sizeof(buf));
            if (atomic_load(&st->cancel)) { err = RUBRAVIEW_RAR_ERR_CANCELLED; break; }
            if (n == 0) {
                if (e->size != RUBRAVIEW_RAR_SIZE_UNKNOWN) err = RUBRAVIEW_RAR_ERR_CORRUPT_STREAM;
                break;
            }
            if (!sink_write(&sink, buf, n)) { err = RUBRAVIEW_RAR_ERR_CANCELLED; break; }
        }
    } else {
        if (!st->unpack) st->unpack = g_codec->unpack_create();
        if (!st->unpack) err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
        else {
            rubraview_rar_unpack_params_t prm = {
                .method = e->method, .solid = e->solid && st->next == index, .drain = a->solid_archive,
                .dict_size = e->dict_size, .dest_size = e->size,
            };
            st->next = SIZE_MAX;
            rubraview_rar_unpack_status_t us = g_codec->unpack_file(st->unpack, &prm, src_read, &src, sink_write, &sink);
            err = map_unpack(us);
            if (err == RUBRAVIEW_RAR_ERR_CANCELLED && !atomic_load(&st->cancel) && !sink.stopped) err = RUBRAVIEW_RAR_ERR_CORRUPT_STREAM;
            if (atomic_load(&st->cancel)) err = RUBRAVIEW_RAR_ERR_CANCELLED;
            if (err == RUBRAVIEW_RAR_OK) {   /* stored files between do not touch the stream */
                size_t nx = index + 1;
                while (nx < a->entry_count && a->entries[nx].method == 0) nx++;
                st->next = nx;
            }
            if (err == RUBRAVIEW_RAR_OK && e->size != RUBRAVIEW_RAR_SIZE_UNKNOWN && sink.written != e->size)
                err = RUBRAVIEW_RAR_ERR_CORRUPT_STREAM;
        }
    }
    if (src.aes) g_codec->aes_destroy(src.aes);

    uint8_t digest[32];
    if (sink.b2) g_codec->blake2sp_final(sink.b2, digest);
    if (err == RUBRAVIEW_RAR_OK && check) {
        bool mac = e->hash_mac && have_hash_key;
        if (e->has_crc) {
            uint32_t want = mac ? g_codec->crc_to_mac(sink.crc, hash_key) : sink.crc;
            if (want != e->crc32) err = RUBRAVIEW_RAR_ERR_CORRUPT_STREAM;
        }
        if (sink.b2) {
            uint8_t m[32];
            if (mac) g_codec->hmac_sha256(hash_key, 32, digest, 32, m);
            if (memcmp(mac ? m : digest, e->hash, 32) != 0) err = RUBRAVIEW_RAR_ERR_CORRUPT_STREAM;
        }
    }
    wipe(hash_key, sizeof(hash_key));
    if (err != RUBRAVIEW_RAR_OK && e->method != 0) st->next = SIZE_MAX;
    return err;
}

rubraview_rar_err_t rubraview_rar_extract(rubraview_rar_archive_t *archive, size_t index,
                                          rubraview_rar_write_fn write, void *ctx) {
    if (!archive || !archive->state || index >= archive->entry_count) return RUBRAVIEW_RAR_ERR_BAD_INDEX;
    rar_state_t *st = (rar_state_t*)archive->state;
    if (atomic_load(&st->cancel)) return RUBRAVIEW_RAR_ERR_CANCELLED;
    rubraview_rar_err_t err = entry_check(archive, &archive->entries[index]);
    if (err != RUBRAVIEW_RAR_OK) return err;
    atomic_store(&st->total, rubraview_rar_read_cost(archive, index));
    atomic_store(&st->done, 0);
    for (size_t i = read_from(archive, index); i < index; ++i) {
        if (archive->entries[i].method == 0) continue;
        err = extract_one(archive, i, false, NULL, NULL);
        if (err != RUBRAVIEW_RAR_OK) return err;
    }
    return extract_one(archive, index, true, write, ctx);
}

typedef struct grow { uint8_t *p; size_t len, cap; uint64_t max; bool too_large; } grow_t;

static bool grow_write(void *ctx, const uint8_t *data, size_t size) {
    grow_t *g = (grow_t*)ctx;
    if (size > g->max - g->len) { g->too_large = true; return false; }
    if (g->len + size > g->cap) {
        size_t cap = g->cap ? g->cap : 65536;
        while (cap < g->len + size) cap *= 2;
        uint8_t *p = (uint8_t*)realloc(g->p, cap);
        if (!p) return false;
        g->p = p;
        g->cap = cap;
    }
    memcpy(g->p + g->len, data, size);
    g->len += size;
    return true;
}

typedef struct fixed { uint8_t *p; size_t len, cap; } fixed_t;

static bool fixed_write(void *ctx, const uint8_t *data, size_t size) {
    fixed_t *f = (fixed_t*)ctx;
    if (size > f->cap - f->len) return false;
    memcpy(f->p + f->len, data, size);
    f->len += size;
    return true;
}

rubraview_rar_data_result_t rubraview_rar_read_entry(proven_arena_t *arena, rubraview_rar_archive_t *archive,
                                                     size_t index, uint64_t max_entry_bytes) {
    rubraview_rar_data_result_t out = { .err = RUBRAVIEW_RAR_ERR_BAD_INDEX, .data = { .ptr = "", .len = 0 } };
    if (!arena || !archive || !archive->state || index >= archive->entry_count) return out;
    const rubraview_rar_entry_t *e = &archive->entries[index];
    if (e->size != RUBRAVIEW_RAR_SIZE_UNKNOWN) {
        if (e->size > max_entry_bytes || e->size >= SIZE_MAX) { out.err = RUBRAVIEW_RAR_ERR_TOO_LARGE; return out; }
        proven_result_mem_mut_t r = proven_arena_alloc(arena, (size_t)e->size + 1);
        if (!proven_is_ok(r.err)) { out.err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; return out; }
        fixed_t f = { r.value.ptr, 0, (size_t)e->size };
        out.err = rubraview_rar_extract(archive, index, fixed_write, &f);
        if (out.err != RUBRAVIEW_RAR_OK) return out;
        r.value.ptr[f.len] = 0;
        out.data = (u8str_t){ .ptr = (const char*)r.value.ptr, .len = f.len };
        return out;
    }
    grow_t g = { .max = max_entry_bytes };
    out.err = rubraview_rar_extract(archive, index, grow_write, &g);
    if (out.err == RUBRAVIEW_RAR_ERR_CANCELLED && g.too_large) out.err = RUBRAVIEW_RAR_ERR_TOO_LARGE;
    if (out.err == RUBRAVIEW_RAR_OK) {
        out.data = arena_copy(arena, g.p ? g.p : (const uint8_t*)"", g.len);
        if (!out.data.ptr) { out.err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; out.data = (u8str_t){ .ptr = "", .len = 0 }; }
    }
    free(g.p);
    return out;
}

/* ------------------------------------------------------------------ */
/* passwords */

rubraview_rar_err_t rubraview_rar_set_password(rubraview_rar_archive_t *archive, u8str_t password) {
    if (!archive || !archive->state) return RUBRAVIEW_RAR_ERR_BAD_INDEX;
    rar_state_t *st = (rar_state_t*)archive->state;
    if (!g_codec) return RUBRAVIEW_RAR_ERR_NO_CODEC;
    state_set_password(st, password);
    /* RAR 5: the stored check values (spec 6.2.2). */
    bool any_check = false;
    for (size_t i = 0; i < archive->entry_count; ++i) {
        const rubraview_rar_entry_t *e = &archive->entries[i];
        if (!e->encrypted || e->crypt != RUBRAVIEW_RAR_CRYPT_50 || !e->psw_check_set) continue;
        uint8_t sum[32];
        g_codec->sha256(e->psw_check, 8, sum);
        if (memcmp(sum, e->psw_check_sum, 4) != 0) continue;   /* damaged: says nothing */
        any_check = true;
        const key50_t *k = keys50(st, e->salt, e->lg2_count);
        if (k && memcmp(k->check, e->psw_check, 8) == 0) return RUBRAVIEW_RAR_OK;
    }
    if (any_check) { state_clear_password(st); return RUBRAVIEW_RAR_ERR_BAD_PASSWORD; }
    /* RAR 4: decode the smallest encrypted file that starts a stream. */
    size_t best = SIZE_MAX;
    for (size_t i = 0; i < archive->entry_count; ++i) {
        const rubraview_rar_entry_t *e = &archive->entries[i];
        if (!e->encrypted || e->crypt == RUBRAVIEW_RAR_CRYPT_OLD || e->split) continue;
        if (e->method != 0 && chain_start(archive, i) != i) continue;
        if (best == SIZE_MAX || e->packed_size < archive->entries[best].packed_size) best = i;
    }
    if (best == SIZE_MAX) return RUBRAVIEW_RAR_OK;
    atomic_store(&st->total, archive->entries[best].size);
    atomic_store(&st->done, 0);
    rubraview_rar_err_t err = extract_one(archive, best, true, NULL, NULL);
    if (err == RUBRAVIEW_RAR_OK) return RUBRAVIEW_RAR_OK;
    if (err == RUBRAVIEW_RAR_ERR_CORRUPT_STREAM || err == RUBRAVIEW_RAR_ERR_BAD_PASSWORD) {
        state_clear_password(st);
        return RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
    }
    if (err != RUBRAVIEW_RAR_ERR_CANCELLED) state_clear_password(st);
    return err;
}

/* ------------------------------------------------------------------ */
/* volumes, by what the start of an archive says and by name (spec 4.1) */

bool rubraview_rar_volume_info(const uint8_t *data, size_t size, rubraview_rar_volume_info_t *out) {
    if (!out) return false;
    *out = (rubraview_rar_volume_info_t){0};
    size_t at;
    int fmt = data ? find_signature(data, size, &at) : 0;
    if (fmt == 4) {
        size_t pos = at + 7;
        while (pos <= size && size - pos >= 7) {
            const uint8_t *h = data + pos;
            size_t hs = rd16(h + 5);
            if (hs < 7 || hs > size - pos) return false;
            if (h[2] == 0x73) {
                uint16_t f = rd16(h + 3);
                out->is_volume = f & 0x0001;
                out->new_numbering = f & 0x0010;
                out->first = (f & 0x0100) || !(f & 0x0001);
                out->headers_encrypted = f & 0x0080;
                return true;
            }
            pos += hs;
            if (h[2] != 0x72 && (rd16(h + 3) & 0x8000) && hs >= 11) pos += rd32(h + 7);
        }
        return false;
    }
    if (fmt == 5) {
        size_t pos = at + 8;
        if (size - pos < 5) return false;
        rd5_t r = { data + pos + 4, size - pos - 4, 0, false };
        uint64_t hs = vint(&r);
        if (r.bad || hs > r.n - r.at) return false;
        rd5_t b = { r.p + r.at, (size_t)hs, 0, false };
        uint64_t type = vint(&b), hflags = vint(&b);
        if (hflags & 1) (void)vint(&b);
        if (hflags & 2) (void)vint(&b);
        out->new_numbering = true;
        if (type == 4) { out->headers_encrypted = true; out->first = true; return true; }
        if (type != 1 || b.bad) return false;
        uint64_t af = vint(&b);
        out->is_volume = af & 1;
        out->first = !(af & 2);
        return !b.bad;
    }
    return false;
}

static size_t ext_dot(u8str_t name) {
    for (size_t i = name.len; i-- > 0;) {
        if (name.ptr[i] == '.') return i;
        if (name.ptr[i] == '/' || name.ptr[i] == '\\') break;
    }
    return SIZE_MAX;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* `.partN` right before the extension: where its digits are. */
static bool part_digits(u8str_t name, size_t dot, size_t *d0, size_t *d1) {
    size_t e = dot, s = dot;
    while (s > 0 && is_digit(name.ptr[s - 1])) s--;
    if (s == e || s < 5) return false;
    u8str_t tag = { name.ptr + s - 5, 5 };
    if (!rubraview_u8_eq_lit_ci(tag, ".part")) return false;
    *d0 = s; *d1 = e;
    return true;
}

static bool old_ext(u8str_t name, size_t dot) {
    if (dot == SIZE_MAX || name.len - dot != 4) return false;
    char c = rubraview_ascii_lower(name.ptr[dot + 1]);
    return c >= 'r' && c <= 'z' && is_digit(name.ptr[dot + 2]) && is_digit(name.ptr[dot + 3]);
}

rubraview_rar_volume_name_kind_t rubraview_rar_volume_name_kind(u8str_t name) {
    size_t dot = ext_dot(name), d0, d1;
    if (old_ext(name, dot)) return RUBRAVIEW_RAR_NAME_OLD;
    if (dot != SIZE_MAX && part_digits(name, dot, &d0, &d1)) return RUBRAVIEW_RAR_NAME_PART;
    return RUBRAVIEW_RAR_NAME_PLAIN;
}

unsigned rubraview_rar_volume_number(u8str_t name) {
    size_t dot = ext_dot(name), d0, d1;
    if (old_ext(name, dot)) {
        unsigned letter = (unsigned)(rubraview_ascii_lower(name.ptr[dot + 1]) - 'r');
        return letter * 100 + (unsigned)(name.ptr[dot + 2] - '0') * 10 + (unsigned)(name.ptr[dot + 3] - '0') + 2;
    }
    if (dot != SIZE_MAX && part_digits(name, dot, &d0, &d1)) {
        unsigned n = 0;
        for (size_t i = d0; i < d1 && n < 100000; ++i) n = n * 10 + (unsigned)(name.ptr[i] - '0');
        return n;
    }
    if (dot != SIZE_MAX) {
        u8str_t ext = { name.ptr + dot, name.len - dot };
        if (rubraview_u8_eq_lit_ci(ext, ".rar") || rubraview_u8_eq_lit_ci(ext, ".cbr")) return 1;
    }
    return 0;
}

static u8str_t splice(proven_arena_t *arena, u8str_t path, size_t from, size_t to, const char *mid, size_t mid_len) {
    size_t n = from + mid_len + (path.len - to);
    proven_result_mem_mut_t r = proven_arena_alloc(arena, n + 1);
    if (!proven_is_ok(r.err)) return (u8str_t){ .ptr = "", .len = 0 };
    memcpy(r.value.ptr, path.ptr, from);
    memcpy(r.value.ptr + from, mid, mid_len);
    memcpy(r.value.ptr + from + mid_len, path.ptr + to, path.len - to);
    r.value.ptr[n] = 0;
    return (u8str_t){ .ptr = (const char*)r.value.ptr, .len = n };
}

u8str_t rubraview_rar_first_volume(proven_arena_t *arena, u8str_t path, bool old) {
    if (!arena) return (u8str_t){ .ptr = "", .len = 0 };
    size_t dot = ext_dot(path), d0, d1;
    if (old_ext(path, dot)) {
        bool upper = path.ptr[dot + 1] >= 'A' && path.ptr[dot + 1] <= 'Z';
        return splice(arena, path, dot + 1, path.len, upper ? "RAR" : "rar", 3);
    }
    if (!old && dot != SIZE_MAX && part_digits(path, dot, &d0, &d1)) {
        char digits[32];
        size_t w = d1 - d0 < sizeof(digits) ? d1 - d0 : sizeof(digits) - 1;
        memset(digits, '0', w);
        digits[w - 1] = '1';
        return splice(arena, path, d0, d1, digits, w);
    }
    return splice(arena, path, 0, 0, "", 0);
}

u8str_t rubraview_rar_next_volume(proven_arena_t *arena, u8str_t path, bool old) {
    if (!arena) return (u8str_t){ .ptr = "", .len = 0 };
    size_t dot = ext_dot(path);
    if (old) {
        if (old_ext(path, dot)) {
            char ext[3] = { path.ptr[dot + 1], path.ptr[dot + 2], path.ptr[dot + 3] };
            unsigned n = (unsigned)(ext[1] - '0') * 10 + (unsigned)(ext[2] - '0') + 1;
            if (n == 100) { n = 0; ext[0]++; }
            ext[1] = (char)('0' + n / 10);
            ext[2] = (char)('0' + n % 10);
            return splice(arena, path, dot + 1, path.len, ext, 3);
        }
        bool upper = dot != SIZE_MAX && dot + 1 < path.len && path.ptr[dot + 1] >= 'A' && path.ptr[dot + 1] <= 'Z';
        if (dot == SIZE_MAX) return splice(arena, path, path.len, path.len, upper ? ".R00" : ".r00", 4);
        return splice(arena, path, dot + 1, path.len, upper ? "R00" : "r00", 3);
    }
    /* New naming: the last run of digits before the extension, its width kept; in `x.part3of5.rar`
       the first of the last two numbers (spec 4.1). */
    size_t end = dot == SIZE_MAX ? path.len : dot;
    size_t e = end;
    while (e > 0 && !is_digit(path.ptr[e - 1])) e--;
    size_t s = e;
    while (s > 0 && is_digit(path.ptr[s - 1])) s--;
    if (s == e) return (u8str_t){ .ptr = "", .len = 0 };
    if (s >= 2 && rubraview_ascii_lower(path.ptr[s - 2]) == 'o' && rubraview_ascii_lower(path.ptr[s - 1]) == 'f') {
        size_t e2 = s - 2, s2 = e2;
        while (s2 > 0 && is_digit(path.ptr[s2 - 1])) s2--;
        if (s2 < e2) { s = s2; e = e2; }
    }
    char digits[34];
    size_t w = e - s;
    if (w > 32) return (u8str_t){ .ptr = "", .len = 0 };
    memcpy(digits + 1, path.ptr + s, w);
    digits[0] = '0';
    size_t i = w;
    for (;;) {
        if (digits[i] == '9') { digits[i] = '0'; i--; continue; }
        digits[i]++;
        break;
    }
    if (digits[0] != '0') return splice(arena, path, s, e, digits, w + 1);
    return splice(arena, path, s, e, digits + 1, w);
}
