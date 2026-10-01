#include "rubraview/rar.h"
#include "rubraview/rar_codec.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/*
 * The headers, read for this project from the format as UnRAR's
 * arcread.cpp / headers.hpp / headers5.hpp and libarchive's
 * archive_read_support_format_rar*.c describe it (both read side by side,
 * 2026-09-30). Every length is checked against the mapped volume.
 *
 * Encrypted headers and volumes (2026-10-01), as UnRAR's arcread.cpp,
 * volume.cpp and rdwrfn.cpp handle them: RAR 4 puts an 8-byte salt before
 * each encrypted header, RAR 5 a 16-byte IV, and each header is padded to
 * the cipher's 16 bytes; a file's packed bytes are one CBC stream even when
 * they run on across volumes.
 */

static const uint8_t SIG4[7] = { 'R', 'a', 'r', '!', 0x1A, 0x07, 0x00 };
static const uint8_t SIG5[8] = { 'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00 };
#define SFX_SEARCH (1u << 20)

/* The format's own sizes (RAR's headers say so; nothing of UnRAR's). */
#define RAR_SALT30          8
#define RAR_SALT50          16
#define RAR_INITV           16
#define RAR_PSWCHECK        8
#define RAR_PSWCHECK_CSUM   4
#define RAR_KDF50_LG2_MAX   24
#define RAR_MAX_PASSWORD    127

/* Decompression and decryption are the UnRAR-licensed module's
   (rar_codec.h, owner 2026-10-01); NULL until one is given. */
static const rubraview_rar_codec_t *g_codec;

void rubraview_rar_set_codec(const rubraview_rar_codec_t *codec) {
    g_codec = codec && codec->version == RUBRAVIEW_RAR_CODEC_VERSION ? codec : NULL;
}

const rubraview_rar_codec_t *rubraview_rar_codec(void) { return g_codec; }

static void rar_wipe(void *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t*)p;
    while (n--) *v++ = 0;
}
#define MAX_VOLUMES 4096u
#define KEY_CACHE 8

/* A key made from the password once and kept: PBKDF2 at RAR 5's 2^15
   rounds, or RAR 3's 2^18 SHA-1 rounds, would be felt on every page. */
typedef struct key_entry {
    bool     used;
    rubraview_rar_crypt_t crypt;
    bool     salt_set;
    uint8_t  lg2;
    uint8_t  salt[16];
    uint8_t  key[32], iv[16], hash_key[32], psw_check[8];
} key_entry_t;

typedef struct rar_state {
    void    *unpack;        /* the codec's */
    size_t next;            /* the entry a solid stream continues with; SIZE_MAX when none */
    _Atomic bool cancel;
    _Atomic uint64_t done, total;
    /* the password, as RAR 5 hashes it (UTF-8) and as RAR 3 does (UTF-16LE) */
    uint8_t  pwd8[RAR_MAX_PASSWORD * 4];
    size_t   pwd8_len;
    uint8_t  pwd16[RAR_MAX_PASSWORD * 2];
    size_t   pwd16_len;
    bool     has_password, password_ok;
    key_entry_t keys[KEY_CACHE];
    unsigned key_next;
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

/* ---- the password and its keys ---- */

/* UTF-8 in, RAR's two forms out: at most 127 UTF-16 units, as RAR cuts it,
   and those same characters again as UTF-8. */
static void password_set(rar_state_t *st, u8str_t password) {
    rar_wipe(st->pwd8, sizeof(st->pwd8));
    rar_wipe(st->pwd16, sizeof(st->pwd16));
    st->pwd8_len = st->pwd16_len = 0;
    st->has_password = password.len > 0;
    st->password_ok = false;
    for (unsigned i = 0; i < KEY_CACHE; ++i) rar_wipe(&st->keys[i], sizeof(st->keys[i]));
    const uint8_t *p = (const uint8_t*)password.ptr;
    size_t i = 0, units = 0;
    while (i < password.len) {
        uint32_t cp = p[i];
        size_t n = 1;
        if (cp >= 0xF0 && i + 3 < password.len) { cp = (cp & 7) << 18 | (p[i + 1] & 0x3Fu) << 12 | (p[i + 2] & 0x3Fu) << 6 | (p[i + 3] & 0x3Fu); n = 4; }
        else if (cp >= 0xE0 && i + 2 < password.len) { cp = (cp & 15) << 12 | (p[i + 1] & 0x3Fu) << 6 | (p[i + 2] & 0x3Fu); n = 3; }
        else if (cp >= 0xC0 && i + 1 < password.len) { cp = (cp & 31) << 6 | (p[i + 1] & 0x3Fu); n = 2; }
        size_t need = cp >= 0x10000 ? 2 : 1;
        if (units + need > RAR_MAX_PASSWORD) break;
        if (cp >= 0x10000) {
            uint32_t v = cp - 0x10000, hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
            st->pwd16[st->pwd16_len++] = (uint8_t)hi; st->pwd16[st->pwd16_len++] = (uint8_t)(hi >> 8);
            st->pwd16[st->pwd16_len++] = (uint8_t)lo; st->pwd16[st->pwd16_len++] = (uint8_t)(lo >> 8);
        } else {
            st->pwd16[st->pwd16_len++] = (uint8_t)cp; st->pwd16[st->pwd16_len++] = (uint8_t)(cp >> 8);
        }
        memcpy(st->pwd8 + st->pwd8_len, p + i, n);
        st->pwd8_len += n;
        units += need;
        i += n;
    }
}

/* The key for a salt, from the cache or made now. NULL when RAR 5's rounds
   are more than it allows. */
static const key_entry_t *key_for(rar_state_t *st, rubraview_rar_crypt_t crypt, const uint8_t *salt, bool salt_set, uint8_t lg2) {
    size_t salt_len = crypt == RUBRAVIEW_RAR_CRYPT_50 ? RAR_SALT50 : RAR_SALT30;
    for (unsigned i = 0; i < KEY_CACHE; ++i) {
        const key_entry_t *k = &st->keys[i];
        if (k->used && k->crypt == crypt && k->salt_set == salt_set && k->lg2 == lg2 &&
            (!salt_set || memcmp(k->salt, salt, salt_len) == 0)) return k;
    }
    if (!g_codec) return NULL;
    key_entry_t *k = &st->keys[st->key_next++ % KEY_CACHE];
    rar_wipe(k, sizeof(*k));
    k->crypt = crypt;
    k->salt_set = salt_set;
    k->lg2 = lg2;
    if (salt_set) memcpy(k->salt, salt, salt_len);
    if (crypt == RUBRAVIEW_RAR_CRYPT_50) {
        if (lg2 > RAR_KDF50_LG2_MAX || !g_codec->kdf50(st->pwd8, st->pwd8_len, salt, lg2, k->key, k->hash_key, k->psw_check)) return NULL;
    } else {
        g_codec->kdf30(st->pwd16, st->pwd16_len, salt_set ? salt : NULL, k->key, k->iv);
    }
    k->used = true;
    return k;
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
    rubraview_rar_piece_t *pieces;
    size_t piece_count, piece_cap;
    bool open_split;        /* the last entry's last piece said more follows */
} list_t;

static bool grow(proven_arena_t *arena, void **items, size_t *cap, size_t count, size_t size) {
    if (count < *cap) return true;
    size_t n = *cap ? *cap * 2 : 64;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, n * size);
    if (!proven_is_ok(res.err)) return false;
    if (count) memcpy(res.value.ptr, *items, count * size);
    *items = res.value.ptr;
    *cap = n;
    return true;
}

static bool list_push(list_t *l, const rubraview_rar_entry_t *e, rubraview_rar_piece_t piece) {
    if (!grow(l->arena, (void**)&l->items, &l->cap, l->count, sizeof(rubraview_rar_entry_t))) return false;
    if (!grow(l->arena, (void**)&l->pieces, &l->piece_cap, l->piece_count, sizeof(rubraview_rar_piece_t))) return false;
    l->items[l->count] = *e;
    l->items[l->count].first_piece = (uint32_t)l->piece_count;
    l->items[l->count].piece_count = 1;
    l->count++;
    l->pieces[l->piece_count++] = piece;
    return true;
}

/* A file that runs on from the previous volume: its piece goes to the entry
   it continues, and the last piece's CRC is the whole file's. */
static bool list_continue(list_t *l, u8str_t name, rubraview_rar_piece_t piece, uint32_t crc, bool more) {
    if (l->count == 0 || !l->open_split) return true;           /* its start is in a volume not given: left out */
    rubraview_rar_entry_t *e = &l->items[l->count - 1];
    if (e->name.len != name.len || memcmp(e->name.ptr, name.ptr, name.len) != 0) return true;
    if (e->first_piece + e->piece_count != l->piece_count) return true;
    if (!grow(l->arena, (void**)&l->pieces, &l->piece_cap, l->piece_count, sizeof(rubraview_rar_piece_t))) return false;
    l->pieces[l->piece_count++] = piece;
    e->piece_count++;
    e->packed_size += piece.size;
    if (!more) { e->crc32 = crc; e->split = false; }
    l->open_split = more;
    return true;
}

/* ---- parsing ---- */

typedef struct parse {
    proven_arena_t *arena;
    rubraview_rar_archive_t *a;
    rar_state_t *st;
    list_t *l;
    uint32_t vol;
    const uint8_t *d;       /* this volume */
    size_t size;
    bool more_volumes;      /* its end header said another follows */
    /* encrypted headers */
    bool hdr_enc;
    bool hdr_checked;       /* a header has decrypted with a good CRC: the password is right */
    rubraview_rar_crypt_t hdr_crypt;
    uint8_t hdr_salt[16], hdr_lg2;
} parse_t;

/* One encrypted header, decrypted into the arena: `pre` bytes of salt (RAR
   4) or IV (RAR 5) at `pos`, then the header padded to 16. `size_of` reads
   the header's own length from its first 16 bytes. NULL when it does not
   fit; *err says why when it is the password. */
typedef size_t (*size_of_fn)(const uint8_t *first16);

static const uint8_t *decrypt_header(parse_t *p, size_t pos, size_t pre, size_of_fn size_of, size_t *out_total,
                                     size_t *out_head, rubraview_rar_err_t *err) {
    *err = RUBRAVIEW_RAR_OK;
    if (pos + pre + 16 > p->size) return NULL;
    const key_entry_t *k = p->hdr_crypt == RUBRAVIEW_RAR_CRYPT_50
        ? key_for(p->st, RUBRAVIEW_RAR_CRYPT_50, p->hdr_salt, true, p->hdr_lg2)
        : key_for(p->st, RUBRAVIEW_RAR_CRYPT_30, p->d + pos, true, 0);
    if (!k) { *err = g_codec ? RUBRAVIEW_RAR_ERR_UNSUPPORTED : RUBRAVIEW_RAR_ERR_NO_CODEC; return NULL; }
    const uint8_t *iv = p->hdr_crypt == RUBRAVIEW_RAR_CRYPT_50 ? p->d + pos : k->iv;
    unsigned bits = p->hdr_crypt == RUBRAVIEW_RAR_CRYPT_50 ? 256 : 128;
    uint8_t first[16];
    memcpy(first, p->d + pos + pre, 16);
    void *aes = g_codec->aes_create(k->key, bits, iv);
    if (!aes) { *err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; return NULL; }
    g_codec->aes_decrypt(aes, first, 16);
    g_codec->aes_destroy(aes);
    size_t head = size_of(first);
    size_t full = (head + 15) & ~(size_t)15;
    if (head == 0 || full > p->size - pos - pre) {
        *err = p->hdr_checked ? RUBRAVIEW_RAR_ERR_CORRUPT : RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
        return NULL;
    }
    proven_result_mem_mut_t res = proven_arena_alloc(p->arena, full);
    if (!proven_is_ok(res.err)) { *err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; return NULL; }
    uint8_t *buf = (uint8_t*)res.value.ptr;
    memcpy(buf, p->d + pos + pre, full);
    aes = g_codec->aes_create(k->key, bits, iv);
    if (!aes) { *err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; return NULL; }
    g_codec->aes_decrypt(aes, buf, full);
    g_codec->aes_destroy(aes);
    *out_total = pre + full;
    *out_head = head;
    return buf;
}

static size_t rar4_size_of(const uint8_t *h) { return le16(h + 5) >= 7 ? le16(h + 5) : 0; }

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

static size_t rar5_size_of(const uint8_t *h) {
    size_t at = 4;
    uint64_t size = 0;
    if (!vint(h, 7, &at, &size) || size == 0 || size > (1u << 21)) return 0;   /* 2 MB: RAR 5's largest header */
    return at + (size_t)size;
}

static rubraview_rar_err_t read_rar4(parse_t *p, size_t pos) {
    rubraview_rar_archive_t *a = p->a;
    list_t *l = p->l;
    size_t main_at = pos;
    while (pos + 7 <= p->size) {
        const uint8_t *h = p->d + pos;
        size_t head_total = 0, head_size = 0;
        if (p->hdr_enc && pos > main_at) {
            if (!g_codec) return RUBRAVIEW_RAR_ERR_NO_CODEC;
            if (!p->st->has_password) return RUBRAVIEW_RAR_ERR_ENCRYPTED;
            rubraview_rar_err_t err;
            h = decrypt_header(p, pos, RAR_SALT30, rar4_size_of, &head_total, &head_size, &err);
            if (!h) return err != RUBRAVIEW_RAR_OK ? err : (l->count ? RUBRAVIEW_RAR_OK : RUBRAVIEW_RAR_ERR_CORRUPT);
            /* The header's CRC is the password's test. */
            if (((crc_update(0xFFFFFFFFu, h + 2, head_size - 2) ^ 0xFFFFFFFFu) & 0xFFFF) != le16(h))
                return p->hdr_checked ? RUBRAVIEW_RAR_ERR_CORRUPT : RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
            p->hdr_checked = true;
        } else {
            head_size = le16(h + 5);
            head_total = head_size;
            if (head_size < 7 || pos + head_size > p->size) return l->count ? RUBRAVIEW_RAR_OK : RUBRAVIEW_RAR_ERR_CORRUPT;
        }
        uint8_t type = h[2];
        uint16_t flags = le16(h + 3);
        uint64_t data_at = pos + head_total, next = data_at;
        if (type == 0x73) {                                  /* main */
            a->is_volume = (flags & 0x0001) != 0;
            a->new_numbering = (flags & 0x0010) != 0;
            a->solid_archive = (flags & 0x0008) != 0;
            if (flags & 0x0080) { a->headers_encrypted = true; p->hdr_enc = true; p->hdr_crypt = RUBRAVIEW_RAR_CRYPT_30; }
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
            if (pack > p->size || data_at + pack > p->size) {
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
                    e.name = decode_rar4_unicode(p->arena, name, plain, name + plain + 1, name_size - plain - 1);
                } else {
                    e.name = keep(p->arena, name, plain);
                    /* plain ASCII is what it is; anything else is a code page's */
                    for (size_t i = 0; i < plain; ++i) if (name[i] >= 0x80) { e.name_is_legacy = true; break; }
                }
                rubraview_rar_piece_t piece = { .volume = p->vol, .offset = data_at, .size = pack };
                bool more = (flags & 0x0002) != 0;
                if (flags & 0x0001) {                        /* runs on from the volume before */
                    if (!list_continue(l, e.name, piece, crc, more)) return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
                } else {
                    e.size = unp;
                    e.packed_size = pack;
                    e.data_offset = data_at;
                    e.crc32 = crc;
                    e.has_crc = true;
                    e.method = method == 0x30 ? 0 : unp_ver;
                    e.dict_size = (uint64_t)0x10000 << ((flags & 0x00e0) >> 5);
                    e.solid = (flags & 0x0010) != 0;
                    e.encrypted = (flags & 0x0004) != 0;
                    e.split = more;
                    if (e.encrypted) {
                        e.crypt = unp_ver >= 29 ? RUBRAVIEW_RAR_CRYPT_30 : RUBRAVIEW_RAR_CRYPT_OLD;
                        if ((flags & 0x0400) && name_at + name_size + RAR_SALT30 <= head_size) {
                            e.salt_set = true;
                            memcpy(e.salt, h + name_at + name_size, RAR_SALT30);
                        }
                    }
                    if (!list_push(l, &e, piece)) return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
                    l->open_split = more;
                }
            }
            next += pack;
        } else if (type == 0x7b) {                           /* end of archive */
            p->more_volumes = (flags & 0x0001) != 0;
            break;
        } else if (flags & 0x8000) {                         /* a long block: its data follows */
            if (head_size < 11) return RUBRAVIEW_RAR_ERR_CORRUPT;
            next += le32(h + 7);
        }
        if (next <= pos || next > p->size) break;
        pos = (size_t)next;
    }
    return RUBRAVIEW_RAR_OK;
}

static rubraview_rar_err_t read_rar5(parse_t *p, size_t pos) {
    rubraview_rar_archive_t *a = p->a;
    list_t *l = p->l;
    while (pos + 7 <= p->size) {
        const uint8_t *h = p->d + pos;
        size_t head_total = 0, head_size = 0;
        if (p->hdr_enc) {
            rubraview_rar_err_t err;
            h = decrypt_header(p, pos, RAR_INITV, rar5_size_of, &head_total, &head_size, &err);
            if (!h) return err != RUBRAVIEW_RAR_OK ? err : (l->count ? RUBRAVIEW_RAR_OK : RUBRAVIEW_RAR_ERR_CORRUPT);
            if ((crc_update(0xFFFFFFFFu, h + 4, head_size - 4) ^ 0xFFFFFFFFu) != le32(h))
                return p->hdr_checked ? RUBRAVIEW_RAR_ERR_CORRUPT : RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
            p->hdr_checked = true;
        } else {
            size_t at = pos + 4;
            uint64_t header_size = 0;
            if (!vint(p->d, p->size, &at, &header_size) || header_size == 0 || header_size > p->size - at)
                return l->count ? RUBRAVIEW_RAR_OK : RUBRAVIEW_RAR_ERR_CORRUPT;
            head_size = at - pos + (size_t)header_size;
            head_total = head_size;
        }
        size_t at = 4, end = head_size;
        uint64_t header_size = 0;
        if (!vint(h, end, &at, &header_size)) return RUBRAVIEW_RAR_ERR_CORRUPT;
        uint64_t type = 0, hflags = 0, extra = 0, data_size = 0;
        if (!vint(h, end, &at, &type) || !vint(h, end, &at, &hflags)) return RUBRAVIEW_RAR_ERR_CORRUPT;
        if ((hflags & 0x0001) && !vint(h, end, &at, &extra)) return RUBRAVIEW_RAR_ERR_CORRUPT;
        if ((hflags & 0x0002) && !vint(h, end, &at, &data_size)) return RUBRAVIEW_RAR_ERR_CORRUPT;
        if (extra > header_size) return RUBRAVIEW_RAR_ERR_CORRUPT;
        uint64_t data_at = (uint64_t)pos + head_total;
        if (data_size > p->size - data_at) return l->count ? RUBRAVIEW_RAR_OK : RUBRAVIEW_RAR_ERR_CORRUPT;
        if (type == 4) {                                     /* the archive's encryption: the headers after it */
            uint64_t version = 0, eflags = 0;
            if (!vint(h, end, &at, &version) || !vint(h, end, &at, &eflags) || at + 1 + RAR_SALT50 > end)
                return RUBRAVIEW_RAR_ERR_CORRUPT;
            if (version != 0) return RUBRAVIEW_RAR_ERR_UNSUPPORTED;
            a->headers_encrypted = true;
            p->hdr_enc = true;
            p->hdr_crypt = RUBRAVIEW_RAR_CRYPT_50;
            p->hdr_lg2 = h[at++];
            memcpy(p->hdr_salt, h + at, RAR_SALT50);
            at += RAR_SALT50;
            if (!g_codec) return RUBRAVIEW_RAR_ERR_NO_CODEC;
            if (!p->st->has_password) return RUBRAVIEW_RAR_ERR_ENCRYPTED;
            if (p->hdr_lg2 > RAR_KDF50_LG2_MAX) return RUBRAVIEW_RAR_ERR_UNSUPPORTED;
            const key_entry_t *k = key_for(p->st, RUBRAVIEW_RAR_CRYPT_50, p->hdr_salt, true, p->hdr_lg2);
            if (!k) return RUBRAVIEW_RAR_ERR_UNSUPPORTED;
            if ((eflags & 0x0001) && at + RAR_PSWCHECK + RAR_PSWCHECK_CSUM <= end) {
                uint8_t digest[32];
                g_codec->sha256(h + at, RAR_PSWCHECK, digest);
                bool check_valid = memcmp(digest, h + at + RAR_PSWCHECK, RAR_PSWCHECK_CSUM) == 0;
                if (check_valid && memcmp(k->psw_check, h + at, RAR_PSWCHECK) != 0) return RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
            }
        } else if (type == 1) {
            uint64_t arc_flags = 0;
            if (!vint(h, end, &at, &arc_flags)) return RUBRAVIEW_RAR_ERR_CORRUPT;
            a->is_volume = (arc_flags & 0x0001) != 0;
            a->new_numbering = true;
            a->solid_archive = (arc_flags & 0x0004) != 0;
        } else if (type == 2) {
            uint64_t file_flags = 0, unp = 0, attrs = 0, comp = 0, host = 0, name_len = 0;
            if (!vint(h, end, &at, &file_flags) || !vint(h, end, &at, &unp) || !vint(h, end, &at, &attrs))
                return RUBRAVIEW_RAR_ERR_CORRUPT;
            if (file_flags & 0x0002) at += 4;                /* mtime */
            uint32_t crc = 0;
            if (file_flags & 0x0004) {
                if (at + 4 > end) return RUBRAVIEW_RAR_ERR_CORRUPT;
                crc = le32(h + at);
                at += 4;
            }
            if (!vint(h, end, &at, &comp) || !vint(h, end, &at, &host) || !vint(h, end, &at, &name_len) ||
                name_len > end - at)
                return RUBRAVIEW_RAR_ERR_CORRUPT;
            const uint8_t *name = h + at;
            at += (size_t)name_len;
            rubraview_rar_entry_t e = {0};
            size_t extra_end = end, extra_at = end - (size_t)extra;
            while (extra && extra_at < extra_end) {          /* the file's extra records: only encryption matters here */
                uint64_t rec_size = 0, rec_type = 0;
                size_t rec = extra_at;
                if (!vint(h, extra_end, &rec, &rec_size) || rec_size == 0 || rec_size > extra_end - rec) break;
                size_t rec_end = rec + (size_t)rec_size;
                if (vint(h, rec_end, &rec, &rec_type) && rec_type == 0x01) {
                    e.encrypted = true;
                    uint64_t version = 0, cflags = 0;
                    if (vint(h, rec_end, &rec, &version) && vint(h, rec_end, &rec, &cflags) && version == 0 &&
                        rec + 1 + RAR_SALT50 + RAR_INITV <= rec_end) {
                        e.crypt = RUBRAVIEW_RAR_CRYPT_50;
                        e.lg2_count = h[rec++];
                        memcpy(e.salt, h + rec, RAR_SALT50); rec += RAR_SALT50;
                        memcpy(e.iv, h + rec, RAR_INITV); rec += RAR_INITV;
                        e.salt_set = true;
                        e.hash_mac = (cflags & 0x0002) != 0;
                        if (g_codec && (cflags & 0x0001) && rec + RAR_PSWCHECK + RAR_PSWCHECK_CSUM <= rec_end) {
                            uint8_t digest[32];
                            g_codec->sha256(h + rec, RAR_PSWCHECK, digest);
                            if (memcmp(digest, h + rec + RAR_PSWCHECK, RAR_PSWCHECK_CSUM) == 0) {
                                e.psw_check_set = true;
                                memcpy(e.psw_check, h + rec, RAR_PSWCHECK);
                            }
                        }
                    } else {
                        e.crypt = RUBRAVIEW_RAR_CRYPT_OLD;   /* a version this reader does not know */
                    }
                }
                extra_at = rec_end;
            }
            if (!(file_flags & 0x0001)) {                    /* not a folder */
                e.name = keep(p->arena, name, (size_t)name_len);
                rubraview_rar_piece_t piece = { .volume = p->vol, .offset = data_at, .size = data_size };
                bool more = (hflags & 0x0010) != 0;
                if (hflags & 0x0008) {                       /* runs on from the volume before */
                    if (!list_continue(l, e.name, piece, crc, more)) return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
                } else {
                    e.size = unp;
                    e.packed_size = data_size;
                    e.data_offset = data_at;
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
                    e.split = more;
                    if (!list_push(l, &e, piece)) return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
                    l->open_split = more;
                }
            }
        } else if (type == 5) {
            uint64_t end_flags = 0;
            if (vint(h, end, &at, &end_flags)) p->more_volumes = (end_flags & 0x0001) != 0;
            break;
        }
        uint64_t next = data_at + data_size;
        if (next <= pos || next > p->size) break;
        pos = (size_t)next;
    }
    return RUBRAVIEW_RAR_OK;
}

static rar_state_t *state_new(void) {
    rar_state_t *st = (rar_state_t*)calloc(1, sizeof(rar_state_t));
    if (st) st->next = SIZE_MAX;
    return st;
}

static void state_free(rar_state_t *st) {
    if (!st) return;
    if (st->unpack && g_codec) g_codec->unpack_destroy(st->unpack);
    rar_wipe(st, sizeof(*st));      /* the password and the keys with it */
    free(st);
}

rubraview_rar_result_t rubraview_rar_open_volumes(proven_arena_t *arena, const rubraview_rar_volume_t *volumes,
                                                  size_t volume_count, u8str_t password) {
    rubraview_rar_result_t r = { .err = RUBRAVIEW_RAR_ERR_NOT_A_RAR };
    if (!arena || !volumes || volume_count == 0 || !volumes[0].data) return r;
    if (volume_count > MAX_VOLUMES) volume_count = MAX_VOLUMES;
    bool rar5 = false;
    long sig = find_signature(volumes[0].data, volumes[0].size, &rar5);
    if (sig < 0) return r;
    crc_init();
    proven_result_mem_mut_t vres = proven_arena_alloc(arena, volume_count * sizeof(rubraview_rar_volume_t));
    if (!proven_is_ok(vres.err)) { r.err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; return r; }
    rubraview_rar_volume_t *vols = (rubraview_rar_volume_t*)(void*)vres.value.ptr;
    memcpy(vols, volumes, volume_count * sizeof(rubraview_rar_volume_t));
    rar_state_t *st = state_new();
    if (!st) { r.err = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; return r; }
    password_set(st, password);

    rubraview_rar_archive_t a = { .data = vols[0].data, .size = vols[0].size, .rar5 = rar5,
                                  .volumes = vols, .volume_count = volume_count };
    list_t l = { .arena = arena };
    size_t used = 0;
    for (size_t v = 0; v < volume_count; ++v) {
        bool v5 = false;
        long vs = v == 0 ? sig : (vols[v].data ? find_signature(vols[v].data, vols[v].size, &v5) : -1);
        if (vs < 0 || (v > 0 && v5 != rar5)) break;
        parse_t p = { .arena = arena, .a = &a, .st = st, .l = &l, .vol = (uint32_t)v,
                      .d = vols[v].data, .size = vols[v].size };
        rubraview_rar_err_t err = rar5 ? read_rar5(&p, (size_t)vs + 8) : read_rar4(&p, (size_t)vs + 7);
        if (err != RUBRAVIEW_RAR_OK) {
            /* A later volume that will not read ends the set there; the first is the archive. */
            if (v == 0) { state_free(st); r.err = err; return r; }
            break;
        }
        used = v + 1;
        if (p.hdr_checked) st->password_ok = true;   /* its headers opened with it */
        if (!p.more_volumes && !(a.is_volume && v + 1 < volume_count)) break;
    }
    a.volume_count = used;
    a.entries = l.items;
    a.entry_count = l.count;
    a.pieces = l.pieces;
    a.piece_count = l.piece_count;
    a.state = st;
    r.err = RUBRAVIEW_RAR_OK;
    r.value = a;
    return r;
}

rubraview_rar_result_t rubraview_rar_open(proven_arena_t *arena, const uint8_t *data, size_t size) {
    rubraview_rar_volume_t one = { .data = data, .size = size };
    return rubraview_rar_open_volumes(arena, &one, 1, (u8str_t){ .ptr = "", .len = 0 });
}

void rubraview_rar_close(rubraview_rar_archive_t *archive) {
    if (!archive || !archive->state) return;
    state_free((rar_state_t*)archive->state);
    archive->state = NULL;
}

/* ---- decoding ---- */

/* The packed bytes of one entry, piece after piece, decrypted when they are. */
typedef struct source {
    const rubraview_rar_archive_t *a;
    const rubraview_rar_entry_t *e;
    uint32_t piece;         /* the next piece, from the entry's first */
    uint64_t in_piece;      /* how far into it */
    bool     decrypt;
    void    *aes;           /* the codec's */
    uint8_t  buf[16384];
    size_t   buf_pos, buf_len;
} source_t;

static size_t raw_read(source_t *s, uint8_t *out, size_t capacity) {
    size_t got = 0;
    while (got < capacity && s->piece < s->e->piece_count) {
        const rubraview_rar_piece_t *pc = &s->a->pieces[s->e->first_piece + s->piece];
        const rubraview_rar_volume_t *v = &s->a->volumes[pc->volume];
        uint64_t left = pc->size - s->in_piece;
        if (left == 0 || pc->offset + pc->size > v->size) { s->piece++; s->in_piece = 0; continue; }
        size_t n = capacity - got < left ? capacity - got : (size_t)left;
        memcpy(out + got, v->data + pc->offset + s->in_piece, n);
        got += n;
        s->in_piece += n;
    }
    return got;
}

static size_t source_read(void *ctx, uint8_t *buffer, size_t capacity) {
    source_t *s = (source_t*)ctx;
    if (!s->decrypt) return raw_read(s, buffer, capacity);
    size_t got = 0;
    while (got < capacity) {
        if (s->buf_pos == s->buf_len) {
            size_t n = raw_read(s, s->buf, sizeof(s->buf)) & ~(size_t)15;   /* whole cipher blocks */
            if (n == 0) break;
            g_codec->aes_decrypt(s->aes, s->buf, n);
            s->buf_pos = 0;
            s->buf_len = n;
        }
        size_t n = s->buf_len - s->buf_pos < capacity - got ? s->buf_len - s->buf_pos : capacity - got;
        memcpy(buffer + got, s->buf + s->buf_pos, n);
        s->buf_pos += n;
        got += n;
    }
    return got;
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
    if (e->split) return RUBRAVIEW_RAR_ERR_MISSING_VOLUME;
    if (!g_codec && (e->encrypted || e->method != 0)) return RUBRAVIEW_RAR_ERR_NO_CODEC;
    source_t *src = (source_t*)calloc(1, sizeof(source_t));
    if (!src) return RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY;
    src->a = a;
    src->e = e;
    const key_entry_t *key = NULL;
    rubraview_rar_err_t result = RUBRAVIEW_RAR_OK;
    if (e->encrypted) {
        if (e->crypt != RUBRAVIEW_RAR_CRYPT_30 && e->crypt != RUBRAVIEW_RAR_CRYPT_50) { result = RUBRAVIEW_RAR_ERR_UNSUPPORTED; goto done; }
        if (!st->has_password) { result = RUBRAVIEW_RAR_ERR_ENCRYPTED; goto done; }
        key = key_for(st, e->crypt, e->salt, e->salt_set, e->lg2_count);
        if (!key) { result = RUBRAVIEW_RAR_ERR_UNSUPPORTED; goto done; }
        if (e->psw_check_set && memcmp(key->psw_check, e->psw_check, RAR_PSWCHECK) != 0) { result = RUBRAVIEW_RAR_ERR_BAD_PASSWORD; goto done; }
        src->decrypt = true;
        src->aes = e->crypt == RUBRAVIEW_RAR_CRYPT_50 ? g_codec->aes_create(key->key, 256, e->iv)
                                                      : g_codec->aes_create(key->key, 128, key->iv);
        if (!src->aes) { result = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; goto done; }
    }
    sink_t sink = { .out = out, .capacity = e->size, .crc = 0xFFFFFFFFu, .st = st };
    if (e->method == 0) {
        /* stored: the bytes as they are (decrypted), cut at the size */
        uint8_t chunk[16384];
        while (sink.written < e->size) {
            size_t n = source_read(src, chunk, sizeof(chunk));
            if (n == 0) break;
            if (!sink_write(&sink, chunk, n)) { result = RUBRAVIEW_RAR_ERR_CANCELLED; goto done; }
        }
    } else {
        if (e->method == 9999) { result = RUBRAVIEW_RAR_ERR_UNSUPPORTED; goto done; }
        if (!st->unpack && !(st->unpack = g_codec->unpack_create())) { result = RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY; goto done; }
        bool solid = e->solid && st->next == index;
        if (!g_codec->unpack_file(st->unpack, e->method, solid, e->dict_size, e->size, source_read, src, sink_write, &sink)) {
            if (atomic_load(&st->cancel)) { result = RUBRAVIEW_RAR_ERR_CANCELLED; goto done; }
            if (e->dict_size > g_codec->max_dict) { result = RUBRAVIEW_RAR_ERR_TOO_LARGE; goto done; }
            result = e->encrypted ? RUBRAVIEW_RAR_ERR_CORRUPT_STREAM : RUBRAVIEW_RAR_ERR_UNSUPPORTED;
            goto done;
        }
        if (atomic_load(&st->cancel)) { result = RUBRAVIEW_RAR_ERR_CANCELLED; goto done; }
    }
    st->next = index + 1;
    if (sink.written < e->size) { result = RUBRAVIEW_RAR_ERR_CORRUPT_STREAM; goto done; }
    if (e->has_crc) {
        uint32_t crc = sink.crc ^ 0xFFFFFFFFu;
        if (e->hash_mac && key) crc = g_codec->crc_to_mac(crc, key->hash_key);
        if (crc != e->crc32) result = RUBRAVIEW_RAR_ERR_CORRUPT_STREAM;
    }
done:
    if (src->aes) g_codec->aes_destroy(src->aes);
    rar_wipe(src, sizeof(*src));
    free(src);
    return result;
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

bool rubraview_rar_needs_codec(const rubraview_rar_archive_t *archive, size_t index) {
    if (g_codec || !archive || index >= archive->entry_count) return false;
    return archive->entries[index].encrypted || archive->entries[index].method != 0;
}

bool rubraview_rar_needs_password(const rubraview_rar_archive_t *archive) {
    if (!archive || !archive->state) return false;
    const rar_state_t *st = (const rar_state_t*)archive->state;
    if (st->password_ok) return false;
    for (size_t i = 0; i < archive->entry_count; ++i) if (archive->entries[i].encrypted) return true;
    return false;
}

rubraview_rar_err_t rubraview_rar_set_password(rubraview_rar_archive_t *archive, u8str_t password) {
    if (!archive || !archive->state) return RUBRAVIEW_RAR_ERR_BAD_INDEX;
    rar_state_t *st = (rar_state_t*)archive->state;
    password_set(st, password);
    if (password.len == 0) return RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
    if (!g_codec) return RUBRAVIEW_RAR_ERR_NO_CODEC;
    /* The file to test it on: RAR 5 tells by its check value at once;
       otherwise the smallest encrypted file that starts a stream, decoded
       and checked by its CRC. */
    size_t best = SIZE_MAX;
    for (size_t i = 0; i < archive->entry_count; ++i) {
        const rubraview_rar_entry_t *e = &archive->entries[i];
        if (!e->encrypted || e->split) continue;
        if (e->crypt != RUBRAVIEW_RAR_CRYPT_30 && e->crypt != RUBRAVIEW_RAR_CRYPT_50) continue;
        if (e->psw_check_set) { best = i; break; }
        if (e->solid && e->method != 0 && chain_start(archive, NULL, i) != i) continue;
        if (best == SIZE_MAX || e->size < archive->entries[best].size) best = i;
    }
    if (best == SIZE_MAX) return RUBRAVIEW_RAR_ERR_UNSUPPORTED;
    const rubraview_rar_entry_t *e = &archive->entries[best];
    rubraview_rar_err_t err;
    if (e->psw_check_set) {
        const key_entry_t *k = key_for(st, e->crypt, e->salt, e->salt_set, e->lg2_count);
        err = !k ? RUBRAVIEW_RAR_ERR_UNSUPPORTED
            : memcmp(k->psw_check, e->psw_check, RAR_PSWCHECK) == 0 ? RUBRAVIEW_RAR_OK : RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
    } else {
        st->next = SIZE_MAX;
        atomic_store(&st->done, 0);
        atomic_store(&st->total, e->size);
        err = decode(archive, st, best, NULL);
        st->next = SIZE_MAX;                                 /* the next read starts its own stream */
        if (err == RUBRAVIEW_RAR_ERR_CORRUPT_STREAM) err = RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
    }
    st->password_ok = err == RUBRAVIEW_RAR_OK;
    return err;
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

/* ---- volumes ---- */

bool rubraview_rar_volume_info(const uint8_t *data, size_t size, rubraview_rar_volume_info_t *out) {
    rubraview_rar_volume_info_t info = {0};
    bool rar5 = false;
    long sig = data ? find_signature(data, size, &rar5) : -1;
    if (sig < 0) { if (out) *out = info; return false; }
    size_t pos = (size_t)sig + (rar5 ? 8 : 7);
    if (!rar5) {
        if (pos + 13 <= size && data[pos + 2] == 0x73) {
            uint16_t flags = le16(data + pos + 3);
            info.is_volume = (flags & 0x0001) != 0;
            info.new_numbering = (flags & 0x0010) != 0;
            info.first = !info.is_volume || (flags & 0x0100) != 0;
            info.headers_encrypted = (flags & 0x0080) != 0;
        }
    } else {
        size_t at = pos + 4;
        uint64_t header_size = 0, type = 0, hflags = 0, skip = 0, arc_flags = 0;
        if (vint(data, size, &at, &header_size) && header_size <= size - at) {
            size_t end = at + (size_t)header_size;
            if (vint(data, end, &at, &type) && vint(data, end, &at, &hflags)) {
                if (type == 4) {
                    info.headers_encrypted = true;
                    info.new_numbering = true;
                } else if (type == 1) {
                    if ((hflags & 0x0001) && !vint(data, end, &at, &skip)) at = end;
                    if ((hflags & 0x0002) && !vint(data, end, &at, &skip)) at = end;
                    if (vint(data, end, &at, &arc_flags)) {
                        info.is_volume = (arc_flags & 0x0001) != 0;
                        info.first = !(arc_flags & 0x0002);   /* the volume number is there for all but the first */
                        info.new_numbering = true;
                    }
                }
            }
        }
    }
    if (out) *out = info;
    return true;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

static size_t name_start(u8str_t path) {
    for (size_t i = path.len; i > 0; --i) if (path.ptr[i - 1] == '/' || path.ptr[i - 1] == '\\') return i;
    return 0;
}

/* `.partN.rar`: where the digits are, or false. */
static bool part_digits(u8str_t name, size_t *from, size_t *to) {
    if (name.len < 10) return false;
    size_t ext = name.len - 4;
    if (name.ptr[ext] != '.' || lower(name.ptr[ext + 1]) != 'r' || lower(name.ptr[ext + 2]) != 'a' || lower(name.ptr[ext + 3]) != 'r') return false;
    size_t d = ext;
    while (d > 0 && is_digit(name.ptr[d - 1])) d--;
    if (d == ext || d < 5) return false;
    const char *p = name.ptr + d - 5;
    if (p[0] != '.' || lower(p[1]) != 'p' || lower(p[2]) != 'a' || lower(p[3]) != 'r' || lower(p[4]) != 't') return false;
    *from = d;
    *to = ext;
    return true;
}

rubraview_rar_volume_name_kind_t rubraview_rar_volume_name_kind(u8str_t name) {
    name = (u8str_t){ name.ptr + name_start(name), name.len - name_start(name) };
    size_t a = 0, b = 0;
    if (part_digits(name, &a, &b)) return RUBRAVIEW_RAR_NAME_PART;
    if (name.len >= 5 && name.ptr[name.len - 4] == '.' && lower(name.ptr[name.len - 3]) >= 'r' &&
        lower(name.ptr[name.len - 3]) <= 'z' && is_digit(name.ptr[name.len - 2]) && is_digit(name.ptr[name.len - 1]))
        return RUBRAVIEW_RAR_NAME_OLD;
    return RUBRAVIEW_RAR_NAME_PLAIN;
}

unsigned rubraview_rar_volume_number(u8str_t name) {
    name = (u8str_t){ name.ptr + name_start(name), name.len - name_start(name) };
    size_t a = 0, b = 0;
    if (part_digits(name, &a, &b)) {
        unsigned n = 0;
        for (size_t i = a; i < b && n < 100000; ++i) n = n * 10 + (unsigned)(name.ptr[i] - '0');
        return n;
    }
    if (rubraview_rar_volume_name_kind(name) == RUBRAVIEW_RAR_NAME_OLD) {
        const char *e = name.ptr + name.len - 3;
        return (unsigned)(lower(e[0]) - 'r') * 100 + (unsigned)(e[1] - '0') * 10 + (unsigned)(e[2] - '0') + 2;
    }
    if (name.len >= 4 && name.ptr[name.len - 4] == '.') return 1;
    return 0;
}

static u8str_t copy_path(proven_arena_t *arena, u8str_t path, size_t extra, char **out) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, path.len + extra + 1);
    if (!proven_is_ok(res.err)) { *out = NULL; return (u8str_t){ .ptr = "", .len = 0 }; }
    *out = (char*)res.value.ptr;
    memcpy(*out, path.ptr, path.len);
    (*out)[path.len] = '\0';
    return (u8str_t){ .ptr = *out, .len = path.len };
}

u8str_t rubraview_rar_first_volume(proven_arena_t *arena, u8str_t path, bool old) {
    size_t base = name_start(path);
    u8str_t name = { path.ptr + base, path.len - base };
    char *p = NULL;
    size_t a = 0, b = 0;
    if (part_digits(name, &a, &b)) {
        u8str_t out = copy_path(arena, path, 0, &p);
        if (!p) return out;
        for (size_t i = base + a; i < base + b; ++i) p[i] = i + 1 == base + b ? '1' : '0';
        return out;
    }
    if (old || rubraview_rar_volume_name_kind(name) == RUBRAVIEW_RAR_NAME_OLD) {
        u8str_t out = copy_path(arena, path, 0, &p);
        if (!p || name.len < 4 || name.ptr[name.len - 4] != '.') return out;
        bool upper = name.ptr[name.len - 3] >= 'A' && name.ptr[name.len - 3] <= 'Z';
        p[path.len - 3] = upper ? 'R' : 'r';
        p[path.len - 2] = upper ? 'A' : 'a';
        p[path.len - 1] = upper ? 'R' : 'r';
        return out;
    }
    return copy_path(arena, path, 0, &p);
}

u8str_t rubraview_rar_next_volume(proven_arena_t *arena, u8str_t path, bool old) {
    size_t base = name_start(path);
    u8str_t name = { path.ptr + base, path.len - base };
    char *p = NULL;
    size_t a = 0, b = 0;
    if (!old && part_digits(name, &a, &b)) {
        u8str_t out = copy_path(arena, path, 1, &p);
        if (!p) return out;
        size_t i = base + b;
        while (i > base + a) {
            --i;
            if (p[i] != '9') { p[i]++; return out; }
            p[i] = '0';
        }
        /* all nines: one digit more (part9 -> part10) */
        memmove(p + base + a + 1, p + base + a, path.len - base - a + 1);
        p[base + a] = '1';
        out.len++;
        return out;
    }
    if (name.len < 4 || name.ptr[name.len - 4] != '.') return copy_path(arena, path, 0, &p);
    u8str_t out = copy_path(arena, path, 0, &p);
    if (!p) return out;
    char *e = p + path.len - 3;
    if (!is_digit(e[1]) || !is_digit(e[2])) {                /* .rar -> .r00 */
        e[1] = '0';
        e[2] = '0';
        return out;
    }
    for (int i = 2; i >= 0; --i) {                           /* .r99 -> .s00, as UnRAR counts */
        if (i > 0 && e[i] == '9') { e[i] = '0'; continue; }
        e[i]++;
        break;
    }
    return out;
}
